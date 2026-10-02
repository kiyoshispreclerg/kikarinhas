/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../harness.h"
#include "config.h"
#include "emoji.h"
#include "emotes.h"
#include "emotewall.h"
#include "util.h"

/* ---- emoji --------------------------------------------------------------- */

/* The emoji found in s, joined by "|". */
static const char *scan(const char *s)
{
    static char out[512];
    size_t len = strlen(s), pos = 0, start, n, used = 0;
    out[0] = '\0';
    while ((n = kk_emoji_next(s, len, &pos, &start))) {
        int w = snprintf(out + used, sizeof out - used, "%s%.*s", used ? "|" : "",
                         (int)n, s + start);
        used += (size_t)w;
    }
    return out;
}

TEST(emoji_found_in_text)
{
    CHECK_STR_EQ(scan("oi chat"), "");
    CHECK_STR_EQ(scan("oi 😂 tudo"), "😂");
    CHECK_STR_EQ(scan("😂😂🔥"), "😂|😂|🔥");
    /* Skin tone, ZWJ sequence, flag, keycap stay whole. */
    CHECK_STR_EQ(scan("👍🏽 👩‍💻 🇧🇷 1️⃣"), "👍🏽|👩‍💻|🇧🇷|1️⃣");
    /* ❤ is a heart with or without U+FE0F. */
    CHECK_STR_EQ(scan("❤️ ❤"), "❤️|❤");
    /* Text-style symbols only with U+FE0F; plain digits and # never. */
    CHECK_STR_EQ(scan("© ™ ©️ a#b 12"), "©️");
    /* A lone regional indicator is not a flag. */
    CHECK_STR_EQ(scan("\xF0\x9F\x87\xA7 x"), "");
    /* Broken UTF-8 is skipped, not fatal. */
    CHECK_STR_EQ(scan("\xff\xc3😂\xe2\x9d"), "😂");
}

TEST(emoji_key_and_one)
{
    char key[32];
    CHECK(kk_emoji_key("❤️", strlen("❤️"), key, sizeof key));
    CHECK_STR_EQ(key, "❤");
    CHECK(kk_emoji_key("👍🏽", strlen("👍🏽"), key, sizeof key));
    CHECK_STR_EQ(key, "👍🏽");
    CHECK(!kk_emoji_key("👍🏽", strlen("👍🏽"), key, 4));
    CHECK(kk_emoji_is_one("😂"));
    CHECK(kk_emoji_is_one("👩‍💻"));
    CHECK(!kk_emoji_is_one("😂😂"));
    CHECK(!kk_emoji_is_one("😂 "));
    CHECK(!kk_emoji_is_one("Kappa"));
}

/* ---- images -------------------------------------------------------------- */

static char tmpdir[64];

static void make_tmpdir(void)
{
    snprintf(tmpdir, sizeof tmpdir, "/tmp/kk-wall-XXXXXX");
    if (!mkdtemp(tmpdir))
        tmpdir[0] = '\0';
}

static void remove_tmpdir(void)
{
    char cmd[128];
    snprintf(cmd, sizeof cmd, "rm -rf '%s'", tmpdir);
    if (tmpdir[0] && system(cmd) != 0)
        fprintf(stderr, "could not remove %s\n", tmpdir);
}

static bool write_png(const char *path, int w, int h)
{
    cairo_surface_t *s = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
    cairo_t *cr = cairo_create(s);
    cairo_set_source_rgb(cr, 1, 0, 0);
    cairo_paint(cr);
    cairo_destroy(cr);
    kk_make_parent_dirs(path);
    bool ok = cairo_surface_write_to_png(s, path) == CAIRO_STATUS_SUCCESS;
    cairo_surface_destroy(s);
    return ok;
}

