#include "micp/micp_session.h"
#include "micp_test.h"

#include <string.h>

/* ----------------------------------------------------------- test harness */

typedef struct {
    uint8_t  buf[4096];
    size_t   len;          /* bytes this node has emitted (its TX queue)     */
    uint8_t  last_rx[MICP_MAX_PAYLOAD];
    size_t   last_rx_len;
    int      rx_count;
    uint16_t last_src;
    int      events;
    micp_state_t last_new_state;
} node_t;

static micp_err_t node_output(void *user, const uint8_t *data, size_t len)
{
    node_t *n = (node_t *)user;
    if (n->len + len > sizeof(n->buf)) {
        return MICP_ERR_NOBUFS;
    }
    memcpy(n->buf + n->len, data, len);
    n->len += len;
    return MICP_OK;
}

static void node_recv(void *user, uint16_t src, const uint8_t *p, size_t len)
{
    node_t *n = (node_t *)user;
    n->rx_count++;
    n->last_src = src;
    n->last_rx_len = len;
    if (len > 0) {
        memcpy(n->last_rx, p, len);
    }
}

static void node_event(void *user, micp_state_t old_s, micp_state_t new_s)
{
    node_t *n = (node_t *)user;
    (void)old_s;
    n->events++;
    n->last_new_state = new_s;
}

/* Deliver everything in 'from' node's TX queue into 'to' session, then clear. */
static void pump(node_t *from, micp_session_t *to)
{
    if (from->len > 0) {
        micp_session_feed(to, from->buf, from->len);
    }
    from->len = 0;
}

/* Drop (lose) whatever is queued without delivering it. */
static void drop(node_t *from)
{
    from->len = 0;
}

/* ------------------------------------------------------------------- tests */

static void test_handshake(void)
{
    node_t na = {0}, nb = {0};
    micp_session_t a, b;
    micp_session_init(&a, 0x0001, node_output, node_recv, &na);
    micp_session_init(&b, 0x0002, node_output, node_recv, &nb);
    micp_session_set_event_cb(&a, node_event);
    micp_session_set_event_cb(&b, node_event);

    CHECK_OK(micp_session_connect(&a, 0x0002));
    CHECK_EQ(a.state, MICP_STATE_CONNECTING);

    pump(&na, &b);                       /* B gets HELLO -> CONNECTED + ACK   */
    CHECK_EQ(b.state, MICP_STATE_CONNECTED);
    CHECK_EQ(b.peer_addr, 0x0001);

    pump(&nb, &a);                       /* A gets HELLO_ACK -> CONNECTED     */
    CHECK_EQ(a.state, MICP_STATE_CONNECTED);
    CHECK_EQ(a.peer_addr, 0x0002);
}

/* Establish a connected pair, leaving queues empty. */
static void establish(micp_session_t *a, micp_session_t *b,
                      node_t *na, node_t *nb)
{
    micp_session_init(a, 0x0001, node_output, node_recv, na);
    micp_session_init(b, 0x0002, node_output, node_recv, nb);
    micp_session_connect(a, 0x0002);
    pump(na, b);
    pump(nb, a);
}

static void test_unreliable_data(void)
{
    node_t na = {0}, nb = {0};
    micp_session_t a, b;
    establish(&a, &b, &na, &nb);

    const uint8_t msg[] = {1, 2, 3, 4};
    CHECK_OK(micp_session_send(&a, msg, sizeof(msg), 0));
    pump(&na, &b);
    CHECK_EQ(nb.rx_count, 1);
    CHECK_EQ(nb.last_rx_len, 4);
    CHECK_EQ(nb.last_src, 0x0001);
    CHECK_EQ(memcmp(nb.last_rx, msg, 4), 0);
}

