/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "actions.h"

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
