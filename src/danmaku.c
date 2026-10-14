/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Danmaku: chat messages flying across the window (see danmaku.h). */
#include "danmaku.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include <pango/pangocairo.h>

#include "avatar.h" /* kk_bubble_emote: an emote image inside the text */

#define DEFAULT_FONT "Sans Bold"
#define MAX_ITEMS 1000
#define MAX_TEXT 400 /* bytes of the message shown; the rest is cut */
/* Space kept between two messages in the same lane, px. */
#define GAP 24
/* U+FFFC OBJECT REPLACEMENT CHARACTER: where an emote image goes. */
#define HOLE "\xEF\xBF\xBC"

typedef struct {
    cairo_surface_t *surf;
    int w, h;
    int lane, y;
    double t0, speed; /* entered at t0 from the right edge, px/s */
    double x;         /* left edge now */
    kk_bubble_emote emotes[KK_BUBBLE_MAX_EMOTES];
    int n_emotes;
    kk_rect drawn; /* on screen since the last paint; empty if not yet */
} item;

struct kk_danmaku {
    kk_emotes *images;
    kk_danmaku_config cfg;
    char *font_name;
    PangoContext *pango;
    PangoFontDescription *font;
    int lane_h;
    int width, height;
    kk_rng rng;
    double now;

    item *items;
    int n, cap;
    kk_rect gone; /* where removed messages were last painted */
    kk_layer layer;
};

static int lanes(const kk_danmaku *d)
{
    int n = d->height / d->lane_h;
    return n < 1 ? 1 : n;
}

static double outline_width(const kk_danmaku *d)
{
    /* Points to pixels at cairo's 96 dpi; thicker for bigger text. */
    return fmax(1.5, d->cfg.size * 96.0 / 72.0 * 0.08);
}

static int pad(const kk_danmaku *d)
{
    return (int)ceil(outline_width(d)) + 1;
}

static PangoLayout *new_layout(const kk_danmaku *d, const char *text)
{
    PangoLayout *layout = pango_layout_new(d->pango);
    pango_layout_set_font_description(layout, d->font);
    pango_layout_set_single_paragraph_mode(layout, TRUE);
    pango_layout_set_text(layout, text, -1);
    return layout;
}

/* A line tall enough for the fallback fonts too (CJK, emoji). */
static void measure_lane(kk_danmaku *d)
{
    PangoLayout *layout = new_layout(d, "Ág日本語😀");
    PangoRectangle logical;
    pango_layout_get_pixel_extents(layout, NULL, &logical);
    g_object_unref(layout);
    d->lane_h = logical.height + 2 * pad(d);
}

static void set_font(kk_danmaku *d)
{
    PangoFontDescription *f = pango_font_description_from_string(
        d->font_name && d->font_name[0] ? d->font_name : DEFAULT_FONT);
    double size = d->cfg.size > 0 ? d->cfg.size : 24.0;
    pango_font_description_set_size(f, (gint)(size * PANGO_SCALE));
    if (d->font)
        pango_font_description_free(d->font);
    d->font = f;
    measure_lane(d);
}

static void layer_damage(void *ud, kk_damage_fn add, void *to);
static void layer_draw(void *ud, cairo_t *cr, kk_rect clip);
static void layer_painted(void *ud);

kk_danmaku *kk_danmaku_new(kk_emotes *emotes, uint64_t seed)
{
    kk_danmaku *d = calloc(1, sizeof *d);
    if (!d)
        return NULL;
    d->images = emotes;
    kk_rng_seed(&d->rng, seed ^ 0xda4a6au);
    d->cfg = (kk_danmaku_config){.seconds = 8, .size = 24, .color = 0xffffff,
                                 .opacity = 1, .avoid_avatars = true,
                                 .max_on_screen = 100};
    d->pango = pango_font_map_create_context(pango_cairo_font_map_get_default());
    d->layer = (kk_layer){layer_damage, layer_draw, layer_painted, d};
    d->width = d->height = 1;
    set_font(d);
    return d;
}

static void item_free(item *it)
{
    if (it->surf)
        cairo_surface_destroy(it->surf);
    for (int i = 0; i < it->n_emotes; i++) {
        kk_bubble_emote_resolve(&it->emotes[i], NULL);
        kk_emote_anim_unref(it->emotes[i].anim);
    }
}

static void remove_item(kk_danmaku *d, int i)
{
    if (!kk_rect_empty(d->items[i].drawn))
        d->gone = kk_rect_empty(d->gone) ? d->items[i].drawn
                                         : kk_rect_union(d->gone, d->items[i].drawn);
    item_free(&d->items[i]);
    /* Keep the order: it is the order they are painted in. */
    memmove(&d->items[i], &d->items[i + 1], (size_t)(d->n - i - 1) * sizeof *d->items);
    d->n--;
}

