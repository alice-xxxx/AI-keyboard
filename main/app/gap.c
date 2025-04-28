#include "gap.h" // 假设 gap.h 是项目相关的头文件，包含必要的定义

#include "host/ble_hs.h" // NimBLE host 核心头文件
#include "host/util/util.h" // NimBLE 实用工具头文件
#include "services/gap/ble_svc_gap.h" // NimBLE GAP 服务头文件

#include "gatt_svc.h" // 假设 gatt_svc.h 是项目相关的 GATT 服务头文件

#include <string.h> // 引入 string.h 以使用 memset 和 strlen
#include <stdio.h>  // 引入 stdio.h 以兼容 ESP_LOGI 等宏定义，根据实际 ESP-IDF 环境可能不需要显式引入



#define MFG_DATA_COMPANY_ID 0x0600 // 厂商ID，示例值，需要根据实际情况修改
#define MFG_DATA_SUBTYPE 0x03       // 厂商数据子类型，示例值，需要根据实际情况修改
#define MFG_DATA_CUSTOM_VALUE 0x80  // 自定义厂商数据值，示例值，需要根据实际情况修改

static const char *TAG = "BLE_GAP"; // 模块日志TAG，使用 static const char* 提高效率和代码可读性

/* Private function declarations */
static void print_conn_desc(struct ble_gap_conn_desc *desc); // 打印连接描述符信息
static void start_advertising(void);                         // 启动广播
static int gap_event_handler(struct ble_gap_event *event, void *arg); // GAP 事件处理函数

/* Private variables */
static uint8_t own_addr_type;                                  // 本设备地址类型
static uint8_t ble_mfg_data_bytes[] = {                       // 厂商自定义广播数据
    (MFG_DATA_COMPANY_ID & 0xFF),         // 厂商ID 低字节
    (MFG_DATA_COMPANY_ID >> 8) & 0xFF,    // 厂商ID 高字节
    MFG_DATA_SUBTYPE,                     // 厂商数据子类型
    0x00,                                 // 预留字段
    MFG_DATA_CUSTOM_VALUE                  // 自定义数据值
};
static ble_uuid16_t hid_service_uuid = BLE_UUID16_INIT(0x1812); // HID 服务 UUID

/* Global variable - 注意全局变量的使用，在多文件项目中需要谨慎 */
uint16_t gatt_connection_handle = 0; // GATT 连接句柄，用于跟踪当前连接，注意在多线程或中断中使用时需要考虑线程安全

/**
 * @brief 打印连接描述符信息
 *
 * @param desc 连接描述符结构体指针
 */
static void print_conn_desc(struct ble_gap_conn_desc *desc)
{
    ESP_LOGI(TAG, "Connection Details:");
    ESP_LOGI(TAG, "  Connection Handle: %d", desc->conn_handle);
    ESP_LOGI(TAG, "  Local ID Address:  Type=%d, Value=%02X:%02X:%02X:%02X:%02X:%02X",
        desc->our_id_addr.type, desc->our_id_addr.val[0], desc->our_id_addr.val[1],
        desc->our_id_addr.val[2], desc->our_id_addr.val[3], desc->our_id_addr.val[4],
        desc->our_id_addr.val[5]);
    ESP_LOGI(TAG, "  Peer ID Address:   Type=%d, Value=%02X:%02X:%02X:%02X:%02X:%02X",
        desc->peer_id_addr.type, desc->peer_id_addr.val[0], desc->peer_id_addr.val[1],
        desc->peer_id_addr.val[2], desc->peer_id_addr.val[3], desc->peer_id_addr.val[4],
        desc->peer_id_addr.val[5]);
    ESP_LOGI(TAG, "  Connection Parameters: Interval=%d, Latency=%d, Timeout=%d",
        desc->conn_itvl, desc->conn_latency, desc->supervision_timeout);
    ESP_LOGI(TAG, "  Security State: Encrypted=%d, Authenticated=%d, Bonded=%d",
        desc->sec_state.encrypted, desc->sec_state.authenticated, desc->sec_state.bonded);
}

/**
 * @brief 启动 BLE 广播
 *
 */
