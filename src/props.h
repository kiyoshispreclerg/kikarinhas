/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef KK_PROPS_H
#define KK_PROPS_H

/* Things on the stage besides the avatars, drawn over them as a layer:
 *  - bombs (!bomb): thrown in an arc, the fuse burns for a moment, and the
 *    blast throws every avatar near it into the air, tumbling;
 *  - collectibles (!spawn, Stream Avatars' "jump catch game"): stars, and
 *    now and then a gem worth more, floating above the avatars' heads, low
 *    enough for a jump to reach. Whoever touches one takes it (the catch
 *    callback hands out the points) and a "+5" rises from it.
 *
 * Everything is drawn with Cairo, no pictures needed. Avatars are looked up
 * on the stage every frame and never held: the stage moves them around in
 * its array. */

#include <stdbool.h>
#include <stdint.h>

#include "layer.h"
#include "stage.h"

typedef struct kk_props kk_props;

/* a caught a collectible worth points; return what to show ("+5"), or 0 to
 * show nothing (e.g. nobody to give points to). */
typedef long (*kk_catch_fn)(void *ud, kk_avatar *a, long points);

kk_props *kk_props_new(kk_stage *stage, uint64_t seed);
void kk_props_free(kk_props *p);
const kk_layer *kk_props_layer(kk_props *p);
void kk_props_set_catch(kk_props *p, kk_catch_fn fn, void *ud);

/* A bomb from thrower (NULL: falls from above) to land at x. False if too
 * many are in the air already. */
bool kk_props_bomb(kk_props *p, const kk_avatar *thrower, double x);
/* n collectibles (at most what fits), each worth points (gems: three
 * times). Returns how many appeared. */
int kk_props_spawn(kk_props *p, int n, long points);
/* Collectibles still waiting to be caught. */
int kk_props_collectibles(const kk_props *p);
/* Bombs in the air or burning. */
int kk_props_bombs(const kk_props *p);
/* Where the i-th collectible floats (for tests); false past the last. */
bool kk_props_collectible_at(const kk_props *p, int i, double *x, double *y);

void kk_props_update(kk_props *p, double dt);
/* Drops everything (the window changed size, say). */
void kk_props_clear(kk_props *p);

#endif