TEST(emotes_unicode_and_disk)
{
    make_tmpdir();
    CHECK(tmpdir[0]);
    kk_emotes *e = kk_emotes_new(NULL, tmpdir, 40);
    CHECK(e);

    cairo_surface_t *img = NULL;
    kk_emote heart = {.text = "❤"};
    CHECK_INT_EQ(kk_emotes_get(e, "youtube", &heart, &img), KK_EMOTE_READY);
    CHECK_INT_EQ(cairo_image_surface_get_width(img), 40);
    /* ❤️ is the same image. */
    cairo_surface_t *again = NULL;
    kk_emote heart2 = {.text = "❤️"};
    kk_emotes_get(e, "x", &heart2, &again);
    CHECK(again == img);

    /* Not on disk and no network: fails. */
    kk_emote custom = {.id = "UCxx/abc", .name = ":_hi:", .url = "https://e.invalid/a.png"};
    CHECK_INT_EQ(kk_emotes_get(e, "youtube", &custom, &img), KK_EMOTE_FAILED);

    /* From the disk cache, fitted to the size (wide images keep their
     * aspect). */
    char path[KK_PATH_MAX];
    CHECK(kk_emotes_file(e, "twitch", "25", path, sizeof path));
    CHECK(write_png(path, 56, 28));
    kk_emote kappa = {.id = "25", .name = "Kappa", .url = "https://e.invalid/25.png"};
    CHECK_INT_EQ(kk_emotes_get(e, "twitch", &kappa, &img), KK_EMOTE_READY);
    CHECK_INT_EQ(cairo_image_surface_get_height(img), 40);
    cairo_surface_flush(img);
    const uint32_t *px = (const uint32_t *)cairo_image_surface_get_data(img);
    int stride = cairo_image_surface_get_stride(img) / 4;
    CHECK(px[20 * stride + 20] == 0xFFFF0000); /* red in the middle */
    CHECK(px[2 * stride + 20] == 0);           /* letterboxed top */

    /* A new size makes everything again. */
    kk_emotes_set_size(e, 24);
    CHECK_INT_EQ(kk_emotes_get(e, "twitch", &kappa, &img), KK_EMOTE_READY);
    CHECK_INT_EQ(cairo_image_surface_get_width(img), 24);

    /* Something that is not a PNG fails. */
    CHECK(kk_emotes_file(e, "twitch", "bad", path, sizeof path));
    FILE *f = fopen(path, "w");
    CHECK(f);
    fputs("GIF89a...", f);
    fclose(f);
    kk_emote bad = {.id = "bad", .url = "https://e.invalid/bad.gif"};
    CHECK_INT_EQ(kk_emotes_get(e, "twitch", &bad, &img), KK_EMOTE_FAILED);

    kk_emotes_free(e);
    remove_tmpdir();
}

/* ---- wall ---------------------------------------------------------------- */

static kk_emotes *images;

static kk_emotewall *wall(kk_wall_config cfg)
{
    kk_emotewall *w = kk_emotewall_new(images, 1);
    cfg.enabled = true;
    if (cfg.duration == 0)
        cfg.duration = 5;
    if (cfg.max_on_screen == 0)
        cfg.max_on_screen = 150;
    if (cfg.combo_window == 0)
        cfg.combo_window = 10;
    kk_emotewall_configure(w, &cfg);
    kk_emotewall_resize(w, 640, 360);
    return w;
}

static void say(kk_emotewall *w, const char *text, double now)
{
    kk_chat_msg m = {.platform = "demo", .user_id = "u", .name = "n", .text = text};
    kk_emotewall_message(w, &m, now);
}

TEST(wall_min_per_message)
{
    kk_emotewall *w = wall((kk_wall_config){.min_per_message = 3, .max_per_message = 10});
    say(w, "oi 😂😂", 0);
    CHECK_INT_EQ(kk_emotewall_waiting(w), 0);
    say(w, "😂 kkk 😂🔥", 0);
    CHECK_INT_EQ(kk_emotewall_waiting(w), 3);
    kk_emotewall_update(w, 0.5);
    CHECK_INT_EQ(kk_emotewall_waiting(w), 0);
    CHECK_INT_EQ(kk_emotewall_flying(w), 3);
    /* Each goes away after its time. */
    kk_emotewall_update(w, 10);
    CHECK_INT_EQ(kk_emotewall_flying(w), 0);
    /* Capped per message. */
    say(w, "🔥🔥🔥🔥🔥🔥🔥🔥🔥🔥🔥🔥🔥🔥🔥🔥🔥🔥🔥🔥", 20);
    CHECK_INT_EQ(kk_emotewall_waiting(w), 10);
    kk_emotewall_free(w);
}

