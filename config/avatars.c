/* SPDX-License-Identifier: GPL-3.0-or-later */
/* kikarinhas-config: where avatars come from ([avatars] paths and sa_dir),
 * and the "Avatares instalados" tab that lists every one of them.
 *
 * The folders list sits in the Avatares tab; kikarinhas' own folder
 * (~/.local/share/kikarinhas/avatars) is always read first and is not in the
 * list, only a button to open it. editor_load_library() reads every folder
 * as the widgets say now; editor_library() keeps one copy for the tabs that
 * list avatars, read the first time one asks (it takes a moment). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "editor.h"
#include "i18n.h"
#include "library.h"

enum {
    V_PIXBUF,
    V_NAME,
    V_KEY,
    V_SIZE,
    V_ANIMS,
    V_PALETTES,
    V_GEAR,
    V_DIR,
    V_INDEX, /* in the library, for the thumbnail loader */
    N_VCOLS,
};

#define THUMB 48 /* px, the thumbnail box */

struct avatars_tab {
    GtkListStore *dirs;
    GtkWidget *dirs_tree;
    kk_sa_library lib;
    bool loaded;
    GdkPixbuf **thumb_cache; /* by library index, made on first use */
    signed char *thumb_done; /* 0 not tried, 1 made (may be NULL: no picture) */
    /* the list tab */
    GtkListStore *store;
    GtkTreeModel *filter;
    GtkWidget *search, *count;
    bool listed;
    guint thumbs;       /* idle source loading thumbnails, 0 when done */
    GtkTreeIter thumb_at; /* next row to give a thumbnail */
    bool thumb_more;
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

static void drop_thumbs(avatars_tab *t)
{
    for (int i = 0; t->thumb_cache && i < t->lib.count; i++)
        if (t->thumb_cache[i])
            g_object_unref(t->thumb_cache[i]);
    g_free(t->thumb_cache);
    g_free(t->thumb_done);
    t->thumb_cache = NULL;
    t->thumb_done = NULL;
}

static GdkPixbuf *thumbnail(const kk_sa_avatar *a);

/* The thumbnail of the i-th avatar of editor_library, made once; NULL if
 * it has no picture. Owned by the cache. */
static GdkPixbuf *thumb_of(editor *e, int i)
{
    avatars_tab *t = e->avatars;
    if (i < 0 || i >= t->lib.count)
        return NULL;
    if (!t->thumb_cache) {
        t->thumb_cache = g_new0(GdkPixbuf *, (gsize)t->lib.count);
        t->thumb_done = g_new0(signed char, (gsize)t->lib.count);
    }
    if (!t->thumb_done[i]) {
        t->thumb_done[i] = 1;
        t->thumb_cache[i] = thumbnail(&t->lib.avatars[i]);
    }
    return t->thumb_cache[i];
}

void editor_library_forget(editor *e)
{
    avatars_tab *t = e->avatars;
    if (t->thumbs) {
        g_source_remove(t->thumbs);
        t->thumbs = 0;
    }
    drop_thumbs(t);
    kk_sa_free(&t->lib);
    t->loaded = false;
    t->listed = false;
}

/* ---- the list of installed avatars --------------------------------------- */

/* The part of a cell that is not transparent: cells have room around the
 * figure for jumps and gear, which would make thumbnails tiny. */
static void opaque_box(GdkPixbuf *p, int *x, int *y, int *w, int *h)
{
    if (!gdk_pixbuf_get_has_alpha(p) || gdk_pixbuf_get_n_channels(p) != 4)
        return;
    const guchar *px = gdk_pixbuf_get_pixels(p);
    int stride = gdk_pixbuf_get_rowstride(p);
    int x0 = *w, y0 = *h, x1 = -1, y1 = -1;
    for (int j = 0; j < *h; j++)
        for (int i = 0; i < *w; i++)
            if (px[(*y + j) * stride + (*x + i) * 4 + 3] > 16) {
                x0 = i < x0 ? i : x0;
                x1 = i > x1 ? i : x1;
                y0 = j < y0 ? j : y0;
                y1 = j > y1 ? j : y1;
            }
    if (x1 < 0)
        return; /* all transparent: keep the cell */
    *x += x0;
    *y += y0;
    *w = x1 - x0 + 1;
    *h = y1 - y0 + 1;
}

/* First frame of the idle animation (else walk, else any), trimmed and
 * fitted in the thumbnail box: pixel art grows by whole steps with nearest,
 * so it stays crisp; anything else is smoothed. */
static GdkPixbuf *thumbnail(const kk_sa_avatar *a)
{
    if (!a->image || a->frame_w <= 0 || a->frame_h <= 0)
        return NULL;
    GdkPixbuf *sheet = gdk_pixbuf_new_from_file(a->image, NULL);
    if (!sheet)
        return NULL;
    int row = -1;
    if (a->anims[KK_ANIM_IDLE].frames)
        row = KK_ANIM_IDLE;
    else if (a->anims[KK_ANIM_WALK].frames)
        row = KK_ANIM_WALK;
    for (int i = 0; row < 0 && i < a->n_anims; i++)
        if (a->anims[i].frames)
            row = i;
    GdkPixbuf *thumb = NULL;
    int x = 0, y = row * a->frame_h, w = a->frame_w, h = a->frame_h;
    if (row >= 0 && (row + 1) * h <= gdk_pixbuf_get_height(sheet) &&
        w <= gdk_pixbuf_get_width(sheet)) {
        opaque_box(sheet, &x, &y, &w, &h);
        GdkPixbuf *cell = gdk_pixbuf_new_subpixbuf(sheet, x, y, w, h);
        double scale = (double)THUMB / (w > h ? w : h);
        bool crisp = !a->smooth && scale >= 1;
        if (crisp)
            scale = (int)scale;
        int tw = (int)(w * scale + 0.5), th = (int)(h * scale + 0.5);
        thumb = gdk_pixbuf_scale_simple(cell, tw > 0 ? tw : 1, th > 0 ? th : 1,
                                        crisp ? GDK_INTERP_NEAREST : GDK_INTERP_BILINEAR);
        g_object_unref(cell);
    }
    g_object_unref(sheet);
    return thumb;
}

/* A few thumbnails per idle call, so the window keeps answering while 200
 * sheets are read. */
static gboolean load_thumbs(gpointer ud)
{
    editor *e = ud;
    avatars_tab *t = e->avatars;
    GtkTreeModel *m = GTK_TREE_MODEL(t->store);
    for (int n = 0; n < 6 && t->thumb_more; n++) {
        int i;
        gtk_tree_model_get(m, &t->thumb_at, V_INDEX, &i, -1);
        GdkPixbuf *p = thumb_of(e, i);
        if (p)
            gtk_list_store_set(t->store, &t->thumb_at, V_PIXBUF, p, -1);
        t->thumb_more = gtk_tree_model_iter_next(m, &t->thumb_at);
    }
    if (t->thumb_more)
        return TRUE;
    t->thumbs = 0;
    return FALSE;
}

static void update_count(editor *e)
{
    avatars_tab *t = e->avatars;
    int all = gtk_tree_model_iter_n_children(GTK_TREE_MODEL(t->store), NULL);
    int shown = gtk_tree_model_iter_n_children(t->filter, NULL);
    char s[64];
    if (shown == all)
        snprintf(s, sizeof s, ngettext("%d avatar", "%d avatars", all), all);
    else
        snprintf(s, sizeof s, _("%d of %d avatars"), shown, all);
    gtk_label_set_text(GTK_LABEL(t->count), s);
}

/* "idle, walk, jump; dance, hug": the base slots, then the custom ones by
 * name (what !emote takes). */
static void anim_names(char *out, size_t size, const kk_sa_avatar *a)
{
    static const char *const base[] = {"idle", "walk", "sit", "stand", "jump"};
    GString *s = g_string_new(NULL);
    for (int i = 0; i < KK_ANIM_CUSTOM1 && i < a->n_anims; i++)
        if (a->anims[i].frames)
            g_string_append_printf(s, "%s%s", s->len ? ", " : "", base[i]);
    bool first = true;
    for (int i = KK_ANIM_CUSTOM1; i < a->n_anims; i++)
        if (a->anims[i].frames) {
            const char *name = a->anims[i].custom_name;
            g_string_append_printf(s, "%s", first ? "; " : ", ");
            if (name && name[0])
                g_string_append(s, name);
            else
                g_string_append_printf(s, "custom%d", i - KK_ANIM_CUSTOM1 + 1);
            first = false;
        }
    snprintf(out, size, "%s", s->str);
    g_string_free(s, TRUE);
}

static void fill_list(editor *e)
{
    avatars_tab *t = e->avatars;
    if (t->listed)
        return;
    t->listed = true;
    gtk_list_store_clear(t->store);
    const kk_sa_library *lib = editor_library(e);
    for (int i = 0; lib && i < lib->count; i++) {
        const kk_sa_avatar *a = &lib->avatars[i];
        char size[32], anims[512];
        if (a->image)
            snprintf(size, sizeof size, "%d×%d", a->frame_w, a->frame_h);
        else
            snprintf(size, sizeof size, "%s", _("no image"));
        anim_names(anims, sizeof anims, a);
        gchar *dir = tilde(a->dir ? a->dir : "");
        GtkTreeIter it;
        gtk_list_store_append(t->store, &it);
        gtk_list_store_set(t->store, &it, V_NAME, a->name, V_KEY, a->key, V_SIZE, size,
                           V_ANIMS, anims, V_PALETTES, a->n_palettes, V_GEAR,
                           a->n_gear_sets, V_DIR, dir, V_INDEX, i, -1);
        g_free(dir);
    }
    update_count(e);
    t->thumb_more = gtk_tree_model_get_iter_first(GTK_TREE_MODEL(t->store), &t->thumb_at);
    if (t->thumb_more && !t->thumbs)
        t->thumbs = g_idle_add(load_thumbs, e);
}

static void on_list_map(GtkWidget *w, gpointer ud)
{
    (void)w;
    fill_list(ud);
}

/* Reads the folders again, as the Avatares tab says now. */
static void on_reload(GtkButton *b, gpointer ud)
{
    (void)b;
    editor *e = ud;
    editor_library_forget(e);
    fill_list(e);
    status(e, _("Avatar folders read again."));
}

static gboolean visible(GtkTreeModel *m, GtkTreeIter *it, gpointer ud)
{
    editor *e = ud;
    if (!e->avatars || !e->avatars->search)
        return TRUE;
    const char *q = gtk_entry_get_text(GTK_ENTRY(e->avatars->search));
    if (!q[0])
        return TRUE;
    gchar *needle = g_utf8_casefold(q, -1);
    gchar *name, *key, *anims, *dir;
    gtk_tree_model_get(m, it, V_NAME, &name, V_KEY, &key, V_ANIMS, &anims, V_DIR, &dir, -1);
    bool yes = false;
    const char *hay[] = {name, key, anims, dir};
    for (int i = 0; i < 4 && !yes; i++) {
        gchar *f = g_utf8_casefold(hay[i] ? hay[i] : "", -1);
        yes = strstr(f, needle) != NULL;
        g_free(f);
    }
    g_free(name);
    g_free(key);
    g_free(anims);
    g_free(dir);
    g_free(needle);
    return yes;
}

static void on_search(GtkEditable *ed, gpointer ud)
{
    (void)ed;
    editor *e = ud;
    gtk_tree_model_filter_refilter(GTK_TREE_MODEL_FILTER(e->avatars->filter));
    update_count(e);
}

GtkWidget *avatars_page(editor *e)
{
    avatars_tab *t = e->avatars;
    GtkWidget *box = gtk_vbox_new(FALSE, 6);
    gtk_container_set_border_width(GTK_CONTAINER(box), 12);
    g_signal_connect(box, "map", G_CALLBACK(on_list_map), e);

    GtkWidget *top = gtk_hbox_new(FALSE, 6);
    gtk_box_pack_start(GTK_BOX(top), gtk_label_new(_("Search")), FALSE, FALSE, 0);
    t->search = gtk_entry_new();
    g_signal_connect(t->search, "changed", G_CALLBACK(on_search), e);
    gtk_box_pack_start(GTK_BOX(top), t->search, TRUE, TRUE, 0);
    t->count = gtk_label_new(NULL);
    gtk_box_pack_start(GTK_BOX(top), t->count, FALSE, FALSE, 0);
    GtkWidget *reload = icon_button(GTK_STOCK_REFRESH, _("_Read again"));
    g_signal_connect(reload, "clicked", G_CALLBACK(on_reload), e);
    gtk_box_pack_start(GTK_BOX(top), reload, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), top, FALSE, FALSE, 0);

