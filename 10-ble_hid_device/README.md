# 实验 10：BLE HID 蓝牙设备（音量遥控器）

## 简介

本实验让 ESP32-S3 作为一个 BLE HID（人机接口设备）向外广播，手机或电脑搜索到名为"HID"的蓝牙设备并配对连接后，点击屏幕上的音量加/减按钮，即可远程控制主机的系统音量。这相当于把开发板变成了一个蓝牙音量遥控器，是学习 BLE 外设开发的有趣实验。

## 原理

### BLE 与 HID

- **BLE（Bluetooth Low Energy）**：低功耗蓝牙，是蓝牙 4.0 引入的协议，适合小数据量、低功耗的设备。ESP32-S3 内置 BLE 功能。
- **HID（Human Interface Device）**：人机接口设备协议，键盘、鼠标、游戏手柄、遥控器等都属于 HID 设备。主机（手机/电脑）通过 HID 描述符了解设备能发送什么类型的数据。

### HID Report Map（报告映射）

HID 设备通过一段叫"Report Map"的描述符告诉主机自己支持哪些功能。本实验的 Report Map 定义了 4 类报告：
- 鼠标（Report ID 1）
- 键盘（Report ID 2）
- **Consumer Devices 消费类设备（Report ID 3）**：音量加减、静音、播放暂停、上下曲等多媒体控制
- Vendor 厂商自定义（Report ID 4，未启用）

本实验用的是 **Consumer Control（消费类控制）**，发送音量加/减的 HID 使用码（Usage），主机收到后调整系统音量。

### Bluedroid 协议栈

ESP-IDF 提供了 Bluedroid 作为蓝牙协议栈。使用 BLE 的初始化流程：
1. 释放经典蓝牙内存（本实验只用 BLE）
2. 初始化并使能 BLE 控制器
3. 初始化并使能 Bluedroid
4. 初始化 HID Device Profile（`esp_hidd_profile_init()`）
5. 注册 GAP 事件回调和 HIDD 事件回调
6. 配置广播数据并开始广播

### 配对与安全

本实验采用 **Just Works** 配对方式（`ESP_IO_CAP_NONE`，无需输入 PIN 码），并启用绑定（`ESP_LE_AUTH_BOND`），配对信息存入 NVS。只有配对加密完成后（`sec_conn = true`），屏幕按钮才有效。

## 硬件接线

本实验使用板载 LCD 和触摸屏，无需额外接线。引脚与实验 08 相同：

| 信号 | GPIO |
|------|------|
| LCD SPI MOSI | GPIO40 |
| LCD SPI CLK | GPIO41 |
| LCD DC | GPIO39 |
| LCD 背光 | GPIO42 |
| LCD CS | 经 PCA9557 P0 |
| 触摸 SDA/SCL | GPIO1/GPIO2 |

BLE 使用 ESP32-S3 内置射频和板载天线，无需外接硬件。

## 代码讲解

工程目录结构：

```
10-ble_hid_device/
├── CMakeLists.txt
├── partitions.csv          # factory 3M
├── sdkconfig.defaults
└── main/
    ├── ble_hidd_demo.c     # HID 演示逻辑 + UI 按钮
    ├── ble_hidd_demo.h
    ├── esp_hidd_prf_api.c  # HIDD 应用 API 层
    ├── esp_hidd_prf_api.h
    ├── hid_dev.c           # HID report 组包与发送
    ├── hid_dev.h
    ├── hid_device_le_prf.c # GATT 服务/Report Map 实现
    ├── hidd_le_prf_int.h
    ├── esp32_s3_szp.c
    ├── esp32_s3_szp.h
    ├── idf_component.yml   # LVGL + esp_lvgl_port + ft5x06
    └── main.c
```

> HID Device Profile 的源码（`esp_hidd_prf_api.c`、`hid_dev.c`、`hid_device_le_prf.c`）是从 ESP-IDF 官方 ble_hid 示例移植的，直接编译进 main 组件。

### 关键流程

`app_main()`：

