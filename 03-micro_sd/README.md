# 实验 03：MicroSD 卡读写

## 简介

本实验演示如何使用 ESP32-S3 内置的 SDMMC 外设挂载 MicroSD（TF）卡，并在卡上创建一个带中文名的文本文件、写入内容、再读回打印。这是学习文件系统操作和 SD 卡接口的入门实验。

## 原理

### SDMMC 外设

ESP32-S3 内置了 SD/MMC 主机控制器，可以直接以 SD 协议驱动 SD 卡，无需额外的 SPI 转换芯片。SD 卡支持 1 线模式和 4 线模式：
- **1 线模式**：只用 CLK、CMD、DAT0 三根信号线，省引脚但速度较慢。
- **4 线模式**：额外使用 DAT1~DAT3，速度更快。

本实验使用 1 线模式。

### FAT 文件系统

SD 卡通常格式化为 FAT32/FAT16 文件系统。ESP-IDF 通过 FatFs 组件 + VFS（虚拟文件系统）层，让你可以像操作普通文件一样用 `fopen`/`fprintf`/`fgets` 读写 SD 卡上的文件。

### 中文文件名支持

要正确创建和显示中文文件名，FatFs 必须开启三项配置：
- **LFN（长文件名）**：支持超过 8.3 格式的文件名
- **代码页 936**：对应简体中文 GBK 编码
- **API 编码 UTF-8**：让文件名字符串以 UTF-8 传递

这三项在 `sdkconfig.defaults` 中已经配置好。

## 硬件接线

将 MicroSD 卡插入开发板的 TF 卡槽即可。卡槽通过 SDMMC 外设连接，引脚如下：

| 信号 | GPIO | 说明 |
|------|------|------|
| SD CLK | GPIO47 | 时钟线 |
| SD CMD | GPIO48 | 命令线 |
| SD DAT0 | GPIO21 | 数据线（1 线模式只用这一根） |

挂载点为 `/sdcard`，使用内部上拉。

## 代码讲解

工程目录结构：

```
03-micro_sd/
├── CMakeLists.txt
├── sdkconfig.defaults
└── main/
    ├── CMakeLists.txt
    └── main.c
```

### 关键流程

`app_main()` 是一个一次性顺序流程（写完读回就结束，不循环）：

1. **配置挂载参数** `mount_config`：
   - `format_if_mount_failed = true`：挂载失败时自动格式化 SD 卡
   - `max_files = 5`：最多同时打开 5 个文件
   - `allocation_unit_size = 16 * 1024`
2. **配置 SDMMC 主机与插槽**：`slot_config.width = 1`（1 线模式），填入 CLK/CMD/D0 引脚，启用内部上拉标志 `SDMMC_SLOT_FLAG_INTERNAL_PULLUP`。
3. **挂载文件系统**：`esp_vfs_fat_sdmmc_mount("/sdcard", ...)`，成功后 `sdmmc_card_print_info()` 打印卡信息。
4. **写文件**：创建 `/sdcard/你好hello.txt`，写入 `你好hello先生 <卡名>!`。
5. **读文件**：读回内容并把换行符替换为字符串结束符，打印到串口。
6. **卸载**：`esp_vfs_fat_sdcard_unmount()`。

### 关键配置（sdkconfig.defaults）

```
CONFIG_IDF_TARGET="esp32s3"
CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y
CONFIG_FATFS_LFN_HEAP=y           # 长文件名
CONFIG_FATFS_CODEPAGE_936=y       # 简体中文 GBK
CONFIG_FATFS_API_ENCODING_UTF_8=y # API 使用 UTF-8
```

## 实验效果

编译烧录后打开串口监视器，会依次看到：

```
I (xxx) main: Initializing SD card
I (xxx) main: Using SDMMC peripheral
I (xxx) main: Mounting filesystem
I (xxx) main: Filesystem mounted
（SD 卡详细信息表：Name、Type、Speed、Size 等）
I (xxx) main: Opening file /sdcard/你好hello.txt
I (xxx) main: File written
I (xxx) main: Reading file /sdcard/你好hello.txt
I (xxx) main: Read from file: '你好hello先生 XXXXX!'
I (xxx) main: Card unmounted
```

把 SD 卡插到电脑上，可以看到根目录下生成了 `你好hello.txt` 文件，内容包含卡的产品名。

### 编译与烧录

```bash
idf.py set-target esp32s3
idf.py build flash monitor
```

## 注意事项

1. **自动格式化风险**：`format_if_mount_failed = true` 意味着如果卡上文件系统损坏，挂载时会被自动格式化，卡内原有数据将丢失。实验前请勿在卡中存放重要文件。
2. **SD 卡格式要求**：卡必须是 FAT32 或 FAT16 格式。大容量 SDXC 卡有时需要先在电脑上格式化为 FAT32。
3. **中文配置三件套缺一不可**：LFN、代码页 936、UTF-8 编码三项必须同时开启，否则中文文件名会乱码或创建失败，这是初学者最常踩的坑。
4. **1 线模式速度有限**：本实验用 1 线模式，速度低于 4 线模式。若需高速读写（如录音、播放视频），应改用 4 线模式并连接 DAT1~DAT3。
5. **一次性流程**：程序写完读回后就卸载结束，重复运行会覆盖同名文件。
6. **字符串长度限制**：`EXAMPLE_MAX_CHAR_SIZE = 64`，写入/读取内容超长会被截断。

## 总结

本实验演示了 ESP32-S3 通过 SDMMC 外设挂载 SD 卡并进行文件读写的完整流程，重点在于 FatFs 的中文文件名配置和 SDMMC 1 线模式的引脚连接。掌握这些后，你就可以在 SD 卡上记录传感器数据、存放音频/图片文件，为后续的数据记录和多媒体实验打下基础。
