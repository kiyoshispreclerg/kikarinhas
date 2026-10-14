/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <errno.h>
#include <getopt.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/signalfd.h>
#include <sys/timerfd.h>
#include <time.h>
#include <unistd.h>

#include "actions.h"
#include "commands.h"
#include "config.h"
#include "control.h"
#include "danmaku.h"
#include "demochat.h"
#include "emotes.h"
#include "emotewall.h"
#include "http.h"
#include "library.h"
#include "log.h"
#include "mpris.h"
#include "overlay.h"
#include "pointer.h"
#include "points.h"
#include "timers.h"
#include "sa.h"
#include "soundboard.h"
#include "stage.h"
#include "twitch.h"
#include "kick.h"
#include "odysee.h"
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
            "                      pode repetir, para ler várias lives juntas\n"
            "                      com um canal, espera ele entrar ao vivo\n"
            "  -t, --twitch CANAL  canal da Twitch (nome ou link twitch.tv/canal);\n"
            "                      pode repetir, e vale junto com -y\n"
            "  -k, --kick CANAL    canal do Kick (nome ou link kick.com/canal)\n"
            "  -o, --odysee ALVO   link da live ou do canal no Odysee, @canal ou\n"
            "                      id da live; com um canal, espera ele entrar ao vivo\n"
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
            "      --sa-dir PASTA|off\n"
            "                      pasta \"data\" do Stream Avatars (padrão: procura\n"
            "                      nas bibliotecas do Steam); off: não usa o SA\n"
            "      --avatar-dir PASTA\n"
            "                      outra pasta de avatares (pode repetir); a\n"
            "                      ~/.local/share/kikarinhas/avatars é lida sempre\n"
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
        OPT_AVATAR_DIR,
    };
    static const struct option longopts[] = {
        {"mode", required_argument, NULL, 'm'},
        {"size", required_argument, NULL, 's'},
        {"fps", required_argument, NULL, 'f'},
        {"count", required_argument, NULL, 'n'},
        {"avatar", required_argument, NULL, 'a'},
        {"sa-dir", required_argument, NULL, OPT_SA_DIR},
        {"avatar-dir", required_argument, NULL, OPT_AVATAR_DIR},
        {"scale", required_argument, NULL, OPT_SCALE},
        {"ground", required_argument, NULL, OPT_GROUND},
        {"seed", required_argument, NULL, OPT_SEED},
        {"list", no_argument, NULL, OPT_LIST},
        {"check", no_argument, NULL, OPT_CHECK},
        {"youtube", required_argument, NULL, 'y'},
        {"twitch", required_argument, NULL, 't'},
        {"kick", required_argument, NULL, 'k'},
        {"odysee", required_argument, NULL, 'o'},
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
    /* Same for -y, -t, -k and -o. */
    bool yt_given = false, tw_given = false, kick_given = false, ody_given = false;
    bool dirs_given = false;                 /* and --avatar-dir */
    int c;
    long v;
    double d;
    optind = 0; /* GNU: start over, this runs more than once */
    while ((c = getopt_long(argc, argv, "m:s:f:n:a:y:t:k:o:d:c:vhV", longopts, NULL)) != -1) {
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
            kk_config_set_str(&cfg->sa_dir, strcmp(optarg, "off") == 0 ? "" : optarg);
            break;
        case OPT_AVATAR_DIR:
            if (!dirs_given)
                kk_config_set_str(&cfg->avatar_paths, NULL);
            dirs_given = true;
            kk_config_append(&cfg->avatar_paths, optarg);
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
            if (!yt_given)
                kk_config_set_str(&cfg->youtube, NULL);
            yt_given = true;
            kk_config_append(&cfg->youtube, optarg);
            break;
        case 't':
            if (!tw_given)
                kk_config_set_str(&cfg->twitch, NULL);
            tw_given = true;
            kk_config_append(&cfg->twitch, optarg);
            break;
        case 'k':
            if (!kick_given)
                kk_config_set_str(&cfg->kick, NULL);
            kick_given = true;
            kk_config_append(&cfg->kick, optarg);
            break;
        case 'o':
            if (!ody_given)
                kk_config_set_str(&cfg->odysee, NULL);
            ody_given = true;
            kk_config_append(&cfg->odysee, optarg);
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
    kk_stage_config cfg = {.scale = 1.0, .max_avatars = 1, .despawn = 1, .seed = 1,
                           .show_names = true, .show_bubbles = true};
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
        if (sh->dedithered)
            printf("info  %s: pontilhado (veio de GIF); cores vizinhas misturadas\n", a->name);
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
 * of interrupting us. SIGPIPE is ignored: our own sockets send with
 * MSG_NOSIGNAL, but this libcurl lets OpenSSL write to its HTTPS sockets
 * itself, and with CURLOPT_NOSIGNAL it no longer ignores SIGPIPE for us, so
 * a reused connection the server had closed would kill the program. */
static int open_signal_fd(void)
{
    signal(SIGPIPE, SIG_IGN);
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
    kk_emotewall *wall;
    kk_danmaku *danmaku;
    kk_timers *timers;
    bool verbose;
} chat_sink;

/* Asked for a command without the points for it: say so over their avatar. */
static void say_poor(chat_sink *sink, kk_avatar *a, const char *key)
{
    kk_actions *ac = sink->actions;
    char text[200];
    snprintf(text, sizeof text, "Custa %ld %s; você tem %ld.",
             kk_commands_last_cost(sink->commands), ac->points_name,
             kk_points_get(ac->points, key));
    kk_stage_help_bubble(sink->stage, a, text, 0, 3, 5.0);
}

static void on_chat(void *ud, const kk_chat_msg *m)
{
    chat_sink *sink = ud;
    kk_emotewall_message(sink->wall, m, now_seconds());
    kk_timers_message(sink->timers);
    kk_avatar *a = kk_stage_chatter(sink->stage, m);
    kk_cmd_result r = KK_CMD_NONE;
    if (a) {
        char key[256];
        snprintf(key, sizeof key, "%s:%s", m->platform, m->user_id);
        /* The level the badges give now; trust is kept apart, by hand. The
         * fake chat is not remembered (see kk_stage_chatter). */
        kk_users *users = sink->stage->cfg.users;
        kk_role role = kk_role_of(m->badges);
        if (users && strcmp(m->platform, "demo") != 0)
            kk_users_set(users, key, KK_USER_ROLE,
                         role == KK_ROLE_ANYONE ? NULL : kk_role_name(role));
        bool demo = strcmp(m->platform, "demo") == 0;
        if (!demo)
            kk_points_message(sink->actions->points, key, m, now_seconds());
        sink->actions->self = a;
        r = kk_commands_handle(sink->commands, m, key, now_seconds());
        if (r == KK_CMD_NONE)
            kk_stage_say(sink->stage, a, m);
        else if (r == KK_CMD_POOR)
            say_poor(sink, a, key);
    }
    if (r == KK_CMD_NONE && kk_danmaku_enabled(sink->danmaku)) {
        kk_rect avatars[256];
        int n = kk_stage_avatar_areas(sink->stage, avatars, 256);
        kk_danmaku_message(sink->danmaku, m, avatars, n, now_seconds());
    }
    if (sink->verbose) {
        static const char *const kinds[] = {"", " [pago]", " [membro]"};
        static const char *const results[] = {"", "  [comando]", "  [em espera]",
                                              "  [sem permissão]", "  [sem pontos]",
                                              "  [nada feito]"};
        fprintf(stderr, "[%s%s%s] %s%s%s%s: %s%s\n", m->platform,
                m->source ? " " : "", m->source ? m->source : "", m->name,
                kinds[m->kind], m->amount ? " " : "", m->amount ? m->amount : "",
                m->text, results[r]);
    }
}

/* A timer's command, as if the channel owner typed it (no cooldown, no
 * cost); bubbles go over someone on screen, picked at random. */
static void run_timer(chat_sink *sink, const char *name, const char *command)
{
    kk_chat_msg m = {.platform = "timer", .user_id = name, .name = "Kikarinhas",
                     .text = command, .badges = KK_BADGE_OWNER};
    char key[128];
    snprintf(key, sizeof key, "timer:%s", name);
    sink->actions->self = kk_stage_random_avatar(sink->stage);
    kk_cmd_result r = kk_commands_handle(sink->commands, &m, key, now_seconds());
    if (sink->verbose || r == KK_CMD_NONE)
        kk_log_info("timer %s: %s%s", name, command,
                    r == KK_CMD_RAN    ? ""
                    : r == KK_CMD_NONE ? " (não é um comando)"
                                       : " (não fez nada)");
}

static void on_reaction(void *ud, const kk_reaction *r)
{
    chat_sink *sink = ud;
    kk_emotewall_reaction(sink->wall, r, now_seconds());
}

static kk_wall_config wall_config(const kk_config *c)
{
    return (kk_wall_config){
        .enabled = c->wall_enabled,
        .duration = c->wall_duration,
        .min_per_message = c->wall_min_message,
        .combo_count = c->wall_combo,
        .combo_window = c->wall_combo_window,
        .max_per_message = c->wall_max_message,
        .max_on_screen = c->wall_max_screen,
        .style = c->wall_style == KK_CONFIG_WALL_FLY      ? KK_WALL_FLY
                 : c->wall_style == KK_CONFIG_WALL_BOUNCE ? KK_WALL_BOUNCE
                                                          : KK_WALL_RISE,
        .reactions = c->wall_reactions,
        .reactions_per_icon = c->wall_reactions_per_icon,
        .blacklist = c->wall_blacklist,
    };
}

static kk_danmaku_config danmaku_config(const kk_config *c)
{
    return (kk_danmaku_config){
        .enabled = c->danmaku_enabled,
        .seconds = c->danmaku_seconds,
        .font = c->danmaku_font,
        .size = c->danmaku_size,
        .color = c->danmaku_color,
        .outline = c->danmaku_outline,
        .paid_color = c->danmaku_paid_color,
        .member_color = c->danmaku_member_color,
        .opacity = c->danmaku_opacity / 100.0,
        .avoid_avatars = c->danmaku_avoid,
        .max_on_screen = c->danmaku_max,
    };
}

/* With danmaku on, chat messages fly across instead of going in bubbles. */
static bool chat_bubbles(const kk_config *c)
{
    return c->show_bubbles && !c->danmaku_enabled;
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

/* One live: a YouTube video or channel, a Twitch or Kick channel, an
 * Odysee stream or channel. */
#define KK_MAX_SOURCES 16

typedef enum {
    PF_YOUTUBE,
    PF_TWITCH,
    PF_KICK,
    PF_ODYSEE,
    N_PLATFORMS,
} platform;

typedef struct {
    platform pf;
    char *target;      /* as listed in the config */
    bool extra_emotes; /* what the Twitch/Kick one was made with */
    kk_youtube *youtube; /* the one of pf; the others NULL */
    kk_twitch *twitch;
    kk_kick *kick;
    kk_odysee *odysee;
} source;

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
    kk_chat_sink chat;    /* what every connector feeds: on_chat, on_reaction */
    kk_emotes *emotes;
    kk_emotewall *wall;
    kk_danmaku *danmaku;  /* chat flying across, when on */
    kk_overlay *overlay;  /* pictures shown by commands */
    kk_props *props;      /* bombs and collectibles */
    kk_pointer *pointer;  /* clicks and drags on the window (OBS mode) */
    kk_mpris *mpris;      /* the desktop's music players; NULL without D-Bus */
    kk_points *points;    /* NULL without a people file */
    kk_timers *timers;
    source sources[KK_MAX_SOURCES]; /* the lives being read */
    int n_sources;
    kk_demochat demo;
    bool demo_on;
    kk_users *users;      /* NULL if not used */
    kk_control *control;  /* NULL if off */
    kk_soundboard *sounds;
    int timer_fd, signal_fd;
    pid_t config_pid; /* kikarinhas-config, while it is running */
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

static bool is_trusted(void *ud, const char *user_key)
{
    return kk_users_trusted(ud, user_key);
}

static kk_commands *build_commands(app *a, const kk_config *cfg, warn_sink *warn)
{
    kk_commands *c = kk_commands_new(a->actions);
    if (!c)
        return NULL;
    kk_actions_register(c, cfg, on_config_warning, warn);
    if (a->users)
        kk_commands_set_trust(c, is_trusted, a->users);
    return c;
}

/* The wanted lives, from the lists in cfg; checked before anything
 * changes, so a typo leaves the running ones alone. */
typedef struct {
    platform pf;
    char target[512];
    bool used; /* a running source already matches it */
} wanted;

static bool target_ok(platform pf, const char *target)
{
    char tmp[512];
    switch (pf) {
    case PF_YOUTUBE:
        return kk_yt_parse_target(target, tmp, sizeof tmp) != KK_YT_TARGET_INVALID;
    case PF_TWITCH:
        return kk_tw_parse_target(target, tmp, sizeof tmp);
    case PF_KICK:
        return kk_kick_parse_target(target, tmp, sizeof tmp);
    default:
        return kk_ody_parse_target(target, tmp, sizeof tmp) != KK_ODY_TARGET_INVALID;
    }
}

static bool add_wanted(wanted *w, int *n, platform pf, const char *target)
{
    static const char *const names[N_PLATFORMS] = {"YouTube", "Twitch", "Kick", "Odysee"};
    static const char *const help[N_PLATFORMS] = {
        "o link da live, do canal ou o @handle",
        "o nome do canal ou o link twitch.tv/canal",
        "o nome do canal ou o link kick.com/canal",
        "o link da live ou do canal, o @canal ou o id da live",
    };
    if (!target_ok(pf, target)) {
        kk_log_error("%s: não entendi \"%s\" (use %s)", names[pf], target, help[pf]);
        return false;
    }
    for (int i = 0; i < *n; i++)
        if (w[i].pf == pf && strcmp(w[i].target, target) == 0)
            return true; /* listed twice */
    if (*n == KK_MAX_SOURCES) {
        kk_log_warn("no máximo %d lives ao mesmo tempo; \"%s\" ficou de fora",
                    KK_MAX_SOURCES, target);
        return true;
    }
    w[*n] = (wanted){.pf = pf};
    snprintf(w[*n].target, sizeof w[*n].target, "%s", target);
    (*n)++;
    return true;
}

static void source_free(source *s)
{
    kk_youtube_free(s->youtube);
    kk_twitch_free(s->twitch);
    kk_kick_free(s->kick);
    kk_odysee_free(s->odysee);
    free(s->target);
    *s = (source){0};
}

/* Only Twitch and Kick read third-party emotes. */
static bool uses_extra_emotes(platform pf)
{
    return pf == PF_TWITCH || pf == PF_KICK;
}

/* Starts and stops chat connectors to match cfg. Lives that stay listed
 * keep their connection. */
static int connect_chats(app *a, const kk_config *cfg)
{
    wanted want[KK_MAX_SOURCES];
    int n_want = 0;
    char item[512];
    const char *lists[N_PLATFORMS] = {cfg->youtube, cfg->twitch, cfg->kick, cfg->odysee};
    for (int pf = 0; pf < N_PLATFORMS; pf++) {
        const char *p = lists[pf] ? lists[pf] : "";
        while (kk_config_next_target(&p, item, sizeof item))
            if (!add_wanted(want, &n_want, (platform)pf, item))
                return -1;
    }

    int kept = 0;
    for (int i = 0; i < a->n_sources; i++) {
        source *s = &a->sources[i];
        int match = -1;
        for (int k = 0; k < n_want && match < 0; k++)
            if (!want[k].used && want[k].pf == s->pf && strcmp(want[k].target, s->target) == 0 &&
                (!uses_extra_emotes(s->pf) || s->extra_emotes == cfg->extra_emotes))
                match = k;
        if (match < 0) {
            source_free(s);
            continue;
        }
        want[match].used = true;
        a->sources[kept++] = *s;
    }
    a->n_sources = kept;

    for (int k = 0; k < n_want; k++) {
        if (want[k].used)
            continue;
        source s = {.pf = want[k].pf, .target = strdup(want[k].target),
                    .extra_emotes = cfg->extra_emotes};
        bool ok = false;
        if (s.target) {
            switch (s.pf) {
            case PF_YOUTUBE:
                ok = (s.youtube = kk_youtube_new(a->http, s.target, &a->chat)) != NULL;
                break;
            case PF_TWITCH:
                ok = (s.twitch = kk_twitch_new(a->http, s.target, &a->chat,
                                               cfg->extra_emotes)) != NULL;
                break;
            case PF_KICK:
                ok = (s.kick = kk_kick_new(a->http, s.target, &a->chat, cfg->extra_emotes)) !=
                     NULL;
                break;
            default:
                ok = (s.odysee = kk_odysee_new(a->http, s.target, &a->chat)) != NULL;
                break;
            }
        }
        if (!ok) {
            source_free(&s);
            return -1;
        }
        a->sources[a->n_sources++] = s;
    }

    if (cfg->demo && !a->demo_on)
        kk_demochat_init(&a->demo, &a->chat, a->seed);
    a->demo_on = cfg->demo;
    return 0;
}

/* The socket a source wants watched, if any. */
static int source_pollfd(const source *s, struct pollfd *fd)
{
    if (s->twitch)
        return kk_twitch_pollfd(s->twitch, fd);
    if (s->kick)
        return kk_kick_pollfd(s->kick, fd);
    if (s->odysee)
        return kk_odysee_pollfd(s->odysee, fd);
    return 0;
}

static void source_tick(source *s, const struct pollfd *fd, double now)
{
    if (s->youtube)
        kk_youtube_tick(s->youtube, now);
    else if (s->twitch)
        kk_twitch_tick(s->twitch, fd, now);
    else if (s->kick)
        kk_kick_tick(s->kick, fd, now);
    else if (s->odysee)
        kk_odysee_tick(s->odysee, fd, now);
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
        {str_differs(old->avatar_paths, cfg.avatar_paths), "paths"},
        {str_differs(old->users, cfg.users), "users"},
        {str_differs(old->socket, cfg.socket), "socket"},
        {str_differs(old->name_font, cfg.name_font) ||
             old->name_size != cfg.name_size,
         "name_font/name_size"},
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
    KEEP(avatar_paths);
    KEEP(users);
    KEEP(socket);
    KEEP(name_font);
    KEEP(name_size);
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
    kk_timers_configure(a->timers, &cfg, now_seconds());
    kk_overlay_clear(a->overlay); /* a changed picture is read again */
    a->sink.verbose = cfg.verbose;
    kk_soundboard_configure(a->sounds, &cfg);
    a->stage->cfg.max_avatars = cfg.max_avatars;
    a->stage->cfg.despawn = cfg.despawn;
    a->stage->cfg.default_avatar = find_default_avatar(a, &cfg, &warn);
    a->stage->cfg.show_names = cfg.show_names;
    a->stage->cfg.name_above = cfg.name_above;
    a->stage->cfg.name_idle = cfg.name_idle_hide ? cfg.name_idle_seconds : 0;
    a->stage->cfg.show_bubbles = chat_bubbles(&cfg);
    a->stage->cfg.bubble_seconds = cfg.bubble_seconds;
    kk_stage_set_bubble_font(a->stage, cfg.bubble_font, cfg.bubble_size);
    kk_emotes_set_size(a->emotes, cfg.wall_size);
    kk_wall_config wc = wall_config(&cfg);
    kk_emotewall_configure(a->wall, &wc);
    kk_danmaku_config dc = danmaku_config(&cfg);
    kk_danmaku_configure(a->danmaku, &dc);
    if (cfg.fps != old->fps)
        set_frame_timer(a->timer_fd, cfg.fps);

    kk_config_free(&a->cfg);
    a->cfg = cfg;
    kk_log_info("configuração recarregada");
    return true;
}

static void refuse(cJSON *reply, const char *error)
{
    cJSON_ReplaceItemInObjectCaseSensitive(reply, "ok", cJSON_CreateFalse());
    cJSON_AddStringToObject(reply, "error", error);
}

/* {"type":"set_user","user":"youtube:UC...","avatar":"pikachu","trusted":true,
 *  "points":100} (each field optional; "set_avatar" is the same; avatar ""
 *  goes back to the one picked for them): from kikarinhas-config,
 * which must not write the people file under our feet. */
static void set_user(app *a, const cJSON *req, cJSON *reply)
{
    const char *key = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(req, "user"));
    const cJSON *avatar = cJSON_GetObjectItemCaseSensitive(req, "avatar");
    const cJSON *trusted = cJSON_GetObjectItemCaseSensitive(req, "trusted");
    const cJSON *points = cJSON_GetObjectItemCaseSensitive(req, "points");
    const char *name = cJSON_GetStringValue(avatar);
    /* "" goes back to the one picked for them (no saved choice). */
    bool reset = name && !name[0];
    const kk_sa_avatar *def = name && !reset ? kk_sa_find(a->lib, name) : NULL;
    const char *error = NULL;
    if (!key || !key[0])
        error = "faltou \"user\"";
    else if (avatar && !def && !reset)
        error = "avatar desconhecido";
    else if (trusted && !cJSON_IsBool(trusted))
        error = "\"trusted\" deve ser true ou false";
    else if (points && (!cJSON_IsNumber(points) || points->valuedouble < 0 ||
                        points->valuedouble > 1e9))
        error = "\"points\" deve ser um número de 0 a 1000000000";
    else if (!a->users)
        error = "sem arquivo de pessoas";
    if (error) {
        refuse(reply, error);
        return;
    }

    if (trusted)
        kk_users_set(a->users, key, KK_USER_TRUSTED, cJSON_IsTrue(trusted) ? "yes" : NULL);
    if (points && a->points)
        kk_points_set(a->points, key, (long)points->valuedouble);
    if (reset) {
        for (int i = 0; i < a->stage->count; i++) {
            kk_avatar *av = &a->stage->avatars[i];
            const kk_sa_avatar *d = kk_stage_default_avatar(a->stage, key);
            if (av->user_id && strcmp(av->user_id, key) == 0 && d && av->def != d)
                kk_stage_set_avatar(a->stage, av, d);
        }
        /* After the change, which saved it: no choice is the point. */
        kk_users_set(a->users, key, KK_USER_AVATAR, NULL);
        kk_users_set(a->users, key, KK_USER_PALETTE, NULL);
        return;
    }
    if (!def)
        return;
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
    } else if (strcmp(type, "set_user") == 0 || strcmp(type, "set_avatar") == 0) {
        set_user(a, req, reply);
    } else if (strcmp(type, "nowplaying") == 0) {
        /* {"ok":true,"text":"Artist — Title","paused":false}; text null if
         * nothing plays. */
        char song[512];
        bool paused = false;
        const char *filter =
            cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(req, "player"));
        if (!a->mpris) {
            cJSON_ReplaceItemInObjectCaseSensitive(reply, "ok", cJSON_CreateFalse());
            cJSON_AddStringToObject(reply, "error", "sem sessão D-Bus");
        } else if (kk_mpris_current(a->mpris, filter, song, sizeof song, &paused)) {
            cJSON_AddStringToObject(reply, "text", song);
            cJSON_AddBoolToObject(reply, "paused", paused);
        } else {
            cJSON_AddNullToObject(reply, "text");
        }
    } else if (strcmp(type, "ping") == 0) {
        cJSON_AddStringToObject(reply, "version", KK_VERSION);
    } else if (strcmp(type, "quit") == 0) {
        a->quit = true;
    } else {
        cJSON_ReplaceItemInObjectCaseSensitive(reply, "ok", cJSON_CreateFalse());
        cJSON_AddStringToObject(reply, "error", "tipo desconhecido");
    }
}

