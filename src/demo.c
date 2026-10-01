/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "demo.h"

#include <math.h>
#include <stdio.h>

#define CARD_W 280.0
#define CARD_H 128.0
#define RADIUS 18.0
/* Extra pixels around the card covered by the stroke and antialiasing. */
#define MARGIN 3

static void clamp_position(kk_demo *d)
{
    double max_x = d->width - CARD_W, max_y = d->height - CARD_H;
    d->x = fmax(0.0, fmin(d->x, max_x));
    d->y = fmax(0.0, fmin(d->y, max_y));
}

void kk_demo_init(kk_demo *d, int width, int height)
{
    *d = (kk_demo){
        .width = width,
        .height = height,
        .x = width / 4.0,
        .y = height / 3.0,
        .vx = 170.0,
        .vy = 110.0,
    };
    clamp_position(d);

    d->pango = pango_font_map_create_context(pango_cairo_font_map_get_default());

    PangoFontDescription *font = pango_font_description_from_string("Sans Bold 24");
    d->title = pango_layout_new(d->pango);
    pango_layout_set_font_description(d->title, font);
    pango_layout_set_text(d->title, "Kikarinhas", -1);
    pango_font_description_free(font);

    font = pango_font_description_from_string("Sans 11");
    d->status = pango_layout_new(d->pango);
    pango_layout_set_font_description(d->status, font);
    pango_layout_set_text(d->status, "fase 0", -1);
    pango_font_description_free(font);

    d->card = cairo_image_surface_create(CAIRO_FORMAT_ARGB32,
                                         (int)CARD_W + 2 * MARGIN,
                                         (int)CARD_H + 2 * MARGIN);
    d->card_dirty = true;
}

void kk_demo_free(kk_demo *d)
{
    cairo_surface_destroy(d->card);
    g_object_unref(d->status);
    g_object_unref(d->title);
    g_object_unref(d->pango);
}

void kk_demo_resize(kk_demo *d, int width, int height)
{
    d->width = width;
    d->height = height;
    clamp_position(d);
}

static void bounce(double *pos, double *vel, double max)
{
    if (*pos < 0.0) {
        *pos = -*pos;
        *vel = fabs(*vel);
    } else if (*pos > max) {
        *pos = fmax(0.0, 2.0 * max - *pos);
        *vel = -fabs(*vel);
    }
}

void kk_demo_update(kk_demo *d, double dt)
{
    d->x += d->vx * dt;
    d->y += d->vy * dt;
    bounce(&d->x, &d->vx, d->width - CARD_W);
    bounce(&d->y, &d->vy, d->height - CARD_H);

    d->clock += dt;
    if (d->clock >= 1.0) {
        d->fps = (int)lround(d->frames / d->clock);
        d->frames = 0;
        d->clock = 0.0;

        char text[64];
        snprintf(text, sizeof text, "fase 0 · %d fps", d->fps);
        pango_layout_set_text(d->status, text, -1);
        d->card_dirty = true;
    }
}

static kk_rect rect_union(kk_rect a, kk_rect b)
{
    if (a.w <= 0 || a.h <= 0)
        return b;
    int x0 = a.x < b.x ? a.x : b.x;
    int y0 = a.y < b.y ? a.y : b.y;
    int x1 = a.x + a.w > b.x + b.w ? a.x + a.w : b.x + b.w;
    int y1 = a.y + a.h > b.y + b.h ? a.y + a.h : b.y + b.h;
    return (kk_rect){x0, y0, x1 - x0, y1 - y0};
}

static void rounded_rect(cairo_t *cr, double x, double y, double w, double h,
                         double r)
{
    cairo_new_sub_path(cr);
    cairo_arc(cr, x + w - r, y + r, r, -M_PI / 2, 0);
    cairo_arc(cr, x + w - r, y + h - r, r, 0, M_PI / 2);
    cairo_arc(cr, x + r, y + h - r, r, M_PI / 2, M_PI);
    cairo_arc(cr, x + r, y + r, r, M_PI, 3 * M_PI / 2);
    cairo_close_path(cr);
}

/* White text with a dark outline, readable over any background. */
static void outlined_text(cairo_t *cr, PangoLayout *layout, double x, double y,
                          double outline)
{
    pango_cairo_update_layout(cr, layout);
    cairo_move_to(cr, x, y);
    pango_cairo_layout_path(cr, layout);
    cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
    cairo_set_line_width(cr, outline);
    cairo_set_source_rgba(cr, 0.1, 0.05, 0.12, 0.85);
    cairo_stroke_preserve(cr);
    cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
    cairo_fill(cr);
}

static void render_card(kk_demo *d)
{
    cairo_t *cr = cairo_create(d->card);
    double x = MARGIN, y = MARGIN;

    cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
    cairo_paint(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);

    /* Half-transparent body: should show the scene below it in OBS. */
    rounded_rect(cr, x + 1, y + 1, CARD_W - 2, CARD_H - 2, RADIUS);
    cairo_set_source_rgba(cr, 0.93, 0.36, 0.62, 0.55);
    cairo_fill_preserve(cr);
    cairo_set_line_width(cr, 2.0);
    cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.9);
    cairo_stroke(cr);

    outlined_text(cr, d->title, x + 20, y + 14, 4.0);
    outlined_text(cr, d->status, x + 22, y + 56, 3.0);

    /* Alpha ramp from 0 to 1: a smooth fade in OBS means real alpha. */
    double bx = x + 20, by = y + CARD_H - 34, bw = CARD_W - 40, bh = 14;
    cairo_pattern_t *ramp = cairo_pattern_create_linear(bx, 0, bx + bw, 0);
    cairo_pattern_add_color_stop_rgba(ramp, 0.0, 1.0, 1.0, 1.0, 0.0);
    cairo_pattern_add_color_stop_rgba(ramp, 1.0, 1.0, 1.0, 1.0, 1.0);
    rounded_rect(cr, bx, by, bw, bh, bh / 2);
    cairo_set_source(cr, ramp);
    cairo_fill(cr);
    cairo_pattern_destroy(ramp);

    cairo_destroy(cr);
    d->card_dirty = false;
}

void kk_demo_draw(kk_demo *d, cairo_t *cr, bool full, kk_rect *damage)
{
    if (d->card_dirty)
        render_card(d);

    /* Integer position: the blit stays a plain copy, no resampling. */
    kk_rect now = {(int)lround(d->x) - MARGIN, (int)lround(d->y) - MARGIN,
                   (int)CARD_W + 2 * MARGIN, (int)CARD_H + 2 * MARGIN};
    kk_rect area = full ? (kk_rect){0, 0, d->width, d->height}
                        : rect_union(d->last, now);

    cairo_save(cr);
    cairo_rectangle(cr, area.x, area.y, area.w, area.h);
    cairo_clip(cr);

    cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
    cairo_paint(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
    cairo_set_source_surface(cr, d->card, now.x, now.y);
    cairo_paint(cr);

    cairo_restore(cr);

    d->last = now;
    d->frames++;
    *damage = area;
}
