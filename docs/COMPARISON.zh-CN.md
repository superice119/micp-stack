# MICP 与 CanPack、CANopen 对比

> 语言: [English](COMPARISON.md) | **中文**

本文把三者并排对比:

- **MICP** —— 本仓库的参考实现(`src/`、`include/micp/`)。
- **CanPack** —— 用户提供的 STM32 固件模块(`CanPack.rar`),一个面向
  STM32F103 + FreeRTOS、基于 ST 标准外设库 CAN 驱动的、CANopen 衍生的协议栈。
- **CANopen(CiA 301)** —— CanPack 所对标的国际开放标准,作为基准引入。

目的是把各自的设计取舍讲清楚,而不是排座次:MICP 与 CanPack 解决的是
**不同层次的不同问题**,而 CANopen 是二者都可对照的成熟标准。

---

## 1. 总览

| 维度 | MICP(本仓库) | CanPack(`CanPack.rar`) | CANopen(CiA 301) |
|---|---|---|---|
| 定位 | 与传输无关的传输/会话层参考栈 | 面向某一产品族的应用/设备网络栈 | 开放的设备网络标准 |
| 下层 | 任意可靠/不可靠字节流(UART、TCP、RF…) | 仅 CAN 2.0B(29 位扩展 ID) | CAN 2.0A/B |
| 依赖 | 无(纯 C11,无 OS/堆/浮点) | STM32 标准外设库 + FreeRTOS + 工程 BSP | 由实现决定 |
| 寻址 | 16 位 `src`/`dst` 节点地址 | 8 位 node-id 打包进 CAN ID | 11 位 COB-ID 中的 7 位 node-id |
| 成帧 | 自描述 12B 头 + 负载 + CRC-16 | 原生 CAN 帧;语义在 29 位 ID 中 | COB-ID + ≤8B;语义在 COB-ID 中 |
| 完整性 | 整帧 CRC-16/CCITT-FALSE | 仅 CAN 硬件 CRC(无应用层 CRC) | 仅 CAN 硬件 CRC |
| 可靠性 | 停等 ARQ(ACK/NACK + 重传) | SDO 带超时+中止;PDO 即发即忘 | SDO 确认;PDO 不确认 |
| 状态机 | DISCONNECTED/CONNECTING/CONNECTED/ERROR(每对等端) | Init/Pre-op/Operational/Stopped(NMT) | 同样的 NMT 状态 |
| 存活检测 | 心跳 + 丢失计数 → ERROR | 节点守护心跳 + 软定时器 | 心跳/节点守护 |
| 数据模型 | 不透明负载(应用定义语义) | 寄存器表上的 PDO/SDO | 对象字典(16 位索引/8 位子索引) |
| 角色 | 对称的点对点 | 主/从(`MyIdentity`) | 主(NMT)/从;生产者/消费者 |
| 测试/构建 | CMake+CTest 与 Makefile,主机单测,ASan/UBSan | 目标板固件,无主机测试框架 | 不适用 |

---

## 2. 三者各自是什么

**MICP** 是一个*传输与会话*参考栈。它不假定 CAN、不假定 RTOS、不假定任何硬件。
它提供:带端到端 CRC 的自描述帧、两个端点之间的带地址连接、握手、心跳,以及
带显式 ACK/NACK 与重传的**停等可靠传输**。字节的*含义*交给应用决定。可以把它
理解为"一个可移植、可测试、能叠加在任意链路之上的连接层"。

**CanPack** 是面向具体产品(一个主站 + 电源板/按键显示板/数据测量板等从站)的
*应用/设备网络*栈。它是手写的 CANopen 风格实现:具备 **NMT**(网络管理)、
**PDO**(过程数据,周期/事件)、**SDO**(服务数据,带 quick + block 模式的确认式
寄存器读写)、**SYNC**(同步)、**EMCY**(紧急)以及由软定时器子系统驱动的
**节点守护/心跳**。它直接对接 STM32 CAN 外设和 FreeRTOS(中断 → 任务通知 → 派发)。

