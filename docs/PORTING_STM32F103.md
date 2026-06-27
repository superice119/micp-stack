# Porting MICP to STM32F103RCT6 + FreeRTOS

This guide covers running the MICP reference stack on an **STM32F103RCT6**
(ARM Cortex-M3 @ 72 MHz, **48 KB SRAM**, **256 KB Flash**) under **FreeRTOS**.
The stack is transport- and OS-agnostic C11, so porting is integration work only —
no changes to the library sources are required.

---

## 1. Why it fits this target

MICP was designed for constrained MCUs and has **no portability blockers** here:

| Concern                | MICP                                                                 |
|------------------------|---------------------------------------------------------------------|
| Dynamic memory         | **None** — no `malloc`/`free` anywhere; all state is in fixed structs |
| OS / RTOS dependency   | **None** — no threads, no syscalls; time is injected via `tick()`    |
| Floating point         | **None** — integer-only (M3 has no FPU; no soft-float pulled in)      |
| 64-bit math            | **None** — 8/16/32-bit only                                          |
| Library headers        | `stdint.h`, `stddef.h`, `string.h` (`memcpy/memmove/memset`) — all in arm-none-eabi newlib |
| Endianness             | Frame codec is byte-wise big-endian; **not** host-byte-order dependent (M3 is little-endian, works unchanged) |
| Language               | Pure **C11** — builds with `arm-none-eabi-gcc -std=c11`              |

## 2. Memory budget (RCT6: 48 KB SRAM / 256 KB Flash)

Default configuration (`MICP_MAX_PAYLOAD = 512`):

| Item                                   | Size            | Notes                                  |
|----------------------------------------|-----------------|----------------------------------------|
| `sizeof(micp_session_t)` (per peer)    | **~1192 B**     | 526 B rx-reassembly + 526 B retransmit cache dominate |
| `sizeof(micp_frame_t)` (transient)     | 524 B           | Lives on the stack during encode/decode |
| Peak stack on `feed()` → `handle_frame`| **~1.0–1.5 KB** | frame struct + encode buffer           |
| Code (`.text`) for the library         | a few KB        | bitwise CRC, codec, session FSM        |

On RCT6's 48 KB SRAM this is comfortable: one session uses ~2.5 % of SRAM. Even a
handful of concurrent sessions plus task stacks fit easily.

> **FreeRTOS task stack:** size the task that calls `micp_session_feed()` /
> `micp_session_send()` to **≥ 2 KB** (≥ 512 words). Do **not** use the default
> `configMINIMAL_STACK_SIZE` — the transient `micp_frame_t` + encode buffer need
> headroom. Verify with `uxTaskGetStackHighWaterMark()` after a soak test.

### Shrinking RAM further (optional)

`MICP_MAX_PAYLOAD` (in `include/micp/micp_types.h`) drives both per-session buffers.
RCT6 does not need this, but for reference:

| `MICP_MAX_PAYLOAD` | `sizeof(micp_session_t)` (approx) |
|--------------------|-----------------------------------|
| 512 (default)      | ~1192 B                           |
| 256                | ~680 B                            |
| 128                | ~424 B                            |
| 64                 | ~296 B                            |

Both peers MUST use the same `MICP_MAX_PAYLOAD` (it bounds the wire `LEN` field).

## 3. Timing model under FreeRTOS

MICP has no internal clock. Drive it from a periodic FreeRTOS task and express all
timeouts in **FreeRTOS ticks** (or any consistent unit you pass to `tick()`):

- With the common `configTICK_RATE_HZ = 1000` (1 ms tick), the defaults map to:
  `rto_ticks=10` → 10 ms, `heartbeat_ticks=30` → 30 ms, `peer_timeout_ticks=100` → 100 ms.
- Tune these to your link RTT and the rate at which you call `tick()`. A coarser
  `tick()` period (e.g. every 10 ms) means you should pass `dt = 10` and scale the
  timeout fields accordingly.

## 4. Transport binding (UART example)

