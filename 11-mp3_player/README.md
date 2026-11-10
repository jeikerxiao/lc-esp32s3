# 实验 11：MP3 音乐播放器

## 简介

本实验实现一个完整的触摸式 MP3 音乐播放器：从 SPIFFS 分区（Flash 内置的 3 首 MP3 歌曲）解码播放音乐到扬声器，LCD 上显示播放器界面（播放/暂停、上一首/下一首、音量滑条、歌曲下拉列表），一首播完自动切换下一首并循环。这是综合运用音频解码、文件系统和 LVGL 界面设计的实战实验。

## 原理

### SPIFFS 文件系统

SPIFFS（SPI Flash File System）是专为 NOR Flash 设计的文件系统。ESP32-S3 的板载 Flash 可以划出一个分区当作文件系统使用，让程序像操作普通文件一样读取存储在 Flash 中的数据。

本实验把 3 首 MP3 歌曲预先打包成 SPIFFS 镜像，随固件一起烧录到 Flash 的 `storage` 分区，运行时挂载到 `/spiffs` 目录。与 SD 卡不同，SPIFFS 的数据是固化在 Flash 里的，掉电不丢失，但容量有限且没有目录层级（所有文件都在根目录下）。

### MP3 解码与音频播放

MP3 是一种压缩音频格式，不能直接送 I2S 播放，必须先解码成 PCM。本实验用 `chmorgan/esp-audio-player` 组件完成"读取 MP3 文件 → 解码 → 通过 I2S 输出"的全流程，它内部维护一个播放状态机（IDLE/PLAYING/PAUSE），并通过回调通知应用层。

音频输出链路：MP3 文件 → audio_player 解码 → esp_codec_dev → I2S → ES8311 DAC → 功放 → 扬声器。

### esp_codec_dev 音频抽象层

`espressif/esp_codec_dev` 是音频编解码设备的抽象层，统一管理 ES8311（DAC 放音）和 ES7210（ADC 录音）。它会根据 MP3 的实际采样率动态重配 I2S 和 codec。

### LVGL 播放器界面

界面上用到的控件：
- `lv_dropdown`：歌曲下拉列表
- `lv_btn` + 图标：播放/暂停、上一首/下一首
- `lv_slider`：音量滑条
- `lv_label`：歌曲标题

## 硬件接线

本实验使用板载 LCD、触摸屏和音频电路，无需额外接线。

音频 I2S 引脚（注意本实验使用 I2S1）：

| 信号 | GPIO | 说明 |
|------|------|------|
| I2S MCLK | GPIO38 | 主时钟 |
| I2S SCLK（BCLK） | GPIO14 | 位时钟 |
| I2S LRCK（WS） | GPIO13 | 帧时钟 |
| I2S DOUT | GPIO45 | 数据输出（放音，接 ES8311） |
| I2S SDIN | GPIO12 | 数据输入（录音，本例未用） |
| I2C SDA/SCL | GPIO1/GPIO2 | 配置 ES8311/ES7210/PCA9557 |

LCD/触摸引脚与实验 08 相同。功放使能 PA_EN 经 PCA9557 P1 控制。

## 代码讲解

工程目录结构：

```
11-mp3_player/
├── CMakeLists.txt
├── partitions.csv          # 增加 storage(spiffs) 3M 分区
├── sdkconfig.defaults
├── spiffs/                 # 预置 3 首 MP3
│   ├── Canon.mp3
│   ├── For_Elise.mp3
│   └── new_epic.mp3
└── main/
    ├── app_ui.c            # 播放器界面与逻辑
    ├── app_ui.h
    ├── esp32_s3_szp.c      # BSP（含 SPIFFS 挂载、codec 初始化）
    ├── esp32_s3_szp.h
    ├── idf_component.yml   # audio-player + file-iterator + codec_dev + LVGL
    └── main.c
```

### 托管组件

`main/idf_component.yml`：

```yaml
dependencies:
  lvgl/lvgl: "~8.3.0"
  espressif/esp_lvgl_port: "~1.4.0"
  espressif/esp_lcd_touch_ft5x06: "~1.0.6"
  chmorgan/esp-audio-player: "~1.0.7"   # MP3 解码播放
  chmorgan/esp-file-iterator: "1.0.0"   # 遍历目录文件
  espressif/esp_codec_dev: "~1.3.0"     # ES8311/ES7210 驱动
```

### SPIFFS 镜像打包

`main/CMakeLists.txt` 中的关键一行：

