#include "app_llm.h"

#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_heap_caps.h"

#include "cJSON.h"

static const char *TAG = "app_llm";
static esp_http_client_handle_t llm_http_handle = NULL;

/**
 * @brief Parse the content from the JSON response from LLM API.
 *
 * This function parses a JSON string to extract the 'content' from the expected structure:
 * choices -> array[0] -> message -> content.
 *
 * @param data Pointer to the JSON data string.
 * @param len  Length of the JSON data string.
 * @return char* - Dynamically allocated string containing the extracted content, or NULL on error.
 *                  The caller is responsible for freeing the returned string.
 */
static char *content_response_parse(const char *data, size_t len)
{
    cJSON *root = NULL;
    cJSON *choices_array = NULL;
    cJSON *first_choice = NULL;
    cJSON *message_node = NULL;
    cJSON *content_node = NULL;
    char *result_value = NULL;

    root = cJSON_ParseWithLength(data, len);
    if (root == NULL) {
        ESP_LOGW(TAG, "JSON Parse Error: %s", cJSON_GetErrorPtr());
        goto cleanup; // Use cleanup label for consistent resource freeing
    }

    choices_array = cJSON_GetObjectItem(root, "choices");
    if (choices_array == NULL || !cJSON_IsArray(choices_array)) {
        ESP_LOGW(TAG, "Error: 'choices' key not found or not an array.");
        goto cleanup;
    }

    first_choice = cJSON_GetArrayItem(choices_array, 0); // Get the first item of the array
    if (first_choice == NULL || !cJSON_IsObject(first_choice)) {
        ESP_LOGW(TAG, "Error: 'choices' array is empty or first item is not an object.");
        goto cleanup;
    }

    message_node = cJSON_GetObjectItem(first_choice, "message");
    if (message_node == NULL || !cJSON_IsObject(message_node)) {
        ESP_LOGW(TAG, "Error: 'message' key not found or not an object in the first choice.");
        goto cleanup;
    }

    content_node = cJSON_GetObjectItem(message_node, "content");
    if (content_node == NULL || !cJSON_IsString(content_node)) {
        ESP_LOGW(TAG, "Error: 'content' key not found or not a string in 'message'.");
        goto cleanup;
    }

    result_value = strdup(content_node->valuestring);
    if (result_value == NULL) {
        ESP_LOGW(TAG, "Error: strdup failed (memory allocation error).");
        goto cleanup;
    }

cleanup: // Unified cleanup label
    if (root != NULL) {
        cJSON_Delete(root);
    }
    return result_value; // result_value is NULL if any error occurred, or points to allocated string on success
}

// void app_main(void)
// {
//     char *json_payload = NULL;
//     cJSON *llm_json = NULL;
//     cJSON *llm_user_json = NULL;
//     cJSON *llm_system_json = NULL;
//     cJSON *messages_json = NULL;
//     llm_json = cJSON_CreateObject();
//     llm_user_json = cJSON_CreateObject();
//     llm_system_json = cJSON_CreateObject();
//     messages_json = cJSON_CreateArray();
//     cJSON_AddStringToObject(llm_system_json, "role", "system");
//     cJSON_AddStringToObject(llm_system_json, "content", "system");
//     cJSON_AddStringToObject(llm_user_json, "role", "user");
//     cJSON_AddStringToObject(llm_user_json, "content", "%s");
//     cJSON_AddStringToObject(llm_json, "model", "glm-4-flash");
//     cJSON_AddItemToArray(messages_json, llm_system_json);
//     cJSON_AddItemToArray(messages_json, llm_user_json);
//     cJSON_AddItemToObject(llm_json, "messages", messages_json);
//     asprintf(&json_payload, cJSON_Print(llm_json), "1111111111111");
//     printf(json_payload);
//     free(json_payload);
//     cJSON_Delete(llm_json);
// }

/**
 * @brief Send request to LLM API.
 *
 * This function initializes the HTTP client, sets headers, constructs the JSON payload,
 * and sends the POST request to the LLM API endpoint.
 *
 * @param content The content string to send to the LLM API.
 * @return esp_err_t - ESP_OK if request was sent successfully, ESP_FAIL otherwise.
 */
