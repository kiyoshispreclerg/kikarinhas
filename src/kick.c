/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "kick.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "json.h"
#include "log.h"
#include "util.h"
#include "ws.h"

/* Kick's public Pusher app (the same key the site uses). */
#define PUSHER_URL                                                             \
    "wss://ws-us2.pusher.com/app/32cbd69e4b950bf97679?protocol=7&client=js&version=8.4.0" \
    "&flash=false"
#define CHANNEL_API "https://kick.com/api/v2/channels/"
#define EMOTE_URL "https://files.kick.com/emotes/%s/fullsize"
#define SEVENTV_GLOBAL "https://7tv.io/v3/emote-sets/global"
#define SEVENTV_CHANNEL "https://7tv.io/v3/users/kick/%lld"
#define MAX_EMOTES 32
#define MAX_TEXT 2048
/* An unknown channel: no point in asking often. */
#define NOT_FOUND_RETRY_S 300.0

/* ---- target -------------------------------------------------------------- */

static bool is_slug_char(char c)
{
    return isalnum((unsigned char)c) || c == '_' || c == '-';
}

static bool take_slug(const char *s, char *out, size_t size)
{
    size_t n = strcspn(s, "/?#");
    if (n == 0 || n > 64 || n >= size)
        return false;
    for (size_t i = 0; i < n; i++) {
        if (!is_slug_char(s[i]))
            return false;
        out[i] = (char)tolower((unsigned char)s[i]);
    }
    out[n] = '\0';
    return true;
}

bool kk_kick_parse_target(const char *input, char *out, size_t size)
{
    while (isspace((unsigned char)*input))
        input++;
    char buf[512];
    size_t n = strcspn(input, " \t\r\n");
    if (n == 0 || n >= sizeof buf)
        return false;
    memcpy(buf, input, n);
    buf[n] = '\0';

    const char *p = buf;
    const char *host = strstr(p, "kick.com/");
    if (host && (host == p || host[-1] == '/' || host[-1] == '.')) {
        p = host + strlen("kick.com/");
        if (strncmp(p, "popout/", 7) == 0)
            p += 7;
        return take_slug(p, out, size);
    }
    if (strchr(p, '/') || strchr(p, '.'))
        return false;
    return p[strcspn(p, "?#")] == '\0' && take_slug(p, out, size);
}

bool kk_kick_parse_channel(const char *json, long long *chatroom, long long *channel,
                           long long *user_id)
{
    cJSON *root = cJSON_Parse(json);
    double room = kk_json_num(root, "chatroom.id", -1);
    double chan = kk_json_num(root, "id", -1);
    double user = kk_json_num(root, "user_id", -1);
    cJSON_Delete(root);
    if (room <= 0)
        return false;
    *chatroom = (long long)room;
    *channel = chan > 0 ? (long long)chan : 0;
    *user_id = user > 0 ? (long long)user : 0;
    return true;
}

bool kk_kick_parse_title(const char *json, char *out, size_t size)
{
    cJSON *root = cJSON_Parse(json);
    const char *t = kk_json_str(root, "livestream.session_title");
    bool ok = t && t[0] && snprintf(out, size, "%s", t) < (int)size;
    if (!ok && size)
        out[0] = '\0';
    cJSON_Delete(root);
    return ok;
}

/* ---- recent chatters ----------------------------------------------------- */

#define NAMES 512

struct kk_kick_names {
    struct {
        char name[64]; /* lowercase */
        char id[32];
    } v[NAMES];
    int next; /* a ring: the oldest goes */
};

kk_kick_names *kk_kick_names_new(void)
{
    return calloc(1, sizeof(kk_kick_names));
}

void kk_kick_names_free(kk_kick_names *n)
{
    free(n);
}

static void lower(char *out, size_t size, const char *s)
{
    size_t i = 0;
    for (; s[i] && i + 1 < size; i++)
        out[i] = (char)tolower((unsigned char)s[i]);
    out[i] = '\0';
}

