/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <stdlib.h>
#include <string.h>

#include "../harness.h"
#include "json.h"
#include "util.h"
#include "youtube.h"

#define MAX_MSGS 16

typedef struct {
    char platform[16], user[64], name[64], text[512], amount[32];
    unsigned badges;
    kk_msg_kind kind;
    int n_emotes;
    char emote_id[64], emote_name[64], emote_url[256];
    size_t emote_start, emote_len;
} msg_copy;

static msg_copy got[MAX_MSGS];
static int n_got;

static void collect(void *ud, const kk_chat_msg *m)
{
    (void)ud;
    if (n_got == MAX_MSGS)
        return;
    msg_copy *c = &got[n_got++];
    snprintf(c->platform, sizeof c->platform, "%s", m->platform);
    snprintf(c->user, sizeof c->user, "%s", m->user_id);
    snprintf(c->name, sizeof c->name, "%s", m->name);
    snprintf(c->text, sizeof c->text, "%s", m->text);
    snprintf(c->amount, sizeof c->amount, "%s", m->amount ? m->amount : "");
    c->badges = m->badges;
    c->kind = m->kind;
    c->n_emotes = m->n_emotes;
    if (m->n_emotes > 0) {
        const kk_emote *e = &m->emotes[0];
        snprintf(c->emote_id, sizeof c->emote_id, "%s", e->id);
        snprintf(c->emote_name, sizeof c->emote_name, "%s", e->name ? e->name : "");
        snprintf(c->emote_url, sizeof c->emote_url, "%s", e->url);
        c->emote_start = e->start;
        c->emote_len = e->len;
    }
}

#define MAX_REACTIONS 8

static struct {
    char emoji[16];
    int count;
    double delay;
} reactions[MAX_REACTIONS];
static int n_reactions;

static void collect_reaction(void *ud, const kk_reaction *r)
{
    (void)ud;
    if (n_reactions == MAX_REACTIONS)
        return;
    snprintf(reactions[n_reactions].emoji, sizeof reactions[0].emoji, "%s", r->emote.text);
    reactions[n_reactions].count = r->count;
    reactions[n_reactions].delay = r->delay;
    n_reactions++;
}

static const kk_chat_sink sink = {collect, collect_reaction, NULL};

static char *fixture(const char *name)
{
    char path[KK_PATH_MAX];
    snprintf(path, sizeof path, "%s/%s", KK_FIXTURES_DIR, name);
    char *s = kk_read_file(path, NULL);
    if (!s) {
        fprintf(stderr, "missing fixture %s\n", path);
        exit(2);
    }
    return s;
}

/* ---- targets ------------------------------------------------------------- */

static kk_yt_target target(const char *in, char *out)
{
    return kk_yt_parse_target(in, out, 512);
}

TEST(target_video_forms)
{
    char out[512];
    CHECK_INT_EQ(target("Abc-123_xyZ", out), KK_YT_TARGET_VIDEO);
    CHECK_STR_EQ(out, "Abc-123_xyZ");
    CHECK_INT_EQ(target("https://www.youtube.com/watch?v=Abc-123_xyZ&t=10", out),
                 KK_YT_TARGET_VIDEO);
    CHECK_STR_EQ(out, "Abc-123_xyZ");
    CHECK_INT_EQ(target("youtube.com/watch?feature=x&v=Abc-123_xyZ", out),
                 KK_YT_TARGET_VIDEO);
    CHECK_STR_EQ(out, "Abc-123_xyZ");
    CHECK_INT_EQ(target("https://youtu.be/Abc-123_xyZ?si=foo", out),
                 KK_YT_TARGET_VIDEO);
    CHECK_STR_EQ(out, "Abc-123_xyZ");
    CHECK_INT_EQ(target("https://www.youtube.com/live/Abc-123_xyZ?feature=share", out),
                 KK_YT_TARGET_VIDEO);
    CHECK_STR_EQ(out, "Abc-123_xyZ");
    CHECK_INT_EQ(target("  Abc-123_xyZ\n", out), KK_YT_TARGET_VIDEO);
}

