/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef KK_PHOTOS_H
#define KK_PHOTOS_H

/* Viewers' profile pictures, for the chat window.
 *
 * YouTube sends the picture's URL with every message (kk_chat_msg.photo);
 * the other platforms need a request per person to learn it:
 *  - Twitch: decapi.me, by user id (Helix wants a login token);
 *  - Kick: the channel API, by slug (the name, lowercased);
 *  - Odysee: the SDK's resolve, by "@name#claim id".
 * Each URL learned is kept on disk for a week (an empty file: no picture),
 * so a restart doesn't ask again. The pictures themselves are downloaded
 * and cached by kk_emotes, keyed by their URL: a new picture is a new URL. */

#include <stddef.h>

#include "emotes.h"
#include "http.h"

typedef struct kk_photos kk_photos;

/* images must outlive it. cache_dir may be NULL (nothing on disk). */
kk_photos *kk_photos_new(kk_http *http, kk_emotes *images, const char *cache_dir);
void kk_photos_free(kk_photos *p);

/* The picture of user_id on platform: url when the message brought one,
 * else looked up (name helps on Kick and Odysee). LOADING until known;
 * FAILED when there is none. Cheap to call again every draw. */
kk_emote_state kk_photos_get(kk_photos *p, const char *platform, const char *user_id,
                             const char *name, const char *url, kk_emote_anim **out);

/* ~/.cache/kikarinhas/photos (or under XDG_CACHE_HOME). */
bool kk_photos_default_dir(char *out, size_t size);

/* The picture's URL out of each platform's answer, for tests. False when
 * the answer says there is none (or can't be read). */
bool kk_photos_parse_twitch(const char *body, char *out, size_t size);
bool kk_photos_parse_kick(const char *body, char *out, size_t size);
bool kk_photos_parse_odysee(const char *body, char *out, size_t size);
/* Kick's slug for a username: "Some_Name" → "some_name". */
bool kk_photos_kick_slug(const char *name, char *out, size_t size);

#endif
