/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "sprite.h"

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
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

/* ---- dithering ----------------------------------------------------------- */

/* Share of opaque pixels in a checkerboard: unlike all four sides, like at
 * least three corners. Measured: dithered pony sheets 2.7-19%, most pixel
 * art under 1%. */
#define DITHERED 0.02
/* Real pixel art may dither on purpose (Pokémon wings), but with a short
 * palette (10-14 colours here); pictures squeezed into a GIF have more
 * (27-529). Only those are blended. */
#define PIXEL_ART_COLOURS 16
/* Sum of |dR|+|dG|+|dB| under which two neighbours are "the same colour,
 * dithered"; outlines and eyes are much further apart. */
#define BLEND_DIST 200

static double checker_share(const uint32_t *px, int w, int h)
{
    long opaque = 0, checker = 0;
    for (int y = 1; y < h - 1; y++)
        for (int x = 1; x < w - 1; x++) {
            uint32_t c = px[y * w + x];
            if (!(c >> 24))
                continue;
            opaque++;
            uint32_t n[4] = {px[y * w + x - 1], px[y * w + x + 1], px[(y - 1) * w + x],
                             px[(y + 1) * w + x]};
            uint32_t d[4] = {px[(y - 1) * w + x - 1], px[(y - 1) * w + x + 1],
                             px[(y + 1) * w + x - 1], px[(y + 1) * w + x + 1]};
            bool sides = true;
            for (int i = 0; i < 4 && sides; i++)
                sides = (n[i] >> 24) && n[i] != c;
            int corners = 0;
            for (int i = 0; i < 4; i++)
                corners += d[i] == c;
            checker += sides && corners >= 3;
        }
    return opaque ? (double)checker / (double)opaque : 0;
}

/* Distinct opaque colours, counting up to limit + 1. */
static int colours(const uint32_t *px, int w, int h, int limit)
{
    uint32_t seen[64];
    int n = 0;
    if (limit >= 64)
        limit = 63;
    for (long i = 0; i < (long)w * h && n <= limit; i++) {
        if (!(px[i] >> 24))
            continue;
        int k = 0;
        while (k < n && seen[k] != px[i])
            k++;
        if (k == n)
            seen[n++] = px[i];
    }
    return n;
}

static int colour_dist(uint32_t a, uint32_t b)
{
    int d = 0;
    for (int sh = 0; sh <= 16; sh += 8)
        d += abs((int)((a >> sh) & 0xff) - (int)((b >> sh) & 0xff));
    return d;
}

/* One pass: each opaque pixel with at least four opaque neighbours of a
 * close colour becomes their average (its own colour counted twice). */
static void blend_pass(const uint32_t *in, uint32_t *out, int w, int h)
{
    memcpy(out, in, (size_t)w * (size_t)h * sizeof *out);
    for (int y = 1; y < h - 1; y++)
        for (int x = 1; x < w - 1; x++) {
            uint32_t c = in[y * w + x];
            if ((c >> 24) != 0xff)
                continue;
            unsigned sum[3] = {2 * ((c >> 16) & 0xff), 2 * ((c >> 8) & 0xff), 2 * (c & 0xff)};
            int n = 2, near = 0;
            for (int dy = -1; dy <= 1; dy++)
                for (int dx = -1; dx <= 1; dx++) {
                    uint32_t v = in[(y + dy) * w + x + dx];
                    if ((!dx && !dy) || (v >> 24) != 0xff || colour_dist(v, c) > BLEND_DIST)
                        continue;
                    sum[0] += (v >> 16) & 0xff;
                    sum[1] += (v >> 8) & 0xff;
                    sum[2] += v & 0xff;
                    n++;
                    near++;
                }
            if (near >= 4)
                out[y * w + x] = 0xff000000u | (sum[0] / (unsigned)n) << 16 |
                                 (sum[1] / (unsigned)n) << 8 | sum[2] / (unsigned)n;
        }
}

/* Blends the dithering of src in place, if it has some; true if it did. */
static bool dedither(cairo_surface_t *src)
{
    if (cairo_image_surface_get_format(src) != CAIRO_FORMAT_ARGB32)
        return false;
    cairo_surface_flush(src);
    int w = cairo_image_surface_get_width(src), h = cairo_image_surface_get_height(src);
    int stride = cairo_image_surface_get_stride(src);
    unsigned char *data = cairo_image_surface_get_data(src);
    uint32_t *a = malloc((size_t)w * (size_t)h * sizeof *a);
    uint32_t *b = malloc((size_t)w * (size_t)h * sizeof *b);
    bool done = false;
    if (a && b) {
        for (int y = 0; y < h; y++)
            memcpy(a + (size_t)y * (size_t)w, data + (size_t)y * (size_t)stride,
                   (size_t)w * sizeof *a);
        if (checker_share(a, w, h) >= DITHERED &&
            colours(a, w, h, PIXEL_ART_COLOURS) > PIXEL_ART_COLOURS) {
            blend_pass(a, b, w, h);
            blend_pass(b, a, w, h);
            for (int y = 0; y < h; y++)
                memcpy(data + (size_t)y * (size_t)stride, a + (size_t)y * (size_t)w,
                       (size_t)w * sizeof *a);
            cairo_surface_mark_dirty(src);
            done = true;
        }
    }
    free(a);
    free(b);
    return done;
}

/* ---- storing ------------------------------------------------------------- */

/* Copies the cells in use into a store_w x store_h grid, scaling every cell
 * on its own (EXTEND_PAD on a sub-surface) so bilinear filtering never
 * bleeds pixels from the neighbouring cells. With grow > 1 (pixel art at a
 * fractional scale), each cell is first enlarged grow times with nearest. */
