// Copyright 2025-2026 Beken
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

#ifndef __PROMPT_TONE_PLAY_H__
#define __PROMPT_TONE_PLAY_H__


#ifdef __cplusplus
extern "C" {
#endif

#include "audio_source.h"
#include "audio_codec.h"
#if CONFIG_AUD_INTF_SUPPORT_PROMPT_TONE_RESAMPLE
#include <modules/audio_rsp_types.h>
#include <modules/audio_rsp.h>
#endif

#define PROMPT_TONE_SRC_SAMP_RATE (CONFIG_AUD_INTF_PROMPT_TONE_SAMPLE_RATE)
#define SAMP_CNT_20MS (PROMPT_TONE_SRC_SAMP_RATE*20/1000*2) //16bits
#define MAX_SAMP_CNT_20MS (48000*20/1000*2) //16bits

typedef struct
{
    audio_source_type_t source_type;
    audio_source_cfg_t source_cfg;
    audio_codec_type_t codec_type;
    audio_codec_cfg_t codec_cfg;
#if CONFIG_AUD_INTF_SUPPORT_PROMPT_TONE_RESAMPLE
    aud_rsp_cfg_t rsp_cfg;
    uint8_t *rsp_out_buff;
#endif
} prompt_tone_play_cfg_t;

typedef enum
{
    PROMPT_TONE_PLAY_STA_NULL = 0,
    PROMPT_TONE_PLAY_STA_IDLE,
    PROMPT_TONE_PLAY_STA_PLAYING,
    PROMPT_TONE_PLAY_STA_MAX,
} prompt_tone_play_sta_t;

struct prompt_tone_play
{
    audio_source_t *source;
    audio_codec_t *codec;
    prompt_tone_play_cfg_t config;
    beken_semaphore_t play_finish_sem;
    prompt_tone_play_sta_t status;
};

typedef struct prompt_tone_play *prompt_tone_play_handle_t;

#define DEFAULT_ARRAY_PCM_PROMPT_TONE_PLAY_CONFIG() {   \
    .source_type = AUDIO_SOURCE_ARRAY,                  \
    .source_cfg = {                                     \
        .url = NULL,                                    \
        .frame_size = DEFAULT_FRAME_SIZE,               \
        .data_handle = NULL,                            \
        .notify = NULL,                                 \
        .usr_data = NULL,                               \
    },                                                  \
    .codec_type = AUDIO_CODEC_PCM,                      \
    .codec_cfg = {                                      \
        .chunk_size = DEFAULT_CHUNK_SIZE,               \
        .pool_size = DEFAULT_POOL_SIZE,                 \
        .data_handle = NULL,                            \
        .empty_cb = NULL,                               \
        .usr_data = NULL,                               \
    },                                                  \
}

#define DEFAULT_VFS_PCM_PROMPT_TONE_PLAY_CONFIG() {     \
    .source_type = AUDIO_SOURCE_VFS,                    \
    .source_cfg = {                                     \
        .url = NULL,                                    \
        .frame_size = DEFAULT_FRAME_SIZE,               \
        .data_handle = NULL,                            \
        .notify = NULL,                                 \
        .usr_data = NULL,                               \
    },                                                  \
    .codec_type = AUDIO_CODEC_PCM,                      \
    .codec_cfg = {                                      \
        .chunk_size = DEFAULT_CHUNK_SIZE,               \
        .pool_size = DEFAULT_POOL_SIZE,                 \
        .data_handle = NULL,                            \
        .empty_cb = NULL,                               \
        .usr_data = NULL,                               \
    },                                                  \
}

