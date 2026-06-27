#include "micp/micp_frame.h" /* 引入 "micp/micp_frame.h" 依赖。 */
#include "micp_test.h" /* 引入 "micp_test.h" 依赖。 */

#include <string.h> /* 引入 <string.h> 依赖。 */

/* 往返测试：编码后再解码应还原原始逻辑帧。 */
static void test_frame_roundtrip(void) /* 定义 test_frame_roundtrip 测试用例。 */
{
    micp_frame_t in; /* 声明 MICP 帧对象。 */
    memset(&in, 0, sizeof(in)); /* 清零结构体初始状态。 */
    in.type   = MICP_MSG_DATA; /* 设置帧类型字段。 */
    in.flags  = MICP_FLAG_ACK_REQ; /* 设置帧标志字段。 */
    in.src    = 0x0102; /* 设置帧源地址字段。 */
    in.dst    = 0x0304; /* 设置帧目标地址字段。 */
    in.seq    = 0xBEEF; /* 设置帧序号字段。 */
    in.length = 5; /* 设置帧负载长度字段。 */
    memcpy(in.payload, "hello", 5); /* 复制数据到目标缓冲区。 */

    uint8_t buf[MICP_MAX_FRAME]; /* 声明最大帧编码缓冲区。 */
    size_t  n = 0; /* 初始化编码输出长度。 */
    CHECK_OK(micp_frame_encode(&in, buf, sizeof(buf), &n)); /* 断言调用成功返回。 */
    CHECK_EQ(n, micp_frame_size(5)); /* 断言实际值等于期望值。 */
    CHECK_EQ(buf[0], MICP_SOF); /* 验证帧起始字节。 */
    CHECK_EQ(buf[1], MICP_VERSION); /* 验证协议版本字节。 */

    micp_frame_t out; /* 声明 MICP 帧对象。 */
    size_t consumed = 0; /* 初始化解码消耗字节数。 */
    CHECK_OK(micp_frame_decode(buf, n, &out, &consumed)); /* 断言调用成功返回。 */
    CHECK_EQ(consumed, n); /* 断言实际值等于期望值。 */
    CHECK_EQ(out.type, MICP_MSG_DATA); /* 断言实际值等于期望值。 */
    CHECK_EQ(out.flags, MICP_FLAG_ACK_REQ); /* 断言实际值等于期望值。 */
    CHECK_EQ(out.src, 0x0102); /* 断言实际值等于期望值。 */
    CHECK_EQ(out.dst, 0x0304); /* 断言实际值等于期望值。 */
    CHECK_EQ(out.seq, 0xBEEF); /* 断言实际值等于期望值。 */
    CHECK_EQ(out.length, 5); /* 断言实际值等于期望值。 */
    CHECK_EQ(memcmp(out.payload, "hello", 5), 0); /* 断言实际值等于期望值。 */
}

static void test_frame_zero_payload(void) /* 定义 test_frame_zero_payload 测试用例。 */
{
    micp_frame_t in; /* 声明 MICP 帧对象。 */
    memset(&in, 0, sizeof(in)); /* 清零结构体初始状态。 */
    in.type = MICP_MSG_HEARTBEAT; /* 设置帧类型字段。 */
    in.src  = 1; /* 设置帧源地址字段。 */
    in.dst  = 2; /* 设置帧目标地址字段。 */

    uint8_t buf[MICP_MAX_FRAME]; /* 声明最大帧编码缓冲区。 */
    size_t  n = 0; /* 初始化编码输出长度。 */
    CHECK_OK(micp_frame_encode(&in, buf, sizeof(buf), &n)); /* 断言调用成功返回。 */
    CHECK_EQ(n, MICP_HEADER_SIZE + MICP_TRAILER_SIZE); /* 断言实际值等于期望值。 */

    micp_frame_t out; /* 声明 MICP 帧对象。 */
    size_t consumed = 0; /* 初始化解码消耗字节数。 */
    CHECK_OK(micp_frame_decode(buf, n, &out, &consumed)); /* 断言调用成功返回。 */
    CHECK_EQ(out.length, 0); /* 断言实际值等于期望值。 */
    CHECK_EQ(out.type, MICP_MSG_HEARTBEAT); /* 断言实际值等于期望值。 */
}

static void test_frame_max_payload(void) /* 定义 test_frame_max_payload 测试用例。 */
{
    micp_frame_t in; /* 声明 MICP 帧对象。 */
    memset(&in, 0, sizeof(in)); /* 清零结构体初始状态。 */
    in.type   = MICP_MSG_DATA; /* 设置帧类型字段。 */
    in.length = MICP_MAX_PAYLOAD; /* 设置帧负载长度字段。 */
    for (size_t i = 0; i < MICP_MAX_PAYLOAD; ++i) { /* 循环处理测试数据。 */
        in.payload[i] = (uint8_t)(i & 0xFF); /* 填充帧负载字节。 */
    }

    uint8_t buf[MICP_MAX_FRAME]; /* 声明最大帧编码缓冲区。 */
    size_t  n = 0; /* 初始化编码输出长度。 */
    CHECK_OK(micp_frame_encode(&in, buf, sizeof(buf), &n)); /* 断言调用成功返回。 */

    micp_frame_t out; /* 声明 MICP 帧对象。 */
    size_t consumed = 0; /* 初始化解码消耗字节数。 */
    CHECK_OK(micp_frame_decode(buf, n, &out, &consumed)); /* 断言调用成功返回。 */
    CHECK_EQ(out.length, MICP_MAX_PAYLOAD); /* 断言实际值等于期望值。 */
    CHECK_EQ(memcmp(out.payload, in.payload, MICP_MAX_PAYLOAD), 0); /* 断言实际值等于期望值。 */
}

