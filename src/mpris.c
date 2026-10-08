/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "mpris.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include <dbus/dbus.h>

#include "log.h"

#define MPRIS_PREFIX "org.mpris.MediaPlayer2."
#define MPRIS_PATH "/org/mpris/MediaPlayer2"
#define PLAYER_IFACE "org.mpris.MediaPlayer2.Player"
#define PROPS_IFACE "org.freedesktop.DBus.Properties"
#define MAX_PLAYERS 16
#define MAX_WATCHES 8
#define MAX_PENDING 32

typedef enum { ST_STOPPED, ST_PAUSED, ST_PLAYING } status;

typedef struct {
    char *name;  /* well-known: org.mpris.MediaPlayer2.spotify */
    char *owner; /* unique name the signals come from: ":1.42" */
    status st;
    unsigned long since; /* order in which it started playing */
    char title[256], artist[256];
} player;

/* A call we sent and whose reply we wait for. */
typedef struct {
    dbus_uint32_t serial;
    char name[128]; /* "" for ListNames */
} pending;

struct kk_mpris {
    DBusConnection *conn; /* NULL offline (tests) or after a disconnect */
    DBusWatch *watches[MAX_WATCHES];
    int n_watches;
    DBusWatch *polled[MAX_WATCHES]; /* what kk_mpris_pollfds handed out */
    int n_polled;
    player players[MAX_PLAYERS];
    int n_players;
    pending pend[MAX_PENDING];
    int next_pend;
    unsigned long clock;
};

/* ---- players ------------------------------------------------------------- */

static player *by_name(kk_mpris *m, const char *name)
{
    for (int i = 0; i < m->n_players; i++)
        if (strcmp(m->players[i].name, name) == 0)
            return &m->players[i];
    return NULL;
}

static player *by_owner(kk_mpris *m, const char *owner)
{
    for (int i = 0; owner && i < m->n_players; i++)
        if (m->players[i].owner && strcmp(m->players[i].owner, owner) == 0)
            return &m->players[i];
    return NULL;
}

static player *add_player(kk_mpris *m, const char *name)
{
    player *p = by_name(m, name);
    if (p || m->n_players == MAX_PLAYERS)
        return p;
    p = &m->players[m->n_players];
    memset(p, 0, sizeof *p);
    if (!(p->name = strdup(name)))
        return NULL;
    m->n_players++;
    return p;
}

static void remove_player(kk_mpris *m, const char *name)
{
    player *p = by_name(m, name);
    if (!p)
        return;
    free(p->name);
    free(p->owner);
    *p = m->players[--m->n_players];
}

static void set_owner(player *p, const char *owner)
{
    if (!owner || (p->owner && strcmp(p->owner, owner) == 0))
        return;
    free(p->owner);
    p->owner = strdup(owner);
}

/* ---- sending ------------------------------------------------------------- */

static void send_call(kk_mpris *m, const char *dest, const char *path, const char *iface,
                      const char *method, const char *arg, const char *pend_name)
{
    if (!m->conn)
        return;
    DBusMessage *msg = dbus_message_new_method_call(dest, path, iface, method);
    if (!msg)
        return;
    if (arg)
        dbus_message_append_args(msg, DBUS_TYPE_STRING, &arg, DBUS_TYPE_INVALID);
    dbus_uint32_t serial = 0;
    if (dbus_connection_send(m->conn, msg, &serial) && pend_name) {
        pending *pd = &m->pend[m->next_pend];
        m->next_pend = (m->next_pend + 1) % MAX_PENDING;
        pd->serial = serial;
        snprintf(pd->name, sizeof pd->name, "%s", pend_name);
    }
    dbus_message_unref(msg);
}

static void get_all(kk_mpris *m, const char *name)
{
    send_call(m, name, MPRIS_PATH, PROPS_IFACE, "GetAll", PLAYER_IFACE, name);
}

/* ---- reading properties -------------------------------------------------- */

static void copy_string(DBusMessageIter *v, char *out, size_t size)
{
    const char *s = NULL;
    if (dbus_message_iter_get_arg_type(v) == DBUS_TYPE_STRING)
        dbus_message_iter_get_basic(v, &s);
    snprintf(out, size, "%s", s ? s : "");
}

