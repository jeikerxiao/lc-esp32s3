# 实验 16：手持智能终端 Plus + 联网时钟

## 简介

本实验在实验 15（手持智能终端 Plus）的基础上，保留原有全部功能，并**新增一个独立的"时钟"子应用**：大号数字时钟（时:分:秒 + 年月日 + 星期），**每次进入都会从互联网(NTP)重新校准一次时间**。开机后进入带**顶部状态栏**的应用网格主界面，共 11 个子应用：

原有 6 个（功能保留并增强）：

1. **运动监测**：读取 QMI8658 姿态传感器，显示倾角并检测运动/静止/剧烈运动状态
2. **音乐播放器**：播放 MP3，新增进度条、音量记忆，并支持播放 SD 卡里的歌曲
3. **SD 卡文件浏览器**：浏览 TF 卡中的文件目录
4. **摄像头取景**：实时取景，新增拍照功能，照片以 BMP 格式保存到 SD 卡
5. **WiFi 设置**：连接 WiFi 后 NTP 校时，凭据保存，开机自动重连
6. **蓝牙控制器**：BLE HID 音量遥控器

新增 5 个：

7. **设置**：亮度、音量、开机音效开关、自动熄屏时间、关于信息，全部掉电保存
8. **录音机**：ES7210 麦克风录音为 WAV 存到 SD 卡，可播放、删除
9. **环境监测仪**：QMI8658 三轴倾角实时曲线，可把数据记录成 CSV 存到 SD 卡
10. **秒表 / 倒计时**：秒表支持计次，倒计时到点蜂鸣提示
11. **时钟**（本实验新增）：大号数字时钟，每次进入自动联网 NTP 校准；无法联网时显示"上次时间"并标记"未校准"，仍每秒走动

此外还保留两个全局体验特性：主界面**顶部状态栏**（时钟 / WiFi / 蓝牙 / 内存）和**自动熄屏省电**（无操作一段时间后关背光，触摸唤醒）。

## 原理

### 综合架构

本项目在实验 08（LVGL 界面）的框架上，把前面各实验的功能模块化整合，并新增了设置持久化与更多子应用：

| 子应用 | 复用的知识点 | 核心技术 |
|--------|-----------|---------|
| 运动监测 | 实验 02 | QMI8658 姿态传感器 + 运动检测 |
| 音乐播放器 | 实验 11 | SPIFFS/SD + MP3 解码 + ES8311 |
| SD 卡浏览 | 实验 03 | SDMMC + FAT 文件系统 |
| 摄像头取景/拍照 | 实验 07 | DVP 摄像头 + RGB565→BMP |
| WiFi 设置 | 实验 09 | WiFi STA + SNTP + NVS 凭据 |
| 蓝牙控制器 | 实验 10 | BLE HID Device |
| 设置 | 实验 08 | NVS 参数持久化 |
| 录音机 | 实验 11 | ES7210 TDM 采集 + WAV 封装 |
| 环境监测仪 | 实验 02 | lv_chart 曲线 + CSV 记录 |
| 秒表/倒计时 | — | esp_timer 微秒计时 + I2S 蜂鸣 |
| 时钟 | 实验 09 | WiFi STA + SNTP + 独立 FreeRTOS 任务 |

### 联网时钟与"每次校准"（本实验核心）

ESP32-S3 **没有电池 RTC**，系统时间只在本次开机内有效：断电重启后时间会回到 1970 年，必须重新联网校准才能恢复正确时间。因此本实验把"校时"做成一个可复用、可随进随校的公共能力，并单独开一个"时钟"应用来演示。

在 `app_ui.c` 中抽出三个公共接口（`app_ui.h` 声明）：

