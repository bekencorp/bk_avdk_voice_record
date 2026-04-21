// Copyright 2023-2024 Beken
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//	   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "stdio.h"
#include <os/os.h>
#include <os/mem.h>
#include <os/str.h>
#include <modules/pm.h>
#include "prompt_tone_play.h"
#include "aud_tras_drv.h"


#define PROMPT_TONE_PLAY_TAG  "rt_play"
#define LOGI(...) BK_LOGI(PROMPT_TONE_PLAY_TAG, ##__VA_ARGS__)
#define LOGW(...) BK_LOGW(PROMPT_TONE_PLAY_TAG, ##__VA_ARGS__)
#define LOGE(...) BK_LOGE(PROMPT_TONE_PLAY_TAG, ##__VA_ARGS__)
#define LOGD(...) BK_LOGD(PROMPT_TONE_PLAY_TAG, ##__VA_ARGS__)

#if CONFIG_PROMPT_TONE_CODEC_MP3
#define CHUNK_SIZE      (4068)
#else
#define CHUNK_SIZE      (640)
#endif

static int source_out_data_handle_cb(char *buffer, uint32_t len, void *params)
{
    prompt_tone_play_handle_t handle = (prompt_tone_play_handle_t)params;

    uint32_t w_len = 0;
    int ret = 0;
    LOGD("%s, %d, len: %d\n", __func__, __LINE__, len);
    while (handle && handle->codec && w_len < len)
    {
        ret = audio_codec_write_data(handle->codec, buffer + w_len, len - w_len);
        if (ret <= 0)
        {
            LOGE("%s, %d, audio_codec_write_data fail, ret: %d\n", __func__, __LINE__, ret);
            break;
        }
        LOGD("%s, %d, ret: %d\n", __func__, __LINE__, ret);
        w_len += ret;
    }

    return w_len;
}

int source_notify_cb(void *play_ctx, void *params)
{

    audio_source_event_t event = (audio_source_event_t)params;

    switch (event)
    {
        case AUDIO_SOURCE_EVENT_EMPTY:
            break;

        case AUDIO_SOURCE_EVENT_FAIL:
            break;

        case AUDIO_SOURCE_EVENT_LACK_RESOURCE:
            LOGW("%s, %d, prompt tone file is not exist\n", __func__, __LINE__);
            prompt_tone_play_stop(play_ctx);
            break;

        default:
            break;
    }

    return BK_OK;
}
      
