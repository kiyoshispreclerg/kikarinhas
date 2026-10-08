/* SPDX-License-Identifier: GPL-3.0-or-later */
/* MPRIS players from D-Bus signals, without a bus: messages made by hand. */
#include <stdio.h>
#include <string.h>

#include <dbus/dbus.h>

#include "../harness.h"
#include "mpris.h"

static void owner_changed(kk_mpris *m, const char *name, const char *old, const char *now)
{
    DBusMessage *s = dbus_message_new_signal("/org/freedesktop/DBus", "org.freedesktop.DBus",
                                             "NameOwnerChanged");
    dbus_message_set_sender(s, "org.freedesktop.DBus");
    dbus_message_append_args(s, DBUS_TYPE_STRING, &name, DBUS_TYPE_STRING, &old,
                             DBUS_TYPE_STRING, &now, DBUS_TYPE_INVALID);
    kk_mpris_handle(m, s);
    dbus_message_unref(s);
}

static void dict_string(DBusMessageIter *dict, const char *key, const char *value)
{
    DBusMessageIter e, v;
    dbus_message_iter_open_container(dict, DBUS_TYPE_DICT_ENTRY, NULL, &e);
    dbus_message_iter_append_basic(&e, DBUS_TYPE_STRING, &key);
    dbus_message_iter_open_container(&e, DBUS_TYPE_VARIANT, "s", &v);
    dbus_message_iter_append_basic(&v, DBUS_TYPE_STRING, &value);
    dbus_message_iter_close_container(&e, &v);
    dbus_message_iter_close_container(dict, &e);
}

/* PropertiesChanged from sender: status (NULL = unchanged) and, if title is
 * given, Metadata with it and the artists (NULL-terminated). */
static void props(kk_mpris *m, const char *sender, const char *iface, const char *status,
                  const char *title, const char *const *artists)
{
    DBusMessage *s = dbus_message_new_signal("/org/mpris/MediaPlayer2",
                                             "org.freedesktop.DBus.Properties",
                                             "PropertiesChanged");
    dbus_message_set_sender(s, sender);
    DBusMessageIter it, dict, inv;
    dbus_message_iter_init_append(s, &it);
    dbus_message_iter_append_basic(&it, DBUS_TYPE_STRING, &iface);
    dbus_message_iter_open_container(&it, DBUS_TYPE_ARRAY, "{sv}", &dict);
    if (status)
        dict_string(&dict, "PlaybackStatus", status);
    if (title) {
        DBusMessageIter e, v, md, me, mv, arr;
        const char *key = "Metadata";
        dbus_message_iter_open_container(&dict, DBUS_TYPE_DICT_ENTRY, NULL, &e);
        dbus_message_iter_append_basic(&e, DBUS_TYPE_STRING, &key);
        dbus_message_iter_open_container(&e, DBUS_TYPE_VARIANT, "a{sv}", &v);
        dbus_message_iter_open_container(&v, DBUS_TYPE_ARRAY, "{sv}", &md);
        dict_string(&md, "xesam:title", title);
        const char *akey = "xesam:artist";
        dbus_message_iter_open_container(&md, DBUS_TYPE_DICT_ENTRY, NULL, &me);
        dbus_message_iter_append_basic(&me, DBUS_TYPE_STRING, &akey);
        dbus_message_iter_open_container(&me, DBUS_TYPE_VARIANT, "as", &mv);
        dbus_message_iter_open_container(&mv, DBUS_TYPE_ARRAY, "s", &arr);
        for (int i = 0; artists && artists[i]; i++)
            dbus_message_iter_append_basic(&arr, DBUS_TYPE_STRING, &artists[i]);
        dbus_message_iter_close_container(&mv, &arr);
        dbus_message_iter_close_container(&me, &mv);
        dbus_message_iter_close_container(&md, &me);
        dbus_message_iter_close_container(&v, &md);
        dbus_message_iter_close_container(&e, &v);
        dbus_message_iter_close_container(&dict, &e);
    }
    dbus_message_iter_close_container(&it, &dict);
    dbus_message_iter_open_container(&it, DBUS_TYPE_ARRAY, "s", &inv);
    dbus_message_iter_close_container(&it, &inv);
    kk_mpris_handle(m, s);
    dbus_message_unref(s);
}

#define PLAYER "org.mpris.MediaPlayer2.Player"

TEST(mpris_playing_paused_and_gone)
{
    kk_mpris *m = kk_mpris_new_offline();
    char out[256];
    bool paused = true;
    bool none = kk_mpris_current(m, NULL, out, sizeof out, &paused);

    owner_changed(m, "org.mpris.MediaPlayer2.musica", "", ":1.5");
    owner_changed(m, "org.example.NotAPlayer", "", ":1.6"); /* ignored */
    static const char *const two[] = {"Banda Um", "Banda Dois", NULL};
    props(m, ":1.5", PLAYER, "Playing", "Canção", two);
    bool playing = kk_mpris_current(m, NULL, out, sizeof out, &paused);
    char first[256];
    snprintf(first, sizeof first, "%s", out);
    bool first_paused = paused;

    /* Another interface or an unknown sender change nothing. */
    props(m, ":1.5", "org.example.Other", "Stopped", NULL, NULL);
    props(m, ":1.9", PLAYER, "Stopped", NULL, NULL);
    bool still = kk_mpris_current(m, NULL, out, sizeof out, &paused);

    props(m, ":1.5", PLAYER, "Paused", NULL, NULL);
    kk_mpris_current(m, NULL, out, sizeof out, &paused);
    bool now_paused = paused;

    owner_changed(m, "org.mpris.MediaPlayer2.musica", ":1.5", ""); /* closed */
    bool gone = kk_mpris_current(m, NULL, out, sizeof out, &paused);
    kk_mpris_free(m);

    CHECK(!none);
    CHECK(playing);
    CHECK_STR_EQ(first, "Banda Um, Banda Dois — Canção");
    CHECK(!first_paused);
    CHECK(still);
    CHECK(now_paused);
    CHECK(!gone);
}

/* Playing beats paused; among players playing, the latest to start; the
 * filter matches part of the bus name. */
TEST(mpris_which_player)
{
    kk_mpris *m = kk_mpris_new_offline();
    owner_changed(m, "org.mpris.MediaPlayer2.spotify", "", ":1.1");
    owner_changed(m, "org.mpris.MediaPlayer2.firefox.instance_1_2", "", ":1.2");
    owner_changed(m, "org.mpris.MediaPlayer2.vlc", "", ":1.3");
    static const char *const a[] = {"A", NULL};
    props(m, ":1.1", PLAYER, "Playing", "Spotify song", a);
    props(m, ":1.2", PLAYER, "Playing", "Browser video", NULL);
    props(m, ":1.3", PLAYER, "Paused", "VLC song", a);
    char out[256], filtered[256], paused_one[256];
    bool paused, fp, pp;
    kk_mpris_current(m, NULL, out, sizeof out, &paused);
    kk_mpris_current(m, "SPOTIFY", filtered, sizeof filtered, &fp);
    bool vlc = kk_mpris_current(m, "vlc", paused_one, sizeof paused_one, &pp);
    bool none = kk_mpris_current(m, "mpv", out + 128, 64, &paused);
    kk_mpris_free(m);
    CHECK_STR_EQ(out, "Browser video");
    CHECK_STR_EQ(filtered, "A — Spotify song");
    CHECK(vlc && pp);
    CHECK_STR_EQ(paused_one, "A — VLC song");
    CHECK(!none);
}

int main(void)
{
    RUN(mpris_playing_paused_and_gone);
    RUN(mpris_which_player);
    return harness_report();
}
