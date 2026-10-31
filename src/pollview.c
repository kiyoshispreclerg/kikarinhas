/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The open poll on the stage (see pollview.h). */
#include "pollview.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <pango/pangocairo.h>

#define DEFAULT_FONT "Sans"
#define MARGIN 10 /* around the panel and between the cards */
#define PAD 10    /* inside a card */
#define RADIUS 12
#define BAR_H 8
#define STRIP_H 4 /* the coloured floor of each part */

struct kk_pollview {
    kk_stage *stage;
    char *font_name;
    double size, results_seconds;
    bool show_panel;
    int align;
    double y_pct;
    PangoContext *pango;

    uint64_t id;       /* shown (0: none) */
    bool open;         /* still taking votes */
    double closed_at;  /* Unix time it was seen closed */
    bool zoned;        /* some avatar has a zone */

    /* What the panel shows, to render it again only when it changes. */
    cairo_surface_t *panel;
    int pw, ph;
    unsigned serial;
    long left;         /* seconds left as shown; -1 none */
    int width, height; /* stage size it was made for */
    int n_parts;       /* options of the poll shown (0: none) */

    /* What is on screen. */
    kk_rect panel_drawn;
    bool fresh;       /* the panel changed: paint it again */
    bool relayout;    /* the parts changed: paint the whole window */
    kk_layer layer;
};

static void layer_damage(void *ud, kk_damage_fn add, void *to);
static void layer_draw(void *ud, cairo_t *cr, kk_rect clip);
static void layer_painted(void *ud);

kk_pollview *kk_pollview_new(kk_stage *stage)
{
    kk_pollview *v = calloc(1, sizeof *v);
    if (!v)
        return NULL;
    v->stage = stage;
    v->size = 16;
    v->results_seconds = 15;
    v->show_panel = true;
    v->left = -1;
    v->pango = pango_font_map_create_context(pango_cairo_font_map_get_default());
    v->layer = (kk_layer){layer_damage, layer_draw, layer_painted, v};
    return v;
}

static void drop_panel(kk_pollview *v)
{
    if (v->panel)
        cairo_surface_destroy(v->panel);
    v->panel = NULL;
    v->pw = v->ph = 0;
}

void kk_pollview_free(kk_pollview *v)
{
    if (!v)
        return;
    drop_panel(v);
    free(v->font_name);
    g_object_unref(v->pango);
    free(v);
}

void kk_pollview_configure(kk_pollview *v, const kk_pollview_config *cfg)
{
    free(v->font_name);
    v->font_name = cfg->font && cfg->font[0] ? strdup(cfg->font) : NULL;
    v->size = cfg->size > 0 ? cfg->size : 16;
    v->results_seconds = cfg->results_seconds >= 0 ? cfg->results_seconds : 0;
    v->show_panel = cfg->panel;
    v->align = cfg->align;
    v->y_pct = cfg->y < 0 ? 0 : cfg->y > 90 ? 90 : cfg->y;
    v->serial = ~0u; /* render again */
    v->relayout = true;
}

void kk_pollview_zone(int width, int n, int i, double *lo, double *hi)
{
    *lo = n > 0 ? (double)width * i / n : 0;
    *hi = n > 0 ? (double)width * (i + 1) / n : width;
}

/* ---- the panel ------------------------------------------------------------ */

typedef struct {
    double r, g, b;
} rgb;

static rgb option_rgb(int i)
{
    static const rgb colors[] = {
        {0.90, 0.30, 0.30}, {0.25, 0.55, 0.95}, {0.95, 0.70, 0.15}, {0.30, 0.75, 0.40},
        {0.65, 0.40, 0.90}, {0.10, 0.75, 0.80}, {0.95, 0.45, 0.70}, {0.55, 0.70, 0.20},
        {0.95, 0.55, 0.25}, {0.50, 0.55, 0.65},
    };
    return colors[i % (int)(sizeof colors / sizeof colors[0])];
}