static void test_frame_crc_error(void) /* 定义 test_frame_crc_error 测试用例。 */
{
    micp_frame_t in; /* 声明 MICP 帧对象。 */
    memset(&in, 0, sizeof(in)); /* 清零结构体初始状态。 */
    in.type   = MICP_MSG_DATA; /* 设置帧类型字段。 */
    in.length = 4; /* 设置帧负载长度字段。 */
    memcpy(in.payload, "data", 4); /* 复制数据到目标缓冲区。 */

    uint8_t buf[MICP_MAX_FRAME]; /* 声明最大帧编码缓冲区。 */
    size_t  n = 0; /* 初始化编码输出长度。 */
    CHECK_OK(micp_frame_encode(&in, buf, sizeof(buf), &n)); /* 断言调用成功返回。 */

    buf[MICP_HEADER_SIZE] ^= 0xFF; /* 篡改负载字节以触发 CRC 错误。 */

    micp_frame_t out; /* 声明 MICP 帧对象。 */
    size_t consumed = 0; /* 初始化解码消耗字节数。 */
    CHECK_EQ(micp_frame_decode(buf, n, &out, &consumed), MICP_ERR_CRC); /* 断言实际值等于期望值。 */
}

static void test_frame_short(void) /* 定义 test_frame_short 测试用例。 */
{
    micp_frame_t in; /* 声明 MICP 帧对象。 */
    memset(&in, 0, sizeof(in)); /* 清零结构体初始状态。 */
    in.type   = MICP_MSG_DATA; /* 设置帧类型字段。 */
    in.length = 8; /* 设置帧负载长度字段。 */
    memcpy(in.payload, "abcdefgh", 8); /* 复制数据到目标缓冲区。 */

    uint8_t buf[MICP_MAX_FRAME]; /* 声明最大帧编码缓冲区。 */
    size_t  n = 0; /* 初始化编码输出长度。 */
    CHECK_OK(micp_frame_encode(&in, buf, sizeof(buf), &n)); /* 断言调用成功返回。 */

    micp_frame_t out; /* 声明 MICP 帧对象。 */
    size_t consumed = 0; /* 初始化解码消耗字节数。 */
    /* 截断帧头应返回短帧错误。 */
    CHECK_EQ(micp_frame_decode(buf, 5, &out, &consumed), MICP_ERR_SHORT); /* 断言实际值等于期望值。 */
    /* 帧头完整但负载截断也应返回短帧错误。 */
    CHECK_EQ(micp_frame_decode(buf, MICP_HEADER_SIZE + 2, &out, &consumed), /* 断言实际值等于期望值。 */
             MICP_ERR_SHORT); /* 指定截断帧期望错误码。 */
}

static void test_frame_bad_sof(void) /* 定义 test_frame_bad_sof 测试用例。 */
{
    uint8_t buf[16] = {0}; /* 准备最小坏 SOF 测试缓冲区。 */
    buf[0] = 0x00; /* 写入非 SOF 字节。 */
    micp_frame_t out; /* 声明 MICP 帧对象。 */
    size_t consumed = 0; /* 初始化解码消耗字节数。 */
    CHECK_EQ(micp_frame_decode(buf, sizeof(buf), &out, &consumed), MICP_ERR_SOF); /* 断言实际值等于期望值。 */
}

static void test_frame_nobufs(void) /* 定义 test_frame_nobufs 测试用例。 */
{
    micp_frame_t in; /* 声明 MICP 帧对象。 */
    memset(&in, 0, sizeof(in)); /* 清零结构体初始状态。 */
    in.length = 10; /* 设置帧负载长度字段。 */
    uint8_t small[8]; /* 声明故意过小的编码缓冲区。 */
    size_t  n = 0; /* 初始化编码输出长度。 */
    CHECK_EQ(micp_frame_encode(&in, small, sizeof(small), &n), MICP_ERR_NOBUFS); /* 断言实际值等于期望值。 */
}

int main(void) /* 定义程序入口。 */
{
    MICP_RUN(test_frame_roundtrip); /* 运行指定测试用例。 */
    MICP_RUN(test_frame_zero_payload); /* 运行指定测试用例。 */
    MICP_RUN(test_frame_max_payload); /* 运行指定测试用例。 */
    MICP_RUN(test_frame_crc_error); /* 运行指定测试用例。 */
    MICP_RUN(test_frame_short); /* 运行指定测试用例。 */
    MICP_RUN(test_frame_bad_sof); /* 运行指定测试用例。 */
    MICP_RUN(test_frame_nobufs); /* 运行指定测试用例。 */
    MICP_TEST_SUMMARY(); /* 输出测试汇总并返回退出码。 */
}
