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

#include "log.h"
#include "sa.h"
#include "stage.h"
#include "window.h"

#define MAX_NAMED 64

typedef struct {
    kk_mode mode;
    int width, height;
    int fps;
    double scale;
    int ground; /* -1 = automatic */
    int count;  /* random avatars to spawn */
    const char *named[MAX_NAMED];
    int n_named;
    const char *sa_dir;
    uint64_t seed;
    bool list, check;
} options;

static void usage(FILE *out)
{
    fprintf(out,
            "Uso: kikarinhas [opções]\n"
            "\n"
            "Janela:\n"
            "  -m, --mode MODO     obs (padrão): janela comum para o OBS capturar\n"
            "                      desktop: sobreposição em tela cheia, o clique atravessa\n"
            "  -s, --size LxA      tamanho da janela no modo obs (padrão 1280x720)\n"
            "  -f, --fps N         quadros por segundo, 1 a 240 (padrão 30)\n"
            "\n"
            "Avatares:\n"
            "      --sa-dir PASTA  pasta \"data\" do Stream Avatars (padrão: procura\n"
            "                      nas bibliotecas do Steam)\n"
            "  -n, --count N       quantos avatares aleatórios mostrar (padrão 6)\n"
            "  -a, --avatar NOME   mostra este avatar (pode repetir)\n"
            "      --scale X       escala dos avatares (padrão 2)\n"
            "      --ground N      pixels entre o chão e a borda de baixo\n"
            "                      (padrão: o espaço do nome)\n"
            "      --seed N        semente do sorteio, para repetir uma cena\n"
            "      --list          lista os avatares encontrados e sai\n"
            "      --check         confere todas as spritesheets e sai\n"
            "\n"
            "  -h, --help          mostra esta ajuda\n"
            "  -V, --version       mostra a versão\n");
}

static bool parse_int(const char *s, long lo, long hi, long *out)
{
    char *end;
    errno = 0;
    long v = strtol(s, &end, 10);
    if (errno || end == s || *end || v < lo || v > hi)
        return false;
    *out = v;
    return true;
}

