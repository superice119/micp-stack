# 将 MICP 移植到 STM32F103RCT6 + FreeRTOS

> 语言:[English](PORTING_STM32F103.md) | **中文**

本指南介绍如何在 **STM32F103RCT6**(ARM Cortex-M3 @ 72 MHz,**48 KB SRAM**,
**256 KB Flash**)上、于 **FreeRTOS** 之下运行 MICP 参考栈。该栈是与传输/OS 无关的 C11,
因此移植只是集成工作 —— **无需改动库源码**。

---

## 1. 为何适配该目标

MICP 为受限 MCU 而设计,在此目标上**没有移植阻断项**:

| 关注点                 | MICP                                                                 |
|------------------------|---------------------------------------------------------------------|
| 动态内存               | **无** —— 任何地方都不用 `malloc`/`free`;所有状态都在固定结构体中    |
| OS / RTOS 依赖         | **无** —— 无线程、无系统调用;时间经 `tick()` 注入                    |
| 浮点                   | **无** —— 纯整型(M3 无 FPU;不会拉入软浮点)                         |
| 64 位运算              | **无** —— 仅 8/16/32 位                                              |
| 库头文件               | `stdint.h`、`stddef.h`、`string.h`(`memcpy/memmove/memset`)—— arm-none-eabi newlib 全部提供 |
| 字节序                 | 帧编解码按字节大端;**不**依赖主机字节序(M3 为小端,无需改动即可工作) |
| 语言                   | 纯 **C11** —— `arm-none-eabi-gcc -std=c11` 可直接构建                |

## 2. 内存预算(RCT6:48 KB SRAM / 256 KB Flash)

默认配置(`MICP_MAX_PAYLOAD = 512`):

| 项目                                   | 大小            | 说明                                   |
|----------------------------------------|-----------------|----------------------------------------|
| `sizeof(micp_session_t)`(每 peer)     | **~1192 B**     | 526 B 接收重组 + 526 B 重传缓冲为主    |
| `sizeof(micp_frame_t)`(瞬态)          | 524 B           | 编/解码期间位于栈上                    |
| `feed()` → `handle_frame` 的栈峰值     | **~1.0–1.5 KB** | 帧结构体 + 编码缓冲                    |
| 库代码(`.text`)                       | 数 KB           | 按位 CRC、编解码、会话 FSM             |

在 RCT6 的 48 KB SRAM 上这非常宽裕:单会话约占 SRAM 的 2.5%。即便若干并发会话加上任务栈
也能轻松容纳。

> **FreeRTOS 任务栈:** 调用 `micp_session_feed()` / `micp_session_send()` 的任务栈应设为
> **≥ 2 KB**(≥ 512 字)。**不要**使用默认的 `configMINIMAL_STACK_SIZE` —— 瞬态的
> `micp_frame_t` + 编码缓冲需要余量。可在压测后用 `uxTaskGetStackHighWaterMark()` 校验。

### 进一步压缩 RAM(可选)

`MICP_MAX_PAYLOAD`(位于 `include/micp/micp_types.h`)同时决定两个每会话缓冲。RCT6 无需
如此,仅供参考:

| `MICP_MAX_PAYLOAD` | `sizeof(micp_session_t)`(约) |
|--------------------|-------------------------------|
| 512(默认)         | ~1192 B                       |
| 256                | ~680 B                        |
| 128                | ~424 B                        |
| 64                 | ~296 B                        |

两端必须使用相同的 `MICP_MAX_PAYLOAD`(它约束线序 `LEN` 字段)。

## 3. FreeRTOS 下的时序模型

MICP 无内部时钟。请从一个周期性 FreeRTOS 任务驱动它,并用 **FreeRTOS tick**(或任何你
传给 `tick()` 的一致单位)表达所有超时:

- 在常见的 `configTICK_RATE_HZ = 1000`(1 ms tick)下,默认值映射为:
  `rto_ticks=10` → 10 ms、`heartbeat_ticks=30` → 30 ms、`peer_timeout_ticks=100` → 100 ms。
- 依你的链路 RTT 和调用 `tick()` 的频率来调整。若 `tick()` 周期更粗(如每 10 ms),则应
  传 `dt = 10` 并相应缩放超时字段。

## 4. 传输绑定(UART 示例)

