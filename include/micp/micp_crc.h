/**
 * @file micp_crc.h
 * @brief CRC-16/CCITT-FALSE used for MICP frame integrity.
 *
 * Parameters: poly=0x1021, init=0xFFFF, refin=false, refout=false, xorout=0x0000.
 * Check value of the ASCII string "123456789" is 0x29B1.
 */
#ifndef MICP_CRC_H
#define MICP_CRC_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Initial CRC seed; pass to micp_crc16_update for an incremental computation. */
#define MICP_CRC16_INIT 0xFFFFu

/**
 * Compute CRC-16/CCITT-FALSE over a buffer in one shot.
 * @param data buffer (may be NULL only if len == 0)
 * @param len  number of bytes
 * @return the 16-bit CRC.
 */
uint16_t micp_crc16(const uint8_t *data, size_t len);

/**
 * Incrementally update a running CRC. Start with @ref MICP_CRC16_INIT.
 * @param crc  running CRC value
 * @param data buffer (may be NULL only if len == 0)
 * @param len  number of bytes
 * @return the updated CRC.
 */
uint16_t micp_crc16_update(uint16_t crc, const uint8_t *data, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* MICP_CRC_H */