static void clear(kk_danmaku *d)
{
    while (d->n > 0)
        remove_item(d, d->n - 1);
}

void kk_danmaku_free(kk_danmaku *d)
{
    if (!d)
        return;
    clear(d);
    free(d->items);
    free(d->font_name);
    if (d->font)
        pango_font_description_free(d->font);
    g_object_unref(d->pango);
    free(d);
}

void kk_danmaku_configure(kk_danmaku *d, const kk_danmaku_config *cfg)
{
    bool font_changed = cfg->size != d->cfg.size ||
                        (cfg->font ? !d->font_name || strcmp(cfg->font, d->font_name) != 0
                                   : d->font_name != NULL);
    d->cfg = *cfg;
    d->cfg.font = NULL;
    if (d->cfg.seconds < 0.5)
        d->cfg.seconds = 0.5;
    if (font_changed) {
        free(d->font_name);
        d->font_name = cfg->font ? strdup(cfg->font) : NULL;
        set_font(d);
    }
    if (!cfg->enabled)
        clear(d);
}

bool kk_danmaku_enabled(const kk_danmaku *d)
{
    return d->cfg.enabled;
}

void kk_danmaku_resize(kk_danmaku *d, int width, int height)
{
    d->width = width > 1 ? width : 1;
    d->height = height > 1 ? height : 1;
}

/* ---- rendering a message ------------------------------------------------- */

static void set_rgb(cairo_t *cr, uint32_t c)
{
    cairo_set_source_rgb(cr, ((c >> 16) & 0xff) / 255.0, ((c >> 8) & 0xff) / 255.0,
                         (c & 0xff) / 255.0);
}

/* Copies text into out (size bytes) as one line, with each image emote
 * replaced by a hole; idx gets where the holes are and slots what goes in
 * them. Returns how many holes. */
static int line_text(kk_danmaku *d, const kk_chat_msg *m, char *out, size_t size,
                     size_t used, int *idx, kk_bubble_emote *slots, int px)
{
    size_t len = strlen(m->text), pos = 0;
    int n = 0;
    for (int i = 0; d->images && i < m->n_emotes && n < KK_BUBBLE_MAX_EMOTES; i++) {
        const kk_emote *em = &m->emotes[i];
        if (!em->url || em->len == 0 || em->start < pos || em->start + em->len > len ||
            used + (em->start - pos) + 3 + 1 > size)
            continue;
        kk_emote_anim *anim;
        kk_emote_state st = kk_emotes_get_anim(d->images, m->platform, em, &anim);
        if (st == KK_EMOTE_FAILED)
            continue;
        memcpy(out + used, m->text + pos, em->start - pos);
        used += em->start - pos;
        idx[n] = (int)used;
        memcpy(out + used, HOLE, 3);
        used += 3;
        pos = em->start + em->len;
        kk_bubble_emote *e = &slots[n++];
        *e = (kk_bubble_emote){.size = px};
        if (st == KK_EMOTE_READY) {
            e->anim = kk_emote_anim_ref(anim);
        } else {
            e->platform = strdup(m->platform);
            e->id = strdup(em->id ? em->id : "");
            e->url = strdup(em->url);
        }
    }
    size_t rest = len - pos;
    if (rest > size - used - 1)
        rest = size - used - 1;
    memcpy(out + used, m->text + pos, rest);
    out[used + rest] = '\0';
    /* One line: line breaks and tabs become spaces (holes are untouched,
     * their bytes are all above 0x80). */
    for (char *c = out; *c; c++)
        if (*c == '\n' || *c == '\r' || *c == '\t')
            *c = ' ';
    return n;
}

