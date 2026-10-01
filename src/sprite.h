/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef KK_SPRITE_H
#define KK_SPRITE_H

#include <stdbool.h>

#include <cairo.h>

/* A spritesheet cropped to the cells in use. Pixel art is kept at its
 * original size and enlarged with nearest filtering when drawn (cheap in
 * pixman, and 4x less memory at scale 2); smooth sheets are pre-scaled once
 * since bilinear filtering per frame would cost real CPU. */
typedef struct {
    cairo_surface_t *pixels; /* avatars face right in the source */
    int store_w, store_h;    /* cell size inside pixels */
    int cell_w, cell_h;      /* on-screen cell size */
    int cols, rows;
    int foot_pad; /* on-screen transparent rows under the feet in idle/walk */
    int head_pad; /* same, above the head */
} kk_sheet;

/* Loads png, cut in frame_w x frame_h cells, scaled by scale. smooth picks
 * bilinear filtering instead of nearest (pixel art). Only the first
 * max_rows x max_cols cells are kept (0 = all). */
int kk_sheet_load(kk_sheet *s, const char *png, int frame_w, int frame_h,
                  double scale, bool smooth, int max_rows, int max_cols);
void kk_sheet_free(kk_sheet *s);

/* Draws cell (row, col) with its top-left corner at (x, y). */
void kk_sheet_draw(const kk_sheet *s, cairo_t *cr, int row, int col, bool left,
                   int x, int y);

#endif
