/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "actions.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

static kk_actions *ctx(const kk_cmd_call *c)
{
    return c->ud;
}

/* A command's data, when it has one, is its argument: "!pika" can be an
 * "avatar" command with data "pikachu". Otherwise, what was typed. */
static const char *arg(const kk_cmd_call *c)
{
    return c->data ? c->data : c->args;
}

static bool is_any(const char *s, const char *const *words)
{
    for (; *words; words++)
        if (strcasecmp(s, *words) == 0)
            return true;
    return false;
}

static const char *const RANDOM[] = {"random", "aleatorio", "aleatório", NULL};
static const char *const NONE[] = {"none", "nada", "remover", "tirar", "off", NULL};

/* !avatar <name|random> */
static bool cmd_avatar(const kk_cmd_call *c)
{
    kk_actions *a = ctx(c);
    kk_stage *s = a->stage;
    const kk_sa_avatar *def = NULL;
    if (is_any(arg(c), RANDOM) && s->n_usable > 0)
        def = &s->lib->avatars[s->usable[kk_rng_int(&s->rng, s->n_usable)]];
    else if (arg(c)[0])
        def = kk_sa_find(s->lib, arg(c));
    return def && def != a->self->def && kk_stage_set_avatar(s, a->self, def);
}

/* !color <palette|none> */
static bool cmd_color(const kk_cmd_call *c)
{
    kk_actions *a = ctx(c);
    const kk_sa_avatar *def = a->self->def;
    int pal;
    if (is_any(arg(c), NONE))
        pal = -1;
    else if (is_any(arg(c), RANDOM) && def->n_palettes > 0)
        pal = kk_rng_int(&a->stage->rng, def->n_palettes);
    else if ((pal = kk_sa_find_palette(def, arg(c))) < 0)
        return false;
    return pal != a->self->palette && kk_stage_set_palette(a->stage, a->self, pal);
}

/* !gear <piece|none> */
static bool cmd_gear(const kk_cmd_call *c)
{
    kk_actions *a = ctx(c);
    if (is_any(arg(c), NONE)) {
        if (a->self->n_gear == 0)
            return false;
        kk_stage_unwear_all(a->stage, a->self);
        return true;
    }
    return arg(c)[0] && kk_stage_wear(a->stage, a->self, arg(c));
}

static bool cmd_jump(const kk_cmd_call *c)
{
    kk_avatar_jump(ctx(c)->self);
    return true;
}

static bool cmd_sit(const kk_cmd_call *c)
{
    return kk_avatar_sit(ctx(c)->self);
}

/* !dance: the avatar's "dance" animation, or any emote it has. */
static bool cmd_dance(const kk_cmd_call *c)
{
    kk_actions *a = ctx(c);
    return kk_avatar_emote(a->self, "dance", &a->stage->rng) ||
           kk_avatar_emote(a->self, NULL, &a->stage->rng);
}

/* !emote <name>: any custom animation by name; random without one. */
static bool cmd_emote(const kk_cmd_call *c)
{
    kk_actions *a = ctx(c);
    return kk_avatar_emote(a->self, arg(c)[0] ? arg(c) : NULL, &a->stage->rng);
}

/* !hug / !attack [@name]: without a name, someone at random. */
static bool interact(const kk_cmd_call *c, kk_action act)
{
    kk_actions *a = ctx(c);
    kk_avatar *b = arg(c)[0] ? kk_stage_find_by_name(a->stage, arg(c))
                              : kk_stage_random_other(a->stage, a->self);
    return kk_stage_interact(a->stage, a->self, b, act);
}

static bool cmd_hug(const kk_cmd_call *c)
{
    return interact(c, KK_ACT_HUG);
}

static bool cmd_attack(const kk_cmd_call *c)
{
    return interact(c, KK_ACT_ATTACK);
}

/* !sound <name> (or a command whose data names the sound). */
static bool cmd_sound(const kk_cmd_call *c)
{
    kk_actions *a = ctx(c);
    const char *name = arg(c);
    return name[0] && a->sound && a->sound(a->sound_ud, c->msg, name);
}

/* !help: a few usage lines picked at random among the commands the sender
 * may use (sounds included), in a bubble over their avatar. */
#define HELP_MAX 20

typedef struct {
    kk_rng *rng;
    const char *pick[HELP_MAX];
    int want, seen, have;
} help_pick;

/* Reservoir sampling: every command is equally likely, however many. */
static void help_each(const char *text, void *ud)
{
    help_pick *h = ud;
    if (h->have < h->want) {
        h->pick[h->have++] = text;
    } else {
        int j = kk_rng_int(h->rng, h->seen + 1);
        if (j < h->want)
            h->pick[j] = text;
    }
    h->seen++;
}

