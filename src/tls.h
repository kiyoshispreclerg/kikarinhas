/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef KK_TLS_H
#define KK_TLS_H

/* A TLS client connection that never blocks, for the chat protocols curl
 * doesn't speak (Twitch's IRC, websockets): kk_http_connect resolves the
 * name and opens the TCP socket, OpenSSL runs on memory BIOs and we move
 * the bytes with recv/send (MSG_NOSIGNAL: a dropped connection is an
 * error, not SIGPIPE). The socket goes into the main loop's poll() through
 * kk_tls_pollfd.
 *
 * The owner keeps the policy (when to reconnect, keepalives); this only
 * carries bytes and says when the connection is over. */

#include <poll.h>
#include <stdbool.h>
#include <stddef.h>

#include "http.h"

typedef struct kk_tls kk_tls;

typedef enum {
    KK_TLS_CLOSED,
    KK_TLS_CONNECTING, /* name and TCP */
    KK_TLS_HANDSHAKE,
    KK_TLS_OPEN,
} kk_tls_state;

typedef struct {
    /* The handshake is done: kk_tls_write works from here on. */
    void (*on_open)(void *ud);
    /* Plain text arrived (data is writable, len bytes, not NUL-terminated).
     * Don't close or free the connection from here: note it and do it
     * after kk_tls_pump returns. */
    void (*on_data)(void *ud, char *data, size_t len);
} kk_tls_cb;

kk_tls *kk_tls_new(kk_http *http, const kk_tls_cb *cb, void *ud);
void kk_tls_free(kk_tls *t);

/* Starts connecting (closing what was open). now: monotonic seconds. */
bool kk_tls_connect(kk_tls *t, const char *host, int port, double now);
void kk_tls_close(kk_tls *t);
kk_tls_state kk_tls_get_state(const kk_tls *t);

/* Queues bytes to send; only when open. False if the connection is broken
 * (the next kk_tls_pump says so). */
bool kk_tls_write(kk_tls *t, const void *data, size_t len);

/* The socket to watch, if any: fills *fd and returns 1, else 0. */
int kk_tls_pollfd(const kk_tls *t, struct pollfd *fd);

/* Every loop turn, after the poll (fd as filled by kk_tls_pollfd, revents
 * set; NULL if it gave none): moves bytes, runs the callbacks. Returns
 * false once when the connection failed or ended: it is then closed and
 * *why says why (valid until the next call). */
bool kk_tls_pump(kk_tls *t, const struct pollfd *fd, double now, const char **why);

/* When bytes last arrived (monotonic seconds), for keepalives. */
double kk_tls_last_rx(const kk_tls *t);

#endif
