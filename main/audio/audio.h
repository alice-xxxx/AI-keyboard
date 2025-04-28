#pragma once

#include "esp_err.h"
#include "driver/i2s_std.h"
//#include "esp_codec_dev.h"


#define EXAMPLE_MCLK_MULTIPLE   (256) // If not using 24-bit data width, 256 should be enough


#define I2S_MAX_KEEP            SOC_I2S_NUM


#define I2C_PORT_NUM      0         // I2C port number to use
#define I2C_SCL_GPIO      (GPIO_NUM_4)        // GPIO pin for I2C SCL
#define I2C_SDA_GPIO      (GPIO_NUM_5)       // GPIO pin for I2C SDA

#define I2S_NUM           0         // I2S number to use
#define I2S_MCK_GPIO      (GPIO_NUM_6)   // GPIO pin for I2S MCLK, configure as needed
#define I2S_BCK_GPIO      (GPIO_NUM_14)   // GPIO pin for I2S BCLK
#define I2S_WS_GPIO       (GPIO_NUM_12)      // GPIO pin for I2S WS
#define I2S_DO_GPIO       (GPIO_NUM_11)    // GPIO pin for I2S DOUT
#define I2S_DI_GPIO       (GPIO_NUM_13) // GPIO pin for I2S DIN

#define ES8311_ADDR       (0x30)    // ES8311 I2C address (default)

#define BOARD_PA_PIN      (GPIO_NUM_9)      // Example: PA pin, set to -1 if not used
#define EXAMPLE_MCLK_MULTIPLE (256)   // Example MCLK multiple, adjust as needed
#ifdef __cplusplus
extern "C"
{
#endif


    typedef struct {
        i2s_chan_handle_t tx_handle;
        i2s_chan_handle_t rx_handle;
    } i2s_keep_t;



    esp_err_t audio_init(void);





#ifdef __cplusplus
}
#endif