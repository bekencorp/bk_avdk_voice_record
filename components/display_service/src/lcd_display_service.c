// Copyright 2020-2021 Beken
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

#include <os/os.h>
#include <os/mem.h>
#include <os/str.h>
#include <components/log.h>
#include <driver/lcd.h>
#include <driver/flash.h>
#include "lcd_display_service.h"
#include "frame_buffer.h"
#include "yuv_encode.h"
#if CONFIG_LCD_QSPI
#include <lcd_qspi_display_service.h>
#endif
#include "mux_pipeline.h"

#include "bk_draw_blend.h"

#include <driver/gpio.h>
#include "gpio_driver.h"
#include <driver/hal/hal_gpio_types.h>

#if CONFIG_LCD_SPI_DISPLAY
#include <lcd_spi_display_service.h>
#endif



#define TAG "lcd_disp"

#define LOGI(...) BK_LOGI(TAG, ##__VA_ARGS__)
#define LOGW(...) BK_LOGW(TAG, ##__VA_ARGS__)
#define LOGE(...) BK_LOGE(TAG, ##__VA_ARGS__)
#define LOGD(...) BK_LOGD(TAG, ##__VA_ARGS__)

#ifdef DISP_DIAG_DEBUG
#define DISPLAY_START()         do { GPIO_UP(GPIO_DVP_D6); } while (0)
#define DISPLAY_END()           do { GPIO_DOWN(GPIO_DVP_D6); } while (0)
#define DISPLAY_ISR_START()         do { GPIO_UP(GPIO_DVP_D7); } while (0)
#define DISPLAY_ISR_END()           do { GPIO_DOWN(GPIO_DVP_D7); } while (0)
#else
#define DISPLAY_START()
#define DISPLAY_END()
#define DISPLAY_ISR_START()
#define DISPLAY_ISR_END()
#endif

extern media_debug_t *media_debug;
extern uint32_t  platform_is_in_interrupt_context(void);

typedef struct
{
    uint32_t event;
    uint32_t param;
} display_msg_t;


typedef struct
{
    uint8_t disp_task_running : 1;
    lcd_type_t lcd_type;
    uint16_t lcd_width;
    uint16_t lcd_height;
    frame_buffer_t *pingpong_frame;
    frame_buffer_t *display_frame;
    beken_semaphore_t disp_sem;
    beken_semaphore_t disp_task_sem;
    beken_thread_t disp_task;
    beken_queue_t queue;
    beken_semaphore_t te_sem;
    uint8_t te_io;
    bool partial_refresh;
    complex_buffer_t * partial_refresh_buf;
    uint16_t partial_y;
} lcd_disp_config_t;

typedef struct
{
    beken_mutex_t lock;
} display_service_info_t;

typedef enum
{
    DISPLAY_FRAME_REQUEST,
    DISPLAY_PARTIAL_REQUEST,
    DISPLAY_FRAME_FREE,
    DISPLAY_FRAME_EXTI,
} lcd_display_msg_type_t;

static lcd_disp_config_t *lcd_disp_config = NULL;
static display_service_info_t *service_info = NULL;
const lcd_device_t *g_lcd_device = NULL;

bk_err_t lcd_display_task_send_msg(uint8_t type, uint32_t param);

static void lcd_display_free(frame_buffer_t *frame)
{
    if (frame->cb)
    {
        frame->cb->free(frame);
    }
    else
    {
        frame_buffer_display_free(frame);
    }
}

#if CONFIG_LCD_TE_ENABLE 
/*ili9488 te time is adjusted to about 20ms, write one frame data is about 16ms, so set timeout to 4ms*/
#define TE_TIMEOUT_MS 4
static uint64_t te_time = 0;

