#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 本文件是 15-handheld-plus 新增的“公共基础设施”，
 * 把多个子应用都要用到的东西集中管理：
 *   1. NVS 设置（亮度 / 音量 / 开机音效 / 熄屏时间）
 *   2. WiFi 账号密码的 NVS 持久化
 *   3. 主界面顶部状态栏（时钟 / WiFi / 蓝牙 / 内存）
 *   4. 无操作自动熄屏
 *   5. 子应用通用页面骨架（标题栏 + 返回按钮）
 */

/*************************** 1. 系统设置（NVS） ***************************/
typedef struct {
    uint8_t  brightness;      // 背光亮度 0~100
    uint8_t  volume;          // 系统音量 0~100
    bool     boot_sound;      // 是否播放开机音效
    uint16_t idle_timeout_s;  // 无操作多少秒后熄屏，0 表示永不熄屏
} app_settings_t;

extern app_settings_t g_settings;   // 全局设置，settings_load() 后有效

void settings_load(void);           // 从 NVS 读取（首次运行用默认值）
void settings_save(void);           // 写回 NVS

/*************************** 2. WiFi 凭据（NVS） ***************************/
// 读取已保存的 WiFi，成功返回 true 并把 ssid/pass 写入缓冲区
bool wifi_cred_load(char *ssid, size_t ssid_len, char *pass, size_t pass_len);
void wifi_cred_save(const char *ssid, const char *pass);  // 保存
void wifi_cred_clear(void);                               // 清除

/*************************** 3. 主界面状态栏 ***************************/
// 在 parent（主界面）顶部创建一条 320x28 的状态栏，返回状态栏对象
lv_obj_t *statusbar_create(lv_obj_t *parent);
void statusbar_set_wifi(bool connected);   // 点亮/熄灭 WiFi 图标
void statusbar_set_bt(bool active);        // 点亮/熄灭 蓝牙 图标
void statusbar_refresh_time(void);         // 读系统时间刷新时钟（未联网显示占位）
void statusbar_refresh_mem(void);          // 刷新内存占用百分比

/*************************** 4. 自动熄屏 ***************************/
void screen_idle_start(void);   // 启动一个 lv_timer 检测无操作并控制背光
void screen_wake(void);         // 立即点亮并重置计时

/*************************** 5. 子应用通用页面骨架 ***************************/
// 创建标准子应用页面：白底圆角画布(320x240) + 40px 彩色标题栏 + 返回按钮。
// 返回按钮点击时调用 back_cb（由具体应用实现：删画布/反初始化硬件）。
// 内部会给全局 icon_in_obj 赋值，返回该画布对象，应用在其上 y>=40 区域加内容。
lv_obj_t *app_page_open(const char *title, uint32_t title_color_rgb,
                        void (*back_cb)(lv_event_t *));

#ifdef __cplusplus
}
#endif
