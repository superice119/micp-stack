#include "micp/micp_session.h" /* 引入 "micp/micp_session.h" 依赖。 */
#include "micp_test.h" /* 引入 "micp_test.h" 依赖。 */

#include <string.h> /* 引入 <string.h> 依赖。 */

/* ----------------------------------------------------------- 测试辅助代码 */

typedef struct { /* 定义测试节点状态结构。 */
    uint8_t  buf[4096]; /* 缓存测试节点发出的字节。 */
    size_t   len;          /* 记录当前有效数据长度。 */
    uint8_t  last_rx[MICP_MAX_PAYLOAD]; /* 保存最近一次接收数据。 */
    size_t   last_rx_len; /* 记录最近接收负载长度。 */
    int      rx_count; /* 统计应用层接收次数。 */
    uint16_t last_src; /* 记录最近一次来源地址。 */
    int      events; /* 统计状态事件次数。 */
    micp_state_t last_new_state; /* 记录最新状态。 */
} node_t; /* 结束节点结构类型定义。 */

static micp_err_t node_output(void *user, const uint8_t *data, size_t len) /* 定义测试节点输出回调。 */
{
    node_t *n = (node_t *)user; /* 将用户上下文转换为节点指针。 */
    if (n->len + len > sizeof(n->buf)) { /* 检查输出队列容量是否足够。 */
        return MICP_ERR_NOBUFS; /* 返回当前函数结果。 */
    }
    memcpy(n->buf + n->len, data, len); /* 复制数据到目标缓冲区。 */
    n->len += len; /* 累加长度或偏移。 */
    return MICP_OK; /* 返回当前函数结果。 */
}

static void node_recv(void *user, uint16_t src, const uint8_t *p, size_t len) /* 定义测试节点接收回调。 */
{
    node_t *n = (node_t *)user; /* 将用户上下文转换为节点指针。 */
    n->rx_count++; /* 统计应用层接收次数。 */
    n->last_src = src; /* 记录最近一次来源地址。 */
    n->last_rx_len = len; /* 记录最近接收负载长度。 */
    if (len > 0) { /* 仅在负载非空时复制数据。 */
        memcpy(n->last_rx, p, len); /* 保存最近一次接收数据。 */
    }
}

static void node_event(void *user, micp_state_t old_s, micp_state_t new_s) /* 定义测试节点状态事件回调。 */
{
    node_t *n = (node_t *)user; /* 将用户上下文转换为节点指针。 */
    (void)old_s; /* 显式标记旧状态参数未使用。 */
    n->events++; /* 统计状态事件次数。 */
    n->last_new_state = new_s; /* 记录最新状态。 */
}

/* 将 from 节点 TX 队列全部投递给 to 会话，然后清空。 */
static void pump(node_t *from, micp_session_t *to) /* 定义队列投递辅助函数。 */
{
    if (from->len > 0) { /* 仅在来源队列非空时投递。 */
        micp_session_feed(to, from->buf, from->len); /* 向会话输入收到的字节。 */
    }
    from->len = 0; /* 清空来源节点发送队列。 */
}

/* 丢弃已排队数据，不投递给对端。 */
static void drop(node_t *from) /* 定义丢弃队列辅助函数。 */
{
    from->len = 0; /* 清空来源节点发送队列。 */
}

/* ------------------------------------------------------------------- 测试用例 */

static void test_handshake(void) /* 定义 test_handshake 测试用例。 */
{
    node_t na = {0}, nb = {0}; /* 初始化本测试使用的节点上下文。 */
    micp_session_t a, b; /* 声明 MICP 会话对象。 */
    micp_session_init(&a, 0x0001, node_output, node_recv, &na); /* 初始化 MICP 会话。 */
    micp_session_init(&b, 0x0002, node_output, node_recv, &nb); /* 初始化 MICP 会话。 */
    micp_session_set_event_cb(&a, node_event); /* 注册状态事件回调。 */
    micp_session_set_event_cb(&b, node_event); /* 注册状态事件回调。 */

    CHECK_OK(micp_session_connect(&a, 0x0002)); /* 断言调用成功返回。 */
    CHECK_EQ(a.state, MICP_STATE_CONNECTING); /* 断言实际值等于期望值。 */

    pump(&na, &b);                       /* 将 A 的队列投递给 B。 */
    CHECK_EQ(b.state, MICP_STATE_CONNECTED); /* 断言实际值等于期望值。 */
    CHECK_EQ(b.peer_addr, 0x0001); /* 验证 B 记录 A 为对端。 */

    pump(&nb, &a);                       /* 将 B 的队列投递给 A。 */
    CHECK_EQ(a.state, MICP_STATE_CONNECTED); /* 断言实际值等于期望值。 */
    CHECK_EQ(a.peer_addr, 0x0002); /* 验证 A 记录 B 为对端。 */
}

