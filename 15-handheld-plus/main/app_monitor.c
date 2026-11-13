/* 环境监测仪：实时曲线显示 QMI8658 倾角，并可选记录 CSV 到 SD 卡 */
#include "app_common.h"
#include "esp32_s3_szp.h"
#include "esp_lvgl_port.h"
#include "esp_log.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

static const char *TAG = "app_monitor";

LV_FONT_DECLARE(font_alipuhui20);

extern lv_obj_t *icon_in_obj; // 应用画布（app_ui.c 定义）
extern int icon_flag;         // 当前应用标记（app_ui.c 定义）

#define MONITOR_CSV_PATH    SD_MOUNT_POINT "/monitor_log.csv"
#define MONITOR_TIMER_MS    200   // 采样周期
#define MONITOR_POINT_CNT   30    // 曲线点数

/* 界面控件 */
static lv_obj_t *s_chart;             // 曲线图
static lv_chart_series_t *s_ser_x;    // X轴曲线（红）
static lv_chart_series_t *s_ser_y;    // Y轴曲线（绿）
static lv_chart_series_t *s_ser_z;    // Z轴曲线（蓝）
static lv_obj_t *s_label_value;       // 数值 X/Y/Z
static lv_obj_t *s_label_motion;      // 运动状态
static lv_obj_t *s_label_log;         // 记录状态
static lv_obj_t *s_label_elapsed;     // 已运行时间
static lv_obj_t *s_btn_log;           // 记录/停止记录 按钮
static lv_obj_t *s_btn_log_label;     // 按钮文字
static lv_timer_t *s_timer;           // 采样定时器

/* 数据记录状态 */
static bool s_logging;                // 是否正在记录
static FILE *s_csv;                   // CSV 文件句柄
static bool s_sd_mounted;             // SD 卡是否由本应用挂载
static uint32_t s_elapsed_ms;         // 累计运行毫秒数

// 停止记录：关闭 CSV 文件
static void log_stop(void)
{
    if (s_csv) {
        fclose(s_csv);
        s_csv = NULL;
    }
    s_logging = false;
}

// 开始记录：挂载 SD 卡并打开 CSV 文件（新文件先写表头）
static bool log_start(void)
{
    if (!s_sd_mounted) {
        if (bsp_sdcard_mount() != ESP_OK) {
            ESP_LOGE(TAG, "SD卡挂载失败");
            return false;
        }
        s_sd_mounted = true;
    }
    s_csv = fopen(MONITOR_CSV_PATH, "a");
    if (!s_csv) {
        ESP_LOGE(TAG, "打开CSV文件失败");
        return false;
    }
    // 文件为空时写入表头
    fseek(s_csv, 0, SEEK_END);
    if (ftell(s_csv) == 0) {
        fprintf(s_csv, "ms,ax,ay,az,motion\n");
    }
    s_logging = true;
    return true;
}

// 记录按钮事件：切换 记录/停止记录
static void btn_log_cb(lv_event_t *e)
{
    if (s_logging) { // 正在记录 -> 停止
        log_stop();
        lv_label_set_text(s_btn_log_label, "记录");
        lv_label_set_text(s_label_log, "记录: 已停止");
    }
    else { // 停止 -> 开始记录
        if (log_start()) {
            lv_label_set_text(s_btn_log_label, "停止记录");
            lv_label_set_text(s_label_log, "记录: 进行中");
        }
        else {
            lv_label_set_text(s_label_log, "记录: SD卡错误");
        }
    }
}

