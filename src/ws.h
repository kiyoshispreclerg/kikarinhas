/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef KK_WS_H
#define KK_WS_H

/* A websocket client (RFC 6455) on top of kk_tls, enough for chat feeds:
 * Kick (Pusher) and Odysee (Commentron) push JSON as text messages.
 *
 * Only wss://, only text out; binary messages arrive like text ones.
 * Fragmented messages are joined; pings are answered; a connection quiet
 * for a while gets a ping of ours and, still quiet, is taken as dead. The
 * owner decides when to reconnect.
 *
 * libcurl only speaks websockets from 7.86 on (and as an experiment), so
 * this is ours. The framing lives in pure functions, tested in tests/. */

#include <poll.h>
#include <stdbool.h>
#include <stddef.h>

#include "http.h"

typedef struct kk_ws kk_ws;

typedef struct {
    /* The upgrade went through: kk_ws_send works from here on. */
    void (*on_open)(void *ud);
    /* A whole message (NUL-terminated, writable). Don't close or free the
     * connection from here: note it and do it after kk_ws_pump returns. */
    void (*on_message)(void *ud, char *text, size_t len);
} kk_ws_cb;

kk_ws *kk_ws_new(kk_http *http, const kk_ws_cb *cb, void *ud);
void kk_ws_free(kk_ws *ws);

/* Starts connecting to url ("wss://host[:port]/path?query"), closing what
 * was open. False if the url is not one. */
bool kk_ws_connect(kk_ws *ws, const char *url, double now);
void kk_ws_close(kk_ws *ws);
bool kk_ws_is_open(const kk_ws *ws);
/* Connecting or open. */
bool kk_ws_is_active(const kk_ws *ws);

/* Sends a text message; only when open. */
bool kk_ws_send(kk_ws *ws, const char *text);

int kk_ws_pollfd(const kk_ws *ws, struct pollfd *fd);
/* Every loop turn, like kk_tls_pump: false once when the connection failed
 * or ended (it is then closed), with *why. */
bool kk_ws_pump(kk_ws *ws, const struct pollfd *fd, double now, const char **why);

/* ---- framing (exposed for tests) ----------------------------------------- */

/* "wss://host:port/path?q" → host, port (443 if absent), "/path?q". */
bool kk_ws_parse_url(const char *url, char *host, size_t host_size, int *port, char *path,
                     size_t path_size);

/* Sec-WebSocket-Accept for a key: base64(SHA-1(key + GUID)). out: 29+ bytes. */
void kk_ws_accept_for(const char *key, char *out, size_t size);

/* Checks the server's answer to the upgrade (head: the status line and
 * headers, NUL-terminated). NULL if it is a good one, else why not. */
const char *kk_ws_check_upgrade(const char *head, const char *key);

enum {
    KK_WS_CONT = 0x0,
    KK_WS_TEXT = 0x1,
    KK_WS_BINARY = 0x2,
    KK_WS_CLOSE = 0x8,
    KK_WS_PING = 0x9,
    KK_WS_PONG = 0xA,
};

typedef struct {
    int opcode;
    bool fin;
    unsigned char *payload; /* inside the buffer, unmasked */
    size_t len;
} kk_ws_frame;

/* The frame at the start of buf (n bytes). Returns the bytes it takes, 0 if
 * more are needed, -1 if it is malformed or longer than max. Masked
 * payloads are unmasked in place. */
long kk_ws_parse_frame(unsigned char *buf, size_t n, size_t max, kk_ws_frame *f);

/* A final frame from the client (always masked with mask) into out, which
 * needs len + 14 bytes. Returns its size. */
size_t kk_ws_build_frame(int opcode, const void *payload, size_t len,
                         const unsigned char mask[4], unsigned char *out);

#endif
