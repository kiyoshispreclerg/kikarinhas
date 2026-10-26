/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The "Questions" tab of kikarinhas-config: the queue of !pergunta as the
 * running kikarinhas has it (asked over the socket every couple of seconds
 * while the tab is in view), with buttons to highlight a question on the
 * stage, remove it or empty the queue; and the [questions] settings. The
 * list never goes to the stream: it lives here. */
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "editor.h"
#include "i18n.h"

enum {
    COL_ID,     /* guint64 */
    COL_STATE,  /* "★" highlighted now, "✓" shown before */
    COL_NAME,
    COL_TEXT,
    COL_AGE,
    N_COLS,
};

struct questions_tab {
    GtkListStore *store;
    GtkWidget *view, *note, *auto_q, *max;
    char *last;    /* the last reply, to leave the list alone when unchanged */
    guint timer;
};

static GtkWidget *spin_at(double lo, double hi, double step, int digits, double v)
{
    GtkWidget *s = spin(lo, hi, step, digits);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(s), v);
    return s;
}

static void age_text(char *out, size_t size, double age)
{
    long s = (long)age;
    if (s < 60)
        snprintf(out, size, _("%ld s"), s);
    else if (s < 3600)
        snprintf(out, size, _("%ld min"), s / 60);
    else
        snprintf(out, size, _("%ld h"), s / 3600);
}

static guint64 selected(questions_tab *t)
{
    GtkTreeIter it;
    GtkTreeModel *model;
    guint64 id = 0;
    GtkTreeSelection *sel = gtk_tree_view_get_selection(GTK_TREE_VIEW(t->view));
    if (gtk_tree_selection_get_selected(sel, &model, &it))
        gtk_tree_model_get(model, &it, COL_ID, &id, -1);
    return id;
}

/* Fills the list from a {"questions":[...]} reply, keeping the selection. */
static void fill(questions_tab *t, const cJSON *r)
{
    guint64 keep = selected(t);
    gtk_list_store_clear(t->store);
    double shown = cJSON_GetNumberValue(cJSON_GetObjectItemCaseSensitive(r, "highlighted"));
    const cJSON *q;
    cJSON_ArrayForEach(q, cJSON_GetObjectItemCaseSensitive(r, "questions"))
    {
        guint64 id = (guint64)cJSON_GetNumberValue(cJSON_GetObjectItemCaseSensitive(q, "id"));
        const char *name = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(q, "name"));
        const char *text = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(q, "text"));
        double age = cJSON_GetNumberValue(cJSON_GetObjectItemCaseSensitive(q, "age"));
        bool was = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(q, "shown"));
        char when[32];
        age_text(when, sizeof when, age);
        GtkTreeIter it;
        gtk_list_store_append(t->store, &it);
        gtk_list_store_set(t->store, &it, COL_ID, id,
                           COL_STATE, (double)id == shown ? "★" : was ? "✓" : "",
                           COL_NAME, name ? name : "", COL_TEXT, text ? text : "",
                           COL_AGE, when, -1);
        if (id == keep)
            gtk_tree_selection_select_iter(gtk_tree_view_get_selection(GTK_TREE_VIEW(t->view)),
                                           &it);
    }
}

/* Asks for the queue and shows it. */
static void refresh(editor *e)
{
    questions_tab *t = e->questions;
    char reply[65536];
    if (!editor_request(e, "{\"type\":\"questions\"}", reply, sizeof reply)) {
        gtk_list_store_clear(t->store);
        free(t->last);
        t->last = NULL;
        gtk_label_set_text(GTK_LABEL(t->note), _("kikarinhas is not running."));
        return;
    }
    if (t->last && strcmp(t->last, reply) == 0)
        return;
    free(t->last);
    t->last = strdup(reply);
    cJSON *r = cJSON_Parse(reply);
    fill(t, r);
    int n = cJSON_GetArraySize(cJSON_GetObjectItemCaseSensitive(r, "questions"));
    char text[128];
    snprintf(text, sizeof text, ngettext("%d question in the queue.",
                                         "%d questions in the queue.", n), n);
    gtk_label_set_text(GTK_LABEL(t->note), text);
    cJSON_Delete(r);
}

static gboolean on_timer(gpointer ud)
{
    editor *e = ud;
    if (gtk_widget_get_mapped(e->questions->view))
        refresh(e);
    return TRUE;
}

static void on_map(GtkWidget *w, gpointer ud)
{
    (void)w;
    refresh(ud);
}

static void send(editor *e, const char *action, guint64 id)
{
    char line[128], reply[512];
    snprintf(line, sizeof line, "{\"type\":\"question\",\"action\":\"%s\",\"id\":%llu}", action,
             (unsigned long long)id);
    if (!editor_request(e, line, reply, sizeof reply))
        status(e, _("kikarinhas is not running."));
    refresh(e);
}

static void on_highlight(GtkButton *b, gpointer ud)
{
    (void)b;
    editor *e = ud;
    guint64 id = selected(e->questions);
    if (id)
        send(e, "highlight", id);
}

static void on_activated(GtkTreeView *v, GtkTreePath *p, GtkTreeViewColumn *c, gpointer ud)
{
    (void)v;
    (void)p;
    (void)c;
    on_highlight(NULL, ud);
}

