/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <stdlib.h>
#include <string.h>

#include "../harness.h"
#include "odysee.h"

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
    CHECK_STR_EQ(m->platform, "odysee");
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

static bool emote_is(const msg_copy *m, int i, const char *word)
{
    return strlen(word) == m->e[i].len && strncmp(m->text + m->e[i].start, word, m->e[i].len) == 0;
}

#define CLAIM "0123456789abcdef0123456789abcdef01234567"
#define USER "fedcba9876543210fedcba9876543210fedcba98"

/* A "delta" event; extra: more fields of the comment. */
#define DELTA(text, extra)                                                                   \
    "{\"type\":\"delta\",\"data\":{\"comment\":{\"channel_id\":\"" USER "\","                \
    "\"channel_name\":\"@Some_Viewer\",\"claim_id\":\"" CLAIM "\",\"comment\":\"" text "\"," \
    "\"comment_id\":\"c1\",\"currency\":\"\",\"is_fiat\":false,\"support_amount\":0,"        \
    "\"timestamp\":1700000000" extra "}}}"

TEST(target_forms)
{
    char out[512];
    CHECK_INT_EQ(kk_ody_parse_target(CLAIM, out, sizeof out), KK_ODY_TARGET_CLAIM);
    CHECK_STR_EQ(out, CLAIM);
    CHECK_INT_EQ(kk_ody_parse_target(" 0123456789ABCDEF0123456789ABCDEF01234567 ", out,
                                     sizeof out),
                 KK_ODY_TARGET_CLAIM);
    CHECK_STR_EQ(out, CLAIM);

    CHECK_INT_EQ(kk_ody_parse_target("@SomeChannel", out, sizeof out), KK_ODY_TARGET_CHANNEL);
    CHECK_STR_EQ(out, "lbry://@SomeChannel");
    CHECK_INT_EQ(kk_ody_parse_target("https://odysee.com/@SomeChannel:a?view=content", out,
                                     sizeof out),
                 KK_ODY_TARGET_CHANNEL);
    CHECK_STR_EQ(out, "lbry://@SomeChannel:a");
    CHECK_INT_EQ(kk_ody_parse_target("odysee.com/@Some-Channel:3/", out, sizeof out),
                 KK_ODY_TARGET_CHANNEL);
    CHECK_STR_EQ(out, "lbry://@Some-Channel:3");

    CHECK_INT_EQ(kk_ody_parse_target("https://odysee.com/@SomeChannel:a/live-show-ep-7:c", out,
                                     sizeof out),
                 KK_ODY_TARGET_STREAM);
    CHECK_STR_EQ(out, "lbry://@SomeChannel:a/live-show-ep-7:c");
    /* Percent-encoded names; lbry:// with "#". */
    CHECK_INT_EQ(kk_ody_parse_target("https://odysee.com/@Some:a/Bob%27s-show:1", out,
                                     sizeof out),
                 KK_ODY_TARGET_STREAM);
    CHECK_STR_EQ(out, "lbry://@Some:a/Bob's-show:1");
    CHECK_INT_EQ(kk_ody_parse_target("lbry://@SomeChannel#a/show#c", out, sizeof out),
                 KK_ODY_TARGET_STREAM);
    CHECK_STR_EQ(out, "lbry://@SomeChannel:a/show:c");
    /* A stream outside any channel. */
    CHECK_INT_EQ(kk_ody_parse_target("https://odysee.com/loose-video:5", out, sizeof out),
                 KK_ODY_TARGET_STREAM);
    CHECK_STR_EQ(out, "lbry://loose-video:5");

    static const char *const bad[] = {
        "", "SomeChannel", "@", "https://odysee.com/", "https://youtube.com/@x",
        "@a/b/c", "https://odysee.com/@a/%22x", "0123",
    };
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++)
        CHECK_MSG(kk_ody_parse_target(bad[i], out, sizeof out) == KK_ODY_TARGET_INVALID, "%s",
                  bad[i]);
}

TEST(resolve_and_live)
{
    char claim[41], channel[64];
    bool is_channel;
    CHECK(kk_ody_parse_resolve(
        "{\"jsonrpc\":\"2.0\",\"result\":{\"lbry://@a:1/b:2\":{\"claim_id\":\"" CLAIM "\","
        "\"value_type\":\"stream\",\"name\":\"b\",\"signing_channel\":{\"name\":\"@a\","
        "\"claim_id\":\"" USER "\"}}}}",
        claim, sizeof claim, channel, sizeof channel, &is_channel));
    CHECK_STR_EQ(claim, CLAIM);
    CHECK_STR_EQ(channel, "@a");
    CHECK(!is_channel);
    CHECK(kk_ody_parse_resolve("{\"result\":{\"lbry://@a\":{\"claim_id\":\"" USER "\","
                               "\"value_type\":\"channel\",\"name\":\"@a\"}}}",
                               claim, sizeof claim, channel, sizeof channel, &is_channel));
    CHECK(is_channel);
    CHECK_STR_EQ(claim, USER);
    CHECK_STR_EQ(channel, "@a");
    CHECK(!kk_ody_parse_resolve("{\"result\":{\"lbry://@a\":{\"error\":{\"name\":\"NOT_FOUND\"}}}}",
                                claim, sizeof claim, channel, sizeof channel, &is_channel));

    bool live;
    CHECK(kk_ody_parse_is_live("{\"success\":true,\"data\":{\"Live\":true,\"ActiveClaim\":"
                               "{\"ClaimID\":\"" CLAIM "\"}}}",
                               &live, claim, sizeof claim));
    CHECK(live);
    CHECK_STR_EQ(claim, CLAIM);
    CHECK(kk_ody_parse_is_live("{\"success\":true,\"data\":{\"Live\":false,\"ActiveClaim\":"
                               "{\"ClaimID\":\"\"}}}",
                               &live, claim, sizeof claim));
    CHECK(!live);
    CHECK_STR_EQ(claim, "");
    CHECK(!kk_ody_parse_is_live("{\"success\":false,\"error\":\"x\",\"data\":null}", &live,
                                claim, sizeof claim));
}