static void start_advertising(void)
{
    int rc = 0;
    const char *name;
    uint16_t appearance;
    struct ble_hs_adv_fields adv_fields = { 0 };     // 广播数据字段结构体
    struct ble_gap_adv_params adv_params = { 0 };     // 广播参数结构体

    /* 配置广播 Flags，LE Limited Discoverable Mode | BR/EDR Not Supported */
    adv_fields.flags = BLE_HS_ADV_F_DISC_LTD | BLE_HS_ADV_F_BREDR_UNSUP;

    /* 配置 16-bit UUIDs，指定 HID Service UUID */
    adv_fields.uuids16 = &hid_service_uuid;
    adv_fields.num_uuids16 = 1;
    adv_fields.uuids16_is_complete = 1; // 完整 UUID 列表

    /* 配置设备外观 */
    appearance = ble_svc_gap_device_appearance(); // 获取 GAP 服务中设置的设备外观值
    adv_fields.appearance = appearance;
    adv_fields.appearance_is_present = 1;

    /* 配置 Manufacturer Specific Data */
    adv_fields.mfg_data = ble_mfg_data_bytes;
    adv_fields.mfg_data_len = sizeof(ble_mfg_data_bytes);

    /* 配置设备名称 */
    name = ble_svc_gap_device_name(); // 获取 GAP 服务中设置的设备名称
    adv_fields.name = (uint8_t *)name;
    adv_fields.name_len = strlen(name);
    adv_fields.name_is_complete = 1; // 完整设备名称

    /* 设置广播数据 */
    rc = ble_gap_adv_set_fields(&adv_fields);
    if (rc != 0) {
        ESP_LOGE(TAG, "Failed to set advertising data, error code: %d", rc);
        return;
    }

    /* 配置广播参数: 非连接性广播, 通用可发现模式 */
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;     // 非连接性广播
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;     // 通用可发现模式

    /* 配置广播间隔 */
    adv_params.itvl_min = BLE_GAP_ADV_ITVL_MS(20);     // 最小广播间隔 20ms
    adv_params.itvl_max = BLE_GAP_ADV_ITVL_MS(30);     // 最大广播间隔 30ms

    /* 启动广播 */
    rc = ble_gap_adv_start(own_addr_type, NULL, BLE_HS_FOREVER, &adv_params,
        gap_event_handler, NULL);
    if (rc != 0) {
        ESP_LOGE(TAG, "Failed to start advertising, error code: %d", rc);
        return;
    }
    ESP_LOGI(TAG, "Advertising started!");
}

/**
 * @brief GAP 事件处理函数，NimBLE 的 GAP 服务使用事件驱动模型
 *        当 GAP 事件发生时，此回调函数会被 NimBLE 协议栈调用
 *
 * @param event GAP 事件结构体指针
 * @param arg 用户参数，这里未使用
 * @return int 0 表示成功，非 0 表示失败
 */
static int gap_event_handler(struct ble_gap_event *event, void *arg)
{
    int rc = 0;
    struct ble_gap_conn_desc desc; // 连接描述符

    switch (event->type) {
    case BLE_GAP_EVENT_PASSKEY_ACTION: // 秘钥操作事件
        struct ble_gap_passkey_params passkey_params = event->passkey.params;
        ESP_LOGI(TAG, "Passkey Action Op: %d", passkey_params.action);
        if (passkey_params.action == BLE_SM_IOACT_DISP) { // 显示 PIN 码
            uint32_t pin_code = passkey_params.numcmp;
            ESP_LOGI(TAG, "Display Passkey: %06lu", pin_code); // 打印 PIN 码到日志，实际应用中应显示在设备屏幕上
        }
        else if (passkey_params.action == BLE_SM_IOACT_NUMCMP) { // 数字比较
            ESP_LOGI(TAG, "Numeric Compare Value: %06lu", passkey_params.numcmp);
            struct ble_sm_io rsp;
            memset(&rsp, 0, sizeof(rsp));
            rsp.action = BLE_SM_IOACT_NUMCMP;
            rsp.numcmp_accept = 1; // 自动接受数字比较，实际应用中应由用户确认
            rc = ble_sm_inject_io(event->passkey.conn_handle, &rsp);
            if (rc != 0) {
                ESP_LOGE(TAG, "ble_sm_inject_io (NUMCMP_ACCEPT) failed: %d", rc);
                return rc;
            }
        }
        else {
            ESP_LOGW(TAG, "Unexpected passkey action: %d", passkey_params.action);
        }
        return 0;

    case BLE_GAP_EVENT_CONNECT: // 连接事件
        ESP_LOGI(TAG, "Connection %s; status=%d",
            event->connect.status == 0 ? "established" : "failed",
            event->connect.status);

        if (event->connect.status == 0) { // 连接成功
            rc = ble_gap_conn_find(event->connect.conn_handle, &desc); // 获取连接描述符
            if (rc != 0) {
                gatt_connection_handle = 0; // 连接失败，重置连接句柄
                ESP_LOGE(TAG, "Failed to find connection by handle, error code: %d", rc);
                return rc;
            }
            gatt_connection_handle = event->connect.conn_handle; // 保存连接句柄

            print_conn_desc(&desc); // 打印连接信息

            /* 更新连接参数 */
            struct ble_gap_upd_params params = {
                .itvl_min = desc.conn_itvl,
                .itvl_max = desc.conn_itvl,
                .latency = 3,
                .supervision_timeout = desc.supervision_timeout
            };
            rc = ble_gap_update_params(event->connect.conn_handle, &params);
            if (rc != 0) {
                ESP_LOGE(TAG, "Failed to update connection parameters, error code: %d", rc);
                return rc;
            }
        }
        else { // 连接失败，重新开始广播
            start_advertising();
        }
        return rc;

    case BLE_GAP_EVENT_DISCONNECT: // 断开连接事件
        ESP_LOGI(TAG, "Disconnected from peer; reason=%d", event->disconnect.reason);
        gatt_connection_handle = 0; // 断开连接，重置连接句柄
        start_advertising(); // 重新开始广播
        return rc;

    case BLE_GAP_EVENT_CONN_UPDATE: // 连接参数更新事件
        ESP_LOGI(TAG, "Connection updated; status=%d", event->conn_update.status);

        rc = ble_gap_conn_find(event->conn_update.conn_handle, &desc); // 获取连接描述符
        if (rc != 0) {
            ESP_LOGE(TAG, "Failed to find connection by handle, error code: %d", rc);
            return rc;
        }
        print_conn_desc(&desc); // 打印连接信息
        return rc;

    case BLE_GAP_EVENT_ADV_COMPLETE: // 广播完成事件
        ESP_LOGI(TAG, "Advertise complete; reason=%d", event->adv_complete.reason);
        start_advertising(); // 重新开始广播
        return rc;

    case BLE_GAP_EVENT_NOTIFY_TX: // Notification 发送事件
        if ((event->notify_tx.status != 0) && (event->notify_tx.status != BLE_HS_EDONE)) {
            ESP_LOGI(TAG, "Notify event; conn_handle=%d attr_handle=%d status=%d is_indication=%d",
                event->notify_tx.conn_handle, event->notify_tx.attr_handle,
                event->notify_tx.status, event->notify_tx.indication);
        }
        return rc;

    case BLE_GAP_EVENT_SUBSCRIBE: // 订阅事件
        ESP_LOGI(TAG, "Subscribe event; conn_handle=%d attr_handle=%d reason=%d prevn=%d curn=%d previ=%d curi=%d",
            event->subscribe.conn_handle, event->subscribe.attr_handle,
            event->subscribe.reason, event->subscribe.prev_notify,
            event->subscribe.cur_notify, event->subscribe.prev_indicate,
            event->subscribe.cur_indicate);
        // gatt_svr_subscribe_cb(event); // GATT 服务订阅回调，如果需要处理订阅事件，则取消注释并实现该函数
        return rc;

    case BLE_GAP_EVENT_MTU: // MTU 更新事件
        ESP_LOGI(TAG, "MTU update event; conn_handle=%d cid=%d mtu=%d",
            event->mtu.conn_handle, event->mtu.channel_id, event->mtu.value);
        return rc;

    default:
        ESP_LOGI(TAG, "Unhandled GAP event type: %d", event->type); // 默认处理，打印未处理的事件类型
        return 0;
    }
}