// 定时采样：读角度更新曲线，并按需写 CSV
static void monitor_timer_cb(lv_timer_t *timer)
{
    t_sQMI8658 qmi;
    qmi8658_fetch_angleFromAcc(&qmi);
    int ax = (int)round(qmi.AngleX);
    int ay = (int)round(qmi.AngleY);
    int az = (int)round(qmi.AngleZ);

    // 更新曲线
    lv_chart_set_next_value(s_chart, s_ser_x, ax);
    lv_chart_set_next_value(s_chart, s_ser_y, ay);
    lv_chart_set_next_value(s_chart, s_ser_z, az);

    // 更新数值标签
    lv_label_set_text_fmt(s_label_value, "X:%d  Y:%d  Z:%d", ax, ay, az);

    // 更新运动状态标签
    uint8_t status = qmi8658_fetch_motion();
    if (status & 0x80) { // Significant-Motion
        lv_label_set_text(s_label_motion, "剧烈运动");
    }
    else if (status & 0x20) { // Any-Motion
        lv_label_set_text(s_label_motion, "运动或震动");
    }
    else if (status & 0x40) { // No-Motion
        lv_label_set_text(s_label_motion, "静止");
    }
    else {
        lv_label_set_text(s_label_motion, "—");
    }

    // 更新时间与CSV记录
    s_elapsed_ms += MONITOR_TIMER_MS;
    lv_label_set_text_fmt(s_label_elapsed, "%u s", (unsigned)(s_elapsed_ms / 1000));
    if (s_logging && s_csv) {
        if (fprintf(s_csv, "%u,%d,%d,%d,%u\n",
                    (unsigned)s_elapsed_ms, ax, ay, az, status) < 0) {
            ESP_LOGE(TAG, "写CSV失败，停止记录");
            log_stop();
            lv_label_set_text(s_btn_log_label, "记录");
            lv_label_set_text(s_label_log, "记录: 写入失败");
        }
        else {
            fflush(s_csv);
        }
    }
}