TEST(wall_combo)
{
    kk_emotewall *w = wall((kk_wall_config){.combo_count = 3, .combo_window = 10,
                                            .max_per_message = 10});
    say(w, "olha 🔥", 0);
    say(w, "🔥🔥", 1); /* one sighting per message */
    CHECK_INT_EQ(kk_emotewall_waiting(w), 0);
    say(w, "sim 🔥 😂", 2); /* third message: the combo starts, 😂 stays */
    CHECK_INT_EQ(kk_emotewall_waiting(w), 3);
    say(w, "🔥", 3); /* running: goes straight up */
    CHECK_INT_EQ(kk_emotewall_waiting(w), 4);
    say(w, "🔥🔥", 12.5); /* still running: the last one extended it */
    CHECK_INT_EQ(kk_emotewall_waiting(w), 6);
    say(w, "🔥", 30); /* over; counting starts again */
    CHECK_INT_EQ(kk_emotewall_waiting(w), 6);
    kk_emotewall_free(w);
}

TEST(wall_rules_off_and_disabled)
{
    kk_emotewall *w = wall((kk_wall_config){.max_per_message = 10});
    say(w, "um 😂", 0);
    CHECK_INT_EQ(kk_emotewall_waiting(w), 1);
    kk_emotewall_update(w, 0);
    CHECK_INT_EQ(kk_emotewall_flying(w), 1);
    /* Turning it off clears the screen and ignores the chat. */
    kk_emotewall_configure(w, &(kk_wall_config){.enabled = false});
    CHECK_INT_EQ(kk_emotewall_flying(w), 0);
    say(w, "um 😂", 1);
    CHECK_INT_EQ(kk_emotewall_waiting(w), 0);
    kk_emotewall_free(w);
}

TEST(wall_blacklist)
{
    kk_emotewall *w = wall((kk_wall_config){.min_per_message = 3, .max_per_message = 10,
                                            .blacklist = "😂️, :_hello:, kappa ,, "});
    CHECK(kk_emotewall_blocked(w, &(kk_emote){.text = "😂"}));
    CHECK(!kk_emotewall_blocked(w, &(kk_emote){.text = "🔥"}));
    CHECK(kk_emotewall_blocked(w, &(kk_emote){.id = "UC/1", .name = ":_hello:", .url = "https://x"}));
    CHECK(kk_emotewall_blocked(w, &(kk_emote){.id = "UC/1", .name = ":hello:", .url = "https://x"}));
    CHECK(kk_emotewall_blocked(w, &(kk_emote){.id = "25", .name = "Kappa", .url = "https://x"}));
    CHECK(!kk_emotewall_blocked(w, &(kk_emote){.id = "26", .name = "KappaPride", .url = "https://x"}));
    /* Blacklisted ones don't count toward the minimum either. */
    say(w, "😂😂😂🔥", 0);
    CHECK_INT_EQ(kk_emotewall_waiting(w), 0);
    say(w, "😂🔥🔥🔥", 0);
    CHECK_INT_EQ(kk_emotewall_waiting(w), 3);
    kk_emotewall_free(w);
}

TEST(wall_image_emotes)
{
    kk_emotewall *w = wall((kk_wall_config){.max_per_message = 10});
    kk_emote em = {.id = "UCxx/abc", .name = ":_hi:", .url = "https://e.invalid/a.png",
                   .start = 3, .len = 5};
    kk_chat_msg m = {.platform = "youtube", .user_id = "u", .name = "n",
                     .text = "oi :_hi: ❤", .emotes = &em, .n_emotes = 1};
    kk_emotewall_message(w, &m, 0);
    CHECK_INT_EQ(kk_emotewall_waiting(w), 2);
    /* No network here: the image fails and only the heart flies (a burst
     * comes out over a moment). */
    kk_emotewall_update(w, 1);
    CHECK_INT_EQ(kk_emotewall_waiting(w), 0);
    CHECK_INT_EQ(kk_emotewall_flying(w), 1);
    kk_emotewall_free(w);
}

