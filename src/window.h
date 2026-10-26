/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef KK_WINDOW_H
#define KK_WINDOW_H

#include <stdbool.h>

#include <X11/Xlib.h>
#include <X11/extensions/XShm.h>
#include <cairo.h>

#include "pointer.h"
#include "util.h"

typedef enum {
    KK_MODE_OBS,     /* normal managed window, captured by OBS (Xcomposite) */
    KK_MODE_DESKTOP, /* fullscreen override-redirect overlay, click-through */
} kk_mode;

#define KK_WINDOW_MAX_POINTER 32
#define KK_WINDOW_MAX_CHILDREN 4

typedef struct {
    bool quit;    /* window closed by the user or the WM */
    bool redraw;  /* exposed or resized: everything must be repainted */
    bool resized; /* width/height changed (buffer already recreated) */
    bool open_config; /* right click on the window, at menu_x/menu_y */
    int menu_x, menu_y;           /* window coordinates of that click */
    int menu_root_x, menu_root_y; /* and on the screen */
    int scroll; /* wheel clicks: < 0 up, > 0 down */
    bool left;  /* the pointer left the window */
    /* Left button, OBS mode only, in order; moves in a row are merged into
     * the last one. Events past the end are dropped. */
    kk_pointer_event pointer[KK_WINDOW_MAX_POINTER];
    int n_pointer;
} kk_window_events;

/* How a second window looks to the window manager (kk_window_open_child). */
typedef struct {
    const char *title;     /* NULL: "Kikarinhas" */
    const char *res_name;  /* WM_CLASS; NULL: "kikarinhas" */
    bool fixed_size; /* the WM may not resize it */
    bool above;      /* kept over the other windows */
    /* Override-redirect (a menu): no WM, placed at x, y on the screen,
     * motion reported without a button held. */
    bool popup;
    int x, y;
} kk_window_opts;

typedef struct kk_window kk_window;

struct kk_window {
    Display *dpy;
    bool owns_dpy; /* false for a child, which shares its parent's */
    int screen;
    Window win;
    Visual *visual;
    Colormap cmap;
    GC gc;
    Atom wm_protocols;
    Atom wm_delete;
    kk_mode mode;
    int width, height;
    bool has_compositor;

    /* Back buffer: an XImage whose pixels cairo draws into directly. With
     * MIT-SHM the server reads them from shared memory; until the matching
     * ShmCompletion arrives the buffer must not be touched (shm_pending). */
    XImage *image;
    cairo_surface_t *surface;
    bool shm_available;
    bool use_shm;
    bool shm_pending;
    int shm_completion;
    XShmSegmentInfo shm;

    /* The other windows on this connection, and the events dispatched for
     * one while it waits for kk_window_take_events. */
    kk_window *parent;
    kk_window *children[KK_WINDOW_MAX_CHILDREN];
    int n_children;
    kk_window_events pending;
    bool popup;

    char *clipboard; /* what this window offers as CLIPBOARD, while it owns it */
};

int kk_window_open(kk_window *w, kk_mode mode, int width, int height);
/* Another window on parent's display connection (always pointer-enabled,
 * like the OBS mode). Its X events come in through the parent's
 * kk_window_dispatch and wait in it for kk_window_take_events. Close it
 * before the parent. */
int kk_window_open_child(kk_window *w, kk_window *parent, const kk_window_opts *o,
                         int width, int height);
void kk_window_close(kk_window *w);

/* A child's events since the last call, into ev (which is overwritten). */
void kk_window_take_events(kk_window *w, kk_window_events *ev);

/* Offers text (copied) as the clipboard's content (CLIPBOARD, the one of
 * Ctrl+V); the window hands it to whoever pastes until another program
 * takes the clipboard. */
void kk_window_set_clipboard(kk_window *w, const char *text);

/* Takes the pointer for a popup, so a click outside it reaches it too (at
 * coordinates outside the window). False if another program holds it. */
bool kk_window_grab_pointer(kk_window *w);

/* File descriptor of the X connection, for poll(). */
int kk_window_fd(const kk_window *w);

/* Handles every queued X event and flushes the output buffer. Call it before
 * each poll(): Xlib may already hold events that poll() would not see.
 * Events of the children are kept in them (kk_window_take_events). */
void kk_window_dispatch(kk_window *w, kk_window_events *ev);

/* True while the server still reads the back buffer of the last present. */
bool kk_window_busy(const kk_window *w);

/* Sends rectangles r[0..n) of the back buffer (clipped to the window). */
void kk_window_present(kk_window *w, const kk_rect *r, int n);

#endif