- `app_ntp_sync_once(timeout_ms)`：执行**一次** SNTP 对时——`esp_netif_sntp_init` → 分片 `esp_netif_sntp_sync_wait` 直到拿到时间或总超时 → `esp_netif_sntp_deinit`，随后 `setenv("TZ","CST-8",1); tzset();` 设置东八区。内部用一把 `s_ntp_mutex` 互斥锁保护，保证开机自动校时任务与时钟应用的校准任务不会同时操作 SNTP。成功返回 `ESP_OK`，超时返回 `ESP_ERR_TIMEOUT`。
- `app_wifi_is_connected()`：读事件组 `WIFI_CONNECTED_BIT`，判断当前是否已联网。
- `app_wifi_ensure_connected(timeout_ms)`：已连接直接返回 `true`；否则用 NVS 中已保存的凭据调用 `wifi_stack_init()` + `esp_wifi_connect()`，在超时内阻塞等待连接结果。无保存凭据或超时返回 `false`。

开机自动校时的 `get_time_task` 也改为调用 `app_ntp_sync_once(20000)`，与时钟应用共用同一套校时逻辑。

### 时钟应用（app_clock.c）

进入 `app_clock_open()` 时：先用 `app_page_open("时钟", ...)` 生成标准页面骨架，建三个标签（大号时间、日期星期、校准状态）和一个"重新校准"按钮，创建每秒一次的 `lv_timer`（`clock_tick_cb`）从系统时间刷新显示，然后 `clock_start_sync()` 启动校准任务。

校准任务 `clock_sync_task` 运行在**独立的 FreeRTOS 任务**里（不能放在 LVGL 任务，因为联网和 NTP 都是阻塞操作）：

```
确保联网(app_wifi_ensure_connected 15s)
  ├─ 失败 → 状态"未联网 · 显示上次时间（未校准）"（红色）
  └─ 成功 → app_ntp_sync_once(20s)
             ├─ ESP_OK  → 状态"已校准 · 东八区时间"（绿色），点亮主界面状态栏 WiFi 图标
             └─ 超时/失败 → 状态"校准失败 · 显示上次时间（未校准）"（红色）
```

时钟走动只读当前系统时间：校准成功时走的是校准后的准确时间；离线时走的是"上次时间"（本次开机内曾经校准过的系统时间，或若从未校准则是 1970 年附近），界面用"未校准"提示区分。点"重新校准"按钮会再次触发同一套流程（用 `s_sync_busy` 防止重复创建任务）。

**线程安全**：校准任务写界面统一走 `clock_set_status()`，内部 `lvgl_port_lock` 加锁并再次确认 `s_active`（应用是否在前台）。返回时 `back_cb` 先把 `s_active=false`、删除 `lv_timer`、再删画布；这样即使校准任务仍在后台跑，也不会去操作已删除的界面对象。

### 应用切换机制

主界面用全局变量 `icon_flag` 标记当前所在应用（0=主界面，1~11=各应用）。点击图标进入对应子应用，子应用返回时把 `icon_flag` 置回 0。每个子应用在进入时初始化对应硬件、创建界面和任务，退出时反初始化并释放资源。新增的子应用统一用 `app_page_open()` 生成"标题栏 + 返回按钮"的标准页面骨架，减少重复代码。

### 设置持久化（NVS）

`app_common.c` 用一个全局结构体 `g_settings` 保存亮度、音量、开机音效开关、自动熄屏时间四项配置，对应 NVS 命名空间 `app_cfg`。开机 `app_main` 先 `settings_load()` 读出并立即应用（背光亮度、系统音量）；在"设置"或"音乐"里改动后调用 `settings_save()` 写回，掉电不丢失。

### WiFi 凭据保存与开机自动重连

WiFi 密码保存在 NVS 命名空间 `wifi_cfg`。手动连接成功后调用 `wifi_cred_save()` 存起来；开机时 `main_page_task` 调用 `app_wifi_auto_connect()`，若读到已保存的凭据就自动连接并启动 NTP 校时任务，无需每次重新输入密码。WiFi 协议栈初始化封装在 `wifi_stack_init()` 里做成幂等函数，自动重连、手动扫描、以及时钟应用的 `app_wifi_ensure_connected()` 共用同一套初始化，避免重复初始化冲突。

### 顶部状态栏

`statusbar_create()` 在主界面顶部画一条 320×28 的半透明黑条：左侧显示日期与时间（NTP 未同步时显示"未联网"），右侧依次是内存占用条、WiFi 图标、蓝牙图标。图标灰/亮表示未连/已连。`statusbar_refresh_time()` 由每秒一次的 `lv_timer` 驱动，`statusbar_set_wifi()`/`statusbar_set_bt()` 在连接状态变化时点亮对应图标。时钟应用校准成功后也会调用这两个函数同步主界面状态栏。

