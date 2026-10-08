/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef KK_TIMERS_H
#define KK_TIMERS_H

/* Commands that run by themselves now and then ([timer.NAME]): a logo
 * every 15 minutes, the Discord link after 50 messages, a sound when the
 * chat is full... A timer is due when every condition it sets holds:
 *  - minutes since it last ran (or since kikarinhas started);
 *  - chat messages since then;
 *  - at least that many people of the chat on screen.
 * Only the decision lives here; main.c runs the command, as the owner. */

#include "config.h"

typedef struct kk_timers kk_timers;

kk_timers *kk_timers_new(void);
void kk_timers_free(kk_timers *t);

/* Takes the timers of cfg. One that keeps its name keeps its clock and
 * message count across reloads; new ones start counting at now. */
void kk_timers_configure(kk_timers *t, const kk_config *cfg, double now);

/* One more chat message for every timer. */
void kk_timers_message(kk_timers *t);

/* The commands due at now with viewers people on screen, at most max (the
 * strings live until the next configure); their clocks and counts start
 * over. Returns how many; names gets the timers' names. */
int kk_timers_due(kk_timers *t, double now, int viewers, const char **commands,
                  const char **names, int max);

#endif