static const char *names_find(const kk_kick_names *n, const char *name)
{
    char key[64];
    lower(key, sizeof key, name);
    for (int i = 0; n && i < NAMES; i++)
        if (n->v[i].id[0] && strcmp(n->v[i].name, key) == 0)
            return n->v[i].id;
    return NULL;
}

static void names_add(kk_kick_names *n, const char *name, const char *id)
{
    if (!n || names_find(n, name))
        return;
    lower(n->v[n->next].name, sizeof n->v[0].name, name);
    snprintf(n->v[n->next].id, sizeof n->v[0].id, "%s", id);
    n->next = (n->next + 1) % NAMES;
}

/* ---- messages ------------------------------------------------------------ */

typedef struct {
    char text[MAX_TEXT];
    size_t len;
    kk_emote list[MAX_EMOTES];
    char ids[MAX_EMOTES][40], names[MAX_EMOTES][100], urls[MAX_EMOTES][160];
    int n;
} parsed;

static bool overlaps(const parsed *p, size_t start, size_t len)
{
    for (int i = 0; i < p->n; i++)
        if (start < p->list[i].start + p->list[i].len && p->list[i].start < start + len)
            return true;
    return false;
}

static void put(parsed *p, const char *s, size_t n)
{
    if (n > sizeof p->text - 1 - p->len)
        n = sizeof p->text - 1 - p->len;
    memcpy(p->text + p->len, s, n);
    p->len += n;
    p->text[p->len] = '\0';
}

/* "[emote:123:Name]" at s: fills id and name, returns its length; 0 if s is
 * not one. */
static size_t kick_emote(const char *s, char *id, size_t id_size, char *name, size_t name_size)
{
    if (strncmp(s, "[emote:", 7) != 0)
        return 0;
    const char *a = s + 7;
    size_t idn = strspn(a, "0123456789");
    if (idn == 0 || idn >= id_size || a[idn] != ':')
        return 0;
    const char *b = a + idn + 1;
    size_t nn = strcspn(b, "]: \t\r\n");
    if (nn == 0 || nn >= name_size || b[nn] != ']')
        return 0;
    memcpy(id, a, idn);
    id[idn] = '\0';
    memcpy(name, b, nn);
    name[nn] = '\0';
    return (size_t)(b + nn + 1 - s);
}

/* The content with its emote tags turned into names, the emotes listed. */
static void parse_content(parsed *p, const char *content, const kk_tw_emotes *extra)
{
    p->len = 0;
    p->text[0] = '\0';
    p->n = 0;
    for (const char *s = content; *s;) {
        char id[40], name[100];
        size_t n = *s == '[' ? kick_emote(s, id, sizeof id, name, sizeof name) : 0;
        if (n == 0) {
            size_t run = strcspn(s + 1, "[") + 1;
            put(p, s, run);
            s += run;
            continue;
        }
        size_t start = p->len;
        put(p, name, strlen(name));
        int k = p->n;
        if (k < MAX_EMOTES && p->len == start + strlen(name) &&
            kk_pathf(p->urls[k], sizeof p->urls[0], EMOTE_URL, id)) {
            snprintf(p->ids[k], sizeof p->ids[0], "%s", id);
            snprintf(p->names[k], sizeof p->names[0], "%s", name);
            p->list[k] = (kk_emote){.id = p->ids[k], .name = p->names[k], .url = p->urls[k],
                                    .start = start, .len = p->len - start};
            p->n++;
        }
        s += n;
    }
    if (!extra)
        return;
    /* 7TV: plain words. */
    for (size_t i = 0; i < p->len && p->n < MAX_EMOTES;) {
        while (i < p->len && p->text[i] == ' ')
            i++;
        size_t len = strcspn(p->text + i, " ");
        const kk_tw_emote *e = len ? kk_tw_emotes_find(extra, p->text + i, len) : NULL;
        if (e && !overlaps(p, i, len))
            p->list[p->n++] = (kk_emote){.id = e->id, .name = e->name, .url = e->url,
                                         .start = i, .len = len};
        i += len;
    }
    /* In text order. */
    for (int i = 1; i < p->n; i++)
        for (int j = i; j > 0 && p->list[j].start < p->list[j - 1].start; j--) {
            kk_emote t = p->list[j];
            p->list[j] = p->list[j - 1];
            p->list[j - 1] = t;
        }
}

