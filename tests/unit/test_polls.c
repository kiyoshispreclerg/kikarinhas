/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Polls: what a request may hold (options, shortcuts and their conflicts),
 * which words vote, votes per platform (changing a vote), time limits, and
 * the history file going to disk and back. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../harness.h"
#include "polls.h"
#include "util.h"

/* "!jump", "!b" and "!3" are taken in these tests. */
static bool taken(void *ud, const char *word)
{
    const char *const *list = ud;
    for (; *list; list++)
        if (strcmp(*list, word) == 0)
            return true;
    return false;
}

static const char *const none[] = {NULL};

static kk_poll *request(const char *json, const char *const *busy, char *err, size_t size)
{
    cJSON *j = cJSON_Parse(json);
    kk_poll *p = kk_poll_from_request(j, taken, (void *)busy, err, size);
    cJSON_Delete(j);
    return p;
}

#define TWO "\"options\":[{\"text\":\"Sim\"},{\"text\":\"Não\"}]"

TEST(request_checks)
{
    char err[200];
    CHECK(!request("{\"question\":\"  \"," TWO "}", none, err, sizeof err));
    CHECK(strstr(err, "pergunta"));
    CHECK(!request("{\"question\":\"x\",\"options\":[{\"text\":\"so uma\"}]}", none, err,
                   sizeof err));
    CHECK(!request("{\"question\":\"x\",\"options\":[{\"text\":\"a\"},{\"text\":\" \"}]}", none,
                   err, sizeof err));
    CHECK(!request("{\"question\":\"x\",\"options\":[{\"text\":\"a\",\"command\":\"duas "
                   "palavras\"},{\"text\":\"b\"}]}",
                   none, err, sizeof err));
    CHECK(!request("{\"question\":\"x\",\"duration\":-5," TWO "}", none, err, sizeof err));

    kk_poll *p = request("{\"question\":\" Qual? \",\"duration\":90,\"show_counts\":false,"
                         "\"options\":[{\"text\":\"Sim\",\"command\":\"!SIM\"},"
                         "{\"text\":\"Não\",\"command\":\"\"}]}",
                         none, err, sizeof err);
    CHECK_MSG(p != NULL, "%s", err);
    CHECK_STR_EQ(p->question, "Qual?");
    CHECK_INT_EQ(p->n_options, 2);
    CHECK_STR_EQ(p->options[0].shortcut, "sim");
    CHECK(p->options[1].shortcut == NULL);
    CHECK_INT_EQ(p->duration, 90);
    CHECK(!p->show_counts && p->letters && p->numbers);
    kk_poll_free(p);
}

TEST(shortcut_conflicts)
{
    char err[200];
    static const char *const busy[] = {"jump", "b", "3", NULL};
    /* A shortcut that is a command. */
    CHECK(!request("{\"question\":\"x\",\"options\":[{\"text\":\"a\",\"command\":\"jump\"},"
                   "{\"text\":\"b\"}]}",
                   busy, err, sizeof err));
    CHECK(strstr(err, "!jump"));
    /* Two options with the same one. */
    CHECK(!request("{\"question\":\"x\",\"options\":[{\"text\":\"a\",\"command\":\"x\"},"
                   "{\"text\":\"b\",\"command\":\"X\"}]}",
                   none, err, sizeof err));
    /* One that is another option's letter or number. */
    CHECK(!request("{\"question\":\"x\",\"options\":[{\"text\":\"a\",\"command\":\"b\"},"
                   "{\"text\":\"b\"}]}",
                   none, err, sizeof err));
    CHECK(!request("{\"question\":\"x\",\"options\":[{\"text\":\"a\",\"command\":\"2\"},"
                   "{\"text\":\"b\"}]}",
                   none, err, sizeof err));
    /* !b is a command: letters don't vote, numbers do. */
    kk_poll *p = request("{\"question\":\"x\"," TWO "}", busy, err, sizeof err);
    CHECK(p && !p->letters && p->numbers);
    kk_poll_free(p);
    /* !b and !3 taken in a 3-option poll: only shortcuts. */
    const char *three = "{\"question\":\"x\",\"options\":[{\"text\":\"a\",\"command\":\"um\"},"
                        "{\"text\":\"b\",\"command\":\"dois\"},{\"text\":\"c\"}]}";
    CHECK(!request(three, busy, err, sizeof err));
    p = request("{\"question\":\"x\",\"options\":[{\"text\":\"a\",\"command\":\"um\"},"
                "{\"text\":\"b\",\"command\":\"dois\"},{\"text\":\"c\",\"command\":\"tres\"}]}",
                busy, err, sizeof err);
    CHECK_MSG(p != NULL, "%s", err);
    CHECK(!p->letters && !p->numbers);
    char w[64], sc[64];
    kk_poll_words(p, 2, w, sizeof w, sc, sizeof sc);
    CHECK_STR_EQ(w, "!tres");
    CHECK_STR_EQ(sc, "");
    kk_poll_free(p);
}

