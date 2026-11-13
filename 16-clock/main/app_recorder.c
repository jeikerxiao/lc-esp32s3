/*
 * app_recorder.c —— 录音机子应用
 * 功能：ES7210 麦克风录音到 SD 卡 WAV 文件（单声道 16kHz 16bit PCM），
 *       并可列表 / 播放（单声道扩展为立体声送喇叭）/ 删除录音。
 * 入口：app_recorder_open()
 */
#include "app_common.h"
#include "esp32_s3_szp.h"
#include "esp_lvgl_port.h"
#include "esp_log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "recorder";

LV_FONT_DECLARE(font_alipuhui20);
extern lv_obj_t *icon_in_obj;   // 页面画布（app_ui.c 定义）
extern int icon_flag;           // 当前子应用标记（app_ui.c 定义）

#define REC_DIR             SD_MOUNT_POINT "/REC"   // 录音存放目录
#define REC_SAMPLE_RATE     16000                   // 采样率
#define REC_BYTE_RATE       (REC_SAMPLE_RATE * 2)   // 单声道16bit字节率 32000
#define RAW_CHUNK_BYTES     1024                    // 每次读麦克风字节数(4通道交织)
#define MONO_SAMPLES        (RAW_CHUNK_BYTES / 8)   // 每块单声道采样数 128
#define PLAY_SAMPLES        256                     // 播放每次读取的单声道采样数

/* 44字节标准PCM WAV头（小端， packed） */
typedef struct __attribute__((packed)) {
    char     riff_id[4];        // "RIFF"
    uint32_t chunk_size;        // 36 + 数据长度
    char     wave_id[4];        // "WAVE"
    char     fmt_id[4];         // "fmt "
    uint32_t fmt_size;          // 16
    uint16_t audio_format;      // 1 = PCM
    uint16_t num_channels;      // 1 = 单声道
    uint32_t sample_rate;       // 16000
    uint32_t byte_rate;         // 32000
    uint16_t block_align;       // 2
    uint16_t bits_per_sample;   // 16
    char     data_id[4];        // "data"
    uint32_t data_size;         // PCM数据字节数
} wav_hdr_t;

/*************************** 内部状态 ***************************/
static volatile bool s_recording  = false;  // 录音请求标志
static volatile bool s_rec_busy   = false;  // 录音任务运行中
static volatile bool s_play_stop  = false;  // 停止播放请求
static volatile bool s_play_busy  = false;  // 播放任务运行中
static bool s_sd_ok = false;                // SD卡挂载成功标志

static lv_obj_t *s_status_label = NULL;     // 状态/时间标签
static lv_obj_t *s_file_list    = NULL;     // 录音文件列表
static char s_selected[96] = "";            // 当前选中的录音完整路径
static char s_play_path[96]  = "";          // 播放任务使用的路径

static FILE    *s_rec_file  = NULL;         // 正在写入的WAV文件
static uint32_t s_rec_bytes = 0;            // 已写入的PCM字节数

static void refresh_list(void);             // 前置声明（需已持有LVGL锁）

