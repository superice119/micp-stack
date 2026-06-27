# MICP — Multica Industrial Communication Protocol

> Language: **English** | [中文](README.zh-CN.md)


A compact, dependency-free **reference implementation** of a private industrial
communication protocol, written in portable **C11**. MICP is designed in the
spirit of commercial fieldbus stacks (CANopen / EtherCAT): not just a spec, but a
buildable reference stack with framing/codec, an addressed session state machine,
error detection & recovery, unit tests, an example, and documentation.

> Status: `v0.1.0` reference implementation. Zero external dependencies.

## Features

- **Frame codec** — deterministic big-endian serialization with a 12-byte header,
  variable payload (≤ 512 B) and a CRC-16/CCITT-FALSE trailer.
- **Byte-stream parser** — SOF resynchronization, partial-frame reassembly across
  reads, and per-frame CRC validation.
- **Session state machine** — `DISCONNECTED → CONNECTING → CONNECTED → ERROR`
  with active/passive open (HELLO / HELLO_ACK), heartbeat and peer-liveness timeout.
- **Reliable delivery** — stop-and-wait ACK + retransmission with a retry limit,
  plus duplicate suppression on the receiver.
- **Transport-agnostic** — you supply a byte-out callback; works over UART, TCP,
  CAN-TP, shared memory, etc. No dynamic allocation; bare-metal friendly.
- **Tested** — unit tests for CRC, codec and session behaviour (CTest + a Makefile
  fallback) and an end-to-end loopback demo.

## Layout

```
include/micp/   public API headers (micp.h is the umbrella include)
src/            implementation (crc, frame, session, type strings)
tests/          unit tests (test_crc, test_frame, test_session) + harness
examples/       loopback_demo.c — two nodes over a simulated wire
docs/           PROTOCOL_SPEC.md, ARCHITECTURE.md, INTEGRATION_GUIDE.md,
                PORTING_STM32F103.md, COMPARISON.md, MICP2_DESIGN.md
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
make test       # build + run all unit tests and the loopback demo
make demo       # run the end-to-end demo only
```

Both paths exit non-zero on any test failure, so they double as CI gates.

### Run the demo

```bash
./build/micp_loopback_demo        # CMake build
# or
make demo                          # Makefile build
```

Expected tail: `Result: OK`.

## Using the library

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
/* feed bytes from the wire: */ micp_session_feed(&s, rx, rx_len);
/* drive timers periodically:  */ micp_session_tick(&s, dt_ticks);
/* send reliably:              */ micp_session_send(&s, data, len, /*reliable=*/1);
```

See **docs/INTEGRATION_GUIDE.md** for a complete walkthrough and
**docs/PROTOCOL_SPEC.md** for the wire format.

## Embedded / MCU targets

The stack is dependency-free, heap-free and OS-agnostic, suitable for bare-metal
and RTOS targets. A worked port for **STM32F103RCT6 + FreeRTOS** (memory budget,
FreeRTOS task skeleton, UART transport binding, toolchain flags) is in
**docs/PORTING_STM32F103.md**.

## Comparison with CanPack / CANopen

A side-by-side comparison of MICP against the user-supplied **CanPack** STM32
module and the **CANopen (CiA 301)** standard — positioning, addressing, frame
format, reliability, state machine, data model and portability — is in
**docs/COMPARISON.md**.

## MICP 2.0 — signal-matrix private CAN protocol

In addition to the 1.x transport/session stack, the repo now includes **MICP
2.0**, a CanPack/DBC-style evolution that defines meaning on top of native
CAN/CAN FD frames via a **communication matrix** — Nodes, Message IDs and
Signals with start bit / length / scale (factor) / offset. It ships a tested,
float-free-capable signal codec (Intel + Motorola byte order, signed, scaling,
clamping), a const table-driven matrix with encode/decode/dispatch, a demo
(`examples/micp2_matrix_demo.c`) and unit tests (`tests/test_micp2_signal.c`).
1.x and 2.0 coexist in the same `libmicp`. Design: **docs/MICP2_DESIGN.md**;
headers under **include/micp2/**.

## For QA

- Primary verification: `cmake -S . -B build && cmake --build build && (cd build && ctest --output-on-failure)`.
- Fallback: `make test`.
- Conformance focus areas: CRC known-answer (`0x29B1`), codec round-trip & boundary
  cases, handshake, reliable ACK/retransmit/exhaustion, duplicate suppression,
  byte-fragmentation reassembly, CRC-error drop, garbage resync, heartbeat & peer
  timeout. These map 1:1 to the `tests/` suites.

## License

MIT — see [LICENSE](LICENSE).
