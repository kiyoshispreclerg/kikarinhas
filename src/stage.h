/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef KK_STAGE_H
#define KK_STAGE_H

#include <stdbool.h>
#include <stdint.h>

#include <cairo.h>
#include <pango/pangocairo.h>

#include "avatar.h"
#include "sa.h"
#include "sprite.h"
#include "util.h"

#define KK_MAX_DAMAGE 16

/* Everything on screen: the avatars, the sheets they share and the
 * bookkeeping of which areas need repainting. */
typedef struct {
    const kk_sa_library *lib;
    double scale;
    int width, height;
    int ground_margin; /* px between the ground line and the window bottom */
    kk_rng rng;

    /* Sheets are loaded on first use, one per library avatar. */
    kk_sheet *sheets;
    signed char *sheet_state; /* 0 not tried, 1 loaded, -1 failed */

    kk_avatar *avatars;
    int count, cap;

    PangoContext *pango;
    PangoFontDescription *font;

    kk_rect damage[KK_MAX_DAMAGE];
    int n_damage;
} kk_stage;

/* ground_margin < 0 picks room for the name tags. */
int kk_stage_init(kk_stage *s, const kk_sa_library *lib, double scale,
                  int ground_margin, uint64_t seed);
void kk_stage_free(kk_stage *s);
void kk_stage_resize(kk_stage *s, int width, int height);

/* Loads (once) the sheet of def at the stage scale; NULL if unusable. */
const kk_sheet *kk_stage_sheet(kk_stage *s, const kk_sa_avatar *def);

/* Adds an avatar at a random spot. */
int kk_stage_spawn(kk_stage *s, const kk_sa_avatar *def, const char *label);

void kk_stage_update(kk_stage *s, double dt);

/* Repaints what changed (everything if full) into cr and returns the
 * painted rectangles in *rects. */
int kk_stage_render(kk_stage *s, cairo_t *cr, bool full,
                    const kk_rect **rects);

#endif