static int codec_out_data_handle_cb(audio_frame_info_t *frame_info, char *buffer, uint32_t len, void *params)
{
    LOGD("channel_number: %d, sample_rate: %d, sample_bits: %d\n", frame_info->channel_number, frame_info->sample_rate, frame_info->sample_bits);

    uint32_t temp_w_len = 0;
    uint32_t w_len = 0;
    int ret = 0;
    #if CONFIG_AUD_INTF_SUPPORT_PROMPT_TONE_RESAMPLE
    uint32_t rsp_out_len,rsp_in_len;
    uint8_t *rsp_out_addr = aud_tras_drv_get_rsp_output_buff();
    uint32_t dest_samp_cnt_20ms = (aud_tras_drv_get_dac_samp_rate()*20/1000*2);
    #endif
    
    while (w_len < len)
    {
        /* if prompt tone data pool has been write data, change speaker source type to prompt tone */
        #if CONFIG_AUD_INTF_SUPPORT_PROMPT_TONE_RESAMPLE
        if (aud_tras_drv_get_prompt_tone_data_bytes_filled() >= dest_samp_cnt_20ms)
        #else
        if (aud_tras_drv_get_prompt_tone_data_bytes_filled() >= SAMP_CNT_20MS)
        #endif
        {
            if (SPK_SOURCE_TYPE_PROMPT_TONE != aud_tras_drv_get_spk_source_type())
            {
                aud_tras_drv_voc_set_spk_source_type(SPK_SOURCE_TYPE_PROMPT_TONE);
            }
        }

        /* write speaker data to ringbuffer, aud_tras_drv read prompt tone data from the ringbuffer */
        if ((len - w_len) >= SAMP_CNT_20MS)
        {
            temp_w_len = SAMP_CNT_20MS;
        }
        else
        {
            temp_w_len = len - w_len;
        }

        #if CONFIG_AUD_INTF_SUPPORT_PROMPT_TONE_RESAMPLE
        if(PROMPT_TONE_SRC_SAMP_RATE != aud_tras_drv_get_dac_samp_rate())
        {
            rsp_in_len = (temp_w_len/2);
            rsp_out_len = MAX_SAMP_CNT_20MS/2;
            ret = bk_aud_rsp_process((int16_t *)(buffer + w_len), &rsp_in_len, (int16_t *)rsp_out_addr, &rsp_out_len);
            if (BK_OK != ret)
            {
                LOGE("%s:%d bk_aud_rsp_process fail\n", __func__, __LINE__);
                break;
            }
            
            ret = aud_tras_drv_write_prompt_tone_data((char *)rsp_out_addr, rsp_out_len*2, 40);
            LOGD("rsp len:%d, w_len:%d,in len:%d,%d,out len:%d,in addr:0x%x,out_addr:0x%x,ret:%d\n",len,w_len,temp_w_len,rsp_in_len,rsp_out_len,(buffer + w_len),rsp_out_addr,ret);

            if (ret <= 0)
            {
                LOGE("%s, %d, aud_tras_drv_write_prompt_tone_data fail, ret: %d\n", __func__, __LINE__, ret);
                break;
            }
            
            ret = temp_w_len;//write size may larger or less than input length after resample

        }
        else
        {
            ret = aud_tras_drv_write_prompt_tone_data(buffer + w_len, temp_w_len, 40);
            if (ret <= 0)
            {
                LOGE("%s, %d, aud_tras_drv_write_prompt_tone_data fail, ret: %d\n", __func__, __LINE__, ret);
                break;
            }
        }
        #else
        ret = aud_tras_drv_write_prompt_tone_data(buffer + w_len, temp_w_len, 40);
        if (ret <= 0)
        {
            LOGE("%s, %d, aud_tras_drv_write_prompt_tone_data fail, ret: %d\n", __func__, __LINE__, ret);
            break;
        }
        #endif
        
        w_len += ret;

        /* start prompt tone play after write frame data to prompt tone ringbuffer pool to avoid read prompt tone fail */
        if (SPK_SOURCE_TYPE_PROMPT_TONE != aud_tras_drv_get_spk_source_type())
        {
            aud_tras_drv_voc_set_spk_source_type(SPK_SOURCE_TYPE_PROMPT_TONE);
        }
    }

    return w_len;
}

static int prompt_tone_pool_empty_notify_cb(void *params)
{
    LOGD("%s, %d, params: %p\n", __func__, __LINE__, params);

    prompt_tone_play_handle_t handle = (prompt_tone_play_handle_t)params;

    if (handle && handle->play_finish_sem)
    {
        rtos_set_semaphore(&handle->play_finish_sem);
    }

    aud_tras_drv_voc_set_spk_source_type(SPK_SOURCE_TYPE_VOICE);

    audio_codec_ctrl(handle->codec, AUDIO_CODEC_CTRL_STOP, NULL);

    prompt_tone_play_stop(handle);

    return BK_OK;
}


