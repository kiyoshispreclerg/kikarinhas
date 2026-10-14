/* SPDX-License-Identifier: GPL-3.0-or-later */
/* kikarinhas-config: the "Scenery" tab, the [scenery.NAME] sections.
 *
 * Pictures kept on the window: a backdrop under the avatars, a frame or a
 * logo over them. Each has a place (centre, in % of the window) and a size
 * (% of the file). Edited in a dialog, like the timers. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "editor.h"
#include "i18n.h"

enum {
    S_ENABLED,
    S_NAME,
    S_FILE,
    S_X,
    S_Y,
    S_SCALE,
    S_FRONT, /* gboolean */
    N_SCOLS,
};

struct scenery_tab {
    GtkListStore *store;
    GtkWidget *tree;
    char *loaded[KK_CONFIG_MAX_SCENERY]; /* sections in the file when loaded/saved */
    int n_loaded;
};

static void remember_loaded(scenery_tab *t)
{
    for (int i = 0; i < t->n_loaded; i++)
        g_free(t->loaded[i]);
    t->n_loaded = 0;
    GtkTreeModel *m = GTK_TREE_MODEL(t->store);
    GtkTreeIter it;
    for (bool more = gtk_tree_model_get_iter_first(m, &it);
         more && t->n_loaded < KK_CONFIG_MAX_SCENERY; more = gtk_tree_model_iter_next(m, &it))
        gtk_tree_model_get(m, &it, S_NAME, &t->loaded[t->n_loaded++], -1);
}

static bool name_taken(scenery_tab *t, const char *name, GtkTreeIter *except)
{
    GtkTreeModel *m = GTK_TREE_MODEL(t->store);
    GtkTreeIter it;
    for (bool more = gtk_tree_model_get_iter_first(m, &it); more;
         more = gtk_tree_model_iter_next(m, &it)) {
        if (except && it.user_data == except->user_data)
            continue;
        gchar *n;
        gtk_tree_model_get(m, &it, S_NAME, &n, -1);
        bool same = n && strcmp(n, name) == 0;
        g_free(n);
        if (same)
            return true;
    }
    return false;
}

/* ---- the dialog ---------------------------------------------------------- */

typedef struct {
    GtkWidget *dlg, *name, *file, *x, *y, *scale, *layer, *enabled, *problem;
} dialog;

static GtkWidget *spin_at(double lo, double hi, double step, int digits, double v)
{
    GtkWidget *s = spin(lo, hi, step, digits);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(s), v);
    return s;
}

static GtkWidget *line(GtkWidget *a, const char *after)
{
    GtkWidget *box = gtk_hbox_new(FALSE, 6);
    gtk_box_pack_start(GTK_BOX(box), a, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), gtk_label_new(after), FALSE, FALSE, 0);
    return box;
}

static void problem(dialog *d, const char *msg)
{
    char *markup = g_markup_printf_escaped("<span foreground=\"#c01c28\">%s</span>", msg);
    gtk_label_set_markup(GTK_LABEL(d->problem), markup);
    g_free(markup);
}

