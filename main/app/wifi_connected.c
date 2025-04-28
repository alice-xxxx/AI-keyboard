#include "wifi_connected.h"
#include "nvs_flash.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_http_server.h"

#include "dns_server.h"



// 定义日志标签，方便过滤日志信息
static const char *TAG_AP = "WiFi SoftAP";
static const char *TAG_STA = "WiFi Station";

// 声明外部链接的 HTML 资源，这些资源在编译时被嵌入到固件中
extern const char root_start[] asm("_binary_root_html_start");
extern const char root_end[] asm("_binary_root_html_end");

// 定义静态变量
static uint8_t s_retry_num = 0;                  // Station 模式下重连尝试计数器
static uint16_t s_ap_record_count = 0;           // 扫描到的 AP 记录数量
static char s_userid[20] = "";                   // 用户 ID 存储
static char s_password[25] = "";                 // 密码存储

static httpd_handle_t s_httpd_server = NULL;      // HTTP Server 句柄
static wifi_ap_record_t s_ap_records[MAX_STA_MUX]; // AP 记录数组，用于存储扫描结果
static EventGroupHandle_t s_wifi_event_group = NULL;// WiFi 事件组句柄，用于线程同步
static dns_server_handle_t s_dns_server_handle = NULL;// DNS Server 句柄

// 定义一个常量，用于在 POST 请求中标识扫描请求
#define WIFI_SCAN_REQUEST_LENGTH 1

// WiFi 事件处理函数
static void wifi_event_handler(void *arg, esp_event_base_t event_base,
    int32_t event_id, void *event_data)
{
    // SoftAP 模式事件处理
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STACONNECTED) {
        wifi_event_ap_staconnected_t *event = (wifi_event_ap_staconnected_t *)event_data;
        ESP_LOGI(TAG_AP, "Station " MACSTR " connected, AID=%d",
            MAC2STR(event->mac), event->aid);
    }
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STADISCONNECTED) {
        wifi_event_ap_stadisconnected_t *event = (wifi_event_ap_stadisconnected_t *)event_data;
        ESP_LOGI(TAG_AP, "Station " MACSTR " disconnected, AID=%d, reason=%d",
            MAC2STR(event->mac), event->aid, event->reason);
    }
    // Station 模式事件处理
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect(); // 尝试连接 WiFi AP
        ESP_LOGI(TAG_STA, "Station started, attempting to connect");
    }
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry_num < ESP_MAXIMUM_RETRY) {
            esp_wifi_connect(); // 重试连接 WiFi AP
            s_retry_num++;
            ESP_LOGI(TAG_STA, "Station disconnected, retrying to connect to the AP, retry count: %d", s_retry_num);
        }
        else {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT); // 超过最大重试次数，设置 WIFI_FAIL_BIT 事件标志
            ESP_LOGI(TAG_STA, "Station failed to connect after maximum retries");
        }
        ESP_LOGI(TAG_STA, "Station disconnected from AP");
    }
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG_STA, "Station got IP address:" IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0; // 重置重试计数器
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT); // 设置 WIFI_CONNECTED_BIT 事件标志
    }
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_SCAN_DONE)
    {
        xEventGroupSetBits(s_wifi_event_group, WIFI_SCAN_BIT); // 扫描完成，设置 WIFI_SCAN_BIT 事件标志
        ESP_LOGI(TAG_STA, "WiFi scan done");
    }
}

// root URI 的 GET 请求处理函数，用于提供 HTML 页面
static esp_err_t root_get_handler(httpd_req_t *req)
{
    // 计算 HTML 页面长度
    const uint32_t root_len = root_end - root_start;

    ESP_LOGI(TAG_AP, "Serving root page");
    httpd_resp_set_type(req, "text/html");             // 设置 Content-Type 为 text/html
    httpd_resp_send(req, root_start, root_len);      // 发送 HTML 内容

    return ESP_OK;
}

// root URI 的 GET 请求处理配置
static const httpd_uri_t root_get = {
    .uri = "/",
    .method = HTTP_GET,
    .handler = root_get_handler
};

