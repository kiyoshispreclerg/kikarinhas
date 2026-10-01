/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The INI editor, the config layers and the control socket. */
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include "../harness.h"
#include "actions.h"
#include "commands.h"
#include "config.h"
#include "control.h"
#include "ini.h"
#include "util.h"

static int n_warnings;
static char last_warning[512];
static int last_line;

static void count_warning(void *ud, int line, const char *msg)
{
    (void)ud;
    n_warnings++;
    last_line = line;
    snprintf(last_warning, sizeof last_warning, "%s", msg);
}

/* ---- ini ----------------------------------------------------------------- */

static const char SAMPLE[] =
    "\xEF\xBB\xBF# topo\n"
    "[Window]\n"
    "size = 1920x1080   \r\n"
    "; comentário\n"
    "fps=60\n"
    "\n"
    "# antes do chat\n"
    "[chat]\n"
    "youtube = https://youtu.be/abc?t=1#x\n"
    "isto não é chave\n"
    "verbose = yes\n";

TEST(ini_reads_values_and_reports_bad_lines)
{
    n_warnings = 0;
    kk_ini *ini = kk_ini_parse(SAMPLE, count_warning, NULL);
    CHECK(ini);
    CHECK_INT_EQ(n_warnings, 1);
    CHECK_INT_EQ(last_line, 10);
    CHECK_STR_EQ(kk_ini_get(ini, "window", "SIZE"), "1920x1080");
    CHECK_STR_EQ(kk_ini_get(ini, "window", "fps"), "60");
    CHECK_STR_EQ(kk_ini_get(ini, "chat", "youtube"), "https://youtu.be/abc?t=1#x");
    CHECK_INT_EQ(kk_ini_line(ini, "chat", "verbose"), 11);
    CHECK(kk_ini_get(ini, "chat", "fps") == NULL);

    int it = 0, n = 0;
    const char *s, *k, *v;
    while (kk_ini_next(ini, &it, &s, &k, &v, NULL))
        n++;
    CHECK_INT_EQ(n, 4);
    kk_ini_free(ini);
}

TEST(ini_edits_keep_everything_else)
{
    kk_ini *ini = kk_ini_parse(SAMPLE, NULL, NULL);
    CHECK(ini);
    CHECK_INT_EQ(kk_ini_set(ini, "window", "fps", "30"), 0);   /* in place */
    CHECK_INT_EQ(kk_ini_set(ini, "window", "mode", "desktop"), 0); /* appended */
    CHECK_INT_EQ(kk_ini_set(ini, "command.buzina", "action", "sound"), 0);
    CHECK_INT_EQ(kk_ini_set(ini, "command.buzina", "data", "buzina"), 0);
    kk_ini_unset(ini, "chat", "verbose");
    char *out = kk_ini_dump(ini);
    CHECK_STR_EQ(out,
                 "# topo\n"
                 "[Window]\n"
                 "size = 1920x1080   \n"
                 "; comentário\n"
                 "fps = 30\n"
                 "mode = desktop\n"
                 "\n"
                 "# antes do chat\n"
                 "[chat]\n"
                 "youtube = https://youtu.be/abc?t=1#x\n"
                 "isto não é chave\n"
                 "\n"
                 "[command.buzina]\n"
                 "action = sound\n"
                 "data = buzina\n");
    free(out);

    /* Removing a section keeps the comment that introduces the next one. */
    kk_ini_remove_section(ini, "window");
    out = kk_ini_dump(ini);
    CHECK_STR_EQ(out,
                 "# topo\n"
                 "\n"
                 "# antes do chat\n"
                 "[chat]\n"
                 "youtube = https://youtu.be/abc?t=1#x\n"
                 "isto não é chave\n"
                 "\n"
                 "[command.buzina]\n"
                 "action = sound\n"
                 "data = buzina\n");
    free(out);

    int it = 0, n = 0;
    const char *sec;
    while (kk_ini_next_section(ini, &it, &sec))
        n++;
    CHECK_INT_EQ(n, 2);
    kk_ini_free(ini);
}