static bool cmd_help(const kk_cmd_call *c)
{
    kk_actions *a = ctx(c);
    if (!a->help_bubbles || !a->self || !c->commands)
        return false;
    help_pick h = {.rng = &a->stage->rng,
                   .want = a->help_count < 1 ? 1
                           : a->help_count > HELP_MAX ? HELP_MAX
                                                      : a->help_count};
    kk_commands_each_help(c->commands, c->role, help_each, &h);
    if (h.have == 0)
        return false;
    char text[600];
    int n = snprintf(text, sizeof text, "Comandos:");
    int title_len = n;
    for (int i = 0; i < h.have && n < (int)sizeof text; i++)
        n += snprintf(text + n, sizeof text - (size_t)n, "\n%s", h.pick[i]);
    kk_stage_help_bubble(a->stage, a->self, text, title_len, h.have + 1,
                         a->help_seconds > 0 ? a->help_seconds : 4.0 + 1.5 * h.have);
    return true;
}

/* Seconds a bubble with text stays: the command's own, else by length. */
static double text_seconds(const kk_cmd_call *c, const char *text)
{
    if (c->params->seconds > 0)
        return c->params->seconds;
    double t = 4.0 + 0.06 * (double)strlen(text);
    return t > 15.0 ? 15.0 : t;
}

/* Copies a text command's data into out: "\n" breaks the line and {name}
 * becomes the sender's name. */
static void expand_text(char *out, size_t size, const char *in, const char *name)
{
    size_t n = 0;
    while (*in && n + 1 < size) {
        if (in[0] == '\\' && in[1] == 'n') {
            out[n++] = '\n';
            in += 2;
        } else if (strncmp(in, "{name}", 6) == 0) {
            int w = snprintf(out + n, size - n, "%s", name ? name : "");
            if (w > 0) /* snprintf cut it short if it didn't fit */
                n += (size_t)w < size - n ? (size_t)w : size - n - 1;
            in += 6;
        } else {
            out[n++] = *in++;
        }
    }
    out[n] = '\0';
}

/* !discord, !regras...: data is the text, shown in a bubble over the
 * sender's avatar (even with message bubbles off, like !help). */
static bool cmd_text(const kk_cmd_call *c)
{
    kk_actions *a = ctx(c);
    if (!c->data || !c->data[0] || !a->self)
        return false;
    char text[600];
    expand_text(text, sizeof text, c->data, c->msg->name);
    int lines = 1;
    for (const char *p = text; *p; p++)
        lines += *p == '\n';
    kk_stage_help_bubble(a->stage, a->self, text, 0, lines + 6, text_seconds(c, text));
    return true;
}

/* !logo: data is a picture file, shown at the command's place and scale for
 * its seconds. */
static bool cmd_image(const kk_cmd_call *c)
{
    kk_actions *a = ctx(c);
    return c->data && c->data[0] && a->overlay &&
           kk_overlay_show(a->overlay, c->name, c->data, c->params);
}

/* !song: what the desktop is playing (MPRIS), in a bubble. data, when set,
 * limits it to players whose name has it ("spotify"), so a browser tab
 * never shows up by accident; what viewers type is ignored. */
static bool cmd_nowplaying(const kk_cmd_call *c)
{
    kk_actions *a = ctx(c);
    if (!a->mpris || !a->self)
        return false;
    char song[520], text[600];
    bool paused;
    if (!kk_mpris_current(a->mpris, c->data, song, sizeof song, &paused))
        snprintf(text, sizeof text, "Nenhuma música tocando agora.");
    else
        snprintf(text, sizeof text, "♪ %s%s", song, paused ? " (pausada)" : "");
    kk_stage_help_bubble(a->stage, a->self, text, 0, 4, text_seconds(c, text));
    return true;
}

/* ---- points -------------------------------------------------------------- */

static bool points_on(const kk_actions *a)
{
    return a->self && kk_points_enabled(a->points);
}

/* "!pontos": the sender's balance. */
static bool cmd_points(const kk_cmd_call *c)
{
    kk_actions *a = ctx(c);
    if (!points_on(a))
        return false;
    char text[200];
    snprintf(text, sizeof text, "%s: %ld %s", c->msg->name,
             kk_points_get(a->points, c->user_key), a->points_name);
    kk_stage_help_bubble(a->stage, a->self, text, 0, 3, text_seconds(c, text));
    return true;
}

static bool whole_number(const char *s, long *out)
{
    char *end;
    long v = strtol(s, &end, 10);
    if (end == s || *end)
        return false;
    *out = v;
    return true;
}

/* "@Fulana Silva 10" or "10 @Fulana": an amount and someone on screen
 * (other than the sender). */
