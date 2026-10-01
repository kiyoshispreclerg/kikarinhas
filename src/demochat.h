/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef KK_DEMOCHAT_H
#define KK_DEMOCHAT_H

#include "chat.h"
#include "util.h"

/* Fake chat for trying things without a live: a dozen invented people
 * saying random lines every second or two, with the odd Super Chat. */
typedef struct {
    kk_chat_cb cb;
    void *ud;
    kk_rng rng;
    double next_at;
} kk_demochat;

void kk_demochat_init(kk_demochat *d, kk_chat_cb cb, void *ud, uint64_t seed);
void kk_demochat_tick(kk_demochat *d, double now);

#endif
