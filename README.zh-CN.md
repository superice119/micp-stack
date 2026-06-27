# MICP 2.0 —— 信号矩阵式私有 CAN 协议

> 语言:[English](README.md) | **中文**

一套紧凑、零依赖的**私有工业 CAN 协议参考实现**,使用可移植的 **C11** 编写。MICP 2.0
采用真正主机厂"私有协议"所用的模型(也是 `CanPack.rar` / CANopen 所实现的):它不是
字节传输层,而是**原生 CAN / CAN FD 帧之上的通信矩阵** —— 节点 Node、帧 ID Message、
信号 Signal(起始位、长度、因子 scale、偏移 offset),思路与 DBC 一致。对标商业现场
总线协议栈,它不仅是一份规范,更是带受测信号编解码器、const 表驱动矩阵、单元测试、示例与
文档的可构建参考栈。

> 状态:`v2.0.0` 参考实现。零外部依赖、无堆、无 OS、无 libm。

## 特性

- **DBC 式信号编解码** —— 把带标定的信号打包/解包进 CAN 载荷:Intel(小端)与
  Motorola(大端)位序、有/无符号(含符号扩展)、`factor`/`offset` 标定及钳位/饱和、
  最多 64 字节载荷(CAN FD)。提供无浮点的 **raw** 路径,适配无 FPU 的 MCU。
- **通信矩阵** —— 把总线描述为一张 `const` 表:节点 → 报文 → 信号。可按 CAN ID 查找
  报文、对整帧编码/解码,并把收到的帧派发给处理函数。
- **传输无关** —— 线上就是原生 CAN 帧(11/29 位 ID + 0–64 字节),你将其绑定到任意 CAN
  控制器。无动态内存分配,适合裸机与 RTOS。
- **充分测试** —— 编解码(位序、符号扩展、标定、钳位、边界)与矩阵(编码/解码/派发)的
  单元测试,经 CTest(及 Makefile 备用路径)运行,另有端到端矩阵示例。

## 目录结构

```
include/micp2/  公共 API 头文件(micp2.h 为总入口)
src/            实现(micp2_signal、micp2_matrix)
tests/          单元测试(test_micp2_signal)+ 测试框架
examples/       micp2_matrix_demo.c —— BMS 风格 编码 → 字节 → 解码
docs/           MICP2_DESIGN(设计 + STM32F103/FreeRTOS 适配性,含 .zh-CN.md 中文版)
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
make test       # 构建并运行单元测试与矩阵示例
make demo       # 仅运行矩阵示例
```

两条路径在任何测试失败时都会以非 0 退出码结束,因此可直接作为 CI 门禁。

### 运行示例

```bash
./build/micp2_matrix_demo          # CMake 构建
# 或
make demo                          # Makefile 构建
```

期望末行:`MICP 2.0 signal-matrix demo OK`。

## 使用库

描述一个信号并对数值编码/解码:

```c
#include "micp2/micp2.h"

/* Intel 第 4 位起的 12 位无符号信号,phys = raw * 0.1(例如 0.1 V/bit)。 */
static const micp2_signal_t volt = {
    .name        = "BattVolt",
    .start_bit   = 4,
    .bit_length  = 12,
    .byte_order  = MICP2_BYTE_ORDER_INTEL,
    .sign        = MICP2_UNSIGNED,
    .factor      = 0.1,
    .offset      = 0.0,
    .phys_min    = 0.0,
    .phys_max    = 409.5,
};

uint8_t payload[8] = {0};
micp2_signal_encode(payload, sizeof payload, &volt, 401.2); /* 物理值 → 字节 */

double v = 0.0;
micp2_signal_decode(payload, sizeof payload, &volt, &v);    /* 字节 → 物理值 */
```

也可把整条总线描述为矩阵并派发帧 —— 见 `examples/micp2_matrix_demo.c` 与
**docs/MICP2_DESIGN.zh-CN.md**。

## 嵌入式 / MCU 目标

协议栈零依赖、无堆、无 libm、与 OS 无关,适合裸机与 RTOS 目标。无浮点的 **raw** 信号
路径让它很适合无 FPU 的 MCU。一份 **STM32F103RCT6 + FreeRTOS** 适配性说明(内存预算、
FreeRTOS 任务模型、bxCAN 绑定)见 **docs/MICP2_DESIGN.zh-CN.md**。

## 与 CanPack、CANopen 的关系

MICP 2.0 采用 CanPack/CANopen 的思路 —— 固定 CAN 帧上的信号/寄存器语义 —— 但用干净、
可复用、`const` 表驱动的数据库加上受测的通用编解码器来表达矩阵,而非焊死在某块板子上的
手写打包。它不强制 CANopen 的完整对象字典或 COB-ID 分配,而是你自定义的*私有*矩阵。
详见 **docs/MICP2_DESIGN.zh-CN.md** 第 6 节。

## 面向 QA

- 主验证:`cmake -S . -B build && cmake --build build && (cd build && ctest --output-on-failure)`。
- 备用:`make test`。
- 一致性关注点:Intel/Motorola 位放置、有符号的符号扩展、factor/offset round-trip、
  min/max 钳位/饱和、越界/超长载荷拒绝,以及矩阵编码/解码/派发。这些与
  `tests/test_micp2_signal.c` 用例对应。

## 许可证

MIT —— 见 [LICENSE](LICENSE)。
