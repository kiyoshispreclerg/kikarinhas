/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef KK_ACTIONS_H
#define KK_ACTIONS_H

#include "commands.h"
#include "config.h"
#include "stage.h"

/* The built-in chat commands and what they do to the avatars. */

/* Asked to play a sound from the sound board; false if there is no such
 * sound (then no cooldown starts). */
typedef bool (*kk_sound_fn)(void *ud, const kk_chat_msg *msg, const char *sound);

typedef struct {
    kk_stage *stage;
    kk_sound_fn sound;
    void *sound_ud;
    kk_avatar *self; /* the sender's avatar, set before each dispatch */
    bool help_bubbles; /* set by kk_actions_register from the config */
    int help_count;
    double help_seconds; /* -1: by the number of lines */
} kk_actions;

/* Handler for an "action =" name of the config, or NULL. */
kk_cmd_fn kk_actions_find(const char *action);

/* Registers the enabled commands of cfg (with their aliases, cooldowns and
 * roles), one command per sound (sharing !sound's cooldowns) when
 * [soundboard] commands is on, and the "!name" shortcut into c, which must have been created with
 * a kk_actions as its ud. Clashing names and aliases are reported to warn
 * and skipped; returns how many. */
int kk_actions_register(kk_commands *c, const kk_config *cfg,
                        kk_config_warn_fn warn, void *ud);

#endif
