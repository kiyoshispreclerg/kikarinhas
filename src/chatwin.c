/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The classic chat window (see chatwin.h). */
#include "chatwin.h"

#include <malloc.h>
#include <stdlib.h>
#include <string.h>

#include "log.h"

/* What one wheel click scrolls, px. */
#define WHEEL_STEP 48

struct kk_chatwin {
    kk_window *parent;
    kk_chatview *view;
    kk_chatwin_handlers h;
    kk_chatwin_config cfg;
    kk_window win;
    bool open;
    bool full; /* exposed or resized: paint even if the view didn't change */
    /* The size the user gave it (resizable), kept until kikarinhas ends. */
    int user_w, user_h;
    /* Left button: where it went down, for telling a click from a drag. */
    bool pressed;
    int press_x, press_y;
};

kk_chatwin *kk_chatwin_new(kk_window *parent, kk_chatview *view,
                           const kk_chatwin_handlers *h)
{
    kk_chatwin *c = calloc(1, sizeof *c);
    if (!c)
        return NULL;
    c->parent = parent;
    c->view = view;
    if (h)
        c->h = *h;
    c->cfg = (kk_chatwin_config){.width = 400, .height = 600, .resizable = true};
    return c;
}

void kk_chatwin_free(kk_chatwin *c)
{
    if (!c)
        return;
    kk_chatwin_close(c);
    free(c);
}

void kk_chatwin_configure(kk_chatwin *c, const kk_chatwin_config *cfg)
{
    bool changed = cfg->width != c->cfg.width || cfg->height != c->cfg.height ||
                   cfg->resizable != c->cfg.resizable || cfg->above != c->cfg.above;
    c->cfg = *cfg;
    if (changed) {
        c->user_w = c->user_h = 0; /* the new size wins over a dragged one */
        if (c->open) {
            kk_chatwin_close(c);
            kk_chatwin_open(c);
        }
    }
}

bool kk_chatwin_open(kk_chatwin *c)
{
    if (c->open)
        return true;
    kk_window_opts o = {.title = "Kikarinhas — chat", .res_name = "kikarinhas-chat",
                        .fixed_size = !c->cfg.resizable, .above = c->cfg.above};
    bool keep = c->cfg.resizable && c->user_w > 0;
    int w = keep ? c->user_w : c->cfg.width, h = keep ? c->user_h : c->cfg.height;
    if (kk_window_open_child(&c->win, c->parent, &o, w, h) < 0) {
        kk_log_error("não consegui abrir a janela de chat");
        return false;
    }
    c->open = true;
    c->full = true;
    c->pressed = false;
    kk_chatview_resize(c->view, c->win.width, c->win.height);
    kk_log_info("janela de chat aberta (%dx%d)", c->win.width, c->win.height);
    return true;
}

void kk_chatwin_close(kk_chatwin *c)
{
    if (!c->open)
        return;
    kk_window_close(&c->win);
    c->open = false;
    /* Only the history stays; the rest goes back to the system. */
    kk_chatview_release(c->view);
    malloc_trim(0);
    kk_log_info("janela de chat fechada");
}

bool kk_chatwin_is_open(const kk_chatwin *c)
{
    return c->open;
}

static void pointer(kk_chatwin *c, const kk_pointer_event *pe)
{
    switch (pe->kind) {
    case KK_POINTER_PRESS:
        c->pressed = true;
        c->press_x = pe->x;
        c->press_y = pe->y;
        break;
    case KK_POINTER_MOVE:
        if (c->pressed && (abs(pe->x - c->press_x) > KK_POINTER_DRAG_SLOP ||
                           abs(pe->y - c->press_y) > KK_POINTER_DRAG_SLOP))
            c->pressed = false; /* a drag: not a click */
        break;
    case KK_POINTER_RELEASE:
        if (c->pressed && c->h.click) {
            uint64_t id = kk_chatview_hit(c->view, c->press_x, c->press_y);
            if (id)
                c->h.click(c->h.ud, id);
        }
        c->pressed = false;
        break;
    }
}

void kk_chatwin_events(kk_chatwin *c, double now)
{
    if (!c->open)
        return;
    kk_window_events ev;
    kk_window_take_events(&c->win, &ev);
    if (ev.quit) {
        kk_chatwin_close(c);
        return;
    }
    if (ev.resized) {
        kk_chatview_resize(c->view, c->win.width, c->win.height);
        if (c->cfg.resizable) {
            c->user_w = c->win.width;
            c->user_h = c->win.height;
        }
    }
    if (ev.redraw)
        c->full = true;
    if (ev.scroll)
        kk_chatview_scroll(c->view, -ev.scroll * WHEEL_STEP, now);
    for (int i = 0; i < ev.n_pointer; i++)
        pointer(c, &ev.pointer[i]);
    if (ev.open_config && c->h.menu)
        c->h.menu(c->h.ud, kk_chatview_hit(c->view, ev.menu_x, ev.menu_y), ev.menu_root_x,
                  ev.menu_root_y);
}

void kk_chatwin_paint(kk_chatwin *c, double now)
{
    if (!c->open || kk_window_busy(&c->win))
        return;
    if (!c->full && !kk_chatview_dirty(c->view, now))
        return;
    cairo_t *cr = cairo_create(c->win.surface);
    kk_chatview_draw(c->view, cr, now);
    cairo_destroy(cr);
    kk_rect all = {0, 0, c->win.width, c->win.height};
    kk_window_present(&c->win, &all, 1);
    c->full = false;
}
