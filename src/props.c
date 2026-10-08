/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "props.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_PROPS 256
#define MAX_BOMBS 6
#define MAX_COLLECTIBLES 30
#define MAX_REMOVED 32
/* Screen y grows down: gravity is positive here. */
#define GRAVITY 1400.0
#define BOMB_FLIGHT_S 0.9 /* from the hand to the ground */
#define BOMB_FUSE_S 2.0   /* from the throw to the bang */
#define BLAST_S 0.55
#define COLLECTIBLE_S 90.0 /* then it fades away */
#define FADE_S 3.0
#define SCORE_S 1.3
#define GEM_CHANCE 0.12

typedef enum {
    P_BOMB,
    P_BLAST,
    P_SPARK,
    P_STAR,
    P_GEM,
    P_SCORE,
} kind;

typedef struct {
    kind k;
    double x, y;   /* centre, screen pixels */
    double vx, vy; /* px/s, y down */
    double t, life;
    double angle, spin;
    double r;     /* size */
    double phase; /* collectibles bob out of step */
    long points;
    bool landed;
    double cr, cg, cb; /* spark colour */
    char text[24];
    kk_rect cur, drawn;
} prop;

struct kk_props {
    kk_stage *stage;
    kk_rng rng;
    kk_layer layer;
    kk_catch_fn on_catch;
    void *catch_ud;
    prop v[MAX_PROPS];
    int n;
    kk_rect removed[MAX_REMOVED];
    int n_removed;
};

static void layer_damage(void *ud, kk_damage_fn add, void *to);
static void layer_draw(void *ud, cairo_t *cr, kk_rect clip);
static void layer_painted(void *ud);

kk_props *kk_props_new(kk_stage *stage, uint64_t seed)
{
    kk_props *p = calloc(1, sizeof *p);
    if (!p)
        return NULL;
    p->stage = stage;
    kk_rng_seed(&p->rng, seed ^ 0xb0b0b0b0ULL);
    p->layer = (kk_layer){layer_damage, layer_draw, layer_painted, p};
    return p;
}

void kk_props_free(kk_props *p)
{
    free(p);
}

const kk_layer *kk_props_layer(kk_props *p)
{
    return &p->layer;
}

void kk_props_set_catch(kk_props *p, kk_catch_fn fn, void *ud)
{
    p->on_catch = fn;
    p->catch_ud = ud;
}

/* One unit: the stage's scale (2 by default) keeps props in proportion to
 * the avatars. */
static double unit(const kk_props *p)
{
    return p->stage->cfg.scale > 0 ? p->stage->cfg.scale : 1.0;
}

static double ground(const kk_props *p)
{
    return kk_stage_view(p->stage).ground_y;
}

static prop *add(kk_props *p, kind k)
{
    if (p->n == MAX_PROPS)
        return NULL;
    prop *q = &p->v[p->n++];
    *q = (prop){.k = k};
    return q;
}

static void add_removed(kk_props *p, kk_rect r)
{
    if (kk_rect_empty(r))
        return;
    if (p->n_removed == MAX_REMOVED)
        p->removed[MAX_REMOVED - 1] = kk_rect_union(p->removed[MAX_REMOVED - 1], r);
    else
        p->removed[p->n_removed++] = r;
}

static void drop(kk_props *p, int i)
{
    add_removed(p, p->v[i].drawn);
    p->v[i] = p->v[--p->n];
}

static int count(const kk_props *p, kind a, kind b)
{
    int n = 0;
    for (int i = 0; i < p->n; i++)
        n += p->v[i].k == a || p->v[i].k == b;
    return n;
}

int kk_props_collectibles(const kk_props *p)
{
    return count(p, P_STAR, P_GEM);
}

int kk_props_bombs(const kk_props *p)
{
    return count(p, P_BOMB, P_BOMB);
}

bool kk_props_collectible_at(const kk_props *p, int i, double *x, double *y)
{
    for (int k = 0; k < p->n; k++)
        if ((p->v[k].k == P_STAR || p->v[k].k == P_GEM) && i-- == 0) {
            *x = p->v[k].x;
            *y = p->v[k].y;
            return true;
        }
    return false;
}

