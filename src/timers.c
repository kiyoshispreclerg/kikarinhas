/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "timers.h"

#include <stdlib.h>
#include <string.h>

typedef struct {
    char *name, *command;
    double minutes;
    int messages, viewers;
    double last;  /* when it last ran (or started counting) */
    int heard;    /* messages since */
} timer;

struct kk_timers {
    timer *t;
    int n;
};

kk_timers *kk_timers_new(void)
{
    return calloc(1, sizeof(kk_timers));
}

static void clear(kk_timers *t)
{
    for (int i = 0; i < t->n; i++) {
        free(t->t[i].name);
        free(t->t[i].command);
    }
    free(t->t);
    t->t = NULL;
    t->n = 0;
}

void kk_timers_free(kk_timers *t)
{
    if (!t)
        return;
    clear(t);
    free(t);
}

void kk_timers_configure(kk_timers *t, const kk_config *cfg, double now)
{
    timer *nt = calloc(cfg->n_timers > 0 ? (size_t)cfg->n_timers : 1, sizeof *nt);
    if (!nt)
        return;
    int n = 0;
    for (int i = 0; i < cfg->n_timers; i++) {
        const kk_config_timer *c = &cfg->timers[i];
        if (!c->enabled)
            continue;
        timer *x = &nt[n];
        *x = (timer){.name = strdup(c->name), .command = strdup(c->command),
                     .minutes = c->minutes, .messages = c->messages, .viewers = c->viewers,
                     .last = now};
        if (!x->name || !x->command) {
            free(x->name);
            free(x->command);
            continue;
        }
        for (int k = 0; k < t->n; k++)
            if (strcmp(t->t[k].name, x->name) == 0) {
                x->last = t->t[k].last;
                x->heard = t->t[k].heard;
            }
        n++;
    }
    clear(t);
    t->t = nt;
    t->n = n;
}

void kk_timers_message(kk_timers *t)
{
    for (int i = 0; i < t->n; i++)
        t->t[i].heard++;
}

int kk_timers_due(kk_timers *t, double now, int viewers, const char **commands,
                  const char **names, int max)
{
    int n = 0;
    for (int i = 0; i < t->n && n < max; i++) {
        timer *x = &t->t[i];
        if ((x->minutes > 0 && now - x->last < x->minutes * 60) ||
            (x->messages > 0 && x->heard < x->messages) ||
            (x->viewers > 0 && viewers < x->viewers))
            continue;
        x->last = now;
        x->heard = 0;
        commands[n] = x->command;
        names[n++] = x->name;
    }
    return n;
}