TEST(matching)
{
    char err[200];
    kk_poll *p = request("{\"question\":\"x\",\"options\":[{\"text\":\"a\"},{\"text\":\"b\"},"
                         "{\"text\":\"c\",\"command\":\"Cê\"}]}",
                         none, err, sizeof err);
    CHECK(p);
    CHECK_INT_EQ(kk_poll_match(p, "!a"), 0);
    CHECK_INT_EQ(kk_poll_match(p, "  !B qualquer coisa"), 1);
    CHECK_INT_EQ(kk_poll_match(p, "!3"), 2);
    CHECK_INT_EQ(kk_poll_match(p, "\xEF\xBC\x81" "2"), 1);
    CHECK_INT_EQ(kk_poll_match(p, "!cê"), 2);
    CHECK_INT_EQ(kk_poll_match(p, "!d"), -1);
    CHECK_INT_EQ(kk_poll_match(p, "!4"), -1);
    CHECK_INT_EQ(kk_poll_match(p, "!03"), -1);
    CHECK_INT_EQ(kk_poll_match(p, "a"), -1);
    CHECK_INT_EQ(kk_poll_match(p, "!"), -1);
    char w[64], sc[64];
    kk_poll_words(p, 2, w, sizeof w, sc, sizeof sc);
    CHECK_STR_EQ(w, "!c");
    CHECK_STR_EQ(sc, "!cê");
    kk_poll_free(p);
}

