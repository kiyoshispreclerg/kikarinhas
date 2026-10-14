/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Mouse gestures: click, drag, throw speed, targets in order, cancel. */
#include <stdbool.h>
#include <string.h>

#include "../harness.h"
#include "pointer.h"

/* A target with one square thing, id 7, at (x, y)-(x+10, y+10). */
typedef struct {
    int x, y;
    bool refuse;
    int hits, clicks, grabs, moves, drops;
    int grab_x, grab_y, at_x, at_y;
    double vx, vy;
} box;

static bool box_hit(void *ud, int x, int y, uint64_t *id)
{
    box *b = ud;
    b->hits++;
    *id = 7;
    return x >= b->x && x < b->x + 10 && y >= b->y && y < b->y + 10;
}

static void box_click(void *ud, uint64_t id, int x, int y)
{
    box *b = ud;
    (void)id;
    b->clicks++;
    b->at_x = x;
    b->at_y = y;
}

static bool box_grab(void *ud, uint64_t id, int x, int y)
{
    box *b = ud;
    if (b->refuse || id != 7)
        return false;
    b->grabs++;
    b->grab_x = x;
    b->grab_y = y;
    return true;
}

static void box_move(void *ud, uint64_t id, int x, int y)
{
    box *b = ud;
    (void)id;
    b->moves++;
    b->at_x = x;
    b->at_y = y;
}

static void box_drop(void *ud, uint64_t id, int x, int y, double vx, double vy)
{
    box *b = ud;
    (void)id;
    b->drops++;
    b->at_x = x;
    b->at_y = y;
    b->vx = vx;
    b->vy = vy;
}

static kk_pointer_target target(box *b)
{
    return (kk_pointer_target){b, box_hit, box_click, box_grab, box_move, box_drop};
}

static void feed(kk_pointer *p, kk_pointer_kind kind, int x, int y, double t)
{
    kk_pointer_event ev = {kind, x, y, t};
    kk_pointer_feed(p, &ev);
}

TEST(click_without_moving)
{
    kk_pointer *p = kk_pointer_new();
    box b = {.x = 0, .y = 0};
    kk_pointer_target t = target(&b);
    CHECK(kk_pointer_add_target(p, &t));
    feed(p, KK_POINTER_PRESS, 5, 5, 1.0);
    feed(p, KK_POINTER_MOVE, 7, 6, 1.05); /* within the slop */
    feed(p, KK_POINTER_RELEASE, 7, 6, 1.1);
    CHECK_INT_EQ(b.clicks, 1);
    CHECK_INT_EQ(b.grabs, 0);
    CHECK_INT_EQ(b.at_x, 7);
    CHECK(!kk_pointer_holding(p, NULL, NULL));
    kk_pointer_free(p);
}

TEST(drag_and_throw)
{
    kk_pointer *p = kk_pointer_new();
    box b = {.x = 0, .y = 0};
    kk_pointer_target t = target(&b);
    kk_pointer_add_target(p, &t);
    feed(p, KK_POINTER_PRESS, 5, 5, 1.0);
    feed(p, KK_POINTER_MOVE, 20, 5, 1.2);
    CHECK_INT_EQ(b.grabs, 1);
    CHECK_INT_EQ(b.grab_x, 5); /* where the press was */
    CHECK_INT_EQ(b.moves, 1);
    void *ud;
    uint64_t id;
    CHECK(kk_pointer_holding(p, &ud, &id));
    CHECK(ud == &b && id == 7);
    /* 100 px right and 50 up in 50 ms: 2000 and -1000 px/s. */
    feed(p, KK_POINTER_MOVE, 100, 100, 2.0);
    feed(p, KK_POINTER_MOVE, 150, 75, 2.025);
    feed(p, KK_POINTER_MOVE, 200, 50, 2.05);
    feed(p, KK_POINTER_RELEASE, 200, 50, 2.06);
    CHECK_INT_EQ(b.drops, 1);
    CHECK_INT_EQ(b.clicks, 0);
    CHECK_MSG(b.vx > 1999 && b.vx < 2001, "vx = %f", b.vx);
    CHECK_MSG(b.vy > -1001 && b.vy < -999, "vy = %f", b.vy);
    CHECK(!kk_pointer_holding(p, NULL, NULL));
    kk_pointer_free(p);
}

TEST(still_before_letting_go)
{
    kk_pointer *p = kk_pointer_new();
    box b = {.x = 0, .y = 0};
    kk_pointer_target t = target(&b);
    kk_pointer_add_target(p, &t);
    feed(p, KK_POINTER_PRESS, 5, 5, 1.0);
    feed(p, KK_POINTER_MOVE, 50, 5, 1.01);
    feed(p, KK_POINTER_RELEASE, 50, 5, 1.5);
    CHECK_INT_EQ(b.drops, 1);
    CHECK(b.vx == 0 && b.vy == 0);
    kk_pointer_free(p);
}

TEST(release_far_away_drags)
{
    /* No moves at all (they were merged away): the release still drags. */
    kk_pointer *p = kk_pointer_new();
    box b = {.x = 0, .y = 0};
    kk_pointer_target t = target(&b);
    kk_pointer_add_target(p, &t);
    feed(p, KK_POINTER_PRESS, 5, 5, 1.0);
    feed(p, KK_POINTER_RELEASE, 60, 5, 1.05);
    CHECK_INT_EQ(b.grabs, 1);
    CHECK_INT_EQ(b.drops, 1);
    CHECK_INT_EQ(b.at_x, 60);
    CHECK_INT_EQ(b.clicks, 0);
    kk_pointer_free(p);
}

