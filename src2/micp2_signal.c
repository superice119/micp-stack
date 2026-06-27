/**
 * @file micp2_signal.c
 * @brief MICP 2.0 DBC-style signal codec implementation.
 *
 * Bit-packing follows the standard DBC conventions used by Vector tools and
 * CANopen/OEM private matrices. The implementation is exhaustively unit-tested
 * (tests/test_micp2_signal.c) for Intel/Motorola order, signedness, scale and
 * offset, clamping and byte-boundary crossings.
 */
#include "micp2/micp2_signal.h"

/* ---- single-bit helpers on a little-endian-addressed byte buffer -------- */

/* DBC bit index: byte = idx/8, bit-in-byte = idx%8 (bit 0 = LSB of the byte). */
static void set_bit(uint8_t *buf, uint16_t idx, int val)
{
    if (val) {
        buf[idx >> 3] |= (uint8_t)(1u << (idx & 7u));
    } else {
        buf[idx >> 3] &= (uint8_t)~(1u << (idx & 7u));
    }
}

static int get_bit(const uint8_t *buf, uint16_t idx)
{
    return (buf[idx >> 3] >> (idx & 7u)) & 1u;
}

/* Highest DBC bit index touched by the signal, used for bounds checking. */
static uint16_t signal_max_bit_index(const micp2_signal_t *sig)
{
    if (sig->byte_order == MICP2_BYTE_ORDER_INTEL) {
        /* bits run start_bit .. start_bit + len - 1 (increasing). */
        return (uint16_t)(sig->start_bit + sig->bit_length - 1u);
    } else {
        /* Motorola sawtooth: walk and track the max touched index. */
        int bit = (int)sig->start_bit;
        uint16_t maxidx = (uint16_t)bit;
        for (uint16_t i = 0; i < sig->bit_length; i++) {
            if ((uint16_t)bit > maxidx) {
                maxidx = (uint16_t)bit;
            }
            if ((bit & 7) == 0) {
                bit += 15; /* jump to MSB of the next byte */
            } else {
                bit -= 1;
            }
        }
        return maxidx;
    }
}

static micp_err_t validate(const uint8_t *frame, size_t frame_len,
                           const micp2_signal_t *sig)
{
    if (frame == NULL || sig == NULL) {
        return MICP_ERR_INVAL;
    }
    if (sig->bit_length == 0 || sig->bit_length > MICP2_MAX_SIGNAL_BITS) {
        return MICP_ERR_INVAL;
    }
    if (frame_len > MICP2_MAX_FRAME) {
        return MICP_ERR_INVAL;
    }
    if (signal_max_bit_index(sig) >= (uint16_t)(frame_len * 8u)) {
        return MICP_ERR_LENGTH;
    }
    return MICP_OK;
}

/* ----------------------------------------------------------- raw path ---- */

micp_err_t micp2_signal_pack_raw(uint8_t *frame, size_t frame_len,
                                 const micp2_signal_t *sig, uint64_t raw)
{
    micp_err_t rc = validate(frame, frame_len, sig);
    if (rc != MICP_OK) {
        return rc;
    }

    if (sig->byte_order == MICP2_BYTE_ORDER_INTEL) {
        for (uint16_t i = 0; i < sig->bit_length; i++) {
            uint16_t pos = (uint16_t)(sig->start_bit + i);
            set_bit(frame, pos, (int)((raw >> i) & 1u));
        }
    } else {
        int bit = (int)sig->start_bit;
        for (uint16_t i = 0; i < sig->bit_length; i++) {
            /* MSB first: output bit i carries raw bit (len-1-i). */
            uint16_t src = (uint16_t)(sig->bit_length - 1u - i);
            set_bit(frame, (uint16_t)bit, (int)((raw >> src) & 1u));
            if ((bit & 7) == 0) {
                bit += 15;
            } else {
                bit -= 1;
            }
        }
    }
    return MICP_OK;
}

