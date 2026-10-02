/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef KK_CONFIG_EDITOR_H
#define KK_CONFIG_EDITOR_H

/* Shared by the files of kikarinhas-config: the window's state and the
 * helpers each tab uses to build its widgets and write the .ini. */

#include <stdbool.h>
#include <stddef.h>

#include <gtk/gtk.h>

#include "config.h"
#include "ini.h"
#include "util.h"

typedef struct sounds_tab sounds_tab;
typedef struct audience_tab audience_tab;
typedef struct wall_tab wall_tab;

typedef struct {
    char path[KK_PATH_MAX];
    kk_ini *ini;
    char *loaded_custom[256]; /* custom commands in the file when loaded */
    int n_loaded_custom;

    GtkWidget *window, *status, *version_label;
    /* [window] */
    GtkWidget *mode, *width, *height, *fps;
    /* [avatars] */
    GtkWidget *scale, *ground_auto, *ground, *count_auto, *count, *show,
        *default_avatar, *sa_dir, *show_names, *name_position, *show_bubbles,
        *name_font, *name_size, *bubble_font, *bubble_size, *bubble_auto,
        *bubble_secs;
    /* [chat] */
    GtkWidget *youtube, *twitch, *extra_emotes, *demo, *max, *despawn, *verbose, *users;
    /* [control] */
    GtkWidget *socket;
    /* [commands] */
    GtkWidget *shortcuts, *shortcut_cd, *help_bubbles, *help_count, *help_auto, *help_secs;
    GtkListStore *commands;
    GtkWidget *tree;

    sounds_tab *sounds;
    audience_tab *audience;
    wall_tab *wall;
} editor;

/* The line at the bottom of the window. */
void status(editor *e, const char *fmt, ...) __attribute__((format(printf, 2, 3)));

/* Widgets. */
GtkWidget *page(GtkWidget *notebook, const char *title);
GtkWidget *row(GtkWidget *table, const char *label, GtkWidget *w, const char *hint);
GtkWidget *spin(double lo, double hi, double step, int digits);
GtkWidget *entry(const char *text);
GtkWidget *check(const char *label, bool on);
const char *text_of(GtkWidget *w);
int spin_int(GtkWidget *w);
/* A button with a stock icon and our own (translated) label: GTK2's own
 * translations are often not installed. */
GtkWidget *icon_button(const char *stock, const char *label);
/* A small label in grey, for explanations under a list. */
GtkWidget *hint_label(const char *text);

void join(char *out, size_t size, char *const *items, int n);
void fmt_num(char *out, size_t size, double v);

/* Writing keys: see put() in main.c. */
void put(editor *e, const char *section, const char *key, const char *value,
         const char *dflt);
void put_int(editor *e, const char *section, const char *key, int v, int dflt);
void put_num(editor *e, const char *section, const char *key, double v,
             double dflt);
void put_bool(editor *e, const char *section, const char *key, GtkWidget *w,
              bool dflt);

/* Effective config of the file as last loaded or saved. */
const char *editor_sa_dir(editor *e, char *buf, size_t size);
/* Path of the control socket from the Chat tab; false if it is "off". */
bool editor_socket(editor *e, char *out, size_t size);
/* Sends one request line to the running kikarinhas; false if none answers. */
bool editor_request(editor *e, const char *line, char *reply, size_t size);
/* Pings the running kikarinhas and updates the footer's version label
 * (e->version_label). Harmless to call often; other tabs that already talk
 * to the socket (e.g. Espectadores' "Atualizar") call it too, so the label
 * stays current without a timer. */
void editor_refresh_kikarinhas_version(editor *e);

/* The "Sons" tab (sounds.c). */
GtkWidget *sounds_page(editor *e, const kk_config *cfg);
void sounds_collect(editor *e);
void sounds_free(editor *e);

/* The "Emote wall" tab (wall.c). */
GtkWidget *wall_page(editor *e, const kk_config *cfg);
void wall_collect(editor *e);
void wall_free(editor *e);

/* The "Espectadores" tab (audience.c). */
GtkWidget *audience_page(editor *e, const kk_config *cfg);
void audience_free(editor *e);

#endif
