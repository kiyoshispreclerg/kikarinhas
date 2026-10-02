/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "twitch.h"

#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include <openssl/err.h>
#include <openssl/ssl.h>
#include <openssl/x509v3.h>

#include "cJSON.h"
#include "json.h"
#include "log.h"
#include "util.h"

#define HOST "irc.chat.twitch.tv"
#define PORT 6697
#define MAX_EMOTES 32
/* Twitch pings every ~5 min; without traffic for this long, we ask... */
#define IDLE_PING_S 240.0
/* ...and after this long the connection is taken as dead. */
#define IDLE_DROP_S 300.0
#define HANDSHAKE_TIMEOUT_S 20.0
#define JOIN_TIMEOUT_S 20.0
/* "2.0" is 56 px, close to the wall's sizes; static: the first frame of an
 * animated one, as PNG. */
#define TWITCH_EMOTE_URL "https://static-cdn.jtvnw.net/emoticons/v2/%s/static/dark/2.0"

/* ---- target -------------------------------------------------------------- */

static bool is_login_char(char c)
{
    return isalnum((unsigned char)c) || c == '_';
}

/* Copies the leading login of s (up to "/", "?", "#" or the end). */
static bool take_login(const char *s, char *out, size_t size)
{
    size_t n = strcspn(s, "/?#");
    if (n == 0 || n > 25 || n >= size)
        return false;
    for (size_t i = 0; i < n; i++) {
        if (!is_login_char(s[i]))
            return false;
        out[i] = (char)tolower((unsigned char)s[i]);
    }
    out[n] = '\0';
    return true;
}

bool kk_tw_parse_target(const char *input, char *out, size_t size)
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
    const char *host = strstr(p, "twitch.tv/");
    if (host) {
        p = host + strlen("twitch.tv/");
        /* The pop-out chat: twitch.tv/popout/NAME/chat. */
        if (strncmp(p, "popout/", 7) == 0)
            p += 7;
        return take_login(p, out, size);
    }
    if (strchr(p, '/') || strchr(p, '.'))
        return false;
    if (*p == '#')
        p++;
    return p[strcspn(p, "?#")] == '\0' && take_login(p, out, size);
}

/* ---- IRC lines ----------------------------------------------------------- */

static char *skip_spaces(char *p)
{
    while (*p == ' ')
        p++;
    return p;
}

/* Cuts the word at p and returns what follows it. */
static char *cut_word(char *p)
{
    char *sp = strchr(p, ' ');
    if (!sp)
        return p + strlen(p);
    *sp = '\0';
    return skip_spaces(sp + 1);
}

bool kk_tw_split(char *line, kk_tw_line *out)
{
    *out = (kk_tw_line){.nick = "", .command = "", .channel = "", .trailing = ""};
    size_t n = strlen(line);
    while (n > 0 && (line[n - 1] == '\r' || line[n - 1] == '\n'))
        line[--n] = '\0';
    char *p = line;
    if (*p == '@') {
        out->tags = p + 1;
        p = cut_word(p);
    }
    if (*p == ':') {
        char *prefix = p + 1;
        p = cut_word(p);
        char *bang = strchr(prefix, '!');
        if (bang)
            *bang = '\0';
        out->nick = prefix;
    }
    if (!*p)
        return false;
    out->command = p;
    p = cut_word(p);
    bool first = true;
    while (*p) {
        if (*p == ':') {
            out->trailing = p + 1;
            break;
        }
        char *param = p;
        p = cut_word(p);
        if (first && param[0] == '#')
            out->channel = param;
        first = false;
    }
    return true;
}

bool kk_tw_tag(const char *tags, const char *key, char *out, size_t size)
{
    if (!tags || size == 0)
        return false;
    size_t klen = strlen(key);
    const char *p = tags;
    while (*p) {
        size_t len = strcspn(p, ";");
        if (len >= klen && strncmp(p, key, klen) == 0 &&
            (len == klen || p[klen] == '=')) {
            const char *v = len == klen ? p + len : p + klen + 1;
            const char *end = p + len;
            size_t o = 0;
            while (v < end && o + 1 < size) {
                char c = *v++;
                if (c == '\\' && v < end) {
                    char e = *v++;
                    c = e == 's' ? ' ' : e == ':' ? ';' : e == 'r' ? '\r'
                        : e == 'n' ? '\n' : e;
                }
                out[o++] = c;
            }
            /* Cut short: don't leave half a UTF-8 sequence. */
            if (v < end) {
                size_t k = o;
                while (k > 0 && ((unsigned char)out[k - 1] & 0xC0) == 0x80)
                    k--;
                unsigned char lead = k > 0 ? (unsigned char)out[k - 1] : 0;
                size_t need = lead >= 0xF0 ? 4 : lead >= 0xE0 ? 3 : lead >= 0xC0 ? 2 : 1;
                if (lead >= 0xC0 && o - (k - 1) < need)
                    o = k - 1;
            }
            out[o] = '\0';
            return true;
        }
        p += len;
        if (*p == ';')
            p++;
    }
    return false;
}

