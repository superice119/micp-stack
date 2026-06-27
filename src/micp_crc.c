#include "micp/micp_crc.h"

/*
 * CRC-16/CCITT-FALSE, computed bitwise (no lookup table) to keep the footprint
 * minimal for constrained targets. poly=0x1021, init=0xFFFF, no reflection.
 */
uint16_t micp_crc16_update(uint16_t crc, const uint8_t *data, size_t len)
{
    if (data == NULL) {
        return crc;
    }
    for (size_t i = 0; i < len; ++i) {
        crc ^= (uint16_t)data[i] << 8;
        for (int b = 0; b < 8; ++b) {
            if (crc & 0x8000u) {
                crc = (uint16_t)((crc << 1) ^ 0x1021u);
            } else {
                crc = (uint16_t)(crc << 1);
            }
        }
    }
    return crc;
}

uint16_t micp_crc16(const uint8_t *data, size_t len)
{
    return micp_crc16_update(MICP_CRC16_INIT, data, len);
}
