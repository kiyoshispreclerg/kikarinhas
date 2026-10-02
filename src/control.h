/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef KK_CONTROL_H
#define KK_CONTROL_H

#include <poll.h>
#include <stdbool.h>
#include <stddef.h>

#include "cJSON.h"
#include "chat.h"

/* The control socket: a unix stream socket where other programs talk to the
 * running kikarinhas, one JSON object per line, each answered by one line.
 *
 *   {"type":"message","platform":"twitch","user_id":"123","name":"Fulano",
 *    "text":"oi Kappa !jump","badges":["member"],"kind":"text","amount":null,
 *    "emotes":[{"id":"25","name":"Kappa","url":"https://...","start":3,"len":5}]}
 *     → a chat message, as if it came from a built-in connector (bridges).
 *       "emotes" lists the platform's image emotes (https PNGs; start/len
 *       are bytes of text, optional); Unicode emoji stay in the text.
 *   {"type":"reaction","platform":"x","emoji":"❤","count":3}
 *     → viewers reacting (the emote wall shows them); an image reaction
 *       gives "id"/"name"/"url" instead of "emoji"
 *   {"type":"reload"} → reread the config file
 *   {"type":"ping"}   → {"ok":true,"version":"..."}
 *   {"type":"quit"}
 *
 * A line that is a bare word ("reload") means {"type":"<word>"}, for
 * `echo reload | socat - UNIX-CONNECT:...`. Replies are {"ok":true,...} or
 * {"ok":false,"error":"..."}. Replies are best effort: a client that never
 * reads them just stops getting them.
 *
 * Everything runs in the main loop: the socket and its clients are polled
 * with the rest, and nothing here blocks. */

typedef struct kk_control kk_control;

/* Handles every type but "message" and "reaction". reply already holds
 * "ok": true; set it to false and add "error", or add other fields. */
typedef void (*kk_control_fn)(void *ud, const char *type, const cJSON *req,
                              cJSON *reply);

/* Creates the socket at path. If another kikarinhas answers there, fails
 * (NULL) and leaves it alone; a stale socket file is replaced. Messages
 * and reactions go to chat (copied; may be NULL). With path
 * NULL there is no socket, only kk_control_handle_line (for tests). */
kk_control *kk_control_open(const char *path, const kk_chat_sink *chat,
                            kk_control_fn on_request, void *ud);
/* Closes the clients and removes the socket file. */
void kk_control_close(kk_control *c);

/* Appends the descriptors to poll (listener and clients) to fds, at most
 * max; returns how many. */
int kk_control_pollfds(const kk_control *c, struct pollfd *fds, int max);
/* After the poll: accepts, reads and answers whatever is ready. fds are the
 * ones kk_control_pollfds filled (with revents set). */
void kk_control_dispatch(kk_control *c, const struct pollfd *fds, int n);

/* Handles one request line as if a client sent it; returns the reply line
 * (without "\n"), which the caller frees. Used by the tests. */
char *kk_control_handle_line(kk_control *c, const char *line);

/* $XDG_RUNTIME_DIR/kikarinhas.sock, else /tmp/kikarinhas-<uid>.sock. */
bool kk_control_default_path(char *out, size_t size);

/* Client side, blocking (for --reload and kikarinhas-config): sends line,
 * waits up to timeout_ms for the reply line and stores it in reply.
 * Returns -1 (errno set) if nobody listens or no reply came. */
int kk_control_request(const char *path, const char *line, char *reply,
                       size_t size, int timeout_ms);

#endif
