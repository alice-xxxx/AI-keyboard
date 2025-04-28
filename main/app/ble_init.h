#pragma once

#include "esp_err.h"
#define CONTAINMENT_TABLE 1
#define KEY_TABLE 0
#ifdef _cplusplus
extern "c"
{
#endif

    // 存在空格可相与
    typedef enum
    {
        // 数字键盘（按钮1-10）
        figure1 = 0b00000001,
        figure2,
        figure3,
        figure4,
        figure5,
        figure6,
        figure7,
        figure8,
        figure9,
        figure10,

        // 通道值（-1、0或1）
        channel1 = 0b00010000,
        channel2,
        channel3,

        // 音量增加 音量减少
        volumeup = 0b01000000,
        volumedown = 0b10000000,
    } Consumer1byte;
    typedef enum
    {
        // 静音、电源、回忆最后、分配选择、播放、暂停、录音、快进、倒带、扫描下一个、扫描上一个和停止
        Consumer_Mute = 0b00000001,
        Consumer_Power,
        Consumer_Recall_Last,
        Consumer_Assign_Select,
        Consumer_Play,
        Consumer_Pause,
        Consumer_Record,
        Consumer_Fast_Forward,
        Consumer_Rewind,
        Consumer_Scan_Next,
        Consumer_Scan_Previous,
        Consumer_Stop,

        // 选择按钮1-3
        selectorbutton1 = 0b00010000,
        selectorbutton2,
        selectorbutton3,
    } Consumer2byte;

    // 定义键码结构体
    typedef struct
    {
        const char *key_str;
        uint8_t key_code;
    } KeyCode;


    static const KeyCode containment_table[] = {
        //-------------------修饰符-------------------------
        {"LEFT_CTRL", 0x01},
        {"LEFT_SHIFT", 0x02},
        {"LEFT_ALT", 0x04},
        {"LEFT_GUI", 0x08},
        {"RIGHT_CTRL", 0x10},
        {"RIGHT_SHIFT", 0x20},
        {"RIGHT_ALT", 0x40},
        {"RIGHT_GUI", 0x80},
        {NULL, 0}

    };
    // 创建键码查找表 (数组)
    // 蓝牙 HID 键盘键码查找表
    static const KeyCode key_table[] = {
        //------------------- 字母 -------------------------
        {"a", 0x04}, //<
        {"b", 0x05},
        {"c", 0x06},
        {"d", 0x07},
        {"e", 0x08},
        {"f", 0x09},
        {"g", 0x0A},
        {"h", 0x0B},
        {"i", 0x0C},
        {"j", 0x0D},
        {"k", 0x0E},
        {"l", 0x0F},
        {"m", 0x10},
        {"n", 0x11},
        {"o", 0x12},
        {"p", 0x13},
        {"q", 0x14},
        {"r", 0x15},
        {"s", 0x16},
        {"t", 0x17},
        {"u", 0x18},
        {"v", 0x19},
        {"w", 0x1A},
        {"x", 0x1B},
        {"y", 0x1C},
        {"z", 0x1D},

        //------------------- 数字 -------------------------
        {"1", 0x1E}, //<
        {"2", 0x1F},
        {"3", 0x20},
        {"4", 0x21},
        {"5", 0x22},
        {"6", 0x23},
        {"7", 0x24},
        {"8", 0x25},
        {"9", 0x26},
        {"0", 0x27},

        //------------------- 功能键 -------------------------
        {"enter", 0x28},
        {"esc", 0x29},
        {"backspace", 0x2A},
        {"tab", 0x2B},
        {"space", 0x2C},
        {"-", 0x2D},  // 减号/连字符
        {"=", 0x2E},  // 等于号
        {"[", 0x2F},  // 左方括号
        {"]", 0x30},  // 右方括号
        {"\\", 0x31}, // 反斜杠
        {"#", 0x32},  //  井号 (在某些键盘布局上)
        {";", 0x33},  // 分号
        {"'", 0x34},  // 单引号
        {"`", 0x35},  // 反引号/重音符
        {",", 0x36},  // 逗号
        {".", 0x37},  // 句点/点
        {"/", 0x38},  // 斜杠
        {"capslock", 0x39},
        {"f1", 0x3A},
        {"f2", 0x3B},
        {"f3", 0x3C},
        {"f4", 0x3D},
        {"f5", 0x3E},
        {"f6", 0x3F},
        {"f7", 0x40},
        {"f8", 0x41},
        {"f9", 0x42},
        {"f10", 0x43},
        {"f11", 0x44},
        {"f12", 0x45},
        {"printscreen", 0x46},
        {"scrolllock", 0x47},
        {"pause", 0x48}, // Pause/Break
        {"insert", 0x49},
        {"home", 0x4A},
        {"pageup", 0x4B},
        {"delete", 0x4C},
        {"end", 0x4D},
        {"pagedown", 0x4E},
        {"right", 0x4F}, // 右箭头
        {"left", 0x50},  // 左箭头
        {"down", 0x51},  // 下箭头
        {"up", 0x52},    // 上箭头

        //------------------- 小键盘 -------------------------
        {"numlock", 0x53},     //>
        {"kp_divide", 0x54},   // 小键盘 除号
        {"kp_multiply", 0x55}, // 小键盘 乘号
        {"kp_minus", 0x56},    // 小键盘 减号
        {"kp_plus", 0x57},     // 小键盘 加号
        {"kp_enter", 0x58},    // 小键盘 Enter
        {"kp_1", 0x59},
        {"kp_2", 0x5A},
        {"kp_3", 0x5B},
        {"kp_4", 0x5C},
        {"kp_5", 0x5D},
        {"kp_6", 0x5E},
        {"kp_7", 0x5F},
        {"kp_8", 0x60},
        {"kp_9", 0x61},
        {"kp_0", 0x62},
        {"kp_decimal", 0x63}, // 小键盘 小数点

        //------------------- 其他特定机器支持 -------------------------
        // {"non_us_backslash", 0x64}, // 非US键盘上的反斜杠和竖线 (在某些键盘上)
        // {"application", 0x65},      // 应用程序键 (Windows) /  上下文菜单键
        // {"kp_equals", 0x67},        // 小键盘 =
        // {"f13", 0x68},
        // {"f14", 0x69},
        // {"f15", 0x6A},
        // {"f16", 0x6B},
        // {"f17", 0x6C},
        // {"f18", 0x6D},
        // {"f19", 0x6E},
        // {"f20", 0x6F},
        // {"f21", 0x70},
        // {"f22", 0x71},
        // {"f23", 0x72},
        // {"f24", 0x73},
        // {"execute", 0x74}, // 执行键
        // {"help", 0x75},    // 帮助键
        // {"menu", 0x76},    // 菜单键
        // {"select", 0x77},  // 选择键
        // {"stop", 0x78},    // 停止键
        // {"again", 0x79},   // 再次键
        // {"undo", 0x7A},    // 撤销
        // {"cut", 0x7B},     // 剪切
        // {"copy", 0x7C},    // 复制
        // {"paste", 0x7D},   // 粘贴
        // {"find", 0x7E},    // 查找
        {NULL, 0} // 哨兵, 必须放在最后

    };

    esp_err_t ble_init(void);
    void send_key_press(uint8_t consumer1, uint8_t consumer2, const char *modifier_str, uint8_t key_count, ...);
    void send_key_release();

#ifdef _cplusplus
}
#endif