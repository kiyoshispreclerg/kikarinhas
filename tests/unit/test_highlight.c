/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The highlighted comment: the [highlight] section, where the card goes,
 * the spotlight following the author's avatar, fading in and out. The
 * avatar is made by hand, with a sheet that has no picture. */
#include <stdlib.h>
#include <string.h>

#include "../harness.h"
#include "config.h"
#include "highlight.h"
#include "ini.h"

#define W 800
#define H 600

static kk_sa_avatar def;
static kk_sheet sheet;
static kk_stage stage;
static kk_avatar avatars[2];

static void setup(void)
{
    for (int i = 0; i < stage.count; i++)
        kk_avatar_free(&avatars[i]);
    def = (kk_sa_avatar){.frame_w = 32, .frame_h = 32, .move_speed = 1.0};
    sheet = (kk_sheet){.store_w = 32, .store_h = 32, .cell_w = 64, .cell_h = 64, .cols = 1,
                       .rows = 1, .foot_pad = 4, .head_pad = 8};
    stage = (kk_stage){.width = W, .height = H, .avatars = avatars, .cap = 2,
                       .cfg = {.scale = 2.0, .ground_margin = 20}};
    kk_rng_seed(&stage.rng, 7);
    kk_avatar_init(&avatars[stage.count++], &def, &sheet, "Ana", "youtube:ana",
                   cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 8, 8), 300, 2.0, &stage.rng);
}

static void count(void *ud, int line, const char *msg)
{
    (void)line;
    (void)msg;
    (*(int *)ud)++;
}

TEST(config_highlight)
{
    kk_config c;
    kk_config_defaults(&c);
    CHECK(c.hl_card && c.hl_spotlight && c.hl_x == 50 && c.hl_y == 25 && c.hl_seconds == 0);
    int warnings = 0;
    kk_ini *ini = kk_ini_parse("[highlight]\ncard = no\nx = 10\ny = 90\nwidth = 30\n"
                               "font = Serif Bold\nfont_size = 24\nseconds = 15\n"
                               "spotlight = no\ny = 120\nwhat = 1\n",
                               NULL, NULL);
    kk_config_apply(&c, ini, count, &warnings);
    kk_ini_free(ini);
    CHECK_INT_EQ(warnings, 2); /* y = 120, what */
    CHECK(!c.hl_card && !c.hl_spotlight && c.hl_x == 10 && c.hl_y == 90);
    CHECK(c.hl_width == 30 && c.hl_size == 24 && c.hl_seconds == 15);
    CHECK_STR_EQ(c.hl_font, "Serif Bold");
    kk_config copy;
    CHECK(kk_config_copy(&copy, &c));
    CHECK_STR_EQ(copy.hl_font, "Serif Bold");
    kk_config_free(&copy);
    kk_config_free(&c);
}

typedef struct {
    kk_rect r[16];
    int n;
} damage;

static void add_damage(void *to, kk_rect r)
{
    damage *d = to;
    if (d->n < 16)
        d->r[d->n++] = r;
}

static damage frame(kk_highlight *h, double now)
{
    kk_highlight_update(h, now);
    damage d = {0};
    const kk_layer *card = kk_highlight_layer(h), *spot = kk_highlight_spot_layer(h);
    card->damage(card->ud, add_damage, &d);
    spot->damage(spot->ud, add_damage, &d);
    cairo_surface_t *s = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, W, H);
    cairo_t *cr = cairo_create(s);
    for (int i = 0; i < d.n; i++) {
        spot->draw(spot->ud, cr, d.r[i]);
        card->draw(card->ud, cr, d.r[i]);
    }
    cairo_destroy(cr);
    cairo_surface_destroy(s);
    card->painted(card->ud);
    spot->painted(spot->ud);
    return d;
}

