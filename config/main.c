/* SPDX-License-Identifier: GPL-3.0-or-later */
/* kikarinhas-config: a small GTK2 editor for kikarinhas.ini.
 *
 * It edits the file through kk_ini, so comments and keys it doesn't know
 * stay where they are, and only writes a key when its value differs from the
 * default or the key was already there. "Salvar e aplicar" then asks the
 * running kikarinhas to reload over the control socket. The core never
 * needs this program.
 *
 * This file has the window and the Janela/Avatares/Chat tabs; commands.c,
 * sounds.c, wall.c and audience.c have the Comandos, Sons, Emote wall and
 * Espectadores ones, sources.c the list of lives of the Chat tab and
 * avatars.c the avatar folders and the Avatares instalados tab. */
#include <errno.h>
#include <locale.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <gtk/gtk.h>

#include "config.h"
#include "control.h"
#include "editor.h"
#include "i18n.h"
#include "ini.h"
#include "sa.h"
#include "util.h"

enum {
    COL_ENABLED,
    COL_NAME,
    COL_ACTION,
    COL_DATA,
    COL_ALIASES,
    COL_CD,
    COL_GCD,
    COL_ROLE,
    COL_CUSTOM, /* not a built-in: name and action can change */
    N_COLS,
};

/* Translated where shown: see role_label() and role_of_label(). */
const char *const ROLE_LABELS[KK_ROLE_COUNT] = {
    N_("anyone"), N_("members"), N_("trusted"), N_("moderators"), N_("owner"),
};


void status(editor *e, const char *fmt, ...)
{
    char msg[2048];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof msg, fmt, ap);
    va_end(ap);
    gtk_label_set_text(GTK_LABEL(e->status), msg);
}

/* ---- widgets ------------------------------------------------------------- */

GtkWidget *page(GtkWidget *notebook, const char *title)
{
    GtkWidget *table = gtk_table_new(1, 2, FALSE);
    gtk_table_set_row_spacings(GTK_TABLE(table), 6);
    gtk_table_set_col_spacings(GTK_TABLE(table), 12);
    gtk_container_set_border_width(GTK_CONTAINER(table), 12);
    GtkWidget *align = gtk_alignment_new(0, 0, 1, 0);
    gtk_container_add(GTK_CONTAINER(align), table);
    /* Long pages scroll instead of making the window taller than the screen. */
    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_NEVER,
                                   GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_add_with_viewport(GTK_SCROLLED_WINDOW(scroll), align);
    gtk_viewport_set_shadow_type(GTK_VIEWPORT(gtk_bin_get_child(GTK_BIN(scroll))),
                                 GTK_SHADOW_NONE);
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), scroll, gtk_label_new(title));
    return table;
}

/* A labelled row; hint (optional) goes under the widget in small text. */
GtkWidget *row(GtkWidget *table, const char *label, GtkWidget *w,
                      const char *hint)
{
    guint n;
    g_object_get(table, "n-rows", &n, NULL);
    if (n == 1 && !gtk_container_get_children(GTK_CONTAINER(table)))
        n = 0;
    gtk_table_resize(GTK_TABLE(table), n + 1, 2);
    if (label) {
        GtkWidget *l = gtk_label_new(label);
        gtk_misc_set_alignment(GTK_MISC(l), 1, 0.5);
        gtk_table_attach(GTK_TABLE(table), l, 0, 1, n, n + 1, GTK_FILL, GTK_FILL, 0, 0);
    }
    GtkWidget *cell = w;
    if (hint) {
        cell = gtk_vbox_new(FALSE, 2);
        GtkWidget *h = gtk_label_new(NULL);
        char *markup = g_markup_printf_escaped("<small>%s</small>", hint);
        gtk_label_set_markup(GTK_LABEL(h), markup);
        g_free(markup);
        gtk_label_set_line_wrap(GTK_LABEL(h), TRUE);
        gtk_misc_set_alignment(GTK_MISC(h), 0, 0);
        gtk_widget_set_sensitive(h, FALSE);
        gtk_box_pack_start(GTK_BOX(cell), w, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(cell), h, FALSE, FALSE, 0);
    }
    gtk_table_attach(GTK_TABLE(table), cell, 1, 2, n, n + 1,
                     GTK_EXPAND | GTK_FILL, GTK_FILL, 0, 0);
    return w;
}

