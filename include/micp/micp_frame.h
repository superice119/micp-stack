/**
 * @file micp_frame.h
 * @brief MICP frame encoder / decoder (serialization & deserialization).
 *
 * Wire layout (all multi-byte integers are big-endian / network order):
 *
 *   Offset  Size  Field
 *   0       1     SOF    (0xA5)
 *   1       1     VER
 *   2       1     TYPE
 *   3       1     FLAGS
 *   4       2     SRC
 *   6       2     DST
 *   8       2     SEQ
 *   10      2     LEN     (payload length, 0..MICP_MAX_PAYLOAD)
 *   12      LEN   PAYLOAD
 *   12+LEN  2     CRC16   (CRC-16/CCITT-FALSE over bytes [1 .. 12+LEN-1],
 *                          i.e. every byte after SOF up to and incl. payload)
 */
#ifndef MICP_FRAME_H
#define MICP_FRAME_H

#include "micp/micp_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Encode a frame into @p out.
 *
 * @param f       frame to encode (frame->length must be <= MICP_MAX_PAYLOAD)
 * @param out     destination buffer
 * @param out_cap capacity of @p out in bytes
 * @param out_len [out] number of bytes written on success
 * @return MICP_OK on success, MICP_ERR_INVAL / MICP_ERR_LENGTH / MICP_ERR_NOBUFS.
 *
 * The VER field of @p f is ignored on input and forced to MICP_VERSION.
 */
micp_err_t micp_frame_encode(const micp_frame_t *f,
                             uint8_t *out, size_t out_cap, size_t *out_len);

/**
 * Decode a single frame from @p in.
 *
 * The decoder expects @p in to start exactly at a SOF byte. It validates the
 * version, length bound and CRC.
 *
 * @param in       input buffer
 * @param in_len   number of valid bytes in @p in
 * @param f        [out] decoded frame
 * @param consumed [out] number of bytes consumed for the decoded frame
 * @return MICP_OK on success; one of:
 *         - MICP_ERR_SOF     : first byte is not SOF
 *         - MICP_ERR_SHORT   : need more bytes (header or full frame incomplete)
 *         - MICP_ERR_VERSION : unsupported version
 *         - MICP_ERR_LENGTH  : declared length exceeds MICP_MAX_PAYLOAD
 *         - MICP_ERR_CRC     : CRC mismatch
 */
micp_err_t micp_frame_decode(const uint8_t *in, size_t in_len,
                             micp_frame_t *f, size_t *consumed);

/** Total on-wire size of a frame carrying @p payload_len bytes. */
static inline size_t micp_frame_size(uint16_t payload_len)
{
    return (size_t)MICP_HEADER_SIZE + payload_len + MICP_TRAILER_SIZE;
}

#ifdef __cplusplus
}
#endif

#endif /* MICP_FRAME_H */