static unsigned parse_badges(const cJSON *list)
{
    unsigned badges = 0;
    const cJSON *b;
    cJSON_ArrayForEach(b, list)
    {
        const char *t = kk_json_str(b, "type");
        if (!t)
            continue;
        if (strcmp(t, "broadcaster") == 0)
            badges |= KK_BADGE_OWNER;
        else if (strcmp(t, "moderator") == 0)
            badges |= KK_BADGE_MOD;
        else if (strcmp(t, "subscriber") == 0 || strcmp(t, "founder") == 0)
            badges |= KK_BADGE_MEMBER;
        else if (strcmp(t, "verified") == 0)
            badges |= KK_BADGE_VERIFIED;
    }
    return badges;
}

static bool emit_chat(const cJSON *d, const char *source, const kk_tw_emotes *extra,
                      kk_kick_names *names, const kk_chat_sink *sink)
{
    const char *content = kk_json_str(d, "content");
    const char *name = kk_json_str(d, "sender.username");
    double uid = kk_json_num(d, "sender.id", -1);
    if (!content || !name || !name[0] || uid <= 0)
        return false;
    char user_id[32];
    snprintf(user_id, sizeof user_id, "%.0f", uid);
    names_add(names, name, user_id);
    const char *type = kk_json_str(d, "type");
    kk_chat_msg m = {.platform = "kick", .source = source, .user_id = user_id, .name = name,
                     .kind = KK_MSG_TEXT,
                     .badges = parse_badges(kk_json_path(d, "sender.identity.badges"))};
    /* A sub anniversary shared in chat. */
    if (type && strcmp(type, "celebration") == 0)
        m.kind = KK_MSG_MEMBER;
    parsed *p = malloc(sizeof *p);
    if (!p)
        return false;
    parse_content(p, content, extra);
    m.text = p->text;
    m.emotes = p->list;
    m.n_emotes = p->n;
    sink->on_msg(sink->ud, &m);
    free(p);
    return true;
}

/* A member message for someone known only by name. */
static bool emit_member(const char *name, const char *text, const char *source,
                        const kk_kick_names *names, const kk_chat_sink *sink)
{
    const char *id = name ? names_find(names, name) : NULL;
    if (!id)
        return false;
    kk_chat_msg m = {.platform = "kick", .source = source, .user_id = id, .name = name,
                     .text = text, .kind = KK_MSG_MEMBER, .badges = KK_BADGE_MEMBER};
    sink->on_msg(sink->ud, &m);
    return true;
}

/* Kicks: {message, sender: {id, username}, gift: {amount, name}}. */
static bool emit_kicks(const cJSON *d, const char *source, const kk_tw_emotes *extra,
                       const kk_chat_sink *sink)
{
    const char *name = kk_json_str(d, "sender.username");
    double uid = kk_json_num(d, "sender.id", kk_json_num(d, "sender.user_id", -1));
    double kicks = kk_json_num(d, "gift.amount", 0);
    const char *text = kk_json_str(d, "message");
    if (!text)
        text = kk_json_str(d, "gift.message");
    if (!name || !name[0] || uid <= 0 || kicks <= 0)
        return false;
    char user_id[32], amount[48];
    snprintf(user_id, sizeof user_id, "%.0f", uid);
    snprintf(amount, sizeof amount, "%.0f Kicks", kicks);
    kk_chat_msg m = {.platform = "kick", .source = source, .user_id = user_id, .name = name,
                     .amount = amount, .kind = KK_MSG_PAID,
                     .badges = parse_badges(kk_json_path(d, "sender.identity.badges"))};
    parsed *p = malloc(sizeof *p);
    if (!p)
        return false;
    parse_content(p, text ? text : "", extra);
    m.text = p->text;
    m.emotes = p->list;
    m.n_emotes = p->n;
    sink->on_msg(sink->ud, &m);
    free(p);
    return true;
}

