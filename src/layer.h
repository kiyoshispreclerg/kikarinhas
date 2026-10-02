/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef KK_LAYER_H
#define KK_LAYER_H

#include <cairo.h>

#include "util.h"

/* Something the stage paints over the avatars (the emote wall, later other
 * effects) without knowing what it is. Each frame the stage asks every
 * layer which areas changed, merges them with its own, then repaints each
 * changed area: avatars first, layers on top, in the order they were
 * added. */

typedef void (*kk_damage_fn)(void *to, kk_rect r);

typedef struct {
    /* Reports, through add(to, rect), the areas that changed since the
     * last paint: where things were drawn and where they will be. */
    void (*damage)(void *ud, kk_damage_fn add, void *to);
    /* Draws whatever touches clip (cr is already clipped to it). */
    void (*draw)(void *ud, cairo_t *cr, kk_rect clip);
    /* The frame went out: what was drawn is now what is on screen. */
    void (*painted)(void *ud);
    void *ud;
} kk_layer;

#endif
