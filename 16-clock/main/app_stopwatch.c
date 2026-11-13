/* 秒表/倒计时 子应用
 * 顶部 lv_btnmatrix 切换两个模式：
 *   1. 秒表：向上计时 MM:SS.cs，支持 开始/暂停、复位、计次
 *   2. 倒计时：滑块设置分钟数，到零显示"时间到！"并蜂鸣提示
 * 计时基于 esp_timer_get_time()（微秒），不依赖 lv_timer 周期，保证精度。
 */
#include "app_common.h"
#include "esp32_s3_szp.h"
#include "esp_lvgl_port.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

LV_FONT_DECLARE(font_alipuhui20);

extern lv_obj_t *icon_in_obj;   // 应用画布（app_ui.c 中定义）
extern int icon_flag;           // 当前应用标记（app_ui.c 中定义）

/*********************** 内部状态 ***********************/
static lv_timer_t *s_timer = NULL;      // 唯一的刷新定时器
static int s_tab = 0;                   // 0=秒表 1=倒计时

static lv_obj_t *s_panel_sw = NULL;     // 秒表面板
static lv_obj_t *s_panel_cd = NULL;     // 倒计时面板
static lv_obj_t *s_sw_label = NULL;     // 秒表时间标签
static lv_obj_t *s_sw_btn_label = NULL; // 开始/暂停 按钮文字
static lv_obj_t *s_lap_list = NULL;     // 计次列表
static int s_lap_cnt = 0;               // 计次序号

static lv_obj_t *s_cd_min_label = NULL; // "N 分钟"标签
static lv_obj_t *s_cd_slider = NULL;    // 分钟滑块
static lv_obj_t *s_cd_label = NULL;     // 剩余时间标签
static lv_obj_t *s_cd_btn_label = NULL; // 开始/停止 按钮文字
static lv_obj_t *s_cd_done_label = NULL;// "时间到！"标签

// 秒表计时状态（微秒）
static int64_t s_accum_us = 0;          // 已累计时间
static int64_t s_start_us = 0;          // 本次开始时刻
static bool s_running = false;          // 秒表是否运行中

// 倒计时状态
static int64_t s_cd_end_us = 0;         // 倒计时结束时刻
static bool s_cd_running = false;       // 倒计时是否运行中
static bool s_cd_done = false;          // 已到零标记
static volatile bool s_beep_busy = false; // 蜂鸣任务进行中，防止重复触发

/*********************** 蜂鸣提示 ***********************/
// 短促蜂鸣任务：生成 1kHz 方波（16000Hz 立体声 16bit，约300ms）写入 I2S
static void beep_task(void *arg)
{
    const int frames = 4800;    // 16000Hz * 0.3s
    int16_t *buf = malloc(frames * 2 * sizeof(int16_t)); // 双声道
    if (buf != NULL) {
        for (int i = 0; i < frames; i++) {
            int16_t v = ((i / 8) % 2) ? 6000 : -6000; // 16000/(2*8)=1kHz
            buf[2 * i] = v;       // 左声道
            buf[2 * i + 1] = v;   // 右声道
        }
        pa_en(1); // 打开功放
        size_t wr = 0;
        bsp_i2s_write(buf, frames * 2 * sizeof(int16_t), &wr, portMAX_DELAY);
        pa_en(0); // 关闭功放
        free(buf);
    }
    s_beep_busy = false;
    vTaskDelete(NULL);
}

// 触发一次蜂鸣（若任务已在运行则忽略，避免循环触发）
static void beep_start(void)
{
    if (s_beep_busy) return;
    s_beep_busy = true;
    xTaskCreatePinnedToCore(beep_task, "beep", 4096, NULL, 5, NULL, 1);
}

/*********************** 事件回调 ***********************/
// 顶部标签切换：显示对应面板
static void tab_event_cb(lv_event_t *e)
{
    lv_obj_t *bm = lv_event_get_target(e);
    uint32_t idx = lv_btnmatrix_get_selected_btn(bm);
    s_tab = (int)idx;
    if (idx == 0) {
        lv_obj_clear_flag(s_panel_sw, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_panel_cd, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s_panel_sw, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_panel_cd, LV_OBJ_FLAG_HIDDEN);
    }
}

// 秒表 开始/暂停
static void sw_start_pause_cb(lv_event_t *e)
{
    if (s_running) { // 暂停：累计已走时间
        s_accum_us += esp_timer_get_time() - s_start_us;
        s_running = false;
        lv_label_set_text(s_sw_btn_label, "开始");
    } else {           // 开始
        s_start_us = esp_timer_get_time();
        s_running = true;
        lv_label_set_text(s_sw_btn_label, "暂停");
    }
}

// 秒表 复位
static void sw_reset_cb(lv_event_t *e)
{
    s_running = false;
    s_accum_us = 0;
    lv_label_set_text(s_sw_label, "00:00.00");
    lv_label_set_text(s_sw_btn_label, "开始");
    lv_obj_clean(s_lap_list); // 清空计次列表
    s_lap_cnt = 0;
}

