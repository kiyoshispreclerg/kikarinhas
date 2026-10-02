/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <stdlib.h>
#include <string.h>

#include "../harness.h"
#include "twitch.h"

#define MAX_MSGS 8
#define MAX_E 8

typedef struct {
    char user[64], name[64], text[512], amount[32];
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
    CHECK_STR_EQ(m->platform, "twitch");
    snprintf(c->user, sizeof c->user, "%s", m->user_id);
    snprintf(c->name, sizeof c->name, "%s", m->name);
    snprintf(c->text, sizeof c->text, "%s", m->text);
    snprintf(c->amount, sizeof c->amount, "%s", m->amount ? m->amount : "");
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

/* Feeds a copy (the parser writes into the line). */
static bool feed(const char *line, const kk_tw_emotes *extra)
{
    char buf[4096];
    snprintf(buf, sizeof buf, "%s", line);
    return kk_tw_emit_line(buf, extra, &sink);
}

/* The text an emote stands for. */
static bool emote_is(const msg_copy *m, int i, const char *word)
{
    return strlen(word) == m->e[i].len && strncmp(m->text + m->e[i].start, word, m->e[i].len) == 0;
}

/* ---- targets ------------------------------------------------------------- */

TEST(target_forms)
{
    char out[32];
    static const char *const ok[] = {
        "SomeChannel",
        "#somechannel",
        "  somechannel  ",
        "twitch.tv/somechannel",
        "https://www.twitch.tv/SomeChannel",
        "https://www.twitch.tv/somechannel/videos?filter=all",
        "https://m.twitch.tv/somechannel",
        "https://www.twitch.tv/popout/somechannel/chat?popout=",
    };
    for (size_t i = 0; i < sizeof ok / sizeof ok[0]; i++) {
        CHECK_MSG(kk_tw_parse_target(ok[i], out, sizeof out), "%s", ok[i]);
        CHECK_STR_EQ(out, "somechannel");
    }
    static const char *const bad[] = {
        "", "   ", "https://www.youtube.com/@x", "twitch.tv/", "a-b",
        "name.with.dots", "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
    };
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++)
        CHECK_MSG(!kk_tw_parse_target(bad[i], out, sizeof out), "%s", bad[i]);
}

/* ---- lines --------------------------------------------------------------- */

TEST(split_lines)
{
    char line[] = "@a=1;b=x\\sy :nick!nick@nick.tmi.twitch.tv PRIVMSG #chan :hello: world\r\n";
    kk_tw_line l;
    CHECK(kk_tw_split(line, &l));
    CHECK_STR_EQ(l.tags, "a=1;b=x\\sy");
    CHECK_STR_EQ(l.nick, "nick");
    CHECK_STR_EQ(l.command, "PRIVMSG");
    CHECK_STR_EQ(l.channel, "#chan");
    CHECK_STR_EQ(l.trailing, "hello: world");

    char ping[] = "PING :tmi.twitch.tv";
    CHECK(kk_tw_split(ping, &l));
    CHECK(l.tags == NULL);
    CHECK_STR_EQ(l.command, "PING");
    CHECK_STR_EQ(l.trailing, "tmi.twitch.tv");

    char numeric[] = ":tmi.twitch.tv 001 justinfan12345 :Welcome, GLHF!";
    CHECK(kk_tw_split(numeric, &l));
    CHECK_STR_EQ(l.command, "001");
    CHECK_STR_EQ(l.channel, "");

    char empty[] = "@x=1 :prefix";
    CHECK(!kk_tw_split(empty, &l));
}

TEST(tags)
{
    const char *tags = "badges=;display-name=Some\\sOne;system-msg=a\\:b\\\\c;flag;x=";
    char v[64];
    CHECK(kk_tw_tag(tags, "display-name", v, sizeof v));
    CHECK_STR_EQ(v, "Some One");
    CHECK(kk_tw_tag(tags, "system-msg", v, sizeof v));
    CHECK_STR_EQ(v, "a;b\\c");
    CHECK(kk_tw_tag(tags, "flag", v, sizeof v));
    CHECK_STR_EQ(v, "");
    CHECK(kk_tw_tag(tags, "badges", v, sizeof v));
    CHECK_STR_EQ(v, "");
    CHECK(!kk_tw_tag(tags, "display", v, sizeof v));
    CHECK(!kk_tw_tag(tags, "missing", v, sizeof v));

    /* Cut inside "ção": no half character left. */
    char small[5];
    CHECK(kk_tw_tag("m=abção", "m", small, sizeof small));
    CHECK_STR_EQ(small, "abç");
}

