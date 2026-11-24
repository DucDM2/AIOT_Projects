/**
 * @file    ut.h
 * @brief   Khung unit test tối giản (không phụ thuộc thư viện ngoài). Chỉ dùng cho code test.
 */
#ifndef UT_H
#define UT_H

#include <stdio.h>
#include <string.h>
#include <stdint.h>

extern int ut_checks;
extern int ut_failures;

#define UT_CHECK(cond) do { \
    ut_checks++; \
    if (!(cond)) { ut_failures++; \
        printf("    FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } \
} while (0)

#define UT_EQ_U32(actual, expected) do { \
    const unsigned long ut_a = (unsigned long)(actual); \
    const unsigned long ut_e = (unsigned long)(expected); \
    ut_checks++; \
    if (ut_a != ut_e) { ut_failures++; \
        printf("    FAIL %s:%d: %s = 0x%08lX, mong doi 0x%08lX\n", __FILE__, __LINE__, #actual, ut_a, ut_e); } \
} while (0)

#define UT_EQ_INT(actual, expected) do { \
    const long ut_a = (long)(actual); \
    const long ut_e = (long)(expected); \
    ut_checks++; \
    if (ut_a != ut_e) { ut_failures++; \
        printf("    FAIL %s:%d: %s = %ld, mong doi %ld\n", __FILE__, __LINE__, #actual, ut_a, ut_e); } \
} while (0)

#define UT_EQ_STR(actual, expected) do { \
    const char *ut_a = (actual); \
    const char *ut_e = (expected); \
    ut_checks++; \
    if (strcmp(ut_a, ut_e) != 0) { ut_failures++; \
        printf("    FAIL %s:%d: %s\n      thuc te : %s\n      mong doi: %s\n", \
               __FILE__, __LINE__, #actual, ut_a, ut_e); } \
} while (0)

typedef void (*ut_fn_t)(void);
void ut_run(const char *name, ut_fn_t fn);

#define UT_RUN(fn) ut_run(#fn, fn)

#endif /* UT_H */
