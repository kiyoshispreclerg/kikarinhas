/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "tls.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include <openssl/err.h>
#include <openssl/ssl.h>
#include <openssl/x509v3.h>

#define HANDSHAKE_TIMEOUT_S 20.0
/* Something is very wrong if this much never leaves. */
#define MAX_OUT (1u << 20)

struct kk_tls {
    kk_http *http;
    kk_tls_cb cb;
    void *ud;

    SSL_CTX *ctx;
    SSL *ssl;
    BIO *rbio, *wbio; /* network → SSL, SSL → network */
    int fd;
    char *out; /* encrypted bytes waiting for send() */
    size_t out_len, out_cap;

    kk_tls_state st;
    char host[256];
    double now, since, last_rx; /* since: start of the handshake */
    bool broken;     /* a write failed: reported by the next pump */
    char error[256]; /* why it closed, until a pump reports it */
    bool has_error;
    char why[256];   /* what the last pump returned */
};

static void close_conn(kk_tls *t)
{
    kk_http_cancel(t->http, t);
    if (t->ssl)
        SSL_free(t->ssl); /* frees the BIOs too */
    t->ssl = NULL;
    t->rbio = t->wbio = NULL;
    if (t->fd >= 0)
        close(t->fd);
    t->fd = -1;
    t->out_len = 0;
    t->broken = false;
    t->st = KK_TLS_CLOSED;
}

/* Closes and keeps why, for the next pump. */
static void close_with(kk_tls *t, const char *why)
{
    close_conn(t);
    snprintf(t->error, sizeof t->error, "%s", why);
    t->has_error = true;
}

/* OpenSSL's reason for the last failure, or fallback. */
static const char *ssl_reason(const kk_tls *t, char *buf, size_t size, const char *fallback)
{
    long v = t->ssl ? SSL_get_verify_result(t->ssl) : X509_V_OK;
    unsigned long e = ERR_get_error();
    ERR_clear_error();
    if (v != X509_V_OK)
        return X509_verify_cert_error_string(v);
    if (e) {
        ERR_error_string_n(e, buf, size);
        return buf;
    }
    return fallback;
}

static bool append_out(kk_tls *t, const char *data, size_t n)
{
    if (t->out_len + n > t->out_cap) {
        size_t cap = t->out_cap ? t->out_cap : 4096;
        while (cap < t->out_len + n)
            cap *= 2;
        if (cap > MAX_OUT)
            return false;
        char *o = realloc(t->out, cap);
        if (!o)
            return false;
        t->out = o;
        t->out_cap = cap;
    }
    memcpy(t->out + t->out_len, data, n);
    t->out_len += n;
    return true;
}

/* Moves what SSL produced to the socket, as much as it takes now. */
static bool flush(kk_tls *t)
{
    char buf[4096];
    int n;
    while ((n = BIO_read(t->wbio, buf, sizeof buf)) > 0)
        if (!append_out(t, buf, (size_t)n)) {
            errno = ENOBUFS;
            return false;
        }
    size_t sent = 0;
    while (sent < t->out_len) {
        ssize_t w = send(t->fd, t->out + sent, t->out_len - sent, MSG_NOSIGNAL | MSG_DONTWAIT);
        if (w < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
                break;
            return false;
        }
        sent += (size_t)w;
    }
    memmove(t->out, t->out + sent, t->out_len - sent);
    t->out_len -= sent;
    return true;
}

/* Runs SSL on what arrived; false (with why) when the connection is over. */
static bool drive(kk_tls *t, char *why, size_t size)
{
    char err[256];
    if (t->st == KK_TLS_HANDSHAKE) {
        int r = SSL_do_handshake(t->ssl);
        if (r == 1) {
            t->st = KK_TLS_OPEN;
            if (t->cb.on_open)
                t->cb.on_open(t->ud);
        } else {
            int e = SSL_get_error(t->ssl, r);
            if (e != SSL_ERROR_WANT_READ && e != SSL_ERROR_WANT_WRITE) {
                snprintf(why, size, "%s", ssl_reason(t, err, sizeof err, "falha no TLS"));
                return false;
            }
        }
    }
    char buf[16384];
    while (t->st == KK_TLS_OPEN) {
        int n = SSL_read(t->ssl, buf, (int)sizeof buf);
        if (n > 0) {
            if (t->cb.on_data)
                t->cb.on_data(t->ud, buf, (size_t)n);
            continue;
        }
        int e = SSL_get_error(t->ssl, n);
        if (e == SSL_ERROR_WANT_READ || e == SSL_ERROR_WANT_WRITE)
            break;
        snprintf(why, size, "%s",
                 e == SSL_ERROR_ZERO_RETURN ? "conexão encerrada pelo servidor"
                                            : ssl_reason(t, err, sizeof err, "erro de leitura"));
        return false;
    }
    if (!flush(t)) {
        snprintf(why, size, "%s", strerror(errno));
        return false;
    }
    return true;
}

