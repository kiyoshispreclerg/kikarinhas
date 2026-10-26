/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The classic chat: the [chat_window] section, the history, what is hidden,
 * scrolling, fading and clicking on a message. */
#include <stdlib.h>
#include <string.h>

#include "../harness.h"
#include "chatview.h"
#include "config.h"
#include "ini.h"

#define W 400
#define H 300

static int warnings;

static void count(void *ud, int line, const char *msg)
{
    (void)ud;
    (void)line;
    (void)msg;
    warnings++;
}

TEST(config_chat_window)
{
    kk_config c;
    kk_config_defaults(&c);
    CHECK(!c.chatwin_open && c.chatwin_resizable && !c.chatwin_bubbles);
    CHECK(c.chatwin_width == 400 && c.chatwin_height == 600);
    CHECK(c.chatwin_background == KK_CONFIG_BG_TRANSPARENT && c.chatwin_hide_commands);
    warnings = 0;
    kk_ini *ini = kk_ini_parse("[chat_window]\nopen = yes\nsize = 500x700\nresizable = no\n"
                               "style = bubbles\nfont = Serif\nfont_size = 18\n"
                               "background = color\nbackground_color = #10203040\n"
                               "background_mode = tile\nbackground_image = /x.png\n"
                               "hide_users = Nightbot, @Bot\nfade = 30\nnewest = top\n"
                               "history = 50\noutline_width = 0\nstyle = round\n"
                               "background = video\nwhat = 1\n",
                               NULL, NULL);
    kk_config_apply(&c, ini, count, NULL);
    kk_ini_free(ini);
    CHECK_INT_EQ(warnings, 3); /* style = round, background = video, what */
    CHECK(c.chatwin_open && !c.chatwin_resizable && c.chatwin_bubbles);
    CHECK(c.chatwin_width == 500 && c.chatwin_height == 700 && c.chatwin_size == 18);
    CHECK(c.chatwin_background == KK_CONFIG_BG_COLOR && c.chatwin_bg_tile);
    CHECK(c.chatwin_bg_color == 0x10203040);
    CHECK_STR_EQ(c.chatwin_font, "Serif");
    CHECK_STR_EQ(c.chatwin_bg_image, "/x.png");
    CHECK(c.chatwin_fade == 30 && c.chatwin_newest_top && c.chatwin_history == 50);
    CHECK(c.chatwin_outline_width == 0);
    kk_config copy;
    CHECK(kk_config_copy(&copy, &c));
    CHECK_STR_EQ(copy.chatwin_hide_users, "Nightbot, @Bot");
    kk_config_free(&copy);
    kk_config_free(&c);

    uint32_t v;
    CHECK(kk_parse_rgba("#abcdef", &v) && v == 0xabcdefff);
    CHECK(kk_parse_rgba("#fff", &v) && v == 0xffffffff);
    CHECK(kk_parse_rgba("00000080", &v) && v == 0x80);
    CHECK(!kk_parse_rgba("#0000008g", &v) && !kk_parse_rgba("#00000", &v));
    char out[10];
    kk_format_rgba(out, 0x0a0b0c0d);
    CHECK_STR_EQ(out, "#0a0b0c0d");
}

static kk_chatview_config defaults(void)
{
    return (kk_chatview_config){.size = 12, .text_color = 0xffffff, .name_color = 0xffffff,
                                .badge_colors = true, .outline_width = 2,
                                .paid_color = 0xffd24a, .member_color = 0x8ef09c,
                                .show_platform = true, .hide_commands = true,
                                .history = 100};
}

static uint64_t say(kk_chatview *v, const char *who, const char *text, bool command, double now)
{
    char id[64];
    snprintf(id, sizeof id, "id-%s", who);
    kk_chat_msg m = {.platform = "youtube", .user_id = id, .name = who, .text = text};
    return kk_chatview_add(v, &m, command, now);
}

static void draw(kk_chatview *v, double now)
{
    cairo_surface_t *s = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, W, H);
    cairo_t *cr = cairo_create(s);
    kk_chatview_draw(v, cr, now);
    cairo_destroy(cr);
    cairo_surface_destroy(s);
}

