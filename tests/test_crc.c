#include "micp/micp_crc.h" /* 引入 "micp/micp_crc.h" 依赖。 */
#include "micp_test.h" /* 引入 "micp_test.h" 依赖。 */

#include <string.h> /* 引入 <string.h> 依赖。 */

/* 已知答案测试：CRC-16/CCITT-FALSE("123456789") 应为 0x29B1。 */
static void test_crc_kat(void) /* 定义 test_crc_kat 测试用例。 */
{
    const char *s = "123456789"; /* 准备 CRC 标准测试字符串。 */
    uint16_t crc = micp_crc16((const uint8_t *)s, strlen(s)); /* 计算一次性 CRC 结果。 */
    CHECK_EQ(crc, 0x29B1); /* 断言实际值等于期望值。 */
}

static void test_crc_empty(void) /* 定义 test_crc_empty 测试用例。 */
{
    /* 零字节输入的 CRC 应等于初始种子。 */
    CHECK_EQ(micp_crc16(NULL, 0), MICP_CRC16_INIT); /* 断言实际值等于期望值。 */
}

static void test_crc_incremental(void) /* 定义 test_crc_incremental 测试用例。 */
{
    const uint8_t data[] = {0xDE, 0xAD, 0xBE, 0xEF, 0x01, 0x02, 0x03}; /* 准备增量 CRC 测试数据。 */
    uint16_t whole = micp_crc16(data, sizeof(data)); /* 计算完整数据 CRC。 */

    uint16_t inc = MICP_CRC16_INIT; /* 初始化增量 CRC 累计值。 */
    inc = micp_crc16_update(inc, data, 3); /* 用前三字节更新增量 CRC。 */
    inc = micp_crc16_update(inc, data + 3, sizeof(data) - 3); /* 用剩余字节完成增量 CRC。 */
    CHECK_EQ(whole, inc); /* 断言实际值等于期望值。 */
}

static void test_crc_sensitivity(void) /* 定义 test_crc_sensitivity 测试用例。 */
{
    uint8_t a[] = {0x10, 0x20, 0x30, 0x40}; /* 准备第一组 CRC 输入数据。 */
    uint8_t b[] = {0x10, 0x20, 0x30, 0x41}; /* 准备第二组 CRC 输入数据。 */
    CHECK(micp_crc16(a, sizeof(a)) != micp_crc16(b, sizeof(b))); /* 断言条件为真。 */
}

int main(void) /* 定义程序入口。 */
{
    MICP_RUN(test_crc_kat); /* 运行指定测试用例。 */
    MICP_RUN(test_crc_empty); /* 运行指定测试用例。 */
    MICP_RUN(test_crc_incremental); /* 运行指定测试用例。 */
    MICP_RUN(test_crc_sensitivity); /* 运行指定测试用例。 */
    MICP_TEST_SUMMARY(); /* 输出测试汇总并返回退出码。 */
}
