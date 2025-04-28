#include "audio.h"

#include "esp_log.h"
#include "esp_codec_dev.h"
#include "esp_codec_dev_defaults.h"

#include "driver/i2c_master.h"
#include "esp_err.h" // Include esp_err.h for esp_err_t and ESP_OK/ESP_FAIL

static const char *TAG = "audio init";



static i2s_keep_t *i2s_keep[I2S_MAX_KEEP]; // Array to keep track of I2S handles per port
static i2c_master_bus_handle_t i2c_bus_handle = NULL; // I2C bus handle
esp_codec_dev_handle_t audio_codec_dev = NULL; // Audio codec device handle

/**
 * @brief Initialize I2C master bus.
 *
 * @param port I2C port number.
 * @return ESP_OK if successful, ESP_FAIL otherwise.
 */
static esp_err_t ut_i2c_init(uint8_t port)
{
    esp_err_t ret = ESP_OK;
    if (i2c_bus_handle != NULL) {
        ESP_LOGW(TAG, "I2C bus already initialized");
        return ESP_OK; // Already initialized, return success
    }

    i2c_master_bus_config_t i2c_bus_config = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .i2c_port = port,
        .scl_io_num = I2C_SCL_GPIO,
        .sda_io_num = I2C_SDA_GPIO,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    ret = i2c_new_master_bus(&i2c_bus_config, &i2c_bus_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize I2C master bus: %s", esp_err_to_name(ret));
    }
    return ret;
}

/**
 * @brief Deinitialize I2C master bus.
 *
 * @param port I2C port number.
 * @return ESP_OK if successful, ESP_FAIL otherwise.
 */
static esp_err_t ut_i2c_deinit(uint8_t port)
{
    if (i2c_bus_handle) {
        i2c_del_master_bus(i2c_bus_handle);
        i2c_bus_handle = NULL; // Reset handle after deletion
    }
    else {
        ESP_LOGW(TAG, "I2C bus not initialized or already deinitialized");
    }
    return ESP_OK;
}

/**
 * @brief Initialize I2S channel for audio.
 *
 * @param port I2S port number (index for i2s_keep array).
 * @return ESP_OK if successful, ESP_FAIL otherwise.
 */
static esp_err_t ut_i2s_init(uint8_t port)
{
    esp_err_t ret = ESP_OK;

    if (port >= I2S_MAX_KEEP) {
        ESP_LOGE(TAG, "I2S port number exceeds maximum limit");
        return ESP_FAIL; // Port number out of range
    }
    if (i2s_keep[port]) {
        ESP_LOGW(TAG, "I2S port %d already initialized", port);
        return ESP_OK; // Already initialized, return success
    }

    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM, I2S_ROLE_MASTER); // Use I2S_NUM macro
    chan_cfg.auto_clear = true;
    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(16000),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = I2S_MCK_GPIO, // Use I2S_MCK_GPIO macro
            .bclk = I2S_BCK_GPIO, // Use I2S_BCK_GPIO macro
            .ws = I2S_WS_GPIO,   // Use I2S_WS_GPIO macro
            .dout = I2S_DO_GPIO,  // Use I2S_DO_GPIO macro
            .din = I2S_DI_GPIO,   // Use I2S_DI_GPIO macro
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };
    std_cfg.clk_cfg.mclk_multiple = EXAMPLE_MCLK_MULTIPLE;
    i2s_keep[port] = (i2s_keep_t *)calloc(1, sizeof(i2s_keep_t));
    if (i2s_keep[port] == NULL) {
        ESP_LOGE(TAG, "Failed to allocate memory for i2s_keep_t");
        return ESP_ERR_NO_MEM; // Return no memory error
    }

    ret = i2s_new_channel(&chan_cfg, &i2s_keep[port]->tx_handle, &i2s_keep[port]->rx_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create I2S channel: %s", esp_err_to_name(ret));
        free(i2s_keep[port]); // Free allocated memory on error
        i2s_keep[port] = NULL;
        return ret;
    }
    ret = i2s_channel_init_std_mode(i2s_keep[port]->tx_handle, &std_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize I2S TX channel in standard mode: %s", esp_err_to_name(ret));
        i2s_del_channel(i2s_keep[port]->tx_handle); // Cleanup TX channel
        i2s_del_channel(i2s_keep[port]->rx_handle); // Cleanup RX channel if created (though unlikely failed before both are created)
        free(i2s_keep[port]);
        i2s_keep[port] = NULL;
        return ret;
    }
    ret = i2s_channel_init_std_mode(i2s_keep[port]->rx_handle, &std_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize I2S RX channel in standard mode: %s", esp_err_to_name(ret));
        i2s_del_channel(i2s_keep[port]->tx_handle); // Cleanup TX channel
        i2s_del_channel(i2s_keep[port]->rx_handle); // Cleanup RX channel
        free(i2s_keep[port]);
        i2s_keep[port] = NULL;
        return ret;
    }

    ret = i2s_channel_enable(i2s_keep[port]->tx_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to enable I2S TX channel: %s", esp_err_to_name(ret));
        i2s_channel_disable(i2s_keep[port]->rx_handle); // Try to disable RX as well
        i2s_del_channel(i2s_keep[port]->tx_handle);     // Cleanup TX channel
        i2s_del_channel(i2s_keep[port]->rx_handle);     // Cleanup RX channel
        free(i2s_keep[port]);
        i2s_keep[port] = NULL;
        return ret;
    }
    ret = i2s_channel_enable(i2s_keep[port]->rx_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to enable I2S RX channel: %s", esp_err_to_name(ret));
        i2s_channel_disable(i2s_keep[port]->tx_handle); // Disable TX channel
        i2s_del_channel(i2s_keep[port]->tx_handle);     // Cleanup TX channel
        i2s_del_channel(i2s_keep[port]->rx_handle);     // Cleanup RX channel
        free(i2s_keep[port]);
        i2s_keep[port] = NULL;
        return ret;
    }

    ESP_LOGI(TAG, "I2S port %d initialized successfully", port);
    return ESP_OK;
}

