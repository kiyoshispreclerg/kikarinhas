/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "users.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "log.h"
#include "util.h"

static const char *const field_names[] = {"avatar", "palette", "gear"};
#define N_FIELDS 3

typedef struct {
    char *key;
    char *fields[N_FIELDS];
    char *extra; /* unknown "k=v" fields, tab separated, kept verbatim */
} record;

struct kk_users {
    char *path;
    record *recs;
    int count, cap;
    bool dirty;
};

static record *find(const kk_users *u, const char *key)
{
    for (int i = 0; i < u->count; i++)
        if (strcmp(u->recs[i].key, key) == 0)
            return &u->recs[i];
    return NULL;
}

static record *add(kk_users *u, const char *key)
{
    if (u->count == u->cap) {
        int cap = u->cap ? u->cap * 2 : 64;
        record *nr = realloc(u->recs, (size_t)cap * sizeof *nr);
        if (!nr)
            return NULL;
        u->recs = nr;
        u->cap = cap;
    }
    record *r = &u->recs[u->count];
    memset(r, 0, sizeof *r);
    r->key = strdup(key);
    if (!r->key)
        return NULL;
    u->count++;
    return r;
}

/* Tabs and newlines would break the format: never store them. */
static char *clean_dup(const char *s)
{
    char *d = strdup(s);
    if (d)
        for (char *p = d; *p; p++)
            if (*p == '\t' || *p == '\n' || *p == '\r')
                *p = ' ';
    return d;
}

static void append_extra(record *r, const char *kv)
{
    size_t old = r->extra ? strlen(r->extra) : 0;
    char *ne = realloc(r->extra, old + strlen(kv) + 2);
    if (!ne)
        return;
    if (old)
        ne[old++] = '\t';
    memcpy(ne + old, kv, strlen(kv) + 1);
    r->extra = ne;
}

static void parse_line(kk_users *u, char *line)
{
    char *save;
    char *key = strtok_r(line, "\t", &save);
    if (!key || !key[0] || key[0] == '#')
        return;
    record *r = find(u, key);
    if (!r && !(r = add(u, key)))
        return;
    for (char *kv; (kv = strtok_r(NULL, "\t", &save));) {
        char *eq = strchr(kv, '=');
        if (!eq)
            continue;
        *eq = '\0';
        int f = -1;
        for (int i = 0; i < N_FIELDS; i++)
            if (strcmp(kv, field_names[i]) == 0)
                f = i;
        if (f >= 0) {
            free(r->fields[f]);
            r->fields[f] = eq[1] ? strdup(eq + 1) : NULL;
        } else {
            *eq = '=';
            append_extra(r, kv);
        }
    }
}

kk_users *kk_users_open(const char *path)
{
    kk_users *u = calloc(1, sizeof *u);
    if (!u || !(u->path = strdup(path))) {
        free(u);
        return NULL;
    }
    char *text = kk_read_file(path, NULL);
    if (!text) {
        if (errno != ENOENT)
            kk_log_warn("não consegui ler %s: %s", path, strerror(errno));
        return u;
    }
    char *save;
    for (char *line = strtok_r(text, "\n", &save); line;
         line = strtok_r(NULL, "\n", &save)) {
        size_t n = strlen(line);
        if (n && line[n - 1] == '\r')
            line[n - 1] = '\0';
        parse_line(u, line);
    }
    free(text);
    return u;
}

void kk_users_free(kk_users *u)
{
    if (!u)
        return;
    for (int i = 0; i < u->count; i++) {
        free(u->recs[i].key);
        for (int f = 0; f < N_FIELDS; f++)
            free(u->recs[i].fields[f]);
        free(u->recs[i].extra);
    }
    free(u->recs);
    free(u->path);
    free(u);
}

bool kk_users_exists(const kk_users *u, const char *key)
{
    return find(u, key) != NULL;
}

const char *kk_users_get(const kk_users *u, const char *key, kk_user_field f)
{
    const record *r = find(u, key);
    return r ? r->fields[f] : NULL;
}

void kk_users_set(kk_users *u, const char *key, kk_user_field f,
                  const char *value)
{
    record *r = find(u, key);
    if (!r && !(r = add(u, key)))
        return;
    const char *old = r->fields[f];
    bool empty = !value || !value[0];
    if ((empty && !old) || (!empty && old && strcmp(old, value) == 0))
        return;
    free(r->fields[f]);
    r->fields[f] = empty ? NULL : clean_dup(value);
    u->dirty = true;
}

bool kk_users_dirty(const kk_users *u)
{
    return u->dirty;
}

int kk_users_count(const kk_users *u)
{
    return u->count;
}

int kk_users_save(kk_users *u)
{
    if (!u->dirty)
        return 0;
    char tmp[KK_PATH_MAX];
    if (!kk_pathf(tmp, sizeof tmp, "%s.tmp", u->path))
        return -1;
    kk_make_parent_dirs(u->path);
    FILE *f = fopen(tmp, "w");
    if (!f) {
        kk_log_warn("não consegui gravar %s: %s", tmp, strerror(errno));
        return -1;
    }
    fputs("# Kikarinhas: avatar, paleta e acessórios de cada pessoa do chat\n", f);
    for (int i = 0; i < u->count; i++) {
        const record *r = &u->recs[i];
        bool any = r->extra != NULL;
        for (int k = 0; k < N_FIELDS; k++)
            any = any || r->fields[k];
        if (!any)
            continue;
        fputs(r->key, f);
        for (int k = 0; k < N_FIELDS; k++)
            if (r->fields[k])
                fprintf(f, "\t%s=%s", field_names[k], r->fields[k]);
        if (r->extra)
            fprintf(f, "\t%s", r->extra);
        fputc('\n', f);
    }
    bool ok = fflush(f) == 0 && !ferror(f);
    ok = fclose(f) == 0 && ok;
    if (!ok || rename(tmp, u->path) < 0) {
        kk_log_warn("não consegui gravar %s: %s", u->path, strerror(errno));
        remove(tmp);
        return -1;
    }
    u->dirty = false;
    return 0;
}

bool kk_users_default_path(char *out, size_t size)
{
    const char *xdg = getenv("XDG_DATA_HOME");
    if (xdg && xdg[0] == '/')
        return kk_pathf(out, size, "%s/kikarinhas/users.tsv", xdg);
    const char *home = getenv("HOME");
    return home && kk_pathf(out, size, "%s/.local/share/kikarinhas/users.tsv", home);
}
