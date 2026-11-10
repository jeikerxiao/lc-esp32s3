# 实验 13：摄像头人脸检测

## 简介

本实验用 DVP 摄像头采集画面，通过 esp-dl 深度学习框架的两级人脸检测模型在本地推理，把检测到的人脸框和 5 个关键点（双眼、鼻子、嘴角）实时画在 LCD 屏幕上。这是 ESP32-S3 端侧 AI（Edge AI）的入门实验，全程离线运行，无需联网。

## 原理

### esp-dl 深度学习框架

`esp-dl` 是乐鑫为 ESP32-S3 设计的端侧深度学习推理框架，支持在 MCU 上运行量化后的神经网络模型。本实验使用的是随仓库携带的 **esp-dl v2.0.0 本地组件**（在 `components/esp-dl` 目录），人脸检测模型已经编译进静态库 `libhuman_face_detect.a` 中，无需单独的模型分区。

> 注意：本实验用的是本地 vendored 的 esp-dl 2.0.0，模型固化在 `.a` 库里。这与乐鑫新版 esp-dl（托管组件 + 独立 model 分区）的用法不兼容，查资料时不要照搬新版文档。

### 两级人脸检测

人脸检测采用两个模型级联，先粗检再精修：

1. **HumanFaceDetectMSR01（一级，粗检）**：在缩小后的图像上快速找出可能包含人脸的候选区域。构造参数 `(0.3F, 0.3F, 10, 0.3F)` 分别表示：分数阈值 0.3、NMS（非极大值抑制）阈值 0.3、最多保留 10 个候选、输入缩放到 30%。
2. **HumanFaceDetectMNP01（二级，精修）**：对一级检出的候选区域做更精确的定位，并输出 5 个关键点。构造参数 `(0.4F, 0.3F, 10)`。

这种"先粗后精"的级联结构在保证速度的同时提高了检测精度。

### 三任务流水线

为了流畅运行，本实验用三个任务 + 两个队列组成流水线（类似实验 07，但中间多了一个 AI 推理环节）：

```
摄像头任务(Core 1) → [队列] → AI推理任务(Core 0) → [队列] → LCD任务(Core 0)
   采集帧              帧指针        检测人脸+画框          刷屏显示
```

- **摄像头任务**：`esp_camera_fb_get()` 取帧，发给 AI 队列。
- **AI 推理任务**：对帧做两级人脸检测，把结果（框和关键点）直接画在帧缓冲上，再转发给 LCD 队列。
- **LCD 任务**：把处理好的帧刷到屏幕，然后归还帧缓冲。

### 检测结果绘制

`who_ai_utils.cpp` 负责在 RGB565 帧上绘制：
- 人脸框：绿色空心矩形
- 5 个关键点（半径 4 的圆点）：左眼红色、右眼蓝色、鼻子绿色、左右嘴角红/蓝色

## 硬件接线

摄像头和 LCD 均焊接在开发板上，无需额外接线。

摄像头 DVP 引脚（与实验 07 相同）：

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

LCD 引脚：MOSI=GPIO40、CLK=GPIO41、DC=GPIO39、背光=GPIO42、CS 经 PCA9557 P0。摄像头 PWDN 经 PCA9557 P2 控制。

## 代码讲解

工程目录结构：

```
13-human_face_detection/
├── CMakeLists.txt
├── components/
│   └── esp-dl/             # 本地 esp-dl v2.0.0 组件（含预编译人脸检测库）
├── partitions.csv          # nvs + phy_init + factory 3M（无 storage/model）
├── sdkconfig.defaults
└── main/
    ├── esp32_s3_szp.c      # I2C/PCA9557/LCD/Camera 驱动
    ├── esp32_s3_szp.h
    ├── idf_component.yml   # espressif/esp32-camera
    ├── logo_en_240x240_lcd.h
    ├── main.cpp            # 主程序（C++）
    ├── who_ai_utils.cpp    # 检测结果绘制
    ├── who_ai_utils.hpp
    ├── who_human_face_detection.cpp  # 三任务流水线 + 人脸检测
    ├── who_human_face_detection.hpp
    └── yingwu.h            # 开机图
```

