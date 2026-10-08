/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef KK_MPRIS_H
#define KK_MPRIS_H

/* What music the desktop is playing, from the MPRIS players on the session
 * D-Bus (the same thing the panels' media widgets show): Spotify, VLC,
 * Audacious, browsers...
 *
 * Nothing blocks: the bus descriptors go into the main poll(), the player
 * list comes from an asynchronous ListNames and then GetAll per player,
 * and NameOwnerChanged/PropertiesChanged signals keep it current, so
 * asking for the current song is instant. Without a session bus it is just
 * unavailable. */

#include <poll.h>
#include <stdbool.h>
#include <stddef.h>

typedef struct kk_mpris kk_mpris;

/* Connects to the session bus; NULL (and a log line) if there is none. */
kk_mpris *kk_mpris_new(void);
void kk_mpris_free(kk_mpris *m);

/* Descriptors to poll (at most max), and what to do after the poll. */
int kk_mpris_pollfds(kk_mpris *m, struct pollfd *fds, int max);
void kk_mpris_dispatch(kk_mpris *m, const struct pollfd *fds, int n);

/* "Artist — Title" of the player that is playing (the latest to start, if
 * several), else of a paused one with paused set; only players whose bus
 * name contains filter (case-insensitive; NULL or "" = any). False if
 * nothing is playing or paused. */
bool kk_mpris_current(const kk_mpris *m, const char *filter, char *out, size_t size,
                      bool *paused);

/* For tests: a kk_mpris without a bus, fed with messages by hand. */
kk_mpris *kk_mpris_new_offline(void);
/* Handles one incoming message (a DBusMessage *). */
void kk_mpris_handle(kk_mpris *m, void *dbus_message);

#endif