TEST(comment_text)
{
    n_got = 0;
    CHECK(kk_ody_emit_event(DELTA("hi @Other_One:7 :space_doge: ok :sweat_smile: :nope: 12:30",
                                  ",\"is_moderator\":true"),
                            "@SomeChannel", &sink));
    CHECK_INT_EQ(n_got, 1);
    msg_copy *m = &got[0];
    CHECK_STR_EQ(m->user, USER);
    CHECK_STR_EQ(m->name, "@Some_Viewer");
    CHECK_STR_EQ(m->text, "hi @Other_One :space_doge: ok \xF0\x9F\x98\x85 :nope: 12:30");
    CHECK_INT_EQ(m->badges, KK_BADGE_MOD);
    CHECK_INT_EQ(m->kind, KK_MSG_TEXT);
    CHECK_INT_EQ(m->n_emotes, 1);
    CHECK(emote_is(m, 0, ":space_doge:"));
    CHECK(strncmp(m->e[0].url, "https://static.odycdn.com/emoticons/48%20px/", 44) == 0);

    /* A sticker with a tip, from the creator. */
    n_got = 0;
    CHECK(kk_ody_emit_event(DELTA("<stkr>:BIG_TIP:<stkr>", ",\"is_creator\":true"), NULL, &sink));
    m = &got[0];
    CHECK_STR_EQ(m->text, ":BIG_TIP:");
    CHECK_INT_EQ(m->n_emotes, 1);
    CHECK_STR_EQ(m->e[0].id, "stkr:BIG_TIP:");
    CHECK(strncmp(m->e[0].url, "https://static.odycdn.com/stickers/", 35) == 0);
    CHECK_INT_EQ(m->badges, KK_BADGE_OWNER);
    /* An unknown sticker stays text. */
    n_got = 0;
    CHECK(kk_ody_emit_event(DELTA("<stkr>:NOT_ONE:<stkr>", ""), NULL, &sink));
    CHECK_STR_EQ(got[0].text, "<stkr>:NOT_ONE:<stkr>");
    CHECK_INT_EQ(got[0].n_emotes, 0);
}

TEST(tips)
{
    n_got = 0;
    /* support_amount comes before the extra fields: later keys win. */
    CHECK(kk_ody_emit_event("{\"type\":\"delta\",\"data\":{\"comment\":{\"channel_id\":\"" USER
                            "\",\"channel_name\":\"@Tipper\",\"comment\":\"thanks\","
                            "\"support_amount\":2.5,\"is_fiat\":false}}}",
                            NULL, &sink));
    CHECK_INT_EQ(got[0].kind, KK_MSG_PAID);
    CHECK_STR_EQ(got[0].amount, "2.5 LBC");
    CHECK(kk_ody_emit_event("{\"type\":\"delta\",\"data\":{\"comment\":{\"channel_id\":\"" USER
                            "\",\"channel_name\":\"@Tipper\",\"comment\":\"\","
                            "\"support_amount\":5,\"is_fiat\":true,\"currency\":\"USD\"}}}",
                            NULL, &sink));
    CHECK_STR_EQ(got[1].amount, "USD 5.00");
}

TEST(other_events)
{
    n_got = 0;
    CHECK(!kk_ody_emit_event("{\"type\":\"viewers\",\"data\":{\"connected\":11}}", NULL, &sink));
    CHECK(!kk_ody_emit_event("{\"type\":\"removed\",\"data\":{\"comment\":{\"comment_id\":"
                             "\"c1\"}}}",
                             NULL, &sink));
    CHECK(!kk_ody_emit_event("{\"type\":\"delta\",\"data\":{\"comment\":{\"comment\":\"x\"}}}",
                             NULL, &sink));
    CHECK(!kk_ody_emit_event("[]", NULL, &sink));
    CHECK_INT_EQ(n_got, 0);
}

int main(void)
{
    RUN(target_forms);
    RUN(resolve_and_live);
    RUN(comment_text);
    RUN(tips);
    RUN(other_events);
    return harness_report();
}