> 本实验源码为 C++（`.cpp`），因为 esp-dl 是 C++ 库。头文件用 `extern "C"` 包裹以便与 C 代码互操作。

### 托管组件

`main/idf_component.yml`：

```yaml
dependencies:
  espressif/esp32-camera: "^2.0.10"
```

esp-dl 不是托管组件，而是放在 `components/esp-dl` 目录随仓库携带。

### 关键流程

`main.cpp` 的 `app_main()`：

1. `bsp_i2c_init()` → `pca9557_init()` → `bsp_lcd_init()`（刷黑底）。
2. `lcd_draw_pictrue(...)` 显示鹦鹉开机图，延时 500ms。
3. `bsp_camera_init()`：配置摄像头为 RGB565、QVGA、双帧缓冲（PSRAM），GC0308 开镜像。
4. `app_camera_ai_lcd()`：启动三任务流水线。

### AI 推理任务

`task_process_ai`（在 `who_human_face_detection.cpp`）：

1. 构造两个检测器：`HumanFaceDetectMSR01 detector(0.3F, 0.3F, 10, 0.3F)` 和 `HumanFaceDetectMNP01 detector2(0.4F, 0.3F, 10)`。
2. 循环从队列取帧：
   - `detector.infer()` 一级粗检，得到候选框。
   - `detector2.infer()` 二级精修。
   - 有结果时 `draw_detection_result()` 画框和关键点，`print_detection_result()` 串口打印坐标。
   - 把帧转发给 LCD 队列。

## 实验效果

编译烧录后：

- 屏幕先显示约 0.5 秒的鹦鹉开机图，随后切换为摄像头实时画面（QVGA 铺满 320×240 横屏）。
- 人脸进入画面时，被绿色矩形框标出，双眼、鼻子、嘴角被红/绿/蓝小圆点标出，框和点随人脸实时移动。
- 串口打印检测结果，包括人脸框坐标 `(x1, y1, x2, y2)` 和各关键点坐标（left eye / right eye / nose / mouth left / mouth right）。

### 编译与烧录

```bash
idf.py set-target esp32s3
idf.py build flash monitor
```

## 注意事项

1. **esp-dl 版本特殊**：使用的是本地 vendored 的 2.0.0 版，模型固化在 `.a` 库中，不要套用乐鑫新版 esp-dl 文档。
2. **帧率有限**：检测是单帧串行推理，AI 推理在 Core 0 上跑，加上刷屏，帧率不会很高，这是端侧 AI 的正常表现。
3. **小脸/远脸检测能力受限**：MSR01 的 `resize_scale = 0.3` 表示输入缩小到 30% 推理，离摄像头太远或太小的脸可能检测不到。检测参数（分数阈值、NMS、top_k）可调。
4. **依赖 PSRAM**：摄像头帧缓冲和 AI 推理都需要 PSRAM。
5. **初始化顺序**：必须先 I2C → PCA9557 → LCD → 摄像头（`dvp_pwdn(0)` 上电），且摄像头 SCCB 复用 I2C0。
6. **无触摸交互**：本实验纯显示，没有用 LVGL 和触摸，直接用 `esp_lcd_panel_draw_bitmap` 刷屏。
7. **components/esp-dl 体积大**：该目录包含 tools、tutorial 等大量文件，但只有 lib 和 include 参与编译。

## 总结

本实验演示了 ESP32-S3 端侧 AI 的完整流程：摄像头采集 → esp-dl 两级人脸检测模型推理 → 结果绘制 → LCD 显示。核心知识点是 esp-dl 框架的使用、级联检测模型的原理，以及三任务流水线的并行架构。这为后续更复杂的 AI 应用（人脸识别、物体检测、图像分类等）打下了基础。端侧 AI 的优势是离线、低延迟、保护隐私，非常适合智能家居、安防监控等场景。
