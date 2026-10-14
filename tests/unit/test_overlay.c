/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Pictures from files and the layer that shows them (action = image). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../harness.h"
#include "config.h"
#include "image.h"
#include "ini.h"
#include "overlay.h"
#include "util.h"

static char png_path[64];

/* A 4x4 picture, half transparent red, written by Cairo. */
static void write_png(void)
{
    snprintf(png_path, sizeof png_path, "/tmp/kk_overlay_XXXXXX");
    int fd = mkstemp(png_path);
    if (fd >= 0)
        close(fd);
    cairo_surface_t *s = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 4, 4);
    cairo_t *cr = cairo_create(s);
    cairo_set_source_rgba(cr, 1, 0, 0, 0.5);
    cairo_paint(cr);
    cairo_destroy(cr);
    cairo_surface_write_to_png(s, png_path);
    cairo_surface_destroy(s);
}

static uint32_t pixel(cairo_surface_t *s)
{
    cairo_surface_flush(s);
    return *(uint32_t *)(void *)cairo_image_surface_get_data(s);
}

TEST(image_png_is_premultiplied)
{
    kk_image img;
    CHECK(kk_image_load(&img, png_path));
    int n = img.n, w = img.w;
    uint32_t p = pixel(img.frames[0]);
    kk_image_free(&img);
    CHECK_INT_EQ(n, 1);
    CHECK_INT_EQ(w, 4);
    /* alpha ~0x80, red premultiplied to ~0x80, no green or blue */
    CHECK((p >> 24) >= 0x7f && (p >> 24) <= 0x81);
    CHECK(((p >> 16) & 0xff) >= 0x7e && ((p >> 16) & 0xff) <= 0x81);
    CHECK_INT_EQ(p & 0xffff, 0);
}

TEST(image_gif_frames_and_delays)
{
    kk_image img;
    CHECK(kk_image_load(&img, KK_FIXTURES_DIR "/anim.gif"));
    int n = img.n, total = img.total_ms;
    int f0 = kk_image_frame_index(&img, 0.1), f1 = kk_image_frame_index(&img, 0.3);
    int loop = kk_image_frame_index(&img, 0.75);
    uint32_t blue = pixel(img.frames[1]);
    kk_image_free(&img);
    CHECK_INT_EQ(n, 2);
    CHECK_INT_EQ(total, 700);
    CHECK_INT_EQ(f0, 0);
    CHECK_INT_EQ(f1, 1);
    CHECK_INT_EQ(loop, 0);
    CHECK_INT_EQ(blue, 0xff0000ff);
}

TEST(image_jpeg_and_junk)
{
    kk_image img;
    bool jpeg = kk_image_load(&img, KK_FIXTURES_DIR "/small.jpg");
    uint32_t p = jpeg ? pixel(img.frames[0]) : 0;
    kk_image_free(&img);
    bool junk = kk_image_load(&img, KK_FIXTURES_DIR "/sa_small.json");
    bool missing = kk_image_load(&img, "/nonexistent/x.png");
    CHECK(jpeg);
    CHECK((p >> 24) == 0xff && ((p >> 8) & 0xff) > 0xf0);
    CHECK(!junk);
    CHECK(!missing);
}

typedef struct {
    kk_rect r[8];
    int n;
} rects;

static void collect(void *to, kk_rect r)
{
    rects *rs = to;
    if (!kk_rect_empty(r) && rs->n < 8)
        rs->r[rs->n++] = r;
}

static rects damage(kk_overlay *o)
{
    rects rs = {0};
    const kk_layer *l = kk_overlay_layer(o);
    l->damage(l->ud, collect, &rs);
    l->painted(l->ud);
    return rs;
}

/* Centred on (x%, y%), scaled; repainted only while something changes;
 * gone after its seconds. */
