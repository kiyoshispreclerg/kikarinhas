/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef KK_DEMO_H
#define KK_DEMO_H

#include <stdbool.h>

#include <cairo.h>
#include <pango/pangocairo.h>

#include "window.h"

/* Phase 0 test scene: a semi-transparent card bouncing around the window,
 * with an alpha ramp and outlined text, to check that OBS captures alpha. */
typedef struct {
    int width, height; /* area the card bounces in */
    double x, y;       /* top-left of the card */
    double vx, vy;     /* px/s */
    kk_rect last;      /* area painted on the previous frame */

    /* The card is rendered once into this surface and only blitted per
     * frame; it is re-rendered when the fps text changes. */
    cairo_surface_t *card;
    bool card_dirty;

    PangoContext *pango;
    PangoLayout *title;
    PangoLayout *status;

    double clock;    /* seconds since the last fps sample */
    int frames;      /* frames drawn since the last fps sample */
    int fps;         /* last measured frames per second */
} kk_demo;

void kk_demo_init(kk_demo *d, int width, int height);
void kk_demo_free(kk_demo *d);
void kk_demo_resize(kk_demo *d, int width, int height);
void kk_demo_update(kk_demo *d, double dt);

/* Paints into cr. With full set the whole area is repainted; otherwise only
 * the old and new card positions. The painted area is stored in damage. */
void kk_demo_draw(kk_demo *d, cairo_t *cr, bool full, kk_rect *damage);

#endif
