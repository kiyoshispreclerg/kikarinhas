/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef KK_INI_H
#define KK_INI_H

#include <stdbool.h>

/* A small INI document that can be read and edited in place:
 *
 *   # comment            ; comment too
 *   [section]
 *   key = value
 *
 * Every line is kept as written, so a file edited by kikarinhas-config keeps
 * the user's comments, order and blank lines; only the touched lines change.
 * Section and key names are case-insensitive. Values are trimmed; there is
 * no quoting or escaping (a value may contain "=", "#" and ";"). */

typedef struct kk_ini kk_ini;

/* Empty document. */
kk_ini *kk_ini_new(void);
/* Parses text. Malformed lines are kept (and skipped by the getters); each
 * one is reported to warn with its 1-based line number, if warn is set. */
kk_ini *kk_ini_parse(const char *text,
                     void (*warn)(void *ud, int line, const char *msg),
                     void *ud);
void kk_ini_free(kk_ini *ini);

/* NULL if absent. With repeated keys, the last one wins. */
const char *kk_ini_get(const kk_ini *ini, const char *section, const char *key);
/* Line number of that key (0 if absent), for messages. */
int kk_ini_line(const kk_ini *ini, const char *section, const char *key);

/* Iteration in file order. Returns false at the end. *section is "" for keys
 * before the first header. Pass i = 0 first, then what it leaves in *i. */
bool kk_ini_next(const kk_ini *ini, int *i, const char **section,
                 const char **key, const char **value, int *line);

/* Sections in file order, each once; same protocol as kk_ini_next. */
bool kk_ini_next_section(const kk_ini *ini, int *i, const char **section);

/* Replaces the value in place, or appends the key to its section (created at
 * the end if missing). */
int kk_ini_set(kk_ini *ini, const char *section, const char *key,
               const char *value);
/* Removes every line with that key. */
void kk_ini_unset(kk_ini *ini, const char *section, const char *key);
/* Removes the section header and its keys (comments inside go too). */
void kk_ini_remove_section(kk_ini *ini, const char *section);

/* The whole document as text; the caller frees it. */
char *kk_ini_dump(const kk_ini *ini);
/* Writes atomically (temp file + rename), creating the parent folders. */
int kk_ini_save(const kk_ini *ini, const char *path);

#endif
