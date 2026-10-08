/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Timers: when each is due, and what survives a reload. */
#include <stdio.h>
#include <string.h>

#include "../harness.h"
#include "config.h"
#include "ini.h"
#include "timers.h"

static int warnings;

static void count(void *ud, int line, const char *msg)
{
    (void)ud;
    (void)line;
    (void)msg;
    warnings++;
}

static kk_config load(const char *text)
{
    kk_config c;
    kk_config_defaults(&c);
    warnings = 0;
    kk_ini *ini = kk_ini_parse(text, NULL, NULL);
    kk_config_apply(&c, ini, count, NULL);
    kk_ini_free(ini);
    return c;
}

TEST(config_timers)
{
    kk_config c = load("[timer.logo]\ncommand = !logo\nminutes = 15\n"
                       "[timer.Discord]\ncommand = !discord\nmessages = 50\nviewers = 3\n"
                       "[timer.off]\ncommand = !x\nminutes = 1\nenabled = no\n"
                       "[timer.sem]\nminutes = 1\n"
                       "[timer.sempre]\ncommand = !y\nviewers = 2\n");
    int n = c.n_timers;
    kk_config copy;
    bool copied = kk_config_copy(&copy, &c);
    CHECK_INT_EQ(warnings, 2); /* no command; nothing but viewers */
    CHECK_INT_EQ(n, 3);
    CHECK_STR_EQ(c.timers[1].name, "discord");
    CHECK_INT_EQ(c.timers[1].messages, 50);
    CHECK_INT_EQ(c.timers[1].viewers, 3);
    CHECK(!c.timers[2].enabled);
    CHECK(copied);
    CHECK_STR_EQ(copy.timers[0].command, "!logo");
    kk_config_free(&copy);
    kk_config_free(&c);
}

TEST(timers_due)
{
    kk_config c = load("[timer.logo]\ncommand = !logo\nminutes = 10\n"
                       "[timer.chat]\ncommand = !discord\nmessages = 3\nviewers = 2\n"
                       "[timer.off]\ncommand = !x\nminutes = 1\nenabled = no\n");
    kk_timers *t = kk_timers_new();
    kk_timers_configure(t, &c, 1000);
    const char *cmds[4], *names[4];
    int early = kk_timers_due(t, 1000 + 599, 0, cmds, names, 4);
    int ten = kk_timers_due(t, 1000 + 600, 0, cmds, names, 4);
    char first[32];
    snprintf(first, sizeof first, "%s", ten ? cmds[0] : "");
    int again = kk_timers_due(t, 1000 + 601, 0, cmds, names, 4); /* clock started over */
    for (int i = 0; i < 3; i++)
        kk_timers_message(t);
    int few_people = kk_timers_due(t, 1700, 1, cmds, names, 4);
    int enough = kk_timers_due(t, 1701, 2, cmds, names, 4);
    char second[32];
    snprintf(second, sizeof second, "%s", enough ? names[0] : "");
    int count_reset = kk_timers_due(t, 1702, 5, cmds, names, 4);

    /* A reload keeps the clock of a timer that stays. */
    kk_timers_message(t);
    kk_timers_configure(t, &c, 1703);
    kk_timers_message(t);
    kk_timers_message(t);
    int kept = kk_timers_due(t, 1704, 2, cmds, names, 4); /* the logo's 10 min aren't up */
    kk_timers_free(t);
    kk_config_free(&c);
    CHECK_INT_EQ(early, 0);
    CHECK_INT_EQ(ten, 1);
    CHECK_STR_EQ(first, "!logo");
    CHECK_INT_EQ(again, 0);
    CHECK_INT_EQ(few_people, 0);
    CHECK_INT_EQ(enough, 1);
    CHECK_STR_EQ(second, "chat");
    CHECK_INT_EQ(count_reset, 0);
    CHECK_INT_EQ(kept, 1); /* 1 + 2 messages, counted across the reload */
}

int main(void)
{
    RUN(config_timers);
    RUN(timers_due);
    return harness_report();
}