/* 建立一对已连接会话，并保持队列为空。 */
static void establish(micp_session_t *a, micp_session_t *b, /* 定义会话建连辅助函数。 */
                      node_t *na, node_t *nb) /* 接收两个节点上下文参数。 */
{
    micp_session_init(a, 0x0001, node_output, node_recv, na); /* 初始化 MICP 会话。 */
    micp_session_init(b, 0x0002, node_output, node_recv, nb); /* 初始化 MICP 会话。 */
    micp_session_connect(a, 0x0002); /* 发起到对端的连接。 */
    pump(na, b); /* 投递排队数据到对端。 */
    pump(nb, a); /* 投递排队数据到对端。 */
}

static void test_unreliable_data(void) /* 定义 test_unreliable_data 测试用例。 */
{
    node_t na = {0}, nb = {0}; /* 初始化本测试使用的节点上下文。 */
    micp_session_t a, b; /* 声明 MICP 会话对象。 */
    establish(&a, &b, &na, &nb); /* 建立测试用连接。 */

    const uint8_t msg[] = {1, 2, 3, 4}; /* 准备测试负载数据。 */
    CHECK_OK(micp_session_send(&a, msg, sizeof(msg), 0)); /* 断言调用成功返回。 */
    pump(&na, &b); /* 投递排队数据到对端。 */
    CHECK_EQ(nb.rx_count, 1); /* 验证应用层收到一次数据。 */
    CHECK_EQ(nb.last_rx_len, 4); /* 验证接收负载长度。 */
    CHECK_EQ(nb.last_src, 0x0001); /* 验证来源地址为节点 A。 */
    CHECK_EQ(memcmp(nb.last_rx, msg, 4), 0); /* 验证接收负载内容。 */
}

static void test_reliable_ack(void) /* 定义 test_reliable_ack 测试用例。 */
{
    node_t na = {0}, nb = {0}; /* 初始化本测试使用的节点上下文。 */
    micp_session_t a, b; /* 声明 MICP 会话对象。 */
    establish(&a, &b, &na, &nb); /* 建立测试用连接。 */

    const uint8_t msg[] = {9, 8, 7}; /* 准备测试负载数据。 */
    CHECK_OK(micp_session_send(&a, msg, sizeof(msg), 1)); /* 断言调用成功返回。 */
    CHECK(micp_session_tx_busy(&a)); /* 断言条件为真。 */

    pump(&na, &b);                       /* 将 A 的队列投递给 B。 */
    CHECK_EQ(nb.rx_count, 1); /* 验证应用层收到一次数据。 */
    pump(&nb, &a);                       /* 将 B 的队列投递给 A。 */
    CHECK(!micp_session_tx_busy(&a)); /* 断言条件为真。 */
    CHECK_EQ(a.stats.acks_rx, 1u); /* 断言实际值等于期望值。 */
}

static void test_busy_rejects_second_reliable(void) /* 定义 test_busy_rejects_second_reliable 测试用例。 */
{
    node_t na = {0}, nb = {0}; /* 初始化本测试使用的节点上下文。 */
    micp_session_t a, b; /* 声明 MICP 会话对象。 */
    establish(&a, &b, &na, &nb); /* 建立测试用连接。 */

    uint8_t m1[] = {1}; /* 准备第一条可靠消息。 */
    uint8_t m2[] = {2}; /* 准备第二条可靠消息。 */
    CHECK_OK(micp_session_send(&a, m1, 1, 1)); /* 断言调用成功返回。 */
    CHECK_EQ(micp_session_send(&a, m2, 1, 1), MICP_ERR_BUSY); /* 断言实际值等于期望值。 */
}