static void build(editor *e, dialog *d, const char *title, const char *name, const char *file,
                  double x, double y, double scale, bool front, bool enabled)
{
    d->dlg = gtk_dialog_new_with_buttons(title, GTK_WINDOW(e->window),
                                         GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                         NULL, NULL);
    gtk_dialog_add_button(GTK_DIALOG(d->dlg), _("_Cancel"), GTK_RESPONSE_CANCEL);
    gtk_dialog_add_button(GTK_DIALOG(d->dlg), _("_OK"), GTK_RESPONSE_OK);
    gtk_dialog_set_default_response(GTK_DIALOG(d->dlg), GTK_RESPONSE_OK);
    GtkWidget *table = gtk_table_new(1, 2, FALSE);
    gtk_table_set_row_spacings(GTK_TABLE(table), 6);
    gtk_table_set_col_spacings(GTK_TABLE(table), 12);
    gtk_container_set_border_width(GTK_CONTAINER(table), 12);

    d->name = row(table, _("Name"), entry(name), _("One word, only to tell the pictures apart."));
    d->file = gtk_file_chooser_button_new(_("Picture"), GTK_FILE_CHOOSER_ACTION_OPEN);
    GtkFileFilter *f = gtk_file_filter_new();
    gtk_file_filter_set_name(f, _("Pictures (PNG, JPEG, GIF, BMP)"));
    static const char *const pats[] = {"*.png", "*.PNG", "*.jpg", "*.JPG", "*.jpeg",
                                       "*.JPEG", "*.gif", "*.GIF", "*.bmp", "*.BMP"};
    for (size_t i = 0; i < sizeof pats / sizeof pats[0]; i++)
        gtk_file_filter_add_pattern(f, pats[i]);
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(d->file), f);
    if (file[0])
        gtk_file_chooser_set_filename(GTK_FILE_CHOOSER(d->file), file);
    row(table, _("Picture"), d->file,
        _("PNG, JPEG, BMP or GIF (animated ones play). Transparency is kept."));
    d->layer = gtk_combo_box_text_new();
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(d->layer), _("Behind the avatars"));
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(d->layer), _("In front of the avatars"));
    gtk_combo_box_set_active(GTK_COMBO_BOX(d->layer), front ? 1 : 0);
    row(table, _("Layer"), d->layer,
        _("Behind: ground, backdrop. In front: frame, logo, watermark. Chat command pictures "
          "and the emote wall go over both."));
    d->x = spin_at(-100, 200, 1, 0, x);
    row(table, _("Centre at"),
        /* xgettext:no-c-format */
        line(d->x, _("% of the window width")),
        _("0 is the left edge, 100 the right one."));
    d->y = spin_at(-100, 200, 1, 0, y);
    row(table, NULL,
        /* xgettext:no-c-format */
        line(d->y, _("% of the window height")),
        _("0 is the top, 100 the bottom."));
    d->scale = spin_at(1, 2000, 5, 0, scale);
    row(table, _("Size"),
        /* xgettext:no-c-format */
        line(d->scale, _("% of the picture")),
        _("Whole multiples of 100 (200, 300...) keep pixel art crisp."));
    d->enabled = row(table, NULL, check(_("Active"), enabled), NULL);
    gtk_entry_set_activates_default(GTK_ENTRY(d->name), TRUE);

    d->problem = gtk_label_new(NULL);
    gtk_misc_set_alignment(GTK_MISC(d->problem), 0, 0.5);
    GtkWidget *scroll = dialog_body(d->dlg, table);
    gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(d->dlg))), d->problem,
                       FALSE, FALSE, 6);
    gtk_widget_show_all(d->dlg);
    dialog_body_fit(scroll);
}

/* The dialog into the row at it; false (and why) if something is off. */
static bool apply(editor *e, dialog *d, GtkTreeIter *it, bool is_new)
{
    scenery_tab *t = e->scenery;
    char name[64];
    const char *s = text_of(d->name);
    size_t n = strlen(s);
    if (n == 0 || n >= sizeof name || strpbrk(s, " \t!,[]=.")) {
        problem(d, _("Invalid name: use a single word, no spaces."));
        return false;
    }
    for (size_t i = 0; i <= n; i++)
        name[i] = (char)g_ascii_tolower(s[i]);
    if (name_taken(t, name, is_new ? NULL : it)) {
        problem(d, _("There is already a picture with that name."));
        return false;
    }
    gchar *file = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(d->file));
    if (!file) {
        problem(d, _("Pick the picture file."));
        return false;
    }
    gtk_list_store_set(t->store, it, S_ENABLED,
                       gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(d->enabled)), S_NAME, name,
                       S_FILE, file, S_X, gtk_spin_button_get_value(GTK_SPIN_BUTTON(d->x)), S_Y,
                       gtk_spin_button_get_value(GTK_SPIN_BUTTON(d->y)), S_SCALE,
                       gtk_spin_button_get_value(GTK_SPIN_BUTTON(d->scale)), S_FRONT,
                       gtk_combo_box_get_active(GTK_COMBO_BOX(d->layer)) == 1, -1);
    g_free(file);
    return true;
}