TEST(ini_save_is_atomic_and_rereadable)
{
    char dir[] = "/tmp/kk-test-XXXXXX";
    CHECK(mkdtemp(dir));
    char path[KK_PATH_MAX];
    snprintf(path, sizeof path, "%s/sub/kikarinhas.ini", dir);
    kk_ini *ini = kk_ini_new();
    kk_ini_set(ini, "avatars", "scale", "3");
    CHECK_INT_EQ(kk_ini_save(ini, path), 0);
    kk_ini_free(ini);

    char *text = kk_read_file(path, NULL);
    CHECK_STR_EQ(text, "[avatars]\nscale = 3\n");
    free(text);
    char sub[KK_PATH_MAX];
    snprintf(sub, sizeof sub, "%s/sub", dir);
    remove(path);
    rmdir(sub);
    rmdir(dir);
}

/* ---- config -------------------------------------------------------------- */

static kk_config load_text(const char *text)
{
    kk_config c;
    kk_config_defaults(&c);
    n_warnings = 0;
    kk_ini *ini = kk_ini_parse(text, count_warning, NULL);
    kk_config_apply(&c, ini, count_warning, NULL);
    kk_ini_free(ini);
    return c;
}

TEST(config_defaults)
{
    kk_config c;
    kk_config_defaults(&c);
    CHECK(!c.desktop);
    CHECK_INT_EQ(c.width, 1280);
    CHECK_INT_EQ(c.fps, 30);
    CHECK_INT_EQ(c.ground, -1);
    CHECK_INT_EQ(c.max_avatars, 30);
    CHECK(c.shortcuts);
    int n;
    kk_config_default_commands(&n);
    CHECK_INT_EQ(c.n_commands, n);
    kk_config_command *dance = kk_config_find_command(&c, "dance");
    CHECK(dance);
    CHECK_STR_EQ(dance->aliases[1], "dança");
    CHECK(dance->user_cd == 60);
    kk_config_free(&c);
}

TEST(config_reads_every_section)
{
    kk_config c = load_text("[window]\nmode = desktop\nsize = 800x600\nfps = 60\n"
                            "[avatars]\nscale = 1.5\nground = 40\ncount = auto\n"
                            "show = pikachu , crewmate,\ndefault = agnes tachyon\n"
                            "[chat]\nyoutube = @canal\ndemo = sim\nmax = 12\n"
                            "despawn = 90\nverbose = on\nusers = /tmp/u.tsv\n"
                            "[control]\nsocket = off\n"
                            "[commands]\nshortcuts = no\nshortcut_cooldown = 2\n");
    CHECK_INT_EQ(n_warnings, 0);
    CHECK(c.desktop);
    CHECK_INT_EQ(c.width, 800);
    CHECK_INT_EQ(c.height, 600);
    CHECK_INT_EQ(c.fps, 60);
    CHECK(c.scale == 1.5);
    CHECK_INT_EQ(c.ground, 40);
    CHECK_INT_EQ(c.count, -1);
    CHECK_INT_EQ(c.n_show, 2);
    CHECK_STR_EQ(c.show[1], "crewmate");
    CHECK_STR_EQ(c.default_avatar, "agnes tachyon");
    CHECK_STR_EQ(c.youtube, "@canal");
    CHECK(c.demo && c.verbose && !c.shortcuts);
    CHECK_INT_EQ(c.max_avatars, 12);
    CHECK(c.despawn == 90);
    CHECK_STR_EQ(c.socket, "");
    CHECK(c.shortcut_cd == 2);

    kk_config copy;
    CHECK(kk_config_copy(&copy, &c));
    kk_config_free(&c);
    CHECK_STR_EQ(copy.show[0], "pikachu");
    CHECK_STR_EQ(copy.youtube, "@canal");
    CHECK(kk_config_find_command(&copy, "sound"));
    kk_config_free(&copy);
}

