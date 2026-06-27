# MICP Architecture

This document describes the internal structure of the MICP reference stack and
the rationale behind its design.

## 1. Design goals

1. **Portability first.** Pure C11, no dynamic allocation, no OS/transport
   dependencies — suitable for MCUs, RTOS tasks and hosted processes alike.
2. **Transport agnostic.** The stack never touches a socket or UART directly; it
   emits bytes through a callback and consumes bytes through `feed()`.
3. **Deterministic.** Fixed-size buffers, bounded work per call, and an explicit
   tick-driven timer model — friendly to hard real-time scheduling.
4. **Testable & conformance-oriented.** Every layer is independently unit-tested,
   mirroring the conformance-test philosophy of CANopen/EtherCAT.

## 2. Layered structure

```
        ┌─────────────────────────────────────────────┐
        │ Application                                  │
        │  - provides output callback (bytes -> wire)  │
        │  - provides recv callback   (payload -> app) │
        │  - calls send / connect / disconnect / tick  │
        └───────────────▲───────────────┬─────────────┘
                        │ on_recv        │ send/connect/tick
        ┌───────────────┴───────────────▼─────────────┐
        │ Session layer  (src/micp_session.c)          │
        │  - state machine (DISCONN/CONNECTING/...)    │
        │  - reliable stop-and-wait + retransmit timer │
        │  - heartbeat / peer-liveness                 │
        │  - byte-stream reassembly (SOF resync)       │
        └───────────────▲───────────────┬─────────────┘
                        │ decode         │ encode
        ┌───────────────┴───────────────▼─────────────┐
        │ Frame codec    (src/micp_frame.c)            │
        │  - serialize/deserialize header+payload      │
        │  - length & version validation               │
        └───────────────▲───────────────┬─────────────┘
                        │                │
        ┌───────────────┴───────────────▼─────────────┐
        │ Integrity      (src/micp_crc.c)              │
        │  - CRC-16/CCITT-FALSE (bitwise, table-free)  │
        └──────────────────────────────────────────────┘
```

## 3. Modules

| Module            | Files                              | Responsibility                                  |
|-------------------|------------------------------------|-------------------------------------------------|
| Types & codes     | `micp_types.h` / `micp_types.c`    | Constants, enums, error codes, name helpers     |
| Integrity         | `micp_crc.{h,c}`                   | One-shot and incremental CRC-16                 |
| Frame codec       | `micp_frame.{h,c}`                 | Encode/decode a single frame; bounds & CRC check|
| Session           | `micp_session.{h,c}`               | State machine, reliability, framing, timers     |
| Umbrella          | `micp.h`                           | Single include + library version macros         |

The dependency graph is strictly downward (session → frame → crc → types). No
module depends on a higher layer, which keeps the codec and CRC reusable in
isolation (e.g. for a sniffer or a conformance harness).

## 4. Data flow

### Transmit
`micp_session_send()` builds a `micp_frame_t`, calls `micp_frame_encode()` to
serialize it (appending the CRC), and hands the bytes to the application's
`output` callback. For reliable sends the encoded bytes are cached in
`pending_buf` so the retransmit path can re-emit without re-encoding.

### Receive
Raw bytes enter through `micp_session_feed()`, which appends to a per-session
reassembly buffer (`rx_buf`). The parser:
1. resynchronizes by discarding any leading non-`SOF` bytes;
2. attempts `micp_frame_decode()`;
3. on `MICP_ERR_SHORT`, waits for more bytes;
4. on `MICP_ERR_CRC` or a malformed header, drops one byte and resyncs;
5. on success, dispatches to `handle_frame()` and continues with any remaining
   buffered bytes.

This makes the stack robust to byte-stream transports that split or coalesce
frames arbitrarily, and to line noise that injects spurious bytes.

## 5. Timing model

The stack has **no internal clock**. The application advances time by calling
`micp_session_tick(s, dt)` from a periodic context (timer ISR, RTOS tick hook, or
a poll loop). `dt` is in application-defined units; all timeouts
(`rto_ticks`, `heartbeat_ticks`, `peer_timeout_ticks`) use the same units. This
keeps the implementation allocation- and syscall-free and lets the integrator
choose the resolution/jitter trade-off.

## 6. Memory model

- A `micp_session_t` is fully self-contained (~1.5 KB, dominated by the reassembly
  and retransmit buffers, each `MICP_MAX_FRAME` = 526 bytes).
- No heap usage anywhere in the library.
- One session represents one peer association. Multiple peers = multiple sessions.

## 7. Concurrency

A single session is **not** internally synchronized. Drive each session from one
context, or wrap calls in a mutex. Distinct sessions are independent and need no
shared locking.

## 8. Extensibility notes

- **Sliding window**: the current reliable layer is stop-and-wait (one in-flight
  frame). The frame already carries a 16-bit `SEQ`, so a windowed ARQ can be added
  in the session layer without a wire-format change.
- **Security**: `FLAGS` and the versioned header leave room for an authenticated/
  encrypted profile (e.g. an AEAD tag appended before the CRC) in a future `VER`.
- **Fragmentation**: payloads above 512 bytes would be segmented at the session
  layer; the codec and CRC are unaffected.

## 9. Testing strategy

| Suite            | Covers                                                              |
|------------------|--------------------------------------------------------------------|
| `test_crc`       | CRC known-answer, empty input, incremental == one-shot, sensitivity|
| `test_frame`     | round-trip, zero/max payload, CRC error, truncation, bad SOF, nobufs|
| `test_session`   | handshake, reliable ACK, busy, retransmit + exhaustion, dedup,     |
|                  | byte fragmentation, CRC drop, garbage resync, disconnect, heartbeat,|
|                  | peer timeout, send-requires-connected                              |
| `loopback_demo`  | end-to-end happy path across two in-process nodes                  |

The unit tests use a tiny header-only harness (`tests/micp_test.h`); a non-zero
process exit code denotes failure, which both CTest and the Makefile use as the
gate.
