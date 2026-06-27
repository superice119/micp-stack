# MICP vs. CanPack vs. CANopen — Comparison

> Language: **English** | [中文](COMPARISON.zh-CN.md)

This document compares three things side by side:

- **MICP** — the reference stack in this repository (`src/`, `include/micp/`).
- **CanPack** — the user-supplied STM32 firmware module (`CanPack.rar`), a
  CANopen-derived stack for STM32F103 + FreeRTOS built on the ST StdPeriph CAN
  driver.
- **CANopen (CiA 301)** — the open international standard CanPack is modeled on,
  included here as the reference baseline.

The goal is to make the design choices explicit, not to rank them: MICP and
CanPack solve **different problems at different layers**, and CANopen is the
mature standard both can be measured against.

---

## 1. At a glance

| Axis | MICP (this repo) | CanPack (`CanPack.rar`) | CANopen (CiA 301) |
|---|---|---|---|
| Positioning | Transport/session reference stack, transport-agnostic | Application/device-network stack for one product family | Open device-network standard |
| Lower layer | Any reliable/unreliable byte stream (UART, TCP, RF…) | CAN 2.0B (29-bit extended ID) only | CAN 2.0A/B |
| Dependencies | None (pure C11, no OS/heap/float) | STM32 StdPeriph + FreeRTOS + project BSP | Implementation-defined |
| Addressing | 16-bit `src`/`dst` node addresses | 8-bit node-id packed into the CAN ID | 7-bit node-id in 11-bit COB-ID |
| Framing | Self-described 12 B header + payload + CRC-16 | Native CAN frame; semantics in the 29-bit ID | COB-ID + ≤8 B; semantics in COB-ID |
| Integrity | CRC-16/CCITT-FALSE over the whole frame | CAN hardware CRC only (no app-layer CRC) | CAN hardware CRC only |
| Reliability | Stop-and-wait ARQ (ACK/NACK + retransmit) | SDO has timeout+abort; PDO is fire-and-forget | SDO confirmed; PDO unconfirmed |
| State machine | DISCONNECTED / CONNECTING / CONNECTED / ERROR (per peer) | Init / Pre-op / Operational / Stopped (NMT) | Identical NMT states |
| Liveness | Heartbeat with miss-count → ERROR | Node-guarding heartbeat + soft timers | Heartbeat / node-guarding |
| Data model | Opaque payload (app defines meaning) | PDO/SDO over a register table | Object Dictionary (16-bit index/8-bit sub) |
| Roles | Symmetric peer-to-peer | Master / slave (`MyIdentity`) | Master(NMT)/slave; producer/consumer |
| Tests/build | CMake+CTest & Makefile, host unit tests, ASan/UBSan | On-target firmware, no host test harness | N/A |

---

## 2. What each one actually is

**MICP** is a *transport-and-session* reference stack. It does not assume CAN, an
RTOS, or any hardware. It gives you: a self-describing wire frame with an
end-to-end CRC, an addressed connection between two endpoints, a handshake,
heartbeats, and **stop-and-wait reliable delivery** with explicit ACK/NACK and
retransmission. What the bytes *mean* is left to the application. Think of it as
"a portable, testable connection layer you can drop on top of any link."

**CanPack** is an *application/device-network* stack for a specific product (a
master plus power / key-display / data-measurement slave boards). It is a
hand-written CANopen-style implementation: it has **NMT** (network management),
**PDO** (process data, cyclic/event), **SDO** (service data, confirmed register
read/write with quick + block modes), **SYNC**, **EMCY** (emergency), and
**node-guarding/heartbeat** driven by a soft-timer subsystem. It is wired
directly to the STM32 CAN peripheral and FreeRTOS (ISR → task-notify → dispatch).

**CANopen (CiA 301)** is the open standard that CanPack mirrors: the same NMT
state machine, the PDO/SDO/SYNC/EMCY service set, and the **Object Dictionary**
as the canonical data model, with a standardized COB-ID allocation.

> Key consequence: **MICP ≈ a transport layer; CanPack/CANopen ≈ an application
> profile on top of CAN.** They are largely *complementary*, not direct
> substitutes. A fair comparison looks at each capability axis rather than
> "which is better."

---

## 3. Addressing and frame format

**MICP.** Every frame is self-described and link-independent: a fixed 12-byte
header — SOF `0xA5`, version, type, flags, 16-bit `src`, 16-bit `dst`, 16-bit
`seq`, 16-bit `len` — followed by ≤512 B payload and a trailing
**CRC-16/CCITT-FALSE** over header+payload (big-endian, byte-wise). Because the
frame carries its own length and CRC, MICP can run over a raw byte stream and
re-synchronize on the SOF byte after corruption. Source/destination are **16-bit**,
so the address space is large and not tied to CAN.

**CanPack.** There is no application-layer frame: it uses the **native CAN frame**
and packs semantics into the **29-bit extended ID**, partitioned as
`[31:24] function-code | [23:16] node-id | [15:8] command | [7:0] length`. The
8-byte CAN payload carries the actual data. Integrity relies entirely on the
**CAN controller's hardware CRC** — there is no additional application CRC. The
node-id is **8-bit**; the function-code byte selects NMT/SYNC/EMCY/PDO1-4/SDO/
node-guard.

**CANopen.** Standard CANopen instead uses the **11-bit COB-ID** = function-code
(4 bits) + node-id (7 bits), capping the bus at 127 nodes. CanPack deliberately
**widens this** to a 29-bit extended ID to fit a custom function/command/length
layout and up to 255 nodes — so CanPack is *CANopen-inspired* but **not
wire-compatible** with standard CANopen tooling.