/* False when it is time to quit. */
static bool handle_signals(app *a)
{
    struct signalfd_siginfo si;
    while (read(a->signal_fd, &si, sizeof si) == (ssize_t)sizeof si) {
        if (si.ssi_signo != SIGHUP)
            return false;
        if (!reload(a, NULL))
            kk_log_warn("configuração não aplicada; segue a anterior");
    }
    return true;
}

/* Right click on the window: opens kikarinhas-config on the same .ini. It
 * is looked up next to this executable first, then in the PATH. Only one at
 * a time. */
static void open_config(app *a)
{
    if (a->config_pid > 0) {
        if (waitpid(a->config_pid, NULL, WNOHANG) == 0)
            return; /* still open */
        a->config_pid = 0;
    }
    char self[KK_PATH_MAX], sibling[KK_PATH_MAX] = "";
    ssize_t n = readlink("/proc/self/exe", self, sizeof self - 1);
    if (n > 0) {
        self[n] = '\0';
        char *slash = strrchr(self, '/');
        if (slash) {
            *slash = '\0';
            if (!kk_pathf(sibling, sizeof sibling, "%s/kikarinhas-config", self) ||
                access(sibling, X_OK) != 0)
                sibling[0] = '\0';
        }
    }
    pid_t pid = fork();
    if (pid < 0) {
        kk_log_error("não consegui abrir o configurador: %s", strerror(errno));
        return;
    }
    if (pid == 0) {
        /* Undo what the main loop set up: blocked and ignored signals
         * (an ignored one stays so across exec) and the descriptors (X,
         * sockets, audio). */
        sigset_t none;
        sigemptyset(&none);
        sigprocmask(SIG_SETMASK, &none, NULL);
        signal(SIGPIPE, SIG_DFL);
        for (int fd = 3; fd < 1024; fd++)
            close(fd);
        char *argv[] = {"kikarinhas-config", (char *)a->config_path, NULL};
        if (sibling[0])
            execv(sibling, argv);
        execvp("kikarinhas-config", argv);
        _exit(127);
    }
    a->config_pid = pid;
}