prompt_tone_play_handle_t prompt_tone_play_create(  prompt_tone_play_cfg_t *config)
{
    LOGI("%s\n", __func__);

    if (!config)
    {
        LOGE("%s, %d, config is NULL\n", __func__, __LINE__);
        return NULL;
    }

    prompt_tone_play_handle_t handle = (prompt_tone_play_handle_t)psram_malloc(sizeof(struct prompt_tone_play));
    if (!handle)
    {
        LOGE("%s, %d, malloc prompt tone play handle fail\n", __func__, __LINE__);
        return NULL;
    }
    os_memset(handle, 0, sizeof(struct prompt_tone_play));

#if 0
    if (BK_OK != rtos_init_semaphore(&prompt_tone_play->play_finish_sem, 1))
    {
        LOGE("%s, %d, ceate semaphore fail\n", __func__, __LINE__);
        goto fail;
    }
#endif

    /* create source */
    config->source_cfg.data_handle = source_out_data_handle_cb;
    config->source_cfg.usr_data = handle;
    config->source_cfg.notify = source_notify_cb;
    handle->source = audio_source_create(config->source_type, &config->source_cfg);
    if (!handle->source)
    {
        LOGE("%s, %d, create audio source handle fail\n", __func__, __LINE__);
        goto fail;
    }

    /* create codec */
    config->codec_cfg.data_handle = codec_out_data_handle_cb;
    config->codec_cfg.usr_data = handle;

    config->codec_cfg.chunk_size = CHUNK_SIZE;
    config->codec_cfg.pool_size = config->codec_cfg.chunk_size * 4;
    handle->codec = audio_codec_create(config->codec_type, &config->codec_cfg);
    if (!handle->codec)
    {
        LOGE("%s, %d, create audio codec handle fail\n", __func__, __LINE__);
        goto fail;
    }

    os_memcpy(&handle->config, config, sizeof(prompt_tone_play_cfg_t));

    
    #if CONFIG_AUD_INTF_SUPPORT_PROMPT_TONE_RESAMPLE
    int ret;
    
    handle->config.rsp_cfg.dest_rate = aud_tras_drv_get_dac_samp_rate();
    if(PROMPT_TONE_SRC_SAMP_RATE != handle->config.rsp_cfg.dest_rate)
    {
        handle->config.rsp_cfg.complexity = 6;
        handle->config.rsp_cfg.src_ch = 1;//only enable SPK left channel
        handle->config.rsp_cfg.dest_ch = 1;//only enable SPK left channel
        handle->config.rsp_cfg.src_bits = 16;
        handle->config.rsp_cfg.dest_bits = 16;
        handle->config.rsp_cfg.src_rate = PROMPT_TONE_SRC_SAMP_RATE;
        handle->config.rsp_cfg.down_ch_idx = 0;
        
        ret = bk_aud_rsp_init(handle->config.rsp_cfg);
        if (ret != BK_OK)
        {
            LOGE("%s, %d, audio resampler init fail\n", __func__, __LINE__);
            goto fail;
        }

        handle->config.rsp_out_buff = (uint8_t *)psram_malloc(MAX_SAMP_CNT_20MS);
        if (!handle->config.rsp_out_buff)
        {
            LOGE("%s, %d, malloc rsp output buffer fail\n", __func__, __LINE__);
            goto fail;
        }
        LOGI("%s:%d rsp_init done!src sr:%d,dest sr:%d,cplx:%d,out buf:0x%x\n",
            __func__, __LINE__,
            handle->config.rsp_cfg.src_rate,
            handle->config.rsp_cfg.dest_rate,
            handle->config.rsp_cfg.complexity,
            handle->config.rsp_out_buff);
    }
    #endif

    return handle;

fail:

#if 0
    if (mp3_play->play_finish_sem)
    {
        rtos_deinit_semaphore(&mp3_play->play_finish_sem);
        mp3_play->play_finish_sem = NULL;
    }
#endif

    if (handle->source)
    {
        audio_source_destroy(handle->source);
        handle->source = NULL;
    }

    if (handle->codec)
    {
        audio_codec_destroy(handle->codec);
        handle->codec = NULL;
    }

    if (handle)
    {
        psram_free(handle);
        handle = NULL;
    }

    return NULL;
}