TEST(config_bad_values_are_skipped)
{
    kk_config c = load_text("[window]\nfps = 1000\nsize = grande\ncor = azul\n"
                            "[chat]\nmax = 20\n[mistério]\nx = 1\n");
    CHECK_INT_EQ(n_warnings, 4);
    CHECK_INT_EQ(c.fps, 30);       /* kept the default */
    CHECK_INT_EQ(c.width, 1280);
    CHECK_INT_EQ(c.max_avatars, 20); /* the good one still applies */
    CHECK_STR_HAS(last_warning, "mistério");
    CHECK_INT_EQ(last_line, 8);
    kk_config_free(&c);
}

TEST(config_commands)
{
    kk_config c = load_text("[command.dance]\naliases = baila, !BAILAR\ncooldown = 10\n"
                            "role = member\n"
                            "[command.attack]\nenabled = no\n"
                            "[command.Buzina]\naction = sound\ndata = buzina\n"
                            "global_cooldown = 20\n"
                            "[command.sem_acao]\ncooldown = 1\n"
                            "[command.x]\naction = voar\n");
    /* sem_acao and x have no (valid) action. */
    CHECK_INT_EQ(n_warnings, 3);
    kk_config_command *d = kk_config_find_command(&c, "dance");
    CHECK_INT_EQ(d->n_aliases, 2);
    CHECK_STR_EQ(d->aliases[1], "bailar");
    CHECK(d->user_cd == 10);
    CHECK_INT_EQ(d->role, KK_ROLE_MEMBER);
    CHECK(!kk_config_find_command(&c, "attack")->enabled);
    kk_config_command *b = kk_config_find_command(&c, "buzina");
    CHECK(b);
    CHECK_STR_EQ(b->action, "sound");
    CHECK_STR_EQ(b->data, "buzina");
    CHECK(b->global_cd == 20);
    CHECK(kk_config_find_command(&c, "sem_acao") == NULL);
    CHECK(kk_config_find_command(&c, "x") == NULL);
    kk_config_free(&c);
}

TEST(every_config_action_has_a_handler)
{
    for (const char *const *a = kk_config_actions(); *a; a++)
        CHECK_MSG(kk_actions_find(*a) != NULL, "sem handler: %s", *a);
    CHECK(kk_actions_find("voar") == NULL);
}

TEST(config_commands_register)
{
    kk_config c = load_text("[command.dance]\naliases = baila\n"
                            "[command.jump]\naliases = baila\n"
                            "[command.attack]\nenabled = no\n");
    CHECK_INT_EQ(n_warnings, 0);
    kk_actions actions = {0};
    kk_commands *cmds = kk_commands_new(&actions);
    n_warnings = 0;
    /* The second "baila" clashes. */
    CHECK_INT_EQ(kk_actions_register(cmds, &c, count_warning, NULL), 1);
    CHECK_INT_EQ(n_warnings, 1);
    CHECK_STR_HAS(last_warning, "baila");

    /* !attack is gone; with no stage behind it, the shortcut can't run, so
     * it reads as a plain message. */
    kk_chat_msg m = {.platform = "t", .user_id = "u", .name = "u", .text = "!attack"};
    kk_commands_set_fallback(cmds, NULL, 0);
    CHECK_INT_EQ(kk_commands_handle(cmds, &m, "t:u", 0), KK_CMD_NONE);
    kk_commands_free(cmds);
    kk_config_free(&c);
}

TEST(config_load_file)
{
    char dir[] = "/tmp/kk-test-XXXXXX";
    CHECK(mkdtemp(dir));
    char path[KK_PATH_MAX];
    snprintf(path, sizeof path, "%s/k.ini", dir);
    kk_config c;
    kk_config_defaults(&c);
    bool found = true;
    CHECK_INT_EQ(kk_config_load(&c, path, &found, NULL, NULL), 0);
    CHECK(!found);

    FILE *f = fopen(path, "w");
    fputs("[window]\nfps = 15\n", f);
    fclose(f);
    CHECK_INT_EQ(kk_config_load(&c, path, &found, NULL, NULL), 0);
    CHECK(found);
    CHECK_INT_EQ(c.fps, 15);
    kk_config_free(&c);
    remove(path);
    rmdir(dir);
}

