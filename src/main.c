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

#include "actions.h"
#include "commands.h"
#include "config.h"
#include "control.h"
#include "demochat.h"
#include "http.h"
#include "log.h"
#include "sa.h"
#include "soundboard.h"
#include "stage.h"
#include "users.h"
#include "window.h"
#include "youtube.h"

/* What only the command line sets. */
typedef struct {
    const char *config_path; /* NULL = default */
    uint64_t seed;
    bool list, check, import_sa, reload;
} cli;

static void usage(FILE *out)
{
    fprintf(out,
            "Uso: kikarinhas [opções]\n"
            "\n"
            "As opções valem por cima do arquivo de configuração\n"
            "(~/.config/kikarinhas/kikarinhas.ini; veja o kikarinhas-config).\n"
            "\n"
            "Janela:\n"
            "  -m, --mode MODO     obs (padrão): janela comum para o OBS capturar\n"
            "                      desktop: sobreposição em tela cheia, o clique atravessa\n"
            "  -s, --size LxA      tamanho da janela no modo obs (padrão 1280x720)\n"
            "  -f, --fps N         quadros por segundo, 1 a 240 (padrão 30)\n"
            "\n"
            "Chat:\n"
            "  -y, --youtube ALVO  link da live ou do canal, @handle ou id do vídeo;\n"
            "                      com um canal, espera ele entrar ao vivo\n"
            "      --demo-chat     chat de mentira, para testar sem live\n"
            "      --max N         avatares do chat ao mesmo tempo (padrão 30)\n"
            "      --despawn S     segundos em silêncio até o avatar sair (padrão 300)\n"
            "  -d, --default-avatar NOME\n"
            "                      todos do chat usam este avatar (padrão: um\n"
            "                      sorteado por pessoa, sempre o mesmo)\n"
            "  -v, --verbose       mostra as mensagens do chat no terminal\n"
            "      --users ARQ     onde guardar o avatar/cor/acessórios de cada\n"
            "                      pessoa (padrão ~/.local/share/kikarinhas/users.tsv)\n"
            "      --import-sa-users\n"
            "                      importa as escolhas do Stream Avatars (feito\n"
            "                      sozinho na primeira vez)\n"
            "\n"
            "Avatares:\n"
            "      --sa-dir PASTA  pasta \"data\" do Stream Avatars (padrão: procura\n"
            "                      nas bibliotecas do Steam)\n"
            "  -n, --count N       avatares aleatórios fora do chat (padrão 6, ou 0\n"
            "                      com chat ou --avatar)\n"
            "  -a, --avatar NOME   mostra este avatar (pode repetir)\n"
            "      --scale X       escala dos avatares (padrão 2)\n"
            "      --ground N      pixels entre o chão e a borda de baixo\n"
            "                      (padrão: o espaço do nome)\n"
            "      --seed N        semente do sorteio, para repetir uma cena\n"
            "      --list          lista os avatares encontrados e sai\n"
            "      --check         confere todas as spritesheets e sai\n"
            "\n"
            "Configuração e controle:\n"
            "  -c, --config ARQ    arquivo de configuração\n"
            "      --socket CAMINHO|off\n"
            "                      socket de controle (padrão\n"
            "                      $XDG_RUNTIME_DIR/kikarinhas.sock)\n"
            "      --reload        pede ao kikarinhas que já está aberto para reler\n"
            "                      a configuração e sai (o mesmo que kill -HUP)\n"
            "\n"
            "  -h, --help          mostra esta ajuda\n"
            "  -V, --version       mostra a versão\n");
}

/* Applies argv on top of cfg. Called at start and again on each reload
 * (after the file), so the command line keeps winning. */
