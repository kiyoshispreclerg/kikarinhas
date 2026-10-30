/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "image.h"

#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <webp/decode.h>
#include <webp/demux.h>

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

static bool is_webp(const unsigned char *d, size_t len)
{
    return len >= 12 && memcmp(d, "RIFF", 4) == 0 && memcmp(d + 8, "WEBP", 4) == 0;
}

/* WebP (Kick's and Odysee's profile pictures, some emotes), still or
 * animated, in the same shape kk_decode_image gives: every frame's RGBA one
 * after the other, and their delays. Free both with free(). */
static unsigned char *decode_webp(const unsigned char *data, size_t len, int *w, int *h,
                                  int *frames, int **delays)
{
    *frames = 0;
    *delays = NULL;
    WebPAnimDecoderOptions opt;
    if (!WebPAnimDecoderOptionsInit(&opt))
        return NULL;
    opt.color_mode = MODE_RGBA;
    opt.use_threads = 0;
    WebPData wd = {data, len};
    WebPAnimDecoder *dec = WebPAnimDecoderNew(&wd, &opt);
    WebPAnimInfo info;
    if (!dec || !WebPAnimDecoderGetInfo(dec, &info) || info.frame_count < 1 ||
        info.canvas_width < 1 || info.canvas_height < 1 ||
        (long)info.canvas_width * info.canvas_height * info.frame_count > MAX_PIXELS) {
        WebPAnimDecoderDelete(dec);
        return NULL;
    }
    int n = (int)info.frame_count;
    size_t frame = (size_t)info.canvas_width * info.canvas_height * 4;
    unsigned char *px = malloc(frame * (size_t)n);
    int *d = n > 1 ? malloc((size_t)n * sizeof *d) : NULL;
    int got = 0, prev_ms = 0;
    while (px && (n == 1 || d) && got < n && WebPAnimDecoderHasMoreFrames(dec)) {
        uint8_t *buf;
        int ts; /* when the frame ends, ms from the start */
        if (!WebPAnimDecoderGetNext(dec, &buf, &ts))
            break;
        memcpy(px + frame * (size_t)got, buf, frame);
        if (d)
            d[got] = ts - prev_ms;
        prev_ms = ts;
        got++;
    }
    WebPAnimDecoderDelete(dec);
    if (got < 1) {
        free(px);
        free(d);
        return NULL;
    }
    *w = (int)info.canvas_width;
    *h = (int)info.canvas_height;
    *frames = got;
    *delays = d;
    return px;
}

/* Any picture we read, as kk_decode_image gives it; release with done(). */
static unsigned char *decode_any(const void *data, size_t len, int *w, int *h, int *n,
                                 int **delays, void (**done)(void *))
{
    if (is_webp(data, len)) {
        *done = free;
        return decode_webp(data, len, w, h, n, delays);
    }
    *done = kk_decode_image_free;
    return kk_decode_image(data, len, w, h, n, delays);
}

/* Frames of a picture in memory into img. what names it in warnings. */
static bool decode_into(kk_image *img, const void *data, size_t len, const char *what)
{
    memset(img, 0, sizeof *img);
    int w, h, n, *delays;
    void (*done)(void *);
    unsigned char *px = decode_any(data, len, &w, &h, &n, &delays, &done);
    if (!px || n < 1) {
        kk_log_warn("%s: não é uma imagem PNG, JPEG, GIF, BMP ou WebP que eu consiga ler", what);
        done(px);
        done(delays);
        return false;
    }
    if ((long)w * h * n > MAX_PIXELS) {
        kk_log_warn("%s: grande demais (%dx%d, %d quadros)", what, w, h, n);
        done(px);
        done(delays);
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
    done(px);
    done(delays);
    img->w = w;
    img->h = h;
    if (!ok) {
        kk_log_warn("%s: sem memória para a imagem", what);
        kk_image_free(img);
    }
    return ok;
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
    bool ok = decode_into(img, data, len, path);
    free(data);
    return ok;
}

bool kk_image_decode(kk_image *img, const void *data, size_t len)
{
    return decode_into(img, data, len, "imagem");
}

cairo_surface_t *kk_image_decode_still(const void *data, size_t len)
{
    int w, h, n, *delays;
    void (*done)(void *);
    unsigned char *px = decode_any(data, len, &w, &h, &n, &delays, &done);
    cairo_surface_t *s = NULL;
    if (px && n >= 1 && (long)w * h * n <= MAX_PIXELS)
        s = to_surface(px, w, h);
    done(px);
    done(delays);
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
