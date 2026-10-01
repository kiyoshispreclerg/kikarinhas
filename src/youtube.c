/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "youtube.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "json.h"
#include "log.h"
#include "util.h"

#define YT "https://www.youtube.com"
#define POLL_URL YT "/youtubei/v1/live_chat/get_live_chat?prettyPrint=false"
/* Used when the page does not say (the server accepts slightly old ones). */
#define FALLBACK_CLIENT_VERSION "2.20260930.00.00"
/* "invalidation" tokens expect a push channel we don't have: poll instead. */
#define INVALIDATION_POLL_MS 2500
#define OFFLINE_RETRY_S 60.0
#define MAX_TEXT 400

/* ---- target -------------------------------------------------------------- */

static bool is_id_char(char c)
{
    return isalnum((unsigned char)c) || c == '-' || c == '_';
}

/* Exactly 11 id characters at s, not followed by another id character. */
static bool take_video_id(const char *s, char *out, size_t size)
{
    for (int i = 0; i < 11; i++)
        if (!is_id_char(s[i]))
            return false;
    if (is_id_char(s[11]) || size < 12)
        return false;
    memcpy(out, s, 11);
    out[11] = '\0';
    return true;
}

kk_yt_target kk_yt_parse_target(const char *input, char *out, size_t size)
{
    while (isspace((unsigned char)*input))
        input++;
    char buf[512];
    size_t n = strcspn(input, " \t\r\n");
    if (n == 0 || n >= sizeof buf)
        return KK_YT_TARGET_INVALID;
    memcpy(buf, input, n);
    buf[n] = '\0';

    if (n == 11 && take_video_id(buf, out, size))
        return KK_YT_TARGET_VIDEO;
    if (buf[0] == '@')
        return kk_pathf(out, size, YT "/%s/live", buf) ? KK_YT_TARGET_CHANNEL
                                                       : KK_YT_TARGET_INVALID;
    if (n == 24 && strncmp(buf, "UC", 2) == 0)
        return kk_pathf(out, size, YT "/channel/%s/live", buf)
                   ? KK_YT_TARGET_CHANNEL
                   : KK_YT_TARGET_INVALID;

    const char *host = strstr(buf, "youtube.com/");
    const char *shortl = strstr(buf, "youtu.be/");
    if (shortl)
        return take_video_id(shortl + 9, out, size) ? KK_YT_TARGET_VIDEO
                                                     : KK_YT_TARGET_INVALID;
    if (!host)
        return KK_YT_TARGET_INVALID;

    const char *path = host + strlen("youtube.com");
    const char *v = strstr(path, "v=");
    if (v && (v[-1] == '?' || v[-1] == '&'))
        return take_video_id(v + 2, out, size) ? KK_YT_TARGET_VIDEO
                                               : KK_YT_TARGET_INVALID;
    if (strncmp(path, "/live/", 6) == 0)
        return take_video_id(path + 6, out, size) ? KK_YT_TARGET_VIDEO
                                                  : KK_YT_TARGET_INVALID;

    /* Channel page: /@handle, /channel/ID, /c/name or /user/name. */
    char seg1[128], seg2[128];
    const char *p = path + 1;
    size_t l1 = strcspn(p, "/?#");
    if (l1 == 0 || l1 >= sizeof seg1)
        return KK_YT_TARGET_INVALID;
    memcpy(seg1, p, l1);
    seg1[l1] = '\0';
    if (seg1[0] == '@')
        return kk_pathf(out, size, YT "/%s/live", seg1) ? KK_YT_TARGET_CHANNEL
                                                        : KK_YT_TARGET_INVALID;
    if (strcmp(seg1, "channel") && strcmp(seg1, "c") && strcmp(seg1, "user"))
        return KK_YT_TARGET_INVALID;
    p += l1;
    if (*p != '/')
        return KK_YT_TARGET_INVALID;
    p++;
    size_t l2 = strcspn(p, "/?#");
    if (l2 == 0 || l2 >= sizeof seg2)
        return KK_YT_TARGET_INVALID;
    memcpy(seg2, p, l2);
    seg2[l2] = '\0';
    return kk_pathf(out, size, YT "/%s/%s/live", seg1, seg2)
               ? KK_YT_TARGET_CHANNEL
               : KK_YT_TARGET_INVALID;
}