/**
 * @brief 初始化 BLE 广播
 *
 * @return esp_err_t ESP 错误码
 */
esp_err_t adv_init(void)
{
    esp_err_t rc = 0;
    uint8_t addr_val[6] = { 0 };

    /* 确保设备具有有效的 BT 身份地址 (首选随机地址) */
    rc = ble_hs_util_ensure_addr(0);
    if (rc != 0) {
        ESP_LOGE(TAG, "Device does not have any available BT address!");
        return rc;
    }

    /* 推断用于广播的 BT 地址类型 (当前不使用隐私) */
    rc = ble_hs_id_infer_auto(0, &own_addr_type);
    if (rc != 0) {
        ESP_LOGE(TAG, "Failed to infer address type, error code: %d", rc);
        return rc;
    }

    /* 复制并打印设备地址 */
    rc = ble_hs_id_copy_addr(own_addr_type, addr_val, NULL);
    if (rc != 0) {
        ESP_LOGE(TAG, "Failed to copy device address, error code: %d", rc);
        return rc;
    }
    ESP_LOGI(TAG, "Device Address: %02X:%02X:%02X:%02X:%02X:%02X",
        addr_val[0], addr_val[1], addr_val[2],
        addr_val[3], addr_val[4], addr_val[5]);

    /* 启动广播 */
    start_advertising();

    return rc;
}

/**
 * @brief 初始化 GAP 服务
 *
 * @return esp_err_t ESP 错误码
 */
esp_err_t gap_init(void)
{
    esp_err_t rc = 0;

    /* 初始化 NimBLE GAP 服务 */
    ble_svc_gap_init();

    /* 设置 GAP 设备名称和外观 */
    rc = ble_svc_gap_device_name_set(DEVICE_NAME);
    rc |= ble_svc_gap_device_appearance_set(DEVICE_APPEARANCE);
    if (rc != 0) {
        ESP_LOGE(TAG, "Failed to set device name to %s, error code: %d", DEVICE_NAME, rc);
        return rc;
    }
    ESP_LOGI(TAG, "GAP service initialized, device name: %s, appearance: %d", DEVICE_NAME, DEVICE_APPEARANCE);
    return rc;
}