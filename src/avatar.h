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

/* Where the avatars live: ground line and window width. */
typedef struct {
    int ground_y;
    int width;
} kk_view;

typedef struct {
    const kk_sa_avatar *def;
    const kk_sheet *sheet;
    char *label;
    char *user_id; /* "platform:id" of the chatter; NULL for demo avatars */
    double quiet;  /* seconds since the last message */
    cairo_surface_t *tag; /* pre-rendered name tag */
    int tag_w, tag_h;
    int tag_row; /* tags are stacked in rows so neighbours don't overlap */

    cairo_surface_t *bubble; /* speech bubble, NULL when silent */
    int bubble_w, bubble_h;
    double bubble_left; /* seconds until the bubble goes away */
    unsigned bubble_serial; /* bumps when the bubble changes */

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
    unsigned drawn_serial;
} kk_avatar;

/* label and user_id (may be NULL) are copied. tag is owned by the avatar
 * from now on. */
void kk_avatar_init(kk_avatar *a, const kk_sa_avatar *def, const kk_sheet *sheet,
                    const char *label, const char *user_id,
                    cairo_surface_t *tag, double x, double scale, kk_rng *rng);
void kk_avatar_free(kk_avatar *a);

/* Advances behaviour and animation; the avatar wanders in [0, width). */
void kk_avatar_update(kk_avatar *a, double dt, int width, kk_rng *rng);

/* Makes the avatar jump now (if it is on the ground). */
void kk_avatar_jump(kk_avatar *a);

/* Shows bubble (owned by the avatar from now on; NULL for none) for
 * seconds, replacing the current one. */
void kk_avatar_say(kk_avatar *a, cairo_surface_t *bubble, double seconds);

/* Area the avatar covers (sprite, tag and bubble). */
kk_rect kk_avatar_bounds(const kk_avatar *a, const kk_view *v);

/* Painting is split so every bubble goes above every body. Both may run
 * several times per frame, once per damaged region. */
void kk_avatar_draw_body(const kk_avatar *a, cairo_t *cr, const kk_view *v);
void kk_avatar_draw_bubble(const kk_avatar *a, cairo_t *cr, const kk_view *v);

/* Records the current pose as painted, after the frame went out. */
void kk_avatar_mark_drawn(kk_avatar *a, const kk_view *v);

/* True if the next paint would differ from the last one. */
bool kk_avatar_changed(const kk_avatar *a, const kk_view *v);

#endif
