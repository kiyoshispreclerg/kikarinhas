/* SPDX-License-Identifier: GPL-3.0-or-later */
/* kikarinhas-config: where avatars come from ([avatars] paths and sa_dir).
 *
 * The folders list sits in the Avatares tab; kikarinhas' own folder
 * (~/.local/share/kikarinhas/avatars) is always read first and is not in the
 * list, only a button to open it. editor_load_library() reads every folder
 * as the widgets say now; editor_library() keeps one copy for the tabs that
 * list avatars, read the first time one asks (it takes a moment). */
#include <stdlib.h>
#include <string.h>

#include "editor.h"
#include "i18n.h"
#include "library.h"

struct avatars_tab {
    GtkListStore *dirs;
    GtkWidget *dirs_tree;
    kk_sa_library lib;
    bool loaded;
};

static void add_dir(avatars_tab *t, const char *dir)
{
    GtkTreeIter it;
    gtk_list_store_append(t->dirs, &it);
    gtk_list_store_set(t->dirs, &it, 0, dir, -1);
}

/* Same splitting as kk_library_each_dir, without expanding "~". */
static void add_list(avatars_tab *t, const char *list)
{
    gchar **items = g_strsplit(list ? list : "", ",", -1);
    for (int i = 0; items[i]; i++) {
        g_strstrip(items[i]);
        if (items[i][0])
            add_dir(t, items[i]);
    }
    g_strfreev(items);
}

/* "/home/me/x" -> "~/x": shorter, and survives a new user name. */
static gchar *tilde(const char *path)
{
    const char *home = g_get_home_dir();
    size_t n = strlen(home);
    if (strncmp(path, home, n) == 0 && path[n] == '/')
        return g_strdup_printf("~%s", path + n);
    return g_strdup(path);
}

static void on_add(GtkButton *b, gpointer ud)
{
    (void)b;
    editor *e = ud;
    GtkWidget *d = gtk_file_chooser_dialog_new(
        _("Avatar folder"), GTK_WINDOW(e->window), GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER,
        _("_Cancel"), GTK_RESPONSE_CANCEL, _("_Add"), GTK_RESPONSE_ACCEPT, NULL);
    if (gtk_dialog_run(GTK_DIALOG(d)) == GTK_RESPONSE_ACCEPT) {
        gchar *dir = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(d));
        gchar *shown = tilde(dir);
        if (strchr(dir, ','))
            status(e, _("Folder names with a comma can't go in the list: %s"), dir);
        else
            add_dir(e->avatars, shown);
        g_free(shown);
        g_free(dir);
    }
    gtk_widget_destroy(d);
}

static void on_remove(GtkButton *b, gpointer ud)
{
    (void)b;
    editor *e = ud;
    GtkTreeIter it;
    GtkTreeSelection *sel = gtk_tree_view_get_selection(GTK_TREE_VIEW(e->avatars->dirs_tree));
    if (gtk_tree_selection_get_selected(sel, NULL, &it))
        gtk_list_store_remove(e->avatars->dirs, &it);
    else
        status(e, _("Pick a folder in the list."));
}

/* Creates the folder if needed and shows it in the file manager. */
static void on_open_own(GtkButton *b, gpointer ud)
{
    (void)b;
    editor *e = ud;
    char dir[KK_PATH_MAX];
    if (!kk_library_own_dir(dir, sizeof dir))
        return;
    g_mkdir_with_parents(dir, 0755);
    char *argv[] = {(char *)"xdg-open", dir, NULL};
    GError *err = NULL;
    if (!g_spawn_async(NULL, argv, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL, NULL, &err)) {
        status(e, _("Could not open %s: %s"), dir, err->message);
        g_error_free(err);
    }
}

