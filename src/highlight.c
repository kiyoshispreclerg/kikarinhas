/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The highlighted comment (see highlight.h). */
#include "highlight.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <pango/pangocairo.h>

#define DEFAULT_FONT "Sans"
#define PAD 14     /* inside the card */
#define SHADOW 4   /* the card's shadow, down and right */
#define RADIUS 14
#define FADE_IN 0.25
#define FADE_OUT 0.35
#define MAX_TEXT 1000
/* U+FFFC OBJECT REPLACEMENT CHARACTER: where an emote image goes. */
#define HOLE "\xEF\xBF\xBC"

struct kk_highlight {
    kk_stage *stage;
    kk_emotes *images;
    kk_highlight_config cfg;
    char *font_name;
    PangoContext *pango;

    bool on;        /* something shown, maybe fading out */
    char *key;      /* "platform:id" of the author, for the spotlight */
    cairo_surface_t *card;
    int cw, ch;
    kk_bubble_emote slots[KK_BUBBLE_MAX_EMOTES];
    int n_slots;
    double t0, t_out; /* shown at; fading out since (< 0: not) */
    double now;

    /* What is on screen since the last paint, to repaint only changes. */
    kk_rect card_drawn, spot_drawn;
    double card_alpha_drawn, spot_alpha_drawn;
    kk_rect gone; /* where things were taken off */
    bool fresh;   /* a new card: paint it whole */
    kk_layer layer, spot_layer;
};

static void layer_damage(void *ud, kk_damage_fn add, void *to);
static void layer_draw(void *ud, cairo_t *cr, kk_rect clip);
static void layer_painted(void *ud);
static void spot_damage(void *ud, kk_damage_fn add, void *to);
static void spot_draw(void *ud, cairo_t *cr, kk_rect clip);
static void spot_painted(void *ud);

kk_highlight *kk_highlight_new(kk_stage *stage, kk_emotes *emotes)
{
    kk_highlight *h = calloc(1, sizeof *h);
    if (!h)
        return NULL;
    h->stage = stage;
    h->images = emotes;
    h->cfg = (kk_highlight_config){.card = true, .x = 50, .y = 25, .width = 50, .size = 18,
                                   .spotlight = true};
    h->pango = pango_font_map_create_context(pango_cairo_font_map_get_default());
    h->layer = (kk_layer){layer_damage, layer_draw, layer_painted, h};
    h->spot_layer = (kk_layer){spot_damage, spot_draw, spot_painted, h};
    h->t_out = -1;
    return h;
}

static void drop_card(kk_highlight *h)
{
    if (h->card)
        cairo_surface_destroy(h->card);
    h->card = NULL;
    for (int i = 0; i < h->n_slots; i++) {
        kk_bubble_emote_resolve(&h->slots[i], NULL);
        kk_emote_anim_unref(h->slots[i].anim);
    }
    h->n_slots = 0;
}

void kk_highlight_free(kk_highlight *h)
{
    if (!h)
        return;
    drop_card(h);
    free(h->key);
    free(h->font_name);
    g_object_unref(h->pango);
    free(h);
}

void kk_highlight_configure(kk_highlight *h, const kk_highlight_config *cfg)
{
    free(h->font_name);
    h->font_name = cfg->font ? strdup(cfg->font) : NULL;
    h->cfg = *cfg;
    h->cfg.font = NULL;
    h->fresh = true; /* moved or hidden: paint again */
}

/* ---- the card ------------------------------------------------------------------ */

typedef struct {
    double r, g, b;
} rgb;

/* The name tags' colours, darker so they read on the light card. */
static rgb name_rgb(unsigned badges)
{
    if (badges & KK_BADGE_OWNER)
        return (rgb){0.62, 0.42, 0.0};
    if (badges & KK_BADGE_MOD)
        return (rgb){0.15, 0.33, 0.78};
    if (badges & KK_BADGE_MEMBER)
        return (rgb){0.1, 0.52, 0.2};
    return (rgb){0.36, 0.2, 0.46};
}

static bool append(char *out, size_t size, size_t *used, const char *s, size_t len)
{
    if (*used + len + 1 > size)
        return false;
    memcpy(out + *used, s, len);
    *used += len;
    out[*used] = '\0';
    return true;
}

