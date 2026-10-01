/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "sa.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <malloc.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "cJSON.h"
#include "json.h"
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

static int find_set(const kk_sa_library *lib, const char *name)
{
    for (int i = 0; i < lib->n_sets; i++)
        if (strcasecmp(lib->sets[i].key, name) == 0)
            return i;
    return -1;
}

static int find_piece_in(const kk_sa_gear_set *set, const char *name)
{
    for (int i = 0; i < set->n_pieces; i++)
        if (strcasecmp(set->pieces[i].key, name) == 0)
            return i;
    return -1;
}

/* Colours are floats 0..1; palettes match exact 8-bit values. */
static uint32_t argb(const cJSON *c)
{
    double ch[4] = {num(c, "a", 1.0), num(c, "r", 0.0), num(c, "g", 0.0),
                    num(c, "b", 0.0)};
    uint32_t v = 0;
    for (int i = 0; i < 4; i++) {
        long b = lround(ch[i] * 255.0);
        v = v << 8 | (uint32_t)(b < 0 ? 0 : b > 255 ? 255 : b);
    }
    return v;
}

static uint32_t *parse_colors(const cJSON *arr, int *n)
{
    *n = cJSON_GetArraySize(arr);
    if (*n <= 0)
        return NULL;
    uint32_t *c = calloc((size_t)*n, sizeof *c);
    if (!c) {
        *n = 0;
        return NULL;
    }
    int i = 0;
    const cJSON *it;
    cJSON_ArrayForEach(it, arr) c[i++] = argb(it);
    return c;
}

static void parse_palettes(kk_sa_avatar *a, const cJSON *obj)
{
    a->main_colors = parse_colors(
        kk_json_path(obj, "mainPalette.colors"), &a->n_main_colors);
    const cJSON *pals = cJSON_GetObjectItemCaseSensitive(obj, "swappablePalettes");
    int n = cJSON_GetArraySize(pals);
    if (!a->main_colors || n <= 0)
        return;
    a->palettes = calloc((size_t)n, sizeof *a->palettes);
    if (!a->palettes)
        return;
    const cJSON *it;
    cJSON_ArrayForEach(it, pals)
    {
        kk_sa_palette *p = &a->palettes[a->n_palettes];
        p->key = lowercase_dup(it->string);
        p->colors = parse_colors(cJSON_GetObjectItemCaseSensitive(it, "colors"), &p->n);
        if (p->key && p->colors)
            a->n_palettes++;
        else {
            free(p->key);
            free(p->colors);
        }
    }
}

static void add_pivot(kk_sa_avatar *a, int *cap, kk_sa_pivot pv)
{
    if (a->n_pivots == *cap) {
        int nc = *cap ? *cap * 2 : 32;
        kk_sa_pivot *np = realloc(a->pivots, (size_t)nc * sizeof *np);
        if (!np)
            return;
        a->pivots = np;
        *cap = nc;
    }
    a->pivots[a->n_pivots++] = pv;
}

/* gearPivot/uniquePivot of every frame, kept only for known sets. */
static void parse_gear(kk_sa_avatar *a, const cJSON *obj,
                       const kk_sa_library *lib)
{
    const cJSON *can = cJSON_GetObjectItemCaseSensitive(obj, "CanUseGear");
    int n = cJSON_GetArraySize(can);
    if (n > 0 && (a->gear_sets = calloc((size_t)n, sizeof *a->gear_sets))) {
        const cJSON *it;
        cJSON_ArrayForEach(it, can)
        {
            int set = cJSON_IsString(it) ? find_set(lib, it->valuestring) : -1;
            if (set >= 0)
                a->gear_sets[a->n_gear_sets++] = (int16_t)set;
        }
    }
    if (a->n_gear_sets == 0)
        return;

    int cap = 0;
    const cJSON *anim;
    cJSON_ArrayForEach(anim, cJSON_GetObjectItemCaseSensitive(obj, "animationData"))
    {
        int row = anim_row(anim->string);
        if (row < 0)
            continue;
        int col = 0;
        const cJSON *frame;
        cJSON_ArrayForEach(frame, cJSON_GetObjectItemCaseSensitive(anim, "frameData"))
        {
            const cJSON *sp;
            cJSON_ArrayForEach(sp, cJSON_GetObjectItemCaseSensitive(frame, "gearPivot"))
            {
                int set = find_set(lib, sp->string);
                if (set >= 0 && (num(sp, "x", 0) != 0 || num(sp, "y", 0) != 0))
                    add_pivot(a, &cap,
                              (kk_sa_pivot){(uint8_t)row, (uint8_t)col, (int16_t)set,
                                            -1, (float)num(sp, "x", 0),
                                            (float)num(sp, "y", 0)});
            }
            const cJSON *us;
            cJSON_ArrayForEach(us, cJSON_GetObjectItemCaseSensitive(frame, "uniquePivot"))
            {
                int set = find_set(lib, us->string);
                if (set < 0)
                    continue;
                const cJSON *pp;
                cJSON_ArrayForEach(pp, us)
                {
                    int piece = find_piece_in(&lib->sets[set], pp->string);
                    if (piece >= 0)
                        add_pivot(a, &cap,
                                  (kk_sa_pivot){(uint8_t)row, (uint8_t)col,
                                                (int16_t)set, (int16_t)piece,
                                                (float)num(pp, "x", 0),
                                                (float)num(pp, "y", 0)});
                }
            }
            col++;
        }
    }
}