static kk_avatar *amount_and_who(const kk_cmd_call *c, long *amount, bool negative_ok)
{
    kk_actions *a = ctx(c);
    char buf[256];
    snprintf(buf, sizeof buf, "%s", c->args);
    char *name = NULL;
    char *last = strrchr(buf, ' '), *first = strchr(buf, ' ');
    if (last && whole_number(last + 1, amount)) {
        *last = '\0';
        name = buf;
    } else if (first) {
        *first = '\0';
        if (whole_number(buf, amount))
            name = first + 1;
    }
    if (!name || *amount == 0 || (*amount < 0 && !negative_ok))
        return NULL;
    while (*name == ' ')
        name++;
    kk_avatar *b = name[0] ? kk_stage_find_by_name(a->stage, name) : NULL;
    return b && b != a->self && b->user_id ? b : NULL;
}

/* "!doar @nome 10": moves points from the sender to someone on screen. */
static bool cmd_give(const kk_cmd_call *c)
{
    kk_actions *a = ctx(c);
    long amount;
    kk_avatar *b = points_on(a) ? amount_and_who(c, &amount, false) : NULL;
    if (!b || !kk_points_spend(a->points, c->user_key, amount))
        return false;
    kk_points_add(a->points, b->user_id, amount);
    char text[300];
    snprintf(text, sizeof text, "%s → %s: %ld %s", c->msg->name, b->label, amount,
             a->points_name);
    kk_stage_help_bubble(a->stage, a->self, text, 0, 3, text_seconds(c, text));
    return true;
}

/* "!premiar @nome 50" (mods): makes points for someone; negative takes. */
static bool cmd_award(const kk_cmd_call *c)
{
    kk_actions *a = ctx(c);
    long amount;
    kk_avatar *b = points_on(a) ? amount_and_who(c, &amount, true) : NULL;
    if (!b)
        return false;
    long now = kk_points_add(a->points, b->user_id, amount);
    char text[300];
    snprintf(text, sizeof text, "%s: %+ld %s (%ld)", b->label, amount, a->points_name, now);
    kk_stage_help_bubble(a->stage, b, text, 0, 3, text_seconds(c, text));
    return true;
}

/* "!ranking": the five with the most points. */
static bool cmd_top(const kk_cmd_call *c)
{
    kk_actions *a = ctx(c);
    if (!points_on(a))
        return false;
    const char *keys[5];
    long values[5];
    int n = kk_points_top(a->points, keys, values, 5);
    if (n == 0)
        return false;
    char text[600];
    int len = snprintf(text, sizeof text, "Ranking");
    int title = len;
    for (int i = 0; i < n && len < (int)sizeof text; i++) {
        const char *name = a->stage->cfg.users
                               ? kk_users_get(a->stage->cfg.users, keys[i], KK_USER_NAME)
                               : NULL;
        if (!name) {
            const char *colon = strchr(keys[i], ':');
            name = colon ? colon + 1 : keys[i];
        }
        len += snprintf(text + len, sizeof text - (size_t)len, "\n%d. %s — %ld", i + 1, name,
                        values[i]);
    }
    kk_stage_help_bubble(a->stage, a->self, text, title, n + 1,
                         c->params->seconds > 0 ? c->params->seconds : 4.0 + 1.5 * n);
    return true;
}

/* ---- moving around ------------------------------------------------------- */

/* A short note over the avatar (it jumps, like any bubble of ours). */
static void note(kk_actions *a, kk_avatar *who, const char *text, double seconds)
{
    int lines = 1;
    for (const char *p = text; *p; p++)
        lines += *p == '\n';
    /* Room for long lines to wrap. */
    kk_stage_help_bubble(a->stage, who, text, 0, lines + 4, seconds);
}

/* A whole number in [lo, hi] at the start of s; fallback if s is empty. */
static bool number_arg(const char *s, long lo, long hi, long fallback, long *out)
{
    while (*s == ' ')
        s++;
    if (!*s) {
        *out = fallback;
        return true;
    }
    long v;
    char *end;
    v = strtol(s, &end, 10);
    if (end == s || (*end && *end != ' ') || v < lo || v > hi)
        return false;
    *out = v;
    return true;
}

/* !left / !right [N]: N percent of the window (default 15). */
static bool step(const kk_cmd_call *c, int dir)
{
    kk_actions *a = ctx(c);
    long pct;
    if (!a->self || !number_arg(arg(c), 1, 100, 15, &pct))
        return false;
    double x = a->self->x + dir * a->stage->width * pct / 100.0;
    if (!kk_avatar_walk_to(a->self, x, a->stage->width))
        return false;
    return true;
}

static bool cmd_left(const kk_cmd_call *c)
{
    return step(c, -1);
}

static bool cmd_right(const kk_cmd_call *c)
{
    return step(c, 1);
}

/* !move [N]: to N percent of the width (0 the left edge); somewhere at
 * random without N. */
