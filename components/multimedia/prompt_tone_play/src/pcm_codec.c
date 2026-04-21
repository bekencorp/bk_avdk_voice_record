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
#include "pcm_codec.h"


#define PCM_CODEC_TAG "pcm_dec"
#define LOGI(...) BK_LOGI(PCM_CODEC_TAG, ##__VA_ARGS__)
#define LOGW(...) BK_LOGW(PCM_CODEC_TAG, ##__VA_ARGS__)
#define LOGE(...) BK_LOGE(PCM_CODEC_TAG, ##__VA_ARGS__)
#define LOGD(...) BK_LOGD(PCM_CODEC_TAG, ##__VA_ARGS__)


#define PCM_CODEC_CHECK_NULL(ptr) do {\
        if (ptr == NULL) {\
            LOGE("PCM_CODEC_CHECK_NULL fail \n");\
            return BK_FAIL;\
        }\
    } while(0)


typedef struct
{
    audio_codec_cfg_t config;
} pcm_codec_priv_t;


static int pcm_codec_open(audio_codec_t *codec, audio_codec_cfg_t *config)
{
    pcm_codec_priv_t *temp_pcm_codec = NULL;

    if (!codec || !config)
    {
        LOGE("%s, %d, params error, codec: %p, config: %p\n", __func__, __LINE__, codec, config);
        return BK_FAIL;
    }

    LOGI("%s\n", __func__);

    pcm_codec_priv_t *pcm_codec = (pcm_codec_priv_t *)codec->codec_ctx;
    if (pcm_codec != NULL)
    {
        LOGE("%s, %d, pcm_codec: %p already open\n", __func__, __LINE__, pcm_codec);
        goto fail;
    }

    temp_pcm_codec = psram_malloc(sizeof(pcm_codec_priv_t));
    if (!temp_pcm_codec)
    {
        LOGE("%s, %d, os_malloc temp_pcm_codec: %d fail\n", __func__, __LINE__, sizeof(pcm_codec_priv_t));
        goto fail;
    }

    os_memset(temp_pcm_codec, 0, sizeof(pcm_codec_priv_t));

    codec->codec_ctx = temp_pcm_codec;

    os_memcpy(&temp_pcm_codec->config, config, sizeof(audio_codec_cfg_t));

    LOGD("pcm codec open complete\n");

    return BK_OK;

fail:

    if (codec->codec_ctx)
    {
        psram_free(codec->codec_ctx);
        codec->codec_ctx = NULL;
    }

    return BK_FAIL;
}

static int pcm_codec_close(audio_codec_t *codec)
{
    PCM_CODEC_CHECK_NULL(codec);
    pcm_codec_priv_t *pcm_codec = (pcm_codec_priv_t *)codec->codec_ctx;

    if (!pcm_codec)
    {
        LOGD("%s, %d, pcm codec already close\n", __func__, __LINE__);
        return BK_OK;
    }

    LOGI("%s \n", __func__);

    if (pcm_codec)
    {
        psram_free(pcm_codec);
        codec->codec_ctx = NULL;
    }

    LOGD("pcm codec close complete\n");
    return BK_OK;
}

static int pcm_codec_write(audio_codec_t *codec, char *buffer, uint32_t len)
{
    if (!codec || !buffer || !len)
    {
        LOGE("%s, %d, params error, codec: %p, buffer: %p, len: %d\n", __func__, __LINE__, codec, buffer, len);
        return BK_FAIL;
    }

    pcm_codec_priv_t *priv = (pcm_codec_priv_t *)codec->codec_ctx;
    PCM_CODEC_CHECK_NULL(priv);

    //uint32_t need_w_len = len;
    int need_w_len = (int)len;

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

static int pcm_codec_ctrl(audio_codec_t *codec, audio_codec_ctrl_op_t op, void *params)
{
    if (!codec)
    {
        LOGE("%s, %d, codec: %p is null\n", __func__, __LINE__, codec);
        return BK_FAIL;
    }

    pcm_codec_priv_t *priv = (pcm_codec_priv_t *)codec->codec_ctx;
    PCM_CODEC_CHECK_NULL(priv);

    bk_err_t ret = BK_OK;

    LOGD("%s, op: %d \n", __func__, op);

    switch (op)
    {
        case AUDIO_CODEC_CTRL_START:
            //nothing todo
            break;

        case AUDIO_CODEC_CTRL_STOP:
            //nothing todo
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


audio_codec_ops_t pcm_codec_ops =
{
    .open =           pcm_codec_open,
    .write =          pcm_codec_write,
    .close =          pcm_codec_close,
    .ctrl =           pcm_codec_ctrl,
};

audio_codec_ops_t *get_pcm_codec_ops(void)
{
    return &pcm_codec_ops;
}