/* ---- messages ------------------------------------------------------------ */

TEST(privmsg_with_emotes)
{
    n_got = 0;
    /* "olá Kappa ❤ Kappa PogChamp": positions count code points, so the
     * second Kappa starts at character 12 but byte 15. */
    CHECK(feed("@badge-info=subscriber/14;badges=moderator/1,subscriber/12;color=#FF0000;"
               "display-name=Fulana;emotes=25:4-8,12-16/88:18-25;id=abc;mod=1;"
               "room-id=1000;user-id=4242;user-type=mod "
               ":fulana!fulana@fulana.tmi.twitch.tv PRIVMSG #canal :olá Kappa ❤ Kappa PogChamp",
               NULL));
    CHECK_INT_EQ(n_got, 1);
    const msg_copy *m = &got[0];
    CHECK_STR_EQ(m->user, "4242");
    CHECK_STR_EQ(m->name, "Fulana");
    CHECK_STR_EQ(m->text, "olá Kappa ❤ Kappa PogChamp");
    CHECK_INT_EQ(m->kind, KK_MSG_TEXT);
    CHECK_INT_EQ(m->badges, KK_BADGE_MOD | KK_BADGE_MEMBER);
    CHECK_INT_EQ(m->n_emotes, 3);
    CHECK(emote_is(m, 0, "Kappa"));
    CHECK(emote_is(m, 1, "Kappa"));
    CHECK_INT_EQ(m->e[1].start, 15);
    CHECK(emote_is(m, 2, "PogChamp"));
    CHECK_STR_EQ(m->e[0].id, "25");
    CHECK_STR_EQ(m->e[0].name, "Kappa");
    CHECK_STR_EQ(m->e[0].url, "https://static-cdn.jtvnw.net/emoticons/v2/25/static/dark/2.0");
    CHECK_STR_EQ(m->e[2].id, "88");
}

TEST(privmsg_name_fallbacks_and_action)
{
    n_got = 0;
    /* No display name: the nick. /me: the inner text, emotes counted
     * from it. */
    CHECK(feed("@badges=broadcaster/1,partner/1;display-name=;emotes=25:6-10;user-id=7 "
               ":dono!dono@dono.tmi.twitch.tv PRIVMSG #dono :\001ACTION acena Kappa\001",
               NULL));
    CHECK_INT_EQ(n_got, 1);
    CHECK_STR_EQ(got[0].name, "dono");
    CHECK_STR_EQ(got[0].text, "acena Kappa");
    CHECK_INT_EQ(got[0].badges, KK_BADGE_OWNER | KK_BADGE_VERIFIED);
    CHECK_INT_EQ(got[0].n_emotes, 1);
    CHECK(emote_is(&got[0], 0, "Kappa"));

    /* Without a user id it is nobody's message. */
    CHECK(!feed("@display-name=X :x!x@x PRIVMSG #c :oi", NULL));
    /* Other commands are not messages. */
    CHECK(!feed("@room-id=1 :tmi.twitch.tv ROOMSTATE #c", NULL));
    CHECK(!feed("PING :tmi.twitch.tv", NULL));
    CHECK_INT_EQ(n_got, 1);
}

TEST(paid_messages)
{
    n_got = 0;
    CHECK(feed("@bits=100;display-name=Doador;user-id=11 :d!d@d PRIVMSG #c :Cheer100 valeu",
               NULL));
    CHECK(feed("@display-name=Hype;pinned-chat-paid-amount=500;pinned-chat-paid-currency=USD;"
               "pinned-chat-paid-exponent=2;user-id=12 :h!h@h PRIVMSG #c :hype!",
               NULL));
    CHECK(feed("@display-name=Yen;pinned-chat-paid-amount=1000;pinned-chat-paid-currency=JPY;"
               "pinned-chat-paid-exponent=0;user-id=13 :y!y@y PRIVMSG #c :arigato",
               NULL));
    CHECK_INT_EQ(n_got, 3);
    CHECK_INT_EQ(got[0].kind, KK_MSG_PAID);
    CHECK_STR_EQ(got[0].amount, "100 bits");
    CHECK_STR_EQ(got[1].amount, "USD 5.00");
    CHECK_STR_EQ(got[2].amount, "JPY 1000");
}