void kk_props_clear(kk_props *p)
{
    while (p->n)
        drop(p, p->n - 1);
}

/* ---- bombs --------------------------------------------------------------- */

bool kk_props_bomb(kk_props *p, const kk_avatar *thrower, double x)
{
    if (kk_props_bombs(p) >= MAX_BOMBS)
        return false;
    prop *b = add(p, P_BOMB);
    if (!b)
        return false;
    double u = unit(p), g = ground(p);
    b->r = 6.0 * u;
    if (thrower) {
        /* From the hand: a little above the middle of the body. */
        b->x = thrower->x;
        b->y = g - thrower->lift - kk_avatar_height(thrower) * 0.7;
    } else {
        b->x = x;
        b->y = -b->r;
    }
    double land = g - b->r;
    double T = BOMB_FLIGHT_S;
    b->vx = (x - b->x) / T;
    /* y(T) = land: y0 + vy T + g T²/2. */
    b->vy = (land - b->y - 0.5 * GRAVITY * T * T) / T;
    b->spin = kk_rng_range(&p->rng, -8.0, 8.0);
    b->life = BOMB_FUSE_S;
    return true;
}

static void spark(kk_props *p, double x, double y, double speed, double life, double r,
                  double cr, double cg, double cb)
{
    prop *s = add(p, P_SPARK);
    if (!s)
        return;
    double a = kk_rng_range(&p->rng, M_PI, 2.0 * M_PI); /* upwards, mostly */
    double v = kk_rng_range(&p->rng, 0.3, 1.0) * speed;
    *s = (prop){.k = P_SPARK, .x = x, .y = y, .vx = cos(a) * v * 1.3, .vy = sin(a) * v,
                .life = life * kk_rng_range(&p->rng, 0.6, 1.0), .r = r, .cr = cr, .cg = cg,
                .cb = cb};
}

/* The bang: a flash, sparks and smoke, and everyone near goes flying. */
static void explode(kk_props *p, double x, double y)
{
    double u = unit(p), reach = 75.0 * u;
    prop *b = add(p, P_BLAST);
    if (b)
        *b = (prop){.k = P_BLAST, .x = x, .y = y, .life = BLAST_S, .r = reach * 0.7};
    for (int i = 0; i < 18; i++)
        spark(p, x, y, 520.0 * u / 2.0, 0.9, kk_rng_range(&p->rng, 1.5, 3.0) * u, 1.0,
              kk_rng_range(&p->rng, 0.45, 0.9), 0.1);
    for (int i = 0; i < 6; i++)
        spark(p, x, y, 160.0 * u / 2.0, 1.3, kk_rng_range(&p->rng, 5.0, 9.0) * u, 0.35, 0.35,
              0.35);

    double g = ground(p);
    for (int i = 0; i < p->stage->count; i++) {
        kk_avatar *a = &p->stage->avatars[i];
        double ay = g - a->lift - kk_avatar_height(a) / 2.0;
        double dx = a->x - x, d = hypot(dx, ay - y);
        if (d > reach)
            continue;
        double f = 1.0 - d / reach; /* 1 at the bomb, 0 at the edge */
        double side = fabs(dx) < 1.0 ? (kk_rng_int(&p->rng, 2) ? 1.0 : -1.0) : (dx > 0 ? 1 : -1);
        double k = u / 2.0;
        kk_avatar_launch(a, side * (180.0 + 420.0 * f) * k, (450.0 + 650.0 * f) * k,
                         side * (5.0 + 9.0 * f));
    }
}

/* ---- collectibles -------------------------------------------------------- */

static int compare_double(const void *a, const void *b)
{
    double x = *(const double *)a, y = *(const double *)b;
    return (x > y) - (x < y);
}

/* How high to float: above a standing head, below a jumping one, for the
 * typical avatar on stage (the median, so one giant doesn't decide). */