micp_err_t micp2_signal_unpack_raw(const uint8_t *frame, size_t frame_len,
                                   const micp2_signal_t *sig, uint64_t *raw_out)
{
    micp_err_t rc = validate(frame, frame_len, sig);
    if (rc != MICP_OK) {
        return rc;
    }
    if (raw_out == NULL) {
        return MICP_ERR_INVAL;
    }

    uint64_t raw = 0;
    if (sig->byte_order == MICP2_BYTE_ORDER_INTEL) {
        for (uint16_t i = 0; i < sig->bit_length; i++) {
            uint16_t pos = (uint16_t)(sig->start_bit + i);
            raw |= (uint64_t)get_bit(frame, pos) << i;
        }
    } else {
        int bit = (int)sig->start_bit;
        for (uint16_t i = 0; i < sig->bit_length; i++) {
            uint16_t dst = (uint16_t)(sig->bit_length - 1u - i);
            raw |= (uint64_t)get_bit(frame, (uint16_t)bit) << dst;
            if ((bit & 7) == 0) {
                bit += 15;
            } else {
                bit -= 1;
            }
        }
    }
    *raw_out = raw;
    return MICP_OK;
}

int64_t micp2_raw_to_signed(uint64_t raw, uint16_t bit_length)
{
    if (bit_length == 0 || bit_length >= 64) {
        return (int64_t)raw;
    }
    uint64_t sign_bit = (uint64_t)1 << (bit_length - 1u);
    if (raw & sign_bit) {
        /* extend the sign: set all bits above bit_length-1. */
        uint64_t ext = ~(((uint64_t)1 << bit_length) - 1u);
        return (int64_t)(raw | ext);
    }
    return (int64_t)raw;
}

/* ------------------------------------------------------ physical path ---- */

/* Round-half-away-from-zero without depending on libm (no -lm needed). */
static int64_t round_to_i64(double v)
{
    return (v >= 0.0) ? (int64_t)(v + 0.5) : (int64_t)(v - 0.5);
}

micp_err_t micp2_signal_encode(uint8_t *frame, size_t frame_len,
                               const micp2_signal_t *sig, double phys)
{
    if (sig == NULL || sig->factor == 0.0) {
        return MICP_ERR_INVAL;
    }

    /* optional clamp */
    if (sig->phys_max > sig->phys_min) {
        if (phys < sig->phys_min) {
            phys = sig->phys_min;
        } else if (phys > sig->phys_max) {
            phys = sig->phys_max;
        }
    }

    int64_t r = round_to_i64((phys - sig->offset) / sig->factor);

    /* clamp raw to the representable range of the field so that out-of-range
       inputs saturate instead of wrapping. */
    if (sig->bit_length < 64) {
        if (sig->sign == MICP2_SIGNED) {
            int64_t hi = (int64_t)(((uint64_t)1 << (sig->bit_length - 1u)) - 1u);
            int64_t lo = -hi - 1;
            if (r > hi) r = hi;
            if (r < lo) r = lo;
        } else {
            if (r < 0) r = 0;
            uint64_t umax = (((uint64_t)1 << sig->bit_length) - 1u);
            if ((uint64_t)r > umax) r = (int64_t)umax;
        }
    }

    uint64_t mask = (sig->bit_length >= 64)
                        ? ~(uint64_t)0
                        : (((uint64_t)1 << sig->bit_length) - 1u);
    uint64_t raw = (uint64_t)r & mask;

    return micp2_signal_pack_raw(frame, frame_len, sig, raw);
}

micp_err_t micp2_signal_decode(const uint8_t *frame, size_t frame_len,
                               const micp2_signal_t *sig, double *phys_out)
{
    if (phys_out == NULL || sig == NULL) {
        return MICP_ERR_INVAL;
    }

    uint64_t raw = 0;
    micp_err_t rc = micp2_signal_unpack_raw(frame, frame_len, sig, &raw);
    if (rc != MICP_OK) {
        return rc;
    }

    double value;
    if (sig->sign == MICP2_SIGNED) {
        value = (double)micp2_raw_to_signed(raw, sig->bit_length);
    } else {
        value = (double)raw;
    }
    *phys_out = value * sig->factor + sig->offset;
    return MICP_OK;
}
