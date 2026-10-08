/* SPDX-License-Identifier: GPL-3.0-or-later */
/* kikarinhas-config: the list of lives in the Chat tab.
 *
 * The file keeps two lists, [chat] youtube and twitch (comma separated);
 * here they are one list of (platform, live) rows, open ended: "Add" asks
 * for a link or channel, guesses the platform from it and checks it the same
 * way kikarinhas will (a target it can't parse would make it refuse the
 * whole reload). */
#include <string.h>

#include "editor.h"
#include "i18n.h"
#include "twitch.h"
#include "youtube.h"

enum { S_PLATFORM, S_TARGET, N_SCOLS };

/* Shown as is: brand names. */
static const char *const PLATFORMS[] = {"YouTube", "Twitch"};
static const char *const PLATFORM_KEYS[] = {"youtube", "twitch"};

struct sources_tab {
    GtkListStore *store;
    GtkWidget *tree;
};

static void append(sources_tab *t, int platform, const char *target)
{
    GtkTreeIter it;
    gtk_list_store_append(t->store, &it);
    gtk_list_store_set(t->store, &it, S_PLATFORM, PLATFORMS[platform], S_TARGET, target, -1);
}

static void append_list(sources_tab *t, int platform, const char *list)
{
    char item[512];
    const char *p = list ? list : "";
    while (kk_config_next_target(&p, item, sizeof item))
        append(t, platform, item);
}

static bool valid(int platform, const char *target)
{
    char tmp[512];
    return platform == 1 ? kk_tw_parse_target(target, tmp, sizeof tmp)
                         : kk_yt_parse_target(target, tmp, sizeof tmp) != KK_YT_TARGET_INVALID;
}

/* "twitch.tv/x" is Twitch; YouTube links, @handles and ids are YouTube; a
 * bare word could be either, so the choice is left alone. */
static void on_target_changed(GtkEditable *ed, gpointer combo)
{
    const char *s = gtk_entry_get_text(GTK_ENTRY(ed));
    if (strstr(s, "twitch.tv"))
        gtk_combo_box_set_active(GTK_COMBO_BOX(combo), 1);
    else if (strstr(s, "youtube.com") || strstr(s, "youtu.be") || s[0] == '@')
        gtk_combo_box_set_active(GTK_COMBO_BOX(combo), 0);
}

static void on_add(GtkButton *b, gpointer ud)
{
    (void)b;
    editor *e = ud;
    GtkWidget *d = gtk_dialog_new_with_buttons(_("Add a live"), GTK_WINDOW(e->window),
                                               GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                               NULL, NULL);
    gtk_dialog_add_button(GTK_DIALOG(d), _("_Cancel"), GTK_RESPONSE_CANCEL);
    gtk_dialog_add_button(GTK_DIALOG(d), _("_Add"), GTK_RESPONSE_OK);
    gtk_dialog_set_default_response(GTK_DIALOG(d), GTK_RESPONSE_OK);

    GtkWidget *table = gtk_table_new(1, 2, FALSE);
    gtk_table_set_row_spacings(GTK_TABLE(table), 6);
    gtk_table_set_col_spacings(GTK_TABLE(table), 12);
    gtk_container_set_border_width(GTK_CONTAINER(table), 12);
    GtkWidget *combo = gtk_combo_box_text_new();
    for (int i = 0; i < 2; i++)
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo), PLATFORMS[i]);
    gtk_combo_box_set_active(GTK_COMBO_BOX(combo), 0);
    GtkWidget *target = entry(NULL);
    gtk_entry_set_activates_default(GTK_ENTRY(target), TRUE);
    gtk_entry_set_width_chars(GTK_ENTRY(target), 48);
    g_signal_connect(target, "changed", G_CALLBACK(on_target_changed), combo);
    row(table, _("Live or channel"), target,
        _("YouTube: link to the stream or channel, @handle or video id; with a channel, "
          "it waits for it to go live. Twitch: channel name or twitch.tv link (read "
          "without logging in)."));
    row(table, _("Platform"), combo, NULL);
    gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(d))), table, TRUE, TRUE,
                       0);
    gtk_widget_show_all(d);

    while (gtk_dialog_run(GTK_DIALOG(d)) == GTK_RESPONSE_OK) {
        char *s = g_strstrip(g_strdup(gtk_entry_get_text(GTK_ENTRY(target))));
        int platform = gtk_combo_box_get_active(GTK_COMBO_BOX(combo));
        if (platform < 0)
            platform = 0;
        if (valid(platform, s)) {
            append(e->sources, platform, s);
            status(e, _("%s: %s added. Save and apply to connect."), PLATFORMS[platform], s);
            g_free(s);
            break;
        }
        status(e, platform == 1
                      ? _("Twitch: could not understand \"%s\" (use the channel name or the "
                          "twitch.tv/channel link).")
                      : _("YouTube: could not understand \"%s\" (use the link to the stream or "
                          "channel, or the @handle)."),
               s);
        g_free(s);
        gtk_widget_grab_focus(target);
    }
    gtk_widget_destroy(d);
}

