# 立创·实战派 ESP32-S3 开发板例程

本仓库是立创·实战派 ESP32-S3 开发板的配套例程合集，共 14 个由浅入深的实验，覆盖 GPIO、传感器、音频、屏幕、摄像头、WiFi、蓝牙、离线语音识别和端侧 AI 等主题。每个实验都是独立的 ESP-IDF 工程，配有面向初学者的中文 README，讲解原理、接线、代码和实验现象。

## 开发板简介

立创·实战派 ESP32-S3 是一款功能丰富的 ESP32-S3 学习开发板，板载资源包括：

| 资源 | 型号/规格 | 接口 |
|------|-----------|------|
| 主控 | ESP32-S3（双核 Xtensa LX7，主频最高 240MHz） | — |
| Flash | 16MB | — |
| PSRAM | 八线（Octal）PSRAM，80MHz | — |
| LCD 屏幕 | ST7789，320×240，SPI 接口 | SPI3 |
| 触摸屏 | FT5x06 电容触摸 | I2C |
| 姿态传感器 | QMI8658 六轴（加速度+陀螺仪） | I2C |
| 音频 ADC | ES7210 四通道（录音，4 麦克风） | I2S + I2C |
| 音频 DAC | ES8311（放音） | I2S + I2C |
| 功放 | 板载功放 + 扬声器 | 经 PCA9557 使能 |
| 摄像头 | GC0308（VGA，DVP 接口） | DVP |
| IO 扩展 | PCA9557（控制 LCD_CS / PA_EN / DVP_PWDN） | I2C |
| SD 卡槽 | MicroSD（TF）卡 | SDMMC 1 线 |
| 按键 | BOOT 按键 | GPIO0 |
| 无线 | WiFi 2.4GHz + BLE 5.0（芯片内置） | — |

### 板载 I2C 器件地址

开发板的 I2C0 总线（SDA=GPIO1，SCL=GPIO2，100kHz）上挂载了多个器件：

| 器件 | I2C 地址 |
|------|---------|
| PCA9557（IO 扩展） | 0x19 |
| QMI8658（姿态） | 0x6A |
| ES7210（音频 ADC） | 0x41 |
| FT5x06（触摸） | 0x38 |

### 主要引脚定义

| 功能 | 信号 | GPIO |
|------|------|------|
| BOOT 按键 | — | GPIO0 |
| I2C | SDA / SCL | GPIO1 / GPIO2 |
| LCD SPI | MOSI / CLK / DC | GPIO40 / GPIO41 / GPIO39 |
| LCD 背光 | BL（LEDC PWM） | GPIO42 |
| 音频 I2S | MCLK / BCLK / WS | GPIO38 / GPIO14 / GPIO13 |
| 音频 I2S | DOUT（放音） / DIN（录音） | GPIO45 / GPIO12 |
| SD 卡 SDMMC | CLK / CMD / DAT0 | GPIO47 / GPIO48 / GPIO21 |
| 摄像头 DVP | XCLK / VSYNC / HREF / PCLK | GPIO5 / GPIO3 / GPIO46 / GPIO7 |
| 摄像头 DVP | D0~D7 | GPIO16 / 18 / 8 / 17 / 15 / 6 / 4 / 9 |

> LCD 片选（CS）、功放使能（PA_EN）、摄像头电源（PWDN）通过 PCA9557 IO 扩展芯片控制，不直接占用 ESP32-S3 的 GPIO。

## 开发环境

- **ESP-IDF**：v5.x（建议 v5.0 及以上，部分实验依赖托管组件要求 IDF ≥ 5.0）
- **目标芯片**：ESP32-S3

安装 ESP-IDF 请参考乐鑫官方文档：https://docs.espressif.com/projects/esp-idf/zh_CN/latest/esp32s3/get-started/

### 通用编译烧录流程

每个实验都是独立工程，进入对应目录后执行：

```bash
cd 01-boot_key          # 进入某个实验目录
idf.py set-target esp32s3   # 首次编译需设置目标芯片
idf.py build            # 编译
idf.py flash monitor    # 烧录并打开串口监视器
```

按 `Ctrl + ]` 退出串口监视器。

> 提示：各实验的 `sdkconfig.defaults` 已预设好 Flash 大小（16MB）、PSRAM、分区表等配置，一般无需手动修改。含托管组件的实验首次编译时会自动联网下载依赖。

## 学习路线

建议按编号顺序学习，14 个实验可分为 6 个阶段循序渐进：

### 阶段一：GPIO 与传感器入门

打基础，理解 GPIO、中断、I2C 通信和传感器数据解算。

- **实验 01 · BOOT 按键中断** — GPIO 中断 + FreeRTOS 队列的经典入门
- **实验 02 · QMI8658 姿态传感器** — I2C 通信 + 加速度计测倾角

### 阶段二：存储与音频

学习 SD 卡文件系统、I2S 音频的录音与放音。

- **实验 03 · MicroSD 卡读写** — SDMMC 外设 + FAT 文件系统 + 中文文件名
- **实验 04 · ES7210 音频录制** — I2S 采集 + 麦克风录音到 WAV 文件
- **实验 05 · ES8311 音频播放** — I2S 输出 + PCM 音乐播放到扬声器

