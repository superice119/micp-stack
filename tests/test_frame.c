#include "micp/micp_frame.h"
#include "micp_test.h"

#include <string.h>

/* Round-trip: encode then decode yields the original logical frame. */
static void test_frame_roundtrip(void)
{
    micp_frame_t in;
    memset(&in, 0, sizeof(in));
    in.type   = MICP_MSG_DATA;
    in.flags  = MICP_FLAG_ACK_REQ;
    in.src    = 0x0102;
    in.dst    = 0x0304;
    in.seq    = 0xBEEF;
    in.length = 5;
    memcpy(in.payload, "hello", 5);

    uint8_t buf[MICP_MAX_FRAME];
    size_t  n = 0;
    CHECK_OK(micp_frame_encode(&in, buf, sizeof(buf), &n));
    CHECK_EQ(n, micp_frame_size(5));
    CHECK_EQ(buf[0], MICP_SOF);
    CHECK_EQ(buf[1], MICP_VERSION);

    micp_frame_t out;
    size_t consumed = 0;
    CHECK_OK(micp_frame_decode(buf, n, &out, &consumed));
    CHECK_EQ(consumed, n);
    CHECK_EQ(out.type, MICP_MSG_DATA);
    CHECK_EQ(out.flags, MICP_FLAG_ACK_REQ);
    CHECK_EQ(out.src, 0x0102);
    CHECK_EQ(out.dst, 0x0304);
    CHECK_EQ(out.seq, 0xBEEF);
    CHECK_EQ(out.length, 5);
    CHECK_EQ(memcmp(out.payload, "hello", 5), 0);
}

static void test_frame_zero_payload(void)
{
    micp_frame_t in;
    memset(&in, 0, sizeof(in));
    in.type = MICP_MSG_HEARTBEAT;
    in.src  = 1;
    in.dst  = 2;

    uint8_t buf[MICP_MAX_FRAME];
    size_t  n = 0;
    CHECK_OK(micp_frame_encode(&in, buf, sizeof(buf), &n));
    CHECK_EQ(n, MICP_HEADER_SIZE + MICP_TRAILER_SIZE);

    micp_frame_t out;
    size_t consumed = 0;
    CHECK_OK(micp_frame_decode(buf, n, &out, &consumed));
    CHECK_EQ(out.length, 0);
    CHECK_EQ(out.type, MICP_MSG_HEARTBEAT);
}

static void test_frame_max_payload(void)
{
    micp_frame_t in;
    memset(&in, 0, sizeof(in));
    in.type   = MICP_MSG_DATA;
    in.length = MICP_MAX_PAYLOAD;
    for (size_t i = 0; i < MICP_MAX_PAYLOAD; ++i) {
        in.payload[i] = (uint8_t)(i & 0xFF);
    }

    uint8_t buf[MICP_MAX_FRAME];
    size_t  n = 0;
    CHECK_OK(micp_frame_encode(&in, buf, sizeof(buf), &n));

    micp_frame_t out;
    size_t consumed = 0;
    CHECK_OK(micp_frame_decode(buf, n, &out, &consumed));
    CHECK_EQ(out.length, MICP_MAX_PAYLOAD);
    CHECK_EQ(memcmp(out.payload, in.payload, MICP_MAX_PAYLOAD), 0);
}

static void test_frame_crc_error(void)
{
    micp_frame_t in;
    memset(&in, 0, sizeof(in));
    in.type   = MICP_MSG_DATA;
    in.length = 4;
    memcpy(in.payload, "data", 4);

    uint8_t buf[MICP_MAX_FRAME];
    size_t  n = 0;
    CHECK_OK(micp_frame_encode(&in, buf, sizeof(buf), &n));

    buf[MICP_HEADER_SIZE] ^= 0xFF; /* corrupt a payload byte */

    micp_frame_t out;
    size_t consumed = 0;
    CHECK_EQ(micp_frame_decode(buf, n, &out, &consumed), MICP_ERR_CRC);
}

static void test_frame_short(void)
{
    micp_frame_t in;
    memset(&in, 0, sizeof(in));
    in.type   = MICP_MSG_DATA;
    in.length = 8;
    memcpy(in.payload, "abcdefgh", 8);

    uint8_t buf[MICP_MAX_FRAME];
    size_t  n = 0;
    CHECK_OK(micp_frame_encode(&in, buf, sizeof(buf), &n));

    micp_frame_t out;
    size_t consumed = 0;
    /* Truncated header. */
    CHECK_EQ(micp_frame_decode(buf, 5, &out, &consumed), MICP_ERR_SHORT);
    /* Header complete, payload truncated. */
    CHECK_EQ(micp_frame_decode(buf, MICP_HEADER_SIZE + 2, &out, &consumed),
             MICP_ERR_SHORT);
}

static void test_frame_bad_sof(void)
{
    uint8_t buf[16] = {0};
    buf[0] = 0x00; /* not SOF */
    micp_frame_t out;
    size_t consumed = 0;
    CHECK_EQ(micp_frame_decode(buf, sizeof(buf), &out, &consumed), MICP_ERR_SOF);
}

static void test_frame_nobufs(void)
{
    micp_frame_t in;
    memset(&in, 0, sizeof(in));
    in.length = 10;
    uint8_t small[8];
    size_t  n = 0;
    CHECK_EQ(micp_frame_encode(&in, small, sizeof(small), &n), MICP_ERR_NOBUFS);
}

int main(void)
{
    MICP_RUN(test_frame_roundtrip);
    MICP_RUN(test_frame_zero_payload);
    MICP_RUN(test_frame_max_payload);
    MICP_RUN(test_frame_crc_error);
    MICP_RUN(test_frame_short);
    MICP_RUN(test_frame_bad_sof);
    MICP_RUN(test_frame_nobufs);
    MICP_TEST_SUMMARY();
}
