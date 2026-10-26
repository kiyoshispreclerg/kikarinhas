/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The question queue: the [questions] section, the !pergunta command,
 * adding (trimmed, no repeats), the size limit and what looks like a
 * question. */
#include <stdlib.h>
#include <string.h>

#include "../harness.h"
#include "config.h"
#include "ini.h"
#include "questions.h"

static kk_chat_msg from(const char *user, const char *text)
{
    return (kk_chat_msg){.platform = "youtube", .user_id = user, .name = user, .text = text};
}

TEST(config_questions)
{
    kk_config c;
    kk_config_defaults(&c);
    CHECK(!c.questions_auto && c.questions_max == 50);
    kk_config_command *k = kk_config_find_command(&c, "question");
    CHECK(k && strcmp(k->action, "question") == 0 && k->enabled);
    bool alias = false;
    for (int i = 0; i < k->n_aliases; i++)
        alias |= strcmp(k->aliases[i], "pergunta") == 0;
    CHECK(alias);
    kk_ini *ini = kk_ini_parse("[questions]\nauto = yes\nmax = 10\n", NULL, NULL);
    kk_config_apply(&c, ini, NULL, NULL);
    kk_ini_free(ini);
    CHECK(c.questions_auto && c.questions_max == 10);
    kk_config_free(&c);
}

TEST(queue)
{
    kk_questions *q = kk_questions_new();
    kk_chat_msg ana = from("ana", "!pergunta  qual o jogo?  ");
    uint64_t a = kk_questions_add(q, &ana, "  qual o jogo?  ", 1);
    CHECK(a > 0);
    CHECK_STR_EQ(kk_questions_at(q, 0)->text, "qual o jogo?");
    CHECK_STR_EQ(kk_questions_at(q, 0)->name, "ana");
    CHECK(kk_questions_add(q, &ana, "qual o jogo?", 2) == 0); /* again */
    CHECK(kk_questions_add(q, &ana, "   ", 2) == 0);           /* nothing */
    kk_chat_msg bia = from("bia", "quando tem live?");
    uint64_t b = kk_questions_add(q, &bia, NULL, 3); /* the message itself */
    CHECK(b > a);
    CHECK_INT_EQ(kk_questions_count(q), 2);

    kk_chat_msg m;
    CHECK(kk_questions_message(q, b, &m));
    CHECK_STR_EQ(m.text, "quando tem live?");
    CHECK_STR_EQ(m.platform, "youtube");
    CHECK(!kk_questions_find(q, b)->shown);
    kk_questions_set_shown(q, b);
    CHECK(kk_questions_find(q, b)->shown);

    CHECK(kk_questions_remove(q, a));
    CHECK(!kk_questions_remove(q, a));
    CHECK(kk_questions_at(q, 0)->id == b);

    kk_questions_set_max(q, 3);
    for (int i = 0; i < 5; i++) {
        char text[32];
        snprintf(text, sizeof text, "pergunta %d?", i);
        CHECK(kk_questions_add(q, &ana, text, 10 + i));
    }
    CHECK_INT_EQ(kk_questions_count(q), 3); /* the oldest went */
    CHECK_STR_EQ(kk_questions_at(q, 0)->text, "pergunta 2?");

    /* Long ones are cut on a character boundary. */
    char longer[1200];
    memset(longer, 'a', sizeof longer);
    for (int i = 498; i < 1100; i += 2)
        memcpy(longer + i, "é", 2);
    longer[sizeof longer - 1] = '\0';
    uint64_t l = kk_questions_add(q, &bia, longer, 20);
    CHECK(l && strlen(kk_questions_find(q, l)->text) <= 500);
    CHECK(strlen(kk_questions_find(q, l)->text) % 2 == 0); /* 498 + whole "é"s */

    kk_questions_clear(q);
    CHECK_INT_EQ(kk_questions_count(q), 0);
    kk_questions_free(q);
}

TEST(looks_like_a_question)
{
    CHECK(kk_questions_looks_like("qual o jogo?"));
    CHECK(kk_questions_looks_like("tudo bem?? "));
    CHECK(kk_questions_looks_like("vai ter live amanhã? 😀"));
    CHECK(!kk_questions_looks_like("?"));
    CHECK(!kk_questions_looks_like("??"));
    CHECK(!kk_questions_looks_like("olá"));
    CHECK(!kk_questions_looks_like("o que? não!"));
    CHECK(!kk_questions_looks_like(""));
}

int main(void)
{
    RUN(config_questions);
    RUN(queue);
    RUN(looks_like_a_question);
    return harness_report();
}