```cmake
spiffs_create_partition_image(storage ../spiffs FLASH_IN_PROJECT)
```

它会在构建时把工程目录下的 `spiffs/` 文件夹打包成 SPIFFS 镜像，并随 `idf.py flash` 一起烧录到 `storage` 分区。所以修改了 `spiffs/` 里的歌曲后，需要重新 `idf.py build flash` 才能生效。

### 关键流程

`app_main()`：

1. `bsp_i2c_init()` → `pca9557_init()` → `bsp_lvgl_start()`。
2. `bsp_spiffs_mount()`：挂载 SPIFFS（分区标签 `storage`，挂载点 `/spiffs`），打印分区容量。
3. `bsp_codec_init()`：初始化 I2S、ES8311（放音）、ES7210（录音，本例仅初始化未使用）。
4. `mp3_player_init()`：初始化播放器并创建界面。

### 播放器初始化

`mp3_player_init()`：

1. `file_iterator_new(SPIFFS_BASE)`：扫描 `/spiffs` 下所有 MP3 文件。
2. 配置 `audio_player_config_t`：注册三个回调函数（静音/音量、写数据、时钟设置）。
3. `audio_player_new()` 创建播放器，`audio_player_callback_register()` 注册状态回调。
4. `music_ui()`：创建播放器界面。

### 播放状态机回调

`_audio_player_callback()`：
- `EVENT_IDLE`（一首播完）：调用 `file_iterator_next()` 取下一首并播放，同时更新下拉框和标题。
- `EVENT_PLAYING`：`pa_en(1)` 打开功放。
- `EVENT_PAUSE`：`pa_en(0)` 关闭功放。

### 界面交互

- **播放/暂停按钮**：根据当前状态在播放、暂停、恢复之间切换。
- **上一首/下一首按钮**：切换歌曲索引并立即播放或保持暂停。
- **音量滑条**：0~100，默认 60，实时调用 `bsp_codec_volume_set()` 调节。
- **歌曲下拉列表**：可直接选歌。

## 实验效果

编译烧录后：

- 屏幕显示播放器界面：顶部歌曲标题、下方下拉列表、中间三个圆形控制按钮、底部音量滑条。
- 触摸播放键开始放歌，扬声器出声（功放自动使能）。
- 一首播完自动切下一首，3 首循环播放。
- 拖动音量滑条实时调节音量，点下拉列表可直接选歌。
- 串口打印分区容量、当前播放文件、音量等信息。

### 编译与烧录

```bash
idf.py set-target esp32s3
idf.py build flash monitor
```

> 烧录时会一并把 SPIFFS 镜像写入 storage 分区。如果只烧录 app 而不烧 storage，SPIFFS 挂载会失败。

## 注意事项

1. **SPIFFS 镜像必须烧录**：`format_if_mount_failed = false`，如果 storage 分区没有正确烧录镜像，挂载会失败并触发 `ESP_ERROR_CHECK` 复位。务必连同 SPIFFS 镜像一起烧录。
2. **修改歌曲需重新打包**：改 `spiffs/` 里的内容后必须 `idf.py build flash`，镜像才会更新。
3. **SPIFFS 无目录层级**：所有 MP3 必须放在 `/spiffs` 根目录下。
4. **文件名长度限制**：受 `CONFIG_SPIFFS_OBJ_NAME_LEN = 128` 限制。
5. **功放自动控制**：`pa_en()` 随播放/暂停自动开关。如果没声音，先检查 PCA9557 初始化是否成功。
6. **音量不保存**：音量滑条只改运行时变量，重启后回到默认值 60（无 NVS 保存）。
7. **使用 I2S1**：本实验用的是 `I2S_NUM_1`，头文件里旧的 `I2S_NUM(0)` 宏是历史遗留，勿混淆。
8. **采样率动态重设**：初始 I2S 配置为 16kHz，实际采样率由播放器根据 MP3 文件动态重设。若换用其他采样率的歌曲播放异常，检查 `clk_set_fn` 回调。

## 总结

本实验综合运用了 SPIFFS 文件系统、MP3 解码、I2S 音频输出和 LVGL 界面，实现了一个功能完整的音乐播放器。核心知识点是 SPIFFS 镜像的打包烧录机制、esp-audio-player 的状态机回调，以及界面控件与播放逻辑的联动。这个播放器界面在实验 12（语音识别）和实验 14（综合项目）中会被复用和扩展。掌握后，你可以进一步添加歌词显示、播放进度条、SD 卡播放等功能。
