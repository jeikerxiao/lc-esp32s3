#include "app_ui.h"
#include "app_common.h"
#include "audio_player.h"
#include "esp32_s3_szp.h"
#include "file_iterator.h"
#include "string.h"
#include <strings.h>
#include <dirent.h>
#include "bt/ble_hidd_demo.h"
#include "esp_wifi.h"
#include "freertos/event_groups.h"
#include "esp_event.h"
#include <sys/stat.h>
#include <time.h>
#include <sys/time.h>
#include "esp_netif_sntp.h"
#include "esp_sntp.h"

static const char *TAG = "app_ui";

LV_FONT_DECLARE(font_alipuhui20);

lv_obj_t * main_obj; // 主界面
lv_obj_t * main_text_label; // 主界面 欢迎语
lv_obj_t * icon_in_obj; // 应用界面
int icon_flag; // 标记现在进入哪个应用 在主界面时为0

/*********************** 开机界面 ****************************/
lv_obj_t * lckfb_logo;
// 设置角度的回调函数
static void set_angle(void * img, int32_t v)
{
    lv_img_set_angle(img, v); // 设置图片的旋转角度
}
// 开机界面
void lv_gui_start(void)
{
    lvgl_port_lock(0);
    // 显示logo
    LV_IMG_DECLARE(image_lckfb_logo);  // 声明图片
    lckfb_logo = lv_img_create(lv_scr_act()); // 创建图片对象
    lv_img_set_src(lckfb_logo, &image_lckfb_logo); // 设置图片对象的图片源
    lv_obj_align(lckfb_logo, LV_ALIGN_CENTER, 0, 0); // 设置图片位置为屏幕正中心
    lv_img_set_pivot(lckfb_logo, 60, 60); // 设置图片围绕自己的中心位置旋转   

    // 设置旋转动画
    lv_anim_t a; // 创建动画变量
    lv_anim_init(&a); // 初始化动画变量
    lv_anim_set_var(&a, lckfb_logo); // 动画变量赋值为logo图片
    lv_anim_set_exec_cb(&a, set_angle); // 创建设置角度的回调函数
    lv_anim_set_values(&a, 0, 3600); // 设置动画旋转角度的开始值和结尾值
    lv_anim_set_time(&a, 200); // 设置转一圈的周期是200毫秒
    lv_anim_set_repeat_count(&a, 5); // 设置旋转5次
    lv_anim_start(&a); // 动画开始
    lvgl_port_unlock();
}



/******************************** 第1个图标 姿态传感器 应用程序*************************************************************************************/
lv_obj_t * label_x; // x角度值
lv_obj_t * label_y; // y角度值
lv_obj_t * label_z; // z角度值
lv_obj_t * x_bar;   // x角度bar
lv_obj_t * y_bar;   // y角度bar
lv_obj_t * z_bar;   // z角度bar

lv_obj_t * att_label; // 标题栏文字

lv_timer_t * my_lv_timer;

lv_obj_t *btn_att_back; // att姿态应用 后退按钮

// 返回主界面按钮事件处理函数
static void btn_att_back_cb(lv_event_t * e)
{
    lv_timer_del(my_lv_timer);
    qmi8658_close(); // 关闭芯片运行
    lv_obj_del(icon_in_obj); // 删除画布
    icon_flag = 0;
}

// 定时更新姿态角度值
void att_update_cb(lv_timer_t * timer)
{
    t_sQMI8658 QMI8658;
    int att_x, att_y, att_z;
    // 获取XYZ角度
    qmi8658_fetch_angleFromAcc(&QMI8658);
    att_x = round(QMI8658.AngleX);  // 四舍五入
    att_y = round(QMI8658.AngleY);  // 四舍五入
    att_z = round(QMI8658.AngleZ);  // 四舍五入
    // 更新角度值
    lv_label_set_text_fmt(label_x, "X: %d", att_x);
    lv_label_set_text_fmt(label_y, "Y: %d", att_y);
    lv_label_set_text_fmt(label_z, "Z: %d", att_z);
    // 更新角度bar
    lv_bar_set_start_value(x_bar, att_x-10, LV_ANIM_OFF);
    lv_bar_set_value(x_bar, att_x+10, LV_ANIM_OFF);
    lv_bar_set_start_value(y_bar, att_y-10, LV_ANIM_OFF);
    lv_bar_set_value(y_bar, att_y+10, LV_ANIM_OFF);
    lv_bar_set_start_value(z_bar, att_z-10, LV_ANIM_OFF);
    lv_bar_set_value(z_bar, att_z+10, LV_ANIM_OFF);

    // 判断运动状态
    uint8_t status = qmi8658_fetch_motion();
    if (status & 0x20) // 判断是否发生Any-Motion
    {
        lv_label_set_text(att_label, "运动或震动");
    }
    else if (status & 0x40) // 判断是否发生No-Motion
    {
        lv_label_set_text(att_label, "静止");
    }
    else if (status & 0x80) // 判断是否发生Significant-Motion
    {
        lv_label_set_text(att_label, "剧烈运动");
    }
}

// 姿态监测处理任务
static void task_process_att(void *arg)
{
    esp_err_t ret = qmi8658_init();
    if (ret != ESP_OK){ // 如果传感器初始化不成功
        // 液晶屏提醒用户 传感器错误
        lvgl_port_lock(0);
        lv_obj_t * label = lv_label_create(icon_in_obj);
        lv_label_set_text(label, "QMI8658传感器错误...");
        lv_obj_set_style_text_color(label, lv_color_hex(0x000000), 0); 
        lv_obj_set_style_text_font(label, &font_alipuhui20, 0);
        lv_obj_align(label, LV_ALIGN_CENTER, 0, 0);
        lvgl_port_unlock();
        vTaskDelay(1000 / portTICK_PERIOD_MS); // 提示词保留1秒
        lvgl_port_lock(0);
        lv_obj_del(icon_in_obj); // 删除画布 回到主界面
        lvgl_port_unlock();
    }
    else{ // 传感器初始化成功
        lvgl_port_lock(0);
        // 显示x角度值
        label_x = lv_label_create(icon_in_obj);
        lv_label_set_text(label_x, "X:");
        lv_obj_set_style_text_color(label_x, lv_color_hex(0x000000), 0);
        lv_obj_set_style_text_font(label_x, &lv_font_montserrat_20, 0);
        lv_obj_align(label_x, LV_ALIGN_TOP_LEFT, 20, 60);
        // 显示x角度bar
        x_bar = lv_bar_create(icon_in_obj);
        lv_obj_set_size(x_bar, 200, 25);
        lv_obj_align(x_bar, LV_ALIGN_TOP_LEFT, 80, 60);
        lv_bar_set_mode(x_bar, LV_BAR_MODE_RANGE);
        lv_bar_set_range(x_bar, -101, 101);
        lv_bar_set_start_value(x_bar, -10, LV_ANIM_OFF);
        lv_bar_set_value(x_bar, 10, LV_ANIM_OFF);

        // 显示y角度值
        label_y = lv_label_create(icon_in_obj);
        lv_label_set_text(label_y, "Y:");
        lv_obj_set_style_text_color(label_y, lv_color_hex(0x000000), 0);
        lv_obj_set_style_text_font(label_y, &lv_font_montserrat_20, 0);
        lv_obj_align(label_y, LV_ALIGN_TOP_LEFT, 20, 120);
        // 显示y角度bar
        y_bar = lv_bar_create(icon_in_obj);
        lv_obj_set_size(y_bar, 200, 25);
        lv_obj_align(y_bar, LV_ALIGN_TOP_LEFT, 80, 120);
        lv_bar_set_mode(y_bar, LV_BAR_MODE_RANGE);
        lv_bar_set_range(y_bar, -101, 101);
        lv_bar_set_start_value(y_bar, -10, LV_ANIM_OFF);
        lv_bar_set_value(y_bar, 10, LV_ANIM_OFF);

        // 显示z角度值
        label_z = lv_label_create(icon_in_obj);
        lv_label_set_text(label_z, "Z:");
        lv_obj_set_style_text_color(label_z, lv_color_hex(0x000000), 0);
        lv_obj_set_style_text_font(label_z, &lv_font_montserrat_20, 0);
        lv_obj_align(label_z, LV_ALIGN_TOP_LEFT, 20, 180);
        // 显示z角度bar
        z_bar = lv_bar_create(icon_in_obj);
        lv_obj_set_size(z_bar, 200, 25);
        lv_obj_align(z_bar, LV_ALIGN_TOP_LEFT, 80, 180);
        lv_bar_set_mode(z_bar, LV_BAR_MODE_RANGE);
        lv_bar_set_range(z_bar, -101, 101);
        lv_bar_set_start_value(z_bar, -10, LV_ANIM_OFF);
        lv_bar_set_value(z_bar, 10, LV_ANIM_OFF);

        lvgl_port_unlock();

        // 创建一个lv_timer 用于更新角度
        my_lv_timer = lv_timer_create(att_update_cb, 200, NULL); 
    }

    vTaskDelete(NULL);
}

