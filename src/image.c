/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "image.h"

#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "decode.h"
#include "log.h"
#include "util.h"

/* Decoded pixels of all frames together, at most this many (4 bytes each). */
#define MAX_PIXELS (64L * 1024 * 1024)
/* GIFs often say 0 or 10 ms meaning "fast"; browsers use 100 ms then. */
#define MIN_DELAY_MS 20

/* stb gives straight RGBA bytes; Cairo wants premultiplied native-endian
 * ARGB words. */
static cairo_surface_t *to_surface(const unsigned char *rgba, int w, int h)
{
    cairo_surface_t *s = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
    if (cairo_surface_status(s) != CAIRO_STATUS_SUCCESS) {
        cairo_surface_destroy(s);
        return NULL;
    }
    cairo_surface_flush(s);
    unsigned char *dst = cairo_image_surface_get_data(s);
    int stride = cairo_image_surface_get_stride(s);
    for (int y = 0; y < h; y++) {
        uint32_t *row = (uint32_t *)(void *)(dst + (size_t)y * (size_t)stride);
        const unsigned char *p = rgba + (size_t)y * (size_t)w * 4;
        for (int x = 0; x < w; x++, p += 4) {
            uint32_t a = p[3];
            uint32_t r = (p[0] * a + 127) / 255, g = (p[1] * a + 127) / 255,
                     b = (p[2] * a + 127) / 255;
            row[x] = a << 24 | r << 16 | g << 8 | b;
        }
    }
    cairo_surface_mark_dirty(s);
    return s;
}

bool kk_image_load(kk_image *img, const char *path)
{
    memset(img, 0, sizeof *img);
    size_t len;
    char *data = kk_read_file(path, &len);
    if (!data) {
        kk_log_warn("não consegui ler a imagem %s: %s", path, strerror(errno));
        return false;
    }
    int w, h, n, *delays;
    unsigned char *px = kk_decode_image((const unsigned char *)data, len, &w, &h, &n, &delays);
    free(data);
    if (!px || n < 1) {
        kk_log_warn("%s: não é uma imagem PNG, JPEG, GIF ou BMP que eu consiga ler", path);
        kk_decode_image_free(px);
        kk_decode_image_free(delays);
        return false;
    }
    if ((long)w * h * n > MAX_PIXELS) {
        kk_log_warn("%s: grande demais (%dx%d, %d quadros)", path, w, h, n);
        kk_decode_image_free(px);
        kk_decode_image_free(delays);
        return false;
    }
    img->frames = calloc((size_t)n, sizeof *img->frames);
    img->delay_ms = n > 1 ? calloc((size_t)n, sizeof *img->delay_ms) : NULL;
    bool ok = img->frames && (n == 1 || img->delay_ms);
    for (int i = 0; ok && i < n; i++) {
        img->frames[i] = to_surface(px + (size_t)i * (size_t)w * (size_t)h * 4, w, h);
        ok = img->frames[i] != NULL;
        img->n++;
        if (img->delay_ms) {
            int d = delays ? delays[i] : 100;
            img->delay_ms[i] = d < MIN_DELAY_MS ? 100 : d;
            img->total_ms += img->delay_ms[i];
        }
    }
    kk_decode_image_free(px);
    kk_decode_image_free(delays);
    img->w = w;
    img->h = h;
    if (!ok) {
        kk_log_warn("%s: sem memória para a imagem", path);
        kk_image_free(img);
    }
    return ok;
}

cairo_surface_t *kk_image_decode_still(const void *data, size_t len)
{
    int w, h, n, *delays;
    unsigned char *px = kk_decode_image(data, len, &w, &h, &n, &delays);
    cairo_surface_t *s = NULL;
    if (px && n >= 1 && (long)w * h * n <= MAX_PIXELS)
        s = to_surface(px, w, h);
    kk_decode_image_free(px);
    kk_decode_image_free(delays);
    return s;
}

void kk_image_free(kk_image *img)
{
    for (int i = 0; i < img->n; i++)
        if (img->frames[i])
            cairo_surface_destroy(img->frames[i]);
    free(img->frames);
    free(img->delay_ms);
    memset(img, 0, sizeof *img);
}

int kk_image_frame_index(const kk_image *img, double t)
{
    if (img->n <= 1 || img->total_ms <= 0 || t <= 0)
        return 0;
    long ms = (long)(t * 1000.0) % img->total_ms;
    for (int i = 0; i < img->n; i++) {
        ms -= img->delay_ms[i];
        if (ms < 0)
            return i;
    }
    return img->n - 1;
}

cairo_surface_t *kk_image_frame(const kk_image *img, double t)
{
    return img->n ? img->frames[kk_image_frame_index(img, t)] : NULL;
}
