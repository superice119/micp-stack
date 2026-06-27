/**
 * @file micp2_matrix.h
 * @brief MICP 2.0 — communication matrix (Nodes / Messages / Signals).
 *
 * The matrix is the MICP 2.0 equivalent of a DBC database and of CanPack's
 * register/COB-ID tables. It describes, statically and in const memory:
 *   - Nodes: who is on the bus (ADAS / BCM / BMS / ...),
 *   - Messages: each CAN frame's ID, length, nominal cycle and sender,
 *   - Signals: the bit-fields packed inside each message (see micp2_signal.h).
 *
 * With the matrix you can encode a set of physical signal values into a CAN
 * frame, decode a received frame back into physical values, look a message up
 * by CAN ID, and dispatch incoming frames to a handler — all without any heap,
 * OS or transport assumptions. Wiring the bytes to a real CAN controller is the
 * application's job (one send hook, one rx feed), exactly as in MICP 1.x.
 */
#ifndef MICP2_MATRIX_H
#define MICP2_MATRIX_H

#include <stddef.h>
#include <stdint.h>

#include "micp2/micp2_signal.h"

#ifdef __cplusplus
extern "C" {
#endif

/** A communication node on the bus. */
typedef struct {
    const char *name;    /**< Node name (ADAS, BCM, BMS, ...).               */
    uint8_t     node_id; /**< Logical node id.                              */
} micp2_node_t;

/** A CAN message: an identified frame carrying a fixed set of signals. */
typedef struct {
    const char           *name;          /**< Message name.                  */
    uint32_t              can_id;         /**< 11- or 29-bit CAN identifier.  */
    uint8_t               is_extended;    /**< 0 = 11-bit, 1 = 29-bit ID.     */
    uint8_t               dlc;            /**< Payload length in bytes.       */
    uint16_t              cycle_ms;       /**< Nominal period; 0 = event.     */
    uint8_t               sender_node_id; /**< Producing node.                */
    const micp2_signal_t *signals;        /**< Signal table.                  */
    size_t                signal_count;   /**< Number of signals.             */
} micp2_message_t;

/** The whole communication matrix. */
typedef struct {
    const char            *name;          /**< Matrix / DBC name.             */
    const micp2_node_t    *nodes;
    size_t                 node_count;
    const micp2_message_t *messages;
    size_t                 message_count;
} micp2_matrix_t;

/** Find a message by CAN id + frame format, or NULL if absent. */
const micp2_message_t *micp2_matrix_find_by_id(const micp2_matrix_t *m,
                                               uint32_t can_id,
                                               uint8_t is_extended);

/** Find a signal by name within a message, or NULL. */
const micp2_signal_t *micp2_message_find_signal(const micp2_message_t *msg,
                                                const char *name);

/**
 * Encode a full message: @p phys_values[i] is the physical value for
 * @p msg->signals[i]. @p frame is first zeroed up to @p msg->dlc, then every
 * signal is packed. @p frame_len must be >= msg->dlc.
 */
micp_err_t micp2_message_encode(const micp2_message_t *msg,
                                const double *phys_values,
                                uint8_t *frame, size_t frame_len);

/**
 * Decode a full message into @p phys_values_out (aligned to msg->signals[]).
 */
micp_err_t micp2_message_decode(const micp2_message_t *msg,
                                const uint8_t *frame, size_t frame_len,
                                double *phys_values_out);

/** Per-message receive handler used by micp2_matrix_dispatch(). */
typedef void (*micp2_rx_handler_t)(void *user, const micp2_message_t *msg,
                                   const double *phys_values);

/**
 * Look @p can_id up in the matrix; if found, decode @p frame into a stack
 * buffer of physical values and invoke @p handler. Up to
 * MICP2_DISPATCH_MAX_SIGNALS signals per message are supported.
 *
 * @return MICP_OK on dispatch, MICP_ERR_INVAL if the id is unknown, or a decode
 *         error.
 */
#define MICP2_DISPATCH_MAX_SIGNALS 64u
micp_err_t micp2_matrix_dispatch(const micp2_matrix_t *m,
                                 uint32_t can_id, uint8_t is_extended,
                                 const uint8_t *frame, size_t frame_len,
                                 micp2_rx_handler_t handler, void *user);

#ifdef __cplusplus
}
#endif

#endif /* MICP2_MATRIX_H */
