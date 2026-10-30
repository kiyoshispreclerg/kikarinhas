/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Viewers' profile pictures (see photos.h). */
#include "photos.h"

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#include "json.h"
#include "log.h"
#include "util.h"

#define BUCKETS 256
#define MAX_PEOPLE 2048 /* least recently seen ones go beyond this */
#define MAX_INFLIGHT 4  /* lookups at once; the rest wait their turn */
#define FRESH_SECONDS (7 * 24 * 3600) /* a URL on disk is trusted this long */
#define RETRY_SECONDS 600             /* after a lookup that failed */

#define TWITCH_URL "https://decapi.me/twitch/avatar/%s?id=true"
#define KICK_URL "https://kick.com/api/v2/channels/%s"
#define ODYSEE_URL "https://api.na-backend.odysee.com/api/v1/proxy?m=resolve"
/* Odysee's own image proxy, as its site does for avatars: small, not the
 * original upload. */
#define ODYSEE_THUMB "https://thumbnails.odycdn.com/optimize/s:96:96/quality:85/plain/%s"

typedef enum {
    P_NEW,     /* just made: disk not looked at yet */
    P_QUEUED,  /* waiting for a lookup slot */
    P_ASKING,  /* lookup under way */
    P_KNOWN,   /* url set */
    P_NONE,    /* the platform says there is no picture */
    P_ERROR,   /* the lookup failed; tried again after a while */
} person_state;

typedef struct person {
    struct person *next; /* in its bucket */
    struct kk_photos *owner;
    char *platform, *user_id, *name;
    char *url;
    person_state st;
    time_t failed_at;
    uint64_t used; /* recency, for eviction */
} person;

struct kk_photos {
    kk_http *http;
    kk_emotes *images;
    char *dir;
    person *buckets[BUCKETS];
    int count, inflight;
    uint64_t clock;
    time_t warned_at; /* one warning a minute: a blocked API fails for all */
};

static uint64_t fnv1a(const char *s, uint64_t h)
{
    for (; *s; s++)
        h = (h ^ (unsigned char)*s) * 0x100000001b3ULL;
    return h;
}

static uint64_t key_hash(const char *platform, const char *user_id)
{
    return fnv1a(user_id, fnv1a("\x1f", fnv1a(platform, 0xcbf29ce484222325ULL)));
}

kk_photos *kk_photos_new(kk_http *http, kk_emotes *images, const char *cache_dir)
{
    kk_photos *p = calloc(1, sizeof *p);
    if (!p)
        return NULL;
    p->http = http;
    p->images = images;
    if (cache_dir && !(p->dir = strdup(cache_dir))) {
        free(p);
        return NULL;
    }
    return p;
}

static void person_free(person *ps)
{
    if (ps->st == P_ASKING) {
        kk_http_cancel(ps->owner->http, ps);
        ps->owner->inflight--;
    }
    free(ps->platform);
    free(ps->user_id);
    free(ps->name);
    free(ps->url);
    free(ps);
}

void kk_photos_free(kk_photos *p)
{
    if (!p)
        return;
    for (int b = 0; b < BUCKETS; b++)
        for (person *ps = p->buckets[b]; ps;) {
            person *next = ps->next;
            person_free(ps);
            ps = next;
        }
    free(p->dir);
    free(p);
}

bool kk_photos_default_dir(char *out, size_t size)
{
    const char *xdg = getenv("XDG_CACHE_HOME");
    if (xdg && xdg[0] == '/')
        return kk_pathf(out, size, "%s/kikarinhas/photos", xdg);
    const char *home = getenv("HOME");
    return home && kk_pathf(out, size, "%s/.cache/kikarinhas/photos", home);
}

/* ---- answers --------------------------------------------------------------- */

static bool is_https(const char *s)
{
    return s && strncmp(s, "https://", 8) == 0 && !strpbrk(s, " \t\r\n\"<>");
}

bool kk_photos_parse_twitch(const char *body, char *out, size_t size)
{
    /* Plain text: the URL, or a sentence ("User not found: ..."). */
    while (*body == ' ' || *body == '\n' || *body == '\r')
        body++;
    size_t n = strcspn(body, " \t\r\n");
    if (body[n + strspn(body + n, " \t\r\n")] != '\0' || n < 9 ||
        strncmp(body, "https://", 8) != 0 || n >= size)
        return false;
    memcpy(out, body, n);
    out[n] = '\0';
    /* 300x300 by default; 150 is plenty for a line of chat. */
    char *dim = strstr(out, "-300x300.");
    if (dim)
        memcpy(dim, "-150x150", 8);
    return true;
}

bool kk_photos_parse_kick(const char *body, char *out, size_t size)
{
    cJSON *root = cJSON_Parse(body);
    const char *url = kk_json_str(root, "user.profile_pic");
    bool ok = is_https(url) && kk_pathf(out, size, "%s", url);
    cJSON_Delete(root);
    return ok;
}