static int count_rows(scenery_tab *t)
{
    return gtk_tree_model_iter_n_children(GTK_TREE_MODEL(t->store), NULL);
}

static void edit(editor *e, GtkTreeIter *it)
{
    scenery_tab *t = e->scenery;
    if (!it && count_rows(t) >= KK_CONFIG_MAX_SCENERY) {
        status(e, _("At most %d pictures."), KK_CONFIG_MAX_SCENERY);
        return;
    }
    dialog d = {0};
    gboolean enabled = TRUE, front = FALSE;
    gchar *name = NULL, *file = NULL;
    double x = 50, y = 50, scale = 100;
    char fresh[32];
    if (it) {
        gtk_tree_model_get(GTK_TREE_MODEL(t->store), it, S_ENABLED, &enabled, S_NAME, &name,
                           S_FILE, &file, S_X, &x, S_Y, &y, S_SCALE, &scale, S_FRONT, &front,
                           -1);
    } else {
        snprintf(fresh, sizeof fresh, "image");
        for (int i = 2; name_taken(t, fresh, NULL); i++)
            snprintf(fresh, sizeof fresh, "image%d", i);
    }
    char title[128];
    if (it)
        snprintf(title, sizeof title, _("Picture %s"), name);
    else
        snprintf(title, sizeof title, "%s", _("New picture"));
    build(e, &d, title, it ? name : fresh, it ? file : "", x, y, scale, front, enabled);
    GtkTreeIter nit;
    if (!it)
        gtk_list_store_append(t->store, &nit);
    bool ok = false;
    while (!ok && gtk_dialog_run(GTK_DIALOG(d.dlg)) == GTK_RESPONSE_OK)
        ok = apply(e, &d, it ? it : &nit, !it);
    if (!ok && !it)
        gtk_list_store_remove(t->store, &nit);
    gtk_widget_destroy(d.dlg);
    g_free(name);
    g_free(file);
}

/* ---- the list ------------------------------------------------------------ */

static bool selected(editor *e, GtkTreeIter *it)
{
    GtkTreeSelection *sel = gtk_tree_view_get_selection(GTK_TREE_VIEW(e->scenery->tree));
    return gtk_tree_selection_get_selected(sel, NULL, it);
}

static void on_add(GtkButton *b, gpointer ud)
{
    (void)b;
    edit(ud, NULL);
}

static void on_edit(GtkButton *b, gpointer ud)
{
    (void)b;
    editor *e = ud;
    GtkTreeIter it;
    if (selected(e, &it))
        edit(e, &it);
    else
        status(e, _("Pick a picture in the list."));
}

static void on_remove(GtkButton *b, gpointer ud)
{
    (void)b;
    editor *e = ud;
    GtkTreeIter it;
    if (selected(e, &it))
        gtk_list_store_remove(e->scenery->store, &it);
    else
        status(e, _("Pick a picture in the list."));
}

static void on_row_activated(GtkTreeView *v, GtkTreePath *p, GtkTreeViewColumn *c, gpointer ud)
{
    (void)v;
    (void)c;
    editor *e = ud;
    GtkTreeIter it;
    if (gtk_tree_model_get_iter(GTK_TREE_MODEL(e->scenery->store), &it, p))
        edit(e, &it);
}

