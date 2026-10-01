/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "sprite.h"

#include <math.h>
#include <stdint.h>
#include <string.h>

#include "log.h"

/* Transparent rows at the bottom (or top) edge of a cell. */
static int cell_gap(cairo_surface_t *surf, int x0, int y0, int w, int h,
                    bool from_top)
{
    const unsigned char *data = cairo_image_surface_get_data(surf);
    int stride = cairo_image_surface_get_stride(surf);
    for (int gap = 0; gap < h; gap++) {
        int y = from_top ? y0 + gap : y0 + h - 1 - gap;
        const uint32_t *row =
            (const uint32_t *)(data + (size_t)y * (size_t)stride) + x0;
        for (int x = 0; x < w; x++)
            if (row[x] >> 24)
                return gap;
    }
    return h; /* empty cell */
}

/* Smallest gap over the idle and walk cells (rows 0 and 1: the poses that
 * stand on the ground), in on-screen pixels. */
static int compute_pad(const kk_sheet *s, bool top)
{
    int pad = s->store_h;
    for (int row = 0; row < 2 && row < s->rows; row++)
        for (int col = 0; col < s->cols; col++) {
            int gap = cell_gap(s->pixels, col * s->store_w, row * s->store_h,
                               s->store_w, s->store_h, top);
            if (gap < pad)
                pad = gap;
        }
    if (pad == s->store_h)
        return 0;
    return (int)lround((double)pad * s->cell_h / s->store_h);
}

/* Copies the cells in use into a store_w x store_h grid, scaling every cell
 * on its own (EXTEND_PAD on a sub-surface) so bilinear filtering never
 * bleeds pixels from the neighbouring cells. */
static cairo_surface_t *store_sheet(cairo_surface_t *src, const kk_sheet *s,
                                    int frame_w, int frame_h, bool smooth)
{
    cairo_surface_t *dst = cairo_image_surface_create(
        CAIRO_FORMAT_ARGB32, s->cols * s->store_w, s->rows * s->store_h);
    cairo_t *cr = cairo_create(dst);
    double sx = (double)s->store_w / frame_w, sy = (double)s->store_h / frame_h;

    for (int row = 0; row < s->rows; row++)
        for (int col = 0; col < s->cols; col++) {
            cairo_surface_t *cell = cairo_surface_create_for_rectangle(
                src, col * frame_w, row * frame_h, frame_w, frame_h);
            cairo_save(cr);
            cairo_rectangle(cr, col * s->store_w, row * s->store_h, s->store_w,
                            s->store_h);
            cairo_clip(cr);
            cairo_translate(cr, col * s->store_w, row * s->store_h);
            cairo_scale(cr, sx, sy);
            cairo_set_source_surface(cr, cell, 0, 0);
            cairo_pattern_t *p = cairo_get_source(cr);
            cairo_pattern_set_extend(p, CAIRO_EXTEND_PAD);
            cairo_pattern_set_filter(p, smooth ? CAIRO_FILTER_GOOD
                                               : CAIRO_FILTER_NEAREST);
            cairo_paint(cr);
            cairo_restore(cr);
            cairo_surface_destroy(cell);
        }
    cairo_destroy(cr);
    return dst;
}

int kk_sheet_load(kk_sheet *s, const char *png, int frame_w, int frame_h,
                  double scale, bool smooth, int max_rows, int max_cols)
{
    memset(s, 0, sizeof *s);
    if (frame_w <= 0 || frame_h <= 0 || scale <= 0.0)
        return -1;

    cairo_surface_t *src = cairo_image_surface_create_from_png(png);
    if (cairo_surface_status(src) != CAIRO_STATUS_SUCCESS) {
        kk_log_warn("%s: %s", png,
                    cairo_status_to_string(cairo_surface_status(src)));
        cairo_surface_destroy(src);
        return -1;
    }
    /* Some sheets carry a few extra pixels: ignore partial cells. */
    s->cols = cairo_image_surface_get_width(src) / frame_w;
    s->rows = cairo_image_surface_get_height(src) / frame_h;
    if (max_cols > 0 && s->cols > max_cols)
        s->cols = max_cols;
    if (max_rows > 0 && s->rows > max_rows)
        s->rows = max_rows;
    s->cell_w = (int)lround(frame_w * scale);
    s->cell_h = (int)lround(frame_h * scale);
    if (s->cols < 1 || s->rows < 1 || s->cell_w < 1 || s->cell_h < 1) {
        kk_log_warn("%s: imagem menor que um quadro de %dx%d", png, frame_w,
                    frame_h);
        cairo_surface_destroy(src);
        return -1;
    }

    s->store_w = smooth ? s->cell_w : frame_w;
    s->store_h = smooth ? s->cell_h : frame_h;
    s->pixels = store_sheet(src, s, frame_w, frame_h, smooth);
    cairo_surface_destroy(src);
    cairo_surface_flush(s->pixels);
    s->foot_pad = compute_pad(s, false);
    s->head_pad = compute_pad(s, true);
    return 0;
}

void kk_sheet_free(kk_sheet *s)
{
    if (s->pixels)
        cairo_surface_destroy(s->pixels);
    memset(s, 0, sizeof *s);
}

void kk_sheet_draw(const kk_sheet *s, cairo_t *cr, int row, int col, bool left,
                   int x, int y)
{
    if (row < 0 || row >= s->rows || col < 0 || col >= s->cols)
        return;
    cairo_save(cr);
    cairo_translate(cr, x, y);
    if (left) {
        cairo_translate(cr, s->cell_w, 0);
        cairo_scale(cr, -1, 1);
    }
    cairo_scale(cr, (double)s->cell_w / s->store_w,
                (double)s->cell_h / s->store_h);
    cairo_set_source_surface(cr, s->pixels, -col * s->store_w,
                             -row * s->store_h);
    /* Smooth sheets are already at screen size: nearest is an exact copy. */
    cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_NEAREST);
    cairo_rectangle(cr, 0, 0, s->store_w, s->store_h);
    cairo_fill(cr);
    cairo_restore(cr);
}
