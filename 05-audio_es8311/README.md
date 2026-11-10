# 实验 05：ES8311 音频播放（扬声器放音）

## 简介

本实验通过 I2S 接口向板载的 ES8311 音频编解码芯片发送嵌入在固件中的 PCM 音乐数据（《卡农》片段），经功放推动扬声器循环播放。这是与实验 04（录音）相对的放音实验，帮助你理解 I2S 音频输出的完整流程。

## 原理

### ES8311 音频编解码芯片

ES8311 是一款低功耗单声道音频编解码芯片，内部集成了 ADC（录音）和 DAC（放音）。本实验只使用它的 DAC 功能，把数字音频数据转换成模拟信号驱动扬声器。它通过 I2C 接口配置参数，通过 I2S 接口接收音频数据流。

### I2S 音频输出

I2S（Inter-IC Sound）是数字音频设备之间的标准串行接口。本实验中 ESP32-S3 作为主机（Master），产生 MCLK、BCLK、WS 时钟信号，并通过 DOUT 数据线把 PCM 音频数据发送给 ES8311。

音频参数：
- 采样率 16000Hz（16kHz）
- 数据位宽 16bit
- 立体声（双声道）
- MCLK = 384 × 采样率 = 6.144MHz

### PCM 裸音频数据

PCM（脉冲编码调制）是最原始的数字音频形式，就是把模拟声音波形按固定采样率量化成一串数值。与 MP3、WAV 不同，裸 PCM 没有文件头，播放器必须事先知道采样率、位深、声道数才能正确播放。

本实验把一段 10 秒的《卡农》PCM 数据（640000 字节）通过 `EMBED_FILES` 机制直接编译进固件。

### 功放使能（PCA9557）

扬声器需要功放芯片驱动，而功放的使能引脚 PA_EN 并非直接接在 ESP32-S3 的 GPIO 上，而是接在 I2C IO 扩展芯片 PCA9557 的输出脚上。因此要先初始化 PCA9557，再通过 I2C 写寄存器来打开功放。

## 硬件接线

ES8311、功放、PCA9557、扬声器均焊接在开发板上，无需额外接线。相关引脚如下：

| 信号 | GPIO | 说明 |
|------|------|------|
| I2C SDA | GPIO1 | 配置 ES8311 和 PCA9557 |
| I2C SCL | GPIO2 | 配置 ES8311 和 PCA9557 |
| I2S MCLK | GPIO38 | 主时钟 |
| I2S BCLK | GPIO14 | 位时钟 |
| I2S WS | GPIO13 | 帧时钟 |
| I2S DOUT | GPIO45 | 数据输出（ESP32-S3 → ES8311） |

> 本实验只放音不录音，因此 I2S 数据输入脚 DIN 设为 -1（未使用）。

PCA9557（I2C 地址 `0x19`）的扩展输出：
- P0：LCD_CS（LCD 片选）
- P1：PA_EN（功放使能，本实验用到）
- P2：DVP_PWDN（摄像头电源控制）

## 代码讲解

工程目录结构：

```
05-audio_es8311/
├── CMakeLists.txt
├── sdkconfig.defaults
└── main/
    ├── CMakeLists.txt      # EMBED_FILES "canon.pcm"
    ├── canon.pcm           # 嵌入的音乐数据（640000 字节，约 10 秒）
    ├── esp32_s3_szp.c      # I2C/ES8311/PCA9557 驱动
    ├── esp32_s3_szp.h
    ├── idf_component.yml   # 依赖 espressif/es8311 组件
    └── main.c
```

### 托管组件

`main/idf_component.yml`：

```yaml
dependencies:
  espressif/es8311: "^1.0.0"
  idf: "^5.0"
```

### 关键流程

`app_main()` 依次执行：

1. `i2s_driver_init()`：创建 I2S 发送通道（仅发送），配置为标准飞利浦（Philips）格式、16kHz、16bit、立体声，并使能通道。
2. `es8311_codec_init()`：初始化 I2C，创建 ES8311 句柄并初始化，配置采样率（16kHz）、数字音量（70%），关闭麦克风功能（只放音）。
3. `pca9557_init()`：初始化 IO 扩展芯片。
4. `pa_en(1)`：通过 PCA9557 打开功放。
5. 创建 `i2s_music` 播放任务。

### 播放任务逻辑

`i2s_music` 任务：

1. 先 `i2s_channel_disable()`，用 `i2s_channel_preload_data()` 把 PCM 数据预装载到 DMA 缓冲区（保证使能后立即有数据播出，避免开头爆音），再 `i2s_channel_enable()`。
2. 循环：`i2s_channel_write()` 阻塞写完一整首曲子 → 数据指针重置回起点 → 延时 1 秒 → 再播一遍。如此无限循环。

每播完一遍，串口打印写入的字节数（640000）。

## 实验效果

编译烧录后：

- 扬声器以 70% 音量循环播放约 10 秒的《卡农》片段，每遍之间停顿 1 秒。
- 串口监视器看到：

```
i2s es8311 codec example start
I (xxx) i2s_es8311: i2s driver init success
I (xxx) i2s_es8311: es8311 codec init success
I (xxx) i2s_es8311: [music] i2s music played, 640000 bytes are written.
（每循环一遍打印一次）
```

### 编译与烧录

```bash
idf.py set-target esp32s3
idf.py build flash monitor
```

## 注意事项

1. **初始化顺序**：先初始化 I2S，再初始化 ES8311，两者任一失败会 `abort()`（程序终止）。
2. **功放依赖 PCA9557**：`pa_en(1)` 必须在 `pca9557_init()` 之后调用。功放使能挂在 I2C 扩展芯片上，不是 ESP32-S3 的直接 GPIO，这是初学者容易忽略的地方。
3. **本实验未开启 PSRAM**：与后续 LCD/摄像头实验不同，`sdkconfig.defaults` 中没有 SPIRAM 配置，因为音乐数据在 Flash 中，播放缓冲区较小。
4. **音量固定 70%**：由 `EXAMPLE_VOICE_VOLUME = 70` 决定，范围 0~100。
5. **预装载的作用**：`i2s_channel_preload_data()` 只能预装 DMA 缓冲区大小的数据，剩余部分靠循环 `write` 补上。预装是为了消除播放开头的杂音。
6. **更换音乐**：想播放别的曲子，需把新的裸 PCM 文件（16kHz/16bit/双声道）替换 `canon.pcm`，格式不符会变速或产生噪声。

## 总结

本实验演示了 ESP32-S3 通过 I2S + ES8311 播放音频的完整链路：I2S 驱动初始化 → 编解码芯片配置 → 功放使能 → PCM 数据写入。结合实验 04 的录音，你就掌握了音频输入输出的双向流程。后续实验（如 MP3 播放器、语音识别）都会复用这套音频输出机制，只是数据源从嵌入的裸 PCM 变成了 SD 卡/SPIFFS 中的 MP3 文件或麦克风实时数据。
