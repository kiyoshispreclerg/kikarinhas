/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "emotes.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <pango/pangocairo.h>

#include "emoji.h"
#include "log.h"
#include "util.h"

#define BUCKETS 256
#define MAX_ENTRIES 512 /* least recently used ones go beyond this */
#define MAX_INFLIGHT 6  /* downloads at once; the rest wait their turn */
#define EMOJI_FONT "Noto Color Emoji, emoji"
#define EMOJI_RENDER_PX 96 /* drawn big once, then scaled down smoothly */

typedef enum {
    E_QUEUED, /* waiting for a download slot */
    E_FETCHING,
    E_READY,
    E_FAILED,
} entry_state;

typedef struct entry {
    struct entry *next; /* in its bucket */
    struct kk_emotes *owner;
    char *key;
    char *url;  /* while queued or fetching */
    char *file; /* disk cache path; NULL without one */
    cairo_surface_t *img;
    entry_state st;
    uint64_t used; /* recency, for eviction */
} entry;

struct kk_emotes {
    kk_http *http;
    char *dir;
    int px;
    entry *buckets[BUCKETS];
    int count, inflight;
    uint64_t clock;
};

static uint64_t fnv1a(const char *s)
{
    uint64_t h = 0xcbf29ce484222325ULL;
    for (; *s; s++)
        h = (h ^ (unsigned char)*s) * 0x100000001b3ULL;
    return h;
}

kk_emotes *kk_emotes_new(kk_http *http, const char *cache_dir, int px)
{
    kk_emotes *e = calloc(1, sizeof *e);
    if (!e)
        return NULL;
    e->http = http;
    e->px = px > 0 ? px : 48;
    if (cache_dir && !(e->dir = strdup(cache_dir))) {
        free(e);
        return NULL;
    }
    return e;
}

static void entry_free(entry *en)
{
    if (en->st == E_FETCHING) {
        kk_http_cancel(en->owner->http, en);
        en->owner->inflight--;
    }
    if (en->img)
        cairo_surface_destroy(en->img);
    free(en->key);
    free(en->url);
    free(en->file);
    free(en);
}

/* Drops every entry for which keep() is false. */
static void prune(kk_emotes *e, bool (*keep)(const entry *, const void *),
                  const void *arg)
{
    for (int b = 0; b < BUCKETS; b++)
        for (entry **p = &e->buckets[b]; *p;) {
            entry *en = *p;
            if (keep && keep(en, arg)) {
                p = &en->next;
                continue;
            }
            *p = en->next;
            entry_free(en);
            e->count--;
        }
}

void kk_emotes_free(kk_emotes *e)
{
    if (!e)
        return;
    prune(e, NULL, NULL);
    free(e->dir);
    free(e);
}

static bool is_busy(const entry *en, const void *arg)
{
    (void)arg;
    return en->st == E_FETCHING;
}

void kk_emotes_set_size(kk_emotes *e, int px)
{
    if (px <= 0 || px == e->px)
        return;
    e->px = px;
    /* Downloads under way finish at the new size. */
    prune(e, is_busy, NULL);
}

int kk_emotes_size(const kk_emotes *e)
{
    return e->px;
}

/* ---- images -------------------------------------------------------------- */

/* src scaled to fit a px square, centred. Takes ownership of src. */
static cairo_surface_t *fit(cairo_surface_t *src, int px)
{
    int w = cairo_image_surface_get_width(src);
    int h = cairo_image_surface_get_height(src);
    cairo_surface_t *dst = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, px, px);
    if (w > 0 && h > 0 && cairo_surface_status(dst) == CAIRO_STATUS_SUCCESS) {
        double k = (double)px / (w > h ? w : h);
        cairo_t *cr = cairo_create(dst);
        cairo_translate(cr, (px - w * k) / 2, (px - h * k) / 2);
        cairo_scale(cr, k, k);
        cairo_set_source_surface(cr, src, 0, 0);
        cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_GOOD);
        cairo_paint(cr);
        cairo_destroy(cr);
    }
    cairo_surface_destroy(src);
    if (cairo_surface_status(dst) != CAIRO_STATUS_SUCCESS || w <= 0 || h <= 0) {
        cairo_surface_destroy(dst);
        return NULL;
    }
    return dst;
}

typedef struct {
    const unsigned char *p;
    size_t left;
} png_reader;

static cairo_status_t read_png(void *closure, unsigned char *data, unsigned len)
{
    png_reader *r = closure;
    if (len > r->left)
        return CAIRO_STATUS_READ_ERROR;
    memcpy(data, r->p, len);
    r->p += len;
    r->left -= len;
    return CAIRO_STATUS_SUCCESS;
}