static bool parse_avatar(kk_sa_avatar *a, const cJSON *obj, const char *dir,
                         const file_list *pngs, const kk_sa_library *lib)
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
    parse_gear(a, obj, lib);
    parse_palettes(a, obj);

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
    free(a->gear_sets);
    free(a->pivots);
    free(a->main_colors);
    for (int i = 0; i < a->n_palettes; i++) {
        free(a->palettes[i].key);
        free(a->palettes[i].colors);
    }
    free(a->palettes);
}

/* "gear": { set: { globalZIndex, gearPiece: { piece: {...} } } } */
static void parse_sets(kk_sa_library *lib, const cJSON *gear,
                       const char *data_dir)
{
    int n = cJSON_GetArraySize(gear);
    if (n <= 0 || !(lib->sets = calloc((size_t)n, sizeof *lib->sets)))
        return;
    const cJSON *it;
    cJSON_ArrayForEach(it, gear)
    {
        kk_sa_gear_set *set = &lib->sets[lib->n_sets];
        set->key = lowercase_dup(it->string);
        const cJSON *pieces = cJSON_GetObjectItemCaseSensitive(it, "gearPiece");
        int np = cJSON_GetArraySize(pieces);
        if (!set->key || np <= 0 ||
            !(set->pieces = calloc((size_t)np, sizeof *set->pieces))) {
            free(set->key);
            continue;
        }
        double global_z = num(it, "globalZIndex", 1);

        char dir[KK_PATH_MAX];
        file_list pngs = {0};
        if (kk_pathf(dir, sizeof dir, "%s/gear/%s", data_dir, it->string))
            list_dir(&pngs, dir);

        const cJSON *pc;
        cJSON_ArrayForEach(pc, pieces)
        {
            kk_sa_piece *p = &set->pieces[set->n_pieces];
            p->key = lowercase_dup(pc->string);
            if (!p->key)
                continue;
            p->w = (int)num(pc, "width", 0);
            p->h = (int)num(pc, "height", 0);
            p->ppu = num(pc, "PPU", 1.0);
            if (p->ppu <= 0.0)
                p->ppu = 1.0;
            p->z = flag(pc, "isUniqueZindex") ? (int)num(pc, "zIndex", 1)
                                              : (int)global_z;
            p->animated = flag(pc, "isAnimated");
            p->aligned = flag(pc, "isAnimatedAligned");
            p->flips = flag(pc, "flipsWithAvatarX");
            p->fps = num(pc, "FPS", 9);
            if (p->fps <= 0.0)
                p->fps = 9;
            const char *file = find_png(&pngs, p->key);
            char path[KK_PATH_MAX];
            if (file && kk_pathf(path, sizeof path, "%s/%s", dir, file))
                p->image = strdup(path);
            p->id = lib->n_pieces++;
            set->n_pieces++;
        }
        free_list(&pngs);
        lib->n_sets++;
    }
}

static int compare_key(const void *x, const void *y)
{
    const kk_sa_avatar *a = x, *b = y;
    return strcmp(a->key, b->key);
}

/* Reads and parses streamavatars_json.txt (UTF-8 with BOM). */
static cJSON *read_json(const char *data_dir)
{
    char path[KK_PATH_MAX];
    size_t len;
    char *text = kk_pathf(path, sizeof path, "%s/" SA_JSON, data_dir)
                     ? kk_read_file(path, &len)
                     : NULL;
    if (!text) {
        kk_log_error("não consegui ler %s: %s", path, strerror(errno));
        return NULL;
    }
    const char *json = text;
    if (len >= 3 && memcmp(json, "\xEF\xBB\xBF", 3) == 0)
        json += 3;
    cJSON *root = cJSON_Parse(json);
    free(text);
    if (!root)
        kk_log_error("%s: JSON inválido", path);
    return root;
}

