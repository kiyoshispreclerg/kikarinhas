/* SPDX-License-Identifier: GPL-3.0-or-later */
/* kikarinhas-config: the "Timers" tab, the [timer.NAME] sections.
 *
 * A timer runs a chat command by itself (as the channel owner: no cooldown,
 * no cost) when every condition it sets holds: minutes since it last ran,
 * chat messages since then, people on screen. Edited in a dialog, like the
 * commands. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "editor.h"
#include "i18n.h"

enum {
    T_ENABLED,
    T_NAME,
    T_COMMAND,
    T_MINUTES,  /* double, 0 = no condition */
    T_MESSAGES, /* int */
    T_VIEWERS,  /* int */
    N_TCOLS,
};

struct timers_tab {
    GtkListStore *store;
    GtkWidget *tree;
    char *loaded[256]; /* timer sections in the file when loaded/saved */
    int n_loaded;
};

static void remember_loaded(timers_tab *t)
{
    for (int i = 0; i < t->n_loaded; i++)
        g_free(t->loaded[i]);
    t->n_loaded = 0;
    GtkTreeModel *m = GTK_TREE_MODEL(t->store);
    GtkTreeIter it;
    for (bool more = gtk_tree_model_get_iter_first(m, &it); more && t->n_loaded < 256;
         more = gtk_tree_model_iter_next(m, &it))
        gtk_tree_model_get(m, &it, T_NAME, &t->loaded[t->n_loaded++], -1);
}

static bool name_taken(timers_tab *t, const char *name, GtkTreeIter *except)
{
    GtkTreeModel *m = GTK_TREE_MODEL(t->store);
    GtkTreeIter it;
    for (bool more = gtk_tree_model_get_iter_first(m, &it); more;
         more = gtk_tree_model_iter_next(m, &it)) {
        if (except && it.user_data == except->user_data)
            continue;
        gchar *n;
        gtk_tree_model_get(m, &it, T_NAME, &n, -1);
        bool same = n && strcmp(n, name) == 0;
        g_free(n);
        if (same)
            return true;
    }
    return false;
}

/* ---- the dialog ---------------------------------------------------------- */

typedef struct {
    GtkWidget *dlg, *name, *command, *minutes, *messages, *viewers, *enabled, *problem;
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

static void add_command_name(const char *name, void *ud)
{
    char s[80];
    snprintf(s, sizeof s, "!%s", name);
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(ud), s);
}

static void problem(dialog *d, const char *msg)
{
    char *markup = g_markup_printf_escaped("<span foreground=\"#c01c28\">%s</span>", msg);
    gtk_label_set_markup(GTK_LABEL(d->problem), markup);
    g_free(markup);
}

static void build(editor *e, dialog *d, const char *title, const char *name, const char *command,
                  double minutes, int messages, int viewers, bool enabled)
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

    d->name = row(table, _("Name"), entry(name), _("One word, only to tell the timers apart."));
    d->command = gtk_combo_box_text_new_with_entry();
    commands_each_name(e, add_command_name, d->command);
    gtk_entry_set_text(GTK_ENTRY(gtk_bin_get_child(GTK_BIN(d->command))), command);
    row(table, _("Runs"), d->command,
        _("Any chat command, with what it takes: !logo, !discord, !sound horn, !song... "
          "It runs as if the channel owner typed it (no cooldown, no cost); bubbles go "
          "over someone on screen."));
    d->enabled = row(table, NULL, check(_("Active"), enabled), NULL);
    d->minutes = spin_at(0, 1440, 1, 1, minutes);
    row(table, _("Every"), line(d->minutes, _("minutes")), _("0: no time condition."));
    d->messages = spin_at(0, 100000, 1, 0, messages);
    row(table, _("After"), line(d->messages, _("chat messages since it last ran")),
        _("0: no message condition. Minutes or messages (or both) must be set."));
    d->viewers = spin_at(0, 1000, 1, 0, viewers);
    row(table, _("Only with"), line(d->viewers, _("or more people of the chat on screen")),
        _("0: any number. Every condition set must hold for the timer to run."));
    for (GtkWidget **w = (GtkWidget *[]){d->name, gtk_bin_get_child(GTK_BIN(d->command)), NULL};
         *w; w++)
        gtk_entry_set_activates_default(GTK_ENTRY(*w), TRUE);

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
    timers_tab *t = e->timers;
    char name[64];
    const char *s = text_of(d->name);
    size_t n = strlen(s);
    if (n == 0 || n >= sizeof name || strpbrk(s, " \t!,[]=")) {
        problem(d, _("Invalid name: use a single word, no spaces."));
        return false;
    }
    for (size_t i = 0; i <= n; i++)
        name[i] = (char)g_ascii_tolower(s[i]);
    if (name_taken(t, name, is_new ? NULL : it)) {
        problem(d, _("There is already a timer with that name."));
        return false;
    }
    gchar *cmd = g_strstrip(
        g_strdup(gtk_entry_get_text(GTK_ENTRY(gtk_bin_get_child(GTK_BIN(d->command))))));
    if (cmd[0] && cmd[0] != '!') {
        gchar *with = g_strdup_printf("!%s", cmd);
        g_free(cmd);
        cmd = with;
    }
    double minutes = gtk_spin_button_get_value(GTK_SPIN_BUTTON(d->minutes));
    int messages = spin_int(d->messages);
    const char *missing = !cmd[0]                          ? _("Pick the command to run.")
                          : minutes <= 0 && messages <= 0 ? _("Set the minutes, the messages or both.")
                                                          : NULL;
    if (missing) {
        problem(d, missing);
        g_free(cmd);
        return false;
    }
    gtk_list_store_set(t->store, it, T_ENABLED,
                       gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(d->enabled)), T_NAME, name,
                       T_COMMAND, cmd, T_MINUTES, minutes, T_MESSAGES, messages, T_VIEWERS,
                       spin_int(d->viewers), -1);
    g_free(cmd);
    return true;
}

