#include "app_common.h"
#include "esp32_s3_szp.h"
#include "esp_lvgl_port.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include <time.h>
#include <sys/time.h>
#include <string.h>

static const char *TAG = "app_common";

LV_FONT_DECLARE(font_alipuhui20);

/* icon_in_obj / icon_flag 定义在 app_ui.c，这里只引用 */
extern lv_obj_t *icon_in_obj;
extern int icon_flag;

/*************************** 1. 系统设置（NVS） ***************************/
app_settings_t g_settings = {
    .brightness = 100,
    .volume = VOLUME_DEFAULT,
    .boot_sound = true,
    .idle_timeout_s = 120,
};

#define NVS_NS_CFG   "app_cfg"
#define NVS_NS_WIFI  "wifi_cfg"

void settings_load(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS_CFG, NVS_READONLY, &h) != ESP_OK) {
        ESP_LOGW(TAG, "settings_load: no NVS namespace yet, use defaults");
        return;
    }
    uint8_t u8;
    uint16_t u16;
    if (nvs_get_u8(h, "bright", &u8) == ESP_OK)  g_settings.brightness = u8;
    if (nvs_get_u8(h, "volume", &u8) == ESP_OK)  g_settings.volume = u8;
    if (nvs_get_u8(h, "bootsnd", &u8) == ESP_OK) g_settings.boot_sound = (u8 != 0);
    if (nvs_get_u16(h, "idle_s", &u16) == ESP_OK) g_settings.idle_timeout_s = u16;
    nvs_close(h);
    ESP_LOGI(TAG, "settings loaded: bright=%d vol=%d bootsnd=%d idle=%ds",
             g_settings.brightness, g_settings.volume, g_settings.boot_sound,
             g_settings.idle_timeout_s);
}

void settings_save(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS_CFG, NVS_READWRITE, &h) != ESP_OK) {
        ESP_LOGE(TAG, "settings_save: nvs_open failed");
        return;
    }
    nvs_set_u8(h, "bright", g_settings.brightness);
    nvs_set_u8(h, "volume", g_settings.volume);
    nvs_set_u8(h, "bootsnd", g_settings.boot_sound ? 1 : 0);
    nvs_set_u16(h, "idle_s", g_settings.idle_timeout_s);
    esp_err_t err = nvs_commit(h);
    nvs_close(h);
    ESP_LOGI(TAG, "settings saved (err=%d)", err);
}

/*************************** 2. WiFi 凭据（NVS） ***************************/
bool wifi_cred_load(char *ssid, size_t ssid_len, char *pass, size_t pass_len)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS_WIFI, NVS_READONLY, &h) != ESP_OK) return false;
    size_t l = ssid_len;
    bool ok = (nvs_get_str(h, "ssid", ssid, &l) == ESP_OK);
    l = pass_len;
    if (nvs_get_str(h, "pass", pass, &l) != ESP_OK) {
        pass[0] = '\0';
    }
    nvs_close(h);
    return ok && ssid[0] != '\0';
}

void wifi_cred_save(const char *ssid, const char *pass)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS_WIFI, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_str(h, "ssid", ssid);
    nvs_set_str(h, "pass", pass ? pass : "");
    nvs_commit(h);
    nvs_close(h);
    ESP_LOGI(TAG, "wifi credential saved: %s", ssid);
}

void wifi_cred_clear(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS_WIFI, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_erase_all(h);
    nvs_commit(h);
    nvs_close(h);
    ESP_LOGI(TAG, "wifi credential cleared");
}

/*************************** 3. 主界面状态栏 ***************************/
static lv_obj_t *sb_date  = NULL;   // 日期
static lv_obj_t *sb_time  = NULL;   // 时间
static lv_obj_t *sb_wifi  = NULL;   // WiFi 图标
static lv_obj_t *sb_bt    = NULL;   // 蓝牙图标
static lv_obj_t *sb_mem   = NULL;   // 内存“电池”条