TEST(topmost_target_wins)
{
    kk_pointer *p = kk_pointer_new();
    box top = {.x = 0, .y = 0}, below = {.x = 5, .y = 5};
    kk_pointer_target t1 = target(&top), t2 = target(&below);
    kk_pointer_add_target(p, &t1);
    kk_pointer_add_target(p, &t2);
    feed(p, KK_POINTER_PRESS, 6, 6, 1.0); /* on both */
    feed(p, KK_POINTER_RELEASE, 6, 6, 1.1);
    CHECK_INT_EQ(top.clicks, 1);
    CHECK_INT_EQ(below.hits, 0);
    feed(p, KK_POINTER_PRESS, 12, 12, 2.0); /* only below */
    feed(p, KK_POINTER_RELEASE, 12, 12, 2.1);
    CHECK_INT_EQ(below.clicks, 1);
    feed(p, KK_POINTER_PRESS, 100, 100, 3.0); /* on nothing */
    feed(p, KK_POINTER_MOVE, 200, 100, 3.1);
    feed(p, KK_POINTER_RELEASE, 200, 100, 3.2);
    CHECK_INT_EQ(top.clicks + top.grabs + below.clicks + below.grabs, 2);
    kk_pointer_free(p);
}

TEST(refused_grab)
{
    kk_pointer *p = kk_pointer_new();
    box b = {.x = 0, .y = 0, .refuse = true};
    kk_pointer_target t = target(&b);
    kk_pointer_add_target(p, &t);
    feed(p, KK_POINTER_PRESS, 5, 5, 1.0);
    feed(p, KK_POINTER_MOVE, 50, 5, 1.1);
    feed(p, KK_POINTER_MOVE, 60, 5, 1.2);
    feed(p, KK_POINTER_RELEASE, 60, 5, 1.3);
    CHECK_INT_EQ(b.moves + b.drops + b.clicks, 0);
    kk_pointer_free(p);
}

TEST(cancel_and_remove)
{
    kk_pointer *p = kk_pointer_new();
    box a = {.x = 0, .y = 0}, b = {.x = 0, .y = 0};
    kk_pointer_target ta = target(&a), tb = target(&b);
    tb.click = NULL;
    kk_pointer_add_target(p, &ta);
    kk_pointer_add_target(p, &tb);

    feed(p, KK_POINTER_PRESS, 5, 5, 1.0);
    feed(p, KK_POINTER_MOVE, 40, 5, 1.05);
    kk_pointer_cancel(p);
    CHECK_INT_EQ(a.drops, 1);
    CHECK(a.vx == 0);
    feed(p, KK_POINTER_RELEASE, 80, 5, 1.1); /* nothing more */
    CHECK_INT_EQ(a.drops, 1);
    CHECK_INT_EQ(a.moves, 1);

    /* Removing the holder drops; the next one takes over its place. */
    feed(p, KK_POINTER_PRESS, 5, 5, 2.0);
    feed(p, KK_POINTER_MOVE, 40, 5, 2.05);
    kk_pointer_remove_target(p, &a);
    CHECK_INT_EQ(a.drops, 2);
    feed(p, KK_POINTER_RELEASE, 40, 5, 2.1);
    feed(p, KK_POINTER_PRESS, 5, 5, 3.0);
    feed(p, KK_POINTER_MOVE, 40, 5, 3.05);
    feed(p, KK_POINTER_RELEASE, 40, 5, 3.1);
    CHECK_INT_EQ(b.grabs, 1);
    CHECK_INT_EQ(b.drops, 1);
    CHECK_INT_EQ(a.grabs, 2);

    /* A target without click: pressing and letting go does nothing. */
    feed(p, KK_POINTER_PRESS, 5, 5, 4.0);
    feed(p, KK_POINTER_RELEASE, 5, 5, 4.1);
    CHECK_INT_EQ(b.drops, 1);
    kk_pointer_free(p);
}

TEST(lost_release)
{
    /* Two presses in a row: what was held is dropped before the second. */
    kk_pointer *p = kk_pointer_new();
    box b = {.x = 0, .y = 0};
    kk_pointer_target t = target(&b);
    kk_pointer_add_target(p, &t);
    feed(p, KK_POINTER_PRESS, 5, 5, 1.0);
    feed(p, KK_POINTER_MOVE, 40, 5, 1.05);
    feed(p, KK_POINTER_PRESS, 5, 5, 2.0);
    CHECK_INT_EQ(b.drops, 1);
    feed(p, KK_POINTER_RELEASE, 5, 5, 2.1);
    CHECK_INT_EQ(b.clicks, 1);
    kk_pointer_free(p);
}

int main(void)
{
    RUN(click_without_moving);
    RUN(drag_and_throw);
    RUN(still_before_letting_go);
    RUN(release_far_away_drags);
    RUN(topmost_target_wins);
    RUN(refused_grab);
    RUN(cancel_and_remove);
    RUN(lost_release);
    return harness_report();
}
