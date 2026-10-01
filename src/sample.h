/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef KK_SAMPLE_H
#define KK_SAMPLE_H

#include <stddef.h>
#include <stdint.h>

/* A sound file decoded whole into memory, already in the mixer's format
 * (interleaved 16-bit stereo at KK_SAMPLE_RATE), so playing it is only a
 * copy with a gain. Sound-board clips are short; files longer than
 * KK_SAMPLE_MAX_SECONDS are cut.
 *
 * WAV (and AIFF/W64), Ogg Vorbis and MP3 are recognised by their first
 * bytes, not by the extension. The decoders are vendored (dr_wav, dr_mp3,
 * stb_vorbis), so there is no library to install. */

#define KK_SAMPLE_RATE 48000
#define KK_SAMPLE_MAX_SECONDS 60

typedef struct {
    int16_t *pcm; /* frames * 2 values */
    size_t frames;
    float peak;     /* highest absolute sample, 0..1 */
    float loudness; /* RMS of the non-silent parts, 0..1 */
} kk_sample;

/* 0 on success; on failure, err says why (in Portuguese, for people). */
int kk_sample_load(kk_sample *s, const char *path, char *err, size_t err_size);
/* From samples already in memory (any rate, 1 or more channels). Takes a
 * copy. Used by the loader and the tests. */
int kk_sample_from_pcm(kk_sample *s, const int16_t *pcm, size_t frames,
                       int channels, int rate);
void kk_sample_free(kk_sample *s);

double kk_sample_seconds(const kk_sample *s);

/* The volume, in percent, that brings the sample to a common loudness
 * (-18 dBFS RMS over its non-silent parts) without clipping: what
 * "Nivelar" sets, from 5 to 400. 100 when it can't tell. */
int kk_sample_level(const kk_sample *s);

#endif