int kk_sa_parse(kk_sa_library *lib, const cJSON *root, const char *data_dir)
{
    memset(lib, 0, sizeof *lib);
    /* streamavatars_json.txt says "avatarData"; a pack's data.txt "avatar". */
    const cJSON *avatars = cJSON_GetObjectItemCaseSensitive(root, "avatarData");
    if (!avatars)
        avatars = cJSON_GetObjectItemCaseSensitive(root, "avatar");
    int n = cJSON_GetArraySize(avatars);
    lib->avatars = calloc(n > 0 ? (size_t)n : 1, sizeof *lib->avatars);
    if (!lib->avatars)
        return -1;

    parse_sets(lib, cJSON_GetObjectItemCaseSensitive(root, "gear"), data_dir);

    char img_dir[KK_PATH_MAX];
    file_list pngs = {0};
    if (kk_pathf(img_dir, sizeof img_dir, "%s/avatars", data_dir))
        list_dir(&pngs, img_dir);

    const cJSON *it;
    cJSON_ArrayForEach(it, avatars)
    {
        kk_sa_avatar *a = &lib->avatars[lib->count];
        if (!cJSON_IsObject(it) || !parse_avatar(a, it, img_dir, &pngs, lib)) {
            free_avatar(a);
            continue;
        }
        lib->count++;
    }
    free_list(&pngs);

    qsort(lib->avatars, (size_t)lib->count, sizeof *lib->avatars, compare_key);
    return 0;
}

int kk_sa_load(kk_sa_library *lib, const char *data_dir)
{
    memset(lib, 0, sizeof *lib);
    cJSON *root = read_json(data_dir);
    if (!root)
        return -1;
    int rc = kk_sa_parse(lib, root, data_dir);
    cJSON_Delete(root);
    /* The parse tree of the whole file (previews in base64 included) is
     * gone; give that heap back instead of keeping it for the session. */
    malloc_trim(0);
    return rc;
}

void kk_sa_free(kk_sa_library *lib)
{
    for (int i = 0; i < lib->count; i++)
        free_avatar(&lib->avatars[i]);
    free(lib->avatars);
    for (int i = 0; i < lib->n_sets; i++) {
        for (int k = 0; k < lib->sets[i].n_pieces; k++) {
            free(lib->sets[i].pieces[k].key);
            free(lib->sets[i].pieces[k].image);
        }
        free(lib->sets[i].pieces);
        free(lib->sets[i].key);
    }
    free(lib->sets);
    memset(lib, 0, sizeof *lib);
}

const kk_sa_avatar *kk_sa_find(const kk_sa_library *lib, const char *name)
{
    for (int i = 0; i < lib->count; i++)
        if (strcasecmp(lib->avatars[i].key, name) == 0 ||
            strcasecmp(lib->avatars[i].name, name) == 0)
            return &lib->avatars[i];
    return NULL;
}

const kk_sa_piece *kk_sa_find_piece(const kk_sa_library *lib,
                                    const kk_sa_avatar *av, const char *name,
                                    int *set)
{
    for (int i = 0; i < av->n_gear_sets; i++) {
        const kk_sa_gear_set *gs = &lib->sets[av->gear_sets[i]];
        int p = find_piece_in(gs, name);
        if (p >= 0) {
            *set = av->gear_sets[i];
            return &gs->pieces[p];
        }
    }
    return NULL;
}

const kk_sa_piece *kk_sa_piece_by_path(const kk_sa_library *lib,
                                       const char *path, int *set)
{
    const char *slash = strrchr(path, '/');
    if (!slash || slash == path)
        return NULL;
    char name[128];
    size_t n = (size_t)(slash - path);
    if (n >= sizeof name)
        return NULL;
    memcpy(name, path, n);
    name[n] = '\0';
    int s = find_set(lib, name);
    int p = s >= 0 ? find_piece_in(&lib->sets[s], slash + 1) : -1;
    if (p < 0)
        return NULL;
    *set = s;
    return &lib->sets[s].pieces[p];
}

int kk_sa_find_palette(const kk_sa_avatar *av, const char *name)
{
    for (int i = 0; i < av->n_palettes; i++)
        if (strcasecmp(av->palettes[i].key, name) == 0)
            return i;
    return -1;
}

bool kk_sa_gear_pivot(const kk_sa_avatar *av, int set, int piece, int row,
                      int col, float *x, float *y)
{
    const kk_sa_pivot *own = NULL, *shared = NULL;
    for (int i = 0; i < av->n_pivots; i++) {
        const kk_sa_pivot *p = &av->pivots[i];
        if (p->set != set || p->row != row || p->col != col)
            continue;
        if (p->piece == piece)
            own = p;
        else if (p->piece < 0)
            shared = p;
    }
    const kk_sa_pivot *use = own ? own : shared;
    *x = use ? use->x : 0.0f;
    *y = use ? use->y : 0.0f;
    /* A set parked far away is hidden on this frame (e.g. chairs). */
    return !shared || shared->y > KK_PIVOT_HIDDEN;
}