static void rounded_rect(cairo_t *cr, double x, double y, double w, double h, double r)
{
    if (r > h / 2)
        r = h / 2;
    cairo_new_path(cr);
    cairo_arc(cr, x + w - r, y + r, r, -M_PI / 2, 0);
    cairo_arc(cr, x + w - r, y + h - r, r, 0, M_PI / 2);
    cairo_arc(cr, x + r, y + h - r, r, M_PI / 2, M_PI);
    cairo_arc(cr, x + r, y + r, r, M_PI, 3 * M_PI / 2);
    cairo_close_path(cr);
}

static PangoLayout *text_layout(kk_pollview *v, const char *text, double scale, bool bold,
                                int width, int lines)
{
    PangoFontDescription *f =
        pango_font_description_from_string(v->font_name ? v->font_name : DEFAULT_FONT);
    pango_font_description_set_size(f, (gint)lround(v->size * scale * PANGO_SCALE));
    if (bold)
        pango_font_description_set_weight(f, PANGO_WEIGHT_BOLD);
    PangoLayout *l = pango_layout_new(v->pango);
    pango_layout_set_font_description(l, f);
    pango_font_description_free(f);
    pango_layout_set_width(l, (width > 10 ? width : 10) * PANGO_SCALE);
    pango_layout_set_wrap(l, PANGO_WRAP_WORD_CHAR);
    pango_layout_set_height(l, -lines);
    pango_layout_set_ellipsize(l, PANGO_ELLIPSIZE_END);
    pango_layout_set_text(l, text, -1);
    return l;
}

static int layout_h(PangoLayout *l)
{
    PangoRectangle r;
    pango_layout_get_pixel_extents(l, NULL, &r);
    return r.height;
}

static int layout_w(PangoLayout *l)
{
    PangoRectangle r;
    pango_layout_get_pixel_extents(l, NULL, &r);
    return r.width;
}

/* Text with a thin dark outline, readable over anything. */
static void show_outlined(cairo_t *cr, PangoLayout *l, double x, double y, rgb c)
{
    cairo_move_to(cr, x, y);
    pango_cairo_layout_path(cr, l);
    cairo_set_line_width(cr, 3);
    cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
    cairo_set_source_rgba(cr, 0, 0, 0, 0.75);
    cairo_stroke(cr);
    cairo_move_to(cr, x, y);
    cairo_set_source_rgb(cr, c.r, c.g, c.b);
    pango_cairo_show_layout(cr, l);
}

static void status_text(const kk_pollview *v, const kk_poll *p, char *out, size_t size)
{
    int total = kk_poll_total(p, -1);
    char votes[64] = "";
    if (p->show_counts)
        snprintf(votes, sizeof votes, " · %d %s", total, total == 1 ? "voto" : "votos");
    if (!v->open)
        snprintf(out, size, "Enquete encerrada%s", votes);
    else if (v->left >= 0)
        snprintf(out, size, "Vote no chat · encerra em %ld:%02ld%s", v->left / 60, v->left % 60,
                 votes);
    else
        snprintf(out, size, "Vote no chat%s", votes);
}

/* The panel: question and status on top, then one card per option, each as
 * wide as its part of the window. */
