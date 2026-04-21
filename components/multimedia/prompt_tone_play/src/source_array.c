// Copyright 2024-2025 Beken
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.


#include <common/bk_include.h>
#include <os/os.h>
#include <os/mem.h>
#include <os/str.h>
#include "source_array.h"


#define ARRAY_SOURCE_TAG "ary_src"
#define LOGI(...) BK_LOGI(ARRAY_SOURCE_TAG, ##__VA_ARGS__)
#define LOGW(...) BK_LOGW(ARRAY_SOURCE_TAG, ##__VA_ARGS__)
#define LOGE(...) BK_LOGE(ARRAY_SOURCE_TAG, ##__VA_ARGS__)
#define LOGD(...) BK_LOGD(ARRAY_SOURCE_TAG, ##__VA_ARGS__)


#define ARRAY_SOURCE_CHECK_NULL(ptr) do {\
        if (ptr == NULL) {\
            LOGE("ARRAY_SOURCE_CHECK_NULL fail \n");\
            return BK_FAIL;\
        }\
    } while(0)

typedef enum
{
    ARRAY_DATA_READ_IDLE = 0,
    ARRAY_DATA_READ_START,
    ARRAY_DATA_READ_EXIT
} array_data_read_op_t;

typedef struct
{
    array_data_read_op_t op;
    void *param;
} array_data_read_msg_t;

typedef struct array_source_priv_s
{
    uint32_t array_total_len;
    uint32_t array_read_offset;

    uint32_t read_buff_size;                        /**< read pool size, unit byte */
    uint8_t *read_buff;                             /**< the read pool buffer save speaker data need to play */

    beken_thread_t array_data_read_task_hdl;
    beken_queue_t array_data_read_msg_que;
    beken_semaphore_t sem;
    bool running;

    audio_source_cfg_t config;
} array_source_priv_t;


static bk_err_t array_data_read_send_msg(beken_queue_t queue, array_data_read_op_t op, void *param)
{
    bk_err_t ret;
    array_data_read_msg_t msg;

    if (!queue)
    {
        LOGE("%s, %d, queue: %p \n", __func__, __LINE__, queue);
        return BK_FAIL;
    }

    msg.op = op;
    msg.param = param;
    ret = rtos_push_to_queue(&queue, &msg, BEKEN_NO_WAIT);
    if (ret != BK_OK)
    {
        LOGE("%s, %d, send message: %d fail, ret: %d\n", __func__, __LINE__, op, ret);
        return BK_FAIL;
    }

    return BK_OK;
}

