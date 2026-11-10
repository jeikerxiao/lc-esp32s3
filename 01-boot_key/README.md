# 实验 01：BOOT 按键中断

## 简介

本实验演示如何通过 GPIO 中断检测立创·实战派 ESP32-S3 开发板上 BOOT 按键的按下动作。按下按键后，串口终端会打印一行提示信息。这是学习 ESP32-S3 GPIO 中断和 FreeRTOS 队列协作的经典入门实验。

## 原理

### GPIO 中断

ESP32-S3 的 GPIO 外设支持中断功能。当引脚电平发生指定方向的变化（上升沿、下降沿或双沿）时，芯片会自动调用预先注册的中断服务函数（ISR）。

本实验中，BOOT 按键一端接 GPIO0，另一端接地。GPIO0 配置为内部上拉输入，空闲时电平为高（1）；按下按键后引脚被拉到低电平（0），产生一个下降沿，触发中断。

### 中断 + 队列 + 任务

在中断服务函数中不应执行耗时操作（如串口打印），否则会影响系统实时性。正确做法是：

1. ISR 中仅将事件投递到一个 FreeRTOS 队列；
2. 一个独立的任务阻塞等待队列消息，收到后再执行打印等慢操作。

这就是嵌入式系统中经典的"顶半部 / 底半部"思想。

## 硬件接线

本实验无需额外接线，使用板载 BOOT 按键即可。

| 信号 | GPIO | 说明 |
|------|------|------|
| BOOT 按键 | GPIO0 | 按下接地，内部上拉，下降沿中断 |

> 提示：GPIO0 是 ESP32-S3 的 strapping 引脚（用于选择启动模式），运行时可安全作为按键输入；但如果在复位瞬间按住 BOOT 键，芯片会进入下载模式。

## 代码讲解

工程目录结构：

```
01-boot_key/
├── CMakeLists.txt
├── sdkconfig.defaults
└── main/
    ├── CMakeLists.txt
    └── main.c
```

### 关键流程

`main.c` 中 `app_main()` 执行以下步骤：

1. **配置 GPIO0**：使用 `gpio_config()` 设置为输入模式、内部上拉、下降沿中断（`GPIO_INTR_NEGEDGE`）。
2. **创建事件队列**：`xQueueCreate(10, sizeof(uint32_t))`，深度为 10。
3. **创建处理任务**：`xTaskCreate(gpio_task_example, ..., 2048, ..., 10, ...)`，栈大小 2048 字节，优先级 10。
4. **安装 GPIO 中断服务**：`gpio_install_isr_service(0)`。
5. **注册中断处理函数**：`gpio_isr_handler_add(GPIO_NUM_0, gpio_isr_handler, (void*)GPIO_NUM_0)`。

### 中断服务函数

```c
static void IRAM_ATTR gpio_isr_handler(void *arg)
{
    uint32_t gpio_num = (uint32_t)arg;
    xQueueSendFromISR(gpio_evt_queue, &gpio_num, NULL);
}
```

- 必须加 `IRAM_ATTR`，确保函数在 IRAM 中执行（中断时 Flash 可能不可访问）。
- 只能使用带 `FromISR` 后缀的 FreeRTOS API。

### 处理任务

```c
static void gpio_task_example(void *arg)
{
    uint32_t io_num;
    while (1) {
        xQueueReceive(gpio_evt_queue, &io_num, portMAX_DELAY);
        printf("GPIO[%"PRIu32"] intr, val: %d\n", io_num, gpio_get_level(io_num));
    }
}
```

任务阻塞等待队列，收到 GPIO 编号后读取当前电平并打印。

## 实验效果

编译烧录后打开串口监视器（波特率 115200），每按一次 BOOT 键会看到：

```
GPIO[0] intr, val: 0
```

松开按键（上升沿）不触发打印，因为只配置了下降沿中断。

### 编译与烧录

```bash
idf.py set-target esp32s3
idf.py build flash monitor
```

## 注意事项

1. **只检测下降沿**：当前配置只在"按下"时触发。若想同时检测松开，需改为 `GPIO_INTR_ANYEDGE`。
2. **无软件消抖**：机械按键在按下/松开瞬间会产生抖动，可能触发多次中断。本实验的队列 + 任务模式天然缓解了一部分抖动问题，但严格应用仍需加消抖逻辑（如延时确认或硬件 RC 消抖）。
3. **GPIO0 的特殊性**：复位时按住 BOOT 键会让芯片进入下载模式，这是 ESP32-S3 的正常行为，不影响运行时使用。
4. **ISR 中禁止耗时操作**：不要在中断函数里调用 `printf`、`vTaskDelay` 等非 ISR 安全的函数。

## 总结

本实验通过最简单的 BOOT 按键，演示了 ESP32-S3 GPIO 中断的完整使用流程：配置引脚 → 安装中断服务 → 注册 ISR → 队列传递事件 → 任务处理。掌握了这套模式，后续连接外部按键、传感器中断引脚等场景都可以直接套用。