static void check_te_sem_valid(void)
{
    uint64_t cur_time;

    cur_time = rtos_get_time();
    uint32_t tmp = (cur_time >= te_time) ? (cur_time - te_time) : (0xffffffff - te_time + cur_time);
    LOGI("%s cur_time = %d,  tmp = %d\n", __func__, cur_time, tmp);

    if(lcd_disp_config->te_sem && tmp > TE_TIMEOUT_MS)
    {
        LOGI("%s rtos_get_semaphore\n", __func__);
        rtos_get_semaphore(&lcd_disp_config->te_sem,0);
    }
}
static void wait_te_sync(int wait_ms)
{
    if(lcd_disp_config->te_sem)
    {
        rtos_get_semaphore(&lcd_disp_config->te_sem,wait_ms);
    }
}

static void lcd_te_isr(gpio_id_t id)
{
    if(lcd_disp_config->te_sem)
    {
        rtos_set_semaphore(&lcd_disp_config->te_sem);
       te_time = rtos_get_time();
       LOGI("%s tmp = %d\n", __func__, te_time);
    }
}

static void bk_lcd_te_config(uint8_t te_io)
{
    gpio_config_t cfg;
    int int_type;
    gpio_dev_unmap(te_io);
    int_type = GPIO_INT_TYPE_FALLING_EDGE;
    cfg.io_mode = GPIO_INPUT_ENABLE;
    cfg.pull_mode = GPIO_PULL_DISABLE;
    bk_gpio_set_config(te_io, &cfg);
    bk_gpio_register_isr(te_io , lcd_te_isr);
    bk_gpio_enable_interrupt(te_io);
    BK_LOG_ON_ERR(bk_gpio_set_interrupt_type(te_io, int_type));
}
#endif

static void lcd_driver_display_mcu_isr(void)
{
    DISPLAY_ISR_START();
    media_debug->isr_lcd++;
    GLOBAL_INT_DECLARATION();

    if (lcd_disp_config->pingpong_frame != NULL)
    {
        media_debug->fps_lcd++;
        if (lcd_disp_config->display_frame)
        {
            lcd_display_free(lcd_disp_config->display_frame);
            lcd_disp_config->display_frame = NULL;
        }

        GLOBAL_INT_DISABLE();
        lcd_disp_config->display_frame = lcd_disp_config->pingpong_frame;
        lcd_disp_config->pingpong_frame = NULL;
        GLOBAL_INT_RESTORE();
        bk_lcd_8080_start_transfer(0);

        rtos_set_semaphore(&lcd_disp_config->disp_sem);
    }
    if (lcd_disp_config->partial_refresh)
    {
        if (lcd_disp_config->partial_y == lcd_disp_config->lcd_height - 16)
        {
            media_debug->fps_lcd++;
        }
        bk_lcd_8080_start_transfer(0);
        rtos_set_semaphore(&lcd_disp_config->disp_sem);
    }
    DISPLAY_ISR_END();
}

