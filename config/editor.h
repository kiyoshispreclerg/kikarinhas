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
#include "sa.h"
#include "util.h"

typedef struct sounds_tab sounds_tab;
typedef struct audience_tab audience_tab;
typedef struct wall_tab wall_tab;
typedef struct danmaku_tab danmaku_tab;
typedef struct sources_tab sources_tab;
typedef struct avatars_tab avatars_tab;
typedef struct points_tab points_tab;
typedef struct timers_tab timers_tab;

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
        *default_avatar, *sa_dir, *show_names, *name_position, *name_idle, *name_idle_secs, *show_bubbles,
        *name_font, *name_size, *bubble_font, *bubble_size, *bubble_auto,
        *bubble_secs;
    /* [chat] */
    GtkWidget *extra_emotes, *demo, *max, *despawn, *verbose, *users;
    /* [control] */
    GtkWidget *socket;
    /* [commands] */
    GtkWidget *shortcuts, *shortcut_cd, *help_bubbles, *help_count, *help_auto, *help_secs;
    GtkListStore *commands;
    GtkWidget *tree;

    sounds_tab *sounds;
    audience_tab *audience;
    wall_tab *wall;
    danmaku_tab *danmaku;
    sources_tab *sources;
    avatars_tab *avatars;
    points_tab *points;
    timers_tab *timers;
} editor;

/* Labels of the levels (kk_role), to translate with _() where shown. */
extern const char *const ROLE_LABELS[KK_ROLE_COUNT];

/* The line at the bottom of the window. */
void status(editor *e, const char *fmt, ...) __attribute__((format(printf, 2, 3)));

/* Widgets. */
GtkWidget *page(GtkWidget *notebook, const char *title);
/* Puts content in a dialog inside a scrolled window, as tall as the content
 * but never more than the screen allows; dialog_body_fit again when the
 * content changes size. Returns the scrolled window. */
GtkWidget *dialog_body(GtkWidget *dlg, GtkWidget *content);
void dialog_body_fit(GtkWidget *scroll);
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
/* An "automatic" check box next to a spin button it disables (value < 0 =
 * automatic), and writing it back as "auto" or the number. */
GtkWidget *auto_spin(GtkWidget **check, GtkWidget **spinner, double lo, double hi,
                     int value);
void put_auto(editor *e, const char *section, const char *key, GtkWidget *check_w,
              GtkWidget *spin_w);

/* The "Comandos" tab (commands.c). */
GtkWidget *commands_page(editor *e, const kk_config *cfg);
/* Calls fn with the name of every active command, as the tab has them. */
void commands_each_name(editor *e, void (*fn)(const char *name, void *ud), void *ud);
/* The commands with their cost in points, editable (for the Points tab). */
GtkWidget *commands_cost_view(editor *e);
void commands_collect(editor *e);
void commands_free(editor *e);

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
/* The sounds as the tab has them now; column 0 is the name. */
GtkTreeModel *sounds_model(editor *e);
/* The sounds with their cost in points, editable (for the Points tab). */
GtkWidget *sounds_cost_view(editor *e);
void sounds_free(editor *e);

/* The "Emote wall" tab (wall.c). */
GtkWidget *wall_page(editor *e, const kk_config *cfg);
void wall_collect(editor *e);
void wall_free(editor *e);

/* The "Danmaku" tab (danmaku.c). */
GtkWidget *danmaku_page(editor *e, const kk_config *cfg);
void danmaku_collect(editor *e);
void danmaku_free(editor *e);

/* Avatar folders (avatars.c): the list in the Avatares tab, and every
 * avatar of every folder as the widgets say, loaded once on demand (NULL if
 * none). editor_library_forget drops it, to read again. */
GtkWidget *avatar_dirs_widget(editor *e, const kk_config *cfg);
void avatar_dirs_collect(editor *e);
const kk_sa_library *editor_library(editor *e);
void editor_library_forget(editor *e);
/* A dialog to pick an avatar by its picture, with a search box. Returns the
 * key (g_free it), "" for "random, always the same", or NULL if cancelled. */
gchar *editor_pick_avatar(editor *e, const char *title, const char *current);
/* The "Avatares instalados" tab: every avatar of every folder. */
GtkWidget *avatars_page(editor *e);
void avatars_free(editor *e);

/* The "Pontos" tab (points.c): [points] and the costs of commands and
 * sounds (it must come after the Commands and Sounds tabs). */
GtkWidget *points_page(editor *e, const kk_config *cfg);
void points_collect(editor *e);
void points_free(editor *e);

/* The "Timers" tab (timers.c): [timer.NAME] sections. */
GtkWidget *timers_page(editor *e, const kk_config *cfg);
void timers_collect(editor *e);
void timers_free(editor *e);

/* The list of lives in the Chat tab (sources.c): [chat] youtube and twitch. */
GtkWidget *sources_widget(editor *e, const kk_config *cfg);
void sources_collect(editor *e);
void sources_free(editor *e);

/* The "Espectadores" tab (audience.c). */
GtkWidget *audience_page(editor *e, const kk_config *cfg);
void audience_free(editor *e);

#endif
