# MICP — Multica 工业通信协议

> 语言:[English](README.md) | **中文**

一套紧凑、零依赖的**私有工业通信协议参考实现**,使用可移植的 **C11** 编写。MICP 的
设计对标商业化现场总线协议栈(CANopen / EtherCAT)的工程标准:不仅是一份规范文本,
而是带帧编解码、寻址会话状态机、错误检测与恢复、单元测试、示例和文档的可构建参考栈。

> 状态:`v0.1.0` 参考实现。零外部依赖。

## 特性

- **帧编解码** —— 确定性的大端序列化:12 字节头 + 变长 payload(≤ 512 B)+ CRC-16/CCITT-FALSE 校验尾。
- **字节流解析** —— SOF 重同步、跨多次读取的半帧重组、逐帧 CRC 校验。
- **会话状态机** —— `DISCONNECTED → CONNECTING → CONNECTED → ERROR`,含主动/被动建链
  (HELLO / HELLO_ACK)、心跳与对端存活超时。
- **可靠传输** —— stop-and-wait 的 ACK + 重传(带重试上限),接收侧重复帧抑制。
- **传输无关** —— 你提供一个字节输出回调;可跑在 UART、TCP、CAN-TP、共享内存之上。
  无动态内存分配,适合裸机。
- **充分测试** —— CRC、编解码、会话行为的单元测试(CTest + Makefile 备用路径),以及端到端
  loopback 示例。

## 目录结构

```
include/micp/   公共 API 头文件(micp.h 为总入口)
src/            实现(crc、frame、session、类型字符串)
tests/          单元测试(test_crc、test_frame、test_session)+ 测试框架
examples/       loopback_demo.c —— 两个节点在模拟链路上通信
docs/           PROTOCOL_SPEC、ARCHITECTURE、INTEGRATION_GUIDE、PORTING_STM32F103、COMPARISON
                (均有 .zh-CN.md 中文版)
CMakeLists.txt  主构建(CMake + CTest)
Makefile        可移植的备用构建/测试
```

## 构建与测试

### 方式 A —— CMake + CTest(推荐)

```bash
cmake -S . -B build
cmake --build build
cd build && ctest --output-on-failure
```

### 方式 B —— Makefile(无需 CMake)

```bash
make            # 构建库、测试与示例
make test       # 构建并运行全部单元测试与 loopback 示例
make demo       # 仅运行端到端示例
```

两条路径在任何测试失败时都会以非 0 退出码结束,因此可直接作为 CI 门禁。

### 运行示例

```bash
./build/micp_loopback_demo        # CMake 构建
# 或
make demo                          # Makefile 构建
```

期望末行:`Result: OK`。

## 使用库

```c
#include "micp/micp.h"

static micp_err_t my_out(void *u, const uint8_t *d, size_t n) {
    return uart_write(d, n) == (int)n ? MICP_OK : MICP_ERR_INVAL;
}
static void my_recv(void *u, uint16_t src, const uint8_t *p, size_t n) {
    handle_app_message(src, p, n);
}

micp_session_t s;
micp_session_init(&s, /*addr=*/0x0001, my_out, my_recv, /*user=*/NULL);
micp_session_connect(&s, /*peer=*/0x0002);
/* 从链路喂入字节: */ micp_session_feed(&s, rx, rx_len);
/* 周期推进定时器:  */ micp_session_tick(&s, dt_ticks);
/* 可靠发送:        */ micp_session_send(&s, data, len, /*reliable=*/1);
```

完整集成步骤见 **docs/INTEGRATION_GUIDE.zh-CN.md**,线序格式见 **docs/PROTOCOL_SPEC.zh-CN.md**。

## 嵌入式 / MCU 目标

协议栈零依赖、无堆、与 OS 无关,适合裸机与 RTOS 目标。一份 **STM32F103RCT6 + FreeRTOS**
的完整移植说明(内存预算、FreeRTOS 任务骨架、UART 传输绑定、工具链参数)见
**docs/PORTING_STM32F103.zh-CN.md**。

## 与 CanPack / CANopen 的对比

MICP 与用户提供的 **CanPack** STM32 模块、以及 **CANopen(CiA 301)** 标准的逐项
对比(定位、寻址、帧格式、可靠性、状态机、数据模型、可移植性)见
**docs/COMPARISON.zh-CN.md**。

## 面向 QA

- 主验证:`cmake -S . -B build && cmake --build build && (cd build && ctest --output-on-failure)`。
- 备用:`make test`。
- 一致性关注点:CRC 已知答案(`0x29B1`)、编解码 round-trip 与边界、握手、可靠 ACK/重传/
  耗尽、重复帧抑制、字节分片重组、CRC 错误丢弃、垃圾字节重同步、心跳与对端超时。这些与
  `tests/` 中的用例一一对应。

## 许可证

MIT —— 见 [LICENSE](LICENSE)。