#if CONFIG_LV_ATTRIBUTE_FAST_MEM
static void lcd_driver_display_rgb_isr(void)
#else
__attribute__((section(".itcm_sec_code"))) static void lcd_driver_display_rgb_isr(void)
#endif
{
    DISPLAY_ISR_START();
    //flash_op_status_t flash_status = FLASH_OP_IDLE;
    //flash_status = bk_flash_get_operate_status();
    media_debug->isr_lcd++;
    //if (flash_status == FLASH_OP_IDLE)
    {
        GLOBAL_INT_DECLARATION();
        if (lcd_disp_config->pingpong_frame != NULL)
        {
            if (lcd_disp_config->display_frame != NULL)
            {
                frame_buffer_t *temp_buffer = NULL;
                bk_err_t ret = BK_OK;
                media_debug->fps_lcd++;

                GLOBAL_INT_DISABLE();

                if (lcd_disp_config->pingpong_frame != lcd_disp_config->display_frame)
                {
                    if (lcd_disp_config->display_frame->width != lcd_disp_config->pingpong_frame->width
                        || lcd_disp_config->display_frame->height != lcd_disp_config->pingpong_frame->height)
                    {
                        lcd_driver_ppi_set(lcd_disp_config->pingpong_frame->width, lcd_disp_config->pingpong_frame->height);
                    }
                    if (lcd_disp_config->display_frame->fmt != lcd_disp_config->pingpong_frame->fmt)
                    {
                        bk_lcd_set_yuv_mode(lcd_disp_config->pingpong_frame->fmt);
                    }
                    if (lcd_disp_config->display_frame->cb != NULL
                        && lcd_disp_config->display_frame->cb->free != NULL)
                    {
                        lcd_disp_config->display_frame->cb->free(lcd_disp_config->display_frame);
                    }
                    else
                    {
                        temp_buffer = lcd_disp_config->display_frame;
                        lcd_disp_config->display_frame = NULL;
                    }
                }
                lcd_disp_config->display_frame = lcd_disp_config->pingpong_frame;
                lcd_disp_config->pingpong_frame = NULL;

                lcd_driver_set_display_base_addr((uint32_t)lcd_disp_config->display_frame->frame);

                if (temp_buffer != NULL)
                {
                    ret = lcd_display_task_send_msg(DISPLAY_FRAME_FREE, (uint32_t)temp_buffer);
                    if (ret != BK_OK)
                    {
                        lcd_display_free(temp_buffer);
                    }
                }
                GLOBAL_INT_RESTORE();
                rtos_set_semaphore(&lcd_disp_config->disp_sem);
            }
            else
            {
                GLOBAL_INT_DISABLE();
                lcd_disp_config->display_frame = lcd_disp_config->pingpong_frame;
                lcd_disp_config->pingpong_frame = NULL;
                GLOBAL_INT_RESTORE();
                rtos_set_semaphore(&lcd_disp_config->disp_sem);
            }
        }
    }
    DISPLAY_ISR_END();
}

static bk_err_t lcd_display_frame(frame_buffer_t *frame)
{
    bk_err_t ret = BK_FAIL;
    DISPLAY_START();
#if CONFIG_LCD_TE_ENABLE 
    check_te_sem_valid();
#endif

    if (lcd_disp_config->display_frame == NULL)
    {
        lcd_driver_ppi_set(frame->width, frame->height);

        bk_lcd_set_yuv_mode(frame->fmt);
        lcd_disp_config->pingpong_frame = frame;

        lcd_driver_set_display_base_addr((uint32_t)frame->frame);
        lcd_driver_display_enable();
        LOGI("display start, frame width, height %d, %d\n", frame->width, frame->height);
    }
    else
    {
        GLOBAL_INT_DECLARATION();
        GLOBAL_INT_DISABLE();
        if (lcd_disp_config->pingpong_frame != NULL)
        {
            lcd_display_free(lcd_disp_config->pingpong_frame);
            lcd_disp_config->pingpong_frame = NULL;
        }
        GLOBAL_INT_RESTORE();

        lcd_disp_config->pingpong_frame = frame;

        if (lcd_disp_config->lcd_type == LCD_TYPE_MCU8080)
        {
#if CONFIG_LCD_TE_ENABLE
            wait_te_sync(40);
#endif

            lcd_driver_ppi_set(frame->width, frame->height);
            bk_lcd_set_yuv_mode(frame->fmt);
            lcd_driver_set_display_base_addr((uint32_t)frame->frame);

            lcd_driver_display_continue();
        }
    }

    ret = rtos_get_semaphore(&lcd_disp_config->disp_sem, BEKEN_NEVER_TIMEOUT);

    if (ret != BK_OK)
    {
        LOGE("%s semaphore get failed: %d\n", __func__, ret);
    }
    DISPLAY_END();

    return ret;
}



