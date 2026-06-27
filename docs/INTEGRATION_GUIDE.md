# MICP Integration Guide

> Language: **English** | [中文](INTEGRATION_GUIDE.zh-CN.md)


How to build MICP, link it into your application, and drive a session over a real
transport. No external dependencies are required.

---

## 1. Building

### CMake (recommended)

```bash
cmake -S . -B build
cmake --build build
cd build && ctest --output-on-failure   # run the test suite
```

Useful options:

| Option                       | Default | Effect                          |
|------------------------------|---------|---------------------------------|
| `-DMICP_BUILD_TESTS=OFF`     | ON      | Skip building unit tests        |
| `-DMICP_BUILD_EXAMPLES=OFF`  | ON      | Skip building the demo          |
| `-DMICP_WARNINGS_AS_ERRORS=ON` | OFF   | Treat compiler warnings as errors |

The build produces a static library `libmicp.a` and (optionally) the
`micp_loopback_demo` executable.

### Makefile (no CMake)

```bash
make            # libmicp.a + tests + demo under ./build
make test       # build and run everything
```

### Embedding in your own CMake project

```cmake
add_subdirectory(micp-stack)
target_link_libraries(my_app PRIVATE micp::micp)
```

Or simply compile the four `src/*.c` files and add `include/` to your include path.

## 2. The API in five calls

```c
#include "micp/micp.h"
```

| Call                          | Purpose                                              |
|-------------------------------|------------------------------------------------------|
| `micp_session_init`           | Initialize a session with its address and callbacks  |
| `micp_session_connect`        | Active open toward a peer (sends HELLO)               |
| `micp_session_send`           | Send a payload (reliable or best-effort)             |
| `micp_session_feed`           | Hand received bytes to the stack                      |
| `micp_session_tick`           | Advance timers (retransmit / heartbeat / liveness)   |

Plus `micp_session_disconnect`, `micp_session_set_event_cb`, and the read-only
`micp_session_tx_busy`.

## 3. Wiring the callbacks

You provide two functions. The **output** callback pushes encoded bytes onto your
transport; the **recv** callback is invoked when an application payload arrives.

```c
static micp_err_t my_output(void *user, const uint8_t *data, size_t len) {
    my_transport_t *t = user;
    return transport_write(t, data, len) == (int)len ? MICP_OK : MICP_ERR_INVAL;
}

static void my_recv(void *user, uint16_t src, const uint8_t *payload, size_t len) {
    /* deliver to your application logic */
    app_on_message(src, payload, len);
}
```

Initialize:

```c
micp_session_t s;
micp_session_init(&s, /*addr=*/0x0010, my_output, my_recv, /*user=*/&my_transport);

/* optional: observe state changes */
micp_session_set_event_cb(&s, my_event_cb);

/* optional: tune timing (units are whatever you pass to tick) */
s.rto_ticks          = 5;
s.max_retries        = 4;
s.heartbeat_ticks    = 50;
s.peer_timeout_ticks = 200;
```

## 4. The runtime loop

A typical integration has three drivers:

```c
/* (a) Whenever bytes arrive from the transport: */
uint8_t rx[256];
int n = transport_read(&my_transport, rx, sizeof(rx));
if (n > 0) micp_session_feed(&s, rx, (size_t)n);

/* (b) Periodically (timer/RTOS tick), advance protocol time: */
micp_session_tick(&s, /*dt=*/1);

/* (c) When the application wants to talk: */
micp_session_send(&s, payload, payload_len, /*reliable=*/1);
```

On a single thread you can interleave (a), (b) and (c) in one poll loop. On an
RTOS, call `tick` from a periodic task and `feed` from the RX task — but route all
calls for **one** session through a single context or guard them with a mutex
(see ARCHITECTURE §7).

## 5. Establishing a connection

One side opens actively, the other accepts passively:

```c
/* Node A (initiator) */
micp_session_connect(&a, /*peer=*/0x0020);   /* -> CONNECTING, sends HELLO */

/* Node B receives the HELLO via feed() and auto-transitions to CONNECTED,
 * replying with HELLO_ACK. When A feeds that HELLO_ACK it becomes CONNECTED. */
```

Check readiness with `s.state == MICP_STATE_CONNECTED` or via the event callback.

## 6. Sending data

```c
/* Best-effort (fire and forget) */
micp_session_send(&s, buf, len, 0);

/* Reliable (ACK + retransmit). Only one reliable frame may be in flight: */
if (!micp_session_tx_busy(&s)) {
    micp_err_t e = micp_session_send(&s, buf, len, 1);
    if (e == MICP_ERR_BUSY) { /* a prior reliable send is still pending */ }
}
```

The completion of a reliable send is observable as `micp_session_tx_busy()`
returning 0 after the matching ACK is fed in. If retransmissions are exhausted the
session enters `MICP_STATE_ERROR` and the next `tick` returns `MICP_ERR_TIMEOUT`.

## 7. Error handling

All entry points return a `micp_err_t`. Use `micp_strerror()` for a readable name.
Per-frame receive errors (CRC, bad version) do **not** fail `feed()`; they are
counted in `s.stats` (`rx_crc_errors`, `rx_dropped`) so you can monitor link
quality:

```c
printf("crc errors: %u, dropped: %u, retransmits: %u\n",
       s.stats.rx_crc_errors, s.stats.rx_dropped, s.stats.retransmits);
```

## 8. Recovering from ERROR

After a timeout the session is in `MICP_STATE_ERROR`. Re-establish by calling
`micp_session_connect()` again (allowed from `ERROR`), which restarts the handshake.

## 9. Porting checklist

- [ ] Implement `output` over your transport (UART/TCP/CAN-TP/shared mem).
- [ ] Call `feed()` from wherever bytes are received.
- [ ] Call `tick()` from a periodic source with consistent `dt` units.
- [ ] Pick `rto_ticks` / `peer_timeout_ticks` relative to your tick rate and link RTT.
- [ ] Ensure single-context (or mutex-guarded) access per session.

For a concrete RTOS port (memory budget, FreeRTOS task skeleton, UART binding and
toolchain flags) on STM32F103RCT6, see **PORTING_STM32F103.md**.

## 10. Reference

See `examples/loopback_demo.c` for a complete, runnable example that performs a
handshake, a reliable exchange, a best-effort reply and an orderly teardown
between two in-process nodes.
