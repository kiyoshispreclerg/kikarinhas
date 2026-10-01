/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef KK_SOUNDBOARD_H
#define KK_SOUNDBOARD_H

#include <stdbool.h>

#include "audio.h"
#include "config.h"

/* The sound board: the [sound.NAME] list of the config, played through a
 * kk_audio. Each file is decoded the first time it plays and kept, so a
 * sound costs a little memory but no disk or CPU the next times. */

typedef struct kk_soundboard kk_soundboard;

kk_soundboard *kk_soundboard_new(void);
void kk_soundboard_free(kk_soundboard *sb);

/* Takes the sounds, master volume and device of cfg. Stops what is playing
 * and forgets the decoded files (they may have changed). */
void kk_soundboard_configure(kk_soundboard *sb, const kk_config *cfg);

/* word: a sound's name or alias. False if unknown, disabled or unplayable. */
bool kk_soundboard_play(kk_soundboard *sb, const char *word, double now);
void kk_soundboard_stop(kk_soundboard *sb);

/* For the main loop: see kk_audio_pollfds / kk_audio_pump. */
int kk_soundboard_pollfds(kk_soundboard *sb, struct pollfd *fds, int max);
void kk_soundboard_pump(kk_soundboard *sb, struct pollfd *fds, int n, double now);

#endif
