#include "rotary_encoder.h"
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "driver/pulse_cnt.h"
#include "driver/gpio.h"
#include "esp_sleep.h"
#include "ble_init.h" 

// 宏定义，用于日志输出的TAG
static const char *TAG = "rotary_encoder_example";

// 定义脉冲计数器的高低限制
#define PCNT_HIGH_LIMIT 100
#define PCNT_LOW_LIMIT -100

// 定义EC11 旋转编码器的GPIO引脚
#define EC11_GPIO_A 41
#define EC11_GPIO_B 42

// 定义按键扫描码，用于模拟按键事件，根据实际的 ble_init.h 中的定义修改
#define KEY_LEFT_SCAN_CODE  0b01000000
#define KEY_RIGHT_SCAN_CODE 0b10000000

// 脉冲计数阈值，只有当脉冲计数变化超过此值时才触发按键事件
#define PULSE_THRESHOLD 4

// PCNT 单元句柄
pcnt_unit_handle_t pcnt_unit = NULL;
// 队列句柄，用于在中断服务程序和任务之间传递事件
QueueHandle_t event_queue = NULL;

/**
 * @brief PCNT 达到监视点事件的回调函数
 *
 * 当 PCNT 计数器值达到预设的监视点时，此函数被中断服务程序调用。
 * 它将监视点的值发送到队列，以通知 rotary_task 任务。
 *
 * @param unit PCNT 单元句柄
 * @param edata 事件数据，包含监视点值
 * @param user_ctx 用户上下文，这里是队列句柄
 * @return 返回 pdTRUE 如果需要唤醒高优先级任务，否则返回 pdFALSE
 */
static bool pcnt_reach_watch_point_callback(pcnt_unit_handle_t unit, const pcnt_watch_event_data_t *edata, void *user_ctx)
{
    BaseType_t high_task_wakeup = pdFALSE;
    QueueHandle_t queue = (QueueHandle_t)user_ctx;
    // 从中断服务程序发送事件数据到队列
    xQueueSendFromISR(queue, &(edata->watch_point_value), &high_task_wakeup);
    return (high_task_wakeup == pdTRUE);
}

/**
 * @brief 旋转编码器处理任务
 *
 * 此任务负责读取旋转编码器的计数，检测旋转方向，并模拟按键事件。
 * 只有当脉冲计数变化绝对值超过 PULSE_THRESHOLD 时才触发按键事件。
 */
void rotary_task(void *arg)
{
    int pulse_count = 0; // 当前脉冲计数值
    int last_pulse_count = 0; // 上一次脉冲计数值
    int event_count = 0; // 从队列接收到的事件计数值
    int pulse_diff = 0; // 脉冲计数差值

    while (1)
    {

        // 尝试从队列接收事件，超时时间为 10ms
        if (xQueueReceive(event_queue, &event_count, pdMS_TO_TICKS(10)))
        {

            ESP_LOGI(TAG, "Watch point event, count: %d", event_count);
        }

        // 获取当前的脉冲计数值
        if (ESP_OK == pcnt_unit_get_count(pcnt_unit, &pulse_count))
        {
            pulse_diff = pulse_count - last_pulse_count; // 计算脉冲差值
            if (pulse_diff >= PULSE_THRESHOLD)
            {
                // 顺时针旋转，且脉冲计数增加超过阈值
                ESP_LOGI(TAG, "Pulse count: %d, Clockwise, diff: %d", pulse_count, pulse_diff);
                send_key_press(KEY_RIGHT_SCAN_CODE, 0b00000000, NULL, 0); // 发送向右按键事件
                vTaskDelay(pdMS_TO_TICKS(10)); // 短暂延时
                send_key_release(); // 发送按键释放事件
                last_pulse_count = pulse_count; // 更新上一次脉冲计数值
            }
            else if (pulse_diff <= -PULSE_THRESHOLD)
            {
                // 逆时针旋转，且脉冲计数减少超过阈值
                ESP_LOGI(TAG, "Pulse count: %d, Counter-Clockwise, diff: %d", pulse_count, pulse_diff);
                send_key_press(KEY_LEFT_SCAN_CODE, 0b00000000, NULL, 0);  // 发送向左按键事件
                vTaskDelay(pdMS_TO_TICKS(10)); // 短暂延时
                send_key_release(); // 发送按键释放事件
                last_pulse_count = pulse_count; // 更新上一次脉冲计数值
            }
            // 如果脉冲变化小于阈值，则不触发按键事件，也不更新 last_pulse_count
        }
        else
        {
            ESP_LOGE(TAG, "Failed to get pulse count"); // 错误日志
        }
        vTaskDelay(pdMS_TO_TICKS(10)); // 任务延时
    }
}

/**
 * @brief 旋转编码器初始化函数
 *
 * 配置 PCNT 单元和通道，设置滤波，注册事件回调，并启动 PCNT 单元和 rotary_task。
 */
