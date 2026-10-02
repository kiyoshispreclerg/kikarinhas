/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "demochat.h"

#include <stdio.h>

static const struct {
    const char *name;
    unsigned badges;
} people[] = {
    {"Kiyoshi", KK_BADGE_OWNER},
    {"Moderadora Bia", KK_BADGE_MOD},
    {"joaozinho_gamer", 0},
    {"Ana Clara", KK_BADGE_MEMBER},
    {"Tio Patinhas", 0},
    {"capivara_feliz", 0},
    {"Lucas", KK_BADGE_MEMBER},
    {"pixelzinha", 0},
    {"O Fulano de Tal com Nome Comprido", 0},
    {"Maré", 0},
    {"bot_do_bem", KK_BADGE_MOD},
    {"Zé", 0},
};

static const char *const lines[] = {
    "oi!",
    "boa noite chat 👋",
    "KKKKKKKKKKKK",
    "que live boa",
    "alguém sabe que música é essa?",
    "primeira vez aqui, curti demais o canal 💜",
    "gg",
    "isso foi muito rápido, nem vi o que aconteceu, volta aí pra gente ver de novo por favor",
    "😂😂😂",
    "salve salve",
    "tô só olhando",
    "!jump",
    "vai dar bom",
    "!dance",
    "!hug",
    "!attack",
    "!sit",
    "!avatar random",
    "!cor random",
    "!som buzina",
    "🔥🔥🔥🔥",
    "GG 🎉🎉🎉",
    "😂😂😂😂😂",
    "amei ❤️❤️❤️",
    "👏👏👏",
    "🇧🇷🇧🇷🇧🇷",
};

static const char *const reactions[] = {"❤", "❤", "❤", "🎉", "💯", "😄", "😳"};

void kk_demochat_init(kk_demochat *d, const kk_chat_sink *sink, uint64_t seed)
{
    *d = (kk_demochat){.sink = *sink};
    kk_rng_seed(&d->rng, seed ^ 0xdeadbeefULL);
}

static void react(kk_demochat *d, double now)
{
    if (d->next_reaction == 0.0)
        d->next_reaction = now + 1.0;
    if (now < d->next_reaction || !d->sink.on_reaction)
        return;
    d->next_reaction = now + 1.0;
    int n = kk_rng_int(&d->rng, 5);
    if (n == 0)
        return;
    const char *e = reactions[kk_rng_int(&d->rng, (int)(sizeof reactions / sizeof reactions[0]))];
    kk_reaction r = {.platform = "demo", .emote = {.id = e, .text = e}, .count = n};
    d->sink.on_reaction(d->sink.ud, &r);
}

void kk_demochat_tick(kk_demochat *d, double now)
{
    react(d, now);
    if (d->next_at == 0.0)
        d->next_at = now + 0.5;
    if (now < d->next_at)
        return;
    d->next_at = now + kk_rng_range(&d->rng, 0.6, 2.2);

    int who = kk_rng_int(&d->rng, (int)(sizeof people / sizeof people[0]));
    char id[16];
    snprintf(id, sizeof id, "demo%d", who);
    kk_chat_msg m = {
        .platform = "demo",
        .user_id = id,
        .name = people[who].name,
        .text = lines[kk_rng_int(&d->rng, (int)(sizeof lines / sizeof lines[0]))],
        .badges = people[who].badges,
        .kind = KK_MSG_TEXT,
    };
    double r = kk_rng_range(&d->rng, 0.0, 1.0);
    if (r < 0.05) {
        m.kind = KK_MSG_PAID;
        m.amount = "R$ 10,00";
        m.text = "toma um cafezinho ☕";
    } else if (r < 0.08) {
        m.kind = KK_MSG_MEMBER;
        m.text = "Boas-vindas ao clube!";
    }
    d->sink.on_msg(d->sink.ud, &m);
}