/* ---- control ------------------------------------------------------------- */

static int n_msgs;
static char msg_text[256], msg_name[64], msg_platform[32];
static unsigned msg_badges;
static kk_msg_kind msg_kind;

static void on_msg(void *ud, const kk_chat_msg *m)
{
    (void)ud;
    n_msgs++;
    snprintf(msg_text, sizeof msg_text, "%s", m->text);
    snprintf(msg_name, sizeof msg_name, "%s", m->name);
    snprintf(msg_platform, sizeof msg_platform, "%s", m->platform);
    msg_badges = m->badges;
    msg_kind = m->kind;
}

static int n_reloads;

static void on_req(void *ud, const char *type, const cJSON *req, cJSON *reply)
{
    (void)ud;
    (void)req;
    if (strcmp(type, "reload") == 0) {
        n_reloads++;
        cJSON_AddItemToObject(reply, "warnings", cJSON_CreateArray());
    } else {
        cJSON_ReplaceItemInObjectCaseSensitive(reply, "ok", cJSON_CreateFalse());
        cJSON_AddStringToObject(reply, "error", "tipo desconhecido");
    }
}

static char reply_buf[1024];

static const char *ask(kk_control *c, const char *line)
{
    char *r = kk_control_handle_line(c, line);
    snprintf(reply_buf, sizeof reply_buf, "%s", r ? r : "(null)");
    free(r);
    return reply_buf;
}

TEST(control_messages_from_bridges)
{
    kk_control *c = kk_control_open(NULL, on_msg, on_req, NULL);
    CHECK(c);
    n_msgs = 0;
    CHECK_STR_EQ(ask(c, "{\"type\":\"message\",\"platform\":\"twitch\",\"user_id\":\"42\","
                        "\"name\":\"Fulana\",\"text\":\"oi !jump\","
                        "\"badges\":[\"subscriber\",\"moderator\",\"???\"]}"),
                 "{\"ok\":true}");
    CHECK_INT_EQ(n_msgs, 1);
    CHECK_STR_EQ(msg_platform, "twitch");
    CHECK_STR_EQ(msg_name, "Fulana");
    CHECK_STR_EQ(msg_text, "oi !jump");
    CHECK_INT_EQ(msg_badges, KK_BADGE_MEMBER | KK_BADGE_MOD);
    CHECK_INT_EQ(msg_kind, KK_MSG_TEXT);

    /* Defaults: platform "bridge", name = id. */
    ask(c, "{\"type\":\"message\",\"user_id\":\"x\",\"kind\":\"paid\",\"amount\":\"R$ 5\"}");
    CHECK_INT_EQ(n_msgs, 2);
    CHECK_STR_EQ(msg_platform, "bridge");
    CHECK_STR_EQ(msg_name, "x");
    CHECK_INT_EQ(msg_kind, KK_MSG_PAID);

    CHECK_STR_HAS(ask(c, "{\"type\":\"message\",\"text\":\"sem id\"}"), "user_id");
    CHECK_STR_HAS(ask(c, "{\"type\":\"message\",\"user_id\":\"x\",\"kind\":\"?\"}"),
                  "\"ok\":false");
    CHECK_INT_EQ(n_msgs, 2);
    kk_control_close(c);
}

TEST(control_requests)
{
    kk_control *c = kk_control_open(NULL, on_msg, on_req, NULL);
    n_reloads = 0;
    CHECK_STR_EQ(ask(c, "{\"type\":\"reload\"}"), "{\"ok\":true,\"warnings\":[]}");
    CHECK_STR_EQ(ask(c, "  reload"), "{\"ok\":true,\"warnings\":[]}");
    CHECK_INT_EQ(n_reloads, 2);
    CHECK_STR_HAS(ask(c, "voar"), "tipo desconhecido");
    CHECK_STR_HAS(ask(c, "{\"type\":"), "JSON inválido");
    CHECK_STR_HAS(ask(c, "[1,2]"), "\"ok\":false");
    CHECK_STR_HAS(ask(c, "{\"tipo\":\"reload\"}"), "faltou");
    kk_control_close(c);
}

