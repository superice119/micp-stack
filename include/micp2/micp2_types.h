/**
 * @file micp2_types.h
 * @brief MICP 2.0 shared return/error codes.
 *
 * MICP 2.0 is fully self-contained with no external dependencies.
 */
#ifndef MICP2_TYPES_H
#define MICP2_TYPES_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Library return / error codes. 0 == success, negative == failure. */
typedef enum {
    MICP2_OK          =  0,  /**< Operation succeeded.                       */
    MICP2_ERR_INVAL   = -1,  /**< Invalid argument.                         */
    MICP2_ERR_LENGTH  = -2,  /**< Field/frame length out of range.          */
    MICP2_ERR_NOBUFS  = -3   /**< Output buffer too small.                  */
} micp2_err_t;

#ifdef __cplusplus
}
#endif

#endif /* MICP2_TYPES_H */