static int parse_options(int argc, char **argv, options *o)
{
    enum { OPT_SA_DIR = 256, OPT_SCALE, OPT_GROUND, OPT_SEED, OPT_LIST, OPT_CHECK };
    static const struct option longopts[] = {
        {"mode", required_argument, NULL, 'm'},
        {"size", required_argument, NULL, 's'},
        {"fps", required_argument, NULL, 'f'},
        {"count", required_argument, NULL, 'n'},
        {"avatar", required_argument, NULL, 'a'},
        {"sa-dir", required_argument, NULL, OPT_SA_DIR},
        {"scale", required_argument, NULL, OPT_SCALE},
        {"ground", required_argument, NULL, OPT_GROUND},
        {"seed", required_argument, NULL, OPT_SEED},
        {"list", no_argument, NULL, OPT_LIST},
        {"check", no_argument, NULL, OPT_CHECK},
        {"help", no_argument, NULL, 'h'},
        {"version", no_argument, NULL, 'V'},
        {NULL, 0, NULL, 0},
    };
    *o = (options){
        .mode = KK_MODE_OBS,
        .width = 1280,
        .height = 720,
        .fps = 30,
        .scale = 2.0,
        .ground = -1,
        .count = -1,
        .seed = (uint64_t)time(NULL) ^ ((uint64_t)getpid() << 32),
    };

    int c;
    long v;
    while ((c = getopt_long(argc, argv, "m:s:f:n:a:hV", longopts, NULL)) != -1) {
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
        case 'f':
            if (!parse_int(optarg, 1, 240, &v)) {
                kk_log_error("fps inválido: %s", optarg);
                return -1;
            }
            o->fps = (int)v;
            break;
        case 'n':
            if (!parse_int(optarg, 0, 1000, &v)) {
                kk_log_error("quantidade inválida: %s", optarg);
                return -1;
            }
            o->count = (int)v;
            break;
        case 'a':
            if (o->n_named == MAX_NAMED) {
                kk_log_error("no máximo %d avatares com --avatar", MAX_NAMED);
                return -1;
            }
            o->named[o->n_named++] = optarg;
            break;
        case OPT_SA_DIR:
            o->sa_dir = optarg;
            break;
        case OPT_SCALE: {
            char *end;
            o->scale = strtod(optarg, &end);
            if (*end || !(o->scale >= 0.1 && o->scale <= 16.0)) {
                kk_log_error("escala inválida: %s", optarg);
                return -1;
            }
            break;
        }
        case OPT_GROUND:
            if (!parse_int(optarg, 0, 16384, &v)) {
                kk_log_error("chão inválido: %s", optarg);
                return -1;
            }
            o->ground = (int)v;
            break;
        case OPT_SEED:
            if (!parse_int(optarg, 0, 0x7fffffffL, &v)) {
                kk_log_error("semente inválida: %s", optarg);
                return -1;
            }
            o->seed = (uint64_t)v;
            break;
        case OPT_LIST:
            o->list = true;
            break;
        case OPT_CHECK:
            o->check = true;
            break;
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
    if (o->count < 0)
        o->count = o->n_named ? 0 : 6;
    return 0;
}

/* ---- --list and --check -------------------------------------------------- */

static int max_frames(const kk_sa_avatar *a)
{
    int m = 0;
    for (int i = 0; i < a->n_anims; i++)
        if (a->anims[i].frames > m)
            m = a->anims[i].frames;
    return m;
}

static void list_avatars(const kk_sa_library *lib)
{
    for (int i = 0; i < lib->count; i++) {
        const kk_sa_avatar *a = &lib->avatars[i];
        printf("%-28s %4dx%-4d ppu %-5.3g %2d animações%s\n", a->name,
               a->frame_w, a->frame_h, a->ppu, a->n_anims,
               a->image ? "" : "  (sem imagem)");
    }
    printf("%d avatares\n", lib->count);
}

/* Loads every sheet at scale 1 and checks it against its definition. */
static int check_avatars(const kk_sa_library *lib)
{
    kk_stage st;
    if (kk_stage_init(&st, lib, 1.0, 0, 1) < 0)
        return 1;
    int bad = 0, warn = 0;
    for (int i = 0; i < lib->count; i++) {
        const kk_sa_avatar *a = &lib->avatars[i];
        const kk_sheet *sh = kk_stage_sheet(&st, a);
        if (!a->image) {
            printf("ERRO  %s: imagem não encontrada\n", a->name);
            bad++;
            continue;
        }
        if (!sh) {
            printf("ERRO  %s: não carregou %s\n", a->name, a->image);
            bad++;
            continue;
        }
        if (sh->rows < a->n_anims) {
            printf("ERRO  %s: %d linhas na imagem, animações usam %d\n",
                   a->name, sh->rows, a->n_anims);
            bad++;
        } else if (sh->cols < max_frames(a)) {
            printf("aviso %s: %d colunas, animação mais longa tem %d quadros "
                   "(cortada)\n",
                   a->name, sh->cols, max_frames(a));
            warn++;
        }
        if (a->anims[KK_ANIM_IDLE].frames == 0 &&
            a->anims[KK_ANIM_WALK].frames == 0) {
            printf("aviso %s: sem idle nem walk\n", a->name);
            warn++;
        }
    }
    kk_stage_free(&st);
    printf("%d avatares: %d com erro, %d com aviso\n", lib->count, bad, warn);
    return bad ? 1 : 0;
}

/* ---- main loop ----------------------------------------------------------- */

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

static void run(kk_window *win, kk_stage *stage, int timer_fd, int signal_fd)
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
            kk_stage_resize(stage, win->width, win->height);
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
                /* After a stall, don't teleport everyone forward. */
                kk_stage_update(stage, dt > 0.1 ? 0.1 : dt);
                tick = true;
            }
        }

        /* While the server still reads the last frame, skip this one; the
         * avatars remember what was painted, so the next frame catches up. */
        if ((tick || full) && !kk_window_busy(win)) {
            cairo_t *cr = cairo_create(win->surface);
            const kk_rect *rects;
            int n = kk_stage_render(stage, cr, full, &rects);
            cairo_destroy(cr);
            kk_window_present(win, rects, n);
            full = false;
        }
    }
}

