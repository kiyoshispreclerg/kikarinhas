/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "sa.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "cJSON.h"
#include "log.h"

#define SA_APP_ID "665300"
#define SA_PREFIX_DATA                                                         \
    "steamapps/compatdata/" SA_APP_ID "/pfx/drive_c/users/steamuser/"          \
    "AppData/LocalLow/ClonzeWork/Streaming Avatars/data"
#define SA_JSON "streamavatars_json.txt"

/* ---- locating the data folder ------------------------------------------- */

static bool try_library(const char *lib, char *out, size_t size)
{
    char json[KK_PATH_MAX];
    return kk_pathf(out, size, "%s/" SA_PREFIX_DATA, lib) &&
           kk_pathf(json, sizeof json, "%s/" SA_JSON, out) &&
           kk_file_exists(json);
}

/* libraryfolders.vdf lines look like:  "path"  "/media/x/SteamLibrary" */
static bool scan_vdf(const char *vdf, char *out, size_t size)
{
    char *text = kk_read_file(vdf, NULL);
    if (!text)
        return false;

    bool found = false;
    for (char *line = strtok(text, "\n"); line && !found;
         line = strtok(NULL, "\n")) {
        char *key = strstr(line, "\"path\"");
        if (!key)
            continue;
        char *start = strchr(key + 6, '"');
        char *end = start ? strchr(start + 1, '"') : NULL;
        if (!end)
            continue;
        *end = '\0';
        found = try_library(start + 1, out, size);
    }
    free(text);
    return found;
}

bool kk_sa_find_data_dir(char *out, size_t size)
{
    static const char *const roots[] = {
        ".steam/steam",
        ".local/share/Steam",
        ".var/app/com.valvesoftware.Steam/.local/share/Steam",
    };
    const char *home = getenv("HOME");
    if (!home)
        return false;

    for (size_t i = 0; i < sizeof roots / sizeof roots[0]; i++) {
        char root[KK_PATH_MAX], vdf[KK_PATH_MAX];
        if (!kk_pathf(root, sizeof root, "%s/%s", home, roots[i]))
            continue;
        if (try_library(root, out, size))
            return true;
        if (kk_pathf(vdf, sizeof vdf, "%s/steamapps/libraryfolders.vdf", root) &&
            scan_vdf(vdf, out, size))
            return true;
    }
    return false;
}

/* ---- parsing ------------------------------------------------------------- */

static char *lowercase_dup(const char *s)
{
    char *d = strdup(s);
    if (d)
        for (char *p = d; *p; p++)
            *p = (char)tolower((unsigned char)*p);
    return d;
}

static double num(const cJSON *obj, const char *key, double fallback)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(obj, key);
    return cJSON_IsNumber(v) ? v->valuedouble : fallback;
}

static bool flag(const cJSON *obj, const char *key)
{
    return cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(obj, key));
}

/* Row of an animation slot, or -1 for names without a row (the legacy
 * "dance", "hug", ... entries, always empty). */
static int anim_row(const char *name)
{
    static const char *const base[] = {"idle", "walk", "sit", "stand", "jump"};
    for (int i = 0; i < 5; i++)
        if (strcmp(name, base[i]) == 0)
            return i;
    if (strncmp(name, "custom", 6) == 0) {
        char *end;
        long n = strtol(name + 6, &end, 10);
        if (*end == '\0' && n >= 1 && n <= KK_ANIM_MAX - KK_ANIM_CUSTOM1)
            return KK_ANIM_CUSTOM1 + (int)n - 1;
    }
    return -1;
}

static void parse_anims(kk_sa_avatar *a, const cJSON *anims)
{
    const cJSON *it;
    cJSON_ArrayForEach(it, anims)
    {
        int row = anim_row(it->string);
        if (row < 0)
            continue;
        int frames = cJSON_GetArraySize(
            cJSON_GetObjectItemCaseSensitive(it, "frameData"));
        if (frames <= 0)
            continue;

        kk_sa_anim *an = &a->anims[row];
        an->frames = frames;
        an->fps = num(it, "framesPerSecond", 9.0);
        if (an->fps <= 0.0)
            an->fps = 9.0;
        an->loops = flag(it, "animationLoops");
        an->loop_count = (int)num(it, "loopCount", 1);
        if (an->loop_count < 1)
            an->loop_count = 1;
        an->hold_last = num(it, "holdLastFrame", 0.0);
        const cJSON *cn = cJSON_GetObjectItemCaseSensitive(it, "customName");
        if (row >= KK_ANIM_CUSTOM1 && cJSON_IsString(cn) && cn->valuestring[0])
            an->custom_name = strdup(cn->valuestring);
        if (row + 1 > a->n_anims)
            a->n_anims = row + 1;
    }
}

/* Lowercased file names of <dir>, to resolve "ABRA.png" for key "abra". */
typedef struct {
    char **names;
    int count;
} file_list;