static void on_remove(GtkButton *b, gpointer ud)
{
    (void)b;
    editor *e = ud;
    guint64 id = selected(e->questions);
    if (id)
        send(e, "remove", id);
}

static void on_clear(GtkButton *b, gpointer ud)
{
    (void)b;
    send(ud, "clear", 0);
}

static GtkTreeViewColumn *text_column(GtkWidget *view, const char *title, int col, bool expand)
{
    GtkCellRenderer *r = gtk_cell_renderer_text_new();
    if (expand) /* the whole question, in as many lines as it takes */
        g_object_set(r, "wrap-mode", PANGO_WRAP_WORD_CHAR, "wrap-width", 420, NULL);
    GtkTreeViewColumn *c = gtk_tree_view_column_new_with_attributes(title, r, "text", col, NULL);
    gtk_tree_view_column_set_expand(c, expand);
    gtk_tree_view_append_column(GTK_TREE_VIEW(view), c);
    return c;
}

GtkWidget *questions_page(editor *e, const kk_config *cfg)
{
    questions_tab *t = calloc(1, sizeof *t);
    e->questions = t;

    GtkWidget *box = gtk_vbox_new(FALSE, 6);
    gtk_container_set_border_width(GTK_CONTAINER(box), 12);
    gtk_box_pack_start(GTK_BOX(box),
                       hint_label(_("Questions the chat sends with !pergunta (or !question), "
                                    "oldest first. Highlight one to show it on the stage like "
                                    "a click in the chat window; the list itself stays here, "
                                    "off the stream. ★ on the stage now, ✓ shown before.")),
                       FALSE, FALSE, 0);

    t->store = gtk_list_store_new(N_COLS, G_TYPE_UINT64, G_TYPE_STRING, G_TYPE_STRING,
                                  G_TYPE_STRING, G_TYPE_STRING);
    t->view = gtk_tree_view_new_with_model(GTK_TREE_MODEL(t->store));
    g_object_unref(t->store);
    text_column(t->view, "", COL_STATE, false);
    text_column(t->view, _("Who"), COL_NAME, false);
    text_column(t->view, _("Question"), COL_TEXT, true);
    text_column(t->view, _("Asked"), COL_AGE, false);
    g_signal_connect(t->view, "row-activated", G_CALLBACK(on_activated), e);
    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_AUTOMATIC,
                                   GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(scroll), GTK_SHADOW_IN);
    gtk_container_add(GTK_CONTAINER(scroll), t->view);
    gtk_widget_set_size_request(scroll, -1, 240);
    gtk_box_pack_start(GTK_BOX(box), scroll, TRUE, TRUE, 0);

    GtkWidget *buttons = gtk_hbox_new(FALSE, 6);
    GtkWidget *hl = gtk_button_new_with_label(_("Highlight / take away"));
    GtkWidget *rm = icon_button(GTK_STOCK_REMOVE, _("_Remove"));
    GtkWidget *clr = icon_button(GTK_STOCK_CLEAR, _("_Empty the queue"));
    g_signal_connect(hl, "clicked", G_CALLBACK(on_highlight), e);
    g_signal_connect(rm, "clicked", G_CALLBACK(on_remove), e);
    g_signal_connect(clr, "clicked", G_CALLBACK(on_clear), e);
    gtk_box_pack_start(GTK_BOX(buttons), hl, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(buttons), rm, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(buttons), clr, FALSE, FALSE, 0);
    t->note = gtk_label_new(NULL);
    gtk_misc_set_alignment(GTK_MISC(t->note), 1, 0.5);
    gtk_box_pack_start(GTK_BOX(buttons), t->note, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(box), buttons, FALSE, FALSE, 0);

    GtkWidget *tb = gtk_table_new(1, 2, FALSE);
    gtk_table_set_row_spacings(GTK_TABLE(tb), 6);
    gtk_table_set_col_spacings(GTK_TABLE(tb), 12);
    t->auto_q = row(tb, NULL,
                    check(_("Chat messages ending in \"?\" go in the queue too"),
                          cfg->questions_auto),
                    _("Otherwise only !pergunta. The command itself is in the Commands tab "
                      "(cooldown, cost, aliases)."));
    t->max = spin_at(1, 1000, 5, 0, cfg->questions_max);
    GtkWidget *max = gtk_hbox_new(FALSE, 6);
    gtk_box_pack_start(GTK_BOX(max), t->max, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(max), gtk_label_new(_("questions; past that the oldest go")),
                       FALSE, FALSE, 0);
    row(tb, _("Keeps up to"), max, NULL);
    gtk_box_pack_start(GTK_BOX(box), tb, FALSE, FALSE, 6);

    g_signal_connect(box, "map", G_CALLBACK(on_map), e);
    t->timer = g_timeout_add_seconds(2, on_timer, e);
    return box;
}

void questions_collect(editor *e)
{
    questions_tab *t = e->questions;
    kk_config d;
    kk_config_defaults(&d);
    put_bool(e, "questions", "auto", t->auto_q, d.questions_auto);
    put_int(e, "questions", "max", spin_int(t->max), d.questions_max);
    kk_config_free(&d);
}

void questions_free(editor *e)
{
    questions_tab *t = e->questions;
    if (!t)
        return;
    if (t->timer)
        g_source_remove(t->timer);
    free(t->last);
    free(t);
    e->questions = NULL;
}
