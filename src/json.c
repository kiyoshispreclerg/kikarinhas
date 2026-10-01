/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "json.h"

#include <stdlib.h>
#include <string.h>

const cJSON *kk_json_path(const cJSON *obj, const char *path)
{
    char key[128];
    const char *p = path;

    while (obj && *p) {
        if (*p == '.') {
            p++;
            continue;
        }
        if (*p == '[') {
            char *end;
            long idx = strtol(p + 1, &end, 10);
            if (*end != ']' || !cJSON_IsArray(obj))
                return NULL;
            if (idx < 0)
                idx += cJSON_GetArraySize(obj);
            obj = idx >= 0 ? cJSON_GetArrayItem(obj, (int)idx) : NULL;
            p = end + 1;
            continue;
        }
        size_t n = strcspn(p, ".[");
        if (n >= sizeof key)
            return NULL;
        memcpy(key, p, n);
        key[n] = '\0';
        obj = cJSON_GetObjectItemCaseSensitive(obj, key);
        p += n;
    }
    return obj;
}

const char *kk_json_str(const cJSON *obj, const char *path)
{
    const cJSON *v = kk_json_path(obj, path);
    return cJSON_IsString(v) ? v->valuestring : NULL;
}

double kk_json_num(const cJSON *obj, const char *path, double fallback)
{
    const cJSON *v = kk_json_path(obj, path);
    if (cJSON_IsNumber(v))
        return v->valuedouble;
    if (cJSON_IsString(v)) {
        char *end;
        double d = strtod(v->valuestring, &end);
        if (end != v->valuestring && *end == '\0')
            return d;
    }
    return fallback;
}