/* xesam:artist is a list of names ("as"); some players send one string. */
static void copy_artists(DBusMessageIter *v, char *out, size_t size)
{
    out[0] = '\0';
    if (dbus_message_iter_get_arg_type(v) != DBUS_TYPE_ARRAY) {
        copy_string(v, out, size);
        return;
    }
    DBusMessageIter a;
    dbus_message_iter_recurse(v, &a);
    size_t n = 0;
    while (dbus_message_iter_get_arg_type(&a) == DBUS_TYPE_STRING && n < size) {
        const char *s;
        dbus_message_iter_get_basic(&a, &s);
        if (s[0]) {
            int w = snprintf(out + n, size - n, "%s%s", n ? ", " : "", s);
            n = w < 0 ? size : n + (size_t)w;
        }
        dbus_message_iter_next(&a);
    }
}

static void read_metadata(player *p, DBusMessageIter *variant)
{
    p->title[0] = p->artist[0] = '\0';
    if (dbus_message_iter_get_arg_type(variant) != DBUS_TYPE_ARRAY)
        return;
    DBusMessageIter dict;
    dbus_message_iter_recurse(variant, &dict);
    while (dbus_message_iter_get_arg_type(&dict) == DBUS_TYPE_DICT_ENTRY) {
        DBusMessageIter entry, val;
        const char *key;
        dbus_message_iter_recurse(&dict, &entry);
        dbus_message_iter_get_basic(&entry, &key);
        dbus_message_iter_next(&entry);
        dbus_message_iter_recurse(&entry, &val);
        if (strcmp(key, "xesam:title") == 0)
            copy_string(&val, p->title, sizeof p->title);
        else if (strcmp(key, "xesam:artist") == 0)
            copy_artists(&val, p->artist, sizeof p->artist);
        dbus_message_iter_next(&dict);
    }
}

static void set_status(kk_mpris *m, player *p, const char *s)
{
    status st = strcmp(s, "Playing") == 0  ? ST_PLAYING
                : strcmp(s, "Paused") == 0 ? ST_PAUSED
                                           : ST_STOPPED;
    if (st == ST_PLAYING && p->st != ST_PLAYING)
        p->since = ++m->clock;
    p->st = st;
}

/* An a{sv} of Player properties, from GetAll or PropertiesChanged. */
static void read_props(kk_mpris *m, player *p, DBusMessageIter *it)
{
    if (dbus_message_iter_get_arg_type(it) != DBUS_TYPE_ARRAY)
        return;
    DBusMessageIter dict;
    dbus_message_iter_recurse(it, &dict);
    while (dbus_message_iter_get_arg_type(&dict) == DBUS_TYPE_DICT_ENTRY) {
        DBusMessageIter entry, val;
        const char *key;
        dbus_message_iter_recurse(&dict, &entry);
        dbus_message_iter_get_basic(&entry, &key);
        dbus_message_iter_next(&entry);
        dbus_message_iter_recurse(&entry, &val);
        if (strcmp(key, "PlaybackStatus") == 0 &&
            dbus_message_iter_get_arg_type(&val) == DBUS_TYPE_STRING) {
            const char *s;
            dbus_message_iter_get_basic(&val, &s);
            set_status(m, p, s);
        } else if (strcmp(key, "Metadata") == 0) {
            read_metadata(p, &val);
        }
        dbus_message_iter_next(&dict);
    }
}

/* ---- incoming messages --------------------------------------------------- */

static bool is_mpris(const char *name)
{
    return strncmp(name, MPRIS_PREFIX, strlen(MPRIS_PREFIX)) == 0;
}

static void on_list_names(kk_mpris *m, DBusMessage *msg)
{
    DBusMessageIter it, arr;
    if (!dbus_message_iter_init(msg, &it) ||
        dbus_message_iter_get_arg_type(&it) != DBUS_TYPE_ARRAY)
        return;
    dbus_message_iter_recurse(&it, &arr);
    while (dbus_message_iter_get_arg_type(&arr) == DBUS_TYPE_STRING) {
        const char *name;
        dbus_message_iter_get_basic(&arr, &name);
        if (is_mpris(name) && add_player(m, name))
            get_all(m, name);
        dbus_message_iter_next(&arr);
    }
}

