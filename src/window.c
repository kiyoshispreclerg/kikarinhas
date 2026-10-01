/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "window.h"

#include "log.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <unistd.h>

#include <X11/Xatom.h>
#include <X11/Xutil.h>
#include <X11/extensions/shape.h>

/* ---- X error trap (used around XShmAttach) ------------------------------ */

static bool x_error_seen;

static int trap_x_error(Display *dpy, XErrorEvent *e)
{
    (void)dpy;
    (void)e;
    x_error_seen = true;
    return 0;
}

/* ---- back buffer --------------------------------------------------------- */

static int host_byte_order(void)
{
    const uint16_t one = 1;
    return *(const uint8_t *)&one ? LSBFirst : MSBFirst;
}

static bool shm_buffer_create(kk_window *w, int stride)
{
    w->image = XShmCreateImage(w->dpy, w->visual, 32, ZPixmap, NULL, &w->shm,
                               (unsigned)w->width, (unsigned)w->height);
    if (!w->image)
        return false;
    if (w->image->bytes_per_line != stride)
        goto fail_image;

    w->shm.shmid = shmget(IPC_PRIVATE, (size_t)stride * (size_t)w->height,
                          IPC_CREAT | 0600);
    if (w->shm.shmid < 0)
        goto fail_image;
    w->shm.shmaddr = shmat(w->shm.shmid, NULL, 0);
    if (w->shm.shmaddr == (char *)-1)
        goto fail_shmid;
    w->shm.readOnly = False;
    w->image->data = w->shm.shmaddr;

    x_error_seen = false;
    XErrorHandler old = XSetErrorHandler(trap_x_error);
    XShmAttach(w->dpy, &w->shm);
    XSync(w->dpy, False);
    XSetErrorHandler(old);
    if (x_error_seen) /* e.g. remote display: the server can't see our shm */
        goto fail_shmat;

    /* Marked for removal now so it is freed even if we crash. */
    shmctl(w->shm.shmid, IPC_RMID, NULL);
    return true;

fail_shmat:
    shmdt(w->shm.shmaddr);
fail_shmid:
    shmctl(w->shm.shmid, IPC_RMID, NULL);
fail_image:
    w->image->data = NULL;
    XDestroyImage(w->image);
    w->image = NULL;
    return false;
}

static bool buffer_create(kk_window *w)
{
    int stride = cairo_format_stride_for_width(CAIRO_FORMAT_ARGB32, w->width);

    w->use_shm = false;
    if (w->shm_available) {
        w->use_shm = shm_buffer_create(w, stride);
        if (!w->use_shm) {
            kk_log_warn("MIT-SHM indisponível, usando XPutImage (mais lento)");
            w->shm_available = false;
        }
    }
    if (!w->use_shm) {
        char *data = calloc((size_t)stride, (size_t)w->height);
        if (!data)
            return false;
        w->image = XCreateImage(w->dpy, w->visual, 32, ZPixmap, 0, data,
                                (unsigned)w->width, (unsigned)w->height, 32,
                                stride);
        if (!w->image) {
            free(data);
            return false;
        }
        /* cairo writes native-endian pixels; Xlib swaps if the server
         * differs. */
        w->image->byte_order = host_byte_order();
    }

    w->surface = cairo_image_surface_create_for_data(
        (unsigned char *)w->image->data, CAIRO_FORMAT_ARGB32, w->width,
        w->height, stride);
    if (cairo_surface_status(w->surface) != CAIRO_STATUS_SUCCESS) {
        kk_log_error("cairo: %s",
                     cairo_status_to_string(cairo_surface_status(w->surface)));
        return false;
    }
    return true;
}

static void buffer_destroy(kk_window *w)
{
    if (w->surface) {
        cairo_surface_destroy(w->surface);
        w->surface = NULL;
    }
    if (!w->image)
        return;
    if (w->use_shm) {
        XShmDetach(w->dpy, &w->shm);
        XSync(w->dpy, False); /* the server is done with the segment */
        shmdt(w->shm.shmaddr);
        w->image->data = NULL;
        w->shm_pending = false;
    }
    XDestroyImage(w->image); /* also frees the calloc'd pixels */
    w->image = NULL;
}