### 自动熄屏省电

`screen_idle_start()` 创建每秒一次的定时器，用 `lv_disp_get_inactive_time()` 读取无操作时长。仅在主界面（`icon_flag==0`）生效，超过设定的熄屏时间就关背光；一旦检测到触摸（无操作时长回落到 1.5 秒内）立即 `screen_wake()` 恢复背光和亮度。熄屏时间设为 0 表示永不熄屏。

### 音乐库扫描（SPIFFS + SD 卡）

实验 14 用 `file_iterator` 只能扫描单个 SPIFFS 目录。本项目改为自建音乐库：`music_lib_scan()` 递归遍历 `/spiffs` 和 `/sdcard` 两个根目录，把所有 `.mp3` 的完整路径和文件名收集到数组里（上限 64 首），播放、上下曲、列表都基于这个数组。进入音乐应用时会先挂载 SD 卡再扫描，退出时卸载。

### 播放进度条（估算）

`esp-audio-player` 组件没有提供播放位置/总时长接口，无法做真正可拖动的进度条。这里做**只显示**的进度：`estimate_mp3_duration()` 解析 MP3 首个帧头拿到比特率，再用文件大小换算出估算总时长；播放时每秒累加已播放秒数，驱动进度条和时间标签。因此进度是估算值，VBR（可变比特率）歌曲会有偏差。

### 摄像头拍照（RGB565 → BMP）

摄像头输出 `PIXFORMAT_RGB565`、`FRAMESIZE_QVGA`（320×240），为大端字节序。点"拍照"按钮置位请求标志，取帧任务复用当前帧，用 `save_bmp()` 手工拼一个 24 位 BMP 文件头、把每个 RGB565 像素拆成 R/G/B 并按 BMP 的自下而上、BGR 顺序写入 `/sdcard/DCIM/IMG_xxx.bmp`。

### 录音机（ES7210 → WAV）

`bsp_codec_init()` 已在开机时初始化 ES7210 四通道麦克风（16kHz/16bit TDM）。录音任务循环调用 `bsp_get_feed_data(true, ...)` 读四通道交织原始数据，只取通道 0 得到单声道，写入自建的 44 字节标准 PCM WAV 头 + 数据，结束时回填长度字段。文件存到 `/sdcard/REC/REC_xxx.wav`。播放时把单声道复制成左右两声道经 `bsp_i2s_write()` 送喇叭。

### 环境监测仪（曲线 + CSV）

用 LVGL v8 的 `lv_chart`（折线、30 点、SHIFT 滚动、量程 -100~100）画 X/Y/Z 三条曲线，200ms 一次的定时器读取 `qmi8658_fetch_angleFromAcc()` 更新曲线和数值。点"记录"后把 `毫秒,X,Y,Z,运动状态` 逐行追加写入 `/sdcard/monitor_log.csv`（新文件先写表头）。

### 秒表 / 倒计时

顶部 `lv_btnmatrix` 切换两个面板。计时基于 `esp_timer_get_time()`（微秒）而非定时器周期，保证精度：秒表把"已累计 + 本次运行"换算成 `MM:SS.cs`，支持开始/暂停/复位/计次；倒计时按滑块设定分钟数计算结束时刻，到零显示"时间到！"并触发一次蜂鸣——蜂鸣任务生成 1kHz 方波（16kHz 立体声 16bit，约 300ms）写 I2S。

### 开机流程

```
NVS 初始化 → settings_load() 读用户设置
   → 硬件初始化(I2C/PCA9557/LVGL/SPIFFS/codec)
   → 应用保存的亮度/音量
   → logo 旋转动画 + 开机音效(受"开机音效"开关控制，可跳过)
   → 进入带状态栏的应用网格主界面
   → 尝试用 NVS 里的 WiFi 凭据自动重连 + NTP 校时
   → 启动自动熄屏定时器
```

## 硬件接线

本项目使用开发板的全部板载资源，无需额外接线。主要引脚汇总：

