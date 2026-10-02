/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "emotewall.h"

#include <ctype.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "emoji.h"
#include "util.h"

#define MAX_IN_MESSAGE 64  /* emotes looked at per message */
#define MAX_JOBS 2000      /* waiting to fly, reactions included */
#define MAX_PARTICLES 1000 /* hard cap over max_on_screen */
#define MAX_COMBOS 64      /* emotes whose combos are tracked */
#define MAX_COMBO_COUNT 50
#define MAX_REMOVED 32
#define GIVE_UP_S 8.0      /* an image that does not arrive by then is skipped */
#define POP_S 0.2          /* grow-in time */
#define FADE_IN_S 0.3
#define REACTION_MAX_ICONS 20 /* per reaction event */
#define KEY_MAX 192
#define TWO_PI 6.283185307179586

typedef struct {
    char *platform;
    kk_emote em; /* strings owned */
    double at;   /* when it may start */
} job;

typedef struct {
    cairo_surface_t *img; /* referenced */
    double born, life;
    kk_wall_style style;
    double x0, y0, x1, y1; /* top-left at the start and at the end */
    double vx, vy;         /* bounce: pixels per second */
    double sway, freq, phase;
    /* Computed by update for the next paint. */
    int x, y;
    double cur_scale, alpha;
    kk_rect cur, drawn; /* drawn: what is on screen now */
} particle;

typedef struct {
    char key[KEY_MAX];
    double times[MAX_COMBO_COUNT]; /* ring of recent sightings */
    int n, head;
    double until; /* combo running until then */
    double last;  /* for reuse */
} combo;

struct kk_emotewall {
    kk_emotes *images;
    kk_wall_config cfg;
    char *blacklist_src;
    char **block; /* normalized items */
    int n_block;
    int width, height;
    kk_rng rng;

    job *jobs;
    int n_jobs;
    particle *parts;
    int n_parts;
    combo combos[MAX_COMBOS];

    kk_rect removed[MAX_REMOVED];
    int n_removed;
    kk_layer layer;
};

/* ---- blacklist ----------------------------------------------------------- */

/* Names compare without surrounding colons, a leading "_" and case:
 * ":_hello:", "_hello" and "Hello" are the same YouTube shortcut. */
static void norm_name(const char *s, char *out, size_t size)
{
    size_t len = strlen(s);
    if (len >= 2 && s[0] == ':' && s[len - 1] == ':') {
        s++;
        len -= 2;
    }
    while (len && *s == '_') {
        s++;
        len--;
    }
    size_t n = 0;
    for (; n < len && n + 1 < size; n++)
        out[n] = (char)tolower((unsigned char)s[n]);
    out[n] = '\0';
}

/* An emoji item is stored by its key, anything else as a name. */
static char *norm_item(const char *item)
{
    char buf[KEY_MAX];
    if (kk_emoji_is_one(item)) {
        if (!kk_emoji_key(item, strlen(item), buf, sizeof buf))
            return NULL;
    } else {
        norm_name(item, buf, sizeof buf);
    }
    return buf[0] ? strdup(buf) : NULL;
}

static void free_blacklist(kk_emotewall *w)
{
    for (int i = 0; i < w->n_block; i++)
        free(w->block[i]);
    free(w->block);
    free(w->blacklist_src);
    w->block = NULL;
    w->n_block = 0;
    w->blacklist_src = NULL;
}

static void parse_blacklist(kk_emotewall *w, const char *list)
{
    free_blacklist(w);
    if (!list)
        return;
    w->blacklist_src = strdup(list);
    size_t cap = 1;
    for (const char *p = list; *p; p++)
        cap += *p == ',';
    w->block = calloc(cap, sizeof *w->block);
    if (!w->block)
        return;
    while (*list) {
        size_t n = strcspn(list, ",");
        const char *s = list;
        size_t len = n;
        while (len && isspace((unsigned char)*s)) {
            s++;
            len--;
        }
        while (len && isspace((unsigned char)s[len - 1]))
            len--;
        char item[KEY_MAX];
        if (len && len < sizeof item) {
            memcpy(item, s, len);
            item[len] = '\0';
            char *norm = norm_item(item);
            if (norm)
                w->block[w->n_block++] = norm;
        }
        list += n;
        if (*list == ',')
            list++;
    }
}