GtkWidget *spin(double lo, double hi, double step, int digits)
{
    GtkWidget *s = gtk_spin_button_new_with_range(lo, hi, step);
    gtk_spin_button_set_digits(GTK_SPIN_BUTTON(s), (guint)digits);
    return s;
}

GtkWidget *entry(const char *text)
{
    GtkWidget *w = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(w), text ? text : "");
    return w;
}

/* A font family entry followed by its size in points. */
static GtkWidget *font_row(GtkWidget *family, GtkWidget *size)
{
    GtkWidget *box = gtk_hbox_new(FALSE, 6);
    gtk_box_pack_start(GTK_BOX(box), family, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(box), size, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), gtk_label_new("pt"), FALSE, FALSE, 0);
    return box;
}

/* A check box _("automatic") next to a spin button it disables. */
static void on_auto_toggled(GtkToggleButton *b, gpointer spinner)
{
    gtk_widget_set_sensitive(GTK_WIDGET(spinner), !gtk_toggle_button_get_active(b));
}

/* A check box next to a widget it enables (the opposite of on_auto_toggled). */
static void on_enable_toggled(GtkToggleButton *b, gpointer widget)
{
    gtk_widget_set_sensitive(GTK_WIDGET(widget), gtk_toggle_button_get_active(b));
}

/* A check box labelled label, next to widget, which it enables/disables.
 * *out_check gets the check box itself, to read back later. */
static GtkWidget *check_with(GtkWidget **out_check, const char *label, bool on,
                             GtkWidget *widget)
{
    GtkWidget *box = gtk_hbox_new(FALSE, 6);
    *out_check = check(label, on);
    gtk_box_pack_start(GTK_BOX(box), *out_check, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), widget, FALSE, FALSE, 0);
    g_signal_connect(*out_check, "toggled", G_CALLBACK(on_enable_toggled), widget);
    on_enable_toggled(GTK_TOGGLE_BUTTON(*out_check), widget);
    return box;
}

GtkWidget *auto_spin(GtkWidget **check, GtkWidget **spinner, double lo,
                            double hi, int value)
{
    GtkWidget *box = gtk_hbox_new(FALSE, 6);
    *check = gtk_check_button_new_with_label(_("automatic"));
    *spinner = spin(lo, hi, 1, 0);
    gtk_box_pack_start(GTK_BOX(box), *check, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), *spinner, FALSE, FALSE, 0);
    g_signal_connect(*check, "toggled", G_CALLBACK(on_auto_toggled), *spinner);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(*check), value < 0);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(*spinner), value < 0 ? lo : value);
    on_auto_toggled(GTK_TOGGLE_BUTTON(*check), *spinner);
    return box;
}

GtkWidget *check(const char *label, bool on)
{
    GtkWidget *c = gtk_check_button_new_with_label(label);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(c), on);
    return c;
}

GtkWidget *hint_label(const char *text)
{
    GtkWidget *l = gtk_label_new(NULL);
    char *markup = g_markup_printf_escaped("<small>%s</small>", text);
    gtk_label_set_markup(GTK_LABEL(l), markup);
    g_free(markup);
    gtk_label_set_line_wrap(GTK_LABEL(l), TRUE);
    /* GTK2 wraps at a narrow default width; use the window's. */
    gtk_widget_set_size_request(l, 820, -1);
    gtk_misc_set_alignment(GTK_MISC(l), 0, 0);
    return l;
}

GtkWidget *icon_button(const char *stock, const char *label)
{
    GtkWidget *b = gtk_button_new_with_mnemonic(label);
    if (stock)
        gtk_button_set_image(GTK_BUTTON(b), gtk_image_new_from_stock(stock, GTK_ICON_SIZE_BUTTON));
    return b;
}

/* What is written in the file for a path key, else the effective value. */
static const char *raw_or(const editor *e, const char *section, const char *key,
                          const char *value)
{
    const char *raw = kk_ini_get(e->ini, section, key);
    return raw ? raw : value;
}

