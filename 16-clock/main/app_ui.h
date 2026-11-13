#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "lvgl.h"

/* 当前子应用画布与标记，定义在 app_ui.c，供各子应用共享。
 * icon_flag: 0=主界面, 1~6=原六个应用, 7=设置, 8=录音机, 9=环境监测仪, 10=秒表, 11=时钟 */
extern lv_obj_t *icon_in_obj;
extern int icon_flag;

/*********************** 开机界面 ****************************/
// 开机界面
void lv_gui_start(void);


/*********************** 主界面 ****************************/
void lv_main_page(void);

/*********************** 新增子应用入口 ****************************/
void app_settings_open(lv_event_t *e);   // 设置
void app_recorder_open(lv_event_t *e);   // 录音机
void app_monitor_open(lv_event_t *e);    // 环境监测仪
void app_stopwatch_open(lv_event_t *e);  // 秒表/倒计时
void app_clock_open(lv_event_t *e);      // 时钟（实验16新增，每次进入从互联网校准时间）

/*********************** WiFi 开机自动重连 ****************************/
void app_wifi_auto_connect(void);        // 读取 NVS 中已保存的 WiFi 并后台连接

/*********************** WiFi / NTP 复用接口（供时钟等应用调用） ****************************/
bool app_wifi_is_connected(void);                        // 当前是否已连接 WiFi
bool app_wifi_ensure_connected(uint32_t timeout_ms);     // 未连接时用已保存凭据重连，阻塞等待结果
esp_err_t app_ntp_sync_once(uint32_t timeout_ms);        // 执行一次 NTP 对时并设置东八区时区


/*********************** 音乐播放器 ****************************/
void mp3_player_init(void);
void music_ui(void);

void ai_gui_in(void);
void ai_gui_out(void);

void ai_play(void);
void ai_pause(void);
void ai_resume(void);
void ai_prev_music(void);
void ai_next_music(void);
void ai_volume_up(void);
void ai_volume_down(void);



void ai_open_icon1(void);
void ai_open_icon2(void);
void ai_open_icon3(void);
void ai_open_icon4(void);
void ai_open_icon5(void);
void ai_open_icon6(void);
void ai_tuichu(void);