GtkWidget *avatar_dirs_widget(editor *e, const kk_config *cfg)
{
    avatars_tab *t = e->avatars = g_new0(avatars_tab, 1);
    t->dirs = gtk_list_store_new(1, G_TYPE_STRING);
    add_list(t, cfg->avatar_paths);

    GtkWidget *box = gtk_vbox_new(FALSE, 6);
    t->dirs_tree = gtk_tree_view_new_with_model(GTK_TREE_MODEL(t->dirs));
    gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(t->dirs_tree), FALSE);
    GtkCellRenderer *r = gtk_cell_renderer_text_new();
    g_object_set(r, "ellipsize", PANGO_ELLIPSIZE_MIDDLE, NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(t->dirs_tree),
                                gtk_tree_view_column_new_with_attributes("", r, "text", 0,
                                                                         NULL));
    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_AUTOMATIC,
                                   GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(scroll), GTK_SHADOW_IN);
    gtk_widget_set_size_request(scroll, -1, 80);
    gtk_container_add(GTK_CONTAINER(scroll), t->dirs_tree);
    gtk_box_pack_start(GTK_BOX(box), scroll, TRUE, TRUE, 0);

    GtkWidget *buttons = gtk_hbox_new(FALSE, 6);
    GtkWidget *add = icon_button(GTK_STOCK_ADD, _("_Add folder"));
    GtkWidget *rm = icon_button(GTK_STOCK_REMOVE, _("_Remove"));
    GtkWidget *own = icon_button(GTK_STOCK_OPEN, _("Open my avatars folder"));
    g_signal_connect(add, "clicked", G_CALLBACK(on_add), e);
    g_signal_connect(rm, "clicked", G_CALLBACK(on_remove), e);
    g_signal_connect(own, "clicked", G_CALLBACK(on_open_own), e);
    gtk_box_pack_start(GTK_BOX(buttons), add, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(buttons), rm, FALSE, FALSE, 0);
    gtk_box_pack_end(GTK_BOX(buttons), own, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), buttons, FALSE, FALSE, 0);
    return box;
}

/* The list as the file has it: "~/a, /b". */
static gchar *dirs_list(editor *e)
{
    GString *s = g_string_new(NULL);
    GtkTreeModel *m = GTK_TREE_MODEL(e->avatars->dirs);
    GtkTreeIter it;
    for (bool more = gtk_tree_model_get_iter_first(m, &it); more;
         more = gtk_tree_model_iter_next(m, &it)) {
        gchar *dir;
        gtk_tree_model_get(m, &it, 0, &dir, -1);
        g_string_append_printf(s, "%s%s", s->len ? ", " : "", dir);
        g_free(dir);
    }
    return g_string_free(s, FALSE);
}

void avatar_dirs_collect(editor *e)
{
    gchar *list = dirs_list(e);
    put(e, "avatars", "paths", list, NULL);
    g_free(list);
}

static int load_library(editor *e, kk_sa_library *lib)
{
    const char *sa = text_of(e->sa_dir);
    char sa_dir[KK_PATH_MAX], used[KK_PATH_MAX];
    const char *sa_arg = NULL;
    if (g_ascii_strcasecmp(sa, "off") == 0)
        sa_arg = "";
    else if (sa[0] && kk_pathf(sa_dir, sizeof sa_dir, "%s", sa))
        sa_arg = sa_dir;
    gchar *list = dirs_list(e);
    int rc = kk_library_load(lib, sa_arg, list, used, sizeof used, false);
    g_free(list);
    return rc;
}

const kk_sa_library *editor_library(editor *e)
{
    avatars_tab *t = e->avatars;
    if (!t->loaded) {
        t->loaded = true;
        GdkCursor *busy = gdk_cursor_new(GDK_WATCH);
        gdk_window_set_cursor(gtk_widget_get_window(e->window), busy);
        while (gtk_events_pending())
            gtk_main_iteration();
        if (load_library(e, &t->lib) < 0)
            status(e, _("No avatars found in any folder."));
        gdk_window_set_cursor(gtk_widget_get_window(e->window), NULL);
        gdk_cursor_unref(busy);
    }
    return t->lib.count ? &t->lib : NULL;
}

void editor_library_forget(editor *e)
{
    kk_sa_free(&e->avatars->lib);
    e->avatars->loaded = false;
}

void avatars_free(editor *e)
{
    if (!e->avatars)
        return;
    kk_sa_free(&e->avatars->lib);
    g_object_unref(e->avatars->dirs);
    g_free(e->avatars);
    e->avatars = NULL;
}
