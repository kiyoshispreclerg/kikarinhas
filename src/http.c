/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "http.h"

#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <unistd.h>

#include <curl/curl.h>

#include "log.h"

#define MAX_BODY (16u << 20)
#define MAX_WAIT_FDS 32
#define USER_AGENT                                                             \
    "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) " \
    "Chrome/130.0 Safari/537.36"

typedef struct request request;

struct kk_http {
    CURLM *multi;
    request *active; /* in-flight requests, to free them on shutdown */
};

struct request {
    request *prev, *next;
    CURL *easy;
    struct curl_slist *headers;
    char *post;
    char *body;
    size_t len, cap;
    bool too_big;
    kk_http_cb cb;
    kk_http_connect_cb connect_cb; /* set for kk_http_connect */
    void *ud;
    char err[CURL_ERROR_SIZE];
};

static size_t on_data(char *data, size_t size, size_t nmemb, void *userp)
{
    request *r = userp;
    size_t n = size * nmemb;
    if (r->len + n > MAX_BODY) {
        r->too_big = true;
        return 0; /* aborts the transfer */
    }
    if (r->len + n + 1 > r->cap) {
        size_t cap = r->cap ? r->cap : 65536;
        while (cap < r->len + n + 1)
            cap *= 2;
        char *nb = realloc(r->body, cap);
        if (!nb)
            return 0;
        r->body = nb;
        r->cap = cap;
    }
    memcpy(r->body + r->len, data, n);
    r->len += n;
    r->body[r->len] = '\0';
    return n;
}

static void request_free(request *r)
{
    if (r->easy)
        curl_easy_cleanup(r->easy);
    curl_slist_free_all(r->headers);
    free(r->post);
    free(r->body);
    free(r);
}

kk_http *kk_http_new(void)
{
    static bool global_done;
    if (!global_done) {
        if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK)
            return NULL;
        global_done = true;
    }
    kk_http *h = calloc(1, sizeof *h);
    if (!h)
        return NULL;
    h->multi = curl_multi_init();
    if (!h->multi) {
        free(h);
        return NULL;
    }
    return h;
}

void kk_http_free(kk_http *h)
{
    if (!h)
        return;
    while (h->active) {
        request *r = h->active;
        h->active = r->next;
        curl_multi_remove_handle(h->multi, r->easy);
        request_free(r);
    }
    curl_multi_cleanup(h->multi);
    free(h);
}

void kk_http_cancel(kk_http *h, void *ud)
{
    request *r = h->active;
    while (r) {
        request *next = r->next;
        if (r->ud == ud) {
            curl_multi_remove_handle(h->multi, r->easy);
            if (r->prev)
                r->prev->next = r->next;
            else
                h->active = r->next;
            if (r->next)
                r->next->prev = r->prev;
            request_free(r);
        }
        r = next;
    }
}

static int add_request(kk_http *h, request *r)
{
    if (curl_multi_add_handle(h->multi, r->easy) != CURLM_OK) {
        request_free(r);
        return -1;
    }
    r->next = h->active;
    if (h->active)
        h->active->prev = r;
    h->active = r;
    return 0;
}