/* ---- third-party emotes -------------------------------------------------- */

typedef struct {
    char *name, *id, *url;
    int src;
} entry;

struct kk_tw_emotes {
    entry *v;
    int n, cap;
};

kk_tw_emotes *kk_tw_emotes_new(void)
{
    return calloc(1, sizeof(kk_tw_emotes));
}

static void entry_free(entry *e)
{
    free(e->name);
    free(e->id);
    free(e->url);
}

void kk_tw_emotes_free(kk_tw_emotes *set)
{
    if (!set)
        return;
    for (int i = 0; i < set->n; i++)
        entry_free(&set->v[i]);
    free(set->v);
    free(set);
}

void kk_tw_emotes_clear(kk_tw_emotes *set, kk_tw_source src)
{
    int o = 0;
    for (int i = 0; i < set->n; i++) {
        if (set->v[i].src == (int)src)
            entry_free(&set->v[i]);
        else
            set->v[o++] = set->v[i];
    }
    set->n = o;
}

int kk_tw_emotes_count(const kk_tw_emotes *set)
{
    return set->n;
}

static bool add_entry(kk_tw_emotes *set, kk_tw_source src, const char *name,
                      const char *id, const char *url)
{
    if (!name || !name[0] || strchr(name, ' '))
        return false;
    if (set->n == set->cap) {
        int cap = set->cap ? set->cap * 2 : 256;
        entry *v = realloc(set->v, (size_t)cap * sizeof *v);
        if (!v)
            return false;
        set->v = v;
        set->cap = cap;
    }
    entry e = {strdup(name), strdup(id), strdup(url), (int)src};
    if (!e.name || !e.id || !e.url) {
        entry_free(&e);
        return false;
    }
    set->v[set->n++] = e;
    return true;
}

/* By name, then the strongest source first. */
static int entry_cmp(const void *a, const void *b)
{
    const entry *x = a, *y = b;
    int c = strcmp(x->name, y->name);
    return c ? c : y->src - x->src;
}

static int word_cmp(const char *w, size_t len, const char *name)
{
    int c = strncmp(w, name, len);
    if (c)
        return c;
    return name[len] ? -1 : 0;
}

const kk_tw_emote *kk_tw_emotes_find(const kk_tw_emotes *set, const char *word,
                                     size_t len)
{
    int lo = 0, hi = set->n;
    while (lo < hi) {
        int mid = lo + (hi - lo) / 2;
        if (word_cmp(word, len, set->v[mid].name) > 0)
            lo = mid + 1;
        else
            hi = mid;
    }
    if (lo < set->n && word_cmp(word, len, set->v[lo].name) == 0)
        return (const kk_tw_emote *)&set->v[lo];
    return NULL;
}

/* {id, code} in an array: BTTV. */
static int load_bttv(kk_tw_emotes *set, kk_tw_source src, const cJSON *list)
{
    int n = 0;
    const cJSON *e;
    cJSON_ArrayForEach(e, list)
    {
        const char *id = kk_json_str(e, "id");
        char url[256], key[128];
        if (id && kk_pathf(url, sizeof url, "https://cdn.betterttv.net/emote/%s/2x.png", id) &&
            kk_pathf(key, sizeof key, "bttv:%s", id) &&
            add_entry(set, src, kk_json_str(e, "code"), key, url))
            n++;
    }
    return n;
}

/* One FFZ set: {emoticons: [{id, name, urls: {"1", "2", "4"}}]}. */
static int load_ffz_set(kk_tw_emotes *set, kk_tw_source src, const cJSON *s)
{
    int n = 0;
    const cJSON *e;
    cJSON_ArrayForEach(e, cJSON_GetObjectItemCaseSensitive(s, "emoticons"))
    {
        const cJSON *id = cJSON_GetObjectItemCaseSensitive(e, "id");
        const char *u = kk_json_str(e, "urls.2");
        if (!u)
            u = kk_json_str(e, "urls.1");
        char url[256], key[64];
        if (cJSON_IsNumber(id) && u &&
            kk_pathf(url, sizeof url, "%s%s", strncmp(u, "//", 2) == 0 ? "https:" : "", u) &&
            kk_pathf(key, sizeof key, "ffz:%.0f", id->valuedouble) &&
            add_entry(set, src, kk_json_str(e, "name"), key, url))
            n++;
    }
    return n;
}

/* 7TV: [{id, name, data: {animated, host: {url: "//cdn.7tv.app/emote/ID"}}}].
 * The name is the set's (it may be an alias). Static PNG either way:
 * animated ones only have it as 2x_static.png, still ones as 2x.png. */
