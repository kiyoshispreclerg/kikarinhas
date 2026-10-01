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
    KK_ST_APPROACH, /* walking up to another avatar to hug/attack it */
} kk_state;

typedef enum {
    KK_ACT_NONE,
    KK_ACT_HUG,
    KK_ACT_ATTACK,
} kk_action;

#define KK_MAX_GEAR 12

/* A worn gear piece. sheet is NULL if its image could not be loaded. */
typedef struct {
    int set;
    const kk_sa_piece *piece;
    int piece_index; /* inside its set */
    const kk_sheet *sheet;
} kk_worn;

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
    const kk_sheet *sheet; /* already recoloured when a palette is chosen */
    int palette;           /* index in def->palettes, -1 = original colours */
    kk_worn gear[KK_MAX_GEAR];
    int n_gear;
    double clock;   /* seconds alive, drives animated gear */
    int gear_tick;  /* animated gear frame counter */
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

    char *goal_key;     /* KK_ST_APPROACH: user_id of the other avatar */
    kk_action goal_act;
    bool arrived;       /* reached the goal: the stage performs the action */

    kk_rect drawn; /* area covered on the last paint */
    int drawn_frame;
    int drawn_row;
    bool drawn_left;
    unsigned drawn_serial;
    int drawn_gear_tick;
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

/* Changes the avatar's look, keeping position and name. Worn gear the new
 * avatar can't wear is dropped. */
void kk_avatar_set_look(kk_avatar *a, const kk_sa_avatar *def,
                        const kk_sheet *sheet, int palette, kk_rng *rng);

/* Wears piece (replacing any piece of the same set). */
void kk_avatar_wear(kk_avatar *a, int set, const kk_sa_piece *piece,
                    int piece_index, const kk_sheet *sheet);
/* Takes off the piece of set, or everything with set < 0. */
void kk_avatar_unwear(kk_avatar *a, int set);

/* Plays the custom animation called name (e.g. "dance"); with name NULL a
 * random one fit for an emote. False if there is none. */
bool kk_avatar_emote(kk_avatar *a, const char *name, kk_rng *rng);
bool kk_avatar_sit(kk_avatar *a);

/* Walks to the avatar whose user_id is key; the stage keeps target updated
 * and performs act once arrived is set. */
void kk_avatar_approach(kk_avatar *a, const char *key, kk_action act);
/* Ends an approach (done or given up) and goes back to idle. */
void kk_avatar_stop(kk_avatar *a, kk_rng *rng);

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