static bool is_event(const char *event, const char *name)
{
    /* With or without the "App\\Events\\" in front. */
    if (strncmp(event, "App\\Events\\", 11) == 0)
        event += 11;
    return strcmp(event, name) == 0;
}

bool kk_kick_emit_event(const char *json, const char *source, const kk_tw_emotes *extra,
                        kk_kick_names *names, const kk_chat_sink *sink)
{
    cJSON *frame = cJSON_Parse(json);
    const char *event = kk_json_str(frame, "event");
    const char *data = kk_json_str(frame, "data");
    cJSON *d = data ? cJSON_Parse(data) : NULL;
    bool ok = false;
    if (event && d) {
        if (is_event(event, "ChatMessageEvent"))
            ok = emit_chat(d, source, extra, names, sink);
        else if (is_event(event, "KicksGifted"))
            ok = emit_kicks(d, source, extra, sink);
        else if (is_event(event, "SubscriptionEvent"))
            ok = emit_member(kk_json_str(d, "username"), "", source, names, sink);
        else if (is_event(event, "GiftedSubscriptionsEvent"))
            ok = emit_member(kk_json_str(d, "gifter_username"), "", source, names, sink);
    }
    cJSON_Delete(d);
    cJSON_Delete(frame);
    return ok;
}

/* ---- client -------------------------------------------------------------- */

typedef enum {
    ST_INFO_WAIT, /* until next_at, then ask for the channel */
    ST_INFO,      /* asking */
    ST_WS_WAIT,   /* until next_at, then connect */
    ST_WS,        /* connecting or connected */
} state;

static void relay_msg(void *ud, const kk_chat_msg *m);

struct kk_kick {
    kk_http *http;
    kk_chat_sink sink;
    kk_chat_sink relay; /* what the parser gets: adds the live's link and title */
    char slug[72];
    char url[96], title[160];
    char label[88]; /* "kick.com/slug": the source of its messages */
    long long chatroom, channel, user_id;
    kk_kick_names *names;

    kk_ws *ws;
    state st;
    double now, next_at;
    bool subscribed;
    int failures;

    kk_tw_emotes *extra; /* 7TV; NULL when off */
    unsigned loaded, pending; /* bits by kk_tw_source */
};

static void relay_msg(void *ud, const kk_chat_msg *m)
{
    const kk_kick *k = ud;
    kk_chat_msg copy = *m;
    copy.source_url = k->url;
    copy.source_title = k->title[0] ? k->title : NULL;
    k->sink.on_msg(k->sink.ud, &copy);
}

static double backoff(kk_kick *k)
{
    k->failures++;
    return fmin(60.0, pow(2.0, fmin(k->failures, 6)));
}

static void on_7tv(kk_kick *k, kk_tw_source src, long status, const char *body,
                   const char *err)
{
    unsigned bit = 1u << src;
    k->pending &= ~bit;
    int n = -1;
    if (status == 404) {
        /* The channel has no 7TV account. */
        kk_tw_emotes_clear(k->extra, src);
        n = 0;
    } else if (status == 200) {
        n = kk_tw_emotes_load(k->extra, src, body);
    }
    const char *what = src == KK_TW_7TV_GLOBAL ? "globais" : "do canal";
    if (n < 0) {
        if (err)
            kk_log_warn("Kick: emotes do 7TV (%s): %s", what, err);
        else
            kk_log_warn("Kick: emotes do 7TV (%s): HTTP %ld", what, status);
        return;
    }
    k->loaded |= bit;
    if (n > 0)
        kk_log_info("Kick %s: %d emotes do 7TV (%s)", k->slug, n, what);
}