static void render_panel(kk_pollview *v, const kk_poll *p)
{
    drop_panel(v);
    int W = v->stage->width, n = p->n_options;
    if (!v->show_panel || W <= 2 * MARGIN || n < 1)
        return;
    PangoAlignment palign = v->align == 1   ? PANGO_ALIGN_LEFT
                            : v->align == 2 ? PANGO_ALIGN_RIGHT
                                            : PANGO_ALIGN_CENTER;
    char status[160];
    status_text(v, p, status, sizeof status);
    PangoLayout *q = text_layout(v, p->question, 1.25, true, W - 4 * MARGIN, 2);
    PangoLayout *st = text_layout(v, status, 0.8, false, W - 4 * MARGIN, 1);
    pango_layout_set_alignment(q, palign);
    pango_layout_set_alignment(st, palign);
    int head_h = layout_h(q) + 4 + layout_h(st);

    int total = kk_poll_total(p, -1), best = 0;
    for (int i = 0; i < n; i++)
        if (kk_poll_votes(p, i, -1) > best)
            best = kk_poll_votes(p, i, -1);

    PangoLayout *badge[KK_POLL_MAX_OPTIONS], *text[KK_POLL_MAX_OPTIONS],
        *alt[KK_POLL_MAX_OPTIONS], *count[KK_POLL_MAX_OPTIONS];
    int card_h = 0;
    for (int i = 0; i < n; i++) {
        double lo, hi;
        kk_pollview_zone(W, n, i, &lo, &hi);
        int inner = (int)(hi - lo) - MARGIN - 2 * PAD;
        char word[64], sc[64];
        kk_poll_words(p, i, word, sizeof word, sc, sizeof sc);
        badge[i] = text_layout(v, word, 1.0, true, inner, 1);
        text[i] = text_layout(v, p->options[i].text, 1.0, false, inner, 3);
        char altw[80] = "";
        if (sc[0])
            snprintf(altw, sizeof altw, "ou %s", sc);
        alt[i] = text_layout(v, altw, 0.75, false, inner, 1);
        int votes = kk_poll_votes(p, i, -1);
        char cnt[64] = "";
        if (p->show_counts)
            snprintf(cnt, sizeof cnt, "%d %s · %d%%", votes, votes == 1 ? "voto" : "votos",
                     total ? (int)lround(votes * 100.0 / total) : 0);
        count[i] = text_layout(v, cnt, 0.8, true, inner, 1);
        int h = PAD + layout_h(badge[i]) + 4 + layout_h(text[i]) +
                (sc[0] ? 2 + layout_h(alt[i]) : 0) +
                (p->show_counts ? 6 + BAR_H + 4 + layout_h(count[i]) : 0) + PAD;
        if (h > card_h)
            card_h = h;
    }

    v->pw = W;
    v->ph = MARGIN + head_h + MARGIN + card_h + 2;
    v->panel = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, v->pw, v->ph);
    cairo_t *cr = cairo_create(v->panel);

    /* The question, in a dark band: in the middle or on one side. Both
     * texts are laid out as wide as the window less the margins, aligned
     * the same way; tx is where that box goes. */
    int qw = layout_w(q) > layout_w(st) ? layout_w(q) : layout_w(st);
    double bw = fmin(qw + 4 * PAD, W - 2 * MARGIN), bx, tx;
    if (v->align == 1) {
        bx = MARGIN;
        tx = bx + 2 * PAD;
    } else if (v->align == 2) {
        bx = W - MARGIN - bw;
        tx = bx + bw - 2 * PAD - (W - 4 * MARGIN);
    } else {
        bx = (W - bw) / 2.0;
        tx = 2 * MARGIN;
    }
    rounded_rect(cr, bx, MARGIN - 4, bw, head_h + 8, RADIUS);
    cairo_set_source_rgba(cr, 0.08, 0.07, 0.12, 0.78);
    cairo_fill(cr);
    cairo_move_to(cr, tx, MARGIN);
    cairo_set_source_rgb(cr, 1, 1, 1);
    pango_cairo_show_layout(cr, q);
    cairo_move_to(cr, tx, MARGIN + layout_h(q) + 4);
    cairo_set_source_rgba(cr, 1, 1, 1, 0.8);
    pango_cairo_show_layout(cr, st);

    int top = MARGIN + head_h + MARGIN;
    for (int i = 0; i < n; i++) {
        double lo, hi;
        kk_pollview_zone(W, n, i, &lo, &hi);
        double x = lo + MARGIN / 2.0, w = hi - lo - MARGIN;
        rgb c = option_rgb(i);
        int votes = kk_poll_votes(p, i, -1);
        bool winner = !v->open && best > 0 && votes == best;
        rounded_rect(cr, x, top, w, card_h, RADIUS);
        cairo_set_source_rgba(cr, 0.08, 0.07, 0.12, winner ? 0.88 : 0.72);
        cairo_fill_preserve(cr);
        cairo_set_line_width(cr, winner ? 4 : 2);
        if (winner)
            cairo_set_source_rgb(cr, 1.0, 0.82, 0.2);
        else
            cairo_set_source_rgba(cr, c.r, c.g, c.b, 0.9);
        cairo_stroke(cr);

        double y = top + PAD;
        PangoRectangle br;
        pango_layout_get_pixel_extents(badge[i], NULL, &br);
        rounded_rect(cr, x + PAD - 6, y - 2, br.width + 12, br.height + 4, 8);
        cairo_set_source_rgb(cr, c.r, c.g, c.b);
        cairo_fill(cr);
        cairo_move_to(cr, x + PAD - br.x, y - br.y);
        cairo_set_source_rgb(cr, 1, 1, 1);
        pango_cairo_show_layout(cr, badge[i]);
        if (winner) {
            PangoLayout *star = text_layout(v, "★", 1.0, true, 100, 1);
            show_outlined(cr, star, x + PAD + br.width + 12, y - br.y, (rgb){1.0, 0.82, 0.2});
            g_object_unref(star);
        }
        y += br.height + 4;
        show_outlined(cr, text[i], x + PAD, y, (rgb){1, 1, 1});
        y += layout_h(text[i]);
        if (pango_layout_get_text(alt[i])[0]) {
            y += 2;
            cairo_move_to(cr, x + PAD, y);
            cairo_set_source_rgba(cr, 1, 1, 1, 0.7);
            pango_cairo_show_layout(cr, alt[i]);
            y += layout_h(alt[i]);
        }
        if (p->show_counts) {
            /* Pinned to the bottom, so the bars line up. */
            y = top + card_h - PAD - layout_h(count[i]) - 4 - BAR_H;
            double bar_w = w - 2 * PAD;
            rounded_rect(cr, x + PAD, y, bar_w, BAR_H, BAR_H / 2.0);
            cairo_set_source_rgba(cr, 1, 1, 1, 0.18);
            cairo_fill(cr);
            if (total > 0 && votes > 0) {
                rounded_rect(cr, x + PAD, y, fmax(BAR_H, bar_w * votes / total), BAR_H,
                             BAR_H / 2.0);
                cairo_set_source_rgb(cr, c.r, c.g, c.b);
                cairo_fill(cr);
            }
            y += BAR_H + 4;
            show_outlined(cr, count[i], x + PAD, y, (rgb){1, 1, 1});
        }
    }
    cairo_destroy(cr);
    for (int i = 0; i < n; i++) {
        g_object_unref(badge[i]);
        g_object_unref(text[i]);
        g_object_unref(alt[i]);
        g_object_unref(count[i]);
    }
    g_object_unref(q);
    g_object_unref(st);
}

