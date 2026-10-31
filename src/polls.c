/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Polls and their history (see polls.h). */
#include "polls.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "log.h"
#include "util.h"

#define MAX_QUESTION 300 /* bytes */
#define MAX_OPTION 120
#define MAX_LIVES 32
#define MAX_DURATION (24 * 3600)

struct kk_polls {
    char *path; /* NULL: never written */
    kk_poll **items; /* oldest first */
    int n, cap;
    uint64_t next_id;
    bool dirty;
    unsigned serial;
};

static void changed(kk_polls *ps)
{
    ps->dirty = true;
    ps->serial++;
}

/* ---- one poll ------------------------------------------------------------- */

static void free_strv(char **v, int n)
{
    for (int i = 0; i < n; i++)
        free(v[i]);
    free(v);
}

static void free_voters(kk_poll *p)
{
    for (int i = 0; i < p->cap_voters; i++)
        free(p->voters[i].key);
    free(p->voters);
    p->voters = NULL;
    p->n_voters = p->cap_voters = 0;
}

void kk_poll_free(kk_poll *p)
{
    if (!p)
        return;
    free(p->question);
    for (int i = 0; i < p->n_options; i++) {
        free(p->options[i].text);
        free(p->options[i].shortcut);
        free(p->options[i].votes);
    }
    free_strv(p->platforms, p->n_platforms);
    for (int i = 0; i < p->n_lives; i++) {
        free(p->lives[i].platform);
        free(p->lives[i].url);
        free(p->lives[i].title);
    }
    free(p->lives);
    free_strv(p->channels, p->n_channels);
    free_voters(p);
    free(p);
}