static bool blocked_word(const kk_emotewall *w, const char *word)
{
    for (int i = 0; i < w->n_block; i++)
        if (strcmp(w->block[i], word) == 0)
            return true;
    return false;
}

bool kk_emotewall_blocked(const kk_emotewall *w, const kk_emote *em)
{
    char buf[KEY_MAX];
    if (em->text && kk_emoji_key(em->text, strlen(em->text), buf, sizeof buf) &&
        blocked_word(w, buf))
        return true;
    if (em->name) {
        norm_name(em->name, buf, sizeof buf);
        if (buf[0] && blocked_word(w, buf))
            return true;
    }
    if (em->id) {
        norm_name(em->id, buf, sizeof buf);
        if (buf[0] && blocked_word(w, buf))
            return true;
    }
    return false;
}

/* ---- life cycle ---------------------------------------------------------- */

static void layer_damage(void *ud, kk_damage_fn add, void *to);
static void layer_draw(void *ud, cairo_t *cr, kk_rect clip);
static void layer_painted(void *ud);

kk_emotewall *kk_emotewall_new(kk_emotes *images, uint64_t seed)
{
    kk_emotewall *w = calloc(1, sizeof *w);
    if (!w)
        return NULL;
    w->images = images;
    w->jobs = calloc(MAX_JOBS, sizeof *w->jobs);
    w->parts = calloc(MAX_PARTICLES, sizeof *w->parts);
    if (!w->jobs || !w->parts) {
        free(w->jobs);
        free(w->parts);
        free(w);
        return NULL;
    }
    kk_rng_seed(&w->rng, seed ^ 0x5eed0f5eedULL);
    w->layer = (kk_layer){layer_damage, layer_draw, layer_painted, w};
    w->cfg = (kk_wall_config){.duration = 5, .max_per_message = 10,
                              .max_on_screen = 150, .reactions_per_icon = 1};
    return w;
}

static void job_free(job *j)
{
    free(j->platform);
    free((char *)j->em.id);
    free((char *)j->em.name);
    free((char *)j->em.url);
    free((char *)j->em.text);
}

static void add_removed(kk_emotewall *w, kk_rect r)
{
    if (kk_rect_empty(r))
        return;
    if (w->n_removed == MAX_REMOVED)
        w->removed[MAX_REMOVED - 1] = kk_rect_union(w->removed[MAX_REMOVED - 1], r);
    else
        w->removed[w->n_removed++] = r;
}

static void drop_particle(kk_emotewall *w, int i)
{
    particle *p = &w->parts[i];
    add_removed(w, p->drawn);
    cairo_surface_destroy(p->img);
    w->parts[i] = w->parts[--w->n_parts];
}

static void clear(kk_emotewall *w)
{
    while (w->n_parts)
        drop_particle(w, w->n_parts - 1);
    for (int i = 0; i < w->n_jobs; i++)
        job_free(&w->jobs[i]);
    w->n_jobs = 0;
    memset(w->combos, 0, sizeof w->combos);
}

void kk_emotewall_free(kk_emotewall *w)
{
    if (!w)
        return;
    clear(w);
    free_blacklist(w);
    free(w->jobs);
    free(w->parts);
    free(w);
}

void kk_emotewall_configure(kk_emotewall *w, const kk_wall_config *cfg)
{
    w->cfg = *cfg;
    w->cfg.blacklist = NULL;
    if (w->cfg.duration < 0.5)
        w->cfg.duration = 0.5;
    if (w->cfg.max_on_screen > MAX_PARTICLES)
        w->cfg.max_on_screen = MAX_PARTICLES;
    if (w->cfg.combo_count > MAX_COMBO_COUNT)
        w->cfg.combo_count = MAX_COMBO_COUNT;
    if (w->cfg.reactions_per_icon < 1)
        w->cfg.reactions_per_icon = 1;
    parse_blacklist(w, cfg->blacklist);
    if (!w->cfg.enabled)
        clear(w);
}

void kk_emotewall_resize(kk_emotewall *w, int width, int height)
{
    w->width = width;
    w->height = height;
}

const kk_layer *kk_emotewall_layer(kk_emotewall *w)
{
    return &w->layer;
}

int kk_emotewall_flying(const kk_emotewall *w)
{
    return w->n_parts;
}

int kk_emotewall_waiting(const kk_emotewall *w)
{
    return w->n_jobs;
}

