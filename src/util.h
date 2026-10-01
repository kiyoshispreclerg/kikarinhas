/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef KK_UTIL_H
#define KK_UTIL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define KK_PATH_MAX 4096

typedef struct {
    int x, y, w, h;
} kk_rect;

static inline bool kk_rect_empty(kk_rect r)
{
    return r.w <= 0 || r.h <= 0;
}

static inline bool kk_rect_intersects(kk_rect a, kk_rect b)
{
    return !kk_rect_empty(a) && !kk_rect_empty(b) && a.x < b.x + b.w &&
           b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h;
}

static inline bool kk_rect_equal(kk_rect a, kk_rect b)
{
    return a.x == b.x && a.y == b.y && a.w == b.w && a.h == b.h;
}

kk_rect kk_rect_union(kk_rect a, kk_rect b);

/* xorshift64*: small, fast, good enough for wandering avatars. */
typedef struct {
    uint64_t s;
} kk_rng;

void kk_rng_seed(kk_rng *r, uint64_t seed);
uint64_t kk_rng_next(kk_rng *r);
/* Uniform in [lo, hi). */
double kk_rng_range(kk_rng *r, double lo, double hi);
/* Uniform integer in [0, n), n > 0. */
int kk_rng_int(kk_rng *r, int n);

/* Reads a whole file into a NUL-terminated buffer; NULL on error (errno set).
 * The caller frees it. */
char *kk_read_file(const char *path, size_t *len);

bool kk_file_exists(const char *path);

/* mkdir -p for the directory part of path (errors are left to whoever
 * then creates the file). */
void kk_make_parent_dirs(const char *path);

/* snprintf for paths: false (and an empty string) if it would not fit. */
bool kk_pathf(char *out, size_t size, const char *fmt, ...)
    __attribute__((format(printf, 3, 4)));

#endif