TEST(target_channel_forms)
{
    char out[512];
    CHECK_INT_EQ(target("@LofiGirl", out), KK_YT_TARGET_CHANNEL);
    CHECK_STR_EQ(out, "https://www.youtube.com/@LofiGirl/live");
    CHECK_INT_EQ(target("https://www.youtube.com/@LofiGirl/streams", out),
                 KK_YT_TARGET_CHANNEL);
    CHECK_STR_EQ(out, "https://www.youtube.com/@LofiGirl/live");
    CHECK_INT_EQ(target("https://youtube.com/@canal?si=1", out), KK_YT_TARGET_CHANNEL);
    CHECK_STR_EQ(out, "https://www.youtube.com/@canal/live");
    CHECK_INT_EQ(target("UCSJ4gkVC6NrvII8umztf0Ow", out), KK_YT_TARGET_CHANNEL);
    CHECK_STR_EQ(out, "https://www.youtube.com/channel/UCSJ4gkVC6NrvII8umztf0Ow/live");
    CHECK_INT_EQ(target("https://www.youtube.com/channel/UCSJ4gkVC6NrvII8umztf0Ow/featured", out),
                 KK_YT_TARGET_CHANNEL);
    CHECK_STR_EQ(out, "https://www.youtube.com/channel/UCSJ4gkVC6NrvII8umztf0Ow/live");
    CHECK_INT_EQ(target("https://www.youtube.com/c/Nome", out), KK_YT_TARGET_CHANNEL);
    CHECK_STR_EQ(out, "https://www.youtube.com/c/Nome/live");
}

TEST(target_invalid)
{
    char out[512];
    CHECK_INT_EQ(target("", out), KK_YT_TARGET_INVALID);
    CHECK_INT_EQ(target("abc", out), KK_YT_TARGET_INVALID);
    CHECK_INT_EQ(target("https://example.com/watch?v=Abc-123_xyZ", out),
                 KK_YT_TARGET_INVALID);
    CHECK_INT_EQ(target("https://www.youtube.com/watch?v=short", out),
                 KK_YT_TARGET_INVALID);
    CHECK_INT_EQ(target("https://www.youtube.com/channel", out), KK_YT_TARGET_INVALID);
    CHECK_INT_EQ(target("https://www.youtube.com/results?search_query=x", out),
                 KK_YT_TARGET_INVALID);
}

/* ---- pages --------------------------------------------------------------- */

TEST(canonical_video)
{
    char id[12];
    char *live = fixture("yt_channel_live.html");
    char *off = fixture("yt_channel_offline.html");
    bool found_live = kk_yt_canonical_video(live, id);
    bool found_off = kk_yt_canonical_video(off, id);
    free(live);
    free(off);
    CHECK(found_live);
    CHECK(!found_off);
}

TEST(chat_page)
{
    char *html = fixture("yt_chat_page.html");
    char ver[64];
    bool has_ver = kk_yt_config_string(html, "INNERTUBE_CLIENT_VERSION", ver, sizeof ver);
    cJSON *initial = kk_yt_initial_data(html);
    char *cont = initial ? kk_yt_live_continuation(initial) : NULL;
    cJSON_Delete(initial);
    free(html);

    CHECK(has_ver);
    CHECK_STR_EQ(ver, "2.20990101.00.00");
    CHECK(cont != NULL);
    /* The "Live chat" view, not "Top chat". */
    CHECK_STR_EQ(cont, "TOKEN_LIVE");
    free(cont);
}

TEST(chat_page_without_chat)
{
    cJSON *initial = kk_yt_initial_data("<script>var ytInitialData = {\"contents\":{}};</script>");
    CHECK(initial != NULL);
    char *cont = kk_yt_live_continuation(initial);
    cJSON_Delete(initial);
    CHECK(cont == NULL);
    CHECK(kk_yt_initial_data("<html>nothing</html>") == NULL);
}

/* ---- polling ------------------------------------------------------------- */