bool kk_photos_parse_odysee(const char *body, char *out, size_t size)
{
    cJSON *root = cJSON_Parse(body);
    /* {"result": {"lbry://@name#id": {"value": {"thumbnail": {"url"}}}}} */
    const cJSON *result = kk_json_path(root, "result");
    const cJSON *claim = result ? result->child : NULL;
    const char *url = claim ? kk_json_str(claim, "value.thumbnail.url") : NULL;
    bool ok = is_https(url) && kk_pathf(out, size, ODYSEE_THUMB, url);
    cJSON_Delete(root);
    return ok;
}

bool kk_photos_kick_slug(const char *name, char *out, size_t size)
{
    size_t n = 0;
    for (const char *s = name; *s; s++) {
        unsigned char c = (unsigned char)*s;
        if (!isalnum(c) && c != '_' && c != '-')
            return false;
        if (n + 1 >= size)
            return false;
        out[n++] = (char)tolower(c);
    }
    out[n] = '\0';
    return n > 0;
}

/* ---- the URL on disk ------------------------------------------------------- */

static bool url_file(const kk_photos *p, const person *ps, char *out, size_t size)
{
    if (!p->dir)
        return false;
    char safe[32];
    size_t n = 0;
    for (const char *s = ps->platform; *s && n < sizeof safe - 1; s++)
        safe[n++] = isalnum((unsigned char)*s) ? *s : '_';
    safe[n] = '\0';
    return kk_pathf(out, size, "%s/%s/%016llx.url", p->dir, n ? safe : "_",
                    (unsigned long long)key_hash(ps->platform, ps->user_id));
}

/* True if the disk knows (a URL, or that there is none) and isn't stale. */
static bool load_url(kk_photos *p, person *ps)
{
    char path[KK_PATH_MAX];
    struct stat st;
    if (!url_file(p, ps, path, sizeof path) || stat(path, &st) < 0 ||
        time(NULL) - st.st_mtime > FRESH_SECONDS)
        return false;
    size_t len;
    char *data = kk_read_file(path, &len);
    if (!data)
        return false;
    data[strcspn(data, "\r\n")] = '\0';
    bool ok = true;
    if (!data[0]) {
        ps->st = P_NONE;
    } else if (is_https(data)) {
        free(ps->url);
        ps->url = data;
        data = NULL;
        ps->st = P_KNOWN;
    } else {
        ok = false;
    }
    free(data);
    return ok;
}

/* Best effort: the file only saves a lookup next time. */
static void save_url(kk_photos *p, const person *ps)
{
    char path[KK_PATH_MAX], tmp[KK_PATH_MAX];
    if (!url_file(p, ps, path, sizeof path) || !kk_pathf(tmp, sizeof tmp, "%s.tmp", path))
        return;
    kk_make_parent_dirs(path);
    FILE *f = fopen(tmp, "w");
    if (!f)
        return;
    const char *url = ps->st == P_KNOWN && ps->url ? ps->url : "";
    bool ok = fprintf(f, "%s\n", url) >= 0;
    ok = fclose(f) == 0 && ok;
    if (!ok || rename(tmp, path) < 0)
        remove(tmp);
}

/* ---- lookups --------------------------------------------------------------- */

static void start_next(kk_photos *p);

static void on_lookup(void *ud, long status, const char *body, size_t len, const char *err)
{
    (void)len;
    person *ps = ud;
    kk_photos *p = ps->owner;
    p->inflight--;
    char url[2048];
    bool found = false;
    if (status == 200 && strcmp(ps->platform, "twitch") == 0)
        found = kk_photos_parse_twitch(body, url, sizeof url);
    else if (status == 200 && strcmp(ps->platform, "kick") == 0)
        found = kk_photos_parse_kick(body, url, sizeof url);
    else if (status == 200 && strcmp(ps->platform, "odysee") == 0)
        found = kk_photos_parse_odysee(body, url, sizeof url);

    if (found) {
        free(ps->url);
        ps->url = strdup(url);
        ps->st = ps->url ? P_KNOWN : P_ERROR;
    } else if (status == 200 || (status >= 400 && status < 500 && status != 429)) {
        ps->st = P_NONE; /* answered: no picture, or no such person */
    } else {
        ps->st = P_ERROR;
        ps->failed_at = time(NULL);
        if (ps->failed_at - p->warned_at >= 60) {
            p->warned_at = ps->failed_at;
            if (err)
                kk_log_warn("foto de %s (%s): %s", ps->name, ps->platform, err);
            else
                kk_log_warn("foto de %s (%s): HTTP %ld", ps->name, ps->platform, status);
        }
    }
    if (ps->st != P_ERROR)
        save_url(p, ps);
    start_next(p);
}