/* ---- page scraping ------------------------------------------------------- */

bool kk_yt_canonical_video(const char *html, char id[12])
{
    const char *p = strstr(html, "<link rel=\"canonical\" href=\"");
    if (!p)
        return false;
    p += strlen("<link rel=\"canonical\" href=\"");
    const char *end = strchr(p, '"');
    const char *v = strstr(p, "watch?v=");
    return end && v && v < end && take_video_id(v + 8, id, 12);
}

cJSON *kk_yt_initial_data(const char *html)
{
    static const char *const markers[] = {"window[\"ytInitialData\"] = ",
                                          "var ytInitialData = ",
                                          "ytInitialData = "};
    for (size_t i = 0; i < sizeof markers / sizeof markers[0]; i++) {
        const char *p = strstr(html, markers[i]);
        if (p)
            /* Stops at the end of the object; the page goes on after it. */
            return cJSON_ParseWithOpts(p + strlen(markers[i]), NULL, false);
    }
    return NULL;
}

bool kk_yt_config_string(const char *html, const char *key, char *out,
                         size_t size)
{
    char pat[96];
    if (!kk_pathf(pat, sizeof pat, "\"%s\":\"", key))
        return false;
    const char *p = strstr(html, pat);
    if (!p)
        return false;
    p += strlen(pat);
    size_t n = strcspn(p, "\"");
    if (n == 0 || n >= size)
        return false;
    memcpy(out, p, n);
    out[n] = '\0';
    return true;
}

char *kk_yt_live_continuation(const cJSON *initial)
{
    const cJSON *chat = kk_json_path(initial, "contents.liveChatRenderer");
    if (!chat)
        return NULL;
    /* The view menu lists "Top chat" then "Live chat" (all messages). */
    const char *c = kk_json_str(
        chat, "header.liveChatHeaderRenderer.viewSelector."
              "sortFilterSubMenuRenderer.subMenuItems[-1]."
              "continuation.reloadContinuationData.continuation");
    if (!c) {
        const cJSON *first = kk_json_path(chat, "continuations[0]");
        const cJSON *data = first ? first->child : NULL;
        c = data ? kk_json_str(data, "continuation") : NULL;
    }
    return c ? strdup(c) : NULL;
}

/* ---- messages ------------------------------------------------------------ */

typedef struct {
    char buf[MAX_TEXT];
    size_t len;
} text_buf;

/* Appends s without splitting a UTF-8 sequence. */
static void text_add(text_buf *t, const char *s)
{
    size_t n = strlen(s);
    if (t->len + n >= sizeof t->buf) {
        n = sizeof t->buf - 1 - t->len;
        while (n > 0 && ((unsigned char)s[n] & 0xC0) == 0x80)
            n--;
    }
    memcpy(t->buf + t->len, s, n);
    t->len += n;
    t->buf[t->len] = '\0';
}

/* Text of a {simpleText} or {runs:[{text}|{emoji}]} object. Standard emoji
 * become their Unicode character; channel emoji their :shortcut:. */
static void runs_text(const cJSON *obj, text_buf *t)
{
    const char *simple = kk_json_str(obj, "simpleText");
    if (simple) {
        text_add(t, simple);
        return;
    }
    const cJSON *run;
    cJSON_ArrayForEach(run, cJSON_GetObjectItemCaseSensitive(obj, "runs"))
    {
        const char *s = kk_json_str(run, "text");
        if (!s) {
            const cJSON *emoji = cJSON_GetObjectItemCaseSensitive(run, "emoji");
            bool custom = cJSON_IsTrue(
                cJSON_GetObjectItemCaseSensitive(emoji, "isCustomEmoji"));
            s = custom ? kk_json_str(emoji, "shortcuts[0]") : NULL;
            if (!s)
                s = kk_json_str(emoji, "emojiId");
        }
        if (s)
            text_add(t, s);
    }
}