static void test_retransmit_then_ack(void) /* 定义 test_retransmit_then_ack 测试用例。 */
{
    node_t na = {0}, nb = {0}; /* 初始化本测试使用的节点上下文。 */
    micp_session_t a, b; /* 声明 MICP 会话对象。 */
    establish(&a, &b, &na, &nb); /* 建立测试用连接。 */

    const uint8_t msg[] = {0xAA, 0xBB}; /* 准备测试负载数据。 */
    CHECK_OK(micp_session_send(&a, msg, sizeof(msg), 1)); /* 断言调用成功返回。 */
    drop(&na);                           /* 丢弃首次 DATA 帧。 */

    CHECK_OK(micp_session_tick(&a, a.rto_ticks)); /* 验证超时触发重传。 */
    CHECK_EQ(a.stats.retransmits, 1u); /* 断言实际值等于期望值。 */
    CHECK(micp_session_tx_busy(&a)); /* 断言条件为真。 */

    pump(&na, &b);                       /* 将 A 的队列投递给 B。 */
    CHECK_EQ(nb.rx_count, 1); /* 验证应用层收到一次数据。 */
    pump(&nb, &a); /* 投递排队数据到对端。 */
    CHECK(!micp_session_tx_busy(&a)); /* 断言条件为真。 */
}

static void test_retransmit_exhaustion(void) /* 定义 test_retransmit_exhaustion 测试用例。 */
{
    node_t na = {0}, nb = {0}; /* 初始化本测试使用的节点上下文。 */
    micp_session_t a, b; /* 声明 MICP 会话对象。 */
    establish(&a, &b, &na, &nb); /* 建立测试用连接。 */

    micp_session_send(&a, (const uint8_t *)"x", 1, 1); /* 发送 MICP 应用数据。 */
    drop(&na); /* 丢弃当前排队数据。 */

    micp_err_t last = MICP_OK; /* 保存最后一次 tick 结果。 */
    for (uint32_t i = 0; i < a.max_retries + 1; ++i) { /* 循环处理测试数据。 */
        last = micp_session_tick(&a, a.rto_ticks); /* 记录本次重传 tick 结果。 */
        drop(&na); /* 丢弃当前排队数据。 */
    }
    CHECK_EQ(last, MICP_ERR_TIMEOUT); /* 断言实际值等于期望值。 */
    CHECK_EQ(a.state, MICP_STATE_ERROR); /* 断言实际值等于期望值。 */
    CHECK(!micp_session_tx_busy(&a)); /* 断言条件为真。 */
}

static void test_duplicate_suppression(void) /* 定义 test_duplicate_suppression 测试用例。 */
{
    node_t na = {0}, nb = {0}; /* 初始化本测试使用的节点上下文。 */
    micp_session_t a, b; /* 声明 MICP 会话对象。 */
    establish(&a, &b, &na, &nb); /* 建立测试用连接。 */

    micp_session_send(&a, (const uint8_t *)"dup", 3, 1); /* 发送 MICP 应用数据。 */
    /* 捕获已编码的可靠 DATA 帧，然后投递两次。 */
    uint8_t frame[MICP_MAX_FRAME]; /* 声明保存可靠数据帧的缓冲区。 */
    size_t flen = na.len; /* 记录捕获帧长度。 */
    memcpy(frame, na.buf, flen); /* 复制数据到目标缓冲区。 */
    na.len = 0; /* 清空节点 A 的待发送队列。 */

    micp_session_feed(&b, frame, flen); /* 向会话输入收到的字节。 */
    micp_session_feed(&b, frame, flen);  /* 再次投递重复的可靠帧。 */
    CHECK_EQ(nb.rx_count, 1);            /* 验证重复帧只交付一次。 */
    CHECK_EQ(b.stats.acks_tx, 2u);      /* 验证重复帧仍分别 ACK。 */
}