bk_err_t lcd_display_task_send_msg(uint8_t type, uint32_t param)
{
    int ret = BK_FAIL;
    display_msg_t msg;
    uint32_t isr_context = platform_is_in_interrupt_context();

    if (lcd_disp_config)
    {
        msg.event = type;
        msg.param = param;

        if (!isr_context)
        {
            rtos_lock_mutex(&service_info->lock);
        }

        if (lcd_disp_config->disp_task_running)
        {
            ret = rtos_push_to_queue(&lcd_disp_config->queue, &msg, BEKEN_WAIT_FOREVER);

            if (ret != BK_OK)
            {
                LOGE("%s push failed\n", __func__);
            }
        }

        if (!isr_context)
        {
            rtos_unlock_mutex(&service_info->lock);
        }

    }

    return ret;
}

bk_err_t lcd_display_frame_request(frame_buffer_t *frame)
{
    return lcd_display_task_send_msg(DISPLAY_FRAME_REQUEST, (uint32_t)frame);
}

bk_err_t lcd_display_partial_request(lcd_partial_area_t *partial_area)
{
    return lcd_display_task_send_msg(DISPLAY_PARTIAL_REQUEST, (uint32_t)partial_area);
}

bk_err_t lcd_display_partial_refresh(lcd_partial_area_t * area)
{
    int ret = BK_OK;

    lcd_disp_config->partial_refresh_buf = (complex_buffer_t *)area->buffer;
    if (lcd_disp_config->partial_refresh_buf == NULL || lcd_disp_config->partial_refresh_buf->data == NULL)
    {
        LOGE("%s partial_refresh_buf NULL \n", __func__);
        return BK_FAIL;
    }
    if (area->start_y != 0 && area->start_y  != (lcd_disp_config->partial_y + 16))
    {
        LOGE("y pos not continue %d %d \n", area->start_y, lcd_disp_config->partial_y);
    }
    lcd_disp_config->partial_y = area->start_y;
    lcd_driver_set_display_base_addr((uint32_t)lcd_disp_config->partial_refresh_buf->data);

    if (lcd_disp_config->partial_refresh == 0)
    {
        lcd_disp_config->partial_refresh = 1;
        bk_lcd_set_yuv_mode(PIXEL_FMT_RGB565_LE);
        #if 1
        g_lcd_device->mcu->set_display_area(area->start_x, area->start_x + area->width, area->start_y, area->start_y + area->height);
        #else
        g_lcd_device->mcu->set_display_area(area->start_x, area->start_y, area->width, area->height);
        #endif
		bk_lcd_pixel_config(area->width, 16);
        lcd_driver_display_enable();
    }
    else
    {
        uint16 ys_l, ys_h, ye_l, ye_h;
        ys_h = area->start_y >> 8;
        ys_l = area->start_y & 0xff;
        ye_h = (area->start_y + area->height - 1) >> 8;
        ye_l = (area->start_y + area->height - 1) & 0xff;
        uint32_t param_row[4] = {ys_h, ys_l, ye_h, ye_l};
        bk_lcd_8080_send_cmd(4, 0x2B, param_row);
		bk_lcd_pixel_config(area->width, 16);
//        g_lcd_device->mcu->set_display_area(area->start_x, area->start_x + area->width, area->start_y, area->start_y + area->height);
        lcd_driver_display_continue();
    }

    ret = rtos_get_semaphore(&lcd_disp_config->disp_sem, BEKEN_NEVER_TIMEOUT);
    if (ret != BK_OK)
    {
        LOGD("%s semaphore get failed: %d\n", __func__, ret);
    }
    area->post_refresh(area);
    return ret;
}