TEST(poll_messages)
{
    char *json = fixture("yt_poll.json");
    char *next = NULL;
    int delay = 0;
    n_got = n_reactions = 0;
    kk_yt_poll r = kk_yt_parse_poll(json, true, &sink, &next, &delay);
    free(json);

    CHECK_INT_EQ(r, KK_YT_POLL_OK);
    CHECK_STR_EQ(next, "TOKEN_NEXT");
    free(next);
    /* Invalidation tokens are polled on our own schedule. */
    CHECK_INT_EQ(delay, 2500);
    /* Placeholder, engagement notice and removal are not messages. */
    CHECK_INT_EQ(n_got, 6);

    CHECK_STR_EQ(got[0].platform, "youtube");
    CHECK_STR_EQ(got[0].user, "UCaaaaaaaaaaaaaaaaaaaaaa");
    CHECK_STR_EQ(got[0].name, "Fulana");
    CHECK_STR_EQ(got[0].text, "oi chat 😂:hand-pink-waving:");
    CHECK_INT_EQ(got[0].kind, KK_MSG_TEXT);
    CHECK_INT_EQ(got[0].badges, 0);
    /* Only the channel emoji is an image emote; 😂 stays in the text. */
    CHECK_INT_EQ(got[0].n_emotes, 1);
    CHECK_STR_EQ(got[0].emote_id, "UCxx/abc");
    CHECK_STR_EQ(got[0].emote_name, ":hand-pink-waving:");
    CHECK_STR_EQ(got[0].emote_url, "https://example.invalid/c.png");
    CHECK_INT_EQ(got[0].emote_start, strlen("oi chat 😂"));
    CHECK_INT_EQ(got[0].emote_len, strlen(":hand-pink-waving:"));
    CHECK_INT_EQ(got[1].n_emotes, 0);

    CHECK_INT_EQ(got[1].badges, KK_BADGE_OWNER);
    CHECK_INT_EQ(got[2].badges, KK_BADGE_MOD | KK_BADGE_MEMBER);

    CHECK_STR_EQ(got[3].name, "Segurada");
    CHECK_STR_EQ(got[3].text, "liberada");

    CHECK_INT_EQ(got[4].kind, KK_MSG_PAID);
    CHECK_STR_EQ(got[4].amount, "R$ 10,00");
    CHECK_STR_EQ(got[4].text, "cafezinho");

    CHECK_INT_EQ(got[5].kind, KK_MSG_MEMBER);
    CHECK_STR_EQ(got[5].text, "Boas-vindas a Canal!");
    CHECK_INT_EQ(got[5].badges, KK_BADGE_MEMBER);

    /* Three one-second buckets (the middle one empty), a second apart. */
    CHECK_INT_EQ(n_reactions, 3);
    CHECK_STR_EQ(reactions[0].emoji, "❤");
    CHECK_INT_EQ(reactions[0].count, 3);
    CHECK(reactions[0].delay == 0.0);
    CHECK_STR_EQ(reactions[1].emoji, "💯");
    CHECK_INT_EQ(reactions[1].count, 1);
    CHECK(reactions[1].delay == 2.0);
    CHECK_STR_EQ(reactions[2].emoji, "❤");
    CHECK_INT_EQ(reactions[2].count, 2);
    CHECK(reactions[2].delay == 2.0);
}

TEST(emote_url_size)
{
    char out[256];
    CHECK(kk_yt_emote_url("https://yt3.ggpht.com/abc-_x=w48-h48-c-k-nd", 96, out, sizeof out));
    CHECK_STR_EQ(out, "https://yt3.ggpht.com/abc-_x=w96-h96-c-k-nd");
    CHECK(kk_yt_emote_url("https://yt3.ggpht.com/abc=w24-h24", 96, out, sizeof out));
    CHECK_STR_EQ(out, "https://yt3.ggpht.com/abc=w96-h96");
    /* Anything else is left alone. */
    CHECK(kk_yt_emote_url("https://example.invalid/c.png", 96, out, sizeof out));
    CHECK_STR_EQ(out, "https://example.invalid/c.png");
    CHECK(kk_yt_emote_url("https://x.invalid/a?q=w1-h1/b.png", 96, out, sizeof out));
    CHECK_STR_EQ(out, "https://x.invalid/a?q=w1-h1/b.png");
    CHECK(!kk_yt_emote_url("https://example.invalid/c.png", 96, out, 8));
}