lv_obj_t *statusbar_create(lv_obj_t *parent)
{
    lvgl_port_lock(0);
    lv_obj_t *bar = lv_obj_create(parent);
    lv_obj_set_size(bar, 320, 28);
    lv_obj_align(bar, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_radius(bar, 0, 0);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_set_style_pad_all(bar, 0, 0);
    lv_obj_set_style_bg_color(bar, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_30, 0);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);

    // 左侧：日期 + 时间
    sb_date = lv_label_create(bar);
    lv_obj_set_style_text_font(sb_date, &font_alipuhui20, 0);
    lv_obj_set_style_text_color(sb_date, lv_color_hex(0xffffff), 0);
    lv_label_set_text(sb_date, "未联网");
    lv_obj_align(sb_date, LV_ALIGN_LEFT_MID, 8, 0);

    sb_time = lv_label_create(bar);
    lv_obj_set_style_text_font(sb_time, &font_alipuhui20, 0);
    lv_obj_set_style_text_color(sb_time, lv_color_hex(0xffffff), 0);
    lv_label_set_text(sb_time, "");
    lv_obj_align_to(sb_time, sb_date, LV_ALIGN_OUT_RIGHT_MID, 8, 0);

    // 右侧：内存条 + WiFi + 蓝牙
    sb_mem = lv_bar_create(bar);
    lv_obj_set_size(sb_mem, 34, 12);
    lv_bar_set_range(sb_mem, 0, 100);
    lv_bar_set_value(sb_mem, 0, LV_ANIM_OFF);
    lv_obj_align(sb_mem, LV_ALIGN_RIGHT_MID, -8, 0);

    sb_wifi = lv_label_create(bar);
    lv_obj_set_style_text_font(sb_wifi, &lv_font_montserrat_20, 0);
    lv_label_set_text(sb_wifi, LV_SYMBOL_WIFI);
    lv_obj_set_style_text_color(sb_wifi, lv_color_hex(0x808080), 0);
    lv_obj_align_to(sb_wifi, sb_mem, LV_ALIGN_OUT_LEFT_MID, -8, 0);

    sb_bt = lv_label_create(bar);
    lv_obj_set_style_text_font(sb_bt, &lv_font_montserrat_20, 0);
    lv_label_set_text(sb_bt, LV_SYMBOL_BLUETOOTH);
    lv_obj_set_style_text_color(sb_bt, lv_color_hex(0x808080), 0);
    lv_obj_align_to(sb_bt, sb_wifi, LV_ALIGN_OUT_LEFT_MID, -8, 0);

    lvgl_port_unlock();
    return bar;
}

void statusbar_set_wifi(bool connected)
{
    if (!sb_wifi) return;
    lvgl_port_lock(0);
    lv_obj_set_style_text_color(sb_wifi,
        lv_color_hex(connected ? 0x00ff00 : 0x808080), 0);
    lvgl_port_unlock();
}

void statusbar_set_bt(bool active)
{
    if (!sb_bt) return;
    lvgl_port_lock(0);
    lv_obj_set_style_text_color(sb_bt,
        lv_color_hex(active ? 0x40c0ff : 0x808080), 0);
    lvgl_port_unlock();
}

void statusbar_refresh_time(void)
{
    if (!sb_time) return;
    time_t now;
    struct tm ti;
    time(&now);
    localtime_r(&now, &ti);
    lvgl_port_lock(0);
    if (ti.tm_year + 1900 < 2000) {   // 尚未通过 NTP 同步
        lv_label_set_text(sb_date, "未联网");
        lv_label_set_text(sb_time, "");
    } else {
        lv_label_set_text_fmt(sb_date, "%d年%02d月%02d日",
                              ti.tm_year + 1900, ti.tm_mon + 1, ti.tm_mday);
        lv_label_set_text_fmt(sb_time, "%02d:%02d:%02d",
                              ti.tm_hour, ti.tm_min, ti.tm_sec);
    }
    lvgl_port_unlock();
}