/*************************** 录音任务 ***************************/
// 打开WAV写头部占位 -> 循环读麦克风提取通道0写文件 -> 回填长度并关闭
static void rec_task(void *arg)
{
    (void)arg;
    int16_t raw[RAW_CHUNK_BYTES / 2];       // 4通道交织原始数据
    int16_t mono[MONO_SAMPLES];             // 提取出的通道0
    char path[96];
    int idx = 0, n;

    // 扫描目录取现有最大编号+1，避免删除后重名
    DIR *dir = opendir(REC_DIR);
    if (dir) {
        struct dirent *ent;
        while ((ent = readdir(dir)) != NULL) {
            if (sscanf(ent->d_name, "REC_%d.wav", &n) == 1 && n >= idx) idx = n + 1;
        }
        closedir(dir);
    }
    snprintf(path, sizeof(path), REC_DIR "/REC_%03d.wav", idx);

    s_rec_file = fopen(path, "wb");
    if (!s_rec_file) {
        ESP_LOGE(TAG, "无法创建 %s", path);
        if (icon_flag == 8) {
            lvgl_port_lock(0);
            if (s_status_label) lv_label_set_text(s_status_label, "无法创建文件");
            lvgl_port_unlock();
        }
        s_rec_busy = false;
        vTaskDelete(NULL);
        return;
    }

    // 先写占位头部，结束时回填两个长度字段
    wav_hdr_t hdr = {
        .riff_id = {'R','I','F','F'}, .chunk_size = 0,
        .wave_id = {'W','A','V','E'},
        .fmt_id  = {'f','m','t',' '}, .fmt_size = 16,
        .audio_format = 1, .num_channels = 1,
        .sample_rate = REC_SAMPLE_RATE, .byte_rate = REC_BYTE_RATE,
        .block_align = 2, .bits_per_sample = 16,
        .data_id = {'d','a','t','a'}, .data_size = 0,
    };
    fwrite(&hdr, sizeof(hdr), 1, s_rec_file);

    s_rec_bytes = 0;
    uint32_t last_sec = 0;
    while (s_recording && icon_flag == 8) {
        if (bsp_get_feed_data(true, raw, sizeof(raw)) != ESP_OK) break;
        for (int i = 0; i < MONO_SAMPLES; i++) mono[i] = raw[i * 4]; // 取通道0
        fwrite(mono, sizeof(int16_t), MONO_SAMPLES, s_rec_file);
        s_rec_bytes += sizeof(mono);
        // 每秒刷新一次时间标签
        uint32_t sec = s_rec_bytes / REC_BYTE_RATE;
        if (sec != last_sec) {
            last_sec = sec;
            if (s_recording && icon_flag == 8) {
                lvgl_port_lock(0);
                if (s_status_label)
                    lv_label_set_text_fmt(s_status_label, "录音中 %02d:%02d",
                                          (int)(sec / 60), (int)(sec % 60));
                lvgl_port_unlock();
            }
        }
    }

    // 回填 chunk_size 与 data_size
    uint32_t cs = 36 + s_rec_bytes;
    fseek(s_rec_file, 4, SEEK_SET);
    fwrite(&cs, 4, 1, s_rec_file);
    fseek(s_rec_file, 40, SEEK_SET);
    fwrite(&s_rec_bytes, 4, 1, s_rec_file);
    fclose(s_rec_file);
    s_rec_file = NULL;
    ESP_LOGI(TAG, "录音结束 %s 共 %u 字节", path, (unsigned)s_rec_bytes);

    // 页面还在则刷新状态与列表
    if (icon_flag == 8) {
        lvgl_port_lock(0);
        if (s_status_label)
            lv_label_set_text(s_status_label, s_rec_bytes ? "已保存" : "录音为空");
        refresh_list();
        lvgl_port_unlock();
    }
    s_rec_busy = false;
    vTaskDelete(NULL);
}

/*************************** 播放任务 ***************************/
// 读单声道WAV数据，每个采样复制成左右两声道后经 bsp_i2s_write 输出
static void play_task(void *arg)
{
    (void)arg;
    int16_t mono[PLAY_SAMPLES];
    int16_t stereo[PLAY_SAMPLES * 2];
    size_t written = 0;

    FILE *f = fopen(s_play_path, "rb");
    if (!f) {
        if (icon_flag == 8) {
            lvgl_port_lock(0);
            if (s_status_label) lv_label_set_text(s_status_label, "无法打开文件");
            lvgl_port_unlock();
        }
        s_play_busy = false;
        vTaskDelete(NULL);
        return;
    }
    fseek(f, 44, SEEK_SET); // 跳过WAV头，直接读PCM数据

    pa_en(1); // 打开功放
    while (!s_play_stop && icon_flag == 8) {
        size_t n = fread(mono, sizeof(int16_t), PLAY_SAMPLES, f);
        if (n == 0) break;
        for (size_t i = 0; i < n; i++) {          // 单声道 -> 立体声 L=R
            stereo[i * 2]     = mono[i];
            stereo[i * 2 + 1] = mono[i];
        }
        bsp_i2s_write(stereo, n * 4, &written, portMAX_DELAY);
    }
    pa_en(0); // 关闭功放
    fclose(f);

    if (icon_flag == 8) {
        lvgl_port_lock(0);
        if (s_status_label) lv_label_set_text(s_status_label, "就绪");
        lvgl_port_unlock();
    }
    s_play_busy = false;
    vTaskDelete(NULL);
}