// 动态生成 JSON 格式的 AP 列表
static char *Request_json()
{
    char *json_payload = (char *)malloc(2 * 1024);
    if (json_payload == NULL) {
        ESP_LOGE(TAG_AP, "Failed to allocate memory for json_payload");
        return NULL;
    }
    json_payload[0] = '\0';

    int offset = 0;
    int remaining = 2 * 1024;

    offset += snprintf(json_payload + offset, remaining, "{");
    remaining -= offset;
    if (remaining <= 0) {
        ESP_LOGE(TAG_AP, "Buffer too small for initial '{'");
        free(json_payload);
        return NULL;
    }

    for (int i = 0; i < s_ap_record_count && i < MAX_STA_MUX; i++) { // 限制循环次数不超过实际扫描结果数量和 MAX_STA_MUX
        char *json_entry = NULL;
        int entry_len = asprintf(&json_entry, REQUEST_JSON, i + 1, 1, (char *)s_ap_records[i].ssid, 2, s_ap_records[i].rssi, 3, s_ap_records[i].authmode);
        if (entry_len < 0 || json_entry == NULL) {
            ESP_LOGE(TAG_AP, "asprintf failed for entry %d", i);
            free(json_payload);
            return NULL;
        }

        if (i > 0) { // 只有在不是第一个 AP 记录时才添加逗号
            offset += snprintf(json_payload + offset, remaining, ",");
            remaining -= offset;
            if (remaining <= 0) {
                ESP_LOGE(TAG_AP, "Buffer overflow before comma");
                free(json_payload);
                free(json_entry);
                return NULL;
            }
        }

        int written = snprintf(json_payload + offset, remaining, "%s", json_entry);
        if (written < 0 || written >= remaining) {
            ESP_LOGE(TAG_AP, "Buffer overflow while appending entry %d", i);
            free(json_payload);
            free(json_entry);
            return NULL;
        }
        offset += written;
        remaining -= written;

        free(json_entry);
        json_entry = NULL;

        ESP_LOGI(TAG_AP, "SSID: %-32s | Channel: %-7d | RSSI: %-4d | Auth Mode: %d",
            (char *)s_ap_records[i].ssid, s_ap_records[i].primary, s_ap_records[i].rssi,
            s_ap_records[i].authmode);
    }

    snprintf(json_payload + offset, remaining, "}");

    ESP_LOGI(TAG_AP, "JSON Payload: %s", json_payload);

    return json_payload;
}

// root URI 的 POST 请求处理函数，用于接收用户数据或触发扫描
static esp_err_t root_post_handler(httpd_req_t *req)
{
    char *recv_buf = NULL;
    int recv_len = 0;
    esp_err_t ret_code = ESP_OK;

    int total_len = req->content_len;

    // 如果 Content-Length 为 WIFI_SCAN_REQUEST_LENGTH (例如 1)，则执行 WiFi 扫描并返回 JSON 结果
    if (total_len == WIFI_SCAN_REQUEST_LENGTH)
    {
        ESP_LOGI(TAG_AP, "Received WiFi scan request");
        wifi_scan_config_t scan_config = {
        .channel = 0, // 扫描所有信道
        .scan_type = WIFI_SCAN_TYPE_PASSIVE, // 使用被动扫描，减少功耗
        };
        ESP_ERROR_CHECK(esp_wifi_scan_start(&scan_config, false));
        ESP_LOGI(TAG_AP, "WiFi scan started");

        char *json_response = Request_json(); // 获取扫描结果 JSON
        if (json_response) {
            httpd_resp_set_type(req, "application/json"); // 设置返回类型为 JSON
            httpd_resp_sendstr(req, json_response);       // 发送 JSON 响应
            free(json_response);                         // 释放 JSON 响应内存
        }
        else {
            ESP_LOGE(TAG_AP, "Failed to generate JSON response");
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to generate JSON response");
            ret_code = ESP_FAIL;
        }
    }
    // 否则，接收并处理用户提交的用户名和密码
    else {
        if (total_len <= 0) {
            ESP_LOGE(TAG_AP, "Content-Length is invalid or missing: %d", total_len);
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid Content-Length"); // 返回 400 错误
            return ESP_FAIL;
        }
        ESP_LOGI(TAG_AP, "Content-Length: %d", total_len);

        recv_buf = malloc(total_len + 1);
        if (recv_buf == NULL) {
            ESP_LOGE(TAG_AP, "Failed to allocate memory for request body");
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Memory allocation failed");
            return ESP_FAIL;
        }

        recv_len = httpd_req_recv(req, recv_buf, total_len);
        if (recv_len <= 0) {
            ESP_LOGE(TAG_AP, "Failed to receive request body, len = %d", recv_len);
            free(recv_buf);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to receive request body");
            return ESP_FAIL;
        }
        recv_buf[recv_len] = '\0'; // Null-terminate the received buffer
        ESP_LOGI(TAG_AP, "Received data: %s", recv_buf);

        char *line = strtok(recv_buf, "\n");
        while (line != NULL) {
            if (strncmp(line, "userid=", 7) == 0) {
                strncpy(s_userid, line + 7, sizeof(s_userid) - 1);
                s_userid[sizeof(s_userid) - 1] = '\0';
            }
            else if (strncmp(line, "password=", 9) == 0) {
                strncpy(s_password, line + 9, sizeof(s_password) - 1);
                s_password[sizeof(s_password) - 1] = '\0';
            }
            line = strtok(NULL, "\n");
        }
        xEventGroupSetBits(s_wifi_event_group, WIFI_RECONNECT_BIT); // 设置 WIFI_RECONNECT_BIT 事件，触发重新连接
        ESP_LOGI(TAG_AP, "Parsed credentials - User ID: [%s], Password: [%s]", s_userid, s_password);

        httpd_resp_set_status(req, HTTPD_200);        // 设置 HTTP 状态码为 200 OK
        httpd_resp_set_type(req, "application/json");     // 设置返回类型为 JSON
        httpd_resp_sendstr(req, "{\"success\":200}");    // 发送 JSON 响应

        free(recv_buf); // 释放接收缓冲区
    }

    return ret_code;
}

