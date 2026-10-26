/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef KK_CHATWIN_H
#define KK_CHATWIN_H

/* The classic chat window: a second window on the main window's X
 * connection that shows a kk_chatview. Opened and closed while running
 * (configurator button, control socket, --chat-window); closing it with
 * the window manager's X frees everything it drew and gives the memory
 * back to the system, keeping only the history.
 *
 * The mouse: the wheel scrolls, a left click on a message and a right
 * click anywhere are handed to the callbacks (highlight, menu), and so is
 * Esc (takes the highlight away). */

#include <stdbool.h>
#include <stdint.h>

#include "chatview.h"
#include "window.h"

typedef struct {
    int width, height;
    bool resizable;
    bool above; /* kept over the other windows */
} kk_chatwin_config;

typedef struct {
    /* Left click on message id. */
    void (*click)(void *ud, uint64_t id);
    /* Right click on message id (0: between messages), at x, y of the
     * screen. */
    void (*menu)(void *ud, uint64_t id, int x, int y);
    /* Esc pressed in the window. */
    void (*escape)(void *ud);
    void *ud;
} kk_chatwin_handlers;

typedef struct kk_chatwin kk_chatwin;

/* parent and view must outlive it. h may be NULL. */
kk_chatwin *kk_chatwin_new(kk_window *parent, kk_chatview *view,
                           const kk_chatwin_handlers *h);
void kk_chatwin_free(kk_chatwin *c);

/* Applies from the next opening; if open, it opens again when the size or
 * the window manager hints change. */
void kk_chatwin_configure(kk_chatwin *c, const kk_chatwin_config *cfg);

/* Opens it, or raises it if already open. */
bool kk_chatwin_open(kk_chatwin *c);
void kk_chatwin_close(kk_chatwin *c);
bool kk_chatwin_is_open(const kk_chatwin *c);

/* Handles the window's events (after the parent's kk_window_dispatch). */
void kk_chatwin_events(kk_chatwin *c, double now);
/* Paints it if something changed and the server is done with the last
 * frame. */
void kk_chatwin_paint(kk_chatwin *c, double now);

#endif
