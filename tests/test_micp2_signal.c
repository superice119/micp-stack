/**
 * @file test_micp2_signal.c
 * @brief Unit tests for the MICP 2.0 signal codec and matrix layer.
 */
#include "micp2/micp2.h"
#include "micp_test.h"

#include <math.h>

static int approx(double a, double b, double eps)
{
    double d = a - b;
    if (d < 0) d = -d;
    return d <= eps;
}

/* ---- raw Intel pack/unpack round-trip ----------------------------------- */
static void test_intel_roundtrip(void)
{
    /* 12-bit unsigned at start bit 4 (Intel). */
    micp2_signal_t s = {"x", 4, 12, MICP2_BYTE_ORDER_INTEL, MICP2_UNSIGNED,
                        1.0, 0.0, 0.0, 0.0, NULL};
    uint8_t f[8] = {0};
    CHECK_OK(micp2_signal_pack_raw(f, 8, &s, 0xABC));
    uint64_t raw = 0;
    CHECK_OK(micp2_signal_unpack_raw(f, 8, &s, &raw));
    CHECK_EQ((long long)raw, 0xABC);

    /* Intel low byte: bits 4..15 carry 0xABC -> byte0 = 0xC0, byte1 = 0xAB. */
    CHECK_EQ(f[0], 0xC0);
    CHECK_EQ(f[1], 0xAB);
}

/* ---- Motorola sawtooth: known vector ------------------------------------ */
static void test_motorola_roundtrip(void)
{
    /* 16-bit Motorola signal whose MSB is at start bit 7 (byte0 MSB).
       This is the classic "big-endian 16-bit at byte 0" layout: byte0 = high
       byte, byte1 = low byte. */
    micp2_signal_t s = {"m", 7, 16, MICP2_BYTE_ORDER_MOTOROLA, MICP2_UNSIGNED,
                        1.0, 0.0, 0.0, 0.0, NULL};
    uint8_t f[8] = {0};
    CHECK_OK(micp2_signal_pack_raw(f, 8, &s, 0x1234));
    CHECK_EQ(f[0], 0x12);
    CHECK_EQ(f[1], 0x34);

    uint64_t raw = 0;
    CHECK_OK(micp2_signal_unpack_raw(f, 8, &s, &raw));
    CHECK_EQ((long long)raw, 0x1234);
}

/* ---- sign extension ----------------------------------------------------- */
static void test_signed(void)
{
    CHECK_EQ((long long)micp2_raw_to_signed(0xFFF, 12), -1);
    CHECK_EQ((long long)micp2_raw_to_signed(0x800, 12), -2048);
    CHECK_EQ((long long)micp2_raw_to_signed(0x7FF, 12), 2047);
    CHECK_EQ((long long)micp2_raw_to_signed(0x00, 8), 0);

    micp2_signal_t s = {"t", 0, 12, MICP2_BYTE_ORDER_INTEL, MICP2_SIGNED,
                        0.1, 0.0, 0.0, 0.0, "C"};
    uint8_t f[8] = {0};
    CHECK_OK(micp2_signal_encode(f, 8, &s, -10.0)); /* raw = -100 */
    double v = 0;
    CHECK_OK(micp2_signal_decode(f, 8, &s, &v));
    CHECK(approx(v, -10.0, 1e-9));
}

/* ---- scale + offset physical round-trip --------------------------------- */
static void test_scale_offset(void)
{
    /* Battery voltage: 16-bit, factor 0.01, offset 0, range 0..655.35 V. */
    micp2_signal_t volt = {"BattVolt", 0, 16, MICP2_BYTE_ORDER_INTEL,
                           MICP2_UNSIGNED, 0.01, 0.0, 0.0, 655.35, "V"};
    uint8_t f[8] = {0};
    CHECK_OK(micp2_signal_encode(f, 8, &volt, 401.23));
    double v = 0;
    CHECK_OK(micp2_signal_decode(f, 8, &volt, &v));
    CHECK(approx(v, 401.23, 0.005));

    /* Temperature with negative offset: factor 0.5, offset -40 (CAN classic). */
    micp2_signal_t temp = {"Temp", 16, 8, MICP2_BYTE_ORDER_INTEL,
                           MICP2_UNSIGNED, 0.5, -40.0, -40.0, 87.5, "C"};
    CHECK_OK(micp2_signal_encode(f, 8, &temp, 25.0)); /* raw = (25+40)/0.5=130 */
    CHECK_EQ(f[2], 130);
    CHECK_OK(micp2_signal_decode(f, 8, &temp, &v));
    CHECK(approx(v, 25.0, 1e-9));
}

