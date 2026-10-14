/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Avatars picked up with the mouse: holding, swinging, throwing, hitting
 * the sprite's pixels, and the whole gesture through kk_pointer. The sheet
 * is made by hand: a 32x32 cell, opaque only in a 16x24 block. */
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "../harness.h"
#include "pointer.h"
#include "stage.h"

#define W 800
#define H 600
#define GROUND_MARGIN 20

static kk_sa_avatar def;
static kk_sheet sheet;
static kk_stage stage;
static kk_avatar avatars[4];

static void add(double x, const char *key)
{
    kk_avatar *a = &avatars[stage.count++];
    kk_avatar_init(a, &def, &sheet, key, key, cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 8, 8),
                   x, 2.0, &stage.rng);
    a->id = ++stage.next_id;
}

static void setup(void)
{
    for (int i = 0; i < stage.count; i++)
        kk_avatar_free(&avatars[i]);
    if (sheet.pixels)
        cairo_surface_destroy(sheet.pixels);
    def = (kk_sa_avatar){.frame_w = 32, .frame_h = 32, .move_speed = 1.0, .n_anims = 1};
    def.anims[KK_ANIM_IDLE] = (kk_sa_anim){.frames = 1, .fps = 1.0};
    /* Stored at 32x32, shown at 64x64; opaque in x 8..23, y 8..31. */
    cairo_surface_t *px = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 32, 32);
    cairo_t *cr = cairo_create(px);
    cairo_rectangle(cr, 8, 8, 16, 24);
    cairo_set_source_rgb(cr, 1, 0, 0);
    cairo_fill(cr);
    cairo_destroy(cr);
    cairo_surface_flush(px);
    sheet = (kk_sheet){.pixels = px, .store_w = 32, .store_h = 32, .cell_w = 64,
                       .cell_h = 64, .cols = 1, .rows = 1};
    stage = (kk_stage){.width = W, .height = H, .avatars = avatars, .cap = 4,
                       .cfg = {.scale = 2.0, .ground_margin = GROUND_MARGIN}};
    kk_rng_seed(&stage.rng, 7);
}

static void step(double seconds)
{
    kk_view v = kk_stage_view(&stage);
    for (double t = 0; t < seconds; t += 1.0 / 60)
        for (int i = 0; i < stage.count; i++)
            kk_avatar_update(&avatars[i], 1.0 / 60, &v, &stage.rng);
}

TEST(held_stays_put_and_inside)
{
    setup();
    add(400, "test:a");
    kk_avatar *a = &avatars[0];
    kk_view v = kk_stage_view(&stage);
    kk_avatar_hold(a);
    CHECK(kk_avatar_held(a));
    kk_avatar_hold_at(a, 300, 200, &v);
    CHECK(a->x == 300 && a->lift == 200);
    /* Nothing else moves it while held. */
    kk_avatar_jump(a);
    kk_avatar_launch(a, 900, 900, 5);
    CHECK(!kk_avatar_walk_to(a, 600, W));
    CHECK(!kk_avatar_sit(a));
    step(3);
    CHECK(kk_avatar_held(a));
    CHECK(a->x == 300 && a->lift == 200);
    /* Off the window: kept inside it, above the ground. */
    kk_avatar_hold_at(a, -100, -50, &v);
    CHECK(a->x == 32 && a->lift == 0);
    kk_avatar_hold_at(a, 5000, 5000, &v);
    CHECK(a->x == W - 32);
    CHECK(a->lift == v.ground_y - 64);
}

TEST(swings_while_carried)
{
    setup();
    add(400, "test:a");
    kk_avatar *a = &avatars[0];
    a->left = false;
    kk_view v = kk_stage_view(&stage);
    kk_avatar_hold(a);
    /* Carried right at 600 px/s: it leans, by at most SWING_MAX. */
    for (int i = 0; i < 30; i++) {
        kk_avatar_hold_at(a, a->x + 10, 100, &v);
        kk_avatar_update(a, 1.0 / 60, &v, &stage.rng);
    }
    CHECK_MSG(a->angle > 0.3 && a->angle <= 0.7 + 1e-9, "angle %f", a->angle);
    /* Stopped: straight again. */
    step(2);
    CHECK_MSG(fabs(a->angle) < 0.01, "angle %f", a->angle);
}

TEST(thrown_flies_and_lands)
{
    setup();
    add(400, "test:a");
    kk_avatar *a = &avatars[0];
    kk_view v = kk_stage_view(&stage);
    kk_avatar_hold(a);
    kk_avatar_hold_at(a, 400, 300, &v);
    kk_avatar_throw(a, -1200, 400);
    CHECK(!kk_avatar_held(a));
    CHECK(kk_avatar_flying(a));
    CHECK(a->left);
    CHECK(a->spin > 0); /* drawn mirrored: turning left on screen */
    double t = 0;
    for (; t < 20 && kk_avatar_flying(a); t += 1.0 / 60)
        kk_avatar_update(a, 1.0 / 60, &v, &stage.rng);
    CHECK_MSG(!kk_avatar_flying(a), "still flying after %.1f s", t);
    CHECK(a->lift == 0 && a->angle == 0);

    /* Let go standing still: it just falls. */
    kk_avatar_hold(a);
    kk_avatar_hold_at(a, 200, 50, &v);
    bool left = a->left;
    kk_avatar_throw(a, 0, 0);
    CHECK(kk_avatar_flying(a) && a->left == left);
    step(3);
    CHECK(!kk_avatar_flying(a) && a->lift == 0 && a->x == 200);
}

