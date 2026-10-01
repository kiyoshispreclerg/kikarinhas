/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "soundboard.h"

#include <stdlib.h>
#include <string.h>

#include "log.h"

typedef struct {
    kk_sample sample;
    signed char state; /* 0 not tried, 1 loaded, -1 failed */
} clip;

struct kk_soundboard {
    kk_config cfg; /* only the sound fields are used */
    clip *clips;   /* one per cfg.sounds */
    kk_audio *audio;
    char *device;
    int voices;
};

kk_soundboard *kk_soundboard_new(void)
{
    kk_soundboard *sb = calloc(1, sizeof *sb);
    if (sb)
        kk_config_defaults(&sb->cfg);
    return sb;
}

static void drop_clips(kk_soundboard *sb)
{
    if (sb->audio)
        kk_audio_stop_all(sb->audio);
    for (int i = 0; sb->clips && i < sb->cfg.n_sounds; i++)
        kk_sample_free(&sb->clips[i].sample);
    free(sb->clips);
    sb->clips = NULL;
}

void kk_soundboard_free(kk_soundboard *sb)
{
    if (!sb)
        return;
    drop_clips(sb);
    kk_audio_free(sb->audio);
    kk_config_free(&sb->cfg);
    free(sb->device);
    free(sb);
}

void kk_soundboard_configure(kk_soundboard *sb, const kk_config *cfg)
{
    drop_clips(sb);
    kk_config_free(&sb->cfg);
    if (!kk_config_copy(&sb->cfg, cfg)) {
        kk_config_free(&sb->cfg);
        kk_config_defaults(&sb->cfg);
        sb->cfg.sound_enabled = false;
    }
    sb->clips = calloc((size_t)(sb->cfg.n_sounds ? sb->cfg.n_sounds : 1),
                       sizeof *sb->clips);

    const char *dev = cfg->sound_device ? cfg->sound_device : "default";
    if (!sb->audio || !sb->device || strcmp(sb->device, dev) != 0 ||
        sb->voices != cfg->sound_voices) {
        kk_audio_free(sb->audio);
        sb->audio = kk_audio_new(dev, cfg->sound_voices);
        free(sb->device);
        sb->device = strdup(dev);
        sb->voices = cfg->sound_voices;
    }
    if (sb->audio)
        kk_audio_set_volume(sb->audio, cfg->sound_volume / 100.0);
}

bool kk_soundboard_play(kk_soundboard *sb, const char *word, double now)
{
    if (!sb->cfg.sound_enabled || !sb->audio || !sb->clips)
        return false;
    const kk_config_sound *s = kk_config_find_sound(&sb->cfg, word);
    if (!s)
        return false;
    clip *c = &sb->clips[s - sb->cfg.sounds];
    if (c->state == 0) {
        char err[256];
        if (kk_sample_load(&c->sample, s->file, err, sizeof err) == 0) {
            c->state = 1;
        } else {
            kk_log_warn("som \"%s\" (%s): %s", s->name, s->file, err);
            c->state = -1;
        }
    }
    return c->state == 1 &&
           kk_audio_play(sb->audio, &c->sample, s->volume / 100.0, now) == 0;
}

void kk_soundboard_stop(kk_soundboard *sb)
{
    if (sb->audio)
        kk_audio_stop_all(sb->audio);
}

int kk_soundboard_pollfds(kk_soundboard *sb, struct pollfd *fds, int max)
{
    return sb->audio ? kk_audio_pollfds(sb->audio, fds, max) : 0;
}

void kk_soundboard_pump(kk_soundboard *sb, struct pollfd *fds, int n, double now)
{
    if (sb->audio)
        kk_audio_pump(sb->audio, fds, n, now);
}
