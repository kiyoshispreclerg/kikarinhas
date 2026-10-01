/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef KK_USERS_H
#define KK_USERS_H

#include <stdbool.h>
#include <stddef.h>

/* Everyone who ever talked in the chat, by "platform:id": what they chose
 * (avatar, palette, gear), their name and when they were first and last
 * seen. Stored as one line per person in a small text file:
 *
 *   youtube:UCxxxx<TAB>avatar=pikachu<TAB>palette=blue<TAB>gear=pokegears/ash_hat
 *   <TAB>name=Fulana<TAB>first=1700000000<TAB>last=1730000000
 *
 * gear lists "set/piece" separated by commas; first/last are Unix times. Unknown fields are kept, so
 * newer versions can add some without older ones dropping them. */

typedef struct kk_users kk_users;

typedef enum {
    KK_USER_AVATAR,
    KK_USER_PALETTE,
    KK_USER_GEAR,
    KK_USER_NAME,  /* display name, as last seen */
    KK_USER_FIRST, /* Unix time of the first message */
    KK_USER_LAST,  /* Unix time of the latest message */
} kk_user_field;

/* Loads path if it exists (a missing file is an empty list). */
kk_users *kk_users_open(const char *path);
void kk_users_free(kk_users *u);

bool kk_users_exists(const kk_users *u, const char *key);
/* NULL when unset. */
const char *kk_users_get(const kk_users *u, const char *key, kk_user_field f);
/* value NULL or "" clears the field. */
void kk_users_set(kk_users *u, const char *key, kk_user_field f,
                  const char *value);

/* Writes the file if anything changed (atomically: temp file + rename). */
int kk_users_save(kk_users *u);
bool kk_users_dirty(const kk_users *u);
int kk_users_count(const kk_users *u);
/* Key of the i-th person, 0 <= i < kk_users_count, in file order. */
const char *kk_users_key(const kk_users *u, int i);

/* Name and times of someone who just talked (now = Unix time). */
void kk_users_seen(kk_users *u, const char *key, const char *name, long long now);

/* Default location: $XDG_DATA_HOME/kikarinhas/users.tsv. */
bool kk_users_default_path(char *out, size_t size);

#endif