TEST(hits_the_pixels)
{
    setup();
    add(400, "test:a");
    kk_avatar *a = &avatars[0];
    a->left = false;
    kk_view v = kk_stage_view(&stage);
    /* Cell 64x64 from x 368, feet on the ground; opaque in x 384..415 and
     * y ground-48..ground (16..47 and 16..63 in the cell). */
    int top = v.ground_y - 64;
    CHECK(kk_avatar_hit(a, &v, 400, top + 40));
    CHECK(kk_avatar_hit(a, &v, 382, top + 40)); /* within the slack */
    CHECK(!kk_avatar_hit(a, &v, 372, top + 40));
    CHECK(!kk_avatar_hit(a, &v, 400, top + 4));  /* transparent head room */
    CHECK(!kk_avatar_hit(a, &v, 400, top - 30)); /* above the cell */
    /* Facing left: the mirror (the block is centred, so the same area). */
    a->left = true;
    CHECK(kk_avatar_hit(a, &v, 400, top + 40));
    /* Lifted: it moves up with the sprite. */
    a->lift = 100;
    CHECK(!kk_avatar_hit(a, &v, 400, top + 40));
    CHECK(kk_avatar_hit(a, &v, 400, top - 60));
    /* Tilted a quarter turn: a point left of the block is now on it. */
    a->lift = 0;
    a->left = false;
    CHECK(!kk_avatar_hit(a, &v, 376, top + 40));
    a->angle = M_PI / 2;
    CHECK(kk_avatar_hit(a, &v, 376, top + 40));
}

static void feed(kk_pointer *p, kk_pointer_kind kind, int x, int y, double t)
{
    kk_pointer_event ev = {kind, x, y, t};
    kk_pointer_feed(p, &ev);
}

TEST(drag_through_the_pointer)
{
    setup();
    add(400, "test:a");
    add(400, "test:b"); /* same place, painted over a */
    kk_view v = kk_stage_view(&stage);
    int y = v.ground_y - 20;
    kk_pointer *p = kk_pointer_new();
    kk_pointer_target t = kk_stage_pointer_target(&stage);
    CHECK(kk_pointer_add_target(p, &t));

    /* Click: the one on top jumps. */
    feed(p, KK_POINTER_PRESS, 400, y, 1.0);
    feed(p, KK_POINTER_RELEASE, 400, y, 1.1);
    CHECK(avatars[1].state == KK_ST_JUMP);
    CHECK(avatars[0].state != KK_ST_JUMP);
    step(2);

    /* Grab a (now alone at 600) a bit right of its middle and carry it. */
    kk_avatar_stop(&avatars[0], &stage.rng);
    avatars[0].x = 600;
    avatars[0].lift = 0;
    avatars[1].x = 100;
    unsigned id = avatars[0].id;
    feed(p, KK_POINTER_PRESS, 605, y, 2.0);
    feed(p, KK_POINTER_MOVE, 625, y - 100, 2.05);
    CHECK(avatars[1].id == id); /* moved on top */
    kk_avatar *a = &avatars[1];
    CHECK(kk_avatar_held(a));
    CHECK(fabs(a->x - 620) < 0.01); /* keeps the grab offset */
    CHECK(fabs(a->lift - 100) < 0.01);
    feed(p, KK_POINTER_MOVE, 645, y - 110, 2.1);
    feed(p, KK_POINTER_MOVE, 725, y - 140, 2.13);
    feed(p, KK_POINTER_RELEASE, 725, y - 140, 2.14);
    CHECK(kk_avatar_flying(a));
    CHECK(a->vx > 1000 && a->vy > 0); /* right and up */

    /* Too fast: slowed down to the limit. */
    step(20);
    kk_avatar_stop(a, &stage.rng);
    a->lift = 0;
    feed(p, KK_POINTER_PRESS, (int)a->x, y, 30.0);
    feed(p, KK_POINTER_MOVE, (int)a->x - 100, y, 30.01);
    feed(p, KK_POINTER_RELEASE, (int)a->x - 200, y, 30.02);
    CHECK_MSG(fabs(hypot(a->vx, a->vy) - KK_STAGE_MAX_THROW) < 1, "v %f", hypot(a->vx, a->vy));

    /* Pressing on nothing does nothing. */
    step(20);
    feed(p, KK_POINTER_PRESS, 100, 50, 60.0);
    feed(p, KK_POINTER_MOVE, 300, 50, 60.1);
    feed(p, KK_POINTER_RELEASE, 300, 50, 60.2);
    CHECK(!kk_avatar_held(&avatars[0]) && !kk_avatar_held(&avatars[1]));
    kk_pointer_free(p);
}

TEST(held_avatar_stays)
{
    /* Silent too long, but in hand: it doesn't leave. */
    setup();
    add(400, "test:a");
    stage.cfg.despawn = 1.0;
    kk_avatar_hold(&avatars[0]);
    kk_stage_update(&stage, 0.5);
    kk_stage_update(&stage, 0.6);
    CHECK_INT_EQ(stage.count, 1);
    kk_avatar_throw(&avatars[0], 0, 0);
    kk_stage_update(&stage, 0.1);
    CHECK_INT_EQ(stage.count, 0);
}

int main(void)
{
    RUN(held_stays_put_and_inside);
    RUN(swings_while_carried);
    RUN(thrown_flies_and_lands);
    RUN(hits_the_pixels);
    RUN(drag_through_the_pointer);
    RUN(held_avatar_stays);
    setup();
    cairo_surface_destroy(sheet.pixels);
    return harness_report();
}
