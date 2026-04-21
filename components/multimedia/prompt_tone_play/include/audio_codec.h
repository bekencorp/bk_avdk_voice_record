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


#ifndef __AUDIO_CODEC_H__
#define __AUDIO_CODEC_H__

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
    AUDIO_CODEC_STA_IDLE = 0,
    AUDIO_CODEC_STA_RUNNING,
    AUDIO_CODEC_STA_PAUSED,
} audio_codec_sta_t;

typedef enum
{
    AUDIO_CODEC_UNKNOWN = 0,
    AUDIO_CODEC_MP3 = 1,
    AUDIO_CODEC_PCM = 2,
    AUDIO_CODEC_WAV = 3,
} audio_codec_type_t;

typedef enum
{
    AUDIO_CODEC_CTRL_START = 0,
    AUDIO_CODEC_CTRL_STOP,
    AUDIO_CODEC_CTRL_CLEAR_POOL,
    AUDIO_CODEC_CTRL_MAX,
} audio_codec_ctrl_op_t;

typedef struct audio_info_s
{
    int channel_number;
    int sample_rate;
    int sample_bits;
} audio_frame_info_t;


typedef struct audio_codec audio_codec_t;

typedef int (*codec_out_data_handle)(audio_frame_info_t *frame_info, char *buffer, uint32_t len, void *params);
typedef int (*data_empty_cb)(audio_codec_t *codec);


typedef struct
{
    uint32_t chunk_size;             /*!< the size (unit byte) of ringbuffer pool saved data need to codec */
    uint32_t pool_size;
    codec_out_data_handle data_handle;
    data_empty_cb empty_cb;
    void *usr_data;
} audio_codec_cfg_t;

#define DEFAULT_CHUNK_SIZE      (4608)
#define DEFAULT_POOL_SIZE       (1940*2)

#define DEFAULT_AUDIO_CODEC_CONFIG() {      \
    .chunk_size = DEFAULT_CHUNK_SIZE,       \
    .pool_size = DEFAULT_POOL_SIZE,         \
    .data_handle = NULL,                    \
    .data_empty_cb = NULL,                  \
    .usr_data = NULL,                       \
}


typedef struct
{
    int (*open)(audio_codec_t *codec, audio_codec_cfg_t *config);
    //int (*get_frame_info)(audio_codec_t *codec);
    int (*write)(audio_codec_t *codec, char *buffer, uint32_t len);
    int (*close)(audio_codec_t *codec);
    int (*ctrl)(audio_codec_t *codec, audio_codec_ctrl_op_t op, void *params);
} audio_codec_ops_t;

struct audio_codec
{
    audio_codec_ops_t *ops;

    audio_codec_cfg_t config;

    void *codec_ctx;
};



/**
 * @brief     Create audio decoder with config
 *
 * This API create audio decoder handle according to codec type and config.
 * This API should be called before other api.
 *
 * @param[in] codec_type    The type of play
 * @param[in] config        Play config used in audio_play_open api
 *
 * @return
 *    - Not NULL: success
 *    - NULL: failed
 */
audio_codec_t *audio_codec_create(  audio_codec_type_t codec_type, audio_codec_cfg_t *config);

/**
 * @brief      Destroy audio decoder
 *
 * This API Destroy audio decoder according to audio decoder handle.
 *
 *
 * @param[in] codec  The audio decoder handle
 *
 * @return
 *    - BK_OK: success
 *    - Others: failed
 */
bk_err_t audio_codec_destroy(audio_codec_t *codec);

/**
 * @brief      Open audio decoder
 *
 * This API open audio decoder.
 *
 *
 * @param[in] codec  The audio decoder handle
 *
 * @return
 *    - BK_OK: success
 *    - Others: failed
 */
bk_err_t audio_codec_open(audio_codec_t *codec);

/**
 * @brief      Close audio decoder
 *
 * This API stop decode and close audio decoder.
 *
 *
 * @param[in] codec  The audio decoder handle
 *
 * @return
 *    - BK_OK: success
 *    - Others: failed
 */
bk_err_t audio_codec_close(audio_codec_t *codec);

/**
 * @brief      Write data to audio decoder
 *
 * This API write pcm data to pool.
 * If memory in pool is not enough, wait until the pool has enough memory.
 *
 *
 * @param[in] codec     The audio decoder handle
 * @param[in] buffer    The data buffer
 * @param[in] len       The length (byte) of data
 *
 * @return
 *    - > 0: size of write success
 *    - Others: failed
 */
bk_err_t audio_codec_write_data(audio_codec_t *codec, char *buffer, uint32_t len);

/**
 * @brief      Control audio decoder
 *
 * This API control audio decoder to start or stop work.
 *
 *
 * @param[in] codec     The audio decoder handle
 * @param[in] op        The opcode
 * @param[in] params    The parameters
 *
 * @return
 *    - BK_OK: success
 *    - Others: failed
 */
bk_err_t audio_codec_ctrl(audio_codec_t *codec, audio_codec_ctrl_op_t op, void *params);


/**
 * @brief      get audio decoder frame information
 *
 * @param[in] codec     The audio decoder handle
 *
 * @return
 *    - BK_OK: success
 *    - Others: failed
 */
//bk_err_t audio_codec_get_frame_info(audio_codec_t *codec);

#ifdef __cplusplus
}
#endif
#endif /* __AUDIO_CODEC_H__ */

