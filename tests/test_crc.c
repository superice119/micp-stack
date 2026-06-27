#include "micp/micp_crc.h"
#include "micp_test.h"

#include <string.h>

/* Known-answer test: CRC-16/CCITT-FALSE("123456789") == 0x29B1. */
static void test_crc_kat(void)
{
    const char *s = "123456789";
    uint16_t crc = micp_crc16((const uint8_t *)s, strlen(s));
    CHECK_EQ(crc, 0x29B1);
}

static void test_crc_empty(void)
{
    /* Over zero bytes the CRC equals the init seed. */
    CHECK_EQ(micp_crc16(NULL, 0), MICP_CRC16_INIT);
}

static void test_crc_incremental(void)
{
    const uint8_t data[] = {0xDE, 0xAD, 0xBE, 0xEF, 0x01, 0x02, 0x03};
    uint16_t whole = micp_crc16(data, sizeof(data));

    uint16_t inc = MICP_CRC16_INIT;
    inc = micp_crc16_update(inc, data, 3);
    inc = micp_crc16_update(inc, data + 3, sizeof(data) - 3);
    CHECK_EQ(whole, inc);
}

static void test_crc_sensitivity(void)
{
    uint8_t a[] = {0x10, 0x20, 0x30, 0x40};
    uint8_t b[] = {0x10, 0x20, 0x30, 0x41}; /* one bit differs */
    CHECK(micp_crc16(a, sizeof(a)) != micp_crc16(b, sizeof(b)));
}

int main(void)
{
    MICP_RUN(test_crc_kat);
    MICP_RUN(test_crc_empty);
    MICP_RUN(test_crc_incremental);
    MICP_RUN(test_crc_sensitivity);
    MICP_TEST_SUMMARY();
}
