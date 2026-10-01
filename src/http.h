/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef KK_HTTP_H
#define KK_HTTP_H

#include <poll.h>
#include <stddef.h>

/* Asynchronous HTTPS on top of the libcurl multi interface. The main loop
 * waits in kk_http_wait(), which also watches the caller's descriptors, so
 * transfers progress without threads. */

typedef struct kk_http kk_http;

/* Called once per request. status is the HTTP code (0 on network error, then
 * err says why). body is NUL-terminated and only valid during the call. */
typedef void (*kk_http_cb)(void *ud, long status, const char *body, size_t len,
                           const char *err);

kk_http *kk_http_new(void);
void kk_http_free(kk_http *h);

int kk_http_get(kk_http *h, const char *url, kk_http_cb cb, void *ud);
int kk_http_post_json(kk_http *h, const char *url, const char *json,
                      kk_http_cb cb, void *ud);

/* Waits up to timeout_ms for network activity or for one of fds to become
 * ready (revents is filled in like poll()), then advances the transfers and
 * runs the callbacks of the finished ones. Returns -1 on error. */
int kk_http_wait(kk_http *h, struct pollfd *fds, int nfds, int timeout_ms);

#endif
