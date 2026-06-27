# MICP 协议规范 —— v0.1.0

> 语言:[English](PROTOCOL_SPEC.md) | **中文**

MICP(Multica Industrial Communication Protocol)是一种面向连接、带寻址、基于帧的协议,
用于在任意字节流或数据报传输之上进行可靠的点对点消息传递。

本文档对**线序格式**与**协议状态机**具有规范性(normative)效力。

---

## 1. 约定

- 所有多字节整型字段以**大端**(网络序)传输。
- 字节值以十六进制表示(如 `0xA5`)。
- “节点”= 由 16 位地址标识的协议端点。

## 2. 寻址

- 每个节点拥有一个 16 位**地址**,范围 `0x0000 .. 0xFFFE`。
- `0xFFFF` 保留为**广播**地址(`MICP_ADDR_BROADCAST`)。
- 当 `DST == 本机地址` 或 `DST == 0xFFFF` 时,节点接收该帧。

## 3. 帧格式

每帧由固定 12 字节头、变长 payload、2 字节 CRC 校验尾组成。

```
 偏移   长度  字段     说明
 ----  ----  -------  ----------------------------------------------------
   0     1   SOF      帧起始,恒为 0xA5
   1     1   VER      协议版本,当前为 0x01
   2     1   TYPE     消息类型(见第 4 节)
   3     1   FLAGS    位域(见第 5 节)
   4     2   SRC      源节点地址
   6     2   DST      目的节点地址(0xFFFF = 广播)
   8     2   SEQ      序列号(按发送方计数,0xFFFF 回绕)
  10     2   LEN      payload 长度,字节(0 .. 512)
  12    LEN  PAYLOAD  应用或控制 payload
 12+LEN  2   CRC16    CRC-16/CCITT-FALSE,覆盖字节 [1 .. 12+LEN-1]
```

- **头长度**:12 字节。**校验尾长度**:2 字节。**最大 payload**:512 字节。
- **最大帧长**:`12 + 512 + 2 = 526` 字节。
- `LEN` 大于 512 必须被拒绝(`MICP_ERR_LENGTH`)。

### 3.1 CRC

- 算法:**CRC-16/CCITT-FALSE** —— `poly=0x1021, init=0xFFFF, refin=false,
  refout=false, xorout=0x0000`。
- 覆盖范围:SOF **之后**直到 payload(含)的所有字节,即偏移 `1 .. (12 + LEN − 1)`。
  SOF 字节与 CRC 字段本身不计入。
- 字符串 `"123456789"` 的已知答案为 `0x29B1`。
- CRC 不匹配的帧必须被丢弃(`MICP_ERR_CRC`)。接收方可发送携带该序列号的 `NACK`。

## 4. 消息类型(TYPE)

| 取值  | 名称        | 方向             | payload            | 用途                            |
|-------|-------------|------------------|--------------------|---------------------------------|
| 0x01  | HELLO       | 发起方 → 对端    | 无                 | 主动建链                        |
| 0x02  | HELLO_ACK   | 对端 → 发起方    | 无                 | 接受连接                        |
| 0x03  | HEARTBEAT   | 双向             | 无                 | 存活保活                        |
| 0x04  | DATA        | 双向             | 应用字节           | 应用数据                        |
| 0x05  | ACK         | 双向             | 无                 | 确认某个 DATA 的 `SEQ`          |
| 0x06  | NACK        | 双向             | 无                 | 否定确认(如 CRC 失败)         |
| 0x07  | DISCONNECT  | 双向             | 无                 | 有序拆链                        |

## 5. FLAGS 位域

| 位  | 掩码 | 名称        | 含义                                               |
|-----|------|-------------|----------------------------------------------------|
| 0   | 0x01 | ACK_REQ     | 发送方请求对该帧进行可靠 ACK                       |
| 1   | 0x02 | BROADCAST   | 广播帧的信息性标记                                 |
| 2–7 | —    | 保留        | 发送时必须为 0;接收时忽略                          |

## 6. 序列号

- 每个节点维护一个单调递增的 16 位 `tx_seq`,每发出一帧自增,从 `0xFFFF` 回绕到 `0x0000`。
- `ACK`/`NACK` 回显其所指 DATA 帧的 `SEQ`。
- 接收方记录最后一个按序投递的 DATA `SEQ`,并抑制 `SEQ` 等于上次已投递值的 DATA 帧
  (重传产生的重复)。即便如此 ACK 仍会重发,以便发送方继续推进。

## 7. 连接状态机

状态:`DISCONNECTED`、`CONNECTING`、`CONNECTED`、`ERROR`。