static bool is_png(const char *data, size_t len)
{
    return len > 8 && memcmp(data, "\x89PNG\r\n\x1a\n", 8) == 0;
}

static cairo_surface_t *decode_png(const char *data, size_t len, int px)
{
    if (!is_png(data, len))
        return NULL;
    png_reader r = {(const unsigned char *)data, len};
    cairo_surface_t *s = cairo_image_surface_create_from_png_stream(read_png, &r);
    if (cairo_surface_status(s) != CAIRO_STATUS_SUCCESS) {
        cairo_surface_destroy(s);
        return NULL;
    }
    return fit(s, px);
}

/* Drawn once at a big size into a surface the size of its ink, then fitted
 * like any image. A font without colour glyphs gives a white silhouette. */
static cairo_surface_t *render_emoji(const char *text, int px)
{
    cairo_surface_t *scratch = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 1, 1);
    cairo_t *cr = cairo_create(scratch);
    PangoLayout *layout = pango_cairo_create_layout(cr);
    PangoFontDescription *fd = pango_font_description_from_string(EMOJI_FONT);
    pango_font_description_set_absolute_size(fd, EMOJI_RENDER_PX * PANGO_SCALE);
    pango_layout_set_font_description(layout, fd);
    pango_font_description_free(fd);
    pango_layout_set_text(layout, text, -1);
    PangoRectangle ink;
    pango_layout_get_pixel_extents(layout, &ink, NULL);

    cairo_surface_t *out = NULL;
    if (ink.width > 0 && ink.height > 0 && ink.width <= 4 * EMOJI_RENDER_PX &&
        ink.height <= 4 * EMOJI_RENDER_PX) {
        cairo_surface_t *img =
            cairo_image_surface_create(CAIRO_FORMAT_ARGB32, ink.width, ink.height);
        cairo_t *c2 = cairo_create(img);
        cairo_set_source_rgb(c2, 1, 1, 1);
        cairo_move_to(c2, -ink.x, -ink.y);
        pango_cairo_show_layout(c2, layout);
        cairo_destroy(c2);
        out = fit(img, px);
    }
    g_object_unref(layout);
    cairo_destroy(cr);
    cairo_surface_destroy(scratch);
    return out;
}

/* ---- disk cache ---------------------------------------------------------- */

bool kk_emotes_file(const kk_emotes *e, const char *platform, const char *id,
                    char *out, size_t size)
{
    if (!e->dir)
        return false;
    char safe[32];
    size_t n = 0;
    for (const char *p = platform; *p && n < sizeof safe - 1; p++)
        safe[n++] = (*p >= 'a' && *p <= 'z') || (*p >= '0' && *p <= '9') ||
                            *p == '-' || *p == '_'
                        ? *p
                        : '_';
    safe[n] = '\0';
    return kk_pathf(out, size, "%s/%s/%016llx.png", e->dir, n ? safe : "_",
                    (unsigned long long)fnv1a(id));
}

static char *cache_file(const kk_emotes *e, const char *platform, const char *id)
{
    char path[KK_PATH_MAX];
    return kk_emotes_file(e, platform, id, path, sizeof path) ? strdup(path) : NULL;
}

static cairo_surface_t *load_file(const char *path, int px)
{
    size_t len;
    char *data = kk_read_file(path, &len);
    if (!data)
        return NULL;
    cairo_surface_t *s = decode_png(data, len, px);
    free(data);
    return s;
}

/* Written to a temporary name and renamed, so a crash never leaves half a
 * file behind. Best effort: the cache is only a speed-up. */
static void save_file(const char *path, const char *data, size_t len)
{
    char tmp[KK_PATH_MAX];
    if (!kk_pathf(tmp, sizeof tmp, "%s.tmp", path))
        return;
    kk_make_parent_dirs(path);
    FILE *f = fopen(tmp, "wb");
    if (!f)
        return;
    bool ok = fwrite(data, 1, len, f) == len;
    ok = fclose(f) == 0 && ok;
    if (!ok || rename(tmp, path) < 0)
        remove(tmp);
}

/* ---- downloads ----------------------------------------------------------- */

static void start_next(kk_emotes *e);

