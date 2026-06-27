# MICP Protocol Specification — v0.1.0

MICP (Multica Industrial Communication Protocol) is a connection-oriented,
addressed, frame-based protocol for reliable point-to-point messaging over an
arbitrary byte-stream or datagram transport.

This document is normative for the on-wire format and the protocol state machine.

---

## 1. Conventions

- All multi-byte integer fields are transmitted in **big-endian** (network) order.
- Byte values are shown in hexadecimal (e.g. `0xA5`).
- "Node" = a protocol endpoint identified by a 16-bit address.

## 2. Addressing

- Each node has a 16-bit **address** in `0x0000 .. 0xFFFE`.
- `0xFFFF` is reserved as the **broadcast** address (`MICP_ADDR_BROADCAST`).
- A node accepts a frame if `DST == own address` or `DST == 0xFFFF`.

## 3. Frame format

Every frame has a fixed 12-byte header, a variable payload, and a 2-byte CRC trailer.

```
 Offset  Size  Field    Description
 ------  ----  -------  ----------------------------------------------------
   0      1    SOF      Start of frame, always 0xA5
   1      1    VER      Protocol version, currently 0x01
   2      1    TYPE     Message type (section 4)
   3      1    FLAGS    Bit-field (section 5)
   4      2    SRC      Source node address
   6      2    DST      Destination node address (0xFFFF = broadcast)
   8      2    SEQ      Sequence number (per-sender, wraps at 0xFFFF)
  10      2    LEN      Payload length in bytes (0 .. 512)
  12     LEN   PAYLOAD  Application or control payload
 12+LEN   2    CRC16    CRC-16/CCITT-FALSE over bytes [1 .. 12+LEN-1]
```

- **Header size**: 12 bytes. **Trailer size**: 2 bytes. **Max payload**: 512 bytes.
- **Max frame size**: `12 + 512 + 2 = 526` bytes.
- `LEN` greater than 512 MUST be rejected (`MICP_ERR_LENGTH`).

### 3.1 CRC

- Algorithm: **CRC-16/CCITT-FALSE** — `poly=0x1021, init=0xFFFF, refin=false,
  refout=false, xorout=0x0000`.
- Coverage: every byte **after** the SOF, up to and **including** the payload —
  i.e. bytes at offsets `1 .. (12 + LEN − 1)`. The SOF byte and the CRC field
  itself are excluded.
- Known-answer value of `"123456789"` is `0x29B1`.
- A frame with a mismatching CRC MUST be dropped (`MICP_ERR_CRC`). A receiver MAY
  emit a `NACK` carrying the offending sequence number.

## 4. Message types (TYPE)

| Value | Name        | Direction        | Payload            | Purpose                         |
|-------|-------------|------------------|--------------------|---------------------------------|
| 0x01  | HELLO       | initiator → peer | none               | Active connection open          |
| 0x02  | HELLO_ACK   | peer → initiator | none               | Accept connection               |
| 0x03  | HEARTBEAT   | both             | none               | Liveness keep-alive             |
| 0x04  | DATA        | both             | application bytes  | Application data                |
| 0x05  | ACK         | both             | none               | Acknowledge a DATA `SEQ`        |
| 0x06  | NACK        | both             | none               | Negative ack (e.g. CRC failure) |
| 0x07  | DISCONNECT  | both             | none               | Orderly teardown                |

## 5. FLAGS bit-field

| Bit | Mask | Name        | Meaning                                            |
|-----|------|-------------|----------------------------------------------------|
| 0   | 0x01 | ACK_REQ     | Sender requests a reliable ACK for this frame      |
| 1   | 0x02 | BROADCAST   | Informational marker for broadcast frames          |
| 2–7 | —    | reserved    | MUST be 0 on send; ignored on receive              |

## 6. Sequence numbers

- Each node maintains a monotonically increasing 16-bit `tx_seq`, incremented for
  every emitted frame, wrapping from `0xFFFF` to `0x0000`.
- An `ACK`/`NACK` echoes the `SEQ` of the DATA frame it refers to.
- The receiver records the last in-order DATA `SEQ` and suppresses a DATA frame
  whose `SEQ` equals the last delivered one (duplicate from a retransmission). The
  ACK is still re-sent so the sender can make progress.

