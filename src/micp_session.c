#include "micp/micp_session.h" /* 引入会话公共接口定义。 */
#include "micp/micp_crc.h" /* 引入帧校验相关接口。 */

#include <string.h> /* 引入内存操作函数。 */

/* ------------------------------------------------------------------ 内部辅助函数 */

static void set_state(micp_session_t *s, micp_state_t ns) /* 定义内部状态切换辅助函数。 */
{
    if (s->state == ns) { /* 状态未变化时无需通知。 */
        return; /* 直接返回避免重复事件。 */
    }
    micp_state_t old = s->state; /* 保存旧状态用于事件回调。 */
    s->state = ns; /* 写入新的会话状态。 */
    if (s->on_event) { /* 存在事件回调时才通知。 */
        s->on_event(s->user, old, ns); /* 通知上层状态从旧值切换到新值。 */
    }
}

/* 将帧编码并通过传输层发出。 */
static micp_err_t emit(micp_session_t *s, uint8_t type, uint8_t flags, /* 定义帧编码并经传输层发送的内部函数。 */
                       uint16_t dst, uint16_t seq, /* 继续声明目标地址与序号参数。 */
                       const uint8_t *payload, size_t len) /* 继续声明载荷指针与长度参数。 */
{
    micp_frame_t f; /* 声明待发送帧结构。 */
    memset(&f, 0, sizeof(f)); /* 清零帧结构避免脏字段。 */
    f.version = MICP_VERSION; /* 写入协议版本。 */
    f.type    = type; /* 写入消息类型。 */
    f.flags   = flags; /* 写入控制标志。 */
    f.src     = s->addr; /* 写入本端地址。 */
    f.dst     = dst; /* 写入目标地址。 */
    f.seq     = seq; /* 写入帧序号。 */
    f.length  = (uint16_t)len; /* 写入载荷长度。 */
    if (len > 0 && payload != NULL) { /* 仅在有效载荷非空时拷贝。 */
        memcpy(f.payload, payload, len); /* 复制载荷到帧缓冲。 */
    }

    uint8_t buf[MICP_MAX_FRAME]; /* 声明编码输出缓冲区。 */
    size_t  n = 0; /* 记录实际编码字节数。 */
    micp_err_t e = micp_frame_encode(&f, buf, sizeof(buf), &n); /* 把帧编码为线缆格式。 */
    if (e != MICP_OK) { /* 编码失败时进入错误分支。 */
        return e; /* 返回编码错误码。 */
    }
    e = s->output(s->user, buf, n); /* 通过用户提供的输出函数发送字节流。 */
    if (e == MICP_OK) { /* 发送成功后更新统计。 */
        s->stats.tx_frames++; /* 累计发送帧数。 */
        if (type == MICP_MSG_ACK) { /* ACK 帧额外计数。 */
            s->stats.acks_tx++; /* 累计发送 ACK 数。 */
        }
    }
    return e; /* 返回发送结果。 */
}

static micp_err_t send_ack(micp_session_t *s, uint16_t dst, uint16_t seq) /* 定义发送 ACK 的便捷函数。 */
{
    return emit(s, MICP_MSG_ACK, 0, dst, seq, NULL, 0); /* 发送无载荷 ACK 帧。 */
}

/* ------------------------------------------------------------------- 公共接口 */

micp_err_t micp_session_init(micp_session_t *s, uint16_t addr, /* 定义会话初始化入口。 */
                             micp_output_fn output, micp_recv_fn on_recv, /* 继续声明输出与接收回调参数。 */
                             void *user) /* 继续声明用户上下文参数。 */
{
    if (s == NULL || output == NULL || addr == MICP_ADDR_BROADCAST) { /* 校验会话指针、输出回调和本端地址。 */
        return MICP_ERR_INVAL; /* 参数非法时返回错误。 */
    }
    memset(s, 0, sizeof(*s)); /* 清零会话对象。 */
    s->addr               = addr; /* 保存本端节点地址。 */
    s->state              = MICP_STATE_DISCONNECTED; /* 初始化为断开状态。 */
    s->output             = output; /* 保存底层输出回调。 */
    s->on_recv            = on_recv; /* 保存上层接收回调。 */
    s->user               = user; /* 保存用户上下文。 */
    s->rto_ticks          = MICP_DEFAULT_RTO_TICKS; /* 设置默认重传超时。 */
    s->max_retries        = MICP_DEFAULT_MAX_RETRIES; /* 设置默认最大重试次数。 */
    s->heartbeat_ticks    = MICP_DEFAULT_HEARTBEAT_TICKS; /* 设置默认心跳周期。 */
    s->peer_timeout_ticks = MICP_DEFAULT_PEER_TIMEOUT; /* 设置默认对端静默超时。 */
    return MICP_OK; /* 初始化成功。 */
}