TEST(poll_skip_backlog)
{
    char *json = fixture("yt_poll.json");
    char *next = NULL;
    int delay = 0;
    n_got = n_reactions = 0;
    kk_yt_poll r = kk_yt_parse_poll(json, false, &sink, &next, &delay);
    free(json);
    free(next);
    CHECK_INT_EQ(r, KK_YT_POLL_OK);
    CHECK_INT_EQ(n_got, 0);
    CHECK_INT_EQ(n_reactions, 0);
}

TEST(poll_timed_and_ended)
{
    char *timed = fixture("yt_poll_timed.json");
    char *ended = fixture("yt_poll_ended.json");
    char *next = NULL;
    int delay = 0;
    kk_yt_poll r1 = kk_yt_parse_poll(timed, true, &sink, &next, &delay);
    char *next1 = next;
    kk_yt_poll r2 = kk_yt_parse_poll(ended, true, &sink, &next, &delay);
    kk_yt_poll r3 = kk_yt_parse_poll("{not json", true, &sink, &next, &delay);
    free(timed);
    free(ended);

    CHECK_INT_EQ(r1, KK_YT_POLL_OK);
    CHECK_STR_EQ(next1, "TOKEN_TIMED");
    free(next1);
    CHECK_INT_EQ(delay, 6000);
    CHECK_INT_EQ(r2, KK_YT_POLL_ENDED);
    CHECK_INT_EQ(r3, KK_YT_POLL_ERROR);
}

/* A message longer than the text buffer is cut on a UTF-8 boundary. */
TEST(long_text_is_cut_cleanly)
{
    char big[1200] = "{\"actions\":[{\"addChatItemAction\":{\"item\":{"
                     "\"liveChatTextMessageRenderer\":{\"message\":{\"runs\":[{\"text\":\"";
    size_t n = strlen(big);
    for (int i = 0; i < 300; i++) { /* 600 bytes of "é" */
        big[n++] = (char)0xC3;
        big[n++] = (char)0xA9;
    }
    snprintf(big + n, sizeof big - n,
             "\"}]},\"authorName\":{\"simpleText\":\"x\"},"
             "\"authorExternalChannelId\":\"UC1\"}}}}]}");
    cJSON *root = cJSON_Parse(big);
    CHECK(root != NULL);
    n_got = 0;
    kk_yt_emit_actions(cJSON_GetObjectItem(root, "actions"), &sink);
    cJSON_Delete(root);
    CHECK_INT_EQ(n_got, 1);
    size_t len = strlen(got[0].text);
    CHECK(len > 0);
    /* Last byte must end a sequence: "é" is C3 A9. */
    CHECK_INT_EQ((unsigned char)got[0].text[len - 1], 0xA9);
}

/* ---- json helper --------------------------------------------------------- */

TEST(json_path)
{
    cJSON *j = cJSON_Parse("{\"a\":{\"b\":[{\"c\":\"x\"},{\"c\":\"y\"}]},\"n\":\"12\"}");
    CHECK(j != NULL);
    const char *first = kk_json_str(j, "a.b[0].c");
    const char *last = kk_json_str(j, "a.b[-1].c");
    const cJSON *missing = kk_json_path(j, "a.b[5].c");
    const cJSON *bad = kk_json_path(j, "a[0]");
    double n = kk_json_num(j, "n", -1);
    char first_c = first ? first[0] : 0, last_c = last ? last[0] : 0;
    cJSON_Delete(j);
    CHECK_INT_EQ(first_c, 'x');
    CHECK_INT_EQ(last_c, 'y');
    CHECK(missing == NULL);
    CHECK(bad == NULL);
    CHECK_INT_EQ((int)n, 12);
}

int main(void)
{
    RUN(target_video_forms);
    RUN(target_channel_forms);
    RUN(target_invalid);
    RUN(canonical_video);
    RUN(chat_page);
    RUN(chat_page_without_chat);
    RUN(poll_messages);
    RUN(emote_url_size);
    RUN(poll_skip_backlog);
    RUN(poll_timed_and_ended);
    RUN(long_text_is_cut_cleanly);
    RUN(json_path);
    return harness_report();
}
