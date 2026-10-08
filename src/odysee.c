/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "odysee.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "json.h"
#include "log.h"
#include "odysee_emotes.h"
#include "util.h"
#include "ws.h"

#define RESOLVE_URL "https://api.na-backend.odysee.com/api/v1/proxy?m=resolve"
#define IS_LIVE_URL "https://api.odysee.live/livestream/is_live?channel_claim_id=%s"
#define SOCKET_URL "wss://sockety.odysee.tv/ws/commentron?id=%s&category=%s&sub_category=viewer"
/* A channel off the air: how often to ask whether it went live... */
#define OFFLINE_RETRY_S 60.0
/* ...and, while reading a live, whether a new one started. */
#define LIVE_RECHECK_S 120.0
#define NOT_FOUND_RETRY_S 300.0
#define MAX_EMOTES 32
#define MAX_TEXT 2048
#define CLAIM_LEN 40

/* ---- target -------------------------------------------------------------- */

static bool is_claim_id(const char *s, size_t n)
{
    if (n != CLAIM_LEN)
        return false;
    for (size_t i = 0; i < n; i++)
        if (!isxdigit((unsigned char)s[i]))
            return false;
    return true;
}

static int hexval(char c)
{
    return isdigit((unsigned char)c) ? c - '0' : tolower((unsigned char)c) - 'a' + 10;
}

/* A name with its optional claim part ("name", "name:3f", "name#3f"),
 * percent-decoded into out at *o, '#' written as ':'. */
static bool copy_name(const char *s, size_t n, char *out, size_t size, size_t *o)
{
    if (n == 0)
        return false;
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)s[i];
        if (c == '%' && i + 2 < n && isxdigit((unsigned char)s[i + 1]) &&
            isxdigit((unsigned char)s[i + 2])) {
            c = (unsigned char)(hexval(s[i + 1]) * 16 + hexval(s[i + 2]));
            i += 2;
        } else if (c == '#') {
            c = ':';
        }
        if (c < 0x20 || c == ' ' || c == '/' || c == '?' || c == '"' || c == '\\' ||
            *o + 1 >= size)
            return false;
        out[(*o)++] = (char)c;
    }
    out[*o] = '\0';
    return true;
}

kk_ody_target kk_ody_parse_target(const char *input, char *out, size_t size)
{
    while (isspace((unsigned char)*input))
        input++;
    char buf[512];
    size_t n = strcspn(input, " \t\r\n");
    if (n == 0 || n >= sizeof buf)
        return KK_ODY_TARGET_INVALID;
    memcpy(buf, input, n);
    buf[n] = '\0';

    const char *p = buf;
    bool link = false;
    if (strncmp(p, "lbry://", 7) == 0) {
        p += 7;
        link = true;
    } else {
        const char *host = strstr(p, "odysee.com/");
        if (host && (host == p || host[-1] == '/' || host[-1] == '.')) {
            p = host + strlen("odysee.com/");
            link = true;
            /* A web link: "?" starts the query, "#" is not part of it. */
            buf[strcspn(buf, "?#")] = '\0';
        }
    }
    size_t len = strlen(p);
    while (len > 0 && p[len - 1] == '/')
        len--;
    if (is_claim_id(p, len)) {
        if (size <= CLAIM_LEN)
            return KK_ODY_TARGET_INVALID;
        for (size_t i = 0; i < CLAIM_LEN; i++)
            out[i] = (char)tolower((unsigned char)p[i]);
        out[CLAIM_LEN] = '\0';
        return KK_ODY_TARGET_CLAIM;
    }
    if (p[0] != '@' && !link)
        return KK_ODY_TARGET_INVALID;
    if (len == 0 || (p[0] == '@' && len == 1))
        return KK_ODY_TARGET_INVALID;

    size_t o = 0;
    if (size < 8)
        return KK_ODY_TARGET_INVALID;
    memcpy(out, "lbry://", 7);
    o = 7;
    size_t first = strcspn(p, "/");
    if (first > len)
        first = len;
    if (!copy_name(p, first, out, size, &o))
        return KK_ODY_TARGET_INVALID;
    if (first == len)
        return p[0] == '@' ? KK_ODY_TARGET_CHANNEL : KK_ODY_TARGET_STREAM;
    /* "@channel/stream": the stream is what we want. */
    const char *s = p + first + 1;
    size_t sn = len - first - 1;
    if (p[0] != '@' || sn == 0 || memchr(s, '/', sn) || o + 1 >= size)
        return KK_ODY_TARGET_INVALID;
    out[o++] = '/';
    if (!copy_name(s, sn, out, size, &o))
        return KK_ODY_TARGET_INVALID;
    return KK_ODY_TARGET_STREAM;
}