bk_err_t prompt_tone_play_destroy(prompt_tone_play_handle_t handle)
{
    LOGI("%s\n", __func__);
    if (!handle)
    {
        LOGW("%s, %d, handle already destroy\n", __func__, __LINE__);
        return BK_OK;
    }

    if (handle->codec)
    {
        audio_codec_destroy(handle->codec);
        handle->codec = NULL;
    }

    if (handle->source)
    {
        audio_source_destroy(handle->source);
        handle->source = NULL;
    }

    #if CONFIG_AUD_INTF_SUPPORT_PROMPT_TONE_RESAMPLE
    if(PROMPT_TONE_SRC_SAMP_RATE != handle->config.rsp_cfg.dest_rate)
    {        
        bk_aud_rsp_deinit();
        psram_free(handle->config.rsp_out_buff);
        handle->config.rsp_out_buff = NULL;
    }
    #endif

    psram_free(handle);

    return BK_OK;
}

bk_err_t prompt_tone_play_open(prompt_tone_play_handle_t handle)
{
    bk_err_t ret = BK_OK;

    if (!handle)
    {
        LOGE("%s, %d, handle is NULL\n", __func__, __LINE__);
        return BK_FAIL;
    }

    if (handle->status != PROMPT_TONE_PLAY_STA_NULL)
    {
        LOGW("%s, %d, prompt_tone_play already open\n", __func__, __LINE__);
        return BK_OK;
    }

    LOGI("%s\n", __func__);

    if (!handle->source || !handle->codec)
    {
        LOGE("%s, %d, handle->source: %p, handle->codec: %p\n", __func__, __LINE__, handle->source, handle->codec);
        return BK_FAIL;
    }

    aud_tras_drv_register_prompt_tone_pool_empty_notify(prompt_tone_pool_empty_notify_cb, handle);

    ret = audio_codec_open(handle->codec);
    if (ret != BK_OK)
    {
        LOGE("%s, %d, audio codec open fail\n", __func__, __LINE__);
        goto fail;
    }

    ret = audio_source_open(handle->source);
    if (ret != BK_OK)
    {
        LOGE("%s, %d, audio source open fail\n", __func__, __LINE__);
        goto fail;
    }

    handle->status = PROMPT_TONE_PLAY_STA_IDLE;

    return BK_OK;

fail:
    aud_tras_drv_register_prompt_tone_pool_empty_notify(NULL, NULL);
    audio_source_close(handle->source);
    audio_codec_close(handle->codec);

    return BK_FAIL;
}

bk_err_t prompt_tone_play_close(prompt_tone_play_handle_t handle, bool wait_play_finish)
{
    if (!handle)
    {
        LOGE("%s, %d, handle is NULL\n", __func__, __LINE__);
        return BK_FAIL;
    }

    if (handle->status == PROMPT_TONE_PLAY_STA_NULL)
    {
        LOGW("%s, %d, prompt_tone_play already close\n", __func__, __LINE__);
        return BK_OK;
    }

    LOGI("%s\n", __func__);

    if (wait_play_finish)
    {
        if (handle->play_finish_sem)
        {
            rtos_get_semaphore(&handle->play_finish_sem, BEKEN_NEVER_TIMEOUT);
        }
    }

    if (handle->play_finish_sem)
    {
        rtos_deinit_semaphore(&handle->play_finish_sem);
        handle->play_finish_sem = NULL;
    }

    if (handle->source)
    {
        audio_source_close(handle->source);
    }

    if (handle->codec)
    {
        audio_codec_close(handle->codec);
    }

    aud_tras_drv_register_prompt_tone_pool_empty_notify(NULL, NULL);

    handle->status = PROMPT_TONE_PLAY_STA_NULL;

    return BK_OK;
}

bk_err_t prompt_tone_play_set_url(prompt_tone_play_handle_t handle, url_info_t *url_info)
{
    if (!handle || !url_info || !url_info->url)
    {
        LOGE("%s, %d, handle: %p, url_info: %p, url_info->url: %p\n", __func__, __LINE__, handle, url_info, url_info->url);
        return BK_FAIL;
    }

    if (handle->status == PROMPT_TONE_PLAY_STA_NULL)
    {
        LOGE("%s, %d, prompt_tone_play not open\n", __func__, __LINE__);
        return BK_FAIL;
    }

    LOGD("%s\n", __func__);

    if (handle->source)
    {
        return audio_source_set_url(handle->source, url_info);
    }
    else
    {
        return BK_FAIL;
    }
}