static void edit(editor *e, GtkTreeIter *it)
{
    timers_tab *t = e->timers;
    dialog d = {0};
    gboolean enabled = TRUE;
    gchar *name = NULL, *command = NULL;
    double minutes = 10;
    int messages = 0, viewers = 0;
    char fresh[32];
    if (it) {
        gtk_tree_model_get(GTK_TREE_MODEL(t->store), it, T_ENABLED, &enabled, T_NAME, &name,
                           T_COMMAND, &command, T_MINUTES, &minutes, T_MESSAGES, &messages,
                           T_VIEWERS, &viewers, -1);
    } else {
        snprintf(fresh, sizeof fresh, "timer");
        for (int i = 2; name_taken(t, fresh, NULL); i++)
            snprintf(fresh, sizeof fresh, "timer%d", i);
    }
    char title[128];
    if (it)
        snprintf(title, sizeof title, _("Timer %s"), name);
    else
        snprintf(title, sizeof title, "%s", _("New timer"));
    build(e, &d, title, it ? name : fresh, it ? command : "", minutes, messages, viewers,
          enabled);
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
    g_free(command);
}

/* ---- the list ------------------------------------------------------------ */

static bool selected(editor *e, GtkTreeIter *it)
{
    GtkTreeSelection *sel = gtk_tree_view_get_selection(GTK_TREE_VIEW(e->timers->tree));
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
        status(e, _("Pick a timer in the list."));
}

static void on_remove(GtkButton *b, gpointer ud)
{
    (void)b;
    editor *e = ud;
    GtkTreeIter it;
    if (selected(e, &it))
        gtk_list_store_remove(e->timers->store, &it);
    else
        status(e, _("Pick a timer in the list."));
}

static void on_row_activated(GtkTreeView *v, GtkTreePath *p, GtkTreeViewColumn *c, gpointer ud)
{
    (void)v;
    (void)c;
    editor *e = ud;
    GtkTreeIter it;
    if (gtk_tree_model_get_iter(GTK_TREE_MODEL(e->timers->store), &it, p))
        edit(e, &it);
}

static void on_toggled(GtkCellRendererToggle *r, gchar *path, gpointer ud)
{
    editor *e = ud;
    GtkTreeIter it;
    if (gtk_tree_model_get_iter_from_string(GTK_TREE_MODEL(e->timers->store), &it, path))
        gtk_list_store_set(e->timers->store, &it, T_ENABLED,
                           !gtk_cell_renderer_toggle_get_active(r), -1);
}

