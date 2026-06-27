/**
 * @file micp_session.h
 * @brief MICP connection session: state machine, reliable delivery and a
 *        byte-stream frame parser.
 *
 * A session is transport-agnostic. The application supplies an @ref micp_output_fn
 * that writes raw bytes to the wire (UART, TCP, CAN-TP, shared memory, ...) and an
 * @ref micp_recv_fn that is invoked when application DATA is delivered. Incoming
 * bytes are fed via micp_session_feed(); time is advanced via micp_session_tick().
 *
 * Reliability model:
 *   - micp_session_send(..., reliable=1) sets the ACK_REQ flag and keeps a copy of
 *     the frame. The peer replies with an ACK carrying the same seq.
 *   - If no ACK arrives within @c rto_ticks, the frame is retransmitted, up to
 *     @c max_retries times, after which the session enters MICP_STATE_ERROR.
 *   - Only one reliable frame may be in flight at a time (stop-and-wait).
 *
 * The implementation performs no dynamic allocation and is safe to use on bare
 * metal. It is not internally synchronized; call into a single session from one
 * context (or provide external locking).
 */
#ifndef MICP_SESSION_H
#define MICP_SESSION_H

#include "micp/micp_types.h"
#include "micp/micp_frame.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Output callback: write @p len bytes from @p data to the transport.
 * @return MICP_OK on success, or a negative micp_err_t on failure.
 */
typedef micp_err_t (*micp_output_fn)(void *user, const uint8_t *data, size_t len);

/**
 * Delivery callback: a DATA payload from @p src has been received in order.
 */
typedef void (*micp_recv_fn)(void *user, uint16_t src,
                             const uint8_t *payload, size_t len);

/**
 * Event callback (optional): notifies the application of state transitions and
 * notable events (connect, disconnect, error). May be NULL.
 */
typedef void (*micp_event_fn)(void *user, micp_state_t old_state,
                              micp_state_t new_state);

/** Default reliability timing (in user-defined tick units). */
#define MICP_DEFAULT_RTO_TICKS        10u
#define MICP_DEFAULT_MAX_RETRIES      3u
#define MICP_DEFAULT_HEARTBEAT_TICKS  30u
#define MICP_DEFAULT_PEER_TIMEOUT     100u

/** Session object. Treat fields as read-only from application code. */
typedef struct {
    uint16_t      addr;        /**< This node's address.                     */
    uint16_t      peer_addr;   /**< Associated peer (valid once CONNECTED).  */
    micp_state_t  state;       /**< Current state-machine state.             */

    uint16_t      tx_seq;      /**< Next sequence number to assign.          */
    int           has_last_rx; /**< Whether last_rx_seq is valid.            */
    uint16_t      last_rx_seq; /**< Last in-order DATA seq delivered (dedup).*/

    /* Stop-and-wait reliable TX state. */
    int           tx_pending;          /**< 1 if a reliable frame is unacked.*/
    uint16_t      pending_seq;         /**< Seq of the in-flight frame.      */
    uint8_t       pending_buf[MICP_MAX_FRAME]; /**< Cached encoded frame.    */
    size_t        pending_len;         /**< Length of cached frame.          */
    uint32_t      retries;             /**< Retransmissions used so far.     */
    uint32_t      rto_timer;           /**< Ticks until next retransmit.     */

    /* Timing configuration. */
    uint32_t      rto_ticks;           /**< Retransmit timeout.              */
    uint32_t      max_retries;         /**< Max retransmissions.             */
    uint32_t      heartbeat_ticks;     /**< Heartbeat emission interval.     */
    uint32_t      peer_timeout_ticks;  /**< Liveness timeout.                */
    uint32_t      heartbeat_timer;     /**< Counts up to heartbeat_ticks.    */
    uint32_t      peer_silence;        /**< Ticks since last frame from peer.*/

    /* Byte-stream reassembly. */
    uint8_t       rx_buf[MICP_MAX_FRAME];
    size_t        rx_len;

    /* Callbacks. */
    micp_output_fn output;
    micp_recv_fn   on_recv;
    micp_event_fn  on_event;
    void          *user;

    micp_stats_t  stats;
} micp_session_t;

/**
 * Initialize a session. All timing parameters default to the MICP_DEFAULT_*
 * values and can be overridden afterwards by writing the struct fields.
 *
 * @param s       session to initialize
 * @param addr    this node's address (must not be MICP_ADDR_BROADCAST)
 * @param output  transport write callback (required)
 * @param on_recv DATA delivery callback (may be NULL)
 * @param user    opaque pointer passed to all callbacks
 * @return MICP_OK or MICP_ERR_INVAL.
 */
micp_err_t micp_session_init(micp_session_t *s, uint16_t addr,
                             micp_output_fn output, micp_recv_fn on_recv,
                             void *user);

/** Register an optional state-transition/event callback. */
void micp_session_set_event_cb(micp_session_t *s, micp_event_fn cb);

/**
 * Actively open a connection to @p peer_addr: sends HELLO and moves to
 * MICP_STATE_CONNECTING. Valid only from MICP_STATE_DISCONNECTED/ERROR.
 */
micp_err_t micp_session_connect(micp_session_t *s, uint16_t peer_addr);

/**
 * Send an application payload to the connected peer.
 *
 * @param s        session (must be CONNECTED)
 * @param payload  data (may be NULL iff len == 0)
 * @param len      payload length (<= MICP_MAX_PAYLOAD)
 * @param reliable non-zero to request ACK + retransmission (stop-and-wait)
 * @return MICP_OK, MICP_ERR_STATE, MICP_ERR_LENGTH, MICP_ERR_BUSY (reliable send
 *         already in flight) or a transport error.
 */
micp_err_t micp_session_send(micp_session_t *s, const uint8_t *payload,
                             size_t len, int reliable);

/** Orderly teardown: sends DISCONNECT and returns to DISCONNECTED. */
micp_err_t micp_session_disconnect(micp_session_t *s);

/**
 * Feed raw bytes received from the transport. Handles SOF resynchronization,
 * partial frames spanning multiple calls, CRC validation and protocol logic.
 * Multiple frames in @p data are all processed.
 *
 * @return MICP_OK (bytes consumed/buffered). Per-frame errors are counted in
 *         stats and do not fail the call.
 */
micp_err_t micp_session_feed(micp_session_t *s, const uint8_t *data, size_t len);

/**
 * Advance time by @p dt ticks. Drives retransmission, heartbeat emission and
 * peer-liveness timeout. Call periodically from the application loop/timer.
 */
micp_err_t micp_session_tick(micp_session_t *s, uint32_t dt);

/** Returns 1 if a reliable send is currently awaiting an ACK. */
static inline int micp_session_tx_busy(const micp_session_t *s)
{
    return s != NULL && s->tx_pending;
}

#ifdef __cplusplus
}
#endif

#endif /* MICP_SESSION_H */