static int load_7tv(kk_tw_emotes *set, kk_tw_source src, const cJSON *list)
{
    int n = 0;
    const cJSON *e;
    cJSON_ArrayForEach(e, list)
    {
        const char *id = kk_json_str(e, "id");
        const char *host = kk_json_str(e, "data.host.url");
        bool animated = cJSON_IsTrue(kk_json_path(e, "data.animated"));
        char url[256], key[128];
        if (id && host &&
            kk_pathf(url, sizeof url, "%s%s/%s", strncmp(host, "//", 2) == 0 ? "https:" : "",
                     host, animated ? "2x_static.png" : "2x.png") &&
            kk_pathf(key, sizeof key, "7tv:%s", id) &&
            add_entry(set, src, kk_json_str(e, "name"), key, url))
            n++;
    }
    return n;
}

int kk_tw_emotes_load(kk_tw_emotes *set, kk_tw_source src, const char *json)
{
    cJSON *root = cJSON_Parse(json);
    if (!root)
        return -1;
    kk_tw_emotes_clear(set, src);
    int n = -1;
    const cJSON *x;
    switch (src) {
    case KK_TW_BTTV_GLOBAL:
        if (cJSON_IsArray(root))
            n = load_bttv(set, src, root);
        break;
    case KK_TW_BTTV_CHANNEL:
        if (cJSON_IsObject(root)) {
            n = load_bttv(set, src, cJSON_GetObjectItemCaseSensitive(root, "channelEmotes"));
            n += load_bttv(set, src, cJSON_GetObjectItemCaseSensitive(root, "sharedEmotes"));
        }
        break;
    case KK_TW_FFZ_GLOBAL: {
        const cJSON *sets = cJSON_GetObjectItemCaseSensitive(root, "sets");
        if (!cJSON_IsObject(sets))
            break;
        n = 0;
        cJSON_ArrayForEach(x, cJSON_GetObjectItemCaseSensitive(root, "default_sets"))
        {
            char k[32];
            if (cJSON_IsNumber(x) && kk_pathf(k, sizeof k, "%.0f", x->valuedouble))
                n += load_ffz_set(set, src, cJSON_GetObjectItemCaseSensitive(sets, k));
        }
        break;
    }
    case KK_TW_FFZ_CHANNEL: {
        const cJSON *sets = cJSON_GetObjectItemCaseSensitive(root, "sets");
        if (!cJSON_IsObject(sets))
            break;
        n = 0;
        cJSON_ArrayForEach(x, sets) n += load_ffz_set(set, src, x);
        break;
    }
    case KK_TW_7TV_GLOBAL:
        x = cJSON_GetObjectItemCaseSensitive(root, "emotes");
        if (cJSON_IsArray(x))
            n = load_7tv(set, src, x);
        break;
    case KK_TW_7TV_CHANNEL:
        /* A user without a set has "emote_set": null. */
        if (cJSON_IsObject(root))
            n = load_7tv(set, src, kk_json_path(root, "emote_set.emotes"));
        break;
    default:
        break;
    }
    cJSON_Delete(root);
    if (set->n > 1)
        qsort(set->v, (size_t)set->n, sizeof *set->v, entry_cmp);
    return n;
}

/* ---- messages ------------------------------------------------------------ */

typedef struct {
    const char *id, *name, *url;
    size_t start, len;
} found_emote;

typedef struct {
    found_emote list[MAX_EMOTES];
    char ids[MAX_EMOTES][80], names[MAX_EMOTES][128], urls[MAX_EMOTES][200];
    int n;
} emote_buf;

/* Byte offset of code point cp in text (len bytes); len if past the end. */
static size_t cp_offset(const char *text, size_t len, long cp)
{
    size_t i = 0;
    for (long k = 0; i < len && k < cp; k++) {
        i++;
        while (i < len && ((unsigned char)text[i] & 0xC0) == 0x80)
            i++;
    }
    return i;
}

static bool overlaps(const emote_buf *eb, size_t start, size_t len)
{
    for (int i = 0; i < eb->n; i++)
        if (start < eb->list[i].start + eb->list[i].len &&
            eb->list[i].start < start + len)
            return true;
    return false;
}

/* The "emotes" tag: "25:0-4,12-16/emotesv2_abc:6-10", in code points. */
static void native_emotes(emote_buf *eb, const char *tag, const char *text)
{
    size_t tlen = strlen(text);
    const char *p = tag;
    while (*p && eb->n < MAX_EMOTES) {
        size_t idlen = strcspn(p, ":/");
        if (p[idlen] != ':' || idlen == 0 || idlen >= sizeof eb->ids[0])
            return;
        char id[sizeof eb->ids[0]];
        memcpy(id, p, idlen);
        id[idlen] = '\0';
        p += idlen + 1;
        while (*p && *p != '/' && eb->n < MAX_EMOTES) {
            char *end;
            long a = strtol(p, &end, 10);
            if (end == p || *end != '-')
                return;
            long b = strtol(end + 1, &end, 10);
            p = end;
            if (*p == ',')
                p++;
            size_t start = cp_offset(text, tlen, a);
            size_t stop = cp_offset(text, tlen, b + 1);
            int k = eb->n;
            if (b < a || start >= stop || overlaps(eb, start, stop - start) ||
                !kk_pathf(eb->urls[k], sizeof eb->urls[0], TWITCH_EMOTE_URL, id))
                continue;
            snprintf(eb->ids[k], sizeof eb->ids[0], "%s", id);
            snprintf(eb->names[k], sizeof eb->names[0], "%.*s", (int)(stop - start),
                     text + start);
            eb->list[k] = (found_emote){eb->ids[k], eb->names[k], eb->urls[k], start,
                                        stop - start};
            eb->n++;
        }
        if (*p == '/')
            p++;
    }
}

