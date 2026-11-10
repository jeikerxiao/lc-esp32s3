# 实验 08：LVGL 图形界面与触摸

## 简介

本实验在 ST7789 LCD 上运行 LVGL（Light and Versatile Graphics Library）图形库，并接入 FT5x06 电容触摸屏，默认运行 LVGL 官方的性能测试演示 `lv_demo_benchmark()`。这是学习嵌入式 GUI 开发的关键实验，后续多个实验（WiFi 连接、MP3 播放器、语音识别、综合项目）都建立在此基础之上。

## 原理

### LVGL 图形库

LVGL 是一个开源的嵌入式图形库，提供了丰富的控件（按钮、滑条、列表、图表等）、动画、主题和演示程序。它不直接操作硬件，而是通过"显示驱动"和"输入驱动"两个抽象接口与底层对接。

本实验使用 **LVGL v8.3** 版本（注意：v8 与 v9 的 API 有较大差异，例如 v8 用 `lv_disp_t`，v9 改名为 `lv_display_t`）。

### esp_lvgl_port 适配层

乐鑫提供了 `espressif/esp_lvgl_port` 组件，作为 LVGL 与 ESP-IDF `esp_lcd` 驱动之间的适配层。它负责：
- 创建 LVGL 的 tick、任务、锁
- 把 esp_lcd 面板注册为 LVGL 显示设备
- 把触摸屏注册为 LVGL 输入设备

有了它，你只需调用 `lvgl_port_init()`、`lvgl_port_add_disp()`、`lvgl_port_add_touch()` 就能让 LVGL 跑起来。

### 触摸屏（FT5x06）

FT5x06 是一款电容触摸控制器，通过 I2C 接口报告触摸坐标。本实验中它挂在开发板的 I2C0 总线上（与 PCA9557、QMI8658 等共用），默认 I2C 地址 `0x38`。

触摸坐标的方向必须与 LCD 面板的旋转/镜像设置保持一致，否则会出现"点的地方和响应的地方对不上"的错位问题。

### 绘制缓冲区

LVGL 不会一次性把整屏画完，而是用一块较小的"绘制缓冲区"分批渲染再刷屏。本实验缓冲区为 320×20 行，并放在 PSRAM 中（`buff_spiram = true`）。

> 注意：`buff_dma`（DMA 内部 RAM 缓冲）和 `buff_spiram`（PSRAM 缓冲）不能同时为 true。本例选择 PSRAM 缓冲，好处是不占用宝贵的内部 RAM，代价是速度略低于 DMA 方案。

## 硬件接线

LCD 和触摸屏均焊接在开发板上，无需额外接线。引脚与实验 06 相同：

| 信号 | GPIO | 说明 |
|------|------|------|
| LCD SPI MOSI | GPIO40 | 数据线 |
| LCD SPI CLK | GPIO41 | 时钟线 |
| LCD DC | GPIO39 | 命令/数据选择 |
| LCD 背光 | GPIO42 | LEDC PWM |
| LCD CS | 经 PCA9557 P0 | 片选 |
| 触摸 SDA/SCL | GPIO1/GPIO2 | 复用 I2C0 |

触摸屏无独立复位/中断脚（均为 GPIO_NUM_NC）。

## 代码讲解

工程目录结构：

```
08-lcd_lvgl/
├── CMakeLists.txt
├── sdkconfig.defaults      # PSRAM + LVGL 配置
└── main/
    ├── CMakeLists.txt
    ├── esp32_s3_szp.c      # I2C/PCA9557/LCD/Touch/LVGL 启动
    ├── esp32_s3_szp.h
    ├── idf_component.yml   # LVGL + esp_lvgl_port + ft5x06
    ├── logo_en_240x240_lcd.h
    ├── yingwu.h
    └── main.c
```

### 托管组件

`main/idf_component.yml`：

```yaml
dependencies:
  lvgl/lvgl: "~8.3.0"
  espressif/esp_lvgl_port: "~1.4.0"
  espressif/esp_lcd_touch_ft5x06: "~1.0.6"
  # espressif/esp32-camera 被注释掉（本实验不用摄像头）
```

