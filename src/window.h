/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef KK_WINDOW_H
#define KK_WINDOW_H

#include <stdbool.h>

#include <X11/Xlib.h>
#include <X11/extensions/XShm.h>
#include <cairo.h>

#include "util.h"

typedef enum {
    KK_MODE_OBS,     /* normal managed window, captured by OBS (Xcomposite) */
    KK_MODE_DESKTOP, /* fullscreen override-redirect overlay, click-through */
} kk_mode;

typedef struct {
    Display *dpy;
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
} kk_window;

typedef struct {
    bool quit;    /* window closed by the user or the WM */
    bool redraw;  /* exposed or resized: everything must be repainted */
    bool resized; /* width/height changed (buffer already recreated) */
    bool open_config; /* right click on the window */
} kk_window_events;

int kk_window_open(kk_window *w, kk_mode mode, int width, int height);
void kk_window_close(kk_window *w);

/* File descriptor of the X connection, for poll(). */
int kk_window_fd(const kk_window *w);

/* Handles every queued X event and flushes the output buffer. Call it before
 * each poll(): Xlib may already hold events that poll() would not see. */
void kk_window_dispatch(kk_window *w, kk_window_events *ev);

/* True while the server still reads the back buffer of the last present. */
bool kk_window_busy(const kk_window *w);

/* Sends rectangles r[0..n) of the back buffer (clipped to the window). */
void kk_window_present(kk_window *w, const kk_rect *r, int n);

#endif