bool kk_ody_parse_resolve(const char *json, char *claim, size_t claim_size, char *channel,
                          size_t channel_size, bool *is_channel)
{
    cJSON *root = cJSON_Parse(json);
    const cJSON *result = kk_json_path(root, "result");
    const cJSON *first = result && result->child ? result->child : NULL;
    const char *id = kk_json_str(first, "claim_id");
    const char *type = kk_json_str(first, "value_type");
    bool ok = id && is_claim_id(id, strlen(id)) && claim_size > CLAIM_LEN;
    if (ok) {
        *is_channel = type && strcmp(type, "channel") == 0;
        const char *name = *is_channel ? kk_json_str(first, "name")
                                       : kk_json_str(first, "signing_channel.name");
        snprintf(claim, claim_size, "%s", id);
        snprintf(channel, channel_size, "%s", name ? name : "");
    }
    cJSON_Delete(root);
    return ok;
}

bool kk_ody_parse_is_live(const char *json, bool *live, char *claim, size_t claim_size)
{
    cJSON *root = cJSON_Parse(json);
    const cJSON *data = kk_json_path(root, "data");
    bool ok = cJSON_IsObject(data);
    if (ok) {
        const char *id = kk_json_str(data, "ActiveClaim.ClaimID");
        *live = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(data, "Live"));
        if (id && is_claim_id(id, strlen(id)))
            snprintf(claim, claim_size, "%s", id);
        else
            *live = false;
        if (!*live && claim_size)
            claim[0] = '\0';
    }
    cJSON_Delete(root);
    return ok;
}

/* ---- messages ------------------------------------------------------------ */

typedef struct {
    char text[MAX_TEXT];
    size_t len;
    kk_emote list[MAX_EMOTES];
    char ids[MAX_EMOTES][48];
    int n;
} parsed;

static void put(parsed *p, const char *s, size_t n)
{
    if (n > sizeof p->text - 1 - p->len)
        n = sizeof p->text - 1 - p->len;
    memcpy(p->text + p->len, s, n);
    p->len += n;
    p->text[p->len] = '\0';
}

static int named_cmp(const void *key, const void *item)
{
    return strcmp(key, ((const kk_ody_named *)item)->name);
}

static const kk_ody_named *find(const kk_ody_named *table, size_t n, const char *name)
{
    return bsearch(name, table, n, sizeof *table, named_cmp);
}

#define FIND(table, name) find(table, sizeof table / sizeof table[0], name)

/* Puts name in the text as an image emote. */
static void put_emote(parsed *p, const char *prefix, const char *name, const char *url)
{
    size_t start = p->len;
    put(p, name, strlen(name));
    int k = p->n;
    if (k == MAX_EMOTES || p->len != start + strlen(name))
        return;
    snprintf(p->ids[k], sizeof p->ids[0], "%s%s", prefix, name);
    p->list[k] = (kk_emote){.id = p->ids[k], .name = name, .url = url, .start = start,
                            .len = p->len - start};
    p->n++;
}

static bool is_code_char(char c)
{
    return isalnum((unsigned char)c) || c == '_' || c == '-' || c == '+';
}

static bool is_mention_char(char c)
{
    return isalnum((unsigned char)c) || c == '_' || c == '-' || c == '.' ||
           (unsigned char)c >= 0x80;
}