/* ---- clamping / saturation ---------------------------------------------- */
static void test_clamp(void)
{
    micp2_signal_t s = {"pct", 0, 8, MICP2_BYTE_ORDER_INTEL, MICP2_UNSIGNED,
                        1.0, 0.0, 0.0, 100.0, "%"};
    uint8_t f[8] = {0};
    CHECK_OK(micp2_signal_encode(f, 8, &s, 250.0)); /* clamps to 100 */
    double v = 0;
    CHECK_OK(micp2_signal_decode(f, 8, &s, &v));
    CHECK(approx(v, 100.0, 1e-9));

    CHECK_OK(micp2_signal_encode(f, 8, &s, -5.0)); /* clamps to 0 */
    CHECK_OK(micp2_signal_decode(f, 8, &s, &v));
    CHECK(approx(v, 0.0, 1e-9));
}

/* ---- bounds checking ---------------------------------------------------- */
static void test_bounds(void)
{
    micp2_signal_t s = {"o", 60, 8, MICP2_BYTE_ORDER_INTEL, MICP2_UNSIGNED,
                        1.0, 0.0, 0.0, 0.0, NULL};
    uint8_t f[8] = {0};
    /* needs bits 60..67 -> beyond 8 bytes (64 bits): must fail. */
    CHECK_EQ(micp2_signal_pack_raw(f, 8, &s, 1), MICP2_ERR_LENGTH);

    micp2_signal_t bad = {"b", 0, 0, MICP2_BYTE_ORDER_INTEL, MICP2_UNSIGNED,
                          1.0, 0.0, 0.0, 0.0, NULL};
    CHECK_EQ(micp2_signal_pack_raw(f, 8, &bad, 1), MICP2_ERR_INVAL);
}

/* ---- whole-message + matrix dispatch ------------------------------------ */
static const micp2_signal_t bms_signals[] = {
    {"BattVolt",    0, 16, MICP2_BYTE_ORDER_INTEL, MICP2_UNSIGNED, 0.01, 0.0,
     0.0, 655.35, "V"},
    {"BattCurrent", 16, 16, MICP2_BYTE_ORDER_INTEL, MICP2_SIGNED, 0.1, 0.0,
     -3000.0, 3000.0, "A"},
    {"SOC",         32, 8, MICP2_BYTE_ORDER_INTEL, MICP2_UNSIGNED, 0.5, 0.0,
     0.0, 100.0, "%"},
};
static const micp2_message_t bms_msgs[] = {
    {"BMS_Status", 0x123, 0, 6, 100, 3, bms_signals, 3},
};
static const micp2_node_t nodes[] = {{"BMS", 3}};
static const micp2_matrix_t matrix = {"demo", nodes, 1, bms_msgs, 1};

static int g_called = 0;
static double g_seen[3];
static void rx_handler(void *user, const micp2_message_t *msg,
                       const double *vals)
{
    (void)user;
    (void)msg;
    g_called = 1;
    g_seen[0] = vals[0];
    g_seen[1] = vals[1];
    g_seen[2] = vals[2];
}

static void test_matrix(void)
{
    const micp2_message_t *m = micp2_matrix_find_by_id(&matrix, 0x123, 0);
    CHECK(m != NULL);
    CHECK(micp2_matrix_find_by_id(&matrix, 0x999, 0) == NULL);
    CHECK(micp2_message_find_signal(m, "SOC") == &bms_signals[2]);

    double in[3] = {401.23, -120.5, 87.0};
    uint8_t frame[8] = {0};
    CHECK_OK(micp2_message_encode(m, in, frame, sizeof frame));

    double out[3] = {0};
    CHECK_OK(micp2_message_decode(m, frame, sizeof frame, out));
    CHECK(approx(out[0], 401.23, 0.005));
    CHECK(approx(out[1], -120.5, 0.05));
    CHECK(approx(out[2], 87.0, 0.25));

    g_called = 0;
    CHECK_OK(micp2_matrix_dispatch(&matrix, 0x123, 0, frame, sizeof frame,
                                   rx_handler, NULL));
    CHECK_EQ(g_called, 1);
    CHECK(approx(g_seen[1], -120.5, 0.05));

    CHECK_EQ(micp2_matrix_dispatch(&matrix, 0x999, 0, frame, sizeof frame,
                                   rx_handler, NULL),
             MICP2_ERR_INVAL);
}

int main(void)
{
    MICP_RUN(test_intel_roundtrip);
    MICP_RUN(test_motorola_roundtrip);
    MICP_RUN(test_signed);
    MICP_RUN(test_scale_offset);
    MICP_RUN(test_clamp);
    MICP_RUN(test_bounds);
    MICP_RUN(test_matrix);
    MICP_TEST_SUMMARY();
}
