# MICP 2.0 — Signal-Matrix Private CAN Protocol (Design)

> Language: **English** | [中文](MICP2_DESIGN.zh-CN.md)

## 1. Motivation

Real OEM "private protocols" (the kind Xiaomi / Li Auto / DJI / Siemens build,
and the kind `CanPack.rar` implements) are **not** primarily a transport — they
are a **communication matrix on top of CAN**. As the reference notes attached to
this issue put it, a private CAN protocol is defined by:

- **Node** — which controller a frame comes from (ADAS, BCM, BMS, …).
- **Message ID** — the unique CAN identifier of each frame (e.g. `0x123`).
- **Signal** — the meaning of each value inside the payload: *start bit, length,
  scale (factor) and offset* (the DBC `Signal` definition).

To read the bus you need (or must reverse-engineer) the **DBC** that ties these
together. That is exactly the CanPack model: fixed CAN frames whose semantics
live in an ID/register/signal table.

**MICP 2.0 implements this model directly**: a signal-matrix protocol for
CAN / CAN FD, with the engineering discipline of a real reference stack — pure
C11, no heap/OS, host-testable and documented. The wire is the native CAN frame;
meaning lives in a clean, const, table-driven communication matrix.

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

- A **matrix** is the whole DBC: the set of nodes and messages.
- A **message** is one CAN frame: identifier, length, nominal cycle, sender.
- A **signal** is one bit-field inside a frame, mapped to a physical value by
  `physical = raw * factor + offset`.

This maps 1:1 onto the Node / Message ID / Signal triad in the reference notes,
and onto CanPack's COB-ID + register-table idea — but expressed as a clean,
const, table-driven database instead of hand-coded packing.

## 3. Signal codec — the core

`micp2_signal.{h,c}` packs/unpacks a signal into a CAN payload with full
DBC-compatible semantics:

| Aspect | Support |
|---|---|
| Byte order | **Intel** (little-endian, `start_bit` = LSB) and **Motorola** (big-endian "sawtooth", `start_bit` = MSB) |
| Width | 1–64 bits, crossing byte boundaries |
| Sign | unsigned or two's-complement signed (with sign extension on decode) |
| Scaling | `factor` (scale) and `offset`, DBC-style |
| Saturation | optional `[phys_min, phys_max]` clamp + raw-range saturation |
| Frame size | up to 64 bytes (CAN FD) |

Two code paths:

- **Raw path** (`micp2_signal_pack_raw` / `unpack_raw`) — *float-free*, packs the
  integer raw bits only. Suitable for MCUs without an FPU; the application can
  pre-scale in fixed point.
- **Physical path** (`micp2_signal_encode` / `decode`) — applies factor/offset
  with `double`. Rounding is half-away-from-zero implemented without `libm` (no
  `-lm` needed).

Bit-packing correctness is locked down by unit tests with known vectors,
including the classic Intel `0xABC@bit4` and Motorola `0x1234@bit7` layouts,
sign extension, negative offsets (`(temp+40)/0.5`), and saturation.

## 4. Public API

```c
#include "micp2/micp2.h"

/* describe the matrix once, in const memory */
static const micp2_signal_t bms_signals[] = {
    {"BattVolt",    0, 16, MICP2_BYTE_ORDER_INTEL, MICP2_UNSIGNED, 0.01, 0,    0, 655.35, "V"},
    {"BattCurrent",16, 16, MICP2_BYTE_ORDER_INTEL, MICP2_SIGNED,   0.1,  0, -3000, 3000,  "A"},
    {"SOC",        32,  8, MICP2_BYTE_ORDER_INTEL, MICP2_UNSIGNED, 0.5,  0,    0, 100,    "%"},
};
static const micp2_message_t messages[] = {
    {"BMS_Status", 0x123, 0, 6, 100, 3, bms_signals, 3},
};
static const micp2_matrix_t matrix = {"vehicle", nodes, n_nodes, messages, 1};

/* producer: physical values -> CAN bytes */
double tx[] = {401.23, -120.5, 87.0};
uint8_t frame[8];
micp2_message_encode(&messages[0], tx, frame, sizeof frame);
/* ... hand `frame`/can_id to your CAN controller ... */

/* consumer: CAN bytes -> physical values, by id */
micp2_matrix_dispatch(&matrix, 0x123, 0, frame, dlc, on_rx, user);
```

