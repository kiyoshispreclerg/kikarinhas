/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Avatars flying (slingshot, bombs), moving on command, and the stage
 * props: bombs and collectibles. The avatars are made by hand, with a sheet
 * that has no picture: physics and collisions don't draw. */
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "../harness.h"
#include "props.h"

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
}

static void setup(void)
{
    for (int i = 0; i < stage.count; i++)
        kk_avatar_free(&avatars[i]);
    def = (kk_sa_avatar){.frame_w = 32, .frame_h = 32, .move_speed = 1.0};
    sheet = (kk_sheet){.store_w = 32, .store_h = 32, .cell_w = 64, .cell_h = 64, .cols = 1,
                       .rows = 1, .foot_pad = 4, .head_pad = 8};
    stage = (kk_stage){.width = W, .height = H, .avatars = avatars, .cap = 4,
                       .cfg = {.scale = 2.0, .ground_margin = GROUND_MARGIN}};
    kk_rng_seed(&stage.rng, 7);
}

static void step_avatars(double seconds)
{
    kk_view v = kk_stage_view(&stage);
    for (double t = 0; t < seconds; t += 1.0 / 60)
        for (int i = 0; i < stage.count; i++)
            kk_avatar_update(&avatars[i], 1.0 / 60, &v, &stage.rng);
}

TEST(flight_stays_inside_and_lands)
{
    setup();
    add(400, "test:a");
    kk_avatar *a = &avatars[0];
    kk_avatar_launch(a, 1500, 1500, 9);
    CHECK(kk_avatar_flying(a));
    kk_view v = kk_stage_view(&stage);
    double half = kk_avatar_width(a) / 2, top = v.ground_y - sheet.cell_h;
    bool hit_wall = false;
    double t = 0;
    for (; t < 20 && kk_avatar_flying(a); t += 1.0 / 60) {
        kk_avatar_update(a, 1.0 / 60, &v, &stage.rng);
        CHECK(a->x >= half - 0.01 && a->x <= W - half + 0.01);
        CHECK(a->lift >= 0 && a->lift <= top + 0.01);
        hit_wall = hit_wall || a->x >= W - half - 0.01;
    }
    CHECK(hit_wall);
    CHECK_MSG(!kk_avatar_flying(a), "still flying after %.1f s", t);
    CHECK(a->lift == 0 && a->angle == 0 && a->vx == 0);
}

TEST(walking_freezing_standing)
{
    setup();
    add(400, "test:a");
    kk_avatar *a = &avatars[0];
    CHECK(kk_avatar_walk_to(a, 100, W));
    CHECK_INT_EQ(a->state, KK_ST_WALK);
    kk_view v = kk_stage_view(&stage);
    for (double t = 0; t < 20 && a->state == KK_ST_WALK; t += 1.0 / 60)
        kk_avatar_update(a, 1.0 / 60, &v, &stage.rng);
    CHECK(fabs(a->x - 100) < 0.01);
    /* Off the edge: kept inside. */
    a->state = KK_ST_IDLE;
    CHECK(kk_avatar_walk_to(a, -500, W));
    CHECK(a->target >= kk_avatar_width(a) / 2);
    a->frozen = true;
    a->state = KK_ST_IDLE;
    CHECK(!kk_avatar_walk_to(a, 600, W));
    double x = a->x;
    step_avatars(30); /* frozen: no wandering */
    CHECK(fabs(a->x - x) < 0.01);
    a->frozen = false;
    CHECK(!kk_avatar_stand(a)); /* not sitting */
    a->state = KK_ST_SITTING;
    CHECK(kk_avatar_stand(a));
    CHECK(a->state != KK_ST_SITTING);
}

TEST(sizes)
{
    setup();
    add(400, "test:a");
    kk_avatar *a = &avatars[0];
    double h = kk_avatar_height(a);
    kk_avatar_set_size(a, 2.0);
    CHECK(fabs(kk_avatar_height(a) - 2 * h) < 0.01);
    kk_avatar_set_size(a, 100);
    CHECK(a->size == KK_AVATAR_MAX_SIZE);
    kk_avatar_set_size(a, 0);
    CHECK(a->size == KK_AVATAR_MIN_SIZE);
}

TEST(bomb_throws_the_near_ones)
{
    setup();
    add(400, "test:near");
    add(780, "test:far"); /* beyond the blast (75 px at scale 1, 150 here) */
    kk_props *p = kk_props_new(&stage, 3);
    CHECK(kk_props_bomb(p, NULL, 400));
    CHECK_INT_EQ(kk_props_bombs(p), 1);
    for (double t = 0; t < 2.5; t += 1.0 / 60)
        kk_props_update(p, 1.0 / 60);
    CHECK_INT_EQ(kk_props_bombs(p), 0);
    CHECK(kk_avatar_flying(&avatars[0]));
    CHECK(!kk_avatar_flying(&avatars[1]));
    /* Several at once, up to a limit. */
    int thrown = 0;
    for (int i = 0; i < 20; i++)
        thrown += kk_props_bomb(p, &avatars[1], 100);
    CHECK(thrown > 1 && thrown < 20);
    kk_props_free(p);
}

static int catches;
static long caught_points;

static long on_catch(void *ud, kk_avatar *a, long points)
{
    (void)ud;
    (void)a;
    catches++;
    caught_points += points;
    return points;
}

TEST(jump_to_catch)
{
    setup();
    add(400, "test:a");
    kk_avatar *a = &avatars[0];
    kk_props *p = kk_props_new(&stage, 5);
    kk_props_set_catch(p, on_catch, NULL);
    catches = 0;
    caught_points = 0;
    CHECK_INT_EQ(kk_props_spawn(p, 1, 5), 1);
    double x, y;
    CHECK(kk_props_collectible_at(p, 0, &x, &y));
    CHECK(!kk_props_collectible_at(p, 1, &x, &y));
    /* Standing (or walking) right under it doesn't reach... */
    a->x = x;
    for (double t = 0; t < 1; t += 1.0 / 60)
        kk_props_update(p, 1.0 / 60);
    CHECK_INT_EQ(kk_props_collectibles(p), 1);
    CHECK_INT_EQ(catches, 0);
    /* ...a jump does. */
    kk_avatar_jump(a);
    kk_view v = kk_stage_view(&stage);
    for (double t = 0; t < 1.5 && kk_props_collectibles(p); t += 1.0 / 60) {
        kk_avatar_update(a, 1.0 / 60, &v, &stage.rng);
        kk_props_update(p, 1.0 / 60);
    }
    CHECK_INT_EQ(kk_props_collectibles(p), 0);
    CHECK_INT_EQ(catches, 1);
    CHECK(caught_points == 5 || caught_points == 15); /* a gem is worth 3 */

    /* Many at once: up to a limit, spread apart. */
    int n = kk_props_spawn(p, 100, 5);
    CHECK(n > 5 && n <= 30);
    kk_props_clear(p);
    CHECK_INT_EQ(kk_props_collectibles(p), 0);
    kk_props_free(p);
}

int main(void)
{
    RUN(flight_stays_inside_and_lands);
    RUN(walking_freezing_standing);
    RUN(sizes);
    RUN(bomb_throws_the_near_ones);
    RUN(jump_to_catch);
    setup(); /* frees the last avatars */
    return harness_report();
}