/* "15 min", "50", "3"; "—" for a condition not set. */
static void condition_text(GtkTreeViewColumn *c, GtkCellRenderer *r, GtkTreeModel *m,
                           GtkTreeIter *it, gpointer ud)
{
    (void)c;
    int col = GPOINTER_TO_INT(ud);
    char s[32];
    if (col == T_MINUTES) {
        double v;
        gtk_tree_model_get(m, it, col, &v, -1);
        if (v > 0)
            snprintf(s, sizeof s, "%g min", v);
        else
            snprintf(s, sizeof s, "—");
    } else {
        int v;
        gtk_tree_model_get(m, it, col, &v, -1);
        if (v > 0)
            snprintf(s, sizeof s, "%d", v);
        else
            snprintf(s, sizeof s, "—");
    }
    g_object_set(r, "text", s, NULL);
}

GtkWidget *timers_page(editor *e, const kk_config *cfg)
{
    timers_tab *t = e->timers = g_new0(timers_tab, 1);
    t->store = gtk_list_store_new(N_TCOLS, G_TYPE_BOOLEAN, G_TYPE_STRING, G_TYPE_STRING,
                                  G_TYPE_DOUBLE, G_TYPE_INT, G_TYPE_INT);
    for (int i = 0; i < cfg->n_timers; i++) {
        const kk_config_timer *k = &cfg->timers[i];
        GtkTreeIter it;
        gtk_list_store_append(t->store, &it);
        gtk_list_store_set(t->store, &it, T_ENABLED, k->enabled, T_NAME, k->name, T_COMMAND,
                           k->command, T_MINUTES, k->minutes, T_MESSAGES, k->messages,
                           T_VIEWERS, k->viewers, -1);
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
                                                            _("Active"), tr, "active", T_ENABLED,
                                                            NULL));
    static const struct {
        const char *title;
        int col;
    } cols[] = {
        {N_("Name"), T_NAME},         {N_("Runs"), T_COMMAND},
        {N_("Every"), T_MINUTES},     {N_("Messages"), T_MESSAGES},
        {N_("People on screen"), T_VIEWERS},
    };
    for (size_t i = 0; i < sizeof cols / sizeof cols[0]; i++) {
        GtkCellRenderer *r = gtk_cell_renderer_text_new();
        GtkTreeViewColumn *c = gtk_tree_view_column_new_with_attributes(_(cols[i].title), r, NULL,
                                                                        NULL);
        if (cols[i].col == T_NAME || cols[i].col == T_COMMAND) {
            gtk_tree_view_column_add_attribute(c, r, "text", cols[i].col);
        } else {
            gtk_tree_view_column_set_cell_data_func(c, r, condition_text,
                                                    GINT_TO_POINTER(cols[i].col), NULL);
        }
        gtk_tree_view_column_set_expand(c, cols[i].col == T_COMMAND);
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
                       hint_label(_("A timer runs a command by itself when every condition "
                                    "set holds: every N minutes, after N chat messages, only "
                                    "with N people on screen. E.g. the logo every 15 minutes; "
                                    "the Discord link every 10 minutes, but only if 30 messages "
                                    "went by. It runs as the channel owner: no cooldown, no "
                                    "cost.")),
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

void timers_collect(editor *e)
{
    timers_tab *t = e->timers;
    for (int i = 0; i < t->n_loaded; i++)
        if (!name_taken(t, t->loaded[i], NULL)) {
            char section[128];
            snprintf(section, sizeof section, "timer.%s", t->loaded[i]);
            kk_ini_remove_section(e->ini, section);
        }
    GtkTreeModel *m = GTK_TREE_MODEL(t->store);
    GtkTreeIter it;
    for (bool more = gtk_tree_model_get_iter_first(m, &it); more;
         more = gtk_tree_model_iter_next(m, &it)) {
        gboolean enabled;
        gchar *name, *command;
        double minutes;
        int messages, viewers;
        gtk_tree_model_get(m, &it, T_ENABLED, &enabled, T_NAME, &name, T_COMMAND, &command,
                           T_MINUTES, &minutes, T_MESSAGES, &messages, T_VIEWERS, &viewers, -1);
        char section[128];
        snprintf(section, sizeof section, "timer.%s", name);
        put(e, section, "command", command, NULL);
        put_num(e, section, "minutes", minutes, 0);
        put_int(e, section, "messages", messages, 0);
        put_int(e, section, "viewers", viewers, 0);
        put(e, section, "enabled", enabled ? "yes" : "no", "yes");
        g_free(name);
        g_free(command);
    }
    remember_loaded(t);
}

void timers_free(editor *e)
{
    timers_tab *t = e->timers;
    if (!t)
        return;
    for (int i = 0; i < t->n_loaded; i++)
        g_free(t->loaded[i]);
    g_object_unref(t->store);
    g_free(t);
    e->timers = NULL;
}