/* ---- following the poll --------------------------------------------------- */

static void clear_zones(kk_pollview *v)
{
    if (!v->zoned)
        return;
    for (int i = 0; i < v->stage->count; i++)
        kk_avatar_set_zone(&v->stage->avatars[i], 0, 0);
    v->zoned = false;
}

static void hide(kk_pollview *v)
{
    clear_zones(v);
    if (v->id)
        v->relayout = true;
    v->id = 0;
    v->n_parts = 0;
    v->open = false;
    drop_panel(v);
}

void kk_pollview_update(kk_pollview *v, const kk_polls *ps, double now)
{
    const kk_poll *p = kk_polls_active(ps);
    if (p && p->id != v->id) {
        hide(v);
        v->id = p->id;
        v->n_parts = p->n_options;
        v->open = true;
        v->serial = ~0u;
        v->relayout = true;
    } else if (!p && v->id) {
        p = kk_polls_find(ps, v->id);
        if (v->open) { /* closed just now */
            v->open = false;
            v->closed_at = now;
            v->serial = ~0u;
            clear_zones(v);
        }
        if (!p || now - v->closed_at >= v->results_seconds) {
            hide(v);
            return;
        }
    }
    if (!p)
        return;

    long left = -1;
    if (v->open && kk_poll_ends_at(p)) {
        left = (long)ceil((double)kk_poll_ends_at(p) - now);
        if (left < 0)
            left = 0;
    }
    if (v->height != v->stage->height) {
        v->height = v->stage->height;
        v->relayout = true;
    }
    if (kk_polls_serial(ps) != v->serial || left != v->left || v->width != v->stage->width) {
        if (v->width != v->stage->width)
            v->relayout = true;
        v->serial = kk_polls_serial(ps);
        v->left = left;
        v->width = v->stage->width;
        render_panel(v, p);
        v->fresh = true;
    }

    if (!v->open)
        return;
    /* Voters stay in their part; everyone else goes anywhere. */
    int W = v->stage->width;
    for (int i = 0; i < v->stage->count; i++) {
        kk_avatar *a = &v->stage->avatars[i];
        int opt = a->user_id ? kk_poll_vote_of(p, a->user_id) : -1;
        double lo = 0, hi = 0;
        if (opt >= 0) {
            kk_pollview_zone(W, p->n_options, opt, &lo, &hi);
            v->zoned = true;
        }
        if (a->zone_lo != lo || a->zone_hi != hi) {
            kk_avatar_set_zone(a, lo, hi);
            /* Off to the new part, even from a seat. */
            if (opt >= 0)
                kk_avatar_stand(a);
        }
    }
}

