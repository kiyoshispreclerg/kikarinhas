/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef KK_AVATAR_H
#define KK_AVATAR_H

#include <stdbool.h>

#include <cairo.h>

#include "sa.h"
#include "sprite.h"
#include "util.h"

typedef enum {
    KK_ST_IDLE,
    KK_ST_WALK,
    KK_ST_SIT,     /* playing the sit animation */
    KK_ST_SITTING, /* holding the last sit frame */
    KK_ST_STAND,   /* playing the stand-up animation */
    KK_ST_JUMP,
    KK_ST_EMOTE, /* playing a custom animation (dance, ...) */
} kk_state;

/* Animation player for one row of a sheet. */
typedef struct {
    int row;
    int frame;
    double t;     /* time into the current frame */
    int loops;    /* completed loops */
    int max_loops; /* 0 = forever */
    double hold;  /* extra time on the last frame once finished */
    bool done;
} kk_anim;

typedef struct {
    const kk_sa_avatar *def;
    const kk_sheet *sheet;
    char *label;
    cairo_surface_t *tag; /* pre-rendered name tag */
    int tag_w, tag_h;

    double x;      /* horizontal centre, px */
    double lift;   /* height above the ground while jumping, px */
    double vy;     /* px/s, positive is up */
    double target; /* walk destination x */
    double speed;  /* walk speed, px/s */
    bool left;     /* facing left (sheets face right) */

    kk_state state;
    double timer; /* seconds left in the current state, where timed */
    kk_anim anim;

    kk_rect drawn; /* area covered on the last paint */
    int drawn_frame;
    int drawn_row;
    bool drawn_left;
} kk_avatar;

/* label is copied. tag is owned by the avatar from now on. */
void kk_avatar_init(kk_avatar *a, const kk_sa_avatar *def, const kk_sheet *sheet,
                    const char *label, cairo_surface_t *tag, double x,
                    double scale, kk_rng *rng);
void kk_avatar_free(kk_avatar *a);

/* Advances behaviour and animation; the avatar wanders in [0, width). */
void kk_avatar_update(kk_avatar *a, double dt, int width, kk_rng *rng);

/* Makes the avatar jump now (if it is on the ground). */
void kk_avatar_jump(kk_avatar *a);

/* Area the avatar covers when standing on ground_y. */
kk_rect kk_avatar_bounds(const kk_avatar *a, int ground_y);

/* Paints sprite and name tag (may run several times per frame, once per
 * damaged region). */
void kk_avatar_draw(const kk_avatar *a, cairo_t *cr, int ground_y);

/* Records the current pose as painted, after the frame went out. */
void kk_avatar_mark_drawn(kk_avatar *a, int ground_y);

/* True if the next paint would differ from the last one. */
bool kk_avatar_changed(const kk_avatar *a, int ground_y);

#endif