static int start(kk_http *h, const char *url, const char *json, kk_http_cb cb,
                 void *ud)
{
    request *r = calloc(1, sizeof *r);
    if (!r)
        return -1;
    r->cb = cb;
    r->ud = ud;
    r->easy = curl_easy_init();
    if (!r->easy) {
        free(r);
        return -1;
    }

    r->headers = curl_slist_append(NULL, "Accept-Language: pt-BR,pt;q=0.9,en;q=0.8");
    if (json) {
        r->headers = curl_slist_append(r->headers, "Content-Type: application/json");
        r->post = strdup(json);
        if (!r->post) {
            request_free(r);
            return -1;
        }
        curl_easy_setopt(r->easy, CURLOPT_POSTFIELDS, r->post);
    }

    CURL *e = r->easy;
    curl_easy_setopt(e, CURLOPT_URL, url);
    curl_easy_setopt(e, CURLOPT_HTTPHEADER, r->headers);
    curl_easy_setopt(e, CURLOPT_USERAGENT, USER_AGENT);
    /* Consent cookie: skips the cookie wall shown to EU visitors. */
    curl_easy_setopt(e, CURLOPT_COOKIE, "SOCS=CAI; CONSENT=YES+");
    curl_easy_setopt(e, CURLOPT_ACCEPT_ENCODING, "");
    curl_easy_setopt(e, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(e, CURLOPT_MAXREDIRS, 5L);
#if LIBCURL_VERSION_NUM >= 0x075500 /* 7.85: the bitmask options are deprecated */
    curl_easy_setopt(e, CURLOPT_PROTOCOLS_STR, "https");
    curl_easy_setopt(e, CURLOPT_REDIR_PROTOCOLS_STR, "https");
#else
    curl_easy_setopt(e, CURLOPT_PROTOCOLS, (long)CURLPROTO_HTTPS);
    curl_easy_setopt(e, CURLOPT_REDIR_PROTOCOLS, (long)CURLPROTO_HTTPS);
#endif
    curl_easy_setopt(e, CURLOPT_CONNECTTIMEOUT, 15L);
    curl_easy_setopt(e, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(e, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(e, CURLOPT_WRITEFUNCTION, on_data);
    curl_easy_setopt(e, CURLOPT_WRITEDATA, r);
    curl_easy_setopt(e, CURLOPT_ERRORBUFFER, r->err);
    curl_easy_setopt(e, CURLOPT_PRIVATE, r);

    return add_request(h, r);
}

int kk_http_get(kk_http *h, const char *url, kk_http_cb cb, void *ud)
{
    return start(h, url, NULL, cb, ud);
}

int kk_http_post_json(kk_http *h, const char *url, const char *json,
                      kk_http_cb cb, void *ud)
{
    return start(h, url, json, cb, ud);
}

int kk_http_connect(kk_http *h, const char *host, int port,
                    kk_http_connect_cb cb, void *ud)
{
    char url[300];
    if (snprintf(url, sizeof url, "http://%s:%d/", host, port) >= (int)sizeof url)
        return -1;
    request *r = calloc(1, sizeof *r);
    if (!r)
        return -1;
    r->connect_cb = cb;
    r->ud = ud;
    r->easy = curl_easy_init();
    if (!r->easy) {
        free(r);
        return -1;
    }
    CURL *e = r->easy;
    curl_easy_setopt(e, CURLOPT_URL, url);
    /* Only DNS and TCP: nothing is sent, and no http_proxy in between. */
    curl_easy_setopt(e, CURLOPT_CONNECT_ONLY, 1L);
    curl_easy_setopt(e, CURLOPT_PROXY, "");
    curl_easy_setopt(e, CURLOPT_CONNECTTIMEOUT, 15L);
    curl_easy_setopt(e, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(e, CURLOPT_ERRORBUFFER, r->err);
    curl_easy_setopt(e, CURLOPT_PRIVATE, r);
    return add_request(h, r);
}

/* The socket of a finished CONNECT_ONLY request, duplicated: curl keeps
 * (and later closes) its own copy. */
static int take_socket(CURL *e)
{
    curl_socket_t s = CURL_SOCKET_BAD;
    if (curl_easy_getinfo(e, CURLINFO_ACTIVESOCKET, &s) != CURLE_OK ||
        s == CURL_SOCKET_BAD)
        return -1;
    int fd = fcntl(s, F_DUPFD_CLOEXEC, 0);
    if (fd >= 0 && fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK) < 0) {
        close(fd);
        return -1;
    }
    return fd;
}

static void finish(kk_http *h, CURL *e, CURLcode result)
{
    request *r;
    curl_easy_getinfo(e, CURLINFO_PRIVATE, (char **)&r);
    int fd = r->connect_cb && result == CURLE_OK ? take_socket(e) : -1;
    curl_multi_remove_handle(h->multi, e);
    if (r->prev)
        r->prev->next = r->next;
    else
        h->active = r->next;
    if (r->next)
        r->next->prev = r->prev;

    if (r->connect_cb) {
        const char *err = fd >= 0             ? NULL
                          : result == CURLE_OK ? "sem socket"
                          : r->err[0]          ? r->err
                                               : curl_easy_strerror(result);
        r->connect_cb(r->ud, fd, err);
        request_free(r);
        return;
    }

    long status = 0;
    const char *err = NULL;
    if (result == CURLE_OK) {
        curl_easy_getinfo(e, CURLINFO_RESPONSE_CODE, &status);
    } else if (r->too_big) {
        err = "resposta grande demais";
    } else {
        err = r->err[0] ? r->err : curl_easy_strerror(result);
    }
    r->cb(r->ud, status, r->body ? r->body : "", r->len, err);
    request_free(r);
}

int kk_http_wait(kk_http *h, struct pollfd *fds, int nfds, int timeout_ms)
{
    struct curl_waitfd extra[MAX_WAIT_FDS];
    if (nfds > MAX_WAIT_FDS)
        return -1;
    for (int i = 0; i < nfds; i++)
        extra[i] = (struct curl_waitfd){.fd = fds[i].fd, .events = fds[i].events};

    int ready;
    if (curl_multi_wait(h->multi, extra, (unsigned)nfds, timeout_ms, &ready) !=
        CURLM_OK)
        return -1;
    /* This libcurl does not report revents: ask the kernel without
     * blocking. */
    for (int i = 0; i < nfds; i++)
        fds[i].revents = 0;
    if (poll(fds, (nfds_t)nfds, 0) < 0)
        return -1;

    int running;
    curl_multi_perform(h->multi, &running);
    CURLMsg *msg;
    int left;
    while ((msg = curl_multi_info_read(h->multi, &left)))
        if (msg->msg == CURLMSG_DONE)
            finish(h, msg->easy_handle, msg->data.result);
    return 0;
}