TEST(history_and_filters)
{
    kk_chatview *v = kk_chatview_new(NULL);
    kk_chatview_config c = defaults();
    c.history = 3;
    c.hide_users = "Nightbot";
    kk_chatview_configure(v, &c);
    kk_chatview_resize(v, W, H);

    uint64_t a = say(v, "Ana", "oi", false, 1);
    CHECK(a > 0);
    say(v, "Bia", "!dance", true, 2);
    say(v, "Nightbot", "siga o canal", false, 3);
    uint64_t d = say(v, "Caio", "tudo bem?", false, 4);
    CHECK_INT_EQ(kk_chatview_count(v), 3); /* Ana went out */
    kk_chat_msg m;
    CHECK(!kk_chatview_get(v, a, &m));
    CHECK(kk_chatview_get(v, d, &m));
    CHECK_STR_EQ(m.name, "Caio");
    CHECK_STR_EQ(m.user_id, "id-Caio");

    draw(v, 5);
    CHECK_INT_EQ(kk_chatview_drawn(v), 1); /* the command and the bot are hidden */
    c.hide_commands = false;
    kk_chatview_configure(v, &c);
    draw(v, 5);
    CHECK_INT_EQ(kk_chatview_drawn(v), 2);

    kk_chatview_ignore(v, "youtube:id-Bia");
    draw(v, 5);
    CHECK_INT_EQ(kk_chatview_drawn(v), 1);
    kk_chatview_hide(v, d);
    CHECK(kk_chatview_dirty(v, 5));
    draw(v, 5);
    CHECK_INT_EQ(kk_chatview_drawn(v), 0);

    /* Growing the history keeps what is there. */
    c.history = 10;
    kk_chatview_configure(v, &c);
    CHECK_INT_EQ(kk_chatview_count(v), 3);
    say(v, "Bia", "voltei", false, 6); /* still ignored */
    draw(v, 6);
    CHECK_INT_EQ(kk_chatview_drawn(v), 0); /* Bia ignored, the bot by name, Caio hidden */
    say(v, "Davi", "olá", false, 7);
    draw(v, 7);
    CHECK_INT_EQ(kk_chatview_drawn(v), 1);
    kk_chatview_free(v);
}

TEST(newest_at_the_bottom_and_clicks)
{
    kk_chatview *v = kk_chatview_new(NULL);
    kk_chatview_config c = defaults();
    kk_chatview_configure(v, &c);
    kk_chatview_resize(v, W, H);
    uint64_t first = say(v, "Ana", "primeira", false, 1);
    uint64_t last = say(v, "Bia", "segunda", false, 2);
    CHECK(kk_chatview_dirty(v, 2));
    draw(v, 2);
    CHECK(!kk_chatview_dirty(v, 2.5));
    CHECK_INT_EQ(kk_chatview_drawn(v), 2);
    CHECK(kk_chatview_hit(v, W / 2, H - 12) == last);
    CHECK(kk_chatview_hit(v, 2, H - 12) == 0); /* the margin */
    CHECK(kk_chatview_hit(v, W / 2, 10) == 0); /* empty above */

    c.newest_top = true;
    kk_chatview_configure(v, &c);
    draw(v, 3);
    CHECK(kk_chatview_hit(v, W / 2, 12) == last);
    uint64_t below = 0;
    for (int y = 12; y < H && !below; y++)
        if (kk_chatview_hit(v, W / 2, y) == first)
            below = first;
    CHECK(below == first);

    kk_chatview_set_marked(v, first);
    CHECK(kk_chatview_marked(v) == first && kk_chatview_dirty(v, 3));
    kk_chatview_free(v);
}