static int parse_args(int argc, char **argv, kk_config *cfg, cli *x)
{
    enum {
        OPT_SA_DIR = 256,
        OPT_SCALE,
        OPT_GROUND,
        OPT_SEED,
        OPT_LIST,
        OPT_CHECK,
        OPT_DEMO_CHAT,
        OPT_MAX,
        OPT_DESPAWN,
        OPT_USERS,
        OPT_IMPORT_SA,
        OPT_SOCKET,
        OPT_RELOAD,
    };
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
        {"youtube", required_argument, NULL, 'y'},
        {"demo-chat", no_argument, NULL, OPT_DEMO_CHAT},
        {"max", required_argument, NULL, OPT_MAX},
        {"despawn", required_argument, NULL, OPT_DESPAWN},
        {"default-avatar", required_argument, NULL, 'd'},
        {"verbose", no_argument, NULL, 'v'},
        {"users", required_argument, NULL, OPT_USERS},
        {"import-sa-users", no_argument, NULL, OPT_IMPORT_SA},
        {"config", required_argument, NULL, 'c'},
        {"socket", required_argument, NULL, OPT_SOCKET},
        {"reload", no_argument, NULL, OPT_RELOAD},
        {"help", no_argument, NULL, 'h'},
        {"version", no_argument, NULL, 'V'},
        {NULL, 0, NULL, 0},
    };

    bool shown = false; /* the first -a replaces the file's list */
    int c;
    long v;
    double d;
    optind = 0; /* GNU: start over, this runs more than once */
    while ((c = getopt_long(argc, argv, "m:s:f:n:a:y:d:c:vhV", longopts, NULL)) != -1) {
        switch (c) {
        case 'm':
            if (strcmp(optarg, "obs") == 0) {
                cfg->desktop = false;
            } else if (strcmp(optarg, "desktop") == 0) {
                cfg->desktop = true;
            } else {
                kk_log_error("modo desconhecido: %s", optarg);
                return -1;
            }
            break;
        case 's':
            if (!kk_parse_size(optarg, &cfg->width, &cfg->height)) {
                kk_log_error("tamanho inválido: %s (use LxA, ex.: 1920x1080)",
                             optarg);
                return -1;
            }
            break;
        case 'f':
            if (!kk_parse_long(optarg, 1, 240, &v)) {
                kk_log_error("fps inválido: %s", optarg);
                return -1;
            }
            cfg->fps = (int)v;
            break;
        case 'n':
            if (!kk_parse_long(optarg, 0, 1000, &v)) {
                kk_log_error("quantidade inválida: %s", optarg);
                return -1;
            }
            cfg->count = (int)v;
            break;
        case 'a':
            if (!shown)
                kk_config_set_show(cfg, "");
            shown = true;
            if (cfg->n_show == KK_CONFIG_MAX_SHOW) {
                kk_log_error("no máximo %d avatares com --avatar", KK_CONFIG_MAX_SHOW);
                return -1;
            }
            cfg->show[cfg->n_show] = NULL;
            if (!kk_config_set_str(&cfg->show[cfg->n_show], optarg))
                return -1;
            cfg->n_show++;
            break;
        case OPT_SA_DIR:
            kk_config_set_str(&cfg->sa_dir, optarg);
            break;
        case OPT_SCALE:
            if (!kk_parse_double(optarg, 0.1, 16.0, &d)) {
                kk_log_error("escala inválida: %s", optarg);
                return -1;
            }
            cfg->scale = d;
            break;
        case OPT_GROUND:
            if (!kk_parse_long(optarg, 0, 16384, &v)) {
                kk_log_error("chão inválido: %s", optarg);
                return -1;
            }
            cfg->ground = (int)v;
            break;
        case OPT_SEED:
            if (!kk_parse_long(optarg, 0, 0x7fffffffL, &v)) {
                kk_log_error("semente inválida: %s", optarg);
                return -1;
            }
            x->seed = (uint64_t)v;
            break;
        case OPT_LIST:
            x->list = true;
            break;
        case 'y':
            kk_config_set_str(&cfg->youtube, optarg);
            break;
        case OPT_DEMO_CHAT:
            cfg->demo = true;
            break;
        case OPT_MAX:
            if (!kk_parse_long(optarg, 1, 1000, &v)) {
                kk_log_error("máximo inválido: %s", optarg);
                return -1;
            }
            cfg->max_avatars = (int)v;
            break;
        case OPT_DESPAWN:
            if (!kk_parse_long(optarg, 5, 86400, &v)) {
                kk_log_error("tempo inválido: %s", optarg);
                return -1;
            }
            cfg->despawn = (double)v;
            break;
        case 'd':
            kk_config_set_str(&cfg->default_avatar, optarg);
            break;
        case 'v':
            cfg->verbose = true;
            break;
        case OPT_USERS:
            kk_config_set_str(&cfg->users, optarg);
            break;
        case OPT_IMPORT_SA:
            x->import_sa = true;
            break;
        case 'c':
            x->config_path = optarg;
            break;
        case OPT_SOCKET:
            kk_config_set_str(&cfg->socket, strcmp(optarg, "off") == 0 ? "" : optarg);
            break;
        case OPT_RELOAD:
            x->reload = true;
            break;
        case OPT_CHECK:
            x->check = true;
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
    return 0;
}

/* Problems found in the config file: logged, and also collected into a JSON
 * array when a reload was asked over the socket. */
typedef struct {
    const char *path;
    cJSON *list; /* may be NULL */
    bool quiet;  /* --reload: the running instance reports them */
} warn_sink;

static void on_config_warning(void *ud, int line, const char *msg)
{
    warn_sink *w = ud;
    char text[1024];
    if (line > 0)
        snprintf(text, sizeof text, "%s:%d: %s", w->path, line, msg);
    else
        snprintf(text, sizeof text, "%s", msg);
    if (!w->quiet)
        kk_log_warn("%s", text);
    if (w->list)
        cJSON_AddItemToArray(w->list, cJSON_CreateString(text));
}

/* Defaults, then the file, then the command line. */
static int load_config(kk_config *cfg, int argc, char **argv,
                       const char *path, warn_sink *warn)
{
    kk_config_defaults(cfg);
    bool found;
    if (path[0] && kk_config_load(cfg, path, &found, on_config_warning, warn) < 0) {
        kk_log_error("não consegui ler %s: %s", path, strerror(errno));
        kk_config_free(cfg);
        return -1;
    }
    cli scratch = {0};
    if (parse_args(argc, argv, cfg, &scratch) < 0) {
        kk_config_free(cfg);
        return -1;
    }
    return 0;
}

static bool socket_path(const kk_config *cfg, char *out, size_t size)
{
    if (cfg->socket)
        return cfg->socket[0] && kk_pathf(out, size, "%s", cfg->socket);
    return kk_control_default_path(out, size);
}

/* --reload: ask the running instance and report what it said. */
static int send_reload(const kk_config *cfg)
{
    char path[KK_PATH_MAX], reply[8192];
    if (!socket_path(cfg, path, sizeof path)) {
        kk_log_error("o socket de controle está desligado na configuração");
        return 1;
    }
    if (kk_control_request(path, "{\"type\":\"reload\"}", reply, sizeof reply,
                           5000) < 0) {
        kk_log_error("nenhum kikarinhas respondeu em %s: %s", path, strerror(errno));
        return 1;
    }
    cJSON *r = cJSON_Parse(reply);
    const cJSON *ok = cJSON_GetObjectItemCaseSensitive(r, "ok");
    const cJSON *w;
    cJSON_ArrayForEach(w, cJSON_GetObjectItemCaseSensitive(r, "warnings"))
        if (cJSON_IsString(w))
            kk_log_warn("%s", w->valuestring);
    int rc = 0;
    if (cJSON_IsTrue(ok)) {
        kk_log_info("configuração recarregada");
    } else {
        const char *err = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(r, "error"));
        kk_log_error("o recarregamento falhou: %s", err ? err : reply);
        rc = 1;
    }
    cJSON_Delete(r);
    return rc;
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
    kk_stage_config cfg = {.scale = 1.0, .max_avatars = 1, .despawn = 1, .seed = 1};
    if (kk_stage_init(&st, lib, &cfg) < 0)
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

/* ---- the running program ------------------------------------------------- */

static double now_seconds(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

static int set_frame_timer(int fd, int fps)
{
    long period = 1000000000L / fps;
    struct itimerspec spec = {
        .it_interval = {period / 1000000000L, period % 1000000000L},
        .it_value = {period / 1000000000L, period % 1000000000L},
    };
    return timerfd_settime(fd, 0, &spec, NULL);
}

static int open_frame_timer(int fps)
{
    int fd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
    if (fd >= 0 && set_frame_timer(fd, fps) < 0) {
        close(fd);
        return -1;
    }
    return fd;
}

/* SIGINT/SIGTERM (quit) and SIGHUP (reload) arrive as a readable fd instead
 * of interrupting us. */
static int open_signal_fd(void)
{
    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGINT);
    sigaddset(&mask, SIGTERM);
    sigaddset(&mask, SIGHUP);
    if (sigprocmask(SIG_BLOCK, &mask, NULL) < 0)
        return -1;
    return signalfd(-1, &mask, SFD_NONBLOCK | SFD_CLOEXEC);
}

typedef struct {
    kk_stage *stage;
    kk_commands *commands;
    kk_actions *actions;
    bool verbose;
} chat_sink;

static void on_chat(void *ud, const kk_chat_msg *m)
{
    chat_sink *sink = ud;
    kk_avatar *a = kk_stage_chatter(sink->stage, m);
    kk_cmd_result r = KK_CMD_NONE;
    if (a) {
        char key[256];
        snprintf(key, sizeof key, "%s:%s", m->platform, m->user_id);
        sink->actions->self = a;
        r = kk_commands_handle(sink->commands, m, key, now_seconds());
        if (r == KK_CMD_NONE)
            kk_stage_say(sink->stage, a, m);
    }
    if (sink->verbose) {
        static const char *const kinds[] = {"", " [pago]", " [membro]"};
        static const char *const results[] = {"", "  [comando]", "  [em espera]",
                                              "  [sem permissão]", "  [nada feito]"};
        fprintf(stderr, "[%s] %s%s%s%s: %s%s\n", m->platform, m->name,
                kinds[m->kind], m->amount ? " " : "", m->amount ? m->amount : "",
                m->text, results[r]);
    }
}

static bool on_sound(void *ud, const kk_chat_msg *m, const char *sound)
{
    (void)m;
    return kk_soundboard_play(ud, sound, now_seconds());
}

typedef struct {
    kk_users *users;
    int imported;
} import_ctx;

static void set_time(kk_users *users, const char *key, kk_user_field f, long long t)
{
    char s[32];
    snprintf(s, sizeof s, "%lld", t);
    if (t > 0)
        kk_users_set(users, key, f, s);
}

/* Never overwrite what is known here: people already in the file only get
 * a name and dates they don't have yet. */
static void on_sa_user(void *ud, const kk_sa_user *u)
{
    import_ctx *ic = ud;
    bool known = kk_users_exists(ic->users, u->key);
    if (!kk_users_get(ic->users, u->key, KK_USER_NAME))
        kk_users_set(ic->users, u->key, KK_USER_NAME, u->name);
    if (!kk_users_get(ic->users, u->key, KK_USER_FIRST))
        set_time(ic->users, u->key, KK_USER_FIRST, u->first);
    if (!kk_users_get(ic->users, u->key, KK_USER_LAST))
        set_time(ic->users, u->key, KK_USER_LAST, u->last);
    if (known)
        return;
    kk_users_set(ic->users, u->key, KK_USER_AVATAR, u->avatar);
    kk_users_set(ic->users, u->key, KK_USER_PALETTE, u->palette);
    char gear[1024] = "";
    size_t n = 0;
    for (int i = 0; i < u->n_gear; i++) {
        int w = snprintf(gear + n, sizeof gear - n, "%s%s", i ? "," : "", u->gear[i]);
        if (w < 0 || (size_t)w >= sizeof gear - n)
            break;
        n += (size_t)w;
    }
    kk_users_set(ic->users, u->key, KK_USER_GEAR, gear);
    ic->imported++;
}

typedef struct {
    int argc;
    char **argv;
    const char *config_path;
    kk_config cfg; /* what is in effect */

    const kk_sa_library *lib;
    kk_window *win;
    kk_stage *stage;
    kk_http *http;
    kk_actions *actions;
    chat_sink sink;
    kk_youtube *youtube;  /* NULL if not used */
    kk_demochat demo;
    bool demo_on;
    kk_users *users;      /* NULL if not used */
    kk_control *control;  /* NULL if off */
    kk_soundboard *sounds;
    int timer_fd, signal_fd;
    uint64_t seed;
    bool quit;
} app;

static const kk_sa_avatar *find_default_avatar(const app *a, const kk_config *cfg,
                                               warn_sink *warn)
{
    if (!cfg->default_avatar)
        return NULL;
    const kk_sa_avatar *def = kk_sa_find(a->lib, cfg->default_avatar);
    if (!def) {
        char msg[300];
        snprintf(msg, sizeof msg, "avatar padrão não encontrado: %s (veja --list)",
                 cfg->default_avatar);
        on_config_warning(warn, 0, msg);
    }
    return def;
}

static kk_commands *build_commands(app *a, const kk_config *cfg, warn_sink *warn)
{
    kk_commands *c = kk_commands_new(a->actions);
    if (c)
        kk_actions_register(c, cfg, on_config_warning, warn);
    return c;
}

/* Starts, stops or retargets the chat connectors to match cfg. */
static int connect_chats(app *a, const kk_config *cfg)
{
    const char *old = a->youtube ? a->cfg.youtube : NULL;
    bool same = old && cfg->youtube && strcmp(old, cfg->youtube) == 0;
    if (!same) {
        kk_youtube_free(a->youtube);
        a->youtube = NULL;
        if (cfg->youtube) {
            a->youtube = kk_youtube_new(a->http, cfg->youtube, on_chat, &a->sink);
            if (!a->youtube)
                return -1;
        }
    }
    if (cfg->demo && !a->demo_on)
        kk_demochat_init(&a->demo, on_chat, &a->sink, a->seed);
    a->demo_on = cfg->demo;
    return 0;
}

static bool str_differs(const char *x, const char *y)
{
    return (x || y) && (!x || !y || strcmp(x, y) != 0);
}

/* Rereads the config and applies what can change while running. */
static bool reload(app *a, cJSON *warnings)
{
    warn_sink warn = {.path = a->config_path, .list = warnings};
    kk_config cfg;
    if (load_config(&cfg, a->argc, a->argv, a->config_path, &warn) < 0)
        return false;

    const kk_config *old = &a->cfg;
    struct {
        bool changed;
        const char *what;
    } restart[] = {
        {old->desktop != cfg.desktop, "mode"},
        {old->width != cfg.width || old->height != cfg.height, "size"},
        {old->scale != cfg.scale, "scale"},
        {old->ground != cfg.ground, "ground"},
        {old->count != cfg.count, "count"},
        {old->n_show != cfg.n_show, "show"},
        {str_differs(old->sa_dir, cfg.sa_dir), "sa_dir"},
        {str_differs(old->users, cfg.users), "users"},
        {str_differs(old->socket, cfg.socket), "socket"},
    };
    for (int i = 0; old->n_show == cfg.n_show && i < cfg.n_show; i++)
        restart[5].changed |= strcmp(old->show[i], cfg.show[i]) != 0;
    for (size_t i = 0; i < sizeof restart / sizeof restart[0]; i++)
        if (restart[i].changed) {
            char msg[128];
            snprintf(msg, sizeof msg, "\"%s\" só muda reiniciando o kikarinhas",
                     restart[i].what);
            on_config_warning(&warn, 0, msg);
        }
    /* Those keep the values in use, so the next reload compares with what
     * is really running. */
    kk_config keep;
    if (!kk_config_copy(&keep, old)) {
        kk_config_free(&cfg);
        return false;
    }
#define KEEP(f) do { __typeof__(cfg.f) t_ = cfg.f; cfg.f = keep.f; keep.f = t_; } while (0)
    KEEP(desktop);
    KEEP(width);
    KEEP(height);
    KEEP(scale);
    KEEP(ground);
    KEEP(count);
    KEEP(sa_dir);
    KEEP(users);
    KEEP(socket);
    for (int i = 0; i < KK_CONFIG_MAX_SHOW; i++)
        KEEP(show[i]);
    KEEP(n_show);
#undef KEEP
    kk_config_free(&keep);

    kk_commands *commands = build_commands(a, &cfg, &warn);
    if (!commands || connect_chats(a, &cfg) < 0) {
        kk_commands_free(commands);
        kk_config_free(&cfg);
        return false;
    }
    kk_commands_free(a->sink.commands);
    a->sink.commands = commands;
    a->sink.verbose = cfg.verbose;
    kk_soundboard_configure(a->sounds, &cfg);
    a->stage->cfg.max_avatars = cfg.max_avatars;
    a->stage->cfg.despawn = cfg.despawn;
    a->stage->cfg.default_avatar = find_default_avatar(a, &cfg, &warn);
    if (cfg.fps != old->fps)
        set_frame_timer(a->timer_fd, cfg.fps);

    kk_config_free(&a->cfg);
    a->cfg = cfg;
    kk_log_info("configuração recarregada");
    return true;
}

/* {"type":"set_avatar","user":"youtube:UC...","avatar":"pikachu"}: from
 * kikarinhas-config, which must not write the people file under our feet. */
static void set_avatar(app *a, const cJSON *req, cJSON *reply)
{
    const char *key = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(req, "user"));
    const char *name = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(req, "avatar"));
    const kk_sa_avatar *def = name ? kk_sa_find(a->lib, name) : NULL;
    const char *error = NULL;
    if (!key || !key[0])
        error = "faltou \"user\"";
    else if (!def)
        error = "avatar desconhecido";
    else if (!a->users)
        error = "sem arquivo de pessoas";
    if (error) {
        cJSON_ReplaceItemInObjectCaseSensitive(reply, "ok", cJSON_CreateFalse());
        cJSON_AddStringToObject(reply, "error", error);
        return;
    }
    for (int i = 0; i < a->stage->count; i++) {
        kk_avatar *av = &a->stage->avatars[i];
        if (av->user_id && strcmp(av->user_id, key) == 0) {
            if (av->def != def)
                kk_stage_set_avatar(a->stage, av, def);
            return; /* saved by the stage */
        }
    }
    kk_users_set(a->users, key, KK_USER_AVATAR, def->key);
    kk_users_set(a->users, key, KK_USER_PALETTE, NULL);
}

