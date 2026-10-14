/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Danmaku: the [danmaku] section, colours, lanes (random, clear of the
 * message ahead, off the avatars) and the crossing itself. */
#include <stdlib.h>
#include <string.h>

#include "../harness.h"
#include "config.h"
#include "danmaku.h"
#include "ini.h"

#define W 1280
#define H 720

static int warnings;

static void count(void *ud, int line, const char *msg)
{
    (void)ud;
    (void)line;
    (void)msg;
    warnings++;
}

TEST(config_danmaku)
{
    kk_config c;
    kk_config_defaults(&c);
    CHECK(!c.danmaku_enabled);
    CHECK(c.danmaku_color == 0xffffff && c.danmaku_avoid);
    warnings = 0;
    kk_ini *ini = kk_ini_parse("[danmaku]\nenabled = yes\nseconds = 6\nfont = Serif Bold\n"
                               "size = 30\ncolor = #FFcc00\noutline = 123\n"
                               "paid_color = nope\nopacity = 80\navoid_avatars = no\n"
                               "max_on_screen = 50\nwhat = 1\n",
                               NULL, NULL);
    kk_config_apply(&c, ini, count, NULL);
    kk_ini_free(ini);
    CHECK_INT_EQ(warnings, 2); /* paid_color, what */
    CHECK(c.danmaku_enabled && c.danmaku_seconds == 6 && c.danmaku_size == 30);
    CHECK_STR_EQ(c.danmaku_font, "Serif Bold");
    CHECK(c.danmaku_color == 0xffcc00);
    CHECK(c.danmaku_outline == 0x112233);
    CHECK(c.danmaku_paid_color == 0xffd24a); /* kept */
    CHECK(c.danmaku_opacity == 80 && !c.danmaku_avoid && c.danmaku_max == 50);
    kk_config copy;
    CHECK(kk_config_copy(&copy, &c));
    CHECK_STR_EQ(copy.danmaku_font, "Serif Bold");
    kk_config_free(&copy);
    kk_config_free(&c);

    char out[8];
    kk_format_color(out, 0x0a0b0c);
    CHECK_STR_EQ(out, "#0a0b0c");
    uint32_t v;
    CHECK(!kk_parse_color("#12345", &v) && !kk_parse_color("#gg0000", &v));
}

static kk_danmaku_config on(void)
{
    return (kk_danmaku_config){.enabled = true, .seconds = 8, .size = 20, .color = 0xffffff,
                               .opacity = 1, .avoid_avatars = true, .max_on_screen = 100};
}

static kk_danmaku *make(kk_danmaku_config cfg, uint64_t seed)
{
    kk_danmaku *d = kk_danmaku_new(NULL, seed);
    kk_danmaku_configure(d, &cfg);
    kk_danmaku_resize(d, W, H);
    return d;
}

static bool say(kk_danmaku *d, const char *text, const kk_rect *avoid, int n, double now)
{
    kk_chat_msg m = {.platform = "demo", .user_id = "x", .name = "x", .text = text};
    return kk_danmaku_message(d, &m, avoid, n, now);
}

TEST(off_or_empty_shows_nothing)
{
    kk_danmaku_config cfg = on();
    cfg.enabled = false;
    kk_danmaku *d = make(cfg, 1);
    CHECK(!say(d, "oi", NULL, 0, 0));
    cfg.enabled = true;
    kk_danmaku_configure(d, &cfg);
    CHECK(!say(d, "", NULL, 0, 0));
    CHECK(say(d, "oi", NULL, 0, 0));
    /* Paid with no text: the amount alone goes. */
    kk_chat_msg m = {.platform = "demo", .user_id = "x", .name = "x", .text = "",
                     .amount = "R$ 5,00", .kind = KK_MSG_PAID};
    CHECK(kk_danmaku_message(d, &m, NULL, 0, 0));
    CHECK_INT_EQ(kk_danmaku_count(d), 2);
    cfg.enabled = false;
    kk_danmaku_configure(d, &cfg); /* turned off: the screen clears */
    CHECK_INT_EQ(kk_danmaku_count(d), 0);
    kk_danmaku_free(d);
}

TEST(crosses_in_time)
{
    kk_danmaku *d = make(on(), 2);
    CHECK(say(d, "atravessando a tela", NULL, 0, 100.0));
    kk_rect r = kk_danmaku_rect(d, 0);
    CHECK_INT_EQ(r.x, W); /* starts just off the right edge */
    CHECK(r.y >= 0 && r.y + r.h <= H);
    kk_danmaku_update(d, 104.0);
    kk_rect half = kk_danmaku_rect(d, 0);
    CHECK_MSG(abs(half.x + half.w / 2 - W / 2) <= 1, "x %d w %d", half.x, half.w);
    CHECK(half.y == r.y);
    kk_danmaku_update(d, 107.9);
    CHECK_INT_EQ(kk_danmaku_count(d), 1);
    kk_danmaku_update(d, 108.01);
    CHECK_INT_EQ(kk_danmaku_count(d), 0);
    kk_danmaku_free(d);
}