static void reach_heights(const kk_props *p, double *head, double *jump)
{
    double hs[256], js[256];
    int n = 0;
    for (int i = 0; i < p->stage->count && n < 256; i++) {
        const kk_avatar *a = &p->stage->avatars[i];
        hs[n] = kk_avatar_height(a);
        js[n] = a->sheet->cell_h * a->size * 0.6; /* see JUMP_HEIGHT */
        n++;
    }
    if (n == 0) {
        *head = 40.0 * unit(p);
        *jump = 30.0 * unit(p);
        return;
    }
    qsort(hs, (size_t)n, sizeof hs[0], compare_double);
    qsort(js, (size_t)n, sizeof js[0], compare_double);
    *head = hs[n / 2];
    *jump = js[n / 2];
}

int kk_props_spawn(kk_props *p, int n, long points)
{
    double u = unit(p), g = ground(p), head, jump;
    reach_heights(p, &head, &jump);
    double r = 7.0 * u;
    int made = 0;
    for (int i = 0; i < n && kk_props_collectibles(p) < MAX_COLLECTIBLES; i++) {
        double margin = r * 3.0;
        if (p->stage->width <= 2 * margin)
            break;
        /* Apart from the others, if it can be. */
        double x = 0;
        for (int tries = 0; tries < 12; tries++) {
            x = kk_rng_range(&p->rng, margin, p->stage->width - margin);
            bool clear = true;
            for (int k = 0; k < p->n && clear; k++)
                if ((p->v[k].k == P_STAR || p->v[k].k == P_GEM) && fabs(p->v[k].x - x) < r * 4)
                    clear = false;
            if (clear)
                break;
        }
        bool gem = kk_rng_range(&p->rng, 0.0, 1.0) < GEM_CHANCE;
        prop *c = add(p, gem ? P_GEM : P_STAR);
        if (!c)
            break;
        double h = head + jump * kk_rng_range(&p->rng, 0.45, 0.7);
        *c = (prop){.k = gem ? P_GEM : P_STAR, .x = x, .y = g - h, .r = r,
                    .life = COLLECTIBLE_S, .points = gem ? points * 3 : points,
                    .phase = kk_rng_range(&p->rng, 0.0, 2.0 * M_PI),
                    .spin = kk_rng_range(&p->rng, -0.8, 0.8)};
        made++;
    }
    return made;
}

/* Does a's body touch the circle (x, y, r)? Sprites have empty room at the
 * sides, so only the middle of the cell counts. */
static bool touches(const kk_props *p, const kk_avatar *a, double x, double y, double r)
{
    double g = ground(p);
    double bottom = g - a->lift, top = bottom - kk_avatar_height(a);
    double half = kk_avatar_width(a) * 0.3;
    double nx = fmax(a->x - half, fmin(x, a->x + half));
    double ny = fmax(top, fmin(y, bottom));
    return hypot(nx - x, ny - y) <= r;
}

static void caught(kk_props *p, int i, kk_avatar *a)
{
    prop c = p->v[i];
    drop(p, i);
    long shown = p->on_catch ? p->on_catch(p->catch_ud, a, c.points) : 0;
    double u = unit(p);
    for (int k = 0; k < 10; k++)
        spark(p, c.x, c.y, 200.0 * u / 2.0, 0.6, kk_rng_range(&p->rng, 1.0, 2.2) * u, 1.0,
              c.k == P_GEM ? 0.95 : 0.85, c.k == P_GEM ? 1.0 : 0.2);
    prop *s = shown > 0 ? add(p, P_SCORE) : NULL;
    if (s) {
        *s = (prop){.k = P_SCORE, .x = c.x, .y = c.y - c.r, .life = SCORE_S, .r = 9.0 * u};
        snprintf(s->text, sizeof s->text, "+%ld", shown);
    }
}

/* ---- update -------------------------------------------------------------- */