/**
 * @brief Deinitialize I2S channel.
 *
 * @param port I2S port number (index for i2s_keep array).
 * @return ESP_OK if successful, ESP_FAIL otherwise.
 */
static esp_err_t ut_i2s_deinit(uint8_t port)
{
    if (port >= I2S_MAX_KEEP) {
        ESP_LOGE(TAG, "I2S port number exceeds maximum limit");
        return ESP_FAIL; // Port number out of range
    }
    if (i2s_keep[port] == NULL) {
        ESP_LOGW(TAG, "I2S port %d not initialized or already deinitialized", port);
        return ESP_OK; // Not initialized, return success
    }
    i2s_channel_disable(i2s_keep[port]->tx_handle);
    i2s_channel_disable(i2s_keep[port]->rx_handle);
    i2s_del_channel(i2s_keep[port]->tx_handle);
    i2s_del_channel(i2s_keep[port]->rx_handle);
    free(i2s_keep[port]);
    i2s_keep[port] = NULL;
    ESP_LOGI(TAG, "I2S port %d deinitialized successfully", port);
    return ESP_OK;
}


/**
 * @brief Initialize audio codec and related drivers (I2C, I2S).
 *
 * @return ESP_OK if initialization is successful, ESP_FAIL otherwise.
 */
esp_err_t audio_init(void)
{
    esp_err_t ret = ESP_OK;
    ESP_LOGI(TAG, "Starting audio initialization");

    // Initialize I2C driver
    ret = ut_i2c_init(I2C_PORT_NUM);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize I2C driver");
        return ESP_FAIL; // Return ESP_FAIL on I2C init failure
    }

    // Initialize I2S driver
    ret = ut_i2s_init(I2S_NUM);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize I2S driver");
        ut_i2c_deinit(I2C_PORT_NUM); // Deinit I2C if I2S init fails
        return ESP_FAIL; // Return ESP_FAIL on I2S init failure
    }

    // Configure I2S data interface for codec
    audio_codec_i2s_cfg_t i2s_cfg = {
        .rx_handle = i2s_keep[I2S_NUM]->rx_handle,
        .tx_handle = i2s_keep[I2S_NUM]->tx_handle,
    };
    const audio_codec_data_if_t *data_if = audio_codec_new_i2s_data(&i2s_cfg);
    if (!data_if) {
        ESP_LOGE(TAG, "Failed to create I2S data interface for codec");
        ut_i2s_deinit(I2S_NUM); // Deinit I2S if data_if creation fails
        ut_i2c_deinit(I2C_PORT_NUM); // Deinit I2C if data_if creation fails
        return ESP_FAIL;
    }

    // Define hardware gain parameters
    esp_codec_dev_hw_gain_t gain = {
        .pa_voltage = 5.0,
        .codec_dac_voltage = 3.3,
    };

    // Configure I2C control interface for ES8311 codec
    audio_codec_i2c_cfg_t i2c_cfg = {
        .addr = ES8311_ADDR // Use ES8311_ADDR macro
    };
    i2c_cfg.bus_handle = i2c_bus_handle;
    i2c_cfg.port = I2C_PORT_NUM; // Use I2C_PORT_NUM macro
    const audio_codec_ctrl_if_t *out_ctrl_if = audio_codec_new_i2c_ctrl(&i2c_cfg);
    if (!out_ctrl_if) {
        ESP_LOGE(TAG, "Failed to create I2C control interface for codec");
        audio_codec_delete_data_if(data_if); // Cleanup data interface
        ut_i2s_deinit(I2S_NUM);        // Deinit I2S
        ut_i2c_deinit(I2C_PORT_NUM);        // Deinit I2C
        return ESP_FAIL;
    }

    // Create GPIO interface (if needed, currently minimal)
    const audio_codec_gpio_if_t *gpio_if = audio_codec_new_gpio();
    if (!gpio_if) {
        ESP_LOGE(TAG, "Failed to create GPIO interface for codec");
        audio_codec_delete_ctrl_if(out_ctrl_if); // Cleanup control interface
        audio_codec_delete_data_if(data_if);      // Cleanup data interface
        ut_i2s_deinit(I2S_NUM);             // Deinit I2S
        ut_i2c_deinit(I2C_PORT_NUM);             // Deinit I2C
        return ESP_FAIL;
    }

    // Configure ES8311 codec specific settings
    es8311_codec_cfg_t es8311_cfg = {
        .codec_mode = ESP_CODEC_DEV_WORK_MODE_BOTH,
        .ctrl_if = out_ctrl_if,
        .gpio_if = gpio_if,
        .pa_pin = BOARD_PA_PIN,
        .use_mclk = true,
        .pa_reverted = false,
        .master_mode = false,
        .digital_mic = false,
        .invert_mclk = false,
        .invert_sclk = false,
        .hw_gain = gain,
    };
    const audio_codec_if_t *out_codec_if = es8311_codec_new(&es8311_cfg);
    if (!out_codec_if) {
        ESP_LOGE(TAG, "Failed to create ES8311 codec interface");
        audio_codec_delete_gpio_if(gpio_if);      // Cleanup GPIO interface
        audio_codec_delete_ctrl_if(out_ctrl_if); // Cleanup control interface
        audio_codec_delete_data_if(data_if);      // Cleanup data interface
        ut_i2s_deinit(I2S_NUM);             // Deinit I2S
        ut_i2c_deinit(I2C_PORT_NUM);             // Deinit I2C
        return ESP_FAIL;
    }

    // Create audio codec device
    esp_codec_dev_cfg_t dev_cfg = {
        .codec_if = out_codec_if,
        .data_if = data_if,
        .dev_type = ESP_CODEC_DEV_TYPE_IN_OUT,
    };
    audio_codec_dev = esp_codec_dev_new(&dev_cfg);
    if (!audio_codec_dev) {
        ESP_LOGE(TAG, "Failed to create audio codec device");
        audio_codec_delete_codec_if(out_codec_if);     // Cleanup codec interface
        audio_codec_delete_gpio_if(gpio_if);      // Cleanup GPIO interface
        audio_codec_delete_ctrl_if(out_ctrl_if); // Cleanup control interface
        audio_codec_delete_data_if(data_if);      // Cleanup data interface
        ut_i2s_deinit(I2S_NUM);             // Deinit I2S
        ut_i2c_deinit(I2C_PORT_NUM);             // Deinit I2C
        return ESP_FAIL;
    }

    // Open and configure audio codec device
    esp_codec_dev_sample_info_t fs = {
        .sample_rate = 16000,
        .channel = 1,
        .bits_per_sample = 16,
    };
    ret = esp_codec_dev_open(audio_codec_dev, &fs);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open audio codec device: %s", esp_err_to_name(ret));
        esp_codec_dev_delete(audio_codec_dev); // Cleanup codec device
        audio_codec_delete_codec_if(out_codec_if);     // Cleanup codec interface
        audio_codec_delete_gpio_if(gpio_if);      // Cleanup GPIO interface
        audio_codec_delete_ctrl_if(out_ctrl_if); // Cleanup control interface
        audio_codec_delete_data_if(data_if);      // Cleanup data interface
        ut_i2s_deinit(I2S_NUM);             // Deinit I2S
        ut_i2c_deinit(I2C_PORT_NUM);             // Deinit I2C
        return ESP_FAIL;
    }

    // Set output volume and input gain
    ret = esp_codec_dev_set_out_vol(audio_codec_dev, 80);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to set output volume: %s", esp_err_to_name(ret)); // Warning, not critical failure
    }
    ret = esp_codec_dev_set_in_gain(audio_codec_dev, 35.0);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to set input gain: %s", esp_err_to_name(ret));    // Warning, not critical failure
    }

    ESP_LOGI(TAG, "Audio initialization successful");
    return ESP_OK;
}