/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <stdlib.h>
#include <string.h>

#include "../harness.h"
#include "kick.h"

#define MAX_MSGS 8
#define MAX_E 8

typedef struct {
    char user[64], name[64], text[512], amount[32], source[64];
    unsigned badges;
    kk_msg_kind kind;
    int n_emotes;
    struct {
        char id[64], name[64], url[256];
        size_t start, len;
    } e[MAX_E];
} msg_copy;

static msg_copy got[MAX_MSGS];
static int n_got;

static void collect(void *ud, const kk_chat_msg *m)
{
    (void)ud;
    if (n_got == MAX_MSGS)
        return;
    msg_copy *c = &got[n_got++];
    CHECK_STR_EQ(m->platform, "kick");
    snprintf(c->user, sizeof c->user, "%s", m->user_id);
    snprintf(c->name, sizeof c->name, "%s", m->name);
    snprintf(c->text, sizeof c->text, "%s", m->text);
    snprintf(c->amount, sizeof c->amount, "%s", m->amount ? m->amount : "");
    snprintf(c->source, sizeof c->source, "%s", m->source ? m->source : "");
    c->badges = m->badges;
    c->kind = m->kind;
    c->n_emotes = m->n_emotes;
    for (int i = 0; i < m->n_emotes && i < MAX_E; i++) {
        snprintf(c->e[i].id, sizeof c->e[i].id, "%s", m->emotes[i].id);
        snprintf(c->e[i].name, sizeof c->e[i].name, "%s", m->emotes[i].name);
        snprintf(c->e[i].url, sizeof c->e[i].url, "%s", m->emotes[i].url);
        c->e[i].start = m->emotes[i].start;
        c->e[i].len = m->emotes[i].len;
    }
}

static const kk_chat_sink sink = {collect, NULL, NULL};

static bool emote_is(const msg_copy *m, int i, const char *word)
{
    return strlen(word) == m->e[i].len && strncmp(m->text + m->e[i].start, word, m->e[i].len) == 0;
}

/* A Pusher frame: data is a JSON string holding the event's JSON. */
#define FRAME(event, channel, data)                                                         \
    "{\"event\":\"" event "\",\"channel\":\"" channel "\",\"data\":\"" data "\"}"

#define CHAT(id, type, content, sender_id, username, badges)                                \
    FRAME("App\\\\Events\\\\ChatMessageEvent", "chatrooms.111.v2",                          \
          "{\\\"id\\\":\\\"" id "\\\",\\\"chatroom_id\\\":111,\\\"content\\\":\\\"" content \
          "\\\",\\\"type\\\":\\\"" type "\\\",\\\"sender\\\":{\\\"id\\\":" sender_id        \
          ",\\\"username\\\":\\\"" username "\\\",\\\"slug\\\":\\\"x\\\",\\\"identity\\\":"  \
          "{\\\"color\\\":\\\"#FF0000\\\",\\\"badges\\\":[" badges "]}}}")

TEST(target_forms)
{
    char out[80];
    static const char *const ok[] = {
        "SomeChannel",
        "  somechannel  ",
        "kick.com/somechannel",
        "https://kick.com/SomeChannel",
        "https://www.kick.com/somechannel/videos?x=1",
        "https://kick.com/popout/somechannel/chat",
    };
    for (size_t i = 0; i < sizeof ok / sizeof ok[0]; i++) {
        CHECK_MSG(kk_kick_parse_target(ok[i], out, sizeof out), "%s", ok[i]);
        CHECK_STR_EQ(out, "somechannel");
    }
    CHECK(kk_kick_parse_target("some-channel_2", out, sizeof out));
    CHECK_STR_EQ(out, "some-channel_2");
    static const char *const bad[] = {
        "", "https://kick.com/", "notkick.com/x", "https://twitch.tv/x", "a.b", "bad!name",
    };
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++)
        CHECK_MSG(!kk_kick_parse_target(bad[i], out, sizeof out), "%s", bad[i]);
}

