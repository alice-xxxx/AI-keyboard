#include "ble_init.h" // 项目相关的头文件，包含 BLE 初始化相关的声明，根据实际项目调整

#include "esp_log.h" // ESP-IDF 日志库

#include "nimble/nimble_port.h" // NimBLE 移植层头文件
#include "host/ble_gap.h"       // NimBLE GAP 头文件
#include "services/gatt/ble_svc_gatt.h" // NimBLE GATT 服务头文件

#include "gap.h"       // 项目相关的 GAP 功能头文件
#include "gatt_svc.h"  // 项目相关的 GATT 服务头文件
#include <stdarg.h>  // 引入 stdarg.h 为了使用可变参数列表
#include <string.h>  // 引入 string.h 为了使用 strcmp

static const char *TAG = "BLE_INIT"; // 模块日志 TAG

/* 外部函数声明，假设 ble_store_config_init 在其他地方实现，用于初始化 BLE 存储配置 */
void ble_store_config_init(void);

/* 静态函数声明 (内部使用) */
static void on_stack_reset(int reason);                // NimBLE 协议栈复位回调
static void on_stack_sync(void);                 // NimBLE 协议栈同步回调
static void nimble_host_config_init(void);             // NimBLE Host 配置初始化

/* 假设 containment_table 和 key_table 以及 KeyCode 结构体在 gatt_svc.h 中定义，如果没有，需要包含正确的头文件并确保定义 */
/* 例如： #include "gatt_svc.h"  或者 #include "key_tables.h" 如果定义在单独的文件中 */
// 假设此函数在 gatt_svc.h 或其他地方声明

/* 堆栈事件回调函数 */
/**
 * @brief 当 NimBLE Host 协议栈因为错误复位时被调用
 *
 * @param reason 复位原因代码
 */
static void on_stack_reset(int reason)
{
    ESP_LOGI(TAG, "NimBLE stack reset, reset reason: %d", reason);
}

/**
 * @brief 当 NimBLE Host 协议栈与 Controller 同步后被调用
 *        在此函数中通常启动广播等操作
 */
static void on_stack_sync(void)
{
    adv_init(); // 调用 adv_init 函数启动 BLE 广播 (实现在 gap.c 中)
}

/**
 * @brief 初始化 NimBLE Host 的配置
 *        设置各种回调函数、安全设置等
 */
static void nimble_host_config_init(void)
{
    /* 配置 NimBLE Host 相关的回调函数 */
    ble_hs_cfg.reset_cb = on_stack_reset;          // 协议栈复位回调
    ble_hs_cfg.sync_cb = on_stack_sync;           // 协议栈同步回调
    ble_hs_cfg.gatts_register_cb = gatt_svr_register_cb; // GATT 服务注册回调 (实现在 gatt_svc.c 中)
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr; // 存储状态回调，用于处理配对信息存储状态
    ble_hs_cfg.sm_io_cap = BLE_HS_IO_DISPLAY_YESNO;    // 配置 IO 能力为仅键盘，设备作为键盘设备，不需要显示和输入
    ble_hs_cfg.sm_bonding = 1;                         // 启用 Bonding 功能，允许设备配对并保存配对信息
    ble_hs_cfg.sm_mitm = 1;                            // 启用中间人攻击 (MITM) 保护，提高安全性
    ble_hs_cfg.sm_sc = 1;                              // 启用安全连接 (Secure Connections) 功能，使用 LE Secure Connections

    /* 初始化 BLE 存储配置，用于持久化存储配对等信息 */
    ble_store_config_init(); // 调用 ble_store_config_init 函数 (需要在其他地方实现)
}

/**
 * @brief NimBLE Host 任务函数
 *        NimBLE 协议栈的主循环在此任务中运行
 *
 * @param param 任务参数，未使用
 */
static void nimble_host_task(void *param)
{
    ESP_LOGI(TAG, "NimBLE host task has been started!");
    /* 运行 NimBLE 协议栈，此函数将阻塞直到 nimble_port_stop() 被调用 */
    nimble_port_run();

    /* 任务退出清理，删除任务自身 */
    vTaskDelete(NULL);
}

/**
 * @brief 初始化 BLE 模块
 *        包括 NimBLE 协议栈初始化、GAP 和 GATT 服务初始化、Host 配置初始化，并创建 NimBLE Host 任务
 *
 * @return esp_err_t ESP 错误码，ESP_OK 表示成功
 */
esp_err_t ble_init(void)
{
    esp_err_t ret = ESP_OK;
    int rc = 0;

    /* 初始化 NimBLE 协议栈 Porting Layer */
    ret = nimble_port_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize nimble port, error code: %d", ret);
        return ESP_FAIL; // 初始化失败返回错误
    }

    /* 初始化 GAP 服务 (Generic Access Profile)，用于设备发现和连接管理 */
    rc = gap_init(); // 调用 gap_init 函数 (实现在 gap.c 中)
    if (rc != 0) {
        ESP_LOGE(TAG, "Failed to initialize GAP service, error code: %d", rc);
        return ESP_FAIL; // GAP 服务初始化失败返回错误
    }

    /* 初始化 GATT 服务 (Generic Attribute Profile)，用于数据交换 */
    rc = gatt_svc_init(); // 调用 gatt_svc_init 函数 (实现在 gatt_svc.c 中)
    if (rc != 0) {
        ESP_LOGE(TAG, "Failed to initialize GATT server, error code: %d", rc);
        return ESP_FAIL; // GATT 服务初始化失败返回错误
    }

    /* 初始化 NimBLE Host 配置，包括设置回调函数和安全参数 */
    nimble_host_config_init();

    /* 创建 NimBLE Host 任务，用于运行 NimBLE 协议栈 */
    if (xTaskCreate(nimble_host_task, "NimBLE Host", 4096, NULL, 5, NULL) != pdPASS) { // 创建任务，栈大小 4K
        ESP_LOGE(TAG, "Failed to create NimBLE host task");
        return ESP_FAIL; // 任务创建失败返回错误
    }

    return ESP_OK; // BLE 初始化成功
}

