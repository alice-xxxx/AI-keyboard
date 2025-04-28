#pragma once
#include "esp_err.h"
#include <stdbool.h>

#define MIN_AUDIO_INPUT_LENGTH  (16000 * 2 * 600 / 1000)
#define MAX_AUDIO_INPUT_LENGTH  (16000 * 2 * 8 )  // 8 seconds audio
#define I2S_CHANNEL_NUM         (1)
#define TOTAL_NUM               (2)
#define AUDIO_STOP_BIT          BIT0
#define AUDIO_CHAT_BIT          BIT1
#define MODE_CHAT               (1)
#ifdef __cplusplus
extern "C"
{
#endif

    typedef enum {
        AUDIO_WAKENET_END = 0,
        AUDIO_WAKENET_START,
        AUDIO_VAD_END,
        AUDIO_VAD_START,
        AUDIO_VAD_WAIT,
        AUDIO_PLAY_MUYU,
    } audio_record_state_t;

    typedef struct {
        size_t len;
        uint8_t *wav;
    } audio_data_t;

    esp_err_t app_sr_init(void);



#ifdef __cplusplus
}
#endif