/* Words of text that name a BTTV/FFZ/7TV emote. */
static void extra_emotes(emote_buf *eb, const kk_tw_emotes *extra, const char *text)
{
    const char *p = text;
    while (*p && eb->n < MAX_EMOTES) {
        while (*p == ' ')
            p++;
        size_t len = strcspn(p, " ");
        size_t start = (size_t)(p - text);
        const kk_tw_emote *e = len ? kk_tw_emotes_find(extra, p, len) : NULL;
        if (e && !overlaps(eb, start, len))
            eb->list[eb->n++] = (found_emote){e->id, e->name, e->url, start, len};
        p += len;
    }
}

static unsigned parse_badges(const char *tags)
{
    char b[512];
    unsigned badges = 0;
    if (!kk_tw_tag(tags, "badges", b, sizeof b))
        return 0;
    for (char *p = b; *p;) {
        size_t len = strcspn(p, ",");
        size_t name = strcspn(p, "/,");
#define IS(s) (name == strlen(s) && strncmp(p, s, name) == 0)
        if (IS("broadcaster"))
            badges |= KK_BADGE_OWNER;
        else if (IS("moderator"))
            badges |= KK_BADGE_MOD;
        else if (IS("subscriber") || IS("founder"))
            badges |= KK_BADGE_MEMBER;
        else if (IS("partner"))
            badges |= KK_BADGE_VERIFIED;
#undef IS
        p += len;
        if (*p == ',')
            p++;
    }
    return badges;
}

static bool is_member_notice(const char *id)
{
    static const char *const ids[] = {
        "sub",           "resub",           "subgift",
        "submysterygift", "giftpaidupgrade", "anongiftpaidupgrade",
        "primepaidupgrade", "standardpayforward", "communitypayforward",
    };
    for (size_t i = 0; i < sizeof ids / sizeof ids[0]; i++)
        if (strcmp(id, ids[i]) == 0)
            return true;
    return false;
}

/* Hype Chat: "USD 5.00" from amount 500, exponent 2. */
static bool hype_amount(const char *tags, char *out, size_t size)
{
    char amount[32], exp_s[8], cur[16];
    if (!kk_tw_tag(tags, "pinned-chat-paid-amount", amount, sizeof amount))
        return false;
    if (!kk_tw_tag(tags, "pinned-chat-paid-currency", cur, sizeof cur))
        cur[0] = '\0';
    long exp = kk_tw_tag(tags, "pinned-chat-paid-exponent", exp_s, sizeof exp_s)
                   ? strtol(exp_s, NULL, 10) : 0;
    long long v = strtoll(amount, NULL, 10);
    if (exp <= 0 || exp > 6) {
        snprintf(out, size, "%s%s%lld", cur, cur[0] ? " " : "", v);
        return true;
    }
    long long div = 1;
    for (long i = 0; i < exp; i++)
        div *= 10;
    snprintf(out, size, "%s%s%lld.%0*lld", cur, cur[0] ? " " : "", v / div, (int)exp,
             v % div);
    return true;
}