static void on_toggled(GtkCellRendererToggle *r, gchar *path, gpointer ud)
{
    editor *e = ud;
    GtkTreeIter it;
    if (gtk_tree_model_get_iter_from_string(GTK_TREE_MODEL(e->scenery->store), &it, path))
        gtk_list_store_set(e->scenery->store, &it, S_ENABLED,
                           !gtk_cell_renderer_toggle_get_active(r), -1);
}

/* The file's name only, the layer in words, "50%, 94%" for the place. */
static void cell_text(GtkTreeViewColumn *c, GtkCellRenderer *r, GtkTreeModel *m, GtkTreeIter *it,
                      gpointer ud)
{
    (void)c;
    int col = GPOINTER_TO_INT(ud);
    char s[128];
    if (col == S_FILE) {
        gchar *f;
        gtk_tree_model_get(m, it, S_FILE, &f, -1);
        gchar *base = f ? g_path_get_basename(f) : NULL;
        snprintf(s, sizeof s, "%s", base ? base : "");
        g_free(base);
        g_free(f);
    } else if (col == S_FRONT) {
        gboolean front;
        gtk_tree_model_get(m, it, S_FRONT, &front, -1);
        snprintf(s, sizeof s, "%s", front ? _("In front") : _("Behind"));
    } else {
        double x, y, k;
        gtk_tree_model_get(m, it, S_X, &x, S_Y, &y, S_SCALE, &k, -1);
        snprintf(s, sizeof s, "%g%%, %g%% · %g%%", x, y, k);
    }
    g_object_set(r, "text", s, NULL);
}

GtkWidget *scenery_page(editor *e, const kk_config *cfg)
{
    scenery_tab *t = e->scenery = g_new0(scenery_tab, 1);
    t->store = gtk_list_store_new(N_SCOLS, G_TYPE_BOOLEAN, G_TYPE_STRING, G_TYPE_STRING,
                                  G_TYPE_DOUBLE, G_TYPE_DOUBLE, G_TYPE_DOUBLE, G_TYPE_BOOLEAN);
    for (int i = 0; i < cfg->n_scenery; i++) {
        const kk_config_scenery *k = &cfg->scenery[i];
        GtkTreeIter it;
        gtk_list_store_append(t->store, &it);
        gtk_list_store_set(t->store, &it, S_ENABLED, k->enabled, S_NAME, k->name, S_FILE, k->file,
                           S_X, k->x, S_Y, k->y, S_SCALE, k->scale, S_FRONT, k->front, -1);
    }
    remember_loaded(t);

    GtkWidget *box = gtk_vbox_new(FALSE, 6);
    gtk_container_set_border_width(GTK_CONTAINER(box), 12);
    t->tree = gtk_tree_view_new_with_model(GTK_TREE_MODEL(t->store));
    gtk_tree_view_set_rules_hint(GTK_TREE_VIEW(t->tree), TRUE);
    g_signal_connect(t->tree, "row-activated", G_CALLBACK(on_row_activated), e);
    GtkCellRenderer *tr = gtk_cell_renderer_toggle_new();
    g_signal_connect(tr, "toggled", G_CALLBACK(on_toggled), e);
    gtk_tree_view_append_column(GTK_TREE_VIEW(t->tree), gtk_tree_view_column_new_with_attributes(
                                                            _("Active"), tr, "active", S_ENABLED,
                                                            NULL));
    static const struct {
        const char *title;
        int col;
    } cols[] = {
        {N_("Name"), S_NAME},
        {N_("Picture"), S_FILE},
        {N_("Layer"), S_FRONT},
        {N_("Centre and size"), S_X},
    };
    for (size_t i = 0; i < sizeof cols / sizeof cols[0]; i++) {
        GtkCellRenderer *r = gtk_cell_renderer_text_new();
        GtkTreeViewColumn *c = gtk_tree_view_column_new_with_attributes(_(cols[i].title), r, NULL,
                                                                        NULL);
        if (cols[i].col == S_NAME)
            gtk_tree_view_column_add_attribute(c, r, "text", S_NAME);
        else
            gtk_tree_view_column_set_cell_data_func(c, r, cell_text,
                                                    GINT_TO_POINTER(cols[i].col), NULL);
        gtk_tree_view_column_set_expand(c, cols[i].col == S_FILE);
        gtk_tree_view_column_set_resizable(c, TRUE);
        gtk_tree_view_append_column(GTK_TREE_VIEW(t->tree), c);
    }
    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_AUTOMATIC,
                                   GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(scroll), GTK_SHADOW_IN);
    gtk_container_add(GTK_CONTAINER(scroll), t->tree);
    gtk_box_pack_start(GTK_BOX(box), scroll, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(box),
                       /* xgettext:no-c-format */
                       hint_label(_("Pictures kept on the window all the time: a ground or "
                                    "backdrop behind the avatars, a frame or logo in front of "
                                    "them. Each one has a place (its centre, in % of the "
                                    "window) and a size (% of the file). Up to 16; later ones "
                                    "are drawn over earlier ones.")),
                       FALSE, FALSE, 0);

    GtkWidget *buttons = gtk_hbutton_box_new();
    gtk_button_box_set_layout(GTK_BUTTON_BOX(buttons), GTK_BUTTONBOX_START);
    gtk_box_set_spacing(GTK_BOX(buttons), 6);
    GtkWidget *add = icon_button(GTK_STOCK_ADD, _("_Add"));
    GtkWidget *ed = icon_button(GTK_STOCK_EDIT, _("_Edit"));
    GtkWidget *rm = icon_button(GTK_STOCK_REMOVE, _("_Remove"));
    g_signal_connect(add, "clicked", G_CALLBACK(on_add), e);
    g_signal_connect(ed, "clicked", G_CALLBACK(on_edit), e);
    g_signal_connect(rm, "clicked", G_CALLBACK(on_remove), e);
    gtk_container_add(GTK_CONTAINER(buttons), add);
    gtk_container_add(GTK_CONTAINER(buttons), ed);
    gtk_container_add(GTK_CONTAINER(buttons), rm);
    gtk_box_pack_start(GTK_BOX(box), buttons, FALSE, FALSE, 0);
    return box;
}