static bool is_space(char c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

/* s without the spaces around it, at most max bytes (cut on a character
 * boundary); NULL if empty or out of memory. */
static char *trimmed(const char *s, size_t max)
{
    if (!s)
        return NULL;
    while (is_space(*s))
        s++;
    size_t n = strlen(s);
    while (n > 0 && is_space(s[n - 1]))
        n--;
    if (n > max) {
        n = max;
        while (n > 0 && ((unsigned char)s[n] & 0xc0) == 0x80)
            n--;
    }
    if (n == 0)
        return NULL;
    char *out = malloc(n + 1);
    if (out) {
        memcpy(out, s, n);
        out[n] = '\0';
    }
    return out;
}

static int platform_index(const kk_poll *p, const char *name, size_t len)
{
    for (int i = 0; i < p->n_platforms; i++)
        if (strlen(p->platforms[i]) == len && strncmp(p->platforms[i], name, len) == 0)
            return i;
    return -1;
}

/* The index of the platform (the first len bytes of name), added with no
 * votes if new; -1 without memory. */
static int add_platform(kk_poll *p, const char *name, size_t len)
{
    int i = platform_index(p, name, len);
    if (i >= 0)
        return i;
    char **np = realloc(p->platforms, (size_t)(p->n_platforms + 1) * sizeof *np);
    if (!np)
        return -1;
    p->platforms = np;
    if (!(np[p->n_platforms] = strndup(name, len)))
        return -1;
    for (int k = 0; k < p->n_options; k++) {
        int *nv = realloc(p->options[k].votes, (size_t)(p->n_platforms + 1) * sizeof *nv);
        if (!nv) {
            free(np[p->n_platforms]);
            return -1;
        }
        nv[p->n_platforms] = 0;
        p->options[k].votes = nv;
    }
    return p->n_platforms++;
}

static uint64_t hash(const char *s)
{
    uint64_t h = 1469598103934665603ULL;
    for (; *s; s++)
        h = (h ^ (unsigned char)*s) * 1099511628211ULL;
    return h;
}

static kk_poll_voter *voter_slot(const kk_poll *p, const char *key)
{
    if (!p->cap_voters)
        return NULL;
    size_t mask = (size_t)p->cap_voters - 1;
    for (size_t i = (size_t)hash(key) & mask;; i = (i + 1) & mask)
        if (!p->voters[i].key || strcmp(p->voters[i].key, key) == 0)
            return &p->voters[i];
}

static bool grow_voters(kk_poll *p)
{
    if ((p->n_voters + 1) * 10 < p->cap_voters * 7)
        return true;
    int cap = p->cap_voters ? p->cap_voters * 2 : 64;
    kk_poll_voter *old = p->voters;
    int old_cap = p->cap_voters;
    p->voters = calloc((size_t)cap, sizeof *p->voters);
    if (!p->voters) {
        p->voters = old;
        return false;
    }
    p->cap_voters = cap;
    for (int i = 0; i < old_cap; i++)
        if (old[i].key)
            *voter_slot(p, old[i].key) = old[i];
    free(old);
    return true;
}

int kk_poll_vote_of(const kk_poll *p, const char *key)
{
    const kk_poll_voter *v = voter_slot(p, key);
    return v && v->key ? v->option : -1;
}

/* Records key's vote on p (no checks); the previous option or -1. -2
 * without memory. */
static int put_vote(kk_poll *p, const char *key, int option)
{
    size_t plen = strcspn(key, ":");
    kk_poll_voter *v = voter_slot(p, key);
    int before = v && v->key ? v->option : -1;
    if (before == option)
        return before;
    if (before < 0) {
        if (!grow_voters(p))
            return -2;
        v = voter_slot(p, key);
        if (!(v->key = strdup(key)))
            return -2;
        p->n_voters++;
    }
    int pf = add_platform(p, key, plen);
    if (pf < 0)
        return -2;
    if (before >= 0)
        p->options[before].votes[pf]--;
    p->options[option].votes[pf]++;
    v->option = option;
    return before;
}

int kk_poll_votes(const kk_poll *p, int i, int pf)
{
    if (i < 0 || i >= p->n_options)
        return 0;
    if (pf >= 0)
        return pf < p->n_platforms ? p->options[i].votes[pf] : 0;
    int n = 0;
    for (int k = 0; k < p->n_platforms; k++)
        n += p->options[i].votes[k];
    return n;
}

int kk_poll_total(const kk_poll *p, int pf)
{
    int n = 0;
    for (int i = 0; i < p->n_options; i++)
        n += kk_poll_votes(p, i, pf);
    return n;
}

long long kk_poll_ends_at(const kk_poll *p)
{
    return p->duration > 0 ? p->opened + p->duration : 0;
}

/* ---- words ---------------------------------------------------------------- */

static void lower_ascii(char *s)
{
    for (; *s; s++)
        *s = (char)tolower((unsigned char)*s);
}

/* "!a".."!j" and "!1".."!10" of option i. */
static void letter_of(int i, char out[2])
{
    out[0] = (char)('a' + i);
    out[1] = '\0';
}

static void number_of(int i, char out[4])
{
    snprintf(out, 4, "%d", (i + 1) % 100); /* i < KK_POLL_MAX_OPTIONS */
}

/* The option word w is as a letter or a number of a poll of n options, or
 * -1. */
static int letter_index(const char *w, int n)
{
    return w[0] >= 'a' && w[0] < 'a' + n && !w[1] ? w[0] - 'a' : -1;
}

static int number_index(const char *w, int n)
{
    if (w[0] < '1' || w[0] > '9')
        return -1;
    char *end;
    long v = strtol(w, &end, 10);
    return !*end && v >= 1 && v <= n ? (int)v - 1 : -1;
}

int kk_poll_match(const kk_poll *p, const char *text)
{
    while (is_space(*text))
        text++;
    if (*text == '!')
        text++;
    else if (strncmp(text, "\xEF\xBC\x81", 3) == 0) /* "！" */
        text += 3;
    else
        return -1;
    size_t n = strcspn(text, " \t\r\n");
    if (n == 0 || n > KK_POLL_MAX_WORD)
        return -1;
    char w[KK_POLL_MAX_WORD + 1];
    memcpy(w, text, n);
    w[n] = '\0';
    lower_ascii(w);
    for (int i = 0; i < p->n_options; i++)
        if (p->options[i].shortcut && strcmp(p->options[i].shortcut, w) == 0)
            return i;
    int i = p->letters ? letter_index(w, p->n_options) : -1;
    if (i < 0 && p->numbers)
        i = number_index(w, p->n_options);
    return i;
}

void kk_poll_words(const kk_poll *p, int i, char *main_word, size_t size, char *shortcut,
                   size_t ssize)
{
    char w[4];
    const char *sc = p->options[i].shortcut;
    if (p->letters)
        letter_of(i, w);
    else if (p->numbers)
        number_of(i, w);
    else
        w[0] = '\0';
    if (w[0]) {
        snprintf(main_word, size, "!%s", w);
        snprintf(shortcut, ssize, "%s%s", sc ? "!" : "", sc ? sc : "");
    } else {
        snprintf(main_word, size, "!%s", sc ? sc : "?");
        if (ssize)
            shortcut[0] = '\0';
    }
}

/* ---- JSON ----------------------------------------------------------------- */

static const char *str_of(const cJSON *o, const char *key)
{
    return cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(o, key));
}

