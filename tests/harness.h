/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Minimal unit test harness. One header, no dependencies.
 *
 *   TEST(parses_seconds) {
 *       CHECK(x == 1);
 *       CHECK_INT_EQ(a, b);
 *       CHECK_STR_EQ(s, "abc");
 *   }
 *
 *   int main(void) {
 *       RUN(parses_seconds);
 *       return harness_report();
 *   }
 *
 * A failing CHECK records the failure and returns from the current test.
 * Test binaries exit non-zero if any test failed.
 */
#ifndef KK_TESTS_HARNESS_H
#define KK_TESTS_HARNESS_H

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

static int harness_tests_run;
static int harness_tests_failed;
static int harness_current_failed;

#define TEST(name) static void name(void)

#define HARNESS_FAIL_(...)                                                   \
    do {                                                                     \
        fprintf(stderr, "  %s:%d: ", __FILE__, __LINE__);                    \
        fprintf(stderr, __VA_ARGS__);                                        \
        fputc('\n', stderr);                                                 \
        harness_current_failed = 1;                                          \
    } while (0)

#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            HARNESS_FAIL_("CHECK(%s) failed", #cond);                        \
            return;                                                          \
        }                                                                    \
    } while (0)

/* Like CHECK, but with a printf-style message on failure. */
#define CHECK_MSG(cond, ...)                                                 \
    do {                                                                     \
        if (!(cond)) {                                                       \
            HARNESS_FAIL_(__VA_ARGS__);                                      \
            return;                                                          \
        }                                                                    \
    } while (0)

#define CHECK_INT_EQ(a, b)                                                   \
    do {                                                                     \
        intmax_t a_ = (intmax_t)(a), b_ = (intmax_t)(b);                     \
        if (a_ != b_) {                                                      \
            HARNESS_FAIL_("%s == %s failed: %jd != %jd", #a, #b, a_, b_);   \
            return;                                                          \
        }                                                                    \
    } while (0)

#define CHECK_STR_EQ(a, b)                                                   \
    do {                                                                     \
        const char *a_ = (a), *b_ = (b);                                     \
        if (a_ == NULL || b_ == NULL || strcmp(a_, b_) != 0) {               \
            HARNESS_FAIL_("%s == %s failed: \"%s\" != \"%s\"", #a, #b,       \
                          a_ ? a_ : "(null)", b_ ? b_ : "(null)");           \
            return;                                                          \
        }                                                                    \
    } while (0)

/* Checks that `haystack` contains `needle`. */
#define CHECK_STR_HAS(haystack, needle)                                      \
    do {                                                                     \
        const char *h_ = (haystack), *n_ = (needle);                         \
        if (h_ == NULL || strstr(h_, n_) == NULL) {                          \
            HARNESS_FAIL_("\"%s\" does not contain \"%s\"",                  \
                          h_ ? h_ : "(null)", n_);                           \
            return;                                                          \
        }                                                                    \
    } while (0)

static void harness_run_(const char *name, void (*fn)(void))
{
    harness_current_failed = 0;
    fn();
    harness_tests_run++;
    if (harness_current_failed) {
        harness_tests_failed++;
        fprintf(stderr, "FAIL %s\n", name);
    }
}

#define RUN(name) harness_run_(#name, name)

static int harness_report_(const char *file)
{
    fprintf(stderr, "%s: %d/%d passed\n", file,
            harness_tests_run - harness_tests_failed, harness_tests_run);
    return harness_tests_failed ? 1 : 0;
}

/* Prints the summary and returns the process exit code. */
#define harness_report() harness_report_(__FILE__)

#endif
