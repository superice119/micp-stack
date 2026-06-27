#include "micp/micp_session.h"
#include "micp/micp_crc.h"

#include <string.h>

/* ------------------------------------------------------------------ helpers */

static void set_state(micp_session_t *s, micp_state_t ns)
{
    if (s->state == ns) {
        return;
    }
    micp_state_t old = s->state;
    s->state = ns;
    if (s->on_event) {
        s->on_event(s->user, old, ns);
    }
}

/* Encode and emit a frame through the transport. */
static micp_err_t emit(micp_session_t *s, uint8_t type, uint8_t flags,
                       uint16_t dst, uint16_t seq,
                       const uint8_t *payload, size_t len)
{
    micp_frame_t f;
    memset(&f, 0, sizeof(f));
    f.version = MICP_VERSION;
    f.type    = type;
    f.flags   = flags;
    f.src     = s->addr;
    f.dst     = dst;
    f.seq     = seq;
    f.length  = (uint16_t)len;
    if (len > 0 && payload != NULL) {
        memcpy(f.payload, payload, len);
    }

    uint8_t buf[MICP_MAX_FRAME];
    size_t  n = 0;
    micp_err_t e = micp_frame_encode(&f, buf, sizeof(buf), &n);
    if (e != MICP_OK) {
        return e;
    }
    e = s->output(s->user, buf, n);
    if (e == MICP_OK) {
        s->stats.tx_frames++;
        if (type == MICP_MSG_ACK) {
            s->stats.acks_tx++;
        }
    }
    return e;
}

static micp_err_t send_ack(micp_session_t *s, uint16_t dst, uint16_t seq)
{
    return emit(s, MICP_MSG_ACK, 0, dst, seq, NULL, 0);
}

/* ------------------------------------------------------------------- public */

micp_err_t micp_session_init(micp_session_t *s, uint16_t addr,
                             micp_output_fn output, micp_recv_fn on_recv,
                             void *user)
{
    if (s == NULL || output == NULL || addr == MICP_ADDR_BROADCAST) {
        return MICP_ERR_INVAL;
    }
    memset(s, 0, sizeof(*s));
    s->addr               = addr;
    s->state              = MICP_STATE_DISCONNECTED;
    s->output             = output;
    s->on_recv            = on_recv;
    s->user               = user;
    s->rto_ticks          = MICP_DEFAULT_RTO_TICKS;
    s->max_retries        = MICP_DEFAULT_MAX_RETRIES;
    s->heartbeat_ticks    = MICP_DEFAULT_HEARTBEAT_TICKS;
    s->peer_timeout_ticks = MICP_DEFAULT_PEER_TIMEOUT;
    return MICP_OK;
}

void micp_session_set_event_cb(micp_session_t *s, micp_event_fn cb)
{
    if (s != NULL) {
        s->on_event = cb;
    }
}

micp_err_t micp_session_connect(micp_session_t *s, uint16_t peer_addr)
{
    if (s == NULL) {
        return MICP_ERR_INVAL;
    }
    if (s->state != MICP_STATE_DISCONNECTED && s->state != MICP_STATE_ERROR) {
        return MICP_ERR_STATE;
    }
    s->peer_addr      = peer_addr;
    s->tx_seq         = 0;
    s->has_last_rx    = 0;
    s->tx_pending     = 0;
    s->heartbeat_timer = 0;
    s->peer_silence   = 0;
    set_state(s, MICP_STATE_CONNECTING);
    return emit(s, MICP_MSG_HELLO, 0, peer_addr, s->tx_seq++, NULL, 0);
}