static void attr_range(PangoAttrList *attrs, PangoAttribute *a, size_t from, size_t to)
{
    a->start_index = (guint)from;
    a->end_index = (guint)to;
    pango_attr_list_insert(attrs, a);
}

static void rounded_rect(cairo_t *cr, double x, double y, double w, double h, double r)
{
    cairo_new_path(cr);
    cairo_arc(cr, x + w - r, y + r, r, -M_PI / 2, 0);
    cairo_arc(cr, x + w - r, y + h - r, r, 0, M_PI / 2);
    cairo_arc(cr, x + r, y + h - r, r, M_PI / 2, M_PI);
    cairo_arc(cr, x + r, y + r, r, M_PI, 3 * M_PI / 2);
    cairo_close_path(cr);
}

static PangoFontDescription *card_font(const kk_highlight *h)
{
    PangoFontDescription *f = pango_font_description_from_string(
        h->font_name && h->font_name[0] ? h->font_name : DEFAULT_FONT);
    pango_font_description_set_size(f, (gint)lround((h->cfg.size > 0 ? h->cfg.size : 18) *
                                                    PANGO_SCALE));
    return f;
}

static int line_px(kk_highlight *h, const PangoFontDescription *f)
{
    PangoLayout *layout = pango_layout_new(h->pango);
    pango_layout_set_font_description(layout, f);
    pango_layout_set_text(layout, "Ág", -1);
    PangoRectangle logical;
    pango_layout_get_pixel_extents(layout, NULL, &logical);
    g_object_unref(layout);
    return logical.height;
}