/* ---- window -------------------------------------------------------------- */

static bool compositor_running(kk_window *w)
{
    char name[32];
    snprintf(name, sizeof name, "_NET_WM_CM_S%d", w->screen);
    Atom sel = XInternAtom(w->dpy, name, False);
    return XGetSelectionOwner(w->dpy, sel) != None;
}

static void set_wm_properties(kk_window *w)
{
    static const char title[] = "Kikarinhas";

    XStoreName(w->dpy, w->win, title);
    XChangeProperty(w->dpy, w->win, XInternAtom(w->dpy, "_NET_WM_NAME", False),
                    XInternAtom(w->dpy, "UTF8_STRING", False), 8,
                    PropModeReplace, (const unsigned char *)title,
                    (int)strlen(title));

    XClassHint class = {.res_name = "kikarinhas", .res_class = "Kikarinhas"};
    XSetClassHint(w->dpy, w->win, &class);

    long pid = (long)getpid();
    XChangeProperty(w->dpy, w->win, XInternAtom(w->dpy, "_NET_WM_PID", False),
                    XA_CARDINAL, 32, PropModeReplace,
                    (const unsigned char *)&pid, 1);

    w->wm_protocols = XInternAtom(w->dpy, "WM_PROTOCOLS", False);
    w->wm_delete = XInternAtom(w->dpy, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(w->dpy, w->win, &w->wm_delete, 1);
}

/* Empty input region: clicks go to whatever is below the overlay. */
static void make_click_through(kk_window *w)
{
    int ev_base, err_base, major = 0, minor = 0;
    if (!XShapeQueryExtension(w->dpy, &ev_base, &err_base) ||
        !XShapeQueryVersion(w->dpy, &major, &minor) ||
        (major == 1 && minor < 1)) {
        kk_log_warn("extensão Shape 1.1 ausente; a sobreposição vai "
                    "capturar cliques");
        return;
    }
    XShapeCombineRectangles(w->dpy, w->win, ShapeInput, 0, 0, NULL, 0,
                            ShapeSet, Unsorted);
}

int kk_window_open(kk_window *w, kk_mode mode, int width, int height)
{
    memset(w, 0, sizeof *w);
    w->mode = mode;

    w->dpy = XOpenDisplay(NULL);
    if (!w->dpy) {
        kk_log_error("não consegui abrir o display X (DISPLAY=%s)",
                     getenv("DISPLAY") ? getenv("DISPLAY") : "");
        return -1;
    }
    w->screen = DefaultScreen(w->dpy);
    Window root = RootWindow(w->dpy, w->screen);

    XVisualInfo vi;
    if (!XMatchVisualInfo(w->dpy, w->screen, 32, TrueColor, &vi)) {
        kk_log_error("o servidor X não oferece visual ARGB de 32 bits");
        kk_window_close(w);
        return -1;
    }
    w->visual = vi.visual;
    w->cmap = XCreateColormap(w->dpy, root, w->visual, AllocNone);

    w->has_compositor = compositor_running(w);
    w->shm_available = XShmQueryExtension(w->dpy);
    if (w->shm_available)
        w->shm_completion = XShmGetEventBase(w->dpy) + ShmCompletion;

    int x = 0, y = 0;
    if (mode == KK_MODE_DESKTOP) {
        width = DisplayWidth(w->dpy, w->screen);
        height = DisplayHeight(w->dpy, w->screen);
    }
    w->width = width;
    w->height = height;

    XSetWindowAttributes attr = {
        .colormap = w->cmap,
        .background_pixel = 0, /* fully transparent */
        .border_pixel = 0,     /* required with a non-default visual */
        .event_mask = ExposureMask | StructureNotifyMask,
        .override_redirect = mode == KK_MODE_DESKTOP,
    };
    w->win = XCreateWindow(w->dpy, root, x, y, (unsigned)width,
                           (unsigned)height, 0, 32, InputOutput, w->visual,
                           CWColormap | CWBackPixel | CWBorderPixel |
                               CWEventMask | CWOverrideRedirect,
                           &attr);
    set_wm_properties(w);
    if (mode == KK_MODE_DESKTOP)
        make_click_through(w);

    w->gc = XCreateGC(w->dpy, w->win, 0, NULL);
    if (!buffer_create(w)) {
        kk_log_error("não consegui criar o buffer de %dx%d", width, height);
        kk_window_close(w);
        return -1;
    }

    XMapRaised(w->dpy, w->win);
    XFlush(w->dpy);
    return 0;
}

void kk_window_close(kk_window *w)
{
    if (!w->dpy)
        return;
    buffer_destroy(w);
    if (w->gc)
        XFreeGC(w->dpy, w->gc);
    if (w->win)
        XDestroyWindow(w->dpy, w->win);
    if (w->cmap)
        XFreeColormap(w->dpy, w->cmap);
    XCloseDisplay(w->dpy);
    w->dpy = NULL;
}

int kk_window_fd(const kk_window *w)
{
    return ConnectionNumber(w->dpy);
}

static void handle_configure(kk_window *w, const XConfigureEvent *ce,
                             kk_window_events *ev)
{
    if (ce->width == w->width && ce->height == w->height)
        return;
    buffer_destroy(w);
    w->width = ce->width;
    w->height = ce->height;
    if (!buffer_create(w)) {
        kk_log_error("não consegui recriar o buffer de %dx%d", w->width,
                     w->height);
        ev->quit = true;
        return;
    }
    ev->resized = true;
    ev->redraw = true;
}

void kk_window_dispatch(kk_window *w, kk_window_events *ev)
{
    while (XPending(w->dpy)) {
        XEvent e;
        XNextEvent(w->dpy, &e);

        if (w->shm_available && e.type == w->shm_completion) {
            /* Ignore completions for a segment already replaced by a
             * resize. */
            const XShmCompletionEvent *ce = (const XShmCompletionEvent *)&e;
            if (w->use_shm && ce->shmseg == w->shm.shmseg)
                w->shm_pending = false;
            continue;
        }

        switch (e.type) {
        case Expose:
            if (e.xexpose.count == 0)
                ev->redraw = true;
            break;
        case ConfigureNotify:
            handle_configure(w, &e.xconfigure, ev);
            break;
        case ClientMessage:
            if (e.xclient.message_type == w->wm_protocols &&
                (Atom)e.xclient.data.l[0] == w->wm_delete)
                ev->quit = true;
            break;
        case DestroyNotify:
            ev->quit = true;
            break;
        default:
            break;
        }
    }
    XFlush(w->dpy);
}

bool kk_window_busy(const kk_window *w)
{
    return w->shm_pending;
}

static bool clip_to_window(const kk_window *w, kk_rect r, kk_rect *out)
{
    int x0 = r.x < 0 ? 0 : r.x;
    int y0 = r.y < 0 ? 0 : r.y;
    int x1 = r.x + r.w > w->width ? w->width : r.x + r.w;
    int y1 = r.y + r.h > w->height ? w->height : r.y + r.h;
    *out = (kk_rect){x0, y0, x1 - x0, y1 - y0};
    return x1 > x0 && y1 > y0;
}

void kk_window_present(kk_window *w, const kk_rect *r, int n)
{
    kk_rect c;
    int last = -1;
    for (int i = 0; i < n; i++)
        if (clip_to_window(w, r[i], &c))
            last = i;
    if (last < 0)
        return;

    cairo_surface_flush(w->surface);
    for (int i = 0; i <= last; i++) {
        if (!clip_to_window(w, r[i], &c))
            continue;
        if (w->use_shm) {
            /* Only the last put asks for a ShmCompletion: requests are
             * processed in order, so it covers the earlier ones too. */
            XShmPutImage(w->dpy, w->win, w->gc, w->image, c.x, c.y, c.x, c.y,
                         (unsigned)c.w, (unsigned)c.h, i == last);
        } else {
            XPutImage(w->dpy, w->win, w->gc, w->image, c.x, c.y, c.x, c.y,
                      (unsigned)c.w, (unsigned)c.h);
        }
    }
    w->shm_pending = w->use_shm;
    XFlush(w->dpy);
}