// 秒表 计次：把当前时间追加到列表
static void sw_lap_cb(lv_event_t *e)
{
    int64_t us = s_accum_us + (s_running ? (esp_timer_get_time() - s_start_us) : 0);
    int cs = (int)((us / 10000) % 100);
    int s  = (int)((us / 1000000) % 60);
    int m  = (int)((us / 60000000));
    s_lap_cnt++;
    lv_obj_t *item = lv_list_add_btn(s_lap_list, NULL, NULL);
    lv_obj_t *label = lv_label_create(item);
    lv_label_set_text_fmt(label, "计次%d  %02d:%02d.%02d", s_lap_cnt, m, s, cs);
    lv_obj_set_style_text_font(label, &font_alipuhui20, 0);
    // 滚动到最新一条
    lv_obj_scroll_to_view(item, LV_ANIM_ON);
}

// 倒计时滑块变化：更新"N 分钟"标签
static void cd_slider_cb(lv_event_t *e)
{
    int v = lv_slider_get_value(s_cd_slider);
    lv_label_set_text_fmt(s_cd_min_label, "%d 分钟", v);
}

// 倒计时 开始/停止
static void cd_start_stop_cb(lv_event_t *e)
{
    if (s_cd_running) { // 停止
        s_cd_running = false;
        lv_label_set_text(s_cd_btn_label, "开始");
    } else {            // 开始：按滑块分钟数设定结束时刻
        int m = lv_slider_get_value(s_cd_slider);
        s_cd_end_us = esp_timer_get_time() + (int64_t)m * 60 * 1000000;
        s_cd_running = true;
        s_cd_done = false;
        lv_obj_add_flag(s_cd_done_label, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(s_cd_btn_label, "停止");
    }
}

// 返回主界面
static void back_cb(lv_event_t *e)
{
    lv_timer_del(s_timer);
    s_timer = NULL;
    s_running = false;      // 停止计时，蜂鸣任务（若有）自行结束
    s_cd_running = false;
    lv_obj_del(icon_in_obj);
    icon_in_obj = NULL;
    icon_flag = 0;
}

/*********************** 定时刷新 ***********************/
static void update_cb(lv_timer_t *timer)
{
    if (s_tab == 0) { // 秒表模式
        int64_t us = s_accum_us + (s_running ? (esp_timer_get_time() - s_start_us) : 0);
        int cs = (int)((us / 10000) % 100);
        int s  = (int)((us / 1000000) % 60);
        int m  = (int)((us / 60000000));
        lv_label_set_text_fmt(s_sw_label, "%02d:%02d.%02d", m, s, cs);
    } else {          // 倒计时模式
        if (s_cd_running) {
            int64_t rem = s_cd_end_us - esp_timer_get_time();
            if (rem <= 0) { // 到零：停止 + 提示 + 蜂鸣（仅一次）
                rem = 0;
                s_cd_running = false;
                lv_label_set_text(s_cd_btn_label, "开始");
                if (!s_cd_done) {
                    s_cd_done = true;
                    lv_obj_clear_flag(s_cd_done_label, LV_OBJ_FLAG_HIDDEN);
                    beep_start();
                }
            }
            int s = (int)((rem / 1000000) % 60);
            int m = (int)(rem / 60000000);
            lv_label_set_text_fmt(s_cd_label, "%02d:%02d", m, s);
        }
    }
}

/*********************** 界面搭建 ***********************/
// 创建一个中文按钮并返回其文字标签
static lv_obj_t *make_btn(lv_obj_t *parent, const char *text, int w, int h,
                          lv_event_cb_t cb, lv_obj_t **out_label)
{
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_set_size(btn, w, h);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0xff9800), 0);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *label = lv_label_create(btn);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, &font_alipuhui20, 0);
    lv_obj_center(label);
    if (out_label) *out_label = label;
    return btn;
}

