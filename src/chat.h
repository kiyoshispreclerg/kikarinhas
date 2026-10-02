/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef KK_CHAT_H
#define KK_CHAT_H

#include <stddef.h>

/* A chat event, the same for every platform. Strings are only valid during
 * the callback.
 *
 * Connectors (youtube.c, the bridges in control.c, a future Twitch...) all
 * talk to the rest of the program through a kk_chat_sink: messages, and
 * reactions for platforms that have them. Emoji follow one rule
 * everywhere:
 *  - Unicode emoji stay in the text as characters; emoji.c finds them.
 *  - Platform emotes that are images (YouTube channel emoji, Twitch emotes,
 *    7TV...) are listed in emotes, with an image URL and where the text
 *    names them (":_hello:", "Kappa"). */

typedef enum {
    KK_MSG_TEXT,
    KK_MSG_PAID,   /* Super Chat / Super Sticker / bits... */
    KK_MSG_MEMBER, /* new member / subscriber */
} kk_msg_kind;

enum {
    KK_BADGE_OWNER = 1 << 0,
    KK_BADGE_MOD = 1 << 1,
    KK_BADGE_MEMBER = 1 << 2,
    KK_BADGE_VERIFIED = 1 << 3,
};

/* An emote: an image (url) or, in reactions only, a Unicode emoji (text). */
typedef struct {
    const char *id;   /* unique within the platform; caches go by it */
    const char *name; /* what people type: ":_hello:", "Kappa"; may be NULL */
    const char *url;  /* image (PNG); NULL for Unicode emoji */
    const char *text; /* the Unicode emoji when url is NULL */
    size_t start, len; /* bytes of the message text it stands for; len 0 if
                        * not in the text (or unknown) */
} kk_emote;

typedef struct {
    const char *platform; /* "youtube", ... */
    const char *user_id;  /* stable per person and platform */
    const char *name;     /* display name */
    const char *text;     /* may be empty */
    const char *amount;   /* KK_MSG_PAID: e.g. "R$ 10,00"; otherwise NULL */
    const kk_emote *emotes; /* image emotes, in text order */
    int n_emotes;
    unsigned badges;
    kk_msg_kind kind;
} kk_chat_msg;

/* Viewers reacting without writing (YouTube's floating hearts). Platforms
 * only send totals, never who reacted. */
typedef struct {
    const char *platform;
    kk_emote emote;
    int count;
    double delay; /* seconds from now when they happened on the platform's
                   * timeline, so a batch can be spread out as it came */
} kk_reaction;

typedef void (*kk_chat_cb)(void *ud, const kk_chat_msg *msg);
typedef void (*kk_reaction_cb)(void *ud, const kk_reaction *r);

/* Where a connector delivers what it reads. on_reaction may be NULL. */
typedef struct {
    kk_chat_cb on_msg;
    kk_reaction_cb on_reaction;
    void *ud;
} kk_chat_sink;

#endif
