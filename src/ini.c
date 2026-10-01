/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "ini.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "util.h"

typedef enum {
    LINE_OTHER, /* blank, comment or malformed */
    LINE_SECTION,
    LINE_KEY,
} line_kind;

typedef struct {
    char *raw;
    line_kind kind;
    char *section; /* the section the line is in (its own name for headers) */
    char *key, *value; /* LINE_KEY only */
    int number;        /* line in the parsed text; 0 for added lines */
} line;

struct kk_ini {
    line *lines;
    int count, cap;
};

kk_ini *kk_ini_new(void)
{
    return calloc(1, sizeof(kk_ini));
}

static void line_free(line *l)
{
    free(l->raw);
    free(l->section);
    free(l->key);
    free(l->value);
}

void kk_ini_free(kk_ini *ini)
{
    if (!ini)
        return;
    for (int i = 0; i < ini->count; i++)
        line_free(&ini->lines[i]);
    free(ini->lines);
    free(ini);
}

static char *dup_trimmed(const char *s, size_t n)
{
    while (n && isspace((unsigned char)*s)) {
        s++;
        n--;
    }
    while (n && isspace((unsigned char)s[n - 1]))
        n--;
    char *d = malloc(n + 1);
    if (d) {
        memcpy(d, s, n);
        d[n] = '\0';
    }
    return d;
}

/* Inserts a zeroed line at index at; NULL without memory. */
static line *insert_line(kk_ini *ini, int at)
{
    if (ini->count == ini->cap) {
        int cap = ini->cap ? ini->cap * 2 : 32;
        line *nl = realloc(ini->lines, (size_t)cap * sizeof *nl);
        if (!nl)
            return NULL;
        ini->lines = nl;
        ini->cap = cap;
    }
    memmove(&ini->lines[at + 1], &ini->lines[at],
            (size_t)(ini->count - at) * sizeof(line));
    ini->count++;
    memset(&ini->lines[at], 0, sizeof(line));
    return &ini->lines[at];
}

static void remove_line(kk_ini *ini, int at)
{
    line_free(&ini->lines[at]);
    memmove(&ini->lines[at], &ini->lines[at + 1],
            (size_t)(ini->count - at - 1) * sizeof(line));
    ini->count--;
}

/* Fills l from its raw text; returns an error message for malformed lines. */
static const char *classify(line *l, const char *section)
{
    const char *s = l->raw;
    while (isspace((unsigned char)*s))
        s++;
    if (!*s || *s == '#' || *s == ';') {
        l->kind = LINE_OTHER;
        l->section = strdup(section);
        return NULL;
    }
    if (*s == '[') {
        const char *end = strchr(s, ']');
        const char *after = end ? end + 1 : NULL;
        while (after && isspace((unsigned char)*after))
            after++;
        if (!end || (*after && *after != '#' && *after != ';')) {
            l->kind = LINE_OTHER;
            l->section = strdup(section);
            return "cabeçalho de seção malformado";
        }
        l->kind = LINE_SECTION;
        l->section = dup_trimmed(s + 1, (size_t)(end - s - 1));
        return NULL;
    }
    const char *eq = strchr(s, '=');
    l->section = strdup(section);
    if (!eq || eq == s) {
        l->kind = LINE_OTHER;
        return "esperava \"chave = valor\"";
    }
    l->kind = LINE_KEY;
    l->key = dup_trimmed(s, (size_t)(eq - s));
    l->value = dup_trimmed(eq + 1, strlen(eq + 1));
    if (!l->key[0]) {
        l->kind = LINE_OTHER;
        return "chave vazia";
    }
    return NULL;
}

kk_ini *kk_ini_parse(const char *text,
                     void (*warn)(void *ud, int line, const char *msg),
                     void *ud)
{
    kk_ini *ini = kk_ini_new();
    if (!ini)
        return NULL;
    if (strncmp(text, "\xEF\xBB\xBF", 3) == 0)
        text += 3;
    const char *section = "";
    int number = 0;
    while (*text) {
        size_t n = strcspn(text, "\n");
        size_t len = n;
        if (len && text[len - 1] == '\r')
            len--;
        line *l = insert_line(ini, ini->count);
        if (!l || !(l->raw = malloc(len + 1))) {
            kk_ini_free(ini);
            return NULL;
        }
        memcpy(l->raw, text, len);
        l->raw[len] = '\0';
        l->number = ++number;
        const char *err = classify(l, section);
        if (!l->section) {
            kk_ini_free(ini);
            return NULL;
        }
        if (err && warn)
            warn(ud, number, err);
        if (l->kind == LINE_SECTION)
            section = l->section;
        text += n;
        if (*text == '\n')
            text++;
    }
    return ini;
}

static bool is_key(const line *l, const char *section, const char *key)
{
    return l->kind == LINE_KEY && strcasecmp(l->section, section) == 0 &&
           strcasecmp(l->key, key) == 0;
}

static int find_key(const kk_ini *ini, const char *section, const char *key)
{
    for (int i = ini->count - 1; i >= 0; i--)
        if (is_key(&ini->lines[i], section, key))
            return i;
    return -1;
}

const char *kk_ini_get(const kk_ini *ini, const char *section, const char *key)
{
    int i = find_key(ini, section, key);
    return i < 0 ? NULL : ini->lines[i].value;
}

int kk_ini_line(const kk_ini *ini, const char *section, const char *key)
{
    int i = find_key(ini, section, key);
    return i < 0 ? 0 : ini->lines[i].number;
}

