/* SPDX-License-Identifier: GPL-3.0-or-later */
/* A poll on the stage: the [polls] section, the voters' avatars walking to
 * their option's part of the window and staying there, everybody free again
 * when it closes, and the final result going away by itself. Avatars are
 * made by hand, with a sheet that has no picture. */
#include <stdlib.h>
#include <string.h>

#include "../harness.h"
#include "config.h"
#include "ini.h"
#include "pollview.h"

#define W 900
#define H 600

static kk_sa_avatar def;
static kk_sheet sheet;
static kk_stage stage;
static kk_avatar avatars[3];

static void setup(void)
{
    for (int i = 0; i < stage.count; i++)
        kk_avatar_free(&avatars[i]);
    def = (kk_sa_avatar){.frame_w = 16, .frame_h = 16, .move_speed = 1.0};
    sheet = (kk_sheet){.store_w = 16, .store_h = 16, .cell_w = 32, .cell_h = 32, .cols = 1,
                       .rows = 1, .foot_pad = 2, .head_pad = 4};
    stage = (kk_stage){.width = W, .height = H, .avatars = avatars, .cap = 3,
                       .cfg = {.scale = 2.0, .ground_margin = 20}};
    kk_rng_seed(&stage.rng, 11);
    static const char *const keys[] = {"youtube:ana", "twitch:bia", "kick:caio"};
    static const double xs[] = {850, 50, 450};
    for (int i = 0; i < 3; i++)
        kk_avatar_init(&avatars[stage.count++], &def, &sheet, keys[i], keys[i],
                       cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 8, 8), xs[i], 2.0,
                       &stage.rng);
}

TEST(config_polls)
{
    kk_config c;
    kk_config_defaults(&c);
    CHECK(!c.polls_file && !c.poll_font && c.poll_size == 16 && c.poll_results == 15);
    kk_ini *ini = kk_ini_parse("[polls]\nfile = /tmp/p.json\nfont = Serif\nfont_size = 20\n"
                               "results_seconds = 0\n",
                               NULL, NULL);
    kk_config_apply(&c, ini, NULL, NULL);
    kk_ini_free(ini);
    CHECK_STR_EQ(c.polls_file, "/tmp/p.json");
    CHECK_STR_EQ(c.poll_font, "Serif");
    CHECK(c.poll_size == 20 && c.poll_results == 0);
    kk_config copy;
    CHECK(kk_config_copy(&copy, &c));
    CHECK_STR_EQ(copy.polls_file, "/tmp/p.json");
    kk_config_free(&copy);
    kk_config_free(&c);
}

/* Runs the stage for seconds, the view following ps. */
static void run(kk_pollview *v, kk_polls *ps, double *now, double seconds)
{
    kk_view view = kk_stage_view(&stage);
    for (double t = 0; t < seconds; t += 0.05) {
        *now += 0.05;
        kk_pollview_update(v, ps, *now);
        for (int i = 0; i < stage.count; i++)
            kk_avatar_update(&avatars[i], 0.05, &view, &stage.rng);
    }
}

TEST(voters_gather)
{
    setup();
    kk_polls *ps = kk_polls_open(NULL);
    kk_pollview *v = kk_pollview_new(&stage);
    kk_pollview_configure(v, &(kk_pollview_config){.size = 12, .results_seconds = 5});
    cJSON *req = cJSON_Parse("{\"question\":\"Qual?\",\"options\":[{\"text\":\"Um\"},"
                             "{\"text\":\"Dois\"},{\"text\":\"Três\"}]}");
    char err[200];
    kk_poll *p = kk_poll_from_request(req, NULL, NULL, err, sizeof err);
    cJSON_Delete(req);
    double now = 1000;
    CHECK(kk_polls_start(ps, p, (long long)now, NULL, 0));
    run(v, ps, &now, 0.1);
    CHECK(kk_pollview_shown(v) == 1);
    kk_rect panel = kk_pollview_panel_rect(v);
    CHECK(panel.w == W && panel.h > 40 && panel.h < H / 2);

    /* Ana (far right) votes for the first part, Bia (far left) for the last;
     * Caio doesn't vote. */
    kk_polls_vote(ps, "youtube:ana", 0);
    kk_polls_vote(ps, "twitch:bia", 2);
    run(v, ps, &now, 60);
    CHECK_MSG(avatars[0].x < W / 3.0, "ana at %g", avatars[0].x);
    CHECK_MSG(avatars[1].x > 2 * W / 3.0, "bia at %g", avatars[1].x);
    CHECK(avatars[2].zone_hi == 0);
    /* And they stay there. */
    for (int k = 0; k < 20; k++) {
        run(v, ps, &now, 1);
        CHECK(avatars[0].x < W / 3.0 && avatars[1].x > 2 * W / 3.0);
    }

    /* Ana changes her mind. */
    kk_polls_vote(ps, "youtube:ana", 1);
    run(v, ps, &now, 60);
    CHECK_MSG(avatars[0].x > W / 3.0 && avatars[0].x < 2 * W / 3.0, "ana at %g", avatars[0].x);

    /* Closed: free again, the result stays a while and goes. */
    kk_polls_close(ps, (long long)now);
    run(v, ps, &now, 0.1);
    CHECK(avatars[0].zone_hi == 0 && avatars[1].zone_hi == 0);
    CHECK(kk_pollview_shown(v) == 1);
    run(v, ps, &now, 6);
    CHECK(kk_pollview_shown(v) == 0);
    CHECK(kk_rect_empty(kk_pollview_panel_rect(v)));

    kk_pollview_free(v);
    kk_polls_free(ps);
    for (int i = 0; i < stage.count; i++)
        kk_avatar_free(&avatars[i]);
    stage.count = 0;
}

TEST(zones)
{
    double lo, hi;
    kk_pollview_zone(900, 3, 0, &lo, &hi);
    CHECK(lo == 0 && hi == 300);
    kk_pollview_zone(900, 3, 2, &lo, &hi);
    CHECK(lo == 600 && hi == 900);
}

int main(void)
{
    RUN(config_polls);
    RUN(voters_gather);
    RUN(zones);
    return harness_report();
}