static void array_data_read_task_main(beken_thread_arg_t param_data)
{
    bk_err_t ret = BK_OK;

    array_source_priv_t *array_source_priv = (array_source_priv_t *)param_data;

    array_source_priv->running = false;
    uint32_t wait_time = BEKEN_WAIT_FOREVER;

    rtos_set_semaphore(&array_source_priv->sem);

    while (1)
    {
        array_data_read_msg_t msg;
        ret = rtos_pop_from_queue(&array_source_priv->array_data_read_msg_que, &msg, wait_time);
        if (kNoErr == ret)
        {
            switch (msg.op)
            {
                case ARRAY_DATA_READ_IDLE:
                    LOGD("%s, %d, ARRAY_DATA_READ_IDLE\n", __func__, __LINE__);
                    array_source_priv->running = false;
                    wait_time = BEKEN_WAIT_FOREVER;
                    break;

                case ARRAY_DATA_READ_EXIT:
                    LOGD("%s, %d, ARRAY_DATA_READ_EXIT\n", __func__, __LINE__);
                    goto array_data_read_exit;
                    break;

                case ARRAY_DATA_READ_START:
                    LOGD("%s, %d, ARRAY_DATA_READ_START\n", __func__, __LINE__);
                    array_source_priv->running = true;
                    wait_time = 0;
                    break;

                default:
                    break;
            }
        }

        /* read speaker data and write to dac fifo */
        if (array_source_priv->running)
        {
            LOGD("%s, %d, array_read_offset: %d, array_total_len: %d\n", __func__, __LINE__, array_source_priv->array_read_offset, array_source_priv->array_total_len);
            /* read speaker data from ringbuffer, and write to dac fifo */
            if (array_source_priv->array_read_offset < array_source_priv->array_total_len)
            {
                uint32_t r_len = 0;
                if ((array_source_priv->array_total_len - array_source_priv->array_read_offset) >= array_source_priv->config.frame_size)
                {
                    r_len = array_source_priv->config.frame_size;
                }
                else
                {
                    r_len = array_source_priv->array_total_len - array_source_priv->array_read_offset;
                }
                os_memcpy(array_source_priv->read_buff, &array_source_priv->config.url[array_source_priv->array_read_offset], r_len);
                LOGD("%s, %d, data_handle: %p\n", __func__, __LINE__, array_source_priv->config.data_handle);
                if (array_source_priv->config.data_handle)
                {
                    array_source_priv->config.data_handle((char *)array_source_priv->read_buff, r_len, array_source_priv->config.usr_data);
                }
                array_source_priv->array_read_offset += r_len;
            }
            else
            {
                /* notify app array data is empty. */
                if (array_source_priv->config.notify)
                {
                    array_source_priv->config.notify(array_source_priv, AUDIO_SOURCE_EVENT_EMPTY);
                }

                /* set array read task to idle state */
                array_data_read_send_msg(array_source_priv->array_data_read_msg_que, ARRAY_DATA_READ_IDLE, NULL);
            }
        }
    }

array_data_read_exit:

    array_source_priv->running = false;

    /* delete msg queue */
    ret = rtos_deinit_queue(&array_source_priv->array_data_read_msg_que);
    if (ret != kNoErr)
    {
        LOGE("%s, %d, delete message queue fail\n", __func__, __LINE__);
    }
    array_source_priv->array_data_read_msg_que = NULL;

    /* delete task */
    array_source_priv->array_data_read_task_hdl = NULL;

    rtos_set_semaphore(&array_source_priv->sem);

    rtos_delete_thread(NULL);
}

static bk_err_t array_data_read_task_init(array_source_priv_t *array_source_priv)
{
    bk_err_t ret = BK_OK;

    ARRAY_SOURCE_CHECK_NULL(array_source_priv);

    array_source_priv->read_buff = psram_malloc(array_source_priv->read_buff_size);
    ARRAY_SOURCE_CHECK_NULL(array_source_priv->read_buff);

    os_memset(array_source_priv->read_buff, 0, array_source_priv->read_buff_size);

    ret = rtos_init_semaphore(&array_source_priv->sem, 1);
    if (ret != BK_OK)
    {
        LOGE("%s, %d, ceate semaphore fail\n", __func__, __LINE__);
        goto fail;
    }

    ret = rtos_init_queue(&array_source_priv->array_data_read_msg_que,
                          "ary_data_rd_que",
                          sizeof(array_data_read_msg_t),
                          5);
    if (ret != BK_OK)
    {
        LOGE("%s, %d, create array data read message queue fail\n", __func__, __LINE__);
        goto fail;
    }

    ret = rtos_create_thread(&array_source_priv->array_data_read_task_hdl,
                             4,
                             "ary_data_rd",
                             (beken_thread_function_t)array_data_read_task_main,
                             1024,
                             (beken_thread_arg_t)array_source_priv);
    if (ret != BK_OK)
    {
        LOGE("%s, %d, create array data read task fail\n", __func__, __LINE__);
        goto fail;
    }

    rtos_get_semaphore(&array_source_priv->sem, BEKEN_NEVER_TIMEOUT);

    array_data_read_send_msg(array_source_priv->array_data_read_msg_que, ARRAY_DATA_READ_START, NULL);

    LOGI("init array data read task complete\n");

    return BK_OK;

fail:

    if (array_source_priv->sem)
    {
        rtos_deinit_semaphore(&array_source_priv->sem);
        array_source_priv->sem = NULL;
    }

    if (array_source_priv->array_data_read_msg_que)
    {
        rtos_deinit_queue(&array_source_priv->array_data_read_msg_que);
        array_source_priv->array_data_read_msg_que = NULL;
    }

    if (array_source_priv->read_buff)
    {
        psram_free(array_source_priv->read_buff);
        array_source_priv->read_buff = NULL;
    }

    return BK_FAIL;
}