static bool cmd_move(const kk_cmd_call *c)
{
    kk_actions *a = ctx(c);
    long pct;
    if (!a->self || !number_arg(arg(c), 0, 100, -1, &pct))
        return false;
    double x = pct < 0 ? kk_rng_range(&a->stage->rng, 0.0, a->stage->width)
                       : a->stage->width * pct / 100.0;
    return kk_avatar_walk_to(a->self, x, a->stage->width);
}

static bool cmd_stand(const kk_cmd_call *c)
{
    kk_actions *a = ctx(c);
    return a->self && kk_avatar_stand(a->self);
}

/* "@nome rest": someone named first (on screen) and what follows; without a
 * name, the sender. NULL if the name is nobody on screen. */
static kk_avatar *target_and_rest(const kk_cmd_call *c, const char **rest, char *buf,
                                  size_t size)
{
    kk_actions *a = ctx(c);
    const char *s = arg(c);
    *rest = s;
    if (s[0] != '@')
        return a->self;
    snprintf(buf, size, "%s", s);
    char *sp = strchr(buf, ' ');
    if (sp) {
        *sp = '\0';
        *rest = s + (sp - buf) + 1;
        while (**rest == ' ')
            (*rest)++;
    } else {
        *rest = "";
    }
    return kk_stage_find_by_name(a->stage, buf);
}

/* !freeze [@nome]: stays put (or lets go) until asked again. */
static bool cmd_freeze(const kk_cmd_call *c)
{
    kk_actions *a = ctx(c);
    char buf[128];
    const char *rest;
    kk_avatar *who = target_and_rest(c, &rest, buf, sizeof buf);
    if (!who || rest[0])
        return false;
    who->frozen = !who->frozen;
    note(a, who, who->frozen ? "❄️ congelado" : "☀️ descongelado", 3.0);
    return true;
}

/* "150", "150%", "1.5", "1,5": a size; 0 if none. */
static double parse_size(const char *s)
{
    char buf[32];
    snprintf(buf, sizeof buf, "%s", s);
    for (char *p = buf; *p; p++)
        if (*p == ',')
            *p = '.';
    char *end;
    double v = strtod(buf, &end);
    if (end == buf)
        return 0;
    bool pct = *end == '%';
    if (*end && !(pct && end[1] == '\0'))
        return 0;
    /* A plain number above 4 can only be a percentage. */
    if (pct || v > KK_AVATAR_MAX_SIZE)
        v /= 100.0;
    return v >= KK_AVATAR_MIN_SIZE && v <= KK_AVATAR_MAX_SIZE ? v : 0;
}

/* !scale [@nome] [N]: bigger or smaller; without N, back to normal. */
static bool cmd_scale(const kk_cmd_call *c)
{
    char buf[128];
    const char *rest;
    kk_avatar *who = target_and_rest(c, &rest, buf, sizeof buf);
    double size = rest[0] ? parse_size(rest) : 1.0;
    if (!who || size <= 0 || fabs(size - who->size) < 0.01)
        return false;
    kk_avatar_set_size(who, size);
    return true;
}

/* !sling [angle] [power]: angle 1-179 (90 straight up, small to the right,
 * big to the left), power 1-100 (100 reaches the top of the window). */
static bool cmd_sling(const kk_cmd_call *c)
{
    kk_actions *a = ctx(c);
    kk_avatar *me = a->self;
    if (!me || me->frozen || kk_avatar_flying(me))
        return false;
    kk_rng *rng = &a->stage->rng;
    long angle = -1, power = -1;
    const char *s = arg(c);
    char *end;
    if (*s) {
        angle = strtol(s, &end, 10);
        if (end == s || angle < 1 || angle > 179)
            return false;
        s = end;
        while (*s == ' ')
            s++;
        if (*s) {
            power = strtol(s, &end, 10);
            if (end == s || *end || power < 1 || power > 100)
                return false;
        }
    }
    if (angle < 0)
        angle = 30 + kk_rng_int(rng, 121);
    if (power < 0)
        power = 55 + kk_rng_int(rng, 46);
    kk_view v = kk_stage_view(a->stage);
    /* Height grows with the square of the speed: power is a share of it. */
    double top = v.ground_y * 0.95 - kk_avatar_height(me);
    double speed = sqrt(2.0 * 1400.0 * fmax(top, 50.0) * power / 100.0);
    double rad = angle * M_PI / 180.0;
    double spin = (angle < 90 ? 1 : -1) * kk_rng_range(rng, 4.0, 9.0);
    kk_avatar_launch(me, cos(rad) * speed, sin(rad) * speed, spin);
    return true;
}

/* ---- stage props --------------------------------------------------------- */

/* !bomb [@nome]: thrown at someone (by name, else at random), or anywhere
 * when nobody else is around. */