| 功能 | 信号 | GPIO |
|------|------|------|
| I2C | SDA / SCL | GPIO1 / GPIO2 |
| LCD SPI | MOSI / CLK / DC | GPIO40 / GPIO41 / GPIO39 |
| LCD 背光 | BL | GPIO42 |
| 音频 I2S1 | MCLK / SCLK / LRCK | GPIO38 / GPIO14 / GPIO13 |
| 音频 I2S1 | DOUT / SDIN | GPIO45 / GPIO12 |
| SD 卡 SDMMC | CLK / CMD / DAT0 | GPIO47 / GPIO48 / GPIO21 |
| 摄像头 DVP | XCLK / VSYNC / HREF / PCLK | GPIO5 / GPIO3 / GPIO46 / GPIO7 |
| 摄像头 DVP | D0~D7 | GPIO16/18/8/17/15/6/4/9 |

LCD CS、功放 PA_EN、摄像头 PWDN 均经 PCA9557 IO 扩展芯片控制。

## 代码讲解

工程目录结构（相对实验 15 的新增文件已标注）：

```
16-clock/
├── CMakeLists.txt
├── partitions.csv          # factory 8M + storage(spiffs) 3M
├── sdkconfig.defaults      # LV_FONT_MONTSERRAT_48（大图标符号 + 时钟大号数字）
├── spiffs/                 # 3 首 MP3
└── main/
    ├── CMakeLists.txt      # 新增 app_clock.c
    ├── assets/             # 图标、logo、中文字体
    ├── bt/                 # BLE HID 源码
    ├── app_common.c/.h     # 设置持久化/状态栏/自动熄屏/页面骨架
    ├── app_settings.c      # 设置子应用
    ├── app_recorder.c      # 录音机子应用
    ├── app_monitor.c       # 环境监测仪子应用
    ├── app_stopwatch.c     # 秒表/倒计时子应用
    ├── app_clock.c         # 【新增】联网时钟子应用
    ├── app_ui.c            # 主界面 + 原 6 个子应用 + NTP/WiFi 公共接口
    ├── app_ui.h            # 新增 app_clock_open / app_ntp_sync_once / app_wifi_* 声明
    ├── esp32_s3_szp.c      # BSP（开机音效任务受 boot_sound 开关控制）
    ├── esp32_s3_szp.h
    ├── sword.pcm           # 开机音效
    ├── idf_component.yml
    └── main.c              # 开机应用设置 + 自动重连 + 启动熄屏
```

### 公共基础设施 app_common.c

- `settings_load/save`：`g_settings` 与 NVS `app_cfg` 的读写。
- `wifi_cred_load/save/clear`：WiFi 凭据与 NVS `wifi_cfg` 的读写。
- `statusbar_create/set_wifi/set_bt/refresh_time/refresh_mem`：顶部状态栏。
- `screen_idle_start/screen_wake`：自动熄屏与唤醒。
- `app_page_open(title, color, back_cb)`：生成标准子应用页面骨架，返回画布对象。

所有对 LVGL 对象的操作都用 `lvgl_port_lock/unlock` 包裹（该锁可重入，嵌套调用安全）。

### 联网校时公共接口 app_ui.c

- `app_ntp_sync_once(timeout_ms)`：互斥保护下做一次 SNTP 对时并设置东八区，成功 `ESP_OK`。
- `app_wifi_is_connected()` / `app_wifi_ensure_connected(timeout_ms)`：查询 / 确保 WiFi 已连接。
- 主界面图标 `s_apps[]` 里，时钟项没有现成的 LVGL 符号（LVGL 不含时钟符号），改用 `icon_builder` 函数指针，由 `clock_icon_builder()` 用圆环 + 时针/分针两条 `lv_line` 自绘一个表盘图标。

### 关键流程

`app_main()`（main.c）：