/* ---- users --------------------------------------------------------------- */

static const char *selected(const cJSON *obj)
{
    const char *s = kk_json_str(obj, "selectedItem");
    return s && s[0] && strcmp(s, "none") != 0 ? s : NULL;
}

int kk_sa_parse_users(const cJSON *root, kk_sa_user_cb cb, void *ud)
{
    int count = 0;
    const cJSON *it;
    cJSON_ArrayForEach(it, cJSON_GetObjectItemCaseSensitive(root, "userData"))
    {
        /* Keys: "Y" + YouTube channel id, plain digits for Twitch. Others
         * (other platforms) are skipped. */
        const char *k = it->string;
        char key[160];
        if (k[0] == 'Y' && strncmp(k + 1, "UC", 2) == 0)
            snprintf(key, sizeof key, "youtube:%s", k + 1);
        else if (isdigit((unsigned char)k[0]) && strspn(k, "0123456789") == strlen(k))
            snprintf(key, sizeof key, "twitch:%s", k);
        else
            continue;

        kk_sa_user u = {.key = key};
        u.avatar = selected(cJSON_GetObjectItemCaseSensitive(it, "avatar"));
        u.palette = selected(cJSON_GetObjectItemCaseSensitive(it, "color"));
        char gear[16][160];
        const cJSON *set;
        cJSON_ArrayForEach(set, kk_json_path(it, "gear.sets"))
        {
            const char *piece = selected(set);
            if (piece && u.n_gear < 16) {
                snprintf(gear[u.n_gear], sizeof gear[0], "%s/%s", set->string, piece);
                u.gear[u.n_gear] = gear[u.n_gear];
                u.n_gear++;
            }
        }
        u.name = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(it, "displayName"));
        if (u.name && !u.name[0])
            u.name = NULL;
        const char *t = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(it, "firstTimeSpawned"));
        if (!t || !kk_parse_iso_time(t, &u.first))
            u.first = 0;
        t = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(it, "lastTimeUsed"));
        if (!t || !kk_parse_iso_time(t, &u.last))
            u.last = 0;
        if (!u.avatar && !u.palette && u.n_gear == 0 && !u.name)
            continue;
        cb(ud, &u);
        count++;
    }
    return count;
}

int kk_sa_read_users(const char *data_dir, kk_sa_user_cb cb, void *ud)
{
    cJSON *root = read_json(data_dir);
    if (!root)
        return -1;
    int n = kk_sa_parse_users(root, cb, ud);
    cJSON_Delete(root);
    malloc_trim(0);
    return n;
}

int kk_sa_parse_sounds(const cJSON *root, const char *sounds_dir,
                       kk_sa_sound_cb cb, void *ud)
{
    static const char *const exts[] = {"ogg", "wav", "mp3"};
    int count = 0;
    const cJSON *it;
    cJSON_ArrayForEach(it, cJSON_GetObjectItemCaseSensitive(root, "soundData"))
    {
        const char *file = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(it, "soundName"));
        if (!it->string || !it->string[0])
            continue;
        if (!file || !file[0])
            file = it->string;
        char path[KK_PATH_MAX];
        kk_sa_sound snd = {.name = it->string, .volume = 100};
        for (size_t e = 0; e < sizeof exts / sizeof exts[0] && !snd.file; e++)
            if (kk_pathf(path, sizeof path, "%s/%s.%s", sounds_dir, file, exts[e]) &&
                kk_file_exists(path))
                snd.file = path;
        double lo = kk_json_num(it, "settings.volume", 1.0);
        double hi = kk_json_num(it, "settings.volumeMax", lo);
        double v = (lo + hi) / 2.0 * 100.0;
        snd.volume = v < 0 ? 0 : v > 400 ? 400 : (int)lround(v);
        cb(ud, &snd);
        count++;
    }
    return count;
}

int kk_sa_read_sounds(const char *data_dir, kk_sa_sound_cb cb, void *ud)
{
    char dir[KK_PATH_MAX];
    if (!kk_pathf(dir, sizeof dir, "%s/sounds", data_dir))
        return -1;
    cJSON *root = read_json(data_dir);
    if (!root)
        return -1;
    int n = kk_sa_parse_sounds(root, dir, cb, ud);
    cJSON_Delete(root);
    malloc_trim(0);
    return n;
}