    t->store = gtk_list_store_new(N_VCOLS, GDK_TYPE_PIXBUF, G_TYPE_STRING, G_TYPE_STRING,
                                  G_TYPE_STRING, G_TYPE_STRING, G_TYPE_INT, G_TYPE_INT,
                                  G_TYPE_STRING, G_TYPE_INT);
    t->filter = gtk_tree_model_filter_new(GTK_TREE_MODEL(t->store), NULL);
    gtk_tree_model_filter_set_visible_func(GTK_TREE_MODEL_FILTER(t->filter), visible, e, NULL);
    GtkTreeModel *sort = gtk_tree_model_sort_new_with_model(t->filter);
    GtkWidget *tree = gtk_tree_view_new_with_model(sort);
    g_object_unref(sort);
    gtk_tree_view_set_rules_hint(GTK_TREE_VIEW(tree), TRUE);
    gtk_tree_view_set_search_entry(GTK_TREE_VIEW(tree), GTK_ENTRY(t->search));

    GtkCellRenderer *pr = gtk_cell_renderer_pixbuf_new();
    gtk_cell_renderer_set_fixed_size(pr, THUMB + 8, THUMB + 4);
    gtk_tree_view_append_column(GTK_TREE_VIEW(tree),
                                gtk_tree_view_column_new_with_attributes(
                                    "", pr, "pixbuf", V_PIXBUF, NULL));
    static const struct {
        const char *title;
        int col;
        bool expand;
    } cols[] = {
        {N_("Name"), V_NAME, false},     {N_("Key"), V_KEY, false},
        {N_("Cell"), V_SIZE, false},     {N_("Animations"), V_ANIMS, true},
        {N_("Palettes"), V_PALETTES, false}, {N_("Gear sets"), V_GEAR, false},
        {N_("Folder"), V_DIR, true},
    };
    for (size_t i = 0; i < sizeof cols / sizeof cols[0]; i++) {
        GtkCellRenderer *r = gtk_cell_renderer_text_new();
        if (cols[i].expand)
            g_object_set(r, "ellipsize", cols[i].col == V_DIR ? PANGO_ELLIPSIZE_START
                                                               : PANGO_ELLIPSIZE_END,
                         NULL);
        GtkTreeViewColumn *c = gtk_tree_view_column_new_with_attributes(
            _(cols[i].title), r, "text", cols[i].col, NULL);
        gtk_tree_view_column_set_resizable(c, TRUE);
        gtk_tree_view_column_set_expand(c, cols[i].expand);
        gtk_tree_view_column_set_sort_column_id(c, cols[i].col);
        gtk_tree_view_append_column(GTK_TREE_VIEW(tree), c);
    }

    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_AUTOMATIC,
                                   GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(scroll), GTK_SHADOW_IN);
    gtk_container_add(GTK_CONTAINER(scroll), tree);
    gtk_box_pack_start(GTK_BOX(box), scroll, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(box),
                       hint_label(_("Every avatar of every folder (Avatars tab), as kikarinhas "
                                    "will see them: on a repeated name, only the first one is "
                                    "here. The name or the key is what people type in !avatar; "
                                    "the animations after \";\" are the ones !emote takes. "
                                    "\"Read again\" after changing the folders.")),
                       FALSE, FALSE, 0);
    return box;
}