static void on_request(void *ud, const char *type, const cJSON *req, cJSON *reply)
{
    app *a = ud;
    if (strcmp(type, "reload") == 0) {
        cJSON *warnings = cJSON_CreateArray();
        if (!reload(a, warnings)) {
            cJSON_ReplaceItemInObjectCaseSensitive(reply, "ok", cJSON_CreateFalse());
            cJSON_AddStringToObject(reply, "error", "configuração não aplicada");
        }
        cJSON_AddItemToObject(reply, "warnings", warnings);
    } else if (strcmp(type, "play") == 0) {
        const char *s = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(req, "sound"));
        if (!s || !kk_soundboard_play(a->sounds, s, now_seconds())) {
            cJSON_ReplaceItemInObjectCaseSensitive(reply, "ok", cJSON_CreateFalse());
            cJSON_AddStringToObject(reply, "error", "som desconhecido ou que não toca");
        }
    } else if (strcmp(type, "stop") == 0) {
        kk_soundboard_stop(a->sounds);
    } else if (strcmp(type, "save") == 0) {
        if (a->users && kk_users_save(a->users) < 0) {
            cJSON_ReplaceItemInObjectCaseSensitive(reply, "ok", cJSON_CreateFalse());
            cJSON_AddStringToObject(reply, "error", "não consegui gravar o arquivo de pessoas");
        }
    } else if (strcmp(type, "set_avatar") == 0) {
        set_avatar(a, req, reply);
    } else if (strcmp(type, "ping") == 0) {
        cJSON_AddStringToObject(reply, "version", KK_VERSION);
    } else if (strcmp(type, "quit") == 0) {
        a->quit = true;
    } else {
        cJSON_ReplaceItemInObjectCaseSensitive(reply, "ok", cJSON_CreateFalse());
        cJSON_AddStringToObject(reply, "error", "tipo desconhecido");
    }
}