/* ---- queueing ------------------------------------------------------------ */

static char *dup_or_null(const char *s)
{
    return s ? strdup(s) : NULL;
}

static void queue(kk_emotewall *w, const char *platform, const kk_emote *em,
                  double at)
{
    if (w->n_jobs == MAX_JOBS)
        return;
    job *j = &w->jobs[w->n_jobs];
    *j = (job){.platform = dup_or_null(platform), .at = at};
    j->em.id = dup_or_null(em->id);
    j->em.name = dup_or_null(em->name);
    j->em.url = dup_or_null(em->url);
    j->em.text = dup_or_null(em->text);
    if ((em->url && !j->em.url) || (em->text && !j->em.text)) {
        job_free(j);
        return;
    }
    w->n_jobs++;
}

/* Same key as the image cache: one per emote, whatever its form. */
static bool emote_key(const char *platform, const kk_emote *em, char *out,
                      size_t size)
{
    if (em->url)
        return kk_pathf(out, size, "%s\x1f%s", platform ? platform : "",
                        em->id && em->id[0] ? em->id : em->url);
    char ek[KEY_MAX];
    return em->text && kk_emoji_key(em->text, strlen(em->text), ek, sizeof ek) &&
           kk_pathf(out, size, "\x1f%s", ek);
}

static combo *find_combo(kk_emotewall *w, const char *key)
{
    combo *oldest = &w->combos[0];
    for (int i = 0; i < MAX_COMBOS; i++) {
        combo *c = &w->combos[i];
        if (c->key[0] && strcmp(c->key, key) == 0)
            return c;
        if (c->last < oldest->last)
            oldest = c;
    }
    memset(oldest, 0, sizeof *oldest);
    kk_pathf(oldest->key, sizeof oldest->key, "%s", key);
    oldest->until = -INFINITY; /* no combo yet, even at time 0 */
    return oldest;
}

/* Counts one more message with key at now. Returns how many copies to send
 * because of the combo: 0 (no combo), 1 (combo running) or combo_count (it
 * just started: this message and the ones that built it). */
static int combo_seen(kk_emotewall *w, const char *key, double now)
{
    int need = w->cfg.combo_count;
    double window = w->cfg.combo_window;
    combo *c = find_combo(w, key);
    c->last = now;
    if (now <= c->until) {
        c->until = now + window;
        return 1;
    }
    c->times[c->head] = now;
    c->head = (c->head + 1) % MAX_COMBO_COUNT;
    if (c->n < MAX_COMBO_COUNT)
        c->n++;
    int recent = 0;
    for (int i = 0; i < c->n; i++)
        recent += now - c->times[i] <= window;
    if (recent < need)
        return 0;
    c->until = now + window;
    c->n = c->head = 0;
    return need;
}

typedef struct {
    kk_emote em;
    char key[KEY_MAX];
    int send; /* copies */
} found;

void kk_emotewall_message(kk_emotewall *w, const kk_chat_msg *m, double now)
{
    if (!w->cfg.enabled)
        return;
    found f[MAX_IN_MESSAGE];
    int n = 0;
    char texts[MAX_IN_MESSAGE * 32]; /* the emoji found, NUL-terminated */
    size_t used = 0;

    for (int i = 0; i < m->n_emotes && n < MAX_IN_MESSAGE; i++) {
        f[n].em = m->emotes[i];
        if (m->emotes[i].url)
            f[n].em.text = NULL;
        n++;
    }
    const char *text = m->text ? m->text : "";
    size_t len = strlen(text), pos = 0, start, elen;
    while (n < MAX_IN_MESSAGE && (elen = kk_emoji_next(text, len, &pos, &start))) {
        if (used + elen + 1 > sizeof texts)
            break;
        memcpy(texts + used, text + start, elen);
        texts[used + elen] = '\0';
        f[n++].em = (kk_emote){.text = texts + used, .start = start, .len = elen};
        used += elen + 1;
    }

    /* Blacklisted ones are as if never typed. */
    int kept = 0;
    for (int i = 0; i < n; i++)
        if (!kk_emotewall_blocked(w, &f[i].em) &&
            emote_key(m->platform, &f[i].em, f[i].key, sizeof f[i].key))
            f[kept++] = f[i];
    n = kept;
    if (n == 0)
        return;

    int min = w->cfg.min_per_message, combo_n = w->cfg.combo_count;
    bool all = (min > 0 && n >= min) || (min <= 0 && combo_n <= 0);
    for (int i = 0; i < n; i++)
        f[i].send = all ? 1 : 0;

    if (combo_n > 0)
        for (int i = 0; i < n; i++) {
            bool first = true;
            for (int k = 0; k < i && first; k++)
                first = strcmp(f[k].key, f[i].key) != 0;
            if (!first)
                continue; /* one sighting per message */
            int extra = combo_seen(w, f[i].key, now);
            if (extra == 0)
                continue;
            /* This message's copies, plus the earlier messages' on start. */
            for (int k = i; k < n; k++)
                if (strcmp(f[k].key, f[i].key) == 0)
                    f[k].send = 1;
            f[i].send += extra > 1 ? extra - 1 : 0;
        }

    int budget = w->cfg.max_per_message > 0 ? w->cfg.max_per_message : MAX_IN_MESSAGE;
    double at = now;
    for (int i = 0; i < n && budget > 0; i++)
        for (int c = 0; c < f[i].send && budget > 0; c++, budget--) {
            queue(w, m->platform, &f[i].em, at);
            /* A burst comes out in a quick ripple, not all in one frame. */
            at += kk_rng_range(&w->rng, 0.05, 0.2);
        }
}

