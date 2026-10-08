/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef KK_GAMES_H
#define KK_GAMES_H

/* Games of chance for the chat, as pure functions over a kk_rng (the
 * commands in actions.c show the results and move the points):
 *  - !roll: a number from 1 to 100, 1 to N, or XdY dice;
 *  - !bola8: the magic 8-ball's twenty answers;
 *  - !slots: Stream Avatars' slot machine: 46% of spins win, and a win is
 *    three cherries (1x the bet), oranges (0.5x), sevens (2x), coins (4x)
 *    or bags (10x), weighted 31/25/24/14/6, about 95% back on average;
 *  - !bet: roulette, 0 to 36: a number or green pays 36x, red/black,
 *    odd/even and low/high 2x, a dozen 3x. Bets gather for a round. */

#include <stdbool.h>
#include <stddef.h>

#include "util.h"

/* "" → 1..100; "20" → 1..20; "2d6" → two dice. The result goes to out
 * ("57 (1-100)", "3 + 5 = 8 (2d6)"). False if arg is none of those. */
bool kk_roll(kk_rng *rng, const char *arg, char *out, size_t size);

const char *kk_eightball(kk_rng *rng);

#define KK_SLOT_SYMBOLS 5
extern const char *const kk_slot_symbols[KK_SLOT_SYMBOLS];

typedef struct {
    int reels[3]; /* indexes into kk_slot_symbols */
    double mult;  /* what the bet comes back as; 0 if lost */
} kk_slots_spin;

kk_slots_spin kk_slots(kk_rng *rng);

/* ---- roulette ------------------------------------------------------------ */

typedef enum {
    KK_BET_NUMBER, /* number: 0..36 (0 is green) */
    KK_BET_RED,
    KK_BET_BLACK,
    KK_BET_ODD,
    KK_BET_EVEN,
    KK_BET_LOW,  /* 1-18 */
    KK_BET_HIGH, /* 19-36 */
    KK_BET_DOZEN, /* number: 1, 2 or 3 */
} kk_bet_kind;

typedef struct {
    kk_bet_kind kind;
    int number;
} kk_bet;

/* "red"/"vermelho", "17", "verde"/"green" (0), "par", "1st12"/"1a12"...
 * False if word is not a bet. */
bool kk_bet_parse(const char *word, kk_bet *out);
/* "vermelho", "17", "1ª dúzia". */
void kk_bet_name(const kk_bet *b, char *out, size_t size);
bool kk_roulette_red(int n);
/* What a bet of 1 returns when the ball stops at n (0 if lost). */
int kk_bet_payout(const kk_bet *b, int n);

#endif
