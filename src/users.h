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
 *   <TAB>name=Fulana<TAB>first=1700000000<TAB>last=1730000000<TAB>role=member
 *   <TAB>trusted=yes<TAB>points=120<TAB>live_url=https://youtu.be/xxxx
 *   <TAB>live_title=Some live
 *
 * gear lists "set/piece" separated by commas; first/last are Unix times;
 * role is the level the chat badges gave on the latest message (absent for
 * common viewers) and trusted is set by hand. Unknown fields are kept, so
 * newer versions can add some without older ones dropping them. live_url
 * and live_title are written once, by the first message seen. */

typedef struct kk_users kk_users;

typedef enum {
    KK_USER_AVATAR,
    KK_USER_PALETTE,
    KK_USER_GEAR,
    KK_USER_NAME,  /* display name, as last seen */
    KK_USER_FIRST, /* Unix time of the first message */
    KK_USER_LAST,  /* Unix time of the latest message */
    KK_USER_ROLE,  /* "member", "mod", "owner" from the latest badges */
    KK_USER_TRUSTED, /* "yes" when marked by hand */
    KK_USER_POINTS,  /* balance (points.c) */
    KK_USER_LIVE_URL,   /* link of the first live they came from */
    KK_USER_LIVE_TITLE, /* its title, when the platform told it */
    KK_USER_N_FIELDS,
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

/* Forgets everyone's data for key; false if there was no such person. */
bool kk_users_remove(kk_users *u, const char *key);

/* Writes everyone, with every field, to another file (a backup) in the
 * same format. The people file itself is untouched. 0 or -1. */
int kk_users_export(const kk_users *u, const char *path);

typedef struct {
    int added;    /* people not in the list yet */
    int replaced; /* people already there, overwritten */
    int skipped;  /* people already there, left as they were */
} kk_users_import_stats;

/* Adds the people of a file written by kk_users_export (or any users.tsv).
 * Someone already in the list is kept as is unless overwrite, which replaces
 * their whole record. Marks the list as changed; -1 if the file cannot be
 * read. st may be NULL. */
int kk_users_import(kk_users *u, const char *path, bool overwrite, kk_users_import_stats *st);

/* Writes the file if anything changed (atomically: temp file + rename). */
int kk_users_save(kk_users *u);
bool kk_users_dirty(const kk_users *u);
int kk_users_count(const kk_users *u);
/* Key of the i-th person, 0 <= i < kk_users_count, in file order. */
const char *kk_users_key(const kk_users *u, int i);

/* Name and times of someone who just talked (now = Unix time). live_url and
 * live_title (either may be NULL) say which live it was; they are kept only
 * if the person has none yet, so they stay the first live. */
void kk_users_seen(kk_users *u, const char *key, const char *name, long long now,
                   const char *live_url, const char *live_title);

/* True if key was marked as trusted. */
bool kk_users_trusted(const kk_users *u, const char *key);

/* Default location: $XDG_DATA_HOME/kikarinhas/users.tsv. */
bool kk_users_default_path(char *out, size_t size);

#endif
