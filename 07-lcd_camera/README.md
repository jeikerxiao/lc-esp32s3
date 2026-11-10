# 实验 07：摄像头实时显示到 LCD（数码相机取景器）

## 简介

本实验驱动板载的 DVP 摄像头（GC0308），以 QVGA（320×240）、RGB565 格式采集画面，通过双帧缓冲和 FreeRTOS 队列把每一帧实时刷新到 ST7789 LCD 上，实现类似"数码相机取景器"的效果。这是学习摄像头采集与 LCD 显示协同工作的进阶实验。

## 原理

### DVP 摄像头接口

DVP（Digital Video Port）是并行数字视频接口，用一组数据线（D0~D7）同时传输 8 位像素数据，配合 XCLK（输入时钟）、PCLK（像素时钟）、VSYNC（场同步）、HREF（行同步）等控制信号工作。相比串行的 SPI 摄像头，DVP 传输速度快，适合实时视频。

### esp32-camera 组件

ESP-IDF 生态提供了 `espressif/esp32-camera` 组件来驱动摄像头。它负责：
- 配置摄像头传感器（分辨率、像素格式、帧缓冲等）
- 通过 DMA 把采集到的图像帧存入帧缓冲（frame buffer）
- 提供 `esp_camera_fb_get()` 取帧、`esp_camera_fb_return()` 还帧的接口

### 双缓冲与生产者-消费者模型

为了让摄像头采集和 LCD 刷新同时进行、互不阻塞，本实验采用了经典的**生产者-消费者**模型：

- **生产者任务**（摄像头）：不断 `esp_camera_fb_get()` 取帧，通过队列把帧指针发出去。
- **消费者任务**（LCD）：从队列收帧，`esp_lcd_panel_draw_bitmap()` 刷屏，用完 `esp_camera_fb_return()` 归还。

两个任务分别绑定到 ESP32-S3 的两个 CPU 核心（Core 0 和 Core 1），实现真正的并行，避免互相抢占导致掉帧。

### RGB565 直出

摄像头配置为 `PIXFORMAT_RGB565`，与 LCD 的 16bit 像素格式直接兼容，无需做格式转换，效率最高。

## 硬件接线

摄像头和 LCD 均焊接在开发板上，无需额外接线。摄像头 DVP 引脚如下：

| 信号 | GPIO | 信号 | GPIO |
|------|------|------|------|
| XCLK | GPIO5 | D0 | GPIO16 |
| SIOD（SCCB SDA） | GPIO1 | D1 | GPIO18 |
| SIOC（SCCB SCL） | GPIO2 | D2 | GPIO8 |
| VSYNC | GPIO3 | D3 | GPIO17 |
| HREF | GPIO46 | D4 | GPIO15 |
| PCLK | GPIO7 | D5 | GPIO6 |
| | | D6 | GPIO4 |
| | | D7 | GPIO9 |

摄像头 XCLK 频率：24MHz。

LCD 引脚与实验 06 相同（MOSI=GPIO40、CLK=GPIO41、DC=GPIO39、背光=GPIO42、CS 经 PCA9557）。

> 关键点 1：摄像头的 SCCB（摄像头专用 I2C）复用了开发板的 I2C0 总线（GPIO1/GPIO2），所以必须先 `bsp_i2c_init()`。代码中 `pin_sccb_sda = -1` + `sccb_i2c_port = 0` 就表示"用已经初始化好的 I2C0"。
>
> 关键点 2：摄像头的电源控制脚 PWDN 接在 PCA9557 的 P2 上，默认上电时是掉电状态（PWDN=1），需要调用 `dvp_pwdn(0)` 给摄像头上电后才能初始化。

## 代码讲解

工程目录结构：

```
07-lcd_camera/
├── CMakeLists.txt
├── sdkconfig.defaults
└── main/
    ├── CMakeLists.txt
    ├── esp32_s3_szp.c      # I2C/PCA9557/LCD/Camera 驱动
    ├── esp32_s3_szp.h
    ├── idf_component.yml   # 依赖 espressif/esp32-camera
    ├── logo_en_240x240_lcd.h
    ├── yingwu.h
    └── main.c
```