static void on_remove(GtkButton *b, gpointer ud)
{
    (void)b;
    editor *e = ud;
    GtkTreeIter it;
    GtkTreeSelection *sel = gtk_tree_view_get_selection(GTK_TREE_VIEW(e->sources->tree));
    if (gtk_tree_selection_get_selected(sel, NULL, &it))
        gtk_list_store_remove(e->sources->store, &it);
    else
        status(e, _("Pick a live in the list."));
}

static int platform_of(const char *label)
{
    return strcmp(label, PLATFORMS[1]) == 0 ? 1 : 0;
}

/* Edited in place: checked like a new one. */
static void on_target_edited(GtkCellRendererText *r, gchar *path, gchar *text, gpointer ud)
{
    (void)r;
    editor *e = ud;
    GtkTreeIter it;
    GtkTreeModel *m = GTK_TREE_MODEL(e->sources->store);
    if (!gtk_tree_model_get_iter_from_string(m, &it, path))
        return;
    gchar *label;
    gtk_tree_model_get(m, &it, S_PLATFORM, &label, -1);
    g_strstrip(text);
    if (valid(platform_of(label), text))
        gtk_list_store_set(e->sources->store, &it, S_TARGET, text, -1);
    else
        status(e, _("%s: could not understand \"%s\"."), label, text);
    g_free(label);
}

GtkWidget *sources_widget(editor *e, const kk_config *cfg)
{
    sources_tab *t = e->sources = g_new0(sources_tab, 1);
    t->store = gtk_list_store_new(N_SCOLS, G_TYPE_STRING, G_TYPE_STRING);
    append_list(t, 0, cfg->youtube);
    append_list(t, 1, cfg->twitch);

    GtkWidget *box = gtk_vbox_new(FALSE, 6);
    t->tree = gtk_tree_view_new_with_model(GTK_TREE_MODEL(t->store));
    gtk_tree_view_set_rules_hint(GTK_TREE_VIEW(t->tree), TRUE);
    GtkCellRenderer *r = gtk_cell_renderer_text_new();
    gtk_tree_view_append_column(GTK_TREE_VIEW(t->tree),
                                gtk_tree_view_column_new_with_attributes(
                                    _("Platform"), r, "text", S_PLATFORM, NULL));
    r = gtk_cell_renderer_text_new();
    g_object_set(r, "editable", TRUE, "ellipsize", PANGO_ELLIPSIZE_END, NULL);
    g_signal_connect(r, "edited", G_CALLBACK(on_target_edited), e);
    GtkTreeViewColumn *c =
        gtk_tree_view_column_new_with_attributes(_("Live or channel"), r, "text", S_TARGET, NULL);
    gtk_tree_view_column_set_expand(c, TRUE);
    gtk_tree_view_append_column(GTK_TREE_VIEW(t->tree), c);

    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_AUTOMATIC,
                                   GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(scroll), GTK_SHADOW_IN);
    gtk_widget_set_size_request(scroll, -1, 130);
    gtk_container_add(GTK_CONTAINER(scroll), t->tree);
    gtk_box_pack_start(GTK_BOX(box), scroll, TRUE, TRUE, 0);

    GtkWidget *buttons = gtk_hbutton_box_new();
    gtk_button_box_set_layout(GTK_BUTTON_BOX(buttons), GTK_BUTTONBOX_START);
    gtk_box_set_spacing(GTK_BOX(buttons), 6);
    GtkWidget *add = icon_button(GTK_STOCK_ADD, _("_Add"));
    GtkWidget *rm = icon_button(GTK_STOCK_REMOVE, _("_Remove"));
    g_signal_connect(add, "clicked", G_CALLBACK(on_add), e);
    g_signal_connect(rm, "clicked", G_CALLBACK(on_remove), e);
    gtk_container_add(GTK_CONTAINER(buttons), add);
    gtk_container_add(GTK_CONTAINER(buttons), rm);
    gtk_box_pack_start(GTK_BOX(box), buttons, FALSE, FALSE, 0);
    return box;
}

void sources_collect(editor *e)
{
    GString *lists[2] = {g_string_new(NULL), g_string_new(NULL)};
    GtkTreeModel *m = GTK_TREE_MODEL(e->sources->store);
    GtkTreeIter it;
    for (bool more = gtk_tree_model_get_iter_first(m, &it); more;
         more = gtk_tree_model_iter_next(m, &it)) {
        gchar *label, *target;
        gtk_tree_model_get(m, &it, S_PLATFORM, &label, S_TARGET, &target, -1);
        GString *l = lists[platform_of(label)];
        if (target[0])
            g_string_append_printf(l, "%s%s", l->len ? ", " : "", target);
        g_free(label);
        g_free(target);
    }
    for (int i = 0; i < 2; i++) {
        put(e, "chat", PLATFORM_KEYS[i], lists[i]->str, NULL);
        g_string_free(lists[i], TRUE);
    }
}

void sources_free(editor *e)
{
    if (!e->sources)
        return;
    g_object_unref(e->sources->store);
    g_free(e->sources);
    e->sources = NULL;
}