static void render_card(kk_highlight *h, const kk_chat_msg *m)
{
    PangoFontDescription *font = card_font(h);
    int px = line_px(h, font);
    char text[MAX_TEXT + 256];
    size_t used = 0;
    text[0] = '\0';
    PangoAttrList *attrs = pango_attr_list_new();
    append(text, sizeof text, &used, m->name, strlen(m->name));
    rgb nc = name_rgb(m->badges);
    attr_range(attrs, pango_attr_weight_new(PANGO_WEIGHT_BOLD), 0, used);
    attr_range(attrs,
               pango_attr_foreground_new((guint16)(nc.r * 65535), (guint16)(nc.g * 65535),
                                         (guint16)(nc.b * 65535)),
               0, used);
    if (m->kind == KK_MSG_PAID && m->amount && m->amount[0]) {
        append(text, sizeof text, &used, "  ", 2);
        size_t from = used;
        append(text, sizeof text, &used, m->amount, strlen(m->amount));
        attr_range(attrs, pango_attr_weight_new(PANGO_WEIGHT_ULTRABOLD), from, used);
    }
    if (m->text[0])
        append(text, sizeof text, &used, "\n", 1);

    /* The text, with image emotes as holes. */
    int idx[KK_BUBBLE_MAX_EMOTES];
    size_t len = strlen(m->text), pos = 0;
    for (int i = 0; h->images && i < m->n_emotes && h->n_slots < KK_BUBBLE_MAX_EMOTES; i++) {
        const kk_emote *em = &m->emotes[i];
        if (!em->url || em->len == 0 || em->start < pos || em->start + em->len > len ||
            used + (em->start - pos) + 3 + 1 > sizeof text)
            continue;
        kk_emote_anim *anim;
        kk_emote_state st = kk_emotes_get_anim(h->images, m->platform, em, &anim);
        if (st == KK_EMOTE_FAILED)
            continue;
        append(text, sizeof text, &used, m->text + pos, em->start - pos);
        idx[h->n_slots] = (int)used;
        append(text, sizeof text, &used, HOLE, 3);
        PangoRectangle r = {0, -px * PANGO_SCALE * 4 / 5, px * PANGO_SCALE, px * PANGO_SCALE};
        attr_range(attrs, pango_attr_shape_new(&r, &r), used - 3, used);
        pos = em->start + em->len;
        kk_bubble_emote *s = &h->slots[h->n_slots++];
        *s = (kk_bubble_emote){.size = px};
        if (st == KK_EMOTE_READY) {
            s->anim = kk_emote_anim_ref(anim);
        } else {
            s->platform = strdup(m->platform);
            s->id = strdup(em->id ? em->id : "");
            s->url = strdup(em->url);
        }
    }
    size_t rest = len - pos;
    if (rest > sizeof text - used - 1)
        rest = sizeof text - used - 1;
    append(text, sizeof text, &used, m->text + pos, rest);

    int max_w = (int)(h->stage->width * h->cfg.width / 100.0) - 2 * PAD - SHADOW;
    if (max_w < 120)
        max_w = 120;
    PangoLayout *layout = pango_layout_new(h->pango);
    pango_layout_set_font_description(layout, font);
    pango_layout_set_width(layout, max_w * PANGO_SCALE);
    pango_layout_set_wrap(layout, PANGO_WRAP_WORD_CHAR);
    pango_layout_set_height(layout, -8); /* lines at most */
    pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_END);
    pango_layout_set_text(layout, text, -1);
    pango_layout_set_attributes(layout, attrs);
    pango_attr_list_unref(attrs);
    PangoRectangle logical;
    pango_layout_get_pixel_extents(layout, NULL, &logical);

    int bw = logical.width + 2 * PAD, bh = logical.height + 2 * PAD;
    h->cw = bw + SHADOW;
    h->ch = bh + SHADOW;
    h->card = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, h->cw, h->ch);
    cairo_t *cr = cairo_create(h->card);
    rounded_rect(cr, SHADOW, SHADOW, bw, bh, RADIUS);
    cairo_set_source_rgba(cr, 0, 0, 0, 0.28);
    cairo_fill(cr);
    rgb fill = m->kind == KK_MSG_PAID     ? (rgb){1.0, 0.84, 0.35}
               : m->kind == KK_MSG_MEMBER ? (rgb){0.62, 0.95, 0.68}
                                          : (rgb){1, 1, 1};
    rounded_rect(cr, 0.75, 0.75, bw - 1.5, bh - 1.5, RADIUS);
    cairo_set_source_rgba(cr, fill.r, fill.g, fill.b, 0.97);
    cairo_fill_preserve(cr);
    cairo_set_line_width(cr, 1.5);
    cairo_set_source_rgba(cr, 0.1, 0.08, 0.12, 0.4);
    cairo_stroke(cr);

    pango_cairo_update_layout(cr, layout);
    double ox = PAD - logical.x, oy = PAD - logical.y;
    cairo_move_to(cr, ox, oy);
    cairo_set_source_rgb(cr, 0.1, 0.09, 0.12);
    pango_cairo_show_layout(cr, layout);
    for (int i = 0; i < h->n_slots; i++) {
        PangoRectangle at;
        pango_layout_index_to_pos(layout, idx[i], &at);
        h->slots[i].x = (int)ox + at.x / PANGO_SCALE;
        h->slots[i].y = (int)oy + at.y / PANGO_SCALE + (at.height / PANGO_SCALE - px) / 2;
    }
    cairo_destroy(cr);
    g_object_unref(layout);
    pango_font_description_free(font);
}

void kk_highlight_show(kk_highlight *h, const kk_chat_msg *m, double now)
{
    if (h->on)
        h->gone = kk_rect_union(h->gone, kk_rect_union(h->card_drawn, h->spot_drawn));
    drop_card(h);
    free(h->key);
    h->key = NULL;
    char key[256];
    snprintf(key, sizeof key, "%s:%s", m->platform ? m->platform : "",
             m->user_id ? m->user_id : "");
    h->key = strdup(key);
    render_card(h, m);
    h->on = true;
    h->t0 = now;
    h->t_out = -1;
    h->now = now;
    h->fresh = true;
}

void kk_highlight_clear(kk_highlight *h, double now)
{
    if (h->on && h->t_out < 0)
        h->t_out = now;
}

bool kk_highlight_active(const kk_highlight *h)
{
    return h->on && h->t_out < 0;
}

/* 0 to 1: fading in, shown, fading out. */
static double alpha(const kk_highlight *h)
{
    if (!h->on)
        return 0;
    double a = fmin(1.0, (h->now - h->t0) / FADE_IN);
    if (h->t_out >= 0)
        a = fmin(a, 1.0 - (h->now - h->t_out) / FADE_OUT);
    return a < 0 ? 0 : a;
}