### 托管组件

`main/idf_component.yml`：

```yaml
dependencies:
  espressif/esp32-camera: "^2.0.10"
  idf: ">=4.1.0"
```

### 关键流程

`app_main()`：

1. `bsp_i2c_init()` → `pca9557_init()` → `bsp_lcd_init()`（与实验 06 相同，刷黑底）。
2. `lcd_draw_pictrue(...)` 先显示一张鹦鹉开机图。
3. `vTaskDelay(500ms)` 让用户看清开机图。
4. `bsp_camera_init()`：给摄像头上电（`dvp_pwdn(0)`），配置为 RGB565、QVGA、双帧缓冲（存 PSRAM），若检测到 GC0308 则开启镜像。
5. `app_camera_lcd()`：启动双任务流水线。

### 摄像头配置

`bsp_camera_init()` 中的关键参数：

- `pixel_format = PIXFORMAT_RGB565`
- `frame_size = FRAMESIZE_QVGA`（320×240，正好铺满屏幕）
- `fb_count = 2`（双缓冲）
- `fb_location = CAMERA_FB_IN_PSRAM`（帧缓冲放 PSRAM）
- `grab_mode = CAMERA_GRAB_WHEN_EMPTY`（帧缓冲空时才抓帧，省带宽）

### 双任务流水线

`app_camera_lcd()`：

1. 创建队列 `xQueueLCDFrame`（深度 2，元素为帧指针 `camera_fb_t*`）。
2. **摄像头任务**（绑定 Core 1）：循环 `esp_camera_fb_get()` → `xQueueSend()`。
3. **LCD 任务**（绑定 Core 0）：`xQueueReceive()` → `esp_lcd_panel_draw_bitmap()` → `esp_camera_fb_return()`。

## 实验效果

编译烧录后：

- 屏幕先显示约 0.5 秒的鹦鹉开机图，随后切换为摄像头实时取景画面（320×240 横屏，画面镜像）。
- 移动开发板或对着摄像头挥手，屏幕画面实时跟随。
- 串口打印背光信息；若摄像头初始化失败会打印 `Camera init failed with error 0x%x`。

### 编译与烧录

```bash
idf.py set-target esp32s3
idf.py build flash monitor
```

## 注意事项

1. **帧缓冲必须在 PSRAM**：双缓冲 × QVGA RGB565 = 2 × 150KB = 300KB，远超内部 RAM，不开 PSRAM 摄像头初始化必然失败。
2. **初始化顺序不能乱**：必须先 I2C → PCA9557 → LCD → 摄像头。摄像头 SCCB 依赖 I2C0，PWDN 依赖 PCA9557。
3. **帧用完必须归还**：调用 `esp_camera_fb_get()` 取到的帧，用完后必须 `esp_camera_fb_return()` 归还，否则 2 个帧缓冲耗尽后取帧会永久阻塞。
4. **双核绑定**：摄像头任务和 LCD 任务分别绑定 Core 1 和 Core 0，修改优先级或绑核可能引起掉帧。
5. **cache 配置敏感**：`sdkconfig.defaults` 中开启了指令 cache 32KB、数据 cache 64KB 等配置，这是 esp32-camera 在 S3 上把帧缓冲放 PSRAM 时的推荐配置，不要随意关闭。
6. **颜色顺序**：GC0308 输出的 RGB565 字节序与 ST7789 一致才能直刷。如果换用其他型号摄像头（如 OV2640），可能需要处理颜色字节序。

## 总结

本实验把实验 06 的 LCD 显示和摄像头采集结合起来，通过双帧缓冲 + 队列 + 双核任务的生产者-消费者模型，实现了流畅的实时取景。理解这个架构后，你就可以在此基础上做图像识别、视频录制、二维码扫描等应用。下一个实验（08）会在屏幕上引入更强大的 LVGL 图形库，而实验 13 则会在摄像头画面上叠加 AI 人脸检测。