void micp_session_set_event_cb(micp_session_t *s, micp_event_fn cb) /* 定义事件回调设置入口。 */
{
    if (s != NULL) { /* 会话指针有效时才设置。 */
        s->on_event = cb; /* 保存状态事件回调。 */
    }
}

micp_err_t micp_session_connect(micp_session_t *s, uint16_t peer_addr) /* 定义主动连接入口。 */
{
    if (s == NULL) { /* 检查会话指针是否有效。 */
        return MICP_ERR_INVAL; /* 空指针返回参数错误。 */
    }
    if (s->state != MICP_STATE_DISCONNECTED && s->state != MICP_STATE_ERROR) { /* 仅允许从断开或错误状态发起连接。 */
        return MICP_ERR_STATE; /* 状态不允许时返回状态错误。 */
    }
    s->peer_addr      = peer_addr; /* 记录目标对端地址。 */
    s->tx_seq         = 0; /* 重置发送序号。 */
    s->has_last_rx    = 0; /* 清除接收去重记录。 */
    s->tx_pending     = 0; /* 清除待确认发送状态。 */
    s->heartbeat_timer = 0; /* 重置心跳计时器。 */
    s->peer_silence   = 0; /* 重置对端静默计时。 */
    set_state(s, MICP_STATE_CONNECTING); /* 切换到连接中状态。 */
    return emit(s, MICP_MSG_HELLO, 0, peer_addr, s->tx_seq++, NULL, 0); /* 发送 HELLO 并递增序号。 */
}

micp_err_t micp_session_send(micp_session_t *s, const uint8_t *payload, /* 定义应用数据发送入口。 */
                             size_t len, int reliable) /* 继续声明载荷长度和可靠发送标志。 */
{
    if (s == NULL || (payload == NULL && len > 0)) { /* 校验会话指针和载荷指针组合。 */
        return MICP_ERR_INVAL; /* 参数非法时返回错误。 */
    }
    if (len > MICP_MAX_PAYLOAD) { /* 检查载荷是否超过协议上限。 */
        return MICP_ERR_LENGTH; /* 载荷过长时返回长度错误。 */
    }
    if (s->state != MICP_STATE_CONNECTED) { /* 只有已连接状态允许发送数据。 */
        return MICP_ERR_STATE; /* 未连接时返回状态错误。 */
    }
    if (reliable && s->tx_pending) { /* 可靠发送采用停等，已有待确认帧时拒绝。 */
        return MICP_ERR_BUSY; /* 返回忙错误提示上层稍后重试。 */
    }

    uint16_t seq   = s->tx_seq++; /* 分配当前帧序号并递增。 */
    uint8_t  flags = reliable ? MICP_FLAG_ACK_REQ : 0; /* 根据可靠标志设置 ACK 请求位。 */

    if (reliable) { /* 可靠发送需要缓存可重传帧。 */
        /* 将帧写入重传缓存并从缓存发送。 */
        micp_frame_t f; /* 声明可靠数据帧结构。 */
        memset(&f, 0, sizeof(f)); /* 清零帧结构。 */
        f.version = MICP_VERSION; /* 写入协议版本。 */
        f.type    = MICP_MSG_DATA; /* 标记为 DATA 消息。 */
        f.flags   = flags; /* 写入可靠性标志。 */
        f.src     = s->addr; /* 写入本端地址。 */
        f.dst     = s->peer_addr; /* 写入当前对端地址。 */
        f.seq     = seq; /* 写入本次发送序号。 */
        f.length  = (uint16_t)len; /* 写入载荷长度。 */
        if (len > 0) { /* 有载荷时才拷贝数据。 */
            memcpy(f.payload, payload, len); /* 复制载荷到帧结构。 */
        }
        micp_err_t e = micp_frame_encode(&f, s->pending_buf, /* 将可靠帧编码到重传缓存。 */
                                         sizeof(s->pending_buf), &s->pending_len); /* 传入缓存容量并接收编码长度。 */
        if (e != MICP_OK) { /* 检查可靠帧编码结果。 */
            return e; /* 编码失败时返回错误。 */
        }
        s->tx_pending  = 1; /* 标记存在待确认帧。 */
        s->pending_seq = seq; /* 记录待确认序号。 */
        s->retries     = 0; /* 清零重试次数。 */
        s->rto_timer   = s->rto_ticks; /* 启动重传超时计时。 */
        e = s->output(s->user, s->pending_buf, s->pending_len); /* 从重传缓存发送首帧。 */
        if (e == MICP_OK) { /* 首发成功后更新统计。 */
            s->stats.tx_frames++; /* 累计发送帧数。 */
        }
        return e; /* 返回首发结果。 */
    }

    return emit(s, MICP_MSG_DATA, flags, s->peer_addr, seq, payload, len); /* 非可靠发送直接编码并输出 DATA 帧。 */
}

