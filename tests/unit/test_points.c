/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Points: what earns them, what costs them, and the commands that pay. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../harness.h"
#include "commands.h"
#include "config.h"
#include "ini.h"
#include "points.h"
#include "users.h"

static char path[64];

static kk_users *fresh_users(void)
{
    snprintf(path, sizeof path, "/tmp/kk_points_XXXXXX");
    int fd = mkstemp(path);
    if (fd >= 0)
        close(fd);
    return kk_users_open(path);
}

static const kk_points_config CFG = {
    .enabled = true,
    .start = 5,
    .message = 1,
    .message_cooldown = 30,
    .watch = 10,
    .watch_minutes = 1,
    .arrival = 20,
    .paid = 100,
    .member = 50,
};

static kk_chat_msg msg(kk_msg_kind kind)
{
    return (kk_chat_msg){.platform = "t", .user_id = "a", .name = "A", .text = "oi",
                         .kind = kind};
}

TEST(points_earned_by_messages)
{
    kk_users *u = fresh_users();
    kk_points *p = kk_points_new(u);
    kk_points_configure(p, &CFG);
    long before = kk_points_get(p, "t:a");
    kk_chat_msg m = msg(KK_MSG_TEXT);
    long first = kk_points_message(p, "t:a", &m, 100);   /* arrival + message */
    long spam = kk_points_message(p, "t:a", &m, 110);    /* cooling down */
    long later = kk_points_message(p, "t:a", &m, 131);   /* message again */
    kk_chat_msg paid = msg(KK_MSG_PAID), member = msg(KK_MSG_MEMBER);
    long sc = kk_points_message(p, "t:a", &paid, 132);   /* paid, no message */
    long sub = kk_points_message(p, "t:a", &member, 133);
    long total = kk_points_get(p, "t:a");
    kk_points_free(p);
    kk_users_free(u);
    unlink(path);
    CHECK_INT_EQ(before, 5);
    CHECK_INT_EQ(first, 21);
    CHECK_INT_EQ(spam, 0);
    CHECK_INT_EQ(later, 1);
    CHECK_INT_EQ(sc, 100);
    CHECK_INT_EQ(sub, 50);
    CHECK_INT_EQ(total, 5 + 21 + 1 + 100 + 50);
}

TEST(points_for_watching)
{
    kk_users *u = fresh_users();
    kk_points *p = kk_points_new(u);
    kk_points_configure(p, &CFG);
    const char *keys[] = {"t:a", "t:b"};
    double t = 1000;
    kk_points_watch(p, keys, 2, t); /* first call only starts the clock */
    for (int i = 0; i < 59; i++)
        kk_points_watch(p, keys, 2, t += 1);
    long almost = kk_points_get(p, "t:a");
    kk_points_watch(p, keys, 2, t += 1);
    long minute = kk_points_get(p, "t:a");
    kk_points_watch(p, keys, 1, t += 3600); /* a stall counts as 5 s at most */
    long stalled = kk_points_get(p, "t:a");
    long b = kk_points_get(p, "t:b");
    kk_points_free(p);
    kk_users_free(u);
    unlink(path);
    CHECK_INT_EQ(almost, 5);
    CHECK_INT_EQ(minute, 15);
    CHECK_INT_EQ(stalled, 15);
    CHECK_INT_EQ(b, 15);
}

TEST(points_spend_add_and_top)
{
    kk_users *u = fresh_users();
    kk_points *p = kk_points_new(u);
    kk_points_configure(p, &CFG);
    kk_points_set(p, "t:rich", 300);
    kk_points_set(p, "t:mid", 120);
    kk_points_set(p, "demo:x", 9999); /* the fake chat never ranks */
    kk_points_set(p, "t:low", 7);
    bool ok = kk_points_spend(p, "t:mid", 20);
    bool too_much = kk_points_spend(p, "t:low", 8);
    long floor = kk_points_add(p, "t:low", -50);
    const char *keys[2];
    long values[2];
    int n = kk_points_top(p, keys, values, 2);
    char k0[32], k1[32];
    snprintf(k0, sizeof k0, "%s", n > 0 ? keys[0] : "");
    snprintf(k1, sizeof k1, "%s", n > 1 ? keys[1] : "");
    long mid = kk_points_get(p, "t:mid");
    kk_points_free(p);
    kk_users_free(u);
    unlink(path);
    CHECK(ok);
    CHECK(!too_much);
    CHECK_INT_EQ(floor, 0);
    CHECK_INT_EQ(mid, 100);
    CHECK_INT_EQ(n, 2);
    CHECK_STR_EQ(k0, "t:rich");
    CHECK_STR_EQ(k1, "t:mid");
    CHECK_INT_EQ(values[1], 100);
}

/* ---- commands that cost ---------------------------------------------------- */

static long wallet = 0;
static long charged = 0;

static long balance(void *ud, const char *key)
{
    (void)ud;
    (void)key;
    return wallet;
}