void join(char *out, size_t size, char *const *items, int n)
{
    out[0] = '\0';
    size_t len = 0;
    for (int i = 0; i < n; i++) {
        int w = snprintf(out + len, size - len, "%s%s", i ? ", " : "", items[i]);
        if (w < 0 || (size_t)w >= size - len)
            return;
        len += (size_t)w;
    }
}

void fmt_num(char *out, size_t size, double v)
{
    snprintf(out, size, "%g", v);
}

/* ---- saving -------------------------------------------------------------- */

/* Writes key when the value differs from the default (NULL: no default) or
 * when the file already has it; an empty value removes the key. */
void put(editor *e, const char *section, const char *key,
                const char *value, const char *dflt)
{
    if (!value[0]) {
        kk_ini_unset(e->ini, section, key);
        return;
    }
    if (dflt && strcmp(value, dflt) == 0 && !kk_ini_get(e->ini, section, key))
        return;
    kk_ini_set(e->ini, section, key, value);
}

void put_int(editor *e, const char *section, const char *key, int v, int dflt)
{
    char s[32], d[32];
    snprintf(s, sizeof s, "%d", v);
    snprintf(d, sizeof d, "%d", dflt);
    put(e, section, key, s, d);
}

void put_num(editor *e, const char *section, const char *key, double v,
             double dflt)
{
    char s[32], d[32];
    fmt_num(s, sizeof s, v);
    fmt_num(d, sizeof d, dflt);
    put(e, section, key, s, d);
}

void put_bool(editor *e, const char *section, const char *key, GtkWidget *w,
                     bool dflt)
{
    bool v = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(w));
    put(e, section, key, v ? "yes" : "no", dflt ? "yes" : "no");
}

void put_auto(editor *e, const char *section, const char *key,
                     GtkWidget *check_w, GtkWidget *spin_w)
{
    if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(check_w)))
        put(e, section, key, "auto", "auto");
    else
        put_int(e, section, key,
                gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(spin_w)), -1);
}

const char *text_of(GtkWidget *w)
{
    return gtk_entry_get_text(GTK_ENTRY(w));
}

int spin_int(GtkWidget *w)
{
    return gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(w));
}

static void collect(editor *e)
{
    kk_config d;
    kk_config_defaults(&d);
    /* [window] */
    put(e, "window", "mode",
        gtk_combo_box_get_active(GTK_COMBO_BOX(e->mode)) == 1 ? "desktop" : "obs",
        "obs");
    char size[32], dsize[32];
    snprintf(size, sizeof size, "%dx%d", spin_int(e->width), spin_int(e->height));
    snprintf(dsize, sizeof dsize, "%dx%d", d.width, d.height);
    put(e, "window", "size", size, dsize);
    put_int(e, "window", "fps", spin_int(e->fps), d.fps);
    /* [avatars] */
    put_num(e, "avatars", "scale",
            gtk_spin_button_get_value(GTK_SPIN_BUTTON(e->scale)), d.scale);
    put_auto(e, "avatars", "ground", e->ground_auto, e->ground);
    put_auto(e, "avatars", "count", e->count_auto, e->count);
    put(e, "avatars", "show", text_of(e->show), NULL);
    put(e, "avatars", "default", text_of(e->default_avatar), NULL);
    put(e, "avatars", "sa_dir", text_of(e->sa_dir), NULL);
    avatar_dirs_collect(e);
    put_bool(e, "avatars", "show_names", e->show_names, d.show_names);
    put_bool(e, "avatars", "name_idle_hide", e->name_idle, d.name_idle_hide);
    put_int(e, "avatars", "name_idle_seconds", spin_int(e->name_idle_secs),
            d.name_idle_seconds);
    put(e, "avatars", "name_position",
        gtk_combo_box_get_active(GTK_COMBO_BOX(e->name_position)) == 1 ? "above" : "below",
        d.name_above ? "above" : "below");
    put_bool(e, "avatars", "show_bubbles", e->show_bubbles, d.show_bubbles);
    put_auto(e, "avatars", "bubble_seconds", e->bubble_auto, e->bubble_secs);
    put(e, "avatars", "name_font", text_of(e->name_font), NULL);
    put_num(e, "avatars", "name_size",
            gtk_spin_button_get_value(GTK_SPIN_BUTTON(e->name_size)), d.name_size);
    put(e, "avatars", "bubble_font", text_of(e->bubble_font), NULL);
    put_num(e, "avatars", "bubble_size",
            gtk_spin_button_get_value(GTK_SPIN_BUTTON(e->bubble_size)), d.bubble_size);
    /* [chat] */
    sources_collect(e);
    put_bool(e, "chat", "third_party_emotes", e->extra_emotes, d.extra_emotes);
    put_bool(e, "chat", "demo", e->demo, d.demo);
    put_int(e, "chat", "max", spin_int(e->max), d.max_avatars);
    put_int(e, "chat", "despawn", spin_int(e->despawn), (int)d.despawn);
    put_bool(e, "chat", "verbose", e->verbose, d.verbose);
    put(e, "chat", "users", text_of(e->users), NULL);
    /* [control] */
    put(e, "control", "socket", text_of(e->socket), NULL);
    kk_config_free(&d);
    commands_collect(e);
    sounds_collect(e);
    wall_collect(e);
    points_collect(e);
    timers_collect(e);
}