void app_stopwatch_open(lv_event_t *e)
{
    app_page_open("秒表", 0xff9800, back_cb);
    icon_flag = 10;

    // 复位内部状态
    s_tab = 0;
    s_accum_us = 0; s_start_us = 0; s_running = false;
    s_cd_running = false; s_cd_done = false; s_lap_cnt = 0;

    // 顶部模式切换标签
    static const char *tab_items[] = {"秒表", "倒计时", ""};
    lv_obj_t *bm = lv_btnmatrix_create(icon_in_obj);
    lv_btnmatrix_set_map(bm, tab_items);
    lv_obj_set_size(bm, 300, 32);
    lv_obj_align(bm, LV_ALIGN_TOP_MID, 0, 44);
    lv_obj_set_style_text_font(bm, &font_alipuhui20, 0);
    lv_btnmatrix_set_btn_ctrl_all(bm, LV_BTNMATRIX_CTRL_CHECKABLE);
    lv_btnmatrix_set_selected_btn(bm, 0);
    lv_obj_add_event_cb(bm, tab_event_cb, LV_EVENT_VALUE_CHANGED, NULL);

    // ---------- 秒表面板 ----------
    s_panel_sw = lv_obj_create(icon_in_obj);
    lv_obj_set_size(s_panel_sw, 320, 158);
    lv_obj_align(s_panel_sw, LV_ALIGN_TOP_LEFT, 0, 80);
    lv_obj_set_style_pad_all(s_panel_sw, 4, 0);
    lv_obj_set_style_border_width(s_panel_sw, 0, 0);
    lv_obj_clear_flag(s_panel_sw, LV_OBJ_FLAG_SCROLLABLE);

    // 大时间标签（纯ASCII用montserrat）
    s_sw_label = lv_label_create(s_panel_sw);
    lv_label_set_text(s_sw_label, "00:00.00");
    lv_obj_set_style_text_font(s_sw_label, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(s_sw_label, lv_color_hex(0x333333), 0);
    lv_obj_align(s_sw_label, LV_ALIGN_TOP_MID, 0, 4);

    // 按钮行：开始/暂停、复位、计次
    lv_obj_t *btn_row = lv_obj_create(s_panel_sw);
    lv_obj_set_size(btn_row, 312, 40);
    lv_obj_align(btn_row, LV_ALIGN_TOP_MID, 0, 34);
    lv_obj_set_style_pad_all(btn_row, 2, 0);
    lv_obj_set_style_border_width(btn_row, 0, 0);
    lv_obj_set_flex_flow(btn_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btn_row, LV_FLEX_ALIGN_SPACE_EVENLY,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    make_btn(btn_row, "开始", 90, 34, sw_start_pause_cb, &s_sw_btn_label);
    make_btn(btn_row, "复位", 90, 34, sw_reset_cb, NULL);
    make_btn(btn_row, "计次", 90, 34, sw_lap_cb, NULL);

    // 计次列表（可滚动）
    s_lap_list = lv_list_create(s_panel_sw);
    lv_obj_set_size(s_lap_list, 300, 72);
    lv_obj_align(s_lap_list, LV_ALIGN_BOTTOM_MID, 0, -2);
    lv_obj_set_style_text_font(s_lap_list, &font_alipuhui20, 0);

    // ---------- 倒计时面板 ----------
    s_panel_cd = lv_obj_create(icon_in_obj);
    lv_obj_set_size(s_panel_cd, 320, 158);
    lv_obj_align(s_panel_cd, LV_ALIGN_TOP_LEFT, 0, 80);
    lv_obj_set_style_pad_all(s_panel_cd, 4, 0);
    lv_obj_set_style_border_width(s_panel_cd, 0, 0);
    lv_obj_clear_flag(s_panel_cd, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_panel_cd, LV_OBJ_FLAG_HIDDEN); // 默认显示秒表

    // 分钟设置：滑块 + "N 分钟"标签
    s_cd_slider = lv_slider_create(s_panel_cd);
    lv_obj_set_size(s_cd_slider, 200, 10);
    lv_obj_align(s_cd_slider, LV_ALIGN_TOP_LEFT, 10, 14);
    lv_slider_set_range(s_cd_slider, 0, 60);
    lv_slider_set_value(s_cd_slider, 5, LV_ANIM_OFF);
    lv_obj_add_event_cb(s_cd_slider, cd_slider_cb, LV_EVENT_VALUE_CHANGED, NULL);

    s_cd_min_label = lv_label_create(s_panel_cd);
    lv_label_set_text(s_cd_min_label, "5 分钟");
    lv_obj_set_style_text_font(s_cd_min_label, &font_alipuhui20, 0);
    lv_obj_set_style_text_color(s_cd_min_label, lv_color_hex(0x333333), 0);
    lv_obj_align(s_cd_min_label, LV_ALIGN_TOP_RIGHT, -10, 6);

    // 剩余时间标签
    s_cd_label = lv_label_create(s_panel_cd);
    lv_label_set_text(s_cd_label, "05:00");
    lv_obj_set_style_text_font(s_cd_label, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(s_cd_label, lv_color_hex(0x333333), 0);
    lv_obj_align(s_cd_label, LV_ALIGN_TOP_MID, 0, 36);

    // "时间到！"提示（默认隐藏）
    s_cd_done_label = lv_label_create(s_panel_cd);
    lv_label_set_text(s_cd_done_label, "时间到！");
    lv_obj_set_style_text_font(s_cd_done_label, &font_alipuhui20, 0);
    lv_obj_set_style_text_color(s_cd_done_label, lv_color_hex(0xe53935), 0);
    lv_obj_align(s_cd_done_label, LV_ALIGN_TOP_MID, 60, 40);
    lv_obj_add_flag(s_cd_done_label, LV_OBJ_FLAG_HIDDEN);

    // 开始/停止 按钮
    lv_obj_t *cd_btn = make_btn(s_panel_cd, "开始", 100, 36, cd_start_stop_cb, &s_cd_btn_label);
    lv_obj_align(cd_btn, LV_ALIGN_TOP_MID, 0, 72);

    // 唯一的刷新定时器（80ms），只更新当前标签页
    s_timer = lv_timer_create(update_cb, 80, NULL);
}