static double num_of(const cJSON *o, const char *key, double dflt)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, key);
    return cJSON_IsNumber(v) ? v->valuedouble : dflt;
}

static bool bool_of(const cJSON *o, const char *key, bool dflt)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, key);
    return cJSON_IsBool(v) ? cJSON_IsTrue(v) : dflt;
}

cJSON *kk_poll_to_json(const kk_poll *p, bool voters)
{
    cJSON *o = cJSON_CreateObject();
    if (!o)
        return NULL;
    cJSON_AddNumberToObject(o, "id", (double)p->id);
    cJSON_AddStringToObject(o, "question", p->question);
    cJSON *opts = cJSON_AddArrayToObject(o, "options");
    for (int i = 0; opts && i < p->n_options; i++) {
        cJSON *op = cJSON_CreateObject();
        cJSON_AddItemToArray(opts, op);
        cJSON_AddStringToObject(op, "text", p->options[i].text);
        if (p->options[i].shortcut)
            cJSON_AddStringToObject(op, "command", p->options[i].shortcut);
        cJSON *v = cJSON_AddObjectToObject(op, "votes");
        for (int k = 0; v && k < p->n_platforms; k++)
            cJSON_AddNumberToObject(v, p->platforms[k], p->options[i].votes[k]);
    }
    cJSON_AddNumberToObject(o, "duration", p->duration);
    cJSON_AddBoolToObject(o, "show_counts", p->show_counts);
    cJSON_AddBoolToObject(o, "letters", p->letters);
    cJSON_AddBoolToObject(o, "numbers", p->numbers);
    cJSON_AddNumberToObject(o, "opened", (double)p->opened);
    cJSON_AddNumberToObject(o, "closed", (double)p->closed);
    cJSON *ch = cJSON_AddArrayToObject(o, "channels");
    for (int i = 0; ch && i < p->n_channels; i++)
        cJSON_AddItemToArray(ch, cJSON_CreateString(p->channels[i]));
    cJSON *lv = cJSON_AddArrayToObject(o, "lives");
    for (int i = 0; lv && i < p->n_lives; i++) {
        cJSON *l = cJSON_CreateObject();
        cJSON_AddItemToArray(lv, l);
        cJSON_AddStringToObject(l, "platform", p->lives[i].platform);
        cJSON_AddStringToObject(l, "url", p->lives[i].url);
        if (p->lives[i].title)
            cJSON_AddStringToObject(l, "title", p->lives[i].title);
    }
    if (voters && p->n_voters) {
        cJSON *vs = cJSON_AddObjectToObject(o, "voters");
        for (int i = 0; vs && i < p->cap_voters; i++)
            if (p->voters[i].key)
                cJSON_AddNumberToObject(vs, p->voters[i].key, p->voters[i].option);
    }
    return o;
}