/* Bridges on the control socket feed the same path as the connectors. */
static void on_bridge(void *ud, const kk_chat_msg *m)
{
    app *a = ud;
    on_chat(&a->sink, m);
}

/* False when it is time to quit. */
static bool handle_signals(app *a)
{
    struct signalfd_siginfo si;
    while (read(a->signal_fd, &si, sizeof si) == (ssize_t)sizeof si) {
        if (si.ssi_signo != SIGHUP)
            return false;
        reload(a, NULL);
    }
    return true;
}

#define MAX_FDS 32

static void run(app *a)
{
    enum { FD_X, FD_TIMER, FD_SIGNAL, FD_FIXED };
    struct pollfd fds[MAX_FDS];
    bool full = true;
    double last = now_seconds();
    double next_save = last + 30.0;

    while (!a->quit) {
        kk_window_events ev = {0};
        kk_window_dispatch(a->win, &ev);
        if (ev.quit)
            return;
        if (ev.resized)
            kk_stage_resize(a->stage, a->win->width, a->win->height);
        if (ev.redraw)
            full = true;

        fds[FD_X] = (struct pollfd){.fd = kk_window_fd(a->win), .events = POLLIN};
        fds[FD_TIMER] = (struct pollfd){.fd = a->timer_fd, .events = POLLIN};
        fds[FD_SIGNAL] = (struct pollfd){.fd = a->signal_fd, .events = POLLIN};
        int nfds = FD_FIXED;
        int n_audio = kk_soundboard_pollfds(a->sounds, fds + nfds, 8);
        nfds += n_audio;
        int ctl_at = nfds;
        if (a->control)
            nfds += kk_control_pollfds(a->control, fds + nfds, MAX_FDS - nfds);

        /* Network transfers advance inside the wait; chat callbacks run
         * from it. The frame timer wakes us at least once per frame. */
        if (kk_http_wait(a->http, fds, nfds, 1000) < 0) {
            kk_log_error("falha no laço principal");
            return;
        }

        if ((fds[FD_SIGNAL].revents & POLLIN) && !handle_signals(a))
            return;
        if (fds[FD_X].revents & (POLLERR | POLLHUP)) {
            kk_log_error("conexão com o servidor X perdida");
            return;
        }
        if (a->control)
            kk_control_dispatch(a->control, fds + ctl_at, nfds - ctl_at);

        double t = now_seconds();
        kk_soundboard_pump(a->sounds, fds + FD_FIXED, n_audio, t);
        if (a->youtube)
            kk_youtube_tick(a->youtube, t);
        if (a->demo_on)
            kk_demochat_tick(&a->demo, t);
        if (a->users && t >= next_save) {
            kk_users_save(a->users);
            next_save = t + 30.0;
        }

        bool tick = false;
        if (fds[FD_TIMER].revents & POLLIN) {
            uint64_t expirations;
            if (read(a->timer_fd, &expirations, sizeof expirations) > 0) {
                double dt = t - last;
                last = t;
                /* After a stall, don't teleport everyone forward. */
                kk_stage_update(a->stage, dt > 0.1 ? 0.1 : dt);
                tick = true;
            }
        }

        /* While the server still reads the last frame, skip this one; the
         * avatars remember what was painted, so the next frame catches up. */
        if ((tick || full) && !kk_window_busy(a->win)) {
            cairo_t *cr = cairo_create(a->win->surface);
            const kk_rect *rects;
            int n = kk_stage_render(a->stage, cr, full, &rects);
            cairo_destroy(cr);
            kk_window_present(a->win, rects, n);
            full = false;
        }
    }
}

