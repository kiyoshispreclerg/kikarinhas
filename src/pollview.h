/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef KK_POLLVIEW_H
#define KK_POLLVIEW_H

/* The open poll on the stage: the window's width split in one part per
 * option, each with its card on top (what to type, the option, and the
 * votes when the poll shows them) and a line between parts. Whoever votes
 * has their avatar walk to their option's part and wander only there until
 * the poll closes. Once closed, the final result (the winner marked) stays
 * a few seconds and goes.
 *
 * A layer under the avatars, so they walk in front of the lines. It looks
 * the poll up by id on every update, never holds it. */

#include <stdbool.h>

#include "layer.h"
#include "polls.h"
#include "stage.h"

typedef struct {
    const char *font; /* Pango family and style; NULL: "Sans". Copied */
    double size;      /* points */
    double results_seconds; /* the final result stays this long; 0: goes at once */
} kk_pollview_config;

typedef struct kk_pollview kk_pollview;

/* stage must outlive it. */
kk_pollview *kk_pollview_new(kk_stage *stage);
void kk_pollview_free(kk_pollview *v);
void kk_pollview_configure(kk_pollview *v, const kk_pollview_config *cfg);

/* Every frame, before the stage updates: follows the open poll of ps (or
 * the one that just closed) and keeps the voters' avatars in their part.
 * now is Unix time (with fractions), as the polls use. */
void kk_pollview_update(kk_pollview *v, const kk_polls *ps, double now);

const kk_layer *kk_pollview_layer(kk_pollview *v);

/* For tests: the id shown (0: none) and its cards' area. */
uint64_t kk_pollview_shown(const kk_pollview *v);
kk_rect kk_pollview_panel_rect(const kk_pollview *v);
/* The part of the window of option i of n, in px: [*lo, *hi). */
void kk_pollview_zone(int width, int n, int i, double *lo, double *hi);

#endif
