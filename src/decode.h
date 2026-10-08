/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef KK_DECODE_H
#define KK_DECODE_H

#include <stddef.h>
#include <stdint.h>

/* Thin wrapper over the vendored decoders (vendor/decoders.c), so the rest
 * of the code never sees their headers. */

typedef enum { KK_DECODE_WAV, KK_DECODE_OGG, KK_DECODE_MP3 } kk_decode_format;

/* Whole file as interleaved 16-bit samples, or NULL. Free with
 * kk_decode_free and the same format. */
int16_t *kk_decode_file(const char *path, kk_decode_format fmt, int *channels,
                        int *rate, uint64_t *frames);
void kk_decode_free(int16_t *pcm, kk_decode_format fmt);

/* An image file's bytes (PNG, JPEG, GIF, BMP) as 8-bit RGBA, w x h, with
 * *frames frames one after another (several only for animated GIFs, whose
 * per-frame delays in ms go to *delays, else NULL). NULL if it can't be
 * read. Free both with kk_decode_image_free. */
unsigned char *kk_decode_image(const unsigned char *data, size_t len, int *w, int *h,
                               int *frames, int **delays);
void kk_decode_image_free(void *p);

#endif