static void on_7tv_global(void *ud, long status, const char *body, size_t len, const char *err)
{
    (void)len;
    on_7tv(ud, KK_TW_7TV_GLOBAL, status, body, err);
}

static void on_7tv_channel(void *ud, long status, const char *body, size_t len, const char *err)
{
    (void)len;
    on_7tv(ud, KK_TW_7TV_CHANNEL, status, body, err);
}

static void fetch_emotes(kk_kick *k)
{
    if (!k->extra)
        return;
    unsigned g = 1u << KK_TW_7TV_GLOBAL, c = 1u << KK_TW_7TV_CHANNEL;
    if (!((k->loaded | k->pending) & g) &&
        kk_http_get(k->http, SEVENTV_GLOBAL, on_7tv_global, k) == 0)
        k->pending |= g;
    char url[128];
    if (k->user_id > 0 && !((k->loaded | k->pending) & c) &&
        kk_pathf(url, sizeof url, SEVENTV_CHANNEL, k->user_id) &&
        kk_http_get(k->http, url, on_7tv_channel, k) == 0)
        k->pending |= c;
}

static void on_channel(void *ud, long status, const char *body, size_t len, const char *err)
{
    (void)len;
    kk_kick *k = ud;
    if (status == 404) {
        kk_log_warn("Kick: o canal %s não existe (nova tentativa em %.0f min)", k->slug,
                    NOT_FOUND_RETRY_S / 60);
        k->st = ST_INFO_WAIT;
        k->next_at = k->now + NOT_FOUND_RETRY_S;
        return;
    }
    long long room, chan, user;
    if (status != 200 || !kk_kick_parse_channel(body, &room, &chan, &user)) {
        double delay = backoff(k);
        if (err)
            kk_log_warn("Kick %s: dados do canal: %s (nova tentativa em %.0f s)", k->slug, err,
                        delay);
        else
            kk_log_warn("Kick %s: dados do canal: HTTP %ld (nova tentativa em %.0f s)", k->slug,
                        status, delay);
        k->st = ST_INFO_WAIT;
        k->next_at = k->now + delay;
        return;
    }
    if (!kk_kick_parse_title(body, k->title, sizeof k->title))
        k->title[0] = '\0';
    k->chatroom = room;
    k->channel = chan;
    k->user_id = user;
    k->st = ST_WS_WAIT;
    k->next_at = k->now;
    fetch_emotes(k);
}

static void send_event(kk_kick *k, const char *event, const char *channel)
{
    char msg[256];
    if (channel)
        snprintf(msg, sizeof msg, "{\"event\":\"%s\",\"data\":{\"auth\":\"\",\"channel\":\"%s\"}}",
                 event, channel);
    else
        snprintf(msg, sizeof msg, "{\"event\":\"%s\",\"data\":{}}", event);
    kk_ws_send(k->ws, msg);
}

static void on_ws_message(void *ud, char *text, size_t len)
{
    (void)len;
    kk_kick *k = ud;
    cJSON *frame = cJSON_Parse(text);
    const char *event = kk_json_str(frame, "event");
    if (!event) {
        cJSON_Delete(frame);
        return;
    }
    if (strcmp(event, "pusher:connection_established") == 0) {
        char channel[64];
        snprintf(channel, sizeof channel, "chatrooms.%lld.v2", k->chatroom);
        send_event(k, "pusher:subscribe", channel);
        /* Kicks. */
        if (k->channel > 0) {
            snprintf(channel, sizeof channel, "channel.%lld", k->channel);
            send_event(k, "pusher:subscribe", channel);
        }
    } else if (strcmp(event, "pusher_internal:subscription_succeeded") == 0) {
        const char *ch = kk_json_str(frame, "channel");
        if (!k->subscribed && ch && strncmp(ch, "chatrooms.", 10) == 0)
            kk_log_info("Kick: conectado ao chat de https://kick.com/%s", k->slug);
        if (ch && strncmp(ch, "chatrooms.", 10) == 0) {
            k->subscribed = true;
            k->failures = 0;
            /* What failed to load last time. */
            fetch_emotes(k);
        }
    } else if (strcmp(event, "pusher:ping") == 0) {
        send_event(k, "pusher:pong", NULL);
    } else if (strcmp(event, "pusher:error") == 0) {
        const char *data = kk_json_str(frame, "data");
        cJSON *d = data ? cJSON_Parse(data) : cJSON_Duplicate(kk_json_path(frame, "data"), 1);
        const char *msg = kk_json_str(d, "message");
        kk_log_warn("Kick %s: Pusher: %s", k->slug, msg ? msg : "erro");
        cJSON_Delete(d);
    } else if (strncmp(event, "pusher", 6) != 0) {
        kk_kick_emit_event(text, k->label, k->extra, k->names, &k->relay);
    }
    cJSON_Delete(frame);
}

