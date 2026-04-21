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
#include "wav_codec.h"
#include <modules/wav_head.h>


#define WAV_CODEC_TAG "wav_dec"
#define LOGI(...) BK_LOGI(WAV_CODEC_TAG, ##__VA_ARGS__)
#define LOGW(...) BK_LOGW(WAV_CODEC_TAG, ##__VA_ARGS__)
#define LOGE(...) BK_LOGE(WAV_CODEC_TAG, ##__VA_ARGS__)
#define LOGD(...) BK_LOGD(WAV_CODEC_TAG, ##__VA_ARGS__)


#define WAV_CODEC_CHECK_NULL(ptr) do {\
        if (ptr == NULL) {\
            LOGE("WAV_CODEC_CHECK_NULL fail \n");\
            return BK_FAIL;\
        }\
    } while(0)


typedef struct
{
    wav_info_t info;
    bool head_parse;

    audio_codec_cfg_t config;
} wav_codec_priv_t;


static int check_wav_head(wav_codec_priv_t *codec_priv, uint8_t *in_data, uint32_t len)
{
    WAV_CODEC_CHECK_NULL(codec_priv);

    LOGI("%s\n", __func__);

    codec_priv->head_parse = true;

    if (len < 44)
    {
        LOGE("%s, %d, wav head len: %d < 44 \n", __func__, __LINE__, len);
        return BK_FAIL;
    }

    if (BK_OK != wav_check_type(in_data, len))
    {
        LOGE("%s, %d, check wav head fail \n", __func__, __LINE__);
        return BK_FAIL;
    }

    if (BK_OK != wav_head_parse((wav_header_t *)in_data, &codec_priv->info))
    {
        LOGE("%s, %d, parse wav head fail \n", __func__, __LINE__);
        return BK_FAIL;
    }

    /* check whether audio format support */
    if (codec_priv->info.bits != 16 || codec_priv->info.channels != 1 || codec_priv->info.samplerate != 16000)
    {
        LOGE("%s, %d, audio format not support, sample_rate: %d, chl_num: %d, bits: %d\n", __func__, __LINE__, codec_priv->info.samplerate, codec_priv->info.channels, codec_priv->info.samplerate);
    }

    return BK_OK;
}

static int wav_codec_open(audio_codec_t *codec, audio_codec_cfg_t *config)
{
    wav_codec_priv_t *temp_wav_codec = NULL;

    if (!codec || !config)
    {
        LOGE("%s, %d, params error, codec: %p, config: %p\n", __func__, __LINE__, codec, config);
        return BK_FAIL;
    }

    LOGI("%s\n", __func__);

    wav_codec_priv_t *wav_codec = (wav_codec_priv_t *)codec->codec_ctx;
    if (wav_codec != NULL)
    {
        LOGE("%s, %d, wav_codec: %p already open\n", __func__, __LINE__, wav_codec);
        goto fail;
    }

    temp_wav_codec = psram_malloc(sizeof(wav_codec_priv_t));
    if (!temp_wav_codec)
    {
        LOGE("%s, %d, os_malloc temp_wav_codec: %d fail\n", __func__, __LINE__, sizeof(wav_codec_priv_t));
        goto fail;
    }

    os_memset(temp_wav_codec, 0, sizeof(wav_codec_priv_t));

    codec->codec_ctx = temp_wav_codec;

    os_memcpy(&temp_wav_codec->config, config, sizeof(audio_codec_cfg_t));

    LOGD("wav codec open complete\n");

    return BK_OK;

fail:

    if (codec->codec_ctx)
    {
        psram_free(codec->codec_ctx);
        codec->codec_ctx = NULL;
    }

    return BK_FAIL;
}

static int wav_codec_close(audio_codec_t *codec)
{
    WAV_CODEC_CHECK_NULL(codec);
    wav_codec_priv_t *wav_codec = (wav_codec_priv_t *)codec->codec_ctx;

    if (!wav_codec)
    {
        LOGD("%s, %d, wav codec already close\n", __func__, __LINE__);
        return BK_OK;
    }

    LOGI("%s \n", __func__);

    if (wav_codec)
    {
        psram_free(wav_codec);
        codec->codec_ctx = NULL;
    }

    LOGD("wav codec close complete\n");
    return BK_OK;
}

static int wav_codec_write(audio_codec_t *codec, char *buffer, uint32_t len)
{
    if (!codec || !buffer || !len)
    {
        LOGE("%s, %d, params error, codec: %p, buffer: %p, len: %d\n", __func__, __LINE__, codec, buffer, len);
        return BK_FAIL;
    }

    wav_codec_priv_t *priv = (wav_codec_priv_t *)codec->codec_ctx;
    WAV_CODEC_CHECK_NULL(priv);

    //uint32_t need_w_len = len;
    int need_w_len = (int)len;

    if (!priv->head_parse)
    {
        if (BK_OK != check_wav_head(priv, (uint8_t *)buffer, len))
        {
            //notify app audio not support
            //TODO
            return BK_FAIL;
        }
        /* skip header */
        need_w_len -= 44;
    }

    LOGD("%s, %d, need_w_len:%d,len:%d\n", __func__, __LINE__, need_w_len,len);

    while (need_w_len > 0 && priv->config.data_handle)
    {
        int w_len = priv->config.data_handle(NULL, buffer + len - need_w_len, need_w_len, priv->config.usr_data);
        if (w_len > 0)
        {
            need_w_len -= w_len;
        }
        else
        {
            LOGE("%s, %d, write pcm data FAIL, w_len: %d \n", __func__, __LINE__, w_len);
            return BK_FAIL;
        }
    }

    return len;
}

static int wav_codec_ctrl(audio_codec_t *codec, audio_codec_ctrl_op_t op, void *params)
{
    if (!codec)
    {
        LOGE("%s, %d, codec: %p is null\n", __func__, __LINE__, codec);
        return BK_FAIL;
    }

    wav_codec_priv_t *priv = (wav_codec_priv_t *)codec->codec_ctx;
    WAV_CODEC_CHECK_NULL(priv);

    bk_err_t ret = BK_OK;

    LOGD("%s, op: %d \n", __func__, op);

    switch (op)
    {
        case AUDIO_CODEC_CTRL_START:
            //nothing todo
            break;

        case AUDIO_CODEC_CTRL_STOP:
            /* Notify codec wav decode complete. Need parse header when receive next file. */
            priv->head_parse = false;
            LOGD("%s, head_parse = false\n", __func__);
            break;

        case AUDIO_CODEC_CTRL_CLEAR_POOL:
            //nothing todo
            break;

        default:
            ret = BK_FAIL;
            break;
    }

    return ret;
}


audio_codec_ops_t wav_codec_ops =
{
    .open = wav_codec_open,
    .write = wav_codec_write,
    .close = wav_codec_close,
    .ctrl = wav_codec_ctrl,
};

audio_codec_ops_t *get_wav_codec_ops(void)
{
    return &wav_codec_ops;
}

