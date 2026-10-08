/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "games.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define MAX_DICE 20
#define MAX_SIDES 1000
#define MAX_RANGE 1000000

/* ---- roll ---------------------------------------------------------------- */

static bool number(const char *s, const char **end, long *out)
{
    if (!isdigit((unsigned char)*s))
        return false;
    char *e;
    *out = strtol(s, &e, 10);
    *end = e;
    return true;
}

bool kk_roll(kk_rng *rng, const char *arg, char *out, size_t size)
{
    while (*arg == ' ')
        arg++;
    if (!*arg) {
        snprintf(out, size, "%d (1-100)", 1 + kk_rng_int(rng, 100));
        return true;
    }
    const char *p;
    long a, b;
    if (*arg == 'd' || *arg == 'D') {
        /* "d20": one die. */
        a = 1;
        p = arg;
    } else if (!number(arg, &p, &a)) {
        return false;
    }
    if (*p == '\0') {
        if (a < 2 || a > MAX_RANGE)
            return false;
        snprintf(out, size, "%d (1-%ld)", 1 + kk_rng_int(rng, (int)a), a);
        return true;
    }
    if ((*p != 'd' && *p != 'D') || !number(p + 1, &p, &b) || *p || a < 1 || a > MAX_DICE ||
        b < 2 || b > MAX_SIDES)
        return false;
    size_t n = 0;
    long sum = 0;
    out[0] = '\0';
    for (long i = 0; i < a; i++) {
        int d = 1 + kk_rng_int(rng, (int)b);
        sum += d;
        int w = snprintf(out + n, n < size ? size - n : 0, "%s%d", i ? " + " : "", d);
        if (w > 0)
            n += (size_t)w;
    }
    if (n < size) {
        if (a > 1)
            snprintf(out + n, size - n, " = %ld (%ldd%ld)", sum, a, b);
        else
            snprintf(out + n, size - n, " (d%ld)", b);
    }
    return true;
}

/* ---- 8-ball -------------------------------------------------------------- */

const char *kk_eightball(kk_rng *rng)
{
    static const char *const answers[] = {
        /* yes */
        "É certo.", "Com certeza.", "Sem dúvida.", "Sim, definitivamente.",
        "Pode contar com isso.", "Do jeito que eu vejo, sim.", "Muito provavelmente.",
        "As perspectivas são boas.", "Sim.", "Os sinais apontam que sim.",
        /* maybe */
        "Resposta nebulosa, tente de novo.", "Pergunte mais tarde.",
        "Melhor não te dizer agora.", "Não dá para prever agora.",
        "Concentre-se e pergunte de novo.",
        /* no */
        "Não conte com isso.", "Minha resposta é não.", "Minhas fontes dizem que não.",
        "As perspectivas não são tão boas.", "Muito duvidoso.",
    };
    return answers[kk_rng_int(rng, (int)(sizeof answers / sizeof answers[0]))];
}

/* ---- slots --------------------------------------------------------------- */

const char *const kk_slot_symbols[KK_SLOT_SYMBOLS] = {"🍒", "🍊", "7️⃣", "🪙", "💰"};

/* Stream Avatars' table: chance of each symbol among the wins (out of 100)
 * and what it pays. */
static const int WEIGHT[KK_SLOT_SYMBOLS] = {31, 25, 24, 14, 6};
static const double MULT[KK_SLOT_SYMBOLS] = {1.0, 0.5, 2.0, 4.0, 10.0};
#define WIN_PERCENT 46

kk_slots_spin kk_slots(kk_rng *rng)
{
    kk_slots_spin s = {.mult = 0};
    if (kk_rng_int(rng, 100) < WIN_PERCENT) {
        int r = kk_rng_int(rng, 100), k = 0;
        while (k < KK_SLOT_SYMBOLS - 1 && r >= WEIGHT[k])
            r -= WEIGHT[k++];
        s.reels[0] = s.reels[1] = s.reels[2] = k;
        s.mult = MULT[k];
        return s;
    }
    /* A loss never shows three of a kind. */
    for (int i = 0; i < 3; i++)
        s.reels[i] = kk_rng_int(rng, KK_SLOT_SYMBOLS);
    if (s.reels[0] == s.reels[1] && s.reels[1] == s.reels[2])
        s.reels[2] = (s.reels[2] + 1 + kk_rng_int(rng, KK_SLOT_SYMBOLS - 1)) % KK_SLOT_SYMBOLS;
    return s;
}

