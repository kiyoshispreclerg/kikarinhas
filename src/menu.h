/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef KK_MENU_H
#define KK_MENU_H

/* A small popup menu: an override-redirect window of its own, so a window
 * OBS captures (the chat window) can offer choices only the streamer sees.
 * It takes the pointer while open; a click outside closes it. Drawn with
 * Cairo, one line per item. */

#include <stdbool.h>

#include "window.h"

#define KK_MENU_MAX_ITEMS 12

typedef struct {
    const char *label; /* copied */
    int action;        /* handed to the callback */
    bool enabled;      /* greyed out otherwise */
} kk_menu_item;

typedef void (*kk_menu_fn)(void *ud, int action);

typedef struct kk_menu kk_menu;

/* Opens at x, y of the screen (moved in if it wouldn't fit) on parent's
 * connection. NULL if it can't. */
kk_menu *kk_menu_open(kk_window *parent, int x, int y, const kk_menu_item *items, int n,
                      kk_menu_fn fn, void *ud);
void kk_menu_close(kk_menu *m);

/* Handles its events (after the parent's kk_window_dispatch) and paints
 * what changed. False once it closed (picked or dismissed): free it then
 * with kk_menu_close. The callback runs from here. */
bool kk_menu_events(kk_menu *m);

#endif
