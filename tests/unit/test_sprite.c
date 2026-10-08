/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Sheets: dithered pictures are blended, real pixel art is left alone, and
 * fractional scales are pre-scaled. */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "../harness.h"
#include "sprite.h"

static char path[64];

/* A 32x32 sheet of one cell: a checkerboard of two close colours over the
 * middle, in `shades` variants by 4x4 block (more shades = more colours,
 * like a GIF). */
static void write_sheet(int shades)
{
    snprintf(path, sizeof path, "/tmp/kk_sprite_XXXXXX");
    int fd = mkstemp(path);
    if (fd >= 0)
        close(fd);
    cairo_surface_t *s = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 32, 32);
    cairo_surface_flush(s);
    uint32_t *px = (uint32_t *)(void *)cairo_image_surface_get_data(s);
    int stride = cairo_image_surface_get_stride(s) / 4;
    for (int y = 4; y < 28; y++)
        for (int x = 4; x < 28; x++) {
            uint32_t shade = (uint32_t)((x / 4 + (y / 4) * 6) % shades) * 3;
            px[y * stride + x] = (x + y) % 2 ? 0xffd08080u + (shade << 8)
                                             : 0xffe0a0a0u + (shade << 8);
        }
    cairo_surface_mark_dirty(s);
    cairo_surface_write_to_png(s, path);
    cairo_surface_destroy(s);
}

TEST(sprite_dithered_picture_is_blended)
{
    write_sheet(12); /* 24 colours */
    kk_sheet sh;
    CHECK_INT_EQ(kk_sheet_load(&sh, path, 32, 32, 2.0, false, 0, 0, NULL), 0);
    bool blended = sh.dedithered;
    int store = sh.store_w;
    kk_sheet_free(&sh);
    unlink(path);
    CHECK(blended);
    CHECK_INT_EQ(store, 64); /* scaled smoothly, at screen size */
}

TEST(sprite_pixel_art_keeps_its_dithering)
{
    write_sheet(2); /* 4 colours: a short palette, dithering on purpose */
    kk_sheet sh;
    CHECK_INT_EQ(kk_sheet_load(&sh, path, 32, 32, 2.0, false, 0, 0, NULL), 0);
    bool blended = sh.dedithered;
    int store = sh.store_w;
    kk_sheet_free(&sh);
    CHECK(!blended);
    CHECK_INT_EQ(store, 32); /* kept 1x, enlarged with nearest when drawn */

    /* At 1.4x every pixel can't be the same size with nearest: pre-scaled. */
    CHECK_INT_EQ(kk_sheet_load(&sh, path, 32, 32, 1.4, false, 0, 0, NULL), 0);
    int frac = sh.store_w, cell = sh.cell_w;
    kk_sheet_free(&sh);
    unlink(path);
    CHECK_INT_EQ(cell, 45);
    CHECK_INT_EQ(frac, 45);
}

int main(void)
{
    RUN(sprite_dithered_picture_is_blended);
    RUN(sprite_pixel_art_keeps_its_dithering);
    return harness_report();
}