static void lcd_display_task_entry(beken_thread_arg_t data)
{
    lcd_disp_config->disp_task_running = true;

    rtos_set_semaphore(&lcd_disp_config->disp_task_sem);

    while (lcd_disp_config->disp_task_running)
    {
        display_msg_t msg;
        int ret = rtos_pop_from_queue(&lcd_disp_config->queue, &msg, BEKEN_WAIT_FOREVER);
        if (ret == BK_OK)
        {
            switch (msg.event)
            {
                case DISPLAY_FRAME_REQUEST:
#if (CONFIG_BLEND_UI)
                    if (g_dyn_array.size > 0)
                    {
                        blend_info_t *info = &g_dyn_array.entry[0];
                        bk_display_blend_handle((frame_buffer_t *)msg.param, lcd_disp_config->lcd_width,
                                                lcd_disp_config->lcd_height, info);
                    }
#endif
                    lcd_display_frame((frame_buffer_t *)msg.param);
                    break;
                case DISPLAY_PARTIAL_REQUEST:
                    lcd_display_partial_refresh((lcd_partial_area_t *)msg.param);
                    break;
                case DISPLAY_FRAME_FREE:
                    lcd_display_free((frame_buffer_t *)msg.param);
#if CONFIG_MEDIA_PSRAM_SIZE_4M
					extern void jpeg_decode_get_next_frame();
					jpeg_decode_get_next_frame();
#endif
                    break;

                case DISPLAY_FRAME_EXTI:
                {
                    rtos_lock_mutex(&service_info->lock);

                    GLOBAL_INT_DECLARATION();
                    GLOBAL_INT_DISABLE();
                    lcd_disp_config->disp_task_running = false;
                    GLOBAL_INT_RESTORE();

                    do
                    {
                        ret = rtos_pop_from_queue(&lcd_disp_config->queue, &msg, BEKEN_NO_WAIT);

                        if (ret == BK_OK)
                        {
                            if (msg.event == DISPLAY_FRAME_REQUEST || msg.event == DISPLAY_FRAME_FREE)
                            {
                                lcd_display_free((frame_buffer_t *)msg.param);
                            }
                        }
                    }
                    while (ret == BK_OK);

                    rtos_unlock_mutex(&service_info->lock);
                }
                goto exit;
                break;
            }
        }
    }

exit:

    lcd_disp_config->disp_task = NULL;
    rtos_set_semaphore(&lcd_disp_config->disp_task_sem);
    rtos_delete_thread(NULL);
}


static bk_err_t lcd_display_task_start(void)
{
    int ret = BK_OK;

    ret = rtos_init_queue(&lcd_disp_config->queue,
                          "display_queue",
                          sizeof(display_msg_t),
                          15);

    if (ret != BK_OK)
    {
        LOGE("%s, init display_queue failed\r\n", __func__);
        return ret;
    }

    ret = rtos_create_thread(&lcd_disp_config->disp_task,
                             BEKEN_DEFAULT_WORKER_PRIORITY,
                             "display_thread",
                             (beken_thread_function_t)lcd_display_task_entry,
                             1024 * 2,
                             (beken_thread_arg_t)NULL);

    if (BK_OK != ret)
    {
        LOGE("%s lcd_display_thread init failed\n", __func__);
        return ret;
    }

    ret = rtos_get_semaphore(&lcd_disp_config->disp_task_sem, BEKEN_NEVER_TIMEOUT);

    if (BK_OK != ret)
    {
        LOGE("%s decoder_sem get failed\n", __func__);
    }

    return ret;
}

static bk_err_t lcd_display_task_stop(void)
{
    bk_err_t ret = BK_OK;
    if (!lcd_disp_config || lcd_disp_config->disp_task_running == false)
    {
        LOGI("%s already stop\n", __func__);
        return ret;
    }

    lcd_display_task_send_msg(DISPLAY_FRAME_EXTI, 0);

    ret = rtos_get_semaphore(&lcd_disp_config->disp_task_sem, BEKEN_NEVER_TIMEOUT);

    if (BK_OK != ret)
    {
        LOGE("%s jpeg_display_sem get failed\n", __func__);
    }

    if (lcd_disp_config->queue)
    {
        rtos_deinit_queue(&lcd_disp_config->queue);
        lcd_disp_config->queue = NULL;
    }

    LOGI("%s complete\n", __func__);

    return ret;
}


