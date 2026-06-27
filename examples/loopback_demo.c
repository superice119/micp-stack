/**
 * @file loopback_demo.c
 * @brief 端到端 MICP 演示：两个进程内节点通过模拟链路交换数据，
 *        覆盖握手、可靠传输与断开流程。
 *
 * 构建(CMake)：生成 `micp_loopback_demo` 目标。
 * 运行：./micp_loopback_demo（成功时退出码为 0）。
 */
#include "micp/micp.h" /* 引入 "micp/micp.h" 依赖。 */

#include <stdio.h> /* 引入 <stdio.h> 依赖。 */
#include <string.h> /* 引入 <string.h> 依赖。 */

typedef struct { /* 定义演示节点状态结构。 */
    uint8_t buf[4096]; /* 缓存演示节点待发送字节。 */
    size_t  len; /* 记录当前有效数据长度。 */
    const char *name; /* 保存节点显示名称。 */
    int     received; /* 统计演示接收次数。 */
} node_t; /* 结束节点结构类型定义。 */

static micp_err_t out_cb(void *user, const uint8_t *data, size_t len) /* 定义演示发送回调。 */
{
    node_t *n = (node_t *)user; /* 将用户上下文转换为节点指针。 */
    memcpy(n->buf + n->len, data, len); /* 复制数据到目标缓冲区。 */
    n->len += len; /* 累加长度或偏移。 */
    return MICP_OK; /* 返回当前函数结果。 */
}

static void recv_cb(void *user, uint16_t src, const uint8_t *p, size_t len) /* 定义演示接收回调。 */
{
    node_t *n = (node_t *)user; /* 将用户上下文转换为节点指针。 */
    n->received++; /* 递增计数或偏移。 */
    printf("  [%s] received %zu bytes from 0x%04X: \"%.*s\"\n", /* 打印演示输出信息。 */
           n->name, len, src, (int)len, (const char *)p); /* 继续传入格式化输出参数。 */
}

static void event_cb(void *user, micp_state_t o, micp_state_t s) /* 定义演示状态事件回调。 */
{
    node_t *n = (node_t *)user; /* 将用户上下文转换为节点指针。 */
    printf("  [%s] state %s -> %s\n", n->name, micp_state_name(o), /* 打印演示输出信息。 */
           micp_state_name(s)); /* 继续传入格式化输出参数。 */
}

static void pump(node_t *from, micp_session_t *to) /* 定义队列投递辅助函数。 */
{
    if (from->len) { /* 仅在来源队列非空时投递。 */
        micp_session_feed(to, from->buf, from->len); /* 向会话输入收到的字节。 */
        from->len = 0; /* 清空来源节点发送队列。 */
    }
}

int main(void) /* 定义程序入口。 */
{
    printf("MICP loopback demo (lib v%s)\n", MICP_LIB_VERSION_STR); /* 打印演示输出信息。 */

    node_t na = {.name = "A"}, nb = {.name = "B"}; /* 初始化演示节点 A 与 B。 */
    micp_session_t a, b; /* 声明 MICP 会话对象。 */
    micp_session_init(&a, 0x0001, out_cb, recv_cb, &na); /* 初始化 MICP 会话。 */
    micp_session_init(&b, 0x0002, out_cb, recv_cb, &nb); /* 初始化 MICP 会话。 */
    micp_session_set_event_cb(&a, event_cb); /* 注册状态事件回调。 */
    micp_session_set_event_cb(&b, event_cb); /* 注册状态事件回调。 */

    puts("1) handshake"); /* 打印演示阶段标题。 */
    micp_session_connect(&a, 0x0002); /* 发起到对端的连接。 */
    pump(&na, &b);   /* 投递 HELLO 或 DATA 到 B。 */
    pump(&nb, &a);   /* 投递 HELLO_ACK 或 ACK 到 A。 */

    if (a.state != MICP_STATE_CONNECTED || b.state != MICP_STATE_CONNECTED) { /* 按条件检查当前状态。 */
        fprintf(stderr, "handshake failed\n"); /* 打印测试或错误信息。 */
        return 1; /* 返回当前函数结果。 */
    }

    puts("2) reliable data A -> B"); /* 打印演示阶段标题。 */
    const char *hello = "hello industrial world"; /* 准备 A 发往 B 的可靠消息。 */
    micp_session_send(&a, (const uint8_t *)hello, strlen(hello), 1); /* 发送 MICP 应用数据。 */
    pump(&na, &b);   /* 投递 HELLO 或 DATA 到 B。 */
    pump(&nb, &a);   /* 投递 HELLO_ACK 或 ACK 到 A。 */

    if (micp_session_tx_busy(&a)) { /* 按条件检查当前状态。 */
        fprintf(stderr, "reliable send not acknowledged\n"); /* 打印测试或错误信息。 */
        return 1; /* 返回当前函数结果。 */
    }

    puts("3) unreliable data B -> A"); /* 打印演示阶段标题。 */
    const char *pong = "ack from node B"; /* 准备 B 发往 A 的非可靠消息。 */
    micp_session_send(&b, (const uint8_t *)pong, strlen(pong), 0); /* 发送 MICP 应用数据。 */
    pump(&nb, &a); /* 投递排队数据到对端。 */

    puts("4) teardown"); /* 打印演示阶段标题。 */
    micp_session_disconnect(&a); /* 发起会话断开。 */
    pump(&na, &b); /* 投递排队数据到对端。 */

    printf("\nSummary: A.tx=%u A.rx=%u | B.tx=%u B.rx=%u acks_tx=%u\n", /* 打印演示输出信息。 */
           a.stats.tx_frames, a.stats.rx_frames, /* 继续传入格式化输出参数。 */
           b.stats.tx_frames, b.stats.rx_frames, b.stats.acks_tx); /* 继续传入格式化输出参数。 */

    int ok = (na.received >= 1) && (nb.received >= 1) && /* 汇总演示成功条件。 */
             (a.state == MICP_STATE_DISCONNECTED) && /* 继续汇总成功条件。 */
             (b.state == MICP_STATE_DISCONNECTED); /* 确认 B 最终断开。 */
    printf("Result: %s\n", ok ? "OK" : "FAIL"); /* 打印演示输出信息。 */
    return ok ? 0 : 1; /* 返回当前函数结果。 */
}