micp_err_t micp_session_disconnect(micp_session_t *s) /* 定义断开连接入口。 */
{
    if (s == NULL) { /* 检查会话指针是否有效。 */
        return MICP_ERR_INVAL; /* 空指针返回参数错误。 */
    }
    micp_err_t e = MICP_OK; /* 默认断开结果为成功。 */
    if (s->state == MICP_STATE_CONNECTED || s->state == MICP_STATE_CONNECTING) { /* 连接中或已连接时通知对端断开。 */
        e = emit(s, MICP_MSG_DISCONNECT, 0, s->peer_addr, s->tx_seq++, NULL, 0); /* 发送 DISCONNECT 并递增序号。 */
    }
    s->tx_pending = 0; /* 清除待确认发送状态。 */
    set_state(s, MICP_STATE_DISCONNECTED); /* 切换到断开状态。 */
    return e; /* 返回断开流程结果。 */
}

/* --------------------------------------------------------- 帧处理逻辑 */

static void handle_frame(micp_session_t *s, const micp_frame_t *f) /* 定义单帧处理函数。 */
{
    /* 忽略非本节点且非广播目标的帧。 */
    if (f->dst != s->addr && f->dst != MICP_ADDR_BROADCAST) { /* 过滤非本节点且非广播目标的帧。 */
        s->stats.rx_dropped++; /* 累计丢弃帧数。 */
        return; /* 丢弃后结束处理。 */
    }

    /*
     * 对端隔离：一旦绑定对端（CONNECTING/CONNECTED），除 HELLO 外的每个帧
     * 都必须来自该对端；HELLO 仍允许被动打开或新对端重新打开。
     * 这样可防止第三方注入 DATA 帧，
     * 或伪造 ACK/NACK 错误清除正在等待确认的可靠帧。
     * 来源不匹配的帧会被丢弃并计数，且关键是不会
     * 刷新下面的对端活性计时。
     */
    if (f->type != MICP_MSG_HELLO && /* 非 HELLO 帧必须通过已绑定对端校验。 */
        (s->state == MICP_STATE_CONNECTED || s->state == MICP_STATE_CONNECTING) && /* 仅在连接中或已连接状态执行对端隔离。 */
        f->src != s->peer_addr) { /* 来源不是当前对端时视为不可信。 */
        s->stats.rx_dropped++; /* 累计丢弃帧数。 */
        return; /* 拒绝刷新活性并返回。 */
    }

    s->stats.rx_frames++; /* 累计接收帧数。 */
    s->peer_silence = 0; /* 收到有效对端帧后清零静默计时。 */

    switch (f->type) { /* 按消息类型分派处理。 */
    case MICP_MSG_HELLO: /* 处理 HELLO 握手帧。 */
        /* 被动打开：接受 HELLO 并回复 HELLO_ACK。 */
        s->peer_addr   = f->src; /* 记录或更新对端地址。 */
        s->has_last_rx = 0; /* 重置接收去重记录。 */
        s->tx_pending  = 0; /* 清除待确认发送状态。 */
        s->heartbeat_timer = 0; /* 重置心跳计时器。 */
        set_state(s, MICP_STATE_CONNECTED); /* 被动打开后进入已连接状态。 */
        emit(s, MICP_MSG_HELLO_ACK, 0, f->src, s->tx_seq++, NULL, 0); /* 回复 HELLO_ACK 并递增序号。 */
        break; /* 结束 HELLO 分支。 */

    case MICP_MSG_HELLO_ACK: /* 处理 HELLO_ACK 握手响应。 */
        if (s->state == MICP_STATE_CONNECTING && f->src == s->peer_addr) { /* 仅在连接中且来源匹配时接受响应。 */
            set_state(s, MICP_STATE_CONNECTED); /* 主动连接完成，进入已连接状态。 */
        }
        break; /* 结束 HELLO_ACK 分支。 */

    case MICP_MSG_HEARTBEAT: /* 处理 HEARTBEAT 心跳帧。 */
        /* 活性已在上方刷新，这里无需额外处理。 */
        break; /* 结束 HEARTBEAT 分支。 */

    case MICP_MSG_DATA: /* 处理 DATA 数据帧。 */
        if (s->state != MICP_STATE_CONNECTED) { /* 非已连接状态不接收数据。 */
            s->stats.rx_dropped++; /* 累计丢弃帧数。 */
            break; /* 退出 DATA 分支。 */
        }
        /* Acknowledge if requested (before dedup so retransmits get ACKed). */
        if (f->flags & MICP_FLAG_ACK_REQ) { /* 检查发送方是否请求确认。 */
            send_ack(s, f->src, f->seq); /* 立即回复对应序号的 ACK。 */
        }
        /* 对可靠流执行重复帧抑制。 */
        if (s->has_last_rx && f->seq == s->last_rx_seq) { /* 检测可靠流中的重复序号。 */
            break; /* duplicate retransmission already delivered */
        }
        s->has_last_rx = 1; /* 标记已有最近接收序号。 */
        s->last_rx_seq = f->seq; /* 记录最近接收序号用于去重。 */
        if (s->on_recv) { /* 存在接收回调时才交付。 */
            s->on_recv(s->user, f->src, f->payload, f->length); /* 向上层交付来源、载荷和长度。 */
        }
        break; /* 结束 DATA 分支。 */

    case MICP_MSG_ACK: /* 处理 ACK 确认帧。 */
        if (s->tx_pending && f->seq == s->pending_seq) { /* 仅匹配待确认序号时清除等待。 */
            s->tx_pending = 0; /* 清除待确认标志。 */
            s->stats.acks_rx++; /* 累计接收 ACK 数。 */
        }
        break; /* 结束 ACK 分支。 */

    case MICP_MSG_NACK: /* 处理 NACK 否定确认帧。 */
        /* 让下一次 tick 立即触发重传。 */
        if (s->tx_pending && f->seq == s->pending_seq) { /* 仅匹配待确认序号时触发重传。 */
            s->rto_timer = 0; /* 把重传计时置零以便下次 tick 立即发送。 */
        }
        break; /* 结束 NACK 分支。 */

    case MICP_MSG_DISCONNECT: /* 处理 DISCONNECT 断开帧。 */
        s->tx_pending = 0; /* 清除待确认发送状态。 */
        set_state(s, MICP_STATE_DISCONNECTED); /* 切换到断开状态。 */
        break; /* 结束 DISCONNECT 分支。 */

    default: /* 处理未知消息类型。 */
        s->stats.rx_dropped++; /* 累计丢弃帧数。 */
        break; /* 结束默认分支。 */
    }
}