bk_err_t prompt_tone_play_start(prompt_tone_play_handle_t handle)
{
    bk_err_t ret = BK_OK;

    if (!handle)
    {
        LOGE("%s, %d, handle is null\n", __func__, __LINE__, handle);
        return BK_FAIL;
    }

    if (handle->status == PROMPT_TONE_PLAY_STA_NULL)
    {
        LOGE("%s, %d, prompt_tone_play not open\n", __func__, __LINE__);
        return BK_FAIL;
    }

    if (handle->status == PROMPT_TONE_PLAY_STA_PLAYING)
    {
        LOGW("%s, %d, prompt_tone_play already start\n", __func__, __LINE__);
        return BK_OK;
    }

    LOGI("%s\n", __func__);

    if (handle->codec)
    {
        ret = audio_codec_ctrl(handle->codec, AUDIO_CODEC_CTRL_START, NULL);
        if (ret != BK_OK)
        {
            LOGE("%s, %d, audio codec stop fail\n", __func__, __LINE__);
            return BK_FAIL;
        }
    }

    if (handle->source)
    {
        ret = audio_source_ctrl(handle->source, AUDIO_SOURCE_CTRL_START, NULL);
        if (ret != BK_OK)
        {
            LOGE("%s, %d, audio source stop fail\n", __func__, __LINE__);
            goto fail;
        }
    }

    handle->status = PROMPT_TONE_PLAY_STA_PLAYING;

    return BK_OK;

fail:
    audio_codec_ctrl(handle->codec, AUDIO_CODEC_CTRL_STOP, NULL);

    return BK_FAIL;
}

bk_err_t prompt_tone_play_stop(prompt_tone_play_handle_t handle)
{
    bk_err_t ret = BK_OK;
    bk_err_t err = BK_OK;

    if (!handle)
    {
        LOGE("%s, %d, handle is null\n", __func__, __LINE__, handle);
        return BK_FAIL;
    }

    if (handle->status == PROMPT_TONE_PLAY_STA_NULL)
    {
        LOGE("%s, %d, prompt_tone_play not open\n", __func__, __LINE__);
        return BK_FAIL;
    }

    if (handle->status == PROMPT_TONE_PLAY_STA_IDLE)
    {
        LOGW("%s, %d, prompt_tone_play already stop\n", __func__, __LINE__);
        return BK_OK;
    }

    LOGI("%s\n", __func__);

    /* reset speaker source type to voice */
    aud_tras_drv_voc_set_spk_source_type(SPK_SOURCE_TYPE_VOICE);

    if (handle->source)
    {
        ret = audio_source_ctrl(handle->source, AUDIO_SOURCE_CTRL_STOP, NULL);
        if (ret != BK_OK)
        {
            LOGE("%s, %d, audio source stop fail\n", __func__, __LINE__);
            err = BK_FAIL;
        }
    }

    if (handle->codec)
    {
        ret = audio_codec_ctrl(handle->codec, AUDIO_CODEC_CTRL_STOP, NULL);
        if (ret != BK_OK)
        {
            LOGE("%s, %d, audio codec stop fail\n", __func__, __LINE__);
            err = BK_FAIL;
        }

        ret = audio_codec_ctrl(handle->codec, AUDIO_CODEC_CTRL_CLEAR_POOL, NULL);
        if (ret != BK_OK)
        {
            LOGE("%s, %d, audio codec clear rx pool fail\n", __func__, __LINE__);
            err = BK_FAIL;
        }
    }

    handle->status = PROMPT_TONE_PLAY_STA_IDLE;

    return err;
}