MICP 通过 `output` 回调输出字节,通过 `feed()` 消费字节。典型的 STM32 HAL + FreeRTOS 接线:

**输出(TX):** 将 `output` 接到 UART 发送。优先使用非阻塞路径(中断/DMA + TX 环形缓冲),
使协议任务不被阻塞。

```c
static micp_err_t uart_output(void *user, const uint8_t *data, size_t len) {
    /* 阻塞版 —— 最简单;生产中换成 IT/DMA + 环形缓冲。 */
    if (HAL_UART_Transmit(&huart1, (uint8_t *)data, (uint16_t)len, 100) == HAL_OK)
        return MICP_OK;
    return MICP_ERR_INVAL;
}
```

**输入(RX):** 从 UART RX 中断(或 DMA idle)把字节经 FreeRTOS 流/队列送进协议任务,由其
调用 `feed()`:

```c
/* ISR:HAL_UART_RxCpltCallback 把每个字节推入流缓冲 */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *h) {
    BaseType_t woken = pdFALSE;
    xStreamBufferSendFromISR(rx_stream, &rx_byte, 1, &woken);
    HAL_UART_Receive_IT(&huart1, &rx_byte, 1);   /* 重新装填 */
    portYIELD_FROM_ISR(woken);
}
```

## 5. 协议任务骨架

```c
#include "micp/micp.h"

static micp_session_t s;

static void app_recv(void *u, uint16_t src, const uint8_t *p, size_t n) {
    (void)u; (void)src;
    /* 把 p[0..n) 投递给你的应用 */
}

void micp_task(void *arg) {
    (void)arg;
    micp_session_init(&s, /*addr=*/0x0001, uart_output, app_recv, NULL);

    /* 超时单位为 tick;1 ms tick 下即毫秒 */
    s.rto_ticks = 10; s.max_retries = 3;
    s.heartbeat_ticks = 30; s.peer_timeout_ticks = 100;

    micp_session_connect(&s, /*peer=*/0x0002);

    uint8_t buf[64];
    TickType_t last = xTaskGetTickCount();
    for (;;) {
        /* 1) 排空收到的字节(非阻塞) */
        size_t n = xStreamBufferReceive(rx_stream, buf, sizeof(buf), 0);
        if (n) micp_session_feed(&s, buf, n);

        /* 2) 大约每毫秒推进一次协议时间 */
        TickType_t now = xTaskGetTickCount();
        if (now != last) { micp_session_tick(&s, (uint32_t)(now - last)); last = now; }

        /* 3) 应用 TX(可靠示例) */
        /* if (!micp_session_tx_busy(&s)) micp_session_send(&s, msg, len, 1); */

        vTaskDelay(pdMS_TO_TICKS(1));
    }
}
```

以 ≥ 2 KB 栈创建:

```c
xTaskCreate(micp_task, "micp", 512 /*字 = 2KB*/, NULL, tskIDLE_PRIORITY + 2, NULL);
```

## 6. 并发规则

`micp_session_t` **不**做内部同步。请从**单个任务**驱动每个会话(如上)。若 `send()` 必须从
其他任务调用,则把 payload 经队列交给协议任务,或用互斥锁保护所有会话调用
(见 `INTEGRATION_GUIDE.zh-CN.md` §4 / §9)。不同会话彼此独立。

## 7. 为目标平台构建

用你的 ARM 工具链编译四个库源文件并把 `include/` 加入头路径 —— 无需任何 MICP 专属编译标志:

```
arm-none-eabi-gcc -mcpu=cortex-m3 -mthumb -std=c11 -Os \
    -Iinclude \
    -c src/micp_crc.c src/micp_frame.c src/micp_session.c src/micp_types.c
```

将生成的目标文件与你的 FreeRTOS + HAL 工程链接。仓库自带的 `CMakeLists.txt` / `Makefile`
面向宿主构建(单元测试 + 示例),不用于固件镜像。

## 8. 验证状态

上述可移植性论断已通过源码审查(无堆 / OS / FPU / 64 位使用)与宿主测试套件
(CMake/CTest + Makefile,含 `-Werror -fsanitize=address,undefined`)验证。片上 HAL/DMA
绑定属于固件工程的集成工作;本指南提供完成该工作的契约与骨架。