static bool cmd_bomb(const kk_cmd_call *c)
{
    kk_actions *a = ctx(c);
    if (!a->props || !a->self)
        return false;
    kk_avatar *at = arg(c)[0] ? kk_stage_find_by_name(a->stage, arg(c))
                              : kk_stage_random_other(a->stage, a->self);
    if (arg(c)[0] && !at)
        return false;
    double x = at ? at->x : kk_rng_range(&a->stage->rng, 0.0, a->stage->width);
    return kk_props_bomb(a->props, a->self, x);
}

/* !spawn [N]: N collectibles (default 5, at most 20). */
static bool cmd_spawn(const kk_cmd_call *c)
{
    kk_actions *a = ctx(c);
    long n;
    if (!a->props || !number_arg(arg(c), 1, 20, 5, &n))
        return false;
    return kk_props_spawn(a->props, (int)n, a->catch_points > 0 ? a->catch_points : 0) > 0;
}

long kk_actions_catch(void *ud, kk_avatar *who, long points)
{
    kk_actions *a = ud;
    if (points <= 0 || !who->user_id)
        return 0;
    /* The fake chat is not remembered: its catches show but pay nothing. */
    if (kk_points_enabled(a->points) && strncmp(who->user_id, "demo:", 5) != 0)
        kk_points_add(a->points, who->user_id, points);
    return points;
}

/* ---- chance -------------------------------------------------------------- */

/* !roll [N | XdY] */
static bool cmd_roll(const kk_cmd_call *c)
{
    kk_actions *a = ctx(c);
    char result[300], text[400];
    if (!a->self || !kk_roll(&a->stage->rng, arg(c), result, sizeof result))
        return false;
    snprintf(text, sizeof text, "🎲 %s: %s", c->msg->name, result);
    note(a, a->self, text, text_seconds(c, text));
    return true;
}

/* !pergunta TEXT: into the question queue, for the streamer to pick. */
static bool cmd_question(const kk_cmd_call *c)
{
    kk_actions *a = ctx(c);
    if (!a->questions || !kk_questions_add(a->questions, c->msg, arg(c), a->now))
        return false;
    if (a->self) {
        char text[120];
        snprintf(text, sizeof text, "❓ Pergunta na fila (%d)", kk_questions_count(a->questions));
        note(a, a->self, text, text_seconds(c, text));
    }
    return true;
}

/* !bola8 [pergunta] */
static bool cmd_eightball(const kk_cmd_call *c)
{
    kk_actions *a = ctx(c);
    if (!a->self)
        return false;
    char text[300];
    snprintf(text, sizeof text, "🎱 %s", kk_eightball(&a->stage->rng));
    note(a, a->self, text, text_seconds(c, text));
    return true;
}

/* A bet: "50", "tudo"/"all" (as much as allowed). False if not a number in
 * [min_bet, max_bet] the sender has. */
static bool bet_amount(kk_actions *a, const char *key, const char *word, long *out)
{
    long have = kk_points_get(a->points, key);
    long max = a->max_bet > 0 ? a->max_bet : have;
    long v;
    if (strcasecmp(word, "tudo") == 0 || strcasecmp(word, "all") == 0) {
        v = have < max ? have : max;
    } else {
        char *end;
        v = strtol(word, &end, 10);
        if (end == word || *end)
            return false;
    }
    if (v < (a->min_bet > 0 ? a->min_bet : 1) || v > max || v > have)
        return false;
    *out = v;
    return true;
}

static void bets_hint(kk_actions *a, const char *key)
{
    char text[200];
    snprintf(text, sizeof text, "Apostas de %ld a %ld %s (você tem %ld).",
             a->min_bet > 0 ? a->min_bet : 1, a->max_bet, a->points_name,
             kk_points_get(a->points, key));
    note(a, a->self, text, 5.0);
}

/* !slots N */
static bool cmd_slots(const kk_cmd_call *c)
{
    kk_actions *a = ctx(c);
    char word[32];
    long bet;
    if (!points_on(a))
        return false;
    snprintf(word, sizeof word, "%s", arg(c));
    if (!word[0] || !bet_amount(a, c->user_key, word, &bet)) {
        bets_hint(a, c->user_key);
        return false;
    }
    kk_points_spend(a->points, c->user_key, bet);
    kk_slots_spin s = kk_slots(&a->stage->rng);
    long back = (long)floor(bet * s.mult + 0.5);
    if (back > 0)
        kk_points_add(a->points, c->user_key, back);
    char text[300];
    int n = snprintf(text, sizeof text, "🎰 %s %s %s\n", kk_slot_symbols[s.reels[0]],
                     kk_slot_symbols[s.reels[1]], kk_slot_symbols[s.reels[2]]);
    long net = back - bet;
    if (back == 0)
        snprintf(text + n, sizeof text - (size_t)n, "Perdeu %ld %s.", bet, a->points_name);
    else if (net > 0)
        snprintf(text + n, sizeof text - (size_t)n, "Ganhou %ld %s!", net, a->points_name);
    else if (net == 0)
        snprintf(text + n, sizeof text - (size_t)n, "Empatou.");
    else
        snprintf(text + n, sizeof text - (size_t)n, "Voltaram %ld de %ld.", back, bet);
    note(a, a->self, text, 6.0);
    return true;
}