static bool add_str(char ***v, int *n, const char *s)
{
    char **nv = realloc(*v, (size_t)(*n + 1) * sizeof *nv);
    if (!nv)
        return false;
    *v = nv;
    if (!(nv[*n] = strdup(s)))
        return false;
    (*n)++;
    return true;
}

static bool add_live(kk_poll *p, const char *platform, const char *url, const char *title)
{
    kk_poll_live *nl = realloc(p->lives, (size_t)(p->n_lives + 1) * sizeof *nl);
    if (!nl)
        return false;
    p->lives = nl;
    kk_poll_live l = {strdup(platform), strdup(url), title && title[0] ? strdup(title) : NULL};
    if (!l.platform || !l.url) {
        free(l.platform);
        free(l.url);
        free(l.title);
        return false;
    }
    nl[p->n_lives++] = l;
    return true;
}

/* An option's shortcut as typed: "!Sim" → "sim". NULL for none; *bad set
 * when it can't be one (spaces, too long). */
static char *shortcut_of(const char *s, bool *bad)
{
    *bad = false;
    if (!s)
        return NULL;
    while (is_space(*s))
        s++;
    if (*s == '!')
        s++;
    char *w = trimmed(s, 1000);
    if (!w)
        return NULL;
    if (strlen(w) > KK_POLL_MAX_WORD || w[strcspn(w, " \t\r\n")] || w[0] == '!') {
        free(w);
        *bad = true;
        return NULL;
    }
    lower_ascii(w);
    return w;
}

/* The poll in o, as the file has it (or the request, without the counts).
 * NULL if it is not a usable poll; *why says what is wrong. */
static kk_poll *parse_poll(const cJSON *o, const char **why)
{
    *why = "sem memória";
    kk_poll *p = calloc(1, sizeof *p);
    if (!p)
        return NULL;
    p->question = trimmed(str_of(o, "question"), MAX_QUESTION);
    if (!p->question) {
        *why = "falta a pergunta";
        goto fail;
    }
    const cJSON *opts = cJSON_GetObjectItemCaseSensitive(o, "options");
    if (cJSON_GetArraySize(opts) < 2 || cJSON_GetArraySize(opts) > KK_POLL_MAX_OPTIONS) {
        *why = "a enquete precisa de 2 a 10 opções";
        goto fail;
    }
    const cJSON *op;
    cJSON_ArrayForEach(op, opts)
    {
        kk_poll_option *dst = &p->options[p->n_options];
        dst->text = trimmed(str_of(op, "text"), MAX_OPTION);
        if (!dst->text) {
            *why = "uma das opções está sem texto";
            goto fail;
        }
        p->n_options++;
        bool bad;
        dst->shortcut = shortcut_of(str_of(op, "command"), &bad);
        if (bad) {
            *why = "o comando de uma opção deve ser uma palavra só, sem espaços";
            goto fail;
        }
        if (!(dst->votes = calloc(1, sizeof *dst->votes))) /* realloc'd per platform */
            goto fail;
    }
    double d = num_of(o, "duration", 0);
    if (d < 0 || d > MAX_DURATION) {
        *why = "o tempo vai de 0 (sem limite) a 24 horas";
        goto fail;
    }
    p->duration = (int)d;
    p->show_counts = bool_of(o, "show_counts", true);
    p->id = (uint64_t)num_of(o, "id", 0);
    p->letters = bool_of(o, "letters", true);
    p->numbers = bool_of(o, "numbers", true);
    p->opened = (long long)num_of(o, "opened", 0);
    p->closed = (long long)num_of(o, "closed", 0);

    /* The counts, per platform. */
    for (int i = 0; i < p->n_options; i++) {
        const cJSON *v;
        cJSON_ArrayForEach(v, cJSON_GetObjectItemCaseSensitive(cJSON_GetArrayItem(opts, i),
                                                               "votes"))
        {
            if (!cJSON_IsNumber(v) || v->valuedouble < 0 || !v->string[0])
                continue;
            int pf = add_platform(p, v->string, strlen(v->string));
            if (pf < 0)
                goto fail;
            p->options[i].votes[pf] = (int)v->valuedouble;
        }
    }
    const cJSON *it;
    cJSON_ArrayForEach(it, cJSON_GetObjectItemCaseSensitive(o, "channels"))
        if (cJSON_IsString(it) && !add_str(&p->channels, &p->n_channels, it->valuestring))
            goto fail;
    cJSON_ArrayForEach(it, cJSON_GetObjectItemCaseSensitive(o, "lives"))
    {
        const char *pf = str_of(it, "platform"), *url = str_of(it, "url");
        if (pf && url && p->n_lives < MAX_LIVES && !add_live(p, pf, url, str_of(it, "title")))
            goto fail;
    }
    /* Who voted, while open: the counts above already have them. */
    cJSON_ArrayForEach(it, cJSON_GetObjectItemCaseSensitive(o, "voters"))
    {
        int opt = cJSON_IsNumber(it) ? (int)it->valuedouble : -1;
        if (opt < 0 || opt >= p->n_options || !strchr(it->string, ':') ||
            kk_poll_vote_of(p, it->string) >= 0)
            continue;
        if (!grow_voters(p))
            goto fail;
        kk_poll_voter *v = voter_slot(p, it->string);
        if (!(v->key = strdup(it->string)))
            goto fail;
        v->option = opt;
        p->n_voters++;
    }
    return p;
fail:
    kk_poll_free(p);
    return NULL;
}