static void test_reliable_ack(void)
{
    node_t na = {0}, nb = {0};
    micp_session_t a, b;
    establish(&a, &b, &na, &nb);

    const uint8_t msg[] = {9, 8, 7};
    CHECK_OK(micp_session_send(&a, msg, sizeof(msg), 1));
    CHECK(micp_session_tx_busy(&a));

    pump(&na, &b);                       /* B delivers + emits ACK            */
    CHECK_EQ(nb.rx_count, 1);
    pump(&nb, &a);                       /* A receives ACK -> not busy        */
    CHECK(!micp_session_tx_busy(&a));
    CHECK_EQ(a.stats.acks_rx, 1u);
}

static void test_busy_rejects_second_reliable(void)
{
    node_t na = {0}, nb = {0};
    micp_session_t a, b;
    establish(&a, &b, &na, &nb);

    uint8_t m1[] = {1};
    uint8_t m2[] = {2};
    CHECK_OK(micp_session_send(&a, m1, 1, 1));
    CHECK_EQ(micp_session_send(&a, m2, 1, 1), MICP_ERR_BUSY);
}

static void test_retransmit_then_ack(void)
{
    node_t na = {0}, nb = {0};
    micp_session_t a, b;
    establish(&a, &b, &na, &nb);

    const uint8_t msg[] = {0xAA, 0xBB};
    CHECK_OK(micp_session_send(&a, msg, sizeof(msg), 1));
    drop(&na);                           /* first DATA is lost                */

    CHECK_OK(micp_session_tick(&a, a.rto_ticks)); /* triggers retransmit      */
    CHECK_EQ(a.stats.retransmits, 1u);
    CHECK(micp_session_tx_busy(&a));

    pump(&na, &b);                       /* retransmit delivered + ACK        */
    CHECK_EQ(nb.rx_count, 1);
    pump(&nb, &a);
    CHECK(!micp_session_tx_busy(&a));
}

static void test_retransmit_exhaustion(void)
{
    node_t na = {0}, nb = {0};
    micp_session_t a, b;
    establish(&a, &b, &na, &nb);

    micp_session_send(&a, (const uint8_t *)"x", 1, 1);
    drop(&na);

    micp_err_t last = MICP_OK;
    for (uint32_t i = 0; i < a.max_retries + 1; ++i) {
        last = micp_session_tick(&a, a.rto_ticks);
        drop(&na); /* keep losing the retransmissions */
    }
    CHECK_EQ(last, MICP_ERR_TIMEOUT);
    CHECK_EQ(a.state, MICP_STATE_ERROR);
    CHECK(!micp_session_tx_busy(&a));
}

static void test_duplicate_suppression(void)
{
    node_t na = {0}, nb = {0};
    micp_session_t a, b;
    establish(&a, &b, &na, &nb);

    micp_session_send(&a, (const uint8_t *)"dup", 3, 1);
    /* Capture the encoded reliable DATA frame, then deliver it twice. */
    uint8_t frame[MICP_MAX_FRAME];
    size_t flen = na.len;
    memcpy(frame, na.buf, flen);
    na.len = 0;

    micp_session_feed(&b, frame, flen);
    micp_session_feed(&b, frame, flen);  /* simulated duplicate retransmit    */
    CHECK_EQ(nb.rx_count, 1);            /* delivered only once               */
    CHECK_EQ(b.stats.acks_tx, 2u);      /* but ACKed both times              */
}

static void test_byte_fragmentation(void)
{
    node_t na = {0}, nb = {0};
    micp_session_t a, b;
    establish(&a, &b, &na, &nb);

    const uint8_t msg[] = {0x11, 0x22, 0x33, 0x44, 0x55};
    micp_session_send(&a, msg, sizeof(msg), 0);

    /* Feed B one byte at a time. */
    for (size_t i = 0; i < na.len; ++i) {
        micp_session_feed(&b, &na.buf[i], 1);
    }
    na.len = 0;
    CHECK_EQ(nb.rx_count, 1);
    CHECK_EQ(memcmp(nb.last_rx, msg, sizeof(msg)), 0);
}

