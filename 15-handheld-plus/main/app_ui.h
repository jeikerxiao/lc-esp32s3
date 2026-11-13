#pragma once

#include "lvgl.h"

/* 当前子应用画布与标记，定义在 app_ui.c，供各子应用共享。
 * icon_flag: 0=主界面, 1~6=原六个应用, 7=设置, 8=录音机, 9=环境监测仪, 10=秒表 */
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

/*********************** WiFi 开机自动重连 ****************************/
void app_wifi_auto_connect(void);        // 读取 NVS 中已保存的 WiFi 并后台连接


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

