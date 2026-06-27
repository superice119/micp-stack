#include "micp/micp_frame.h" /* 引入帧结构、常量和编解码接口。 */
#include "micp/micp_crc.h" /* 引入帧校验所需的 CRC16 计算接口。 */

#include <string.h> /* 引入 memcpy 用于载荷拷贝。 */

/* Big-endian helpers. 大端序读写辅助函数。 */
static void put_u16(uint8_t *p, uint16_t v) /* 将 16 位整数按网络字节序写入缓冲区。 */
{
    p[0] = (uint8_t)(v >> 8); /* 写入高 8 位。 */
    p[1] = (uint8_t)(v & 0xFFu); /* 写入低 8 位。 */
}

static uint16_t get_u16(const uint8_t *p) /* 从缓冲区按网络字节序读取 16 位整数。 */
{
    return (uint16_t)(((uint16_t)p[0] << 8) | (uint16_t)p[1]); /* 合成高低字节为 16 位值。 */
}

micp_err_t micp_frame_encode(const micp_frame_t *f, /* 将内存中的帧结构编码为线缆字节流。 */
                             uint8_t *out, size_t out_cap, size_t *out_len) /* 提供输出缓冲区、容量和实际长度回填。 */
{
    if (f == NULL || out == NULL || out_len == NULL) { /* 校验所有必需指针。 */
        return MICP_ERR_INVAL; /* 指针无效时返回参数错误。 */
    }
    if (f->length > MICP_MAX_PAYLOAD) { /* 确认载荷长度不超过协议上限。 */
        return MICP_ERR_LENGTH; /* 长度越界时拒绝编码。 */
    }

    const size_t total = micp_frame_size(f->length); /* 计算完整帧长度。 */
    if (out_cap < total) { /* 检查输出缓冲区是否足够。 */
        return MICP_ERR_NOBUFS; /* 容量不足时返回无缓冲错误。 */
    }

    out[0] = MICP_SOF; /* 写入帧起始字节。 */
    out[1] = MICP_VERSION; /* 写入协议版本号。 */
    out[2] = f->type; /* 写入消息类型。 */
    out[3] = f->flags; /* 写入标志位。 */
    put_u16(&out[4], f->src); /* 写入源节点地址。 */
    put_u16(&out[6], f->dst); /* 写入目标节点地址。 */
    put_u16(&out[8], f->seq); /* 写入序列号。 */
    put_u16(&out[10], f->length); /* 写入载荷长度。 */

    if (f->length > 0) { /* 仅在存在载荷时执行拷贝。 */
        memcpy(&out[MICP_HEADER_SIZE], f->payload, f->length); /* 将载荷复制到帧头之后。 */
    }

    /* CRC covers everything after SOF, up to and including payload. CRC 覆盖 SOF 之后到载荷末尾的所有字节。 */
    const size_t crc_span = (size_t)(MICP_HEADER_SIZE - 1) + f->length; /* bytes [1 .. ]; 计算参与 CRC 的字节数。 */
    const uint16_t crc = micp_crc16(&out[1], crc_span); /* 计算待写入的帧校验值。 */
    put_u16(&out[MICP_HEADER_SIZE + f->length], crc); /* 将 CRC 写到载荷之后。 */

    *out_len = total; /* 回填实际编码出的完整帧长度。 */
    return MICP_OK; /* 编码成功。 */
}

micp_err_t micp_frame_decode(const uint8_t *in, size_t in_len, /* 从输入字节流解析一帧。 */
                             micp_frame_t *f, size_t *consumed) /* 输出帧结构并回填已消费字节数。 */
{
    if (in == NULL || f == NULL || consumed == NULL) { /* 校验输入、输出和消费长度指针。 */
        return MICP_ERR_INVAL; /* 任一必需指针为空即返回参数错误。 */
    }
    if (in_len < 1) { /* 至少需要一个字节才能检查 SOF。 */
        return MICP_ERR_SHORT; /* 数据不足时要求更多字节。 */
    }
    if (in[0] != MICP_SOF) { /* 检查帧起始字节是否匹配。 */
        return MICP_ERR_SOF; /* 起始字节不匹配时返回同步错误。 */
    }
    if (in_len < MICP_HEADER_SIZE) { /* 完整帧头尚未到齐。 */
        return MICP_ERR_SHORT; /* 帧头不足时继续等待数据。 */
    }

    const uint8_t  version = in[1]; /* 读取协议版本字段。 */
    const uint16_t length  = get_u16(&in[10]); /* 读取载荷长度字段。 */

    if (version != MICP_VERSION) { /* 校验版本是否为当前实现支持的版本。 */
        return MICP_ERR_VERSION; /* 版本不兼容时返回版本错误。 */
    }
    if (length > MICP_MAX_PAYLOAD) { /* 校验声明载荷长度的协议上限。 */
        return MICP_ERR_LENGTH; /* 长度越界时返回长度错误。 */
    }

    const size_t total = micp_frame_size(length); /* 根据载荷长度计算整帧大小。 */
    if (in_len < total) { /* 检查输入缓冲区是否包含整帧。 */
        return MICP_ERR_SHORT; /* 整帧未到齐时要求更多字节。 */
    }

    const size_t crc_span = (size_t)(MICP_HEADER_SIZE - 1) + length; /* 计算 CRC 覆盖范围长度。 */
    const uint16_t calc = micp_crc16(&in[1], crc_span); /* 计算输入帧的期望 CRC。 */
    const uint16_t got  = get_u16(&in[MICP_HEADER_SIZE + length]); /* 读取帧尾携带的 CRC。 */
    if (calc != got) { /* 比较计算值与接收值。 */
        return MICP_ERR_CRC; /* 校验失败时返回 CRC 错误。 */
    }

    f->version = version; /* 保存版本字段。 */
    f->type    = in[2]; /* 保存消息类型字段。 */
    f->flags   = in[3]; /* 保存标志字段。 */
    f->src     = get_u16(&in[4]); /* 保存源节点地址。 */
    f->dst     = get_u16(&in[6]); /* 保存目标节点地址。 */
    f->seq     = get_u16(&in[8]); /* 保存序列号。 */
    f->length  = length; /* 保存载荷长度。 */
    if (length > 0) { /* 仅在存在载荷时拷贝载荷内容。 */
        memcpy(f->payload, &in[MICP_HEADER_SIZE], length); /* 将载荷复制到帧结构中。 */
    }

    *consumed = total; /* 回填本次解析消费的字节数。 */
    return MICP_OK; /* 解码成功。 */
}