micp_err_t micp_session_feed(micp_session_t *s, const uint8_t *data, size_t len) /* 定义字节流输入解析入口。 */
{
    if (s == NULL || (data == NULL && len > 0)) { /* 校验会话指针和输入缓冲组合。 */
        return MICP_ERR_INVAL; /* 参数非法时返回错误。 */
    }

    for (size_t i = 0; i < len; ++i) { /* 逐字节喂入解析缓冲。 */
        /* 累积到 rx_buf，同时防止溢出。 */
        if (s->rx_len >= sizeof(s->rx_buf)) { /* 检查接收缓冲是否已满。 */
            /* 正常解析会排空缓冲；若仍溢出则防御性重同步。 */
            s->rx_len = 0; /* 溢出时清空缓冲以重新同步。 */
        }
        s->rx_buf[s->rx_len++] = data[i]; /* 追加当前字节并递增缓冲长度。 */

        /* 尽可能从缓冲中提取完整帧。 */
        for (;;) { /* 循环解析当前缓冲中的完整帧。 */
            if (s->rx_len == 0) { /* 缓冲为空时停止解析。 */
                break; /* 跳出内层解析循环。 */
            }
            /* 重同步到 SOF：丢弃前导非 SOF 字节。 */
            if (s->rx_buf[0] != MICP_SOF) { /* 首字节不是帧起始符时执行重同步。 */
                size_t k = 1; /* 从下一个字节开始寻找起始符。 */
                while (k < s->rx_len && s->rx_buf[k] != MICP_SOF) { /* 扫描直到找到起始符或缓冲尾。 */
                    k++; /* 前进扫描位置。 */
                }
                memmove(s->rx_buf, s->rx_buf + k, s->rx_len - k); /* 丢弃起始符前的噪声字节。 */
                s->rx_len -= k; /* 更新剩余缓冲长度。 */
                if (s->rx_len == 0) { /* 丢弃后为空则等待更多输入。 */
                    break; /* 跳出内层解析循环。 */
                }
            }

            micp_frame_t f; /* 声明解码后的帧结构。 */
            size_t consumed = 0; /* 记录本次解码消耗字节数。 */
            micp_err_t e = micp_frame_decode(s->rx_buf, s->rx_len, &f, &consumed); /* 尝试从接收缓冲解码一帧。 */
            if (e == MICP_OK) { /* 解码成功时处理完整帧。 */
                handle_frame(s, &f); /* 分派已解码帧。 */
                memmove(s->rx_buf, s->rx_buf + consumed, s->rx_len - consumed); /* 移除已消费的帧字节。 */
                s->rx_len -= consumed; /* 更新接收缓冲长度。 */
                continue; /* 可能还有下一帧已在缓冲中。 */
            } else if (e == MICP_ERR_SHORT) { /* 帧不完整时等待更多字节。 */
                break; /* 需要更多字节后再解析。 */
            } else if (e == MICP_ERR_CRC) { /* CRC 错误时进入重同步路径。 */
                s->stats.rx_crc_errors++; /* 累计 CRC 错误数。 */
                /* 丢弃当前 SOF 字节并继续重同步。 */
                memmove(s->rx_buf, s->rx_buf + 1, s->rx_len - 1); /* 丢弃当前起始符字节。 */
                s->rx_len -= 1; /* 更新接收缓冲长度。 */
                continue; /* 继续重试解析剩余数据。 */
            } else { /* 其他解码错误进入通用重同步路径。 */
                /* VERSION / LENGTH / SOF 错误：丢弃一字节并重同步。 */
                s->stats.rx_dropped++; /* 累计丢弃帧数。 */
                memmove(s->rx_buf, s->rx_buf + 1, s->rx_len - 1); /* 丢弃一个字节以寻找下一个帧。 */
                s->rx_len -= 1; /* 更新接收缓冲长度。 */
                continue; /* 继续重试解析剩余数据。 */
            }
        }
    }
    return MICP_OK; /* 所有输入处理完成。 */
}