```
        connect()/发送 HELLO
 DISCONNECTED ───────────────► CONNECTING
      ▲   │                        │ 收到 HELLO_ACK
      │   │ 收到 HELLO /           ▼
      │   │ 发送 HELLO_ACK     CONNECTED ──── 收/发 DATA、HEARTBEAT、ACK
      │   └───────────────────►   │  │
      │  收到 DISCONNECT          │  │ 重传次数超限
      └───────────────────────────┘  │ 或对端存活超时
                                      ▼
                                    ERROR ── connect() ──► CONNECTING
```

状态转移:

| 起始          | 事件                                    | 动作                           | 目标          |
|---------------|-----------------------------------------|--------------------------------|---------------|
| DISCONNECTED  | `connect(peer)`                         | 发送 HELLO                     | CONNECTING    |
| DISCONNECTED  | 收到 HELLO                              | 发送 HELLO_ACK                 | CONNECTED     |
| CONNECTING    | 收到 HELLO_ACK(来自 peer)             | —                              | CONNECTED     |
| CONNECTED     | 收到 DATA                              | 投递;若 ACK_REQ 则回 ACK      | CONNECTED     |
| CONNECTED     | 收到 HEARTBEAT                          | 刷新存活                       | CONNECTED     |
| CONNECTED     | `tick` 达到心跳间隔                     | 发送 HEARTBEAT                 | CONNECTED     |
| CONNECTED     | 重传计数 > max_retries                  | 放弃在途 TX                    | ERROR         |
| CONNECTED     | 对端静默 > peer_timeout                 | —                              | ERROR         |
| CONNECTED/ING | 收到 DISCONNECT 或 `disconnect()`       | 发送 DISCONNECT(本地调用)    | DISCONNECTED  |
| ERROR         | `connect(peer)`                         | 发送 HELLO                     | CONNECTING    |

被动建链:处于 `DISCONNECTED` 的节点收到 `HELLO` 时,采纳发送方为其 peer 并直接转入
`CONNECTED`。

### 7.1 Peer 来源校验

一旦节点已关联 peer(`CONNECTING` 或 `CONNECTED`),除 `HELLO` 外收到的每一帧都必须
来自该 peer(`SRC == peer 地址`)。来自其他源的帧将被丢弃并计入 `rx_dropped`,且不刷新
对端存活。`HELLO` 豁免,以便新 peer 进行(重新)被动建链。该规则隔离了点对点会话:
第三方既不能向数据流注入 `DATA`,也不能伪造 `ACK`/`NACK` 误清在途可靠帧。`ACK`/`NACK`
仅在既来自 peer 又回显在途 `SEQ` 时才被采纳。

## 8. 可靠传输(stop-and-wait)

1. 带 `ACK_REQ` 发送的 `DATA` 会被缓存并记录 `SEQ`;同一时刻**只能有一个**在途可靠帧
   (否则返回 `MICP_ERR_BUSY`)。
2. 接收方投递 payload(若非重复)并回 `ACK`,序列号相同。
3. 收到匹配在途 `SEQ` 的 `ACK` 时,发送方清除 pending 状态。
4. 若 `rto_ticks` 内未收到 `ACK`,发送方重传,最多 `max_retries` 次。超过上限则会话进入
   `ERROR`(`MICP_ERR_TIMEOUT`)。
5. 针对在途 `SEQ` 的 `NACK` 会在下一个 tick 触发立即重传。

## 9. 错误检测与恢复汇总

| 情况                       | 检测方式                          | 恢复方式                                  |
|----------------------------|-----------------------------------|-------------------------------------------|
| 帧内比特错误               | CRC-16 不匹配                     | 丢弃;可选 NACK;发送方重传                |
| 可靠 DATA/ACK 丢失         | 重传定时器到期                    | 重传,最多 `max_retries` 次               |
| 重复 DATA                  | `SEQ == 上次已投递`               | 抑制投递;重发 ACK                         |
| 帧失步 / 垃圾字节          | SOF 扫描 + CRC 校验               | 跳过字节直到找到有效帧                     |
| 外来 / 伪造帧              | 关联后 `SRC != peer`              | 丢弃;计入 `rx_dropped`                    |
| 对端失联                   | `peer_timeout` 内无帧             | 转入 ERROR                                |

## 10. 默认值(可按会话调整)

| 参数                 | 默认值  | 字段                        |
|----------------------|---------|-----------------------------|
| 重传超时             | 10      | `rto_ticks`                 |
| 最大重传次数         | 3       | `max_retries`               |
| 心跳间隔             | 30      | `heartbeat_ticks`           |
| 对端存活超时         | 100     | `peer_timeout_ticks`        |

tick 的单位由应用定义(即传给 `micp_session_tick` 的值)。

## 11. 版本管理

`VER` 字段为 `0x01`。接收方必须拒绝不支持版本的帧(`MICP_ERR_VERSION`)。后续修订将提升
`VER` 并在此处记录差异。