TEST(no_overlap_while_lanes_are_free)
{
    /* Many at once: each in a lane of its own, all on screen. */
    kk_danmaku *d = make(on(), 3);
    int lanes = H / kk_danmaku_lane_height(d);
    CHECK(lanes >= 10);
    for (int i = 0; i < lanes; i++)
        CHECK(say(d, i % 2 ? "curta" : "uma mensagem bem mais comprida que a outra", NULL, 0, 0));
    for (int i = 0; i < lanes; i++)
        for (int k = i + 1; k < lanes; k++)
            CHECK(kk_danmaku_rect(d, i).y != kk_danmaku_rect(d, k).y);
    /* A short one, then a long (faster) one in the same lane when only that
     * lane is free: only once the long one can't catch up. */
    kk_danmaku_free(d);
    kk_danmaku_config cfg = on();
    d = make(cfg, 4);
    kk_danmaku_resize(d, W, kk_danmaku_lane_height(d)); /* one lane */
    CHECK(say(d, "curta", NULL, 0, 0));
    CHECK(say(d, "uma mensagem bem mais comprida que a outra, bem mais", NULL, 0, 0.5));
    kk_danmaku_update(d, 0.5);
    /* With one lane there is nowhere else: it goes anyway (the lane that
     * frees first); with two, the free one. */
    CHECK_INT_EQ(kk_danmaku_count(d), 2);
    kk_danmaku_resize(d, W, 2 * kk_danmaku_lane_height(d));
    CHECK(say(d, "terceira", NULL, 0, 0.6));
    CHECK(kk_danmaku_rect(d, 2).y > kk_danmaku_rect(d, 0).y);
    kk_danmaku_free(d);
}

TEST(keeps_off_the_avatars)
{
    /* Avatars over the bottom half: everything goes to the top half while
     * there is room, and at random there (not always the same lane). */
    kk_rect avatars[] = {{100, H / 2, 200, H / 2}, {900, H / 2 + 40, 100, H / 2 - 40}};
    int seen[64] = {0}, distinct = 0;
    for (uint64_t seed = 1; seed <= 40; seed++) {
        kk_danmaku *d = make(on(), seed);
        CHECK(say(d, "oi", avatars, 2, 0));
        kk_rect r = kk_danmaku_rect(d, 0);
        CHECK_MSG(r.y + r.h <= H / 2 + 2, "y %d h %d", r.y, r.h);
        int lane = r.y / kk_danmaku_lane_height(d);
        if (lane < 64 && !seen[lane]++)
            distinct++;
        kk_danmaku_free(d);
    }
    CHECK_MSG(distinct >= 4, "only %d lanes used", distinct);

    /* Top half full: then over the avatars, the least covered first. */
    kk_danmaku *d = make(on(), 9);
    int lane_h = kk_danmaku_lane_height(d), top = (H / 2) / lane_h;
    for (int i = 0; i < top; i++)
        CHECK(say(d, "enchendo", avatars, 2, 0));
    for (int i = 0; i < top; i++)
        CHECK(kk_danmaku_rect(d, i).y + kk_danmaku_rect(d, i).h <= H / 2 + 2);
    CHECK(say(d, "agora embaixo", avatars, 2, 0));
    kk_rect r = kk_danmaku_rect(d, top);
    CHECK(r.y + r.h > H / 2);
    CHECK_MSG(r.y + r.h <= H / 2 + 40 + lane_h, "y %d", r.y); /* only the wide one */
    kk_danmaku_free(d);

    /* Avoiding off: anywhere. */
    kk_danmaku_config cfg = on();
    cfg.avoid_avatars = false;
    bool below = false;
    for (uint64_t seed = 1; seed <= 40 && !below; seed++) {
        d = make(cfg, seed);
        say(d, "oi", avatars, 2, 0);
        below = kk_danmaku_rect(d, 0).y > H / 2;
        kk_danmaku_free(d);
    }
    CHECK(below);
}

TEST(full_drops)
{
    kk_danmaku_config cfg = on();
    cfg.max_on_screen = 3;
    kk_danmaku *d = make(cfg, 5);
    CHECK(say(d, "1", NULL, 0, 0) && say(d, "2", NULL, 0, 0) && say(d, "3", NULL, 0, 0));
    CHECK(!say(d, "4", NULL, 0, 0));
    kk_danmaku_free(d);
}

int main(void)
{
    RUN(config_danmaku);
    RUN(off_or_empty_shows_nothing);
    RUN(crosses_in_time);
    RUN(no_overlap_while_lanes_are_free);
    RUN(keeps_off_the_avatars);
    RUN(full_drops);
    return harness_report();
}
