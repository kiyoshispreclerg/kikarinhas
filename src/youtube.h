/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef KK_YOUTUBE_H
#define KK_YOUTUBE_H

/* YouTube live chat reader, public chat only, no login or API key. It uses
 * the same endpoints as the pop-out chat in the browser (InnerTube):
 *
 *  1. channel or handle: GET <channel>/live, whose canonical link names the
 *     current live video (retried every minute while the channel is
 *     offline);
 *  2. GET /live_chat?is_popout=1&v=ID: ytInitialData holds the chat; the
 *     "Live chat" view (not "Top chat") gives the continuation token;
 *  3. POST /youtubei/v1/live_chat/get_live_chat with the token, every few
 *     seconds; each answer has new actions and the next token. The first
 *     answer repeats the backlog and is skipped.
 *
 * Channel (member) emoji go out as image emotes of the message; reactions
 * (the floating hearts) come in every answer as per-second totals, in
 * frameworkUpdates...emojiFountainDataEntity.reactionBuckets, and go out
 * one bucket per second.
 *
 * Not an official API: YouTube may change it. The parsing lives in the pure
 * functions below, tested against recorded shapes in tests/. */

#include <stdbool.h>
#include <stddef.h>

#include "cJSON.h"
#include "chat.h"
#include "http.h"

typedef struct kk_youtube kk_youtube;

/* target: video link or id, channel link, @handle or UC... channel id.
 * sink is copied. Free it before the http client (it cancels its pending
 * requests). */
kk_youtube *kk_youtube_new(kk_http *http, const char *target,
                           const kk_chat_sink *sink);
void kk_youtube_free(kk_youtube *yt);

/* Starts the next request when it is due. now: monotonic seconds. */
void kk_youtube_tick(kk_youtube *yt, double now);

/* ---- parsing (exposed for tests) ----------------------------------------- */

typedef enum {
    KK_YT_TARGET_INVALID,
    KK_YT_TARGET_VIDEO,   /* out = 11-character video id */
    KK_YT_TARGET_CHANNEL, /* out = https URL of the channel's /live page */
} kk_yt_target;

kk_yt_target kk_yt_parse_target(const char *input, char *out, size_t size);

/* Video id from the canonical link of a watch or /live page. */
bool kk_yt_canonical_video(const char *html, char id[12]);

/* Parses ytInitialData out of a page; NULL if absent. */
cJSON *kk_yt_initial_data(const char *html);

/* Copies "KEY":"value" from the page config (e.g. INNERTUBE_CLIENT_VERSION). */
bool kk_yt_config_string(const char *html, const char *key, char *out,
                         size_t size);

/* Continuation of the "Live chat" view in the pop-out page data; malloc'd. */
char *kk_yt_live_continuation(const cJSON *initial);

/* Sends every chat message in an actions array to the sink. Returns how
 * many. */
int kk_yt_emit_actions(const cJSON *actions, const kk_chat_sink *sink);

/* Sends the reactions in a get_live_chat answer (or ytInitialData) to the
 * sink, bucket i with a delay of i seconds. Returns how many. */
int kk_yt_emit_reactions(const cJSON *root, const kk_chat_sink *sink);

/* An emoji thumbnail URL ("...=w48-h48-c-k-nd") asking for px x px. Other
 * URLs are copied as they are. False if out is too small. */
bool kk_yt_emote_url(const char *url, int px, char *out, size_t size);

typedef enum {
    KK_YT_POLL_OK,
    KK_YT_POLL_ENDED, /* no continuation: the live is over */
    KK_YT_POLL_ERROR,
} kk_yt_poll;

/* Parses a get_live_chat answer. With emit false the actions and reactions
 * are skipped.
 * On OK, *next (malloc'd) and *delay_ms say when and how to poll again. */
kk_yt_poll kk_yt_parse_poll(const char *json, bool emit,
                            const kk_chat_sink *sink, char **next, int *delay_ms);

#endif
