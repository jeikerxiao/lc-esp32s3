/* 时钟 子应用（实验16新增）
 * 大号数字时钟：HH:MM:SS + 年月日 + 星期。
 * 每次进入都会从互联网(NTP)重新校准一次系统时间；
 * 校准需要 WiFi：若当前未联网，会用 NVS 中已保存的凭据尝试重连。
 * 无保存凭据 / 连接失败 / 校准失败时，显示"上次时间"（当前系统时间）并标记"未校准"，
 * 时钟仍然每秒走动（走的是未经校准的系统时间）。
 *
 * 注意：ESP32-S3 没有电池 RTC，系统时间只在本次开机内有效；
 *       断电重启后需要重新联网校准才能恢复正确时间。
 */
#include "app_common.h"
#include "app_ui.h"
#include "esp_lvgl_port.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <time.h>
#include <stdio.h>
#include <string.h>

LV_FONT_DECLARE(font_alipuhui20);

static const char *TAG = "clock";

extern lv_obj_t *icon_in_obj;   // 应用画布（app_ui.c 中定义）
extern int icon_flag;           // 当前应用标记（app_ui.c 中定义）

/*********************** 内部状态 ***********************/
static lv_timer_t *s_timer = NULL;          // 每秒刷新时钟的定时器
static lv_obj_t *s_time_label = NULL;       // HH:MM:SS 大号标签
static lv_obj_t *s_date_label = NULL;       // 年月日 + 星期
static lv_obj_t *s_status_label = NULL;     // 校准状态提示

static volatile bool s_active = false;      // 应用是否处于前台（返回后置 false）
static volatile bool s_sync_busy = false;   // 校准任务是否正在运行，防止重复创建
static bool s_calibrated = false;           // 本次进入是否已成功校准

static const char *s_weekday[] = {
    "星期日", "星期一", "星期二", "星期三", "星期四", "星期五", "星期六"
};

/*********************** 状态提示（线程安全） ***********************/
// 校准任务运行在独立 FreeRTOS 任务里，更新界面必须加 LVGL 锁并再次确认前台有效
static void clock_set_status(const char *txt, uint32_t color)
{
    if (!s_active) return;
    lvgl_port_lock(0);
    if (s_active && s_status_label != NULL) {
        lv_label_set_text(s_status_label, txt);
        lv_obj_set_style_text_color(s_status_label, lv_color_hex(color), 0);
    }
    lvgl_port_unlock();
}

/*********************** 每秒刷新 ***********************/
static void clock_tick_cb(lv_timer_t *timer)
{
    time_t now;
    struct tm t;
    time(&now);
    localtime_r(&now, &t);

    lv_label_set_text_fmt(s_time_label, "%02d:%02d:%02d",
                          t.tm_hour, t.tm_min, t.tm_sec);
    lv_label_set_text_fmt(s_date_label, "%04d年%02d月%02d日  %s",
                          t.tm_year + 1900, t.tm_mon + 1, t.tm_mday,
                          s_weekday[t.tm_wday & 7]);
}

/*********************** 校准任务 ***********************/
// 独立任务：确保联网 -> NTP 对时 -> 刷新状态。阻塞操作不能放在 LVGL 任务里。
static void clock_sync_task(void *arg)
{
    ESP_LOGI(TAG, "开始联网校准时间");

    clock_set_status("正在连接 WiFi...", 0x2196f3);
    bool conn = app_wifi_ensure_connected(15000);
    if (!conn) {
        ESP_LOGW(TAG, "WiFi 未连接，显示上次时间（未校准）");
        s_calibrated = false;
        clock_set_status("未联网 · 显示上次时间（未校准）", 0xe53935);
        s_sync_busy = false;
        vTaskDelete(NULL);
        return;
    }

    clock_set_status("正在从互联网校准...", 0x2196f3);
    esp_err_t r = app_ntp_sync_once(20000);
    if (r == ESP_OK) {
        s_calibrated = true;
        // 点亮主界面状态栏 WiFi 图标并同步其时钟
        lvgl_port_lock(0);
        if (s_active) {
            statusbar_set_wifi(true);
            statusbar_refresh_time();
        }
        lvgl_port_unlock();
        clock_set_status("已校准 · 东八区时间", 0x2e7d32);
        ESP_LOGI(TAG, "NTP 校准成功");
    } else {
        s_calibrated = false;
        clock_set_status("校准失败 · 显示上次时间（未校准）", 0xe53935);
        ESP_LOGW(TAG, "NTP 校准失败: %s", esp_err_to_name(r));
    }

    s_sync_busy = false;
    vTaskDelete(NULL);
}

