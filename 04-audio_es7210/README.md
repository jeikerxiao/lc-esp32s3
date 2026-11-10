# 实验 04：ES7210 音频录制（麦克风录音到 SD 卡）

## 简介

本实验使用板载的 ES7210 四通道音频 ADC（模数转换器）采集麦克风信号，以 I2S TDM 模式、48kHz/16bit/双声道录制 10 秒音频，并保存为 SD 卡上的标准 WAV 文件 `/sdcard/RECORD.WAV`。录完后把卡插到电脑即可播放。这是学习 I2S 音频采集和 WAV 文件格式的进阶实验。

## 原理

### ES7210 音频 ADC

ES7210 是一款四通道音频模数转换芯片，负责把麦克风采集到的模拟信号转换成数字音频数据。它通过 I2C 接口配置参数（增益、采样率等），通过 I2S 接口输出数字音频流。I2C 器件地址为 `0x41`。

### I2S 与 TDM 模式

I2S（Inter-IC Sound）是数字音频设备之间的标准接口，主要信号线有：
- **MCLK**：主时钟，本实验为 256 × 采样率 = 12.288MHz
- **BCLK（SCLK）**：位时钟
- **WS（LRCK）**：帧时钟 / 左右声道选择
- **DIN/DOUT**：数据线

TDM（时分复用）模式可以在一组 I2S 线上传输多个声道。ES7210 有 4 个通道，需要 TDM 模式输出。本实验虽然开启了 TDM，但只使用其中 2 个 slot（SLOT0、SLOT1），因此实际录制双声道。

### WAV 文件格式

WAV 是微软的音频容器格式，由 44 字节文件头 + PCM 原始数据组成。文件头描述了采样率、位深、声道数等信息，播放器据此解析后续数据。本实验使用 `format_wav.h` 中的 `WAV_HEADER_PCM_DEFAULT()` 宏生成标准 PCM 头。

录音参数：48000Hz × 2 声道 × 16bit = 192000 字节/秒，10 秒约 1.92MB。

## 硬件接线

ES7210、麦克风、SD 卡均焊接/连接在开发板上，通过板载总线连接，无需额外接线。相关引脚如下：

| 信号 | GPIO | 说明 |
|------|------|------|
| I2C SDA | GPIO1 | 配置 ES7210 |
| I2C SCL | GPIO2 | 配置 ES7210 |
| I2S MCLK | GPIO38 | 主时钟（256×fs） |
| I2S BCLK | GPIO14 | 位时钟 |
| I2S WS | GPIO13 | 帧时钟 |
| I2S DIN | GPIO12 | 数据输入（ES7210 → ESP32-S3） |
| SD CLK | GPIO47 | SD 卡时钟 |
| SD CMD | GPIO48 | SD 卡命令 |
| SD DAT0 | GPIO21 | SD 卡数据（1 线模式） |

> 注意：ES7210 是纯 ADC（只能录音不能放音），所以 I2S 不配置输出数据线（`dout = -1`）。

## 代码讲解

工程目录结构：

```
04-audio_es7210/
├── CMakeLists.txt
├── sdkconfig.defaults
└── main/
    ├── CMakeLists.txt
    ├── format_wav.h        # WAV 文件头结构体与生成宏
    ├── idf_component.yml   # 依赖 espressif/es7210 组件
    └── main.c
```

### 托管组件

`main/idf_component.yml` 声明依赖乐鑫官方的 ES7210 驱动组件：

```yaml
dependencies:
  espressif/es7210: "^1.0.0"
  idf: ">=4.4.0"
```

### 关键流程

`app_main()` 依次执行：

1. `es7210_i2s_init()`：创建 I2S 接收通道（仅接收，不发送），配置为 TDM 模式、48kHz、16bit、双声道、MCLK 256 倍。
2. `es7210_codec_init()`：初始化 I2C，创建 ES7210 器件句柄，配置 I2S 格式、采样率、位深、麦克风偏置电压（2.87V）、麦克风增益（30dB）、数字音量（0dB）。
3. `mount_sdcard()`：挂载 SD 卡（1 线模式）。挂载失败会每秒无限重试。
4. `record_wav()`：录音 10 秒并写入 WAV 文件。
5. 卸载 SD 卡。

### 录音逻辑

`record_wav()` 的核心流程：

1. 计算字节率 `byte_rate = 48000 × 2 × 16/8 = 192000`，总大小 `wav_size = byte_rate × 10`。
2. 用 `WAV_HEADER_PCM_DEFAULT()` 生成 44 字节文件头，先写入文件。
3. `i2s_channel_enable()` 启动接收通道。
4. 循环 `i2s_channel_read()` 读取音频数据到 8KB 静态缓冲区，`fwrite()` 追加到文件，每跨过 1 秒打印一次进度。
5. 写满后 `i2s_channel_disable()`、`fclose()`。

> WAV 文件头在录音前就按预定大小写好，因此录音中途若出错或掉电，头部记录的大小会与实际数据不符，播放器可能报错。

## 实验效果

编译烧录后打开串口监视器，会看到：

```
I (xxx) example: Create I2S receive channel
I (xxx) example: Configure I2S receive channel to TDM mode
I (xxx) example: Init I2C used to configure ES7210
I (xxx) example: Configure ES7210 codec parameters
I (xxx) example: Mounting SD card
I (xxx) example: Card size: xxxxMB, speed: xxMHz
I (xxx) example: Opening file /RECORD.WAV
I (xxx) example: Recording: 1/10s
...
I (xxx) example: Recording: 10/10s
I (xxx) example: Recording done! Flushing file buffer
I (xxx) example: Audio was successfully recorded into /RECORD.WAV...
```

录音期间对着麦克风说话，10 秒后程序结束并提示可安全拔卡。把 SD 卡插到电脑，打开 `RECORD.WAV` 即可听到刚才录制的声音（48kHz、16bit、双声道）。

### 编译与烧录

```bash
idf.py set-target esp32s3
idf.py build flash monitor
```

## 注意事项

1. **录音时长固定 10 秒**：由 `EXAMPLE_RECORD_TIME_SEC = 10` 决定，产生约 1.92MB 文件。SD 卡空间不足时写入会失败。
2. **MCLK 必须正确输出**：ES7210 需要 256×fs 的主时钟才能工作，`EXAMPLE_I2S_MCLK_MULTIPLE` 与 codec 配置中的 `mclk_ratio` 必须一致。
3. **忘插 SD 卡会卡住**：`mount_sdcard()` 挂载失败后每秒无限重试，程序会停在 "Mounting filesystem"。
4. **I2C 与 I2S 驱动版本混用**：I2C 用的是旧版 `driver/i2c.h`（新版 IDF 有弃用警告），I2S 用的是 v5.x 新驱动 `i2s_new_channel()`，两者可以共存。
5. **TDM 只用了 2 个 slot**：ES7210 虽有 4 通道，但 slot mask 只开了 SLOT0|SLOT1。若想录 4 声道，需同时修改 slot mask 和 `EXAMPLE_I2S_CHAN_NUM`。
6. **每次运行覆盖旧文件**：文件以 `"w"` 模式打开，重复运行会覆盖之前的录音。
7. **麦克风增益可调**：声音太小或削顶时，可调整 `EXAMPLE_ES7210_MIC_GAIN`（当前 30dB）。

## 总结

本实验综合运用了 I2C 配置、I2S TDM 数据采集、SD 卡文件系统操作和 WAV 文件封装四项技术，完成了一个完整的录音设备。理解了这个流程后，你可以进一步扩展：增加录音时长、改用 4 声道、加入按键控制录音起止，或把录音数据通过 WiFi 上传等。
