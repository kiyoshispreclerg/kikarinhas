/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "points.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* What only this session knows about someone: when they last earned for a
 * message, how long their avatar has been up since the last award, and
 * whether they already got the arrival bonus. */
typedef struct {
    uint64_t hash;
    double last_msg;
    double watched;
    bool arrived;
} session;

struct kk_points {
    kk_users *users;
    kk_points_config cfg;
    session *s;
    int n, cap;
    double last_watch; /* when kk_points_watch last ran; 0 never */
};

kk_points *kk_points_new(kk_users *users)
{
    kk_points *p = calloc(1, sizeof *p);
    if (p)
        p->users = users;
    return p;
}

void kk_points_free(kk_points *p)
{
    if (!p)
        return;
    free(p->s);
    free(p);
}

void kk_points_configure(kk_points *p, const kk_points_config *cfg)
{
    p->cfg = *cfg;
}

bool kk_points_enabled(const kk_points *p)
{
    return p && p->cfg.enabled && p->users;
}

static uint64_t hash_of(const char *s)
{
    uint64_t h = 1469598103934665603ULL;
    for (; *s; s++)
        h = (h ^ (unsigned char)*s) * 1099511628211ULL;
    return h;
}

static session *find(kk_points *p, const char *key)
{
    uint64_t h = hash_of(key);
    for (int i = 0; i < p->n; i++)
        if (p->s[i].hash == h)
            return &p->s[i];
    if (p->n == p->cap) {
        int cap = p->cap ? p->cap * 2 : 64;
        session *ns = realloc(p->s, (size_t)cap * sizeof *ns);
        if (!ns)
            return NULL;
        p->s = ns;
        p->cap = cap;
    }
    p->s[p->n] = (session){.hash = h, .last_msg = -1e9};
    return &p->s[p->n++];
}

long kk_points_get(const kk_points *p, const char *key)
{
    const char *v = p->users ? kk_users_get(p->users, key, KK_USER_POINTS) : NULL;
    return v ? strtol(v, NULL, 10) : p->cfg.start;
}

void kk_points_set(kk_points *p, const char *key, long value)
{
    if (!p->users)
        return;
    char s[32];
    snprintf(s, sizeof s, "%ld", value < 0 ? 0 : value);
    kk_users_set(p->users, key, KK_USER_POINTS, s);
}

long kk_points_add(kk_points *p, const char *key, long delta)
{
    long v = kk_points_get(p, key);
    /* No overflow from a silly config or a flood of awards. */
    if (delta > 0 && v > 1000000000L - delta)
        v = 1000000000L;
    else
        v += delta;
    if (v < 0)
        v = 0;
    kk_points_set(p, key, v);
    return v;
}

bool kk_points_spend(kk_points *p, const char *key, long cost)
{
    if (cost <= 0)
        return true;
    if (kk_points_get(p, key) < cost)
        return false;
    kk_points_add(p, key, -cost);
    return true;
}

long kk_points_message(kk_points *p, const char *key, const kk_chat_msg *m, double now)
{
    if (!kk_points_enabled(p))
        return 0;
    session *s = find(p, key);
    if (!s)
        return 0;
    long earned = 0;
    if (!s->arrived) {
        s->arrived = true;
        earned += p->cfg.arrival;
    }
    if (now - s->last_msg >= p->cfg.message_cooldown) {
        s->last_msg = now;
        earned += p->cfg.message;
    }
    if (m->kind == KK_MSG_PAID)
        earned += p->cfg.paid;
    else if (m->kind == KK_MSG_MEMBER)
        earned += p->cfg.member;
    if (earned)
        kk_points_add(p, key, earned);
    else if (!kk_users_get(p->users, key, KK_USER_POINTS))
        kk_points_set(p, key, p->cfg.start); /* the starting balance shows in the list */
    return earned;
}

void kk_points_watch(kk_points *p, const char *const *keys, int n, double now)
{
    double dt = p->last_watch > 0 ? now - p->last_watch : 0;
    p->last_watch = now;
    /* After a stall (suspend, a long block) don't hand out a lump. */
    if (dt > 5)
        dt = 5;
    double interval = p->cfg.watch_minutes * 60;
    if (!kk_points_enabled(p) || p->cfg.watch <= 0 || interval <= 0 || dt <= 0)
        return;
    for (int i = 0; i < n; i++) {
        session *s = find(p, keys[i]);
        if (!s)
            continue;
        s->watched += dt;
        if (s->watched >= interval) {
            s->watched -= interval;
            kk_points_add(p, keys[i], p->cfg.watch);
        }
    }
}

int kk_points_top(const kk_points *p, const char **keys, long *values, int n)
{
    int have = 0;
    for (int i = 0; p->users && i < kk_users_count(p->users); i++) {
        const char *key = kk_users_key(p->users, i);
        const char *v = kk_users_get(p->users, key, KK_USER_POINTS);
        if (!v || strncmp(key, "demo:", 5) == 0)
            continue;
        long pts = strtol(v, NULL, 10);
        int at = have < n ? have : n;
        while (at > 0 && values[at - 1] < pts)
            at--;
        if (at >= n)
            continue;
        int last = have < n ? have : n - 1;
        for (int k = last; k > at; k--) {
            keys[k] = keys[k - 1];
            values[k] = values[k - 1];
        }
        keys[at] = key;
        values[at] = pts;
        if (have < n)
            have++;
    }
    return have;
}
