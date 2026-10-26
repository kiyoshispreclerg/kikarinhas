/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef KK_QUESTIONS_H
#define KK_QUESTIONS_H

/* The question queue: questions the chat sends with !pergunta (and,
 * optionally, any message ending in "?"), kept in arrival order for the
 * streamer to go through. The list is shown only to the streamer (the
 * configurator asks for it over the socket), never on the stream; picking
 * one highlights it on the stage like a chat window click. */

#include <stdbool.h>
#include <stdint.h>

#include "chat.h"

typedef struct {
    uint64_t id;
    char *platform, *user_id, *name, *text;
    unsigned badges;
    double t;   /* monotonic, when it arrived */
    bool shown; /* already highlighted once */
} kk_question;

typedef struct kk_questions kk_questions;

kk_questions *kk_questions_new(void);
void kk_questions_free(kk_questions *q);
/* At most max questions; past that the oldest go. */
void kk_questions_set_max(kk_questions *q, int max);

/* Queues text (trimmed; NULL: the message's own text) asked by m's author.
 * Returns its id, or 0 if empty or the same person already asked the same
 * thing and it is still queued. */
uint64_t kk_questions_add(kk_questions *q, const kk_chat_msg *m, const char *text, double now);

int kk_questions_count(const kk_questions *q);
/* Oldest first. */
const kk_question *kk_questions_at(const kk_questions *q, int i);
const kk_question *kk_questions_find(const kk_questions *q, uint64_t id);
/* The question as a chat message, to highlight (valid until it changes). */
bool kk_questions_message(const kk_questions *q, uint64_t id, kk_chat_msg *out);
void kk_questions_set_shown(kk_questions *q, uint64_t id);
bool kk_questions_remove(kk_questions *q, uint64_t id);
void kk_questions_clear(kk_questions *q);

/* Whether a chat message looks like a question: ends in "?" (ignoring
 * spaces and emoji after it), and has some text before. */
bool kk_questions_looks_like(const char *text);

#endif