static void test_byte_fragmentation(void) /* 定义 test_byte_fragmentation 测试用例。 */
{
    node_t na = {0}, nb = {0}; /* 初始化本测试使用的节点上下文。 */
    micp_session_t a, b; /* 声明 MICP 会话对象。 */
    establish(&a, &b, &na, &nb); /* 建立测试用连接。 */

    const uint8_t msg[] = {0x11, 0x22, 0x33, 0x44, 0x55}; /* 准备测试负载数据。 */
    micp_session_send(&a, msg, sizeof(msg), 0); /* 发送 MICP 应用数据。 */

    /* 按单字节方式喂给 B。 */
    for (size_t i = 0; i < na.len; ++i) { /* 循环处理测试数据。 */
        micp_session_feed(&b, &na.buf[i], 1); /* 逐字节投递到接收会话。 */
    }
    na.len = 0; /* 清空节点 A 的待发送队列。 */
    CHECK_EQ(nb.rx_count, 1); /* 验证应用层收到一次数据。 */
    CHECK_EQ(memcmp(nb.last_rx, msg, sizeof(msg)), 0); /* 验证分片接收内容。 */
}

static void test_crc_error_dropped(void) /* 定义 test_crc_error_dropped 测试用例。 */
{
    node_t na = {0}, nb = {0}; /* 初始化本测试使用的节点上下文。 */
    micp_session_t a, b; /* 声明 MICP 会话对象。 */
    establish(&a, &b, &na, &nb); /* 建立测试用连接。 */

    micp_session_send(&a, (const uint8_t *)"abcd", 4, 0); /* 发送 MICP 应用数据。 */
    na.buf[MICP_HEADER_SIZE + 1] ^= 0xFF; /* 篡改负载字节以模拟 CRC 错误。 */
    pump(&na, &b); /* 投递排队数据到对端。 */
    CHECK_EQ(nb.rx_count, 0); /* 验证应用层未收到数据。 */
    CHECK_EQ(b.stats.rx_crc_errors, 1u); /* 断言实际值等于期望值。 */
}

static void test_resync_after_garbage(void) /* 定义 test_resync_after_garbage 测试用例。 */
{
    node_t na = {0}, nb = {0}; /* 初始化本测试使用的节点上下文。 */
    micp_session_t a, b; /* 声明 MICP 会话对象。 */
    establish(&a, &b, &na, &nb); /* 建立测试用连接。 */

    /* 在有效帧前插入垃圾字节，解析器必须重新同步到 SOF。 */
    micp_session_send(&a, (const uint8_t *)"ok", 2, 0); /* 发送 MICP 应用数据。 */
    uint8_t stream[64]; /* 声明带噪声输入流缓冲区。 */
    size_t off = 0; /* 初始化输入流写入偏移。 */
    stream[off++] = 0x00; /* 插入第一段垃圾字节。 */
    stream[off++] = 0x13; /* 插入第二段垃圾字节。 */
    stream[off++] = 0xA5; /* 插入类似 SOF 的干扰字节。 */
    stream[off++] = 0x42; /* 插入最后一个垃圾字节。 */
    memcpy(stream + off, na.buf, na.len); /* 复制数据到目标缓冲区。 */
    off += na.len; /* 累加长度或偏移。 */
    na.len = 0; /* 清空节点 A 的待发送队列。 */

    micp_session_feed(&b, stream, off); /* 向会话输入收到的字节。 */
    CHECK_EQ(nb.rx_count, 1); /* 验证应用层收到一次数据。 */
    CHECK_EQ(memcmp(nb.last_rx, "ok", 2), 0); /* 验证重同步后的负载内容。 */
}

static void test_disconnect(void) /* 定义 test_disconnect 测试用例。 */
{
    node_t na = {0}, nb = {0}; /* 初始化本测试使用的节点上下文。 */
    micp_session_t a, b; /* 声明 MICP 会话对象。 */
    establish(&a, &b, &na, &nb); /* 建立测试用连接。 */

    CHECK_OK(micp_session_disconnect(&a)); /* 断言调用成功返回。 */
    CHECK_EQ(a.state, MICP_STATE_DISCONNECTED); /* 断言实际值等于期望值。 */
    pump(&na, &b); /* 投递排队数据到对端。 */
    CHECK_EQ(b.state, MICP_STATE_DISCONNECTED); /* 断言实际值等于期望值。 */
}