static void charge(void *ud, const char *key, long cost)
{
    (void)ud;
    (void)key;
    wallet -= cost;
    charged += cost;
}

static bool works = true;

static bool handler(const kk_cmd_call *c)
{
    (void)c;
    return works;
}

static kk_cmd_result send(kk_commands *c, const char *text, unsigned badges)
{
    kk_chat_msg m = {.platform = "t", .user_id = "a", .name = "a", .text = text,
                     .badges = badges};
    return kk_commands_handle(c, &m, "t:a", 0);
}

TEST(commands_cost_points)
{
    kk_commands *c = kk_commands_new(NULL);
    kk_commands_add(c, "jump", handler, NULL, 0, 0, KK_ROLE_ANYONE);
    kk_commands_add(c, "sound", handler, NULL, 0, 0, KK_ROLE_ANYONE);
    kk_commands_add(c, "buzina", handler, "buzina", 0, 0, KK_ROLE_ANYONE);
    kk_commands_set_cost(c, "jump", NULL, 10);
    kk_commands_set_cost(c, "sound", NULL, 3);
    kk_commands_set_cost(c, "sound", "buzina", 40);
    kk_commands_set_cost(c, "buzina", NULL, 40);
    kk_cmd_result free_before = send(c, "!jump", 0); /* no wallet yet */
    kk_commands_set_wallet(c, balance, charge, NULL);
    wallet = 15;
    charged = 0;
    kk_cmd_result paid = send(c, "!jump", 0);
    kk_cmd_result poor = send(c, "!jump", 0);
    long lacked = kk_commands_last_cost(c);
    kk_cmd_result owner = send(c, "!jump", KK_BADGE_OWNER);
    wallet = 100;
    send(c, "!sound buzina", 0); /* the sound's own price */
    long after_sound = wallet;
    send(c, "!sound outro", 0);  /* !sound's price */
    long after_other = wallet;
    works = false;
    kk_cmd_result failed = send(c, "!buzina", 0); /* did nothing: free */
    works = true;
    long after_failed = wallet;
    long jump_cost = kk_commands_cost(c, "jump");
    kk_commands_free(c);
    CHECK_INT_EQ(free_before, KK_CMD_RAN);
    CHECK_INT_EQ(paid, KK_CMD_RAN);
    CHECK_INT_EQ(poor, KK_CMD_POOR);
    CHECK_INT_EQ(lacked, 10);
    CHECK_INT_EQ(owner, KK_CMD_RAN);
    CHECK_INT_EQ(charged, 10 + 40 + 3);
    CHECK_INT_EQ(after_sound, 60);
    CHECK_INT_EQ(after_other, 57);
    CHECK_INT_EQ(failed, KK_CMD_FAILED);
    CHECK_INT_EQ(after_failed, 57);
    CHECK_INT_EQ(jump_cost, 10);
}

TEST(config_points)
{
    const char *text = "[points]\nname = moedas\nstart = 50\nwatch_minutes = 10\n"
                       "paid = 0\nmember = -3\n"
                       "[command.jump]\ncost = 25\n"
                       "[sound.buzina]\nfile = /x.ogg\ncost = 7\n"
                       "[sound.outro]\nfile = /y.ogg\n"
                       "[commands]\nshortcut_cost = 4\n";
    kk_ini *ini = kk_ini_parse(text, NULL, NULL);
    kk_config c;
    kk_config_defaults(&c);
    kk_config_apply(&c, ini, NULL, NULL);
    kk_ini_free(ini);
    kk_config_command *jump = kk_config_find_command(&c, "jump");
    kk_config_command *award = kk_config_find_command(&c, "award");
    const kk_config_sound *b = kk_config_find_sound(&c, "buzina");
    const kk_config_sound *o = kk_config_find_sound(&c, "outro");
    CHECK(c.points_enabled);
    CHECK_STR_EQ(c.points_name, "moedas");
    CHECK_INT_EQ(c.points_start, 50);
    CHECK(c.points_watch_minutes == 10);
    CHECK_INT_EQ(c.points_paid, 0);
    CHECK_INT_EQ(c.points_member, 100); /* -3 refused, the default stays */
    CHECK_INT_EQ(c.points_message, 1);
    CHECK(jump && jump->cost == 25);
    CHECK(award && award->role == KK_ROLE_MOD);
    CHECK(b && b->cost == 7);
    CHECK(o && o->cost == -1);
    CHECK_INT_EQ(c.shortcut_cost, 4);
    kk_config copy;
    CHECK(kk_config_copy(&copy, &c));
    CHECK_STR_EQ(copy.points_name, "moedas");
    CHECK(kk_config_find_command(&copy, "jump")->cost == 25);
    kk_config_free(&copy);
    kk_config_free(&c);
}

int main(void)
{
    RUN(points_earned_by_messages);
    RUN(points_for_watching);
    RUN(points_spend_add_and_top);
    RUN(commands_cost_points);
    RUN(config_points);
    return harness_report();
}