bk_err_t array_data_read_task_deinit(array_source_priv_t *array_source_priv)
{
    bk_err_t ret = BK_OK;

    ARRAY_SOURCE_CHECK_NULL(array_source_priv);

    LOGI("%s\n", __func__);

    ret = array_data_read_send_msg(array_source_priv->array_data_read_msg_que, ARRAY_DATA_READ_EXIT, NULL);
    if (ret != BK_OK)
    {
        LOGE("%s, %d, send message: ARRAY_DATA_READ_EXIT fail\n", __func__, __LINE__);
        return BK_FAIL;
    }

    rtos_get_semaphore(&array_source_priv->sem, BEKEN_NEVER_TIMEOUT);

    rtos_deinit_semaphore(&array_source_priv->sem);
    array_source_priv->sem = NULL;

    if (array_source_priv->read_buff)
    {
        psram_free(array_source_priv->read_buff);
        array_source_priv->read_buff = NULL;
    }

    LOGD("deinit array data read complete\n");

    return BK_OK;
}

static int array_source_open(audio_source_t *source, audio_source_cfg_t *config)
{
    array_source_priv_t *temp_array_source = NULL;
    bk_err_t ret = BK_OK;

    if (!source || !config)
    {
        LOGE("%s, %d, params error, source: %p, config: %p\n", __func__, __LINE__, source, config);
        return BK_FAIL;
    }

    LOGI("%s\n", __func__);

    array_source_priv_t *array_source = (array_source_priv_t *)source->source_ctx;
    if (array_source != NULL)
    {
        LOGE("%s, %d, array_source: %p already open\n", __func__, __LINE__, array_source);
        goto fail;
    }

    temp_array_source = psram_malloc(sizeof(array_source_priv_t));
    if (!temp_array_source)
    {
        LOGE("%s, %d, os_malloc temp_array_source: %d fail\n", __func__, __LINE__, sizeof(array_source_priv_t));
        goto fail;
    }

    os_memset(temp_array_source, 0, sizeof(array_source_priv_t));

    source->source_ctx = temp_array_source;

    temp_array_source->read_buff_size  = config->frame_size;
    temp_array_source->array_total_len = config->total_size;
    os_memcpy(&temp_array_source->config, config, sizeof(audio_source_cfg_t));

    ret = array_data_read_task_init(source->source_ctx);
    if (ret != BK_OK)
    {
        LOGE("%s, %d, array_data_read_task_init fail\n", __func__, __LINE__);
        goto fail;
    }

    LOGD("array source open complete\n");

    return BK_OK;

fail:

    if (source->source_ctx)
    {
        psram_free(source->source_ctx);
        source->source_ctx = NULL;
    }

    return BK_FAIL;
}

static int array_source_close(audio_source_t *source)
{
    ARRAY_SOURCE_CHECK_NULL(source);
    array_source_priv_t *array_source = (array_source_priv_t *)source->source_ctx;

    if (!array_source)
    {
        LOGD("%s, %d, array source already close\n", __func__, __LINE__);
        return BK_OK;
    }

    LOGI("%s \n", __func__);

    array_data_read_task_deinit(array_source);

    if (array_source)
    {
        psram_free(array_source);
        source->source_ctx = NULL;
    }

    LOGD("array source close complete\n");
    return BK_OK;
}