**CANopen(CiA 301)** 是 CanPack 所镜像的开放标准:相同的 NMT 状态机,
PDO/SDO/SYNC/EMCY 服务集,以**对象字典**为规范数据模型,并有标准化的 COB-ID 分配。

> 关键结论:**MICP ≈ 传输层;CanPack/CANopen ≈ CAN 之上的应用规约。**
> 二者很大程度上是*互补*的,而非直接替代。公平的对比是按能力维度逐项看,
> 而不是"谁更好"。

---

## 3. 寻址与帧格式

**MICP。** 每帧都自描述、与链路无关:固定 12 字节头 —— SOF `0xA5`、版本、类型、
标志、16 位 `src`、16 位 `dst`、16 位 `seq`、16 位 `len` —— 后跟 ≤512B 负载,以及
对 头+负载 计算的 **CRC-16/CCITT-FALSE** 尾校验(大端、逐字节)。由于帧自带长度
与 CRC,MICP 可以跑在裸字节流上,并在数据损坏后基于 SOF 字节重新同步。源/目的为
**16 位**,地址空间大且不绑定 CAN。

**CanPack。** 没有应用层帧:它用**原生 CAN 帧**,把语义打包进 **29 位扩展 ID**,
划分为 `[31:24] 功能码 | [23:16] node-id | [15:8] 命令字 | [7:0] 长度`。8 字节 CAN
负载承载实际数据。完整性**完全依赖 CAN 控制器的硬件 CRC** —— 没有额外的应用层
CRC。node-id 为 **8 位**;功能码字节选择 NMT/SYNC/EMCY/PDO1-4/SDO/节点守护。

**CANopen。** 标准 CANopen 使用 **11 位 COB-ID** = 功能码(4 位)+ node-id(7 位),
将总线节点上限定为 127。CanPack 有意**拓宽**为 29 位扩展 ID,以容纳自定义的
功能/命令/长度布局和最多 255 个节点 —— 因此 CanPack 是*受 CANopen 启发*的,但
**与标准 CANopen 工具不在线兼容**。

---

## 4. 可靠性与错误处理

**MICP。** 可靠性是一等的通用机制:**停等 ARQ**。一个可靠 DATA 帧作为唯一在途帧
被保留,直到收到其 ACK;收到 NACK 或 `rto_ticks` 超时则重传,最多 `max_retries`
次,之后会话进入 **ERROR**。完整性由帧 CRC 端到端保证;并且 —— 按 `PROTOCOL_SPEC.md`
§7.1 记录的 QA 修复 —— CONNECTED 会话会校验入站 `src == peer_addr`,因此外来 DATA
被丢弃、伪造 ACK 无法误清在途帧(计入 `stats.rx_dropped`)。

**CanPack。** 可靠性是**分服务**的,与 CANopen 一致:
- **SDO** 是*确认式*的 —— 每次传输带超时(`QuickSDOtimeout`)和中止/错误路径,
  含 quick(≤6 字节内联)和 block(多帧)模式,由每节点的 `transfer[]` 表跟踪。
- **PDO** 是*即发即忘*的(无 ack)—— 与 CANopen 过程数据语义一致。
- **存活检测**为节点守护/心跳:每个已知节点一个软定时器;丢失则把节点标为
  `Disconnected` 并触发回调。没有应用层 CRC,也没有对任意数据的通用重传 ——
  完整性交给 CAN 硬件,只有 SDO 单独被确认。

**CANopen。** 同样的划分:SDO 带中止码确认;PDO 不确认;心跳/节点守护做存活检测;
EMCY 做故障广播。CanPack 紧贴该模型,并加入了自有 EMCY 错误码
(`NodeVoltageErr`、`NodeTempErr` 等)。

---

## 5. 状态机与角色

两个栈都由状态机驱动,但作用域不同。

