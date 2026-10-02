/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef KK_TWITCH_H
#define KK_TWITCH_H

/* Twitch chat reader, read only and anonymous: IRC over TLS at
 * irc.chat.twitch.tv:6697 as "justinfanNNNNN", which needs no account or
 * token. With the twitch.tv/tags capability every message carries the
 * sender's id, display name, badges and the positions of the Twitch
 * emotes in it.
 *
 * The connection never blocks: kk_http_connect resolves the name and opens
 * the TCP socket, OpenSSL runs on memory BIOs and we move the bytes with
 * recv/send (MSG_NOSIGNAL: a dropped connection is an error, not SIGPIPE).
 *
 * Emotes:
 *  - Twitch's own (global, subscriber, follower) come marked in the
 *    "emotes" tag, by code point; they become image emotes of the message
 *    (static PNG from static-cdn.jtvnw.net, no key needed).
 *  - BTTV, FFZ and 7TV emotes are plain words in the text: their lists
 *    (global and the channel's, found by the room-id of ROOMSTATE) are
 *    downloaded once per connection, and words in the text that name one
 *    become image emotes too (always asking for a static PNG).
 *
 * Bits and Hype Chat are paid messages; subs, resubs and gifts are member
 * messages; raids and announcements are ordinary messages. Twitch has no
 * reactions. The parsing lives in pure functions, tested in tests/. */

#include <poll.h>
#include <stdbool.h>
#include <stddef.h>

#include "chat.h"
#include "http.h"

typedef struct kk_twitch kk_twitch;

/* target: channel name, "#name" or a twitch.tv link. extra_emotes: also
 * fetch BTTV/FFZ/7TV. sink is copied. Free it before the http client. */
kk_twitch *kk_twitch_new(kk_http *http, const char *target,
                         const kk_chat_sink *sink, bool extra_emotes);
void kk_twitch_free(kk_twitch *tw);

/* The socket to watch, if any: fills *fd and returns 1, else 0. */
int kk_twitch_pollfd(const kk_twitch *tw, struct pollfd *fd);
/* After the poll (fd as filled by kk_twitch_pollfd, revents set; NULL if
 * kk_twitch_pollfd gave none): reads, writes, reconnects when due.
 * now: monotonic seconds. */
void kk_twitch_tick(kk_twitch *tw, const struct pollfd *fd, double now);

/* ---- parsing (exposed for tests) ----------------------------------------- */

/* "Name", "#name", "twitch.tv/name", "https://www.twitch.tv/name/..." →
 * "name" (lowercase). False if it is not a channel. */
bool kk_tw_parse_target(const char *input, char *out, size_t size);

/* One IRC line, split in place. Pointers go into the line; absent parts
 * are NULL (tags) or "" (the rest). */
typedef struct {
    const char *tags;     /* "a=1;b=2", without the "@" */
    const char *nick;     /* from the prefix "nick!user@host" */
    const char *command;  /* "PRIVMSG", "001"... */
    const char *channel;  /* first parameter when it starts with "#" */
    char *trailing;       /* text after " :" (writable: part of the line) */
} kk_tw_line;

bool kk_tw_split(char *line, kk_tw_line *out);

/* Value of a tag, unescaped (\s, \:, \\, \r, \n). False if absent. */
bool kk_tw_tag(const char *tags, const char *key, char *out, size_t size);

/* Third-party emotes by name. Lists come from several places and may
 * arrive in any order; on a name clash the channel beats the globals and
 * 7TV beats BTTV beats FFZ. */
typedef enum {
    KK_TW_FFZ_GLOBAL,
    KK_TW_BTTV_GLOBAL,
    KK_TW_7TV_GLOBAL,
    KK_TW_FFZ_CHANNEL,
    KK_TW_BTTV_CHANNEL,
    KK_TW_7TV_CHANNEL,
    KK_TW_N_SOURCES,
} kk_tw_source;

typedef struct kk_tw_emotes kk_tw_emotes;

typedef struct {
    const char *name, *id, *url;
} kk_tw_emote;

kk_tw_emotes *kk_tw_emotes_new(void);
void kk_tw_emotes_free(kk_tw_emotes *set);
/* Replaces what src had with the emotes in json (the provider's answer).
 * Returns how many, or -1 if json is not what that provider sends. */
int kk_tw_emotes_load(kk_tw_emotes *set, kk_tw_source src, const char *json);
/* Drops what src had (e.g. the channel changed). */
void kk_tw_emotes_clear(kk_tw_emotes *set, kk_tw_source src);
int kk_tw_emotes_count(const kk_tw_emotes *set);
const kk_tw_emote *kk_tw_emotes_find(const kk_tw_emotes *set, const char *word,
                                     size_t len);

/* Turns a PRIVMSG or USERNOTICE line (modified in place) into a chat
 * message for sink. extra may be NULL. Returns false for any other line or
 * one without a sender. */
bool kk_tw_emit_line(char *line, const kk_tw_emotes *extra,
                     const kk_chat_sink *sink);

#endif