kk_poll *kk_poll_from_request(const cJSON *req, kk_poll_taken_fn taken, void *ud, char *err,
                              size_t size)
{
    const char *why;
    kk_poll *p = parse_poll(req, &why);
    if (!p) {
        snprintf(err, size, "%s", why);
        return NULL;
    }
    /* What a request can't decide. */
    p->id = 0;
    p->opened = p->closed = 0;
    free_strv(p->channels, p->n_channels);
    p->channels = NULL;
    p->n_channels = 0;
    free_voters(p);
    free_strv(p->platforms, p->n_platforms);
    p->platforms = NULL;
    p->n_platforms = 0;

    p->letters = p->numbers = true;
    for (int i = 0; i < p->n_options; i++) {
        char l[2], n[4];
        letter_of(i, l);
        number_of(i, n);
        if (taken && taken(ud, l))
            p->letters = false;
        if (taken && taken(ud, n))
            p->numbers = false;
    }
    for (int i = 0; i < p->n_options; i++) {
        const char *sc = p->options[i].shortcut;
        if (!sc) {
            if (!p->letters && !p->numbers) {
                snprintf(err, size,
                         "!a, !b... e !1, !2... já são comandos: dê um comando a cada opção");
                goto fail;
            }
            continue;
        }
        if (letter_index(sc, p->n_options) >= 0 || number_index(sc, p->n_options) >= 0) {
            snprintf(err, size, "!%s já vota numa opção desta enquete", sc);
            goto fail;
        }
        for (int k = 0; k < i; k++)
            if (p->options[k].shortcut && strcmp(p->options[k].shortcut, sc) == 0) {
                snprintf(err, size, "duas opções com o mesmo comando, !%s", sc);
                goto fail;
            }
        if (taken && taken(ud, sc)) {
            snprintf(err, size, "!%s já é um comando (ou um avatar); escolha outro", sc);
            goto fail;
        }
    }
    return p;
fail:
    kk_poll_free(p);
    return NULL;
}

/* ---- the history ---------------------------------------------------------- */

