/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef KK_KICK_H
#define KK_KICK_H

/* Kick chat reader, read only and anonymous. Kick's site gets its chat
 * from Pusher: a websocket to ws-us2.pusher.com with Kick's public app key,
 * subscribed to the channel "chatrooms.<id>.v2". The chatroom id comes from
 * kick.com/api/v2/channels/<slug>, which also gives the user id 7TV knows
 * the channel by.
 *
 * Kicks (paid gifts) come on another Pusher channel, "channel.<id>".
 *
 * Messages carry the sender's numeric id, name and badges; Kick emotes sit
 * in the text as "[emote:ID:NAME]", which becomes NAME in the text and an
 * image emote (files.kick.com, PNG). With extra emotes on, 7TV's lists for
 * Kick (global and the channel's) are matched word by word, like on
 * Twitch. Kicks become paid messages. Subscriptions and gifted subs become
 * member messages, but those events only name the person: the id is looked
 * up among the recent chatters (who never wrote gets no message). The chat
 * works with the channel offline too, so there is nothing to wait for. The
 * parsing lives in pure functions, tested in tests/. */

#include <poll.h>
#include <stdbool.h>
#include <stddef.h>

#include "chat.h"
#include "http.h"
#include "twitch.h"

typedef struct kk_kick kk_kick;

/* target: channel name or kick.com link. sink is copied. Free it before the
 * http client. */
kk_kick *kk_kick_new(kk_http *http, const char *target, const kk_chat_sink *sink,
                     bool extra_emotes);
void kk_kick_free(kk_kick *k);

/* Like the Twitch ones: the socket to watch, then the work after poll(). */
int kk_kick_pollfd(const kk_kick *k, struct pollfd *fd);
void kk_kick_tick(kk_kick *k, const struct pollfd *fd, double now);

/* ---- parsing (exposed for tests) ----------------------------------------- */

/* "Name", "kick.com/name", "https://kick.com/name/videos",
 * "https://kick.com/popout/name/chat" → "name" (lowercase). */
bool kk_kick_parse_target(const char *input, char *out, size_t size);

/* The title of the live going on in that same reply; false when offline. */
bool kk_kick_parse_title(const char *json, char *out, size_t size);

/* From kick.com/api/v2/channels/<slug>: the chatroom, the channel (for
 * "channel.<id>") and the user id (for 7TV). */
bool kk_kick_parse_channel(const char *json, long long *chatroom, long long *channel,
                           long long *user_id);

/* Recent chatters by name, for the events that only name someone. */
typedef struct kk_kick_names kk_kick_names;
kk_kick_names *kk_kick_names_new(void);
void kk_kick_names_free(kk_kick_names *n);

/* One Pusher event (the whole frame: {"event", "channel", "data"}) turned
 * into a chat message for sink; chat messages are remembered in names.
 * source, extra and names may be NULL. Returns false for events that are
 * not one. */
bool kk_kick_emit_event(const char *json, const char *source, const kk_tw_emotes *extra,
                        kk_kick_names *names, const kk_chat_sink *sink);

#endif