#define DEFAULT_VFS_WAV_PROMPT_TONE_PLAY_CONFIG() {     \
    .source_type = AUDIO_SOURCE_VFS,                    \
    .source_cfg = {                                     \
        .url = NULL,                                    \
        .frame_size = DEFAULT_FRAME_SIZE,               \
        .data_handle = NULL,                            \
        .notify = NULL,                                 \
        .usr_data = NULL,                               \
    },                                                  \
    .codec_type = AUDIO_CODEC_WAV,                      \
    .codec_cfg = {                                      \
        .chunk_size = DEFAULT_CHUNK_SIZE,               \
        .pool_size = DEFAULT_POOL_SIZE,                 \
        .data_handle = NULL,                            \
        .empty_cb = NULL,                               \
        .usr_data = NULL,                               \
    },                                                  \
}

#define DEFAULT_VFS_MP3_PROMPT_TONE_PLAY_CONFIG() {     \
    .source_type = AUDIO_SOURCE_VFS,                    \
    .source_cfg = {                                     \
        .url = NULL,                                    \
        .frame_size = DEFAULT_FRAME_SIZE,               \
        .data_handle = NULL,                            \
        .notify = NULL,                                 \
        .usr_data = NULL,                               \
    },                                                  \
    .codec_type = AUDIO_CODEC_MP3,                      \
    .codec_cfg = {                                      \
        .chunk_size = DEFAULT_CHUNK_SIZE,               \
        .pool_size = DEFAULT_POOL_SIZE,                 \
        .data_handle = NULL,                            \
        .empty_cb = NULL,                               \
        .usr_data = NULL,                               \
    },                                                  \
}

/**
 * @brief     Create prompt tone player with config
 *
 * This API create prompt tone player handle according to config.
 * This API should be called before other api.
 *
 * @param[in] config    Prompt tone play config
 *
 * @return
 *    - Not NULL: success
 *    - NULL: failed
 */
prompt_tone_play_handle_t prompt_tone_play_create(  prompt_tone_play_cfg_t *config);

/**
 * @brief      Destroy prompt tone player
 *
 * This API Destroy prompt tone player according to prompt tone player handle.
 *
 *
 * @param[in] handle    The prompt tone player handle
 *
 * @return
 *    - BK_OK: success
 *    - Others: failed
 */
bk_err_t prompt_tone_play_destroy(prompt_tone_play_handle_t handle);

/**
 * @brief      Open prompt tone player
 *
 * This API open prompt tone player.
 *
 *
 * @param[in] handle    The prompt tone player handle
 *
 * @return
 *    - BK_OK: success
 *    - Others: failed
 */
bk_err_t prompt_tone_play_open(prompt_tone_play_handle_t handle);

/**
 * @brief      Close prompt tone player
 *
 * This API close prompt tone player
 *
 *
 * @param[in] handle            The prompt tone player handle
 * @param[in] wait_play_finish  The flag to declare whether waiting play finish
 *
 * @return
 *    - BK_OK: success
 *    - Others: failed
 */
bk_err_t prompt_tone_play_close(prompt_tone_play_handle_t handle, bool wait_play_finish);

/**
 * @brief      Set url information of prompt tone
 *
 * This API set url information of prompt tone
 *
 *
 * @param[in] handle            The prompt tone player handle
 * @param[in] wait_play_finish  The flag to declare whether waiting play finish
 *
 * @return
 *    - BK_OK: success
 *    - Others: failed
 */
bk_err_t prompt_tone_play_set_url(prompt_tone_play_handle_t handle, url_info_t *url_info);

/**
 * @brief      Start playing prompt tone
 *
 * This API start playing prompt tone
 *
 *
 * @param[in] handle    The prompt tone player handle
 *
 * @return
 *    - BK_OK: success
 *    - Others: failed
 */
bk_err_t prompt_tone_play_start(prompt_tone_play_handle_t handle);

/**
 * @brief      Stop playing prompt tone
 *
 * This API stop playing prompt tone
 *
 *
 * @param[in] handle    The prompt tone player handle
 *
 * @return
 *    - BK_OK: success
 *    - Others: failed
 */
bk_err_t prompt_tone_play_stop(prompt_tone_play_handle_t handle);


#ifdef __cplusplus
}
#endif
#endif /* __PROMPT_TONE_PLAY_H__ */