static bool render(kk_danmaku *d, const kk_chat_msg *m, item *it)
{
    char text[MAX_TEXT + 64];
    size_t bold = 0;
    text[0] = '\0';
    if (m->kind == KK_MSG_PAID && m->amount && m->amount[0]) {
        snprintf(text, sizeof text, "%s%s", m->amount, m->text[0] ? "  " : "");
        bold = strlen(m->amount);
    }
    int px = d->lane_h - 2 * pad(d);
    px = px * 4 / 5;
    int idx[KK_BUBBLE_MAX_EMOTES];
    it->n_emotes = line_text(d, m, text, sizeof text, strlen(text), idx, it->emotes, px);
    if (!text[0])
        return false;

    PangoLayout *layout = new_layout(d, text);
    PangoAttrList *attrs = pango_attr_list_new();
    if (bold) {
        PangoAttribute *a = pango_attr_weight_new(PANGO_WEIGHT_ULTRABOLD);
        a->start_index = 0;
        a->end_index = (guint)bold;
        pango_attr_list_insert(attrs, a);
    }
    for (int i = 0; i < it->n_emotes; i++) {
        PangoRectangle r = {0, -px * PANGO_SCALE * 4 / 5, px * PANGO_SCALE, px * PANGO_SCALE};
        PangoAttribute *shape = pango_attr_shape_new(&r, &r);
        shape->start_index = (guint)idx[i];
        shape->end_index = (guint)idx[i] + 3;
        pango_attr_list_insert(attrs, shape);
    }
    pango_layout_set_attributes(layout, attrs);
    pango_attr_list_unref(attrs);

    PangoRectangle logical;
    pango_layout_get_pixel_extents(layout, NULL, &logical);
    int p = pad(d);
    it->w = logical.width + 2 * p;
    it->h = logical.height + 2 * p;
    if (it->w > 30000)
        it->w = 30000; /* past what cairo takes; MAX_TEXT keeps far from it */
    it->surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, it->w, it->h);
    cairo_t *cr = cairo_create(it->surf);
    double ox = p - logical.x, oy = p - logical.y;

    cairo_move_to(cr, ox, oy);
    pango_cairo_layout_path(cr, layout);
    cairo_set_line_width(cr, 2.0 * outline_width(d));
    cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
    set_rgb(cr, d->cfg.outline);
    cairo_stroke(cr);

    uint32_t color = m->kind == KK_MSG_PAID     ? d->cfg.paid_color
                     : m->kind == KK_MSG_MEMBER ? d->cfg.member_color
                                                : d->cfg.color;
    cairo_move_to(cr, ox, oy);
    set_rgb(cr, color);
    pango_cairo_show_layout(cr, layout);

    for (int i = 0; i < it->n_emotes; i++) {
        PangoRectangle at;
        pango_layout_index_to_pos(layout, idx[i], &at);
        it->emotes[i].x = (int)ox + at.x / PANGO_SCALE;
        it->emotes[i].y = (int)oy + at.y / PANGO_SCALE + (at.height / PANGO_SCALE - px) / 2;
    }
    cairo_destroy(cr);
    g_object_unref(layout);
    return cairo_surface_status(it->surf) == CAIRO_STATUS_SUCCESS;
}

/* ---- lanes --------------------------------------------------------------- */

/* Whether a message w px wide, entering now at the right edge, keeps clear
 * of every message already in lane. */
static bool lane_free(const kk_danmaku *d, int lane, int w)
{
    double speed = (d->width + w) / d->cfg.seconds;
    for (int i = 0; i < d->n; i++) {
        const item *o = &d->items[i];
        if (o->lane != lane)
            continue;
        double tail = o->x + o->w + GAP;
        if (tail > d->width)
            return false; /* still coming in */
        /* Faster than the one ahead: still behind it when it leaves? */
        if (speed > o->speed && d->width - speed * (tail / o->speed) < 0)
            return false;
    }
    return true;
}

/* How far into the window the last message of lane still reaches. */
static double lane_tail(const kk_danmaku *d, int lane)
{
    double tail = 0;
    for (int i = 0; i < d->n; i++)
        if (d->items[i].lane == lane)
            tail = fmax(tail, d->items[i].x + d->items[i].w);
    return tail;
}

/* How much of the avatars a message in lane would cross. */
static double lane_cost(const kk_danmaku *d, int lane, const kk_rect *avoid, int n)
{
    if (!d->cfg.avoid_avatars)
        return 0;
    int top = lane * d->lane_h, bottom = top + d->lane_h;
    double cost = 0;
    for (int i = 0; i < n; i++) {
        int y0 = avoid[i].y > top ? avoid[i].y : top;
        int y1 = avoid[i].y + avoid[i].h < bottom ? avoid[i].y + avoid[i].h : bottom;
        if (y1 > y0 && avoid[i].w > 0)
            cost += (double)(y1 - y0) * avoid[i].w;
    }
    return cost;
}

static int pick_lane(kk_danmaku *d, int w, const kk_rect *avoid, int n)
{
    int nl = lanes(d);
    int best[512], nb = 0;
    double best_cost = INFINITY;
    /* Free lanes, crossing the avatars least; at random among equals. */
    for (int l = 0; l < nl && l < 512; l++) {
        if (!lane_free(d, l, w))
            continue;
        double c = lane_cost(d, l, avoid, n);
        if (c < best_cost) {
            best_cost = c;
            nb = 0;
        }
        if (c == best_cost)
            best[nb++] = l;
    }
    if (nb > 0)
        return best[kk_rng_int(&d->rng, nb)];
    /* All taken: the one that frees up first, off the avatars if it can. */
    int pick = 0;
    double pick_cost = INFINITY, pick_tail = INFINITY;
    for (int l = 0; l < nl; l++) {
        double c = lane_cost(d, l, avoid, n), t = lane_tail(d, l);
        if (c < pick_cost || (c == pick_cost && t < pick_tail)) {
            pick = l;
            pick_cost = c;
            pick_tail = t;
        }
    }
    return pick;
}