static void update_bomb(kk_props *p, prop *b, double dt)
{
    double land = ground(p) - b->r;
    if (!b->landed) {
        b->x += b->vx * dt;
        b->y += b->vy * dt;
        b->vy += GRAVITY * dt;
        b->angle += b->spin * dt;
        if (b->x < b->r || b->x > p->stage->width - b->r) {
            b->x = fmax(b->r, fmin(p->stage->width - b->r, b->x));
            b->vx = -b->vx * 0.5;
        }
        if (b->y >= land && b->vy > 0) {
            b->y = land;
            if (b->vy > 300.0) { /* a small hop, then it sits */
                b->vy = -b->vy * 0.3;
                b->vx *= 0.5;
            } else {
                b->landed = true;
                b->vx = b->vy = 0;
            }
        }
    }
}

void kk_props_update(kk_props *p, double dt)
{
    for (int i = 0; i < p->n;) {
        prop *q = &p->v[i];
        q->t += dt;
        switch (q->k) {
        case P_BOMB:
            update_bomb(p, q, dt);
            if (q->t >= q->life) {
                double x = q->x, y = q->y;
                drop(p, i);
                explode(p, x, y);
                continue;
            }
            break;
        case P_SPARK:
            q->x += q->vx * dt;
            q->y += q->vy * dt;
            q->vy += GRAVITY * 0.5 * dt;
            break;
        case P_SCORE:
            q->y -= 30.0 * dt;
            break;
        case P_STAR:
        case P_GEM: {
            q->angle += q->spin * dt;
            kk_avatar *who = NULL;
            for (int k = 0; k < p->stage->count && !who; k++)
                if (touches(p, &p->stage->avatars[k], q->x, q->y, q->r))
                    who = &p->stage->avatars[k];
            if (who) {
                caught(p, i, who);
                continue;
            }
            break;
        }
        case P_BLAST:
            break;
        }
        if (q->t >= q->life) {
            drop(p, i);
            continue;
        }
        i++;
    }
}

/* ---- drawing ------------------------------------------------------------- */

/* Where the bobbing collectible is now. */
static double bob(const prop *q)
{
    return q->y + sin(q->t * 2.4 + q->phase) * q->r * 0.35;
}

/* 0..1: pops in, fades out at the end of its life. */
static double presence(const prop *q)
{
    double in = q->t < 0.25 ? q->t / 0.25 : 1.0;
    double out = q->life - q->t < FADE_S ? (q->life - q->t) / FADE_S : 1.0;
    return fmax(0.0, fmin(in, out));
}

static kk_rect around(double x, double y, double r)
{
    int ir = (int)ceil(r) + 2;
    return (kk_rect){(int)floor(x) - ir, (int)floor(y) - ir, 2 * ir + 1, 2 * ir + 1};
}

static kk_rect extent(const prop *q)
{
    switch (q->k) {
    case P_BOMB:
        return around(q->x, q->y, q->r * 2.4); /* fuse and spark */
    case P_BLAST:
        return around(q->x, q->y, q->r * 1.05);
    case P_STAR:
    case P_GEM:
        return around(q->x, bob(q), q->r * 1.6); /* the glow */
    case P_SCORE:
        return around(q->x, q->y, q->r * 2.2);
    case P_SPARK:
        break;
    }
    return around(q->x, q->y, q->r);
}

