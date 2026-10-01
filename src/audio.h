/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef KK_AUDIO_H
#define KK_AUDIO_H

#include <poll.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "sample.h"

/* A small mixer that plays kk_samples through ALSA without blocking (with
 * PipeWire or PulseAudio, through their ALSA plugin, so OBS hears it as
 * desktop audio). The device is opened on the first sound and closed after
 * a few idle seconds, so an idle kikarinhas holds no audio stream.
 *
 * The caller adds kk_audio_pollfds to its poll() and calls kk_audio_pump
 * after each wake-up; the ALSA buffer (~120 ms) is topped up there. */

typedef struct kk_audio kk_audio;

/* device: an ALSA name ("default", "pipewire", "hw:1"...); NULL = default.
 * Nothing is opened yet. */
kk_audio *kk_audio_new(const char *device, int max_voices);
void kk_audio_free(kk_audio *a);

/* Master gain (1.0 = as is). */
void kk_audio_set_volume(kk_audio *a, double gain);

/* Starts s at gain (1.0 = as is). s must stay alive while it plays: call
 * kk_audio_stop_all before freeing samples. With every voice busy, the
 * oldest one is replaced. Returns -1 if the device can't be opened. */
int kk_audio_play(kk_audio *a, const kk_sample *s, double gain, double now);
void kk_audio_stop_all(kk_audio *a);
int kk_audio_playing(const kk_audio *a);
/* True while the device is open (playing, or idle but not closed yet). */
bool kk_audio_active(const kk_audio *a);

int kk_audio_pollfds(kk_audio *a, struct pollfd *fds, int max);
/* Writes what the device has room for; closes it when idle long enough.
 * fds/nfds: what kk_audio_pollfds filled, after the poll (or NULL, 0 when
 * pumping from a timer). */
void kk_audio_pump(kk_audio *a, struct pollfd *fds, int nfds, double now);

/* The mixer itself, without a device: frames of stereo 16-bit into out.
 * Advances the voices. Used by kk_audio_pump and by the tests. */
void kk_audio_mix(kk_audio *a, int16_t *out, size_t frames);

#endif