esp_err_t llm_send_request(const char *content)
{
    if (content == NULL)
    {
        ESP_LOGE(TAG, "NULL content input");
        return ESP_FAIL; // Return ESP_FAIL to indicate an error for NULL content
    }
    char *json_payload = NULL;
    esp_http_client_config_t config = {
        .timeout_ms = 30000,
        .buffer_size = 1460,
        .url = WEB_URL_LLM,
        .method = HTTP_METHOD_POST,
        .skip_cert_common_name_check = true,
    };
    /* Initialize HTTP client */
    llm_http_handle = esp_http_client_init(&config);
    if (llm_http_handle == NULL) {
        ESP_LOGE(TAG, "Failed to initialize HTTP client");
        return ESP_FAIL; // Return ESP_FAIL if client initialization fails
    }
    esp_http_client_set_header(llm_http_handle, "Content-Type", "application/json");
    esp_http_client_set_header(llm_http_handle, "Authorization", LLM_KEY);
    /* Create JSON payload with format and content using asprintf */
    if (asprintf(&json_payload, BAIDUBCE_MESSAGE_FORMAT, content) == -1) {
        ESP_LOGE(TAG, "Failed to create JSON payload");
        esp_http_client_cleanup(llm_http_handle); // Cleanup HTTP client before exiting on error
        llm_http_handle = NULL;
        return ESP_FAIL; // Return ESP_FAIL if payload creation fails
    }
    ESP_LOGI(TAG, "json_payload: %s", json_payload);

    esp_err_t err = esp_http_client_open(llm_http_handle, strlen(json_payload));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open HTTP connection: %s", esp_err_to_name(err));
        free(json_payload);
        esp_http_client_cleanup(llm_http_handle);
        llm_http_handle = NULL;
        return ESP_FAIL; // Return ESP_FAIL if connection open fails
    }
    err = esp_http_client_write(llm_http_handle, json_payload, strlen(json_payload));
    if (err < 0) {
        ESP_LOGE(TAG, "Failed to write request data: %s", esp_err_to_name(err));
        free(json_payload);
        esp_http_client_close(llm_http_handle); // Close connection on write failure
        esp_http_client_cleanup(llm_http_handle);
        llm_http_handle = NULL;
        return ESP_FAIL; // Return ESP_FAIL if write fails
    }

    free(json_payload);
    return ESP_OK; // Return ESP_OK if request sending process is successful
}

/**
 * @brief Receive response from LLM API.
 *
 * This function reads the response from the HTTP connection, parses the JSON response,
 * and extracts the content.
 *
 * @param response_data Pointer to a char* to store the dynamically allocated response data.
 *                      The caller is responsible for freeing this memory.
 * @return esp_err_t - ESP_OK if response was received and parsed successfully, ESP_FAIL otherwise.
 */
esp_err_t llm_recv_response(char **response_data)
{
    int content_length = esp_http_client_fetch_headers(llm_http_handle);
    if (content_length < 0) {
        ESP_LOGE(TAG, "Failed to fetch content length: %s", esp_err_to_name(content_length));
        esp_http_client_close(llm_http_handle);
        esp_http_client_cleanup(llm_http_handle);
        llm_http_handle = NULL;
        return ESP_FAIL; // Return ESP_FAIL if fetching headers fails
    }
    char *output_buffer = heap_caps_calloc(1, content_length + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (output_buffer == NULL) {
        ESP_LOGE(TAG, "Failed to allocate memory for response buffer");
        esp_http_client_close(llm_http_handle);
        esp_http_client_cleanup(llm_http_handle);
        llm_http_handle = NULL;
        return ESP_FAIL; // Return ESP_FAIL if memory allocation fails
    }

    int read_length = esp_http_client_read_response(llm_http_handle, output_buffer, content_length);

    if (read_length > 0) {
        ESP_LOGI(TAG, "read_length: %d output_buffer: %s", read_length, output_buffer);
        *response_data = content_response_parse(output_buffer, read_length);
        if (*response_data == NULL) {
            ESP_LOGE(TAG, "Failed to parse response content");
            free(output_buffer);
            esp_http_client_close(llm_http_handle);
            esp_http_client_cleanup(llm_http_handle);
            llm_http_handle = NULL;
            return ESP_FAIL; // Return ESP_FAIL if JSON parsing fails
        }
    }
    else {
        ESP_LOGE(TAG, "Failed to read response, read_length = %d", read_length);
        free(output_buffer);
        esp_http_client_close(llm_http_handle);
        esp_http_client_cleanup(llm_http_handle);
        llm_http_handle = NULL;
        return ESP_FAIL; // Return ESP_FAIL if reading response data fails
    }

    esp_http_client_close(llm_http_handle);
    esp_http_client_cleanup(llm_http_handle);
    llm_http_handle = NULL;

    free(output_buffer);

    return ESP_OK; // Return ESP_OK if response received and processed successfully
}