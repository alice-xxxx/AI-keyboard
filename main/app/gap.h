#pragma once

#include "esp_err.h"

/* Defines */
#define DEVICE_APPEARANCE 0x03C1
#define DEVICE_NAME "MY-H3RGB5.0"

#ifdef _cplusplus
extern "c"
{
#endif

    /* Public function declarations */
    esp_err_t adv_init(void);
    esp_err_t gap_init(void);

#ifdef _cplusplus
}
#endif