static void on_image(void *ud, long status, const char *body, size_t len,
                     const char *err)
{
    entry *en = ud;
    kk_emotes *e = en->owner;
    e->inflight--;
    cairo_surface_t *img = status == 200 ? decode_png(body, len, e->px) : NULL;
    if (img) {
        en->img = img;
        en->st = E_READY;
        if (en->file)
            save_file(en->file, body, len);
    } else {
        en->st = E_FAILED;
        if (err)
            kk_log_warn("emote %s: %s", en->url, err);
        else if (status != 200)
            kk_log_warn("emote %s: HTTP %ld", en->url, status);
        else
            kk_log_warn("emote %s: não é uma imagem PNG", en->url);
    }
    free(en->url);
    en->url = NULL;
    start_next(e);
}

static bool fetch(kk_emotes *e, entry *en)
{
    if (kk_http_get(e->http, en->url, on_image, en) < 0)
        return false;
    en->st = E_FETCHING;
    e->inflight++;
    return true;
}

static void start_next(kk_emotes *e)
{
    for (int b = 0; b < BUCKETS && e->inflight < MAX_INFLIGHT; b++)
        for (entry *en = e->buckets[b]; en && e->inflight < MAX_INFLIGHT; en = en->next)
            if (en->st == E_QUEUED && !fetch(e, en)) {
                en->st = E_FAILED;
                free(en->url);
                en->url = NULL;
            }
}

/* ---- lookup -------------------------------------------------------------- */

static bool not_oldest(const entry *en, const void *arg)
{
    return en != arg;
}

static void evict_one(kk_emotes *e)
{
    const entry *oldest = NULL;
    for (int b = 0; b < BUCKETS; b++)
        for (const entry *en = e->buckets[b]; en; en = en->next)
            if (en->st != E_FETCHING && (!oldest || en->used < oldest->used))
                oldest = en;
    if (oldest)
        prune(e, not_oldest, oldest);
}

static entry *find(kk_emotes *e, const char *key, entry ***bucket)
{
    *bucket = &e->buckets[fnv1a(key) % BUCKETS];
    for (entry *en = **bucket; en; en = en->next)
        if (strcmp(en->key, key) == 0)
            return en;
    return NULL;
}

static kk_emote_state state_of(const entry *en, cairo_surface_t **out)
{
    switch (en->st) {
    case E_READY:
        *out = en->img;
        return KK_EMOTE_READY;
    case E_FAILED:
        return KK_EMOTE_FAILED;
    default:
        return KK_EMOTE_LOADING;
    }
}

kk_emote_state kk_emotes_get(kk_emotes *e, const char *platform,
                             const kk_emote *em, cairo_surface_t **out)
{
    *out = NULL;
    char key[512];
    if (em->url) {
        const char *id = em->id && em->id[0] ? em->id : em->url;
        if (!kk_pathf(key, sizeof key, "%s\x1f%s", platform ? platform : "", id))
            return KK_EMOTE_FAILED;
    } else {
        char ek[128];
        if (!em->text || !kk_emoji_key(em->text, strlen(em->text), ek, sizeof ek) ||
            !kk_pathf(key, sizeof key, "\x1f%s", ek))
            return KK_EMOTE_FAILED;
    }

    entry **bucket;
    entry *en = find(e, key, &bucket);
    if (en) {
        en->used = ++e->clock;
        return state_of(en, out);
    }

    if (e->count >= MAX_ENTRIES)
        evict_one(e);
    en = calloc(1, sizeof *en);
    if (!en || !(en->key = strdup(key))) {
        free(en);
        return KK_EMOTE_FAILED;
    }
    en->owner = e;
    en->used = ++e->clock;
    en->next = *bucket;
    *bucket = en;
    e->count++;

    if (!em->url) {
        en->img = render_emoji(em->text, e->px);
        en->st = en->img ? E_READY : E_FAILED;
        return state_of(en, out);
    }

    en->file = cache_file(e, platform ? platform : "", em->id && em->id[0] ? em->id : em->url);
    if (en->file && (en->img = load_file(en->file, e->px))) {
        en->st = E_READY;
    } else if (!e->http || !(en->url = strdup(em->url))) {
        en->st = E_FAILED;
    } else {
        en->st = E_QUEUED;
        if (e->inflight < MAX_INFLIGHT && !fetch(e, en)) {
            en->st = E_FAILED;
            free(en->url);
            en->url = NULL;
        }
    }
    return state_of(en, out);
}

bool kk_emotes_default_dir(char *out, size_t size)
{
    const char *xdg = getenv("XDG_CACHE_HOME");
    if (xdg && xdg[0] == '/')
        return kk_pathf(out, size, "%s/kikarinhas/emotes", xdg);
    const char *home = getenv("HOME");
    return home && kk_pathf(out, size, "%s/.cache/kikarinhas/emotes", home);
}