bk_err_t lcd_display_config_free(void)
{
    int ret = BK_OK;

    if (lcd_disp_config)
    {
        if (lcd_disp_config->disp_task_sem)
        {
            rtos_deinit_semaphore(&lcd_disp_config->disp_task_sem);
            lcd_disp_config->disp_task_sem = NULL;
        }

        lcd_driver_deinit();

        if (lcd_disp_config->disp_sem)
        {
            rtos_deinit_semaphore(&lcd_disp_config->disp_sem);
            lcd_disp_config->disp_sem = NULL;
        }
#if CONFIG_LCD_TE_ENABLE
        if (lcd_disp_config->te_sem)
        {
            rtos_deinit_semaphore(&lcd_disp_config->te_sem);
            lcd_disp_config->te_sem = NULL;
        }
        bk_gpio_disable_interrupt(lcd_disp_config->te_io);
#endif

        if (lcd_disp_config->pingpong_frame)
        {
            LOGI("%s pingpong_frame free\n", __func__);
            lcd_display_free(lcd_disp_config->pingpong_frame);
            lcd_disp_config->pingpong_frame = NULL;
        }

        if (lcd_disp_config->display_frame)
        {
            LOGI("%s display_frame free\n", __func__);
            lcd_display_free(lcd_disp_config->display_frame);
            lcd_disp_config->display_frame = NULL;
        }

        if (lcd_disp_config)
        {
            os_free(lcd_disp_config);
            lcd_disp_config = NULL;
        }
    }
    LOGD("%s %d\n", __func__, __LINE__);
    return ret;
}


bool check_lcd_task_is_open(void)
{
    if (lcd_disp_config == NULL)
    {
        return false;
    }
    else
    {
        return lcd_disp_config->disp_task_running;
    }
}

bk_err_t lcd_display_get_info(lcd_info_t *info)
{
    int ret = BK_FAIL;

    if (lcd_disp_config == NULL)
    {
        LOGD("%s lcd info is NULL\r\n", __func__);
        return BK_FAIL;
    }
    else
    {
        if (info)
        {
            info->interface = lcd_disp_config->lcd_type;
            info->width = lcd_disp_config->lcd_width;
            info->height = lcd_disp_config->lcd_height;
            return BK_OK;
        }
    }
    return ret;
}