static void spawn_avatars(kk_stage *stage, const kk_sa_library *lib,
                          const kk_config *cfg)
{
    for (int i = 0; i < cfg->n_show; i++) {
        const kk_sa_avatar *a = kk_sa_find(lib, cfg->show[i]);
        if (!a)
            kk_log_warn("avatar não encontrado: %s (veja --list)", cfg->show[i]);
        else
            kk_stage_spawn(stage, a, a->name);
    }

    int count = cfg->count;
    if (count < 0)
        count = cfg->n_show || cfg->youtube || cfg->demo ? 0 : 6;

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
    for (int i = 0; i < lib->count && spawned < count; i++) {
        const kk_sa_avatar *a = &lib->avatars[order[i]];
        if (a->image && kk_stage_spawn(stage, a, a->name) == 0)
            spawned++;
    }
    free(order);
}

static kk_users *open_users(const kk_config *cfg, const cli *x, const char *sa_dir)
{
    /* Who wears what. The first time, bring over Stream Avatars' choices. */
    char path[KK_PATH_MAX];
    if (cfg->users)
        snprintf(path, sizeof path, "%s", cfg->users);
    else if (!kk_users_default_path(path, sizeof path))
        return NULL;
    bool first = !kk_file_exists(path);
    kk_users *users = kk_users_open(path);
    if (users && (x->import_sa || first)) {
        import_ctx ic = {.users = users};
        if (kk_sa_read_users(sa_dir, on_sa_user, &ic) >= 0) {
            kk_log_info("%d pessoas importadas do Stream Avatars para %s",
                        ic.imported, path);
            kk_users_save(users);
        }
    }
    return users;
}

