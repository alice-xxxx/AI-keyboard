#include "app_tts.h"

#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"

static const char *TAG = "app_tts";
static esp_http_client_handle_t tts_http_handle = NULL;

#define LOG_TRACE(...) ESP_LOGI(TAG, ##__VA_ARGS__)

#define MAX_HTTP_RECV_BUFFER (1024)

/**
 * @brief Send text to the TTS API for speech synthesis.
 *
 * This function initializes an HTTP client, sets headers, constructs the JSON payload with the text,
 * and sends a POST request to the TTS API endpoint.
 *
 * @param text The text string to be synthesized into speech.
 * @return esp_err_t - ESP_OK if the request was sent successfully, ESP_FAIL otherwise.
 */
esp_err_t tts_send_text(const char *text)
{
    if (text == NULL) {
        ESP_LOGE(TAG, "Input text is NULL");
        return ESP_ERR_INVALID_ARG; // Return specific error code for invalid input
    }

    char *payload = NULL;
    esp_http_client_config_t config = {
        .url = WEB_URL_TTS, // URL for TTS API (defined in app_tts.h)
        .method = HTTP_METHOD_POST,
        .timeout_ms = 30000,
        .buffer_size = 1460,
        .skip_cert_common_name_check = true, // For development/testing, disable certificate verification if needed
    };

    tts_http_handle = esp_http_client_init(&config);
    if (tts_http_handle == NULL) {
        ESP_LOGE(TAG, "Failed to initialize HTTP client");
        return ESP_FAIL; // Return ESP_FAIL if client initialization fails
    }

    esp_http_client_set_header(tts_http_handle, "Content-Type", "application/json");

    if (asprintf(&payload, WEB_TTS_FORMAT, text) == -1) {
        ESP_LOGE(TAG, "Failed to allocate memory for payload");
        esp_http_client_cleanup(tts_http_handle); // Cleanup resources before exiting on error
        tts_http_handle = NULL;
        return ESP_FAIL; // Return ESP_FAIL if payload creation fails
    }

    esp_err_t err = esp_http_client_open(tts_http_handle, strlen(payload));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open HTTP connection: %s", esp_err_to_name(err));
        free(payload); // Free payload memory in case of connection error
        esp_http_client_cleanup(tts_http_handle);
        tts_http_handle = NULL;
        return ESP_FAIL; // Return ESP_FAIL if connection open fails
    }

    err = esp_http_client_write(tts_http_handle, payload, strlen(payload));
    if (err < 0) {
        ESP_LOGE(TAG, "Failed to write data to HTTP connection: %s", esp_err_to_name(err));
        free(payload); // Free payload memory in case of write error
        esp_http_client_close(tts_http_handle); // Close connection upon write failure
        esp_http_client_cleanup(tts_http_handle);
        tts_http_handle = NULL;
        return ESP_FAIL; // Return ESP_FAIL if write fails
    }

    free(payload);
    return ESP_OK; // Return ESP_OK if request was successfully sent
}

/**
 * @brief Receive audio data from the TTS API response in chunks.
 *
 * This function is designed to be called repeatedly to stream audio data from the TTS API.
 * In the first call, it fetches the content length of the audio. In subsequent calls, it reads chunks of audio data.
 * The function manages HTTP client cleanup when the entire audio is received or an error occurs.
 *
 * @param data Pointer to a uint8_t* to store the received audio data chunk. Caller must free this memory.
 * @param len Pointer to a size_t to store the length of the received audio data chunk.
 * @param total_len Pointer to a size_t to store the total length of the audio data (fetched in the first call).
 * @return bool - true if an audio chunk is successfully received, false if no more data or an error occurred.
 */
bool tts_recv_audio(uint8_t **data, size_t *len, size_t *total_len)
{
    static int tts_audio_total_len = -1; // Static to persist across calls, initialized to -1 to indicate not fetched yet
    int read_len = 0;
    int chunk_size = MAX_HTTP_RECV_BUFFER; // Define chunk size for reading audio data

    if (tts_audio_total_len == -1) {
        tts_audio_total_len = esp_http_client_fetch_headers(tts_http_handle);
        if (tts_audio_total_len < 0) {
            ESP_LOGE(TAG, "Failed to fetch audio content length: %s", esp_err_to_name(tts_audio_total_len));
            goto end; // Jump to end for cleanup on error
        }
        ESP_LOGI(TAG, "TTS audio total length: %d bytes", tts_audio_total_len);
        *total_len = tts_audio_total_len;
    }

    uint8_t *output_buffer = heap_caps_calloc(1, chunk_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (output_buffer == NULL) {
        ESP_LOGE(TAG, "Failed to allocate memory for audio buffer");
        goto end; // Jump to end for cleanup on memory allocation failure
    }

    read_len = esp_http_client_read(tts_http_handle, (char *)output_buffer, chunk_size);

    if (read_len > 0) {
        ESP_LOGV(TAG, "Audio chunk read length: %d bytes", read_len); // Use ESP_LOGV for verbose logs of chunk reads
        *len = read_len;
        *data = output_buffer;
        return true; // Return true to indicate audio chunk received
    }
    else {
        free(output_buffer); // Free buffer if no data or error occurs
        if (read_len == 0) {
            ESP_LOGI(TAG, "End of audio stream reached"); // Log end of stream normally
            goto end; // Jump to end for final cleanup after stream ends
        }
        else {
            ESP_LOGE(TAG, "Failed to read audio data chunk: %s", esp_err_to_name(read_len)); // Log error if read failed (negative value)
            goto end; // Jump to end for cleanup on read error
        }
    }

end:
    ESP_LOGW(TAG, "End of TTS audio response or error occurred, cleaning up");
    *data = NULL; // Set data to NULL to indicate no data returned
    if (tts_http_handle) { // Check if handle is valid before cleanup to avoid potential issues if init failed earlier.
        esp_http_client_close(tts_http_handle);
        esp_http_client_cleanup(tts_http_handle);
        tts_http_handle = NULL; // Reset handle after cleanup
    }
    tts_audio_total_len = -1; // Reset total length for next TTS request
    return false; // Return false to indicate no audio data received in this call or stream ended/errored
}