bk_err_t lcd_display_open(lcd_open_t *config)
{
    int ret = BK_OK;

    if (lcd_disp_config && lcd_disp_config->disp_task_running)
    {
        LOGE("%s lcd display task is running!\r\n", __func__);
        return ret;
    }

    lcd_disp_config = (lcd_disp_config_t *)os_malloc(sizeof(lcd_disp_config_t));
    if (lcd_disp_config == NULL)
    {
        LOGE("%s, malloc lcd_disp_config fail!\r\n", __func__);
        ret = BK_FAIL;
        return ret;
    }

    os_memset(lcd_disp_config, 0, sizeof(lcd_disp_config_t));

    ret = rtos_init_semaphore(&lcd_disp_config->disp_task_sem, 1);
    if (ret != BK_OK)
    {
        LOGE("%s disp_task_sem init failed: %d\n", __func__, ret);
        goto out;
    }

    ret = rtos_init_semaphore(&lcd_disp_config->disp_sem, 1);
    if (ret != BK_OK)
    {
        LOGE("%s disp_sem init failed: %d\n", __func__, ret);
        goto out;
    }

    if (config->device_name != NULL)
    {
        g_lcd_device = get_lcd_device_by_name(config->device_name);
    }

    if (g_lcd_device == NULL)
    {
        g_lcd_device = get_lcd_device_by_ppi(config->device_ppi);
    }

    if (g_lcd_device == NULL)
    {
        LOGE("%s lcd device not found\n", __func__);
        goto out;
    }

    lcd_disp_config->lcd_width = g_lcd_device->ppi >> 16;
    lcd_disp_config->lcd_height = g_lcd_device->ppi & 0xFFFF;
    lcd_disp_config->lcd_type = g_lcd_device->type;

    // step 4: init frame buffer
    LOGI("%s %d lcd ppi:%d %d\n", __func__, __LINE__, lcd_disp_config->lcd_width, lcd_disp_config->lcd_height);

    // step 5: init lcd display
    ret = lcd_driver_init(g_lcd_device);
    if (ret != BK_OK)
    {
        LOGE("%s, lcd_driver_init fail\r\n", __func__);
        goto out;
    }

    if(g_lcd_device->type == LCD_TYPE_MCU8080)
    {
        bk_lcd_isr_register(I8080_OUTPUT_EOF, lcd_driver_display_mcu_isr);
#if CONFIG_LCD_TE_ENABLE
        lcd_disp_config->te_io = lcd_device->mcu->te_io;
        bk_lcd_te_config(lcd_device->mcu->te_io);
        BK_LOG_ON_ERR(rtos_init_semaphore(&lcd_disp_config->te_sem, 1));
#endif 
    }
    else
    {
        bk_lcd_isr_register(RGB_OUTPUT_EOF, lcd_driver_display_rgb_isr);
    }

    media_debug->fps_lcd = 0;
    media_debug->isr_lcd = 0;

    if (g_lcd_device->type == LCD_TYPE_SPI)
    {
#if CONFIG_LCD_SPI_DISPLAY
        lcd_spi_init(g_lcd_device);
        lcd_spi_set_display_area(0, lcd_disp_config->lcd_width - 1, 0, lcd_disp_config->lcd_height - 1);
#endif
    }
    else if (g_lcd_device->type == LCD_TYPE_QSPI)
    {
#if CONFIG_LCD_QSPI
        bk_lcd_qspi_disp_task_start(g_lcd_device);
        lcd_disp_config->disp_task_running = true;
#endif
    }
    else
    {
        ret = lcd_display_task_start();
        if (ret != BK_OK)
        {
            LOGE("%s lcd_display_task_start failed: %d\n", __func__, ret);
            goto out;
        }
    }

    if (g_lcd_device->type != LCD_TYPE_SPI)
    {
        lcd_driver_backlight_open();
    }

    LOGI("%s %d complete\n", __func__, __LINE__);

    return ret;

out:

    LOGE("%s failed\r\n", __func__);
    lcd_display_config_free();

    return ret;
}

bk_err_t lcd_display_close(void)
{
    LOGD("%s, %d\n", __func__, __LINE__);

    if (lcd_disp_config == NULL)
    {
        LOGE("%s, have been closed!\r\n", __func__);
        return BK_OK;
    }

    if (lcd_disp_config->lcd_type != LCD_TYPE_SPI)
    {
        lcd_driver_backlight_close();
    }

    if (lcd_disp_config->lcd_type == LCD_TYPE_SPI)
    {
#if CONFIG_LCD_SPI_DISPLAY
        lcd_spi_deinit();
#endif
    }
    else if (lcd_disp_config->lcd_type == LCD_TYPE_QSPI)
    {
#if CONFIG_LCD_QSPI
        bk_lcd_qspi_disp_task_stop();
        lcd_disp_config->disp_task_running = false;
#endif
    }    else
    {
        lcd_display_task_stop();
    }

    lcd_display_config_free();

    LOGI("%s complete, %d\n", __func__, __LINE__);

    return BK_OK;
}

bk_err_t lcd_display_service_init(void)
{
    bk_err_t ret =  BK_FAIL;

    service_info = os_malloc(sizeof(display_service_info_t));

    if (service_info == NULL)
    {
        LOGE("%s, malloc service_info failed\n", __func__);
        goto error;
    }

    os_memset(service_info, 0, sizeof(display_service_info_t));

    ret = rtos_init_mutex(&service_info->lock);

    if (ret != BK_OK)
    {
        LOGE("%s, init mutex failed\n", __func__);
        goto error;
    }

    return BK_OK;

error:

    return BK_FAIL;
}