static void test_heartbeat_emitted(void) /* 定义 test_heartbeat_emitted 测试用例。 */
{
    node_t na = {0}, nb = {0}; /* 初始化本测试使用的节点上下文。 */
    micp_session_t a, b; /* 声明 MICP 会话对象。 */
    establish(&a, &b, &na, &nb); /* 建立测试用连接。 */

    na.len = 0; /* 清空节点 A 的待发送队列。 */
    CHECK_OK(micp_session_tick(&a, a.heartbeat_ticks)); /* 断言调用成功返回。 */
    /* 此时 A 应已排队一个心跳帧。 */
    CHECK(na.len >= MICP_HEADER_SIZE + MICP_TRAILER_SIZE); /* 断言条件为真。 */
    micp_frame_t f; size_t c; /* 声明 MICP 帧对象。 */
    CHECK_OK(micp_frame_decode(na.buf, na.len, &f, &c)); /* 断言调用成功返回。 */
    CHECK_EQ(f.type, MICP_MSG_HEARTBEAT); /* 断言实际值等于期望值。 */
}

static void test_peer_timeout(void) /* 定义 test_peer_timeout 测试用例。 */
{
    node_t na = {0}, nb = {0}; /* 初始化本测试使用的节点上下文。 */
    micp_session_t a, b; /* 声明 MICP 会话对象。 */
    establish(&a, &b, &na, &nb); /* 建立测试用连接。 */

    /* 超过对端超时时间未收到帧，应进入 ERROR。 */
    micp_err_t e = micp_session_tick(&a, a.peer_timeout_ticks); /* 执行 tick 并记录错误码。 */
    CHECK_EQ(e, MICP_ERR_TIMEOUT); /* 断言实际值等于期望值。 */
    CHECK_EQ(a.state, MICP_STATE_ERROR); /* 断言实际值等于期望值。 */
}

static void test_send_requires_connected(void) /* 定义 test_send_requires_connected 测试用例。 */
{
    node_t na = {0}; /* 初始化本测试使用的节点上下文。 */
    micp_session_t a; /* 声明 MICP 会话对象。 */
    micp_session_init(&a, 0x0001, node_output, node_recv, &na); /* 初始化 MICP 会话。 */
    CHECK_EQ(micp_session_send(&a, (const uint8_t *)"x", 1, 0), MICP_ERR_STATE); /* 断言实际值等于期望值。 */
}

/* 已连接会话必须拒绝来源不是绑定对端的 DATA。 */
static void test_connected_rejects_data_from_non_peer(void) /* 定义 test_connected_rejects_data_from_non_peer 测试用例。 */
{
    node_t na = {0}, nb = {0}; /* 初始化本测试使用的节点上下文。 */
    micp_session_t a, b; /* 声明 MICP 会话对象。 */
    establish(&a, &b, &na, &nb);   /* 建立测试用连接。 */

    /* 伪造来自无关节点 0x0099、发往 b(0x0002) 的 DATA。 */
    micp_frame_t f; /* 声明 MICP 帧对象。 */
    memset(&f, 0, sizeof(f)); /* 清零结构体初始状态。 */
    f.type   = MICP_MSG_DATA; /* 设置帧类型字段。 */
    f.src    = 0x0099;             /* 设置帧源地址字段。 */
    f.dst    = 0x0002; /* 设置帧目标地址字段。 */
    f.seq    = 0x1234; /* 设置帧序号字段。 */
    f.length = 3; /* 设置帧负载长度字段。 */
    memcpy(f.payload, "evt", 3); /* 复制数据到目标缓冲区。 */

    uint8_t buf[MICP_MAX_FRAME]; /* 声明最大帧编码缓冲区。 */
    size_t  n = 0; /* 初始化编码输出长度。 */
    CHECK_OK(micp_frame_encode(&f, buf, sizeof(buf), &n)); /* 断言调用成功返回。 */
    micp_session_feed(&b, buf, n); /* 向会话输入收到的字节。 */

    CHECK_EQ(nb.rx_count, 0);          /* 验证外来 DATA 未交付应用层。 */
    CHECK_EQ(b.stats.rx_dropped, 1u);  /* 验证外来 DATA 被丢弃。 */
}

