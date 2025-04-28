#pragma once

#include "esp_err.h"

#define CONFIG_BAIDU_AUDIO_ACCESS_TOKEN "xxxxxxxxxxxxxxxxx"
#define BAIDUBCE_STT_URL "http://vop.baidu.com/server_api?dev_pid=1537&cuid=123456PHP&token=%s"

#ifdef __cplusplus
extern "C"
{
#endif /**< _cplusplus */

    /*
     * Sends audio input to Baidu's Speech-to-Text (STT) service.
     *
     * This function initializes an HTTP client if not already done,
     * sends the audio data in PCM format, and handles the connection
     * to the Baidu STT service.
     *
     * Parameters:
     *     audio: Pointer to the audio data to be sent.
     *     len: Length of the audio data.
     *     total_len: Total length of the data to be sent.
     *
     * Returns:
     *     ESP_OK on success.
     */
    esp_err_t baidu_asr_send_audio(const int16_t *audio, size_t len, size_t total_len);

    /*
     * Receives the text response from Baidu's STT service.
     *
     * This function fetches the headers of the response, reads the response data,
     * and parses the received text. It also cleans up the HTTP client after use.
     *
     * Parameters:
     *     text: Pointer to a string pointer that will hold the parsed text response.
     *
     * Returns:
     *     ESP_OK on success.
     */
    esp_err_t baidu_asr_recv_text(char **text);

#ifdef __cplusplus
}
#endif /**< _cplusplus */