int main(int argc, char **argv)
{
    /* First pass: validate the command line and find the config file. */
    cli x = {.seed = (uint64_t)time(NULL) ^ ((uint64_t)getpid() << 32)};
    kk_config probe;
    kk_config_defaults(&probe);
    int prc = parse_args(argc, argv, &probe, &x);
    kk_config_free(&probe);
    if (prc < 0)
        return 2;

    char config_path[KK_PATH_MAX] = "";
    if (x.config_path)
        snprintf(config_path, sizeof config_path, "%s", x.config_path);
    else
        kk_config_default_path(config_path, sizeof config_path);
    if (x.config_path && !kk_file_exists(config_path)) {
        kk_log_error("arquivo de configuração não encontrado: %s", config_path);
        return 2;
    }

    app a = {.argc = argc, .argv = argv, .config_path = config_path, .seed = x.seed};
    warn_sink warn = {.path = config_path};
    warn.quiet = x.reload;
    if (load_config(&a.cfg, argc, argv, config_path, &warn) < 0)
        return 2;
    warn.quiet = false;
    if (x.reload) {
        int rc = send_reload(&a.cfg);
        kk_config_free(&a.cfg);
        return rc;
    }

    char found[KK_PATH_MAX];
    const char *sa_dir = a.cfg.sa_dir;
    if (!sa_dir) {
        if (!kk_sa_find_data_dir(found, sizeof found)) {
            kk_log_error("não achei o Stream Avatars nas bibliotecas do Steam; "
                         "indique a pasta com --sa-dir ou sa_dir no %s",
                         config_path);
            kk_config_free(&a.cfg);
            return 1;
        }
        sa_dir = found;
    }

    kk_sa_library lib;
    if (kk_sa_load(&lib, sa_dir) < 0) {
        kk_config_free(&a.cfg);
        return 1;
    }
    a.lib = &lib;
    if (x.list || x.check) {
        int rc = 0;
        if (x.list)
            list_avatars(&lib);
        if (x.check)
            rc = check_avatars(&lib);
        kk_sa_free(&lib);
        kk_config_free(&a.cfg);
        return rc;
    }
    kk_log_info("%d avatares do Stream Avatars em %s", lib.count, sa_dir);

    a.signal_fd = open_signal_fd();
    a.timer_fd = open_frame_timer(a.cfg.fps);
    if (a.signal_fd < 0 || a.timer_fd < 0) {
        kk_log_error("timerfd/signalfd: %s", strerror(errno));
        return 1;
    }

    kk_window win;
    kk_mode mode = a.cfg.desktop ? KK_MODE_DESKTOP : KK_MODE_OBS;
    if (kk_window_open(&win, mode, a.cfg.width, a.cfg.height) < 0)
        return 1;
    if (!win.has_compositor)
        kk_log_warn("nenhum compositor ativo: a transparência vai aparecer "
                    "preta na tela");
    kk_log_info("janela %dx%d no modo %s, %d fps%s", win.width, win.height,
                a.cfg.desktop ? "desktop" : "obs", a.cfg.fps,
                win.use_shm ? ", MIT-SHM" : "");
    a.win = &win;
    a.users = open_users(&a.cfg, &x, sa_dir);

    kk_stage_config scfg = {
        .users = a.users,
        .scale = a.cfg.scale,
        .ground_margin = a.cfg.ground,
        .max_avatars = a.cfg.max_avatars,
        .despawn = a.cfg.despawn,
        .default_avatar = find_default_avatar(&a, &a.cfg, &warn),
        .seed = x.seed,
    };
    kk_stage stage;
    a.http = kk_http_new();
    if (!a.http || kk_stage_init(&stage, &lib, &scfg) < 0) {
        kk_log_error("sem memória");
        return 1;
    }
    a.stage = &stage;
    kk_stage_resize(&stage, win.width, win.height);
    spawn_avatars(&stage, &lib, &a.cfg);

    a.sounds = kk_soundboard_new();
    if (!a.sounds) {
        kk_log_error("sem memória");
        return 1;
    }
    kk_soundboard_configure(a.sounds, &a.cfg);
    kk_actions actions = {.stage = &stage, .sound = on_sound, .sound_ud = a.sounds};
    a.actions = &actions;
    a.sink = (chat_sink){
        .stage = &stage,
        .actions = &actions,
        .verbose = a.cfg.verbose,
        .commands = build_commands(&a, &a.cfg, &warn),
    };
    if (!a.sink.commands) {
        kk_log_error("sem memória");
        return 1;
    }

    char sock[KK_PATH_MAX];
    if (socket_path(&a.cfg, sock, sizeof sock)) {
        a.control = kk_control_open(sock, on_bridge, on_request, &a);
        if (a.control)
            kk_log_info("socket de controle em %s", sock);
    }

    /* connect_chats compares with a.cfg: start from "nothing connected". */
    char *youtube = a.cfg.youtube;
    a.cfg.youtube = NULL;
    int rc = connect_chats(&a, &(kk_config){.youtube = youtube, .demo = a.cfg.demo});
    a.cfg.youtube = youtube;
    if (rc == 0)
        run(&a);
    else
        rc = 2;

    kk_control_close(a.control);
    /* The connectors cancel their requests, so they go before the client. */
    kk_youtube_free(a.youtube);
    kk_http_free(a.http);
    kk_commands_free(a.sink.commands);
    kk_soundboard_free(a.sounds);
    if (a.users) {
        kk_users_save(a.users);
        kk_users_free(a.users);
    }
    kk_stage_free(&stage);
    kk_window_close(&win);
    kk_sa_free(&lib);
    kk_config_free(&a.cfg);
    close(a.timer_fd);
    close(a.signal_fd);
    return rc;
}
