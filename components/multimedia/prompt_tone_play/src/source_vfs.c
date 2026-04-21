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
#include "source_vfs.h"
#include "bk_posix.h"
#include "driver/flash_partition.h"
#if CONFIG_LITTLEFS_USE_LITTLEFS_PARTITION
#include "vendor_flash_partition.h"
#endif


#define VFS_SOURCE_TAG "vfs_src"
#define LOGI(...) BK_LOGI(VFS_SOURCE_TAG, ##__VA_ARGS__)
#define LOGW(...) BK_LOGW(VFS_SOURCE_TAG, ##__VA_ARGS__)
#define LOGE(...) BK_LOGE(VFS_SOURCE_TAG, ##__VA_ARGS__)
#define LOGD(...) BK_LOGD(VFS_SOURCE_TAG, ##__VA_ARGS__)


#define MOUNT_ENABLE
#define UNMOUNT_VFS_TIMER_INTERVAL      (3000)

#define VFS_SOURCE_CHECK_NULL(ptr) do {\
        if (ptr == NULL) {\
            LOGE("VFS_SOURCE_CHECK_NULL fail \n");\
            return BK_FAIL;\
        }\
    } while(0)

typedef enum
{
    VFS_DATA_READ_IDLE = 0,
    VFS_DATA_READ_START,
    VFS_DATA_READ_EXIT
} vfs_data_read_op_t;

typedef struct
{
    vfs_data_read_op_t op;
    void *param;
} vfs_data_read_msg_t;

typedef struct vfs_source_priv_s
{
    int fd;

    uint32_t read_buff_size;
    uint8_t *read_buff;

    beken_thread_t vfs_data_read_task_hdl;
    beken_queue_t vfs_data_read_msg_que;
    beken_semaphore_t sem;
    bool running;

    bool mount_state;
    beken2_timer_t unmount_timer;

    audio_source_cfg_t config;
} vfs_source_priv_t;

#ifdef MOUNT_ENABLE
int vfs_source_mount(vfs_source_priv_t *vfs_source)
{
    int ret = BK_FAIL;

    /* check whether vfs already mount */
    if (vfs_source->mount_state)
    {
        /* stop unmount timer */
        ret = rtos_stop_oneshot_timer(&vfs_source->unmount_timer);
        if (ret != BK_OK)
        {
            LOGE("%s, %d, stop unmount timer fail \n", __func__, __LINE__);
        }

        LOGI("%s, vfs already mount\n", __func__);

        return BK_OK;
    }

    LOGI("%s\n", __func__);

#if (CONFIG_FATFS)
    struct bk_fatfs_partition partition;
    char *fs_name = NULL;

    fs_name = "fatfs";
    partition.part_type = FATFS_DEVICE;
#if (CONFIG_SDCARD)
    partition.part_dev.device_name = FATFS_DEV_SDCARD;
#else
    partition.part_dev.device_name = FATFS_DEV_FLASH;
#endif
    partition.mount_path = "/";

    ret = mount("SOURCE_NONE", partition.mount_path, fs_name, 0, &partition);
#endif

#if (CONFIG_LITTLEFS)

    struct bk_little_fs_partition partition;
    char *fs_name = NULL;
#ifdef BK_PARTITION_LITTLEFS_USER
    bk_logic_partition_t *pt = bk_flash_partition_get_info(BK_PARTITION_LITTLEFS_USER);
#else
    bk_logic_partition_t *pt = bk_flash_partition_get_info(BK_PARTITION_USR_CONFIG);
#endif

    fs_name = "littlefs";
    partition.part_type = LFS_FLASH;
    partition.part_flash.start_addr = pt->partition_start_addr;
    partition.part_flash.size = pt->partition_length;
    partition.mount_path = "/";

    ret = mount("SOURCE_NONE", partition.mount_path, fs_name, 0, &partition);
#endif

    if (ret != BK_OK)
    {
        LOGE("%s, %d, mount fail, ret: %d\n", __FUNCTION__, __LINE__, ret);
        return BK_FAIL;
    }
    else
    {
        LOGI("mount success\n");
        vfs_source->mount_state = true;
        return BK_OK;
    }
}

