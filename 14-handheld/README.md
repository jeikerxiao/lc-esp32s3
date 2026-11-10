# 实验 14：手持智能终端（综合项目）

## 简介

本实验是整个例程的集大成者——一个模拟"智能手机/掌机"的综合桌面固件。开机时有 logo 旋转动画和音效，随后进入 6 宫格主界面，集成了 6 个子应用：

1. **运动监测**：读取 QMI8658 姿态传感器，显示倾角并检测运动/静止/剧烈运动状态
2. **音乐播放器**：播放 SPIFFS 中的 MP3 歌曲
3. **SD 卡文件浏览器**：浏览 TF 卡中的文件目录
4. **摄像头取景**：实时显示摄像头画面
5. **WiFi 设置 + 时钟**：连接 WiFi 后通过 NTP 显示实时时钟
6. **蓝牙控制器**：BLE HID 音量遥控器

这个实验把前面所有实验的知识点融合到一个项目中，是检验学习成果的综合实战。

## 原理

### 综合架构

本项目在实验 08（LVGL 界面）的框架上，把前面各实验的功能模块化整合：

| 子应用 | 复用的实验 | 核心技术 |
|--------|-----------|---------|
| 运动监测 | 实验 02 | QMI8658 姿态传感器 + 运动检测 |
| 音乐播放器 | 实验 11 | SPIFFS + MP3 解码 + ES8311 |
| SD 卡浏览 | 实验 03 | SDMMC + FAT 文件系统 |
| 摄像头取景 | 实验 07 | DVP 摄像头 + esp32-camera |
| WiFi 时钟 | 实验 09 | WiFi STA + SNTP |
| 蓝牙控制器 | 实验 10 | BLE HID Device |

### 应用切换机制

主界面用全局变量 `icon_flag` 标记当前所在应用（0=主界面，1~6=各应用）。点击图标进入对应子应用，子应用返回时把 `icon_flag` 置回 0。每个子应用在进入时初始化对应硬件、创建界面和任务，退出时反初始化并释放资源。

### 开机流程

```
NVS 初始化 → 硬件初始化(I2C/PCA9557/LVGL/SPIFFS/codec)
   → logo 旋转动画 + 开机音效(并行)
   → 音效播完置事件位 → 进入 6 宫格主界面
   → 每 5 秒打印内存使用情况
```

### 运动检测（QMI8658 高级功能）

与实验 02 只读倾角不同，本实验还启用了 QMI8658 的**运动检测中断功能**：通过配置 CTRL8/CTRL9 寄存器使能 Any-Motion（任意运动）、No-Motion（静止）、Significant-Motion（剧烈运动）检测，读取 STATUS1 寄存器判断当前状态，在界面标题栏显示"运动或震动/静止/剧烈运动"。

### NTP 网络时钟

WiFi 连接成功后，用 SNTP（简单网络时间协议）从 `cn.pool.ntp.org` 同步时间，设置东八区时区（`CST-8`），在主界面左上角显示日期和时间，每秒刷新。

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

工程目录结构：

```
14-handheld/
├── CMakeLists.txt
├── partitions.csv          # factory 8M + storage(spiffs) 3M
├── sdkconfig.defaults      # WiFi + BT + PSRAM + LVGL 全开
├── spiffs/                 # 3 首 MP3
└── main/
    ├── assets/             # 图标、logo、中文字体
    │   ├── image_lckfb_logo.c
    │   ├── font_alipuhui20.c
    │   └── img_*_icon.c    # 6 个应用图标
    ├── bt/                 # BLE HID 源码
    │   ├── ble_hidd_demo.c/h
    │   ├── esp_hidd_prf_api.c/h
    │   ├── hid_dev.c/h
    │   ├── hid_device_le_prf.c
    │   └── hidd_le_prf_int.h
    ├── app_ui.c            # 主界面 + 6 个子应用逻辑
    ├── app_ui.h
    ├── esp32_s3_szp.c      # BSP（含全部外设驱动 + 开机音效任务）
    ├── esp32_s3_szp.h
    ├── sword.pcm           # 开机音效（拔剑声，嵌入固件）
    ├── idf_component.yml
    └── main.c
```

### 托管组件

`main/idf_component.yml` 声明了 esp32-camera、LVGL、esp_lvgl_port、ft5x06、audio-player、file-iterator、codec_dev、esp-sr 等组件。

> 注意：虽然声明了 esp-sr，但本项目代码中并未使用语音识别（无 app_sr.c、无 model 分区），这是从实验 12 拷贝模板时的残留，可忽略。

### 关键流程

`app_main()`（main.c）：

1. `nvs_flash_init()`：BLE 绑定信息需要 NVS。
2. 初始化 I2C / PCA9557 / LVGL / SPIFFS / codec。
3. `lv_gui_start()`：显示开机 logo 旋转动画（以中心为轴旋转 5 圈）。
4. 创建事件组、`power_music_task`（Core 1，播放开机音效）和 `main_page_task`（Core 0）。
5. `app_main` 自身每 5 秒打印内部 DRAM 和 PSRAM 的内存使用情况。

