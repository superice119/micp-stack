#include "micp/micp_types.h" /* 引入错误码、状态和消息类型定义。 */

const char *micp_strerror(micp_err_t err) /* 将错误码转换为可读字符串。 */
{
    switch (err) { /* 按错误码枚举选择说明文本。 */
    case MICP_OK:          return "OK"; /* 无错误。 */
    case MICP_ERR_INVAL:   return "invalid argument"; /* 参数无效。 */
    case MICP_ERR_NOBUFS:  return "output buffer too small"; /* 输出缓冲区过小。 */
    case MICP_ERR_SHORT:   return "need more bytes"; /* 输入字节数不足。 */
    case MICP_ERR_SOF:     return "start-of-frame not found"; /* 未找到帧起始字节。 */
    case MICP_ERR_VERSION: return "unsupported version"; /* 协议版本不支持。 */
    case MICP_ERR_LENGTH:  return "length out of range"; /* 长度字段超出范围。 */
    case MICP_ERR_CRC:     return "CRC mismatch"; /* CRC 校验不匹配。 */
    case MICP_ERR_STATE:   return "invalid state"; /* 状态机状态无效。 */
    case MICP_ERR_TIMEOUT: return "timeout"; /* 操作超时。 */
    case MICP_ERR_BUSY:    return "reliable send in flight"; /* 可靠发送仍在进行。 */
    default:               return "unknown error"; /* 未识别的错误码。 */
    }
}

const char *micp_state_name(micp_state_t state) /* 将连接状态转换为状态名称。 */
{
    switch (state) { /* 按状态枚举选择名称。 */
    case MICP_STATE_DISCONNECTED: return "DISCONNECTED"; /* 未连接状态。 */
    case MICP_STATE_CONNECTING:   return "CONNECTING"; /* 正在连接状态。 */
    case MICP_STATE_CONNECTED:    return "CONNECTED"; /* 已连接状态。 */
    case MICP_STATE_ERROR:        return "ERROR"; /* 错误状态。 */
    default:                      return "UNKNOWN"; /* 未识别的状态。 */
    }
}

const char *micp_msg_name(uint8_t type) /* 将消息类型转换为消息名称。 */
{
    switch (type) { /* 按消息类型选择名称。 */
    case MICP_MSG_HELLO:      return "HELLO"; /* 握手请求消息。 */
    case MICP_MSG_HELLO_ACK:  return "HELLO_ACK"; /* 握手确认消息。 */
    case MICP_MSG_HEARTBEAT:  return "HEARTBEAT"; /* 心跳消息。 */
    case MICP_MSG_DATA:       return "DATA"; /* 数据消息。 */
    case MICP_MSG_ACK:        return "ACK"; /* 确认消息。 */
    case MICP_MSG_NACK:       return "NACK"; /* 否定确认消息。 */
    case MICP_MSG_DISCONNECT: return "DISCONNECT"; /* 断开连接消息。 */
    default:                  return "UNKNOWN"; /* 未识别的消息类型。 */
    }
}
