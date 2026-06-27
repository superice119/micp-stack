/**
 * @file micp.h
 * @brief MICP umbrella header — include this to use the full protocol stack.
 *
 * MICP — Multica Industrial Communication Protocol.
 * A compact, dependency-free, transport-agnostic reference stack written in C11.
 */
#ifndef MICP_H
#define MICP_H

#include "micp/micp_types.h"
#include "micp/micp_crc.h"
#include "micp/micp_frame.h"
#include "micp/micp_session.h"

/** Library version (semantic). */
#define MICP_LIB_VERSION_MAJOR 0
#define MICP_LIB_VERSION_MINOR 1
#define MICP_LIB_VERSION_PATCH 0
#define MICP_LIB_VERSION_STR   "0.1.0"

#endif /* MICP_H */
