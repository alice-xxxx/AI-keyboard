#pragma once

#include "esp_err.h"
#include "stdbool.h"
#define _UA_            "esp32_S3_86_box"
#define TTS_SERVER      "xxxxxxxxxxx"
#define WEB_URL_TTS     "http://"TTS_SERVER"/text2audio"
#define WEB_TTS_FORMAT  "tex=%s&spd=10&vol=8&pit=5&per=4&cuid=baidu_speech_demo&idx=1&cod=2&lan=zh&ctp=1&pdt=505&aue=6&res_tag=audio"
//spd语速，取值0 - 15，默认为5中语速
//pit音调，取值0 - 15，默认为5中语调
//vol音量，基础音库取值0 - 9，精品音库取值0 - 15，默认为5中音量（取值为0时为音量最小值，并非为无声）
//per（基础音库）度小宇 = 1，度小美 = 0，度逍遥（基础） = 3，度丫丫 = 4
#ifdef __cplusplus
extern "C"
{
#endif


    esp_err_t tts_send_text(const char *text);

    bool tts_recv_audio(uint8_t **data, size_t *len, size_t *total_len);

#ifdef __cplusplus
}
#endif
