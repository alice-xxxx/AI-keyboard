#include "key.h"

#include "esp_log.h"
#include "esp_pm.h"

#include "iot_button.h"
#include "button_gpio.h"

#include "ble_init.h"
// #include "app_weather.h"

static const char *TAG = "button key";

#define BUTTON_ACTIVE_LEVEL 0

static void button_event_cb(void *arg, void *data)
{
    Key_name *key_data = (Key_name *)data;

    button_event_t event;
    event = iot_button_get_event(arg);
    switch (event)
    {
    case BUTTON_PRESS_DOWN:
        send_key_press(key_data->one_byte, key_data->two_byte, key_data->containment_str, key_data->key_number, key_data->key1_str, key_data->key2_str, key_data->key3_str, key_data->key4_str, key_data->key5_str, key_data->key6_str);
        // ESP_LOGI(TAG, "key %s", key_data->key1_str);
        // ESP_LOGI(TAG, "key %x", key_data->one_byte);
        // ESP_LOGI(TAG, "key %x", key_data->two_byte);
        break;
    case BUTTON_PRESS_UP:
        send_key_release();
        break;
    case BUTTON_PRESS_REPEAT:
        /* code */
        break;
    case BUTTON_PRESS_REPEAT_DONE:
        /* code */
        break;
    case BUTTON_SINGLE_CLICK:
        /* code */
        break;
    case BUTTON_DOUBLE_CLICK:
        /* code */
        break;
    case BUTTON_LONG_PRESS_HOLD:
        /* code */
        break;
    case BUTTON_LONG_PRESS_START:
        /* code */
        break;
    case BUTTON_LONG_PRESS_UP:
        /* code */
        break;
    case BUTTON_PRESS_END:
        /* code */
        break;
    default:
        break;
    }
}

esp_err_t button_init(uint32_t button_num, Key_name_t Key_name)
{
    // button_config_t btn_cfg = { 0 };
    // button_gpio_config_t gpio_cfg = {
    //     .gpio_num = button_num,
    //     .active_level = BUTTON_ACTIVE_LEVEL,
    //     .enable_power_save = true,
    // };

    // button_handle_t btn;
    // esp_err_t ret = iot_button_new_gpio_device(&btn_cfg, &gpio_cfg, &btn);
    // if (ret != ESP_OK)
    // {
    //     ESP_LOGI(TAG, "button init %lu fail", button_num);
    //     return ESP_FAIL;
    //}

    esp_err_t ret;
    button_config_t btn_cfg = {
        .type = BUTTON_TYPE_GPIO,
        .gpio_button_config = {
            .gpio_num = button_num,
            .active_level = BUTTON_ACTIVE_LEVEL,
        },
    };
    button_handle_t btn = iot_button_create(&btn_cfg);
    ret = iot_button_register_cb(btn, BUTTON_PRESS_DOWN, button_event_cb, Key_name);
    ret |= iot_button_register_cb(btn, BUTTON_PRESS_UP, button_event_cb, Key_name);
    ret |= iot_button_register_cb(btn, BUTTON_PRESS_REPEAT, button_event_cb, Key_name);
    ret |= iot_button_register_cb(btn, BUTTON_PRESS_REPEAT_DONE, button_event_cb, Key_name);
    ret |= iot_button_register_cb(btn, BUTTON_SINGLE_CLICK, button_event_cb, Key_name);
    ret |= iot_button_register_cb(btn, BUTTON_DOUBLE_CLICK, button_event_cb, Key_name);
    ret |= iot_button_register_cb(btn, BUTTON_LONG_PRESS_START, button_event_cb, Key_name);
    ret |= iot_button_register_cb(btn, BUTTON_LONG_PRESS_HOLD, button_event_cb, Key_name);
    ret |= iot_button_register_cb(btn, BUTTON_LONG_PRESS_UP, button_event_cb, Key_name);
    ret |= iot_button_register_cb(btn, BUTTON_PRESS_END, button_event_cb, Key_name);
    if (ret != ESP_OK)
    {
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "button %lu to register successfully", button_num);
    return ESP_OK;
}