static void list_dir(file_list *fl, const char *dir)
{
    fl->names = NULL;
    fl->count = 0;
    DIR *d = opendir(dir);
    if (!d)
        return;
    int cap = 0;
    struct dirent *e;
    while ((e = readdir(d))) {
        if (e->d_name[0] == '.')
            continue;
        if (fl->count == cap) {
            cap = cap ? cap * 2 : 256;
            char **nn = realloc(fl->names, (size_t)cap * sizeof *nn);
            if (!nn)
                break;
            fl->names = nn;
        }
        fl->names[fl->count++] = strdup(e->d_name);
    }
    closedir(d);
}

static void free_list(file_list *fl)
{
    for (int i = 0; i < fl->count; i++)
        free(fl->names[i]);
    free(fl->names);
}

static const char *find_png(const file_list *fl, const char *base)
{
    size_t n = strlen(base);
    for (int i = 0; i < fl->count; i++) {
        const char *f = fl->names[i];
        if (strncasecmp(f, base, n) == 0 && strcasecmp(f + n, ".png") == 0)
            return f;
    }
    return NULL;
}

static bool parse_avatar(kk_sa_avatar *a, const cJSON *obj, const char *dir,
                         const file_list *pngs)
{
    memset(a, 0, sizeof *a);
    const cJSON *name = cJSON_GetObjectItemCaseSensitive(obj, "name");
    a->key = lowercase_dup(obj->string);
    a->name = strdup(cJSON_IsString(name) ? name->valuestring : obj->string);
    if (!a->key || !a->name)
        return false;

    a->frame_w = (int)num(obj, "width", 0);
    a->frame_h = (int)num(obj, "height", 0);
    a->ppu = num(obj, "pixelsPerUnit", 1.0);
    if (a->ppu <= 0.0)
        a->ppu = 1.0;
    a->smooth = flag(obj, "bilinearFilter");
    a->move_speed = num(obj, "moveSpeed", 1.0);
    if (a->move_speed <= 0.0)
        a->move_speed = 1.0;
    parse_anims(a, cJSON_GetObjectItemCaseSensitive(obj, "animationData"));

    const char *file = find_png(pngs, a->name);
    if (!file)
        file = find_png(pngs, a->key);
    char path[KK_PATH_MAX];
    if (file && kk_pathf(path, sizeof path, "%s/%s", dir, file))
        a->image = strdup(path);
    return true;
}

static void free_avatar(kk_sa_avatar *a)
{
    free(a->key);
    free(a->name);
    free(a->image);
    for (int i = 0; i < KK_ANIM_MAX; i++)
        free(a->anims[i].custom_name);
}

static int compare_key(const void *x, const void *y)
{
    const kk_sa_avatar *a = x, *b = y;
    return strcmp(a->key, b->key);
}

int kk_sa_load(kk_sa_library *lib, const char *data_dir)
{
    lib->avatars = NULL;
    lib->count = 0;

    char path[KK_PATH_MAX];
    size_t len;
    char *text = kk_pathf(path, sizeof path, "%s/" SA_JSON, data_dir)
                     ? kk_read_file(path, &len)
                     : NULL;
    if (!text) {
        kk_log_error("não consegui ler %s: %s", path, strerror(errno));
        return -1;
    }
    /* UTF-8 BOM */
    const char *json = text;
    if (len >= 3 && memcmp(json, "\xEF\xBB\xBF", 3) == 0)
        json += 3;

    cJSON *root = cJSON_Parse(json);
    free(text);
    if (!root) {
        kk_log_error("%s: JSON inválido", path);
        return -1;
    }

    const cJSON *avatars = cJSON_GetObjectItemCaseSensitive(root, "avatarData");
    int n = cJSON_GetArraySize(avatars);
    lib->avatars = calloc(n > 0 ? (size_t)n : 1, sizeof *lib->avatars);
    if (!lib->avatars) {
        cJSON_Delete(root);
        return -1;
    }

    char img_dir[KK_PATH_MAX];
    file_list pngs = {0};
    if (kk_pathf(img_dir, sizeof img_dir, "%s/avatars", data_dir))
        list_dir(&pngs, img_dir);

    const cJSON *it;
    cJSON_ArrayForEach(it, avatars)
    {
        kk_sa_avatar *a = &lib->avatars[lib->count];
        if (!cJSON_IsObject(it) || !parse_avatar(a, it, img_dir, &pngs)) {
            free_avatar(a);
            continue;
        }
        lib->count++;
    }
    free_list(&pngs);
    cJSON_Delete(root);
    /* The parse tree of the whole file (previews in base64 included) is
     * gone; give that heap back instead of keeping it for the session. */
    malloc_trim(0);

    qsort(lib->avatars, (size_t)lib->count, sizeof *lib->avatars, compare_key);
    return 0;
}

void kk_sa_free(kk_sa_library *lib)
{
    for (int i = 0; i < lib->count; i++)
        free_avatar(&lib->avatars[i]);
    free(lib->avatars);
    lib->avatars = NULL;
    lib->count = 0;
}

const kk_sa_avatar *kk_sa_find(const kk_sa_library *lib, const char *name)
{
    for (int i = 0; i < lib->count; i++)
        if (strcasecmp(lib->avatars[i].key, name) == 0 ||
            strcasecmp(lib->avatars[i].name, name) == 0)
            return &lib->avatars[i];
    return NULL;
}
