/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef KK_STAGE_H
#define KK_STAGE_H

#include <stdbool.h>
#include <stdint.h>

#include <cairo.h>
#include <pango/pangocairo.h>

#include "avatar.h"
#include "chat.h"
#include "sa.h"
#include "sprite.h"
#include "util.h"

#define KK_MAX_DAMAGE 16

typedef struct {
    double scale;
    int ground_margin; /* < 0: room for two rows of name tags */
    int max_avatars;   /* chatters on screen at once */
    double despawn;    /* seconds of silence before a chatter leaves */
    const kk_sa_avatar *default_avatar; /* NULL: one per person, by hash */
    uint64_t seed;
} kk_stage_config;

/* Everything on screen: the avatars, the sheets they share and the
 * bookkeeping of which areas need repainting. */
typedef struct {
    const kk_sa_library *lib;
    kk_stage_config cfg;
    int width, height;
    kk_rng rng;

    /* Sheets are loaded on first use, one per library avatar. */
    kk_sheet *sheets;
    signed char *sheet_state; /* 0 not tried, 1 loaded, -1 failed */
    int *usable; /* library indexes that look drawable */
    int n_usable;

    kk_avatar *avatars;
    int count, cap;

    PangoContext *pango;
    PangoFontDescription *tag_font;
    PangoFontDescription *bubble_font;

    kk_rect removed; /* area of avatars removed since the last paint */
    kk_rect damage[KK_MAX_DAMAGE];
    int n_damage;
} kk_stage;

int kk_stage_init(kk_stage *s, const kk_sa_library *lib,
                  const kk_stage_config *cfg);
void kk_stage_free(kk_stage *s);
void kk_stage_resize(kk_stage *s, int width, int height);

/* Loads (once) the sheet of def at the stage scale; NULL if unusable. */
const kk_sheet *kk_stage_sheet(kk_stage *s, const kk_sa_avatar *def);

/* Adds an avatar that is not tied to anyone in the chat. */
int kk_stage_spawn(kk_stage *s, const kk_sa_avatar *def, const char *label);

/* A chat message: the sender's avatar appears (or is found), jumps and
 * shows the text in a bubble. */
void kk_stage_chat(kk_stage *s, const kk_chat_msg *msg);

void kk_stage_update(kk_stage *s, double dt);

/* Repaints what changed (everything if full) into cr and returns the
 * painted rectangles in *rects. */
int kk_stage_render(kk_stage *s, cairo_t *cr, bool full,
                    const kk_rect **rects);

#endif
