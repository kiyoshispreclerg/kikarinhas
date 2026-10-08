/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "ws.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/sha.h>

#include "tls.h"
#include "util.h"

#define GUID "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"
/* Chat events are a few KB; anything this big is not one. */
#define MAX_MESSAGE (1u << 20)
#define MAX_HEAD 8192
#define UPGRADE_TIMEOUT_S 20.0
/* Quiet this long: we ping... */
#define IDLE_PING_S 40.0
/* ...and this long: the connection is dead. */
#define IDLE_DROP_S 80.0

/* ---- framing ------------------------------------------------------------- */

bool kk_ws_parse_url(const char *url, char *host, size_t host_size, int *port, char *path,
                     size_t path_size)
{
    if (strncmp(url, "wss://", 6) != 0)
        return false;
    const char *h = url + 6;
    size_t hn = strcspn(h, ":/?#");
    if (hn == 0 || hn >= host_size)
        return false;
    for (size_t i = 0; i < hn; i++)
        if (!isalnum((unsigned char)h[i]) && h[i] != '-' && h[i] != '.')
            return false;
    memcpy(host, h, hn);
    host[hn] = '\0';
    const char *p = h + hn;
    *port = 443;
    if (*p == ':') {
        char *end;
        long v = strtol(p + 1, &end, 10);
        if (end == p + 1 || v < 1 || v > 65535)
            return false;
        *port = (int)v;
        p = end;
    }
    if (*p == '#' || strpbrk(p, " \r\n#"))
        return false;
    int n = snprintf(path, path_size, "%s%s", *p == '/' ? "" : "/", p);
    return n > 0 && (size_t)n < path_size;
}

void kk_ws_accept_for(const char *key, char *out, size_t size)
{
    char buf[128];
    unsigned char sha[SHA_DIGEST_LENGTH];
    int n = snprintf(buf, sizeof buf, "%s" GUID, key);
    if (size < 29 || n < 0 || (size_t)n >= sizeof buf) {
        if (size)
            out[0] = '\0';
        return;
    }
    SHA1((const unsigned char *)buf, (size_t)n, sha);
    EVP_EncodeBlock((unsigned char *)out, sha, SHA_DIGEST_LENGTH);
}

/* Value of header name in head (after the status line), trimmed; false if
 * absent. */
static bool header(const char *head, const char *name, char *out, size_t size)
{
    size_t nlen = strlen(name);
    const char *p = strstr(head, "\r\n");
    while (p) {
        p += 2;
        if (strncasecmp(p, name, nlen) == 0 && p[nlen] == ':') {
            const char *v = p + nlen + 1;
            while (*v == ' ' || *v == '\t')
                v++;
            size_t vn = strcspn(v, "\r\n");
            while (vn > 0 && (v[vn - 1] == ' ' || v[vn - 1] == '\t'))
                vn--;
            if (vn >= size)
                return false;
            memcpy(out, v, vn);
            out[vn] = '\0';
            return true;
        }
        p = strstr(p, "\r\n");
    }
    return false;
}

const char *kk_ws_check_upgrade(const char *head, const char *key)
{
    static char why[96];
    if (strncmp(head, "HTTP/1.1 ", 9) != 0)
        return "resposta que não é HTTP";
    if (strncmp(head + 9, "101", 3) != 0) {
        snprintf(why, sizeof why, "o servidor recusou (HTTP %.3s)", head + 9);
        return why;
    }
    char v[128], want[64];
    if (!header(head, "Upgrade", v, sizeof v) || strcasecmp(v, "websocket") != 0)
        return "o servidor não passou para websocket";
    kk_ws_accept_for(key, want, sizeof want);
    if (!header(head, "Sec-WebSocket-Accept", v, sizeof v) || strcmp(v, want) != 0)
        return "Sec-WebSocket-Accept errado";
    return NULL;
}