void kk_highlight_update(kk_highlight *h, double now)
{
    h->now = now;
    if (!h->on)
        return;
    if (h->t_out < 0 && h->cfg.seconds > 0 && now - h->t0 >= h->cfg.seconds)
        h->t_out = now;
    if (h->t_out >= 0 && now - h->t_out >= FADE_OUT) {
        /* Gone: what was painted must be painted over. */
        h->gone = kk_rect_union(h->gone, kk_rect_union(h->card_drawn, h->spot_drawn));
        drop_card(h);
        free(h->key);
        h->key = NULL;
        h->on = false;
        return;
    }
    for (int k = 0; h->images && k < h->n_slots; k++) {
        kk_bubble_emote *s = &h->slots[k];
        if (s->anim || !s->url)
            continue;
        kk_emote em = {.id = s->id, .url = s->url};
        kk_emote_anim *anim;
        kk_emote_state st = kk_emotes_get_anim(h->images, s->platform, &em, &anim);
        if (st != KK_EMOTE_LOADING) {
            kk_bubble_emote_resolve(s, st == KK_EMOTE_READY ? anim : NULL);
            h->fresh = true;
        }
    }
}

static kk_rect card_rect(const kk_highlight *h)
{
    if (!h->on || !h->card || !h->cfg.card)
        return (kk_rect){0, 0, 0, 0};
    int W = h->stage->width, H = h->stage->height;
    int x = (int)lround(W * h->cfg.x / 100.0) - h->cw / 2;
    int y = (int)lround(H * h->cfg.y / 100.0) - h->ch / 2;
    x = x + h->cw > W ? W - h->cw : x;
    y = y + h->ch > H ? H - h->ch : y;
    return (kk_rect){x < 0 ? 0 : x, y < 0 ? 0 : y, h->cw, h->ch};
}

static bool card_moving(const kk_highlight *h)
{
    for (int k = 0; k < h->n_slots; k++)
        if (h->slots[k].anim && h->slots[k].anim->n > 1)
            return true;
    return false;
}

static void layer_damage(void *ud, kk_damage_fn add, void *to)
{
    kk_highlight *h = ud;
    if (!kk_rect_empty(h->gone))
        add(to, h->gone);
    kk_rect r = card_rect(h);
    bool changed = h->fresh || alpha(h) != h->card_alpha_drawn || card_moving(h);
    if (changed && !kk_rect_empty(r))
        add(to, r);
    if (h->fresh && !kk_rect_empty(h->card_drawn))
        add(to, h->card_drawn);
}

static void layer_draw(void *ud, cairo_t *cr, kk_rect clip)
{
    kk_highlight *h = ud;
    kk_rect r = card_rect(h);
    if (!kk_rect_intersects(r, clip))
        return;
    double a = alpha(h);
    /* Comes in growing a little from its centre. */
    double grow = 0.92 + 0.08 * fmin(1.0, (h->now - h->t0) / FADE_IN);
    cairo_save(cr);
    cairo_translate(cr, r.x + r.w / 2.0, r.y + r.h / 2.0);
    cairo_scale(cr, grow, grow);
    cairo_translate(cr, -r.w / 2.0, -r.h / 2.0);
    cairo_push_group(cr);
    cairo_set_source_surface(cr, h->card, 0, 0);
    cairo_paint(cr);
    for (int k = 0; k < h->n_slots; k++) {
        const kk_bubble_emote *s = &h->slots[k];
        if (!s->anim)
            continue;
        cairo_surface_t *f = kk_emote_anim_frame(s->anim, h->now - h->t0);
        double sc = (double)s->size / cairo_image_surface_get_width(f);
        cairo_save(cr);
        cairo_translate(cr, s->x, s->y);
        cairo_scale(cr, sc, sc);
        cairo_set_source_surface(cr, f, 0, 0);
        cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_GOOD);
        cairo_paint(cr);
        cairo_restore(cr);
    }
    cairo_pop_group_to_source(cr);
    cairo_paint_with_alpha(cr, a);
    cairo_restore(cr);
}

static void layer_painted(void *ud)
{
    kk_highlight *h = ud;
    h->card_drawn = card_rect(h);
    h->card_alpha_drawn = alpha(h);
    h->gone = (kk_rect){0, 0, 0, 0};
    h->fresh = false;
}

/* ---- the spotlight --------------------------------------------------------- */

typedef struct {
    double cx, feet, top_half, foot_half;
} spot_geom;