static bool emit(const kk_tw_line *l, const kk_tw_emotes *extra, const kk_chat_sink *sink)
{
    bool notice = strcmp(l->command, "USERNOTICE") == 0;
    if ((!notice && strcmp(l->command, "PRIVMSG") != 0) || !l->tags)
        return false;

    char user_id[32], name[128], msg_id[48], system[512], amount[64], bits[16], tag[2048];
    kk_chat_msg m = {.platform = "twitch", .kind = KK_MSG_TEXT};
    if (!kk_tw_tag(l->tags, "user-id", user_id, sizeof user_id) || !user_id[0])
        return false;
    if ((!kk_tw_tag(l->tags, "display-name", name, sizeof name) || !name[0]) &&
        (!kk_tw_tag(l->tags, "login", name, sizeof name) || !name[0]))
        snprintf(name, sizeof name, "%s", l->nick);
    if (!name[0])
        return false;

    char *text = l->trailing;
    /* "/me": the text sits inside \1ACTION ...\1; emote positions count
     * from the inner text. */
    if (strncmp(text, "\001ACTION ", 8) == 0) {
        text += 8;
        size_t n = strlen(text);
        if (n > 0 && text[n - 1] == '\001')
            text[n - 1] = '\0';
    }
    bool own_text = true; /* false: Twitch's sentence, no emotes tag for it */

    if (notice) {
        if (!kk_tw_tag(l->tags, "msg-id", msg_id, sizeof msg_id))
            return false;
        if (is_member_notice(msg_id))
            m.kind = KK_MSG_MEMBER;
        else if (strcmp(msg_id, "raid") && strcmp(msg_id, "announcement"))
            return false;
        if (!text[0]) {
            if (!kk_tw_tag(l->tags, "system-msg", system, sizeof system))
                system[0] = '\0';
            text = system;
            own_text = false;
        }
    } else if (kk_tw_tag(l->tags, "bits", bits, sizeof bits) && bits[0]) {
        m.kind = KK_MSG_PAID;
        snprintf(amount, sizeof amount, "%s bits", bits);
        m.amount = amount;
    } else if (hype_amount(l->tags, amount, sizeof amount)) {
        m.kind = KK_MSG_PAID;
        m.amount = amount;
    }

    emote_buf eb;
    eb.n = 0;
    if (own_text && kk_tw_tag(l->tags, "emotes", tag, sizeof tag))
        native_emotes(&eb, tag, text);
    if (extra)
        extra_emotes(&eb, extra, text);
    /* In text order. */
    for (int i = 1; i < eb.n; i++)
        for (int j = i; j > 0 && eb.list[j].start < eb.list[j - 1].start; j--) {
            found_emote t = eb.list[j];
            eb.list[j] = eb.list[j - 1];
            eb.list[j - 1] = t;
        }
    kk_emote emotes[MAX_EMOTES];
    for (int i = 0; i < eb.n; i++)
        emotes[i] = (kk_emote){.id = eb.list[i].id, .name = eb.list[i].name,
                               .url = eb.list[i].url, .start = eb.list[i].start,
                               .len = eb.list[i].len};

    m.user_id = user_id;
    m.name = name;
    m.text = text;
    m.badges = parse_badges(l->tags);
    m.emotes = emotes;
    m.n_emotes = eb.n;
    sink->on_msg(sink->ud, &m);
    return true;
}

bool kk_tw_emit_line(char *line, const kk_tw_emotes *extra, const kk_chat_sink *sink)
{
    kk_tw_line l;
    return kk_tw_split(line, &l) && emit(&l, extra, sink);
}

/* ---- client -------------------------------------------------------------- */

typedef enum {
    ST_WAIT,    /* until next_at, then connect */
    ST_CONNECT, /* kk_http_connect running */
    ST_TLS,     /* handshake */
    ST_ONLINE,
} state;

struct kk_twitch {
    kk_http *http;
    kk_chat_sink sink;
    char channel[32];

    SSL_CTX *ctx;
    SSL *ssl;
    BIO *rbio, *wbio; /* network → SSL, SSL → network */
    int fd;
    char in[16384]; /* plain text not yet split into lines */
    size_t in_len;
    char *out; /* encrypted bytes waiting for send() */
    size_t out_len, out_cap;

    state st;
    double now, next_at, since; /* since: start of the handshake or JOIN */
    double last_rx;
    bool pinged, in_room, warned_join, reconnect;
    int failures;

    /* BTTV/FFZ/7TV */
    kk_tw_emotes *extra; /* NULL when off */
    char room_id[32];
    unsigned loaded, pending; /* bits by kk_tw_source */
};

static void close_conn(kk_twitch *tw)
{
    if (tw->ssl)
        SSL_free(tw->ssl); /* frees the BIOs too */
    tw->ssl = NULL;
    tw->rbio = tw->wbio = NULL;
    if (tw->fd >= 0)
        close(tw->fd);
    tw->fd = -1;
    tw->in_len = tw->out_len = 0;
    tw->in_room = tw->pinged = tw->warned_join = tw->reconnect = false;
}

static void retry(kk_twitch *tw, double delay)
{
    close_conn(tw);
    tw->st = ST_WAIT;
    tw->next_at = tw->now + delay;
}

static void fail(kk_twitch *tw, const char *what, const char *err)
{
    tw->failures++;
    double delay = fmin(60.0, pow(2.0, fmin(tw->failures, 6)));
    kk_log_warn("Twitch #%s: %s: %s (nova tentativa em %.0f s)", tw->channel, what,
                err, delay);
    retry(tw, delay);
}

/* OpenSSL's reason for the last failure, or fallback. */
static const char *ssl_reason(const kk_twitch *tw, const char *fallback)
{
    static char buf[256];
    long v = tw->ssl ? SSL_get_verify_result(tw->ssl) : X509_V_OK;
    unsigned long e = ERR_get_error();
    ERR_clear_error();
    if (v != X509_V_OK)
        return X509_verify_cert_error_string(v);
    if (e) {
        ERR_error_string_n(e, buf, sizeof buf);
        return buf;
    }
    return fallback;
}