// root URI 的 POST 请求处理配置
static const httpd_uri_t root_post = {
    .uri = "/",
    .method = HTTP_POST,
    .handler = root_post_handler
};

// 404 错误处理函数，重定向到根目录
static esp_err_t http_404_error_handler(httpd_req_t *req, httpd_err_code_t err)
{
    ESP_LOGI(TAG_AP, "404 error detected, redirecting to root");
    httpd_resp_set_status(req, "302 Temporary Redirect");
    httpd_resp_set_hdr(req, "Location", "/");
    // 对于 iOS Captive Portal 检测，需要响应内容
    httpd_resp_send(req, "Redirect to captive portal", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

// 启动 Web 服务器
static httpd_handle_t start_webserver(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_open_sockets = 2;
    config.lru_purge_enable = true;

    ESP_LOGI(TAG_AP, "Starting HTTP server on port: '%d'", config.server_port);
    if (httpd_start(&s_httpd_server, &config) == ESP_OK) {
        ESP_LOGI(TAG_AP, "Registering URI handlers");
        httpd_register_uri_handler(s_httpd_server, &root_get);
        httpd_register_uri_handler(s_httpd_server, &root_post);
        httpd_register_err_handler(s_httpd_server, HTTPD_404_NOT_FOUND, http_404_error_handler);
        return s_httpd_server;
    }

    ESP_LOGE(TAG_AP, "Error starting HTTP server");
    return NULL;
}

// 初始化 SoftAP
static void wifi_init_softap(void)
{
    wifi_config_t wifi_ap_config = {
        .ap = {
            .ssid = ESP_WIFI_AP_SSID,                 // SoftAP SSID
            .ssid_len = strlen(ESP_WIFI_AP_SSID),      // SSID 长度
            .channel = ESP_WIFI_CHANNEL,               // 信道
            .password = ESP_WIFI_AP_PASSWD,             // SoftAP 密码
            .max_connection = MAX_STA_CONN,            // 最大连接数
            .authmode = ESP_WIFI_AP_AUTH_MODE,         // 认证模式
            .pmf_cfg = {
                .required = false,
            },
        },
    };
    if (strlen(ESP_WIFI_AP_PASSWD) == 0) {
        wifi_ap_config.ap.authmode = WIFI_AUTH_OPEN; // 无密码时使用开放模式
    }

    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_ap_config)); // 设置 SoftAP 配置

    ESP_LOGI(TAG_AP, "SoftAP initialized. SSID:%s, password:%s, channel:%d, auth_mode:%d",
        ESP_WIFI_AP_SSID, ESP_WIFI_AP_PASSWD, ESP_WIFI_CHANNEL, wifi_ap_config.ap.authmode);
}

