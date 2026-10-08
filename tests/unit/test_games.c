/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "../harness.h"
#include "games.h"

static kk_rng rng;

TEST(roll_forms)
{
    char out[300];
    for (int i = 0; i < 2000; i++) {
        CHECK(kk_roll(&rng, "", out, sizeof out));
        int v = atoi(out);
        CHECK(v >= 1 && v <= 100);
        CHECK(strstr(out, "(1-100)"));
        CHECK(kk_roll(&rng, "6", out, sizeof out));
        v = atoi(out);
        CHECK(v >= 1 && v <= 6);
    }
    CHECK(kk_roll(&rng, "2d6", out, sizeof out));
    int a, b, sum;
    CHECK(sscanf(out, "%d + %d = %d (2d6)", &a, &b, &sum) == 3);
    CHECK(a >= 1 && a <= 6 && b >= 1 && b <= 6 && sum == a + b);
    CHECK(kk_roll(&rng, "d20", out, sizeof out));
    CHECK(strstr(out, "(d20)"));
    CHECK(kk_roll(&rng, "20d1000", out, sizeof out)); /* the most dice */

    static const char *const bad[] = {"0", "1", "abc", "2d1", "21d6", "3d", "-5", "2d6x",
                                      "1000001"};
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++)
        CHECK_MSG(!kk_roll(&rng, bad[i], out, sizeof out), "%s", bad[i]);
    /* A buffer too small is cut, never overrun. */
    char tiny[8];
    CHECK(kk_roll(&rng, "20d6", tiny, sizeof tiny));
    CHECK(strlen(tiny) < sizeof tiny);
}

TEST(eightball)
{
    for (int i = 0; i < 100; i++) {
        const char *s = kk_eightball(&rng);
        CHECK(s && s[0]);
    }
}

/* Stream Avatars' numbers: 46% of spins win, about 95% comes back. */
TEST(slots_odds)
{
    int wins = 0;
    double back = 0;
    const int spins = 200000;
    for (int i = 0; i < spins; i++) {
        kk_slots_spin s = kk_slots(&rng);
        bool three = s.reels[0] == s.reels[1] && s.reels[1] == s.reels[2];
        CHECK(three == (s.mult > 0)); /* three of a kind only when it pays */
        for (int k = 0; k < 3; k++)
            CHECK(s.reels[k] >= 0 && s.reels[k] < KK_SLOT_SYMBOLS);
        wins += s.mult > 0;
        back += s.mult;
    }
    double rate = (double)wins / spins, ev = back / spins;
    CHECK_MSG(fabs(rate - 0.46) < 0.01, "win rate %.3f", rate);
    CHECK_MSG(fabs(ev - 0.9545) < 0.02, "return %.3f", ev);
}

TEST(roulette_bets)
{
    kk_bet b;
    CHECK(kk_bet_parse("vermelho", &b) && b.kind == KK_BET_RED);
    CHECK(kk_bet_parse("BLACK", &b) && b.kind == KK_BET_BLACK);
    CHECK(kk_bet_parse("ímpar", &b) && b.kind == KK_BET_ODD);
    CHECK(kk_bet_parse("par", &b) && b.kind == KK_BET_EVEN);
    CHECK(kk_bet_parse("1-18", &b) && b.kind == KK_BET_LOW);
    CHECK(kk_bet_parse("alto", &b) && b.kind == KK_BET_HIGH);
    CHECK(kk_bet_parse("2nd12", &b) && b.kind == KK_BET_DOZEN && b.number == 2);
    CHECK(kk_bet_parse("3a12", &b) && b.kind == KK_BET_DOZEN && b.number == 3);
    CHECK(kk_bet_parse("verde", &b) && b.kind == KK_BET_NUMBER && b.number == 0);
    CHECK(kk_bet_parse("17", &b) && b.kind == KK_BET_NUMBER && b.number == 17);
    CHECK(!kk_bet_parse("37", &b));
    CHECK(!kk_bet_parse("-1", &b));
    CHECK(!kk_bet_parse("azul", &b));
    CHECK(!kk_bet_parse("", &b));

    int reds = 0;
    for (int n = 1; n <= 36; n++)
        reds += kk_roulette_red(n);
    CHECK_INT_EQ(reds, 18);
    CHECK(!kk_roulette_red(0));

    kk_bet red = {KK_BET_RED, 0}, seventeen = {KK_BET_NUMBER, 17}, zero = {KK_BET_NUMBER, 0},
           even = {KK_BET_EVEN, 0}, d3 = {KK_BET_DOZEN, 3}, low = {KK_BET_LOW, 0};
    CHECK_INT_EQ(kk_bet_payout(&red, 1), 2);
    CHECK_INT_EQ(kk_bet_payout(&red, 2), 0);
    CHECK_INT_EQ(kk_bet_payout(&seventeen, 17), 36);
    CHECK_INT_EQ(kk_bet_payout(&seventeen, 18), 0);
    CHECK_INT_EQ(kk_bet_payout(&zero, 0), 36);
    /* Zero loses every outside bet, even "even". */
    CHECK_INT_EQ(kk_bet_payout(&even, 0), 0);
    CHECK_INT_EQ(kk_bet_payout(&red, 0), 0);
    CHECK_INT_EQ(kk_bet_payout(&d3, 0), 0);
    CHECK_INT_EQ(kk_bet_payout(&low, 0), 0);
    CHECK_INT_EQ(kk_bet_payout(&even, 36), 2);
    CHECK_INT_EQ(kk_bet_payout(&d3, 25), 3);
    CHECK_INT_EQ(kk_bet_payout(&d3, 24), 0);
    CHECK_INT_EQ(kk_bet_payout(&low, 18), 2);

    /* Every bet loses a little in the long run (1/37 of it). */
    kk_bet kinds[] = {red, seventeen, even, d3, low};
    for (size_t k = 0; k < sizeof kinds / sizeof kinds[0]; k++) {
        int total = 0;
        for (int n = 0; n <= 36; n++)
            total += kk_bet_payout(&kinds[k], n);
        CHECK_INT_EQ(total, 36);
    }

    char name[32];
    kk_bet_name(&d3, name, sizeof name);
    CHECK_STR_EQ(name, "3ª dúzia");
    kk_bet_name(&zero, name, sizeof name);
    CHECK_STR_EQ(name, "verde (0)");
}

int main(void)
{
    kk_rng_seed(&rng, 12345);
    RUN(roll_forms);
    RUN(eightball);
    RUN(slots_odds);
    RUN(roulette_bets);
    return harness_report();
}