TEST(scrolling_back_and_returning)
{
    kk_chatview *v = kk_chatview_new(NULL);
    kk_chatview_config c = defaults();
    kk_chatview_configure(v, &c);
    kk_chatview_resize(v, W, H);
    uint64_t ids[40];
    for (int i = 0; i < 40; i++) {
        char text[32];
        snprintf(text, sizeof text, "mensagem %d", i);
        ids[i] = say(v, "Ana", text, false, i);
    }
    draw(v, 40);
    CHECK(kk_chatview_hit(v, W / 2, H - 12) == ids[39]);
    CHECK(!kk_chatview_scrolled(v));

    kk_chatview_scroll(v, 100000, 41); /* clamped at the oldest */
    draw(v, 41);
    CHECK(kk_chatview_scrolled(v));
    CHECK(kk_chatview_hit(v, W / 2, 12) == ids[0]);

    /* A new message doesn't move what is being read. */
    say(v, "Bia", "nova", false, 42);
    draw(v, 42);
    CHECK(kk_chatview_hit(v, W / 2, 12) == ids[0]);

    /* Left alone, it goes back to the newest. */
    CHECK(kk_chatview_dirty(v, 42 + 31));
    draw(v, 42 + 31);
    CHECK(!kk_chatview_scrolled(v));
    kk_chatview_scroll(v, -50, 80); /* past the newest: stays there */
    CHECK(!kk_chatview_scrolled(v));
    kk_chatview_free(v);
}

TEST(fading)
{
    kk_chatview *v = kk_chatview_new(NULL);
    kk_chatview_config c = defaults();
    c.fade = 10;
    kk_chatview_configure(v, &c);
    kk_chatview_resize(v, W, H);
    say(v, "Ana", "velha", false, 0);
    say(v, "Bia", "nova", false, 5);
    draw(v, 6);
    CHECK_INT_EQ(kk_chatview_drawn(v), 2);
    CHECK(!kk_chatview_dirty(v, 8.5));
    CHECK(kk_chatview_dirty(v, 9.2)); /* Ana starts fading */
    draw(v, 9.5);
    CHECK_INT_EQ(kk_chatview_drawn(v), 2);
    CHECK(kk_chatview_dirty(v, 9.6)); /* still fading */
    draw(v, 10.5);
    CHECK_INT_EQ(kk_chatview_drawn(v), 1);
    draw(v, 16);
    CHECK_INT_EQ(kk_chatview_drawn(v), 0);
    kk_chatview_free(v);
}

TEST(styles_and_release)
{
    kk_chatview *v = kk_chatview_new(NULL);
    kk_chatview_config c = defaults();
    c.bubbles = true;
    c.show_time = true;
    c.background = KK_CHATVIEW_BG_IMAGE;
    c.bg_image = "/não/existe.png"; /* just nothing behind */
    kk_chatview_configure(v, &c);
    kk_chatview_resize(v, W, H);
    kk_chat_msg paid = {.platform = "twitch", .user_id = "1", .name = "Ana",
                        .text = "toma 😀", .amount = "100 bits", .kind = KK_MSG_PAID,
                        .badges = KK_BADGE_MOD};
    kk_chat_msg member = {.platform = "kick", .user_id = "2", .name = "Bia", .text = "",
                          .kind = KK_MSG_MEMBER};
    CHECK(kk_chatview_add(v, &paid, false, 1));
    CHECK(kk_chatview_add(v, &member, false, 1));
    draw(v, 1);
    CHECK_INT_EQ(kk_chatview_drawn(v), 2);
    kk_chatview_release(v);
    CHECK_INT_EQ(kk_chatview_count(v), 2);
    CHECK(kk_chatview_hit(v, W / 2, H - 12) == 0);
    c.bubbles = false;
    c.background = KK_CHATVIEW_BG_COLOR;
    c.bg_color = 0x20202080;
    kk_chatview_configure(v, &c);
    kk_chatview_resize(v, 120, H); /* narrow: the text wraps */
    draw(v, 2);
    CHECK_INT_EQ(kk_chatview_drawn(v), 2);
    kk_chatview_free(v);
}

int main(void)
{
    RUN(config_chat_window);
    RUN(history_and_filters);
    RUN(newest_at_the_bottom_and_clicks);
    RUN(scrolling_back_and_returning);
    RUN(fading);
    RUN(styles_and_release);
    return harness_report();
}
