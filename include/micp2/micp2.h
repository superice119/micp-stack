/**
 * @file micp2.h
 * @brief MICP 2.0 umbrella header — signal-matrix private-CAN protocol.
 *
 * Include this to use the full MICP 2.0 API (signal codec + communication
 * matrix). MICP 2.0 is a CanPack/DBC-style private-CAN protocol: it defines
 * meaning on top of native CAN / CAN FD frames via Nodes, Message IDs and
 * Signals with start-bit / length / scale (factor) / offset mapping.
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
