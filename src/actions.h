/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef KK_ACTIONS_H
#define KK_ACTIONS_H

#include "commands.h"
#include "stage.h"

/* The built-in chat commands and what they do to the avatars. */

/* Asked to play a sound from the sound board (phase 7). */
typedef void (*kk_sound_fn)(void *ud, const kk_chat_msg *msg, const char *sound);

typedef struct {
    kk_stage *stage;
    kk_sound_fn sound;
    void *sound_ud;
    kk_avatar *self; /* the sender's avatar, set before each dispatch */
} kk_actions;

/* Registers the default commands, aliases and cooldowns into c, which must
 * have been created with a kk_actions as its ud. */
void kk_actions_register(kk_commands *c);

#endif
