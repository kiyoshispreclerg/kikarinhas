/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "sample.h"

#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "decode.h"

/* Bigger files are refused before decoding: clips are small, and a whole
 * album decoded into memory would not be. */
#define MAX_FILE_BYTES (24u << 20)

typedef enum { FMT_UNKNOWN, FMT_WAV, FMT_OGG, FMT_MP3 } format;

static format sniff(const char *path)
{
    unsigned char b[12] = {0};
    FILE *f = fopen(path, "rb");
    if (!f)
        return FMT_UNKNOWN;
    size_t n = fread(b, 1, sizeof b, f);
    fclose(f);
    if (n >= 4 && (memcmp(b, "RIFF", 4) == 0 || memcmp(b, "RF64", 4) == 0 ||
                   memcmp(b, "FORM", 4) == 0 || memcmp(b, "riff", 4) == 0))
        return FMT_WAV;
    if (n >= 4 && memcmp(b, "OggS", 4) == 0)
        return FMT_OGG;
    if (n >= 3 && memcmp(b, "ID3", 3) == 0)
        return FMT_MP3;
    if (n >= 2 && b[0] == 0xFF && (b[1] & 0xE0) == 0xE0)
        return FMT_MP3; /* a bare MPEG frame header */
    return FMT_UNKNOWN;
}

static void measure(kk_sample *s)
{
    /* Loudness over 50 ms blocks, ignoring near-silent ones (below -50
     * dBFS), so a long silent tail doesn't make a clip look quiet. */
    const size_t block = KK_SAMPLE_RATE / 20;
    const double gate = pow(10.0, -50.0 / 10.0); /* power */
    double sum = 0.0;
    size_t blocks = 0;
    int peak = 0;
    for (size_t start = 0; start < s->frames; start += block) {
        size_t end = start + block < s->frames ? start + block : s->frames;
        double power = 0.0;
        for (size_t i = start * 2; i < end * 2; i++) {
            int v = s->pcm[i];
            int a = v < 0 ? -v : v;
            if (a > peak)
                peak = a;
            double x = v / 32768.0;
            power += x * x;
        }
        power /= (double)((end - start) * 2);
        if (power > gate) {
            sum += power;
            blocks++;
        }
    }
    s->peak = (float)(peak / 32768.0);
    s->loudness = blocks ? (float)sqrt(sum / (double)blocks) : 0.0f;
}

int kk_sample_from_pcm(kk_sample *s, const int16_t *pcm, size_t frames,
                       int channels, int rate)
{
    memset(s, 0, sizeof *s);
    if (channels < 1 || rate < 1000 || rate > 384000)
        return -1;
    /* Linear resampling: plenty for sound effects. */
    double step = (double)rate / KK_SAMPLE_RATE;
    size_t out = (size_t)((double)frames / step);
    size_t max = (size_t)KK_SAMPLE_MAX_SECONDS * KK_SAMPLE_RATE;
    if (out > max)
        out = max;
    s->pcm = malloc((out ? out : 1) * 2 * sizeof *s->pcm);
    if (!s->pcm)
        return -1;
    for (size_t i = 0; i < out; i++) {
        double pos = (double)i * step;
        size_t a = (size_t)pos;
        size_t b = a + 1 < frames ? a + 1 : a;
        double t = pos - (double)a;
        for (int c = 0; c < 2; c++) {
            /* Mono goes to both sides; beyond stereo, the first two. */
            int src = channels == 1 ? 0 : c;
            double va = pcm[a * (size_t)channels + (size_t)src];
            double vb = pcm[b * (size_t)channels + (size_t)src];
            s->pcm[i * 2 + (size_t)c] = (int16_t)lrint(va + (vb - va) * t);
        }
    }
    s->frames = out;
    measure(s);
    return 0;
}

int kk_sample_load(kk_sample *s, const char *path, char *err, size_t err_size)
{
    memset(s, 0, sizeof *s);
    struct stat st;
    if (stat(path, &st) < 0) {
        snprintf(err, err_size, "%s", strerror(errno));
        return -1;
    }
    if (!S_ISREG(st.st_mode) || (unsigned long long)st.st_size > MAX_FILE_BYTES) {
        snprintf(err, err_size, "não é um arquivo de som pequeno (máximo %u MB)",
                 MAX_FILE_BYTES >> 20);
        return -1;
    }

    kk_decode_format fmt;
    switch (sniff(path)) {
    case FMT_WAV:
        fmt = KK_DECODE_WAV;
        break;
    case FMT_OGG:
        fmt = KK_DECODE_OGG;
        break;
    case FMT_MP3:
        fmt = KK_DECODE_MP3;
        break;
    default:
        snprintf(err, err_size, "formato desconhecido (use wav, ogg ou mp3)");
        return -1;
    }
    int channels, rate;
    uint64_t frames;
    int16_t *pcm = kk_decode_file(path, fmt, &channels, &rate, &frames);
    if (!pcm || frames == 0) {
        kk_decode_free(pcm, fmt);
        snprintf(err, err_size, "não consegui decodificar o arquivo");
        return -1;
    }
    int rc = kk_sample_from_pcm(s, pcm, (size_t)frames, channels, rate);
    kk_decode_free(pcm, fmt);
    if (rc < 0)
        snprintf(err, err_size, "formato de áudio sem suporte ou sem memória");
    return rc;
}

void kk_sample_free(kk_sample *s)
{
    free(s->pcm);
    memset(s, 0, sizeof *s);
}

double kk_sample_seconds(const kk_sample *s)
{
    return (double)s->frames / KK_SAMPLE_RATE;
}

int kk_sample_level(const kk_sample *s)
{
    /* -18 dBFS of RMS: clear over a game and a voice, with headroom. */
    const double target = 0.12589;
    if (s->loudness <= 0.0f || s->peak <= 0.0f)
        return 100;
    double gain = target / s->loudness;
    if (gain * s->peak > 1.0)
        gain = 1.0 / s->peak; /* never clip */
    long pct = lrint(gain * 100.0);
    return (int)(pct < 5 ? 5 : pct > 400 ? 400 : pct);
}