static int array_source_seek(audio_source_t *source, int offset, uint32_t whence)
{
    ARRAY_SOURCE_CHECK_NULL(source);
    array_source_priv_t *array_source = (array_source_priv_t *)source->source_ctx;
    ARRAY_SOURCE_CHECK_NULL(array_source);

    LOGI("%s \n", __func__);

    if (whence == 0)        //SEEK_SET
    {
        if (offset < 0)
        {
            LOGE("%s, %d, offset: %d is invalid when whence is SEEK_SET\n", __func__, __LINE__, offset);
            return BK_FAIL;
        }

        if (offset > array_source->array_total_len)
        {
            LOGE("%s, %d, offset: %d out of range (0 ~ %d) when whence is SEEK_SET\n", __func__, __LINE__, offset, array_source->array_total_len);
            return BK_FAIL;
        }

        array_source->array_read_offset = offset;
    }
    else if (whence == 1)   //SEEK_CUR
    {
        if ((array_source->array_read_offset + offset) > array_source->array_total_len)
        {
            LOGE("%s, %d, offset: %d out of range (0 ~ -%d) when whence is SEEK_CUR\n", __func__, __LINE__, offset, array_source->array_total_len - array_source->array_read_offset);
            return BK_FAIL;
        }
        array_source->array_read_offset = array_source->array_total_len + offset;
    }
    else if (whence == 2)   //SEEK_END
    {
        if (offset > 0)
        {
            LOGE("%s, %d, offset: %d is invalid when whence is SEEK_END\n", __func__, __LINE__, offset);
            return BK_FAIL;
        }

        if ((offset + array_source->array_total_len) < 0)
        {
            LOGE("%s, %d, offset: %d out of range (0 ~ -%d) when whence is SEEK_END\n", __func__, __LINE__, offset, array_source->array_total_len);
            return BK_FAIL;
        }

        array_source->array_read_offset = array_source->array_total_len + offset;
    }
    else
    {
        LOGE("%s, %d, whence: %d is invalid\n", __func__, __LINE__, whence);
        return BK_FAIL;
    }

    return BK_OK;
}

static int array_source_set_url(audio_source_t *source, url_info_t *url_info)
{
    ARRAY_SOURCE_CHECK_NULL(url_info);
    ARRAY_SOURCE_CHECK_NULL(url_info->url);
    ARRAY_SOURCE_CHECK_NULL(source);
    array_source_priv_t *array_source = (array_source_priv_t *)source->source_ctx;
    ARRAY_SOURCE_CHECK_NULL(array_source);

    LOGD("%s, url: %s \n", __func__, url_info->url);

    /* update new url */
    array_source->config.url        = url_info->url;
    array_source->config.total_size = url_info->total_len;

	array_source->array_total_len   = url_info->total_len;
	array_source->array_read_offset = 0;

    return BK_OK;
}

static int array_source_ctrl(audio_source_t *source, audio_source_ctrl_op_t op, void *params)
{
    ARRAY_SOURCE_CHECK_NULL(source);
    array_source_priv_t *array_source = (array_source_priv_t *)source->source_ctx;
    ARRAY_SOURCE_CHECK_NULL(array_source);

    bk_err_t ret = BK_FAIL;

    LOGD("%s, op: %d \n", __func__, op);

    switch (op)
    {
        case AUDIO_SOURCE_CTRL_START:
            ret = array_data_read_send_msg(array_source->array_data_read_msg_que, ARRAY_DATA_READ_IDLE, NULL);
            if (ret != BK_OK)
            {
                LOGE("%s, %d, send msg to stop read old array data fail\n", __func__, __LINE__);
                break;
            }
            ret = array_data_read_send_msg(array_source->array_data_read_msg_que, ARRAY_DATA_READ_START, NULL);
            if (ret != BK_OK)
            {
                LOGE("%s, %d, send msg to start read new array data fail\n", __func__, __LINE__);
            }
            break;

        case AUDIO_SOURCE_CTRL_STOP:
            ret = array_data_read_send_msg(array_source->array_data_read_msg_que, ARRAY_DATA_READ_IDLE, NULL);
            if (ret != BK_OK)
            {
                LOGE("%s, %d, send msg to stop read old array data fail\n", __func__, __LINE__);
                break;
            }
            break;

        default:
            ret = BK_FAIL;
            break;
    }

    return ret;
}


audio_source_ops_t array_source_ops =
{
    .audio_source_open = array_source_open,
    .audio_source_seek = array_source_seek,
    .audio_source_close = array_source_close,
    .audio_source_set_url = array_source_set_url,
    .audio_source_ctrl = array_source_ctrl,
};

audio_source_ops_t *get_array_source_ops(void)
{
    return &array_source_ops;
}