/* Feeds what the socket has to SSL. */
static bool pump_in(kk_tls *t, char *why, size_t size)
{
    char buf[16384];
    for (;;) {
        ssize_t n = recv(t->fd, buf, sizeof buf, MSG_DONTWAIT);
        if (n > 0) {
            BIO_write(t->rbio, buf, (int)n);
            t->last_rx = t->now;
            continue;
        }
        if (n == 0) {
            snprintf(why, size, "conexão encerrada");
            return false;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK)
            return true;
        if (errno != EINTR) {
            snprintf(why, size, "%s", strerror(errno));
            return false;
        }
    }
}

static void on_connected(void *ud, int fd, const char *err)
{
    kk_tls *t = ud;
    if (fd < 0) {
        close_with(t, err ? err : "?");
        return;
    }
    t->fd = fd;
    t->ssl = SSL_new(t->ctx);
    t->rbio = BIO_new(BIO_s_mem());
    t->wbio = BIO_new(BIO_s_mem());
    if (!t->ssl || !t->rbio || !t->wbio) {
        BIO_free(t->rbio);
        BIO_free(t->wbio);
        t->rbio = t->wbio = NULL;
        close_with(t, "sem memória");
        return;
    }
    SSL_set_bio(t->ssl, t->rbio, t->wbio);
    SSL_set_connect_state(t->ssl);
    SSL_set_tlsext_host_name(t->ssl, t->host);
    SSL_set1_host(t->ssl, t->host);
    t->st = KK_TLS_HANDSHAKE;
    t->since = t->last_rx = t->now;
    char why[256];
    if (!drive(t, why, sizeof why))
        close_with(t, why);
}

kk_tls *kk_tls_new(kk_http *http, const kk_tls_cb *cb, void *ud)
{
    kk_tls *t = calloc(1, sizeof *t);
    if (!t)
        return NULL;
    t->fd = -1;
    t->http = http;
    t->cb = *cb;
    t->ud = ud;
    t->ctx = SSL_CTX_new(TLS_client_method());
    if (!t->ctx) {
        free(t);
        return NULL;
    }
    SSL_CTX_set_min_proto_version(t->ctx, TLS1_2_VERSION);
    SSL_CTX_set_default_verify_paths(t->ctx);
    SSL_CTX_set_verify(t->ctx, SSL_VERIFY_PEER, NULL);
    return t;
}

void kk_tls_free(kk_tls *t)
{
    if (!t)
        return;
    close_conn(t);
    SSL_CTX_free(t->ctx);
    free(t->out);
    free(t);
}

bool kk_tls_connect(kk_tls *t, const char *host, int port, double now)
{
    close_conn(t);
    t->has_error = false;
    t->now = now;
    if ((size_t)snprintf(t->host, sizeof t->host, "%s", host) >= sizeof t->host)
        return false;
    t->st = KK_TLS_CONNECTING;
    if (kk_http_connect(t->http, host, port, on_connected, t) < 0) {
        t->st = KK_TLS_CLOSED;
        return false;
    }
    return true;
}

void kk_tls_close(kk_tls *t)
{
    close_conn(t);
    t->has_error = false;
}

kk_tls_state kk_tls_get_state(const kk_tls *t)
{
    return t->st;
}

double kk_tls_last_rx(const kk_tls *t)
{
    return t->last_rx;
}

bool kk_tls_write(kk_tls *t, const void *data, size_t len)
{
    if (t->st != KK_TLS_OPEN || len == 0 || len > MAX_OUT)
        return t->st == KK_TLS_OPEN && len == 0;
    /* Memory BIO: all or nothing. */
    if (SSL_write(t->ssl, data, (int)len) <= 0 || !flush(t)) {
        t->broken = true;
        return false;
    }
    return true;
}

int kk_tls_pollfd(const kk_tls *t, struct pollfd *fd)
{
    if (t->fd < 0 || (t->st != KK_TLS_HANDSHAKE && t->st != KK_TLS_OPEN))
        return 0;
    *fd = (struct pollfd){.fd = t->fd, .events = (short)(POLLIN | (t->out_len ? POLLOUT : 0))};
    return 1;
}

bool kk_tls_pump(kk_tls *t, const struct pollfd *fd, double now, const char **why)
{
    t->now = now;
    if (t->st == KK_TLS_HANDSHAKE || t->st == KK_TLS_OPEN) {
        char why_in[256] = "", why_run[256] = "";
        bool in_ok = true, ok = true;
        if (fd && fd->revents) {
            in_ok = !(fd->revents & (POLLIN | POLLHUP | POLLERR)) ||
                    pump_in(t, why_in, sizeof why_in);
            /* Even after an error: what arrived before it is still read. */
            ok = drive(t, why_run, sizeof why_run);
        } else if (t->out_len || t->broken) {
            ok = !t->broken && flush(t);
            if (!ok)
                snprintf(why_run, sizeof why_run, "%s",
                         t->broken ? "erro de escrita" : strerror(errno));
        }
        if (!in_ok || !ok)
            close_with(t, why_in[0] ? why_in : why_run[0] ? why_run : "erro");
        else if (t->st == KK_TLS_HANDSHAKE && now - t->since > HANDSHAKE_TIMEOUT_S)
            close_with(t, "o servidor não respondeu ao TLS");
    }
    if (!t->has_error)
        return true;
    t->has_error = false;
    snprintf(t->why, sizeof t->why, "%s", t->error);
    *why = t->why;
    return false;
}