static bool append_out(kk_twitch *tw, const char *data, size_t n)
{
    if (tw->out_len + n > tw->out_cap) {
        size_t cap = tw->out_cap ? tw->out_cap : 4096;
        while (cap < tw->out_len + n)
            cap *= 2;
        /* Something is very wrong if this much never leaves. */
        if (cap > (1u << 20))
            return false;
        char *o = realloc(tw->out, cap);
        if (!o)
            return false;
        tw->out = o;
        tw->out_cap = cap;
    }
    memcpy(tw->out + tw->out_len, data, n);
    tw->out_len += n;
    return true;
}

/* Moves what SSL produced to the socket, as much as it takes now. */
static bool flush(kk_twitch *tw)
{
    char buf[4096];
    int n;
    while ((n = BIO_read(tw->wbio, buf, sizeof buf)) > 0)
        if (!append_out(tw, buf, (size_t)n))
            return false;
    size_t sent = 0;
    while (sent < tw->out_len) {
        ssize_t w = send(tw->fd, tw->out + sent, tw->out_len - sent,
                         MSG_NOSIGNAL | MSG_DONTWAIT);
        if (w < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
                break;
            return false;
        }
        sent += (size_t)w;
    }
    memmove(tw->out, tw->out + sent, tw->out_len - sent);
    tw->out_len -= sent;
    return true;
}

static void send_line(kk_twitch *tw, const char *fmt, ...)
    __attribute__((format(printf, 2, 3)));

static void send_line(kk_twitch *tw, const char *fmt, ...)
{
    char line[512];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(line, sizeof line - 2, fmt, ap);
    va_end(ap);
    if (n < 0 || n >= (int)sizeof line - 2 || !tw->ssl)
        return;
    memcpy(line + n, "\r\n", 2);
    SSL_write(tw->ssl, line, n + 2); /* memory BIO: all or nothing */
}

/* ---- BTTV/FFZ/7TV lists -------------------------------------------------- */

static const char *const source_names[KK_TW_N_SOURCES] = {
    "FFZ (globais)",   "BTTV (globais)",   "7TV (globais)",
    "FFZ (do canal)",  "BTTV (do canal)",  "7TV (do canal)",
};

static void on_emotes(kk_twitch *tw, kk_tw_source src, long status,
                      const char *body, const char *err)
{
    unsigned bit = 1u << src;
    tw->pending &= ~bit;
    int n = -1;
    if (status == 404) {
        /* The channel has nothing there. */
        kk_tw_emotes_clear(tw->extra, src);
        n = 0;
    } else if (status == 200) {
        n = kk_tw_emotes_load(tw->extra, src, body);
    }
    if (n < 0) {
        if (err)
            kk_log_warn("Twitch: emotes de %s: %s", source_names[src], err);
        else
            kk_log_warn("Twitch: emotes de %s: HTTP %ld", source_names[src], status);
        return;
    }
    tw->loaded |= bit;
    if (n > 0)
        kk_log_info("Twitch #%s: %d emotes de %s", tw->channel, n, source_names[src]);
}

#define SOURCE_CB(fn, src)                                                     \
    static void fn(void *ud, long status, const char *body, size_t len,        \
                   const char *err)                                            \
    {                                                                          \
        (void)len;                                                             \
        on_emotes(ud, src, status, body, err);                                 \
    }
SOURCE_CB(on_ffz_global, KK_TW_FFZ_GLOBAL)
SOURCE_CB(on_bttv_global, KK_TW_BTTV_GLOBAL)
SOURCE_CB(on_7tv_global, KK_TW_7TV_GLOBAL)
SOURCE_CB(on_ffz_channel, KK_TW_FFZ_CHANNEL)
SOURCE_CB(on_bttv_channel, KK_TW_BTTV_CHANNEL)
SOURCE_CB(on_7tv_channel, KK_TW_7TV_CHANNEL)
#undef SOURCE_CB

/* Asks for every list not loaded nor on its way. */
static void fetch_emotes(kk_twitch *tw)
{
    static const struct {
        const char *url; /* the channel's end in "/": room id follows */
        kk_http_cb cb;
    } sources[KK_TW_N_SOURCES] = {
        {"https://api.frankerfacez.com/v1/set/global", on_ffz_global},
        {"https://api.betterttv.net/3/cached/emotes/global", on_bttv_global},
        {"https://7tv.io/v3/emote-sets/global", on_7tv_global},
        {"https://api.frankerfacez.com/v1/room/id/", on_ffz_channel},
        {"https://api.betterttv.net/3/cached/users/twitch/", on_bttv_channel},
        {"https://7tv.io/v3/users/twitch/", on_7tv_channel},
    };
    for (int i = 0; i < KK_TW_N_SOURCES; i++) {
        unsigned bit = 1u << i;
        char url[256];
        if ((tw->loaded | tw->pending) & bit)
            continue;
        size_t n = strlen(sources[i].url);
        if (!kk_pathf(url, sizeof url, "%s%s", sources[i].url,
                      sources[i].url[n - 1] == '/' ? tw->room_id : ""))
            continue;
        if (kk_http_get(tw->http, url, sources[i].cb, tw) == 0)
            tw->pending |= bit;
    }
}