/* !bet N TIPO (or TIPO N): opens a round, or joins the open one. */
static bool cmd_bet(const kk_cmd_call *c)
{
    kk_actions *a = ctx(c);
    if (!points_on(a))
        return false;
    char buf[128], *w1 = buf, *w2;
    snprintf(buf, sizeof buf, "%s", arg(c));
    w2 = strchr(buf, ' ');
    kk_bet b;
    long amount;
    bool ok = false;
    if (w2) {
        *w2++ = '\0';
        while (*w2 == ' ')
            w2++;
        /* Either order: "50 vermelho" or "vermelho 50". */
        ok = (kk_bet_parse(w2, &b) && bet_amount(a, c->user_key, w1, &amount)) ||
             (kk_bet_parse(w1, &b) && bet_amount(a, c->user_key, w2, &amount));
    }
    kk_roulette_round *r = &a->roulette;
    if (!ok || r->n == KK_MAX_BETS) {
        char text[300];
        snprintf(text, sizeof text,
                 "!%s N TIPO: vermelho, preto, par, ímpar, baixo (1-18), alto (19-36), "
                 "1a12, 2a12, 3a12, verde ou um número de 0 a 36. De %ld a %ld %s.",
                 c->name, a->min_bet > 0 ? a->min_bet : 1, a->max_bet, a->points_name);
        note(a, a->self, text, 8.0);
        return false;
    }
    kk_points_spend(a->points, c->user_key, amount);
    bool opening = r->ends_at <= 0;
    if (opening) {
        r->n = 0;
        r->ends_at = a->now + (a->roulette_seconds > 0 ? a->roulette_seconds : 20);
    }
    kk_placed_bet *p = &r->bets[r->n++];
    snprintf(p->key, sizeof p->key, "%s", c->user_key);
    snprintf(p->name, sizeof p->name, "%s", c->msg->name);
    p->amount = amount;
    p->bet = b;
    char what[32], text[300];
    kk_bet_name(&b, what, sizeof what);
    if (opening)
        snprintf(text, sizeof text, "Roleta aberta: %.0f s para apostar\n!%s N TIPO\n%ld em %s",
                 r->ends_at - a->now, c->name, amount, what);
    else
        snprintf(text, sizeof text, "Roleta: %ld em %s", amount, what);
    if (a->self)
        note(a, a->self, text, opening ? 6.0 : 3.0);
    return true;
}

static void spin_roulette(kk_actions *a)
{
    kk_roulette_round *r = &a->roulette;
    int n = kk_rng_int(&a->stage->rng, 37);
    const char *color = n == 0 ? "verde" : kk_roulette_red(n) ? "vermelho" : "preto";
    /* One bubble per person, with everything they bet. */
    for (int i = 0; i < r->n; i++) {
        if (!r->bets[i].key[0])
            continue;
        long staked = 0, won = 0;
        for (int k = i; k < r->n; k++) {
            kk_placed_bet *p = &r->bets[k];
            if (strcmp(p->key, r->bets[i].key) != 0)
                continue;
            staked += p->amount;
            won += p->amount * kk_bet_payout(&p->bet, n);
            if (k > i)
                p->key[0] = '\0';
        }
        if (won > 0)
            kk_points_add(a->points, r->bets[i].key, won);
        kk_avatar *who = kk_stage_find_user(a->stage, r->bets[i].key);
        if (!who)
            continue;
        char text[300];
        long net = won - staked;
        if (won == 0)
            snprintf(text, sizeof text, "Roleta: deu %d (%s)\nPerdeu %ld %s.", n, color, staked,
                     a->points_name);
        else if (net > 0)
            snprintf(text, sizeof text, "Roleta: deu %d (%s)\nGanhou %ld %s!", n, color, net,
                     a->points_name);
        else
            snprintf(text, sizeof text, "Roleta: deu %d (%s)\nVoltaram %ld de %ld.", n, color,
                     won, staked);
        note(a, who, text, 7.0);
    }
    r->n = 0;
    r->ends_at = 0;
}

void kk_actions_tick(kk_actions *a, double now)
{
    a->now = now;
    if (a->roulette.ends_at > 0 && now >= a->roulette.ends_at)
        spin_roulette(a);
}

void kk_actions_refund(kk_actions *a)
{
    kk_roulette_round *r = &a->roulette;
    for (int i = 0; a->points && i < r->n; i++)
        kk_points_add(a->points, r->bets[i].key, r->bets[i].amount);
    r->n = 0;
    r->ends_at = 0;
}