TEST(usernotices)
{
    n_got = 0;
    /* Resub with a message: the message, with its emotes. */
    CHECK(feed("@badges=subscriber/6;display-name=Fiel;emotes=25:0-4;login=fiel;msg-id=resub;"
               "system-msg=Fiel\\ssubscribed\\sfor\\s6\\smonths!;user-id=21 "
               ":tmi.twitch.tv USERNOTICE #c :Kappa 6 meses",
               NULL));
    /* Sub without one: Twitch's sentence, no emotes from the tag. */
    CHECK(feed("@display-name=Novo;emotes=25:0-4;login=novo;msg-id=sub;"
               "system-msg=Novo\\ssubscribed\\sat\\sTier\\s1.;user-id=22 "
               ":tmi.twitch.tv USERNOTICE #c",
               NULL));
    CHECK(feed("@display-name=Invasor;login=invasor;msg-id=raid;msg-param-viewerCount=50;"
               "system-msg=50\\sraiders\\sfrom\\sInvasor\\shave\\sjoined!;user-id=23 "
               ":tmi.twitch.tv USERNOTICE #c",
               NULL));
    /* Not a person showing up: ignored. */
    CHECK(!feed("@display-name=X;login=x;msg-id=bitsbadgetier;user-id=24 "
                ":tmi.twitch.tv USERNOTICE #c",
                NULL));
    CHECK_INT_EQ(n_got, 3);
    CHECK_INT_EQ(got[0].kind, KK_MSG_MEMBER);
    CHECK_STR_EQ(got[0].text, "Kappa 6 meses");
    CHECK_INT_EQ(got[0].n_emotes, 1);
    CHECK_INT_EQ(got[1].kind, KK_MSG_MEMBER);
    CHECK_STR_EQ(got[1].text, "Novo subscribed at Tier 1.");
    CHECK_INT_EQ(got[1].n_emotes, 0);
    CHECK_INT_EQ(got[2].kind, KK_MSG_TEXT);
    CHECK_STR_EQ(got[2].name, "Invasor");
    CHECK_STR_EQ(got[2].text, "50 raiders from Invasor have joined!");
}

/* ---- third-party emotes -------------------------------------------------- */

static const char bttv_global[] =
    "[{\"id\":\"b1\",\"code\":\"catJAM\",\"imageType\":\"gif\",\"animated\":true},"
    "{\"id\":\"b2\",\"code\":\"Clap\",\"imageType\":\"png\",\"animated\":false}]";
static const char bttv_channel[] =
    "{\"id\":\"u\",\"channelEmotes\":[{\"id\":\"b3\",\"code\":\"meuEmote\"}],"
    "\"sharedEmotes\":[{\"id\":\"b4\",\"code\":\"Shared\"}]}";
static const char ffz_global[] =
    "{\"default_sets\":[3],\"sets\":{\"3\":{\"id\":3,\"emoticons\":["
    "{\"id\":9,\"name\":\"Clap\",\"urls\":{\"1\":\"https://cdn.frankerfacez.com/emote/9/1\","
    "\"2\":\"https://cdn.frankerfacez.com/emote/9/2\"}}]},"
    "\"4\":{\"id\":4,\"emoticons\":[{\"id\":10,\"name\":\"NotDefault\",\"urls\":{\"1\":\"x\"}}]}}}";
static const char ffz_channel[] =
    "{\"room\":{\"set\":77},\"sets\":{\"77\":{\"emoticons\":["
    "{\"id\":70,\"name\":\"ffzOnly\",\"urls\":{\"1\":\"//cdn.frankerfacez.com/emote/70/1\"}}]}}}";
static const char seventv_channel[] =
    "{\"id\":\"1000\",\"emote_set\":{\"emotes\":["
    "{\"id\":\"s1\",\"name\":\"catJAM\",\"data\":{\"animated\":true,"
    "\"host\":{\"url\":\"//cdn.7tv.app/emote/s1\"}}},"
    "{\"id\":\"s2\",\"name\":\"peepoHi\",\"data\":{\"animated\":false,"
    "\"host\":{\"url\":\"//cdn.7tv.app/emote/s2\"}}}]}}";