/* Checks what will be written the same way kikarinhas will read it. */
static void count_warning(void *ud, int line, const char *msg)
{
    GString *s = ud;
    if (s->len)
        return; /* the first one is enough for the status line */
    if (line > 0)
        g_string_append_printf(s, _("line %d: "), line);
    g_string_append(s, msg);
}

static bool save(editor *e)
{
    collect(e);
    char *text = kk_ini_dump(e->ini);
    GString *first = g_string_new(NULL);
    kk_ini *check_ini = text ? kk_ini_parse(text, count_warning, first) : NULL;
    if (check_ini) {
        kk_config c;
        kk_config_defaults(&c);
        kk_config_apply(&c, check_ini, count_warning, first);
        kk_config_free(&c);
        kk_ini_free(check_ini);
    }
    free(text);
    if (kk_ini_save(e->ini, e->path) < 0) {
        status(e, _("Could not write %s: %s"), e->path, g_strerror(errno));
        g_string_free(first, TRUE);
        return false;
    }
    if (first->len)
        status(e, _("Saved to %s, but with a problem: %s"), e->path, first->str);
    else
        status(e, _("Saved to %s."), e->path);
    g_string_free(first, TRUE);
    return true;
}

static void on_save(GtkButton *b, gpointer ud)
{
    (void)b;
    save(ud);
}

bool editor_socket(editor *e, char *out, size_t size)
{
    const char *s = text_of(e->socket);
    if (strcmp(s, "off") == 0)
        return false;
    if (s[0] == '~')
        return kk_pathf(out, size, "%s%s", g_get_home_dir(), s + 1);
    if (s[0])
        return kk_pathf(out, size, "%s", s);
    return kk_control_default_path(out, size);
}

bool editor_request(editor *e, const char *line, char *reply, size_t size)
{
    char sock[KK_PATH_MAX];
    return editor_socket(e, sock, sizeof sock) &&
           kk_control_request(sock, line, reply, size, 3000) == 0;
}

void editor_refresh_kikarinhas_version(editor *e)
{
    char reply[256], text[160];
    if (!editor_request(e, "{\"type\":\"ping\"}", reply, sizeof reply)) {
        snprintf(text, sizeof text, _("kikarinhas: not running"));
    } else {
        cJSON *r = cJSON_Parse(reply);
        const char *v =
            cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(r, "version"));
        if (v)
            snprintf(text, sizeof text, _("kikarinhas running: v%s"), v);
        else
            snprintf(text, sizeof text, _("kikarinhas running"));
        cJSON_Delete(r);
    }
    gtk_label_set_text(GTK_LABEL(e->version_label), text);
}