TEST(wall_reactions)
{
    kk_emotewall *w = wall((kk_wall_config){.reactions = true, .reactions_per_icon = 2,
                                            .max_per_message = 10});
    kk_reaction r = {.platform = "youtube", .emote = {.id = "❤", .text = "❤"},
                     .count = 5, .delay = 2};
    kk_emotewall_reaction(w, &r, 0);
    CHECK_INT_EQ(kk_emotewall_waiting(w), 3);
    kk_emotewall_update(w, 1.9);
    CHECK_INT_EQ(kk_emotewall_flying(w), 0); /* not yet: they happened later */
    kk_emotewall_update(w, 3.01);
    CHECK_INT_EQ(kk_emotewall_flying(w), 3);
    kk_emotewall_free(w);

    w = wall((kk_wall_config){.reactions = false});
    kk_emotewall_reaction(w, &r, 0);
    CHECK_INT_EQ(kk_emotewall_waiting(w), 0);
    kk_emotewall_free(w);
}

TEST(wall_max_on_screen)
{
    kk_emotewall *w = wall((kk_wall_config){.max_on_screen = 5, .max_per_message = 10});
    say(w, "😂😂😂😂😂😂😂😂", 0);
    kk_emotewall_update(w, 2);
    CHECK_INT_EQ(kk_emotewall_flying(w), 5);
    kk_emotewall_free(w);
}

static kk_rect damage_sum;
static int n_damage;

static void collect_damage(void *to, kk_rect r)
{
    (void)to;
    if (kk_rect_empty(r))
        return;
    damage_sum = n_damage++ ? kk_rect_union(damage_sum, r) : r;
}

TEST(wall_layer_draws_and_damages)
{
    kk_emotewall *w = wall((kk_wall_config){.max_per_message = 10, .style = KK_WALL_FLY});
    const kk_layer *l = kk_emotewall_layer(w);
    say(w, "🔥", 0);
    kk_emotewall_update(w, 0);   /* takes off */
    kk_emotewall_update(w, 2.5); /* mid-flight, fully visible */
    CHECK_INT_EQ(kk_emotewall_flying(w), 1);

    n_damage = 0;
    l->damage(l->ud, collect_damage, NULL);
    CHECK(n_damage > 0);
    kk_rect where = damage_sum;

    cairo_surface_t *s = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 640, 360);
    cairo_t *cr = cairo_create(s);
    l->draw(l->ud, cr, (kk_rect){0, 0, 640, 360});
    cairo_destroy(cr);
    l->painted(l->ud);
    cairo_surface_flush(s);
    const uint32_t *px = (const uint32_t *)cairo_image_surface_get_data(s);
    int stride = cairo_image_surface_get_stride(s) / 4, painted = 0;
    for (int y = 0; y < 360; y++)
        for (int x = 0; x < 640; x++)
            if (px[y * stride + x] >> 24) {
                painted++;
                CHECK(kk_rect_intersects(where, (kk_rect){x, y, 1, 1}));
            }
    cairo_surface_destroy(s);
    CHECK(painted > 100);

    /* Once gone, its last place is reported so it gets erased. */
    kk_emotewall_update(w, 6);
    CHECK_INT_EQ(kk_emotewall_flying(w), 0);
    n_damage = 0;
    l->damage(l->ud, collect_damage, NULL);
    CHECK_INT_EQ(n_damage, 1);
    l->painted(l->ud);
    n_damage = 0;
    l->damage(l->ud, collect_damage, NULL);
    CHECK_INT_EQ(n_damage, 0);
    kk_emotewall_free(w);
}

