/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Mouse gestures on the window: click, drag and throw (see pointer.h). */
#include "pointer.h"

#include <stdlib.h>
#include <string.h>

#define MAX_TARGETS 8
#define N_SAMPLES 8
/* The throw speed comes from the moves of this last stretch of time; an
 * older one means the pointer stopped before letting go. */
#define SPEED_WINDOW 0.1

typedef enum { IDLE, PRESSED, DRAGGING } state;

typedef struct {
    int x, y;
    double t;
} sample;

struct kk_pointer {
    kk_pointer_target targets[MAX_TARGETS];
    int n_targets;

    state st;
    int target; /* index into targets, -1 when the press hit nothing */
    uint64_t id;
    int press_x, press_y;
    int last_x, last_y;

    sample samples[N_SAMPLES]; /* ring of the latest positions */
    int n_samples, next_sample;
};

kk_pointer *kk_pointer_new(void)
{
    kk_pointer *p = calloc(1, sizeof *p);
    if (p)
        p->target = -1;
    return p;
}

void kk_pointer_free(kk_pointer *p)
{
    free(p);
}

bool kk_pointer_add_target(kk_pointer *p, const kk_pointer_target *t)
{
    if (p->n_targets == MAX_TARGETS || !t->hit)
        return false;
    if (t->grab && (!t->move || !t->drop))
        return false;
    p->targets[p->n_targets++] = *t;
    return true;
}

static void reset(kk_pointer *p)
{
    p->st = IDLE;
    p->target = -1;
    p->n_samples = 0;
    p->next_sample = 0;
}

void kk_pointer_cancel(kk_pointer *p)
{
    if (p->st == DRAGGING) {
        const kk_pointer_target *t = &p->targets[p->target];
        t->drop(t->ud, p->id, p->last_x, p->last_y, 0, 0);
    }
    reset(p);
}

void kk_pointer_remove_target(kk_pointer *p, void *ud)
{
    for (int i = 0; i < p->n_targets; i++) {
        if (p->targets[i].ud != ud)
            continue;
        if (p->target == i)
            kk_pointer_cancel(p);
        else if (p->target > i)
            p->target--;
        memmove(&p->targets[i], &p->targets[i + 1],
                (size_t)(p->n_targets - i - 1) * sizeof p->targets[0]);
        p->n_targets--;
        return;
    }
}

bool kk_pointer_holding(const kk_pointer *p, void **ud, uint64_t *id)
{
    if (p->st != DRAGGING)
        return false;
    if (ud)
        *ud = p->targets[p->target].ud;
    if (id)
        *id = p->id;
    return true;
}

static void add_sample(kk_pointer *p, int x, int y, double t)
{
    p->samples[p->next_sample] = (sample){x, y, t};
    p->next_sample = (p->next_sample + 1) % N_SAMPLES;
    if (p->n_samples < N_SAMPLES)
        p->n_samples++;
}

/* Speed over the samples of the last SPEED_WINDOW before now. */
static void speed(const kk_pointer *p, double now, double *vx, double *vy)
{
    *vx = *vy = 0;
    if (p->n_samples == 0)
        return;
    int newest = (p->next_sample + N_SAMPLES - 1) % N_SAMPLES;
    const sample *last = &p->samples[newest];
    if (now - last->t > SPEED_WINDOW)
        return; /* it stood still before letting go */
    const sample *first = last;
    for (int k = 1; k < p->n_samples; k++) {
        const sample *s = &p->samples[(newest + N_SAMPLES - k) % N_SAMPLES];
        if (last->t - s->t > SPEED_WINDOW)
            break;
        first = s;
    }
    double dt = last->t - first->t;
    if (dt < 0.005)
        return;
    *vx = (last->x - first->x) / dt;
    *vy = (last->y - first->y) / dt;
}

static void press(kk_pointer *p, const kk_pointer_event *ev)
{
    if (p->st == DRAGGING)
        kk_pointer_cancel(p); /* a release got lost */
    reset(p);
    p->st = PRESSED;
    p->press_x = p->last_x = ev->x;
    p->press_y = p->last_y = ev->y;
    add_sample(p, ev->x, ev->y, ev->t);
    for (int i = 0; i < p->n_targets; i++) {
        const kk_pointer_target *t = &p->targets[i];
        if (t->hit(t->ud, ev->x, ev->y, &p->id)) {
            p->target = i;
            return;
        }
    }
}

static void move(kk_pointer *p, const kk_pointer_event *ev)
{
    if (p->st == IDLE)
        return;
    p->last_x = ev->x;
    p->last_y = ev->y;
    add_sample(p, ev->x, ev->y, ev->t);
    if (p->target < 0)
        return;
    const kk_pointer_target *t = &p->targets[p->target];
    if (p->st == PRESSED) {
        int dx = ev->x - p->press_x, dy = ev->y - p->press_y;
        if (dx * dx + dy * dy <= KK_POINTER_DRAG_SLOP * KK_POINTER_DRAG_SLOP)
            return;
        if (!t->grab || !t->grab(t->ud, p->id, p->press_x, p->press_y)) {
            p->target = -1; /* the rest of this gesture goes nowhere */
            return;
        }
        p->st = DRAGGING;
    }
    t->move(t->ud, p->id, ev->x, ev->y);
}

static void release(kk_pointer *p, const kk_pointer_event *ev)
{
    if (p->st == IDLE)
        return;
    if (ev->x != p->last_x || ev->y != p->last_y)
        move(p, ev);
    if (p->target >= 0) {
        const kk_pointer_target *t = &p->targets[p->target];
        if (p->st == DRAGGING) {
            double vx, vy;
            speed(p, ev->t, &vx, &vy);
            t->drop(t->ud, p->id, ev->x, ev->y, vx, vy);
        } else if (t->click) {
            t->click(t->ud, p->id, ev->x, ev->y);
        }
    }
    reset(p);
}

void kk_pointer_feed(kk_pointer *p, const kk_pointer_event *ev)
{
    switch (ev->kind) {
    case KK_POINTER_PRESS:
        press(p, ev);
        break;
    case KK_POINTER_MOVE:
        move(p, ev);
        break;
    case KK_POINTER_RELEASE:
        release(p, ev);
        break;
    }
}
