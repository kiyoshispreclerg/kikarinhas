/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef KK_JSON_H
#define KK_JSON_H

#include "cJSON.h"

/* Follows a dotted path such as "a.b[0].c" from obj; NULL if any step is
 * missing. Array indexes may be negative to count from the end. */
const cJSON *kk_json_path(const cJSON *obj, const char *path);

/* String at path, or NULL. */
const char *kk_json_str(const cJSON *obj, const char *path);

/* Number at path (strings holding a number are accepted), or fallback. */
double kk_json_num(const cJSON *obj, const char *path, double fallback);

#endif