static int ask(kk_photos *p, person *ps)
{
    char url[512];
    if (strcmp(ps->platform, "twitch") == 0)
        return kk_pathf(url, sizeof url, TWITCH_URL, ps->user_id)
                   ? kk_http_get(p->http, url, on_lookup, ps)
                   : -1;
    if (strcmp(ps->platform, "kick") == 0) {
        char slug[128];
        return kk_photos_kick_slug(ps->name, slug, sizeof slug) &&
                       kk_pathf(url, sizeof url, KICK_URL, slug)
                   ? kk_http_get(p->http, url, on_lookup, ps)
                   : -1;
    }
    /* odysee */
    const char *at = ps->name[0] == '@' ? "" : "@";
    if (!kk_pathf(url, sizeof url, "lbry://%s%s#%s", at, ps->name, ps->user_id))
        return -1;
    cJSON *req = cJSON_CreateObject();
    cJSON *urls = cJSON_AddArrayToObject(cJSON_AddObjectToObject(req, "params"), "urls");
    cJSON_AddItemToArray(urls, cJSON_CreateString(url));
    cJSON_AddStringToObject(req, "jsonrpc", "2.0");
    cJSON_AddStringToObject(req, "method", "resolve");
    cJSON_AddNumberToObject(req, "id", 1);
    char *body = cJSON_PrintUnformatted(req);
    cJSON_Delete(req);
    int r = body ? kk_http_post_json(p->http, ODYSEE_URL, body, on_lookup, ps) : -1;
    cJSON_free(body);
    return r;
}

static void fetch(kk_photos *p, person *ps)
{
    if (ask(p, ps) < 0) {
        ps->st = P_ERROR;
        ps->failed_at = time(NULL);
        return;
    }
    ps->st = P_ASKING;
    p->inflight++;
}

static void start_next(kk_photos *p)
{
    for (int b = 0; b < BUCKETS && p->inflight < MAX_INFLIGHT; b++)
        for (person *ps = p->buckets[b]; ps && p->inflight < MAX_INFLIGHT; ps = ps->next)
            if (ps->st == P_QUEUED)
                fetch(p, ps);
}

static bool can_look_up(const char *platform)
{
    return strcmp(platform, "twitch") == 0 || strcmp(platform, "kick") == 0 ||
           strcmp(platform, "odysee") == 0;
}

/* ---- lookup ---------------------------------------------------------------- */

static void evict_one(kk_photos *p)
{
    person **oldest = NULL;
    for (int b = 0; b < BUCKETS; b++)
        for (person **pp = &p->buckets[b]; *pp; pp = &(*pp)->next)
            if ((*pp)->st != P_ASKING && (!oldest || (*pp)->used < (*oldest)->used))
                oldest = pp;
    if (oldest) {
        person *ps = *oldest;
        *oldest = ps->next;
        person_free(ps);
        p->count--;
    }
}

static person *find(kk_photos *p, const char *platform, const char *user_id, person ***bucket)
{
    *bucket = &p->buckets[key_hash(platform, user_id) % BUCKETS];
    for (person *ps = **bucket; ps; ps = ps->next)
        if (strcmp(ps->platform, platform) == 0 && strcmp(ps->user_id, user_id) == 0)
            return ps;
    return NULL;
}

kk_emote_state kk_photos_get(kk_photos *p, const char *platform, const char *user_id,
                             const char *name, const char *url, kk_emote_anim **out)
{
    *out = NULL;
    if (!platform || !platform[0] || !user_id || !user_id[0])
        return KK_EMOTE_FAILED;
    person **bucket;
    person *ps = find(p, platform, user_id, &bucket);
    if (!ps) {
        if (p->count >= MAX_PEOPLE)
            evict_one(p);
        ps = calloc(1, sizeof *ps);
        if (!ps)
            return KK_EMOTE_FAILED;
        ps->platform = strdup(platform);
        ps->user_id = strdup(user_id);
        ps->name = strdup(name ? name : "");
        if (!ps->platform || !ps->user_id || !ps->name) {
            free(ps->platform);
            free(ps->user_id);
            free(ps->name);
            free(ps);
            return KK_EMOTE_FAILED;
        }
        ps->owner = p;
        ps->next = *bucket;
        *bucket = ps;
        p->count++;
    }
    ps->used = ++p->clock;

    if (is_https(url) && (ps->st != P_KNOWN || strcmp(ps->url, url) != 0)) {
        /* The message brought it (YouTube): nothing to look up. */
        char *copy = strdup(url);
        if (copy) {
            if (ps->st == P_ASKING) {
                kk_http_cancel(p->http, ps);
                p->inflight--;
            }
            free(ps->url);
            ps->url = copy;
            ps->st = P_KNOWN;
        }
    }
    if (ps->st == P_ERROR && time(NULL) - ps->failed_at >= RETRY_SECONDS)
        ps->st = P_QUEUED;
    if (ps->st == P_NEW) {
        if (load_url(p, ps))
            ; /* known from an earlier run */
        else if (p->http && can_look_up(platform))
            ps->st = P_QUEUED;
        else
            ps->st = P_NONE;
    }
    if (ps->st == P_QUEUED && p->inflight < MAX_INFLIGHT)
        fetch(p, ps);

    switch (ps->st) {
    case P_KNOWN: {
        kk_emote em = {.id = ps->url, .url = ps->url};
        return kk_emotes_get_anim(p->images, platform, &em, out);
    }
    case P_QUEUED:
    case P_ASKING:
        return KK_EMOTE_LOADING;
    default:
        return KK_EMOTE_FAILED;
    }
}
