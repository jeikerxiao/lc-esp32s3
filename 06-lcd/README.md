# 实验 06：LCD 屏幕显示图片

## 简介

本实验驱动板载的 ST7789 SPI 接口 LCD（分辨率 320×240），先把屏幕刷成黑色，再全屏显示一张"三只鹦鹉"的彩色图片。这是学习 ESP32-S3 驱动 SPI 屏幕、理解 esp_lcd 组件的入门实验，也是后续 LVGL 图形界面和摄像头显示的基础。

## 原理

### ST7789 LCD 与 SPI 接口

ST7789 是一款常见的 TFT LCD 控制器，支持 SPI 接口。本实验通过 ESP32-S3 的 SPI3 外设向屏幕发送命令和像素数据，像素格式为 RGB565（每个像素 2 字节，红 5 位、绿 6 位、蓝 5 位）。

### esp_lcd 组件

ESP-IDF 提供了统一的 `esp_lcd` 组件来驱动各种屏幕，主要分三层：
1. **SPI 总线**（`spi_bus_initialize`）：配置 SPI 引脚和时钟。
2. **Panel IO**（`esp_lcd_new_panel_io_spi`）：封装对屏幕的命令/数据读写。
3. **Panel**（`esp_lcd_new_panel_st7789`）：屏幕控制器驱动，提供复位、初始化、画图、开关显示等接口。

### 屏幕方向与颜色

ST7789 面板初始化后需要额外设置：
- `esp_lcd_panel_invert_color(true)`：反色。**ST7789 必须反色**，否则颜色异常。
- `esp_lcd_panel_swap_xy(true)` + `esp_lcd_panel_mirror(true, false)`：把屏幕旋转为横屏（320 宽 × 240 高）。

### 背光 PWM 调光

屏幕背光通过 LEDC（PWM）控制亮度。本实验用 LEDC 通道 0、5kHz、10bit 分辨率，并设置 `output_invert = true`（PWM 反相输出）——这意味着占空比逻辑与直觉相反，如果删掉这个标志，背光会变为最暗。

## 硬件接线

LCD 焊接在开发板上，无需额外接线。相关引脚如下：

| 信号 | GPIO | 说明 |
|------|------|------|
| LCD SPI MOSI | GPIO40 | 数据线 |
| LCD SPI CLK | GPIO41 | 时钟线 |
| LCD DC | GPIO39 | 命令/数据选择 |
| LCD 背光 | GPIO42 | LEDC PWM 调光 |
| LCD CS | 经 PCA9557 | 片选，由 IO 扩展芯片 P0 控制（非 ESP32 GPIO） |
| LCD RST | 无 | 无硬件复位脚（GPIO_NUM_NC） |

SPI 配置：`SPI3_HOST`，像素时钟 80MHz，SPI mode 2，DMA 自动通道。

> 关键点：LCD 的片选 CS 不在 ESP32-S3 的 GPIO 上，而是接在 I2C IO 扩展芯片 PCA9557 的 P0 脚。所以必须先初始化 I2C 和 PCA9557，再把 CS 拉低。

## 代码讲解

工程目录结构：

```
06-lcd/
├── CMakeLists.txt
├── sdkconfig.defaults      # 开启八线 PSRAM
└── main/
    ├── CMakeLists.txt
    ├── esp32_s3_szp.c      # I2C/PCA9557/LCD/背光驱动
    ├── esp32_s3_szp.h      # 引脚宏定义
    ├── logo_en_240x240_lcd.h  # 乐鑫 logo 图片数组（本例未启用）
    ├── yingwu.h            # "三只鹦鹉"图片数组（320×240 RGB565）
    └── main.c
```

### 关键流程

`main.c` 的 `app_main()` 只有几行：

1. `bsp_i2c_init()`：初始化 I2C 总线。
2. `pca9557_init()`：初始化 IO 扩展芯片。
3. `bsp_lcd_init()`：初始化背光 PWM、SPI 总线、Panel IO、ST7789 面板，刷黑屏，开显示，背光设为 100%。
4. `lcd_draw_pictrue(0, 0, 320, 240, gImage_yingwu)`：全屏显示鹦鹉图片。

### 图片显示函数

`lcd_draw_pictrue()` 的做法：

1. 用 `heap_caps_malloc(..., MALLOC_CAP_SPIRAM)` 在 **PSRAM** 中分配一块图像缓冲区。
2. `memcpy` 把图片数组数据拷贝到缓冲区。
3. `esp_lcd_panel_draw_bitmap()` 把缓冲区刷到屏幕指定区域。
4. 释放缓冲区。

为什么要用 PSRAM？一帧 320×240 的 RGB565 图像需要 `320 × 240 × 2 = 153600` 字节（150KB），超过了 ESP32-S3 内部 RAM 单个可用块的大小，所以必须放到 PSRAM。

### PSRAM 配置（sdkconfig.defaults）

```
CONFIG_SPIRAM=y            # 开启 PSRAM
CONFIG_SPIRAM_MODE_OCT=y   # 八线模式
CONFIG_SPIRAM_SPEED_80M=y  # 80MHz
```

## 实验效果

编译烧录后：

- 屏幕先短暂黑屏，随后全屏显示三只鹦鹉的彩色图片，背光 100% 亮度。
- 串口打印背光设置信息：

```
I (xxx) esp32_s3_szp: Setting LCD backlight: 100%
```

### 编译与烧录

```bash
idf.py set-target esp32s3
idf.py build flash monitor
```

## 注意事项

1. **必须开启 PSRAM**：图片显示函数强制在 SPIRAM 分配内存，不开 PSRAM 会分配失败并打印 `Memory for bitmap is not enough`。
2. **ST7789 必须反色**：`invert_color(true)` 不能省略，否则颜色显示异常。
3. **CS 由 PCA9557 控制**：初始化顺序必须是 I2C → PCA9557 → LCD，且 `lcd_cs()` 通过 I2C 读改写寄存器，速度较慢，不能在高频刷屏时反复调用（本例初始化时拉低后一直保持）。
4. **屏幕方向**：`swap_xy` 和 `mirror` 的组合决定了横屏方向。如果要改变坐标方向，需要理解这几个变换的关系，同时触摸坐标（后续实验）也要保持一致。
5. **背光 PWM 反相**：LEDC 配置了 `output_invert = true`，删除会导致背光最暗。
6. **图片字节序**：图片数组是 `unsigned char` 直接 memcpy，没有做字节序转换，RGB565 数据必须与面板字节序匹配，否则颜色会红蓝颠倒。

## 总结

本实验演示了 ESP32-S3 通过 esp_lcd 组件驱动 SPI 屏幕的完整流程：SPI 总线 → Panel IO → ST7789 面板 → 背光控制 → 图片显示。重点在于理解 esp_lcd 的三层架构、PSRAM 的必要性，以及 CS 引脚经 PCA9557 扩展的特殊设计。掌握这些后，你就可以在屏幕上显示任意图形，为接下来的 LVGL 图形界面和摄像头实时显示做好准备。