void kk_emotewall_reaction(kk_emotewall *w, const kk_reaction *r, double now)
{
    if (!w->cfg.enabled || !w->cfg.reactions || r->count <= 0 ||
        kk_emotewall_blocked(w, &r->emote))
        return;
    int per = w->cfg.reactions_per_icon;
    int icons = (r->count + per - 1) / per;
    if (icons > REACTION_MAX_ICONS)
        icons = REACTION_MAX_ICONS;
    double at = now + (r->delay > 0 ? r->delay : 0);
    for (int i = 0; i < icons; i++)
        queue(w, r->platform, &r->emote, at + kk_rng_range(&w->rng, 0, 1.0));
}

/* ---- motion -------------------------------------------------------------- */

/* p folded into [0, len] as if bouncing between two walls. */
static double reflect(double p, double len)
{
    if (len <= 0)
        return 0;
    double m = fmod(p, 2 * len);
    if (m < 0)
        m += 2 * len;
    return m <= len ? m : 2 * len - m;
}

static void launch(kk_emotewall *w, cairo_surface_t *img, double now)
{
    if (w->n_parts >= w->cfg.max_on_screen || w->n_parts == MAX_PARTICLES)
        return;
    double px = kk_emotes_size(w->images);
    double W = w->width, H = w->height;
    kk_rng *rng = &w->rng;
    particle *p = &w->parts[w->n_parts++];
    /* Some faster, some slower, so a burst spreads out instead of moving
     * as a block. */
    *p = (particle){.img = cairo_surface_reference(img), .born = now,
                    .life = w->cfg.duration * kk_rng_range(rng, 0.8, 1.2),
                    .style = w->cfg.style,
                    .phase = kk_rng_range(rng, 0, TWO_PI)};

    switch (p->style) {
    case KK_WALL_FLY: {
        /* Edge to edge through a random line; it enters and leaves fully. */
        double angle = kk_rng_range(rng, 0, TWO_PI);
        double cx = kk_rng_range(rng, W * 0.25, W * 0.75);
        double cy = kk_rng_range(rng, H * 0.25, H * 0.75);
        double reach = hypot(W, H) * 0.75 + px;
        p->x0 = cx - cos(angle) * reach - px / 2;
        p->y0 = cy - sin(angle) * reach - px / 2;
        p->x1 = cx + cos(angle) * reach - px / 2;
        p->y1 = cy + sin(angle) * reach - px / 2;
        break;
    }
    case KK_WALL_BOUNCE: {
        /* The DVD logo: a diagonal at constant speed, off every edge. */
        p->x0 = kk_rng_range(rng, 0, W > px ? W - px : 1);
        p->y0 = kk_rng_range(rng, 0, H > px ? H - px : 1);
        double angle = kk_rng_range(rng, 0.5, 1.07); /* ~30..60 degrees */
        double speed = kk_rng_range(rng, 140, 260);  /* px per second */
        p->vx = cos(angle) * speed * (kk_rng_int(rng, 2) ? 1 : -1);
        p->vy = sin(angle) * speed * (kk_rng_int(rng, 2) ? 1 : -1);
        break;
    }
    default:
        p->x0 = p->x1 = kk_rng_range(rng, 0, W > px ? W - px : 1);
        p->y0 = H;
        p->y1 = -px;
        p->sway = px * kk_rng_range(rng, 0.3, 0.9);
        p->freq = kk_rng_range(rng, 0.5, 1.4);
        break;
    }
}

