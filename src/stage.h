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
#include "users.h"
#include "util.h"

#define KK_MAX_DAMAGE 16

typedef struct {
    double scale;
    int ground_margin; /* < 0: room for two rows of name tags (or 4px, see show_names) */
    int max_avatars;   /* chatters on screen at once */
    double despawn;    /* seconds of silence before a chatter leaves */
    const kk_sa_avatar *default_avatar; /* NULL: one per person, by hash */
    kk_users *users; /* saved choices; may be NULL */
    uint64_t seed;
    bool show_names;   /* name tag under (or over) each avatar */
    bool name_above;   /* false: below the feet; true: above the head */
    bool show_bubbles; /* speech bubble with the chat message */
    /* Pango family and style ("Sans Bold") plus size in points; NULL or 0
     * keep the defaults. Copied, so the strings may go away. */
    const char *name_font, *bubble_font;
    double name_size, bubble_size;
} kk_stage_config;

/* A sheet recoloured with one of its avatar's palettes. */
typedef struct kk_palette_sheet {
    struct kk_palette_sheet *next;
    int avatar, palette;
    kk_sheet sheet;
    bool ok;
} kk_palette_sheet;

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
    kk_palette_sheet *palette_sheets;
    kk_sheet *gear_sheets; /* by piece id, loaded on first use */
    signed char *gear_state;

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

/* Changes the font of the bubbles drawn from now on (open ones keep theirs).
 * Name tags are rendered once and the ground margin is measured from them,
 * so their font only comes from kk_stage_init. */
void kk_stage_set_bubble_font(kk_stage *s, const char *font, double size);

/* Loads (once) the sheet of def at the stage scale; NULL if unusable. */
const kk_sheet *kk_stage_sheet(kk_stage *s, const kk_sa_avatar *def);

/* Adds an avatar that is not tied to anyone in the chat. */
int kk_stage_spawn(kk_stage *s, const kk_sa_avatar *def, const char *label);

/* The sender's avatar: found, or spawned with their saved choices. Marks
 * them as active. NULL if no avatar could be made. */
kk_avatar *kk_stage_chatter(kk_stage *s, const kk_chat_msg *msg);

/* Jump and show the message in a bubble. */
void kk_stage_say(kk_stage *s, kk_avatar *a, const kk_chat_msg *msg);

/* Look changes; they are saved for next time when a->user_id is set.
 * palette -1 = original colours. */
bool kk_stage_set_avatar(kk_stage *s, kk_avatar *a, const kk_sa_avatar *def);
bool kk_stage_set_palette(kk_stage *s, kk_avatar *a, int palette);
bool kk_stage_wear(kk_stage *s, kk_avatar *a, const char *piece_name);
void kk_stage_unwear_all(kk_stage *s, kk_avatar *a);

/* Shows text in a blue bubble over a (the first title_len bytes in bold, at
 * most lines lines, up for seconds), even with show_bubbles off. */
void kk_stage_help_bubble(kk_stage *s, kk_avatar *a, const char *text,
                          int title_len, int lines, double seconds);

/* Avatar whose name tag matches name (case-insensitive, "@" ignored). */
kk_avatar *kk_stage_find_by_name(kk_stage *s, const char *name);
/* A random avatar other than not, or NULL. */
kk_avatar *kk_stage_random_other(kk_stage *s, const kk_avatar *not);

/* a walks to b and hugs/attacks it. */
bool kk_stage_interact(kk_stage *s, kk_avatar *a, kk_avatar *b, kk_action act);

void kk_stage_update(kk_stage *s, double dt);

/* Repaints what changed (everything if full) into cr and returns the
 * painted rectangles in *rects. */
int kk_stage_render(kk_stage *s, cairo_t *cr, bool full,
                    const kk_rect **rects);

#endif
