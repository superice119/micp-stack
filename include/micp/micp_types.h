/**
 * @file micp_types.h
 * @brief MICP (Multica Industrial Communication Protocol) core types,
 *        constants and error codes.
 *
 * MICP is a lightweight, transport-agnostic private industrial communication
 * protocol. This header defines the wire-level constants, message types and
 * the public error/return codes shared by all modules.
 */
#ifndef MICP_TYPES_H
#define MICP_TYPES_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Protocol version encoded in every frame. */
#define MICP_VERSION         0x01u

/** Start-Of-Frame delimiter byte. */
#define MICP_SOF             0xA5u

/** Fixed header size in bytes: SOF,VER,TYPE,FLAGS,SRC(2),DST(2),SEQ(2),LEN(2). */
#define MICP_HEADER_SIZE     12u

/** Trailer size in bytes (CRC-16). */
#define MICP_TRAILER_SIZE    2u

/** Maximum payload size (bytes) carried by a single frame. */
#define MICP_MAX_PAYLOAD     512u

/** Maximum total frame size on the wire. */
#define MICP_MAX_FRAME       (MICP_HEADER_SIZE + MICP_MAX_PAYLOAD + MICP_TRAILER_SIZE)

/** Reserved broadcast destination address. */
#define MICP_ADDR_BROADCAST  0xFFFFu

/** Frame FLAGS bit-field. */
#define MICP_FLAG_ACK_REQ    0x01u  /**< Sender requests a reliable ACK.        */
#define MICP_FLAG_BROADCAST  0x02u  /**< Frame is a broadcast (informational).  */

/** Message types carried in the TYPE field. */
typedef enum {
    MICP_MSG_HELLO      = 0x01, /**< Connection request (active open).        */
    MICP_MSG_HELLO_ACK  = 0x02, /**< Connection accept (passive open reply).  */
    MICP_MSG_HEARTBEAT  = 0x03, /**< Keep-alive / liveness probe.             */
    MICP_MSG_DATA       = 0x04, /**< Application data.                        */
    MICP_MSG_ACK        = 0x05, /**< Positive acknowledgement of a seq.       */
    MICP_MSG_NACK       = 0x06, /**< Negative acknowledgement (e.g. CRC err). */
    MICP_MSG_DISCONNECT = 0x07  /**< Orderly teardown.                        */
} micp_msg_type_t;

/** Library return / error codes. 0 == success, negative == failure. */
typedef enum {
    MICP_OK              =  0,  /**< Operation succeeded.                     */
    MICP_ERR_INVAL       = -1,  /**< Invalid argument.                       */
    MICP_ERR_NOBUFS      = -2,  /**< Output buffer too small.                */
    MICP_ERR_SHORT       = -3,  /**< Not enough bytes yet (need more input). */
    MICP_ERR_SOF         = -4,  /**< Start-of-frame byte not found.          */
    MICP_ERR_VERSION     = -5,  /**< Unsupported protocol version.           */
    MICP_ERR_LENGTH      = -6,  /**< Declared length out of range.           */
    MICP_ERR_CRC         = -7,  /**< CRC check failed.                       */
    MICP_ERR_STATE       = -8,  /**< Operation invalid in current state.     */
    MICP_ERR_TIMEOUT     = -9,  /**< Retransmission limit / peer timeout.    */
    MICP_ERR_BUSY        = -10  /**< A reliable send is already in flight.   */
} micp_err_t;

/** Connection state-machine states. */
typedef enum {
    MICP_STATE_DISCONNECTED = 0, /**< No peer association.                   */
    MICP_STATE_CONNECTING,       /**< HELLO sent, awaiting HELLO_ACK.        */
    MICP_STATE_CONNECTED,        /**< Association established.               */
    MICP_STATE_ERROR             /**< Fatal error (e.g. retransmit exhausted)*/
} micp_state_t;

/** Decoded frame structure (host representation). */
typedef struct {
    uint8_t  version;                    /**< Protocol version.              */
    uint8_t  type;                       /**< micp_msg_type_t.               */
    uint8_t  flags;                      /**< MICP_FLAG_* bit-field.         */
    uint16_t src;                        /**< Source node address.           */
    uint16_t dst;                        /**< Destination node address.      */
    uint16_t seq;                        /**< Sequence number.               */
    uint16_t length;                     /**< Payload length in bytes.       */
    uint8_t  payload[MICP_MAX_PAYLOAD];  /**< Payload bytes.                 */
} micp_frame_t;

/** Per-session counters for diagnostics / conformance checks. */
typedef struct {
    uint32_t tx_frames;      /**< Frames successfully encoded & emitted.     */
    uint32_t rx_frames;      /**< Valid frames decoded.                      */
    uint32_t rx_crc_errors;  /**< Frames dropped due to CRC mismatch.        */
    uint32_t rx_dropped;     /**< Frames dropped (bad len/version/SOF).      */
    uint32_t retransmits;    /**< Reliable retransmissions performed.        */
    uint32_t acks_tx;        /**< ACK frames emitted.                        */
    uint32_t acks_rx;        /**< ACK frames received.                       */
} micp_stats_t;

/** Human-readable name of an error code (never NULL). */
const char *micp_strerror(micp_err_t err);

/** Human-readable name of a state (never NULL). */
const char *micp_state_name(micp_state_t state);

/** Human-readable name of a message type (never NULL). */
const char *micp_msg_name(uint8_t type);

#ifdef __cplusplus
}
#endif

#endif /* MICP_TYPES_H */