static void place(kk_emotewall *w, particle *p, double now)
{
    double size = kk_emotes_size(w->images);
    double age = now - p->born;
    double t = age / p->life;
    t = t < 0 ? 0 : t > 1 ? 1 : t;
    double x, y;
    if (p->style == KK_WALL_BOUNCE) {
        x = reflect(p->x0 + p->vx * age, w->width - size);
        y = reflect(p->y0 + p->vy * age, w->height - size);
    } else {
        x = p->x0 + (p->x1 - p->x0) * t +
            p->sway * sin(TWO_PI * p->freq * age / 2 + p->phase);
        y = p->y0 + (p->y1 - p->y0) * t;
    }
    /* Images are painted 1:1 (the fast path); only the short pop-in
     * scales, around the centre. */
    double pop = age < POP_S ? 0.5 + 0.5 * age / POP_S : 1.0;
    p->cur_scale = pop;
    x += size * (1 - pop) / 2;
    y += size * (1 - pop) / 2;
    p->alpha = 1.0;
    if (t > 0.75)
        p->alpha = (1 - t) / 0.25;
    if (p->style != KK_WALL_RISE && age < FADE_IN_S)
        p->alpha = age / FADE_IN_S;
    p->x = (int)lround(x);
    p->y = (int)lround(y);
    int side = (int)ceil(size * pop) + 1;
    p->cur = (kk_rect){p->x, p->y, side, side};
}

void kk_emotewall_update(kk_emotewall *w, double now)
{
    /* Start what is due; images still loading wait their turn. */
    for (int i = 0; i < w->n_jobs;) {
        job *j = &w->jobs[i];
        cairo_surface_t *img = NULL;
        kk_emote_state st = KK_EMOTE_LOADING;
        if (j->at <= now)
            st = kk_emotes_get(w->images, j->platform, &j->em, &img);
        if (j->at > now || (st == KK_EMOTE_LOADING && now - j->at < GIVE_UP_S)) {
            i++;
            continue;
        }
        if (st == KK_EMOTE_READY)
            launch(w, img, now);
        job_free(j);
        w->jobs[i] = w->jobs[--w->n_jobs];
    }

    for (int i = 0; i < w->n_parts;) {
        particle *p = &w->parts[i];
        if (now - p->born >= p->life) {
            drop_particle(w, i);
            continue;
        }
        place(w, p, now);
        i++;
    }
}

/* ---- layer --------------------------------------------------------------- */

static void layer_damage(void *ud, kk_damage_fn add, void *to)
{
    kk_emotewall *w = ud;
    for (int i = 0; i < w->n_removed; i++)
        add(to, w->removed[i]);
    for (int i = 0; i < w->n_parts; i++) {
        const particle *p = &w->parts[i];
        add(to, p->drawn);
        add(to, p->cur);
    }
}

static void layer_draw(void *ud, cairo_t *cr, kk_rect clip)
{
    kk_emotewall *w = ud;
    for (int i = 0; i < w->n_parts; i++) {
        const particle *p = &w->parts[i];
        if (kk_rect_empty(p->cur) || !kk_rect_intersects(p->cur, clip))
            continue;
        cairo_save(cr);
        cairo_translate(cr, p->x, p->y);
        if (fabs(p->cur_scale - 1.0) > 1e-3)
            cairo_scale(cr, p->cur_scale, p->cur_scale);
        cairo_set_source_surface(cr, p->img, 0, 0);
        if (p->alpha >= 0.999)
            cairo_paint(cr);
        else
            cairo_paint_with_alpha(cr, p->alpha);
        cairo_restore(cr);
    }
}

static void layer_painted(void *ud)
{
    kk_emotewall *w = ud;
    w->n_removed = 0;
    for (int i = 0; i < w->n_parts; i++)
        w->parts[i].drawn = w->parts[i].cur;
}