long kk_ws_parse_frame(unsigned char *buf, size_t n, size_t max, kk_ws_frame *f)
{
    if (n < 2)
        return 0;
    if (buf[0] & 0x70)
        return -1; /* reserved bits: no extension was agreed */
    f->fin = buf[0] & 0x80;
    f->opcode = buf[0] & 0x0F;
    bool masked = buf[1] & 0x80;
    uint64_t len = buf[1] & 0x7F;
    size_t at = 2;
    if (len == 126) {
        if (n < 4)
            return 0;
        len = (uint64_t)buf[2] << 8 | buf[3];
        at = 4;
    } else if (len == 127) {
        if (n < 10)
            return 0;
        len = 0;
        for (int i = 0; i < 8; i++)
            len = len << 8 | buf[2 + i];
        at = 10;
    }
    bool control = f->opcode & 0x8;
    if (len > max || (control && (len > 125 || !f->fin)))
        return -1;
    unsigned char mask[4] = {0};
    if (masked) {
        if (n < at + 4)
            return 0;
        memcpy(mask, buf + at, 4);
        at += 4;
    }
    if (n - at < len)
        return 0;
    f->payload = buf + at;
    f->len = (size_t)len;
    if (masked)
        for (size_t i = 0; i < f->len; i++)
            f->payload[i] ^= mask[i & 3];
    return (long)(at + f->len);
}

size_t kk_ws_build_frame(int opcode, const void *payload, size_t len,
                         const unsigned char mask[4], unsigned char *out)
{
    size_t at = 0;
    out[at++] = (unsigned char)(0x80 | (opcode & 0x0F));
    if (len < 126) {
        out[at++] = (unsigned char)(0x80 | len);
    } else if (len <= 0xFFFF) {
        out[at++] = 0x80 | 126;
        out[at++] = (unsigned char)(len >> 8);
        out[at++] = (unsigned char)len;
    } else {
        out[at++] = 0x80 | 127;
        for (int i = 7; i >= 0; i--)
            out[at++] = (unsigned char)((uint64_t)len >> (8 * i));
    }
    memcpy(out + at, mask, 4);
    at += 4;
    const unsigned char *p = payload;
    for (size_t i = 0; i < len; i++)
        out[at + i] = p[i] ^ mask[i & 3];
    return at + len;
}

/* ---- client -------------------------------------------------------------- */

typedef enum {
    ST_CLOSED,
    ST_TLS,     /* connecting, handshake */
    ST_UPGRADE, /* GET sent, waiting for the 101 */
    ST_OPEN,
} state;

struct kk_ws {
    kk_tls *tls;
    kk_ws_cb cb;
    void *ud;

    char host[256], path[1024];
    char key[32];
    state st;
    double now, since, last_rx;
    bool pinged;

    unsigned char *in; /* bytes not yet taken as frames (or the head) */
    size_t in_len, in_cap;
    char *msg; /* a fragmented message being joined */
    size_t msg_len, msg_cap;
    int msg_opcode; /* 0: none in progress */

    char error[160]; /* why we gave up, from inside a callback */
    bool failed;
};

static void fail(kk_ws *ws, const char *fmt, ...) __attribute__((format(printf, 2, 3)));

static void fail(kk_ws *ws, const char *fmt, ...)
{
    if (ws->failed)
        return;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(ws->error, sizeof ws->error, fmt, ap);
    va_end(ap);
    ws->failed = true;
}

static bool send_frame(kk_ws *ws, int opcode, const void *data, size_t len)
{
    unsigned char mask[4];
    if (RAND_bytes(mask, sizeof mask) != 1)
        memset(mask, 0x5A, sizeof mask);
    unsigned char small[256];
    unsigned char *out = len + 14 <= sizeof small ? small : malloc(len + 14);
    if (!out)
        return false;
    size_t n = kk_ws_build_frame(opcode, data, len, mask, out);
    bool ok = kk_tls_write(ws->tls, out, n);
    if (out != small)
        free(out);
    return ok;
}

static bool grow(void *pbuf, size_t *cap, size_t need)
{
    char **buf = pbuf;
    if (need <= *cap)
        return true;
    size_t c = *cap ? *cap : 4096;
    while (c < need)
        c *= 2;
    char *b = realloc(*buf, c);
    if (!b)
        return false;
    *buf = b;
    *cap = c;
    return true;
}

static void deliver(kk_ws *ws, char *text, size_t len)
{
    if (ws->cb.on_message)
        ws->cb.on_message(ws->ud, text, len);
}