const char *editor_sa_dir(editor *e, char *buf, size_t size)
{
    const char *s = text_of(e->sa_dir);
    if (g_ascii_strcasecmp(s, "off") == 0) /* off for avatars; sounds may still come */
        s = "";
    if (s[0] == '~' && kk_pathf(buf, size, "%s%s", g_get_home_dir(), s + 1))
        return buf;
    if (s[0] && kk_pathf(buf, size, "%s", s))
        return buf;
    return kk_sa_find_data_dir(buf, size) ? buf : NULL;
}

static void on_apply(GtkButton *b, gpointer ud)
{
    (void)b;
    editor *e = ud;
    if (!save(e))
        return;
    char sock[KK_PATH_MAX], reply[8192];
    if (!editor_socket(e, sock, sizeof sock)) {
        status(e, _("Saved. The socket is off: restart kikarinhas to apply."));
        return;
    }
    if (!editor_request(e, "{\"type\":\"reload\"}", reply, sizeof reply)) {
        status(e, _("Saved. kikarinhas is not running (%s); it takes effect when it starts."), sock);
        editor_refresh_kikarinhas_version(e);
        return;
    }
    editor_refresh_kikarinhas_version(e);
    cJSON *r = cJSON_Parse(reply);
    const cJSON *warnings = cJSON_GetObjectItemCaseSensitive(r, "warnings");
    int n = cJSON_GetArraySize(warnings);
    if (!cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(r, "ok")))
        status(e, _("Saved, but kikarinhas did not apply it: %s"),
               cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(r, "error")));
    else if (n > 0)
        status(e, _("Applied, with %d warning(s): %s"), n,
               cJSON_GetStringValue(cJSON_GetArrayItem(warnings, 0)));
    else
        status(e, _("Saved and applied."));
    cJSON_Delete(r);
}

/* ---- the window ---------------------------------------------------------- */

/* Wraps a page in a scrolled window: a notebook is as tall as its tallest
 * page, so without this the window can't be smaller than the biggest tab. */
static GtkWidget *scrolled(GtkWidget *content)
{
    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_AUTOMATIC,
                                   GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_add_with_viewport(GTK_SCROLLED_WINDOW(scroll), content);
    gtk_viewport_set_shadow_type(GTK_VIEWPORT(gtk_bin_get_child(GTK_BIN(scroll))),
                                 GTK_SHADOW_NONE);
    return scroll;
}

