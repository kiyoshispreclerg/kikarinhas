/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef KK_DECODE_H
#define KK_DECODE_H

#include <stdint.h>

/* Thin wrapper over the vendored decoders (vendor/decoders.c), so the rest
 * of the code never sees their headers. */

typedef enum { KK_DECODE_WAV, KK_DECODE_OGG, KK_DECODE_MP3 } kk_decode_format;

/* Whole file as interleaved 16-bit samples, or NULL. Free with
 * kk_decode_free and the same format. */
int16_t *kk_decode_file(const char *path, kk_decode_format fmt, int *channels,
                        int *rate, uint64_t *frames);
void kk_decode_free(int16_t *pcm, kk_decode_format fmt);

#endif