TEST(channel_info)
{
    long long room, chan, user;
    CHECK(kk_kick_parse_channel("{\"id\":500,\"user_id\":42,\"slug\":\"somechannel\","
                                "\"chatroom\":{\"id\":111,\"slow_mode\":false}}",
                                &room, &chan, &user));
    CHECK_INT_EQ(room, 111);
    CHECK_INT_EQ(chan, 500);
    CHECK_INT_EQ(user, 42);
    CHECK(!kk_kick_parse_channel("{\"message\":\"Not found\"}", &room, &chan, &user));
    CHECK(!kk_kick_parse_channel("<html>blocked</html>", &room, &chan, &user));
}

TEST(chat_with_emotes)
{
    n_got = 0;
    CHECK(kk_kick_emit_event(CHAT("m1", "message", "hi [emote:37226:KEKW] there[emote:9:Wave]",
                                  "1001", "Viewer_One",
                                  "{\\\"type\\\":\\\"moderator\\\",\\\"text\\\":\\\"Moderator\\\"},"
                                  "{\\\"type\\\":\\\"subscriber\\\",\\\"count\\\":3}"),
                             "kick.com/somechannel", NULL, NULL, &sink));
    CHECK_INT_EQ(n_got, 1);
    msg_copy *m = &got[0];
    CHECK_STR_EQ(m->user, "1001");
    CHECK_STR_EQ(m->name, "Viewer_One");
    CHECK_STR_EQ(m->source, "kick.com/somechannel");
    CHECK_STR_EQ(m->text, "hi KEKW thereWave");
    CHECK_INT_EQ(m->kind, KK_MSG_TEXT);
    CHECK_INT_EQ(m->badges, KK_BADGE_MOD | KK_BADGE_MEMBER);
    CHECK_INT_EQ(m->n_emotes, 2);
    CHECK(emote_is(m, 0, "KEKW"));
    CHECK_STR_EQ(m->e[0].id, "37226");
    CHECK_STR_EQ(m->e[0].url, "https://files.kick.com/emotes/37226/fullsize");
    CHECK(emote_is(m, 1, "Wave"));

    /* Replies are messages too; broken tags stay text. */
    n_got = 0;
    CHECK(kk_kick_emit_event(CHAT("m2", "reply", "[emote:x:Bad] [emote:12:] ok", "1002",
                                  "Viewer_Two", "{\\\"type\\\":\\\"broadcaster\\\"}"),
                             NULL, NULL, NULL, &sink));
    CHECK_STR_EQ(got[0].text, "[emote:x:Bad] [emote:12:] ok");
    CHECK_INT_EQ(got[0].n_emotes, 0);
    CHECK_INT_EQ(got[0].badges, KK_BADGE_OWNER);
}

TEST(seventv_words)
{
    kk_tw_emotes *extra = kk_tw_emotes_new();
    CHECK(kk_tw_emotes_load(extra, KK_TW_7TV_CHANNEL,
                            "{\"emote_set\":{\"emotes\":[{\"id\":\"s1\",\"name\":\"catJAM\","
                            "\"data\":{\"animated\":true,\"host\":{\"url\":\"//cdn.7tv.app/emote/s1\"}}}]}}") ==
          1);
    n_got = 0;
    CHECK(kk_kick_emit_event(CHAT("m3", "message", "catJAM [emote:5:catJAM] catJAMs", "1003",
                                  "Viewer_Three", ""),
                             NULL, extra, NULL, &sink));
    msg_copy *m = &got[0];
    CHECK_STR_EQ(m->text, "catJAM catJAM catJAMs");
    CHECK_INT_EQ(m->n_emotes, 2);
    /* In text order: the 7TV word first, then Kick's own. */
    CHECK_STR_EQ(m->e[0].id, "7tv:s1");
    CHECK_INT_EQ(m->e[0].start, 0);
    CHECK_STR_EQ(m->e[1].id, "5");
    CHECK_INT_EQ(m->e[1].start, 7);
    kk_tw_emotes_free(extra);
}