/* ---- picking an avatar --------------------------------------------------- */

enum { P_PIXBUF, P_NAME, P_KEY, P_INDEX, N_PCOLS };
#define RESPONSE_RANDOM 1

typedef struct {
    editor *e;
    GtkListStore *store;
    GtkTreeModel *filter;
    GtkWidget *dlg, *search, *view, *count;
    GtkTreeIter at; /* next row for a thumbnail */
    bool more;
    guint idle;
} picker;

static gboolean picker_thumbs(gpointer ud)
{
    picker *p = ud;
    GtkTreeModel *m = GTK_TREE_MODEL(p->store);
    for (int n = 0; n < 8 && p->more; n++) {
        int i;
        gtk_tree_model_get(m, &p->at, P_INDEX, &i, -1);
        GdkPixbuf *pix = thumb_of(p->e, i);
        if (pix)
            gtk_list_store_set(p->store, &p->at, P_PIXBUF, pix, -1);
        p->more = gtk_tree_model_iter_next(m, &p->at);
    }
    if (p->more)
        return TRUE;
    p->idle = 0;
    return FALSE;
}

static gboolean picker_visible(GtkTreeModel *m, GtkTreeIter *it, gpointer ud)
{
    picker *p = ud;
    const char *q = p->search ? gtk_entry_get_text(GTK_ENTRY(p->search)) : "";
    if (!q[0])
        return TRUE;
    gchar *name, *key;
    gtk_tree_model_get(m, it, P_NAME, &name, P_KEY, &key, -1);
    gchar *needle = g_utf8_casefold(q, -1);
    gchar *a = g_utf8_casefold(name ? name : "", -1), *b = g_utf8_casefold(key ? key : "", -1);
    bool yes = strstr(a, needle) || strstr(b, needle);
    g_free(a);
    g_free(b);
    g_free(needle);
    g_free(name);
    g_free(key);
    return yes;
}