/* The DVD style never leaves the window, however long it flies. */
TEST(wall_bounce_stays_inside)
{
    kk_emotewall *w = wall((kk_wall_config){.max_per_message = 10, .style = KK_WALL_BOUNCE,
                                            .duration = 60});
    const kk_layer *l = kk_emotewall_layer(w);
    say(w, "😂🔥❤️👏🎉", 0);
    for (double t = 0; t < 1; t += 0.05)
        kk_emotewall_update(w, t);
    CHECK_INT_EQ(kk_emotewall_flying(w), 5);
    int outside = 0;
    kk_rect seen = {0, 0, 0, 0};
    for (double t = 1; t < 40; t += 0.1) {
        kk_emotewall_update(w, t);
        n_damage = 0;
        l->damage(l->ud, collect_damage, NULL);
        l->painted(l->ud);
        seen = kk_rect_union(seen, damage_sum);
        outside += damage_sum.x < 0 || damage_sum.y < 0 ||
                   damage_sum.x + damage_sum.w > 640 + 1 ||
                   damage_sum.y + damage_sum.h > 360 + 1;
    }
    CHECK_INT_EQ(outside, 0);
    /* And it does reach the corners' neighbourhood. */
    CHECK(seen.x < 40 && seen.y < 40 && seen.x + seen.w > 600 && seen.y + seen.h > 320);
    kk_emotewall_free(w);
}

/* ---- config -------------------------------------------------------------- */

static int n_warnings;

static void count_warning(void *ud, int line, const char *msg)
{
    (void)ud;
    (void)line;
    (void)msg;
    n_warnings++;
}

TEST(config_emote_wall)
{
    kk_config c;
    kk_config_defaults(&c);
    CHECK(c.wall_enabled);
    CHECK_INT_EQ(c.wall_min_message, 3);
    CHECK_INT_EQ(c.wall_combo, 3);
    n_warnings = 0;
    kk_ini *ini = kk_ini_parse("[emote_wall]\nenabled = no\nduration = 7.5\nsize = 64\n"
                               "style = bounce\nmin_per_message = 0\ncombo_count = 5\n"
                               "combo_window = 20\nmax_per_message = 4\nmax_on_screen = 50\n"
                               "reactions = no\nreactions_per_icon = 10\n"
                               "blacklist = 😂, :_hi:, Kappa\n"
                               "style = spin\nsize = 2\nfoo = 1\n",
                               count_warning, NULL);
    kk_config_apply(&c, ini, count_warning, NULL);
    kk_ini_free(ini);
    CHECK_INT_EQ(n_warnings, 3); /* bad style, size too small, unknown key */
    CHECK(!c.wall_enabled);
    CHECK(c.wall_duration == 7.5);
    CHECK_INT_EQ(c.wall_size, 64);
    CHECK_INT_EQ(c.wall_style, KK_CONFIG_WALL_BOUNCE);
    CHECK_INT_EQ(c.wall_min_message, 0);
    CHECK_INT_EQ(c.wall_combo, 5);
    CHECK(c.wall_combo_window == 20);
    CHECK_INT_EQ(c.wall_max_message, 4);
    CHECK_INT_EQ(c.wall_max_screen, 50);
    CHECK(!c.wall_reactions);
    CHECK_INT_EQ(c.wall_reactions_per_icon, 10);
    CHECK_STR_EQ(c.wall_blacklist, "😂, :_hi:, Kappa");

    kk_config copy;
    CHECK(kk_config_copy(&copy, &c));
    CHECK_STR_EQ(copy.wall_blacklist, "😂, :_hi:, Kappa");
    CHECK(copy.wall_blacklist != c.wall_blacklist);
    kk_config_free(&copy);
    kk_config_free(&c);
}

int main(void)
{
    RUN(emoji_found_in_text);
    RUN(emoji_key_and_one);
    RUN(emotes_unicode_and_disk);
    images = kk_emotes_new(NULL, NULL, 32);
    RUN(wall_min_per_message);
    RUN(wall_combo);
    RUN(wall_rules_off_and_disabled);
    RUN(wall_blacklist);
    RUN(wall_image_emotes);
    RUN(wall_reactions);
    RUN(wall_max_on_screen);
    RUN(wall_bounce_stays_inside);
    RUN(wall_layer_draws_and_damages);
    kk_emotes_free(images);
    RUN(config_emote_wall);
    return harness_report();
}