1. `bsp_i2c_init()` → `pca9557_init()` → `bsp_lvgl_start()`：启动 LVGL 界面。
2. `app_hid_ctrl()`：创建界面按钮并启动 BLE HID。

### 界面与按钮

`app_hid_ctrl()` 在屏幕中央创建两个 80×80 的触摸按钮：
- 左侧 `btn1`：音量减图标（`LV_SYMBOL_VOLUME_MID`）
- 右侧 `btn2`：音量加图标（`LV_SYMBOL_VOLUME_MAX`）

然后调用 `bt_hid_start()` 启动 BLE。

### BLE HID 启动

`bt_hid_start()`：初始化 NVS → 释放经典蓝牙内存 → 初始化 BLE 控制器和 Bluedroid → `esp_hidd_profile_init()` → 注册回调 → 配置安全参数。

### 事件状态机

- `ESP_HIDD_EVENT_REG_FINISH`：Profile 注册成功，设置设备名"HID"和广播数据。
- `ESP_GAP_BLE_ADV_DATA_SET_COMPLETE_EVT`：广播数据就绪，开始广播。
- `ESP_HIDD_EVENT_BLE_CONNECT`：主机连接，记录连接 ID。
- `ESP_GAP_BLE_SEC_REQ_EVT`：主机请求配对，自动接受。
- `ESP_GAP_BLE_AUTH_CMPL_EVT`：配对完成，置 `sec_conn = true`。
- `ESP_HIDD_EVENT_BLE_DISCONNECT`：断开后自动重新广播。

### 按钮发送 HID 报告

按钮回调逻辑（仅在 `sec_conn` 为真时有效）：
- `LV_EVENT_PRESSING`（按住期间）：发送 `esp_hidd_send_consumer_value(hid_conn_id, HID_CONSUMER_VOLUME_UP/DOWN, true)`，模拟"按住音量键"。
- `LV_EVENT_RELEASED`（松开）：发送 `false`，表示松开按键。

## 实验效果

编译烧录后：

- 屏幕显示两个音量图标按钮（左减右加）。
- 打开手机或电脑的蓝牙，搜索到名为 **"HID"** 的设备，点击配对（无需密码，Just Works）。
- 配对连接成功后，**按住**屏幕左侧按钮，主机音量减小；按住右侧按钮，主机音量增大。
- 串口打印配对信息（远端蓝牙地址、配对成功）和按键事件。
- 断开连接后，开发板自动恢复广播，可再次连接。

### 编译与烧录

```bash
idf.py set-target esp32s3
idf.py build flash monitor
```

## 注意事项

1. **配对前按钮无效**：只有在配对加密完成（`sec_conn = true`）后，点击按钮才会发送 HID 报告。
2. **iPhone 的 GATT 报错可忽略**：源码注释说明，iPhone 在 HID 加密完成前就会写 CCCD，可能出现 `GATT_INSUF_ENCRYPTION` 错误，这是正常现象，可以忽略。
3. **Win10 不支持 vendor report**：所以代码中 `SUPPORT_REPORT_VENDOR` 恒为 false。
4. **只能用 BLE**：代码释放了经典蓝牙内存，本工程不能同时使用 Classic BT。
5. **NVS 存配对信息**：如果换主机配对异常，可以考虑擦除 NVS 重新配对。
6. **工程名与目录名不一致**：根 `CMakeLists.txt` 中项目名是 `project(lvgl)`（沿用了实验 08 的工程名），不影响功能。
7. **可扩展键盘/鼠标**：代码中还有 `esp_hidd_send_keyboard_value()`、`esp_hidd_send_mouse_value()` 接口，可自行扩展成蓝牙键盘或鼠标。

## 总结

本实验演示了如何用 ESP32-S3 实现一个 BLE HID 外设，涵盖 BLE 协议栈初始化、HID Device Profile、广播配对、GATT 通信和 Consumer Control 报告发送。核心是理解 HID 的 Report Map 机制和事件驱动的配对流程。掌握后，你可以把开发板做成蓝牙键盘、鼠标、游戏手柄、多媒体遥控器等各种 HID 设备。本实验的 BLE HID 功能也会被集成到实验 14 的综合项目中。