static void draw_bomb(const prop *q, cairo_t *cr)
{
    double r = q->r;
    cairo_save(cr);
    cairo_translate(cr, q->x, q->y);
    cairo_rotate(cr, q->angle);
    /* Fuse: a cap and a curl of cord with its spark. */
    cairo_set_source_rgb(cr, 0.35, 0.35, 0.38);
    cairo_rectangle(cr, -r * 0.35, -r * 1.25, r * 0.7, r * 0.45);
    cairo_fill(cr);
    cairo_set_source_rgb(cr, 0.75, 0.6, 0.4);
    cairo_set_line_width(cr, r * 0.18);
    cairo_move_to(cr, 0, -r * 1.25);
    cairo_curve_to(cr, r * 0.2, -r * 1.8, r * 0.8, -r * 1.6, r * 0.9, -r * 2.0);
    cairo_stroke(cr);
    double flick = 0.6 + 0.4 * sin(q->t * 40.0);
    cairo_set_source_rgba(cr, 1.0, 0.8, 0.2, flick);
    cairo_arc(cr, r * 0.9, -r * 2.0, r * 0.35 * (0.8 + flick * 0.4), 0, 2 * M_PI);
    cairo_fill(cr);
    /* Body, red-hot near the end. */
    double hot = q->t > q->life - 0.6 ? 0.5 + 0.5 * sin(q->t * 30.0) : 0.0;
    cairo_pattern_t *g = cairo_pattern_create_radial(-r * 0.35, -r * 0.35, r * 0.1, 0, 0, r);
    cairo_pattern_add_color_stop_rgb(g, 0, 0.45 + hot * 0.5, 0.45, 0.5);
    cairo_pattern_add_color_stop_rgb(g, 1, 0.08 + hot * 0.5, 0.08, 0.1);
    cairo_set_source(cr, g);
    cairo_arc(cr, 0, 0, r, 0, 2 * M_PI);
    cairo_fill(cr);
    cairo_pattern_destroy(g);
    cairo_restore(cr);
}

static void draw_blast(const prop *q, cairo_t *cr)
{
    double k = q->t / q->life;
    double r = q->r * (0.35 + 0.7 * sqrt(k));
    double a = 1.0 - k;
    cairo_pattern_t *g = cairo_pattern_create_radial(q->x, q->y, 0, q->x, q->y, r);
    cairo_pattern_add_color_stop_rgba(g, 0.0, 1.0, 1.0, 0.85, a);
    cairo_pattern_add_color_stop_rgba(g, 0.35, 1.0, 0.8, 0.2, a * 0.9);
    cairo_pattern_add_color_stop_rgba(g, 0.75, 1.0, 0.35, 0.05, a * 0.6);
    cairo_pattern_add_color_stop_rgba(g, 1.0, 0.6, 0.1, 0.0, 0.0);
    cairo_set_source(cr, g);
    cairo_arc(cr, q->x, q->y, r, 0, 2 * M_PI);
    cairo_fill(cr);
    cairo_pattern_destroy(g);
}

static void star_path(cairo_t *cr, double r)
{
    for (int i = 0; i < 10; i++) {
        double a = -M_PI / 2 + i * M_PI / 5;
        double rr = i % 2 ? r * 0.45 : r;
        if (i == 0)
            cairo_move_to(cr, cos(a) * rr, sin(a) * rr);
        else
            cairo_line_to(cr, cos(a) * rr, sin(a) * rr);
    }
    cairo_close_path(cr);
}