Per-signal access (`micp2_signal_encode`/`decode`) and lookups
(`micp2_matrix_find_by_id`, `micp2_message_find_signal`) are also public.

## 5. Wire format

MICP 2.0 does **not** add an application header — that is the point. The wire is
the **native CAN/CAN FD frame**: an 11- or 29-bit identifier plus 0–64 payload
bytes. Meaning comes entirely from the shared matrix, exactly like a DBC. This
is the CanPack philosophy, and it preserves CAN's native ID-based arbitration
and hardware filtering.

Integrity at this layer is the CAN controller's hardware CRC. Confirmed
delivery, large multi-frame transfers and tamper/replay protection are addressed
by the roadmap items in §7 (counter/CRC and MAC signals), layered on top of the
matrix as ordinary signals.

## 6. How this relates to CanPack and CANopen

- **Like CanPack/CANopen**: signal/register meaning on fixed CAN frames; node
  model; per-message identity; cyclic publication.
- **Unlike CanPack**: MICP 2.0 expresses the matrix as a clean, reusable,
  *const table-driven* database with a tested generic codec, instead of
  hand-written per-message packing welded to a specific board's StdPeriph +
  FreeRTOS. It is host-unit-tested and portable; CanPack is efficient but locked
  to its target.
- **Unlike standard CANopen**: 2.0 does not impose the full Object Dictionary or
  the CiA COB-ID allocation; it is a *private* matrix you define, which is what
  the OEM use-case calls for.

## 7. Roadmap (not yet implemented)

The reference notes also stress **functional safety / anti-tamper** ("protect
chassis/steering/brake control, prevent unauthorized access"). Natural 2.x
additions, deliberately left out of this first cut to keep the core small and
verified:

- Message counter + checksum signals (e.g. E2E-style rolling counter + CRC in
  two reserved signals) for tamper/replay detection on safety frames.
- Per-message authentication (CMAC/MAC signal) for the "not open to unauthorized
  third parties" requirement.
- A small code-generator from a `.dbc`/CSV matrix to the const tables above.

## 8. Files

| File | Purpose |
|---|---|
| `include/micp2/micp2_signal.h`, `src/micp2_signal.c` | DBC-style signal codec |
| `include/micp2/micp2_matrix.h`, `src/micp2_matrix.c` | Node/Message/Signal matrix, encode/decode, dispatch |
| `include/micp2/micp2_types.h` | shared return/error codes |
| `include/micp2/micp2.h` | umbrella header |
| `examples/micp2_matrix_demo.c` | BMS-style matrix encode→bytes→decode demo |
| `tests/test_micp2_signal.c` | codec + matrix unit tests |

Build with the `make test` or CMake/CTest targets; MICP 2.0 builds into the
`libmicp` static library.

## 9. STM32F103RCT6 + FreeRTOS adaptability

MICP 2.0's core (`micp2_signal` + `micp2_matrix`) is pure C11 with **no heap, no
OS and no libm dependency**, so it drops onto an STM32F103RCT6 (Cortex-M3,
64 KB Flash / 20 KB RAM, no FPU) without modification.

- **No FPU, no libm** — the M3 has no hardware floating point. Use the **raw**
  path (`micp2_signal_pack_raw` / `micp2_signal_unpack_raw`) to keep the bus path
  integer-only; the controller works in raw register units and applies
  `factor`/`offset` (the `*_encode`/`*_decode` helpers) only where a physical
  value is actually needed. This avoids pulling in soft-float routines on the hot
  path.
- **Footprint** — the matrix is `const`, so Nodes/Messages/Signals live in Flash,
  not RAM. RAM use is limited to the 8/64-byte frame buffers and a few locals;
  comfortably within 20 KB.
- **bxCAN binding** — the wire is the native CAN frame. Map a message's CAN ID +
  encoded bytes to a `CAN_TxMsg` (bxCAN / StdPeriph or HAL); on RX, feed the
  received `(id, data, dlc)` straight into `micp2_matrix_dispatch`. Use the bxCAN
  hardware filter banks to pre-select the IDs in your matrix.
- **FreeRTOS task model** — a typical layout is a cyclic **TX task** that encodes
  and queues the periodic matrix frames (e.g. 10/20/100 ms groups via
  `vTaskDelayUntil`) and an **RX task / ISR** that pushes received frames onto a
  queue for `micp2_matrix_dispatch`. The codec is reentrant and state-free, so no
  locking is needed around encode/decode beyond protecting your own shared
  application data.
