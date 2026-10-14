/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "library.h"

#include <ctype.h>
#include <malloc.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "log.h"
#include "util.h"

bool kk_library_own_dir(char *out, size_t size)
{
    const char *xdg = getenv("XDG_DATA_HOME");
    if (xdg && xdg[0] == '/')
        return kk_pathf(out, size, "%s/kikarinhas/avatars", xdg);
    const char *home = getenv("HOME");
    return home && kk_pathf(out, size, "%s/.local/share/kikarinhas/avatars", home);
}

static bool expand(char *out, size_t size, const char *path)
{
    const char *home = getenv("HOME");
    if (strncmp(path, "~/", 2) == 0 && home)
        return kk_pathf(out, size, "%s%s", home, path + 1);
    return kk_pathf(out, size, "%s", path);
}

int kk_library_each_dir(const char *sa_dir, const char *paths,
                        void (*fn)(void *ud, const char *dir, bool is_sa),
                        void *ud)
{
    int n = 0;
    char dir[KK_PATH_MAX];
    if (kk_library_own_dir(dir, sizeof dir) && kk_dir_exists(dir)) {
        fn(ud, dir, false);
        n++;
    }
    /* Comma separated, trimmed: folder names may have spaces. */
    for (const char *p = paths ? paths : ""; *p;) {
        size_t len = strcspn(p, ",");
        const char *s = p;
        size_t k = len;
        while (k && isspace((unsigned char)*s)) {
            s++;
            k--;
        }
        while (k && isspace((unsigned char)s[k - 1]))
            k--;
        char item[KK_PATH_MAX];
        if (k && k < sizeof item) {
            memcpy(item, s, k);
            item[k] = '\0';
            if (expand(dir, sizeof dir, item)) {
                fn(ud, dir, false);
                n++;
            }
        }
        p += len;
        if (*p == ',')
            p++;
    }
    if (sa_dir && !sa_dir[0])
        return n; /* off */
    if (sa_dir ? expand(dir, sizeof dir, sa_dir) : kk_sa_find_data_dir(dir, sizeof dir)) {
        fn(ud, dir, true);
        n++;
    }
    return n;
}

#ifndef KK_DATADIR
#define KK_DATADIR "/usr/share"
#endif

#define MAX_SYSTEM_DIRS 16

typedef struct {
    char dirs[MAX_SYSTEM_DIRS][KK_PATH_MAX];
    int n;
} system_dirs;

/* Remembers path if it is a folder we don't have yet (compared by its real
 * path, so ".." and symlinks don't make repeats). */
static void add_system_dir(system_dirs *sd, const char *path)
{
    char real[KK_PATH_MAX];
    if (sd->n == MAX_SYSTEM_DIRS || !kk_dir_exists(path) || !realpath(path, real))
        return;
    for (int i = 0; i < sd->n; i++)
        if (strcmp(sd->dirs[i], real) == 0)
            return;
    kk_pathf(sd->dirs[sd->n++], sizeof sd->dirs[0], "%s", real);
}

int kk_library_each_system_dir(void (*fn)(void *ud, const char *dir), void *ud)
{
    system_dirs sd = {.n = 0};
    char path[KK_PATH_MAX], exe[KK_PATH_MAX];
    ssize_t n = readlink("/proc/self/exe", exe, sizeof exe - 1);
    if (n > 0) {
        exe[n] = '\0';
        char *slash = strrchr(exe, '/');
        if (slash) {
            *slash = '\0';
            if (kk_pathf(path, sizeof path, "%s/../share/kikarinhas/avatars", exe))
                add_system_dir(&sd, path);
            if (kk_pathf(path, sizeof path, "%s/../data/avatars", exe))
                add_system_dir(&sd, path);
            if (kk_pathf(path, sizeof path, "%s/../../data/avatars", exe))
                add_system_dir(&sd, path);
        }
    }
    const char *xdg = getenv("XDG_DATA_DIRS");
    if (!xdg || !xdg[0])
        xdg = "/usr/local/share:/usr/share";
    for (const char *p = xdg; *p;) {
        size_t len = strcspn(p, ":");
        if (len && p[0] == '/' && len < sizeof path - 32 &&
            kk_pathf(path, sizeof path, "%.*s/kikarinhas/avatars", (int)len, p))
            add_system_dir(&sd, path);
        p += len;
        if (*p == ':')
            p++;
    }
    if (kk_pathf(path, sizeof path, "%s/kikarinhas/avatars", KK_DATADIR))
        add_system_dir(&sd, path);
    for (int i = 0; i < sd.n; i++)
        fn(ud, sd.dirs[i]);
    return sd.n;
}

typedef struct {
    kk_sa_library *lib;
    char *sa_used;
    size_t size;
    bool verbose;
} load_ctx;

static void load_dir(load_ctx *c, const char *dir, bool is_sa, bool system)
{
    kk_sa_library one;
    if (kk_sa_load_dir(&one, dir) < 0) {
        kk_log_warn("nenhum avatar em %s (falta streamavatars_json.txt ou data.txt)", dir);
        kk_sa_free(&one);
        return;
    }
    int found = one.count;
    for (int i = 0; system && i < one.count; i++)
        one.avatars[i].system = true;
    int added = kk_sa_merge(c->lib, &one);
    if (is_sa)
        snprintf(c->sa_used, c->size, "%s", dir);
    const char *what = is_sa ? "do Stream Avatars " : system ? "do sistema " : "";
    if (c->verbose && added == found)
        kk_log_info("%d avatares %sem %s", added, what, dir);
    else if (c->verbose)
        kk_log_info("%d avatares %sem %s (%d com nome repetido ficaram de fora)", added, what,
                    dir, found - added);
}

static void load_one(void *ud, const char *dir, bool is_sa)
{
    load_dir(ud, dir, is_sa, false);
}

int kk_library_load_with_system(kk_sa_library *lib, const char *sa_dir,
                                const char *paths, const char *const *system,
                                int n_system, char *sa_used, size_t size,
                                bool verbose)
{
    memset(lib, 0, sizeof *lib);
    sa_used[0] = '\0';
    load_ctx c = {.lib = lib, .sa_used = sa_used, .size = size, .verbose = verbose};
    kk_library_each_dir(sa_dir, paths, load_one, &c);
    for (int i = 0; i < n_system; i++)
        load_dir(&c, system[i], false, true);
    malloc_trim(0);
    return lib->count > 0 ? 0 : -1;
}

static void collect_dir(void *ud, const char *dir)
{
    system_dirs *sd = ud;
    if (sd->n < MAX_SYSTEM_DIRS)
        kk_pathf(sd->dirs[sd->n++], sizeof sd->dirs[0], "%s", dir);
}

int kk_library_load(kk_sa_library *lib, const char *sa_dir, const char *paths,
                    char *sa_used, size_t size, bool verbose)
{
    system_dirs sd = {.n = 0};
    kk_library_each_system_dir(collect_dir, &sd);
    const char *list[MAX_SYSTEM_DIRS];
    for (int i = 0; i < sd.n; i++)
        list[i] = sd.dirs[i];
    return kk_library_load_with_system(lib, sa_dir, paths, list, sd.n, sa_used, size, verbose);
}
