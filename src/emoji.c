/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "emoji.h"

#include <string.h>

#include "emoji_table.h"

#define VS16 0xFE0Fu
#define ZWJ 0x200Du
#define KEYCAP 0x20E3u

/* Decodes the code point at s[*i] and advances *i; returns 0 (and skips
 * one byte) on invalid UTF-8. *i must be < len. */
static unsigned decode(const char *s, size_t len, size_t *i)
{
    const unsigned char *p = (const unsigned char *)s + *i;
    size_t left = len - *i;
    unsigned c = p[0];
    int n;
    if (c < 0x80) {
        *i += 1;
        return c;
    } else if ((c & 0xE0) == 0xC0) {
        n = 1;
        c &= 0x1F;
    } else if ((c & 0xF0) == 0xE0) {
        n = 2;
        c &= 0x0F;
    } else if ((c & 0xF8) == 0xF0) {
        n = 3;
        c &= 0x07;
    } else {
        *i += 1;
        return 0;
    }
    if ((size_t)n >= left) {
        *i += 1;
        return 0;
    }
    for (int k = 1; k <= n; k++) {
        if ((p[k] & 0xC0) != 0x80) {
            *i += 1;
            return 0;
        }
        c = c << 6 | (p[k] & 0x3F);
    }
    *i += (size_t)n + 1;
    return c;
}

static bool in(const kk_cp_range *t, size_t n, unsigned c)
{
    size_t lo = 0, hi = n;
    while (lo < hi) {
        size_t mid = (lo + hi) / 2;
        if (c < t[mid].lo)
            hi = mid;
        else if (c > t[mid].hi)
            lo = mid + 1;
        else
            return true;
    }
    return false;
}

#define IN(table, c) in(table, sizeof table / sizeof table[0], c)

static bool is_regional(unsigned c)
{
    return c >= 0x1F1E6 && c <= 0x1F1FF;
}

static bool is_modifier(unsigned c)
{
    return c >= 0x1F3FB && c <= 0x1F3FF;
}

static bool is_tag(unsigned c)
{
    return c >= 0xE0020 && c <= 0xE007F;
}

/* Code point at i without consuming it; 0 at the end. */
static unsigned peek(const char *s, size_t len, size_t i, size_t *after)
{
    *after = i;
    return i < len ? decode(s, len, after) : 0;
}

/* Consumes c if it is next. */
static bool take(const char *s, size_t len, size_t *i, unsigned c)
{
    size_t after;
    if (peek(s, len, *i, &after) != c)
        return false;
    *i = after;
    return true;
}

/* The selector, skin tone and tag sequence after an emoji base. */
static void take_trailers(const char *s, size_t len, size_t *i)
{
    take(s, len, i, VS16);
    size_t after;
    if (is_modifier(peek(s, len, *i, &after)))
        *i = after;
    while (is_tag(peek(s, len, *i, &after)))
        *i = after;
}

/* An emoji base at c (already consumed, *i after it): true if it starts an
 * emoji, with *i moved past the whole sequence. */
static bool emoji_at(const char *s, size_t len, unsigned c, size_t *i)
{
    size_t after;
    unsigned next = peek(s, len, *i, &after);

    if (is_regional(c)) {
        if (is_regional(next))
            *i = after; /* a flag; a lone indicator stays a letter-like box */
        return is_regional(next);
    }
    if ((c >= '0' && c <= '9') || c == '#' || c == '*') {
        size_t j = *i;
        take(s, len, &j, VS16);
        if (!take(s, len, &j, KEYCAP))
            return false;
        *i = j;
        return true;
    }
    if (!(IN(kk_emoji_pict, c) || (next == VS16 && IN(kk_emoji_any, c))))
        return false;
    take_trailers(s, len, i);
    /* ZWJ sequences: 👩 ZWJ 💻, 🏳️ ZWJ 🌈... */
    for (;;) {
        size_t j = *i;
        if (!take(s, len, &j, ZWJ))
            break;
        unsigned d = peek(s, len, j, &after);
        if (!IN(kk_emoji_any, d))
            break;
        *i = after;
        take_trailers(s, len, i);
    }
    return true;
}

size_t kk_emoji_next(const char *s, size_t len, size_t *pos, size_t *start)
{
    size_t i = *pos;
    while (i < len) {
        size_t at = i;
        unsigned c = decode(s, len, &i);
        if (c && emoji_at(s, len, c, &i)) {
            *start = at;
            *pos = i;
            return i - at;
        }
    }
    *pos = len;
    return 0;
}

bool kk_emoji_key(const char *s, size_t len, char *out, size_t size)
{
    size_t n = 0;
    for (size_t i = 0; i < len;) {
        size_t at = i;
        unsigned c = decode(s, len, &i);
        if (c == VS16)
            continue;
        if (n + (i - at) >= size)
            return false;
        memcpy(out + n, s + at, i - at);
        n += i - at;
    }
    if (n >= size)
        return false;
    out[n] = '\0';
    return true;
}

bool kk_emoji_is_one(const char *s)
{
    size_t len = strlen(s), pos = 0, start;
    size_t n = kk_emoji_next(s, len, &pos, &start);
    return n > 0 && start == 0 && n == len;
}
