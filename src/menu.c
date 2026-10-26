/* SPDX-License-Identifier: GPL-3.0-or-later */
/* A small popup menu (see menu.h). */
#include "menu.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include <pango/pangocairo.h>

#define FONT "Sans 10"
#define PAD_X 16  /* left and right of the labels */
#define PAD_Y 5   /* above and below each label */
#define BORDER 4  /* between the frame and the first and last items */

struct kk_menu {
    kk_window win;
    kk_menu_item items[KK_MENU_MAX_ITEMS];
    int n;
    kk_menu_fn fn;
    void *ud;
    int item_h;
    int hover; /* -1: none */
    bool dirty;
};

static void layouts(PangoContext *ctx, const kk_menu_item *items, int n, int *w, int *h)
{
    PangoFontDescription *font = pango_font_description_from_string(FONT);
    PangoLayout *layout = pango_layout_new(ctx);
    pango_layout_set_font_description(layout, font);
    *w = *h = 0;
    for (int i = 0; i < n; i++) {
        PangoRectangle logical;
        pango_layout_set_text(layout, items[i].label, -1);
        pango_layout_get_pixel_extents(layout, NULL, &logical);
        *w = logical.width > *w ? logical.width : *w;
        *h = logical.height > *h ? logical.height : *h;
    }
    g_object_unref(layout);
    pango_font_description_free(font);
}

kk_menu *kk_menu_open(kk_window *parent, int x, int y, const kk_menu_item *items, int n,
                      kk_menu_fn fn, void *ud)
{
    if (n < 1)
        return NULL;
    kk_menu *m = calloc(1, sizeof *m);
    if (!m)
        return NULL;
    m->n = n > KK_MENU_MAX_ITEMS ? KK_MENU_MAX_ITEMS : n;
    for (int i = 0; i < m->n; i++) {
        m->items[i] = items[i];
        m->items[i].label = strdup(items[i].label ? items[i].label : "");
    }
    m->fn = fn;
    m->ud = ud;
    m->hover = -1;
    m->dirty = true;

    PangoContext *ctx = pango_font_map_create_context(pango_cairo_font_map_get_default());
    int tw, th;
    layouts(ctx, m->items, m->n, &tw, &th);
    g_object_unref(ctx);
    m->item_h = th + 2 * PAD_Y;
    int w = tw + 2 * PAD_X, h = m->n * m->item_h + 2 * BORDER;

    /* Inside the screen, like any menu. */
    int sw = DisplayWidth(parent->dpy, parent->screen);
    int sh = DisplayHeight(parent->dpy, parent->screen);
    x = x + w > sw ? sw - w : x;
    y = y + h > sh ? y - h : y;
    kk_window_opts o = {.popup = true, .x = x < 0 ? 0 : x, .y = y < 0 ? 0 : y,
                        .title = "Kikarinhas — menu", .res_name = "kikarinhas-menu"};
    if (kk_window_open_child(&m->win, parent, &o, w, h) < 0) {
        kk_menu_close(m);
        return NULL;
    }
    kk_window_grab_pointer(&m->win);
    return m;
}

void kk_menu_close(kk_menu *m)
{
    if (!m)
        return;
    if (m->win.dpy) {
        XUngrabPointer(m->win.dpy, CurrentTime);
        kk_window_close(&m->win);
    }
    for (int i = 0; i < m->n; i++)
        free((char *)m->items[i].label);
    free(m);
}

static int item_at(const kk_menu *m, int x, int y)
{
    if (x < 0 || x >= m->win.width || y < BORDER)
        return -1;
    int i = (y - BORDER) / m->item_h;
    return i < m->n ? i : -1;
}

static void rounded_rect(cairo_t *cr, double x, double y, double w, double h, double r)
{
    cairo_new_path(cr);
    cairo_arc(cr, x + w - r, y + r, r, -M_PI / 2, 0);
    cairo_arc(cr, x + w - r, y + h - r, r, 0, M_PI / 2);
    cairo_arc(cr, x + r, y + h - r, r, M_PI / 2, M_PI);
    cairo_arc(cr, x + r, y + r, r, M_PI, 3 * M_PI / 2);
    cairo_close_path(cr);
}

static void paint(kk_menu *m)
{
    cairo_t *cr = cairo_create(m->win.surface);
    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_set_source_rgba(cr, 0, 0, 0, 0);
    cairo_paint(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
    double w = m->win.width, h = m->win.height;
    rounded_rect(cr, 0.5, 0.5, w - 1, h - 1, 6);
    cairo_set_source_rgba(cr, 0.13, 0.11, 0.16, 0.97);
    cairo_fill_preserve(cr);
    cairo_set_source_rgba(cr, 1, 1, 1, 0.18);
    cairo_set_line_width(cr, 1);
    cairo_stroke(cr);

    PangoFontDescription *font = pango_font_description_from_string(FONT);
    PangoLayout *layout = pango_cairo_create_layout(cr);
    pango_layout_set_font_description(layout, font);
    for (int i = 0; i < m->n; i++) {
        double y = BORDER + i * m->item_h;
        bool on = m->items[i].enabled;
        if (i == m->hover && on) {
            rounded_rect(cr, 3, y, w - 6, m->item_h, 4);
            cairo_set_source_rgba(cr, 0.55, 0.42, 0.85, 0.85);
            cairo_fill(cr);
        }
        pango_layout_set_text(layout, m->items[i].label, -1);
        PangoRectangle logical;
        pango_layout_get_pixel_extents(layout, NULL, &logical);
        cairo_move_to(cr, PAD_X - logical.x, y + PAD_Y - logical.y);
        cairo_set_source_rgba(cr, 1, 1, 1, on ? 0.95 : 0.4);
        pango_cairo_show_layout(cr, layout);
    }
    g_object_unref(layout);
    pango_font_description_free(font);
    cairo_destroy(cr);
    kk_rect all = {0, 0, m->win.width, m->win.height};
    kk_window_present(&m->win, &all, 1);
    m->dirty = false;
}

static void set_hover(kk_menu *m, int i)
{
    if (i != m->hover)
        m->dirty = true;
    m->hover = i;
}

bool kk_menu_events(kk_menu *m)
{
    kk_window_events ev;
    kk_window_take_events(&m->win, &ev);
    if (ev.quit)
        return false;
    if (ev.redraw)
        m->dirty = true;
    if (ev.open_config && item_at(m, ev.menu_x, ev.menu_y) < 0)
        return false; /* a right click elsewhere */
    for (int k = 0; k < ev.n_pointer; k++) {
        const kk_pointer_event *pe = &ev.pointer[k];
        int i = item_at(m, pe->x, pe->y);
        switch (pe->kind) {
        case KK_POINTER_MOVE:
            set_hover(m, i);
            break;
        case KK_POINTER_PRESS:
            if (i < 0)
                return false; /* clicked outside */
            set_hover(m, i);
            break;
        case KK_POINTER_RELEASE:
            if (i >= 0 && m->items[i].enabled) {
                m->fn(m->ud, m->items[i].action);
                return false;
            }
            break;
        }
    }
    if (ev.left)
        set_hover(m, -1);
    if (m->dirty && !kk_window_busy(&m->win))
        paint(m);
    return true;
}