static unsigned parse_badges(const cJSON *item)
{
    unsigned badges = 0;
    const cJSON *b;
    cJSON_ArrayForEach(b, cJSON_GetObjectItemCaseSensitive(item, "authorBadges"))
    {
        const cJSON *r =
            cJSON_GetObjectItemCaseSensitive(b, "liveChatAuthorBadgeRenderer");
        const char *icon = kk_json_str(r, "icon.iconType");
        if (icon && strcmp(icon, "OWNER") == 0)
            badges |= KK_BADGE_OWNER;
        else if (icon && strcmp(icon, "MODERATOR") == 0)
            badges |= KK_BADGE_MOD;
        else if (icon && strcmp(icon, "VERIFIED") == 0)
            badges |= KK_BADGE_VERIFIED;
        else if (cJSON_GetObjectItemCaseSensitive(r, "customThumbnail"))
            badges |= KK_BADGE_MEMBER;
    }
    return badges;
}

/* One chat item (the value inside "item"); false if not a message. */
static bool emit_item(const cJSON *item, kk_chat_cb cb, void *ud)
{
    const cJSON *r;
    kk_chat_msg m = {.platform = "youtube"};
    text_buf text = {0};
    text_buf amount = {0};

    if ((r = cJSON_GetObjectItemCaseSensitive(item, "liveChatTextMessageRenderer"))) {
        m.kind = KK_MSG_TEXT;
        runs_text(cJSON_GetObjectItemCaseSensitive(r, "message"), &text);
    } else if ((r = cJSON_GetObjectItemCaseSensitive(item, "liveChatPaidMessageRenderer")) ||
               (r = cJSON_GetObjectItemCaseSensitive(item, "liveChatPaidStickerRenderer"))) {
        m.kind = KK_MSG_PAID;
        runs_text(cJSON_GetObjectItemCaseSensitive(r, "message"), &text);
        runs_text(cJSON_GetObjectItemCaseSensitive(r, "purchaseAmountText"), &amount);
        m.amount = amount.buf;
    } else if ((r = cJSON_GetObjectItemCaseSensitive(item, "liveChatMembershipItemRenderer"))) {
        m.kind = KK_MSG_MEMBER;
        /* Milestones carry a message; new members only the header. */
        runs_text(cJSON_GetObjectItemCaseSensitive(r, "message"), &text);
        if (text.len == 0)
            runs_text(cJSON_GetObjectItemCaseSensitive(r, "headerSubtext"), &text);
    } else {
        return false;
    }

    m.user_id = kk_json_str(r, "authorExternalChannelId");
    text_buf name = {0};
    runs_text(cJSON_GetObjectItemCaseSensitive(r, "authorName"), &name);
    if (!m.user_id || name.len == 0)
        return false;
    /* Handles come as "@name": the @ is noise above an avatar. */
    m.name = name.buf[0] == '@' && name.buf[1] ? name.buf + 1 : name.buf;
    m.text = text.buf;
    m.badges = parse_badges(r);
    cb(ud, &m);
    return true;
}

int kk_yt_emit_actions(const cJSON *actions, kk_chat_cb cb, void *ud)
{
    int count = 0;
    const cJSON *a;
    cJSON_ArrayForEach(a, actions)
    {
        const cJSON *item = kk_json_path(a, "addChatItemAction.item");
        /* Held messages appear as placeholders, later replaced. */
        if (!item)
            item = kk_json_path(a, "replaceChatItemAction.replacementItem");
        if (item && emit_item(item, cb, ud))
            count++;
    }
    return count;
}

kk_yt_poll kk_yt_parse_poll(const char *json, bool emit, kk_chat_cb cb,
                            void *ud, char **next, int *delay_ms)
{
    *next = NULL;
    cJSON *root = cJSON_Parse(json);
    if (!root)
        return KK_YT_POLL_ERROR;

    kk_yt_poll result = KK_YT_POLL_ERROR;
    const cJSON *lcc =
        kk_json_path(root, "continuationContents.liveChatContinuation");
    if (!lcc) {
        /* A finished live answers without the chat continuation at all. */
        result = cJSON_GetObjectItemCaseSensitive(root, "responseContext")
                     ? KK_YT_POLL_ENDED
                     : KK_YT_POLL_ERROR;
        goto out;
    }
    if (emit)
        kk_yt_emit_actions(cJSON_GetObjectItemCaseSensitive(lcc, "actions"), cb, ud);

    const cJSON *first = kk_json_path(lcc, "continuations[0]");
    const cJSON *data = first ? first->child : NULL;
    const char *token = data ? kk_json_str(data, "continuation") : NULL;
    if (!token) {
        result = KK_YT_POLL_ENDED;
        goto out;
    }
    *next = strdup(token);
    if (!*next)
        goto out;

    int timeout = (int)kk_json_num(data, "timeoutMs", 5000);
    if (strcmp(data->string, "invalidationContinuationData") == 0)
        timeout = INVALIDATION_POLL_MS;
    else if (strcmp(data->string, "reloadContinuationData") == 0)
        timeout = 1000;
    *delay_ms = timeout < 1000 ? 1000 : timeout > 10000 ? 10000 : timeout;
    result = KK_YT_POLL_OK;
out:
    cJSON_Delete(root);
    return result;
}

