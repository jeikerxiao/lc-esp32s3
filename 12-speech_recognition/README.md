# 实验 12：离线语音识别控制音乐播放器

## 简介

本实验基于乐鑫 esp-sr 离线语音框架，实现"动口不动手"控制音乐播放器：先说唤醒词唤醒设备，然后用中文语音命令（播放音乐、暂停、继续、上一首、下一首、声音大一点、声音小一点）控制屏幕上的 LVGL 播放器。唤醒时屏幕中央会出现一个 B 站小电视 GIF 动画作为"AI 助手"形象。所有识别均在本地离线完成，无需联网。

## 原理

### esp-sr 离线语音框架

`espressif/esp-sr` 是乐鑫的离线语音识别框架，包含三大模块：

1. **WakeNet（唤醒词引擎）**：负责检测唤醒词（如"Hi 乐鑫"），低功耗常驻运行。
2. **MultiNet（命令词引擎）**：唤醒后识别预定义的命令词（如"播放音乐"），支持中文。
3. **AFE（音频前端）**：对麦克风原始数据做降噪、回声消除、自动增益等处理，提升识别率。

### 语音识别流程

```
麦克风采集 → AFE 前端处理 → WakeNet 检测唤醒词
                                    ↓（唤醒成功）
                              MultiNet 识别命令词 → 执行动作
                                    ↓（6 秒无命令超时）
                              回到 WakeNet 等待唤醒
```

### 命令词（拼音）

MultiNet 使用拼音字符串注册命令词。本实验注册了 7 条中文命令：

| 拼音 | 命令 | 动作 |
|------|------|------|
| `bo fang yin yue` | 播放音乐 | `ai_play()` |
| `zan ting` | 暂停 | `ai_pause()` |
| `ji xu` | 继续 | `ai_resume()` |
| `shang yi shou` | 上一首 | `ai_prev_music()` |
| `xia yi shou` | 下一首 | `ai_next_music()` |
| `sheng yin da yi dian` | 声音大一点 | `ai_volume_up()`（+5） |
| `sheng yin xiao yi dian` | 声音小一点 | `ai_volume_down()`（-5） |

### 双任务架构

语音识别用两个任务分工（分别绑定两个 CPU 核心）：
- **feed 任务（Core 0）**：不断从麦克风读取音频数据，喂给 AFE。
- **detect 任务（Core 1）**：从 AFE 取出处理后的数据，交给 WakeNet/MultiNet 识别，根据结果执行动作。

### 模型分区

esp-sr 的语音模型（WakeNet、MultiNet）体积较大，存放在专门的 `model` 分区（5168KB），构建时由 esp-sr 组件自动打包烧录。程序启动时用 `esp_srmodel_init("model")` 从该分区加载模型。

## 硬件接线

本实验使用板载麦克风、音频电路、LCD 和触摸屏，无需额外接线。

音频 I2S 引脚（使用 I2S1）：

| 信号 | GPIO | 说明 |
|------|------|------|
| I2S MCLK | GPIO38 | 主时钟 |
| I2S SCLK（BCLK） | GPIO14 | 位时钟 |
| I2S LRCK（WS） | GPIO13 | 帧时钟 |
| I2S DOUT | GPIO45 | 放音输出（ES8311） |
| I2S SDIN | GPIO12 | 录音输入（ES7210，4 麦克风） |

LCD/触摸引脚与实验 08 相同。功放使能经 PCA9557 P1 控制。

## 代码讲解

工程目录结构：

```
12-speech_recognition/
├── CMakeLists.txt
├── partitions.csv          # factory 3M + storage 3M + model 5168K
├── sdkconfig.defaults
├── spiffs/                 # 预置 3 首 MP3
│   ├── the_beginning.mp3
│   ├── they_say.mp3
│   └── little_changes.mp3
└── main/
    ├── app_sr.c            # 语音识别逻辑
    ├── app_sr.h
    ├── app_ui.c            # 播放器界面 + ai_* 控制函数
    ├── app_ui.h
    ├── esp32_s3_szp.c      # BSP
    ├── esp32_s3_szp.h
    ├── img_bilibili120.c   # B 站小电视 GIF 动画数据
    ├── idf_component.yml   # esp-sr + audio-player + codec_dev + LVGL 等
    └── main.c
```

### 托管组件

`main/idf_component.yml`：

```yaml
dependencies:
  lvgl/lvgl: "~8.3.0"
  espressif/esp_lvgl_port: "~1.4.0"
  espressif/esp_lcd_touch_ft5x06: "~1.0.6"
  chmorgan/esp-audio-player: "~1.0.7"
  chmorgan/esp-file-iterator: "1.0.0"
  espressif/esp_codec_dev: "~1.3.0"
  espressif/esp-sr: "~1.6.0"    # 语音识别核心
```