// 触发一次校准（若已有任务在跑则忽略）
static void clock_start_sync(void)
{
    if (s_sync_busy) return;
    s_sync_busy = true;
    xTaskCreatePinnedToCore(clock_sync_task, "clock_sync", 4 * 1024, NULL, 5, NULL, 0);
}

/*********************** 事件回调 ***********************/
static void recalibrate_cb(lv_event_t *e)
{
    clock_start_sync();
}

static void back_cb(lv_event_t *e)
{
    s_active = false;                 // 先置无效，校准任务据此跳过界面更新
    if (s_timer != NULL) {
        lv_timer_del(s_timer);
        s_timer = NULL;
    }
    lv_obj_del(icon_in_obj);
    icon_in_obj = NULL;
    icon_flag = 0;
}

/*********************** 界面搭建 ***********************/
void app_clock_open(lv_event_t *e)
{
    app_page_open("时钟", 0x2196f3, back_cb);
    icon_flag = 11;

    s_active = true;
    s_sync_busy = false;
    s_calibrated = false;

    // 大号时间 HH:MM:SS（纯 ASCII，用 montserrat）
    s_time_label = lv_label_create(icon_in_obj);
    lv_label_set_text(s_time_label, "--:--:--");
    lv_obj_set_style_text_font(s_time_label, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(s_time_label, lv_color_hex(0x212121), 0);
    lv_obj_align(s_time_label, LV_ALIGN_TOP_MID, 0, 56);

    // 年月日 + 星期
    s_date_label = lv_label_create(icon_in_obj);
    lv_label_set_text(s_date_label, "----年--月--日");
    lv_obj_set_style_text_font(s_date_label, &font_alipuhui20, 0);
    lv_obj_set_style_text_color(s_date_label, lv_color_hex(0x555555), 0);
    lv_obj_align(s_date_label, LV_ALIGN_TOP_MID, 0, 116);

    // 校准状态提示
    s_status_label = lv_label_create(icon_in_obj);
    lv_label_set_text(s_status_label, "准备校准...");
    lv_obj_set_style_text_font(s_status_label, &font_alipuhui20, 0);
    lv_obj_set_style_text_color(s_status_label, lv_color_hex(0x2196f3), 0);
    lv_obj_align(s_status_label, LV_ALIGN_TOP_MID, 0, 150);

    // 重新校准按钮
    lv_obj_t *btn = lv_btn_create(icon_in_obj);
    lv_obj_set_size(btn, 150, 40);
    lv_obj_align(btn, LV_ALIGN_TOP_MID, 0, 182);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x2196f3), 0);
    lv_obj_add_event_cb(btn, recalibrate_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *btn_label = lv_label_create(btn);
    lv_label_set_text(btn_label, "重新校准");
    lv_obj_set_style_text_font(btn_label, &font_alipuhui20, 0);
    lv_obj_center(btn_label);

    // 每秒走动
    s_timer = lv_timer_create(clock_tick_cb, 1000, NULL);
    clock_tick_cb(s_timer);       // 立即刷新一次，避免第一秒空白

    // 进入即自动校准一次
    clock_start_sync();
}