#define MAX_FDS 32

static void run(app *a)
{
    enum { FD_X, FD_TIMER, FD_SIGNAL, FD_FIXED };
    struct pollfd fds[MAX_FDS];
    bool full = true;
    double last = now_seconds();
    double next_save = last + 30.0;
    double next_watch = last + 1.0;

    while (!a->quit) {
        kk_window_events ev = {0};
        kk_window_dispatch(a->win, &ev);
        if (ev.quit)
            return;
        if (ev.open_config)
            open_config(a);
        for (int i = 0; i < ev.n_pointer; i++)
            kk_pointer_feed(a->pointer, &ev.pointer[i]);
        if (ev.resized) {
            kk_stage_resize(a->stage, a->win->width, a->win->height);
            kk_emotewall_resize(a->wall, a->win->width, a->win->height);
            kk_overlay_resize(a->overlay, a->win->width, a->win->height);
            kk_danmaku_resize(a->danmaku, a->win->width, a->win->height);
        }
        if (ev.redraw)
            full = true;

        fds[FD_X] = (struct pollfd){.fd = kk_window_fd(a->win), .events = POLLIN};
        fds[FD_TIMER] = (struct pollfd){.fd = a->timer_fd, .events = POLLIN};
        fds[FD_SIGNAL] = (struct pollfd){.fd = a->signal_fd, .events = POLLIN};
        int nfds = FD_FIXED;
        int n_audio = kk_soundboard_pollfds(a->sounds, fds + nfds, 8);
        nfds += n_audio;
        /* Where each chat socket went, -1 if it has none now. */
        int src_at[KK_MAX_SOURCES];
        for (int i = 0; i < a->n_sources; i++) {
            src_at[i] = -1;
            if (nfds < MAX_FDS && source_pollfd(&a->sources[i], &fds[nfds]))
                src_at[i] = nfds++;
        }
        int bus_at = nfds;
        int room = MAX_FDS - nfds < 4 ? MAX_FDS - nfds : 4;
        int n_bus = a->mpris ? kk_mpris_pollfds(a->mpris, fds + bus_at, room) : 0;
        nfds += n_bus;
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
        if (a->mpris)
            kk_mpris_dispatch(a->mpris, fds + bus_at, n_bus);
        if (a->control)
            kk_control_dispatch(a->control, fds + ctl_at, nfds - ctl_at);

        double t = now_seconds();
        kk_soundboard_pump(a->sounds, fds + FD_FIXED, n_audio, t);
        kk_actions_tick(a->actions, t);
        for (int i = 0; i < a->n_sources; i++)
            source_tick(&a->sources[i], src_at[i] >= 0 ? &fds[src_at[i]] : NULL, t);
        if (a->demo_on)
            kk_demochat_tick(&a->demo, t);
        if (t >= next_watch) {
            /* Everyone whose avatar is up is watching. */
            const char *keys[256];
            int n = 0;
            for (int i = 0; i < a->stage->count && n < 256; i++) {
                const char *k = a->stage->avatars[i].user_id;
                if (k && strncmp(k, "demo:", 5) != 0)
                    keys[n++] = k;
            }
            if (a->points)
                kk_points_watch(a->points, keys, n, t);
            const char *cmds[16], *names[16];
            int due = kk_timers_due(a->timers, t, n, cmds, names, 16);
            for (int i = 0; i < due; i++)
                run_timer(&a->sink, names[i], cmds[i]);
            next_watch = t + 1.0;
        }
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
                kk_props_update(a->props, dt > 0.1 ? 0.1 : dt);
                kk_emotewall_update(a->wall, t);
                kk_overlay_update(a->overlay, t);
                kk_danmaku_update(a->danmaku, t);
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
        count = cfg->n_show || cfg->youtube || cfg->twitch || cfg->kick || cfg->odysee ||
                        cfg->demo
                    ? 0
                    : 6;

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
    if (users && sa_dir[0] && (x->import_sa || first)) {
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

    char sa_dir[KK_PATH_MAX];
    kk_sa_library lib;
    if (kk_library_load(&lib, a.cfg.sa_dir, a.cfg.avatar_paths, sa_dir, sizeof sa_dir,
                        !x.list && !x.check) < 0) {
        char own[KK_PATH_MAX] = "";
        kk_library_own_dir(own, sizeof own);
        kk_log_error("nenhum avatar encontrado: ponha avatares em %s, indique outras "
                     "pastas com --avatar-dir (paths no %s) ou a do Stream Avatars com "
                     "--sa-dir",
                     own, config_path);
        kk_sa_free(&lib);
        kk_config_free(&a.cfg);
        return 1;
    }
    if (!sa_dir[0] && !(a.cfg.sa_dir && !a.cfg.sa_dir[0]))
        kk_log_info("Stream Avatars não encontrado nas bibliotecas do Steam; seguem os "
                    "outros avatares");
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
        .show_names = a.cfg.show_names,
        .name_above = a.cfg.name_above,
        .name_idle = a.cfg.name_idle_hide ? a.cfg.name_idle_seconds : 0,
        .show_bubbles = chat_bubbles(&a.cfg),
        .bubble_seconds = a.cfg.bubble_seconds,
        .name_font = a.cfg.name_font,
        .name_size = a.cfg.name_size,
        .bubble_font = a.cfg.bubble_font,
        .bubble_size = a.cfg.bubble_size,
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
    char emote_dir[KK_PATH_MAX];
    a.emotes = kk_emotes_new(a.http, kk_emotes_default_dir(emote_dir, sizeof emote_dir)
                                         ? emote_dir : NULL,
                             a.cfg.wall_size);
    a.wall = a.emotes ? kk_emotewall_new(a.emotes, x.seed) : NULL;
    kk_stage_set_emotes(a.stage, a.emotes);
    if (!a.wall) {
        kk_log_error("sem memória");
        return 1;
    }
    kk_wall_config wc = wall_config(&a.cfg);
    kk_emotewall_configure(a.wall, &wc);
    kk_emotewall_resize(a.wall, win.width, win.height);
    a.overlay = kk_overlay_new();
    if (!a.overlay) {
        kk_log_error("sem memória");
        return 1;
    }
    kk_overlay_resize(a.overlay, win.width, win.height);
    a.props = kk_props_new(&stage, x.seed);
    if (!a.props) {
        kk_log_error("sem memória");
        return 1;
    }
    a.pointer = kk_pointer_new();
    if (!a.pointer) {
        kk_log_error("sem memória");
        return 1;
    }
    kk_pointer_target avatars_target = kk_stage_pointer_target(&stage);
    kk_pointer_add_target(a.pointer, &avatars_target);
    a.danmaku = kk_danmaku_new(a.emotes, x.seed);
    if (!a.danmaku) {
        kk_log_error("sem memória");
        return 1;
    }
    kk_danmaku_config dc = danmaku_config(&a.cfg);
    kk_danmaku_configure(a.danmaku, &dc);
    kk_danmaku_resize(a.danmaku, win.width, win.height);
    /* Pictures under the bombs and stars, then the danmaku, the emote wall
     * over all. */
    kk_stage_add_layer(&stage, kk_overlay_layer(a.overlay));
    kk_stage_add_layer(&stage, kk_props_layer(a.props));
    kk_stage_add_layer(&stage, kk_danmaku_layer(a.danmaku));
    kk_stage_add_layer(&stage, kk_emotewall_layer(a.wall));

    a.mpris = kk_mpris_new();
    a.points = a.users ? kk_points_new(a.users) : NULL;
    a.timers = kk_timers_new();
    if (!a.timers) {
        kk_log_error("sem memória");
        return 1;
    }
    kk_timers_configure(a.timers, &a.cfg, now_seconds());
    kk_actions actions = {.stage = &stage, .sound = on_sound, .sound_ud = a.sounds,
                          .overlay = a.overlay, .mpris = a.mpris, .points = a.points,
                          .props = a.props, .now = now_seconds()};
    a.actions = &actions;
    kk_props_set_catch(a.props, kk_actions_catch, &actions);
    a.sink = (chat_sink){
        .stage = &stage,
        .actions = &actions,
        .wall = a.wall,
        .danmaku = a.danmaku,
        .verbose = a.cfg.verbose,
        .timers = a.timers,
        .commands = build_commands(&a, &a.cfg, &warn),
    };
    a.chat = (kk_chat_sink){.on_msg = on_chat, .on_reaction = on_reaction, .ud = &a.sink};
    if (!a.sink.commands) {
        kk_log_error("sem memória");
        return 1;
    }

    char sock[KK_PATH_MAX];
    if (socket_path(&a.cfg, sock, sizeof sock)) {
        a.control = kk_control_open(sock, &a.chat, on_request, &a);
        if (a.control)
            kk_log_info("socket de controle em %s", sock);
    }

    int rc = connect_chats(&a, &a.cfg);
    if (rc == 0)
        run(&a);
    else
        rc = 2;

    kk_control_close(a.control);
    /* The connectors cancel their requests, so they go before the client. */
    for (int i = 0; i < a.n_sources; i++)
        source_free(&a.sources[i]);
    kk_stage_remove_layer(&stage, kk_emotewall_layer(a.wall));
    kk_stage_remove_layer(&stage, kk_props_layer(a.props));
    kk_stage_remove_layer(&stage, kk_danmaku_layer(a.danmaku));
    kk_stage_remove_layer(&stage, kk_overlay_layer(a.overlay));
    kk_emotewall_free(a.wall);
    kk_props_free(a.props);
    kk_overlay_free(a.overlay);
    kk_danmaku_free(a.danmaku);
    kk_pointer_free(a.pointer);
    kk_mpris_free(a.mpris);
    /* A roulette round still open: the bets go back. */
    kk_actions_refund(&actions);
    kk_points_free(a.points);
    kk_timers_free(a.timers);
    kk_emotes_free(a.emotes);
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
