/**
 * @file micp2_matrix_demo.c
 * @brief MICP 2.0 demo — define a small CAN matrix, encode signals into a CAN
 *        frame, then decode them back via the dispatcher.
 *
 * This mirrors the CanPack / OEM private-CAN workflow: a node (here a BMS)
 * publishes a cyclic frame whose payload is a set of scaled signals; a receiver
 * looks the CAN ID up in the shared matrix and recovers physical values. No
 * heap, no OS — the only thing a real target adds is the CAN send/receive glue.
 */
#include "micp2/micp2.h"

#include <stdio.h>

/* ---- communication matrix (the "DBC") ----------------------------------- */
static const micp2_signal_t bms_signals[] = {
    /* name          start len  order                      sign           factor offset  min      max     unit */
    {"BattVolt",        0, 16, MICP2_BYTE_ORDER_INTEL, MICP2_UNSIGNED,  0.01,   0.0,    0.0,   655.35, "V"},
    {"BattCurrent",    16, 16, MICP2_BYTE_ORDER_INTEL, MICP2_SIGNED,    0.1,    0.0, -3000.0,  3000.0, "A"},
    {"SOC",            32,  8, MICP2_BYTE_ORDER_INTEL, MICP2_UNSIGNED,  0.5,    0.0,    0.0,   100.0,  "%"},
    {"PackTemp",       40,  8, MICP2_BYTE_ORDER_INTEL, MICP2_UNSIGNED,  0.5,  -40.0,  -40.0,    87.5,  "C"},
};

static const micp2_message_t messages[] = {
    {"BMS_Status", 0x123, /*ext*/0, /*dlc*/6, /*cycle*/100, /*sender*/3,
     bms_signals, sizeof bms_signals / sizeof bms_signals[0]},
};

static const micp2_node_t nodes[] = {
    {"ADAS", 1}, {"BCM", 2}, {"BMS", 3},
};

static const micp2_matrix_t matrix = {
    "vehicle_demo", nodes, 3, messages, 1
};

/* ---- receive handler ---------------------------------------------------- */
static void on_rx(void *user, const micp2_message_t *msg, const double *vals)
{
    (void)user;
    printf("RX  id=0x%03X  %s:\n", (unsigned)msg->can_id, msg->name);
    for (size_t i = 0; i < msg->signal_count; i++) {
        printf("      %-12s = %8.2f %s\n", msg->signals[i].name, vals[i],
               msg->signals[i].unit ? msg->signals[i].unit : "");
    }
}

int main(void)
{
    const micp2_message_t *m = micp2_matrix_find_by_id(&matrix, 0x123, 0);
    if (m == NULL) {
        fprintf(stderr, "message not found\n");
        return 1;
    }

    /* Producer side: physical values aligned to m->signals[]. */
    double tx[] = {401.23 /*V*/, -120.5 /*A*/, 87.0 /*%*/, 31.5 /*C*/};

    uint8_t frame[8] = {0};
    if (micp2_message_encode(m, tx, frame, sizeof frame) != MICP_OK) {
        fprintf(stderr, "encode failed\n");
        return 1;
    }

    printf("TX  id=0x%03X  dlc=%u  bytes:", (unsigned)m->can_id, m->dlc);
    for (uint8_t i = 0; i < m->dlc; i++) {
        printf(" %02X", frame[i]);
    }
    printf("\n");

    /* Consumer side: dispatch the raw CAN frame by id. */
    if (micp2_matrix_dispatch(&matrix, 0x123, 0, frame, m->dlc, on_rx, NULL)
        != MICP_OK) {
        fprintf(stderr, "dispatch failed\n");
        return 1;
    }

    printf("MICP 2.0 signal-matrix demo OK\n");
    return 0;
}
