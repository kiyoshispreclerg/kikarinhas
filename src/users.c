/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "users.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "log.h"
#include "util.h"

static const char *const field_names[] = {"avatar", "palette", "gear",    "name",  "first",
                                          "last",   "role",    "trusted", "points",
                                          "live_url", "live_title"};
#define N_FIELDS KK_USER_N_FIELDS
_Static_assert(sizeof field_names / sizeof field_names[0] == N_FIELDS,
               "one name per kk_user_field");

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

/* A people file is usable if it is not empty and has no NUL bytes; as a
 * backup it must also end in a newline, or the write was cut short. */
static bool users_usable(const char *text, size_t len)
{
    return len > 0 && strlen(text) == len;
}

static bool users_clean(const char *text, size_t len)
{
    return users_usable(text, len) && text[len - 1] == '\n';
}

/* Adds the people of the file at path; false if it cannot be read. */
static bool load_file(kk_users *u, const char *path, bool use_backup)
{
    char *text = use_backup ? kk_read_file_or_backup(path, users_usable, NULL)
                            : kk_read_file(path, NULL);
    if (!text)
        return false;
    char *save;
    for (char *line = strtok_r(text, "\n", &save); line;
         line = strtok_r(NULL, "\n", &save)) {
        size_t n = strlen(line);
        if (n && line[n - 1] == '\r')
            line[n - 1] = '\0';
        parse_line(u, line);
    }
    free(text);
    return true;
}

kk_users *kk_users_open(const char *path)
{
    kk_users *u = calloc(1, sizeof *u);
    if (!u || !(u->path = strdup(path))) {
        free(u);
        return NULL;
    }
    if (!load_file(u, path, true) && errno != ENOENT)
        kk_log_warn("não consegui ler %s: %s", path, strerror(errno));
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

bool kk_users_remove(kk_users *u, const char *key)
{
    record *r = find(u, key);
    if (!r)
        return false;
    free(r->key);
    for (int f = 0; f < N_FIELDS; f++)
        free(r->fields[f]);
    free(r->extra);
    memmove(r, r + 1, (size_t)(u->count - 1 - (int)(r - u->recs)) * sizeof *r);
    u->count--;
    u->dirty = true;
    return true;
}

bool kk_users_dirty(const kk_users *u)
{
    return u->dirty;
}

int kk_users_count(const kk_users *u)
{
    return u->count;
}

/* Everyone, atomically (temp file + fsync + rename) to path; with backup, the
 * previous file is first copied to path.backup. */
static int write_file(const kk_users *u, const char *path, bool backup)
{
    char tmp[KK_PATH_MAX];
    if (!kk_pathf(tmp, sizeof tmp, "%s.tmp", path))
        return -1;
    kk_make_parent_dirs(path);
    if (backup)
        kk_backup_file(path, users_clean);
    FILE *f = fopen(tmp, "w");
    if (!f) {
        kk_log_warn("não consegui gravar %s: %s", tmp, strerror(errno));
        return -1;
    }
    fputs("# Kikarinhas: quem já passou pelo chat (avatar, paleta, acessórios, "
          "nome, primeira e última vez, nível, pontos)\n", f);
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
    bool ok = kk_flush_sync(f) == 0;
    ok = fclose(f) == 0 && ok;
    if (!ok || rename(tmp, path) < 0) {
        kk_log_warn("não consegui gravar %s: %s", path, strerror(errno));
        remove(tmp);
        return -1;
    }
    return 0;
}

int kk_users_save(kk_users *u)
{
    if (!u->dirty)
        return 0;
    if (write_file(u, u->path, true) < 0)
        return -1;
    u->dirty = false;
    return 0;
}

int kk_users_export(const kk_users *u, const char *path)
{
    return write_file(u, path, false);
}

static void clear_record(record *r)
{
    for (int f = 0; f < N_FIELDS; f++) {
        free(r->fields[f]);
        r->fields[f] = NULL;
    }
    free(r->extra);
    r->extra = NULL;
}

int kk_users_import(kk_users *u, const char *path, bool overwrite, kk_users_import_stats *st)
{
    kk_users_import_stats none;
    if (!st)
        st = &none;
    *st = (kk_users_import_stats){0};
    kk_users *src = calloc(1, sizeof *src);
    if (!src)
        return -1;
    if (!load_file(src, path, false)) {
        kk_users_free(src);
        return -1;
    }
    for (int i = 0; i < src->count; i++) {
        record *from = &src->recs[i];
        record *to = find(u, from->key);
        bool existed = to != NULL;
        if (existed && !overwrite) {
            st->skipped++;
            continue;
        }
        if (existed)
            clear_record(to);
        else if (!(to = add(u, from->key)))
            continue;
        /* the record moves over, unknown fields included */
        memcpy(to->fields, from->fields, sizeof to->fields);
        to->extra = from->extra;
        memset(from->fields, 0, sizeof from->fields);
        from->extra = NULL;
        u->dirty = true;
        if (existed)
            st->replaced++;
        else
            st->added++;
    }
    kk_users_free(src);
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

const char *kk_users_key(const kk_users *u, int i)
{
    return i >= 0 && i < u->count ? u->recs[i].key : NULL;
}

void kk_users_seen(kk_users *u, const char *key, const char *name, long long now,
                   const char *live_url, const char *live_title)
{
    char t[32];
    snprintf(t, sizeof t, "%lld", now);
    if (name && name[0])
        kk_users_set(u, key, KK_USER_NAME, name);
    if (!kk_users_get(u, key, KK_USER_FIRST))
        kk_users_set(u, key, KK_USER_FIRST, t);
    kk_users_set(u, key, KK_USER_LAST, t);
    if (live_url && live_url[0] && !kk_users_get(u, key, KK_USER_LIVE_URL)) {
        kk_users_set(u, key, KK_USER_LIVE_URL, live_url);
        kk_users_set(u, key, KK_USER_LIVE_TITLE, live_title);
    } else if (live_title && live_title[0] && !kk_users_get(u, key, KK_USER_LIVE_TITLE) &&
               kk_users_get(u, key, KK_USER_LIVE_URL) &&
               live_url && strcmp(live_url, kk_users_get(u, key, KK_USER_LIVE_URL)) == 0) {
        /* the title can show up after the first message of that same live */
        kk_users_set(u, key, KK_USER_LIVE_TITLE, live_title);
    }
}

bool kk_users_trusted(const kk_users *u, const char *key)
{
    const char *v = kk_users_get(u, key, KK_USER_TRUSTED);
    return v && strcmp(v, "yes") == 0;
}
