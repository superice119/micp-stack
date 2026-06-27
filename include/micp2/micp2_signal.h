/**
 * @file micp2_signal.h
 * @brief MICP 2.0 — DBC-style signal codec.
 *
 * MICP 2.0 is a **signal-matrix** private-CAN protocol following the CanPack /
 * OEM private protocol approach: the bus carries fixed CAN frames whose meaning
 * is defined by a *communication matrix* (DBC) — Nodes, Message IDs and Signals.
 * Each Signal is a bit-field inside a frame with a start bit, length, byte
 * order, sign, and a linear scale/offset mapping between the raw bits and a
 * physical value (physical = raw * factor + offset).
 *
 * This header is the low-level codec: it packs/unpacks one signal into/out of a
 * CAN frame payload. Two paths are provided:
 *   - raw integer path  (float-free, MCU-friendly): pack/unpack the raw bits.
 *   - physical path      (double): apply factor/offset/clamping.
 *
 * Bit numbering follows the DBC convention:
 *   - Intel    (little-endian): @p start_bit is the LSB of the signal; bit
 *                               positions increase from there.
 *   - Motorola (big-endian / "sawtooth"): @p start_bit is the MSB of the
 *                               signal; bit positions walk MSB-first with the
 *                               standard byte-boundary sawtooth.
 */
#ifndef MICP2_SIGNAL_H
#define MICP2_SIGNAL_H

#include <stddef.h>
#include <stdint.h>

#include "micp2/micp2_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Maximum CAN frame payload supported (CAN FD = 64 bytes). */
#define MICP2_MAX_FRAME 64u

/** Maximum signal width in bits. */
#define MICP2_MAX_SIGNAL_BITS 64u

/** Byte order of a signal within the frame. */
typedef enum {
    MICP2_BYTE_ORDER_INTEL    = 0, /**< Little-endian, start_bit = LSB.      */
    MICP2_BYTE_ORDER_MOTOROLA = 1  /**< Big-endian sawtooth, start_bit = MSB.*/
} micp2_byte_order_t;

/** Raw value signedness. */
typedef enum {
    MICP2_UNSIGNED = 0,
    MICP2_SIGNED   = 1
} micp2_sign_t;

/**
 * Signal descriptor — one bit-field inside a CAN frame.
 *
 * The linear conversion is: physical = raw * factor + offset.
 * If @p phys_max > @p phys_min the encoded physical value is clamped to
 * [phys_min, phys_max]; set them equal (e.g. both 0) to disable clamping.
 */
typedef struct {
    const char        *name;       /**< Signal name (for tooling/debug).     */
    uint16_t           start_bit;  /**< DBC start bit (see byte_order).      */
    uint16_t           bit_length; /**< Width 1..64.                         */
    micp2_byte_order_t byte_order; /**< Intel or Motorola.                   */
    micp2_sign_t       sign;       /**< Unsigned or signed (two's complement)*/
    double             factor;     /**< Scale (must be non-zero).            */
    double             offset;     /**< Offset.                              */
    double             phys_min;   /**< Physical clamp min (incl).           */
    double             phys_max;   /**< Physical clamp max (incl).           */
    const char        *unit;       /**< Engineering unit (optional, may NULL)*/
} micp2_signal_t;

/* ------------------------------------------------------------ raw path --- */

/**
 * Pack @p raw (the low @c bit_length bits) into @p frame at the signal's
 * position. Other bits in @p frame are left untouched. Float-free.
 *
 * @return MICP2_OK, MICP2_ERR_INVAL (bad args) or MICP2_ERR_LENGTH (field would
 *         exceed @p frame_len).
 */
micp2_err_t micp2_signal_pack_raw(uint8_t *frame, size_t frame_len,
                                 const micp2_signal_t *sig, uint64_t raw);

/**
 * Unpack the raw bits of @p sig from @p frame into @p raw_out (zero-extended).
 * Float-free.
 */
micp2_err_t micp2_signal_unpack_raw(const uint8_t *frame, size_t frame_len,
                                   const micp2_signal_t *sig, uint64_t *raw_out);

/** Sign-extend a @p bit_length-bit raw value to a signed 64-bit integer. */
int64_t micp2_raw_to_signed(uint64_t raw, uint16_t bit_length);

/* ------------------------------------------------------- physical path --- */

/**
 * Encode a physical value into @p frame: clamps (if enabled), applies the
 * inverse linear map raw = round((phys - offset) / factor), masks to the
 * signal width and packs it. Uses double arithmetic (host / FPU-capable MCU).
 */
micp2_err_t micp2_signal_encode(uint8_t *frame, size_t frame_len,
                               const micp2_signal_t *sig, double phys);

/**
 * Decode a physical value from @p frame: unpacks raw, applies sign extension
 * when signed, then physical = raw * factor + offset.
 */
micp2_err_t micp2_signal_decode(const uint8_t *frame, size_t frame_len,
                               const micp2_signal_t *sig, double *phys_out);

#ifdef __cplusplus
}
#endif

#endif /* MICP2_SIGNAL_H */
