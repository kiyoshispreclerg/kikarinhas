/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "control.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

#include "log.h"
#include "util.h"

#define MAX_CLIENTS 16
#define MAX_LINE 65536

typedef struct {
    int fd;
    char *buf; /* MAX_LINE bytes */
    size_t len;
} client;

struct kk_control {
    int fd; /* -1 without a socket (tests) */
    char path[sizeof(((struct sockaddr_un *)0)->sun_path)];
    kk_chat_cb on_msg;
    kk_control_fn on_request;
    void *ud;
    client clients[MAX_CLIENTS];
    int n_clients;
};

static bool set_flags(int fd)
{
    int fl = fcntl(fd, F_GETFL);
    return fl >= 0 && fcntl(fd, F_SETFL, fl | O_NONBLOCK) == 0 &&
           fcntl(fd, F_SETFD, FD_CLOEXEC) == 0;
}

static bool make_addr(struct sockaddr_un *sa, const char *path)
{
    memset(sa, 0, sizeof *sa);
    sa->sun_family = AF_UNIX;
    return kk_pathf(sa->sun_path, sizeof sa->sun_path, "%s", path);
}

/* True if something accepts connections at path. */
static bool someone_listens(const struct sockaddr_un *sa)
{
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0)
        return false;
    bool yes = connect(fd, (const struct sockaddr *)sa, sizeof *sa) == 0;
    close(fd);
    return yes;
}

kk_control *kk_control_open(const char *path, kk_chat_cb on_msg,
                            kk_control_fn on_request, void *ud)
{
    kk_control *c = calloc(1, sizeof *c);
    if (!c)
        return NULL;
    c->fd = -1;
    c->on_msg = on_msg;
    c->on_request = on_request;
    c->ud = ud;
    if (!path)
        return c;

    struct sockaddr_un sa;
    if (!make_addr(&sa, path)) {
        kk_log_error("caminho do socket comprido demais: %s", path);
        free(c);
        return NULL;
    }
    struct stat st;
    if (lstat(path, &st) == 0) {
        if (!S_ISSOCK(st.st_mode)) {
            kk_log_error("%s existe e não é um socket; não vou mexer nele", path);
            free(c);
            return NULL;
        }
        if (someone_listens(&sa)) {
            kk_log_error("outro kikarinhas já usa o socket %s", path);
            free(c);
            return NULL;
        }
        unlink(path); /* left behind by a crash */
    }

    kk_make_parent_dirs(path);
    c->fd = socket(AF_UNIX, SOCK_STREAM, 0);
    /* Only this user may connect: the socket can make avatars talk. */
    mode_t old = umask(0177);
    bool ok = c->fd >= 0 && set_flags(c->fd) &&
              bind(c->fd, (struct sockaddr *)&sa, sizeof sa) == 0;
    umask(old);
    if (!ok || listen(c->fd, 8) < 0) {
        kk_log_error("socket %s: %s", path, strerror(errno));
        if (c->fd >= 0)
            close(c->fd);
        free(c);
        return NULL;
    }
    snprintf(c->path, sizeof c->path, "%s", path);
    return c;
}

static void drop_client(kk_control *c, int i)
{
    close(c->clients[i].fd);
    free(c->clients[i].buf);
    c->clients[i] = c->clients[--c->n_clients];
}

void kk_control_close(kk_control *c)
{
    if (!c)
        return;
    while (c->n_clients)
        drop_client(c, 0);
    if (c->fd >= 0) {
        close(c->fd);
        unlink(c->path);
    }
    free(c);
}

int kk_control_pollfds(const kk_control *c, struct pollfd *fds, int max)
{
    int n = 0;
    if (c->fd >= 0 && n < max)
        fds[n++] = (struct pollfd){.fd = c->fd, .events = POLLIN};
    for (int i = 0; i < c->n_clients && n < max; i++)
        fds[n++] = (struct pollfd){.fd = c->clients[i].fd, .events = POLLIN};
    return n;
}

/* ---- requests ------------------------------------------------------------ */

