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
#include <modules/mp3dec.h>
#include "ring_buffer.h"
#include "mp3_codec.h"
#include <modules/pm.h>


#define MP3_CODEC_TAG "mp3_dec"
#define LOGI(...) BK_LOGI(MP3_CODEC_TAG, ##__VA_ARGS__)
#define LOGW(...) BK_LOGW(MP3_CODEC_TAG, ##__VA_ARGS__)
#define LOGE(...) BK_LOGE(MP3_CODEC_TAG, ##__VA_ARGS__)
#define LOGD(...) BK_LOGD(MP3_CODEC_TAG, ##__VA_ARGS__)


#define MP3_CODEC_CHECK_NULL(ptr) do {\
        if (ptr == NULL) {\
            LOGE("MP3_CODEC_CHECK_NULL fail \n");\
            return BK_FAIL;\
        }\
    } while(0)


#define MP3_AUDIO_BUF_SZ    (4 * 1024) /* feel free to change this, but keep big enough for >= one frame at high bitrates */


typedef enum
{
    MP3_CODEC_IDLE = 0,
    MP3_CODEC_START,
    MP3_CODEC_EXIT
} mp3_codec_op_t;

typedef struct
{
    mp3_codec_op_t op;
    void *param;
} mp3_codec_msg_t;

typedef struct
{
    /* mp3 information */
    HMP3Decoder decoder;
    MP3FrameInfo frame_info;
    uint32_t frames;
    uint32_t stream_offset;

    /* mp3 read session */
    uint8_t *read_buffer;
    uint8_t *read_ptr;
    uint32_t bytes_left;

    /* mp3 decode output */
    char *out_buffer;
    uint32_t out_buffer_size;

    /* mp3 pool */
    ringbuf_handle_t rb;                    /**< read pool ringbuffer handle */
    uint32_t rb_size;                       /**< read pool size, unit byte */
    //uint8_t *read_buff;                     /**< the read pool buffer save speaker data need to play */

    beken_thread_t mp3_codec_task_hdl;
    beken_queue_t mp3_codec_msg_que;
    beken_semaphore_t sem;
    bool running;

    bool skip_idtag;

    audio_codec_sta_t state;                 /**< play state */
    audio_codec_cfg_t config;
    audio_frame_info_t cb_frame_info;
} mp3_codec_priv_t;

static mp3_codec_priv_t *gl_mp3_codec = NULL;


static void mp3_codec_state_set(mp3_codec_priv_t *mp3_codec, audio_codec_sta_t state)
{
    mp3_codec->state = state;
}

static audio_codec_sta_t mp3_codec_state_get(mp3_codec_priv_t *mp3_codec)
{
    return mp3_codec->state;
}

