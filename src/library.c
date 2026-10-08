/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "library.h"

#include <ctype.h>
#include <malloc.h>
#include <stdlib.h>
#include <string.h>

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

typedef struct {
    kk_sa_library *lib;
    char *sa_used;
    size_t size;
    bool verbose;
} load_ctx;

static void load_one(void *ud, const char *dir, bool is_sa)
{
    load_ctx *c = ud;
    kk_sa_library one;
    if (kk_sa_load_dir(&one, dir) < 0) {
        kk_log_warn("nenhum avatar em %s (falta streamavatars_json.txt ou data.txt)", dir);
        kk_sa_free(&one);
        return;
    }
    int found = one.count;
    int added = kk_sa_merge(c->lib, &one);
    if (is_sa)
        snprintf(c->sa_used, c->size, "%s", dir);
    if (c->verbose && added == found)
        kk_log_info("%d avatares %sem %s", added, is_sa ? "do Stream Avatars " : "", dir);
    else if (c->verbose)
        kk_log_info("%d avatares %sem %s (%d com nome repetido ficaram de fora)", added,
                    is_sa ? "do Stream Avatars " : "", dir, found - added);
}

int kk_library_load(kk_sa_library *lib, const char *sa_dir, const char *paths,
                    char *sa_used, size_t size, bool verbose)
{
    memset(lib, 0, sizeof *lib);
    sa_used[0] = '\0';
    load_ctx c = {.lib = lib, .sa_used = sa_used, .size = size, .verbose = verbose};
    kk_library_each_dir(sa_dir, paths, load_one, &c);
    malloc_trim(0);
    return lib->count > 0 ? 0 : -1;
}
