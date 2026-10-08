/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef KK_POINTS_H
#define KK_POINTS_H

/* Viewers' points: earned by taking part, spent on commands.
 *
 * Every way of earning, each set in [points] (0 turns it off):
 *  - message: per chat message, at most once every message_cooldown s;
 *  - watch: every watch_minutes while the viewer's avatar is on screen
 *    (they talked within the despawn time);
 *  - arrival: the first message of each session (each run of kikarinhas);
 *  - paid: a Super Chat, Super Sticker, bits...;
 *  - member: a new member or subscriber (also renewals and gifts);
 *  - start: what someone who was never seen begins with.
 * Spending: each command's cost (and each sound's, for !sound NAME), taken
 * only when the command did something; !give moves points between people
 * and !award (mods) creates them. The channel owner never pays.
 *
 * Balances live in the people file (points=), so they last. */

#include <stdbool.h>

#include "chat.h"
#include "users.h"

typedef struct {
    bool enabled;
    long start;
    long message;
    double message_cooldown; /* seconds */
    long watch;
    double watch_minutes;
    long arrival;
    long paid;
    long member;
} kk_points_config;

typedef struct kk_points kk_points;

/* users must outlive it. */
kk_points *kk_points_new(kk_users *users);
void kk_points_free(kk_points *p);
void kk_points_configure(kk_points *p, const kk_points_config *cfg);
bool kk_points_enabled(const kk_points *p);

/* Balance of key ("platform:id"); start for someone never seen. */
long kk_points_get(const kk_points *p, const char *key);
/* Adds delta (negative takes away; never below 0). Returns the balance. */
long kk_points_add(kk_points *p, const char *key, long delta);
/* Sets the balance (by hand). */
void kk_points_set(kk_points *p, const char *key, long value);
/* Takes cost if the balance has it; false (nothing taken) otherwise. */
bool kk_points_spend(kk_points *p, const char *key, long cost);

/* What a message earns (message, arrival, paid, member); returns it. */
long kk_points_message(kk_points *p, const char *key, const kk_chat_msg *m, double now);
/* Time passing for the viewers on screen: call it regularly with the keys of
 * every chatter whose avatar is up (now: monotonic seconds). */
void kk_points_watch(kk_points *p, const char *const *keys, int n, double now);

/* The n richest people (keys and balances), highest first; returns how
 * many. Keys point into the people list: use them before it changes. */
int kk_points_top(const kk_points *p, const char **keys, long *values, int n);

#endif