TEST(extra_emote_lists)
{
    kk_tw_emotes *set = kk_tw_emotes_new();
    CHECK(set);
    CHECK_INT_EQ(kk_tw_emotes_load(set, KK_TW_BTTV_GLOBAL, bttv_global), 2);
    CHECK_INT_EQ(kk_tw_emotes_load(set, KK_TW_BTTV_CHANNEL, bttv_channel), 2);
    CHECK_INT_EQ(kk_tw_emotes_load(set, KK_TW_FFZ_GLOBAL, ffz_global), 1);
    CHECK_INT_EQ(kk_tw_emotes_load(set, KK_TW_FFZ_CHANNEL, ffz_channel), 1);
    CHECK_INT_EQ(kk_tw_emotes_load(set, KK_TW_7TV_CHANNEL, seventv_channel), 2);
    CHECK_INT_EQ(kk_tw_emotes_load(set, KK_TW_7TV_GLOBAL, "{\"oops\":1}"), -1);
    CHECK_INT_EQ(kk_tw_emotes_load(set, KK_TW_7TV_GLOBAL, "not json"), -1);
    /* A user without a 7TV set. */
    CHECK_INT_EQ(kk_tw_emotes_load(set, KK_TW_7TV_GLOBAL, "{\"emotes\":[]}"), 0);
    CHECK_INT_EQ(kk_tw_emotes_count(set), 8);

    /* The channel's 7TV beats the global BTTV; BTTV beats FFZ. */
    const kk_tw_emote *e = kk_tw_emotes_find(set, "catJAM", 6);
    CHECK(e);
    CHECK_STR_EQ(e->id, "7tv:s1");
    CHECK_STR_EQ(e->url, "https://cdn.7tv.app/emote/s1/2x_static.png");
    e = kk_tw_emotes_find(set, "Clap", 4);
    CHECK(e);
    CHECK_STR_EQ(e->id, "bttv:b2");
    CHECK_STR_EQ(e->url, "https://cdn.betterttv.net/emote/b2/2x.png");
    e = kk_tw_emotes_find(set, "peepoHi", 7);
    CHECK(e && strcmp(e->url, "https://cdn.7tv.app/emote/s2/2x.png") == 0);
    e = kk_tw_emotes_find(set, "ffzOnly", 7);
    CHECK(e && strcmp(e->url, "https://cdn.frankerfacez.com/emote/70/1") == 0);
    CHECK(kk_tw_emotes_find(set, "NotDefault", 10) == NULL);
    CHECK(kk_tw_emotes_find(set, "clap", 4) == NULL); /* case matters */
    CHECK(kk_tw_emotes_find(set, "Cla", 3) == NULL);
    CHECK(kk_tw_emotes_find(set, "Claps", 5) == NULL);

    /* Reloading a source replaces it; clearing drops it. */
    CHECK_INT_EQ(kk_tw_emotes_load(set, KK_TW_7TV_CHANNEL, seventv_channel), 2);
    CHECK_INT_EQ(kk_tw_emotes_count(set), 8);
    kk_tw_emotes_clear(set, KK_TW_7TV_CHANNEL);
    e = kk_tw_emotes_find(set, "catJAM", 6);
    CHECK(e && strcmp(e->id, "bttv:b1") == 0);

    /* In a message: whole words only, after the Twitch ones, in text
     * order, never on top of a Twitch emote. */
    kk_tw_emotes_load(set, KK_TW_7TV_CHANNEL, seventv_channel);
    n_got = 0;
    CHECK(feed("@display-name=A;emotes=25:13-17;user-id=1 :a!a@a PRIVMSG #c "
               ":peepoHi Clap Kappa catJAMs catJAM",
               set));
    CHECK_INT_EQ(n_got, 1);
    const msg_copy *m = &got[0];
    CHECK_INT_EQ(m->n_emotes, 4);
    CHECK(emote_is(m, 0, "peepoHi"));
    CHECK(emote_is(m, 1, "Clap"));
    CHECK(emote_is(m, 2, "Kappa"));
    CHECK_STR_EQ(m->e[2].id, "25");
    CHECK(emote_is(m, 3, "catJAM"));
    CHECK_STR_EQ(m->e[3].id, "7tv:s1");
    CHECK_INT_EQ(m->e[3].start, 27);
    kk_tw_emotes_free(set);
}

int main(void)
{
    RUN(target_forms);
    RUN(split_lines);
    RUN(tags);
    RUN(privmsg_with_emotes);
    RUN(privmsg_name_fallbacks_and_action);
    RUN(paid_messages);
    RUN(usernotices);
    RUN(extra_emote_lists);
    return harness_report();
}
