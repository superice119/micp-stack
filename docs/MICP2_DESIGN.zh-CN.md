# MICP 2.0 —— 信号矩阵式私有 CAN 协议(设计)

> 语言: [English](MICP2_DESIGN.md) | **中文**

## 1. 动机

真正的主机厂"私有协议"(小米/理想/大疆/西门子那一类,也是 `CanPack.rar` 所实现的)
**主要不是传输层**,而是 **CAN 之上的通信矩阵**。正如本 issue 附图所述,私有 CAN 协议
由三要素定义:

- **节点 Node** —— 报文由哪个域控制器发出(ADAS、BCM、BMS…)。
- **帧 ID Message ID** —— 每条报文的唯一 CAN 标识符(如 `0x123`)。
- **信号 Signal** —— 载荷中每个数值的含义:*起始位、长度、因子(scale)与偏移
  (offset)*(即 DBC 的 Signal 定义)。

要读懂总线就得拿到(或破解)把它们绑在一起的 **DBC**。这正是 CanPack 的模型:固定的
CAN 帧,语义存放在 ID/寄存器/信号表里。

**MICP 2.0 直接实现了这个模型**:面向 CAN / CAN FD 的信号矩阵协议,具备真正参考栈的
工程素养——纯 C11、无堆无 OS、可主机单测、有文档。线上就是原生 CAN 帧;语义存放在一张
干净、const、表驱动的通信矩阵里。

## 2. 模型

```
matrix  ──┬── nodes[]      : { name, node_id }
          └── messages[]   : { name, can_id, is_extended, dlc, cycle_ms,
                               sender_node_id, signals[] }
                              │
                              └── signals[] : { name, start_bit, bit_length,
                                                byte_order, sign,
                                                factor, offset,
                                                phys_min, phys_max, unit }
```

- **matrix(矩阵)** 是整张 DBC:全部节点与报文。
- **message(报文)** 是一条 CAN 帧:标识符、长度、标称周期、发送方。
- **signal(信号)** 是帧内一个位域,通过 `物理值 = 原始值 * factor + offset` 映射。

这与附图的 节点/帧 ID/信号 三要素一一对应,也对应 CanPack 的 COB-ID + 寄存器表思路 ——
只是用一张干净的、const、表驱动的数据库来表达,而不是手写打包。

## 3. 信号编解码 —— 核心

`micp2_signal.{h,c}` 以完整的 DBC 兼容语义把信号打包/解包进 CAN 载荷:

| 维度 | 支持 |
|---|---|
| 字节序 | **Intel**(小端,`start_bit`=LSB)与 **Motorola**(大端"锯齿",`start_bit`=MSB) |
| 位宽 | 1–64 位,可跨字节边界 |
| 符号 | 无符号或二进制补码有符号(解码时符号扩展) |
| 标定 | DBC 式 `factor`(因子)与 `offset`(偏移) |
| 饱和 | 可选 `[phys_min, phys_max]` 钳位 + 原始值范围饱和 |
| 帧长 | 最高 64 字节(CAN FD) |

两条代码路径:

- **原始路径**(`micp2_signal_pack_raw`/`unpack_raw`)—— *无浮点*,只打包整数原始位。
  适合无 FPU 的 MCU;应用可用定点预先标定。
- **物理路径**(`micp2_signal_encode`/`decode`)—— 用 `double` 应用因子/偏移。四舍五入
  采用就近远离零,且不依赖 `libm`(无需 `-lm`)。

位打包的正确性由带已知向量的单测锁定:经典 Intel `0xABC@bit4`、Motorola `0x1234@bit7`
布局,符号扩展,负偏移(`(temp+40)/0.5`),以及饱和。

## 4. 公共 API

```c
#include "micp2/micp2.h"

/* 用 const 内存一次性描述矩阵 */
static const micp2_signal_t bms_signals[] = {
    {"BattVolt",    0, 16, MICP2_BYTE_ORDER_INTEL, MICP2_UNSIGNED, 0.01, 0,    0, 655.35, "V"},
    {"BattCurrent",16, 16, MICP2_BYTE_ORDER_INTEL, MICP2_SIGNED,   0.1,  0, -3000, 3000,  "A"},
    {"SOC",        32,  8, MICP2_BYTE_ORDER_INTEL, MICP2_UNSIGNED, 0.5,  0,    0, 100,    "%"},
};
static const micp2_message_t messages[] = {
    {"BMS_Status", 0x123, 0, 6, 100, 3, bms_signals, 3},
};
static const micp2_matrix_t matrix = {"vehicle", nodes, n_nodes, messages, 1};

/* 生产方:物理值 -> CAN 字节 */
double tx[] = {401.23, -120.5, 87.0};
uint8_t frame[8];
micp2_message_encode(&messages[0], tx, frame, sizeof frame);
/* ... 把 frame/can_id 交给你的 CAN 控制器 ... */

/* 消费方:CAN 字节 -> 物理值,按 id 派发 */
micp2_matrix_dispatch(&matrix, 0x123, 0, frame, dlc, on_rx, user);
```

