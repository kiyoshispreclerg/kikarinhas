/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Who gets which avatar by default: the system-wide avatars are on offer
 * (for !avatar random and the like) but not handed out by hash while there
 * are others, so installing a kit doesn't change who looks like what. */
#include <stdio.h>
#include <string.h>

#include "../harness.h"
#include "stage.h"

static kk_sa_avatar make(char *key, bool system)
{
    kk_sa_avatar a = {.key = key, .name = key, .image = (char *)"x.png", .frame_w = 16,
                      .frame_h = 16, .ppu = 1.0, .move_speed = 1.0, .system = system};
    a.anims[KK_ANIM_IDLE] = (kk_sa_anim){.frames = 1, .fps = 1.0};
    a.n_anims = 1;
    return a;
}

static kk_stage_config config(void)
{
    return (kk_stage_config){.scale = 1.0, .max_avatars = 4, .despawn = 60, .seed = 1};
}

TEST(hash_defaults_skip_system_avatars)
{
    char k1[] = "ana", k2[] = "bia", k3[] = "default_gato", k4[] = "default_robo";
    kk_sa_avatar av[] = {make(k1, false), make(k2, false), make(k3, true), make(k4, true)};
    kk_sa_library lib = {.avatars = av, .count = 4};
    kk_stage s;
    kk_stage_config cfg = config();
    CHECK_INT_EQ(kk_stage_init(&s, &lib, &cfg), 0);
    CHECK_INT_EQ(s.n_usable, 4); /* all of them can be picked */
    CHECK_INT_EQ(s.n_pool, 2);   /* but only these are handed out */
    int got[2] = {0, 0};
    for (int i = 0; i < 300; i++) {
        char key[32];
        snprintf(key, sizeof key, "youtube:user%d", i);
        const kk_sa_avatar *d = kk_stage_default_avatar(&s, key);
        CHECK(d != NULL && !d->system);
        got[strcmp(d->key, "ana") == 0 ? 0 : 1]++;
    }
    CHECK(got[0] > 50 && got[1] > 50); /* both of them are used */
    kk_stage_free(&s);
}

TEST(only_system_avatars_are_all_there_is)
{
    char k1[] = "default_gato", k2[] = "default_robo";
    kk_sa_avatar av[] = {make(k1, true), make(k2, true)};
    kk_sa_library lib = {.avatars = av, .count = 2};
    kk_stage s;
    kk_stage_config cfg = config();
    CHECK_INT_EQ(kk_stage_init(&s, &lib, &cfg), 0);
    CHECK_INT_EQ(s.n_pool, 2);
    const kk_sa_avatar *d = kk_stage_default_avatar(&s, "youtube:someone");
    CHECK(d != NULL && d->system);
    kk_stage_free(&s);
}

int main(void)
{
    RUN(hash_defaults_skip_system_avatars);
    RUN(only_system_avatars_are_all_there_is);
    return harness_report();
}