micp_err_t micp_session_send(micp_session_t *s, const uint8_t *payload,
                             size_t len, int reliable)
{
    if (s == NULL || (payload == NULL && len > 0)) {
        return MICP_ERR_INVAL;
    }
    if (len > MICP_MAX_PAYLOAD) {
        return MICP_ERR_LENGTH;
    }
    if (s->state != MICP_STATE_CONNECTED) {
        return MICP_ERR_STATE;
    }
    if (reliable && s->tx_pending) {
        return MICP_ERR_BUSY;
    }

    uint16_t seq   = s->tx_seq++;
    uint8_t  flags = reliable ? MICP_FLAG_ACK_REQ : 0;

    if (reliable) {
        /* Build the frame into the retransmit cache and emit from there. */
        micp_frame_t f;
        memset(&f, 0, sizeof(f));
        f.version = MICP_VERSION;
        f.type    = MICP_MSG_DATA;
        f.flags   = flags;
        f.src     = s->addr;
        f.dst     = s->peer_addr;
        f.seq     = seq;
        f.length  = (uint16_t)len;
        if (len > 0) {
            memcpy(f.payload, payload, len);
        }
        micp_err_t e = micp_frame_encode(&f, s->pending_buf,
                                         sizeof(s->pending_buf), &s->pending_len);
        if (e != MICP_OK) {
            return e;
        }
        s->tx_pending  = 1;
        s->pending_seq = seq;
        s->retries     = 0;
        s->rto_timer   = s->rto_ticks;
        e = s->output(s->user, s->pending_buf, s->pending_len);
        if (e == MICP_OK) {
            s->stats.tx_frames++;
        }
        return e;
    }

    return emit(s, MICP_MSG_DATA, flags, s->peer_addr, seq, payload, len);
}

micp_err_t micp_session_disconnect(micp_session_t *s)
{
    if (s == NULL) {
        return MICP_ERR_INVAL;
    }
    micp_err_t e = MICP_OK;
    if (s->state == MICP_STATE_CONNECTED || s->state == MICP_STATE_CONNECTING) {
        e = emit(s, MICP_MSG_DISCONNECT, 0, s->peer_addr, s->tx_seq++, NULL, 0);
    }
    s->tx_pending = 0;
    set_state(s, MICP_STATE_DISCONNECTED);
    return e;
}

/* --------------------------------------------------------- frame processing */

static void handle_frame(micp_session_t *s, const micp_frame_t *f)
{
    /* Ignore frames not addressed to us unless broadcast. */
    if (f->dst != s->addr && f->dst != MICP_ADDR_BROADCAST) {
        s->stats.rx_dropped++;
        return;
    }

    s->stats.rx_frames++;
    s->peer_silence = 0;

    switch (f->type) {
    case MICP_MSG_HELLO:
        /* Passive open: accept and reply with HELLO_ACK. */
        s->peer_addr   = f->src;
        s->has_last_rx = 0;
        s->tx_pending  = 0;
        s->heartbeat_timer = 0;
        set_state(s, MICP_STATE_CONNECTED);
        emit(s, MICP_MSG_HELLO_ACK, 0, f->src, s->tx_seq++, NULL, 0);
        break;

    case MICP_MSG_HELLO_ACK:
        if (s->state == MICP_STATE_CONNECTING && f->src == s->peer_addr) {
            set_state(s, MICP_STATE_CONNECTED);
        }
        break;

    case MICP_MSG_HEARTBEAT:
        /* Liveness already refreshed above; nothing else to do. */
        break;

    case MICP_MSG_DATA:
        if (s->state != MICP_STATE_CONNECTED) {
            s->stats.rx_dropped++;
            break;
        }
        /* Acknowledge if requested (before dedup so retransmits get ACKed). */
        if (f->flags & MICP_FLAG_ACK_REQ) {
            send_ack(s, f->src, f->seq);
        }
        /* Duplicate suppression for reliable stream. */
        if (s->has_last_rx && f->seq == s->last_rx_seq) {
            break; /* duplicate retransmission already delivered */
        }
        s->has_last_rx = 1;
        s->last_rx_seq = f->seq;
        if (s->on_recv) {
            s->on_recv(s->user, f->src, f->payload, f->length);
        }
        break;

    case MICP_MSG_ACK:
        if (s->tx_pending && f->seq == s->pending_seq) {
            s->tx_pending = 0;
            s->stats.acks_rx++;
        }
        break;

    case MICP_MSG_NACK:
        /* Trigger an immediate retransmit on the next tick. */
        if (s->tx_pending && f->seq == s->pending_seq) {
            s->rto_timer = 0;
        }
        break;

    case MICP_MSG_DISCONNECT:
        s->tx_pending = 0;
        set_state(s, MICP_STATE_DISCONNECTED);
        break;

    default:
        s->stats.rx_dropped++;
        break;
    }
}