// 初始化 Station 模式配置 (获取当前配置)
static void wifi_init_sta(void)
{
    wifi_config_t wifi_sta_config;
    ESP_ERROR_CHECK(esp_wifi_get_config(WIFI_IF_STA, &wifi_sta_config)); // 获取 Station 模式配置
    ESP_LOGI(TAG_STA, "Station mode configuration initialized (getting current config)");
}

// 根据用户名和密码重新初始化 Station 模式并连接
static void wifi_reconnect_sta(void)
{
    wifi_config_t wifi_sta_config = {
        .sta = {
            .ssid = {0},
            .password = {0},
            .threshold.authmode = ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD, // 认证模式阈值
            .pmf_cfg.capable = true,
            .pmf_cfg.required = false,
        },
    };
    strncpy((char *)wifi_sta_config.sta.ssid, s_userid, sizeof(wifi_sta_config.sta.ssid) - 1);
    wifi_sta_config.sta.ssid[sizeof(wifi_sta_config.sta.ssid) - 1] = '\0';
    strncpy((char *)wifi_sta_config.sta.password, s_password, sizeof(wifi_sta_config.sta.password) - 1);
    wifi_sta_config.sta.password[sizeof(wifi_sta_config.sta.password) - 1] = '\0';

    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_sta_config)); // 设置 Station 模式配置
    ESP_ERROR_CHECK(esp_wifi_connect());                                 // 连接 Wi-Fi AP

    ESP_LOGI(TAG_STA, "Station reconnect initialized with new credentials, attempting to connect");
}

