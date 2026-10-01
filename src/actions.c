/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "actions.h"

#include <stdlib.h>
#include <string.h>
#include <strings.h>

static kk_actions *ctx(const kk_cmd_call *c)
{
    return c->ud;
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
    if (is_any(c->args, RANDOM) && s->n_usable > 0)
        def = &s->lib->avatars[s->usable[kk_rng_int(&s->rng, s->n_usable)]];
    else if (c->args[0])
        def = kk_sa_find(s->lib, c->args);
    return def && def != a->self->def && kk_stage_set_avatar(s, a->self, def);
}

/* !color <palette|none> */
static bool cmd_color(const kk_cmd_call *c)
{
    kk_actions *a = ctx(c);
    const kk_sa_avatar *def = a->self->def;
    int pal;
    if (is_any(c->args, NONE))
        pal = -1;
    else if (is_any(c->args, RANDOM) && def->n_palettes > 0)
        pal = kk_rng_int(&a->stage->rng, def->n_palettes);
    else if ((pal = kk_sa_find_palette(def, c->args)) < 0)
        return false;
    return pal != a->self->palette && kk_stage_set_palette(a->stage, a->self, pal);
}

/* !gear <piece|none> */
static bool cmd_gear(const kk_cmd_call *c)
{
    kk_actions *a = ctx(c);
    if (is_any(c->args, NONE)) {
        if (a->self->n_gear == 0)
            return false;
        kk_stage_unwear_all(a->stage, a->self);
        return true;
    }
    return c->args[0] && kk_stage_wear(a->stage, a->self, c->args);
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
    return kk_avatar_emote(a->self, c->args[0] ? c->args : NULL, &a->stage->rng);
}

/* !hug / !attack [@name]: without a name, someone at random. */
static bool interact(const kk_cmd_call *c, kk_action act)
{
    kk_actions *a = ctx(c);
    kk_avatar *b = c->args[0] ? kk_stage_find_by_name(a->stage, c->args)
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
    const char *name = c->data ? c->data : c->args;
    if (!name[0] || !a->sound)
        return false;
    a->sound(a->sound_ud, c->msg, name);
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

void kk_actions_register(kk_commands *c)
{
    static const struct {
        const char *name;
        kk_cmd_fn fn;
        double user_cd, global_cd;
        const char *aliases[6];
    } defaults[] = {
        /* Cooldowns follow Stream Avatars' defaults where it has one. */
        {"avatar", cmd_avatar, 5, 0, {"personagem", "char"}},
        {"color", cmd_color, 5, 0, {"cor", "colour", "paleta", "palette"}},
        {"gear", cmd_gear, 5, 0, {"item", "acessorio", "acessório", "equip"}},
        {"jump", cmd_jump, 3, 0, {"pula", "pular"}},
        {"sit", cmd_sit, 10, 0, {"senta", "sentar"}},
        {"dance", cmd_dance, 60, 0, {"danca", "dança", "dancar", "dançar"}},
        {"emote", cmd_emote, 15, 0, {"anim"}},
        {"hug", cmd_hug, 60, 0, {"abraco", "abraço", "abracar", "abraçar"}},
        {"attack", cmd_attack, 120, 0, {"ataque", "atacar", "bater"}},
        {"sound", cmd_sound, 30, 3, {"som", "play", "sfx", "mesa"}},
    };
    for (size_t i = 0; i < sizeof defaults / sizeof defaults[0]; i++) {
        kk_commands_add(c, defaults[i].name, defaults[i].fn, NULL,
                        defaults[i].user_cd, defaults[i].global_cd,
                        KK_ROLE_ANYONE);
        for (int k = 0; k < 6 && defaults[i].aliases[k]; k++)
            kk_commands_alias(c, defaults[i].name, defaults[i].aliases[k]);
    }
    kk_commands_set_fallback(c, fallback, 5);
}