MICP emits bytes through your `output` callback and consumes bytes via `feed()`.
A typical STM32 HAL + FreeRTOS wiring:

**Output (TX):** point `output` at a UART transmit. Prefer a non-blocking path
(interrupt/DMA + a TX ring buffer) so the protocol task never stalls.

```c
static micp_err_t uart_output(void *user, const uint8_t *data, size_t len) {
    /* Blocking variant — simplest; swap for IT/DMA + ring buffer in production. */
    if (HAL_UART_Transmit(&huart1, (uint8_t *)data, (uint16_t)len, 100) == HAL_OK)
        return MICP_OK;
    return MICP_ERR_INVAL;
}
```

**Input (RX):** feed bytes from a UART RX interrupt (or DMA idle) through a
FreeRTOS stream/queue into the protocol task, which calls `feed()`:

```c
/* ISR: HAL_UART_RxCpltCallback pushes each byte to a stream buffer */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *h) {
    BaseType_t woken = pdFALSE;
    xStreamBufferSendFromISR(rx_stream, &rx_byte, 1, &woken);
    HAL_UART_Receive_IT(&huart1, &rx_byte, 1);   /* re-arm */
    portYIELD_FROM_ISR(woken);
}
```

## 5. Protocol task skeleton

```c
#include "micp/micp.h"

static micp_session_t s;

static void app_recv(void *u, uint16_t src, const uint8_t *p, size_t n) {
    (void)u; (void)src;
    /* deliver p[0..n) to your application */
}

void micp_task(void *arg) {
    (void)arg;
    micp_session_init(&s, /*addr=*/0x0001, uart_output, app_recv, NULL);

    /* timeouts in ticks; with 1 ms tick these are ms */
    s.rto_ticks = 10; s.max_retries = 3;
    s.heartbeat_ticks = 30; s.peer_timeout_ticks = 100;

    micp_session_connect(&s, /*peer=*/0x0002);

    uint8_t buf[64];
    TickType_t last = xTaskGetTickCount();
    for (;;) {
        /* 1) drain received bytes (non-blocking) */
        size_t n = xStreamBufferReceive(rx_stream, buf, sizeof(buf), 0);
        if (n) micp_session_feed(&s, buf, n);

        /* 2) advance protocol time once per ms-ish */
        TickType_t now = xTaskGetTickCount();
        if (now != last) { micp_session_tick(&s, (uint32_t)(now - last)); last = now; }

        /* 3) application TX (reliable example) */
        /* if (!micp_session_tx_busy(&s)) micp_session_send(&s, msg, len, 1); */

        vTaskDelay(pdMS_TO_TICKS(1));
    }
}
```

Create it with a ≥ 2 KB stack:

```c
xTaskCreate(micp_task, "micp", 512 /*words = 2KB*/, NULL, tskIDLE_PRIORITY + 2, NULL);
```

## 6. Concurrency rule

A `micp_session_t` is **not** internally synchronized. Drive each session from a
**single task** (as above). If `send()` must be called from other tasks, hand the
payload to the protocol task via a queue, or guard all session calls with a mutex
(see `INTEGRATION_GUIDE.md` §4 / §9). Distinct sessions are independent.

## 7. Build for the target

Compile the four library sources with your ARM toolchain and add `include/` to the
include path — no MICP-specific build flags are needed:

```
arm-none-eabi-gcc -mcpu=cortex-m3 -mthumb -std=c11 -Os \
    -Iinclude \
    -c src/micp_crc.c src/micp_frame.c src/micp_session.c src/micp_types.c
```

Link the resulting objects with your FreeRTOS + HAL project. The provided
`CMakeLists.txt` / `Makefile` target a hosted build (unit tests + demo) and are not
used for the firmware image.

## 8. Validation status

The portability claims above are verified by inspection of the sources (no heap /
OS / FPU / 64-bit usage) and the host test suite (CMake/CTest + Makefile, including
`-Werror -fsanitize=address,undefined`). On-target HAL/DMA binding is integration
work owned by the firmware project; this guide provides the contract and skeleton
to do it.