1. `nvs_flash_init()`：设置与 BLE 绑定信息都需要 NVS，损坏时擦除重建。
2. `settings_load()`：读出亮度/音量/开机音效/熄屏时间。
3. 初始化 I2C / PCA9557 / LVGL / SPIFFS / codec，随后 `bsp_display_brightness_set()`、`bsp_codec_volume_set()` 应用保存的亮度和音量。
4. `lv_gui_start()`：开机 logo 旋转动画。
5. 创建 `power_music_task`（开机音效，若开关关闭则直接跳过）和 `main_page_task`。
6. `main_page_task`：`lv_main_page()` 建主界面后调用 `app_wifi_auto_connect()` 自动重连、`screen_idle_start()` 启动熄屏。

### 主界面

`lv_main_page()`：先画顶部状态栏，再建一个可上下滚动的柔性网格（3 列），把 `s_apps[]` 里的 11 个应用逐个生成为 100×100 圆角按钮。前 6 个沿用原图片图标（缩放到约 48px 与符号统一），设置/录音/监测/秒表用 `montserrat_48` 大号符号图标，时钟用自绘表盘图标；图标下方用中文字体显示标题。11 个图标分 4 行，向下滚动可看到最后一行。

### 十一个子应用

- **运动监测**（`att_event_handler`）：初始化 QMI8658（失败重试 3 次），200ms 更新倾角和运动状态，退出时关闭传感器。
- **音乐播放器**（`music_event_handler`）：挂载 SD → 扫描 SPIFFS+SD 的 MP3 → 建播放器界面（含进度条、音量条、下拉列表）；音量改动写入 `g_settings` 并保存；退出时删定时器、删播放器、卸载 SD。
- **SD 卡浏览**（`sdcard_event_handler`）：挂载 SD，递归列目录，按扩展名显示图标，退到根目录时卸载。
- **摄像头取景/拍照**（`camera_event_handler`）：挂载 SD 并建 `DCIM` 目录，循环取帧刷新画面；"拍照"按钮置位请求，任务把当前帧存成 BMP；退出时反初始化摄像头、卸载 SD、掉电。
- **WiFi 设置**（`wifiset_event_handler`）：扫描热点、滚轮输入密码连接，成功后保存凭据、点亮状态栏 WiFi 图标并启动 NTP 校时。
- **蓝牙控制器**（`btset_event_handler`）：BLE HID 音量遥控，连接时点亮状态栏蓝牙图标，退出时反初始化。
- **设置**（`app_settings_open`）：亮度/音量滑条实时生效、开机音效开关、自动熄屏下拉框、关于信息（固件/IDF 版本、堆内存）；返回时 `settings_save()` 写回 NVS。
- **录音机**（`app_recorder_open`）：挂载 SD 建 `REC` 目录，录音/停止/删除三按钮 + 录音列表，点列表项播放。
- **环境监测仪**（`app_monitor_open`）：后台初始化 QMI8658 成功后建曲线图，"记录"按钮开关 CSV 写盘，退出时删定时器、关文件、卸载 SD、关传感器。
- **秒表/倒计时**（`app_stopwatch_open`）：单一 80ms 定时器只刷新当前面板，秒表支持计次，倒计时到点蜂鸣。
- **时钟**（`app_clock_open`）：大号数字时钟，进入即在独立任务里"确保联网 → NTP 校准 → 刷新状态"，`lv_timer` 每秒从系统时间刷新显示；离线显示"上次时间（未校准）"仍走动；"重新校准"按钮可手动再校一次。

## 实验效果

编译烧录后：

