/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef KK_AVATAR_H
#define KK_AVATAR_H

#include <stdbool.h>

#include <cairo.h>

#include "emotes.h"
#include "sa.h"
#include "sprite.h"
#include "util.h"

/* An emote image inside a speech bubble: a hole in the bubble surface the
 * avatar fills with the current frame of anim each time it paints. The
 * stage fills anim in once the image is loaded (platform/id/url are what it
 * asks the cache for until then). */
#define KK_BUBBLE_MAX_EMOTES 12
typedef struct {
    int x, y, size; /* in the bubble surface */
    kk_emote_anim *anim; /* referenced; NULL while loading */
    char *platform, *id, *url; /* owned; NULL once resolved */
} kk_bubble_emote;

typedef enum {
    KK_ST_IDLE,
    KK_ST_WALK,
    KK_ST_SIT,     /* playing the sit animation */
    KK_ST_SITTING, /* holding the last sit frame */
    KK_ST_STAND,   /* playing the stand-up animation */
    KK_ST_JUMP,
    KK_ST_EMOTE, /* playing a custom animation (dance, ...) */
    KK_ST_APPROACH, /* walking up to another avatar to hug/attack it */
    KK_ST_FLY,      /* thrown (slingshot, a bomb): moving on both axes */
    KK_ST_HELD,     /* picked up with the mouse: hangs where it is put */
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

/* Where the avatars live: ground line and window width, plus what the
 * stage decides to show (configurable). */
typedef struct {
    int ground_y;
    int width;
    bool show_names;
    bool name_above; /* false: below the feet (default); true: above the head */
    double name_idle; /* > 0: the name hides after this many quiet seconds */
} kk_view;

/* Avatar size limits for kk_avatar_set_size (1 = the stage's scale). */
#define KK_AVATAR_MIN_SIZE 0.25
#define KK_AVATAR_MAX_SIZE 4.0

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
    kk_bubble_emote bubble_emotes[KK_BUBBLE_MAX_EMOTES];
    int n_bubble_emotes;
    double bubble_age; /* seconds since the bubble appeared (animations) */
    unsigned drawn_emote_sig; /* frames of the emotes as last painted */

    double x;      /* horizontal centre, px */
    double lift;   /* height above the ground while jumping, px */
    double vy;     /* px/s, positive is up */
    double vx;     /* px/s while flying */
    double angle;  /* radians, while flying (tumbling); 0 otherwise */
    double spin;   /* radians/s */
    double target; /* walk destination x; while held, x on the last update */
    double speed;  /* walk speed, px/s, at size 1 */
    double size;   /* 1 = the stage's scale; see kk_avatar_set_size */
    bool left;     /* facing left (sheets face right) */
    bool frozen;   /* stays where it is: no wandering, no walking */
    unsigned id;   /* set by the stage, unique while it lives */

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
    double drawn_angle;
    unsigned drawn_serial;
    int drawn_gear_tick;
} kk_avatar;

/* label and user_id (may be NULL) are copied. tag is owned by the avatar
 * from now on. */
void kk_avatar_init(kk_avatar *a, const kk_sa_avatar *def, const kk_sheet *sheet,
                    const char *label, const char *user_id,
                    cairo_surface_t *tag, double x, double scale, kk_rng *rng);
void kk_avatar_free(kk_avatar *a);

/* Advances behaviour and animation; the avatar wanders in [0, v->width)
 * and flies below the top of the window. */
void kk_avatar_update(kk_avatar *a, double dt, const kk_view *v, kk_rng *rng);

/* Makes the avatar jump now (if it is on the ground). */
void kk_avatar_jump(kk_avatar *a);

/* Throws the avatar: vx, vy in px/s (vy positive is up), spinning at spin
 * radians/s. It bounces off the sides and the ground until it settles. */
void kk_avatar_launch(kk_avatar *a, double vx, double vy, double spin);
bool kk_avatar_flying(const kk_avatar *a);

/* Picked up with the mouse: it stops whatever it was doing and hangs,
 * swinging as it is carried, until thrown. Meanwhile nothing else moves
 * it (jumps, emotes, walks, bombs). */
void kk_avatar_hold(kk_avatar *a);
bool kk_avatar_held(const kk_avatar *a);
/* Puts the held avatar at x (centre) and lift, kept inside the window. */
void kk_avatar_hold_at(kk_avatar *a, double x, double lift, const kk_view *v);
/* Lets go: it flies off at vx, vy px/s (vy positive is up) and lands. */
void kk_avatar_throw(kk_avatar *a, double vx, double vy);

/* Whether (x, y) is on the sprite: a non-transparent pixel within a few
 * pixels of it, so thin avatars can be grabbed too. */
bool kk_avatar_hit(const kk_avatar *a, const kk_view *v, int x, int y);

/* Walks to x (kept inside [0, width)). False if it can't now: frozen, in
 * the air, or already there. */
bool kk_avatar_walk_to(kk_avatar *a, double x, int width);
/* Gets up if sitting; false otherwise. */
bool kk_avatar_stand(kk_avatar *a);
/* Bigger or smaller, from KK_AVATAR_MIN_SIZE to KK_AVATAR_MAX_SIZE. */
void kk_avatar_set_size(kk_avatar *a, double size);

/* Height of the sprite on screen and where its middle is (for things that
 * hit it, like a collectible). */
double kk_avatar_height(const kk_avatar *a);
double kk_avatar_width(const kk_avatar *a);

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
/* The same with emote images to paint over the bubble; the avatar takes
 * over what the n slots own. */
void kk_avatar_say_emotes(kk_avatar *a, cairo_surface_t *bubble, double seconds,
                          const kk_bubble_emote *slots, int n);
/* Gives a loaded image to the slot that was waiting for it. */
void kk_bubble_emote_resolve(kk_bubble_emote *e, kk_emote_anim *anim);

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
