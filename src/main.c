/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <errno.h>
#include <getopt.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/signalfd.h>
#include <sys/timerfd.h>
#include <time.h>
#include <unistd.h>

#include "demo.h"
#include "log.h"
#include "window.h"

typedef struct {
    kk_mode mode;
    int width, height;
    int fps;
} options;

static void usage(FILE *out)
{
    fprintf(out,
            "Uso: kikarinhas [opções]\n"
            "\n"
            "  -m, --mode MODO   obs (padrão): janela comum para o OBS capturar\n"
            "                    desktop: sobreposição em tela cheia, o clique atravessa\n"
            "  -s, --size LxA    tamanho da janela no modo obs (padrão 1280x720)\n"
            "  -f, --fps N       quadros por segundo, 1 a 240 (padrão 30)\n"
            "  -h, --help        mostra esta ajuda\n"
            "  -V, --version     mostra a versão\n");
}

static int parse_options(int argc, char **argv, options *o)
{
    static const struct option longopts[] = {
        {"mode", required_argument, NULL, 'm'},
        {"size", required_argument, NULL, 's'},
        {"fps", required_argument, NULL, 'f'},
        {"help", no_argument, NULL, 'h'},
        {"version", no_argument, NULL, 'V'},
        {NULL, 0, NULL, 0},
    };
    *o = (options){.mode = KK_MODE_OBS, .width = 1280, .height = 720, .fps = 30};

    int c;
    while ((c = getopt_long(argc, argv, "m:s:f:hV", longopts, NULL)) != -1) {
        switch (c) {
        case 'm':
            if (strcmp(optarg, "obs") == 0) {
                o->mode = KK_MODE_OBS;
            } else if (strcmp(optarg, "desktop") == 0) {
                o->mode = KK_MODE_DESKTOP;
            } else {
                kk_log_error("modo desconhecido: %s", optarg);
                return -1;
            }
            break;
        case 's': {
            char extra;
            if (sscanf(optarg, "%dx%d%c", &o->width, &o->height, &extra) != 2 ||
                o->width < 1 || o->height < 1 || o->width > 16384 ||
                o->height > 16384) {
                kk_log_error("tamanho inválido: %s (use LxA, ex.: 1920x1080)",
                             optarg);
                return -1;
            }
            break;
        }
        case 'f': {
            char *end;
            long fps = strtol(optarg, &end, 10);
            if (*end || fps < 1 || fps > 240) {
                kk_log_error("fps inválido: %s", optarg);
                return -1;
            }
            o->fps = (int)fps;
            break;
        }
        case 'h':
            usage(stdout);
            exit(0);
        case 'V':
            printf("kikarinhas %s\n", KK_VERSION);
            exit(0);
        default:
            usage(stderr);
            return -1;
        }
    }
    if (optind < argc) {
        kk_log_error("argumento inesperado: %s", argv[optind]);
        return -1;
    }
    return 0;
}

static double now_seconds(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

static int open_frame_timer(int fps)
{
    int fd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
    if (fd < 0)
        return -1;
    long period = 1000000000L / fps;
    struct itimerspec spec = {
        .it_interval = {period / 1000000000L, period % 1000000000L},
        .it_value = {period / 1000000000L, period % 1000000000L},
    };
    if (timerfd_settime(fd, 0, &spec, NULL) < 0) {
        close(fd);
        return -1;
    }
    return fd;
}

/* SIGINT/SIGTERM arrive as a readable fd instead of interrupting us. */
static int open_signal_fd(void)
{
    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGINT);
    sigaddset(&mask, SIGTERM);
    if (sigprocmask(SIG_BLOCK, &mask, NULL) < 0)
        return -1;
    return signalfd(-1, &mask, SFD_NONBLOCK | SFD_CLOEXEC);
}

static void run(kk_window *win, kk_demo *demo, int timer_fd, int signal_fd)
{
    enum { FD_X, FD_TIMER, FD_SIGNAL };
    struct pollfd fds[] = {
        [FD_X] = {.fd = kk_window_fd(win), .events = POLLIN},
        [FD_TIMER] = {.fd = timer_fd, .events = POLLIN},
        [FD_SIGNAL] = {.fd = signal_fd, .events = POLLIN},
    };
    bool full = true;
    double last = now_seconds();

    for (;;) {
        kk_window_events ev = {0};
        kk_window_dispatch(win, &ev);
        if (ev.quit)
            return;
        if (ev.resized)
            kk_demo_resize(demo, win->width, win->height);
        if (ev.redraw)
            full = true;

        if (poll(fds, sizeof fds / sizeof fds[0], -1) < 0) {
            if (errno == EINTR)
                continue;
            kk_log_error("poll: %s", strerror(errno));
            return;
        }

        if (fds[FD_SIGNAL].revents & POLLIN)
            return;
        if (fds[FD_X].revents & (POLLERR | POLLHUP)) {
            kk_log_error("conexão com o servidor X perdida");
            return;
        }

        bool tick = false;
        if (fds[FD_TIMER].revents & POLLIN) {
            uint64_t expirations;
            if (read(timer_fd, &expirations, sizeof expirations) > 0) {
                double t = now_seconds();
                double dt = t - last;
                last = t;
                /* After a stall, don't teleport the scene forward. */
                kk_demo_update(demo, dt > 0.1 ? 0.1 : dt);
                tick = true;
            }
        }

        /* While the server still reads the last frame, skip this one; the
         * demo's damage tracking covers the gap on the next frame. */
        if ((tick || full) && !kk_window_busy(win)) {
            cairo_t *cr = cairo_create(win->surface);
            kk_rect damage;
            kk_demo_draw(demo, cr, full, &damage);
            cairo_destroy(cr);
            kk_window_present(win, &damage);
            full = false;
        }
    }
}

int main(int argc, char **argv)
{
    options opt;
    if (parse_options(argc, argv, &opt) < 0)
        return 2;

    int signal_fd = open_signal_fd();
    int timer_fd = open_frame_timer(opt.fps);
    if (signal_fd < 0 || timer_fd < 0) {
        kk_log_error("timerfd/signalfd: %s", strerror(errno));
        return 1;
    }

    kk_window win;
    if (kk_window_open(&win, opt.mode, opt.width, opt.height) < 0)
        return 1;
    if (!win.has_compositor)
        kk_log_warn("nenhum compositor ativo: a transparência vai aparecer "
                    "preta na tela");
    kk_log_info("janela %dx%d no modo %s, %d fps%s", win.width, win.height,
                opt.mode == KK_MODE_OBS ? "obs" : "desktop", opt.fps,
                win.use_shm ? ", MIT-SHM" : "");

    kk_demo demo;
    kk_demo_init(&demo, win.width, win.height);
    run(&win, &demo, timer_fd, signal_fd);
    kk_demo_free(&demo);

    kk_window_close(&win);
    close(timer_fd);
    close(signal_fd);
    return 0;
}