static bool spot_geometry(const kk_highlight *h, spot_geom *g)
{
    if (!h->on || !h->cfg.spotlight || !h->key)
        return false;
    kk_avatar *a = kk_stage_find_user(h->stage, h->key);
    if (!a)
        return false;
    kk_view v = kk_stage_view(h->stage);
    double w = kk_avatar_width(a);
    g->cx = a->x;
    g->feet = v.ground_y - a->lift;
    g->foot_half = fmax(w * 0.8, 28);
    g->top_half = g->foot_half * 0.35;
    return true;
}

static kk_rect spot_rect_of(const spot_geom *g)
{
    int x0 = (int)floor(g->cx - g->foot_half) - 2, x1 = (int)ceil(g->cx + g->foot_half) + 2;
    int y1 = (int)ceil(g->feet + g->foot_half * 0.3) + 2;
    return (kk_rect){x0, 0, x1 - x0, y1};
}

static kk_rect spot_rect(const kk_highlight *h)
{
    spot_geom g;
    return spot_geometry(h, &g) ? spot_rect_of(&g) : (kk_rect){0, 0, 0, 0};
}

static void spot_damage(void *ud, kk_damage_fn add, void *to)
{
    kk_highlight *h = ud;
    kk_rect r = spot_rect(h);
    bool changed = !kk_rect_equal(r, h->spot_drawn) || alpha(h) != h->spot_alpha_drawn;
    if (!changed)
        return;
    if (!kk_rect_empty(r))
        add(to, kk_rect_empty(h->spot_drawn) ? r : kk_rect_union(r, h->spot_drawn));
    else if (!kk_rect_empty(h->spot_drawn))
        add(to, h->spot_drawn);
}

static void spot_draw(void *ud, cairo_t *cr, kk_rect clip)
{
    kk_highlight *h = ud;
    spot_geom g;
    if (!spot_geometry(h, &g) || !kk_rect_intersects(spot_rect_of(&g), clip))
        return;
    double a = alpha(h);
    /* A cone of warm light from the top of the window down to the feet. */
    cairo_pattern_t *beam = cairo_pattern_create_linear(0, 0, 0, g.feet);
    cairo_pattern_add_color_stop_rgba(beam, 0, 1.0, 0.95, 0.75, 0.0);
    cairo_pattern_add_color_stop_rgba(beam, 1, 1.0, 0.95, 0.75, 0.32 * a);
    cairo_new_path(cr);
    cairo_move_to(cr, g.cx - g.top_half, 0);
    cairo_line_to(cr, g.cx + g.top_half, 0);
    cairo_line_to(cr, g.cx + g.foot_half, g.feet);
    cairo_line_to(cr, g.cx - g.foot_half, g.feet);
    cairo_close_path(cr);
    cairo_set_source(cr, beam);
    cairo_fill(cr);
    cairo_pattern_destroy(beam);
    /* And where it lands. */
    cairo_save(cr);
    cairo_translate(cr, g.cx, g.feet);
    cairo_scale(cr, 1.0, 0.25);
    cairo_pattern_t *pool = cairo_pattern_create_radial(0, 0, 0, 0, 0, g.foot_half);
    cairo_pattern_add_color_stop_rgba(pool, 0, 1.0, 0.95, 0.75, 0.55 * a);
    cairo_pattern_add_color_stop_rgba(pool, 1, 1.0, 0.95, 0.75, 0.0);
    cairo_arc(cr, 0, 0, g.foot_half, 0, 2 * M_PI);
    cairo_set_source(cr, pool);
    cairo_fill(cr);
    cairo_pattern_destroy(pool);
    cairo_restore(cr);
}

static void spot_painted(void *ud)
{
    kk_highlight *h = ud;
    h->spot_drawn = spot_rect(h);
    h->spot_alpha_drawn = alpha(h);
}

const kk_layer *kk_highlight_layer(kk_highlight *h)
{
    return &h->layer;
}

const kk_layer *kk_highlight_spot_layer(kk_highlight *h)
{
    return &h->spot_layer;
}

kk_rect kk_highlight_card_rect(const kk_highlight *h)
{
    return card_rect(h);
}

kk_rect kk_highlight_spot_rect(const kk_highlight *h)
{
    return spot_rect(h);
}
