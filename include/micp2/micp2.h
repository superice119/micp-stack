/**
 * @file micp2.h
 * @brief MICP 2.0 umbrella header — signal-matrix private-CAN protocol.
 *
 * Include this to use the full MICP 2.0 API (signal codec + communication
 * matrix). MICP 2.0 is the CanPack/DBC-style evolution of MICP: instead of a
 * transport-agnostic reliable byte stream, it defines meaning on top of fixed
 * CAN frames via Nodes, Message IDs and Signals with scale/offset mapping.
 *
 * MICP 1.x (include/micp/micp.h) and MICP 2.0 are complementary and can coexist
 * in the same build: use 1.x for reliable point-to-point byte transfer, 2.0 for
 * the cyclic signal matrix.
 */
#ifndef MICP2_H
#define MICP2_H

#include "micp2/micp2_signal.h"
#include "micp2/micp2_matrix.h"

#define MICP2_LIB_VERSION_MAJOR 2
#define MICP2_LIB_VERSION_MINOR 0
#define MICP2_LIB_VERSION_PATCH 0
#define MICP2_LIB_VERSION_STR   "2.0.0"

#endif /* MICP2_H */