static void parse_comment(parsed *p, const char *s)
{
    p->len = 0;
    p->text[0] = '\0';
    p->n = 0;
    const char *start = s;
    while (*s) {
        char name[64];
        /* <stkr>:NAME:<stkr> */
        if (strncmp(s, "<stkr>", 6) == 0) {
            const char *in = s + 6;
            const char *end = strstr(in, "<stkr>");
            size_t n = end ? (size_t)(end - in) : 0;
            const kk_ody_named *st = NULL;
            if (n > 2 && n < sizeof name) {
                memcpy(name, in, n);
                name[n] = '\0';
                st = FIND(kk_ody_stickers, name);
            }
            if (st) {
                put_emote(p, "stkr", st->name, st->value);
                s = end + 6;
                continue;
            }
        }
        /* :name: */
        if (*s == ':') {
            size_t n = 1;
            while (n < sizeof name - 1 && is_code_char(s[n]))
                n++;
            if (n > 1 && s[n] == ':' && n + 1 < sizeof name) {
                memcpy(name, s, n + 1);
                name[n + 1] = '\0';
                const kk_ody_named *e = FIND(kk_ody_emotes, name);
                const kk_ody_named *tw = e ? NULL : FIND(kk_ody_twemoji, name);
                if (e) {
                    put_emote(p, "", e->name, e->value);
                    s += n + 1;
                    continue;
                }
                if (tw) {
                    put(p, tw->value, strlen(tw->value));
                    s += n + 1;
                    continue;
                }
            }
        }
        /* @name:claim → @name */
        if (*s == '@' && (s == start || s[-1] == ' ' || s[-1] == '\n')) {
            size_t n = 1;
            while (is_mention_char(s[n]))
                n++;
            size_t hex = 0;
            if (n > 1 && s[n] == ':')
                while (isxdigit((unsigned char)s[n + 1 + hex]))
                    hex++;
            if (hex > 0) {
                put(p, s, n);
                s += n + 1 + hex;
                continue;
            }
        }
        put(p, s, 1);
        s++;
    }
}

static bool emit_comment(const cJSON *c, const char *source, const kk_chat_sink *sink)
{
    const char *text = kk_json_str(c, "comment");
    const char *user = kk_json_str(c, "channel_id");
    const char *name = kk_json_str(c, "channel_name");
    if (!text || !user || !user[0] || !name || !name[0])
        return false;
    kk_chat_msg m = {.platform = "odysee", .source = source, .user_id = user, .name = name,
                     .kind = KK_MSG_TEXT};
#define FLAG(k) cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(c, k))
    if (FLAG("is_creator"))
        m.badges |= KK_BADGE_OWNER;
    if (FLAG("is_moderator") || FLAG("is_global_mod"))
        m.badges |= KK_BADGE_MOD;
    char amount[48];
    double tip = kk_json_num(c, "support_amount", 0);
    if (tip > 0) {
        const char *cur = kk_json_str(c, "currency");
        if (FLAG("is_fiat"))
            snprintf(amount, sizeof amount, "%s %.2f", cur && cur[0] ? cur : "USD", tip);
        else
            snprintf(amount, sizeof amount, "%g LBC", tip);
        m.kind = KK_MSG_PAID;
        m.amount = amount;
    }
#undef FLAG
    parsed *p = malloc(sizeof *p);
    if (!p)
        return false;
    parse_comment(p, text);
    m.text = p->text;
    m.emotes = p->list;
    m.n_emotes = p->n;
    sink->on_msg(sink->ud, &m);
    free(p);
    return true;
}

bool kk_ody_emit_event(const char *json, const char *source, const kk_chat_sink *sink)
{
    cJSON *root = cJSON_Parse(json);
    const char *type = kk_json_str(root, "type");
    bool ok = type && strcmp(type, "delta") == 0 &&
              emit_comment(kk_json_path(root, "data.comment"), source, sink);
    cJSON_Delete(root);
    return ok;
}

/* ---- client -------------------------------------------------------------- */

typedef enum {
    ST_WAIT,    /* until next_at, then the next step */
    ST_RESOLVE, /* asking what the target is */
    ST_IS_LIVE, /* asking whether the channel is live */
    ST_WS,      /* connecting or connected */
} state;

struct kk_odysee {
    kk_http *http;
    kk_chat_sink sink;
    kk_ody_target kind;
    char target[512]; /* claim id or lbry:// url */
    char label[80];   /* the source of its messages */

    char channel_id[CLAIM_LEN + 1]; /* kind CHANNEL, once resolved */
    char channel[256];              /* "@name": the socket's category */
    char claim[CLAIM_LEN + 1];      /* the stream whose chat we read */
    bool resolved;

    kk_ws *ws;
    state st;
    double now, next_at, next_check;
    bool connected, said_offline, busy_check;
    int failures;
};

static double backoff(kk_odysee *o)
{
    o->failures++;
    return fmin(60.0, pow(2.0, fmin(o->failures, 6)));
}

static void wait_for(kk_odysee *o, double delay)
{
    o->st = ST_WAIT;
    o->next_at = o->now + delay;
}