/* "!pikachu", "!ash_hat", "!blue": an avatar, a piece or a palette name. */
static bool fallback(const kk_cmd_call *c)
{
    kk_actions *a = ctx(c);
    kk_stage *s = a->stage;
    const kk_sa_avatar *def = kk_sa_find(s->lib, c->args);
    if (def)
        return def != a->self->def && kk_stage_set_avatar(s, a->self, def);
    int set;
    if (kk_sa_find_piece(s->lib, a->self->def, c->args, &set))
        return kk_stage_wear(s, a->self, c->args);
    int pal = kk_sa_find_palette(a->self->def, c->args);
    return pal >= 0 && pal != a->self->palette &&
           kk_stage_set_palette(s, a->self, pal);
}

kk_cmd_fn kk_actions_find(const char *action)
{
    static const struct {
        const char *name;
        kk_cmd_fn fn;
    } table[] = {
        {"avatar", cmd_avatar}, {"color", cmd_color}, {"gear", cmd_gear},
        {"jump", cmd_jump},     {"sit", cmd_sit},     {"dance", cmd_dance},
        {"emote", cmd_emote},   {"hug", cmd_hug},     {"attack", cmd_attack},
        {"sound", cmd_sound},   {"help", cmd_help},   {"text", cmd_text},
        {"image", cmd_image},   {"nowplaying", cmd_nowplaying},
        {"points", cmd_points}, {"give", cmd_give},   {"award", cmd_award},
        {"top", cmd_top},
        {"left", cmd_left},     {"right", cmd_right}, {"move", cmd_move},
        {"stand", cmd_stand},   {"freeze", cmd_freeze}, {"scale", cmd_scale},
        {"sling", cmd_sling},   {"bomb", cmd_bomb},   {"spawn", cmd_spawn},
        {"roll", cmd_roll},     {"8ball", cmd_eightball}, {"slots", cmd_slots},
        {"bet", cmd_bet},       {"question", cmd_question},
    };
    for (size_t i = 0; i < sizeof table / sizeof table[0]; i++)
        if (strcmp(table[i].name, action) == 0)
            return table[i].fn;
    return NULL;
}

/* The line !help shows: "!hug [@nome]". A command with fixed data takes no
 * argument; a command with an unknown action is just "!name". */
/* "!jump (10 pontos)": the help line, with the price when there is one. */
static void help_line(char *out, size_t size, const char *base, long cost,
                      const kk_actions *a)
{
    if (cost > 0 && a && kk_points_enabled(a->points))
        snprintf(out, size, "%s (%ld %s)", base, cost, a->points_name);
    else
        snprintf(out, size, "%s", base);
}

static void set_help(kk_commands *c, const kk_config_command *k)
{
    static const struct {
        const char *action, *hint;
    } hints[] = {
        {"avatar", "NOME"}, {"color", "COR"},      {"gear", "PEÇA"},
        {"emote", "[NOME]"}, {"hug", "[@nome]"},   {"attack", "[@nome]"},
        {"sound", "NOME"},
        {"give", "@nome N"}, {"award", "@nome N"},
        {"left", "[N]"},      {"right", "[N]"},   {"move", "[N]"},
        {"freeze", "[@nome]"}, {"scale", "[@nome] [N]"},
        {"sling", "[ângulo] [força]"}, {"bomb", "[@nome]"}, {"spawn", "[N]"},
        {"roll", "[N]"},      {"8ball", "[pergunta]"}, {"slots", "N"},
        {"bet", "N TIPO"},    {"question", "TEXTO"},
    };
    char base[160], line[200];
    const char *hint = "";
    for (size_t i = 0; !k->data && i < sizeof hints / sizeof hints[0]; i++)
        if (strcmp(k->action, hints[i].action) == 0)
            hint = hints[i].hint;
    snprintf(base, sizeof base, "!%s%s%s", k->name, hint[0] ? " " : "", hint);
    help_line(line, sizeof line, base, k->cost, kk_commands_ud(c));
    kk_commands_set_help(c, k->name, line);
}

static long wallet_balance(void *ud, const char *user_key)
{
    return kk_points_get(ud, user_key);
}

static void wallet_charge(void *ud, const char *user_key, long cost)
{
    kk_points_spend(ud, user_key, cost);
}

kk_points_config kk_actions_points_config(const kk_config *cfg)
{
    return (kk_points_config){
        .enabled = cfg->points_enabled,
        .start = cfg->points_start,
        .message = cfg->points_message,
        .message_cooldown = cfg->points_message_cd,
        .watch = cfg->points_watch,
        .watch_minutes = cfg->points_watch_minutes,
        .arrival = cfg->points_arrival,
        .paid = cfg->points_paid,
        .member = cfg->points_member,
    };
}