### 关键流程

`app_main()`：

1. `bsp_i2c_init()` → `pca9557_init()`。
2. `bsp_lvgl_start()`：启动 LVGL 并初始化显示 + 触摸。
3. 运行演示程序 `lv_demo_benchmark()`。

### LVGL 启动流程

`bsp_lvgl_start()` 内部：

1. `lvgl_port_init()`：初始化 LVGL 任务、tick、锁。
2. `bsp_display_lcd_init()`：初始化 ST7789 面板（同实验 06），刷白底，然后用 `lvgl_port_add_disp()` 注册为 LVGL 显示设备（缓冲区 320×20，单缓冲，PSRAM）。
3. `bsp_display_indev_init()`：初始化 FT5x06 触摸屏，用 `lvgl_port_add_touch()` 注册为 LVGL 输入设备。
4. `bsp_display_backlight_on()`：打开背光。

### 屏幕方向一致性

代码中有重要注释：**LVGL 的 rotation 配置必须与 LCD 面板的 swap_xy/mirror 设置一致**，触摸的方向标志也要一致，否则触摸坐标会错位。本实验中三者都设为 `swap_xy = true, mirror_x = true, mirror_y = false`。

### 演示程序

`main.c` 最后运行演示，代码中列出了 5 个可选 demo，但**一次只能打开一个**（当前启用 benchmark）：

```c
lv_demo_benchmark();     // 性能测试（当前启用）
// lv_demo_widgets();    // 控件展示
// lv_demo_music();      // 音乐播放器界面
// lv_demo_keypad_encoder();
// lv_demo_stress();     // 压力测试
```

## 实验效果

编译烧录后：

- 白底屏幕上运行 LVGL Benchmark 性能测试，显示各种渲染场景的动画，最终给出 FPS（帧率）和 CPU 占用率统计。
- 触摸屏已就绪（benchmark 本身交互较少；若切换到 `lv_demo_widgets()` 或 `lv_demo_music()` 可充分体验触摸操作）。
- 串口打印背光信息及 LVGL/benchmark 的输出。

### 编译与烧录

```bash
idf.py set-target esp32s3
idf.py build flash monitor
```

## 注意事项

1. **LVGL 版本锁定 v8.3**：`~8.3.0` 表示只用 8.3.x。LVGL v9 的 API 有大量改动，若升级到 v9 本例程会编译失败，网上查资料时也要注意区分版本。
2. **颜色字节交换**：`CONFIG_LV_COLOR_16_SWAP=y` 必须开启，否则 SPI 屏颜色会红蓝颠倒/花屏。
3. **一次只运行一个 demo**：多个 demo 同时开启会因内存不足或界面冲突而出错。想换 demo，把当前的注释掉、取消目标 demo 的注释即可。
4. **buff_dma 与 buff_spiram 互斥**：两者不能同时为 true。本例用 PSRAM 缓冲。
5. **跨任务操作 LVGL 要加锁**：如果你在自己的任务里修改 LVGL 控件，必须用 `lvgl_port_lock(0)` / `lvgl_port_unlock()` 包裹，否则会与 LVGL 任务冲突导致花屏或崩溃。
6. **摄像头功能被关闭**：头文件中 `CAMERA_EN = 0`，`idf_component.yml` 里 esp32-camera 也被注释。若要启用需同时改这两处。
7. **PSRAM 必开**：LVGL 缓冲和字体都依赖 PSRAM。

## 总结

本实验把实验 06 的裸屏显示升级为功能完整的 LVGL 图形界面，并加入了触摸交互。核心知识点是 esp_lvgl_port 适配层的使用、显示/输入设备的注册，以及屏幕方向在 LCD、LVGL、触摸三处必须保持一致。掌握 LVGL 后，你就能开发出漂亮的图形界面——后续的 WiFi 配网、音乐播放器、语音识别、综合手持设备实验，界面部分都是基于本实验的 LVGL 框架构建的。建议动手改一改 demo，或用 LVGL 的几个基础控件（按钮、标签、滑条）做一个自己的小界面练手。
