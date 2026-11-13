/* 设置子应用：亮度 / 音量 / 开机音效 / 自动熄屏 / 关于 */
#include "app_common.h"
#include "esp32_s3_szp.h"
#include "esp_lvgl_port.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_system.h"
#include "esp_app_desc.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "app_settings";

LV_FONT_DECLARE(font_alipuhui20);

extern lv_obj_t *icon_in_obj; // 应用画布（定义在 app_ui.c）
extern int icon_flag;         // 当前应用标记（定义在 app_ui.c）

static lv_obj_t *label_bright_val; // 亮度数值标签
static lv_obj_t *label_volume_val; // 音量数值标签

// 熄屏时间选项对应的秒数，0 表示永不
static const uint16_t idle_opts_s[5] = {30, 60, 120, 300, 0};

// 返回主界面：先保存设置再删除画布
static void back_cb(lv_event_t *e)
{
    settings_save(); // 把修改写回 NVS
    lv_obj_del(icon_in_obj); // 删除画布
    icon_in_obj = NULL;
    icon_flag = 0;
}

// 亮度滑条事件：实时调节背光并记录
static void bright_slider_cb(lv_event_t *e)
{
    lv_obj_t *slider = lv_event_get_target(e);
    int v = lv_slider_get_value(slider);
    bsp_display_brightness_set(v); // 实时生效
    g_settings.brightness = (uint8_t)v;
    lv_label_set_text_fmt(label_bright_val, "%d", v);
}

// 音量滑条事件：实时调节音量并记录
static void volume_slider_cb(lv_event_t *e)
{
    lv_obj_t *slider = lv_event_get_target(e);
    int v = lv_slider_get_value(slider);
    bsp_codec_volume_set(v, NULL); // 实时生效
    g_settings.volume = (uint8_t)v;
    lv_label_set_text_fmt(label_volume_val, "%d", v);
}

// 开机音效开关事件
static void boot_sound_cb(lv_event_t *e)
{
    lv_obj_t *sw = lv_event_get_target(e);
    g_settings.boot_sound = lv_obj_has_state(sw, LV_STATE_CHECKED);
}

// 自动熄屏下拉框事件：把选项序号映射为秒数
static void idle_dd_cb(lv_event_t *e)
{
    lv_obj_t *dd = lv_event_get_target(e);
    uint16_t idx = lv_dropdown_get_selected(dd);
    if (idx < 5) {
        g_settings.idle_timeout_s = idle_opts_s[idx];
    }
}

// 创建中文小标题标签
static lv_obj_t *caption_create(lv_obj_t *parent, const char *txt, lv_coord_t x, lv_coord_t y)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, txt);
    lv_obj_set_style_text_font(label, &font_alipuhui20, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(0x333333), 0);
    lv_obj_align(label, LV_ALIGN_TOP_LEFT, x, y);
    return label;
}

