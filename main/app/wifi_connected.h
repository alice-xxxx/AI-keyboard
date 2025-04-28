#pragma once

#define MAX_STA_CONN        2
#define MAX_STA_MUX         5
#define ESP_MAXIMUM_RETRY   2
#define WIFI_FAIL_BIT       BIT1
#define WIFI_CONNECTED_BIT  BIT0
#define WIFI_RECONNECT_BIT  BIT3
#define WIFI_SCAN_BIT       BIT2
#define ESP_WIFI_AP_SSID    "ESP_2.4G_SSID"
#define ESP_WIFI_AP_PASSWD  "esp32password"
#define REQUEST_JSON "\"%d\":  {\"%d\": \"%s\",\"%d\" : \"%d\",\"%d\" : \"%d\"}" 
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA2_PSK
#define ESP_WIFI_AP_AUTH_MODE  WIFI_AUTH_WPA2_PSK
#define ESP_WIFI_CHANNEL 1



#ifdef __cplusplus
extern "C" {
#endif






    void wifi_connected(void);




#ifdef __cplusplus
}
#endif