static void picker_count(picker *p)
{
    int n = gtk_tree_model_iter_n_children(p->filter, NULL);
    char s[64];
    snprintf(s, sizeof s, ngettext("%d avatar", "%d avatars", n), n);
    gtk_label_set_text(GTK_LABEL(p->count), s);
}

/* Typing filters; the first match is selected, so Enter takes it. */
static void on_picker_search(GtkEditable *ed, gpointer ud)
{
    (void)ed;
    picker *p = ud;
    gtk_tree_model_filter_refilter(GTK_TREE_MODEL_FILTER(p->filter));
    picker_count(p);
    GtkTreePath *first = gtk_tree_path_new_first();
    GtkTreeIter it;
    if (gtk_tree_model_get_iter(p->filter, &it, first)) {
        gtk_icon_view_select_path(GTK_ICON_VIEW(p->view), first);
        gtk_icon_view_scroll_to_path(GTK_ICON_VIEW(p->view), first, FALSE, 0, 0);
    }
    gtk_tree_path_free(first);
}

static void on_picker_activated(GtkIconView *v, GtkTreePath *path, gpointer ud)
{
    (void)v;
    (void)path;
    gtk_dialog_response(GTK_DIALOG(((picker *)ud)->dlg), GTK_RESPONSE_OK);
}

