/**
 * @file loopback_demo.c
 * @brief End-to-end MICP demo: two in-process nodes exchange data over a
 *        simulated wire, exercising handshake, reliable delivery and teardown.
 *
 * Build (CMake): produces the `micp_loopback_demo` target.
 * Run: ./micp_loopback_demo  (exit code 0 on success)
 */
#include "micp/micp.h"

#include <stdio.h>
#include <string.h>

typedef struct {
    uint8_t buf[4096];
    size_t  len;
    const char *name;
    int     received;
} node_t;

static micp_err_t out_cb(void *user, const uint8_t *data, size_t len)
{
    node_t *n = (node_t *)user;
    memcpy(n->buf + n->len, data, len);
    n->len += len;
    return MICP_OK;
}

static void recv_cb(void *user, uint16_t src, const uint8_t *p, size_t len)
{
    node_t *n = (node_t *)user;
    n->received++;
    printf("  [%s] received %zu bytes from 0x%04X: \"%.*s\"\n",
           n->name, len, src, (int)len, (const char *)p);
}

static void event_cb(void *user, micp_state_t o, micp_state_t s)
{
    node_t *n = (node_t *)user;
    printf("  [%s] state %s -> %s\n", n->name, micp_state_name(o),
           micp_state_name(s));
}

static void pump(node_t *from, micp_session_t *to)
{
    if (from->len) {
        micp_session_feed(to, from->buf, from->len);
        from->len = 0;
    }
}

int main(void)
{
    printf("MICP loopback demo (lib v%s)\n", MICP_LIB_VERSION_STR);

    node_t na = {.name = "A"}, nb = {.name = "B"};
    micp_session_t a, b;
    micp_session_init(&a, 0x0001, out_cb, recv_cb, &na);
    micp_session_init(&b, 0x0002, out_cb, recv_cb, &nb);
    micp_session_set_event_cb(&a, event_cb);
    micp_session_set_event_cb(&b, event_cb);

    puts("1) handshake");
    micp_session_connect(&a, 0x0002);
    pump(&na, &b);   /* HELLO -> B */
    pump(&nb, &a);   /* HELLO_ACK -> A */

    if (a.state != MICP_STATE_CONNECTED || b.state != MICP_STATE_CONNECTED) {
        fprintf(stderr, "handshake failed\n");
        return 1;
    }

    puts("2) reliable data A -> B");
    const char *hello = "hello industrial world";
    micp_session_send(&a, (const uint8_t *)hello, strlen(hello), 1);
    pump(&na, &b);   /* DATA + B emits ACK */
    pump(&nb, &a);   /* ACK -> A */

    if (micp_session_tx_busy(&a)) {
        fprintf(stderr, "reliable send not acknowledged\n");
        return 1;
    }

    puts("3) unreliable data B -> A");
    const char *pong = "ack from node B";
    micp_session_send(&b, (const uint8_t *)pong, strlen(pong), 0);
    pump(&nb, &a);

    puts("4) teardown");
    micp_session_disconnect(&a);
    pump(&na, &b);

    printf("\nSummary: A.tx=%u A.rx=%u | B.tx=%u B.rx=%u acks_tx=%u\n",
           a.stats.tx_frames, a.stats.rx_frames,
           b.stats.tx_frames, b.stats.rx_frames, b.stats.acks_tx);

    int ok = (na.received >= 1) && (nb.received >= 1) &&
             (a.state == MICP_STATE_DISCONNECTED) &&
             (b.state == MICP_STATE_DISCONNECTED);
    printf("Result: %s\n", ok ? "OK" : "FAIL");
    return ok ? 0 : 1;
}