static void on_roomstate(kk_twitch *tw, const kk_tw_line *l)
{
    char room[32];
    if (!kk_tw_tag(l->tags, "room-id", room, sizeof room) || !room[0])
        return;
    if (!tw->in_room) {
        tw->in_room = true;
        tw->failures = 0;
        kk_log_info("Twitch: conectado ao chat de https://www.twitch.tv/%s", tw->channel);
    }
    if (!tw->extra)
        return;
    if (strcmp(room, tw->room_id) != 0) {
        snprintf(tw->room_id, sizeof tw->room_id, "%s", room);
        for (int s = KK_TW_FFZ_CHANNEL; s <= KK_TW_7TV_CHANNEL; s++) {
            kk_tw_emotes_clear(tw->extra, (kk_tw_source)s);
            tw->loaded &= ~(1u << s);
        }
    }
    fetch_emotes(tw);
}

static void handle_line(kk_twitch *tw, char *line)
{
    kk_tw_line l;
    if (!kk_tw_split(line, &l))
        return;
    if (strcmp(l.command, "PING") == 0)
        send_line(tw, "PONG :%s", l.trailing);
    else if (strcmp(l.command, "RECONNECT") == 0)
        tw->reconnect = true; /* the server is going away */
    else if (strcmp(l.command, "ROOMSTATE") == 0)
        on_roomstate(tw, &l);
    else if (strcmp(l.command, "NOTICE") == 0)
        kk_log_warn("Twitch #%s: %s", tw->channel, l.trailing);
    else
        emit(&l, tw->extra, &tw->sink);
}

/* Splits the plain text into lines and handles the whole ones. */
static void take_lines(kk_twitch *tw)
{
    size_t start = 0;
    for (size_t i = 0; i < tw->in_len; i++) {
        if (tw->in[i] != '\n')
            continue;
        tw->in[i] = '\0';
        if (i > start)
            handle_line(tw, tw->in + start);
        start = i + 1;
    }
    memmove(tw->in, tw->in + start, tw->in_len - start);
    tw->in_len -= start;
    /* A line longer than the buffer: drop it. */
    if (tw->in_len == sizeof tw->in - 1)
        tw->in_len = 0;
}

static void login(kk_twitch *tw)
{
    unsigned nick = 10000u + (unsigned)((uintptr_t)tw ^ (uintptr_t)(tw->now * 1000)) % 90000u;
    send_line(tw, "CAP REQ :twitch.tv/tags twitch.tv/commands");
    send_line(tw, "NICK justinfan%u", nick);
    send_line(tw, "JOIN #%s", tw->channel);
    tw->since = tw->now;
}

/* Runs SSL on what arrived; false (with *why) when the connection is over. */
static bool drive(kk_twitch *tw, const char **why)
{
    if (tw->st == ST_TLS) {
        int r = SSL_do_handshake(tw->ssl);
        if (r == 1) {
            tw->st = ST_ONLINE;
            login(tw);
        } else {
            int e = SSL_get_error(tw->ssl, r);
            if (e != SSL_ERROR_WANT_READ && e != SSL_ERROR_WANT_WRITE) {
                *why = ssl_reason(tw, "falha no TLS");
                return false;
            }
        }
    }
    while (tw->st == ST_ONLINE && !tw->reconnect) {
        int n = SSL_read(tw->ssl, tw->in + tw->in_len, (int)(sizeof tw->in - 1 - tw->in_len));
        if (n > 0) {
            tw->in_len += (size_t)n;
            take_lines(tw);
            continue;
        }
        int e = SSL_get_error(tw->ssl, n);
        if (e == SSL_ERROR_WANT_READ || e == SSL_ERROR_WANT_WRITE)
            break;
        *why = e == SSL_ERROR_ZERO_RETURN ? "conexão encerrada pelo servidor"
                                          : ssl_reason(tw, "erro de leitura");
        return false;
    }
    if (!flush(tw)) {
        *why = strerror(errno);
        return false;
    }
    return true;
}

/* Feeds what the socket has to SSL. */
static bool pump_in(kk_twitch *tw, const char **why)
{
    char buf[16384];
    for (;;) {
        ssize_t n = recv(tw->fd, buf, sizeof buf, MSG_DONTWAIT);
        if (n > 0) {
            BIO_write(tw->rbio, buf, (int)n);
            tw->last_rx = tw->now;
            tw->pinged = false;
            continue;
        }
        if (n == 0) {
            *why = "conexão encerrada";
            return false;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK)
            return true;
        if (errno != EINTR) {
            *why = strerror(errno);
            return false;
        }
    }
}