static void build(editor *e, const kk_config *cfg)
{
    e->window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    char title[64];
    snprintf(title, sizeof title, _("Kikarinhas v%s — settings"), KK_VERSION);
    gtk_window_set_title(GTK_WINDOW(e->window), title);
    /* Fits small screens: leaves room for the panels around the window. */
    GdkScreen *screen = gdk_screen_get_default();
    gtk_window_set_default_size(GTK_WINDOW(e->window),
                                MIN(860, gdk_screen_get_width(screen) - 40),
                                MIN(560, gdk_screen_get_height(screen) - 120));
    g_signal_connect(e->window, "destroy", G_CALLBACK(gtk_main_quit), NULL);

    GtkWidget *vbox = gtk_vbox_new(FALSE, 6);
    gtk_container_set_border_width(GTK_CONTAINER(vbox), 6);
    gtk_container_add(GTK_CONTAINER(e->window), vbox);
    GtkWidget *nb = gtk_notebook_new();
    gtk_box_pack_start(GTK_BOX(vbox), nb, TRUE, TRUE, 0);

    GtkWidget *t = page(nb, _("Window"));
    e->mode = gtk_combo_box_text_new();
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(e->mode), "obs");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(e->mode), "desktop");
    gtk_combo_box_set_active(GTK_COMBO_BOX(e->mode), cfg->desktop ? 1 : 0);
    row(t, _("Mode"), e->mode,
        _("obs: ordinary window for OBS's Window Capture (Xcomposite). "
          "desktop: over the desktop, clicks go through."));
    GtkWidget *size = gtk_hbox_new(FALSE, 6);
    e->width = spin(1, 16384, 1, 0);
    e->height = spin(1, 16384, 1, 0);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(e->width), cfg->width);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(e->height), cfg->height);
    gtk_box_pack_start(GTK_BOX(size), e->width, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(size), gtk_label_new("×"), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(size), e->height, FALSE, FALSE, 0);
    row(t, _("Size"), size, _("Only in obs mode."));
    e->fps = row(t, _("Frames per second"), spin(1, 240, 1, 0), NULL);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(e->fps), cfg->fps);

    t = page(nb, _("Avatars"));
    e->scale = row(t, _("Scale"), spin(0.1, 16, 0.1, 1), NULL);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(e->scale), cfg->scale);
    row(t, _("Ground"), auto_spin(&e->ground_auto, &e->ground, 0, 16384, cfg->ground),
        _("Pixels between the feet and the bottom edge; automatic leaves room for the name."));
    row(t, _("Random"), auto_spin(&e->count_auto, &e->count, 0, 1000, cfg->count),
        _("Avatars that walk around without belonging to anyone in the chat; automatic: 6 without chat, 0 with chat."));
    e->show = row(t, _("Always on screen"), entry(NULL),
                  _("Avatar names separated by commas."));
    char shown[2048];
    join(shown, sizeof shown, (char *const *)cfg->show, cfg->n_show);
    gtk_entry_set_text(GTK_ENTRY(e->show), shown);
    e->default_avatar = row(t, _("Everyone's avatar"), entry(cfg->default_avatar),
                            _("Empty: a random one per person, always the same."));
    e->sa_dir = row(t, _("Stream Avatars folder"),
                    entry(raw_or(e, "avatars", "sa_dir", cfg->sa_dir)),
                    _("The \"data\" folder; empty: searches the Steam libraries; off: "
                      "don't use Stream Avatars."));
    row(t, _("More avatar folders"), avatar_dirs_widget(e, cfg),
        _("Each one may hold a data folder (streamavatars_json.txt), a pack (data.txt) or "
          "folders of those. Your own folder is read first, then these, then Stream "
          "Avatars; on a repeated name the first one wins. Only changes when kikarinhas "
          "restarts."));

    e->name_position = gtk_combo_box_text_new();
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(e->name_position), _("below"));
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(e->name_position), _("above"));
    gtk_combo_box_set_active(GTK_COMBO_BOX(e->name_position), cfg->name_above ? 1 : 0);
    row(t, _("Avatar name"),
        check_with(&e->show_names, _("Show names"), cfg->show_names, e->name_position),
        _("Above or below the avatar."));
    e->name_idle_secs = spin(1, 86400, 5, 0);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(e->name_idle_secs), cfg->name_idle_seconds);
    GtkWidget *idle_box = gtk_hbox_new(FALSE, 6);
    gtk_box_pack_start(GTK_BOX(idle_box), e->name_idle_secs, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(idle_box), gtk_label_new(_("seconds")), FALSE, FALSE, 0);
    row(t, NULL,
        check_with(&e->name_idle, _("Hide the name after inactivity of"), cfg->name_idle_hide,
                   idle_box),
        _("The name shows on arrival, on a message or command and while a bubble is up; "
          "after this quiet time only the drawing stays. Independent of the time until "
          "the avatar leaves."));
    e->show_bubbles = row(t, _("Messages"),
                          check(_("Show a speech bubble with the message"), cfg->show_bubbles), NULL);

    row(t, _("Message bubble stays"),
        auto_spin(&e->bubble_auto, &e->bubble_secs, 1, 600,
                  cfg->bubble_seconds < 0 ? -1 : (int)(cfg->bubble_seconds + 0.5)),
        _("Seconds on screen; automatic: 4 to 12 s, by the length of the text."));

    e->name_font = entry(cfg->name_font);
    e->name_size = spin(4, 200, 1, 1);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(e->name_size), cfg->name_size);
    row(t, _("Name font"), font_row(e->name_font, e->name_size),
        _("Pango family and style (e.g. Sans Bold) and size in points; empty: Sans Bold. "
          "Only changes when kikarinhas restarts."));
    e->bubble_font = entry(cfg->bubble_font);
    e->bubble_size = spin(4, 200, 1, 1);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(e->bubble_size), cfg->bubble_size);
    row(t, _("Bubble font"), font_row(e->bubble_font, e->bubble_size),
        _("Empty: Sans. Applies to the next bubbles."));

    t = page(nb, _("Chat"));
    row(t, _("Lives"), sources_widget(e, cfg),
        _("Every live in the list is read at the same time, YouTube and Twitch together. "
          "Double-click a link to edit it."));
    e->extra_emotes = row(t, NULL,
                          check(_("BTTV, FFZ and 7TV emotes on Twitch, 7TV on Kick"), cfg->extra_emotes),
                          _("Downloads the global lists and the channel's, so those emotes "
                            "also fly on the emote wall."));
    e->demo = row(t, NULL, check(_("Fake chat (to test without a stream)"), cfg->demo), NULL);
    e->max = row(t, _("Avatars at the same time"), spin(1, 1000, 1, 0), NULL);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(e->max), cfg->max_avatars);
    e->despawn = row(t, _("Leaves after"), spin(5, 86400, 10, 0),
                     _("Seconds of silence until the avatar leaves."));
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(e->despawn), cfg->despawn);
    e->verbose = row(t, NULL, check(_("Print the messages in the terminal"), cfg->verbose), NULL);
    e->users = row(t, _("People file"), entry(raw_or(e, "chat", "users", cfg->users)),
                   _("Avatar, color and accessories of each person; empty: "
                     "~/.local/share/kikarinhas/users.tsv."));
    e->socket = row(t, _("Control socket"),
                    entry(raw_or(e, "control", "socket", cfg->socket)),
                    _("For bridges and the Apply button; empty: the default; off: disabled."));

    /* Right after the Avatars tab, which sets the folders it lists. */
    gtk_notebook_insert_page(GTK_NOTEBOOK(nb), scrolled(avatars_page(e)),
                             gtk_label_new(_("Installed avatars")), 2);
    gtk_notebook_append_page(GTK_NOTEBOOK(nb), scrolled(commands_page(e, cfg)),
                             gtk_label_new(_("Commands")));
    gtk_notebook_append_page(GTK_NOTEBOOK(nb), scrolled(sounds_page(e, cfg)),
                             gtk_label_new(_("Sounds")));
    gtk_notebook_append_page(GTK_NOTEBOOK(nb), scrolled(wall_page(e, cfg)),
                             gtk_label_new(_("Emote wall")));
    gtk_notebook_append_page(GTK_NOTEBOOK(nb), scrolled(points_page(e, cfg)),
                             gtk_label_new(_("Points")));
    gtk_notebook_append_page(GTK_NOTEBOOK(nb), scrolled(timers_page(e, cfg)),
                             gtk_label_new(_("Timers")));
    gtk_notebook_append_page(GTK_NOTEBOOK(nb), scrolled(audience_page(e, cfg)),
                             gtk_label_new(_("Viewers")));

    GtkWidget *footer = gtk_hbox_new(FALSE, 12);
    e->status = gtk_label_new(NULL);
    gtk_misc_set_alignment(GTK_MISC(e->status), 0, 0.5);
    gtk_label_set_ellipsize(GTK_LABEL(e->status), PANGO_ELLIPSIZE_END);
    gtk_box_pack_start(GTK_BOX(footer), e->status, TRUE, TRUE, 0);
    /* Persistent (unlike e->status, which carries the last action's
     * message): which kikarinhas, if any, is listening on the socket. */
    e->version_label = gtk_label_new(NULL);
    gtk_misc_set_alignment(GTK_MISC(e->version_label), 1, 0.5);
    gtk_widget_set_sensitive(e->version_label, FALSE);
    gtk_box_pack_start(GTK_BOX(footer), e->version_label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), footer, FALSE, FALSE, 0);
    editor_refresh_kikarinhas_version(e);

    GtkWidget *buttons = gtk_hbutton_box_new();
    gtk_button_box_set_layout(GTK_BUTTON_BOX(buttons), GTK_BUTTONBOX_END);
    gtk_box_set_spacing(GTK_BOX(buttons), 6);
    GtkWidget *close_b = icon_button(GTK_STOCK_CLOSE, _("_Close"));
    GtkWidget *save_b = icon_button(GTK_STOCK_SAVE, _("_Save"));
    GtkWidget *apply_b = icon_button(GTK_STOCK_APPLY, _("Save and _apply"));
    g_signal_connect_swapped(close_b, "clicked", G_CALLBACK(gtk_widget_destroy), e->window);
    g_signal_connect(save_b, "clicked", G_CALLBACK(on_save), e);
    g_signal_connect(apply_b, "clicked", G_CALLBACK(on_apply), e);
    gtk_container_add(GTK_CONTAINER(buttons), close_b);
    gtk_container_add(GTK_CONTAINER(buttons), save_b);
    gtk_container_add(GTK_CONTAINER(buttons), apply_b);
    gtk_box_pack_start(GTK_BOX(vbox), buttons, FALSE, FALSE, 0);
}