TEST(kicks_and_subs)
{
    kk_kick_names *names = kk_kick_names_new();
    n_got = 0;
    /* Someone chats, then subscribes: known by name. */
    CHECK(kk_kick_emit_event(CHAT("m4", "message", "hello", "2001", "Sub_Person", ""), NULL,
                             NULL, names, &sink));
    CHECK(kk_kick_emit_event(FRAME("App\\\\Events\\\\SubscriptionEvent", "chatrooms.111.v2",
                                   "{\\\"chatroom_id\\\":111,\\\"username\\\":\\\"sub_person\\\","
                                   "\\\"months\\\":2}"),
                             NULL, NULL, names, &sink));
    CHECK_INT_EQ(n_got, 2);
    CHECK_INT_EQ(got[1].kind, KK_MSG_MEMBER);
    CHECK_STR_EQ(got[1].user, "2001");
    CHECK_STR_EQ(got[1].name, "sub_person");
    /* Never chatted: nothing to tie it to. */
    CHECK(!kk_kick_emit_event(FRAME("App\\\\Events\\\\SubscriptionEvent", "chatrooms.111.v2",
                                    "{\\\"chatroom_id\\\":111,\\\"username\\\":\\\"Stranger\\\","
                                    "\\\"months\\\":1}"),
                              NULL, NULL, names, &sink));
    CHECK(kk_kick_emit_event(FRAME("App\\\\Events\\\\GiftedSubscriptionsEvent",
                                   "chatrooms.111.v2",
                                   "{\\\"chatroom_id\\\":111,\\\"gifted_usernames\\\":[\\\"a\\\"],"
                                   "\\\"gifter_username\\\":\\\"Sub_Person\\\"}"),
                             NULL, NULL, names, &sink));
    CHECK_INT_EQ(n_got, 3);
    CHECK_INT_EQ(got[2].kind, KK_MSG_MEMBER);

    /* Kicks: a paid message with its text. */
    CHECK(kk_kick_emit_event(FRAME("KicksGifted", "channel.500",
                                   "{\\\"message\\\":\\\"gg [emote:7:Clap]\\\",\\\"sender\\\":"
                                   "{\\\"id\\\":2002,\\\"username\\\":\\\"Big_Giver\\\"},"
                                   "\\\"gift\\\":{\\\"gift_id\\\":\\\"rage_quit\\\",\\\"name\\\":"
                                   "\\\"Rage Quit\\\",\\\"amount\\\":500}}"),
                             NULL, NULL, names, &sink));
    CHECK_INT_EQ(n_got, 4);
    msg_copy *m = &got[3];
    CHECK_INT_EQ(m->kind, KK_MSG_PAID);
    CHECK_STR_EQ(m->amount, "500 Kicks");
    CHECK_STR_EQ(m->user, "2002");
    CHECK_STR_EQ(m->text, "gg Clap");
    CHECK_INT_EQ(m->n_emotes, 1);
    kk_kick_names_free(names);
}

TEST(other_events)
{
    n_got = 0;
    CHECK(!kk_kick_emit_event(FRAME("App\\\\Events\\\\UserBannedEvent", "chatrooms.111.v2",
                                    "{\\\"user\\\":{\\\"id\\\":1,\\\"username\\\":\\\"x\\\"}}"),
                              NULL, NULL, NULL, &sink));
    CHECK(!kk_kick_emit_event("{\"event\":\"pusher:pong\",\"data\":\"{}\"}", NULL, NULL, NULL,
                              &sink));
    CHECK(!kk_kick_emit_event("not json", NULL, NULL, NULL, &sink));
    /* A message without a sender id. */
    CHECK(!kk_kick_emit_event(CHAT("m5", "message", "x", "0", "Nobody", ""), NULL, NULL, NULL,
                              &sink));
    CHECK_INT_EQ(n_got, 0);
}

int main(void)
{
    RUN(target_forms);
    RUN(channel_info);
    RUN(chat_with_emotes);
    RUN(seventv_words);
    RUN(kicks_and_subs);
    RUN(other_events);
    return harness_report();
}