## 7. Connection state machine

States: `DISCONNECTED`, `CONNECTING`, `CONNECTED`, `ERROR`.

```
        connect()/send HELLO
 DISCONNECTED ───────────────► CONNECTING
      ▲   │                        │ recv HELLO_ACK
      │   │ recv HELLO /           ▼
      │   │ send HELLO_ACK     CONNECTED ──── recv/send DATA, HEARTBEAT, ACK
      │   └───────────────────►   │  │
      │  recv DISCONNECT          │  │ retransmit limit exceeded
      └───────────────────────────┘  │ or peer-liveness timeout
                                      ▼
                                    ERROR ── connect() ──► CONNECTING
```

Transitions:

| From          | Event                                   | Action                         | To            |
|---------------|-----------------------------------------|--------------------------------|---------------|
| DISCONNECTED  | `connect(peer)`                         | send HELLO                     | CONNECTING    |
| DISCONNECTED  | recv HELLO                               | send HELLO_ACK                 | CONNECTED     |
| CONNECTING    | recv HELLO_ACK (from peer)              | —                              | CONNECTED     |
| CONNECTED     | recv DATA                               | deliver; ACK if ACK_REQ        | CONNECTED     |
| CONNECTED     | recv HEARTBEAT                           | refresh liveness               | CONNECTED     |
| CONNECTED     | `tick` reaches heartbeat interval       | send HEARTBEAT                 | CONNECTED     |
| CONNECTED     | retransmit count > max_retries          | abort pending TX               | ERROR         |
| CONNECTED     | peer silent > peer_timeout              | —                              | ERROR         |
| CONNECTED/ING | recv DISCONNECT or `disconnect()`       | send DISCONNECT (local call)   | DISCONNECTED  |
| ERROR         | `connect(peer)`                         | send HELLO                     | CONNECTING    |

Passive open: a node in `DISCONNECTED` that receives a `HELLO` adopts the sender
as its peer and transitions directly to `CONNECTED`.

## 8. Reliable delivery (stop-and-wait)

1. `DATA` sent with `ACK_REQ` is cached and `SEQ` recorded; only **one** reliable
   frame may be outstanding at a time (`MICP_ERR_BUSY` otherwise).
2. The receiver delivers the payload (unless it is a duplicate) and emits `ACK`
   with the same `SEQ`.
3. On `ACK` matching the in-flight `SEQ`, the sender clears the pending state.
4. If no `ACK` arrives within `rto_ticks`, the sender retransmits, up to
   `max_retries` times. Exceeding the limit moves the session to `ERROR`
   (`MICP_ERR_TIMEOUT`).
5. A `NACK` for the in-flight `SEQ` triggers an immediate retransmit on the next tick.

## 9. Error detection & recovery summary

| Condition                  | Detection                         | Recovery                                  |
|----------------------------|-----------------------------------|-------------------------------------------|
| Bit error in a frame       | CRC-16 mismatch                   | drop frame; optional NACK; sender retransmits |
| Lost reliable DATA / ACK   | retransmit timer expiry           | retransmit up to `max_retries`            |
| Duplicate DATA             | `SEQ == last delivered`           | suppress delivery; re-ACK                 |
| Framing desync / garbage   | SOF scan + CRC validation         | skip bytes until a valid frame is found   |
| Dead peer                  | no frame within `peer_timeout`    | transition to ERROR                       |

## 10. Defaults (tunable per session)

| Parameter            | Default | Field                       |
|----------------------|---------|-----------------------------|
| Retransmit timeout   | 10      | `rto_ticks`                 |
| Max retransmissions  | 3       | `max_retries`               |
| Heartbeat interval   | 30      | `heartbeat_ticks`           |
| Peer liveness timeout| 100     | `peer_timeout_ticks`        |

Tick units are defined by the application (the value passed to `micp_session_tick`).

## 11. Versioning

The `VER` field is `0x01`. A receiver MUST reject frames with an unsupported
version (`MICP_ERR_VERSION`). Future revisions will bump `VER` and document the
delta here.