kk_kick *kk_kick_new(kk_http *http, const char *target, const kk_chat_sink *sink,
                     bool extra_emotes)
{
    kk_kick *k = calloc(1, sizeof *k);
    if (!k)
        return NULL;
    if (!kk_kick_parse_target(target, k->slug, sizeof k->slug)) {
        kk_log_error("Kick: não entendi \"%s\" (use o nome do canal ou o link kick.com/canal)",
                     target);
        free(k);
        return NULL;
    }
    snprintf(k->label, sizeof k->label, "kick.com/%s", k->slug);
    k->http = http;
    k->sink = *sink;
    k->relay = (kk_chat_sink){relay_msg, NULL, k};
    snprintf(k->url, sizeof k->url, "https://kick.com/%s", k->slug);
    k->ws = kk_ws_new(http, &(kk_ws_cb){NULL, on_ws_message}, k);
    k->names = kk_kick_names_new();
    if (extra_emotes)
        k->extra = kk_tw_emotes_new();
    if (!k->ws || !k->names || (extra_emotes && !k->extra)) {
        kk_kick_free(k);
        return NULL;
    }
    k->st = ST_INFO_WAIT;
    return k;
}

void kk_kick_free(kk_kick *k)
{
    if (!k)
        return;
    kk_http_cancel(k->http, k);
    kk_ws_free(k->ws);
    kk_tw_emotes_free(k->extra);
    kk_kick_names_free(k->names);
    free(k);
}

int kk_kick_pollfd(const kk_kick *k, struct pollfd *fd)
{
    return kk_ws_pollfd(k->ws, fd);
}

void kk_kick_tick(kk_kick *k, const struct pollfd *fd, double now)
{
    k->now = now;
    switch (k->st) {
    case ST_INFO_WAIT: {
        if (now < k->next_at)
            return;
        char url[160];
        if (kk_pathf(url, sizeof url, CHANNEL_API "%s", k->slug) &&
            kk_http_get(k->http, url, on_channel, k) == 0) {
            k->st = ST_INFO;
        } else {
            k->next_at = now + backoff(k);
        }
        return;
    }
    case ST_INFO:
        return;
    case ST_WS_WAIT:
        if (now < k->next_at)
            return;
        k->subscribed = false;
        k->st = ST_WS;
        if (!kk_ws_connect(k->ws, PUSHER_URL, now)) {
            double delay = backoff(k);
            kk_log_warn("Kick %s: não consegui iniciar a conexão (nova tentativa em %.0f s)",
                        k->slug, delay);
            k->st = ST_WS_WAIT;
            k->next_at = now + delay;
        }
        return;
    case ST_WS: {
        const char *why;
        if (!kk_ws_pump(k->ws, fd, now, &why)) {
            double delay = backoff(k);
            kk_log_warn("Kick %s: chat: %s (nova tentativa em %.0f s)", k->slug, why, delay);
            k->st = ST_WS_WAIT;
            k->next_at = now + delay;
        }
        return;
    }
    }
}
