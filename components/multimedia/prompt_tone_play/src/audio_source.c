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
#include "audio_source.h"

#if CONFIG_PROMPT_TONE_SOURCE_ARRAY
#include "source_array.h"
#endif

#if CONFIG_PROMPT_TONE_SOURCE_VFS
#include "source_vfs.h"
#endif

#define AUDIO_SOURCE_TAG "aud_src"

#define LOGI(...) BK_LOGI(AUDIO_SOURCE_TAG, ##__VA_ARGS__)
#define LOGW(...) BK_LOGW(AUDIO_SOURCE_TAG, ##__VA_ARGS__)
#define LOGE(...) BK_LOGE(AUDIO_SOURCE_TAG, ##__VA_ARGS__)
#define LOGD(...) BK_LOGD(AUDIO_SOURCE_TAG, ##__VA_ARGS__)


audio_source_t *audio_source_create(  audio_source_type_t source_type, audio_source_cfg_t *config)
{
    audio_source_ops_t *temp_ops = NULL;

    if (!config)
    {
        return NULL;
    }

    /* check whether source type support */
    switch (source_type)
    {
#if CONFIG_PROMPT_TONE_SOURCE_ARRAY
        case AUDIO_SOURCE_ARRAY:
            temp_ops = get_array_source_ops();
            break;
#endif

#if CONFIG_PROMPT_TONE_SOURCE_VFS
        case AUDIO_SOURCE_VFS:
            temp_ops = get_vfs_source_ops();
            break;
#endif

        default:
            break;
    }

    /* check codec config */
    //TODO

    if (!temp_ops)
    {
        LOGE("%s, %d, source_type: %d not support\n", __func__, __LINE__, source_type);
        return NULL;
    }

    audio_source_t *source = psram_malloc(sizeof(audio_source_t));
    if (!source)
    {
        LOGE("%s, %d, os_malloc source_handle: %d fail\n", __func__, __LINE__, sizeof(audio_source_t));
        return NULL;
    }
    os_memset(source, 0, sizeof(audio_source_t));

    source->ops = temp_ops;
    os_memcpy(&source->config, config, sizeof(audio_source_cfg_t));

    return source;
}

bk_err_t audio_source_destroy(audio_source_t *source)
{
    bk_err_t ret = BK_OK;

    if (!source)
    {
        return BK_OK;
    }

    ret = audio_source_close(source);
    if (ret != BK_OK)
    {
        LOGE("%s, %d, audio_source_close fail, ret: %d\n", __func__, __LINE__, ret);
    }

    psram_free(source);

    return BK_OK;
}

bk_err_t audio_source_open(audio_source_t *source)
{
    bk_err_t ret = BK_OK;

    if (!source)
    {
        LOGE("%s, %d, param error, source: %p\n", __func__, __LINE__, source);
        return BK_FAIL;
    }

    ret = source->ops->audio_source_open(source, &source->config);
    if (ret != BK_OK)
    {
        LOGE("%s, %d, audio_source_open fail, ret: %d\n", __func__, __LINE__, ret);
        return BK_FAIL;
    }

    return BK_OK;
}

bk_err_t audio_source_close(audio_source_t *source)
{
    bk_err_t ret = BK_OK;

    if (!source)
    {
        LOGE("%s, %d, param error, source: %p\n", __func__, __LINE__, source);
        return BK_FAIL;
    }

    ret = source->ops->audio_source_close(source);
    if (ret != BK_OK)
    {
        LOGE("%s, %d, audio_source_close fail, ret: %d\n", __func__, __LINE__, ret);
        return BK_FAIL;
    }

    return BK_OK;
}

int audio_source_seek(audio_source_t *source, int offset, uint32_t whence)
{
    if (!source || !source->ops->audio_source_seek)
    {
        return BK_FAIL;
    }

    return source->ops->audio_source_seek(source, offset, whence);
}

int audio_source_set_url(audio_source_t *source, url_info_t *url_info)
{
    if (!source || !source->ops->audio_source_set_url)
    {
        return BK_FAIL;
    }

    return source->ops->audio_source_set_url(source, url_info);
}

int audio_source_ctrl(audio_source_t *source, audio_source_ctrl_op_t op, void *params)
{
    if (!source || !source->ops->audio_source_ctrl)
    {
        return BK_FAIL;
    }

    return source->ops->audio_source_ctrl(source, op, params);
}

