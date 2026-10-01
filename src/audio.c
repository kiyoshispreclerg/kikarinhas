/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "audio.h"

#include <errno.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include <alsa/asoundlib.h>

#include "log.h"

#define LATENCY_US 120000
#define IDLE_CLOSE_S 3.0
#define CHUNK 1024

typedef struct {
    const kk_sample *s;
    size_t pos;
    float gain;
    uint64_t serial; /* start order, to replace the oldest */
} voice;

struct kk_audio {
    char *device;
    snd_pcm_t *pcm;
    voice *voices;
    int max_voices;
    uint64_t serial;
    float master;
    double idle_since; /* < 0 while something plays */
    double retry_at;   /* after a failed open, don't retry on every sound */
    int16_t buf[CHUNK * 2];
};

kk_audio *kk_audio_new(const char *device, int max_voices)
{
    kk_audio *a = calloc(1, sizeof *a);
    if (!a)
        return NULL;
    a->max_voices = max_voices < 1 ? 1 : max_voices;
    a->voices = calloc((size_t)a->max_voices, sizeof *a->voices);
    a->device = strdup(device && device[0] ? device : "default");
    if (!a->voices || !a->device) {
        kk_audio_free(a);
        return NULL;
    }
    a->master = 1.0f;
    a->idle_since = -1.0;
    return a;
}

static void close_device(kk_audio *a)
{
    if (a->pcm) {
        snd_pcm_close(a->pcm);
        a->pcm = NULL;
    }
}

void kk_audio_free(kk_audio *a)
{
    if (!a)
        return;
    close_device(a);
    free(a->voices);
    free(a->device);
    free(a);
}

void kk_audio_set_volume(kk_audio *a, double gain)
{
    a->master = (float)(gain < 0 ? 0 : gain);
}

static bool open_device(kk_audio *a, double now)
{
    if (a->pcm)
        return true;
    if (now < a->retry_at)
        return false;
    int err = snd_pcm_open(&a->pcm, a->device, SND_PCM_STREAM_PLAYBACK,
                           SND_PCM_NONBLOCK);
    if (err >= 0)
        err = snd_pcm_set_params(a->pcm, SND_PCM_FORMAT_S16_LE,
                                 SND_PCM_ACCESS_RW_INTERLEAVED, 2,
                                 KK_SAMPLE_RATE, 1, LATENCY_US);
    if (err < 0) {
        kk_log_warn("não consegui abrir o áudio \"%s\": %s", a->device,
                    snd_strerror(err));
        close_device(a);
        a->retry_at = now + 10.0;
        return false;
    }
    return true;
}

int kk_audio_play(kk_audio *a, const kk_sample *s, double gain, double now)
{
    if (!s->frames || !open_device(a, now))
        return -1;
    int slot = 0;
    for (int i = 0; i < a->max_voices; i++) {
        if (!a->voices[i].s) {
            slot = i;
            break;
        }
        if (a->voices[i].serial < a->voices[slot].serial)
            slot = i;
    }
    a->voices[slot] = (voice){.s = s, .gain = (float)gain, .serial = ++a->serial};
    a->idle_since = -1.0;
    return 0;
}

void kk_audio_stop_all(kk_audio *a)
{
    memset(a->voices, 0, (size_t)a->max_voices * sizeof *a->voices);
}

int kk_audio_playing(const kk_audio *a)
{
    int n = 0;
    for (int i = 0; i < a->max_voices; i++)
        n += a->voices[i].s != NULL;
    return n;
}

void kk_audio_mix(kk_audio *a, int16_t *out, size_t frames)
{
    static int32_t acc[CHUNK * 2];
    while (frames) {
        size_t n = frames < CHUNK ? frames : CHUNK;
        memset(acc, 0, n * 2 * sizeof *acc);
        for (int v = 0; v < a->max_voices; v++) {
            voice *vo = &a->voices[v];
            if (!vo->s)
                continue;
            /* Fixed point: gain in 1/4096 steps. */
            int32_t g = (int32_t)lrintf(vo->gain * a->master * 4096.0f);
            size_t left = vo->s->frames - vo->pos;
            size_t m = left < n ? left : n;
            const int16_t *src = vo->s->pcm + vo->pos * 2;
            for (size_t i = 0; i < m * 2; i++)
                acc[i] += (int32_t)(((int64_t)src[i] * g) >> 12);
            vo->pos += m;
            if (vo->pos >= vo->s->frames)
                vo->s = NULL;
        }
        for (size_t i = 0; i < n * 2; i++)
            out[i] = (int16_t)(acc[i] > 32767 ? 32767 : acc[i] < -32768 ? -32768 : acc[i]);
        out += n * 2;
        frames -= n;
    }
}

bool kk_audio_active(const kk_audio *a)
{
    return a->pcm != NULL;
}

int kk_audio_pollfds(kk_audio *a, struct pollfd *fds, int max)
{
    if (!a->pcm || max <= 0)
        return 0;
    int n = snd_pcm_poll_descriptors(a->pcm, fds, (unsigned)max);
    return n < 0 ? 0 : n;
}

void kk_audio_pump(kk_audio *a, struct pollfd *fds, int nfds, double now)
{
    if (!a->pcm)
        return;
    if (nfds > 0) {
        /* Lets the plugin clear its wake-up descriptors. */
        unsigned short revents;
        snd_pcm_poll_descriptors_revents(a->pcm, fds, (unsigned)nfds, &revents);
    }

    if (kk_audio_playing(a)) {
        a->idle_since = -1.0;
    } else if (a->idle_since < 0) {
        a->idle_since = now;
    } else if (now - a->idle_since > IDLE_CLOSE_S) {
        close_device(a);
        return;
    }

    for (int guard = 0; guard < 64; guard++) {
        snd_pcm_sframes_t avail = snd_pcm_avail_update(a->pcm);
        if (avail < 0) {
            if (snd_pcm_recover(a->pcm, (int)avail, 1) < 0) {
                kk_log_warn("áudio: %s", snd_strerror((int)avail));
                close_device(a);
                return;
            }
            continue;
        }
        if (avail == 0)
            return;
        size_t n = (size_t)avail < CHUNK ? (size_t)avail : CHUNK;
        kk_audio_mix(a, a->buf, n);
        snd_pcm_sframes_t w = snd_pcm_writei(a->pcm, a->buf, n);
        if (w == -EAGAIN)
            return;
        if (w < 0 && snd_pcm_recover(a->pcm, (int)w, 1) < 0) {
            close_device(a);
            return;
        }
    }
}