void statusbar_refresh_mem(void)
{
    if (!sb_mem) return;
    size_t total = heap_caps_get_total_size(MALLOC_CAP_INTERNAL);
    size_t free_ = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    int used_pct = total ? (int)((total - free_) * 100 / total) : 0;
    lvgl_port_lock(0);
    lv_bar_set_value(sb_mem, used_pct, LV_ANIM_OFF);
    lvgl_port_unlock();
}

/*************************** 4. 自动熄屏 ***************************/
static bool s_screen_off = false;
static lv_timer_t *s_idle_timer = NULL;

static void screen_idle_cb(lv_timer_t *t)
{
    if (g_settings.idle_timeout_s == 0) return;      // 0 = 永不熄屏
    if (icon_flag != 0) return;                      // 仅在主界面生效，避免打断子应用
    uint32_t inactive = lv_disp_get_inactive_time(NULL);
    uint32_t limit = (uint32_t)g_settings.idle_timeout_s * 1000;
    if (!s_screen_off && inactive > limit) {
        bsp_display_backlight_off();
        s_screen_off = true;
        ESP_LOGI(TAG, "screen off (idle %lus)", (unsigned long)(inactive / 1000));
    } else if (s_screen_off && inactive < 1500) {
        screen_wake();
    }
}

void screen_wake(void)
{
    if (!s_screen_off) return;
    bsp_display_backlight_on();
    bsp_display_brightness_set(g_settings.brightness);
    s_screen_off = false;
    ESP_LOGI(TAG, "screen on");
}

void screen_idle_start(void)
{
    if (s_idle_timer) return;
    lvgl_port_lock(0);
    s_idle_timer = lv_timer_create(screen_idle_cb, 1000, NULL);
    lvgl_port_unlock();
}

/*************************** 5. 子应用通用页面骨架 ***************************/
lv_obj_t *app_page_open(const char *title, uint32_t title_color_rgb,
                        void (*back_cb)(lv_event_t *))
{
    lvgl_port_lock(0);

    static lv_style_t style;
    lv_style_init(&style);
    lv_style_set_radius(&style, 10);
    lv_style_set_bg_opa(&style, LV_OPA_COVER);
    lv_style_set_bg_color(&style, lv_color_hex(0xffffff));
    lv_style_set_border_width(&style, 0);
    lv_style_set_pad_all(&style, 0);
    lv_style_set_width(&style, 320);
    lv_style_set_height(&style, 240);

    icon_in_obj = lv_obj_create(lv_scr_act());
    lv_obj_add_style(icon_in_obj, &style, 0);

    // 标题栏
    lv_obj_t *bar = lv_obj_create(icon_in_obj);
    lv_obj_set_size(bar, 320, 40);
    lv_obj_set_style_pad_all(bar, 0, 0);
    lv_obj_align(bar, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_bg_color(bar, lv_color_hex(title_color_rgb), 0);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_set_style_radius(bar, 0, 0);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *label = lv_label_create(bar);
    lv_label_set_text(label, title);
    lv_obj_set_style_text_color(label, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_text_font(label, &font_alipuhui20, 0);
    lv_obj_align(label, LV_ALIGN_CENTER, 0, 0);

    // 返回按钮
    lv_obj_t *btn = lv_btn_create(bar);
    lv_obj_align(btn, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_size(btn, 60, 30);
    lv_obj_set_style_border_width(btn, 0, 0);
    lv_obj_set_style_pad_all(btn, 0, 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_shadow_opa(btn, LV_OPA_TRANSP, LV_PART_MAIN);
    if (back_cb) lv_obj_add_event_cb(btn, back_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *back = lv_label_create(btn);
    lv_label_set_text(back, LV_SYMBOL_LEFT);
    lv_obj_set_style_text_font(back, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(back, lv_color_hex(0xffffff), 0);
    lv_obj_align(back, LV_ALIGN_CENTER, -10, 0);

    lvgl_port_unlock();
    return icon_in_obj;
}