void scenery_collect(editor *e)
{
    scenery_tab *t = e->scenery;
    for (int i = 0; i < t->n_loaded; i++)
        if (!name_taken(t, t->loaded[i], NULL)) {
            char section[128];
            snprintf(section, sizeof section, "scenery.%s", t->loaded[i]);
            kk_ini_remove_section(e->ini, section);
        }
    GtkTreeModel *m = GTK_TREE_MODEL(t->store);
    GtkTreeIter it;
    for (bool more = gtk_tree_model_get_iter_first(m, &it); more;
         more = gtk_tree_model_iter_next(m, &it)) {
        gboolean enabled, front;
        gchar *name, *file;
        double x, y, scale;
        gtk_tree_model_get(m, &it, S_ENABLED, &enabled, S_NAME, &name, S_FILE, &file, S_X, &x,
                           S_Y, &y, S_SCALE, &scale, S_FRONT, &front, -1);
        char section[128];
        snprintf(section, sizeof section, "scenery.%s", name);
        put(e, section, "file", file, NULL);
        put_num(e, section, "x", x, 50);
        put_num(e, section, "y", y, 50);
        put_num(e, section, "scale", scale, 100);
        put(e, section, "layer", front ? "front" : "behind", "behind");
        put(e, section, "enabled", enabled ? "yes" : "no", "yes");
        g_free(name);
        g_free(file);
    }
    remember_loaded(t);
}

void scenery_free(editor *e)
{
    scenery_tab *t = e->scenery;
    if (!t)
        return;
    for (int i = 0; i < t->n_loaded; i++)
        g_free(t->loaded[i]);
    g_object_unref(t->store);
    g_free(t);
    e->scenery = NULL;
}