### 开机音效任务

`power_music_task`：把嵌入固件的 `sword.pcm`（拔剑音效）通过 I2S 直写播放，播完置事件位 `START_MUSIC_COMPLETED`。`main_page_task` 等到该事件位后才进入主界面——保证开机音效播完再显示主菜单。

### 主界面

`lv_main_page()`：黑底 + 蓝绿渐变面板，左上角滚动欢迎语"欢迎使用立创实战派开发板"，6 个 80×80 圆角图标按钮（两行三列），每个图标对应一个子应用的事件处理函数。

### 六个子应用

- **运动监测**（`att_event_handler`）：创建姿态显示页面，初始化 QMI8658（失败重试 3 次，UI 提示错误），用 `lv_timer` 每 200ms 更新倾角和运动状态。退出时关闭传感器。
- **音乐播放器**（`music_event_handler`）：进入时才初始化播放器，界面与实验 11 一致，退出时删除播放器。
- **SD 卡浏览**（`sdcard_event_handler`）：挂载 SD 卡，递归列目录，按扩展名显示不同图标（音频/视频/图片/目录/文件），可进入子目录，退到根目录时卸载。
- **摄像头取景**（`camera_event_handler`）：初始化摄像头，循环取帧刷新到 `lv_img`，退出时反初始化并给摄像头掉电。
- **WiFi 设置**（`wifiset_event_handler`）：扫描热点（最多 5 个），滚轮输入密码连接，成功后启动 NTP 时钟任务。
- **蓝牙控制器**（`btset_event_handler`）：创建音量加减按钮，启动 BLE HID（与实验 10 相同），退出时反初始化蓝牙。

## 实验效果

编译烧录后：

- **开机**：LCD 显示立创实战派 logo 旋转动画（约 1 秒），同时扬声器播放"拔剑"开机音效。串口每 5 秒打印内存统计。
- **主界面**：蓝绿渐变背景、滚动欢迎语、6 个彩色应用图标，全部触摸操作。
- **运动监测**：显示 X/Y/Z 三轴倾角和进度条，晃动/静置开发板时标题显示对应运动状态。
- **音乐播放器**：触摸播放 SPIFFS 中的歌曲，可切歌、调音量。
- **SD 卡浏览**：显示 SD 卡容量和文件树，可进入目录浏览（插入 TF 卡后生效）。
- **摄像头取景**：LCD 实时显示摄像头画面，点返回退出。
- **WiFi 时钟**：连接 WiFi 后主界面左上角显示实时日期时间。
- **蓝牙控制器**：手机/电脑搜索"HID"设备配对后，按住屏幕按钮调节主机音量。

### 编译与烧录

```bash
idf.py set-target esp32s3
idf.py build flash monitor
```

## 注意事项

1. **分区较大**：factory 分区 8M（含 WiFi+BT+LVGL+摄像头），storage 分区 3M（3 首 MP3）。烧录时需一并写入 SPIFFS 镜像。
2. **WiFi 与蓝牙共存抢资源**：两者同时开启会占用大量内存和射频，代码通过 `SPIRAM_TRY_ALLOCATE_WIFI_LWIP`、`SPIRAM_MALLOC_ALWAYSINTERNAL=2048` 和缩减 WiFi 缓冲来腾空间。
3. **NVS 必须初始化**：BLE 配对绑定信息存在 NVS，NVS 损坏时代码会擦除重建。
4. **开机音效播完才进主界面**：靠事件位同步，如果音效写入失败会 `abort()`。
5. **音乐采样率固定**：`bsp_codec_set_fs` 被注释掉，假定 SPIFFS 内 MP3 固定 16000Hz，换其他采样率的歌曲需打开该注释。
6. **SD 卡挂载失败不格式化**：`format_if_mount_failed = false`，只提示不格式化，插好 FAT32 格式的 TF 卡才有内容。
7. **LVGL 跨任务加锁**：所有子应用在任务里更新界面都必须用 `lvgl_port_lock/unlock` 包裹。
8. **iPhone 蓝牙报错可忽略**：BLE HID 配对时 iPhone 可能出现 `GATT_INSUF_ENCRYPTION`，属正常现象。
9. **子应用资源要配对释放**：进入时初始化、退出时反初始化（尤其摄像头、蓝牙），反复进出时注意配对，避免内存泄漏。

## 总结

本实验是一个综合性实战项目，把前面 13 个实验的知识点（GPIO、姿态传感器、SD 卡、音频录放、LCD、摄像头、LVGL、WiFi、BLE、语音识别框架）整合到一个类智能手机的桌面系统中。通过这个项目，你能完整体会到一个嵌入式产品从硬件驱动、中间件到应用界面的分层架构，以及多任务、多外设协同工作的资源管理技巧。

建议的学习方式：先跑通整个项目，再逐个进入子应用，对照前面独立的实验理解每个模块的实现，最后尝试自己添加一个新应用（如天气显示、小游戏、传感器数据记录仪等），真正把所学知识融会贯通。
