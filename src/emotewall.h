/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef KK_EMOTEWALL_H
#define KK_EMOTEWALL_H

/* The emote wall: emoji and emotes from the chat flying over the stage.
 *
 * It only sees platform-neutral events (kk_chat_msg, kk_reaction): Unicode
 * emoji are found in the text, image emotes come listed in the message.
 * What goes up is decided per message:
 *  - min_per_message: a message with at least that many emotes sends all
 *    of them (up to max_per_message);
 *  - combo: when the same emote shows up in combo_count messages within
 *    combo_window seconds, those copies go up at once, and so does every
 *    further message with it while the combo lasts (each one extends it).
 * Either rule is enough; with both off (0) every emote goes up. Blacklisted
 * emotes never do. Reactions (YouTube's hearts) fly along with the rest,
 * one icon per reactions_per_icon reactions, spread over the second they
 * happened in.
 *
 * Images come from kk_emotes, at its size; an emote still downloading
 * waits (up to a few seconds) and then flies. The wall is drawn as a stage
 * layer. */

#include <stdbool.h>
#include <stdint.h>

#include "chat.h"
#include "emotes.h"
#include "layer.h"

typedef enum {
    KK_WALL_RISE,   /* from the bottom up, swaying */
    KK_WALL_BOUNCE, /* diagonals bouncing off the edges, like the DVD logo */
    KK_WALL_FLY,    /* across the screen, edge to edge */
} kk_wall_style;

typedef struct {
    bool enabled;
    double duration;     /* seconds each emote is on screen */
    int min_per_message; /* 0 = rule off */
    int combo_count;     /* 0 = rule off */
    double combo_window; /* seconds */
    int max_per_message;
    int max_on_screen;
    kk_wall_style style;
    bool reactions;
    int reactions_per_icon;
    const char *blacklist; /* "😂, :_hello:, Kappa"; copied */
} kk_wall_config;

typedef struct kk_emotewall kk_emotewall;

/* images must outlive the wall. */
kk_emotewall *kk_emotewall_new(kk_emotes *images, uint64_t seed);
void kk_emotewall_free(kk_emotewall *w);

/* Applies cfg (on start and on reload). Turning it off clears the screen. */
void kk_emotewall_configure(kk_emotewall *w, const kk_wall_config *cfg);
void kk_emotewall_resize(kk_emotewall *w, int width, int height);

void kk_emotewall_message(kk_emotewall *w, const kk_chat_msg *m, double now);
void kk_emotewall_reaction(kk_emotewall *w, const kk_reaction *r, double now);

/* Starts what is due and moves everything to time now (monotonic s). */
void kk_emotewall_update(kk_emotewall *w, double now);

/* The layer to add to the stage. */
const kk_layer *kk_emotewall_layer(kk_emotewall *w);

/* For tests: emotes on screen, and waiting to start. */
int kk_emotewall_flying(const kk_emotewall *w);
int kk_emotewall_waiting(const kk_emotewall *w);

/* True if the blacklist (as configured) hides this emote: an emoji (❤ and
 * ❤️ alike), a name (":_hello:", "_hello" or "Kappa", any case) or an id. */
bool kk_emotewall_blocked(const kk_emotewall *w, const kk_emote *em);

#endif