static void on_frame(kk_ws *ws, const kk_ws_frame *f)
{
    switch (f->opcode) {
    case KK_WS_PING:
        send_frame(ws, KK_WS_PONG, f->payload, f->len);
        return;
    case KK_WS_PONG:
        return;
    case KK_WS_CLOSE: {
        int code = f->len >= 2 ? f->payload[0] << 8 | f->payload[1] : 0;
        send_frame(ws, KK_WS_CLOSE, f->payload, f->len >= 2 ? 2 : 0);
        if (f->len > 2)
            fail(ws, "o servidor fechou (%d: %.*s)", code, (int)(f->len - 2 > 100 ? 100 : f->len - 2),
                 (const char *)f->payload + 2);
        else
            fail(ws, "o servidor fechou (%d)", code);
        return;
    }
    case KK_WS_TEXT:
    case KK_WS_BINARY:
        if (ws->msg_opcode) {
            fail(ws, "mensagem nova no meio de outra");
            return;
        }
        if (f->fin) {
            /* The frame's own bytes: the byte after it is borrowed for
             * the NUL and put back. */
            char *t = (char *)f->payload;
            char saved = t[f->len];
            t[f->len] = '\0';
            deliver(ws, t, f->len);
            t[f->len] = saved;
            return;
        }
        ws->msg_opcode = f->opcode;
        ws->msg_len = 0;
        /* fall through */
    case KK_WS_CONT:
        if (!ws->msg_opcode) {
            fail(ws, "continuação sem mensagem");
            return;
        }
        if (ws->msg_len + f->len > MAX_MESSAGE ||
            !grow(&ws->msg, &ws->msg_cap, ws->msg_len + f->len + 1)) {
            fail(ws, "mensagem grande demais");
            return;
        }
        memcpy(ws->msg + ws->msg_len, f->payload, f->len);
        ws->msg_len += f->len;
        if (f->fin) {
            ws->msg[ws->msg_len] = '\0';
            ws->msg_opcode = 0;
            deliver(ws, ws->msg, ws->msg_len);
        }
        return;
    default:
        fail(ws, "frame desconhecido (%d)", f->opcode);
    }
}

/* Frames out of what arrived. */
static void take_frames(kk_ws *ws)
{
    size_t at = 0;
    while (!ws->failed && ws->st == ST_OPEN) {
        kk_ws_frame f;
        /* One spare byte after the frame for its NUL (see on_frame). */
        long n = kk_ws_parse_frame(ws->in + at, ws->in_len - at, MAX_MESSAGE, &f);
        if (n == 0)
            break;
        if (n < 0) {
            fail(ws, "frame inválido");
            break;
        }
        on_frame(ws, &f);
        at += (size_t)n;
    }
    memmove(ws->in, ws->in + at, ws->in_len - at);
    ws->in_len -= at;
}

/* The 101 and its headers; what follows them is already frames. */
static void take_head(kk_ws *ws)
{
    unsigned char *end = NULL;
    for (size_t i = 3; i < ws->in_len && !end; i++)
        if (memcmp(ws->in + i - 3, "\r\n\r\n", 4) == 0)
            end = ws->in + i + 1;
    if (!end) {
        if (ws->in_len > MAX_HEAD)
            fail(ws, "cabeçalho grande demais");
        return;
    }
    char head[MAX_HEAD + 1];
    size_t hn = (size_t)(end - ws->in);
    if (hn > MAX_HEAD) {
        fail(ws, "cabeçalho grande demais");
        return;
    }
    memcpy(head, ws->in, hn);
    head[hn] = '\0';
    const char *why = kk_ws_check_upgrade(head, ws->key);
    if (why) {
        fail(ws, "%s", why);
        return;
    }
    memmove(ws->in, end, ws->in_len - hn);
    ws->in_len -= hn;
    ws->st = ST_OPEN;
    ws->last_rx = ws->now;
    if (ws->cb.on_open)
        ws->cb.on_open(ws->ud);
}

static void on_tls_data(void *ud, char *data, size_t len)
{
    kk_ws *ws = ud;
    if (ws->failed)
        return;
    ws->last_rx = ws->now;
    ws->pinged = false;
    /* +1: room for the NUL a frame borrows. */
    if (ws->in_len + len > MAX_MESSAGE + MAX_HEAD ||
        !grow(&ws->in, &ws->in_cap, ws->in_len + len + 1)) {
        fail(ws, "dados demais de uma vez");
        return;
    }
    memcpy(ws->in + ws->in_len, data, len);
    ws->in_len += len;
    if (ws->st == ST_UPGRADE)
        take_head(ws);
    if (ws->st == ST_OPEN)
        take_frames(ws);
}