// WiFi 事件处理任务
static void wifi_event_task(void *param)
{
    uint8_t s_num = 0;

    while (true) {
        // 等待事件组中的事件标志
        EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
            WIFI_CONNECTED_BIT | WIFI_FAIL_BIT | WIFI_SCAN_BIT | WIFI_RECONNECT_BIT,
            pdFALSE,
            pdFALSE,
            portMAX_DELAY);

        // 根据接收到的事件标志进行处理
        if (bits & WIFI_CONNECTED_BIT) {
            ESP_LOGI(TAG_STA, "Successfully connected to AP");
            xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT); // 清除 WIFI_CONNECTED_BIT
            if (s_httpd_server != NULL)
            {
                httpd_stop(s_httpd_server);
                s_httpd_server = NULL; // 重置 HTTP 服务器句柄
                ESP_LOGI(TAG_STA, "stopping http services");
            }
            if (s_dns_server_handle != NULL)
            {
                // 停止 SoftAP, DNS 服务器和 HTTP 服务器，切换到 Station 模式
                ESP_LOGI(TAG_STA, "Switching to Station mode, stopping SoftAP services");
                stop_dns_server(s_dns_server_handle);
                s_dns_server_handle = NULL;
            }
            if (s_num == 0)
            {
                ESP_ERROR_CHECK(esp_wifi_stop());
                ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
                ESP_ERROR_CHECK(esp_wifi_start());
                s_num = 1;
            }


        }
        else if (bits & WIFI_FAIL_BIT) {
            ESP_LOGI(TAG_STA, "Failed to connect to AP, starting SoftAP and captive portal");
            xEventGroupClearBits(s_wifi_event_group, WIFI_FAIL_BIT); // 清除 WIFI_FAIL_BIT

            // 初始化并启动 SoftAP 模式
            ESP_LOGI(TAG_AP, "Initializing ESP_WIFI_MODE_AP");
            wifi_init_softap();

            // 启动 WiFi AP 扫描
            wifi_scan_config_t scan_config = {
                .channel = 0,
                .scan_type = WIFI_SCAN_TYPE_PASSIVE,
            };
            ESP_ERROR_CHECK(esp_wifi_scan_start(&scan_config, true));
            ESP_LOGI(TAG_AP, "SoftAP WiFi scan started");
            // 启动 Web 服务器用于 Captive Portal
            if (s_httpd_server == NULL) { // 避免多次启动 HTTP 服务器
                s_httpd_server = start_webserver();
            }
            if (s_httpd_server == NULL) {
                ESP_LOGE(TAG_AP, "Failed to start HTTP server for captive portal");
            }
            // 启动 DNS 服务器用于 Captive Portal
            if (s_dns_server_handle == NULL) { // 避免多次启动 DNS 服务器
                dns_server_config_t dns_config = DNS_SERVER_CONFIG_SINGLE("*" /* all A queries */, "WIFI_AP_DEF" /* softAP netif ID */);// 获取 SoftAP netif 描述符
                s_dns_server_handle = start_dns_server(&dns_config);
            }
            if (s_dns_server_handle == NULL) {
                ESP_LOGE(TAG_AP, "Failed to start DNS server for captive portal");
            }

        }
        else if (bits & WIFI_SCAN_BIT) {
            ESP_LOGI(TAG_AP, "Processing WiFi scan results");
            xEventGroupClearBits(s_wifi_event_group, WIFI_SCAN_BIT); // 清除 WIFI_SCAN_BIT
            uint16_t ap_count;
            ESP_ERROR_CHECK(esp_wifi_scan_get_ap_num(&ap_count));
            if (ap_count > MAX_STA_MUX) {
                ap_count = MAX_STA_MUX; // 限制获取的 AP 数量
            }
            s_ap_record_count = ap_count; // 更新 AP 记录数量
            if (ap_count > 0) {
                ESP_ERROR_CHECK(esp_wifi_scan_get_ap_records(&s_ap_record_count, s_ap_records)); // 获取 AP 记录
                ESP_LOGI(TAG_AP, "Successfully retrieved %d AP records", s_ap_record_count);
            }
            else {
                ESP_LOGI(TAG_AP, "No APs found during scan");
            }

        }
        else if (bits & WIFI_RECONNECT_BIT) {
            ESP_LOGI(TAG_STA, "Reconnecting to AP with provided credentials");
            xEventGroupClearBits(s_wifi_event_group, WIFI_RECONNECT_BIT); // 清除 WIFI_RECONNECT_BIT
            wifi_reconnect_sta(); // 使用新的凭据重新连接 Station 模式
        }
        else {
            ESP_LOGE(TAG_AP, "Unexpected event bits: 0x%ld", bits);
        }

    }
    vTaskDelete(NULL); // 任务结束时删除自身
}

// 初始化 WiFi 连接，包括 Station 和 SoftAP 模式
void wifi_connected(void)
{

    // 降低 HTTP 服务器相关日志级别，避免过多日志输出
    esp_log_level_set("httpd_uri", ESP_LOG_WARN);
    esp_log_level_set("httpd_txrx", ESP_LOG_WARN);
    esp_log_level_set("httpd_parse", ESP_LOG_WARN);

    ESP_ERROR_CHECK(esp_netif_init());                      // 初始化 TCP/IP 协议栈
    ESP_ERROR_CHECK(esp_event_loop_create_default());       // 创建默认事件循环

    s_wifi_event_group = xEventGroupCreate();             // 创建 WiFi 事件组

    // 注册 WiFi 和 IP 事件处理函数
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL));

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));                  // 初始化 WiFi 驱动

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));   // 设置 WiFi 模式为 AP 和 Station 共存

    esp_netif_create_default_wifi_sta();                // 创建默认 Station 网络接口
    esp_netif_create_default_wifi_ap();                 // 创建默认 SoftAP 网络接口

    ESP_LOGI(TAG_STA, "Initializing ESP_WIFI_MODE_STA");
    wifi_init_sta();                                  // 初始化 Station 模式配置 (获取当前配置)

    ESP_ERROR_CHECK(esp_wifi_start());                   // 启动 WiFi

    // 创建 WiFi 事件处理任务
    BaseType_t task_created = xTaskCreate(wifi_event_task, "wifi_event_task", 2.5 * 1024, NULL, 5, NULL);
    if (task_created != pdPASS) {
        ESP_LOGE(TAG_STA, "Failed to create wifi_event_task");
    }
    else {
        ESP_LOGI(TAG_STA, "wifi_event_task created successfully");
    }
}
