/**
 * @file micp_test.h
 * @brief 极简、无外部依赖的头文件式单元测试框架。
 *
 * 每个测试文件使用 CHECK / CHECK_EQ / CHECK_OK 等宏注册断言；
 * 非零进程退出码表示失败，供 CTest 和 Makefile 判定。
 */
#ifndef MICP_TEST_H /* 开始头文件防重包含判断。 */
#define MICP_TEST_H /* 定义测试头文件防重包含宏。 */

#include <stdio.h> /* 引入 <stdio.h> 依赖。 */
#include <stdlib.h> /* 引入 <stdlib.h> 依赖。 */

static int micp_test_failures = 0; /* 初始化全局测试失败计数。 */
static int micp_test_checks   = 0; /* 初始化全局断言检查计数。 */

#define CHECK(cond) /* 定义条件断言宏。 */ \
    do { /* 开始宏的安全单语句块。 */ \
        micp_test_checks++; /* 递增计数或偏移。 */ \
        if (!(cond)) { /* 条件失败时记录断言错误。 */ \
            micp_test_failures++; /* 递增计数或偏移。 */ \
            fprintf(stderr, "  FAIL %s:%d: CHECK(%s)\n", /* 打印测试或错误信息。 */ \
                    __FILE__, __LINE__, #cond); /* 传入断言失败位置信息。 */ \
        } /* 结束失败处理分支。 */ \
    } while (0) /* 结束宏的安全单语句块。 */

#define CHECK_EQ(a, b) /* 定义相等断言宏。 */ \
    do { /* 开始宏的安全单语句块。 */ \
        micp_test_checks++; /* 递增计数或偏移。 */ \
        long long _a = (long long)(a); /* 缓存左侧表达式整数值。 */ \
        long long _b = (long long)(b); /* 缓存右侧表达式整数值。 */ \
        if (_a != _b) { /* 两侧数值不等时记录失败。 */ \
            micp_test_failures++; /* 递增计数或偏移。 */ \
            fprintf(stderr, "  FAIL %s:%d: CHECK_EQ(%s, %s) -> %lld != %lld\n", /* 打印测试或错误信息。 */ \
                    __FILE__, __LINE__, #a, #b, _a, _b); /* 传入断言失败位置信息。 */ \
        } /* 结束失败处理分支。 */ \
    } while (0) /* 结束宏的安全单语句块。 */

#define CHECK_OK(expr) CHECK_EQ((expr), MICP_OK) /* 定义 MICP_OK 断言宏。 */

#define MICP_RUN(fn) /* 定义测试用例运行宏。 */ \
    do { /* 开始宏的安全单语句块。 */ \
        fprintf(stderr, "[run] %s\n", #fn); /* 打印测试或错误信息。 */ \
        fn(); /* 调用当前测试函数。 */ \
    } while (0) /* 结束宏的安全单语句块。 */

#define MICP_TEST_SUMMARY() /* 定义测试汇总退出宏。 */ \
    do { /* 开始宏的安全单语句块。 */ \
        fprintf(stderr, "%d checks, %d failures\n", /* 打印测试或错误信息。 */ \
                micp_test_checks, micp_test_failures); /* 传入检查数和失败数。 */ \
        return micp_test_failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE; /* 返回当前函数结果。 */ \
    } while (0) /* 结束宏的安全单语句块。 */

#endif /* 结束头文件防重包含保护。 */
