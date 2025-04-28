#pragma once

#include "esp_err.h"
#include "ble_init.h"
#define BOOT_BUTTON_NUM 0
#ifdef _cplusplus
extern "c"
{
#endif

    typedef struct
    {
        Consumer1byte one_byte;
        Consumer2byte two_byte;
        const char *containment_str;
        const uint8_t key_number;
        const char *key1_str;
        const char *key2_str;
        const char *key3_str;
        const char *key4_str;
        const char *key5_str;
        const char *key6_str;
    } Key_name;
    typedef Key_name *Key_name_t;

    esp_err_t
    button_init(uint32_t button_num, Key_name_t Key_name);

#ifdef _cplusplus
}
#endif