- **开机**：logo 旋转动画 + "拔剑"音效（若"开机音效"关闭则静音直接进入），随后是带状态栏的应用网格主界面。
- **状态栏**：顶部显示日期时间（未联网时显示"未联网"）、内存条、WiFi/蓝牙图标。
- **自动重连**：若之前连过 WiFi，开机自动重连并 NTP 校时，状态栏时间开始走动、WiFi 图标变绿。
- **时钟应用**：主界面向下滚动到第 4 行，点"时钟"图标进入。屏幕中央是大号 `时:分:秒`，下方是 `年月日 星期`，再下方是校准状态。进入后状态先显示"正在连接 WiFi..."→"正在从互联网校准..."，成功后变绿显示"已校准 · 东八区时间"，时钟按准确时间每秒走动；点"重新校准"可再次联网校时。
- **时钟离线**：若没保存过 WiFi 或连不上/校准失败，状态显示红色"未联网 · 显示上次时间（未校准）"或"校准失败 · 显示上次时间（未校准）"，时钟仍每秒走动（走的是未经校准的系统时间）。
- **返回**：点时钟页面左上角返回箭头回到主界面，不重启；后台校准任务即使还在跑也不会再操作界面。
- **自动熄屏**：主界面无操作到设定时间后背光熄灭，触摸屏幕立即恢复。
- **音乐**：可播放 SPIFFS 和 SD 卡里的 MP3，进度条与时间随播放走动，调音量后下次开机保持。
- **拍照**：相机应用里点拍照按钮，照片存到 `/sdcard/DCIM/IMG_xxx.bmp`，屏幕提示已保存张数。
- **设置**：调亮度/音量立即生效，改开机音效、熄屏时间后返回即保存，重启仍生效。
- **录音机**：点录音开始采集、停止后存为 `/sdcard/REC/REC_xxx.wav`，点列表项回放，可删除。
- **监测**：三轴倾角实时曲线，点"记录"把数据写入 `/sdcard/monitor_log.csv`。
- **秒表**：秒表计时/计次，倒计时到点显示"时间到！"并蜂鸣。
- **运动/SD/蓝牙**：与实验 15 一致。

### 编译与烧录

```bash
idf.py set-target esp32s3
idf.py build flash monitor
```

## 注意事项

