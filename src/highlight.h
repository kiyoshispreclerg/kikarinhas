/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef KK_HIGHLIGHT_H
#define KK_HIGHLIGHT_H

/* The highlighted comment: a chat message the streamer picked (a click in
 * the chat window, or the control socket) shown big on the stage, in a
 * card at a configurable place, while a spotlight follows the avatar of
 * whoever wrote it. One at a time: a new one replaces it.
 *
 * Two stage layers: the card over everything, the spotlight under the
 * avatars (so they stand in the light). The avatar is looked up on the
 * stage every frame by its user key, never held. */

#include <stdbool.h>

#include "chat.h"
#include "emotes.h"
#include "layer.h"
#include "stage.h"

typedef struct {
    bool card;        /* show the card (else only the spotlight) */
    double x, y;      /* centre of the card, % of the window */
    double width;     /* widest the card gets, % of the window width */
    const char *font; /* Pango family and style; NULL: "Sans". Copied */
    double size;      /* points */
    double seconds;   /* until it goes by itself; 0: until cleared */
    bool spotlight;
} kk_highlight_config;

typedef struct kk_highlight kk_highlight;

/* stage and emotes (may be NULL) must outlive it. */
kk_highlight *kk_highlight_new(kk_stage *stage, kk_emotes *emotes);
void kk_highlight_free(kk_highlight *h);
void kk_highlight_configure(kk_highlight *h, const kk_highlight_config *cfg);

/* Highlights m from now on (monotonic seconds). */
void kk_highlight_show(kk_highlight *h, const kk_chat_msg *m, double now);
/* Takes it away (it fades out). */
void kk_highlight_clear(kk_highlight *h, double now);
/* Whether one is highlighted now (not fading out). */
bool kk_highlight_active(const kk_highlight *h);

void kk_highlight_update(kk_highlight *h, double now);

const kk_layer *kk_highlight_layer(kk_highlight *h);      /* the card */
const kk_layer *kk_highlight_spot_layer(kk_highlight *h); /* the light */

/* For tests: where the card and the light are now (empty if not shown). */
kk_rect kk_highlight_card_rect(const kk_highlight *h);
kk_rect kk_highlight_spot_rect(const kk_highlight *h);

#endif