static int connect_to(const char *path)
{
    struct sockaddr_un sa = {.sun_family = AF_UNIX};
    kk_pathf(sa.sun_path, sizeof sa.sun_path, "%s", path);
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd >= 0 && connect(fd, (struct sockaddr *)&sa, sizeof sa) < 0) {
        close(fd);
        return -1;
    }
    return fd;
}

/* One round of the main loop's poll + dispatch. */
static void pump(kk_control *c)
{
    for (int round = 0; round < 3; round++) {
        struct pollfd fds[32];
        int n = kk_control_pollfds(c, fds, 32);
        poll(fds, (nfds_t)n, 50);
        kk_control_dispatch(c, fds, n);
    }
}

TEST(control_socket_end_to_end)
{
    char dir[] = "/tmp/kk-test-XXXXXX";
    CHECK(mkdtemp(dir));
    char path[KK_PATH_MAX];
    snprintf(path, sizeof path, "%s/k.sock", dir);

    kk_control *c = kk_control_open(path, on_msg, on_req, NULL);
    CHECK(c);
    /* A second instance must not steal the socket. */
    CHECK(kk_control_open(path, on_msg, on_req, NULL) == NULL);

    int fd = connect_to(path);
    CHECK(fd >= 0);
    pump(c); /* accept */
    n_msgs = 0;
    /* Two requests, the second split across writes. */
    const char *part1 = "{\"type\":\"message\",\"user_id\":\"1\",\"text\":\"a\"}\n{\"type\":";
    const char *part2 = "\"reload\"}\n";
    CHECK(write(fd, part1, strlen(part1)) > 0);
    pump(c);
    CHECK(write(fd, part2, strlen(part2)) > 0);
    pump(c);
    char buf[512] = "";
    size_t len = 0;
    while (len < sizeof buf - 1) {
        struct pollfd p = {.fd = fd, .events = POLLIN};
        if (poll(&p, 1, 100) <= 0)
            break;
        ssize_t r = read(fd, buf + len, sizeof buf - 1 - len);
        if (r <= 0)
            break;
        len += (size_t)r;
    }
    buf[len] = '\0';
    CHECK_STR_EQ(buf, "{\"ok\":true}\n{\"ok\":true,\"warnings\":[]}\n");
    CHECK_INT_EQ(n_msgs, 1);
    close(fd);
    pump(c); /* notices the hang-up */

    kk_control_close(c);
    CHECK(!kk_file_exists(path));
    /* Nobody listening now. */
    char reply[64];
    CHECK_INT_EQ(kk_control_request(path, "ping", reply, sizeof reply, 100), -1);

    /* A stale socket file is replaced. */
    int stale = socket(AF_UNIX, SOCK_STREAM, 0);
    struct sockaddr_un sa = {.sun_family = AF_UNIX};
    kk_pathf(sa.sun_path, sizeof sa.sun_path, "%s", path);
    CHECK(bind(stale, (struct sockaddr *)&sa, sizeof sa) == 0);
    close(stale);
    c = kk_control_open(path, on_msg, on_req, NULL);
    CHECK(c);
    kk_control_close(c);

    /* Never replaces something that is not a socket. */
    FILE *f = fopen(path, "w");
    fclose(f);
    CHECK(kk_control_open(path, on_msg, on_req, NULL) == NULL);
    CHECK(kk_file_exists(path));
    remove(path);
    rmdir(dir);
}

int main(void)
{
    RUN(ini_reads_values_and_reports_bad_lines);
    RUN(ini_edits_keep_everything_else);
    RUN(ini_save_is_atomic_and_rereadable);
    RUN(config_defaults);
    RUN(config_reads_every_section);
    RUN(config_bad_values_are_skipped);
    RUN(config_commands);
    RUN(every_config_action_has_a_handler);
    RUN(config_commands_register);
    RUN(config_load_file);
    RUN(control_messages_from_bridges);
    RUN(control_requests);
    RUN(control_socket_end_to_end);
    return harness_report();
}