static void http_failed(kk_odysee *o, const char *what, long status, const char *err)
{
    double delay = backoff(o);
    if (err)
        kk_log_warn("Odysee %s: %s: %s (nova tentativa em %.0f s)", o->label, what, err, delay);
    else
        kk_log_warn("Odysee %s: %s: HTTP %ld (nova tentativa em %.0f s)", o->label, what,
                    status, delay);
    wait_for(o, delay);
}

/* The chat of o->claim, from now. */
static void connect_chat(kk_odysee *o)
{
    char cat[3 * sizeof o->channel], url[700];
    const char *src = o->channel[0] ? o->channel : o->claim;
    size_t c = 0;
    for (const char *s = src; *s && c + 4 < sizeof cat; s++) {
        unsigned char ch = (unsigned char)*s;
        if (isalnum(ch) || ch == '-' || ch == '_' || ch == '.')
            cat[c++] = (char)ch;
        else
            c += (size_t)snprintf(cat + c, sizeof cat - c, "%%%02X", ch);
    }
    cat[c] = '\0';
    o->connected = false;
    o->st = ST_WS;
    o->next_check = o->now + LIVE_RECHECK_S;
    if (!kk_pathf(url, sizeof url, SOCKET_URL, o->claim, cat) ||
        !kk_ws_connect(o->ws, url, o->now)) {
        double delay = backoff(o);
        kk_log_warn("Odysee %s: não consegui iniciar a conexão (nova tentativa em %.0f s)",
                    o->label, delay);
        wait_for(o, delay);
    }
}

static void on_is_live(void *ud, long status, const char *body, size_t len, const char *err)
{
    (void)len;
    kk_odysee *o = ud;
    bool checking = o->busy_check;
    o->busy_check = false;
    bool live;
    char claim[CLAIM_LEN + 1];
    if (status != 200 || !kk_ody_parse_is_live(body, &live, claim, sizeof claim)) {
        if (!checking)
            http_failed(o, "estado da live", status, err);
        return;
    }
    if (checking) {
        /* Reading a live: only a new one changes anything. */
        if (live && strcmp(claim, o->claim) != 0) {
            kk_log_info("Odysee %s: live nova, passando para ela", o->label);
            snprintf(o->claim, sizeof o->claim, "%s", claim);
            kk_ws_close(o->ws);
            connect_chat(o);
        }
        return;
    }
    if (!live) {
        if (!o->said_offline)
            kk_log_info("Odysee %s: o canal não está ao vivo; esperando", o->label);
        o->said_offline = true;
        o->failures = 0;
        wait_for(o, OFFLINE_RETRY_S);
        return;
    }
    o->said_offline = false;
    snprintf(o->claim, sizeof o->claim, "%s", claim);
    connect_chat(o);
}

static void ask_is_live(kk_odysee *o, bool checking)
{
    char url[160];
    if (kk_pathf(url, sizeof url, IS_LIVE_URL, o->channel_id) &&
        kk_http_get(o->http, url, on_is_live, o) == 0) {
        o->busy_check = checking;
        if (!checking)
            o->st = ST_IS_LIVE;
    } else if (!checking) {
        wait_for(o, backoff(o));
    }
}

static void on_resolve(void *ud, long status, const char *body, size_t len, const char *err)
{
    (void)len;
    kk_odysee *o = ud;
    char claim[CLAIM_LEN + 1];
    bool is_channel;
    if (status != 200) {
        http_failed(o, "endereço", status, err);
        return;
    }
    if (!kk_ody_parse_resolve(body, claim, sizeof claim, o->channel, sizeof o->channel,
                              &is_channel)) {
        /* The proxy sometimes says NOT_FOUND for what exists: ask again,
         * slowly once it keeps saying so. */
        double delay = o->failures >= 3 ? NOT_FOUND_RETRY_S : backoff(o);
        if (o->failures >= 3)
            o->failures++;
        kk_log_warn("Odysee: não achei %s (nova tentativa em %.0f s)", o->target, delay);
        wait_for(o, delay);
        return;
    }
    o->resolved = true;
    if (o->kind == KK_ODY_TARGET_CHANNEL) {
        snprintf(o->channel_id, sizeof o->channel_id, "%s", claim);
        ask_is_live(o, false);
    } else {
        snprintf(o->claim, sizeof o->claim, "%s", claim);
        connect_chat(o);
    }
}