TEST(overlay_place_and_life)
{
    kk_overlay *o = kk_overlay_new();
    kk_overlay_resize(o, 1000, 500);
    kk_cmd_params p = {.seconds = 2, .x = 10, .y = 20, .scale = 200};
    bool bad = kk_overlay_show(o, "logo", "/nonexistent/x.png", &p);
    bool good = kk_overlay_show(o, "logo", png_path, &p);
    kk_overlay_update(o, 100.0);
    rects first = damage(o);
    kk_overlay_update(o, 100.5); /* faded in, still */
    damage(o);
    kk_overlay_update(o, 101.0);
    rects still = damage(o);
    int showing = kk_overlay_showing(o);
    kk_overlay_show(o, "logo", png_path, &p); /* again: one slot, time over */
    /* More files: the cache grows while the first picture is up. */
    kk_overlay_show(o, "gif", KK_FIXTURES_DIR "/anim.gif", &p);
    kk_overlay_show(o, "jpg", KK_FIXTURES_DIR "/small.jpg", &p);
    kk_overlay_update(o, 102.5);
    int again = kk_overlay_showing(o);
    kk_overlay_update(o, 105.0);
    rects gone = damage(o);
    int after = kk_overlay_showing(o);
    kk_overlay_free(o);
    CHECK(!bad);
    CHECK(good);
    CHECK_INT_EQ(first.n, 1);
    /* 4x4 at 200% = 8x8 around (100, 100), plus a pixel of margin */
    CHECK_INT_EQ(first.r[0].x, 96);
    CHECK_INT_EQ(first.r[0].y, 96);
    CHECK_INT_EQ(first.r[0].w, 10);
    CHECK_INT_EQ(still.n, 0);
    CHECK_INT_EQ(showing, 1);
    CHECK_INT_EQ(again, 3);
    CHECK_INT_EQ(after, 0);
    CHECK_INT_EQ(gone.n, 1);
}

/* Scenery stays, never fades, and a command's pictures can't push it out. */
TEST(overlay_pinned_stays)
{
    kk_overlay *o = kk_overlay_new();
    kk_overlay_resize(o, 1000, 500);
    kk_cmd_params p = {.seconds = 1, .x = 50, .y = 50, .scale = 100};
    bool ok = kk_overlay_pin(o, "scenery.bg", png_path, &p);
    kk_overlay_update(o, 10.0);
    rects first = damage(o);
    kk_overlay_update(o, 10.05); /* a fade would be repainting by now */
    rects still = damage(o);
    for (int i = 0; i < 60; i++) {
        char key[16];
        snprintf(key, sizeof key, "cmd%d", i);
        kk_overlay_show(o, key, png_path, &p);
    }
    kk_overlay_update(o, 500.0); /* the pictures start... */
    kk_overlay_update(o, 503.0); /* ...and end; the scenery doesn't */
    int left = kk_overlay_showing(o);
    kk_overlay_clear(o);
    int cleared = kk_overlay_showing(o);
    kk_overlay_free(o);
    CHECK(ok);
    CHECK_INT_EQ(first.n, 1);
    CHECK_INT_EQ(still.n, 0);
    CHECK_INT_EQ(left, 1);
    CHECK_INT_EQ(cleared, 0);
}

static int warnings;

static void count(void *ud, int line, const char *msg)
{
    (void)ud;
    (void)line;
    (void)msg;
    warnings++;
}

TEST(config_scenery)
{
    kk_config c;
    kk_config_defaults(&c);
    kk_ini *ini = kk_ini_parse("[scenery.Grama]\nfile = /x/grama.png\ny = 90\nscale = 200\n"
                               "[scenery.moldura]\nfile = /x/m.png\nlayer = front\nenabled = no\n"
                               "[scenery.sem]\nx = 10\n"
                               "[scenery.ruim]\nfile = /x/r.png\nlayer = meio\n",
                               NULL, NULL);
    kk_config_apply(&c, ini, count, NULL);
    kk_ini_free(ini);
    kk_config copy;
    bool copied = kk_config_copy(&copy, &c);
    int n = c.n_scenery;
    CHECK_INT_EQ(warnings, 2); /* no file; bad layer */
    CHECK_INT_EQ(n, 3);
    CHECK_STR_EQ(c.scenery[0].name, "grama");
    CHECK_STR_EQ(c.scenery[0].file, "/x/grama.png");
    CHECK(c.scenery[0].y == 90 && c.scenery[0].x == 50 && c.scenery[0].scale == 200);
    CHECK(!c.scenery[0].front);
    CHECK(c.scenery[1].front && !c.scenery[1].enabled);
    CHECK(copied);
    CHECK_STR_EQ(copy.scenery[0].file, "/x/grama.png");
    CHECK(copy.scenery[1].front);
    kk_config_free(&copy);
    kk_config_free(&c);
}

int main(void)
{
    write_png();
    RUN(image_png_is_premultiplied);
    RUN(image_gif_frames_and_delays);
    RUN(image_jpeg_and_junk);
    RUN(overlay_place_and_life);
    RUN(overlay_pinned_stays);
    RUN(config_scenery);
    unlink(png_path);
    return harness_report();
}