bool kk_polls_default_path(char *out, size_t size)
{
    const char *xdg = getenv("XDG_DATA_HOME");
    if (xdg && xdg[0] == '/')
        return kk_pathf(out, size, "%s/kikarinhas/polls.json", xdg);
    const char *home = getenv("HOME");
    return home && kk_pathf(out, size, "%s/.local/share/kikarinhas/polls.json", home);
}

static bool json_usable(const char *text, size_t len)
{
    (void)len;
    cJSON *j = cJSON_Parse(text);
    bool ok = cJSON_IsArray(cJSON_GetObjectItemCaseSensitive(j, "polls"));
    cJSON_Delete(j);
    return ok;
}

static bool append(kk_polls *ps, kk_poll *p)
{
    if (ps->n == ps->cap) {
        int cap = ps->cap ? ps->cap * 2 : 16;
        kk_poll **ni = realloc(ps->items, (size_t)cap * sizeof *ni);
        if (!ni)
            return false;
        ps->items = ni;
        ps->cap = cap;
    }
    ps->items[ps->n++] = p;
    return true;
}

kk_polls *kk_polls_open(const char *path)
{
    kk_polls *ps = calloc(1, sizeof *ps);
    if (!ps)
        return NULL;
    ps->next_id = 1;
    if (!path)
        return ps;
    if (!(ps->path = strdup(path))) {
        free(ps);
        return NULL;
    }
    char *text = kk_read_file_or_backup(path, json_usable, NULL);
    if (!text) {
        if (errno != ENOENT)
            kk_log_warn("não consegui ler %s: %s", path, strerror(errno));
        return ps;
    }
    cJSON *j = cJSON_Parse(text);
    free(text);
    const cJSON *o;
    bool open_seen = false;
    cJSON_ArrayForEach(o, cJSON_GetObjectItemCaseSensitive(j, "polls"))
    {
        const char *why;
        kk_poll *p = parse_poll(o, &why);
        if (!p || !p->id || kk_polls_find(ps, p->id)) {
            kk_poll_free(p);
            continue;
        }
        if (!p->closed) {
            if (open_seen) /* only one is open */
                p->closed = p->opened;
            open_seen = true;
        }
        if (p->closed)
            free_voters(p);
        if (!append(ps, p)) {
            kk_poll_free(p);
            break;
        }
        if (p->id >= ps->next_id)
            ps->next_id = p->id + 1;
    }
    double next = num_of(j, "next_id", 0);
    if (next > (double)ps->next_id)
        ps->next_id = (uint64_t)next;
    cJSON_Delete(j);
    return ps;
}

void kk_polls_free(kk_polls *ps)
{
    if (!ps)
        return;
    for (int i = 0; i < ps->n; i++)
        kk_poll_free(ps->items[i]);
    free(ps->items);
    free(ps->path);
    free(ps);
}

int kk_polls_save(kk_polls *ps)
{
    if (!ps->dirty || !ps->path)
        return 0;
    cJSON *j = cJSON_CreateObject();
    cJSON *list = j ? cJSON_AddArrayToObject(j, "polls") : NULL;
    for (int i = 0; list && i < ps->n; i++)
        cJSON_AddItemToArray(list, kk_poll_to_json(ps->items[i], true));
    if (j)
        cJSON_AddNumberToObject(j, "next_id", (double)ps->next_id);
    char *text = j ? cJSON_Print(j) : NULL;
    cJSON_Delete(j);
    char tmp[KK_PATH_MAX];
    if (!text || !kk_pathf(tmp, sizeof tmp, "%s.tmp", ps->path)) {
        free(text);
        return -1;
    }
    kk_make_parent_dirs(ps->path);
    kk_backup_file(ps->path, json_usable);
    FILE *f = fopen(tmp, "w");
    bool ok = f && fputs(text, f) >= 0 && fputc('\n', f) != EOF;
    free(text);
    if (f) {
        ok = kk_flush_sync(f) == 0 && ok;
        ok = fclose(f) == 0 && ok;
    }
    if (!ok || rename(tmp, ps->path) < 0) {
        kk_log_warn("não consegui gravar %s: %s", ps->path, strerror(errno));
        remove(tmp);
        return -1;
    }
    ps->dirty = false;
    return 0;
}