static void on_reply(kk_mpris *m, DBusMessage *msg)
{
    dbus_uint32_t serial = dbus_message_get_reply_serial(msg);
    for (int i = 0; i < MAX_PENDING; i++) {
        pending *pd = &m->pend[i];
        if (!pd->serial || pd->serial != serial)
            continue;
        pd->serial = 0;
        if (dbus_message_get_type(msg) == DBUS_MESSAGE_TYPE_ERROR)
            return;
        if (!pd->name[0]) {
            on_list_names(m, msg);
            return;
        }
        player *p = by_name(m, pd->name);
        DBusMessageIter it;
        if (p && dbus_message_iter_init(msg, &it)) {
            set_owner(p, dbus_message_get_sender(msg));
            read_props(m, p, &it);
        }
        return;
    }
}

static void on_owner_changed(kk_mpris *m, DBusMessage *msg)
{
    const char *name, *old, *new_owner;
    if (!dbus_message_get_args(msg, NULL, DBUS_TYPE_STRING, &name, DBUS_TYPE_STRING, &old,
                               DBUS_TYPE_STRING, &new_owner, DBUS_TYPE_INVALID) ||
        !is_mpris(name))
        return;
    if (!new_owner[0]) {
        remove_player(m, name);
        return;
    }
    player *p = add_player(m, name);
    if (p) {
        set_owner(p, new_owner);
        get_all(m, name);
    }
}

static void on_props_changed(kk_mpris *m, DBusMessage *msg)
{
    player *p = by_owner(m, dbus_message_get_sender(msg));
    DBusMessageIter it;
    const char *iface;
    if (!p || !dbus_message_iter_init(msg, &it) ||
        dbus_message_iter_get_arg_type(&it) != DBUS_TYPE_STRING)
        return;
    dbus_message_iter_get_basic(&it, &iface);
    if (strcmp(iface, PLAYER_IFACE) != 0 || !dbus_message_iter_next(&it))
        return;
    read_props(m, p, &it);
}

void kk_mpris_handle(kk_mpris *m, void *dbus_message)
{
    DBusMessage *msg = dbus_message;
    int type = dbus_message_get_type(msg);
    if (type == DBUS_MESSAGE_TYPE_METHOD_RETURN || type == DBUS_MESSAGE_TYPE_ERROR)
        on_reply(m, msg);
    else if (dbus_message_is_signal(msg, "org.freedesktop.DBus", "NameOwnerChanged"))
        on_owner_changed(m, msg);
    else if (dbus_message_is_signal(msg, PROPS_IFACE, "PropertiesChanged"))
        on_props_changed(m, msg);
}