TEST(votes_and_history)
{
    char path[] = "/tmp/kk-polls-XXXXXX";
    int fd = mkstemp(path);
    CHECK(fd >= 0);
    close(fd);
    unlink(path);

    kk_polls *ps = kk_polls_open(path);
    CHECK(ps && kk_polls_count(ps) == 0 && !kk_polls_active(ps));
    char err[200];
    kk_poll *draft = request("{\"question\":\"Qual?\",\"duration\":60," TWO "}", none, err,
                             sizeof err);
    const char *const ch[] = {"youtube @canal"};
    const kk_poll *p = kk_polls_start(ps, draft, 1000, ch, 1);
    CHECK(p && p->id == 1 && p->opened == 1000 && kk_polls_active(ps) == p);
    CHECK_INT_EQ(kk_poll_ends_at(p), 1060);
    /* A second one can't open meanwhile. */
    draft = request("{\"question\":\"Outra\"," TWO "}", none, err, sizeof err);
    CHECK(!kk_polls_start(ps, draft, 1001, NULL, 0));

    CHECK_INT_EQ(kk_polls_vote(ps, "youtube:ana", 0), -1);
    CHECK_INT_EQ(kk_polls_vote(ps, "twitch:bia", 1), -1);
    CHECK_INT_EQ(kk_polls_vote(ps, "youtube:caio", 1), -1);
    CHECK_INT_EQ(kk_polls_vote(ps, "youtube:ana", 1), 0); /* changed her mind */
    CHECK_INT_EQ(kk_polls_vote(ps, "youtube:ana", 1), 1);
    CHECK_INT_EQ(kk_polls_vote(ps, "youtube:ana", 7), -2);
    CHECK_INT_EQ(kk_poll_votes(p, 0, -1), 0);
    CHECK_INT_EQ(kk_poll_votes(p, 1, -1), 3);
    CHECK_INT_EQ(p->n_platforms, 2);
    CHECK_STR_EQ(p->platforms[0], "youtube");
    CHECK_INT_EQ(kk_poll_votes(p, 1, 0), 2);
    CHECK_INT_EQ(kk_poll_total(p, 1), 1);
    CHECK_INT_EQ(kk_poll_vote_of(p, "twitch:bia"), 1);
    CHECK_INT_EQ(kk_poll_vote_of(p, "twitch:ana"), -1);
    kk_polls_note_live(ps, "youtube", "https://youtube.com/watch?v=x", NULL);
    kk_polls_note_live(ps, "youtube", "https://youtube.com/watch?v=x", "A live");
    CHECK(p->n_lives == 1 && strcmp(p->lives[0].title, "A live") == 0);

    /* Saved while open, voters too: a restart keeps them. */
    CHECK(kk_polls_dirty(ps));
    CHECK_INT_EQ(kk_polls_save(ps), 0);
    kk_polls_free(ps);
    ps = kk_polls_open(path);
    p = kk_polls_active(ps);
    CHECK(p && p->id == 1);
    CHECK_INT_EQ(kk_poll_vote_of(p, "youtube:ana"), 1);
    CHECK_INT_EQ(kk_polls_vote(ps, "youtube:ana", 1), 1); /* not twice */
    CHECK_INT_EQ(kk_poll_votes(p, 1, -1), 3);
    CHECK(p->n_channels == 1 && strcmp(p->channels[0], "youtube @canal") == 0);

    /* Its time ran out while nobody was looking: closed when it did. */
    CHECK(!kk_polls_due(ps, 1059));
    CHECK(kk_polls_due(ps, 5000));
    CHECK(!kk_polls_remove(ps, 1)); /* open */
    CHECK(kk_polls_close(ps, 5000));
    CHECK(!kk_polls_active(ps));
    p = kk_polls_find(ps, 1);
    CHECK_INT_EQ(p->closed, 1060);
    CHECK_INT_EQ(kk_polls_vote(ps, "youtube:dani", 0), -2);
    draft = request("{\"question\":\"Outra\"," TWO "}", none, err, sizeof err);
    CHECK(kk_polls_start(ps, draft, 6000, NULL, 0)->id == 2);
    kk_polls_close(ps, 6010);
    CHECK_INT_EQ(kk_polls_save(ps), 0);
    kk_polls_free(ps);

    /* Closed: counts stay, who voted doesn't. */
    char *text = kk_read_file(path, NULL);
    CHECK(text && !strstr(text, "youtube:ana") && strstr(text, "\"twitch\""));
    free(text);
    ps = kk_polls_open(path);
    CHECK_INT_EQ(kk_polls_count(ps), 2);
    p = kk_polls_at(ps, 0);
    CHECK_INT_EQ(kk_poll_votes(p, 1, -1), 3);
    CHECK_INT_EQ(p->n_voters, 0);
    CHECK(kk_polls_remove(ps, 1));
    CHECK_INT_EQ(kk_polls_count(ps), 1);
    kk_polls_save(ps);
    kk_polls_free(ps);
    ps = kk_polls_open(path);
    draft = request("{\"question\":\"Mais uma\"," TWO "}", none, err, sizeof err);
    CHECK(kk_polls_start(ps, draft, 7000, NULL, 0)->id == 3); /* ids never come back */
    kk_polls_free(ps);

    char bak[300];
    snprintf(bak, sizeof bak, "%s.backup", path);
    unlink(path);
    unlink(bak);
}

int main(void)
{
    RUN(request_checks);
    RUN(shortcut_conflicts);
    RUN(matching);
    RUN(votes_and_history);
    return harness_report();
}