static void on_picker_enter(GtkEntry *en, gpointer ud)
{
    (void)en;
    gtk_dialog_response(GTK_DIALOG(((picker *)ud)->dlg), GTK_RESPONSE_OK);
}

gchar *editor_pick_avatar(editor *e, const char *title, const char *current)
{
    const kk_sa_library *lib = editor_library(e);
    if (!lib) {
        status(e, _("No avatars found in any folder."));
        return NULL;
    }
    picker p = {.e = e};
    p.store = gtk_list_store_new(N_PCOLS, GDK_TYPE_PIXBUF, G_TYPE_STRING, G_TYPE_STRING,
                                 G_TYPE_INT);
    GtkTreeIter it;
    for (int i = 0; i < lib->count; i++) {
        if (!lib->avatars[i].image)
            continue;
        gtk_list_store_append(p.store, &it);
        gtk_list_store_set(p.store, &it, P_NAME, lib->avatars[i].name, P_KEY,
                           lib->avatars[i].key, P_INDEX, i, -1);
    }
    p.filter = gtk_tree_model_filter_new(GTK_TREE_MODEL(p.store), NULL);
    gtk_tree_model_filter_set_visible_func(GTK_TREE_MODEL_FILTER(p.filter), picker_visible, &p,
                                           NULL);

    p.dlg = gtk_dialog_new_with_buttons(title, GTK_WINDOW(e->window),
                                        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                        NULL, NULL);
    gtk_dialog_add_button(GTK_DIALOG(p.dlg), _("_Random (always the same)"), RESPONSE_RANDOM);
    gtk_dialog_add_button(GTK_DIALOG(p.dlg), _("_Cancel"), GTK_RESPONSE_CANCEL);
    gtk_dialog_add_button(GTK_DIALOG(p.dlg), _("_OK"), GTK_RESPONSE_OK);
    gtk_dialog_set_default_response(GTK_DIALOG(p.dlg), GTK_RESPONSE_OK);
    gtk_window_set_default_size(GTK_WINDOW(p.dlg), 760, 520);

    GtkWidget *box = gtk_vbox_new(FALSE, 6);
    gtk_container_set_border_width(GTK_CONTAINER(box), 12);
    GtkWidget *top = gtk_hbox_new(FALSE, 6);
    gtk_box_pack_start(GTK_BOX(top), gtk_label_new(_("Search")), FALSE, FALSE, 0);
    p.search = gtk_entry_new();
    g_signal_connect(p.search, "changed", G_CALLBACK(on_picker_search), &p);
    g_signal_connect(p.search, "activate", G_CALLBACK(on_picker_enter), &p);
    gtk_box_pack_start(GTK_BOX(top), p.search, TRUE, TRUE, 0);
    p.count = gtk_label_new(NULL);
    gtk_box_pack_start(GTK_BOX(top), p.count, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), top, FALSE, FALSE, 0);

    p.view = gtk_icon_view_new_with_model(p.filter);
    gtk_icon_view_set_pixbuf_column(GTK_ICON_VIEW(p.view), P_PIXBUF);
    gtk_icon_view_set_text_column(GTK_ICON_VIEW(p.view), P_NAME);
    gtk_icon_view_set_item_width(GTK_ICON_VIEW(p.view), 96);
    gtk_icon_view_set_selection_mode(GTK_ICON_VIEW(p.view), GTK_SELECTION_BROWSE);
    g_signal_connect(p.view, "item-activated", G_CALLBACK(on_picker_activated), &p);
    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_NEVER,
                                   GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(scroll), GTK_SHADOW_IN);
    gtk_container_add(GTK_CONTAINER(scroll), p.view);
    gtk_box_pack_start(GTK_BOX(box), scroll, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(p.dlg))), box, TRUE, TRUE,
                       0);
    picker_count(&p);
    gtk_widget_show_all(p.dlg);
    gtk_widget_grab_focus(p.search);

    /* Start on the avatar they have now. */
    GtkTreeModel *m = p.filter;
    for (bool more = gtk_tree_model_get_iter_first(m, &it); more && current && current[0];
         more = gtk_tree_model_iter_next(m, &it)) {
        gchar *key;
        gtk_tree_model_get(m, &it, P_KEY, &key, -1);
        bool same = g_ascii_strcasecmp(key, current) == 0;
        g_free(key);
        if (same) {
            GtkTreePath *path = gtk_tree_model_get_path(m, &it);
            gtk_icon_view_select_path(GTK_ICON_VIEW(p.view), path);
            gtk_icon_view_scroll_to_path(GTK_ICON_VIEW(p.view), path, TRUE, 0.5, 0);
            gtk_tree_path_free(path);
            break;
        }
    }
    p.more = gtk_tree_model_get_iter_first(GTK_TREE_MODEL(p.store), &p.at);
    if (p.more)
        p.idle = g_idle_add(picker_thumbs, &p);

    gchar *chosen = NULL;
    int r = gtk_dialog_run(GTK_DIALOG(p.dlg));
    if (r == RESPONSE_RANDOM) {
        chosen = g_strdup("");
    } else if (r == GTK_RESPONSE_OK) {
        GList *sel = gtk_icon_view_get_selected_items(GTK_ICON_VIEW(p.view));
        if (sel && gtk_tree_model_get_iter(p.filter, &it, sel->data))
            gtk_tree_model_get(p.filter, &it, P_KEY, &chosen, -1);
        g_list_free_full(sel, (GDestroyNotify)gtk_tree_path_free);
    }
    if (p.idle)
        g_source_remove(p.idle);
    gtk_widget_destroy(p.dlg);
    g_object_unref(p.filter);
    g_object_unref(p.store);
    return chosen;
}

void avatars_free(editor *e)
{
    if (!e->avatars)
        return;
    if (e->avatars->thumbs)
        g_source_remove(e->avatars->thumbs);
    if (e->avatars->store) {
        g_object_unref(e->avatars->filter);
        g_object_unref(e->avatars->store);
    }
    drop_thumbs(e->avatars);
    kk_sa_free(&e->avatars->lib);
    g_object_unref(e->avatars->dirs);
    g_free(e->avatars);
    e->avatars = NULL;
}
