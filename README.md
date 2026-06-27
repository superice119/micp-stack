# MICP 2.0 — Signal-Matrix Private CAN Protocol

> Language: **English** | [中文](README.zh-CN.md)

A compact, dependency-free **reference implementation** of a private industrial
CAN protocol, written in portable **C11**. MICP 2.0 follows the model real OEM
"private protocols" use (and the kind `CanPack.rar` / CANopen implement): it is
not a byte transport but a **communication matrix on top of native CAN / CAN FD**
— Nodes, Message IDs and Signals (start bit, length, scale/factor, offset), in
the spirit of a DBC. Like commercial fieldbus stacks, it is not just a spec but a
buildable reference stack with a tested signal codec, a const table-driven matrix,
unit tests, an example and documentation.

> Status: `v2.0.0` reference implementation. Zero external dependencies, no heap,
> no OS, no libm.

## Features

- **DBC-style signal codec** — pack/unpack scaled signals into CAN payloads:
  Intel (little-endian) and Motorola (big-endian) bit order, signed/unsigned with
  sign extension, `factor`/`offset` scaling with clamp/saturation, up to 64 payload
  bytes (CAN FD). A float-free **raw** path is available for MCUs without an FPU.
- **Communication matrix** — describe the bus as a `const` table of Nodes →
  Messages → Signals. Find a message by CAN ID, encode/decode a whole frame, and
  dispatch received frames to a handler.
- **Transport-agnostic** — the wire is the native CAN frame (11/29-bit ID + 0–64
  bytes); you bind it to any CAN controller. No dynamic allocation; bare-metal and
  RTOS friendly.
- **Tested** — unit tests for the codec (byte order, sign extension, scaling,
  clamping, boundaries) and the matrix (encode/decode/dispatch), via CTest with a
  Makefile fallback, plus an end-to-end matrix demo.

## Layout

```
include/micp2/  public API headers (micp2.h is the umbrella include)
src/            implementation (micp2_signal, micp2_matrix)
tests/          unit tests (test_micp2_signal) + harness
examples/       micp2_matrix_demo.c — BMS-style encode → bytes → decode
docs/           MICP2_DESIGN.md (design + STM32F103/FreeRTOS adaptability)
CMakeLists.txt  primary build (CMake + CTest)
Makefile        portable fallback build/test
```

## Build & test

### Option A — CMake + CTest (recommended)

```bash
cmake -S . -B build
cmake --build build
cd build && ctest --output-on-failure
```

### Option B — Makefile (no CMake required)

```bash
make            # build library, tests and demo
make test       # build + run unit tests and the matrix demo
make demo       # run the matrix demo only
```

Both paths exit non-zero on any test failure, so they double as CI gates.

### Run the demo

```bash
./build/micp2_matrix_demo          # CMake build
# or
make demo                          # Makefile build
```

Expected tail: `MICP 2.0 signal-matrix demo OK`.

## Using the library

Describe one signal and encode/decode a value:

```c
#include "micp2/micp2.h"

/* A 12-bit unsigned signal at Intel bit 4, phys = raw * 0.1 (e.g. 0.1 V/bit). */
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
micp2_signal_encode(payload, sizeof payload, &volt, 401.2); /* phys → bytes */

double v = 0.0;
micp2_signal_decode(payload, sizeof payload, &volt, &v);    /* bytes → phys */
```

Or describe the whole bus as a matrix and dispatch frames — see
`examples/micp2_matrix_demo.c` and **docs/MICP2_DESIGN.md**.

## Embedded / MCU targets

The stack is dependency-free, heap-free, libm-free and OS-agnostic, suitable for
bare-metal and RTOS targets. The float-free **raw** signal path makes it a good
fit for FPU-less MCUs. A worked **STM32F103RCT6 + FreeRTOS** adaptability note
(memory budget, FreeRTOS task model, bxCAN binding) is in
**docs/MICP2_DESIGN.md**.

## How it relates to CanPack and CANopen

MICP 2.0 adopts the CanPack/CANopen idea — signal/register meaning on fixed CAN
frames — but expresses the matrix as a clean, reusable, `const` table-driven
database with a tested generic codec, instead of board-locked hand-written
packing. It does not impose CANopen's full Object Dictionary or COB-ID
allocation; it is a *private* matrix you define. See **docs/MICP2_DESIGN.md** §6.

## For QA

- Primary verification: `cmake -S . -B build && cmake --build build && (cd build && ctest --output-on-failure)`.
- Fallback: `make test`.
- Conformance focus areas: Intel/Motorola bit placement, signed sign-extension,
  factor/offset round-trip, clamp/saturation at min/max, out-of-range/oversized
  payload rejection, and matrix encode/decode/dispatch. These map to the
  `tests/test_micp2_signal.c` suite.

## License

MIT — see [LICENSE](LICENSE).
