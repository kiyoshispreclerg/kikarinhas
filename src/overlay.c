/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "overlay.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "image.h"
#include "util.h"

#define FADE_S 0.3
#define DEFAULT_S 5.0
#define MAX_SHOWN 16

typedef struct {
    char *path;
    kk_image img;
    bool ok;
} cached;

typedef struct {
    char *key;
    const cached *pic;
    kk_cmd_params p;
    double start; /* < 0: not started yet */
    double life;
    double alpha;
    int frame;
    kk_rect cur, drawn;
    bool changed; /* repaint on the next frame */
} shown;

struct kk_overlay {
    kk_layer layer;
    int width, height;
    cached **cache; /* pointers: shown pictures keep theirs while it grows */
    int n_cache;
    shown items[MAX_SHOWN];
    int n_items;
    kk_rect removed[MAX_SHOWN];
    int n_removed;
};

static void layer_damage(void *ud, kk_damage_fn add, void *to);
static void layer_draw(void *ud, cairo_t *cr, kk_rect clip);
static void layer_painted(void *ud);

kk_overlay *kk_overlay_new(void)
{
    kk_overlay *o = calloc(1, sizeof *o);
    if (o)
        o->layer = (kk_layer){layer_damage, layer_draw, layer_painted, o};
    return o;
}

static void drop(kk_overlay *o, int i)
{
    shown *s = &o->items[i];
    if (!kk_rect_empty(s->drawn) && o->n_removed < MAX_SHOWN)
        o->removed[o->n_removed++] = s->drawn;
    free(s->key);
    o->items[i] = o->items[--o->n_items];
}

void kk_overlay_clear(kk_overlay *o)
{
    while (o->n_items)
        drop(o, 0);
    for (int i = 0; i < o->n_cache; i++) {
        free(o->cache[i]->path);
        kk_image_free(&o->cache[i]->img);
        free(o->cache[i]);
    }
    free(o->cache);
    o->cache = NULL;
    o->n_cache = 0;
}

void kk_overlay_free(kk_overlay *o)
{
    if (!o)
        return;
    kk_overlay_clear(o);
    free(o);
}

void kk_overlay_resize(kk_overlay *o, int width, int height)
{
    o->width = width;
    o->height = height;
    for (int i = 0; i < o->n_items; i++)
        o->items[i].changed = true;
}

static const cached *load(kk_overlay *o, const char *path)
{
    for (int i = 0; i < o->n_cache; i++)
        if (strcmp(o->cache[i]->path, path) == 0)
            return o->cache[i]->ok ? o->cache[i] : NULL;
    cached **nc = realloc(o->cache, (size_t)(o->n_cache + 1) * sizeof *nc);
    if (!nc)
        return NULL;
    o->cache = nc;
    cached *c = calloc(1, sizeof *c);
    if (!c || !(c->path = strdup(path))) {
        free(c);
        return NULL;
    }
    nc[o->n_cache++] = c;
    /* A file that failed is remembered too, so chat can't make us retry it
     * on every message. */
    c->ok = kk_image_load(&c->img, path);
    return c->ok ? c : NULL;
}

bool kk_overlay_show(kk_overlay *o, const char *key, const char *file,
                     const kk_cmd_params *p)
{
    const cached *pic = load(o, file);
    if (!pic)
        return false;
    shown *s = NULL;
    for (int i = 0; i < o->n_items && !s; i++)
        if (strcmp(o->items[i].key, key) == 0)
            s = &o->items[i];
    if (!s) {
        if (o->n_items == MAX_SHOWN)
            drop(o, 0); /* the oldest-placed slot makes room */
        s = &o->items[o->n_items];
        *s = (shown){.key = strdup(key)};
        if (!s->key)
            return false;
        o->n_items++;
    }
    bool same = s->pic == pic && s->start >= 0;
    s->pic = pic;
    s->p = *p;
    s->life = p->seconds > 0 ? p->seconds : DEFAULT_S;
    /* Again while showing: keep it up (no new fade-in), time starts over. */
    s->start = same ? -2 : -1;
    s->changed = true;
    return true;
}

/* Where the picture goes now: centred on (x%, y%) of the window. */
static kk_rect place(const kk_overlay *o, const shown *s)
{
    double k = s->p.scale / 100.0;
    double w = s->pic->img.w * k, h = s->pic->img.h * k;
    double cx = o->width * s->p.x / 100.0, cy = o->height * s->p.y / 100.0;
    int x = (int)floor(cx - w / 2), y = (int)floor(cy - h / 2);
    return (kk_rect){x, y, (int)ceil(w) + 2, (int)ceil(h) + 2};
}

void kk_overlay_update(kk_overlay *o, double now)
{
    for (int i = 0; i < o->n_items;) {
        shown *s = &o->items[i];
        if (s->start == -2) /* shown again: skip the fade-in */
            s->start = now - FADE_S;
        else if (s->start < 0)
            s->start = now;
        double age = now - s->start;
        if (age >= s->life) {
            drop(o, i);
            continue;
        }
        double a = 1.0;
        if (age < FADE_S)
            a = age / FADE_S;
        else if (s->life - age < FADE_S)
            a = (s->life - age) / FADE_S;
        int frame = kk_image_frame_index(&s->pic->img, age);
        kk_rect r = place(o, s);
        if (fabs(a - s->alpha) > 0.01 || frame != s->frame || r.x != s->cur.x ||
            r.y != s->cur.y || r.w != s->cur.w || r.h != s->cur.h)
            s->changed = true;
        s->alpha = a;
        s->frame = frame;
        s->cur = r;
        i++;
    }
}

int kk_overlay_showing(const kk_overlay *o)
{
    return o->n_items;
}

const kk_layer *kk_overlay_layer(kk_overlay *o)
{
    return &o->layer;
}

/* ---- layer --------------------------------------------------------------- */

static void layer_damage(void *ud, kk_damage_fn add, void *to)
{
    kk_overlay *o = ud;
    for (int i = 0; i < o->n_removed; i++)
        add(to, o->removed[i]);
    for (int i = 0; i < o->n_items; i++)
        if (o->items[i].changed) {
            add(to, o->items[i].drawn);
            add(to, o->items[i].cur);
        }
}

static void layer_draw(void *ud, cairo_t *cr, kk_rect clip)
{
    kk_overlay *o = ud;
    for (int i = 0; i < o->n_items; i++) {
        const shown *s = &o->items[i];
        if (s->start < 0 || kk_rect_empty(s->cur) || !kk_rect_intersects(s->cur, clip))
            continue;
        cairo_surface_t *frame = s->pic->img.frames[s->frame];
        double k = s->p.scale / 100.0;
        cairo_save(cr);
        cairo_translate(cr, s->cur.x + 1, s->cur.y + 1);
        if (fabs(k - 1.0) > 1e-3)
            cairo_scale(cr, k, k);
        cairo_set_source_surface(cr, frame, 0, 0);
        /* Grown by a whole factor: likely pixel art, keep it crisp. */
        if (k >= 2 && fabs(k - round(k)) < 1e-6)
            cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_NEAREST);
        if (s->alpha >= 0.999)
            cairo_paint(cr);
        else
            cairo_paint_with_alpha(cr, s->alpha);
        cairo_restore(cr);
    }
}

static void layer_painted(void *ud)
{
    kk_overlay *o = ud;
    o->n_removed = 0;
    for (int i = 0; i < o->n_items; i++) {
        o->items[i].drawn = o->items[i].cur;
        o->items[i].changed = false;
    }
}
