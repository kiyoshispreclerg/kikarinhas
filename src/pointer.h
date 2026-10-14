/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef KK_POINTER_H
#define KK_POINTER_H

/* The mouse on the window (OBS mode only; the desktop overlay lets every
 * click through). window.c turns X events into kk_pointer_events; this
 * module turns those into gestures and hands them to whatever is under
 * the pointer: a click, or a drag that ends in a drop with the speed the
 * pointer had when let go, so a held thing can be thrown.
 *
 * Things that can be clicked or dragged register as targets (the stage's
 * avatars, the picture layer, later HTML blocks), topmost first. Only the
 * left button drives gestures; the right one opens the configurator in
 * window.c and never gets here. */

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    KK_POINTER_PRESS,
    KK_POINTER_MOVE,
    KK_POINTER_RELEASE,
} kk_pointer_kind;

typedef struct {
    kk_pointer_kind kind;
    int x, y;  /* window coordinates */
    double t;  /* seconds, from the X server's clock */
} kk_pointer_event;

/* A press that moves farther than this (in pixels) becomes a drag. */
#define KK_POINTER_DRAG_SLOP 4

typedef struct {
    void *ud;
    /* Whether something of this target is at (x, y); *id names it for the
     * calls below. Required. */
    bool (*hit)(void *ud, int x, int y, uint64_t *id);
    /* Pressed and released without moving. May be NULL. */
    void (*click)(void *ud, uint64_t id, int x, int y);
    /* A drag starts: (x, y) is where the press was, so the target can keep
     * the grab offset. False refuses it (the gesture then does nothing).
     * May be NULL: the target only takes clicks. */
    bool (*grab)(void *ud, uint64_t id, int x, int y);
    /* The pointer moved while holding id. Required with grab. */
    void (*move)(void *ud, uint64_t id, int x, int y);
    /* Let go at (x, y) moving at (vx, vy) pixels per second (0 if the
     * pointer stood still before letting go). Required with grab. Also
     * called, with no speed, when the grab is cancelled. */
    void (*drop)(void *ud, uint64_t id, int x, int y, double vx, double vy);
} kk_pointer_target;

typedef struct kk_pointer kk_pointer;

kk_pointer *kk_pointer_new(void);
void kk_pointer_free(kk_pointer *p);

/* Adds a target below the ones already there (copied). False if full. */
bool kk_pointer_add_target(kk_pointer *p, const kk_pointer_target *t);
/* Removes the target with this ud, dropping what it holds. */
void kk_pointer_remove_target(kk_pointer *p, void *ud);

void kk_pointer_feed(kk_pointer *p, const kk_pointer_event *ev);

/* Lets go of what is held, without speed; for a target whose thing is
 * gone (an avatar that left while held). */
void kk_pointer_cancel(kk_pointer *p);

/* Whether a drag is going on; if so, what is held. */
bool kk_pointer_holding(const kk_pointer *p, void **ud, uint64_t *id);

#endif
