#include "app_stt.h"

#include "esp_log.h"
#include "esp_http_client.h"

#include "cJSON.h"

static const char *TAG = "baidu_stt";
static esp_http_client_handle_t g_baidu_asr_http_handle = NULL;
//esp_http_client_handle_t baidu_erniebot_http_handle = NULL;


static char *baidu_stt_response_parse(const char *data, size_t len)
{
    cJSON *root = cJSON_Parse(data);
    if (root == NULL)
    {
        ESP_LOGI(TAG, "Error parsing JSON: %s\n", cJSON_GetErrorPtr());
        return NULL;
    }

    cJSON *result_node = cJSON_GetObjectItem(root, "result");
    if (result_node == NULL || result_node->type != cJSON_Array)
    {
        ESP_LOGI(TAG, "Error: 'result' key not found in JSON.\n");
        ESP_LOGI(TAG, "Data received: %s", data);
        cJSON_Delete(root);
        return NULL;
    }

    cJSON *first_result = cJSON_GetArrayItem(result_node, 0);
    if (first_result == NULL || first_result->type != cJSON_String)
    {
        ESP_LOGI(TAG, "Error: First item in 'result' is not a string.\n");
        ESP_LOGI(TAG, "Data received: %s", data);
        cJSON_Delete(root);
        return NULL;
    }

    char *result_value = NULL;
    asprintf(&result_value, "%s", first_result->valuestring);
    cJSON_Delete(root);

    ESP_LOGI(TAG, "result_value: %s", result_value);

    return result_value;
}


esp_err_t baidu_asr_send_audio(const int16_t *audio, size_t len, size_t total_len)
{
    char *url_str = NULL;
    asprintf(&url_str, BAIDUBCE_STT_URL, CONFIG_BAIDU_AUDIO_ACCESS_TOKEN);

    if (!g_baidu_asr_http_handle)
    {
        esp_http_client_config_t config = {
            .url = url_str,
            .method = HTTP_METHOD_POST,
            .skip_cert_common_name_check = true,
            .buffer_size = 1460,
            .timeout_ms = 30000,
        };

        /* Set the headers */
        g_baidu_asr_http_handle = esp_http_client_init(&config);
        esp_http_client_set_header(g_baidu_asr_http_handle, "Content-Type", "audio/pcm;rate=16000");

        esp_http_client_open(g_baidu_asr_http_handle, total_len);
    }

    for (int written_len = 0; written_len < len;)
    {
        written_len += esp_http_client_write(g_baidu_asr_http_handle, (char *)(audio + written_len), len - written_len);
    }

    return ESP_OK;
}

esp_err_t baidu_asr_recv_text(char **text)
{
    int content_length = esp_http_client_fetch_headers(g_baidu_asr_http_handle);

    char *output_buffer = heap_caps_calloc(1, content_length + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);

    int read_length = esp_http_client_read_response(g_baidu_asr_http_handle, output_buffer, content_length);
    if (read_length > 0)
    {
        *text = baidu_stt_response_parse(output_buffer, content_length);
    }
    else
    {
        ESP_LOGE(TAG, "Failed to read any data from response");
    }

    free(output_buffer);
    output_buffer = NULL;
    esp_http_client_close(g_baidu_asr_http_handle);
    esp_http_client_cleanup(g_baidu_asr_http_handle);
    g_baidu_asr_http_handle = NULL;

    return ESP_OK;
}