/* ---- client -------------------------------------------------------------- */

typedef enum {
    ST_RESOLVE,   /* fetch the channel's /live page */
    ST_CHAT_PAGE, /* fetch the pop-out chat page */
    ST_POLL,      /* poll get_live_chat */
    ST_STOPPED,
} state;

struct kk_youtube {
    kk_http *http;
    kk_chat_cb cb;
    void *ud;

    char channel_url[512]; /* empty when started from a video */
    char video[12];
    char client_version[64];
    char *continuation;
    bool skip_backlog;

    state st;
    bool busy;
    bool said_offline; /* "not live" was logged; don't repeat every minute */
    double now, next_at;
    int failures;
};

kk_youtube *kk_youtube_new(kk_http *http, const char *target, kk_chat_cb cb,
                           void *ud)
{
    kk_youtube *yt = calloc(1, sizeof *yt);
    if (!yt)
        return NULL;
    char out[512];
    switch (kk_yt_parse_target(target, out, sizeof out)) {
    case KK_YT_TARGET_VIDEO:
        memcpy(yt->video, out, sizeof yt->video);
        yt->st = ST_CHAT_PAGE;
        break;
    case KK_YT_TARGET_CHANNEL:
        snprintf(yt->channel_url, sizeof yt->channel_url, "%s", out);
        yt->st = ST_RESOLVE;
        break;
    default:
        kk_log_error("YouTube: não entendi \"%s\" (use o link da live, do "
                     "canal ou o @handle)",
                     target);
        free(yt);
        return NULL;
    }
    yt->http = http;
    yt->cb = cb;
    yt->ud = ud;
    snprintf(yt->client_version, sizeof yt->client_version, "%s",
             FALLBACK_CLIENT_VERSION);
    return yt;
}

void kk_youtube_free(kk_youtube *yt)
{
    if (!yt)
        return;
    free(yt->continuation);
    free(yt);
}

static void schedule(kk_youtube *yt, double seconds)
{
    yt->busy = false;
    yt->next_at = yt->now + seconds;
}

/* Starts over from the channel page (or the chat page for a fixed video). */
static void restart(kk_youtube *yt, double delay)
{
    free(yt->continuation);
    yt->continuation = NULL;
    yt->st = yt->channel_url[0] ? ST_RESOLVE : ST_CHAT_PAGE;
    schedule(yt, delay);
}

static void fail(kk_youtube *yt, const char *what, long status, const char *err)
{
    yt->failures++;
    double delay = fmin(60.0, pow(2.0, fmin(yt->failures, 6)));
    if (err)
        kk_log_warn("YouTube: %s: %s (nova tentativa em %.0f s)", what, err, delay);
    else
        kk_log_warn("YouTube: %s: HTTP %ld (nova tentativa em %.0f s)", what,
                    status, delay);
    /* A token that keeps failing may have expired: fetch a new one. */
    if (yt->st == ST_POLL && yt->failures >= 3)
        restart(yt, delay);
    else
        schedule(yt, delay);
}

static void on_channel(void *ud, long status, const char *body, size_t len,
                       const char *err)
{
    kk_youtube *yt = ud;
    (void)len;
    if (status != 200) {
        fail(yt, "página do canal", status, err);
        return;
    }
    char id[12];
    if (!kk_yt_canonical_video(body, id)) {
        if (!yt->said_offline)
            kk_log_info("YouTube: o canal não está ao vivo; tentando de novo "
                        "a cada %.0f s",
                        OFFLINE_RETRY_S);
        yt->said_offline = true;
        yt->failures = 0;
        schedule(yt, OFFLINE_RETRY_S);
        return;
    }
    yt->said_offline = false;
    memcpy(yt->video, id, sizeof yt->video);
    yt->st = ST_CHAT_PAGE;
    schedule(yt, 0);
}