int kk_actions_register(kk_commands *c, const kk_config *cfg,
                        kk_config_warn_fn warn, void *ud)
{
    char msg[256];
    int problems = 0;
    kk_actions *self = kk_commands_ud(c);
    if (self) {
        self->help_bubbles = cfg->help_bubbles;
        self->help_count = cfg->help_count;
        self->help_seconds = cfg->help_seconds;
        snprintf(self->points_name, sizeof self->points_name, "%s",
                 cfg->points_name ? cfg->points_name : "pontos");
        self->catch_points = cfg->points_catch;
        self->min_bet = cfg->points_min_bet;
        self->max_bet = cfg->points_max_bet;
        self->roulette_seconds = cfg->roulette_seconds;
        if (self->points) {
            kk_points_config pc = kk_actions_points_config(cfg);
            kk_points_configure(self->points, &pc);
            /* Points off: costs are ignored. */
            if (pc.enabled)
                kk_commands_set_wallet(c, wallet_balance, wallet_charge, self->points);
        }
    }
    /* With a command per sound, those are listed instead of "!sound". */
    bool sounds_listed = cfg->sound_enabled && cfg->sound_commands && cfg->n_sounds > 0;
    for (int i = 0; i < cfg->n_commands; i++) {
        const kk_config_command *k = &cfg->commands[i];
        kk_cmd_fn fn = k->action ? kk_actions_find(k->action) : NULL;
        if (!k->enabled || !fn)
            continue;
        if (kk_commands_add(c, k->name, fn, k->data, k->user_cd, k->global_cd,
                            k->role) < 0) {
            snprintf(msg, sizeof msg, "!%s já é apelido de outro comando", k->name);
            if (warn)
                warn(ud, 0, msg);
            problems++;
            continue;
        }
        kk_commands_set_params(c, k->name, &k->params);
        kk_commands_set_cost(c, k->name, NULL, k->cost);
        bool is_help = strcmp(k->action, "help") == 0;
        bool is_sound = strcmp(k->action, "sound") == 0 && !k->data;
        if (!is_help && !(is_sound && (sounds_listed || !cfg->sound_enabled ||
                                       cfg->n_sounds == 0)))
            set_help(c, k);
    }
    /* Aliases after every name, so an alias can't steal a later name. */
    for (int i = 0; i < cfg->n_commands; i++) {
        const kk_config_command *k = &cfg->commands[i];
        if (!k->enabled || !k->action || !kk_actions_find(k->action))
            continue;
        for (int a = 0; a < k->n_aliases; a++)
            if (kk_commands_alias(c, k->name, k->aliases[a]) < 0) {
                snprintf(msg, sizeof msg,
                         "apelido !%s de !%s ignorado: já é de outro comando",
                         k->aliases[a], k->name);
                if (warn)
                    warn(ud, 0, msg);
                problems++;
            }
    }

    /* "!buzina" for each sound, as if it were "!sound buzina". */
    const kk_config_command *base = NULL;
    for (int i = 0; i < cfg->n_commands; i++)
        if (strcmp(cfg->commands[i].name, "sound") == 0 && cfg->commands[i].enabled)
            base = &cfg->commands[i];
    /* A sound's own cost holds for "!sound NAME" as well as for "!NAME". */
    for (int i = 0; base && i < cfg->n_sounds; i++) {
        const kk_config_sound *s = &cfg->sounds[i];
        for (int w = -1; s->cost >= 0 && w < s->n_aliases; w++)
            kk_commands_set_cost(c, base->name, w < 0 ? s->name : s->aliases[w], s->cost);
    }
    for (int i = 0; base && cfg->sound_enabled && cfg->sound_commands &&
                    i < cfg->n_sounds; i++) {
        const kk_config_sound *s = &cfg->sounds[i];
        long cost = s->cost >= 0 ? s->cost : base->cost;
        for (int w = -1; w < s->n_aliases; w++) {
            const char *word = w < 0 ? s->name : s->aliases[w];
            if (kk_commands_has(c, word)) {
                snprintf(msg, sizeof msg,
                         "o som \"%s\" não vira !%s: já é outro comando",
                         s->name, word);
                if (warn)
                    warn(ud, 0, msg);
                problems++;
                continue;
            }
            kk_commands_add(c, word, cmd_sound, s->name, base->user_cd,
                            base->global_cd, base->role);
            kk_commands_set_group(c, word, "sound");
            kk_commands_set_cost(c, word, NULL, cost);
            if (w < 0) {
                char name[80], line[200];
                snprintf(name, sizeof name, "!%s", word);
                help_line(line, sizeof line, name, cost, self);
                kk_commands_set_help(c, word, line);
            }
        }
    }

    if (cfg->shortcuts) {
        kk_commands_set_fallback(c, fallback, cfg->shortcut_cd);
        kk_commands_set_fallback_cost(c, cfg->shortcut_cost);
    }
    return problems;
}