static cairo_surface_t *store_sheet(cairo_surface_t *src, const kk_sheet *s,
                                    int frame_w, int frame_h, bool smooth, int grow)
{
    cairo_surface_t *dst = cairo_image_surface_create(
        CAIRO_FORMAT_ARGB32, s->cols * s->store_w, s->rows * s->store_h);
    cairo_t *cr = cairo_create(dst);
    double sx = (double)s->store_w / frame_w, sy = (double)s->store_h / frame_h;

    for (int row = 0; row < s->rows; row++)
        for (int col = 0; col < s->cols; col++) {
            cairo_surface_t *cell = cairo_surface_create_for_rectangle(
                src, col * frame_w, row * frame_h, frame_w, frame_h);
            if (grow > 1) {
                cairo_surface_t *big = cairo_image_surface_create(
                    CAIRO_FORMAT_ARGB32, frame_w * grow, frame_h * grow);
                cairo_t *bc = cairo_create(big);
                cairo_scale(bc, grow, grow);
                cairo_set_source_surface(bc, cell, 0, 0);
                cairo_pattern_set_filter(cairo_get_source(bc), CAIRO_FILTER_NEAREST);
                cairo_paint(bc);
                cairo_destroy(bc);
                cairo_surface_destroy(cell);
                cell = big;
            }
            cairo_save(cr);
            cairo_rectangle(cr, col * s->store_w, row * s->store_h, s->store_w,
                            s->store_h);
            cairo_clip(cr);
            cairo_translate(cr, col * s->store_w, row * s->store_h);
            cairo_scale(cr, sx / grow, sy / grow);
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

static uint32_t premultiply(uint32_t c)
{
    uint32_t a = c >> 24;
    uint32_t r = ((c >> 16) & 0xff) * a / 255;
    uint32_t g = ((c >> 8) & 0xff) * a / 255;
    uint32_t b = (c & 0xff) * a / 255;
    return a << 24 | r << 16 | g << 8 | b;
}

/* cairo pixels are premultiplied ARGB32: compare in that form. */
static void apply_recolor(cairo_surface_t *surf, const kk_recolor *rc)
{
    uint32_t from[32], to[32];
    int n = rc->n < 32 ? rc->n : 32;
    for (int i = 0; i < n; i++) {
        from[i] = premultiply(rc->from[i]);
        to[i] = premultiply(rc->to[i]);
    }
    cairo_surface_flush(surf);
    unsigned char *data = cairo_image_surface_get_data(surf);
    int stride = cairo_image_surface_get_stride(surf);
    int w = cairo_image_surface_get_width(surf);
    int h = cairo_image_surface_get_height(surf);
    for (int y = 0; y < h; y++) {
        uint32_t *row = (uint32_t *)(data + (size_t)y * (size_t)stride);
        for (int x = 0; x < w; x++) {
            if (!(row[x] >> 24))
                continue;
            for (int i = 0; i < n; i++)
                if (row[x] == from[i]) {
                    row[x] = to[i];
                    break;
                }
        }
    }
    cairo_surface_mark_dirty(surf);
}

int kk_sheet_load(kk_sheet *s, const char *png, int frame_w, int frame_h,
                  double scale, bool smooth, int max_rows, int max_cols,
                  const kk_recolor *recolor)
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
    if (recolor && recolor->n > 0 &&
        cairo_image_surface_get_format(src) == CAIRO_FORMAT_ARGB32)
        apply_recolor(src, recolor);

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

    /* Pixel art that is dithered, or at a fractional scale, is pre-scaled
     * smoothly (see sprite.h). */
    int grow = 1;
    if (!smooth && dedither(src)) {
        s->dedithered = true;
        smooth = true;
    } else if (!smooth && fabs(scale - lround(scale)) > 0.01) {
        grow = (int)ceil(scale);
        smooth = true;
    }
    s->store_w = smooth ? s->cell_w : frame_w;
    s->store_h = smooth ? s->cell_h : frame_h;
    s->pixels = store_sheet(src, s, frame_w, frame_h, smooth, grow);
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
    kk_sheet_draw_zoom(s, cr, row, col, left, x, y, 1.0);
}

void kk_sheet_draw_zoom(const kk_sheet *s, cairo_t *cr, int row, int col, bool left,
                        double x, double y, double zoom)
{
    if (row < 0 || row >= s->rows || col < 0 || col >= s->cols)
        return;
    cairo_save(cr);
    cairo_translate(cr, x, y);
    if (zoom != 1.0)
        cairo_scale(cr, zoom, zoom);
    if (left) {
        cairo_translate(cr, s->cell_w, 0);
        cairo_scale(cr, -1, 1);
    }
    cairo_scale(cr, (double)s->cell_w / s->store_w,
                (double)s->cell_h / s->store_h);
    cairo_set_source_surface(cr, s->pixels, -col * s->store_w,
                             -row * s->store_h);
    /* Smooth sheets are already at screen size: nearest is an exact copy,
     * unless this one avatar is drawn bigger or smaller. */
    bool presized = s->store_w == s->cell_w && s->store_h == s->cell_h;
    cairo_pattern_set_filter(cairo_get_source(cr), presized && zoom != 1.0
                                                       ? CAIRO_FILTER_GOOD
                                                       : CAIRO_FILTER_NEAREST);
    cairo_rectangle(cr, 0, 0, s->store_w, s->store_h);
    cairo_fill(cr);
    cairo_restore(cr);
}