bool kk_ini_next(const kk_ini *ini, int *i, const char **section,
                 const char **key, const char **value, int *line_no)
{
    for (; *i < ini->count; (*i)++) {
        const line *l = &ini->lines[*i];
        if (l->kind != LINE_KEY)
            continue;
        *section = l->section;
        *key = l->key;
        *value = l->value;
        if (line_no)
            *line_no = l->number;
        (*i)++;
        return true;
    }
    return false;
}

static bool seen_before(const kk_ini *ini, int at, const char *section)
{
    for (int k = 0; k < at; k++)
        if (ini->lines[k].kind == LINE_SECTION &&
            strcasecmp(ini->lines[k].section, section) == 0)
            return true;
    return false;
}

bool kk_ini_next_section(const kk_ini *ini, int *i, const char **section)
{
    for (; *i < ini->count; (*i)++) {
        const line *l = &ini->lines[*i];
        if (l->kind != LINE_SECTION || seen_before(ini, *i, l->section))
            continue;
        *section = l->section;
        (*i)++;
        return true;
    }
    return false;
}

static char *format_key(const char *key, const char *value)
{
    size_t n = strlen(key) + strlen(value) + 4;
    char *s = malloc(n);
    if (s)
        snprintf(s, n, "%s = %s", key, value);
    return s;
}

/* Index after the last line of section that is a header or a key, or -1. */
static int section_end(const kk_ini *ini, const char *section)
{
    int end = -1;
    for (int i = 0; i < ini->count; i++) {
        const line *l = &ini->lines[i];
        if (l->kind != LINE_OTHER && strcasecmp(l->section, section) == 0)
            end = i + 1;
    }
    return end;
}

int kk_ini_set(kk_ini *ini, const char *section, const char *key,
               const char *value)
{
    char *raw = format_key(key, value);
    char *v = strdup(value);
    if (!raw || !v) {
        free(raw);
        free(v);
        return -1;
    }
    int i = find_key(ini, section, key);
    if (i >= 0) {
        line *l = &ini->lines[i];
        free(l->raw);
        free(l->value);
        l->raw = raw;
        l->value = v;
        return 0;
    }

    int at = section_end(ini, section);
    if (at < 0 && section[0]) {
        /* New section at the end, after a blank line. */
        at = ini->count;
        if (at > 0 && ini->lines[at - 1].raw[0]) {
            line *blank = insert_line(ini, at);
            if (!blank)
                goto fail;
            blank->raw = strdup("");
            blank->section = strdup(ini->lines[at - 1].section);
            at++;
        }
        size_t n = strlen(section) + 3;
        line *h = insert_line(ini, at);
        if (!h)
            goto fail;
        h->kind = LINE_SECTION;
        h->raw = malloc(n);
        h->section = strdup(section);
        if (h->raw)
            snprintf(h->raw, n, "[%s]", section);
        at++;
    } else if (at < 0) {
        at = 0; /* keys without a section go before the first header */
    }
    line *l = insert_line(ini, at);
    if (!l)
        goto fail;
    l->kind = LINE_KEY;
    l->raw = raw;
    l->value = v;
    l->section = strdup(section);
    l->key = strdup(key);
    return 0;
fail:
    free(raw);
    free(v);
    return -1;
}

void kk_ini_unset(kk_ini *ini, const char *section, const char *key)
{
    for (int i = ini->count - 1; i >= 0; i--)
        if (is_key(&ini->lines[i], section, key))
            remove_line(ini, i);
}

void kk_ini_remove_section(kk_ini *ini, const char *section)
{
    if (!section[0])
        return;
    for (;;) {
        int start = -1;
        for (int i = 0; i < ini->count && start < 0; i++)
            if (ini->lines[i].kind == LINE_SECTION &&
                strcasecmp(ini->lines[i].section, section) == 0)
                start = i;
        if (start < 0)
            return;
        /* Up to the last key of this block; comments after it most likely
         * introduce the next section, so they stay. */
        int end = start + 1;
        for (int i = start + 1; i < ini->count; i++) {
            if (ini->lines[i].kind == LINE_SECTION)
                break;
            if (ini->lines[i].kind == LINE_KEY)
                end = i + 1;
        }
        while (end > start)
            remove_line(ini, --end);
        /* Don't leave two blank lines where the section was. */
        if (start > 0 && start < ini->count && !ini->lines[start].raw[0] &&
            !ini->lines[start - 1].raw[0])
            remove_line(ini, start);
    }
}

char *kk_ini_dump(const kk_ini *ini)
{
    size_t n = 1;
    for (int i = 0; i < ini->count; i++)
        n += strlen(ini->lines[i].raw) + 1;
    char *s = malloc(n), *p = s;
    if (!s)
        return NULL;
    for (int i = 0; i < ini->count; i++) {
        size_t len = strlen(ini->lines[i].raw);
        memcpy(p, ini->lines[i].raw, len);
        p += len;
        *p++ = '\n';
    }
    *p = '\0';
    return s;
}

int kk_ini_save(const kk_ini *ini, const char *path)
{
    char tmp[KK_PATH_MAX];
    char *text = kk_ini_dump(ini);
    if (!text || !kk_pathf(tmp, sizeof tmp, "%s.tmp", path)) {
        free(text);
        errno = ENOMEM;
        return -1;
    }
    kk_make_parent_dirs(path);
    FILE *f = fopen(tmp, "w");
    if (!f) {
        free(text);
        return -1;
    }
    bool ok = fputs(text, f) >= 0 && fflush(f) == 0;
    ok = fclose(f) == 0 && ok;
    free(text);
    if (!ok || rename(tmp, path) < 0) {
        int e = errno;
        remove(tmp);
        errno = e;
        return -1;
    }
    return 0;
}
