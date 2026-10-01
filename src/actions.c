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
        {"sound", cmd_sound},
    };
    for (size_t i = 0; i < sizeof table / sizeof table[0]; i++)
        if (strcmp(table[i].name, action) == 0)
            return table[i].fn;
    return NULL;
}

int kk_actions_register(kk_commands *c, const kk_config *cfg,
                        kk_config_warn_fn warn, void *ud)
{
    char msg[256];
    int problems = 0;
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
        }
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
    for (int i = 0; base && cfg->sound_enabled && cfg->sound_commands &&
                    i < cfg->n_sounds; i++) {
        const kk_config_sound *s = &cfg->sounds[i];
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
        }
    }

    if (cfg->shortcuts)
        kk_commands_set_fallback(c, fallback, cfg->shortcut_cd);
    return problems;
}