TEST(card_and_spotlight)
{
    setup();
    kk_highlight *h = kk_highlight_new(&stage, NULL);
    kk_highlight_config c = {.card = true, .x = 50, .y = 25, .width = 50, .size = 18,
                             .spotlight = true};
    kk_highlight_configure(h, &c);
    CHECK(kk_rect_empty(kk_highlight_card_rect(h)));
    CHECK(!kk_highlight_active(h));

    kk_chat_msg m = {.platform = "youtube", .user_id = "ana", .name = "Ana",
                     .text = "uma pergunta bem comprida para quebrar em mais de uma linha "
                             "no cartão do destaque, só para ver"};
    kk_highlight_show(h, &m, 10);
    CHECK(kk_highlight_active(h));
    damage d = frame(h, 10);
    CHECK(d.n >= 2); /* the card and the light */
    kk_rect r = kk_highlight_card_rect(h);
    CHECK(r.w <= W / 2 + 4 && r.w > 100 && r.h > 40);
    CHECK(abs(r.x + r.w / 2 - W / 2) <= 1 && abs(r.y + r.h / 2 - H / 4) <= 1);
    kk_rect s = kk_highlight_spot_rect(h);
    CHECK(s.y == 0 && s.x < 300 && s.x + s.w > 300); /* from the top, around Ana */

    frame(h, 11); /* faded in */
    d = frame(h, 11.5);
    CHECK_INT_EQ(d.n, 0); /* nothing moves: nothing to repaint */
    avatars[0].x = 500;
    d = frame(h, 12);
    CHECK_INT_EQ(d.n, 1); /* the light follows */
    s = kk_highlight_spot_rect(h);
    CHECK(s.x < 500 && s.x + s.w > 500);

    /* Near a corner the card stays inside the window. */
    c.x = 100;
    c.y = 100;
    kk_highlight_configure(h, &c);
    frame(h, 12.1);
    r = kk_highlight_card_rect(h);
    CHECK(r.x + r.w == W && r.y + r.h == H);

    kk_highlight_clear(h, 13);
    CHECK(!kk_highlight_active(h));
    frame(h, 13.1);
    CHECK(!kk_rect_empty(kk_highlight_card_rect(h))); /* fading out */
    d = frame(h, 14);
    CHECK(d.n >= 1); /* where it was gets repainted */
    CHECK(kk_rect_empty(kk_highlight_card_rect(h)) && kk_rect_empty(kk_highlight_spot_rect(h)));
    kk_highlight_free(h);
}

TEST(goes_by_itself_and_without_avatar)
{
    setup();
    kk_highlight *h = kk_highlight_new(&stage, NULL);
    kk_highlight_config c = {.card = true, .x = 50, .y = 25, .width = 50, .size = 18,
                             .seconds = 5, .spotlight = true};
    kk_highlight_configure(h, &c);
    kk_chat_msg m = {.platform = "twitch", .user_id = "nobody", .name = "Bia", .text = "oi",
                     .amount = "100 bits", .kind = KK_MSG_PAID};
    kk_highlight_show(h, &m, 0);
    frame(h, 0);
    CHECK(kk_rect_empty(kk_highlight_spot_rect(h))); /* no avatar: no light */
    CHECK(!kk_rect_empty(kk_highlight_card_rect(h)));
    frame(h, 4.9);
    CHECK(kk_highlight_active(h));
    frame(h, 5.0);
    CHECK(!kk_highlight_active(h));
    frame(h, 6);
    CHECK(kk_rect_empty(kk_highlight_card_rect(h)));

    /* Only the light. */
    c.card = false;
    kk_highlight_configure(h, &c);
    m.user_id = "ana";
    m.platform = "youtube";
    kk_highlight_show(h, &m, 10);
    frame(h, 10);
    CHECK(kk_rect_empty(kk_highlight_card_rect(h)));
    CHECK(!kk_rect_empty(kk_highlight_spot_rect(h)));
    kk_highlight_free(h);
}

int main(void)
{
    RUN(config_highlight);
    RUN(card_and_spotlight);
    RUN(goes_by_itself_and_without_avatar);
    for (int i = 0; i < stage.count; i++)
        kk_avatar_free(&avatars[i]);
    return harness_report();
}