单信号读写(`micp2_signal_encode`/`decode`)与查找(`micp2_matrix_find_by_id`、
`micp2_message_find_signal`)同样是公共接口。

## 5. 线格式

MICP 2.0 **不**增加应用层帧头 —— 这正是重点。线上就是**原生 CAN/CAN FD 帧**:11 或
29 位标识符 + 0–64 字节载荷。含义完全来自共享矩阵,与 DBC 一模一样。这就是 CanPack
哲学,并且保留了 CAN 原生的基于 ID 的仲裁与硬件过滤。

这一层的完整性交给 CAN 控制器硬件 CRC。确认式投递、跨多帧大块传输,以及防篡改/防重放,
由 §7 路线图项(计数器/CRC 与 MAC 信号)以普通信号的形式叠加在矩阵之上来解决。

## 6. 与 CanPack、CANopen 的关系

- **与 CanPack/CANopen 相同**:固定 CAN 帧上的信号/寄存器语义;节点模型;每报文身份;
  周期发布。
- **与 CanPack 不同**:MICP 2.0 用干净、可复用、*const 表驱动*的数据库 + 经测试的通用
  编解码器来表达矩阵,而非焊死在某块板子 StdPeriph + FreeRTOS 上的手写逐报文打包。它可
  主机单测、可移植;CanPack 高效但锁定目标板。
- **与标准 CANopen 不同**:2.0 不强制完整对象字典或 CiA 的 COB-ID 分配;它是你自定义的
  *私有*矩阵,这正是主机厂场景所需。

## 7. 路线图(尚未实现)

附图同样强调**功能安全/防篡改**("保护底盘/转向/制动控制权,防止非法访问")。以下是自然的
2.x 扩展,为保持首版核心精简且经过验证而刻意留待后续:

- 报文计数器 + 校验信号(如 E2E 风格的滚动计数器 + 两个保留信号里的 CRC),用于安全帧的
  防篡改/防重放。
- 每报文认证(CMAC/MAC 信号),满足"不对非授权第三方开放"的要求。
- 一个从 `.dbc`/CSV 矩阵生成上述 const 表的小型代码生成器。

## 8. 文件

| 文件 | 作用 |
|---|---|
| `include/micp2/micp2_signal.h`、`src/micp2_signal.c` | DBC 式信号编解码 |
| `include/micp2/micp2_matrix.h`、`src/micp2_matrix.c` | 节点/报文/信号矩阵,编解码,派发 |
| `include/micp2/micp2_types.h` | 共享返回码/错误码 |
| `include/micp2/micp2.h` | 总头文件 |
| `examples/micp2_matrix_demo.c` | BMS 风格矩阵 编码→字节→解码 演示 |
| `tests/test_micp2_signal.c` | 编解码 + 矩阵单测 |

用 `make test` 或 CMake/CTest 目标构建;MICP 2.0 构建进 `libmicp` 静态库。

## 9. STM32F103RCT6 + FreeRTOS 适配性

MICP 2.0 的核心(`micp2_signal` + `micp2_matrix`)是纯 C11,**无堆、无 OS、不依赖
libm**,因此可不加修改地落到 STM32F103RCT6(Cortex-M3,64 KB Flash / 20 KB RAM,无
FPU)上。

- **无 FPU、无 libm** —— M3 没有硬件浮点。请用 **raw** 路径(`micp2_signal_pack_raw` /
  `micp2_signal_unpack_raw`)让总线通路保持纯整数;控制器以原始寄存器单位工作,仅在确实
  需要物理值的地方再调用 `factor`/`offset` 的 `*_encode`/`*_decode`。这样热路径上不会拉入
  软浮点例程。
- **占用** —— 矩阵是 `const`,节点/报文/信号存放在 Flash 而非 RAM。RAM 仅用于 8/64 字节
  帧缓冲与少量局部变量,20 KB 内绰绰有余。
- **bxCAN 绑定** —— 线上就是原生 CAN 帧。把某报文的 CAN ID + 编码字节映射为 `CAN_TxMsg`
  (bxCAN / StdPeriph 或 HAL);接收侧把收到的 `(id, data, dlc)` 直接喂给
  `micp2_matrix_dispatch`。用 bxCAN 硬件过滤器组预筛你矩阵中的 ID。
- **FreeRTOS 任务模型** —— 典型布局是一个周期 **TX 任务**(用 `vTaskDelayUntil` 编码并
  入队 10/20/100 ms 分组的周期帧)与一个 **RX 任务/ISR**(把收到的帧压入队列交给
  `micp2_matrix_dispatch`)。编解码器可重入且无状态,除保护你自己的共享应用数据外,
  encode/decode 周围无需加锁。