void rotary_encoder_init(void)
{
    ESP_LOGI(TAG, "Initialize PCNT unit");

    // PCNT 单元配置
    pcnt_unit_config_t unit_config = {
        .high_limit = PCNT_HIGH_LIMIT,
        .low_limit = PCNT_LOW_LIMIT,
    };
    ESP_ERROR_CHECK(pcnt_new_unit(&unit_config, &pcnt_unit)); // 创建 PCNT 单元

    // 设置毛刺滤波器
    ESP_LOGI(TAG, "Set glitch filter");
    pcnt_glitch_filter_config_t filter_config = {
        .max_glitch_ns = 100, // 最大毛刺时间 100ns
    };
    ESP_ERROR_CHECK(pcnt_unit_set_glitch_filter(pcnt_unit, &filter_config)); // 设置毛刺滤波器

    // PCNT 通道配置
    ESP_LOGI(TAG, "Install PCNT channels");
    pcnt_chan_config_t chan_a_config = {
        .edge_gpio_num = EC11_GPIO_A, // A 相 GPIO
        .level_gpio_num = EC11_GPIO_B, // B 相 GPIO，用于判断方向
    };
    pcnt_channel_handle_t pcnt_chan_a = NULL;
    ESP_ERROR_CHECK(pcnt_new_channel(pcnt_unit, &chan_a_config, &pcnt_chan_a)); // 创建通道 A

    pcnt_chan_config_t chan_b_config = {
        .edge_gpio_num = EC11_GPIO_B, // B 相 GPIO
        .level_gpio_num = EC11_GPIO_A, // A 相 GPIO，用于判断方向
    };
    pcnt_channel_handle_t pcnt_chan_b = NULL;
    ESP_ERROR_CHECK(pcnt_new_channel(pcnt_unit, &chan_b_config, &pcnt_chan_b)); // 创建通道 B

    // 设置 PCNT 通道的边沿和电平动作，根据 EC11 编码器原理配置
    ESP_LOGI(TAG, "Set edge and level actions for PCNT channels");
    ESP_ERROR_CHECK(pcnt_channel_set_edge_action(pcnt_chan_a, PCNT_CHANNEL_EDGE_ACTION_DECREASE, PCNT_CHANNEL_EDGE_ACTION_INCREASE)); // 通道 A 上升沿计数增加，下降沿计数减少
    ESP_ERROR_CHECK(pcnt_channel_set_level_action(pcnt_chan_a, PCNT_CHANNEL_LEVEL_ACTION_KEEP, PCNT_CHANNEL_LEVEL_ACTION_INVERSE)); // 通道 A 电平反转时计数方向反转
    ESP_ERROR_CHECK(pcnt_channel_set_edge_action(pcnt_chan_b, PCNT_CHANNEL_EDGE_ACTION_INCREASE, PCNT_CHANNEL_EDGE_ACTION_DECREASE)); // 通道 B 上升沿计数增加，下降沿计数减少
    ESP_ERROR_CHECK(pcnt_channel_set_level_action(pcnt_chan_b, PCNT_CHANNEL_LEVEL_ACTION_KEEP, PCNT_CHANNEL_LEVEL_ACTION_INVERSE)); // 通道 B 电平反转时计数方向反转

    // 添加监视点和注册事件回调
    ESP_LOGI(TAG, "Add watch points and register callbacks");
    int watch_points[] = { PCNT_LOW_LIMIT, -50, 0, 50, PCNT_HIGH_LIMIT }; // 监视点数组
    for (size_t i = 0; i < sizeof(watch_points) / sizeof(watch_points[0]); i++)
    {
        ESP_ERROR_CHECK(pcnt_unit_add_watch_point(pcnt_unit, watch_points[i])); // 添加监视点
    }
    pcnt_event_callbacks_t cbs = {
        .on_reach = pcnt_reach_watch_point_callback, // 注册监视点到达回调函数
    };
    event_queue = xQueueCreate(10, sizeof(int)); // 创建队列，队列长度为 10，每个元素大小为 int
    ESP_ERROR_CHECK(pcnt_unit_register_event_callbacks(pcnt_unit, &cbs, event_queue)); // 注册事件回调函数

    // 启用 PCNT 单元
    ESP_LOGI(TAG, "Enable PCNT unit");
    ESP_ERROR_CHECK(pcnt_unit_enable(pcnt_unit)); // 启用 PCNT 单元

    // 清零 PCNT 计数器
    ESP_LOGI(TAG, "Clear PCNT unit");
    ESP_ERROR_CHECK(pcnt_unit_clear_count(pcnt_unit)); // 清零计数器

    // 启动 PCNT 单元
    ESP_LOGI(TAG, "Start PCNT unit");
    ESP_ERROR_CHECK(pcnt_unit_start(pcnt_unit)); // 启动计数

    // 创建旋转编码器处理任务
    xTaskCreate(rotary_task, "rotary_encoder_task", 2.5 * 1024, NULL, 5, NULL);
}