static void vfs_unmount_timer_callback(void *param, void *param1)
{
    vfs_source_priv_t *vfs_source = (vfs_source_priv_t *)param;
    bk_err_t ret = BK_FAIL;

    if (!vfs_source)
    {
        LOGE("%s, %d, vfs_source is null\n", __FUNCTION__, __LINE__);
        return;
    }

    LOGI("%s\n", __func__);

    ret = umount("/");
    if (BK_OK != ret)
    {
        LOGE("%s, %d, unmount fail:%d\n", __FUNCTION__, __LINE__, ret);
    }
    else
    {
        LOGI("unmount success\n");
        vfs_source->mount_state = false;
    }
}

int vfs_source_unmount(vfs_source_priv_t *vfs_source)
{
    bk_err_t ret = BK_FAIL;

    ret = rtos_start_oneshot_timer(&vfs_source->unmount_timer);
    if (ret != BK_OK)
    {
        LOGE("%s, %d, start %s vfs unmount timer fail \n", __func__, __LINE__);
    }

    return ret;
}
#endif

static bk_err_t vfs_data_read_send_msg(beken_queue_t queue, vfs_data_read_op_t op, void *param)
{
    bk_err_t ret;
    vfs_data_read_msg_t msg;

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

static void vfs_data_read_task_main(beken_thread_arg_t param_data)
{
    bk_err_t ret = BK_OK;

    vfs_source_priv_t *vfs_source_priv = (vfs_source_priv_t *)param_data;

    vfs_source_priv->running = false;
    uint32_t wait_time = BEKEN_WAIT_FOREVER;

    rtos_set_semaphore(&vfs_source_priv->sem);

    while (1)
    {
        vfs_data_read_msg_t msg;
        ret = rtos_pop_from_queue(&vfs_source_priv->vfs_data_read_msg_que, &msg, wait_time);
        if (kNoErr == ret)
        {
            switch (msg.op)
            {
                case VFS_DATA_READ_IDLE:
                    LOGD("%s, %d, VFS_DATA_READ_IDLE\n", __func__, __LINE__);
                    vfs_source_priv->running = false;
                    wait_time = BEKEN_WAIT_FOREVER;
                    /* check whether file has been open, and close file */
                    if (vfs_source_priv->fd >= 0)
                    {
                        close(vfs_source_priv->fd);
                        vfs_source_priv->fd = -1;
#ifdef MOUNT_ENABLE
                        /* unmount vfs system after close file to avoid high power consumption */
                        vfs_source_unmount(vfs_source_priv);
#endif
                    }
                    break;

                case VFS_DATA_READ_EXIT:
                    LOGD("%s, %d, VFS_DATA_READ_EXIT\n", __func__, __LINE__);
                    goto vfs_data_read_exit;
                    break;

                case VFS_DATA_READ_START:
                    LOGD("%s, %d, VFS_DATA_READ_START\n", __func__, __LINE__);
#ifdef MOUNT_ENABLE
                    /* mount vfs system before open file to avoid high power consumption */
                    /* mount file */
                    ret = vfs_source_mount(vfs_source_priv);
                    if (ret != BK_OK)
                    {
                        LOGE("%s, %d, mount fail\n", __func__, __LINE__);
                        break;
                    }
#endif
                    /* check whether url is valid, and open file */
                    if (vfs_source_priv->config.url)
                    {
                        vfs_source_priv->fd = open(vfs_source_priv->config.url, O_RDONLY);
                        if (vfs_source_priv->fd < 0)
                        {
                            LOGE("%s, %d, open :%s fail, fd: %d\n", __func__, __LINE__, vfs_source_priv->config.url, vfs_source_priv->fd);
#ifdef MOUNT_ENABLE
                            vfs_source_unmount(vfs_source_priv);
#endif
                            /* notify app prompt tone is not exist. */
                            if (vfs_source_priv->config.notify)
                            {
                                vfs_source_priv->config.notify(vfs_source_priv->config.usr_data, (void *)AUDIO_SOURCE_EVENT_LACK_RESOURCE);
                            }
                            /* set vfs file read task to idle state */
                            vfs_data_read_send_msg(vfs_source_priv->vfs_data_read_msg_que, VFS_DATA_READ_IDLE, NULL);
                        }
                        else
                        {
                            vfs_source_priv->running = true;
                            wait_time = 0;
                        }
                    }
                    else
                    {
                        LOGE("%s, %d, url:%s is invalid\n", __func__, __LINE__, vfs_source_priv->config.url);
                    }
                    break;

                default:
                    break;
            }

            continue;
        }

        /* read speaker data and write to dac fifo */
        if (vfs_source_priv->running)
        {
            int r_len = read(vfs_source_priv->fd, vfs_source_priv->read_buff, vfs_source_priv->read_buff_size);
            LOGD("%s %d read_buff_size:%d,r_len:%d\n", __func__, __LINE__, vfs_source_priv->read_buff_size, r_len);
            if (r_len > 0)
            {
                if (vfs_source_priv->config.data_handle)
                {
                    vfs_source_priv->config.data_handle((char *)vfs_source_priv->read_buff, r_len, vfs_source_priv->config.usr_data);
                }
            }
            else if (r_len == 0)
            {
                /* notify app file is empty. */
                if (vfs_source_priv->config.notify)
                {
                    vfs_source_priv->config.notify(vfs_source_priv->config.usr_data, (void *)AUDIO_SOURCE_EVENT_EMPTY);
                }

                /* set vfs file read task to idle state */
                vfs_data_read_send_msg(vfs_source_priv->vfs_data_read_msg_que, VFS_DATA_READ_IDLE, NULL);
            }
            else
            {
                /* notify app read file fail. */
                if (vfs_source_priv->config.notify)
                {
                    vfs_source_priv->config.notify(vfs_source_priv->config.usr_data, (void *)AUDIO_SOURCE_EVENT_FAIL);
                }

                /* set vfs file read task to idle state */
                vfs_data_read_send_msg(vfs_source_priv->vfs_data_read_msg_que, VFS_DATA_READ_IDLE, NULL);
            }
        }
    }

vfs_data_read_exit:

    vfs_source_priv->running = false;

    /* check whether file has been open, and close file */
    if (vfs_source_priv->fd >= 0)
    {
        close(vfs_source_priv->fd);
        vfs_source_priv->fd = -1;
    }

    /* delete msg queue */
    ret = rtos_deinit_queue(&vfs_source_priv->vfs_data_read_msg_que);
    if (ret != kNoErr)
    {
        LOGE("%s, %d, delete message queue fail\n", __func__, __LINE__);
    }
    vfs_source_priv->vfs_data_read_msg_que = NULL;

    /* delete task */
    vfs_source_priv->vfs_data_read_task_hdl = NULL;

    rtos_set_semaphore(&vfs_source_priv->sem);

    rtos_delete_thread(NULL);
}

static bk_err_t vfs_data_read_task_init(vfs_source_priv_t *vfs_source_priv)
{
    bk_err_t ret = BK_OK;

    VFS_SOURCE_CHECK_NULL(vfs_source_priv);

    vfs_source_priv->read_buff = psram_malloc(vfs_source_priv->read_buff_size);
    VFS_SOURCE_CHECK_NULL(vfs_source_priv->read_buff);

    os_memset(vfs_source_priv->read_buff, 0, vfs_source_priv->read_buff_size);

    ret = rtos_init_semaphore(&vfs_source_priv->sem, 1);
    if (ret != BK_OK)
    {
        LOGE("%s, %d, ceate semaphore fail\n", __func__, __LINE__);
        goto fail;
    }

    ret = rtos_init_queue(&vfs_source_priv->vfs_data_read_msg_que,
                          "vfs_data_rd_que",
                          sizeof(vfs_data_read_msg_t),
                          5);
    if (ret != BK_OK)
    {
        LOGE("%s, %d, create vfs data read message queue fail\n", __func__, __LINE__);
        goto fail;
    }

    ret = rtos_create_thread(&vfs_source_priv->vfs_data_read_task_hdl,
                             4,
                             "vfs_data_rd",
                             (beken_thread_function_t)vfs_data_read_task_main,
                             2048,
                             (beken_thread_arg_t)vfs_source_priv);
    if (ret != BK_OK)
    {
        LOGE("%s, %d, create vfs data read task fail\n", __func__, __LINE__);
        goto fail;
    }

    rtos_get_semaphore(&vfs_source_priv->sem, BEKEN_NEVER_TIMEOUT);

    LOGI("init vfs data read task complete\n");

    return BK_OK;

fail:

    if (vfs_source_priv->sem)
    {
        rtos_deinit_semaphore(&vfs_source_priv->sem);
        vfs_source_priv->sem = NULL;
    }

    if (vfs_source_priv->vfs_data_read_msg_que)
    {
        rtos_deinit_queue(&vfs_source_priv->vfs_data_read_msg_que);
        vfs_source_priv->vfs_data_read_msg_que = NULL;
    }

    if (vfs_source_priv->read_buff)
    {
        psram_free(vfs_source_priv->read_buff);
        vfs_source_priv->read_buff = NULL;
    }

    return BK_FAIL;
}

bk_err_t vfs_data_read_task_deinit(vfs_source_priv_t *vfs_source_priv)
{
    bk_err_t ret = BK_OK;

    VFS_SOURCE_CHECK_NULL(vfs_source_priv);

    LOGI("%s\n", __func__);

    ret = vfs_data_read_send_msg(vfs_source_priv->vfs_data_read_msg_que, VFS_DATA_READ_EXIT, NULL);
    if (ret != BK_OK)
    {
        LOGE("%s, %d, send message: VFS_DATA_READ_EXIT fail\n", __func__, __LINE__);
        return BK_FAIL;
    }

    rtos_get_semaphore(&vfs_source_priv->sem, BEKEN_NEVER_TIMEOUT);

    rtos_deinit_semaphore(&vfs_source_priv->sem);
    vfs_source_priv->sem = NULL;

    if (vfs_source_priv->read_buff)
    {
        psram_free(vfs_source_priv->read_buff);
        vfs_source_priv->read_buff = NULL;
    }

    LOGD("deinit vfs data read complete\n");

    return BK_OK;
}

static int vfs_source_open(audio_source_t *source, audio_source_cfg_t *config)
{
    vfs_source_priv_t *temp_vfs_source = NULL;
    bk_err_t ret = BK_OK;

    if (!source || !config)
    {
        LOGE("%s, %d, params error, source: %p, config: %p\n", __func__, __LINE__, source, config);
        return BK_FAIL;
    }

    LOGI("%s\n", __func__);

    vfs_source_priv_t *vfs_source = (vfs_source_priv_t *)source->source_ctx;
    if (vfs_source != NULL)
    {
        LOGE("%s, %d, vfs_source: %p already open\n", __func__, __LINE__, vfs_source);
        goto fail;
    }

    temp_vfs_source = psram_malloc(sizeof(vfs_source_priv_t));
    if (!temp_vfs_source)
    {
        LOGE("%s, %d, os_malloc temp_vfs_source: %d fail\n", __func__, __LINE__, sizeof(vfs_source_priv_t));
        goto fail;
    }

    os_memset(temp_vfs_source, 0, sizeof(vfs_source_priv_t));
    temp_vfs_source->fd = -1;

    source->source_ctx = temp_vfs_source;

    temp_vfs_source->read_buff_size = config->frame_size;
    os_memcpy(&temp_vfs_source->config, config, sizeof(audio_source_cfg_t));

#ifdef MOUNT_ENABLE
    ret = rtos_init_oneshot_timer(&temp_vfs_source->unmount_timer, UNMOUNT_VFS_TIMER_INTERVAL, vfs_unmount_timer_callback, source->source_ctx, NULL);
    if (ret != BK_OK)
    {
        LOGE("%s, %d, init %s vfs unmount timer fail \n", __func__, __LINE__);
        goto fail;
    }
#endif

    ret = vfs_data_read_task_init(source->source_ctx);
    if (ret != BK_OK)
    {
        LOGE("%s, %d, vfs_data_read_task_init fail\n", __func__, __LINE__);
        goto fail;
    }

    LOGD("vfs source open complete\n");

    return BK_OK;

fail:

    if (source->source_ctx)
    {
        psram_free(source->source_ctx);
        source->source_ctx = NULL;
    }

#ifdef MOUNT_ENABLE
    if (temp_vfs_source && temp_vfs_source->unmount_timer.handle)
    {
        ret = rtos_deinit_oneshot_timer(&temp_vfs_source->unmount_timer);
        if (ret != BK_OK)
        {
            LOGE("%s, %d, deinit vfs unmount timer fail \n", __func__, __LINE__);
        }
        temp_vfs_source->unmount_timer.handle = NULL;
    }
#endif

    return BK_FAIL;
}

static int vfs_source_close(audio_source_t *source)
{
    VFS_SOURCE_CHECK_NULL(source);
    vfs_source_priv_t *vfs_source = (vfs_source_priv_t *)source->source_ctx;

    if (!vfs_source)
    {
        LOGD("%s, %d, vfs source already close\n", __func__, __LINE__);
        return BK_OK;
    }

    LOGI("%s \n", __func__);

    vfs_data_read_task_deinit(vfs_source);

#ifdef MOUNT_ENABLE
    rtos_stop_oneshot_timer(&vfs_source->unmount_timer);

    if (vfs_source->mount_state)
    {
        vfs_unmount_timer_callback(vfs_source, NULL);
    }

    if (vfs_source->unmount_timer.handle)
    {
        rtos_deinit_oneshot_timer(&vfs_source->unmount_timer);
        vfs_source->unmount_timer.handle = NULL;
    }
#endif

    if (vfs_source)
    {
        psram_free(vfs_source);
        source->source_ctx = NULL;
    }

    LOGD("vfs source close complete\n");
    return BK_OK;
}

static int vfs_source_seek(audio_source_t *source, int offset, uint32_t whence)
{
    VFS_SOURCE_CHECK_NULL(source);
    vfs_source_priv_t *vfs_source = (vfs_source_priv_t *)source->source_ctx;
    VFS_SOURCE_CHECK_NULL(vfs_source);

    LOGI("%s \n", __func__);

    if (vfs_source->fd >= 0)
    {
        return lseek(vfs_source->fd, offset, whence);
    }
    else
    {
        return BK_FAIL;
    }
}

static int vfs_source_set_url(audio_source_t *source, url_info_t *url_info)
{
    VFS_SOURCE_CHECK_NULL(url_info);
    VFS_SOURCE_CHECK_NULL(url_info->url);
    VFS_SOURCE_CHECK_NULL(source);
    vfs_source_priv_t *vfs_source = (vfs_source_priv_t *)source->source_ctx;
    VFS_SOURCE_CHECK_NULL(vfs_source);

    LOGI("%s, url: %s \n", __func__, url_info->url);

    /* update new url */
    vfs_source->config.url = url_info->url;
    //vfs_source->config.total_size = url_info->total_len;

    return BK_OK;
}

static int vfs_source_ctrl(audio_source_t *source, audio_source_ctrl_op_t op, void *params)
{
    VFS_SOURCE_CHECK_NULL(source);
    vfs_source_priv_t *vfs_source = (vfs_source_priv_t *)source->source_ctx;
    VFS_SOURCE_CHECK_NULL(vfs_source);

    bk_err_t ret = BK_FAIL;

    LOGD("%s, op: %d \n", __func__, op);

    switch (op)
    {
        case AUDIO_SOURCE_CTRL_START:
            ret = vfs_data_read_send_msg(vfs_source->vfs_data_read_msg_que, VFS_DATA_READ_IDLE, NULL);
            if (ret != BK_OK)
            {
                LOGE("%s, %d, send msg to stop read old vfs file fail\n", __func__, __LINE__);
                break;
            }
            ret = vfs_data_read_send_msg(vfs_source->vfs_data_read_msg_que, VFS_DATA_READ_START, NULL);
            if (ret != BK_OK)
            {
                LOGE("%s, %d, send msg to start read new vfs file fail\n", __func__, __LINE__);
            }
            break;

        case AUDIO_SOURCE_CTRL_STOP:
            ret = vfs_data_read_send_msg(vfs_source->vfs_data_read_msg_que, VFS_DATA_READ_IDLE, NULL);
            if (ret != BK_OK)
            {
                LOGE("%s, %d, send msg to stop read old vfs file fail\n", __func__, __LINE__);
                break;
            }
            break;

        default:
            ret = BK_FAIL;
            break;
    }

    return ret;
}


audio_source_ops_t vfs_source_ops =
{
    .audio_source_open = vfs_source_open,
    .audio_source_seek = vfs_source_seek,
    .audio_source_close = vfs_source_close,
    .audio_source_set_url = vfs_source_set_url,
    .audio_source_ctrl = vfs_source_ctrl,
};

audio_source_ops_t *get_vfs_source_ops(void)
{
    return &vfs_source_ops;
}