static void draw_collectible(const prop *q, cairo_t *cr)
{
    double k = presence(q);
    /* Blinks in its last seconds. */
    if (q->life - q->t < FADE_S && fmod(q->t * 6.0, 1.0) < 0.3)
        k *= 0.4;
    double pop = q->t < 0.25 ? 1.0 + 0.3 * sin(q->t / 0.25 * M_PI) : 1.0;
    double r = q->r * pop, y = bob(q);
    bool gem = q->k == P_GEM;
    cairo_save(cr);
    cairo_translate(cr, q->x, y);
    /* Glow. */
    cairo_pattern_t *g = cairo_pattern_create_radial(0, 0, r * 0.3, 0, 0, r * 1.6);
    cairo_pattern_add_color_stop_rgba(g, 0, gem ? 0.5 : 1.0, gem ? 0.95 : 0.95, gem ? 1.0 : 0.5,
                                      0.55 * k);
    cairo_pattern_add_color_stop_rgba(g, 1, 1.0, 1.0, 1.0, 0.0);
    cairo_set_source(cr, g);
    cairo_arc(cr, 0, 0, r * 1.6, 0, 2 * M_PI);
    cairo_fill(cr);
    cairo_pattern_destroy(g);

    cairo_rotate(cr, q->angle);
    if (gem) {
        cairo_move_to(cr, 0, -r);
        cairo_line_to(cr, r * 0.8, -r * 0.25);
        cairo_line_to(cr, 0, r);
        cairo_line_to(cr, -r * 0.8, -r * 0.25);
        cairo_close_path(cr);
        cairo_set_source_rgba(cr, 0.25, 0.8, 1.0, k);
        cairo_fill_preserve(cr);
        cairo_set_source_rgba(cr, 0.05, 0.35, 0.6, k);
        cairo_set_line_width(cr, r * 0.12);
        cairo_stroke(cr);
        cairo_move_to(cr, -r * 0.8, -r * 0.25);
        cairo_line_to(cr, r * 0.8, -r * 0.25);
        cairo_set_source_rgba(cr, 0.8, 0.97, 1.0, k);
        cairo_set_line_width(cr, r * 0.08);
        cairo_stroke(cr);
    } else {
        star_path(cr, r);
        cairo_set_source_rgba(cr, 1.0, 0.85, 0.15, k);
        cairo_fill_preserve(cr);
        cairo_set_source_rgba(cr, 0.75, 0.45, 0.0, k);
        cairo_set_line_width(cr, r * 0.12);
        cairo_stroke(cr);
        star_path(cr, r * 0.45);
        cairo_set_source_rgba(cr, 1.0, 0.97, 0.7, k);
        cairo_fill(cr);
    }
    cairo_restore(cr);
}

static void draw_spark(const prop *q, cairo_t *cr)
{
    double a = 1.0 - q->t / q->life;
    cairo_set_source_rgba(cr, q->cr, q->cg, q->cb, a);
    cairo_arc(cr, q->x, q->y, q->r, 0, 2 * M_PI);
    cairo_fill(cr);
}

static void draw_score(const prop *q, cairo_t *cr)
{
    double a = 1.0 - q->t / q->life;
    cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
    cairo_set_font_size(cr, q->r * 1.8);
    cairo_text_extents_t te;
    cairo_text_extents(cr, q->text, &te);
    cairo_move_to(cr, q->x - te.width / 2 - te.x_bearing, q->y - te.height / 2 - te.y_bearing);
    cairo_text_path(cr, q->text);
    cairo_set_source_rgba(cr, 0.15, 0.1, 0.0, a);
    cairo_set_line_width(cr, q->r * 0.35);
    cairo_stroke_preserve(cr);
    cairo_set_source_rgba(cr, 1.0, 0.9, 0.3, a);
    cairo_fill(cr);
}

static void layer_draw(void *ud, cairo_t *cr, kk_rect clip)
{
    kk_props *p = ud;
    /* Blasts and sparks under the rest. */
    for (int pass = 0; pass < 2; pass++)
        for (int i = 0; i < p->n; i++) {
            const prop *q = &p->v[i];
            bool back = q->k == P_BLAST || q->k == P_SPARK;
            if (back != (pass == 0) || !kk_rect_intersects(extent(q), clip))
                continue;
            switch (q->k) {
            case P_BOMB:
                draw_bomb(q, cr);
                break;
            case P_BLAST:
                draw_blast(q, cr);
                break;
            case P_SPARK:
                draw_spark(q, cr);
                break;
            case P_STAR:
            case P_GEM:
                draw_collectible(q, cr);
                break;
            case P_SCORE:
                draw_score(q, cr);
                break;
            }
        }
}

static void layer_damage(void *ud, kk_damage_fn add_fn, void *to)
{
    kk_props *p = ud;
    for (int i = 0; i < p->n_removed; i++)
        add_fn(to, p->removed[i]);
    /* Everything here moves or glows every frame. */
    for (int i = 0; i < p->n; i++) {
        prop *q = &p->v[i];
        q->cur = extent(q);
        add_fn(to, q->drawn);
        add_fn(to, q->cur);
    }
}

static void layer_painted(void *ud)
{
    kk_props *p = ud;
    p->n_removed = 0;
    for (int i = 0; i < p->n; i++)
        p->v[i].drawn = p->v[i].cur;
}
