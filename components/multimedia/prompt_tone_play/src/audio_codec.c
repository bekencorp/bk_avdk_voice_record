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

#include "audio_codec.h"
#if CONFIG_PROMPT_TONE_CODEC_MP3
#include "mp3_codec.h"
#endif

#if CONFIG_PROMPT_TONE_CODEC_PCM
#include "pcm_codec.h"
#endif

#if CONFIG_PROMPT_TONE_CODEC_WAV
#include "wav_codec.h"
#endif

#define AUDIO_CODEC_TAG "aud_codec"

#define LOGI(...) BK_LOGI(AUDIO_CODEC_TAG, ##__VA_ARGS__)
#define LOGW(...) BK_LOGW(AUDIO_CODEC_TAG, ##__VA_ARGS__)
#define LOGE(...) BK_LOGE(AUDIO_CODEC_TAG, ##__VA_ARGS__)
#define LOGD(...) BK_LOGD(AUDIO_CODEC_TAG, ##__VA_ARGS__)


audio_codec_t *audio_codec_create(  audio_codec_type_t codec_type, audio_codec_cfg_t *config)
{
    audio_codec_ops_t *temp_ops = NULL;

    if (!config)
    {
        return NULL;
    }

    /* check whether codec type support */
    switch (codec_type)
    {
#if CONFIG_PROMPT_TONE_CODEC_MP3
        case AUDIO_CODEC_MP3:
            temp_ops = get_mp3_codec_ops();
            break;
#endif

#if CONFIG_PROMPT_TONE_CODEC_PCM
        case AUDIO_CODEC_PCM:
            temp_ops = get_pcm_codec_ops();
            break;
#endif

#if CONFIG_PROMPT_TONE_CODEC_WAV
        case AUDIO_CODEC_WAV:
            temp_ops = get_wav_codec_ops();
            break;
#endif

        default:
            break;
    }

    /* check codec config */
    //TODO

    if (!temp_ops)
    {
        LOGE("%s, %d, codec_type: %d not support\n", __func__, __LINE__, codec_type);
        return NULL;
    }

    audio_codec_t *codec = psram_malloc(sizeof(audio_codec_t));
    if (!codec)
    {
        LOGE("%s, %d, os_malloc codec_handle: %d fail\n", __func__, __LINE__, sizeof(audio_codec_t));
        return NULL;
    }
    os_memset(codec, 0, sizeof(audio_codec_t));

    codec->ops = temp_ops;
    os_memcpy(&codec->config, config, sizeof(audio_codec_cfg_t));

    return codec;
}

bk_err_t audio_codec_destroy(audio_codec_t *codec)
{
    bk_err_t ret = BK_OK;

    if (!codec)
    {
        return BK_OK;
    }

    ret = audio_codec_close(codec);
    if (ret != BK_OK)
    {
        LOGE("%s, %d, audio_codec_close fail, ret: %d\n", __func__, __LINE__, ret);
    }

    psram_free(codec);

    return BK_OK;
}

bk_err_t audio_codec_open(audio_codec_t *codec)
{
    bk_err_t ret = BK_OK;

    if (!codec)
    {
        LOGE("%s, %d, param error, codec: %p\n", __func__, __LINE__, codec);
        return BK_FAIL;
    }

    ret = codec->ops->open(codec, &codec->config);
    if (ret != BK_OK)
    {
        LOGE("%s, %d, audio_codec_open fail, ret: %d\n", __func__, __LINE__, ret);
        return BK_FAIL;
    }

    return BK_OK;
}

bk_err_t audio_codec_close(audio_codec_t *codec)
{
    bk_err_t ret = BK_OK;

    if (!codec)
    {
        LOGE("%s, %d, param error, codec: %p\n", __func__, __LINE__, codec);
        return BK_FAIL;
    }

    ret = codec->ops->close(codec);
    if (ret != BK_OK)
    {
        LOGE("%s, %d, audio_codec_close fail, ret: %d\n", __func__, __LINE__, ret);
        return BK_FAIL;
    }

    return BK_OK;
}

bk_err_t audio_codec_write_data(audio_codec_t *codec, char *buffer, uint32_t len)
{
    if (!codec || !buffer || !len)
    {
        LOGE("%s, %d, params error, codec: %p, buffer: %p, len: %d\n", __func__, __LINE__, codec, buffer, len);
        return BK_FAIL;
    }
    LOGD("%s, %d, len: %d\n", __func__, __LINE__, len);
    return codec->ops->write(codec, buffer, len);
}

bk_err_t audio_codec_ctrl(audio_codec_t *codec, audio_codec_ctrl_op_t op, void *params)
{
    bk_err_t ret = BK_OK;

    if (!codec)
    {
        LOGE("%s, %d, param error, codec: %p\n", __func__, __LINE__, codec);
        return BK_FAIL;
    }

    ret = codec->ops->ctrl(codec, op, params);
    if (ret != BK_OK)
    {
        LOGE("%s, %d, op: %d fail, ret: %d\n", __func__, __LINE__, op, ret);
        return BK_FAIL;
    }

    return BK_OK;
}

#if 0
bk_err_t audio_codec_get_frame_info(audio_codec_t *codec)
{
    bk_err_t ret = BK_OK;

    if (!codec)
    {
        LOGE("%s, %d, param error, codec: %p\n", __func__, __LINE__, codec);
        return BK_FAIL;
    }

    ret = codec->ops->get_frame_info(codec);
    if (ret != BK_OK)
    {
        LOGE("%s, %d, audio_codec_get_frame_info fail, ret: %d\n", __func__, __LINE__, ret);
        return BK_FAIL;
    }

    return BK_OK;
}
#endif