static const char *str_or(const cJSON *req, const char *key, const char *dflt)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(req, key);
    return cJSON_IsString(v) ? v->valuestring : dflt;
}

static void fail(cJSON *reply, const char *error)
{
    cJSON_ReplaceItemInObjectCaseSensitive(reply, "ok", cJSON_CreateFalse());
    cJSON_AddStringToObject(reply, "error", error);
}

static void handle_message(kk_control *c, const cJSON *req, cJSON *reply)
{
    kk_chat_msg m = {
        .platform = str_or(req, "platform", "bridge"),
        .user_id = str_or(req, "user_id", NULL),
        .text = str_or(req, "text", ""),
        .amount = str_or(req, "amount", NULL),
    };
    if (!m.user_id || !m.user_id[0] || !m.platform[0]) {
        fail(reply, "faltou \"user_id\"");
        return;
    }
    m.name = str_or(req, "name", "");
    if (!m.name[0])
        m.name = m.user_id;
    const char *kind = str_or(req, "kind", "text");
    if (strcmp(kind, "paid") == 0)
        m.kind = KK_MSG_PAID;
    else if (strcmp(kind, "member") == 0)
        m.kind = KK_MSG_MEMBER;
    else if (strcmp(kind, "text") != 0) {
        fail(reply, "\"kind\" deve ser text, paid ou member");
        return;
    }
    static const struct {
        const char *name;
        unsigned bit;
    } badges[] = {
        {"owner", KK_BADGE_OWNER}, {"broadcaster", KK_BADGE_OWNER},
        {"mod", KK_BADGE_MOD},     {"moderator", KK_BADGE_MOD},
        {"member", KK_BADGE_MEMBER}, {"subscriber", KK_BADGE_MEMBER},
        {"verified", KK_BADGE_VERIFIED},
    };
    const cJSON *b;
    cJSON_ArrayForEach(b, cJSON_GetObjectItemCaseSensitive(req, "badges"))
        for (size_t i = 0; cJSON_IsString(b) && i < sizeof badges / sizeof badges[0]; i++)
            if (strcasecmp(b->valuestring, badges[i].name) == 0)
                m.badges |= badges[i].bit;
    if (c->on_msg)
        c->on_msg(c->ud, &m);
}

char *kk_control_handle_line(kk_control *c, const char *line)
{
    cJSON *reply = cJSON_CreateObject();
    if (!reply)
        return NULL;
    cJSON_AddTrueToObject(reply, "ok");

    while (*line == ' ' || *line == '\t')
        line++;
    cJSON *req = NULL;
    if (*line == '{') {
        req = cJSON_Parse(line);
    } else {
        /* A bare word, as typed in a terminal. */
        char word[32];
        size_t n = strcspn(line, " \t\r");
        if (n && n < sizeof word) {
            memcpy(word, line, n);
            word[n] = '\0';
            req = cJSON_CreateObject();
            if (req)
                cJSON_AddStringToObject(req, "type", word);
        }
    }

    const char *type = req ? str_or(req, "type", NULL) : NULL;
    if (!req || !cJSON_IsObject(req))
        fail(reply, "JSON inválido");
    else if (!type)
        fail(reply, "faltou \"type\"");
    else if (strcmp(type, "message") == 0)
        handle_message(c, req, reply);
    else if (c->on_request)
        c->on_request(c->ud, type, req, reply);
    else
        fail(reply, "tipo desconhecido");

    char *out = cJSON_PrintUnformatted(reply);
    cJSON_Delete(req);
    cJSON_Delete(reply);
    return out;
}

/* ---- clients ------------------------------------------------------------- */

static void send_line(int fd, const char *s)
{
    /* Best effort: a full socket buffer means the client isn't reading. */
    size_t n = strlen(s);
    if (send(fd, s, n, MSG_NOSIGNAL | MSG_DONTWAIT) == (ssize_t)n)
        send(fd, "\n", 1, MSG_NOSIGNAL | MSG_DONTWAIT);
}