/* 已连接会话必须拒绝来源不是绑定对端的 ACK。 */
static void test_connected_rejects_ack_from_non_peer(void) /* 定义 test_connected_rejects_ack_from_non_peer 测试用例。 */
{
    node_t na = {0}, nb = {0}; /* 初始化本测试使用的节点上下文。 */
    micp_session_t a, b; /* 声明 MICP 会话对象。 */
    establish(&a, &b, &na, &nb);   /* 建立测试用连接。 */

    /* a 发送可靠 DATA 后，正在等待 pending_seq 对应 ACK。 */
    micp_session_send(&a, (const uint8_t *)"hi", 2, 1); /* 发送 MICP 应用数据。 */
    na.len = 0;                         /* 丢弃在途 DATA 以便伪造 ACK。 */
    CHECK(micp_session_tx_busy(&a)); /* 断言条件为真。 */
    uint16_t pseq = a.pending_seq; /* 保存待确认的序号。 */

    /* 伪造来自陌生节点 0x0099、序号正确的 ACK。 */
    micp_frame_t f; /* 声明 MICP 帧对象。 */
    memset(&f, 0, sizeof(f)); /* 清零结构体初始状态。 */
    f.type = MICP_MSG_ACK; /* 设置帧类型字段。 */
    f.src  = 0x0099;                    /* 设置帧源地址字段。 */
    f.dst  = 0x0001; /* 设置帧目标地址字段。 */
    f.seq  = pseq; /* 设置帧序号字段。 */

    uint8_t buf[MICP_MAX_FRAME]; /* 声明最大帧编码缓冲区。 */
    size_t  n = 0; /* 初始化编码输出长度。 */
    CHECK_OK(micp_frame_encode(&f, buf, sizeof(buf), &n)); /* 断言调用成功返回。 */
    micp_session_feed(&a, buf, n); /* 向会话输入收到的字节。 */

    CHECK(micp_session_tx_busy(&a));    /* 验证伪造 ACK 未清除待确认帧。 */
    CHECK_EQ(a.stats.acks_rx, 0u); /* 验证未计入外来 ACK。 */
    CHECK_EQ(a.stats.rx_dropped, 1u); /* 验证外来 ACK 被丢弃。 */
}

int main(void) /* 定义程序入口。 */
{
    MICP_RUN(test_handshake); /* 运行指定测试用例。 */
    MICP_RUN(test_unreliable_data); /* 运行指定测试用例。 */
    MICP_RUN(test_reliable_ack); /* 运行指定测试用例。 */
    MICP_RUN(test_busy_rejects_second_reliable); /* 运行指定测试用例。 */
    MICP_RUN(test_retransmit_then_ack); /* 运行指定测试用例。 */
    MICP_RUN(test_retransmit_exhaustion); /* 运行指定测试用例。 */
    MICP_RUN(test_duplicate_suppression); /* 运行指定测试用例。 */
    MICP_RUN(test_byte_fragmentation); /* 运行指定测试用例。 */
    MICP_RUN(test_crc_error_dropped); /* 运行指定测试用例。 */
    MICP_RUN(test_resync_after_garbage); /* 运行指定测试用例。 */
    MICP_RUN(test_disconnect); /* 运行指定测试用例。 */
    MICP_RUN(test_heartbeat_emitted); /* 运行指定测试用例。 */
    MICP_RUN(test_peer_timeout); /* 运行指定测试用例。 */
    MICP_RUN(test_send_requires_connected); /* 运行指定测试用例。 */
    MICP_RUN(test_connected_rejects_data_from_non_peer); /* 运行指定测试用例。 */
    MICP_RUN(test_connected_rejects_ack_from_non_peer); /* 运行指定测试用例。 */
    MICP_TEST_SUMMARY(); /* 输出测试汇总并返回退出码。 */
}