static void spawn_avatars(kk_stage *stage, const kk_sa_library *lib,
                          const options *opt)
{
    for (int i = 0; i < opt->n_named; i++) {
        const kk_sa_avatar *a = kk_sa_find(lib, opt->named[i]);
        if (!a)
            kk_log_warn("avatar não encontrado: %s (veja --list)", opt->named[i]);
        else
            kk_stage_spawn(stage, a, a->name);
    }

    /* Random distinct avatars: shuffle the indices, take the first usable. */
    int *order = malloc((size_t)lib->count * sizeof *order);
    if (!order)
        return;
    for (int i = 0; i < lib->count; i++)
        order[i] = i;
    for (int i = lib->count - 1; i > 0; i--) {
        int j = kk_rng_int(&stage->rng, i + 1);
        int t = order[i];
        order[i] = order[j];
        order[j] = t;
    }
    int spawned = 0;
    for (int i = 0; i < lib->count && spawned < opt->count; i++) {
        const kk_sa_avatar *a = &lib->avatars[order[i]];
        if (a->image && kk_stage_spawn(stage, a, a->name) == 0)
            spawned++;
    }
    free(order);
}

int main(int argc, char **argv)
{
    options opt;
    if (parse_options(argc, argv, &opt) < 0)
        return 2;

    char found[KK_PATH_MAX];
    const char *sa_dir = opt.sa_dir;
    if (!sa_dir) {
        if (!kk_sa_find_data_dir(found, sizeof found)) {
            kk_log_error("não achei o Stream Avatars nas bibliotecas do Steam; "
                         "indique a pasta com --sa-dir");
            return 1;
        }
        sa_dir = found;
    }

    kk_sa_library lib;
    if (kk_sa_load(&lib, sa_dir) < 0)
        return 1;
    if (opt.list || opt.check) {
        int rc = 0;
        if (opt.list)
            list_avatars(&lib);
        if (opt.check)
            rc = check_avatars(&lib);
        kk_sa_free(&lib);
        return rc;
    }
    kk_log_info("%d avatares do Stream Avatars em %s", lib.count, sa_dir);

    int signal_fd = open_signal_fd();
    int timer_fd = open_frame_timer(opt.fps);
    if (signal_fd < 0 || timer_fd < 0) {
        kk_log_error("timerfd/signalfd: %s", strerror(errno));
        kk_sa_free(&lib);
        return 1;
    }

    kk_window win;
    if (kk_window_open(&win, opt.mode, opt.width, opt.height) < 0) {
        kk_sa_free(&lib);
        return 1;
    }
    if (!win.has_compositor)
        kk_log_warn("nenhum compositor ativo: a transparência vai aparecer "
                    "preta na tela");
    kk_log_info("janela %dx%d no modo %s, %d fps%s", win.width, win.height,
                opt.mode == KK_MODE_OBS ? "obs" : "desktop", opt.fps,
                win.use_shm ? ", MIT-SHM" : "");

    kk_stage stage;
    if (kk_stage_init(&stage, &lib, opt.scale, opt.ground, opt.seed) < 0) {
        kk_log_error("sem memória");
        return 1;
    }
    kk_stage_resize(&stage, win.width, win.height);
    spawn_avatars(&stage, &lib, &opt);

    run(&win, &stage, timer_fd, signal_fd);

    kk_stage_free(&stage);
    kk_window_close(&win);
    kk_sa_free(&lib);
    close(timer_fd);
    close(signal_fd);
    return 0;
}