static bk_err_t mp3_data_read_send_msg(beken_queue_t queue, mp3_codec_op_t op, void *param)
{
    bk_err_t ret;
    mp3_codec_msg_t msg;

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

/* read mp3 data from pool */
static int read_mp3_data(mp3_codec_priv_t *mp3_codec, char *buffer, uint32_t len, uint32_t time_to_wait)
{
    return rb_read(mp3_codec->rb, buffer, len, time_to_wait / portTICK_RATE_MS);
}

/* skip id3 tag */
static int codec_mp3_skip_idtag(mp3_codec_priv_t *mp3_codec)
{
    int offset = 0;
    uint8_t *tag;
    int ret = 0;

    LOGD("%s\n", __func__);

    /* set the read_ptr to the read buffer */
    mp3_codec->read_ptr = mp3_codec->read_buffer;

    tag = mp3_codec->read_ptr;
    /* read idtag v2 */
    ret = read_mp3_data(mp3_codec, (char *)mp3_codec->read_ptr, 3, 500);
    if (ret != 3)
    {
        LOGE("%s, %d, read ID3 fail, ret: %d\n", __func__, __LINE__, ret);
        goto __exit;
    }

    mp3_codec->bytes_left = 3;
    if (tag[0] == 'I' &&
        tag[1] == 'D' &&
        tag[2] == '3')
    {
        int  size;

        if (read_mp3_data(mp3_codec, (char *)mp3_codec->read_ptr + 3, 7, 5) != 7)
        {
            LOGE("%s, %d, read ID3 TAG fail\n", __func__, __LINE__);
            goto __exit;
        }

        size = ((tag[6] & 0x7F) << 21) | ((tag[7] & 0x7F) << 14) | ((tag[8] & 0x7F) << 7) | ((tag[9] & 0x7F));

        offset = size + 10;

        /* read all of idv3 */
        {
            int rest_size = size;
            while (1)
            {
                int length;
                int chunk;

                if (rest_size > MP3_AUDIO_BUF_SZ)
                {
                    chunk = MP3_AUDIO_BUF_SZ;
                }
                else
                {
                    chunk = rest_size;
                }

                length = read_mp3_data(mp3_codec, (char *)mp3_codec->read_buffer, chunk, 5);
                if (length > 0)
                {
                    rest_size -= length;
                }
                else
                {
                    break; /* read failed */
                }
            }

            mp3_codec->bytes_left = 0;
        }

        return offset;
    }

__exit:

    return -1;
}

static int check_mp3_sync_word(mp3_codec_priv_t *mp3_codec)
{
    int err;

    os_memset(&mp3_codec->frame_info, 0, sizeof(MP3FrameInfo));

    err = MP3GetNextFrameInfo(mp3_codec->decoder, &mp3_codec->frame_info, mp3_codec->read_ptr);
    if (err == ERR_MP3_INVALID_FRAMEHEADER)
    {
        LOGE("%s, ERR_MP3_INVALID_FRAMEHEADER, %d\n", __func__, __LINE__);
        goto __err;
    }
    else if (err != ERR_MP3_NONE)
    {
        LOGE("%s, MP3GetNextFrameInfo fail, err=%d, %d\n", __func__, err, __LINE__);
        goto __err;
    }
    else if (mp3_codec->frame_info.nChans != 1 && mp3_codec->frame_info.nChans != 2)
    {
        LOGE("%s, nChans is not 1 or 2, nChans=%d, %d\n", __func__, mp3_codec->frame_info.nChans, __LINE__);
        goto __err;
    }
    else if (mp3_codec->frame_info.bitsPerSample != 16 && mp3_codec->frame_info.bitsPerSample != 8)
    {
        LOGE("%s, bitsPerSample is not 16 or 8, bitsPerSample=%d, %d\n", __func__, mp3_codec->frame_info.bitsPerSample, __LINE__);
        goto __err;
    }
    else
    {
        //noting todo
    }

    return 0;

__err:
    return -1;
}

static int32_t codec_mp3_fill_buffer(mp3_codec_priv_t *mp3_codec)
{
    int bytes_read;
    size_t bytes_to_read;

    if (mp3_codec->bytes_left > 0)
    {
        os_memmove(mp3_codec->read_buffer, mp3_codec->read_ptr, mp3_codec->bytes_left);
    }
    mp3_codec->read_ptr = mp3_codec->read_buffer;

    bytes_to_read = (MP3_AUDIO_BUF_SZ - mp3_codec->bytes_left) & ~(512 - 1);

__retry:
    bytes_read = read_mp3_data(mp3_codec, (char *)(mp3_codec->read_buffer + mp3_codec->bytes_left), bytes_to_read, 5);
    if (bytes_read > 0)
    {
        mp3_codec->bytes_left = mp3_codec->bytes_left + bytes_read;
        return 0;
    }
    else
    {
        if (bytes_read == RB_TIMEOUT)
        {
            goto exit;
        }

        if (bytes_read == 0)
        {
            if (mp3_codec->bytes_left >= MAINBUF_SIZE)
            {
                return 0;
            }
            else
            {
                goto exit;
            }
        }

        if (bytes_read == 0)
        {
            if (mp3_codec->bytes_left >= MAINBUF_SIZE)
            {
                return 0;
            }
            else
            {
                goto __retry;
            }
        }
        else
        {
            LOGE("read more data error. left=%d\n", mp3_codec->bytes_left);
            //TODO
        }
    }

exit:
    LOGD("can't read more data, end of stream. left=%d\n", mp3_codec->bytes_left);
    return -1;
}

static int mp3_codec_process(mp3_codec_priv_t *mp3_codec, char *buffer, uint32_t len)
{
    int err;
    int read_offset;
    LOGD("%s\n", __func__);

    if ((mp3_codec->read_ptr == NULL) || mp3_codec->bytes_left < 2 * MAINBUF_SIZE)
    {
        codec_mp3_fill_buffer(mp3_codec);
    }

    if (mp3_codec->bytes_left == 0)
    {
        LOGD("%s, %d, bytes_left = 0\n", __func__, __LINE__);
        return 0;
    }

    read_offset = MP3FindSyncWord(mp3_codec->read_ptr, mp3_codec->bytes_left);
    if (read_offset < 0)
    {
        /* discard this data */
        LOGE("%s, %d, MP3FindSyncWord fail, outof sync, byte left: %d\n", __func__, __LINE__, mp3_codec->bytes_left);

        mp3_codec->bytes_left = 0;
        return 0;
    }

    if (read_offset > mp3_codec->bytes_left)
    {
        LOGE("%s, %d, find sync exception, read_offset:%d > bytes_left:%d, %d\n", __func__, __LINE__, read_offset, mp3_codec->bytes_left);
        mp3_codec->read_ptr += mp3_codec->bytes_left;
        mp3_codec->bytes_left = 0;
    }
    else
    {
        mp3_codec->read_ptr += read_offset;
        mp3_codec->bytes_left -= read_offset;
    }

    if (check_mp3_sync_word(mp3_codec) == -1)
    {
        if (mp3_codec->bytes_left > 0)
        {
            mp3_codec->bytes_left --;
            mp3_codec->read_ptr ++;
        }

        LOGW("check_mp3_sync_word fail\n");

        return 0;
    }

    err = MP3Decode(mp3_codec->decoder, &mp3_codec->read_ptr, (int *)&mp3_codec->bytes_left, (short *)buffer, 0);
    mp3_codec->frames++;
    if (err != ERR_MP3_NONE)
    {
        switch (err)
        {
            case ERR_MP3_INDATA_UNDERFLOW:
                // LOGE("ERR_MP3_INDATA_UNDERFLOW.");
                // codec_mp3->bytes_left = 0;
                if (codec_mp3_fill_buffer(mp3_codec) != 0)
                {
                    /* release this memory block */
                    return -1;
                }
                break;

            case ERR_MP3_MAINDATA_UNDERFLOW:
                /* do nothing - next call to decode will provide more mainData */
                // LOGE("ERR_MP3_MAINDATA_UNDERFLOW.");
                break;

            default:
                // LOGE("unknown error: %d, left: %d.", err, codec_mp3->bytes_left);
                // LOGD("stream position: %d.", codec_mp3->parent.stream->position);
                // stream_buffer(0, NULL);

                // skip this frame
                if (mp3_codec->bytes_left > 0)
                {
                    mp3_codec->bytes_left --;
                    mp3_codec->read_ptr ++;
                }
                else
                {
                    // TODO
                    BK_ASSERT(0);
                }
                break;
        }
    }
    else
    {
        int outputSamps;
        /* no error */
        MP3GetLastFrameInfo(mp3_codec->decoder, &mp3_codec->frame_info);

        mp3_codec->cb_frame_info.channel_number = mp3_codec->frame_info.nChans;
        mp3_codec->cb_frame_info.sample_rate = mp3_codec->frame_info.samprate;
        mp3_codec->cb_frame_info.sample_bits = mp3_codec->frame_info.bitsPerSample;

        /* write to sound device */
        outputSamps = mp3_codec->frame_info.outputSamps;
        if (outputSamps > 0)
        {
            /* check output pcm data length, for debug */
            uint32_t out_pcm_len = outputSamps * sizeof(uint16_t);
            if (out_pcm_len > 4608)
            {
                BK_ASSERT(0);
            }
            return outputSamps * sizeof(uint16_t);
        }
    }

    return 0;
}

static void mp3_codec_task_main(beken_thread_arg_t param_data)
{
    bk_err_t ret = BK_OK;
    int pcm_size = 0;
    int out_size = 0;

    mp3_codec_priv_t *mp3_codec = (mp3_codec_priv_t *)param_data;

    if (!mp3_codec)
    {
        LOGE("%s, %d, mp3_codec is NULL\n", __func__, __LINE__);
        goto mp3_codec_exit;
    }

    mp3_codec->decoder = MP3InitDecoder();
    if (!mp3_codec->decoder)
    {
        LOGE("%s, %d, MP3InitDecoder create fail\n", __func__, __LINE__);
        goto mp3_codec_exit;
    }

    LOGI("%s, %d, mp3_codec: %p\n", __func__, __LINE__, mp3_codec);

    mp3_codec->running = false;
    uint32_t wait_time = BEKEN_WAIT_FOREVER;

    rtos_set_semaphore(&mp3_codec->sem);

    while (1)
    {
        mp3_codec_msg_t msg;
        ret = rtos_pop_from_queue(&mp3_codec->mp3_codec_msg_que, &msg, wait_time);
        if (kNoErr == ret)
        {
            switch (msg.op)
            {
                case MP3_CODEC_IDLE:
                    mp3_codec->running = false;
                    wait_time = BEKEN_WAIT_FOREVER;
                    mp3_codec->skip_idtag = false;
                    /* clear ring buffer */
                    mp3_codec->read_ptr = mp3_codec->read_buffer;
                    mp3_codec->bytes_left = 0;
                    break;

                case MP3_CODEC_EXIT:
                    goto mp3_codec_exit;
                    break;

                case MP3_CODEC_START:
                    mp3_codec->running = true;
                    wait_time = 0;
                    break;

                default:
                    break;
            }
        }


        if (mp3_codec->running)
        {
            if (mp3_codec->skip_idtag == false)
            {
                ret = codec_mp3_skip_idtag(mp3_codec);
                if (ret > 0)
                {
                    mp3_codec->skip_idtag = true;
                    LOGI("%s, %d, codec_mp3_skip_idtag complete\n", __func__, __LINE__);
                }
            }

            /* read mp3 data and write to dac fifo */
            pcm_size = mp3_codec_process(mp3_codec, mp3_codec->out_buffer, mp3_codec->out_buffer_size);
            if (pcm_size > 0 && mp3_codec->config.data_handle)
            {
                out_size = mp3_codec->config.data_handle(&mp3_codec->cb_frame_info, mp3_codec->out_buffer, pcm_size, mp3_codec->config.usr_data);
                if (out_size != pcm_size)
                {
                    LOGE("%s, %d, data_handle size: %d != %d\n", __func__, __LINE__, out_size, pcm_size);
                }
            }
       }
    }

mp3_codec_exit:

    mp3_codec->running = false;

    if (mp3_codec->decoder)
    {
        MP3FreeDecoder(mp3_codec->decoder);
        mp3_codec->decoder = NULL;
    }

    /* delete msg queue */
    ret = rtos_deinit_queue(&mp3_codec->mp3_codec_msg_que);
    if (ret != kNoErr)
    {
        LOGE("%s, %d, delete message queue fail\n", __func__, __LINE__);
    }
    mp3_codec->mp3_codec_msg_que = NULL;

    /* delete task */
    mp3_codec->mp3_codec_task_hdl = NULL;

    rtos_set_semaphore(&mp3_codec->sem);

    rtos_delete_thread(NULL);
}

static bk_err_t mp3_codec_task_init(mp3_codec_priv_t *mp3_codec)
{
    bk_err_t ret = BK_OK;

    MP3_CODEC_CHECK_NULL(mp3_codec);

    /* init output buffer */
    mp3_codec->out_buffer = psram_malloc(mp3_codec->out_buffer_size);
    if (!mp3_codec->out_buffer)
    {
        LOGE("%s, %d, malloc output buffer: %d fail\n", __func__, __LINE__, mp3_codec->out_buffer_size);
        goto fail;
    }
    os_memset(mp3_codec->out_buffer, 0, sizeof(mp3_codec->out_buffer_size));

    /* init mp3 read buffer */
    mp3_codec->read_buffer = psram_malloc(MP3_AUDIO_BUF_SZ);
    if (!mp3_codec->read_buffer)
    {
        LOGE("%s, %d, malloc read buffer: %d fail\n", __func__, __LINE__, mp3_codec->read_buffer);
        goto fail;
    }
    os_memset(mp3_codec->read_buffer, 0, MP3_AUDIO_BUF_SZ);

    ret = rtos_init_semaphore(&mp3_codec->sem, 1);
    if (ret != BK_OK)
    {
        LOGE("%s, %d, ceate semaphore fail\n", __func__, __LINE__);
        goto fail;
    }

    ret = rtos_init_queue(&mp3_codec->mp3_codec_msg_que,
                          "mp3_codec_que",
                          sizeof(mp3_codec_msg_t),
                          5);
    if (ret != BK_OK)
    {
        LOGE("%s, %d, ceate mp3 data read message queue fail\n", __func__, __LINE__);
        goto fail;
    }

    LOGI("%s, %d, mp3_codec: %p\n", __func__, __LINE__, mp3_codec);

    ret = rtos_create_thread(&mp3_codec->mp3_codec_task_hdl,
                             (BEKEN_DEFAULT_WORKER_PRIORITY - 1),
                             "mp3_codec",
                             (beken_thread_function_t)mp3_codec_task_main,
                             2048,
                             (beken_thread_arg_t)mp3_codec);
    if (ret != BK_OK)
    {
        LOGE("%s, %d, create mp3 codec task fail\n", __func__, __LINE__);
        goto fail;
    }

    rtos_get_semaphore(&mp3_codec->sem, BEKEN_NEVER_TIMEOUT);

    LOGI("init mp3 codec task complete\n");

    return BK_OK;

fail:

    if (mp3_codec->sem)
    {
        rtos_deinit_semaphore(&mp3_codec->sem);
        mp3_codec->sem = NULL;
    }

    if (mp3_codec->mp3_codec_msg_que)
    {
        rtos_deinit_queue(&mp3_codec->mp3_codec_msg_que);
        mp3_codec->mp3_codec_msg_que = NULL;
    }

    if (mp3_codec->out_buffer)
    {
        psram_free(mp3_codec->out_buffer);
        mp3_codec->out_buffer = NULL;
    }

    if (mp3_codec->read_buffer)
    {
        psram_free(mp3_codec->read_buffer);
        mp3_codec->read_buffer = NULL;
    }

    return BK_FAIL;
}

static bk_err_t mp3_codec_task_deinit(mp3_codec_priv_t *mp3_codec)
{
    bk_err_t ret = BK_OK;

    MP3_CODEC_CHECK_NULL(mp3_codec);

    LOGI("%s\n", __func__);

    ret = mp3_data_read_send_msg(mp3_codec->mp3_codec_msg_que, MP3_CODEC_EXIT, NULL);
    if (ret != BK_OK)
    {
        LOGE("%s, %d, send message: MP3_CODEC_EXIT fail\n", __func__, __LINE__);
        return BK_FAIL;
    }

    rtos_get_semaphore(&mp3_codec->sem, BEKEN_NEVER_TIMEOUT);

    rtos_deinit_semaphore(&mp3_codec->sem);
    mp3_codec->sem = NULL;

    if (mp3_codec->read_buffer)
    {
        psram_free(mp3_codec->read_buffer);
        mp3_codec->read_buffer = NULL;
    }

    if (mp3_codec->out_buffer)
    {
        psram_free(mp3_codec->out_buffer);
        mp3_codec->out_buffer = NULL;
    }

    LOGD("deinit mp3 data read complete\n");

    return BK_OK;
}

static int mp3_codec_open(audio_codec_t *codec, audio_codec_cfg_t *config)
{
    mp3_codec_priv_t *temp_mp3_codec = NULL;
    bk_err_t ret = BK_OK;

    if (!codec || !config)
    {
        LOGE("%s, %d, params error, codec: %p, config: %p\n", __func__, __LINE__, codec, config);
        return BK_FAIL;
    }

    mp3_codec_priv_t *mp3_codec = (mp3_codec_priv_t *)codec->codec_ctx;
    if (mp3_codec != NULL)
    {
        LOGE("%s, %d, mp3_codec: %p already open\n", __func__, __LINE__, mp3_codec);
        goto fail;
    }

    temp_mp3_codec = psram_malloc(sizeof(mp3_codec_priv_t));
    if (!temp_mp3_codec)
    {
        LOGE("%s, %d, os_malloc temp_mp3_codec: %d fail\n", __func__, __LINE__, sizeof(mp3_codec_priv_t));
        goto fail;
    }

    os_memset(temp_mp3_codec, 0, sizeof(mp3_codec_priv_t));

    codec->codec_ctx = temp_mp3_codec;
    gl_mp3_codec = temp_mp3_codec;

    os_memcpy(&temp_mp3_codec->config, config, sizeof(audio_codec_cfg_t));
    temp_mp3_codec->rb_size = config->pool_size;
    temp_mp3_codec->out_buffer_size = config->chunk_size;

    /* init ringbuffer */
    temp_mp3_codec->rb = rb_create(temp_mp3_codec->rb_size);
    if (!temp_mp3_codec->rb)
    {
        LOGE("%s, %d, create pool ringbuffer fail\n", __func__, __LINE__);
        goto fail;
    }

    LOGI("%s, %d, codec->codec_ctx: %p\n", __func__, __LINE__, codec->codec_ctx);

    /* init mp3 codec task */
    ret = mp3_codec_task_init(codec->codec_ctx);
    if (ret != BK_OK)
    {
        LOGE("%s, %d, create mp3 codec task fail\n", __func__, __LINE__);
        goto fail;
    }

    bk_pm_module_vote_cpu_freq(PM_DEV_ID_AUDIO, PM_CPU_FRQ_480M);

    mp3_codec_state_set(temp_mp3_codec, AUDIO_CODEC_STA_RUNNING);

    LOGI("mp3 codec open complete\n");

    return BK_OK;

fail:

    if (temp_mp3_codec->rb)
    {
        rb_destroy(temp_mp3_codec->rb);
        temp_mp3_codec->rb = NULL;
    }

    if (codec->codec_ctx)
    {
        psram_free(codec->codec_ctx);
        codec->codec_ctx = NULL;
    }

    return BK_FAIL;
}

static int mp3_codec_close(audio_codec_t *codec)
{
    MP3_CODEC_CHECK_NULL(codec);
    mp3_codec_priv_t *mp3_codec = (mp3_codec_priv_t *)codec->codec_ctx;

    if (!mp3_codec)
    {
        LOGD("%s, %d, mp3 codec already close\n", __func__, __LINE__);
        return BK_OK;
    }

    if (mp3_codec_state_get(mp3_codec) == AUDIO_CODEC_STA_IDLE)
    {
        return BK_OK;
    }

    LOGI("%s \n", __func__);

    /* deinit mp3 codec task */
    mp3_codec_task_deinit(mp3_codec);

    mp3_codec_state_set(mp3_codec, AUDIO_CODEC_STA_IDLE);

    /* deinit ringbuffer */
    if (mp3_codec->rb)
    {
        rb_destroy(mp3_codec->rb);
        mp3_codec->rb = NULL;
    }

    if (mp3_codec)
    {
        psram_free(mp3_codec);
        codec->codec_ctx = NULL;
    }

    bk_pm_module_vote_cpu_freq(PM_DEV_ID_AUDIO, PM_CPU_FRQ_DEFAULT);

    LOGD("onboard spk close complete\n");
    return BK_OK;
}

static int mp3_codec_write(audio_codec_t *codec, char *buffer, uint32_t len)
{
    if (!codec || !buffer || !len)
    {
        LOGE("%s, %d, params error, codec: %p, buffer: %p, len: %d\n", __func__, __LINE__, codec, buffer, len);
        return BK_FAIL;
    }

    mp3_codec_priv_t *priv = (mp3_codec_priv_t *)codec->codec_ctx;
    MP3_CODEC_CHECK_NULL(priv);

    //uint32_t need_w_len = len;
    int need_w_len = (int)len;

    while (need_w_len)
    {
        int w_len = rb_write(priv->rb, buffer, need_w_len, BEKEN_WAIT_FOREVER);
        if (w_len > 0)
        {
            need_w_len -= w_len;
        }
        else
        {
            LOGE("rb_write FAIL, w_len: %d \n", w_len);
            return BK_FAIL;
        }
    }

    return len;
}

static int mp3_codec_ctrl(audio_codec_t *codec, audio_codec_ctrl_op_t op, void *params)
{
    if (!codec)
    {
        LOGE("%s, %d, codec: %p is null\n", __func__, __LINE__, codec);
        return BK_FAIL;
    }

    mp3_codec_priv_t *priv = (mp3_codec_priv_t *)codec->codec_ctx;
    MP3_CODEC_CHECK_NULL(priv);

    bk_err_t ret = BK_OK;

    LOGD("%s, op: %d \n", __func__, op);

    switch (op)
    {
        case AUDIO_CODEC_CTRL_START:
#if 0
            ret = mp3_data_read_send_msg(priv->mp3_codec_msg_que, MP3_CODEC_IDLE, NULL);
            if (ret != BK_OK)
            {
                LOGE("%s, %d, send msg to stop mp3 decode fail\n", __func__, __LINE__);
                break;
            }
#endif
            ret = mp3_data_read_send_msg(priv->mp3_codec_msg_que, MP3_CODEC_START, NULL);
            if (ret != BK_OK)
            {
                LOGE("%s, %d, send msg to start mp3 decode fail\n", __func__, __LINE__);
            }
            break;

        case AUDIO_CODEC_CTRL_STOP:
            ret = mp3_data_read_send_msg(priv->mp3_codec_msg_que, MP3_CODEC_IDLE, NULL);
            if (ret != BK_OK)
            {
                LOGE("%s, %d, send msg to stop mp3 decode fail\n", __func__, __LINE__);
                break;
            }
            break;

        case AUDIO_CODEC_CTRL_CLEAR_POOL:
            rb_abort_write(priv->rb);
            rb_reset(priv->rb);
            break;

        default:
            ret = BK_FAIL;
            break;
    }

    return ret;
}


audio_codec_ops_t mp3_codec_ops =
{
    .open = mp3_codec_open,
    .write = mp3_codec_write,
    .close = mp3_codec_close,
    .ctrl = mp3_codec_ctrl,
};

audio_codec_ops_t *get_mp3_codec_ops(void)
{
    return &mp3_codec_ops;
}

