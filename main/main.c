#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_err.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "wifi_connected.h"
#include "audio.h"
#include "app_sr.h"
#include "key.h"
#include "rotary_encoder.h"
Key_name key_1 = {
    //.one_byte = volumeup,
    //.two_byte = Consumer_Play,
    //.containment_str = "LEFT_SHIFT",
    .key_number = 1,
    .key1_str = "a",
    .key2_str = "b",
    .key3_str = "c",
    .key4_str = "d",
    .key5_str = "e",
    .key6_str = "f",

};
Key_name key_2 = {
    //.one_byte = volumeup,
    //.two_byte = Consumer_Play,
    //.containment_str = "LEFT_SHIFT",
    .key_number = 1,
    .key1_str = "b",
    .key2_str = "b",
    .key3_str = "c",
    .key4_str = "d",
    .key5_str = "e",
    .key6_str = "f",

};
Key_name key_3 = {
    //.one_byte = volumeup,
    //.two_byte = Consumer_Play,
    //.containment_str = "LEFT_SHIFT",
    .key_number = 1,
    .key1_str = "c",
    .key2_str = "b",
    .key3_str = "c",
    .key4_str = "d",
    .key5_str = "e",
    .key6_str = "f",

};
Key_name key_4 = {
    //.one_byte = volumeup,
    //.two_byte = Consumer_Play,
    .containment_str = "LEFT_SHIFT",
    .key_number = 0,
    .key1_str = "a",
    .key2_str = "b",
    .key3_str = "c",
    .key4_str = "d",
    .key5_str = "e",
    .key6_str = "f",

};

void app_main(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }

    wifi_connected();
    audio_init();
    app_sr_init();

    ble_init();

    button_init(38, &key_1);
    button_init(39, &key_2);
    button_init(40, &key_3);
    button_init(10, &key_4);
    rotary_encoder_init();
}