static void resolve(kk_odysee *o)
{
    cJSON *req = cJSON_CreateObject();
    cJSON *params = cJSON_AddObjectToObject(req, "params");
    cJSON *urls = cJSON_AddArrayToObject(params, "urls");
    cJSON_AddItemToArray(urls, cJSON_CreateString(o->target));
    cJSON_AddStringToObject(req, "jsonrpc", "2.0");
    cJSON_AddStringToObject(req, "method", "resolve");
    cJSON_AddNumberToObject(req, "id", 1);
    char *body = cJSON_PrintUnformatted(req);
    cJSON_Delete(req);
    if (body && kk_http_post_json(o->http, RESOLVE_URL, body, on_resolve, o) == 0)
        o->st = ST_RESOLVE;
    else
        wait_for(o, backoff(o));
    cJSON_free(body);
}

static void on_ws_open(void *ud)
{
    kk_odysee *o = ud;
    if (!o->connected)
        kk_log_info("Odysee: conectado ao chat de %s", o->label);
    o->connected = true;
    o->failures = 0;
}

static void on_ws_message(void *ud, char *text, size_t len)
{
    (void)len;
    kk_odysee *o = ud;
    kk_ody_emit_event(text, o->label, &o->sink);
}

kk_odysee *kk_odysee_new(kk_http *http, const char *target, const kk_chat_sink *sink)
{
    kk_odysee *o = calloc(1, sizeof *o);
    if (!o)
        return NULL;
    o->kind = kk_ody_parse_target(target, o->target, sizeof o->target);
    if (o->kind == KK_ODY_TARGET_INVALID) {
        kk_log_error("Odysee: não entendi \"%s\" (use o link da live ou do canal, o "
                     "@canal ou o id da live)",
                     target);
        free(o);
        return NULL;
    }
    /* "@channel" (without the claim part) or the start of the id. */
    if (o->kind == KK_ODY_TARGET_CLAIM)
        snprintf(o->label, sizeof o->label, "%.8s", o->target);
    else
        snprintf(o->label, sizeof o->label, "%.*s", (int)strcspn(o->target + 7, ":/"),
                 o->target + 7);
    o->http = http;
    o->sink = *sink;
    o->ws = kk_ws_new(http, &(kk_ws_cb){on_ws_open, on_ws_message}, o);
    if (!o->ws) {
        kk_odysee_free(o);
        return NULL;
    }
    o->st = ST_WAIT;
    return o;
}

void kk_odysee_free(kk_odysee *o)
{
    if (!o)
        return;
    kk_http_cancel(o->http, o);
    kk_ws_free(o->ws);
    free(o);
}

int kk_odysee_pollfd(const kk_odysee *o, struct pollfd *fd)
{
    return kk_ws_pollfd(o->ws, fd);
}

void kk_odysee_tick(kk_odysee *o, const struct pollfd *fd, double now)
{
    o->now = now;
    switch (o->st) {
    case ST_WAIT:
        if (now < o->next_at)
            return;
        if (o->kind == KK_ODY_TARGET_CLAIM) {
            memcpy(o->claim, o->target, CLAIM_LEN);
            o->claim[CLAIM_LEN] = '\0';
            connect_chat(o);
        } else if (!o->resolved) {
            resolve(o);
        } else if (o->kind == KK_ODY_TARGET_CHANNEL && !o->claim[0]) {
            ask_is_live(o, false);
        } else {
            connect_chat(o);
        }
        return;
    case ST_RESOLVE:
    case ST_IS_LIVE:
        return;
    case ST_WS: {
        const char *why;
        if (!kk_ws_pump(o->ws, fd, now, &why)) {
            double delay = backoff(o);
            kk_log_warn("Odysee %s: chat: %s (nova tentativa em %.0f s)", o->label, why, delay);
            o->connected = false;
            kk_http_cancel(o->http, o);
            o->busy_check = false;
            /* A channel: maybe the live ended and a new one began. */
            if (o->kind == KK_ODY_TARGET_CHANNEL)
                o->claim[0] = '\0';
            wait_for(o, delay);
            return;
        }
        if (o->kind == KK_ODY_TARGET_CHANNEL && !o->busy_check && now >= o->next_check) {
            o->next_check = now + LIVE_RECHECK_S;
            ask_is_live(o, true);
        }
        return;
    }
    }
}