/**
 * @brief 根据按键字符串获取对应的键码
 *
 * @param key_str 按键字符串 (例如 "MOD_CONTROL_LEFT", "KEYCODE_A")
 * @param table_type 表格类型，1 表示 containment_table，2 表示 key_table
 * @return uint8_t 键码，如果未找到则返回 0
 */
uint8_t get_key_code(const char *key_str, uint8_t table_type)
{
    if (key_str == NULL) {
        return 0; // 字符串为空，返回 0 (表示未找到或错误)
    }

    const KeyCode *p = NULL;

    if (table_type == CONTAINMENT_TABLE) {
        p = containment_table; // 选择 containment_table
    }
    else if (table_type == KEY_TABLE) {
        p = key_table;       // 选择 key_table
    }
    else {
        ESP_LOGW(TAG, "Invalid table type: %d", table_type);
        return 0;             // 无效的表格类型
    }

    if (p != NULL) {
        while (p->key_str != NULL) {
            if (strcmp(key_str, p->key_str) == 0) {
                return p->key_code; // 找到匹配的按键字符串，返回对应的键码
            }
            p++; // 指向下一个 KeyCode 结构体
        }
    }

    return 0; // 遍历完表格未找到匹配的按键字符串，返回 0
}

/**
 * @brief 发送按键按下报告
 *        支持 Consumer 键和普通键盘按键，使用可变参数指定多个普通键盘按键
 *
 * @param consumer_code1 Consumer 键码高字节
 * @param consumer_code2 Consumer 键码低字节
 * @param modifier_str 修饰键字符串 (例如 "MOD_CONTROL_LEFT"，可以为 NULL 表示无修饰键)
 * @param key_count 普通键盘按键数量 (最多支持 6 个普通按键)
 * @param ... 可变参数列表，按键字符串，最多 key_count 个，例如 "KEYCODE_A", "KEYCODE_B"
 */
void send_key_press(uint8_t consumer1, uint8_t consumer2, const char *modifier_str, uint8_t key_count, ...)
{
    va_list valist;           // 定义 va_list 变量，用于访问可变参数列表
    va_start(valist, key_count); // 初始化 va_list，key_count 是最后一个固定参数

    // 键盘输入报告缓冲区，大小必须与 HID Report Descriptor 定义匹配。这里假设是 8 字节
    uint8_t key_report[8] = {
        0x00, // 字节 1: 修饰键
        0x00, // 字节 2: 保留字节 (填充)
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00  // 字节 3-8: 按键码 (最多 6 个)
    };


    uint8_t consumer_report[2] = { 0x00, 0x00 };
    consumer_report[0] = consumer1;
    consumer_report[1] = consumer2;
    // 处理修饰键
    if (modifier_str != NULL) {
        key_report[0] = get_key_code(modifier_str, CONTAINMENT_TABLE); // 获取修饰键码
    }

    // 处理普通键盘按键
    for (int i = 0; i < key_count && i < 6; i++) { // 最多处理 6 个按键
        const char *key_str = va_arg(valist, const char *); // 获取可变参数中的按键字符串
        key_report[i + 2] = get_key_code(key_str, KEY_TABLE);     // 获取按键码并填充到报告中
    }

    ESP_LOGI(TAG, "Sending keyboard input report: MOD=0x%02X, Keys=%02X %02X %02X %02X %02X %02X",
        key_report[0], key_report[2], key_report[3], key_report[4], key_report[5], key_report[6], key_report[7]);
    ESP_LOGI(TAG, "发送键盘输入报告 %x %x", consumer_report[0], consumer_report[1]);

    ble_keyboard_send_input_report(key_report, sizeof(key_report));       // 发送键盘输入报告
    ble_keyboard_send_input_report(consumer_report, sizeof(consumer_report)); // 发送 Consumer 报告

    va_end(valist); // 结束可变参数列表的访问
}

/**
 * @brief 发送按键释放报告
 *        将所有按键和修饰键释放，发送空报告
 */
void send_key_release(void)
{
    // 键盘按键释放报告，所有字节设置为 0 表示释放所有按键
    uint8_t key_report[8] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
    // Consumer 键释放报告，设置为 0
    uint8_t consumer_report[2] = { 0x00, 0x00 };

    ble_keyboard_send_input_report(key_report, sizeof(key_report));         // 发送键盘按键释放报告
    ble_keyboard_send_input_report(consumer_report, sizeof(consumer_report)); // 发送 Consumer 键释放报告
}