static void on_connected(void *ud, int fd, const char *err)
{
    kk_twitch *tw = ud;
    if (fd < 0) {
        fail(tw, "conexão", err ? err : "?");
        return;
    }
    tw->fd = fd;
    tw->ssl = SSL_new(tw->ctx);
    tw->rbio = BIO_new(BIO_s_mem());
    tw->wbio = BIO_new(BIO_s_mem());
    if (!tw->ssl || !tw->rbio || !tw->wbio) {
        BIO_free(tw->rbio);
        BIO_free(tw->wbio);
        tw->rbio = tw->wbio = NULL;
        fail(tw, "TLS", "sem memória");
        return;
    }
    SSL_set_bio(tw->ssl, tw->rbio, tw->wbio);
    SSL_set_connect_state(tw->ssl);
    SSL_set_tlsext_host_name(tw->ssl, HOST);
    SSL_set1_host(tw->ssl, HOST);
    tw->st = ST_TLS;
    tw->since = tw->last_rx = tw->now;
    const char *why = NULL;
    if (!drive(tw, &why))
        fail(tw, "TLS", why);
}

kk_twitch *kk_twitch_new(kk_http *http, const char *target,
                         const kk_chat_sink *sink, bool extra_emotes)
{
    kk_twitch *tw = calloc(1, sizeof *tw);
    if (!tw)
        return NULL;
    tw->fd = -1;
    if (!kk_tw_parse_target(target, tw->channel, sizeof tw->channel)) {
        kk_log_error("Twitch: não entendi \"%s\" (use o nome do canal ou o link "
                     "twitch.tv/canal)",
                     target);
        free(tw);
        return NULL;
    }
    tw->http = http;
    tw->sink = *sink;
    tw->ctx = SSL_CTX_new(TLS_client_method());
    if (extra_emotes)
        tw->extra = kk_tw_emotes_new();
    if (!tw->ctx || (extra_emotes && !tw->extra)) {
        kk_twitch_free(tw);
        return NULL;
    }
    SSL_CTX_set_min_proto_version(tw->ctx, TLS1_2_VERSION);
    SSL_CTX_set_default_verify_paths(tw->ctx);
    SSL_CTX_set_verify(tw->ctx, SSL_VERIFY_PEER, NULL);
    tw->st = ST_WAIT;
    return tw;
}

void kk_twitch_free(kk_twitch *tw)
{
    if (!tw)
        return;
    kk_http_cancel(tw->http, tw);
    close_conn(tw);
    SSL_CTX_free(tw->ctx);
    kk_tw_emotes_free(tw->extra);
    free(tw->out);
    free(tw);
}

int kk_twitch_pollfd(const kk_twitch *tw, struct pollfd *fd)
{
    if (tw->fd < 0 || (tw->st != ST_TLS && tw->st != ST_ONLINE))
        return 0;
    *fd = (struct pollfd){.fd = tw->fd, .events = (short)(POLLIN | (tw->out_len ? POLLOUT : 0))};
    return 1;
}

void kk_twitch_tick(kk_twitch *tw, const struct pollfd *fd, double now)
{
    tw->now = now;
    if (tw->st == ST_WAIT) {
        if (now < tw->next_at)
            return;
        tw->st = ST_CONNECT;
        if (kk_http_connect(tw->http, HOST, PORT, on_connected, tw) < 0)
            fail(tw, "conexão", "não consegui iniciar");
        return;
    }
    if (tw->st == ST_CONNECT)
        return;

    if (fd && fd->revents) {
        const char *why_in = NULL, *why = NULL;
        bool in_ok = !(fd->revents & (POLLIN | POLLHUP | POLLERR)) || pump_in(tw, &why_in);
        /* Even after an error: what arrived before it is still read. */
        bool ok = drive(tw, &why);
        if (!in_ok || !ok) {
            fail(tw, "leitura do chat", why_in ? why_in : why ? why : "erro");
            return;
        }
    }
    if (tw->reconnect) {
        kk_log_info("Twitch #%s: o servidor pediu para reconectar", tw->channel);
        retry(tw, 0);
        return;
    }
    if (tw->st == ST_TLS && now - tw->since > HANDSHAKE_TIMEOUT_S) {
        fail(tw, "TLS", "o servidor não respondeu");
        return;
    }
    if (tw->st != ST_ONLINE)
        return;
    if (!tw->in_room && !tw->warned_join && now - tw->since > JOIN_TIMEOUT_S) {
        kk_log_warn("Twitch: #%s não respondeu ao JOIN (o canal existe?)", tw->channel);
        tw->warned_join = true;
    }
    if (now - tw->last_rx > IDLE_DROP_S) {
        fail(tw, "leitura do chat", "sem resposta do servidor");
    } else if (now - tw->last_rx > IDLE_PING_S && !tw->pinged) {
        send_line(tw, "PING :kikarinhas");
        tw->pinged = true;
        if (!flush(tw))
            fail(tw, "escrita", strerror(errno));
    }
}