static void att_event_handler(lv_event_t * e)
{
    // 创建一个界面
    static lv_style_t style;
    lv_style_init(&style);
    lv_style_set_radius(&style, 10);  
    lv_style_set_bg_opa( &style, LV_OPA_COVER );
    lv_style_set_bg_color(&style, lv_color_hex(0xffffff));
    lv_style_set_border_width(&style, 0);
    lv_style_set_pad_all(&style, 0);
    lv_style_set_width(&style, 320);  
    lv_style_set_height(&style, 240); 

    icon_in_obj = lv_obj_create(lv_scr_act());
    lv_obj_add_style(icon_in_obj, &style, 0);

    // 创建标题背景
    lv_obj_t *att_title = lv_obj_create(icon_in_obj);
    lv_obj_set_size(att_title, 320, 40);
    lv_obj_set_style_pad_all(att_title, 0, 0);  // 设置间隙
    lv_obj_align(att_title, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_bg_color(att_title, lv_color_hex(0x30a830), 0);
    // 显示标题
    att_label = lv_label_create(att_title);
    lv_label_set_text(att_label, "运动监测");
    lv_obj_set_style_text_color(att_label, lv_color_hex(0xffffff), 0); 
    lv_obj_set_style_text_font(att_label, &font_alipuhui20, 0);
    lv_obj_align(att_label, LV_ALIGN_CENTER, 0, 0);
    // 创建后退按钮
    btn_att_back = lv_btn_create(att_title);
    lv_obj_align(btn_att_back, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_size(btn_att_back, 60, 30);
    lv_obj_set_style_border_width(btn_att_back, 0, 0); // 设置边框宽度
    lv_obj_set_style_pad_all(btn_att_back, 0, 0);  // 设置间隙
    lv_obj_set_style_bg_opa(btn_att_back, LV_OPA_TRANSP, LV_PART_MAIN); // 背景透明
    lv_obj_set_style_shadow_opa(btn_att_back, LV_OPA_TRANSP, LV_PART_MAIN); // 阴影透明
    lv_obj_add_event_cb(btn_att_back, btn_att_back_cb, LV_EVENT_CLICKED, NULL); // 添加按键处理函数

    lv_obj_t *label_back = lv_label_create(btn_att_back); 
    lv_label_set_text(label_back, LV_SYMBOL_LEFT);  // 按键上显示左箭头符号
    lv_obj_set_style_text_font(label_back, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(label_back, lv_color_hex(0xffffff), 0); 
    lv_obj_align(label_back, LV_ALIGN_CENTER, -10, 0);

    icon_flag = 1; // 标记已经进入第一个应用
    // 栈需足够容纳 qmi8658_init 的 i2c 事务与随后创建多个 LVGL 控件，2KB 会溢出，用 4KB
    xTaskCreatePinnedToCore(task_process_att, "task_process_att", 4 * 1024, NULL, 5, NULL, 1);
}


/*********************  第2个图标   音乐播放器 *********************************************************************************************/
static audio_player_config_t player_config = {0};
static uint8_t g_sys_volume = VOLUME_DEFAULT;

/* ---- 自建音乐库：同时扫描 SPIFFS 和 SD 卡（递归查找 mp3） ---- */
#define MUSIC_MAX_FILES   64
static char *s_music_paths[MUSIC_MAX_FILES];   // 每首歌的完整路径
static char *s_music_names[MUSIC_MAX_FILES];   // 每首歌显示的名称
static int   s_music_count = 0;                // 歌曲总数
static int   s_music_index = 0;                // 当前播放序号

lv_obj_t *music_list;
lv_obj_t *label_play_pause;
lv_obj_t *btn_play_pause;
lv_obj_t *volume_slider;

lv_obj_t *music_title_label;
lv_obj_t *btn_music_back;
static lv_obj_t *music_progress = NULL;        // 播放进度条（仅显示，估算）
static lv_obj_t *music_time_label = NULL;      // 已播放/总时长
static lv_timer_t *music_progress_timer = NULL;
static int s_music_elapsed = 0;                // 已播放秒数
static int s_music_total   = 0;                // 估算总时长秒

// 释放音乐库占用的内存
static void music_lib_clear(void)
{
    for (int i = 0; i < s_music_count; i++) {
        free(s_music_paths[i]);
        free(s_music_names[i]);
    }
    s_music_count = 0;
    s_music_index = 0;
}

// 判断文件名是否为 mp3（不区分大小写）
static bool is_mp3_name(const char *name)
{
    const char *dot = strrchr(name, '.');
    return dot && strcasecmp(dot, ".mp3") == 0;
}

// 递归扫描一个目录，把找到的 mp3 加入音乐库
static void music_lib_scan(const char *dirpath)
{
    DIR *dir = opendir(dirpath);
    if (!dir) return;                 // 目录不存在（如未插 SD 卡）则跳过
    struct dirent *ent;
    while ((ent = readdir(dir)) != NULL && s_music_count < MUSIC_MAX_FILES) {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) continue;
        char full[256];
        snprintf(full, sizeof(full), "%s/%s", dirpath, ent->d_name);
        if (ent->d_type == DT_DIR) {
            music_lib_scan(full);     // 进入子目录
        } else if (ent->d_type == DT_REG && is_mp3_name(ent->d_name)) {
            s_music_paths[s_music_count] = strdup(full);
            s_music_names[s_music_count] = strdup(ent->d_name);
            s_music_count++;
        }
    }
    closedir(dir);
}

static void music_lib_next(void) { if (s_music_count) s_music_index = (s_music_index + 1) % s_music_count; }
static void music_lib_prev(void) { if (s_music_count) s_music_index = (s_music_index - 1 + s_music_count) % s_music_count; }

/* MPEG1 Layer3 码率表（kbps），索引即帧头里的 bitrate_index */
static const int s_bitrate_table[16] = {0,32,40,48,56,64,80,96,112,128,160,192,224,256,320,0};

// 粗略估算 mp3 时长（秒）：解析首个帧头得到码率，再用文件大小换算
static int estimate_mp3_duration(const char *path)
{
    FILE *fp = fopen(path, "rb");
    if (!fp) return 0;
    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    // 用堆缓冲读取帧头，避免在 LVGL 任务栈上放 2KB 数组导致栈溢出
    const int hdr_len = 2048;
    uint8_t *buf = malloc(hdr_len);
    if (!buf) { fclose(fp); return 0; }
    int n = (int) fread(buf, 1, hdr_len, fp);
    fclose(fp);
    int duration = 0;
    for (int i = 0; i + 3 < n; i++) {
        if (buf[i] == 0xFF && (buf[i+1] & 0xE0) == 0xE0) {   // 帧同步头
            int version = (buf[i+1] >> 3) & 0x03;            // 3=MPEG1, 2=MPEG2, 0=MPEG2.5
            int layer   = (buf[i+1] >> 1) & 0x03;            // 1=Layer3
            int br_idx  = (buf[i+2] >> 4) & 0x0F;
            if (layer != 1 || br_idx == 0 || br_idx == 15) continue;
            int br = s_bitrate_table[br_idx];
            if (version != 3) br /= 2;                       // MPEG2/2.5 码率减半
            if (br <= 0) continue;
            duration = (int) (size / ((long) br * 1000 / 8));
            break;
        }
    }
    free(buf);
    return duration;
}

// 进度条定时刷新（每秒一次）
static void music_progress_cb(lv_timer_t *t)
{
    if (audio_player_get_state() == AUDIO_PLAYER_STATE_PLAYING) {
        s_music_elapsed++;
    }
    if (s_music_total > 0) {
        int pct = s_music_elapsed * 100 / s_music_total;
        if (pct > 100) pct = 100;
        lv_bar_set_value(music_progress, pct, LV_ANIM_OFF);
    }
    lv_label_set_text_fmt(music_time_label, "%02d:%02d / %02d:%02d",
                          s_music_elapsed / 60, s_music_elapsed % 60,
                          s_music_total / 60, s_music_total % 60);
}

// 播放指定序号的音乐
static void play_index(int index)
{
    ESP_LOGI(TAG, "play_index(%d)", index);
    if (index < 0 || index >= s_music_count) {
        ESP_LOGE(TAG, "invalid index %d", index);
        return;
    }
    s_music_index = index;
    const char *filename = s_music_paths[index];

    FILE *fp = fopen(filename, "rb");
    if (fp) {
        ESP_LOGI(TAG, "Playing '%s'", filename);
        s_music_elapsed = 0;
        s_music_total = estimate_mp3_duration(filename);
        audio_player_play(fp);
    } else {
        ESP_LOGE(TAG, "unable to open index %d, filename '%s'", index, filename);
    }
}

// 设置声音处理函数
static esp_err_t _audio_player_mute_fn(AUDIO_PLAYER_MUTE_SETTING setting)
{
    esp_err_t ret = ESP_OK;
    // 判断是否需要静音
    bsp_codec_mute_set(setting == AUDIO_PLAYER_MUTE ? true : false);
    // 如果不是静音 设置音量
    if (setting == AUDIO_PLAYER_UNMUTE) {
        bsp_codec_volume_set(g_sys_volume, NULL);
    }
    ret = ESP_OK;

    return ret;
}

// 播放音乐函数 播放音乐的时候 会不断进入
static esp_err_t _audio_player_write_fn(void *audio_buffer, size_t len, size_t *bytes_written, uint32_t timeout_ms)
{
    esp_err_t ret = ESP_OK;

    ret = bsp_i2s_write(audio_buffer, len, bytes_written, timeout_ms);

    return ret;
}

// 设置采样率 播放的时候进入一次
static esp_err_t _audio_player_std_clock(uint32_t rate, uint32_t bits_cfg, i2s_slot_mode_t ch)
{
    esp_err_t ret = ESP_OK;

    // ret = bsp_codec_set_fs(rate, bits_cfg, ch); // 如果播放的音乐固定是16000采样率 这里可以不用打开 如果采样率未知 把这里打开
    return ret;
}

// 回调函数 播放器每次动作都会进入
static void _audio_player_callback(audio_player_cb_ctx_t *ctx)
{
    ESP_LOGI(TAG, "ctx->audio_event = %d", ctx->audio_event);
    switch (ctx->audio_event) {
    case AUDIO_PLAYER_CALLBACK_EVENT_IDLE: {  // 播放完一首歌 进入这个case
        ESP_LOGI(TAG, "AUDIO_PLAYER_REQUEST_IDLE");
        // 指向下一首歌
        music_lib_next();
        int index = s_music_index;
        ESP_LOGI(TAG, "playing index '%d'", index);
        play_index(index);
        // 修改当前播放的音乐名称
        lvgl_port_lock(0);
        lv_dropdown_set_selected(music_list, index);
        lvgl_port_unlock();
        break;
    }
    case AUDIO_PLAYER_CALLBACK_EVENT_PLAYING: // 正在播放音乐
        ESP_LOGI(TAG, "AUDIO_PLAYER_REQUEST_PLAY");
        pa_en(1); // 打开音频功放
        break;
    case AUDIO_PLAYER_CALLBACK_EVENT_PAUSE: // 正在暂停音乐
        ESP_LOGI(TAG, "AUDIO_PLAYER_REQUEST_PAUSE");
        pa_en(0); // 关闭音频功放
        break;
    default:
        break;
    }
}

// mp3播放器初始化
void mp3_player_init(void)
{
    // 扫描 SPIFFS 和 SD 卡里的所有 mp3
    music_lib_clear();
    music_lib_scan(SPIFFS_BASE);
    music_lib_scan(SD_MOUNT_POINT);
    ESP_LOGI(TAG, "music library: %d files", s_music_count);

    // 初始化音频播放
    player_config.mute_fn = _audio_player_mute_fn;
    player_config.write_fn = _audio_player_write_fn;
    player_config.clk_set_fn = _audio_player_std_clock;
    player_config.priority = 6;
    player_config.coreID = 1;

    ESP_ERROR_CHECK(audio_player_new(player_config));
    ESP_ERROR_CHECK(audio_player_callback_register(_audio_player_callback, NULL));
}


// 按钮样式相关定义
typedef struct {
    lv_style_t style_bg;
    lv_style_t style_focus_no_outline;
} button_style_t;

static button_style_t g_btn_styles;

button_style_t *ui_button_styles(void)
{
    return &g_btn_styles;
}

// 按钮样式初始化
static void ui_button_style_init(void)
{
    /*Init the style for the default state*/
    lv_style_init(&g_btn_styles.style_focus_no_outline);
    lv_style_set_outline_width(&g_btn_styles.style_focus_no_outline, 0);

    lv_style_init(&g_btn_styles.style_bg);
    lv_style_set_bg_opa(&g_btn_styles.style_bg, LV_OPA_100);
    lv_style_set_bg_color(&g_btn_styles.style_bg, lv_color_make(255, 255, 255));
    lv_style_set_shadow_width(&g_btn_styles.style_bg, 0);
}

// 播放暂停按钮 事件处理函数
static void btn_play_pause_cb(lv_event_t *event)
{
    lv_obj_t *btn = lv_event_get_target(event);
    lv_obj_t *lab = (lv_obj_t *) btn->user_data;

    audio_player_state_t state = audio_player_get_state();
    printf("state=%d\n", state);
    if(state == AUDIO_PLAYER_STATE_IDLE){
        lvgl_port_lock(0);
        lv_label_set_text_static(lab, LV_SYMBOL_PAUSE);
        lvgl_port_unlock();
        int index = s_music_index;
        ESP_LOGI(TAG, "playing index '%d'", index);
        play_index(index);
    }else if (state == AUDIO_PLAYER_STATE_PAUSE) {
        lvgl_port_lock(0);
        lv_label_set_text_static(lab, LV_SYMBOL_PAUSE);
        lvgl_port_unlock();
        audio_player_resume();
    } else if (state == AUDIO_PLAYER_STATE_PLAYING) {
        lvgl_port_lock(0);
        lv_label_set_text_static(lab, LV_SYMBOL_PLAY);
        lvgl_port_unlock();
        audio_player_pause();
    }
}

// 上一首 下一首 按键事件处理函数
static void btn_prev_next_cb(lv_event_t *event)
{
    bool is_next = (bool) event->user_data;

    if (is_next) {
        ESP_LOGI(TAG, "btn next");
        music_lib_next();
    } else {
        ESP_LOGI(TAG, "btn prev");
        music_lib_prev();
    }
    // 修改当前的音乐名称
    int index = s_music_index;
    lvgl_port_lock(0);
    lv_dropdown_set_selected(music_list, index);
    lvgl_port_unlock();
    // 执行音乐事件
    audio_player_state_t state = audio_player_get_state();
    printf("prev_next_state=%d\n", state);
    if (state == AUDIO_PLAYER_STATE_IDLE) { 
        // Nothing to do
    }else if (state == AUDIO_PLAYER_STATE_PAUSE){ // 如果当前正在暂停歌曲
        ESP_LOGI(TAG, "playing index '%d'", index);
        play_index(index);
        audio_player_pause();
    } else if (state == AUDIO_PLAYER_STATE_PLAYING) { // 如果当前正在播放歌曲
        // 播放歌曲
        ESP_LOGI(TAG, "playing index '%d'", index);
        play_index(index);
    }
}

// 音量调节滑动条 事件处理函数
static void volume_slider_cb(lv_event_t *event)
{
    lv_obj_t *slider = lv_event_get_target(event);
    int volume = lv_slider_get_value(slider); // 获取slider的值
    bsp_codec_volume_set(volume, NULL); // 设置声音大小
    g_sys_volume = volume; // 把声音赋值给g_sys_volume保存
    g_settings.volume = volume; // 记忆音量到设置
    settings_save();            // 写入 NVS，下次开机自动恢复
    ESP_LOGI(TAG, "volume '%d'", volume);
}

// 音乐列表 点击事件处理函数
static void music_list_cb(lv_event_t *event)
{   
    uint16_t index = lv_dropdown_get_selected(music_list);
    ESP_LOGI(TAG, "switching index to '%d'", index);

    audio_player_state_t state = audio_player_get_state();
    if (state == AUDIO_PLAYER_STATE_PAUSE){ // 如果当前正在暂停歌曲
        play_index(index);
        audio_player_pause();
    } else if (state == AUDIO_PLAYER_STATE_PLAYING) { // 如果当前正在播放歌曲
        play_index(index);
    } else {
        s_music_index = index; // 空闲时仅记录选择，等点击播放键再播放
    }
}

// 音乐名称加入列表
static void build_file_list(lv_obj_t *music_list)
{
    lvgl_port_lock(0);
    lv_dropdown_clear_options(music_list);
    for (int i = 0; i < s_music_count; i++) {
        lv_dropdown_add_option(music_list, s_music_names[i], i); // 添加音乐名称到列表中
    }
    if (s_music_count == 0) {
        lv_dropdown_add_option(music_list, "无MP3文件", 0);
    }
    lv_dropdown_set_selected(music_list, 0); // 选择列表中的第一个
    lvgl_port_unlock();
}

// 播放器界面初始化
void music_ui(void)
{
    lvgl_port_lock(0);

    ui_button_style_init();// 初始化按键风格

    /* 创建播放暂停控制按键 */
    btn_play_pause = lv_btn_create(icon_in_obj);
    lv_obj_align(btn_play_pause, LV_ALIGN_CENTER, 0, 40);
    lv_obj_set_size(btn_play_pause, 50, 50);
    lv_obj_set_style_radius(btn_play_pause, 25, LV_STATE_DEFAULT);
    lv_obj_add_flag(btn_play_pause, LV_OBJ_FLAG_CHECKABLE);

    lv_obj_add_style(btn_play_pause, &ui_button_styles()->style_focus_no_outline, LV_STATE_FOCUS_KEY);
    lv_obj_add_style(btn_play_pause, &ui_button_styles()->style_focus_no_outline, LV_STATE_FOCUSED);

    label_play_pause = lv_label_create(btn_play_pause);

    lv_label_set_text_static(label_play_pause, LV_SYMBOL_PLAY);
    lv_obj_center(label_play_pause);

    lv_obj_set_user_data(btn_play_pause, (void *) label_play_pause);
    lv_obj_add_event_cb(btn_play_pause, btn_play_pause_cb, LV_EVENT_VALUE_CHANGED, NULL);

    /* 创建上一首控制按键 */
    lv_obj_t *btn_play_prev = lv_btn_create(icon_in_obj);
    lv_obj_set_size(btn_play_prev, 50, 50);
    lv_obj_set_style_radius(btn_play_prev, 25, LV_STATE_DEFAULT);
    lv_obj_clear_flag(btn_play_prev, LV_OBJ_FLAG_CHECKABLE);
    lv_obj_align_to(btn_play_prev, btn_play_pause, LV_ALIGN_OUT_LEFT_MID, -40, 0); 

    lv_obj_add_style(btn_play_prev, &ui_button_styles()->style_focus_no_outline, LV_STATE_FOCUS_KEY);
    lv_obj_add_style(btn_play_prev, &ui_button_styles()->style_focus_no_outline, LV_STATE_FOCUSED);
    lv_obj_add_style(btn_play_prev, &ui_button_styles()->style_bg, LV_STATE_FOCUS_KEY);
    lv_obj_add_style(btn_play_prev, &ui_button_styles()->style_bg, LV_STATE_FOCUSED);
    lv_obj_add_style(btn_play_prev, &ui_button_styles()->style_bg, LV_STATE_DEFAULT);

    lv_obj_t *label_prev = lv_label_create(btn_play_prev);
    lv_label_set_text_static(label_prev, LV_SYMBOL_PREV);
    lv_obj_set_style_text_font(label_prev, &lv_font_montserrat_24, LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(label_prev, lv_color_make(0, 0, 0), LV_STATE_DEFAULT);
    lv_obj_center(label_prev);
    lv_obj_set_user_data(btn_play_prev, (void *) label_prev);
    lv_obj_add_event_cb(btn_play_prev, btn_prev_next_cb, LV_EVENT_CLICKED, (void *) false);

    /* 创建下一首控制按键 */
    lv_obj_t *btn_play_next = lv_btn_create(icon_in_obj);
    lv_obj_set_size(btn_play_next, 50, 50);
    lv_obj_set_style_radius(btn_play_next, 25, LV_STATE_DEFAULT);
    lv_obj_clear_flag(btn_play_next, LV_OBJ_FLAG_CHECKABLE);
    lv_obj_align_to(btn_play_next, btn_play_pause, LV_ALIGN_OUT_RIGHT_MID, 40, 0);

    lv_obj_add_style(btn_play_next, &ui_button_styles()->style_focus_no_outline, LV_STATE_FOCUS_KEY);
    lv_obj_add_style(btn_play_next, &ui_button_styles()->style_focus_no_outline, LV_STATE_FOCUSED);
    lv_obj_add_style(btn_play_next, &ui_button_styles()->style_bg, LV_STATE_FOCUS_KEY);
    lv_obj_add_style(btn_play_next, &ui_button_styles()->style_bg, LV_STATE_FOCUSED);
    lv_obj_add_style(btn_play_next, &ui_button_styles()->style_bg, LV_STATE_DEFAULT);

    lv_obj_t *label_next = lv_label_create(btn_play_next);
    lv_label_set_text_static(label_next, LV_SYMBOL_NEXT);
    lv_obj_set_style_text_font(label_next, &lv_font_montserrat_24, LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(label_next, lv_color_make(0, 0, 0), LV_STATE_DEFAULT);
    lv_obj_center(label_next);
    lv_obj_set_user_data(btn_play_next, (void *) label_next);
    lv_obj_add_event_cb(btn_play_next, btn_prev_next_cb, LV_EVENT_CLICKED, (void *) true);

    /* 创建声音调节滑动条 */
    volume_slider = lv_slider_create(icon_in_obj);
    lv_obj_set_size(volume_slider, 200, 10);
    lv_obj_set_ext_click_area(volume_slider, 15);
    lv_obj_align(volume_slider, LV_ALIGN_BOTTOM_MID, 0, -20);
    lv_slider_set_range(volume_slider, 0, 100);
    lv_slider_set_value(volume_slider, g_sys_volume, LV_ANIM_ON);
    lv_obj_add_event_cb(volume_slider, volume_slider_cb, LV_EVENT_VALUE_CHANGED, NULL);

    lv_obj_t *lab_vol_min = lv_label_create(icon_in_obj);
    lv_label_set_text_static(lab_vol_min, LV_SYMBOL_VOLUME_MID);
    lv_obj_set_style_text_font(lab_vol_min, &lv_font_montserrat_20, LV_STATE_DEFAULT);
    lv_obj_align_to(lab_vol_min, volume_slider, LV_ALIGN_OUT_LEFT_MID, -10, 0);

    lv_obj_t *lab_vol_max = lv_label_create(icon_in_obj);
    lv_label_set_text_static(lab_vol_max, LV_SYMBOL_VOLUME_MAX);
    lv_obj_set_style_text_font(lab_vol_max, &lv_font_montserrat_20, LV_STATE_DEFAULT);
    lv_obj_align_to(lab_vol_max, volume_slider, LV_ALIGN_OUT_RIGHT_MID, 10, 0);

    /* 创建音乐列表 */ 
    music_list = lv_dropdown_create(icon_in_obj);
    lv_dropdown_clear_options(music_list);
    lv_dropdown_set_options_static(music_list, "扫描中...");
    lv_obj_set_style_text_font(music_list, &font_alipuhui20, LV_STATE_ANY);
    lv_obj_set_width(music_list, 200);
    lv_obj_align(music_list, LV_ALIGN_TOP_MID, 0, 60);
    lv_obj_add_event_cb(music_list, music_list_cb, LV_EVENT_VALUE_CHANGED, NULL);

    build_file_list(music_list);

    /* 播放时间 + 进度条（仅显示，时长为估算值） */
    music_time_label = lv_label_create(icon_in_obj);
    lv_obj_set_style_text_font(music_time_label, &lv_font_montserrat_14, 0);
    lv_label_set_text(music_time_label, "00:00 / 00:00");
    lv_obj_align(music_time_label, LV_ALIGN_TOP_MID, 0, 95);

    music_progress = lv_bar_create(icon_in_obj);
    lv_obj_set_size(music_progress, 240, 6);
    lv_bar_set_range(music_progress, 0, 100);
    lv_bar_set_value(music_progress, 0, LV_ANIM_OFF);
    lv_obj_align(music_progress, LV_ALIGN_TOP_MID, 0, 116);

    lvgl_port_unlock();
}

// 返回主界面按钮事件处理函数
static void btn_music_back_cb(lv_event_t * e)
{
    if (music_progress_timer) {
        lv_timer_del(music_progress_timer);
        music_progress_timer = NULL;
    }
    audio_player_delete();
    lv_obj_del(icon_in_obj); 
    bsp_sdcard_unmount();   // 卸载本应用挂载的 SD 卡
    music_lib_clear();
    icon_flag = 0;
}

// 进入音乐播放应用
static void music_event_handler(lv_event_t * e)
{
    bsp_sdcard_mount();                 // 挂载 SD 卡以扫描其中的 mp3（无卡则忽略）
    g_sys_volume = g_settings.volume;   // 使用记忆的音量
    bsp_codec_volume_set(g_sys_volume, NULL);
    // 初始化mp3播放器
    mp3_player_init();
    // 创建一个界面对象
    static lv_style_t style;
    lv_style_init(&style);
    lv_style_set_radius(&style, 10);  
    lv_style_set_bg_opa( &style, LV_OPA_COVER );
    lv_style_set_bg_color(&style, lv_color_hex(0xffffff));
    lv_style_set_border_width(&style, 0);
    lv_style_set_pad_all(&style, 0);
    lv_style_set_width(&style, 320);  
    lv_style_set_height(&style, 240); 

    icon_in_obj = lv_obj_create(lv_scr_act());
    lv_obj_add_style(icon_in_obj, &style, 0);

    // 创建标题背景
    lv_obj_t *music_title = lv_obj_create(icon_in_obj);
    lv_obj_set_size(music_title, 320, 40);
    lv_obj_set_style_pad_all(music_title, 0, 0);  // 设置间隙
    lv_obj_align(music_title, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_bg_color(music_title, lv_color_hex(0xf87c30), 0);
    // 显示标题
    music_title_label = lv_label_create(music_title);
    lv_label_set_text(music_title_label, "音乐播放器");
    lv_obj_set_style_text_color(music_title_label, lv_color_hex(0xffffff), 0); 
    lv_obj_set_style_text_font(music_title_label, &font_alipuhui20, 0);
    lv_obj_align(music_title_label, LV_ALIGN_CENTER, 0, 0);
    // 创建后退按钮
    btn_music_back = lv_btn_create(music_title);
    lv_obj_align(btn_music_back, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_size(btn_music_back, 60, 30);
    lv_obj_set_style_border_width(btn_music_back, 0, 0); // 设置边框宽度
    lv_obj_set_style_pad_all(btn_music_back, 0, 0);  // 设置间隙
    lv_obj_set_style_bg_opa(btn_music_back, LV_OPA_TRANSP, LV_PART_MAIN); // 背景透明
    lv_obj_set_style_shadow_opa(btn_music_back, LV_OPA_TRANSP, LV_PART_MAIN); // 阴影透明
    lv_obj_add_event_cb(btn_music_back, btn_music_back_cb, LV_EVENT_CLICKED, NULL); // 添加按键处理函数

    lv_obj_t *label_back = lv_label_create(btn_music_back); 
    lv_label_set_text(label_back, LV_SYMBOL_LEFT);  // 按键上显示左箭头符号
    lv_obj_set_style_text_font(label_back, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(label_back, lv_color_hex(0xffffff), 0); 
    lv_obj_align(label_back, LV_ALIGN_CENTER, -10, 0);

    icon_flag = 2; // 标记已经进入第二个应用

    music_ui(); // 音乐播放器界面

    // 启动进度刷新定时器
    s_music_elapsed = 0;
    s_music_total = 0;
    music_progress_timer = lv_timer_create(music_progress_cb, 1000, NULL);
}


/******************************** 第3个图标 SD卡 应用程序***************************************************************************/
lv_obj_t * sdcard_title; // SD卡页面标题背景
lv_obj_t * sdcard_label; // SD卡页面标题
lv_obj_t * sdcard_file_list; // SD卡文件列表

extern sdmmc_card_t *sdmmc_card;

struct file_path_info
{
    uint8_t path_index;  // 在第几级目录
    char path_now[512]; // 当前文件路径
    char path_back[512]; // 上级文件路径
};
struct file_path_info file_path_info;

// 函数声明
esp_err_t list_sdcard_files(char * path);
static void file_list_btn_cb(lv_event_t * e); 

// 返回主界面按钮事件处理函数
static void btn_sdback_cb(lv_event_t * e)
{
    if (file_path_info.path_index == 0){ // 如果当前是根目录
        bsp_sdcard_unmount(); // 卸载SD卡
        lv_obj_del(icon_in_obj); // 回到主界面
    }else{
        lv_obj_clean(sdcard_file_list); // 清除当前wifi列表
        esp_err_t ret = list_sdcard_files(file_path_info.path_back); // 列出上一级目录文件
        if (ret == ESP_OK){ // 如果成功列出目录
            strcpy(file_path_info.path_now, file_path_info.path_back); // 刚刚进入的这个目录路径 变成当前路径
            file_path_info.path_index--; // 目录级数索引退一级
            // 计算再向下退一级的目录路径
            char *slash = strrchr(file_path_info.path_back, '/'); // 从后往前查找字符'/'
            if (slash!= NULL) { // 如果查找到
                *slash = '\0'; // 替换为NULL 表示字符串结束
            }
            ESP_LOGI(TAG, "path_index: %d", file_path_info.path_index);
            ESP_LOGI(TAG, "path_now: %s", file_path_info.path_now);
            ESP_LOGI(TAG, "path_back: %s", file_path_info.path_back);
        }
    }
}

// 列出SD卡中的文件
esp_err_t list_sdcard_files(char * path) 
{
    esp_err_t ret;
    DIR *dir;
    struct dirent *ent;
    lv_obj_t * btn;
    if ((dir = opendir(path))!= NULL) { // 打开目录
        while ((ent = readdir(dir))!= NULL) { // 读取目录中的文件
            /* 常规文件处理 */
            if (ent->d_type == DT_REG){ // 如果是常规文件
                int file_type_flag = 0;
                const char* extension = strrchr(ent->d_name, '.'); // 从后往前 找到字符'.'
                if (extension != NULL) { // 如果找到了'.'
                    extension++; // 指针地址+1
                    if (strcmp(extension, "mp3") == 0) { // 如果是mp3
                        file_type_flag = 1; // 标记为音乐文件
                    } else if (strcmp(extension, "wav") == 0) { // 如果是wav
                        file_type_flag = 1; // 标记为音乐文件
                    } else if (strcmp(extension, "mp4") == 0) {
                        file_type_flag = 2; // 标记为视频文件
                    } else if (strcmp(extension, "avi") == 0) {
                        file_type_flag = 2; // 标记为视频文件
                    } else if (strcmp(extension, "jpg") == 0) {
                        file_type_flag = 3; // 标记为图片
                    } else if (strcmp(extension, "jpeg") == 0) {
                        file_type_flag = 3; // 标记为图片
                    } else if (strcmp(extension, "png") == 0) {
                        file_type_flag = 3; // 标记为图片
                    } else if (strcmp(extension, "bmp") == 0) {
                        file_type_flag = 3; // 标记为图片
                    } else if (strcmp(extension, "gif") == 0) {
                        file_type_flag = 3; // 标记为图片
                    } else {
                        file_type_flag = 0; // 除了以上文件 其它文件都归类为普通文件
                    }
                }
                lvgl_port_lock(0);
                switch (file_type_flag)
                {
                case 1:
                    btn = lv_list_add_btn(sdcard_file_list, LV_SYMBOL_AUDIO, (const char *)ent->d_name);  // 显示音乐文件图标
                    break;
                case 2:
                    btn = lv_list_add_btn(sdcard_file_list, LV_SYMBOL_VIDEO, (const char *)ent->d_name);  // 显示视频文件图标
                    break;
                case 3:
                    btn = lv_list_add_btn(sdcard_file_list, LV_SYMBOL_IMAGE, (const char *)ent->d_name);  // 显示图片文件图标
                    break;
                default:
                    btn = lv_list_add_btn(sdcard_file_list, LV_SYMBOL_FILE, (const char *)ent->d_name);  // 显示普通文件图标
                    break;
                }
                lv_obj_t *icon = lv_obj_get_child(btn, 0); // 获取图标指针
                lv_obj_set_style_text_font(icon, &lv_font_montserrat_24, 0); // 修改图标的字体    
                lv_obj_add_event_cb(btn, file_list_btn_cb, LV_EVENT_CLICKED, NULL);  // 添加点击回调函数
                lvgl_port_unlock();
            }
            /* 文件夹处理 */
            else if (ent->d_type == DT_DIR) { // 如果是文件夹
                lvgl_port_lock(0);
                btn = lv_list_add_btn(sdcard_file_list, LV_SYMBOL_DIRECTORY, (const char *)ent->d_name); 
                lv_obj_t *icon = lv_obj_get_child(btn, 0); // 获取图标指针
                lv_obj_set_style_text_font(icon, &lv_font_montserrat_24, 0); // 修改图标的字体
                lv_obj_add_event_cb(btn, file_list_btn_cb, LV_EVENT_CLICKED, NULL); // 添加点击回调函数
                lvgl_port_unlock();
            }
        }
        closedir(dir);
        ret = ESP_OK;
    } else {
        ESP_LOGE(TAG, "Failed to open directory %s.", path);
        ret = ESP_FAIL;
    }
    return ret;
}

// 文件点击 事件处理函数
static void file_list_btn_cb(lv_event_t * e)
{
    const char *file_name = NULL; // 当前文件名称
    // 获取点击的按钮名称 即文件名称
    file_name = lv_list_get_btn_text(lv_obj_get_parent(e->target), e->target);
    ESP_LOGI(TAG, "file name: %s", file_name);
    // 列出 SD 卡中的文件
    struct stat st; // 获取文件状态信息结构体
    strcpy(file_path_info.path_back, file_path_info.path_now); // 保存上一级目录
    strcat(file_path_info.path_now, "/");
    strcat(file_path_info.path_now, file_name);
    if (stat(file_path_info.path_now, &st) == 0){ // 如果成功获取到状态信息
        if (S_ISDIR(st.st_mode)){ // 如果是目录
            lv_obj_clean(sdcard_file_list); // 清除当前wifi列表
            esp_err_t ret = list_sdcard_files(file_path_info.path_now);
            if (ret == ESP_OK){ // 如果成功列出了目录
                file_path_info.path_index++; // 表示进入到下一集目录
                ESP_LOGI(TAG, "path_index: %d", file_path_info.path_index);
                ESP_LOGI(TAG, "path_now: %s", file_path_info.path_now);
                ESP_LOGI(TAG, "path_back: %s", file_path_info.path_back);
            }
            return;
        }
    }
    // 如果没有成功进入目录
    strcpy(file_path_info.path_now, file_path_info.path_back); // 没有列出新的列表 还原当前路径
    // 还原再向下退一级的目录路径
    char *slash = strrchr(file_path_info.path_back, '/'); // 从后往前查找字符'/'
    if (slash!= NULL) { // 如果查找到
        *slash = '\0'; // 替换为NULL 表示字符串结束
    }
    ESP_LOGI(TAG, "path_index: %d", file_path_info.path_index);
    ESP_LOGI(TAG, "path_now: %s", file_path_info.path_now);
    ESP_LOGI(TAG, "path_back: %s", file_path_info.path_back);
}

// SD卡处理任务
static void task_process_sdcard(void *arg)
{
    esp_err_t ret;

    ret = bsp_sdcard_mount(); // 挂载SD卡
    if(ret != ESP_OK){ // 如果没有挂载成功
        ESP_LOGE(TAG, "Failed to mount filesystem.");
        lvgl_port_lock(0);
        lv_label_set_text(sdcard_label, "SD卡挂载不成功");
        lvgl_port_unlock();
        vTaskDelay(1000 / portTICK_PERIOD_MS); // 给上面一点显示的时间
        lvgl_port_lock(0);
        lv_obj_del(icon_in_obj);
        lvgl_port_unlock();
    }else{ // 如果挂载成功
        // 终端显示SD卡信息
        sdmmc_card_print_info(stdout, sdmmc_card);
        // 液晶屏标题栏显示SD卡容量
        lvgl_port_lock(0);
        lv_label_set_text_fmt(sdcard_label, "SD: %lluGB",
            (((uint64_t)sdmmc_card->csd.capacity) * sdmmc_card->csd.sector_size) >> 30);
        lvgl_port_unlock();

        // 创建返回按钮
        lvgl_port_lock(0);
        lv_obj_t *btn_back = lv_btn_create(sdcard_title);
        lv_obj_align(btn_back, LV_ALIGN_LEFT_MID, 0, 0);
        lv_obj_set_size(btn_back, 60, 30);
        lv_obj_set_style_border_width(btn_back, 0, 0); // 设置边框宽度
        lv_obj_set_style_pad_all(btn_back, 0, 0);  // 设置间隙
        lv_obj_set_style_bg_opa(btn_back, LV_OPA_TRANSP, LV_PART_MAIN); // 背景透明
        lv_obj_set_style_shadow_opa(btn_back, LV_OPA_TRANSP, LV_PART_MAIN); // 阴影透明
        lv_obj_add_event_cb(btn_back, btn_sdback_cb, LV_EVENT_CLICKED, NULL); // 添加按键处理函数

        lv_obj_t *label_back = lv_label_create(btn_back); 
        lv_label_set_text(label_back, LV_SYMBOL_LEFT);  // 按键上显示左箭头符号
        lv_obj_set_style_text_font(label_back, &lv_font_montserrat_20, 0);
        lv_obj_set_style_text_color(label_back, lv_color_hex(0xffffff), 0); 
        lv_obj_align(label_back, LV_ALIGN_CENTER, -10, 0);

        // 创建文件列表
        sdcard_file_list = lv_list_create(icon_in_obj);
        lv_obj_set_size(sdcard_file_list, 320, 200);
        lv_obj_align(sdcard_file_list, LV_ALIGN_TOP_LEFT, 0, 40);
        lv_obj_set_style_border_width(sdcard_file_list, 0, 0);
        lv_obj_set_style_text_font(sdcard_file_list, &font_alipuhui20, 0);
        lv_obj_set_scrollbar_mode(sdcard_file_list, LV_SCROLLBAR_MODE_OFF); // 隐藏wifi_list滚动条
        lvgl_port_unlock();
        // 列出 SD 卡中的文件
        file_path_info.path_index = 0; // 表示当前在根目录
        strcpy(file_path_info.path_now, SD_MOUNT_POINT); // 装入当前路径
        list_sdcard_files(file_path_info.path_now); // 列出当前目录文件
    }
    
    vTaskDelete(NULL);
}


// 进入SD卡应用程序
static void sdcard_event_handler(lv_event_t * e)
{
    // 创建一个界面对象
    static lv_style_t style;
    lv_style_init(&style);
    lv_style_set_radius(&style, 10);  
    lv_style_set_bg_opa( &style, LV_OPA_COVER );
    lv_style_set_bg_color(&style, lv_color_hex(0xffffff));
    lv_style_set_border_width(&style, 0);
    lv_style_set_pad_all(&style, 0);
    lv_style_set_width(&style, 320);  
    lv_style_set_height(&style, 240); 

    icon_in_obj = lv_obj_create(lv_scr_act());
    lv_obj_add_style(icon_in_obj, &style, 0);

    // 创建标题背景
    sdcard_title = lv_obj_create(icon_in_obj);
    lv_obj_set_size(sdcard_title, 320, 40);
    lv_obj_set_style_pad_all(sdcard_title, 0, 0);  // 设置间隙
    lv_obj_align(sdcard_title, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_bg_color(sdcard_title, lv_color_hex(0x008b8b), 0);
    // 显示标题
    sdcard_label = lv_label_create(sdcard_title);
    lv_label_set_text(sdcard_label, "TF卡扫描中...");
    lv_obj_set_style_text_color(sdcard_label, lv_color_hex(0xffffff), 0); 
    lv_obj_set_style_text_font(sdcard_label, &font_alipuhui20, 0);
    lv_obj_align(sdcard_label, LV_ALIGN_CENTER, 0, 0);

    icon_flag = 3; // 标记已经进入第三个应用

    xTaskCreatePinnedToCore(task_process_sdcard, "task_process_sdcard", 3 * 1024, NULL, 5, NULL, 1);
}



/******************************** 第4个图标 摄像头 应用程序 *****************************************************************************/
lv_obj_t * img_camera;

// 摄像头图像
lv_img_dsc_t img_camera_dsc = {
  .header.cf = LV_IMG_CF_TRUE_COLOR,
  .header.always_zero = 0,
  .header.reserved = 0,
  .header.w = 320,
  .header.h = 240,
  .data_size = 240*320*2,
};

// 摄像头处理任务
static volatile bool s_capture_request = false; // 拍照请求标志（由按钮置位，任务里处理）
static int  s_photo_count = 0;                  // 已拍照片数
static bool s_cam_sd_ok   = false;              // 摄像头应用是否成功挂载了 SD 卡
static lv_obj_t *cam_status_label = NULL;       // 拍照状态提示

// 把一帧 RGB565 图像保存为 24 位 BMP 文件。
// 摄像头 PIXFORMAT_RGB565 输出为大端字节序（高字节在前），故按 (p0<<8)|p1 解析。
// 若烧录后照片颜色异常，把下面 px 的两个字节对调即可。
static esp_err_t save_bmp(const uint8_t *rgb565, int w, int h, const char *path)
{
    FILE *fp = fopen(path, "wb");
    if (!fp) return ESP_FAIL;

    int row_size = w * 3;                 // 24bpp 每行字节数
    int pad = (4 - (row_size % 4)) % 4;   // 每行补齐到 4 字节边界
    uint32_t img_size = (uint32_t)(row_size + pad) * h;
    uint32_t file_size = 54 + img_size;

    uint8_t hdr[54];
    memset(hdr, 0, sizeof(hdr));
    hdr[0] = 'B'; hdr[1] = 'M';                          // 文件头 14 字节
    hdr[2] = file_size & 0xFF; hdr[3] = (file_size >> 8) & 0xFF;
    hdr[4] = (file_size >> 16) & 0xFF; hdr[5] = (file_size >> 24) & 0xFF;
    hdr[10] = 54;                                        // 像素数据偏移
    hdr[14] = 40;                                        // DIB 信息头 40 字节
    hdr[18] = w & 0xFF; hdr[19] = (w >> 8) & 0xFF;       // 宽
    hdr[22] = h & 0xFF; hdr[23] = (h >> 8) & 0xFF;       // 高
    hdr[26] = 1;                                         // 色彩平面数
    hdr[28] = 24;                                        // 位深
    hdr[34] = img_size & 0xFF; hdr[35] = (img_size >> 8) & 0xFF;
    hdr[36] = (img_size >> 16) & 0xFF; hdr[37] = (img_size >> 24) & 0xFF;
    fwrite(hdr, 1, sizeof(hdr), fp);

    uint8_t *line = malloc(row_size + pad);
    if (!line) { fclose(fp); return ESP_ERR_NO_MEM; }
    for (int y = h - 1; y >= 0; y--) {                   // BMP 行序自下而上
        for (int x = 0; x < w; x++) {
            const uint8_t *p = rgb565 + (size_t)(y * w + x) * 2;
            uint16_t px = (p[0] << 8) | p[1];            // 大端 RGB565
            uint8_t r = (px >> 11) & 0x1F;
            uint8_t g = (px >> 5) & 0x3F;
            uint8_t b = px & 0x1F;
            r = (r << 3) | (r >> 2);                     // 5/6/5 位扩展到 8 位
            g = (g << 2) | (g >> 2);
            b = (b << 3) | (b >> 2);
            line[x * 3 + 0] = b;                         // BMP 为 BGR 顺序
            line[x * 3 + 1] = g;
            line[x * 3 + 2] = r;
        }
        memset(line + row_size, 0, pad);
        fwrite(line, 1, row_size + pad, fp);
    }
    free(line);
    fclose(fp);
    ESP_LOGI(TAG, "photo saved: %s (%dx%d)", path, w, h);
    return ESP_OK;
}

// 摄像头处理任务
static void task_process_camera(void *arg)
{
    while (icon_flag == 4)
    {
        camera_fb_t *frame = esp_camera_fb_get();
        if (frame == NULL) continue;
        img_camera_dsc.data = frame->buf;
        lv_img_set_src(img_camera, &img_camera_dsc);

        if (s_capture_request) {                 // 处理拍照请求（复用当前帧）
            s_capture_request = false;
            if (s_cam_sd_ok) {
                char path[64];
                s_photo_count++;
                snprintf(path, sizeof(path), "/sdcard/DCIM/IMG_%03d.bmp", s_photo_count);
                lvgl_port_lock(0);
                if (save_bmp(frame->buf, frame->width, frame->height, path) == ESP_OK) {
                    lv_label_set_text_fmt(cam_status_label, "已保存 %d 张", s_photo_count);
                } else {
                    lv_label_set_text(cam_status_label, "保存失败");
                    s_photo_count--;
                }
                lvgl_port_unlock();
            } else {
                lvgl_port_lock(0);
                lv_label_set_text(cam_status_label, "无SD卡，无法拍照");
                lvgl_port_unlock();
            }
        }

        esp_camera_fb_return(frame);
    }
    esp_camera_deinit(); // 取消初始化摄像头
    if (s_cam_sd_ok) {   // 卸载本应用挂载的 SD 卡
        bsp_sdcard_unmount();
        s_cam_sd_ok = false;
    }
    lvgl_port_lock(0);
    lv_obj_del(icon_in_obj); // 删除摄像头画布
    lvgl_port_unlock();
    dvp_pwdn(1); // 摄像头进入掉电模式
    vTaskDelete(NULL);
}

// 返回主界面按钮事件处理函数
static void btn_camback_cb(lv_event_t * e)
{
    icon_flag = 0;
}

// 拍照按钮：置位请求标志，由摄像头任务保存当前帧
static void btn_capture_cb(lv_event_t * e)
{
    s_capture_request = true;
}

// 进入摄像头应用
static void camera_event_handler(lv_event_t * e)
{
    bsp_camera_init(); // 摄像头初始化
    // 创建一个界面对象
    static lv_style_t style;
    lv_style_init(&style);
    lv_style_set_radius(&style, 10);  
    lv_style_set_bg_opa( &style, LV_OPA_COVER );
    lv_style_set_bg_color(&style, lv_color_hex(0xcccccc));
    lv_style_set_border_width(&style, 0);
    lv_style_set_pad_all(&style, 0);
    lv_style_set_width(&style, 320);  
    lv_style_set_height(&style, 240); 

    icon_in_obj = lv_obj_create(lv_scr_act());
    lv_obj_add_style(icon_in_obj, &style, 0);

    img_camera = lv_img_create(icon_in_obj);
    lv_obj_set_pos(img_camera, 0, 0);
    lv_obj_set_size(img_camera, 320, 240);

    // 创建返回按钮
    lv_obj_t *btn_back = lv_btn_create(icon_in_obj);
    lv_obj_align(btn_back, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_size(btn_back, 60, 30);
    lv_obj_set_style_border_width(btn_back, 0, 0); // 设置边框宽度
    lv_obj_set_style_pad_all(btn_back, 0, 0);  // 设置间隙
    lv_obj_set_style_bg_opa(btn_back, LV_OPA_TRANSP, LV_PART_MAIN); // 背景透明
    lv_obj_set_style_shadow_opa(btn_back, LV_OPA_TRANSP, LV_PART_MAIN); // 阴影透明
    lv_obj_add_event_cb(btn_back, btn_camback_cb, LV_EVENT_CLICKED, NULL); // 添加按键处理函数

    lv_obj_t *label_back = lv_label_create(btn_back); 
    lv_label_set_text(label_back, LV_SYMBOL_LEFT);  // 按键上显示左箭头符号
    lv_obj_set_style_text_font(label_back, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(label_back, lv_color_hex(0xffffff), 0); 
    lv_obj_align(label_back, LV_ALIGN_CENTER, -10, 0);

    // 挂载 SD 卡并准备照片目录（拍照存到 /sdcard/DCIM）
    s_cam_sd_ok = (bsp_sdcard_mount() == ESP_OK);
    if (s_cam_sd_ok) {
        mkdir("/sdcard/DCIM", 0755);
    }

    // 创建拍照按钮
    lv_obj_t *btn_capture = lv_btn_create(icon_in_obj);
    lv_obj_align(btn_capture, LV_ALIGN_BOTTOM_RIGHT, -10, -10);
    lv_obj_set_size(btn_capture, 80, 36);
    lv_obj_set_style_bg_color(btn_capture, lv_color_hex(0x2196f3), LV_PART_MAIN);
    lv_obj_add_event_cb(btn_capture, btn_capture_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *label_capture = lv_label_create(btn_capture);
    lv_label_set_text(label_capture, LV_SYMBOL_IMAGE);
    lv_obj_set_style_text_font(label_capture, &lv_font_montserrat_20, 0);
    lv_obj_center(label_capture);

    // 拍照状态提示
    cam_status_label = lv_label_create(icon_in_obj);
    lv_obj_set_style_text_font(cam_status_label, &font_alipuhui20, 0);
    lv_obj_set_style_text_color(cam_status_label, lv_color_hex(0xffff00), 0);
    lv_label_set_text(cam_status_label, s_cam_sd_ok ? "点击拍照" : "未插入SD卡");
    lv_obj_align(cam_status_label, LV_ALIGN_BOTTOM_LEFT, 10, -14);

    s_photo_count = 0;
    icon_flag = 4; // 标记已经进入第四个应用

    xTaskCreatePinnedToCore(task_process_camera, "task_process_camera", 4 * 1024, NULL, 5, NULL, 1);
}


/******************************** 第5个图标 WiFi设置 应用程序*****************************************************************************/

lv_obj_t *wifi_scan_page;     // wifi扫描页面 obj
lv_obj_t *wifi_password_page; // wifi密码页面 obj
lv_obj_t *wifi_connect_page;  // wifi连接页面 obj
lv_obj_t *obj_scan_title;     // wifi扫描页面标题
lv_obj_t *label_wifi_scan;    // wif扫描页面 label
lv_obj_t *wifi_list;          // wifi列表  list
lv_obj_t *label_wifi_connect; // wifi连接页面label 
lv_obj_t *ta_pass_text;       // 密码输入文本框 textarea
lv_obj_t *roller_num;         // 数字roller
lv_obj_t *roller_letter_low;  // 小写字母roller
lv_obj_t *roller_letter_up;   // 大写字母roller
lv_obj_t *label_wifi_name;    // wifi名称label


#define DEFAULT_SCAN_LIST_SIZE   5                // 最大扫描wifi个数

// wifi事件组
static EventGroupHandle_t s_wifi_event_group = NULL;
// wifi事件
#define WIFI_CONNECTED_BIT    BIT0
#define WIFI_FAIL_BIT         BIT1
#define WIFI_START_BIT        BIT2
#define WIFI_GET_SNTP_BIT     BIT3
// wifi最大重连次数
#define EXAMPLE_ESP_MAXIMUM_RETRY  3

// wifi账号队列
static QueueHandle_t xQueueWifiAccount = NULL;
// 队列要传输的内容
typedef struct {
    char wifi_ssid[32];  // 获取wifi名称
    char wifi_password[64]; // 获取wifi密码  
    char back_flag; // 是否退出       
} wifi_account_t;

// 密码roller的遮罩显示效果
static void mask_event_cb(lv_event_t * e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t * obj = lv_event_get_target(e);

    static int16_t mask_top_id = -1;
    static int16_t mask_bottom_id = -1;

    if(code == LV_EVENT_COVER_CHECK) {
        lv_event_set_cover_res(e, LV_COVER_RES_MASKED);
    }
    else if(code == LV_EVENT_DRAW_MAIN_BEGIN) {
        /* add mask */
        const lv_font_t * font = lv_obj_get_style_text_font(obj, LV_PART_MAIN);
        lv_coord_t line_space = lv_obj_get_style_text_line_space(obj, LV_PART_MAIN);
        lv_coord_t font_h = lv_font_get_line_height(font);

        lv_area_t roller_coords;
        lv_obj_get_coords(obj, &roller_coords);

        lv_area_t rect_area;
        rect_area.x1 = roller_coords.x1;
        rect_area.x2 = roller_coords.x2;
        rect_area.y1 = roller_coords.y1;
        rect_area.y2 = roller_coords.y1 + (lv_obj_get_height(obj) - font_h - line_space) / 2;

        lv_draw_mask_fade_param_t * fade_mask_top = lv_mem_buf_get(sizeof(lv_draw_mask_fade_param_t));
        lv_draw_mask_fade_init(fade_mask_top, &rect_area, LV_OPA_TRANSP, rect_area.y1, LV_OPA_COVER, rect_area.y2);
        mask_top_id = lv_draw_mask_add(fade_mask_top, NULL);

        rect_area.y1 = rect_area.y2 + font_h + line_space - 1;
        rect_area.y2 = roller_coords.y2;

        lv_draw_mask_fade_param_t * fade_mask_bottom = lv_mem_buf_get(sizeof(lv_draw_mask_fade_param_t));
        lv_draw_mask_fade_init(fade_mask_bottom, &rect_area, LV_OPA_COVER, rect_area.y1, LV_OPA_TRANSP, rect_area.y2);
        mask_bottom_id = lv_draw_mask_add(fade_mask_bottom, NULL);

    }
    else if(code == LV_EVENT_DRAW_POST_END) {
        lv_draw_mask_fade_param_t * fade_mask_top = lv_draw_mask_remove_id(mask_top_id);
        lv_draw_mask_fade_param_t * fade_mask_bottom = lv_draw_mask_remove_id(mask_bottom_id);
        lv_draw_mask_free_param(fade_mask_top);
        lv_draw_mask_free_param(fade_mask_bottom);
        lv_mem_buf_release(fade_mask_top);
        lv_mem_buf_release(fade_mask_bottom);
        mask_top_id = -1;
        mask_bottom_id = -1;
    }
}

// 数字键 处理函数
static void btn_num_cb(lv_event_t * e)
{
    lv_event_code_t code = lv_event_get_code(e);

    if(code == LV_EVENT_CLICKED) {
        ESP_LOGI(TAG, "Btn-Num Clicked");
        char buf[2]; // 接收roller的值
        lv_roller_get_selected_str(roller_num, buf, sizeof(buf));
        lv_textarea_add_text(ta_pass_text, buf);
    }
}

// 小写字母确认键 处理函数
static void btn_letter_low_cb(lv_event_t * e)
{
    lv_event_code_t code = lv_event_get_code(e);

    if(code == LV_EVENT_CLICKED) {
        ESP_LOGI(TAG, "Btn-Letter-Low Clicked");
        char buf[2]; // 接收roller的值
        lv_roller_get_selected_str(roller_letter_low, buf, sizeof(buf));
        lv_textarea_add_text(ta_pass_text, buf);
    }
}

// 大写字母确认键 处理函数
static void btn_letter_up_cb(lv_event_t * e)
{
    lv_event_code_t code = lv_event_get_code(e);

    if(code == LV_EVENT_CLICKED) {
        ESP_LOGI(TAG, "Btn-Letter-Up Clicked");
        char buf[2]; // 接收roller的值
        lv_roller_get_selected_str(roller_letter_up, buf, sizeof(buf));
        lv_textarea_add_text(ta_pass_text, buf);
    }
}

static void lv_wifi_connect(void)
{
    lv_obj_del(wifi_password_page); // 删除密码输入界面

    // 创建一个面板对象
    static lv_style_t style;
    lv_style_init(&style);
    lv_style_set_bg_opa( &style, LV_OPA_COVER );
    lv_style_set_border_width(&style, 0);
    lv_style_set_pad_all(&style, 0);
    lv_style_set_radius(&style, 0);  
    lv_style_set_width(&style, 320);  
    lv_style_set_height(&style, 240); 

    wifi_connect_page = lv_obj_create(lv_scr_act());
    lv_obj_add_style(wifi_connect_page, &style, 0);

    // 绘制label提示
    label_wifi_connect = lv_label_create(wifi_connect_page);
    lv_label_set_text(label_wifi_connect, "WLAN连接中...");
    lv_obj_set_style_text_font(label_wifi_connect, &font_alipuhui20, 0);
    lv_obj_align(label_wifi_connect, LV_ALIGN_CENTER, 0, -50);
}

// WiFi连接按钮 处理函数
static void btn_connect_cb(lv_event_t * e)
{
    lv_event_code_t code = lv_event_get_code(e);

    if(code == LV_EVENT_CLICKED) {
        ESP_LOGI(TAG, "OK Clicked");
        const char *wifi_ssid = lv_label_get_text(label_wifi_name);
        const char *wifi_password = lv_textarea_get_text(ta_pass_text);
        if(*wifi_password != '\0') // 判断是否为空字符串
        {
            wifi_account_t wifi_account;
            strcpy(wifi_account.wifi_ssid, wifi_ssid);
            strcpy(wifi_account.wifi_password, wifi_password);
            ESP_LOGI(TAG, "connected to ap SSID:%s password:%s",
                    wifi_account.wifi_ssid, wifi_account.wifi_password);
            wifi_account.back_flag = 0; // 正常连接
            lv_wifi_connect(); // 显示wifi连接界面
            // 发送WiFi账号密码信息到队列
            xQueueSend(xQueueWifiAccount, &wifi_account, portMAX_DELAY); 
        }
    }
}

// 删除密码按钮
static void btn_del_cb(lv_event_t * e)
{
    lv_event_code_t code = lv_event_get_code(e);

    if(code == LV_EVENT_CLICKED) {
        ESP_LOGI(TAG, "Clicked");
        lv_textarea_del_char(ta_pass_text);
    }
}

// 返回按钮
static void btn_back_cb(lv_event_t * e)
{
    lv_event_code_t code = lv_event_get_code(e);

    if(code == LV_EVENT_CLICKED) {
        ESP_LOGI(TAG, "btn_back Clicked");
        lv_obj_del(wifi_password_page); // 删除密码输入界面
    }
}

// 进入输入密码界面
static void list_btn_cb(lv_event_t * e)
{
    // 获取点击到的WiFi名称
    const char *wifi_name=NULL;
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t * obj = lv_event_get_target(e);
    if(code == LV_EVENT_CLICKED) {
        wifi_name = lv_list_get_btn_text(wifi_list, obj);
        ESP_LOGI(TAG, "WLAN Name: %s", wifi_name);
    }

    // 创建密码输入页面
    wifi_password_page = lv_obj_create(lv_scr_act());
    lv_obj_set_size(wifi_password_page, 320, 240);
    lv_obj_set_style_border_width(wifi_password_page, 0, 0); // 设置边框宽度
    lv_obj_set_style_pad_all(wifi_password_page, 0, 0);  // 设置间隙
    lv_obj_set_style_radius(wifi_password_page, 0, 0); // 设置圆角

    // 创建返回按钮
    lv_obj_t *btn_back = lv_btn_create(wifi_password_page);
    lv_obj_align(btn_back, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_size(btn_back, 60, 40);
    lv_obj_set_style_border_width(btn_back, 0, 0); // 设置边框宽度
    lv_obj_set_style_pad_all(btn_back, 0, 0);  // 设置间隙
    lv_obj_set_style_bg_opa(btn_back, LV_OPA_TRANSP, LV_PART_MAIN); // 背景透明
    lv_obj_set_style_shadow_opa(btn_back, LV_OPA_TRANSP, LV_PART_MAIN); // 阴影透明
    lv_obj_add_event_cb(btn_back, btn_back_cb, LV_EVENT_ALL, NULL); // 添加按键处理函数

    lv_obj_t *label_back = lv_label_create(btn_back); 
    lv_label_set_text(label_back, LV_SYMBOL_LEFT);  // 按键上显示左箭头符号
    lv_obj_set_style_text_font(label_back, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(label_back, lv_color_hex(0x000000), 0); 
    lv_obj_align(label_back, LV_ALIGN_TOP_LEFT, 10, 10);

    // 显示选中的wifi名称
    label_wifi_name = lv_label_create(wifi_password_page);
    lv_obj_set_style_text_font(label_wifi_name, &lv_font_montserrat_20, 0);
    lv_label_set_text(label_wifi_name, wifi_name);
    lv_obj_align(label_wifi_name, LV_ALIGN_TOP_MID, 0, 10);

    // 创建密码输入框
    ta_pass_text = lv_textarea_create(wifi_password_page);
    lv_obj_set_style_text_font(ta_pass_text, &lv_font_montserrat_20, 0);
    lv_textarea_set_one_line(ta_pass_text, true);  // 一行显示
    lv_textarea_set_password_mode(ta_pass_text, false); // 是否使用密码输入显示模式
    lv_textarea_set_placeholder_text(ta_pass_text, "password"); // 设置提醒词
    lv_obj_set_width(ta_pass_text, 150); // 宽度
    lv_obj_align(ta_pass_text, LV_ALIGN_TOP_LEFT, 10, 40); // 位置
    lv_obj_add_state(ta_pass_text, LV_STATE_FOCUSED); // 显示光标

    // 创建“连接按钮”
    lv_obj_t *btn_connect = lv_btn_create(wifi_password_page);
    lv_obj_align(btn_connect, LV_ALIGN_TOP_LEFT, 170, 40);
    lv_obj_set_width(btn_connect, 65); // 宽度
    lv_obj_add_event_cb(btn_connect, btn_connect_cb, LV_EVENT_ALL, NULL); // 事件处理函数

    lv_obj_t *label_ok = lv_label_create(btn_connect);
    lv_label_set_text(label_ok, "OK");
    lv_obj_set_style_text_font(label_ok, &lv_font_montserrat_20, 0);
    lv_obj_center(label_ok);

    // 创建“删除按钮”
    lv_obj_t *btn_del = lv_btn_create(wifi_password_page);
    lv_obj_align(btn_del, LV_ALIGN_TOP_LEFT, 245, 40);
    lv_obj_set_width(btn_del, 65); // 宽度
    lv_obj_add_event_cb(btn_del, btn_del_cb, LV_EVENT_ALL, NULL);  // 事件处理函数

    lv_obj_t *label_del = lv_label_create(btn_del);
    lv_label_set_text(label_del, LV_SYMBOL_BACKSPACE);
    lv_obj_set_style_text_font(label_del, &lv_font_montserrat_20, 0);
    lv_obj_center(label_del);

    // 创建roller样式
    static lv_style_t style;
    lv_style_init(&style);
    lv_style_set_bg_color(&style, lv_color_black());
    lv_style_set_text_color(&style, lv_color_white());
    lv_style_set_border_width(&style, 0);
    lv_style_set_pad_all(&style, 0);
    lv_style_set_radius(&style, 0);

    // 创建"数字"roller
    const char * opts_num = "0\n1\n2\n3\n4\n5\n6\n7\n8\n9";

    roller_num = lv_roller_create(wifi_password_page);
    lv_obj_add_style(roller_num, &style, 0);
    lv_obj_set_style_bg_opa(roller_num, LV_OPA_50, LV_PART_SELECTED);

    lv_roller_set_options(roller_num, opts_num, LV_ROLLER_MODE_INFINITE);
    lv_roller_set_visible_row_count(roller_num, 3); // 显示3行
    lv_roller_set_selected(roller_num, 5, LV_ANIM_OFF); // 默认选择
    lv_obj_set_width(roller_num, 90);
    lv_obj_set_style_text_font(roller_num, &lv_font_montserrat_20, 0);
    lv_obj_align(roller_num, LV_ALIGN_BOTTOM_LEFT, 15, -53);
    lv_obj_add_event_cb(roller_num, mask_event_cb, LV_EVENT_ALL, NULL); // 事件处理函数

    // 创建"数字"roller 的确认键
    lv_obj_t *btn_num_ok = lv_btn_create(wifi_password_page);
    lv_obj_align(btn_num_ok, LV_ALIGN_BOTTOM_LEFT, 15, -10); // 位置
    lv_obj_set_width(btn_num_ok, 90); // 宽度
    lv_obj_add_event_cb(btn_num_ok, btn_num_cb, LV_EVENT_ALL, NULL); // 事件处理函数

    lv_obj_t *label_num_ok = lv_label_create(btn_num_ok);
    lv_label_set_text(label_num_ok, LV_SYMBOL_OK);
    lv_obj_set_style_text_font(label_num_ok, &lv_font_montserrat_20, 0);
    lv_obj_center(label_num_ok);

    // 创建"小写字母"roller
    const char * opts_letter_low = "a\nb\nc\nd\ne\nf\ng\nh\ni\nj\nk\nl\nm\nn\no\np\nq\nr\ns\nt\nu\nv\nw\nx\ny\nz";

    roller_letter_low = lv_roller_create(wifi_password_page);
    lv_obj_add_style(roller_letter_low, &style, 0);
    lv_obj_set_style_bg_opa(roller_letter_low, LV_OPA_50, LV_PART_SELECTED); // 设置选中项的透明度
    lv_roller_set_options(roller_letter_low, opts_letter_low, LV_ROLLER_MODE_INFINITE); // 循环滚动模式
    lv_roller_set_visible_row_count(roller_letter_low, 3);
    lv_roller_set_selected(roller_letter_low, 15, LV_ANIM_OFF); // 
    lv_obj_set_width(roller_letter_low, 90);
    lv_obj_set_style_text_font(roller_letter_low, &lv_font_montserrat_20, 0);
    lv_obj_align(roller_letter_low, LV_ALIGN_BOTTOM_LEFT, 115, -53);
    lv_obj_add_event_cb(roller_letter_low, mask_event_cb, LV_EVENT_ALL, NULL); // 事件处理函数

    // 创建"小写字母"roller的确认键
    lv_obj_t *btn_letter_low_ok = lv_btn_create(wifi_password_page);
    lv_obj_align(btn_letter_low_ok, LV_ALIGN_BOTTOM_LEFT, 115, -10);
    lv_obj_set_width(btn_letter_low_ok, 90); // 宽度
    lv_obj_add_event_cb(btn_letter_low_ok, btn_letter_low_cb, LV_EVENT_ALL, NULL); // 事件处理函数

    lv_obj_t *label_letter_low_ok = lv_label_create(btn_letter_low_ok);
    lv_label_set_text(label_letter_low_ok, LV_SYMBOL_OK);
    lv_obj_set_style_text_font(label_letter_low_ok, &lv_font_montserrat_20, 0);
    lv_obj_center(label_letter_low_ok);

    // 创建"大写字母"roller
    const char * opts_letter_up = "A\nB\nC\nD\nE\nF\nG\nH\nI\nJ\nK\nL\nM\nN\nO\nP\nQ\nR\nS\nT\nU\nV\nW\nX\nY\nZ";

    roller_letter_up = lv_roller_create(wifi_password_page);
    lv_obj_add_style(roller_letter_up, &style, 0);
    lv_obj_set_style_bg_opa(roller_letter_up, LV_OPA_50, LV_PART_SELECTED); // 设置选中项的透明度
    lv_roller_set_options(roller_letter_up, opts_letter_up, LV_ROLLER_MODE_INFINITE); // 循环滚动模式
    lv_roller_set_visible_row_count(roller_letter_up, 3);
    lv_roller_set_selected(roller_letter_up, 15, LV_ANIM_OFF);
    lv_obj_set_width(roller_letter_up, 90);
    lv_obj_set_style_text_font(roller_letter_up, &lv_font_montserrat_20, 0);
    lv_obj_align(roller_letter_up, LV_ALIGN_BOTTOM_LEFT, 215, -53);
    lv_obj_add_event_cb(roller_letter_up, mask_event_cb, LV_EVENT_ALL, NULL); // 事件处理函数

    // 创建"大写字母"roller的确认键
    lv_obj_t *btn_letter_up_ok = lv_btn_create(wifi_password_page);
    lv_obj_align(btn_letter_up_ok, LV_ALIGN_BOTTOM_LEFT, 215, -10);
    lv_obj_set_width(btn_letter_up_ok, 90); 
    lv_obj_add_event_cb(btn_letter_up_ok, btn_letter_up_cb, LV_EVENT_ALL, NULL); // 事件处理函数

    lv_obj_t *label_letter_up_ok = lv_label_create(btn_letter_up_ok);
    lv_label_set_text(label_letter_up_ok, LV_SYMBOL_OK);
    lv_obj_set_style_text_font(label_letter_up_ok, &lv_font_montserrat_20, 0);
    lv_obj_center(label_letter_up_ok);

}

lv_obj_t * date_label;
lv_obj_t * time_label;

time_t now;
struct tm timeinfo;

// 更新时间函数（每秒刷新状态栏时钟）
void value_update_cb(lv_timer_t * timer)
{
    statusbar_refresh_time();
}

// 获得日期时间 任务函数
static void get_time_task(void *pvParameters)
{
    xEventGroupWaitBits(s_wifi_event_group, WIFI_CONNECTED_BIT, pdFALSE, pdFALSE, portMAX_DELAY);

    esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG("cn.pool.ntp.org");
    esp_netif_sntp_init(&config);
    // wait for time to be set
    int retry = 0;
    // const int retry_count = 6;
    while (esp_netif_sntp_sync_wait(2000 / portTICK_PERIOD_MS) == ESP_ERR_TIMEOUT) {
        ESP_LOGI(TAG, "Waiting for system time to be set... (%d)", retry++);
    }

    esp_netif_sntp_deinit();
    // 设置时区
    setenv("TZ", "CST-8", 1); 
    tzset();
    // 获取系统时间
    time(&now);
    localtime_r(&now, &timeinfo);

    // NTP 同步成功：点亮状态栏 WiFi 图标并立即刷新一次时钟
    statusbar_set_wifi(true);
    statusbar_refresh_time();

    xEventGroupSetBits(s_wifi_event_group, WIFI_GET_SNTP_BIT);

    lv_timer_create(value_update_cb, 1000, NULL);  // 创建一个lv_timer 每秒更新一次时间
    
    vTaskDelete(NULL);
}

// 网络连接 事件处理函数
static void event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data)
{
    static int s_retry_num = 0;

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        xEventGroupSetBits(s_wifi_event_group, WIFI_START_BIT);
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry_num < EXAMPLE_ESP_MAXIMUM_RETRY) {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGI(TAG, "retry to connect to the AP");
        } else {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
        ESP_LOGI(TAG,"connect to the AP fail");
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
        statusbar_set_wifi(true);   // 点亮状态栏 WiFi 图标
    }
}

esp_event_handler_instance_t instance_any_id;
esp_event_handler_instance_t instance_got_ip;
esp_netif_t *sta_netif = NULL;

// WiFi 协议栈是否已初始化（netif/事件循环/wifi 驱动），供扫描和开机自动重连复用
static bool s_wifi_stack_inited = false;

// 初始化 WiFi 协议栈（STA 模式），只执行一次
static void wifi_stack_init(void)
{
    if (s_wifi_stack_inited) return;

    s_wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    sta_netif = esp_netif_create_default_wifi_sta();
    assert(sta_netif);

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &event_handler,
                                                        NULL,
                                                        &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &event_handler,
                                                        NULL,
                                                        &instance_got_ip));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());

    s_wifi_stack_inited = true;
}

// 扫描附近wifi
static void wifi_scan(wifi_ap_record_t ap_info[], uint16_t *ap_number)
{
    wifi_stack_init();   // 确保协议栈已就绪（可能开机自动重连时已初始化）

    uint16_t ap_count = 0;
    memset(ap_info, 0, *ap_number * sizeof(wifi_ap_record_t));

    esp_wifi_scan_start(NULL, true);

    ESP_LOGI(TAG, "Max AP number ap_info can hold = %u", *ap_number);
    ESP_ERROR_CHECK(esp_wifi_scan_get_ap_num(&ap_count));  // 获取扫描到的wifi数量
    ESP_ERROR_CHECK(esp_wifi_scan_get_ap_records(ap_number, ap_info)); // 获取真实的获取到wifi数量和信息
    ESP_LOGI(TAG, "Total APs scanned = %u, actual AP number ap_info holds = %u", ap_count, *ap_number);
}

// 清除wifi初始化内容
// 可重复调用：协议栈未初始化或已拆除时直接返回，避免二次注销/删除事件循环触发 abort
static void wifiset_deinit(void)
{
    if (!s_wifi_stack_inited) {
        return;
    }
    // 注销事件处理器：即便未注册(ESP_ERR_NOT_FOUND)也不应 abort，故不使用 ESP_ERROR_CHECK
    if (instance_any_id) {
        esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, &instance_any_id);
        instance_any_id = NULL;
    }
    if (instance_got_ip) {
        esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, &instance_got_ip);
        instance_got_ip = NULL;
    }
    esp_err_t err = esp_wifi_stop();
    if (err != ESP_ERR_WIFI_NOT_INIT) {
        ESP_ERROR_CHECK(err);
        ESP_ERROR_CHECK(esp_wifi_deinit());
        if (sta_netif) {
            ESP_ERROR_CHECK(esp_wifi_clear_default_wifi_driver_and_handlers(sta_netif));
            esp_netif_destroy(sta_netif);
            sta_netif = NULL;
        }
    }
    ESP_ERROR_CHECK(esp_event_loop_delete_default());
    s_wifi_stack_inited = false;   // 协议栈已完全拆除，允许下次重新初始化
    s_wifi_event_group = NULL;
}

// WIFI连接任务
static void wifi_connect(void *arg)
{
    wifi_account_t wifi_account;

    while (true)
    {
        // 如果收到wifi账号队列消息
        if(xQueueReceive(xQueueWifiAccount, &wifi_account, portMAX_DELAY))
        {
            if (wifi_account.back_flag == 1){  // 退出任务标志
                break; // 跳出while循环
            }
            
            wifi_config_t wifi_config = {
                .sta = {
                    .threshold.authmode = WIFI_AUTH_WPA2_PSK,
                    .sae_pwe_h2e = WPA3_SAE_PWE_BOTH,
                    .sae_h2e_identifier = "",
                    },
            };
            strcpy((char *)wifi_config.sta.ssid, wifi_account.wifi_ssid);
            strcpy((char *)wifi_config.sta.password, wifi_account.wifi_password);
            ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config) );
            esp_wifi_connect();
            /* Waiting until either the connection is established (WIFI_CONNECTED_BIT) or connection failed for the maximum
            * number of re-tries (WIFI_FAIL_BIT). The bits are set by event_handler() (see above) */
            EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
            WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
            pdFALSE,
            pdFALSE,
            portMAX_DELAY);

            /* xEventGroupWaitBits() returns the bits before the call returned, hence we can test which event actually
            * happened. */
            if (bits & WIFI_CONNECTED_BIT) {
                ESP_LOGI(TAG, "connected to ap SSID:%s password:%s", wifi_config.sta.ssid, wifi_config.sta.password);
                wifi_cred_save(wifi_account.wifi_ssid, wifi_account.wifi_password);  // 保存到 NVS，下次开机自动重连
                lvgl_port_lock(0);
                lv_label_set_text(label_wifi_connect, "WLAN 连接成功");
                lvgl_port_unlock();
                vTaskDelay(1000 / portTICK_PERIOD_MS); // 给上面的显示一点时间
                lvgl_port_lock(0);
                lv_obj_del(wifi_connect_page); // 删除此页面
                lv_obj_del(wifi_scan_page); // 删除此页面
                lvgl_port_unlock();
                vQueueDelete(xQueueWifiAccount); // 删除队列
                xQueueWifiAccount = NULL;
                icon_flag = 0; // 标记回到主界面
                xTaskCreatePinnedToCore(get_time_task, "get_time_task", 2 * 1024, NULL, 5, NULL, 0);  // 创建获取时间任务
                break; // 跳出while循环删除任务
            } else if (bits & WIFI_FAIL_BIT) {
                ESP_LOGI(TAG, "Failed to connect to SSID:%s, password:%s", wifi_config.sta.ssid, wifi_config.sta.password);
                lvgl_port_lock(0);
                lv_label_set_text(label_wifi_connect, "WLAN 连接失败");
                lvgl_port_unlock();
                vTaskDelay(1000 / portTICK_PERIOD_MS); // 给上面的显示一点时间
                lvgl_port_lock(0);
                lv_obj_del(wifi_connect_page); // 删除此页面
                lvgl_port_unlock();
                xEventGroupClearBits(s_wifi_event_group, WIFI_FAIL_BIT); // 清除此事件标志位

            } else {
                ESP_LOGE(TAG, "UNEXPECTED EVENT");
                lvgl_port_lock(0);
                lv_label_set_text(label_wifi_connect, "WLAN 连接异常");
                lvgl_port_unlock();
                vTaskDelay(1000 / portTICK_PERIOD_MS); // 给上面的显示一点时间
                lvgl_port_lock(0);
                lv_obj_del(wifi_connect_page); // 删除此页面
                lv_obj_del(wifi_scan_page); // 删除此页面
                lvgl_port_unlock();
                wifiset_deinit(); // 清除wifi初始化
            }
        }
    }
    vTaskDelete(NULL);
}

// 返回主界面按钮事件处理函数
static void btn_backmain_cb(lv_event_t * e)
{
    ESP_LOGI(TAG, "btn_backmain Clicked");

    // // 删除wifi扫描界面
    lvgl_port_lock(0);
    lv_obj_del(wifi_scan_page); 
    lvgl_port_unlock();

    // 通知wifi_connect任务退出（队列可能已被连接成功路径删除，需判空）
    wifi_account_t wifi_account;
    wifi_account.back_flag = 1;
    if (xQueueWifiAccount != NULL) {
        xQueueSend(xQueueWifiAccount, &wifi_account, portMAX_DELAY);
    }

    wifiset_deinit();// 清除wifi扫描痕迹
    if (xQueueWifiAccount != NULL) {
        vQueueDelete(xQueueWifiAccount); // 删除队列
        xQueueWifiAccount = NULL;
    }
    icon_flag = 0;
}

// wifi连接
void app_wifi_connect(void *arg)
{
    vTaskDelay(200 / portTICK_PERIOD_MS);
    // 扫描WLAN信息
    wifi_ap_record_t ap_info[DEFAULT_SCAN_LIST_SIZE];  // 记录扫描到的wifi信息
    uint16_t ap_number = DEFAULT_SCAN_LIST_SIZE; 
    wifi_scan(ap_info, &ap_number); // 扫描附近wifi

    lvgl_port_lock(0);
    // 修改标题
    lv_label_set_text_fmt(label_wifi_scan, "%d WLAN", ap_number);

    // 创建返回按钮
    lv_obj_t *btn_back = lv_btn_create(obj_scan_title);
    lv_obj_align(btn_back, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_size(btn_back, 60, 30);
    lv_obj_set_style_border_width(btn_back, 0, 0); // 设置边框宽度
    lv_obj_set_style_pad_all(btn_back, 0, 0);  // 设置间隙
    lv_obj_set_style_bg_opa(btn_back, LV_OPA_TRANSP, LV_PART_MAIN); // 背景透明
    lv_obj_set_style_shadow_opa(btn_back, LV_OPA_TRANSP, LV_PART_MAIN); // 阴影透明
    lv_obj_add_event_cb(btn_back, btn_backmain_cb, LV_EVENT_CLICKED, NULL); // 添加按键处理函数

    lv_obj_t *label_back = lv_label_create(btn_back); 
    lv_label_set_text(label_back, LV_SYMBOL_LEFT);  // 按键上显示左箭头符号
    lv_obj_set_style_text_font(label_back, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(label_back, lv_color_hex(0xffffff), 0); 
    lv_obj_align(label_back, LV_ALIGN_CENTER, -10, 0);

    // 创建wifi信息列表
    wifi_list = lv_list_create(wifi_scan_page);
    lv_obj_set_size(wifi_list, 320, 200);
    lv_obj_align(wifi_list, LV_ALIGN_TOP_LEFT, 0, 40);
    lv_obj_set_style_border_width(wifi_list, 0, 0);
    lv_obj_set_style_text_font(wifi_list, &font_alipuhui20, 0);
    lv_obj_set_scrollbar_mode(wifi_list, LV_SCROLLBAR_MODE_OFF); // 隐藏wifi_list滚动条
    // 显示wifi信息
    lv_obj_t * btn;
    for (int i = 0; i < ap_number; i++) {
        ESP_LOGI(TAG, "SSID \t\t%s", ap_info[i].ssid);  // 终端输出wifi名称
        ESP_LOGI(TAG, "RSSI \t\t%d", ap_info[i].rssi);  // 终端输出wifi信号质量
        // 添加wifi列表
        btn = lv_list_add_btn(wifi_list, LV_SYMBOL_WIFI, (const char *)ap_info[i].ssid); 
        lv_obj_add_event_cb(btn, list_btn_cb, LV_EVENT_CLICKED, NULL); // 添加点击回调函数
    }
    lvgl_port_unlock();
    
    // 创建wifi连接任务
    xQueueWifiAccount = xQueueCreate(2, sizeof(wifi_account_t));
    xTaskCreatePinnedToCore(wifi_connect, "wifi_connect", 4 * 1024, NULL, 5, NULL, 1);  // 创建wifi连接任务
    vTaskDelete(NULL);
}

//  任务函数
static void wifiset_tips_task(void *pvParameters)
{
    // 显示扫描情况
    lvgl_port_lock(0);
    label_wifi_scan = lv_label_create(wifi_scan_page);
    lv_label_set_text(label_wifi_scan, "WLAN 已连接");
    lv_obj_set_style_text_color(label_wifi_scan, lv_color_hex(0x000000), 0); 
    lv_obj_set_style_text_font(label_wifi_scan, &font_alipuhui20, 0);
    lv_obj_align(label_wifi_scan, LV_ALIGN_CENTER, 0, -50);
    lvgl_port_unlock();
    vTaskDelay(500 / portTICK_PERIOD_MS); 
    lvgl_port_lock(0);
    lv_obj_del(wifi_scan_page);
    lvgl_port_unlock();

    vTaskDelete(NULL);
}

// 进入WIFI设置应用
static void wifiset_event_handler(lv_event_t * e)
{  
    // 创建一个界面对象
    static lv_style_t style;
    lv_style_init(&style);
    lv_style_set_radius(&style, 10);  
    lv_style_set_bg_opa( &style, LV_OPA_COVER );
    lv_style_set_bg_color(&style, lv_color_hex(0xFFFFFF));
    lv_style_set_border_width(&style, 0);
    lv_style_set_pad_all(&style, 0);
    lv_style_set_width(&style, 320);  
    lv_style_set_height(&style, 240); 

    wifi_scan_page = lv_obj_create(lv_scr_act());
    lv_obj_add_style(wifi_scan_page, &style, 0);

    // 判断wifi是否已连接
    int isconnect_flag = 1;
    if (s_wifi_event_group != NULL) // 如果创建了此事件组
    {
        // 查看是否已经连接wifi
        EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group, WIFI_CONNECTED_BIT, pdFALSE, pdFALSE, 10);
        if (bits & WIFI_CONNECTED_BIT) { // 如果已经连接到wifi
            xTaskCreatePinnedToCore(wifiset_tips_task, "wifiset_tips_task", 2048, NULL, 5, NULL, 0);
        }
        else{
            isconnect_flag = 0;
        } 
    }
    if (s_wifi_event_group == NULL || isconnect_flag == 0) // 没有连接到wifi
    {
        // 创建标题背景
        obj_scan_title = lv_obj_create(wifi_scan_page);
        lv_obj_set_size(obj_scan_title, 320, 40);
        lv_obj_set_style_pad_all(obj_scan_title, 0, 0);  // 设置间隙
        lv_obj_align(obj_scan_title, LV_ALIGN_TOP_LEFT, 0, 0);
        lv_obj_set_style_bg_color(obj_scan_title, lv_color_hex(0x008b8b), 0);
        // 显示扫描情况
        label_wifi_scan = lv_label_create(obj_scan_title);
        lv_label_set_text(label_wifi_scan, "WLAN扫描中...");
        lv_obj_set_style_text_color(label_wifi_scan, lv_color_hex(0xffffff), 0); 
        lv_obj_set_style_text_font(label_wifi_scan, &font_alipuhui20, 0);
        lv_obj_align(label_wifi_scan, LV_ALIGN_CENTER, 0, 0);

        icon_flag = 5; // 标记已经进入第5个应用

        xTaskCreatePinnedToCore(app_wifi_connect, "app_wifi_connect", 4*1024, NULL, 5, NULL, 0);
    }
}

// 开机自动重连：读取 NVS 中已保存的 WiFi 账号密码，后台静默连接（无界面）
// 由 main.c 在进入主界面后调用。连接成功后 event_handler 会置位并点亮状态栏，
// get_time_task 负责 NTP 对时。
void app_wifi_auto_connect(void)
{
    char ssid[32] = {0};
    char pass[64] = {0};
    if (!wifi_cred_load(ssid, sizeof(ssid), pass, sizeof(pass))) {
        ESP_LOGI(TAG, "无已保存的 WiFi，跳过自动重连");
        return;
    }
    ESP_LOGI(TAG, "自动重连已保存的 AP: %s", ssid);

    wifi_stack_init();   // 初始化协议栈（若已初始化则直接返回）

    wifi_config_t wifi_config = {
        .sta = {
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
            .sae_pwe_h2e = WPA3_SAE_PWE_BOTH,
            .sae_h2e_identifier = "",
        },
    };
    strcpy((char *)wifi_config.sta.ssid, ssid);
    strcpy((char *)wifi_config.sta.password, pass);
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    esp_wifi_connect();

    // 启动获取时间任务：等待 CONNECTED 位后做 NTP 对时并刷新状态栏时钟
    xTaskCreatePinnedToCore(get_time_task, "get_time_task", 2 * 1024, NULL, 5, NULL, 0);
}


/******************************** 第6个图标 蓝牙设置 应用程序***********************************************************************************/
lv_obj_t * ble_label;
lv_obj_t * btn_ble_back;

// 返回主界面按钮事件处理函数
static void btn_ble_back_cb(lv_event_t * e)
{
    bt_hid_end();
    statusbar_set_bt(false);   // 熄灭状态栏蓝牙图标
    lv_obj_del(icon_in_obj);
    icon_flag = 0;
}

// 进入蓝牙设置应用
static void btset_event_handler(lv_event_t * e)
{
    // 创建一个界面对象
    static lv_style_t style;
    lv_style_init(&style);
    lv_style_set_radius(&style, 10);  
    lv_style_set_bg_opa( &style, LV_OPA_COVER );
    lv_style_set_bg_color(&style, lv_color_hex(0xffffff));
    lv_style_set_border_width(&style, 0);
    lv_style_set_pad_all(&style, 0);
    lv_style_set_width(&style, 320);  
    lv_style_set_height(&style, 240); 

    icon_in_obj = lv_obj_create(lv_scr_act());
    lv_obj_add_style(icon_in_obj, &style, 0);
    
    // 创建标题背景
    lv_obj_t *ble_title = lv_obj_create(icon_in_obj);
    lv_obj_set_size(ble_title, 320, 40);
    lv_obj_set_style_pad_all(ble_title, 0, 0);  // 设置间隙
    lv_obj_align(ble_title, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_bg_color(ble_title, lv_color_hex(0xb87fa8), 0);
    // 显示标题
    ble_label = lv_label_create(ble_title);
    lv_label_set_text(ble_label, "蓝牙控制器");
    lv_obj_set_style_text_color(ble_label, lv_color_hex(0xffffff), 0); 
    lv_obj_set_style_text_font(ble_label, &font_alipuhui20, 0);
    lv_obj_align(ble_label, LV_ALIGN_CENTER, 0, 0);
    // 创建后退按钮
    btn_ble_back = lv_btn_create(ble_title);
    lv_obj_align(btn_ble_back, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_size(btn_ble_back, 60, 30);
    lv_obj_set_style_border_width(btn_ble_back, 0, 0); // 设置边框宽度
    lv_obj_set_style_pad_all(btn_ble_back, 0, 0);  // 设置间隙
    lv_obj_set_style_bg_opa(btn_ble_back, LV_OPA_TRANSP, LV_PART_MAIN); // 背景透明
    lv_obj_set_style_shadow_opa(btn_ble_back, LV_OPA_TRANSP, LV_PART_MAIN); // 阴影透明
    lv_obj_add_event_cb(btn_ble_back, btn_ble_back_cb, LV_EVENT_CLICKED, NULL); // 添加按键处理函数

    lv_obj_t *label_back = lv_label_create(btn_ble_back); 
    lv_label_set_text(label_back, LV_SYMBOL_LEFT);  // 按键上显示左箭头符号
    lv_obj_set_style_text_font(label_back, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(label_back, lv_color_hex(0xffffff), 0); 
    lv_obj_align(label_back, LV_ALIGN_CENTER, -10, 0);

    app_hid_ctrl();

    statusbar_set_bt(true);   // 点亮状态栏蓝牙图标
    icon_flag = 6; // 标记已经进入第6个应用
}


/******************************** 主界面  ******************************/

/* 6 个原有应用的图标（图片资源） */
LV_IMG_DECLARE(img_att_icon);
LV_IMG_DECLARE(img_music_icon);
LV_IMG_DECLARE(img_sd_icon);
LV_IMG_DECLARE(img_camera_icon);
LV_IMG_DECLARE(img_wifiset_icon);
LV_IMG_DECLARE(img_btset_icon);

/* 主界面应用清单：中文标题 / 背景色 / 点击回调 / 图片图标 / 符号图标 */
typedef struct {
    const char    *caption;   // 图标下方的中文标题
    uint32_t       color;     // 按钮背景色
    lv_event_cb_t  cb;        // 点击进入的子应用
    const void    *img;       // 图片图标（前 6 个应用），无则为 NULL
    const char    *symbol;    // 符号图标（新增 4 个应用），无则为 NULL
} app_entry_t;

static const app_entry_t s_apps[] = {
    {"运动", 0x30a830, att_event_handler,    &img_att_icon,      NULL},
    {"音乐", 0xf87c30, music_event_handler,  &img_music_icon,    NULL},
    {"SD卡", 0x008b8b, sdcard_event_handler, &img_sd_icon,       NULL},
    {"相机", 0xd8b010, camera_event_handler, &img_camera_icon,   NULL},
    {"WiFi", 0xcd5c5c, wifiset_event_handler,&img_wifiset_icon,  NULL},
    {"蓝牙", 0xb87fa8, btset_event_handler,  &img_btset_icon,    NULL},
    {"设置", 0x607d8b, app_settings_open,    NULL, LV_SYMBOL_SETTINGS},
    {"录音", 0xe91e63, app_recorder_open,    NULL, LV_SYMBOL_AUDIO},
    {"监测", 0x3f51b5, app_monitor_open,     NULL, LV_SYMBOL_BARS},
    {"秒表", 0xff9800, app_stopwatch_open,   NULL, LV_SYMBOL_LOOP},
};
#define APP_COUNT (sizeof(s_apps) / sizeof(s_apps[0]))

void lv_main_page(void)
{
    lvgl_port_lock(0);

    lv_obj_del(lckfb_logo); // 删除开机logo
    lv_obj_set_style_bg_color(lv_scr_act(), lv_color_hex(0x000000), 0); // 背景黑色

    static lv_style_t style;
    lv_style_init(&style);
    lv_style_set_radius(&style, 10);
    lv_style_set_bg_opa(&style, LV_OPA_COVER);
    lv_style_set_bg_color(&style, lv_color_hex(0x00BFFF));
    lv_style_set_bg_grad_color(&style, lv_color_hex(0x00BF00));
    lv_style_set_bg_grad_dir(&style, LV_GRAD_DIR_VER);
    lv_style_set_border_width(&style, 0);
    lv_style_set_pad_all(&style, 0);
    lv_style_set_width(&style, 320);
    lv_style_set_height(&style, 240);

    main_obj = lv_obj_create(lv_scr_act());
    lv_obj_add_style(main_obj, &style, 0);
    lv_obj_clear_flag(main_obj, LV_OBJ_FLAG_SCROLLABLE);

    /* 顶部状态栏：时钟 / WiFi / 蓝牙 / 内存（电池条） */
    statusbar_create(main_obj);
    statusbar_refresh_time();
    statusbar_refresh_mem();

    /* 可滚动的应用图标网格：3 列，容纳 10 个应用，向下滚动查看更多 */
    lv_obj_t *grid = lv_obj_create(main_obj);
    lv_obj_set_size(grid, 320, 212);
    lv_obj_align(grid, LV_ALIGN_TOP_LEFT, 0, 28);
    lv_obj_set_style_bg_opa(grid, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(grid, 0, 0);
    lv_obj_set_style_radius(grid, 0, 0);
    lv_obj_set_style_pad_all(grid, 4, 0);
    lv_obj_set_style_pad_gap(grid, 4, 0);
    lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(grid, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_scroll_dir(grid, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(grid, LV_SCROLLBAR_MODE_AUTO);

    for (size_t i = 0; i < APP_COUNT; i++) {
        /* 每个应用一个圆角按钮 */
        lv_obj_t *cell = lv_btn_create(grid);
        lv_obj_set_size(cell, 100, 100);
        lv_obj_set_style_radius(cell, 14, 0);
        lv_obj_set_style_bg_color(cell, lv_color_hex(s_apps[i].color), 0);
        lv_obj_set_style_border_width(cell, 0, 0);
        lv_obj_set_style_shadow_width(cell, 0, 0);
        lv_obj_set_style_pad_all(cell, 2, 0);
        lv_obj_clear_flag(cell, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
        lv_obj_add_event_cb(cell, s_apps[i].cb, LV_EVENT_CLICKED, NULL);

        if (s_apps[i].img) {                 // 原有应用：图片图标，缩放到约 48px 与符号统一
            lv_obj_t *im = lv_img_create(cell);
            lv_img_set_src(im, s_apps[i].img);
            lv_img_set_pivot(im, 37, 37);    // 以图片中心为轴缩放
            lv_img_set_zoom(im, 164);        // 75px * 164/256 ≈ 48px
            lv_obj_align(im, LV_ALIGN_CENTER, 0, -12);
        } else {                             // 新增应用：大号符号图标
            lv_obj_t *sy = lv_label_create(cell);
            lv_label_set_text(sy, s_apps[i].symbol);
            lv_obj_set_style_text_font(sy, &lv_font_montserrat_48, 0);
            lv_obj_set_style_text_color(sy, lv_color_hex(0xffffff), 0);
            lv_obj_align(sy, LV_ALIGN_CENTER, 0, -12);
        }

        lv_obj_t *cap = lv_label_create(cell);   // 图标下方的中文标题
        lv_label_set_text(cap, s_apps[i].caption);
        lv_obj_set_style_text_font(cap, &font_alipuhui20, 0);
        lv_obj_set_style_text_color(cap, lv_color_hex(0xffffff), 0);
        lv_obj_align(cap, LV_ALIGN_BOTTOM_MID, 0, -4);
    }

    lvgl_port_unlock();
}

