/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef KK_EMOJI_H
#define KK_EMOJI_H

/* Finds Unicode emoji in chat text. Platforms send them as plain
 * characters, so this is the one place that knows what an emoji looks like
 * in UTF-8: a pictograph with its optional U+FE0F, skin tone and tag
 * sequence, ZWJ sequences (👩‍💻), flag pairs (🇧🇷) and keycaps (1️⃣). A symbol
 * that is text by default (©, ™, ↔) counts only when followed by U+FE0F.
 *
 * The code point tables come from Unicode's emoji-data.txt, through
 * tools/gen_emoji_table.py. */

#include <stdbool.h>
#include <stddef.h>

/* Next emoji in s[0..len) from *pos on: its start goes to *start and its
 * byte length is returned; 0 when there are no more. *pos moves past it,
 * so a loop just calls this again. Invalid UTF-8 is skipped. */
size_t kk_emoji_next(const char *s, size_t len, size_t *pos, size_t *start);

/* The emoji in s[0..len) without the U+FE0F selectors, so ❤ and ❤️ are the
 * same thing (for caches, combos and blacklists). False if it does not fit
 * in out. */
bool kk_emoji_key(const char *s, size_t len, char *out, size_t size);

/* True if s is exactly one emoji and nothing else. */
bool kk_emoji_is_one(const char *s);

#endif
