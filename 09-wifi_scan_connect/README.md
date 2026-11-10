# 实验 09：WiFi 扫描与连接

## 简介

本实验在 LCD 上用 LVGL 构建一个触摸式 WiFi 配网界面：开机自动扫描周围的 WiFi 热点并列出，点选某个热点后，通过屏幕上的三个滚轮（数字/小写字母/大写字母）输入密码，连接成功后串口打印获取到的 IP 地址。这是学习 ESP32-S3 WiFi 功能与 GUI 交互结合的典型实验。

## 原理

### ESP32-S3 WiFi STA 模式

ESP32-S3 内置 2.4GHz WiFi，支持 Station（STA，客户端）、SoftAP（热点）和混合模式。本实验使用 **STA 模式**，即开发板作为客户端去连接家里的路由器。

WiFi 连接流程：
1. 初始化 NVS（WiFi 校准数据必须存在 NVS 中）
2. 初始化网络接口（`esp_netif`）和事件循环（`esp_event`）
3. 初始化 WiFi 驱动，设置 STA 模式
4. 扫描热点 → 选择目标 → 配置 SSID 和密码 → 发起连接
5. 通过事件回调获知连接结果，连接成功后由 DHCP 分配 IP

### 事件驱动机制

WiFi 的工作是异步的，连接结果通过**事件**通知应用程序。本实验注册了两类事件回调：
- `WIFI_EVENT`：WiFi 状态变化（开始扫描、断开连接等）
- `IP_EVENT`：获取到 IP 地址（`IP_EVENT_STA_GOT_IP`）

连接任务通过 FreeRTOS 的**事件组**（EventGroup）等待"连接成功"或"连接失败"标志位，从而知道何时该更新界面。

### LVGL 界面交互

界面上用到了 LVGL 的多个控件：
- `lv_list`：WiFi 热点列表
- `lv_roller`：三个可循环滚动的密码输入滚轮（数字 0-9、小写 a-z、大写 A-Z）
- `lv_textarea`：密码显示框
- `lv_btn`：返回、确认、退格等按钮

### 任务间通信

界面任务（运行在 LVGL 上下文）和 WiFi 连接任务（绑定 Core 1）之间通过 FreeRTOS **队列** 传递 SSID 和密码，避免在一个任务里既操作界面又阻塞等待连接。

## 硬件接线

本实验使用板载 LCD 和触摸屏，无需额外接线。引脚与实验 08 相同：

| 信号 | GPIO | 说明 |
|------|------|------|
| LCD SPI MOSI | GPIO40 | |
| LCD SPI CLK | GPIO41 | |
| LCD DC | GPIO39 | |
| LCD 背光 | GPIO42 | |
| LCD CS | 经 PCA9557 P0 | |
| 触摸 SDA/SCL | GPIO1/GPIO2 | 复用 I2C0 |

WiFi 使用 ESP32-S3 内置射频，通过板载天线收发，无需外接硬件。

## 代码讲解

工程目录结构：

```
09-wifi_scan_connect/
├── CMakeLists.txt
├── partitions.csv          # 自定义分区（factory 7M，容纳中文字库）
├── sdkconfig.defaults
└── main/
    ├── CMakeLists.txt
    ├── app_ui.c            # WiFi 扫描/连接界面与逻辑
    ├── app_ui.h
    ├── esp32_s3_szp.c      # BSP
    ├── esp32_s3_szp.h
    ├── font_alipuhui20.c   # 阿里巴巴普惠体 20px 中文字库
    ├── idf_component.yml   # LVGL + esp_lvgl_port + ft5x06
    └── main.c
```

### 关键流程

`app_main()`：

1. `nvs_flash_init()`：初始化 NVS，失败则擦除后重试（WiFi 必须依赖 NVS）。
2. `bsp_i2c_init()` → `pca9557_init()` → `bsp_lvgl_start()`：启动 LVGL 界面。
3. `app_wifi_connect()`：WiFi 配网主逻辑。

### WiFi 配网逻辑（app_ui.c）

`app_wifi_connect()`：

1. 屏幕显示"WLAN扫描中..."（使用中文字库 `font_alipuhui20`）。
2. `wifi_scan()`：初始化网络接口、事件循环、WiFi 驱动，设为 STA 模式，注册事件回调，然后**阻塞式扫描** `esp_wifi_scan_start(NULL, true)`，最多获取 10 个热点（`DEFAULT_SCAN_LIST_SIZE = 10`）。
3. 把扫描结果填入 LVGL 列表，每项带 WiFi 图标，同时串口打印 SSID 和信号强度 RSSI。
4. 创建队列 `xQueueWifiAccount` 和连接任务 `wifi_connect`（绑定 Core 1）。
5. 用户点击某个热点 → 弹出密码输入页（三个滚轮 + 确认/退格键）→ 点 OK 后 SSID 和密码通过队列发给连接任务。

### 连接任务

`wifi_connect` 任务：

1. 从队列取出 SSID 和密码，填入 `wifi_config_t`。
2. 设置加密方式为 `WIFI_AUTH_WPA2_PSK`（兼容 WPA3），调用 `esp_wifi_connect()`。
3. 用 `xEventGroupWaitBits()` 等待连接结果：
   - 收到 `IP_EVENT_STA_GOT_IP` → 置成功标志
   - 断开且重试达 3 次（`EXAMPLE_ESP_MAXIMUM_RETRY = 3`）→ 置失败标志
4. 在界面 label 上显示"WLAN连接中... / 连接成功 / 连接失败"。

## 实验效果

编译烧录后：

- 开机白底屏显示"WLAN扫描中..."，随后列出最多 10 个 WiFi 热点。
- 串口打印扫描到的 SSID 和信号强度。
- 点击某个热点，弹出密码输入页，用三个滚轮逐字符输入密码，点 OK 连接。
- 连接成功后串口打印 `got ip:x.x.x.x`，屏幕显示"WLAN连接成功"。

### 编译与烧录

```bash
idf.py set-target esp32s3
idf.py build flash monitor
```

## 注意事项

1. **扫描期间界面无响应**：扫描是阻塞式的且在 LVGL 启动后同步执行，扫描几秒钟内界面卡住属正常现象。
2. **不支持开放网络（无密码）**：密码为空时点 OK 不会发起连接，所以无法连接没有密码的开放热点。
3. **滚轮无特殊字符**：密码输入只有 0-9、a-z、A-Z 三类，含符号（如 @、#）的密码无法输入。
4. **密码会明文打印到串口**：日志中会用 `ESP_LOGI` 打印 SSID 和密码，这只适合演示，不是安全做法，实际产品中不要这样打印。
5. **只连接 WPA2/WPA3**：加密阈值设为 `WIFI_AUTH_WPA2_PSK`，老旧的 WEP 或不加密网络连接不了。
6. **中文字库占用空间大**：`font_alipuhui20.c` 包含大量中文字符，导致 factory 分区需要 7M，这是自定义分区表的原因。
7. **跨任务改 LVGL 控件要加锁**：连接任务里更新界面 label 时必须用 `lvgl_port_lock()` 包裹。

## 总结

本实验把 WiFi 网络功能和 LVGL 图形界面结合起来，实现了一个可通过触摸操作的 WiFi 配网应用。核心知识点包括：ESP32-S3 STA 模式的完整连接流程、事件驱动 + 事件组的异步结果处理、队列在多任务间传递数据，以及中文字库在 LVGL 中的使用。掌握 WiFi 连接后，你就可以让开发板接入互联网，实现 MQTT、HTTP、NTP 时钟等联网功能。
