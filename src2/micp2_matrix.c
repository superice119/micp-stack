/**
 * @file micp2_matrix.c
 * @brief MICP 2.0 communication-matrix lookup, encode/decode and dispatch.
 */
#include "micp2/micp2_matrix.h"

/* Minimal strcmp to avoid pulling <string.h> on tiny targets. */
static int str_eq(const char *a, const char *b)
{
    if (a == NULL || b == NULL) {
        return a == b;
    }
    while (*a && (*a == *b)) {
        a++;
        b++;
    }
    return *a == *b;
}

const micp2_message_t *micp2_matrix_find_by_id(const micp2_matrix_t *m,
                                               uint32_t can_id,
                                               uint8_t is_extended)
{
    if (m == NULL) {
        return NULL;
    }
    for (size_t i = 0; i < m->message_count; i++) {
        const micp2_message_t *msg = &m->messages[i];
        if (msg->can_id == can_id && msg->is_extended == is_extended) {
            return msg;
        }
    }
    return NULL;
}

const micp2_signal_t *micp2_message_find_signal(const micp2_message_t *msg,
                                                const char *name)
{
    if (msg == NULL || name == NULL) {
        return NULL;
    }
    for (size_t i = 0; i < msg->signal_count; i++) {
        if (str_eq(msg->signals[i].name, name)) {
            return &msg->signals[i];
        }
    }
    return NULL;
}

micp_err_t micp2_message_encode(const micp2_message_t *msg,
                                const double *phys_values,
                                uint8_t *frame, size_t frame_len)
{
    if (msg == NULL || phys_values == NULL || frame == NULL) {
        return MICP_ERR_INVAL;
    }
    if (frame_len < msg->dlc) {
        return MICP_ERR_LENGTH;
    }

    for (size_t i = 0; i < msg->dlc; i++) {
        frame[i] = 0;
    }
    for (size_t i = 0; i < msg->signal_count; i++) {
        micp_err_t rc = micp2_signal_encode(frame, msg->dlc,
                                            &msg->signals[i], phys_values[i]);
        if (rc != MICP_OK) {
            return rc;
        }
    }
    return MICP_OK;
}

micp_err_t micp2_message_decode(const micp2_message_t *msg,
                                const uint8_t *frame, size_t frame_len,
                                double *phys_values_out)
{
    if (msg == NULL || frame == NULL || phys_values_out == NULL) {
        return MICP_ERR_INVAL;
    }
    if (frame_len < msg->dlc) {
        return MICP_ERR_LENGTH;
    }

    for (size_t i = 0; i < msg->signal_count; i++) {
        micp_err_t rc = micp2_signal_decode(frame, msg->dlc,
                                            &msg->signals[i],
                                            &phys_values_out[i]);
        if (rc != MICP_OK) {
            return rc;
        }
    }
    return MICP_OK;
}

micp_err_t micp2_matrix_dispatch(const micp2_matrix_t *m,
                                 uint32_t can_id, uint8_t is_extended,
                                 const uint8_t *frame, size_t frame_len,
                                 micp2_rx_handler_t handler, void *user)
{
    const micp2_message_t *msg =
        micp2_matrix_find_by_id(m, can_id, is_extended);
    if (msg == NULL) {
        return MICP_ERR_INVAL;
    }
    if (msg->signal_count > MICP2_DISPATCH_MAX_SIGNALS) {
        return MICP_ERR_NOBUFS;
    }

    double values[MICP2_DISPATCH_MAX_SIGNALS];
    micp_err_t rc = micp2_message_decode(msg, frame, frame_len, values);
    if (rc != MICP_OK) {
        return rc;
    }
    if (handler != NULL) {
        handler(user, msg, values);
    }
    return MICP_OK;
}