- **MICP** 跑**每连接**状态机(DISCONNECTED→CONNECTING→CONNECTED→ERROR),且
  **对称**:任一端都可发起 HELLO 握手,不存在主/从不对称。
- **CanPack/CANopen** 跑 **NMT 节点**状态机
  (Initialisation→Pre-operational→Operational→Stopped),且**主/从**:`MyIdentity`
  选择角色,主站下发 NMT 状态切换命令并读取节点状态,从站上报 boot-up 与心跳。
  状态切换决定哪些服务被激活(例如 PDO 仅在 Operational 下),与 CANopen 完全一致。

---

## 6. 数据模型

- **MICP** 把负载视为**不透明**:协议搬运带地址、带完整性校验、可选可靠的字节块,
  解释权交给应用。没有对象字典。
- **CanPack** 使用**寄存器/PDO-SDO 表**模型(`structPDO_Data`/`structSDO_Data`,
  含 `var_addr`/`var_size`),是对完整对象字典的一种务实、轻量的替代,按节点索引。
- **CANopen** 强制完整的**对象字典**(16 位索引 + 8 位子索引),带标准化的通信与
  设备规约 —— 三者中最丰富、最具互操作性,代价是占用与复杂度。

---

## 7. 可移植性、占用与依赖

- **MICP** 是纯 **C11**,**无** OS/堆/浮点/64 位依赖;每会话约 1.2KB RAM;可在主机上
  构建与单测(CMake + CTest、Makefile、`-Werror`、ASan/UBSan),并按
  `PORTING_STM32F103.md` 移植到 STM32F103 + FreeRTOS。链路层是注入的,所以*同一份*
  代码可跑在 UART、TCP 或 RF 上。
- **CanPack** 与其目标**强耦合**:STM32 标准外设库(`CAN_Transmit`、`CanRxMsg`)、
  FreeRTOS(队列、任务通知、软件定时器)以及工程专有 BSP/用户头文件(`Userbsp.h`、
  `UserObj_Create.h`)。这让它**在那块板子上**高效且开箱即用,但无法照原样在主机
  单测,脱离 CAN/STM32/FreeRTOS 也无法直接移植。注释为 GBK 编码。
- **CANopen** 的可移植性取决于具体实现(如 CANopenNode、CanFestival);标准本身绑定 CAN。

---

## 8. 结论

1. **层次不同。** MICP 是传输/会话层;CanPack 与 CANopen 是绑定 CAN 的应用/设备
   网络规约。诚实的对比是逐能力比较,而非"赢者通吃"。
2. **CanPack 是定制的 CANopen 规约。** 它复用 CANopen 的 NMT/PDO/SDO/SYNC/EMCY/
   心跳模型,但**重新定义了线格式**(29 位扩展 ID、8 位 node-id 至 255、用寄存器表
   代替完整对象字典),因此**与标准 CANopen 主站/工具不互操作**。
3. **完整性与可靠性性质不同。** MICP 为*所有*可靠数据增加了**应用层 CRC 与通用停等
   ARQ**;CanPack/CANopen 依赖 **CAN 硬件 CRC**,且只确认 **SDO**,PDO 为尽力而为。
4. **可移植性与可测性。** MICP 无依赖且可主机测试;CanPack 高效但锁定在
   STM32 + FreeRTOS + 标准外设库,且无主机测试框架。
5. **互补而非竞争。** 若产品只用 CAN 且当下就要在 STM32 上用 CANopen 服务模型,
   CanPack(或真正的 CANopen 栈)合适。若你需要一个可移植、与传输无关、带 CRC 保护、
   可靠投递的会话层,并且能脱离目标板运行与测试,MICP 合适 —— 而且 MICP 甚至可以把
   CAN 链路作为它的一种传输来叠加运行。

MICP 的规范细节见 [`PROTOCOL_SPEC.zh-CN.md`](PROTOCOL_SPEC.zh-CN.md);STM32 目标见
[`PORTING_STM32F103.zh-CN.md`](PORTING_STM32F103.zh-CN.md)。
