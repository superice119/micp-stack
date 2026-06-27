#include "micp/micp_frame.h"
#include "micp/micp_crc.h"

#include <string.h>

/* Big-endian helpers. */
static void put_u16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)(v & 0xFFu);
}

static uint16_t get_u16(const uint8_t *p)
{
    return (uint16_t)(((uint16_t)p[0] << 8) | (uint16_t)p[1]);
}

micp_err_t micp_frame_encode(const micp_frame_t *f,
                             uint8_t *out, size_t out_cap, size_t *out_len)
{
    if (f == NULL || out == NULL || out_len == NULL) {
        return MICP_ERR_INVAL;
    }
    if (f->length > MICP_MAX_PAYLOAD) {
        return MICP_ERR_LENGTH;
    }

    const size_t total = micp_frame_size(f->length);
    if (out_cap < total) {
        return MICP_ERR_NOBUFS;
    }

    out[0] = MICP_SOF;
    out[1] = MICP_VERSION;
    out[2] = f->type;
    out[3] = f->flags;
    put_u16(&out[4], f->src);
    put_u16(&out[6], f->dst);
    put_u16(&out[8], f->seq);
    put_u16(&out[10], f->length);

    if (f->length > 0) {
        memcpy(&out[MICP_HEADER_SIZE], f->payload, f->length);
    }

    /* CRC covers everything after SOF, up to and including payload. */
    const size_t crc_span = (size_t)(MICP_HEADER_SIZE - 1) + f->length; /* bytes [1 .. ] */
    const uint16_t crc = micp_crc16(&out[1], crc_span);
    put_u16(&out[MICP_HEADER_SIZE + f->length], crc);

    *out_len = total;
    return MICP_OK;
}

micp_err_t micp_frame_decode(const uint8_t *in, size_t in_len,
                             micp_frame_t *f, size_t *consumed)
{
    if (in == NULL || f == NULL || consumed == NULL) {
        return MICP_ERR_INVAL;
    }
    if (in_len < 1) {
        return MICP_ERR_SHORT;
    }
    if (in[0] != MICP_SOF) {
        return MICP_ERR_SOF;
    }
    if (in_len < MICP_HEADER_SIZE) {
        return MICP_ERR_SHORT;
    }

    const uint8_t  version = in[1];
    const uint16_t length  = get_u16(&in[10]);

    if (version != MICP_VERSION) {
        return MICP_ERR_VERSION;
    }
    if (length > MICP_MAX_PAYLOAD) {
        return MICP_ERR_LENGTH;
    }

    const size_t total = micp_frame_size(length);
    if (in_len < total) {
        return MICP_ERR_SHORT;
    }

    const size_t crc_span = (size_t)(MICP_HEADER_SIZE - 1) + length;
    const uint16_t calc = micp_crc16(&in[1], crc_span);
    const uint16_t got  = get_u16(&in[MICP_HEADER_SIZE + length]);
    if (calc != got) {
        return MICP_ERR_CRC;
    }

    f->version = version;
    f->type    = in[2];
    f->flags   = in[3];
    f->src     = get_u16(&in[4]);
    f->dst     = get_u16(&in[6]);
    f->seq     = get_u16(&in[8]);
    f->length  = length;
    if (length > 0) {
        memcpy(f->payload, &in[MICP_HEADER_SIZE], length);
    }

    *consumed = total;
    return MICP_OK;
}
