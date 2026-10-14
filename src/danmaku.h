/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef KK_DANMAKU_H
#define KK_DANMAKU_H

/* Danmaku: chat messages flying across the window from right to left, as on
 * Niconico and Bilibili. Only the text, on one line, with an outline and no
 * name; image emotes stay in it as pictures. When it is on, chat messages
 * get no speech bubble (main.c turns them off).
 *
 * The window is cut into lanes one line tall. A new message takes a lane at
 * random among those where it can't catch up with the one ahead (each
 * message crosses in the same time, so longer ones go faster); among those,
 * the ones that cross the avatars least, so the text goes over the empty
 * part of the stage when it can. With every lane taken, the one that frees
 * up first.
 *
 * Each message is rendered once to a surface and copied every frame. Drawn
 * as a stage layer. */

#include <stdbool.h>
#include <stdint.h>

#include "chat.h"
#include "emotes.h"
#include "layer.h"
#include "util.h"

typedef struct {
    bool enabled;
    double seconds;     /* to cross the window */
    const char *font;   /* Pango family and style; NULL: "Sans Bold". Copied */
    double size;        /* points */
    uint32_t color;     /* 0xRRGGBB: the text */
    uint32_t outline;
    uint32_t paid_color;   /* Super Chats, bits... */
    uint32_t member_color; /* new members */
    double opacity;     /* 0 to 1 */
    bool avoid_avatars;
    int max_on_screen;  /* past this, new messages are dropped */
} kk_danmaku_config;

typedef struct kk_danmaku kk_danmaku;

/* emotes (may be NULL: emotes stay as text) must outlive it. */
kk_danmaku *kk_danmaku_new(kk_emotes *emotes, uint64_t seed);
void kk_danmaku_free(kk_danmaku *d);

/* On start and on reload. Turning it off clears the screen; a new font is
 * used from the next message on. */
void kk_danmaku_configure(kk_danmaku *d, const kk_danmaku_config *cfg);
bool kk_danmaku_enabled(const kk_danmaku *d);
void kk_danmaku_resize(kk_danmaku *d, int width, int height);

/* Sends m across, starting at now (monotonic seconds). avoid are the areas
 * the avatars cover now (n of them). False if it went nowhere: off, full,
 * or nothing to show. */
bool kk_danmaku_message(kk_danmaku *d, const kk_chat_msg *m, const kk_rect *avoid,
                        int n, double now);

/* Moves everything to time now. */
void kk_danmaku_update(kk_danmaku *d, double now);

const kk_layer *kk_danmaku_layer(kk_danmaku *d);

/* For tests: messages on screen, and where message i is now. */
int kk_danmaku_count(const kk_danmaku *d);
kk_rect kk_danmaku_rect(const kk_danmaku *d, int i);
/* Height of a lane, px. */
int kk_danmaku_lane_height(const kk_danmaku *d);

#endif