// 创建曲线图与控制区
static void monitor_ui_create(void)
{
    // 曲线图 300x120
    s_chart = lv_chart_create(icon_in_obj);
    lv_obj_set_size(s_chart, 300, 120);
    lv_obj_align(s_chart, LV_ALIGN_TOP_LEFT, 10, 48);
    lv_chart_set_type(s_chart, LV_CHART_TYPE_LINE);
    lv_chart_set_point_count(s_chart, MONITOR_POINT_CNT);
    lv_chart_set_update_mode(s_chart, LV_CHART_UPDATE_MODE_SHIFT);
    lv_chart_set_range(s_chart, LV_CHART_AXIS_PRIMARY_Y, -100, 100);
    lv_chart_set_div_line_count(s_chart, 4, 5);
    s_ser_x = lv_chart_add_series(s_chart, lv_color_hex(0xff0000), LV_CHART_AXIS_PRIMARY_Y);
    s_ser_y = lv_chart_add_series(s_chart, lv_color_hex(0x00b000), LV_CHART_AXIS_PRIMARY_Y);
    s_ser_z = lv_chart_add_series(s_chart, lv_color_hex(0x0000ff), LV_CHART_AXIS_PRIMARY_Y);

    // 数值标签
    s_label_value = lv_label_create(icon_in_obj);
    lv_label_set_text(s_label_value, "X:--  Y:--  Z:--");
    lv_obj_set_style_text_font(s_label_value, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(s_label_value, lv_color_hex(0x000000), 0);
    lv_obj_align(s_label_value, LV_ALIGN_TOP_LEFT, 15, 176);

    // 运动状态标签
    s_label_motion = lv_label_create(icon_in_obj);
    lv_label_set_text(s_label_motion, "—");
    lv_obj_set_style_text_font(s_label_motion, &font_alipuhui20, 0);
    lv_obj_set_style_text_color(s_label_motion, lv_color_hex(0x3f51b5), 0);
    lv_obj_align(s_label_motion, LV_ALIGN_TOP_LEFT, 230, 176);

    // 记录按钮
    s_btn_log = lv_btn_create(icon_in_obj);
    lv_obj_set_size(s_btn_log, 90, 34);
    lv_obj_align(s_btn_log, LV_ALIGN_TOP_LEFT, 15, 202);
    lv_obj_set_style_bg_color(s_btn_log, lv_color_hex(0x3f51b5), 0);
    lv_obj_add_event_cb(s_btn_log, btn_log_cb, LV_EVENT_CLICKED, NULL);
    s_btn_log_label = lv_label_create(s_btn_log);
    lv_label_set_text(s_btn_log_label, "记录");
    lv_obj_set_style_text_font(s_btn_log_label, &font_alipuhui20, 0);
    lv_obj_set_style_text_color(s_btn_log_label, lv_color_hex(0xffffff), 0);
    lv_obj_align(s_btn_log_label, LV_ALIGN_CENTER, 0, 0);

    // 记录状态标签
    s_label_log = lv_label_create(icon_in_obj);
    lv_label_set_text(s_label_log, "记录: 未开始");
    lv_obj_set_style_text_font(s_label_log, &font_alipuhui20, 0);
    lv_obj_set_style_text_color(s_label_log, lv_color_hex(0x000000), 0);
    lv_obj_align(s_label_log, LV_ALIGN_TOP_LEFT, 115, 208);

    // 运行时间标签
    s_label_elapsed = lv_label_create(icon_in_obj);
    lv_label_set_text(s_label_elapsed, "0 s");
    lv_obj_set_style_text_font(s_label_elapsed, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_label_elapsed, lv_color_hex(0x666666), 0);
    lv_obj_align(s_label_elapsed, LV_ALIGN_TOP_LEFT, 265, 212);

    // 创建采样定时器
    s_timer = lv_timer_create(monitor_timer_cb, MONITOR_TIMER_MS, NULL);
}

// 返回主界面：释放定时器/文件/传感器
static void back_cb(lv_event_t *e)
{
    if (s_timer) {
        lv_timer_del(s_timer);
        s_timer = NULL;
    }
    log_stop(); // 关闭CSV文件
    if (s_sd_mounted) {
        bsp_sdcard_unmount();
        s_sd_mounted = false;
    }
    qmi8658_close(); // 关闭芯片运行
    lv_obj_del(icon_in_obj); // 删除画布
    icon_in_obj = NULL;
    icon_flag = 0;
}

// 传感器初始化任务（后台执行，避免阻塞 LVGL）
static void task_monitor_init(void *arg)
{
    esp_err_t ret = ESP_FAIL;
    for (int i = 0; i < 3; i++) { // 重试3次
        ret = qmi8658_init();
        if (ret == ESP_OK) break;
        ESP_LOGW(TAG, "QMI8658初始化失败，重试 %d", i + 1);
        vTaskDelay(200 / portTICK_PERIOD_MS);
    }
    if (ret != ESP_OK) { // 初始化失败，提示后返回主界面
        lvgl_port_lock(0);
        if (icon_in_obj) {
            lv_obj_t *label = lv_label_create(icon_in_obj);
            lv_label_set_text(label, "QMI8658传感器错误...");
            lv_obj_set_style_text_color(label, lv_color_hex(0x000000), 0);
            lv_obj_set_style_text_font(label, &font_alipuhui20, 0);
            lv_obj_align(label, LV_ALIGN_CENTER, 0, 0);
        }
        lvgl_port_unlock();
        vTaskDelay(1000 / portTICK_PERIOD_MS); // 提示词保留1秒
        lvgl_port_lock(0);
        if (icon_in_obj) {
            lv_obj_del(icon_in_obj); // 删除画布 回到主界面
            icon_in_obj = NULL;
            icon_flag = 0;
        }
        lvgl_port_unlock();
    }
    else { // 初始化成功，创建界面与定时器
        lvgl_port_lock(0);
        if (icon_in_obj && icon_flag == 9) {
            monitor_ui_create();
        }
        lvgl_port_unlock();
    }
    vTaskDelete(NULL);
}

// 应用入口：环境监测仪
void app_monitor_open(lv_event_t *e)
{
    app_page_open("环境监测仪", 0x3f51b5, back_cb);
    icon_flag = 9; // 标记进入环境监测仪应用

    // 复位内部状态
    s_chart = NULL;
    s_timer = NULL;
    s_csv = NULL;
    s_logging = false;
    s_sd_mounted = false;
    s_elapsed_ms = 0;

    xTaskCreatePinnedToCore(task_monitor_init, "task_monitor", 4 * 1024, NULL, 5, NULL, 1);
}