/* Translations: ../locale next to the executable (a build tree), else the
 * installed ones. */
static void setup_gettext(void)
{
    char dir[KK_PATH_MAX], self[KK_PATH_MAX];
    const char *localedir = LOCALEDIR;
    ssize_t n = readlink("/proc/self/exe", self, sizeof self - 1);
    if (n > 0) {
        self[n] = '\0';
        char *slash = strrchr(self, '/');
        if (slash) {
            *slash = '\0';
            if (kk_pathf(dir, sizeof dir, "%s/locale", self) && g_file_test(dir, G_FILE_TEST_IS_DIR))
                localedir = dir;
        }
    }
    bindtextdomain(GETTEXT_DOMAIN, localedir);
    bind_textdomain_codeset(GETTEXT_DOMAIN, "UTF-8");
    textdomain(GETTEXT_DOMAIN);
}

int main(int argc, char **argv)
{
    gtk_init(&argc, &argv); /* also does setlocale(LC_ALL, "") */
    setup_gettext();
    /* The file uses "1.5", whatever the language of the desktop. */
    setlocale(LC_NUMERIC, "C");

    editor e = {0};
    if (argc == 2 && (strcmp(argv[1], "-V") == 0 || strcmp(argv[1], "--version") == 0)) {
        printf("kikarinhas-config %s\n", KK_VERSION);
        return 0;
    }
    if (argc > 2 || (argc == 2 && argv[1][0] == '-')) {
        fprintf(stderr, _("Usage: kikarinhas-config [FILE]\n"
                          "       kikarinhas-config -V, --version\n"
                          "Edits ~/.config/kikarinhas/kikarinhas.ini (or FILE).\n"));
        return 2;
    }
    if (argc == 2)
        snprintf(e.path, sizeof e.path, "%s", argv[1]);
    else if (!kk_config_default_path(e.path, sizeof e.path)) {
        fprintf(stderr, _("kikarinhas-config: no $HOME\n"));
        return 1;
    }

    GString *problem = g_string_new(NULL);
    char *text = kk_read_file(e.path, NULL);
    if (!text && errno != ENOENT) {
        fprintf(stderr, _("kikarinhas-config: could not read %s: %s\n"), e.path,
                g_strerror(errno));
        return 1;
    }
    e.ini = kk_ini_parse(text ? text
                              : _("# Kikarinhas: generated by kikarinhas-config. You may "
                                  "edit it by hand;\n# comments are kept.\n"),
                         count_warning, problem);
    free(text);
    kk_config cfg;
    kk_config_defaults(&cfg);
    if (e.ini)
        kk_config_apply(&cfg, e.ini, count_warning, problem);
    if (!e.ini) {
        fprintf(stderr, _("kikarinhas-config: out of memory\n"));
        return 1;
    }

    build(&e, &cfg);
    if (problem->len)
        status(&e, "%s: %s", e.path, problem->str);
    else
        status(&e, "%s", e.path);
    g_string_free(problem, TRUE);
    kk_config_free(&cfg);

    gtk_widget_show_all(e.window);
    gtk_main();
    sounds_free(&e);
    sources_free(&e);
    avatars_free(&e);
    audience_free(&e);
    wall_free(&e);
    points_free(&e);
    timers_free(&e);
    kk_ini_free(e.ini);
    commands_free(&e);
    return 0;
}
