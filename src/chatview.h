/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef KK_CHATVIEW_H
#define KK_CHATVIEW_H

/* The classic chat: every platform's messages in one list, newest at the
 * bottom (or the top), drawn into the optional chat window (chatwin.c),
 * which the streamer reads and OBS may capture instead of a browser-source
 * chat. No X here, only Cairo, so it can be tested.
 *
 * Two parts with different lifetimes:
 *  - the history: copies of the last N messages (text, names, emotes),
 *    kept from the start even while no window shows them, so opening the
 *    window shows the chat so far;
 *  - what is drawn: one surface per message at the current width, made on
 *    demand and dropped by kk_chatview_release when the window closes.
 *
 * Whatever shows here also goes to the stream, so nothing meant only for
 * the streamer is drawn: filters hide commands and bots, and a highlighted
 * message gets only a discreet mark. */

#include <stdbool.h>
#include <stdint.h>

#include <cairo.h>

#include "chat.h"
#include "emotes.h"
#include "util.h"

typedef enum {
    KK_CHATVIEW_BG_TRANSPARENT,
    KK_CHATVIEW_BG_COLOR,
    KK_CHATVIEW_BG_IMAGE,
} kk_chatview_bg;

typedef struct {
    /* The speech bubbles' look: each message in a white box, bubble_font. */
    bool bubbles;
    const char *font;   /* Pango family and style; NULL: "Sans". Copied */
    double size;        /* points */
    const char *bubble_font; /* the stage's bubbles; NULL: "Sans". Copied */
    double bubble_size;
    uint32_t text_color, name_color, outline, paid_color, member_color; /* 0xRRGGBB */
    bool badge_colors;  /* owner, mods and members in the name tags' colours */
    int outline_width;  /* px around the text; 0: none */
    kk_chatview_bg background;
    uint32_t bg_color;  /* 0xRRGGBBAA */
    const char *bg_image; /* file; copied, read on the next draw */
    bool bg_tile;       /* repeated; else stretched to the window */
    bool show_platform, show_time;
    bool hide_commands;
    const char *hide_users; /* "Nightbot, StreamElements"; copied */
    double fade;        /* seconds a message stays; 0: forever */
    bool newest_top;
    int history;        /* messages kept */
} kk_chatview_config;

typedef struct kk_chatview kk_chatview;

/* emotes (may be NULL: emotes stay as text) must outlive it. */
kk_chatview *kk_chatview_new(kk_emotes *emotes);
void kk_chatview_free(kk_chatview *v);

/* On start and on reload; everything is drawn again. */
void kk_chatview_configure(kk_chatview *v, const kk_chatview_config *cfg);
void kk_chatview_resize(kk_chatview *v, int width, int height);

/* Keeps a copy of m, arrived at now (monotonic seconds). command: it was a
 * chat command (hidden with hide_commands). Returns its id (> 0), 0 if not
 * kept. */
uint64_t kk_chatview_add(kk_chatview *v, const kk_chat_msg *m, bool command, double now);

/* Message id as a kk_chat_msg (strings valid until the next add or free).
 * False if it is gone from the history. */
bool kk_chatview_get(const kk_chatview *v, uint64_t id, kk_chat_msg *out);

/* Scrolls by px (> 0: back to older messages). Back at the newest, it
 * follows the chat again; scrolled back, it returns by itself after a
 * while without scrolling, so the stream doesn't stay stuck on old ones. */
void kk_chatview_scroll(kk_chatview *v, int px, double now);
bool kk_chatview_scrolled(const kk_chatview *v);

/* The message drawn at (x, y) in the last draw, or 0. */
uint64_t kk_chatview_hit(const kk_chatview *v, int x, int y);

/* The message with a mark (the highlighted one); 0: none. */
void kk_chatview_set_marked(kk_chatview *v, uint64_t id);
uint64_t kk_chatview_marked(const kk_chatview *v);
/* Takes a message off the list (it stays in the history, unseen). */
void kk_chatview_hide(kk_chatview *v, uint64_t id);
/* Hides key's ("platform:id") messages, past and future, until restart. */
void kk_chatview_ignore(kk_chatview *v, const char *user_key);

/* Whether a draw at now would change anything: a new message, a scroll, a
 * fade, an animated emote... */
bool kk_chatview_dirty(kk_chatview *v, double now);
/* Paints the whole view into cr (width x height of the last resize). */
void kk_chatview_draw(kk_chatview *v, cairo_t *cr, double now);

/* Frees what was drawn (surfaces, the background image); the history
 * stays. For when the window closes. */
void kk_chatview_release(kk_chatview *v);

/* For tests: messages in the history, and how many were drawn. */
int kk_chatview_count(const kk_chatview *v);
int kk_chatview_drawn(const kk_chatview *v);

#endif