static void accept_clients(kk_control *c)
{
    for (;;) {
        int fd = accept(c->fd, NULL, NULL);
        if (fd < 0)
            return;
        if (c->n_clients == MAX_CLIENTS || !set_flags(fd)) {
            send_line(fd, "{\"ok\":false,\"error\":\"conexões demais\"}");
            close(fd);
            continue;
        }
        char *buf = malloc(MAX_LINE);
        if (!buf) {
            close(fd);
            continue;
        }
        c->clients[c->n_clients++] = (client){.fd = fd, .buf = buf};
    }
}

/* Reads what is there and answers each complete line. False to drop the
 * client (closed, error, or a line too long). */
static bool serve(kk_control *c, client *cl)
{
    for (;;) {
        ssize_t r = recv(cl->fd, cl->buf + cl->len, MAX_LINE - 1 - cl->len, 0);
        if (r == 0)
            return false;
        if (r < 0)
            return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR;
        cl->len += (size_t)r;
        cl->buf[cl->len] = '\0';

        char *start = cl->buf, *nl;
        while ((nl = strchr(start, '\n'))) {
            *nl = '\0';
            if (nl > start && nl[-1] == '\r')
                nl[-1] = '\0';
            if (*start) {
                char *reply = kk_control_handle_line(c, start);
                if (reply)
                    send_line(cl->fd, reply);
                free(reply);
            }
            start = nl + 1;
        }
        cl->len -= (size_t)(start - cl->buf);
        memmove(cl->buf, start, cl->len);
        if (cl->len == MAX_LINE - 1) {
            send_line(cl->fd, "{\"ok\":false,\"error\":\"linha comprida demais\"}");
            return false;
        }
    }
}

void kk_control_dispatch(kk_control *c, const struct pollfd *fds, int n)
{
    for (int k = 0; k < n; k++) {
        if (!fds[k].revents)
            continue;
        if (fds[k].fd == c->fd) {
            accept_clients(c);
            continue;
        }
        for (int i = 0; i < c->n_clients; i++)
            if (c->clients[i].fd == fds[k].fd) {
                if (!serve(c, &c->clients[i]))
                    drop_client(c, i);
                break;
            }
    }
}

/* ---- paths and the client side ------------------------------------------- */

bool kk_control_default_path(char *out, size_t size)
{
    const char *run = getenv("XDG_RUNTIME_DIR");
    if (run && run[0] == '/')
        return kk_pathf(out, size, "%s/kikarinhas.sock", run);
    return kk_pathf(out, size, "/tmp/kikarinhas-%u.sock", (unsigned)getuid());
}

static double now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1e3 + (double)ts.tv_nsec / 1e6;
}

int kk_control_request(const char *path, const char *line, char *reply,
                       size_t size, int timeout_ms)
{
    struct sockaddr_un sa;
    if (!make_addr(&sa, path) || size == 0) {
        errno = ENAMETOOLONG;
        return -1;
    }
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0)
        return -1;
    if (connect(fd, (struct sockaddr *)&sa, sizeof sa) < 0 ||
        send(fd, line, strlen(line), MSG_NOSIGNAL) < 0 ||
        send(fd, "\n", 1, MSG_NOSIGNAL) < 0) {
        int e = errno;
        close(fd);
        errno = e;
        return -1;
    }
    size_t len = 0;
    double deadline = now_ms() + timeout_ms;
    reply[0] = '\0';
    for (;;) {
        int left = (int)(deadline - now_ms());
        struct pollfd p = {.fd = fd, .events = POLLIN};
        if (left <= 0 || poll(&p, 1, left) <= 0)
            break;
        ssize_t r = recv(fd, reply + len, size - 1 - len, 0);
        if (r <= 0)
            break;
        len += (size_t)r;
        reply[len] = '\0';
        char *nl = strchr(reply, '\n');
        if (nl) {
            *nl = '\0';
            close(fd);
            return 0;
        }
        if (len == size - 1)
            break;
    }
    close(fd);
    errno = ETIMEDOUT;
    return -1;
}
