/**
 * @file micp_test.h
 * @brief Minimal dependency-free unit-test harness (header-only).
 *
 * Each test file defines main() via MICP_TEST_MAIN and registers checks with
 * the CHECK / CHECK_EQ / CHECK_OK macros. A non-zero process exit code signals
 * failure, which is what CTest (and the Makefile) keys off of.
 */
#ifndef MICP_TEST_H
#define MICP_TEST_H

#include <stdio.h>
#include <stdlib.h>

static int micp_test_failures = 0;
static int micp_test_checks   = 0;

#define CHECK(cond)                                                            \
    do {                                                                       \
        micp_test_checks++;                                                    \
        if (!(cond)) {                                                         \
            micp_test_failures++;                                              \
            fprintf(stderr, "  FAIL %s:%d: CHECK(%s)\n",                       \
                    __FILE__, __LINE__, #cond);                                \
        }                                                                      \
    } while (0)

#define CHECK_EQ(a, b)                                                         \
    do {                                                                       \
        micp_test_checks++;                                                    \
        long long _a = (long long)(a);                                        \
        long long _b = (long long)(b);                                        \
        if (_a != _b) {                                                        \
            micp_test_failures++;                                              \
            fprintf(stderr, "  FAIL %s:%d: CHECK_EQ(%s, %s) -> %lld != %lld\n", \
                    __FILE__, __LINE__, #a, #b, _a, _b);                       \
        }                                                                      \
    } while (0)

#define CHECK_OK(expr) CHECK_EQ((expr), MICP_OK)

#define MICP_RUN(fn)                                                           \
    do {                                                                       \
        fprintf(stderr, "[run] %s\n", #fn);                                    \
        fn();                                                                  \
    } while (0)

#define MICP_TEST_SUMMARY()                                                    \
    do {                                                                       \
        fprintf(stderr, "%d checks, %d failures\n",                           \
                micp_test_checks, micp_test_failures);                         \
        return micp_test_failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;          \
    } while (0)

#endif /* MICP_TEST_H */