/*************************** 列表项点击：播放 ***************************/
// 记录选中文件路径，并启动播放任务（上一次播放未完则先请求停止）
static void file_btn_cb(lv_event_t *e)
{
    lv_obj_t *btn = lv_event_get_target(e);
    const char *name = lv_list_get_btn_text(lv_obj_get_parent(btn), btn);
    if (!name) return;
    snprintf(s_selected, sizeof(s_selected), REC_DIR "/%s", name);

    if (s_rec_busy) return;             // 录音中不播放
    if (s_play_busy) {                  // 正在播放则先停止，等下一次点击
        s_play_stop = true;
        return;
    }
    snprintf(s_play_path, sizeof(s_play_path), "%s", s_selected);
    s_play_stop = false;
    s_play_busy = true;
    if (s_status_label) lv_label_set_text(s_status_label, "播放中...");
    xTaskCreatePinnedToCore(play_task, "play", 4096, NULL, 5, NULL, 1);
}

/*************************** 列表刷新 ***************************/
// 重新扫描 REC 目录填充列表（调用前必须已持有LVGL锁）
static void refresh_list(void)
{
    if (!s_file_list) return;
    lv_obj_clean(s_file_list);

    DIR *dir = opendir(REC_DIR);
    if (!dir) {
        lv_obj_t *btn = lv_list_add_btn(s_file_list, NULL, "暂无录音");
        lv_obj_clear_flag(btn, LV_OBJ_FLAG_CLICKABLE);
        return;
    }
    struct dirent *ent;
    int count = 0;
    while ((ent = readdir(dir)) != NULL) {
        if (ent->d_type != DT_REG) continue;
        const char *ext = strrchr(ent->d_name, '.');
        if (!ext || strcmp(ext, ".wav") != 0) continue;
        lv_obj_t *btn = lv_list_add_btn(s_file_list, LV_SYMBOL_AUDIO, ent->d_name);
        lv_obj_t *icon = lv_obj_get_child(btn, 0);
        if (icon) lv_obj_set_style_text_font(icon, &lv_font_montserrat_14, 0);
        lv_obj_add_event_cb(btn, file_btn_cb, LV_EVENT_CLICKED, NULL);
        count++;
    }
    closedir(dir);
    if (count == 0) {
        lv_obj_t *btn = lv_list_add_btn(s_file_list, NULL, "暂无录音");
        lv_obj_clear_flag(btn, LV_OBJ_FLAG_CLICKABLE);
    }
}

/*************************** 按钮回调 ***************************/
// 开始录音
static void start_btn_cb(lv_event_t *e)
{
    if (!s_sd_ok) {
        if (s_status_label) lv_label_set_text(s_status_label, "未检测到SD卡");
        return;
    }
    if (s_rec_busy || s_play_busy) return;  // 防止重复启动
    s_recording = true;
    s_rec_busy  = true;
    if (s_status_label) lv_label_set_text(s_status_label, "录音中 00:00");
    xTaskCreatePinnedToCore(rec_task, "rec", 4096, NULL, 5, NULL, 1);
}

// 停止录音（录音任务自行收尾并回填WAV头）
static void stop_btn_cb(lv_event_t *e)
{
    if (s_rec_busy) {
        s_recording = false;
        if (s_status_label) lv_label_set_text(s_status_label, "正在保存...");
    } else if (s_play_busy) {
        s_play_stop = true;
    }
}