// 入口：打开设置页面
void app_settings_open(lv_event_t *e)
{
    // 创建标准页面骨架（标题栏 + 返回按钮），返回画布对象
    lv_obj_t *page = app_page_open("设置", 0x607d8b, back_cb);
    icon_flag = 7; // 标记进入设置应用

    lv_obj_set_scrollbar_mode(page, LV_SCROLLBAR_MODE_AUTO); // 内容超高时可滚动

    /**************** 亮度 ****************/
    caption_create(page, "亮度", 15, 48);
    label_bright_val = lv_label_create(page);
    lv_label_set_text_fmt(label_bright_val, "%d", g_settings.brightness);
    lv_obj_set_style_text_font(label_bright_val, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(label_bright_val, lv_color_hex(0x333333), 0);
    lv_obj_align(label_bright_val, LV_ALIGN_TOP_LEFT, 275, 50);

    lv_obj_t *slider_bright = lv_slider_create(page);
    lv_obj_set_size(slider_bright, 240, 8);
    lv_obj_align(slider_bright, LV_ALIGN_TOP_LEFT, 15, 74);
    lv_slider_set_range(slider_bright, 0, 100);
    lv_slider_set_value(slider_bright, g_settings.brightness, LV_ANIM_OFF);
    lv_obj_add_event_cb(slider_bright, bright_slider_cb, LV_EVENT_VALUE_CHANGED, NULL);

    /**************** 音量 ****************/
    caption_create(page, "音量", 15, 96);
    label_volume_val = lv_label_create(page);
    lv_label_set_text_fmt(label_volume_val, "%d", g_settings.volume);
    lv_obj_set_style_text_font(label_volume_val, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(label_volume_val, lv_color_hex(0x333333), 0);
    lv_obj_align(label_volume_val, LV_ALIGN_TOP_LEFT, 275, 98);

    lv_obj_t *slider_volume = lv_slider_create(page);
    lv_obj_set_size(slider_volume, 240, 8);
    lv_obj_align(slider_volume, LV_ALIGN_TOP_LEFT, 15, 122);
    lv_slider_set_range(slider_volume, 0, 100);
    lv_slider_set_value(slider_volume, g_settings.volume, LV_ANIM_OFF);
    lv_obj_add_event_cb(slider_volume, volume_slider_cb, LV_EVENT_VALUE_CHANGED, NULL);

    /**************** 开机音效 ****************/
    caption_create(page, "开机音效", 15, 146);
    lv_obj_t *sw_boot = lv_switch_create(page);
    lv_obj_set_size(sw_boot, 44, 22);
    lv_obj_align(sw_boot, LV_ALIGN_TOP_LEFT, 255, 146);
    if (g_settings.boot_sound) {
        lv_obj_add_state(sw_boot, LV_STATE_CHECKED);
    }
    lv_obj_add_event_cb(sw_boot, boot_sound_cb, LV_EVENT_VALUE_CHANGED, NULL);

    /**************** 自动熄屏 ****************/
    caption_create(page, "自动熄屏", 15, 184);
    lv_obj_t *dd_idle = lv_dropdown_create(page);
    lv_dropdown_set_options(dd_idle, "30秒\n1分钟\n2分钟\n5分钟\n永不");
    lv_obj_set_style_text_font(dd_idle, &font_alipuhui20, 0);
    lv_obj_set_width(dd_idle, 120);
    lv_obj_align(dd_idle, LV_ALIGN_TOP_LEFT, 180, 180);
    // 根据当前设置选中对应项
    uint16_t sel = 0;
    for (int i = 0; i < 5; i++) {
        if (idle_opts_s[i] == g_settings.idle_timeout_s) {
            sel = (uint16_t)i;
            break;
        }
    }
    lv_dropdown_set_selected(dd_idle, sel);
    lv_obj_add_event_cb(dd_idle, idle_dd_cb, LV_EVENT_VALUE_CHANGED, NULL);

    /**************** 关于 ****************/
    caption_create(page, "关于", 15, 226);
    lv_obj_t *label_about = lv_label_create(page);
    lv_obj_set_style_text_font(label_about, &font_alipuhui20, 0);
    lv_obj_set_style_text_color(label_about, lv_color_hex(0x555555), 0);
    lv_obj_set_width(label_about, 290);
    lv_label_set_long_mode(label_about, LV_LABEL_LONG_WRAP); // 自动换行
    const esp_app_desc_t *desc = esp_app_get_description();
    lv_label_set_text_fmt(label_about,
                          "固件版本: %s\nIDF版本: %s\n空闲内部堆: %u 字节\n空闲PSRAM: %u 字节\n总堆大小: %u 字节",
                          desc->version,
                          esp_get_idf_version(),
                          (unsigned)esp_get_free_heap_size(),
                          (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
                          (unsigned)heap_caps_get_total_size(MALLOC_CAP_INTERNAL));
    lv_obj_align(label_about, LV_ALIGN_TOP_LEFT, 15, 250);

    lv_obj_scroll_to_y(page, 0, LV_ANIM_OFF); // 回到顶部
    ESP_LOGI(TAG, "settings page opened");
}