bool kk_danmaku_message(kk_danmaku *d, const kk_chat_msg *m, const kk_rect *avoid,
                        int n, double now)
{
    if (!d->cfg.enabled || d->n >= d->cfg.max_on_screen || d->n >= MAX_ITEMS)
        return false;
    if (d->n == d->cap) {
        int cap = d->cap ? d->cap * 2 : 32;
        item *ni = realloc(d->items, (size_t)cap * sizeof *ni);
        if (!ni)
            return false;
        d->items = ni;
        d->cap = cap;
    }
    kk_danmaku_update(d, now); /* lanes are judged where things are now */
    item it = {0};
    if (!render(d, m, &it)) {
        item_free(&it);
        return false;
    }
    it.lane = pick_lane(d, it.w, avoid, n);
    it.y = it.lane * d->lane_h + (d->lane_h - it.h) / 2;
    it.t0 = now;
    it.speed = (d->width + it.w) / d->cfg.seconds;
    it.x = d->width;
    d->items[d->n++] = it;
    return true;
}

void kk_danmaku_update(kk_danmaku *d, double now)
{
    d->now = now;
    for (int i = d->n - 1; i >= 0; i--) {
        item *it = &d->items[i];
        it->x = d->width - it->speed * (now - it->t0);
        if (it->x + it->w <= 0) {
            remove_item(d, i);
            continue;
        }
        for (int k = 0; d->images && k < it->n_emotes; k++) {
            kk_bubble_emote *e = &it->emotes[k];
            if (e->anim || !e->url)
                continue;
            kk_emote em = {.id = e->id, .url = e->url};
            kk_emote_anim *anim;
            kk_emote_state st = kk_emotes_get_anim(d->images, e->platform, &em, &anim);
            if (st != KK_EMOTE_LOADING)
                kk_bubble_emote_resolve(e, st == KK_EMOTE_READY ? anim : NULL);
        }
    }
}

/* ---- layer --------------------------------------------------------------- */

static kk_rect where(const item *it)
{
    return (kk_rect){(int)lround(it->x), it->y, it->w, it->h};
}

static void layer_damage(void *ud, kk_damage_fn add, void *to)
{
    kk_danmaku *d = ud;
    if (!kk_rect_empty(d->gone))
        add(to, d->gone);
    for (int i = 0; i < d->n; i++) {
        kk_rect r = where(&d->items[i]);
        /* Moving a few px a frame: one rectangle over both places. */
        add(to, kk_rect_empty(d->items[i].drawn) ? r : kk_rect_union(r, d->items[i].drawn));
    }
}

static void layer_draw(void *ud, cairo_t *cr, kk_rect clip)
{
    kk_danmaku *d = ud;
    for (int i = 0; i < d->n; i++) {
        const item *it = &d->items[i];
        kk_rect r = where(it);
        if (!kk_rect_intersects(r, clip))
            continue;
        bool alpha = d->cfg.opacity < 1.0;
        if (alpha)
            cairo_push_group(cr);
        cairo_set_source_surface(cr, it->surf, r.x, r.y);
        cairo_paint(cr);
        for (int k = 0; k < it->n_emotes; k++) {
            const kk_bubble_emote *e = &it->emotes[k];
            if (!e->anim)
                continue;
            cairo_surface_t *f = kk_emote_anim_frame(e->anim, d->now - it->t0);
            double s = (double)e->size / cairo_image_surface_get_width(f);
            cairo_save(cr);
            cairo_translate(cr, r.x + e->x, r.y + e->y);
            cairo_scale(cr, s, s);
            cairo_set_source_surface(cr, f, 0, 0);
            cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_GOOD);
            cairo_paint(cr);
            cairo_restore(cr);
        }
        if (alpha) {
            cairo_pop_group_to_source(cr);
            cairo_paint_with_alpha(cr, d->cfg.opacity);
        }
    }
}

static void layer_painted(void *ud)
{
    kk_danmaku *d = ud;
    d->gone = (kk_rect){0, 0, 0, 0};
    for (int i = 0; i < d->n; i++)
        d->items[i].drawn = where(&d->items[i]);
}

const kk_layer *kk_danmaku_layer(kk_danmaku *d)
{
    return &d->layer;
}

int kk_danmaku_count(const kk_danmaku *d)
{
    return d->n;
}

kk_rect kk_danmaku_rect(const kk_danmaku *d, int i)
{
    return where(&d->items[i]);
}

int kk_danmaku_lane_height(const kk_danmaku *d)
{
    return d->lane_h;
}