bool kk_polls_dirty(const kk_polls *ps)
{
    return ps->dirty;
}

unsigned kk_polls_serial(const kk_polls *ps)
{
    return ps->serial;
}

int kk_polls_count(const kk_polls *ps)
{
    return ps->n;
}

const kk_poll *kk_polls_at(const kk_polls *ps, int i)
{
    return i >= 0 && i < ps->n ? ps->items[i] : NULL;
}

static int index_of(const kk_polls *ps, uint64_t id)
{
    for (int i = 0; i < ps->n; i++)
        if (ps->items[i]->id == id)
            return i;
    return -1;
}

const kk_poll *kk_polls_find(const kk_polls *ps, uint64_t id)
{
    return kk_polls_at(ps, index_of(ps, id));
}

static kk_poll *active(const kk_polls *ps)
{
    /* The open one is the newest unless the file was edited by hand. */
    for (int i = ps->n - 1; i >= 0; i--)
        if (!ps->items[i]->closed)
            return ps->items[i];
    return NULL;
}

const kk_poll *kk_polls_active(const kk_polls *ps)
{
    return active(ps);
}

const kk_poll *kk_polls_start(kk_polls *ps, kk_poll *p, long long now,
                              const char *const *channels, int n_channels)
{
    if (active(ps)) {
        kk_poll_free(p);
        return NULL;
    }
    for (int i = 0; i < n_channels; i++)
        if (!add_str(&p->channels, &p->n_channels, channels[i])) {
            kk_poll_free(p);
            return NULL;
        }
    p->id = ps->next_id;
    p->opened = now > 0 ? now : 1;
    p->closed = 0;
    if (!append(ps, p)) {
        kk_poll_free(p);
        return NULL;
    }
    ps->next_id++;
    changed(ps);
    return p;
}

bool kk_polls_close(kk_polls *ps, long long now)
{
    kk_poll *p = active(ps);
    if (!p)
        return false;
    /* Past its time limit (kikarinhas was not running): when it ran out. */
    long long end = kk_poll_ends_at(p);
    if (end && now > end)
        now = end;
    p->closed = now > p->opened ? now : p->opened;
    free_voters(p);
    changed(ps);
    return true;
}

bool kk_polls_due(const kk_polls *ps, long long now)
{
    const kk_poll *p = active(ps);
    return p && p->duration > 0 && now >= kk_poll_ends_at(p);
}

bool kk_polls_remove(kk_polls *ps, uint64_t id)
{
    int i = index_of(ps, id);
    if (i < 0 || !ps->items[i]->closed)
        return false;
    kk_poll_free(ps->items[i]);
    memmove(&ps->items[i], &ps->items[i + 1], (size_t)(ps->n - i - 1) * sizeof *ps->items);
    ps->n--;
    changed(ps);
    return true;
}

int kk_polls_vote(kk_polls *ps, const char *key, int option)
{
    kk_poll *p = active(ps);
    if (!p || option < 0 || option >= p->n_options || !strchr(key, ':'))
        return -2;
    int before = put_vote(p, key, option);
    if (before != option && before != -2)
        changed(ps);
    return before;
}

void kk_polls_note_live(kk_polls *ps, const char *platform, const char *url,
                        const char *title)
{
    kk_poll *p = active(ps);
    if (!p || !platform || !url || !url[0])
        return;
    for (int i = 0; i < p->n_lives; i++)
        if (strcmp(p->lives[i].url, url) == 0) {
            /* The title may come later than the link. */
            if (!p->lives[i].title && title && title[0] &&
                (p->lives[i].title = strdup(title)))
                changed(ps);
            return;
        }
    if (p->n_lives < MAX_LIVES && add_live(p, platform, url, title))
        changed(ps);
}