static DBusHandlerResult filter(DBusConnection *c, DBusMessage *msg, void *ud)
{
    (void)c;
    kk_mpris *m = ud;
    if (dbus_message_is_signal(msg, DBUS_INTERFACE_LOCAL, "Disconnected")) {
        kk_log_warn("D-Bus: a sessão caiu; a música tocando fica indisponível");
        while (m->n_players)
            remove_player(m, m->players[0].name);
        return DBUS_HANDLER_RESULT_HANDLED;
    }
    kk_mpris_handle(m, msg);
    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

/* ---- the connection in our poll() ---------------------------------------- */

static dbus_bool_t add_watch(DBusWatch *w, void *ud)
{
    kk_mpris *m = ud;
    if (m->n_watches == MAX_WATCHES)
        return FALSE;
    m->watches[m->n_watches++] = w;
    return TRUE;
}

static void remove_watch(DBusWatch *w, void *ud)
{
    kk_mpris *m = ud;
    for (int i = 0; i < m->n_watches; i++)
        if (m->watches[i] == w) {
            m->watches[i] = m->watches[--m->n_watches];
            break;
        }
    for (int i = 0; i < m->n_polled; i++)
        if (m->polled[i] == w)
            m->polled[i] = NULL;
}

static void toggle_watch(DBusWatch *w, void *ud)
{
    (void)w;
    (void)ud; /* dbus_watch_get_enabled is read at each poll */
}

/* Calls never wait with a timeout (replies come or the bus drops), so
 * timeouts are accepted and ignored. */
static dbus_bool_t add_timeout(DBusTimeout *t, void *ud)
{
    (void)t;
    (void)ud;
    return TRUE;
}

static void no_timeout(DBusTimeout *t, void *ud)
{
    (void)t;
    (void)ud;
}

kk_mpris *kk_mpris_new_offline(void)
{
    return calloc(1, sizeof(kk_mpris));
}

kk_mpris *kk_mpris_new(void)
{
    DBusError err;
    dbus_error_init(&err);
    DBusConnection *conn = dbus_bus_get_private(DBUS_BUS_SESSION, &err);
    if (!conn) {
        kk_log_info("D-Bus: sem sessão (%s); o comando de música tocando fica desligado",
                    err.message ? err.message : "?");
        dbus_error_free(&err);
        return NULL;
    }
    kk_mpris *m = kk_mpris_new_offline();
    if (!m) {
        dbus_connection_close(conn);
        dbus_connection_unref(conn);
        return NULL;
    }
    m->conn = conn;
    dbus_connection_set_exit_on_disconnect(conn, FALSE);
    dbus_connection_set_watch_functions(conn, add_watch, remove_watch, toggle_watch, m, NULL);
    dbus_connection_set_timeout_functions(conn, add_timeout, no_timeout, no_timeout, m, NULL);
    dbus_connection_add_filter(conn, filter, m, NULL);
    /* No error argument: the rules go out without waiting for the reply. */
    dbus_bus_add_match(conn,
                       "type='signal',sender='org.freedesktop.DBus',"
                       "interface='org.freedesktop.DBus',member='NameOwnerChanged',"
                       "arg0namespace='org.mpris.MediaPlayer2'",
                       NULL);
    dbus_bus_add_match(conn,
                       "type='signal',interface='" PROPS_IFACE "',member='PropertiesChanged',"
                       "path='" MPRIS_PATH "'",
                       NULL);
    send_call(m, "org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus",
              "ListNames", NULL, "");
    return m;
}

void kk_mpris_free(kk_mpris *m)
{
    if (!m)
        return;
    if (m->conn) {
        dbus_connection_remove_filter(m->conn, filter, m);
        dbus_connection_close(m->conn);
        dbus_connection_unref(m->conn);
    }
    while (m->n_players)
        remove_player(m, m->players[0].name);
    free(m);
}

int kk_mpris_pollfds(kk_mpris *m, struct pollfd *fds, int max)
{
    m->n_polled = 0;
    if (!m->conn || !dbus_connection_get_is_connected(m->conn))
        return 0;
    for (int i = 0; i < m->n_watches && m->n_polled < max; i++) {
        DBusWatch *w = m->watches[i];
        if (!dbus_watch_get_enabled(w))
            continue;
        unsigned flags = dbus_watch_get_flags(w);
        fds[m->n_polled] = (struct pollfd){
            .fd = dbus_watch_get_unix_fd(w),
            .events = (short)((flags & DBUS_WATCH_READABLE ? POLLIN : 0) |
                              (flags & DBUS_WATCH_WRITABLE ? POLLOUT : 0)),
        };
        m->polled[m->n_polled++] = w;
    }
    return m->n_polled;
}

void kk_mpris_dispatch(kk_mpris *m, const struct pollfd *fds, int n)
{
    if (!m->conn)
        return;
    for (int i = 0; i < n && i < m->n_polled; i++) {
        short r = fds[i].revents;
        if (!r || !m->polled[i])
            continue;
        unsigned flags = (r & POLLIN ? DBUS_WATCH_READABLE : 0) |
                         (r & POLLOUT ? DBUS_WATCH_WRITABLE : 0) |
                         (r & POLLERR ? DBUS_WATCH_ERROR : 0) |
                         (r & POLLHUP ? DBUS_WATCH_HANGUP : 0);
        dbus_watch_handle(m->polled[i], flags);
    }
    while (dbus_connection_dispatch(m->conn) == DBUS_DISPATCH_DATA_REMAINS)
        ;
}

/* ---- asking -------------------------------------------------------------- */

static bool matches(const player *p, const char *filter)
{
    if (!filter || !filter[0])
        return true;
    size_t n = strlen(filter);
    for (const char *s = p->name; *s; s++)
        if (strncasecmp(s, filter, n) == 0)
            return true;
    return false;
}

bool kk_mpris_current(const kk_mpris *m, const char *filter, char *out, size_t size,
                      bool *paused)
{
    const player *best = NULL;
    for (int i = 0; i < m->n_players; i++) {
        const player *p = &m->players[i];
        if (p->st == ST_STOPPED || !p->title[0] || !matches(p, filter))
            continue;
        if (!best || p->st > best->st || (p->st == best->st && p->since > best->since))
            best = p;
    }
    if (!best)
        return false;
    if (best->artist[0])
        snprintf(out, size, "%s — %s", best->artist, best->title);
    else
        snprintf(out, size, "%s", best->title);
    *paused = best->st == ST_PAUSED;
    return true;
}
