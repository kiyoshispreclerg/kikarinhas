/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef KK_ACTIONS_H
#define KK_ACTIONS_H

#include "commands.h"
#include "config.h"
#include "games.h"
#include "mpris.h"
#include "overlay.h"
#include "points.h"
#include "props.h"
#include "stage.h"

/* The built-in chat commands and what they do to the avatars. */

/* Asked to play a sound from the sound board; false if there is no such
 * sound (then no cooldown starts). */
typedef bool (*kk_sound_fn)(void *ud, const kk_chat_msg *msg, const char *sound);

/* A roulette round: bets gather until ends_at, then one spin settles them
 * all. Bets are taken from the balance when placed. */
#define KK_MAX_BETS 64

typedef struct {
    char key[96]; /* "platform:id" */
    char name[64];
    long amount;
    kk_bet bet;
} kk_placed_bet;

typedef struct {
    kk_placed_bet bets[KK_MAX_BETS];
    int n;
    double ends_at; /* 0: no round open */
} kk_roulette_round;

typedef struct {
    kk_stage *stage;
    kk_sound_fn sound;
    void *sound_ud;
    kk_avatar *self; /* the sender's avatar, set before each dispatch */
    kk_overlay *overlay; /* where action = image shows pictures; may be NULL */
    kk_mpris *mpris;     /* the desktop's players; NULL without a session bus */
    kk_points *points;   /* balances; NULL without a people file */
    char points_name[32]; /* set by kk_actions_register: "pontos" */
    bool help_bubbles; /* set by kk_actions_register from the config */
    int help_count;
    double help_seconds; /* -1: by the number of lines */
    kk_props *props;     /* bombs and collectibles; may be NULL */
    /* Set by kk_actions_register from [points]. */
    long catch_points;   /* each collectible (!spawn) */
    long min_bet, max_bet; /* !slots and !bet */
    double roulette_seconds; /* how long a round takes bets */
    double now;          /* monotonic seconds, from kk_actions_tick */
    kk_roulette_round roulette;
} kk_actions;

/* The [points] part of cfg, for kk_points_configure. */
kk_points_config kk_actions_points_config(const kk_config *cfg);

/* Time passing (now: monotonic seconds): spins the roulette when its round
 * is over. Call it every loop turn. */
void kk_actions_tick(kk_actions *a, double now);
/* Gives back the bets of an unfinished round (the program is closing). */
void kk_actions_refund(kk_actions *a);
/* kk_catch_fn for the props: points for the catcher, when it is someone. */
long kk_actions_catch(void *ud, kk_avatar *a, long points);

/* Handler for an "action =" name of the config, or NULL. */
kk_cmd_fn kk_actions_find(const char *action);

/* Registers the enabled commands of cfg (with their aliases, cooldowns and
 * roles), one command per sound (sharing !sound's cooldowns) when
 * [soundboard] commands is on, and the "!name" shortcut into c, which must have been created with
 * a kk_actions as its ud. Clashing names and aliases are reported to warn
 * and skipped; returns how many. Also configures the ud's points (if any)
 * and gives c the wallet when points are on. */
int kk_actions_register(kk_commands *c, const kk_config *cfg,
                        kk_config_warn_fn warn, void *ud);

#endif