---

## 4. Reliability and error handling

**MICP.** Reliability is a first-class, generic mechanism: **stop-and-wait ARQ**.
A reliable DATA frame is held as the single in-flight frame until its ACK
arrives; on NACK or `rto_ticks` timeout it is retransmitted up to `max_retries`,
after which the session enters **ERROR**. Integrity is end-to-end via the frame
CRC, and — following the QA fix recorded in `PROTOCOL_SPEC.md` §7.1 — a CONNECTED
session validates inbound `src == peer_addr`, so foreign DATA is dropped and
spoofed ACKs cannot false-clear the pending frame (counted in `stats.rx_dropped`).

**CanPack.** Reliability is **per-service**, mirroring CANopen:
- **SDO** is *confirmed* — each transfer has a timeout (`QuickSDOtimeout`) and an
  abort/error path, with quick (≤6 data bytes inline) and block (multi-frame)
  modes, tracked in a per-node `transfer[]` table.
- **PDO** is *fire-and-forget* (no ack) — matching CANopen process-data semantics.
- **Liveness** is node-guarding/heartbeat: each known node has a soft timer; a
  miss marks the node `Disconnected` and fires a callback. There is no
  application CRC and no generic retransmit for arbitrary data — integrity is the
  CAN hardware's job, and only SDO is individually confirmed.

**CANopen.** Same split: SDO confirmed with abort codes; PDO unconfirmed;
heartbeat/node-guarding for liveness; EMCY for fault broadcast. CanPack follows
this model closely and adds its own EMCY error codes (`NodeVoltageErr`,
`NodeTempErr`, …).

---

## 5. State machine and roles

Both stacks are state-machine driven, but at different scopes.

- **MICP** runs a **per-connection** state machine
  (DISCONNECTED→CONNECTING→CONNECTED→ERROR) and is **symmetric**: either side may
  initiate the HELLO handshake; there is no master/slave asymmetry.
- **CanPack/CANopen** run the **NMT node** state machine
  (Initialisation→Pre-operational→Operational→Stopped) and are **master/slave**:
  `MyIdentity` selects role, the master issues NMT state-change commands and reads
  node states, slaves report boot-up and heartbeat. State transitions gate which
  services are active (e.g. PDO only in Operational), exactly like CANopen.

---

## 6. Data model

- **MICP** treats the payload as **opaque**: the protocol moves addressed,
  integrity-checked, optionally-reliable byte blobs and leaves interpretation to
  the application. There is no object dictionary.
- **CanPack** uses a **register/PDO-SDO table** model (`structPDO_Data` /
  `structSDO_Data` with `var_addr`/`var_size`), a pragmatic, lightweight stand-in
  for a full Object Dictionary, indexed per node.
- **CANopen** mandates the full **Object Dictionary** (16-bit index + 8-bit
  sub-index) with standardized communication and device profiles — the richest
  and most interoperable of the three, at the cost of footprint and complexity.

---

## 7. Portability, footprint, dependencies

- **MICP** is pure **C11** with **no** OS, heap, floating-point, or 64-bit
  dependencies; ~1.2 KB RAM per session; builds and unit-tests on a host (CMake +
  CTest, Makefile, `-Werror`, ASan/UBSan) and ports to STM32F103 + FreeRTOS as
  documented in `PORTING_STM32F103.md`. The link layer is injected, so the *same*
  code runs over UART, TCP, or RF.
- **CanPack** is **tightly coupled** to its target: STM32 StdPeriph (`CAN_Transmit`,
  `CanRxMsg`), FreeRTOS (queues, task notifications, software timers), and
  project-specific BSP/user headers (`Userbsp.h`, `UserObj_Create.h`). This makes
  it efficient and ready-to-run **on that board**, but it cannot be unit-tested on
  a host as-is and is not portable off CAN/STM32/FreeRTOS without rework. Comments
  are GBK-encoded.
- **CANopen** portability depends on the implementation (e.g. CANopenNode,
  CanFestival); the standard itself is CAN-bound.

---

## 8. Takeaways

1. **Different layers.** MICP is a transport/session layer; CanPack and CANopen
   are application/device-network profiles bound to CAN. The honest comparison is
   capability-by-capability, not "winner takes all."
2. **CanPack is a custom CANopen profile.** It reuses CANopen's NMT/PDO/SDO/SYNC/
   EMCY/heartbeat model but **redefines the wire format** (29-bit extended ID,
   8-bit node-id up to 255, register table instead of a full OD), so it is **not
   interoperable** with standard CANopen masters/tools.
3. **Integrity & reliability differ in kind.** MICP adds an **application-layer
   CRC and a generic stop-and-wait ARQ** for *all* reliable data; CanPack/CANopen
   rely on the **CAN hardware CRC** and confirm only **SDO**, leaving PDO
   best-effort.
4. **Portability & testability.** MICP is dependency-free and host-testable;
   CanPack is efficient but locked to STM32 + FreeRTOS + StdPeriph and has no host
   test harness.
5. **Complementary, not competing.** If a product is CAN-only and needs the
   CANopen service model on STM32 today, CanPack (or a real CANopen stack) fits.
   If you need a portable, transport-agnostic, CRC-protected, reliably-delivered
   session layer that you can also run and test off-target, MICP fits — and MICP
   could even ride on top of a CAN link as one of its transports.

For MICP's normative details see [`PROTOCOL_SPEC.md`](PROTOCOL_SPEC.md); for the
STM32 target see [`PORTING_STM32F103.md`](PORTING_STM32F103.md).
