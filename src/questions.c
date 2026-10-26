/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The question queue (see questions.h). */
#include "questions.h"

#include <stdlib.h>
#include <string.h>

#define MAX_TEXT 500 /* bytes of a question kept */

struct kk_questions {
    kk_question *items; /* oldest first */
    int n, cap, max;
    uint64_t next_id;
};

kk_questions *kk_questions_new(void)
{
    kk_questions *q = calloc(1, sizeof *q);
    if (!q)
        return NULL;
    q->max = 50;
    q->next_id = 1;
    return q;
}

static void item_free(kk_question *it)
{
    free(it->platform);
    free(it->user_id);
    free(it->name);
    free(it->text);
}

static void remove_at(kk_questions *q, int i)
{
    item_free(&q->items[i]);
    memmove(&q->items[i], &q->items[i + 1], (size_t)(q->n - i - 1) * sizeof *q->items);
    q->n--;
}

void kk_questions_clear(kk_questions *q)
{
    while (q->n > 0)
        remove_at(q, q->n - 1);
}

void kk_questions_free(kk_questions *q)
{
    if (!q)
        return;
    kk_questions_clear(q);
    free(q->items);
    free(q);
}

void kk_questions_set_max(kk_questions *q, int max)
{
    q->max = max < 1 ? 1 : max;
    while (q->n > q->max)
        remove_at(q, 0);
}

static bool is_space(char c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

/* text without the spaces around it, cut at MAX_TEXT on a character
 * boundary. NULL if empty or out of memory. */
static char *trimmed(const char *text)
{
    while (is_space(*text))
        text++;
    size_t n = strlen(text);
    while (n > 0 && is_space(text[n - 1]))
        n--;
    if (n > MAX_TEXT) {
        n = MAX_TEXT;
        while (n > 0 && ((unsigned char)text[n] & 0xc0) == 0x80)
            n--; /* not in the middle of a UTF-8 character */
    }
    if (n == 0)
        return NULL;
    char *s = malloc(n + 1);
    if (s) {
        memcpy(s, text, n);
        s[n] = '\0';
    }
    return s;
}

uint64_t kk_questions_add(kk_questions *q, const kk_chat_msg *m, const char *text, double now)
{
    char *t = trimmed(text ? text : m->text ? m->text : "");
    if (!t)
        return 0;
    const char *platform = m->platform ? m->platform : "";
    const char *user = m->user_id ? m->user_id : "";
    for (int i = 0; i < q->n; i++)
        if (strcmp(q->items[i].text, t) == 0 && strcmp(q->items[i].user_id, user) == 0 &&
            strcmp(q->items[i].platform, platform) == 0) {
            free(t);
            return 0; /* asked twice */
        }
    if (q->n == q->max)
        remove_at(q, 0);
    if (q->n == q->cap) {
        int cap = q->cap ? q->cap * 2 : 16;
        kk_question *ni = realloc(q->items, (size_t)cap * sizeof *ni);
        if (!ni) {
            free(t);
            return 0;
        }
        q->items = ni;
        q->cap = cap;
    }
    kk_question *it = &q->items[q->n];
    *it = (kk_question){.id = q->next_id, .platform = strdup(platform), .user_id = strdup(user),
                        .name = strdup(m->name ? m->name : ""), .text = t,
                        .badges = m->badges, .t = now};
    if (!it->platform || !it->user_id || !it->name) {
        item_free(it);
        return 0;
    }
    q->n++;
    return q->next_id++;
}

int kk_questions_count(const kk_questions *q)
{
    return q->n;
}

const kk_question *kk_questions_at(const kk_questions *q, int i)
{
    return i >= 0 && i < q->n ? &q->items[i] : NULL;
}

static int index_of(const kk_questions *q, uint64_t id)
{
    for (int i = 0; i < q->n; i++)
        if (q->items[i].id == id)
            return i;
    return -1;
}

const kk_question *kk_questions_find(const kk_questions *q, uint64_t id)
{
    return kk_questions_at(q, index_of(q, id));
}

bool kk_questions_message(const kk_questions *q, uint64_t id, kk_chat_msg *out)
{
    const kk_question *it = kk_questions_find(q, id);
    if (!it)
        return false;
    *out = (kk_chat_msg){.platform = it->platform, .user_id = it->user_id, .name = it->name,
                         .text = it->text, .badges = it->badges, .kind = KK_MSG_TEXT};
    return true;
}

void kk_questions_set_shown(kk_questions *q, uint64_t id)
{
    int i = index_of(q, id);
    if (i >= 0)
        q->items[i].shown = true;
}

bool kk_questions_remove(kk_questions *q, uint64_t id)
{
    int i = index_of(q, id);
    if (i < 0)
        return false;
    remove_at(q, i);
    return true;
}

bool kk_questions_looks_like(const char *text)
{
    /* Back from the end over spaces and anything that isn't ASCII (emoji,
     * which people add after the question mark). */
    size_t n = strlen(text);
    while (n > 0 && (is_space(text[n - 1]) || (unsigned char)text[n - 1] >= 0x80))
        n--;
    if (n == 0 || text[n - 1] != '?')
        return false;
    while (n > 0 && (text[n - 1] == '?' || is_space(text[n - 1])))
        n--;
    return n >= 3; /* a lonely "?" or "??" is not a question */
}