### 关键流程

`app_main()`：

1. `bsp_i2c_init()` → `pca9557_init()` → `bsp_lvgl_start()`。
2. `bsp_spiffs_mount()`：挂载 SPIFFS（3 首 MP3）。
3. `bsp_codec_init()`：初始化 ES8311（放音）+ ES7210（4 麦克风录音），16kHz。
4. `mp3_player_init()`：创建音乐播放器（界面和逻辑与实验 11 基本一致）。
5. `app_sr_init()`：初始化语音识别。

### 语音识别初始化（app_sr.c）

`app_sr_init()`：

1. `esp_srmodel_init("model")`：从 model 分区加载模型。
2. 配置 AFE，选择默认 WakeNet 唤醒词模型，创建 AFE 句柄。
3. 创建 feed 任务（Core 0）和 detect 任务（Core 1）。

### feed 任务

循环从 ES7210 读取 4 通道 I2S 音频数据（缓冲区分配在 PSRAM），并把通道重排为 3 通道（其中一路作为参考/回采信号用于回声消除），再 `afe_handle->feed()` 送入 AFE。

### detect 任务

1. 选择中文 MultiNet 命令词模型，`multinet->create(mn_name, 6000)`——**唤醒后等待命令的超时为 6 秒**。
2. 注册 7 条命令词。
3. 状态机：
   - `WAKENET_DETECTED`：检测到唤醒词，清 MultiNet 状态。
   - `WAKENET_CHANNEL_VERIFIED`：唤醒确认，禁用 WakeNet，置 `detect_flag = 1`，调用 `ai_gui_in()` 在屏幕中央显示 GIF 动画。
   - 识别到命令词：执行对应的 `ai_*` 函数。
   - `ESP_MN_STATE_TIMEOUT`（6 秒无命令）：重新使能 WakeNet，`ai_gui_out()` 删除 GIF，回到待唤醒状态。

### ai_* 控制函数（app_ui.c）

每个 `ai_*` 函数在操作播放器的同时，同步更新 LVGL 控件（按钮图标、下拉列表、音量滑条），所有界面操作都通过 `lvgl_port_lock/unlock` 保护。

## 实验效果

编译烧录后：

- 屏幕白底显示音乐播放器界面，可触摸操作播放。
- 串口打印模型名（如 `multinet:mn5...`、`wakenet:wn9...`）、命令词列表和 `------------detect start------------`。
- 说唤醒词后，串口打印 `WAKEWORD DETECTED`，屏幕中央出现 B 站小电视 GIF"AI 助手"。
- 在 6 秒内说命令词（如"播放音乐""下一首""声音大一点"），串口打印识别结果，播放器执行相应动作，界面同步更新。
- 6 秒内无命令，GIF 消失，回到待唤醒状态，等待下一次唤醒。

### 编译与烧录

```bash
idf.py set-target esp32s3
idf.py build flash monitor
```

> 烧录时会一并写入 SPIFFS（歌曲）和 model（语音模型）分区。

## 注意事项

1. **命令窗口只有 6 秒**：唤醒后必须在 6 秒内说命令，否则超时回到待唤醒状态，需重新唤醒。
2. **命令词是拼音**：注册的命令词必须与 MultiNet 模型支持的拼音一致，不能随意改成中文汉字或英文。
3. **麦克风通道顺序**：`bsp_get_feed_data()` 中人为把 4 通道重排为 3 通道，第 3 通道是参考/回采通道，改动硬件麦克风接法会影响识别效果。
4. **model 分区必须保留**：分区表中的 model 分区（5168K）不能删，否则找不到模型。
5. **依赖 PSRAM**：AFE 缓冲、GIF、图片缓冲都要求 PSRAM，PSRAM 相关配置（OCT/80M）不能关闭。
6. **LVGL 加锁**：detect 任务里更新界面必须用 `lvgl_port_lock()` 包裹。
7. **GIF 需要 LV_USE_GIF**：`CONFIG_LV_USE_GIF=y` 已在 sdkconfig 开启，AI 动画才能显示。

## 总结

本实验在实验 11 音乐播放器的基础上，加入了 esp-sr 离线语音识别，实现了语音控制播放。核心知识点是 esp-sr 的三大模块（WakeNet/MultiNet/AFE）、双任务 feed/detect 架构、模型分区的加载，以及语音命令与 LVGL 界面的联动。这是一个非常实用的交互方式——离线、低延迟、无需联网。掌握后，你可以自定义唤醒词和命令词，做出语音控制的智能家居、语音遥控小车等项目。