### 阶段三：屏幕显示

从裸屏驱动到图形界面，逐步掌握显示技术。

- **实验 06 · LCD 屏幕显示** — esp_lcd 组件 + SPI 屏 + PSRAM 图片显示
- **实验 07 · 摄像头实时显示** — DVP 摄像头采集 + 双任务流水线刷屏
- **实验 08 · LVGL 图形界面** — LVGL 图形库 + 触摸屏交互

### 阶段四：无线通信

让开发板接入网络，实现 WiFi 配网和蓝牙外设。

- **实验 09 · WiFi 扫描与连接** — WiFi STA 模式 + 触摸式配网界面
- **实验 10 · BLE HID 蓝牙设备** — BLE 外设 + 蓝牙音量遥控器

### 阶段五：智能交互与端侧 AI

体验离线语音识别和端侧人工智能。

- **实验 11 · MP3 音乐播放器** — SPIFFS + MP3 解码 + 触摸播放器界面
- **实验 12 · 离线语音识别** — esp-sr 唤醒词 + 中文命令词控制播放器
- **实验 13 · 摄像头人脸检测** — esp-dl 端侧 AI + 实时人脸框与关键点

### 阶段六：综合实战

融会贯通，把所有知识点整合到一个类智能手机系统中。

- **实验 14 · 手持智能终端** — 6 宫格桌面，集成姿态/音乐/SD卡/摄像头/WiFi时钟/蓝牙

## 实验索引

| 编号 | 实验名称 | 主题 | 关键技术/组件 |
|------|---------|------|--------------|
| [01](01-boot_key/) | BOOT 按键中断 | GPIO 入门 | GPIO 中断、FreeRTOS 队列 |
| [02](02-attitude/) | QMI8658 姿态传感器 | I2C 传感器 | I2C、QMI8658、倾角解算 |
| [03](03-micro_sd/) | MicroSD 卡读写 | 存储 | SDMMC、FatFs、中文文件名 |
| [04](04-audio_es7210/) | ES7210 音频录制 | 音频输入 | I2S TDM、ES7210、WAV 文件 |
| [05](05-audio_es8311/) | ES8311 音频播放 | 音频输出 | I2S、ES8311、PCM、PCA9557 |
| [06](06-lcd/) | LCD 屏幕显示 | 显示 | esp_lcd、ST7789、PSRAM |
| [07](07-lcd_camera/) | 摄像头实时显示 | 摄像头 | esp32-camera、GC0308、DVP |
| [08](08-lcd_lvgl/) | LVGL 图形界面 | GUI | LVGL 8.3、esp_lvgl_port、FT5x06 |
| [09](09-wifi_scan_connect/) | WiFi 扫描与连接 | 无线网络 | WiFi STA、事件组、LVGL |
| [10](10-ble_hid_device/) | BLE HID 蓝牙设备 | 蓝牙 | BLE、Bluedroid、HID Device |
| [11](11-mp3_player/) | MP3 音乐播放器 | 综合音频 | SPIFFS、esp-audio-player、codec_dev |
| [12](12-speech_recognition/) | 离线语音识别 | 语音 AI | esp-sr、WakeNet、MultiNet |
| [13](13-human_face_detection/) | 摄像头人脸检测 | 端侧 AI | esp-dl、两级人脸检测模型 |
| [14](14-handheld/) | 手持智能终端 | 综合实战 | 全部外设整合、多任务桌面系统 |

## 目录结构

```
lc-esp32s3/
├── README.md               # 本文件
├── 01-boot_key/            # 每个实验都是独立 ESP-IDF 工程
├── 02-attitude/
├── ...
└── 14-handheld/
    ├── main/               # 源码
    ├── CMakeLists.txt      # 工程配置
    ├── sdkconfig.defaults  # 默认配置
    ├── partitions.csv      # 分区表（部分实验）
    └── README.md           # 实验说明文档
```

## 常见问题

- **编译时下载托管组件失败**：部分实验（04/05/07~14）依赖 ESP-IDF 托管组件，首次编译需联网下载。若下载失败，检查网络或配置组件仓库镜像。
- **烧录后无输出/无法启动**：确认 `idf.py set-target esp32s3` 已执行，且烧录时选择了正确的串口。
- **屏幕无显示**：多数显示实验依赖 PSRAM，确认 `sdkconfig.defaults` 中 PSRAM 相关配置未被修改。
- **摄像头/音频无声无画**：这些外设的使能脚接在 PCA9557 上，务必保证 I2C 和 PCA9557 初始化成功。

## 说明

- 本仓库例程基于 ESP-IDF v5.x 编写，部分实验使用旧版 I2C 驱动（`driver/i2c.h`），在新版 IDF 中会有 deprecated 警告，但不影响编译运行。
- 各实验的详细原理、接线、代码讲解和注意事项，请参见对应目录下的 README.md。
- 建议初学者从实验 01 开始，按顺序学习，动手修改代码观察效果，逐步掌握 ESP32-S3 的开发。