/* ---- the layer ------------------------------------------------------------ */

/* The cards' top: y_pct of the window, but never past the bottom. */
static int panel_y(const kk_pollview *v)
{
    int y = (int)lround(v->stage->height * v->y_pct / 100.0);
    if (y + v->ph > v->stage->height)
        y = v->stage->height - v->ph;
    return y < 0 ? 0 : y;
}

static kk_rect panel_rect(const kk_pollview *v)
{
    return v->panel ? (kk_rect){0, panel_y(v), v->pw, v->ph} : (kk_rect){0, 0, 0, 0};
}

static void layer_damage(void *ud, kk_damage_fn add, void *to)
{
    kk_pollview *v = ud;
    if (v->relayout) {
        add(to, (kk_rect){0, 0, v->stage->width, v->stage->height});
        return;
    }
    if (v->fresh) {
        kk_rect r = panel_rect(v);
        add(to, kk_rect_empty(v->panel_drawn) ? r
                : kk_rect_empty(r)            ? v->panel_drawn
                                              : kk_rect_union(r, v->panel_drawn));
    }
}

static void layer_draw(void *ud, cairo_t *cr, kk_rect clip)
{
    kk_pollview *v = ud;
    const kk_rect pr = panel_rect(v);
    if (v->panel && kk_rect_intersects(pr, clip)) {
        cairo_set_source_surface(cr, v->panel, 0, pr.y);
        cairo_paint(cr);
    }
    /* The parts: a line between each two, and a coloured floor. */
    kk_view view = kk_stage_view(v->stage);
    int W = v->stage->width, n = v->n_parts;
    if (n < 1)
        return;
    /* Lines only under the cards: without them, only the floor. */
    double y0 = pr.y + pr.h, y1 = view.ground_y;
    cairo_save(cr);
    cairo_set_line_width(cr, 2);
    double dash[] = {8, 6};
    for (int i = 1; v->panel && i < n && y1 > y0; i++) {
        double lo, hi;
        kk_pollview_zone(W, n, i, &lo, &hi);
        double x = floor(lo) + 0.5;
        cairo_set_dash(cr, NULL, 0, 0);
        cairo_set_source_rgba(cr, 0, 0, 0, 0.35);
        cairo_move_to(cr, x + 1, y0);
        cairo_line_to(cr, x + 1, y1);
        cairo_stroke(cr);
        cairo_set_dash(cr, dash, 2, 0);
        cairo_set_source_rgba(cr, 1, 1, 1, 0.55);
        cairo_move_to(cr, x, y0);
        cairo_line_to(cr, x, y1);
        cairo_stroke(cr);
    }
    cairo_set_dash(cr, NULL, 0, 0);
    for (int i = 0; i < n; i++) {
        double lo, hi;
        kk_pollview_zone(W, n, i, &lo, &hi);
        rgb c = option_rgb(i);
        cairo_rectangle(cr, lo + 3, view.ground_y, hi - lo - 6, STRIP_H);
        cairo_set_source_rgba(cr, c.r, c.g, c.b, 0.6);
        cairo_fill(cr);
    }
    cairo_restore(cr);
}

static void layer_painted(void *ud)
{
    kk_pollview *v = ud;
    v->panel_drawn = panel_rect(v);
    v->fresh = v->relayout = false;
}

const kk_layer *kk_pollview_layer(kk_pollview *v)
{
    return &v->layer;
}

uint64_t kk_pollview_shown(const kk_pollview *v)
{
    return v->id;
}

kk_rect kk_pollview_panel_rect(const kk_pollview *v)
{
    return panel_rect(v);
}