micp_err_t micp_session_tick(micp_session_t *s, uint32_t dt) /* 定义周期性计时入口。 */
{
    if (s == NULL) { /* 检查会话指针是否有效。 */
        return MICP_ERR_INVAL; /* 空指针返回参数错误。 */
    }

    /* 停等可靠发送的重传计时器。 */
    if (s->tx_pending) { /* 存在待确认可靠帧时处理重传计时。 */
        if (s->rto_timer <= dt) { /* 重传计时已到期时进入重传逻辑。 */
            if (s->retries >= s->max_retries) { /* 达到最大重试次数时判定超时。 */
                s->tx_pending = 0; /* 清除待确认状态。 */
                set_state(s, MICP_STATE_ERROR); /* 切换到错误状态。 */
                return MICP_ERR_TIMEOUT; /* 返回超时错误。 */
            }
            s->retries++; /* 增加重试次数。 */
            s->stats.retransmits++; /* 累计重传次数。 */
            s->rto_timer = s->rto_ticks; /* 重置重传计时器。 */
            (void)s->output(s->user, s->pending_buf, s->pending_len); /* 重新发送缓存帧，忽略本次输出错误。 */
            s->stats.tx_frames++; /* 累计发送帧数。 */
        } else { /* 重传计时未到期时进入倒计时。 */
            s->rto_timer -= dt; /* 扣减经过的 tick 数。 */
        }
    }

    /* 仅在已关联对端时发送心跳并检查活性。 */
    if (s->state == MICP_STATE_CONNECTED) { /* 仅已连接状态发送心跳并检测活性。 */
        s->heartbeat_timer += dt; /* 累加心跳计时。 */
        if (s->heartbeat_timer >= s->heartbeat_ticks) { /* 达到心跳周期时发送心跳。 */
            s->heartbeat_timer = 0; /* 重置心跳计时器。 */
            emit(s, MICP_MSG_HEARTBEAT, 0, s->peer_addr, s->tx_seq++, NULL, 0); /* 发送 HEARTBEAT 并递增序号。 */
        }
        s->peer_silence += dt; /* 累加对端静默时长。 */
        if (s->peer_silence >= s->peer_timeout_ticks) { /* 静默超过阈值时判定对端超时。 */
            s->tx_pending = 0; /* 清除待确认状态。 */
            set_state(s, MICP_STATE_ERROR); /* 切换到错误状态。 */
            return MICP_ERR_TIMEOUT; /* 返回超时错误。 */
        }
    }
    return MICP_OK; /* 计时处理成功完成。 */
}