micp_err_t micp_session_feed(micp_session_t *s, const uint8_t *data, size_t len)
{
    if (s == NULL || (data == NULL && len > 0)) {
        return MICP_ERR_INVAL;
    }

    for (size_t i = 0; i < len; ++i) {
        /* Accumulate into rx_buf, guarding against overflow. */
        if (s->rx_len >= sizeof(s->rx_buf)) {
            /* Should not happen given the parser drains; resync defensively. */
            s->rx_len = 0;
        }
        s->rx_buf[s->rx_len++] = data[i];

        /* Try to extract as many complete frames as possible. */
        for (;;) {
            if (s->rx_len == 0) {
                break;
            }
            /* Resync to SOF: drop leading non-SOF bytes. */
            if (s->rx_buf[0] != MICP_SOF) {
                size_t k = 1;
                while (k < s->rx_len && s->rx_buf[k] != MICP_SOF) {
                    k++;
                }
                memmove(s->rx_buf, s->rx_buf + k, s->rx_len - k);
                s->rx_len -= k;
                if (s->rx_len == 0) {
                    break;
                }
            }

            micp_frame_t f;
            size_t consumed = 0;
            micp_err_t e = micp_frame_decode(s->rx_buf, s->rx_len, &f, &consumed);
            if (e == MICP_OK) {
                handle_frame(s, &f);
                memmove(s->rx_buf, s->rx_buf + consumed, s->rx_len - consumed);
                s->rx_len -= consumed;
                continue; /* maybe another frame is buffered */
            } else if (e == MICP_ERR_SHORT) {
                break; /* need more bytes */
            } else if (e == MICP_ERR_CRC) {
                s->stats.rx_crc_errors++;
                /* Drop this SOF byte and resync past it. */
                memmove(s->rx_buf, s->rx_buf + 1, s->rx_len - 1);
                s->rx_len -= 1;
                continue;
            } else {
                /* VERSION / LENGTH / SOF: drop one byte and resync. */
                s->stats.rx_dropped++;
                memmove(s->rx_buf, s->rx_buf + 1, s->rx_len - 1);
                s->rx_len -= 1;
                continue;
            }
        }
    }
    return MICP_OK;
}

micp_err_t micp_session_tick(micp_session_t *s, uint32_t dt)
{
    if (s == NULL) {
        return MICP_ERR_INVAL;
    }

    /* Retransmission timer (stop-and-wait). */
    if (s->tx_pending) {
        if (s->rto_timer <= dt) {
            if (s->retries >= s->max_retries) {
                s->tx_pending = 0;
                set_state(s, MICP_STATE_ERROR);
                return MICP_ERR_TIMEOUT;
            }
            s->retries++;
            s->stats.retransmits++;
            s->rto_timer = s->rto_ticks;
            (void)s->output(s->user, s->pending_buf, s->pending_len);
            s->stats.tx_frames++;
        } else {
            s->rto_timer -= dt;
        }
    }

    /* Heartbeat emission and peer liveness — only while associated. */
    if (s->state == MICP_STATE_CONNECTED) {
        s->heartbeat_timer += dt;
        if (s->heartbeat_timer >= s->heartbeat_ticks) {
            s->heartbeat_timer = 0;
            emit(s, MICP_MSG_HEARTBEAT, 0, s->peer_addr, s->tx_seq++, NULL, 0);
        }
        s->peer_silence += dt;
        if (s->peer_silence >= s->peer_timeout_ticks) {
            s->tx_pending = 0;
            set_state(s, MICP_STATE_ERROR);
            return MICP_ERR_TIMEOUT;
        }
    }
    return MICP_OK;
}
