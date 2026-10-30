/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Profile pictures: what each platform answers, WebP pictures, and the
 * pictures already on disk (no network here). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <webp/encode.h>

#include "../harness.h"
#include "chatview.h"
#include "image.h"
#include "photos.h"
#include "util.h"

TEST(answers)
{
    char url[512];
    CHECK(kk_photos_parse_twitch("https://static-cdn.jtvnw.net/jtv_user_pictures/"
                                 "abc-profile_image-300x300.png\n",
                                 url, sizeof url));
    CHECK_STR_EQ(url, "https://static-cdn.jtvnw.net/jtv_user_pictures/"
                      "abc-profile_image-150x150.png");
    CHECK(!kk_photos_parse_twitch("User not found: 0", url, sizeof url));
    CHECK(!kk_photos_parse_twitch("", url, sizeof url));

    CHECK(kk_photos_parse_kick("{\"id\":1,\"user\":{\"username\":\"Fulana\","
                               "\"profile_pic\":\"https://files.example.invalid/p.webp\"}}",
                               url, sizeof url));
    CHECK_STR_EQ(url, "https://files.example.invalid/p.webp");
    CHECK(!kk_photos_parse_kick("{\"user\":{\"profile_pic\":null}}", url, sizeof url));
    CHECK(!kk_photos_parse_kick("<html>", url, sizeof url));

    CHECK(kk_photos_parse_odysee(
        "{\"jsonrpc\":\"2.0\",\"result\":{\"lbry://@fulana#abc\":{\"value\":"
        "{\"thumbnail\":{\"url\":\"https://thumbs.example.invalid/a.webp\"}}}}}",
        url, sizeof url));
    CHECK_STR_EQ(url, "https://thumbnails.odycdn.com/optimize/s:96:96/quality:85/plain/"
                      "https://thumbs.example.invalid/a.webp");
    CHECK(!kk_photos_parse_odysee("{\"result\":{\"lbry://@x#1\":{\"value\":{}}}}", url,
                                  sizeof url));

    char slug[64];
    CHECK(kk_photos_kick_slug("Some_Name9", slug, sizeof slug));
    CHECK_STR_EQ(slug, "some_name9");
    CHECK(!kk_photos_kick_slug("a b", slug, sizeof slug));
    CHECK(!kk_photos_kick_slug("", slug, sizeof slug));
}

/* A 2x2 WebP: red, green, blue and see-through. */
static size_t make_webp(uint8_t **out)
{
    static const uint8_t rgba[] = {255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 0, 0, 0, 0};
    return WebPEncodeLosslessRGBA(rgba, 2, 2, 8, out);
}

TEST(webp)
{
    uint8_t *data;
    size_t len = make_webp(&data);
    CHECK(len > 0);
    kk_image img;
    CHECK(kk_image_decode(&img, data, len));
    CHECK(img.n == 1 && img.w == 2 && img.h == 2);
    cairo_surface_t *s = img.frames[0];
    cairo_surface_flush(s);
    const uint32_t *px = (const uint32_t *)(void *)cairo_image_surface_get_data(s);
    CHECK(px[0] == 0xffff0000 && px[1] == 0xff00ff00);
    kk_image_free(&img);
    cairo_surface_t *still = kk_image_decode_still(data, len);
    CHECK(still != NULL);
    cairo_surface_destroy(still);
    CHECK(!kk_image_decode(&img, "RIFF\x10\0\0\0WEBPjunk", 16));
    WebPFree(data);
}

TEST(pictures_on_disk)
{
    char dir[] = "/tmp/kk_photos_XXXXXX", emote_dir[64], photo_dir[64], file[KK_PATH_MAX];
    CHECK(mkdtemp(dir) != NULL);
    snprintf(emote_dir, sizeof emote_dir, "%s/emotes", dir);
    snprintf(photo_dir, sizeof photo_dir, "%s/photos", dir);
    kk_emotes *em = kk_emotes_new(NULL, emote_dir, 32);
    kk_photos *p = kk_photos_new(NULL, em, photo_dir);
    CHECK(em && p);

    /* YouTube brings the URL; the picture was downloaded in an earlier run. */
    const char *url = "https://yt.example.invalid/photo=s64";
    CHECK(kk_emotes_file(em, "youtube", url, file, sizeof file));
    kk_make_parent_dirs(file);
    uint8_t *data;
    size_t len = make_webp(&data);
    FILE *f = fopen(file, "wb");
    CHECK(f && fwrite(data, 1, len, f) == len);
    fclose(f);
    WebPFree(data);
    kk_emote_anim *a;
    CHECK(kk_photos_get(p, "youtube", "UCaaa", "Fulana", url, &a) == KK_EMOTE_READY);
    CHECK(a && a->n == 1 && cairo_image_surface_get_width(a->frames[0]) == 32);
    /* Later messages without the URL still find it. */
    CHECK(kk_photos_get(p, "youtube", "UCaaa", "Fulana", NULL, &a) == KK_EMOTE_READY);

    /* Nothing to ask with (no network) or no way to ask (the demo). */
    CHECK(kk_photos_get(p, "twitch", "123", "fulano", NULL, &a) == KK_EMOTE_FAILED);
    CHECK(kk_photos_get(p, "demo", "x", "Ana", NULL, &a) == KK_EMOTE_FAILED);
    CHECK(kk_photos_get(p, "youtube", "", "Ana", NULL, &a) == KK_EMOTE_FAILED);
    CHECK(kk_photos_get(p, "youtube", "UCbbb", "Bia", "http://no.tls/x", &a) ==
          KK_EMOTE_FAILED);

    /* In the chat: one with a picture, one with the initial. */
    kk_chatview *v = kk_chatview_new(em, p);
    kk_chatview_config c = {.size = 12, .text_color = 0xffffff, .show_platform = true,
                            .show_photo = true, .history = 10};
    kk_chatview_configure(v, &c);
    kk_chatview_resize(v, 300, 200);
    kk_chat_msg m = {.platform = "youtube", .user_id = "UCaaa", .name = "Fulana",
                     .photo = url, .text = "oi"};
    CHECK(kk_chatview_add(v, &m, false, 1));
    kk_chat_msg n = {.platform = "odysee", .user_id = "abc", .name = "@ção", .text = "olá"};
    uint64_t id = kk_chatview_add(v, &n, false, 1);
    cairo_surface_t *s = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 300, 200);
    cairo_t *cr = cairo_create(s);
    kk_chatview_draw(v, cr, 1);
    CHECK_INT_EQ(kk_chatview_drawn(v), 2);
    CHECK(!kk_chatview_dirty(v, 1.5)); /* nothing loading */
    kk_chat_msg back;
    CHECK(kk_chatview_get(v, id, &back) && back.photo == NULL);
    cairo_destroy(cr);
    cairo_surface_destroy(s);
    kk_chatview_free(v);

    kk_photos_free(p);
    kk_emotes_free(em);
    char cmd[128];
    snprintf(cmd, sizeof cmd, "rm -rf '%s'", dir);
    CHECK(system(cmd) == 0);
}

int main(void)
{
    RUN(answers);
    RUN(webp);
    RUN(pictures_on_disk);
    return harness_report();
}