static void on_tls_open(void *ud)
{
    kk_ws *ws = ud;
    unsigned char raw[16];
    if (RAND_bytes(raw, sizeof raw) != 1) {
        fail(ws, "sem números aleatórios");
        return;
    }
    EVP_EncodeBlock((unsigned char *)ws->key, raw, sizeof raw);
    char req[2048];
    int n = snprintf(req, sizeof req,
                     "GET %s HTTP/1.1\r\n"
                     "Host: %s\r\n"
                     "Upgrade: websocket\r\n"
                     "Connection: Upgrade\r\n"
                     "Sec-WebSocket-Key: %s\r\n"
                     "Sec-WebSocket-Version: 13\r\n"
                     "User-Agent: kikarinhas/" KK_VERSION "\r\n"
                     "\r\n",
                     ws->path, ws->host, ws->key);
    if (n < 0 || (size_t)n >= sizeof req || !kk_tls_write(ws->tls, req, (size_t)n)) {
        fail(ws, "não consegui pedir o websocket");
        return;
    }
    ws->st = ST_UPGRADE;
    ws->since = ws->now;
}

kk_ws *kk_ws_new(kk_http *http, const kk_ws_cb *cb, void *ud)
{
    kk_ws *ws = calloc(1, sizeof *ws);
    if (!ws)
        return NULL;
    ws->cb = *cb;
    ws->ud = ud;
    ws->tls = kk_tls_new(http, &(kk_tls_cb){on_tls_open, on_tls_data}, ws);
    if (!ws->tls) {
        free(ws);
        return NULL;
    }
    return ws;
}

void kk_ws_close(kk_ws *ws)
{
    if (ws->st == ST_OPEN)
        send_frame(ws, KK_WS_CLOSE, "\x03\xe8", 2); /* 1000: normal */
    kk_tls_close(ws->tls);
    ws->st = ST_CLOSED;
    ws->in_len = ws->msg_len = 0;
    ws->msg_opcode = 0;
    ws->failed = ws->pinged = false;
}

void kk_ws_free(kk_ws *ws)
{
    if (!ws)
        return;
    kk_tls_free(ws->tls);
    free(ws->in);
    free(ws->msg);
    free(ws);
}

bool kk_ws_connect(kk_ws *ws, const char *url, double now)
{
    kk_ws_close(ws);
    int port;
    if (!kk_ws_parse_url(url, ws->host, sizeof ws->host, &port, ws->path, sizeof ws->path))
        return false;
    ws->now = ws->since = now;
    if (!kk_tls_connect(ws->tls, ws->host, port, now))
        return false;
    ws->st = ST_TLS;
    return true;
}

bool kk_ws_is_open(const kk_ws *ws)
{
    return ws->st == ST_OPEN;
}

bool kk_ws_is_active(const kk_ws *ws)
{
    return ws->st != ST_CLOSED;
}

bool kk_ws_send(kk_ws *ws, const char *text)
{
    return ws->st == ST_OPEN && send_frame(ws, KK_WS_TEXT, text, strlen(text));
}

int kk_ws_pollfd(const kk_ws *ws, struct pollfd *fd)
{
    return kk_tls_pollfd(ws->tls, fd);
}

bool kk_ws_pump(kk_ws *ws, const struct pollfd *fd, double now, const char **why)
{
    ws->now = now;
    if (ws->st == ST_CLOSED)
        return true;
    bool ok = kk_tls_pump(ws->tls, fd, now, why);
    if (ok && !ws->failed) {
        if (ws->st == ST_UPGRADE && now - ws->since > UPGRADE_TIMEOUT_S) {
            fail(ws, "o servidor não respondeu ao pedido do websocket");
        } else if (ws->st == ST_OPEN) {
            if (now - ws->last_rx > IDLE_DROP_S) {
                fail(ws, "sem resposta do servidor");
            } else if (now - ws->last_rx > IDLE_PING_S && !ws->pinged) {
                ws->pinged = true;
                send_frame(ws, KK_WS_PING, "kk", 2);
            }
        }
    }
    if (ok && !ws->failed)
        return true;
    if (ws->failed) {
        /* Copied out: closing clears it. */
        static char saved[sizeof ws->error];
        snprintf(saved, sizeof saved, "%s", ws->error);
        *why = saved;
    }
    kk_ws_close(ws);
    return false;
}