1. **ESP32-S3 无电池 RTC，时间不跨断电保存**：系统时间只在本次开机内有效。断电重启后时间回到 1970 年附近，必须重新联网校准。时钟应用每次进入都会重新校准，正是为了在联网可用时把时间拉回准确值。
2. **校准依赖已保存的 WiFi 凭据**：`app_wifi_ensure_connected()` 只能连之前在"WiFi 设置"里手动连过并保存的热点。从没连过 WiFi 时进时钟应用会直接显示"未联网（未校准）"，需先去 WiFi 应用连一次。
3. **联网/校时是阻塞操作，必须放独立任务**：`clock_sync_task` 运行在自建 FreeRTOS 任务（栈 4KB）里，绝不能放进 LVGL 回调或 `lv_timer`，否则会卡死界面甚至栈溢出重启。
4. **SNTP 用互斥锁串行化**：开机自动校时任务与时钟应用校准任务可能并发，`app_ntp_sync_once()` 内部用 `s_ntp_mutex` 保证同一时刻只有一个在操作 SNTP，避免重复 init/deinit 崩溃。该锁在 `lv_main_page()` 里提前创建。
5. **后台任务写界面要判前台标志**：校准任务通过 `clock_set_status()` 更新界面，内部加 LVGL 锁并检查 `s_active`；`back_cb` 先置 `s_active=false` 再删画布，避免任务访问已释放对象。
6. **时区固定东八区**：`app_ntp_sync_once()` 里 `setenv("TZ","CST-8",1)`。若在其他时区使用，改成对应的 POSIX 时区串即可。
7. **分区较大且已接近写满**：factory 分区 8M（含 WiFi+BT+LVGL+摄像头），storage 分区 3M（3 首 MP3）。当前固件占用约 96%（仅剩约 4%），继续加功能需注意 app 分区容量，必要时调大 `partitions.csv` 的 factory 分区。
8. **拍照/录音/监测/音乐都依赖 SD 卡**：需插入 FAT32 格式的 TF 卡。挂载失败时相应功能会提示"未检测到 SD 卡"，但不会格式化（`format_if_mount_failed=false`）。时钟应用不依赖 SD 卡。
9. **多个子应用会各自挂载/卸载 SD 卡**：进入时挂载、退出时卸载，请勿在两个应用间"嵌套"访问 SD，避免重复挂载冲突。
10. **BMP 照片颜色**：摄像头为大端 RGB565，`save_bmp()` 已按此解析；若烧录后照片颜色异常，把该函数里像素的两个字节对调即可。BMP 未压缩，单张 320×240 约 225KB。
11. **进度条是估算值**：基于 MP3 首帧比特率与文件大小换算，VBR 歌曲会偏差，且不支持拖动定位（组件无 seek 接口）。
12. **音乐采样率固定**：`_audio_player_std_clock` 里的 `bsp_codec_set_fs` 被注释掉，假定 MP3 为 16000Hz。播放 SD 卡里其他采样率（如 44.1kHz）的歌曲会变速/变调，需打开该注释让播放器按实际采样率重设 I2S 时钟。
13. **音乐库上限 64 首**：`MUSIC_MAX_FILES` 限制，超出部分不收录。
14. **录音为单声道 16kHz/16bit**：只取 ES7210 通道 0，播放时扩展为立体声；WAV 头为自建标准 44 字节 PCM 头。
15. **自动熄屏只在主界面生效**：进入任何子应用（`icon_flag!=0`）都不会熄屏，避免打断录音、拍照、看时钟等操作。
16. **WiFi 与蓝牙共存抢资源**：两者同开占用大量内存和射频，靠 PSRAM 相关配置腾空间；开机自动重连失败（如换了路由器）可在 WiFi 应用里重新手动连接，会覆盖保存的凭据。
17. **LVGL 跨任务加锁**：所有在任务/定时器里更新界面的代码都必须用 `lvgl_port_lock/unlock` 包裹（锁可重入）。
18. **子应用资源要配对释放**：进入初始化、退出反初始化（尤其摄像头、蓝牙、传感器、SD、lv_timer），反复进出注意配对，避免内存泄漏。
19. **iPhone 蓝牙报错可忽略**：BLE HID 配对时 iPhone 可能出现 `GATT_INSUF_ENCRYPTION`，属正常现象。
20. **任务栈要留足余量**：LVGL 回调、按钮事件都运行在 LVGL 任务栈（默认 4KB）里，切勿在其中定义大数组（如读取 MP3 帧头的 2KB 缓冲），否则栈溢出重启——本例 `estimate_mp3_duration()` 已改用 `malloc` 堆缓冲。自建任务（如运动监测 `task_process_att`、时钟校准 `clock_sync_task`）若要在任务里做 I2C/网络事务并创建多个 LVGL 控件，栈至少给 4KB（2KB 会溢出）。排查这类重启可抓串口里的 `A stack overflow in task xxx` 或 `Guru Meditation` 回溯，用 `xtensa-esp32s3-elf-addr2line -e build/handheld.elf <地址>` 定位。
21. **反初始化要可重复调用（幂等）**：WiFi 退出时 `wifiset_deinit()` 可能从"返回键"和"连接异常"两条路径各触发一次。注销事件处理器用 `ESP_ERROR_CHECK` 包裹时，第二次会因 `ESP_ERR_NOT_FOUND` 直接 `abort()` 重启。修复方式：用 `s_wifi_stack_inited` 标志做幂等保护，注销/删队列等清理动作判空且不用 `ESP_ERROR_CHECK` 强校验，删除后置 `NULL`。

## 总结

本实验在实验 15 的综合项目之上，新增了一个"联网时钟"子应用，把"从互联网校准时间"做成一个可复用、可随进随校的公共能力：抽出 `app_ntp_sync_once()`（互斥保护的一次性 SNTP 对时 + 东八区）和 `app_wifi_ensure_connected()`（按需联网），开机自动校时和时钟应用共用同一套逻辑。通过这个应用，你能进一步体会嵌入式产品里几个典型工程实践：**没有 RTC 时如何用网络时间保持时钟准确**、**阻塞的联网/校时为什么要放到独立 FreeRTOS 任务**、**后台任务如何安全地跨线程更新 LVGL 界面（加锁 + 前台标志）**，以及**多个功能如何复用同一段带互斥的公共代码而不互相冲突**。

建议的学习方式：先跑通整个项目，进时钟应用观察"联网 → 校准 → 走动"的完整过程；再断开 WiFi 或清掉凭据，看它如何退化为"显示上次时间（未校准）"；最后对照 `app_clock.c` 与 `app_ui.c` 里的 `app_ntp_sync_once/app_wifi_ensure_connected`，理解公共接口是如何被开机流程和子应用共同复用的。你也可以在此基础上继续扩展，例如做成模拟指针表盘、增加世界时钟/闹钟，或把校准改成后台周期性自动进行。
