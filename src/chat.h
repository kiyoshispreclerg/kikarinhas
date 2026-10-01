/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef KK_CHAT_H
#define KK_CHAT_H

/* A chat event, the same for every platform. Strings are only valid during
 * the callback. */

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

typedef struct {
    const char *platform; /* "youtube", ... */
    const char *user_id;  /* stable per person and platform */
    const char *name;     /* display name */
    const char *text;     /* may be empty */
    const char *amount;   /* KK_MSG_PAID: e.g. "R$ 10,00"; otherwise NULL */
    unsigned badges;
    kk_msg_kind kind;
} kk_chat_msg;

typedef void (*kk_chat_cb)(void *ud, const kk_chat_msg *msg);

#endif