static void test_crc_error_dropped(void)
{
    node_t na = {0}, nb = {0};
    micp_session_t a, b;
    establish(&a, &b, &na, &nb);

    micp_session_send(&a, (const uint8_t *)"abcd", 4, 0);
    na.buf[MICP_HEADER_SIZE + 1] ^= 0xFF; /* corrupt payload                  */
    pump(&na, &b);
    CHECK_EQ(nb.rx_count, 0);
    CHECK_EQ(b.stats.rx_crc_errors, 1u);
}

static void test_resync_after_garbage(void)
{
    node_t na = {0}, nb = {0};
    micp_session_t a, b;
    establish(&a, &b, &na, &nb);

    /* Prepend garbage bytes before a valid frame; parser must resync to SOF. */
    micp_session_send(&a, (const uint8_t *)"ok", 2, 0);
    uint8_t stream[64];
    size_t off = 0;
    stream[off++] = 0x00;
    stream[off++] = 0x13;
    stream[off++] = 0xA5; /* a stray SOF-looking byte with no valid frame     */
    stream[off++] = 0x42;
    memcpy(stream + off, na.buf, na.len);
    off += na.len;
    na.len = 0;

    micp_session_feed(&b, stream, off);
    CHECK_EQ(nb.rx_count, 1);
    CHECK_EQ(memcmp(nb.last_rx, "ok", 2), 0);
}

static void test_disconnect(void)
{
    node_t na = {0}, nb = {0};
    micp_session_t a, b;
    establish(&a, &b, &na, &nb);

    CHECK_OK(micp_session_disconnect(&a));
    CHECK_EQ(a.state, MICP_STATE_DISCONNECTED);
    pump(&na, &b);
    CHECK_EQ(b.state, MICP_STATE_DISCONNECTED);
}

static void test_heartbeat_emitted(void)
{
    node_t na = {0}, nb = {0};
    micp_session_t a, b;
    establish(&a, &b, &na, &nb);

    na.len = 0;
    CHECK_OK(micp_session_tick(&a, a.heartbeat_ticks));
    /* A heartbeat frame should now be queued. */
    CHECK(na.len >= MICP_HEADER_SIZE + MICP_TRAILER_SIZE);
    micp_frame_t f; size_t c;
    CHECK_OK(micp_frame_decode(na.buf, na.len, &f, &c));
    CHECK_EQ(f.type, MICP_MSG_HEARTBEAT);
}

static void test_peer_timeout(void)
{
    node_t na = {0}, nb = {0};
    micp_session_t a, b;
    establish(&a, &b, &na, &nb);

    /* No frames from peer for longer than the timeout -> ERROR. */
    micp_err_t e = micp_session_tick(&a, a.peer_timeout_ticks);
    CHECK_EQ(e, MICP_ERR_TIMEOUT);
    CHECK_EQ(a.state, MICP_STATE_ERROR);
}

static void test_send_requires_connected(void)
{
    node_t na = {0};
    micp_session_t a;
    micp_session_init(&a, 0x0001, node_output, node_recv, &na);
    CHECK_EQ(micp_session_send(&a, (const uint8_t *)"x", 1, 0), MICP_ERR_STATE);
}

int main(void)
{
    MICP_RUN(test_handshake);
    MICP_RUN(test_unreliable_data);
    MICP_RUN(test_reliable_ack);
    MICP_RUN(test_busy_rejects_second_reliable);
    MICP_RUN(test_retransmit_then_ack);
    MICP_RUN(test_retransmit_exhaustion);
    MICP_RUN(test_duplicate_suppression);
    MICP_RUN(test_byte_fragmentation);
    MICP_RUN(test_crc_error_dropped);
    MICP_RUN(test_resync_after_garbage);
    MICP_RUN(test_disconnect);
    MICP_RUN(test_heartbeat_emitted);
    MICP_RUN(test_peer_timeout);
    MICP_RUN(test_send_requires_connected);
    MICP_TEST_SUMMARY();
}
