/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef KK_ODYSEE_H
#define KK_ODYSEE_H

/* Odysee chat reader, read only and anonymous. Live chat on Odysee is the
 * comment section of the stream's claim, pushed by Commentron's websocket
 * (wss://sockety.odysee.tv/ws/commentron?id=<claim id>) as "delta"
 * events. The target becomes a claim id this way:
 *  - a claim id (40 hex digits): used as is;
 *  - a stream link (odysee.com/@channel:x/stream:y): "resolve" on Odysee's
 *    SDK proxy gives its claim id;
 *  - a channel (odysee.com/@channel or @channel): resolved too, then
 *    api.odysee.live says whether it is live and which claim the live is;
 *    asked again from time to time, so a new live is followed.
 *
 * Commenters are channels: the channel's claim id is the person's id.
 * Comments with a tip are paid messages (LBC or dollars). Odysee's own
 * emotes and stickers (":name:" and "<stkr>:NAME:<stkr>") become image
 * emotes, and its ":name:" for Unicode emoji become the emoji (tables in
 * odysee_emotes.h, generated from the site's code). Mentions lose the
 * claim part ("@name:c" → "@name"). The parsing lives in pure functions,
 * tested in tests/. */

#include <poll.h>
#include <stdbool.h>
#include <stddef.h>

#include "chat.h"
#include "http.h"

typedef struct kk_odysee kk_odysee;

/* target: see above. sink is copied. Free it before the http client. */
kk_odysee *kk_odysee_new(kk_http *http, const char *target, const kk_chat_sink *sink);
void kk_odysee_free(kk_odysee *o);

int kk_odysee_pollfd(const kk_odysee *o, struct pollfd *fd);
void kk_odysee_tick(kk_odysee *o, const struct pollfd *fd, double now);

/* ---- parsing (exposed for tests) ----------------------------------------- */

typedef enum {
    KK_ODY_TARGET_INVALID,
    KK_ODY_TARGET_CLAIM,   /* out: the claim id */
    KK_ODY_TARGET_STREAM,  /* out: "lbry://@channel:x/stream:y" */
    KK_ODY_TARGET_CHANNEL, /* out: "lbry://@channel:x" */
} kk_ody_target;

kk_ody_target kk_ody_parse_target(const char *input, char *out, size_t size);

/* The answer to "resolve" for one url: claim id and, for a stream, the
 * name of its channel ("@name", for the socket); for a channel, its own. */
bool kk_ody_parse_resolve(const char *json, char *claim, size_t claim_size, char *channel,
                          size_t channel_size, bool *is_channel);

/* api.odysee.live's is_live: whether it is live and the live's claim id. */
bool kk_ody_parse_is_live(const char *json, bool *live, char *claim, size_t claim_size);

/* One websocket message; a new comment becomes a chat message for sink.
 * source may be NULL. Returns false for anything else. */
bool kk_ody_emit_event(const char *json, const char *source, const kk_chat_sink *sink);

#endif
