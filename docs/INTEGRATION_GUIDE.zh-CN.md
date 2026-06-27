# MICP 集成指南

> 语言:[English](INTEGRATION_GUIDE.md) | **中文**

如何构建 MICP、将其链接进你的应用,并在真实传输上驱动一个会话。无需任何外部依赖。

---

## 1. 构建

### CMake(推荐)

```bash
cmake -S . -B build
cmake --build build
cd build && ctest --output-on-failure   # 运行测试套件
```

常用选项:

| 选项                         | 默认 | 作用                            |
|------------------------------|------|---------------------------------|
| `-DMICP_BUILD_TESTS=OFF`     | ON   | 跳过构建单元测试                |
| `-DMICP_BUILD_EXAMPLES=OFF`  | ON   | 跳过构建示例                    |
| `-DMICP_WARNINGS_AS_ERRORS=ON` | OFF | 将编译告警视为错误             |

构建产物为静态库 `libmicp.a`,以及(可选)`micp_loopback_demo` 可执行文件。

### Makefile(无 CMake)

```bash
make            # ./build 下生成 libmicp.a + 测试 + 示例
make test       # 构建并运行全部
```

### 嵌入你自己的 CMake 工程

```cmake
add_subdirectory(micp-stack)
target_link_libraries(my_app PRIVATE micp::micp)
```

或直接编译四个 `src/*.c` 文件,并把 `include/` 加入头文件搜索路径。

## 2. 五个核心调用

```c
#include "micp/micp.h"
```

| 调用                          | 用途                                                 |
|-------------------------------|------------------------------------------------------|
| `micp_session_init`           | 用地址与回调初始化会话                               |
| `micp_session_connect`        | 朝某 peer 主动建链(发送 HELLO)                     |
| `micp_session_send`           | 发送 payload(可靠或尽力)                           |
| `micp_session_feed`           | 把收到的字节交给协议栈                               |
| `micp_session_tick`           | 推进定时器(重传 / 心跳 / 存活)                     |

另有 `micp_session_disconnect`、`micp_session_set_event_cb`,以及只读的
`micp_session_tx_busy`。

## 3. 接好回调

你需要提供两个函数。**output** 回调把编码后的字节推到你的传输上;**recv** 回调在应用
payload 到达时被调用。

```c
static micp_err_t my_output(void *user, const uint8_t *data, size_t len) {
    my_transport_t *t = user;
    return transport_write(t, data, len) == (int)len ? MICP_OK : MICP_ERR_INVAL;
}

static void my_recv(void *user, uint16_t src, const uint8_t *payload, size_t len) {
    /* 投递给你的应用逻辑 */
    app_on_message(src, payload, len);
}
```

初始化:

```c
micp_session_t s;
micp_session_init(&s, /*addr=*/0x0010, my_output, my_recv, /*user=*/&my_transport);

/* 可选:观察状态变化 */
micp_session_set_event_cb(&s, my_event_cb);

/* 可选:调整时序(单位即你传给 tick 的值) */
s.rto_ticks          = 5;
s.max_retries        = 4;
s.heartbeat_ticks    = 50;
s.peer_timeout_ticks = 200;
```

## 4. 运行循环

一个典型集成有三个驱动:

```c
/* (a) 每当传输有字节到达: */
uint8_t rx[256];
int n = transport_read(&my_transport, rx, sizeof(rx));
if (n > 0) micp_session_feed(&s, rx, (size_t)n);

/* (b) 周期性(定时器/RTOS tick)推进协议时间: */
micp_session_tick(&s, /*dt=*/1);

/* (c) 当应用要发送时: */
micp_session_send(&s, payload, payload_len, /*reliable=*/1);
```

单线程下可在一个轮询循环里交替执行 (a)(b)(c)。在 RTOS 上,从周期任务调用 `tick`、从 RX
任务调用 `feed` —— 但对**同一个**会话的所有调用必须经由单一上下文,或用互斥锁保护
(见 ARCHITECTURE §7)。

## 5. 建立连接

一方主动建链,另一方被动接受:

```c
/* 节点 A(发起方) */
micp_session_connect(&a, /*peer=*/0x0020);   /* -> CONNECTING, 发送 HELLO */

/* 节点 B 经 feed() 收到 HELLO,自动转入 CONNECTED 并回 HELLO_ACK。
 * 当 A feed 进该 HELLO_ACK 后即变为 CONNECTED。 */
```

可用 `s.state == MICP_STATE_CONNECTED` 或事件回调判断就绪。

## 6. 发送数据

```c
/* 尽力(发完即忘) */
micp_session_send(&s, buf, len, 0);

/* 可靠(ACK + 重传)。同一时刻只能有一个在途可靠帧: */
if (!micp_session_tx_busy(&s)) {
    micp_err_t e = micp_session_send(&s, buf, len, 1);
    if (e == MICP_ERR_BUSY) { /* 前一个可靠发送仍在途 */ }
}
```

可靠发送的完成可由 `micp_session_tx_busy()` 在喂入匹配的 ACK 后返回 0 来观察。若重传耗尽,
会话进入 `MICP_STATE_ERROR`,下一次 `tick` 返回 `MICP_ERR_TIMEOUT`。

## 7. 错误处理

所有入口返回 `micp_err_t`。用 `micp_strerror()` 取可读名称。接收侧的逐帧错误(CRC、坏
版本)**不会**使 `feed()` 失败;它们计入 `s.stats`(`rx_crc_errors`、`rx_dropped`),便于
你监控链路质量:

```c
printf("crc errors: %u, dropped: %u, retransmits: %u\n",
       s.stats.rx_crc_errors, s.stats.rx_dropped, s.stats.retransmits);
```

## 8. 从 ERROR 恢复

超时后会话处于 `MICP_STATE_ERROR`。再次调用 `micp_session_connect()`(允许从 `ERROR`
发起)即可重启握手。

## 9. 移植清单

- [ ] 在你的传输(UART/TCP/CAN-TP/共享内存)上实现 `output`。
- [ ] 在收到字节的位置调用 `feed()`。
- [ ] 从周期性来源以一致的 `dt` 单位调用 `tick()`。
- [ ] 依据 tick 率与链路 RTT 选择 `rto_ticks` / `peer_timeout_ticks`。
- [ ] 确保每个会话单上下文(或互斥锁保护)访问。

针对 STM32F103RCT6 的具体 RTOS 移植(内存预算、FreeRTOS 任务骨架、UART 绑定与工具链
参数),见 **PORTING_STM32F103.zh-CN.md**。

## 10. 参考

完整可运行示例见 `examples/loopback_demo.c`,它在两个进程内节点间完成握手、可靠交换、
尽力回复与有序拆链。