// 删除当前选中的录音并刷新列表
static void del_btn_cb(lv_event_t *e)
{
    if (s_selected[0] == '\0') {
        if (s_status_label) lv_label_set_text(s_status_label, "请先选择录音");
        return;
    }
    if (s_play_busy) { s_play_stop = true; return; } // 先停止播放，再点一次删除
    if (remove(s_selected) == 0) {
        ESP_LOGI(TAG, "已删除 %s", s_selected);
        if (s_status_label) lv_label_set_text(s_status_label, "已删除");
    } else {
        if (s_status_label) lv_label_set_text(s_status_label, "删除失败");
    }
    s_selected[0] = '\0';
    refresh_list(); // 事件回调中已持有LVGL锁
}

/*************************** 返回主界面 ***************************/
static void back_cb(lv_event_t *e)
{
    // 先请求停止录音/播放，等待任务收尾（短暂释放锁避免与任务死锁）
    s_recording = false;
    s_play_stop = true;
    int wait = 0;
    while ((s_rec_busy || s_play_busy) && wait++ < 100) {
        lvgl_port_unlock();
        vTaskDelay(pdMS_TO_TICKS(20));
        lvgl_port_lock(0);
    }
    bsp_sdcard_unmount(); // 卸载SD卡
    lv_obj_del(icon_in_obj); // 回到主界面
    icon_in_obj = NULL;
    icon_flag = 0;
    s_status_label = NULL;
    s_file_list = NULL;
    s_selected[0] = '\0';
}

/*************************** 入口 ***************************/
// 进入录音机应用（主界面图标点击事件）
void app_recorder_open(lv_event_t *e)
{
    lv_obj_t *page = app_page_open("录音机", 0xe91e63, back_cb);
    icon_flag = 8;

    // 挂载SD卡并创建录音目录（已存在则忽略）
    s_sd_ok = (bsp_sdcard_mount() == ESP_OK);
    if (s_sd_ok) mkdir(REC_DIR, 0755);

    lvgl_port_lock(0);
    if (!s_sd_ok) {
        // 未挂载成功：只显示提示，仍可点标题栏返回
        lv_obj_t *tip = lv_label_create(page);
        lv_label_set_text(tip, "未检测到SD卡");
        lv_obj_set_style_text_font(tip, &font_alipuhui20, 0);
        lv_obj_align(tip, LV_ALIGN_TOP_MID, 0, 100);
        lvgl_port_unlock();
        return;
    }

    // 状态/时间标签
    s_status_label = lv_label_create(page);
    lv_label_set_text(s_status_label, "就绪");
    lv_obj_set_style_text_font(s_status_label, &font_alipuhui20, 0);
    lv_obj_align(s_status_label, LV_ALIGN_TOP_MID, 0, 8);

    // 三个功能按钮：录音 / 停止 / 删除
    static const struct { const char *txt; int16_t x; void (*cb)(lv_event_t *); } btns[3] = {
        {"录音", -105, start_btn_cb},
        {"停止",    0, stop_btn_cb},
        {"删除",  105, del_btn_cb},
    };
    for (int i = 0; i < 3; i++) {
        lv_obj_t *btn = lv_btn_create(page);
        lv_obj_set_size(btn, 90, 34);
        lv_obj_align(btn, LV_ALIGN_TOP_MID, btns[i].x, 36);
        lv_obj_add_event_cb(btn, btns[i].cb, LV_EVENT_CLICKED, NULL);
        lv_obj_t *lab = lv_label_create(btn);
        lv_label_set_text(lab, btns[i].txt);
        lv_obj_set_style_text_font(lab, &font_alipuhui20, 0);
        lv_obj_center(lab);
    }

    // 录音文件列表
    s_file_list = lv_list_create(page);
    lv_obj_set_size(s_file_list, 300, 122);
    lv_obj_align(s_file_list, LV_ALIGN_TOP_MID, 0, 78);
    lv_obj_set_style_border_width(s_file_list, 0, 0);
    lv_obj_set_style_text_font(s_file_list, &font_alipuhui20, 0);
    refresh_list(); // 已持有LVGL锁
    lvgl_port_unlock();
}
