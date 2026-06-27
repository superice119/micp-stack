#include "micp/micp_crc.h" /* 引入 CRC 接口与常量定义。 */

/*
 * CRC-16/CCITT-FALSE, computed bitwise (no lookup table) to keep the footprint
 * minimal for constrained targets. poly=0x1021, init=0xFFFF, no reflection.
 * CRC-16/CCITT-FALSE: 按位计算以避免查表,适合资源受限目标;多项式 0x1021,初值 0xFFFF,不反射。
 */
uint16_t micp_crc16_update(uint16_t crc, const uint8_t *data, size_t len) /* 在既有 CRC 基础上继续累加数据。 */
{
    if (data == NULL) { /* 空数据指针表示无新字节可处理。 */
        return crc; /* 保持传入的 CRC 值不变。 */
    }
    for (size_t i = 0; i < len; ++i) { /* 逐字节遍历输入缓冲区。 */
        crc ^= (uint16_t)data[i] << 8; /* 将当前字节并入 CRC 高 8 位。 */
        for (int b = 0; b < 8; ++b) { /* 对当前字节的 8 个比特逐位推进。 */
            if (crc & 0x8000u) { /* 最高位为 1 时需要按多项式反馈。 */
                crc = (uint16_t)((crc << 1) ^ 0x1021u); /* 左移后异或 CCITT 多项式。 */
            } else { /* 最高位为 0 时只需普通左移。 */
                crc = (uint16_t)(crc << 1); /* 推进一位且不进行多项式反馈。 */
            }
        }
    }
    return crc; /* 返回更新后的 CRC 累计值。 */
}

uint16_t micp_crc16(const uint8_t *data, size_t len) /* 使用标准初值计算完整缓冲区 CRC。 */
{
    return micp_crc16_update(MICP_CRC16_INIT, data, len); /* 从协议规定初值开始复用增量计算。 */
}