static void on_chat_page(void *ud, long status, const char *body, size_t len,
                         const char *err)
{
    kk_youtube *yt = ud;
    (void)len;
    if (status != 200) {
        fail(yt, "página do chat", status, err);
        return;
    }
    kk_yt_config_string(body, "INNERTUBE_CLIENT_VERSION", yt->client_version,
                        sizeof yt->client_version);
    cJSON *initial = kk_yt_initial_data(body);
    char *cont = initial ? kk_yt_live_continuation(initial) : NULL;
    cJSON_Delete(initial);
    if (!cont) {
        kk_log_warn("YouTube: o vídeo %s não tem chat ao vivo (live encerrada "
                    "ou chat desativado); tentando de novo em %.0f s",
                    yt->video, OFFLINE_RETRY_S);
        restart(yt, OFFLINE_RETRY_S);
        return;
    }
    kk_log_info("YouTube: conectado ao chat de https://youtu.be/%s", yt->video);
    free(yt->continuation);
    yt->continuation = cont;
    yt->skip_backlog = true;
    yt->failures = 0;
    yt->st = ST_POLL;
    schedule(yt, 0);
}

static void on_poll(void *ud, long status, const char *body, size_t len,
                    const char *err)
{
    kk_youtube *yt = ud;
    (void)len;
    if (status != 200) {
        fail(yt, "leitura do chat", status, err);
        return;
    }
    char *next;
    int delay_ms = 3000;
    switch (kk_yt_parse_poll(body, !yt->skip_backlog, yt->cb, yt->ud, &next,
                             &delay_ms)) {
    case KK_YT_POLL_OK:
        free(yt->continuation);
        yt->continuation = next;
        yt->skip_backlog = false;
        yt->failures = 0;
        schedule(yt, delay_ms / 1000.0);
        break;
    case KK_YT_POLL_ENDED:
        kk_log_info("YouTube: a live terminou");
        if (yt->channel_url[0]) {
            restart(yt, OFFLINE_RETRY_S);
        } else {
            yt->st = ST_STOPPED;
            yt->busy = false;
        }
        break;
    case KK_YT_POLL_ERROR:
        fail(yt, "leitura do chat", status, "resposta inesperada");
        break;
    }
}

static char *poll_body(const kk_youtube *yt)
{
    cJSON *root = cJSON_CreateObject();
    cJSON *client = cJSON_AddObjectToObject(
        cJSON_AddObjectToObject(root, "context"), "client");
    cJSON_AddStringToObject(client, "clientName", "WEB");
    cJSON_AddStringToObject(client, "clientVersion", yt->client_version);
    cJSON_AddStringToObject(client, "hl", "pt");
    cJSON_AddStringToObject(client, "gl", "BR");
    cJSON_AddStringToObject(root, "continuation", yt->continuation);
    char *s = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return s;
}

void kk_youtube_tick(kk_youtube *yt, double now)
{
    yt->now = now;
    if (yt->busy || yt->st == ST_STOPPED || now < yt->next_at)
        return;

    char url[640];
    int rc = -1;
    switch (yt->st) {
    case ST_RESOLVE:
        rc = kk_http_get(yt->http, yt->channel_url, on_channel, yt);
        break;
    case ST_CHAT_PAGE:
        if (kk_pathf(url, sizeof url, YT "/live_chat?is_popout=1&hl=pt&v=%s",
                     yt->video))
            rc = kk_http_get(yt->http, url, on_chat_page, yt);
        break;
    case ST_POLL: {
        char *body = poll_body(yt);
        if (body)
            rc = kk_http_post_json(yt->http, POLL_URL, body, on_poll, yt);
        free(body);
        break;
    }
    case ST_STOPPED:
        return;
    }
    if (rc == 0)
        yt->busy = true;
    else
        fail(yt, "requisição", 0, "não consegui iniciar");
}
