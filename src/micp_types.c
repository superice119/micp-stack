#include "micp/micp_types.h"

const char *micp_strerror(micp_err_t err)
{
    switch (err) {
    case MICP_OK:          return "OK";
    case MICP_ERR_INVAL:   return "invalid argument";
    case MICP_ERR_NOBUFS:  return "output buffer too small";
    case MICP_ERR_SHORT:   return "need more bytes";
    case MICP_ERR_SOF:     return "start-of-frame not found";
    case MICP_ERR_VERSION: return "unsupported version";
    case MICP_ERR_LENGTH:  return "length out of range";
    case MICP_ERR_CRC:     return "CRC mismatch";
    case MICP_ERR_STATE:   return "invalid state";
    case MICP_ERR_TIMEOUT: return "timeout";
    case MICP_ERR_BUSY:    return "reliable send in flight";
    default:               return "unknown error";
    }
}

const char *micp_state_name(micp_state_t state)
{
    switch (state) {
    case MICP_STATE_DISCONNECTED: return "DISCONNECTED";
    case MICP_STATE_CONNECTING:   return "CONNECTING";
    case MICP_STATE_CONNECTED:    return "CONNECTED";
    case MICP_STATE_ERROR:        return "ERROR";
    default:                      return "UNKNOWN";
    }
}

const char *micp_msg_name(uint8_t type)
{
    switch (type) {
    case MICP_MSG_HELLO:      return "HELLO";
    case MICP_MSG_HELLO_ACK:  return "HELLO_ACK";
    case MICP_MSG_HEARTBEAT:  return "HEARTBEAT";
    case MICP_MSG_DATA:       return "DATA";
    case MICP_MSG_ACK:        return "ACK";
    case MICP_MSG_NACK:       return "NACK";
    case MICP_MSG_DISCONNECT: return "DISCONNECT";
    default:                  return "UNKNOWN";
    }
}