/* ---- roulette ------------------------------------------------------------ */

bool kk_roulette_red(int n)
{
    static const int red[] = {1, 3, 5, 7, 9, 12, 14, 16, 18, 19, 21, 23, 25, 27, 30, 32, 34, 36};
    for (size_t i = 0; i < sizeof red / sizeof red[0]; i++)
        if (red[i] == n)
            return true;
    return false;
}

static bool any(const char *s, const char *const *words)
{
    for (; *words; words++)
        if (strcasecmp(s, *words) == 0)
            return true;
    return false;
}

bool kk_bet_parse(const char *word, kk_bet *out)
{
    static const char *const RED[] = {"red", "vermelho", "vermelha", "r", NULL};
    static const char *const BLACK[] = {"black", "preto", "preta", "b", NULL};
    static const char *const ODD[] = {"odd", "impar", "ímpar", NULL};
    static const char *const EVEN[] = {"even", "par", NULL};
    static const char *const LOW[] = {"low", "baixo", "1-18", NULL};
    static const char *const HIGH[] = {"high", "alto", "19-36", NULL};
    static const char *const GREEN[] = {"green", "verde", "zero", NULL};
    static const char *const D1[] = {"1st12", "1-12", "1a12", NULL};
    static const char *const D2[] = {"2nd12", "13-24", "2a12", NULL};
    static const char *const D3[] = {"3rd12", "25-36", "3a12", NULL};
    *out = (kk_bet){.kind = KK_BET_NUMBER};
    if (any(word, RED))
        out->kind = KK_BET_RED;
    else if (any(word, BLACK))
        out->kind = KK_BET_BLACK;
    else if (any(word, ODD))
        out->kind = KK_BET_ODD;
    else if (any(word, EVEN))
        out->kind = KK_BET_EVEN;
    else if (any(word, LOW))
        out->kind = KK_BET_LOW;
    else if (any(word, HIGH))
        out->kind = KK_BET_HIGH;
    else if (any(word, GREEN))
        out->number = 0;
    else if (any(word, D1) || any(word, D2) || any(word, D3))
        *out = (kk_bet){KK_BET_DOZEN, any(word, D1) ? 1 : any(word, D2) ? 2 : 3};
    else {
        char *end;
        long n = strtol(word, &end, 10);
        if (end == word || *end || n < 0 || n > 36)
            return false;
        out->number = (int)n;
    }
    return true;
}

void kk_bet_name(const kk_bet *b, char *out, size_t size)
{
    switch (b->kind) {
    case KK_BET_NUMBER:
        if (b->number == 0)
            snprintf(out, size, "verde (0)");
        else
            snprintf(out, size, "%d", b->number);
        break;
    case KK_BET_RED:
        snprintf(out, size, "vermelho");
        break;
    case KK_BET_BLACK:
        snprintf(out, size, "preto");
        break;
    case KK_BET_ODD:
        snprintf(out, size, "ímpar");
        break;
    case KK_BET_EVEN:
        snprintf(out, size, "par");
        break;
    case KK_BET_LOW:
        snprintf(out, size, "1-18");
        break;
    case KK_BET_HIGH:
        snprintf(out, size, "19-36");
        break;
    case KK_BET_DOZEN:
        snprintf(out, size, "%dª dúzia", b->number);
        break;
    }
}

int kk_bet_payout(const kk_bet *b, int n)
{
    bool z = n == 0; /* zero loses every outside bet */
    switch (b->kind) {
    case KK_BET_NUMBER:
        return n == b->number ? 36 : 0;
    case KK_BET_RED:
        return !z && kk_roulette_red(n) ? 2 : 0;
    case KK_BET_BLACK:
        return !z && !kk_roulette_red(n) ? 2 : 0;
    case KK_BET_ODD:
        return !z && n % 2 == 1 ? 2 : 0;
    case KK_BET_EVEN:
        return !z && n % 2 == 0 ? 2 : 0;
    case KK_BET_LOW:
        return n >= 1 && n <= 18 ? 2 : 0;
    case KK_BET_HIGH:
        return n >= 19 ? 2 : 0;
    case KK_BET_DOZEN:
        return !z && (n - 1) / 12 + 1 == b->number ? 3 : 0;
    }
    return 0;
}
