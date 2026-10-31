/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The "Polls" tab of kikarinhas-config: every poll ever made, read from
 * the history file kikarinhas keeps (polls.json), newest first, with the
 * selected one's results below, in total and per platform. They are
 * partial while it is open: kikarinhas writes the votes every couple of
 * seconds and the tab reads the file again when it changes, while in view.
 *
 * New polls open in the running kikarinhas, over the socket: it checks the
 * options' commands against its own and refuses a clash, and the dialog
 * stays open to fix it. Ending one goes the same way. Deleting one too, or
 * straight in the file when kikarinhas is not running. And the [polls]
 * settings. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#include "cJSON.h"
#include "editor.h"
#include "i18n.h"
#include "polls.h"

enum {
    P_ID, /* guint64 */
    P_STATE,
    P_QUESTION,
    P_OPENED,
    P_LENGTH,
    P_VOTES,
    P_WHERE,
    N_PCOLS,
};

struct polls_tab {
    GtkListStore *store;
    GtkWidget *view, *note, *details, *results, *end_b, *dup_b, *del_b;
    GtkWidget *file, *panel, *align, *y, *font, *size, *linger;
    kk_polls *polls; /* the file as last read */
    char path[KK_PATH_MAX];
    struct timespec mtime;
    off_t fsize;
    bool have_file;
    guint64 shown;     /* whose details are below */
    unsigned shown_at; /* kk_polls_serial... of the read they came from */
    unsigned reads;    /* bumps on every read of the file */
    guint timer;
};

/* ---- reading the history -------------------------------------------------- */

/* The file the [polls] file entry names, else the default. */
static bool history_path(editor *e, char *out, size_t size)
{
    const char *s = text_of(e->polls->file);
    if (s[0] == '~')
        return kk_pathf(out, size, "%s%s", g_get_home_dir(), s + 1);
    if (s[0])
        return kk_pathf(out, size, "%s", s);
    return kk_polls_default_path(out, size);
}

static const char *platform_label(const char *pf)
{
    static const struct {
        const char *id, *label;
    } names[] = {{"youtube", "YouTube"}, {"twitch", "Twitch"}, {"kick", "Kick"},
                 {"odysee", "Odysee"}};
    for (size_t i = 0; i < sizeof names / sizeof names[0]; i++)
        if (strcmp(names[i].id, pf) == 0)
            return names[i].label;
    return pf;
}

static void fmt_time(char *out, size_t size, long long t, bool with_date)
{
    time_t tt = (time_t)t;
    struct tm tm;
    if (t <= 0 || !localtime_r(&tt, &tm) ||
        !strftime(out, size, with_date ? "%Y-%m-%d %H:%M" : "%H:%M", &tm))
        snprintf(out, size, "?");
}

static void fmt_span(char *out, size_t size, long long s)
{
    if (s < 60)
        snprintf(out, size, _("%lld s"), s);
    else if (s < 3600)
        snprintf(out, size, _("%lld min %02lld s"), s / 60, s % 60);
    else
        snprintf(out, size, _("%lld h %02lld min"), s / 3600, s % 3600 / 60);
}

/* "youtube @canal, twitch fulano": the lives read when it opened. */
static void where_text(const kk_poll *p, char *out, size_t size)
{
    out[0] = '\0';
    size_t used = 0;
    for (int i = 0; i < p->n_channels; i++) {
        int w = snprintf(out + used, size - used, "%s%s", i ? ", " : "", p->channels[i]);
        if (w < 0 || (size_t)w >= size - used)
            break;
        used += (size_t)w;
    }
}

static guint64 selected_id(polls_tab *t)
{
    GtkTreeIter it;
    GtkTreeModel *model;
    guint64 id = 0;
    GtkTreeSelection *sel = gtk_tree_view_get_selection(GTK_TREE_VIEW(t->view));
    if (gtk_tree_selection_get_selected(sel, &model, &it))
        gtk_tree_model_get(model, &it, P_ID, &id, -1);
    return id;
}

static const kk_poll *selected_poll(polls_tab *t)
{
    guint64 id = selected_id(t);
    return id && t->polls ? kk_polls_find(t->polls, id) : NULL;
}

/* The results of p in the table below the list: one row per option, one
 * column per platform votes came from. */
static void show_results(polls_tab *t, const kk_poll *p)
{
    GtkTreeView *v = GTK_TREE_VIEW(t->results);
    GList *cols = gtk_tree_view_get_columns(v);
    for (GList *c = cols; c; c = c->next)
        gtk_tree_view_remove_column(v, c->data);
    g_list_free(cols);
    gtk_tree_view_set_model(v, NULL);
    if (!p)
        return;
    int n = 3 + p->n_platforms;
    GType *types = g_new(GType, n);
    for (int i = 0; i < n; i++)
        types[i] = G_TYPE_STRING;
    GtkListStore *store = gtk_list_store_newv(n, types);
    g_free(types);
    const char *titles[3] = {_("Option"), _("Votes"), "%"};
    for (int i = 0; i < n; i++) {
        GtkCellRenderer *r = gtk_cell_renderer_text_new();
        if (i == 0)
            g_object_set(r, "ellipsize", PANGO_ELLIPSIZE_END, NULL);
        else
            g_object_set(r, "xalign", 1.0, NULL);
        GtkTreeViewColumn *c = gtk_tree_view_column_new_with_attributes(
            i < 3 ? titles[i] : platform_label(p->platforms[i - 3]), r, "markup", i, NULL);
        gtk_tree_view_column_set_expand(c, i == 0);
        gtk_tree_view_append_column(v, c);
    }
    int total = kk_poll_total(p, -1), best = 0;
    for (int i = 0; i < p->n_options; i++)
        if (kk_poll_votes(p, i, -1) > best)
            best = kk_poll_votes(p, i, -1);
    for (int i = 0; i <= p->n_options; i++) {
        bool sum = i == p->n_options; /* the last row: totals */
        int votes = sum ? total : kk_poll_votes(p, i, -1);
        bool won = !sum && p->closed && best > 0 && votes == best;
        char word[64], sc[64], label[512], cnt[32], pct[16];
        gchar *text;
        if (sum) {
            text = g_markup_printf_escaped("<b>%s</b>", _("Total"));
        } else {
            kk_poll_words(p, i, word, sizeof word, sc, sizeof sc);
            snprintf(label, sizeof label, "%s%s%s%s  %s", word, sc[0] ? " / " : "", sc,
                     won ? " ★" : "", p->options[i].text);
            text = g_markup_printf_escaped(won ? "<b>%s</b>" : "%s", label);
        }
        snprintf(cnt, sizeof cnt, "%d", votes);
        snprintf(pct, sizeof pct, "%d%%", total ? (int)(votes * 100.0 / total + 0.5) : 0);
        GtkTreeIter it;
        gtk_list_store_append(store, &it);
        gtk_list_store_set(store, &it, 0, text, 1, cnt, 2, sum ? "" : pct, -1);
        g_free(text);
        for (int k = 0; k < p->n_platforms; k++) {
            int pv = sum ? kk_poll_total(p, k) : kk_poll_votes(p, i, k);
            snprintf(cnt, sizeof cnt, "%d", pv);
            gtk_list_store_set(store, &it, 3 + k, cnt, -1);
        }
    }
    gtk_tree_view_set_model(v, GTK_TREE_MODEL(store));
    g_object_unref(store);
}

/* What is above the results: question, times, where it ran. */
static void show_details(editor *e)
{
    polls_tab *t = e->polls;
    const kk_poll *p = selected_poll(t);
    gtk_widget_set_sensitive(t->end_b, p && !p->closed);
    gtk_widget_set_sensitive(t->dup_b, p != NULL);
    gtk_widget_set_sensitive(t->del_b, p && p->closed);
    if (!p) {
        gtk_label_set_text(GTK_LABEL(t->details),
                           t->polls && kk_polls_count(t->polls)
                               ? _("Select a poll to see its results.")
                               : _("No polls yet: \"New poll\" opens one in kikarinhas."));
        show_results(t, NULL);
        t->shown = 0;
        return;
    }
    char opened[64], closed[64], span[64], when[256];
    fmt_time(opened, sizeof opened, p->opened, true);
    long long now = (long long)time(NULL);
    if (p->closed) {
        fmt_time(closed, sizeof closed, p->closed, false);
        fmt_span(span, sizeof span, p->closed - p->opened);
        snprintf(when, sizeof when, _("Closed. From %s to %s (%s)."), opened, closed, span);
    } else if (kk_poll_ends_at(p)) {
        fmt_time(closed, sizeof closed, kk_poll_ends_at(p), false);
        long long left = kk_poll_ends_at(p) - now;
        fmt_span(span, sizeof span, left > 0 ? left : 0);
        snprintf(when, sizeof when, _("Open since %s; ends at %s (%s left). Partial results."),
                 opened, closed, span);
    } else {
        snprintf(when, sizeof when,
                 _("Open since %s, until you end it. Partial results."), opened);
    }
    GString *s = g_string_new(NULL);
    gchar *q = g_markup_escape_text(p->question, -1);
    gchar *w = g_markup_escape_text(when, -1);
    g_string_append_printf(s, "<b>%s</b>\n%s", q, w);
    g_free(q);
    g_free(w);
    char where[1024];
    where_text(p, where, sizeof where);
    if (where[0]) {
        gchar *x = g_markup_printf_escaped(_("\nLives read: %s"), where);
        g_string_append(s, x);
        g_free(x);
    }
    for (int i = 0; i < p->n_lives; i++) {
        gchar *x = g_markup_printf_escaped("\n%s: %s%s%s%s", platform_label(p->lives[i].platform),
                                           p->lives[i].url, p->lives[i].title ? " (" : "",
                                           p->lives[i].title ? p->lives[i].title : "",
                                           p->lives[i].title ? ")" : "");
        g_string_append(s, x);
        g_free(x);
    }
    if (!p->show_counts)
        g_string_append_printf(s, "\n<i>%s</i>", _("The votes were not shown on the stream."));
    gtk_label_set_markup(GTK_LABEL(t->details), s->str);
    g_string_free(s, TRUE);
    if (t->shown != p->id || t->shown_at != t->reads) {
        show_results(t, p);
        t->shown = p->id;
        t->shown_at = t->reads;
    }
}

static void fill(editor *e)
{
    polls_tab *t = e->polls;
    guint64 keep = selected_id(t);
    gtk_list_store_clear(t->store);
    int n = t->polls ? kk_polls_count(t->polls) : 0;
    GtkTreeSelection *sel = gtk_tree_view_get_selection(GTK_TREE_VIEW(t->view));
    for (int i = n - 1; i >= 0; i--) {
        const kk_poll *p = kk_polls_at(t->polls, i);
        char opened[64], length[64], votes[32], where[1024];
        fmt_time(opened, sizeof opened, p->opened, true);
        if (p->closed)
            fmt_span(length, sizeof length, p->closed - p->opened);
        else
            snprintf(length, sizeof length, "%s", _("open"));
        snprintf(votes, sizeof votes, "%d", kk_poll_total(p, -1));
        where_text(p, where, sizeof where);
        GtkTreeIter it;
        gtk_list_store_append(t->store, &it);
        gtk_list_store_set(t->store, &it, P_ID, (guint64)p->id, P_STATE, p->closed ? "" : "●",
                           P_QUESTION, p->question, P_OPENED, opened, P_LENGTH, length,
                           P_VOTES, votes, P_WHERE, where, -1);
        /* The open one is selected when nothing else is. */
        if (p->id == keep || (!keep && !p->closed))
            gtk_tree_selection_select_iter(sel, &it);
    }
}

/* Reads the file again if it changed (or always, with force). */
static void refresh(editor *e, bool force)
{
    polls_tab *t = e->polls;
    char path[KK_PATH_MAX];
    if (!history_path(e, path, sizeof path))
        path[0] = '\0';
    struct stat st;
    bool have = path[0] && stat(path, &st) == 0;
    bool same = have == t->have_file && strcmp(path, t->path) == 0 &&
                (!have || (st.st_mtim.tv_sec == t->mtime.tv_sec &&
                           st.st_mtim.tv_nsec == t->mtime.tv_nsec && st.st_size == t->fsize));
    if (same && !force && t->polls) {
        show_details(e); /* the time left moves on */
        return;
    }
    snprintf(t->path, sizeof t->path, "%s", path);
    t->have_file = have;
    if (have) {
        t->mtime = st.st_mtim;
        t->fsize = st.st_size;
    }
    kk_polls_free(t->polls);
    t->polls = kk_polls_open(path[0] ? path : NULL);
    t->reads++;
    fill(e);
    show_details(e);
    int n = t->polls ? kk_polls_count(t->polls) : 0;
    char text[160];
    snprintf(text, sizeof text, ngettext("%d poll.", "%d polls.", n), n);
    gtk_label_set_text(GTK_LABEL(t->note), text);
}

static gboolean on_timer(gpointer ud)
{
    editor *e = ud;
    if (gtk_widget_get_mapped(e->polls->view))
        refresh(e, false);
    return TRUE;
}

static void on_map(GtkWidget *w, gpointer ud)
{
    (void)w;
    refresh(ud, false);
}

static void on_selection(GtkTreeSelection *s, gpointer ud)
{
    (void)s;
    show_details(ud);
}

/* ---- talking to kikarinhas ------------------------------------------------ */

/* Sends req (deleted here); true if kikarinhas said ok. Otherwise err gets
 * why: its own message, or that it is not running (*running false). */
static bool send(editor *e, cJSON *req, char *err, size_t size, bool *running)
{
    char *line = cJSON_PrintUnformatted(req);
    cJSON_Delete(req);
    char reply[8192];
    bool sent = line && editor_request(e, line, reply, sizeof reply);
    free(line);
    if (running)
        *running = sent;
    if (!sent) {
        snprintf(err, size, "%s", _("kikarinhas is not running: start it first, polls open there."));
        return false;
    }
    cJSON *r = cJSON_Parse(reply);
    bool ok = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(r, "ok"));
    if (!ok) {
        const char *why = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(r, "error"));
        snprintf(err, size, "%s", why ? why : _("kikarinhas refused it."));
    }
    cJSON_Delete(r);
    return ok;
}

static cJSON *request(const char *action)
{
    cJSON *r = cJSON_CreateObject();
    cJSON_AddStringToObject(r, "type", "poll");
    cJSON_AddStringToObject(r, "action", action);
    return r;
}

static bool ask(editor *e, const char *question, const char *detail, const char *yes)
{
    GtkWidget *d = gtk_message_dialog_new(GTK_WINDOW(e->window), GTK_DIALOG_MODAL,
                                          GTK_MESSAGE_QUESTION, GTK_BUTTONS_NONE, "%s",
                                          question);
    if (detail)
        gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(d), "%s", detail);
    gtk_dialog_add_buttons(GTK_DIALOG(d), _("_Cancel"), GTK_RESPONSE_CANCEL, yes,
                           GTK_RESPONSE_OK, NULL);
    gtk_dialog_set_default_response(GTK_DIALOG(d), GTK_RESPONSE_CANCEL);
    bool ok = gtk_dialog_run(GTK_DIALOG(d)) == GTK_RESPONSE_OK;
    gtk_widget_destroy(d);
    return ok;
}

static void on_end(GtkButton *b, gpointer ud)
{
    (void)b;
    editor *e = ud;
    const kk_poll *p = selected_poll(e->polls);
    if (!p || p->closed ||
        !ask(e, _("End the poll now?"), _("Votes stop counting and the result shows on the stage."),
             _("_End")))
        return;
    char err[512];
    if (send(e, request("close"), err, sizeof err, NULL))
        status(e, "%s", _("Poll ended."));
    else
        status(e, "%s", err);
    refresh(e, true);
}

static void on_delete(GtkButton *b, gpointer ud)
{
    (void)b;
    editor *e = ud;
    polls_tab *t = e->polls;
    const kk_poll *p = selected_poll(t);
    if (!p || !p->closed ||
        !ask(e, _("Delete this poll from the history?"), _("Its results are lost."),
             _("_Delete")))
        return;
    uint64_t id = p->id;
    cJSON *r = request("remove");
    cJSON_AddNumberToObject(r, "id", (double)id);
    char err[512];
    bool running;
    if (send(e, r, err, sizeof err, &running)) {
        status(e, "%s", _("Poll deleted."));
    } else if (running) {
        status(e, "%s", err);
    } else if (kk_polls_remove(t->polls, id) && kk_polls_save(t->polls) == 0) {
        /* Nobody else writes the file now. */
        status(e, "%s", _("Poll deleted."));
    } else {
        status(e, _("Could not write %s."), t->path);
    }
    refresh(e, true);
}

/* ---- the new poll dialog -------------------------------------------------- */

typedef struct {
    editor *e;
    GtkWidget *dlg, *question, *rows, *add, *limit, *minutes, *seconds, *counts, *problem;
    GtkWidget *row[KK_POLL_MAX_OPTIONS], *label[KK_POLL_MAX_OPTIONS],
        *text[KK_POLL_MAX_OPTIONS], *cmd[KK_POLL_MAX_OPTIONS], *remove[KK_POLL_MAX_OPTIONS];
    GtkWidget *scroll;
    int n;
} poll_dialog;

static void problem(poll_dialog *d, const char *msg)
{
    char *markup = g_markup_printf_escaped("<span foreground=\"#c01c28\">%s</span>", msg);
    gtk_label_set_markup(GTK_LABEL(d->problem), markup);
    g_free(markup);
}

static void show_rows(poll_dialog *d)
{
    for (int i = 0; i < KK_POLL_MAX_OPTIONS; i++) {
        if (i < d->n)
            gtk_widget_show_all(d->row[i]);
        else
            gtk_widget_hide(d->row[i]);
        gtk_widget_set_sensitive(d->remove[i], d->n > 2);
    }
    gtk_widget_set_sensitive(d->add, d->n < KK_POLL_MAX_OPTIONS);
    dialog_body_fit(d->scroll);
}

static void on_add_option(GtkButton *b, gpointer ud)
{
    (void)b;
    poll_dialog *d = ud;
    if (d->n == KK_POLL_MAX_OPTIONS)
        return;
    d->n++;
    show_rows(d);
    gtk_widget_grab_focus(d->text[d->n - 1]);
}

static void on_remove_option(GtkButton *b, gpointer ud)
{
    poll_dialog *d = ud;
    int i = 0;
    while (i < d->n && d->remove[i] != GTK_WIDGET(b))
        i++;
    if (i == d->n || d->n <= 2)
        return;
    for (; i < d->n - 1; i++) {
        gtk_entry_set_text(GTK_ENTRY(d->text[i]), text_of(d->text[i + 1]));
        gtk_entry_set_text(GTK_ENTRY(d->cmd[i]), text_of(d->cmd[i + 1]));
    }
    gtk_entry_set_text(GTK_ENTRY(d->text[d->n - 1]), "");
    gtk_entry_set_text(GTK_ENTRY(d->cmd[d->n - 1]), "");
    d->n--;
    show_rows(d);
}

static void on_limit(GtkToggleButton *b, gpointer ud)
{
    poll_dialog *d = ud;
    bool on = gtk_toggle_button_get_active(b);
    gtk_widget_set_sensitive(d->minutes, on);
    gtk_widget_set_sensitive(d->seconds, on);
}

static GtkWidget *spin_at(double lo, double hi, double v)
{
    GtkWidget *s = spin(lo, hi, 1, 0);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(s), v);
    return s;
}

/* from: a poll to start from (its question, options and settings), or
 * NULL for an empty one. */
static void build_dialog(poll_dialog *d, const kk_poll *from)
{
    editor *e = d->e;
    d->dlg = gtk_dialog_new_with_buttons(_("New poll"), GTK_WINDOW(e->window),
                                         GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT, NULL,
                                         NULL);
    gtk_dialog_add_button(GTK_DIALOG(d->dlg), _("_Cancel"), GTK_RESPONSE_CANCEL);
    gtk_dialog_add_button(GTK_DIALOG(d->dlg), _("_Start the poll"), GTK_RESPONSE_OK);
    gtk_dialog_set_default_response(GTK_DIALOG(d->dlg), GTK_RESPONSE_OK);
    GtkWidget *table = gtk_table_new(1, 2, FALSE);
    gtk_table_set_row_spacings(GTK_TABLE(table), 6);
    gtk_table_set_col_spacings(GTK_TABLE(table), 12);
    gtk_container_set_border_width(GTK_CONTAINER(table), 12);

    d->question = row(table, _("Question"), entry(from ? from->question : ""), NULL);
    gtk_entry_set_width_chars(GTK_ENTRY(d->question), 50);

    d->rows = gtk_vbox_new(FALSE, 4);
    GtkWidget *head = gtk_hbox_new(FALSE, 6);
    GtkWidget *h1 = gtk_label_new(_("Option")), *h2 = gtk_label_new(_("Own command (optional)"));
    gtk_misc_set_alignment(GTK_MISC(h1), 0, 0.5);
    gtk_box_pack_start(GTK_BOX(head), gtk_label_new("      "), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(head), h1, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(head), h2, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(head), gtk_label_new("      "), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(d->rows), head, FALSE, FALSE, 0);
    for (int i = 0; i < KK_POLL_MAX_OPTIONS; i++) {
        char l[16];
        snprintf(l, sizeof l, "%c / %d", 'A' + i, i + 1);
        d->row[i] = gtk_hbox_new(FALSE, 6);
        d->label[i] = gtk_label_new(l);
        gtk_widget_set_size_request(d->label[i], 44, -1);
        d->text[i] = entry(from && i < from->n_options ? from->options[i].text : "");
        d->cmd[i] = entry(from && i < from->n_options && from->options[i].shortcut
                              ? from->options[i].shortcut
                              : "");
        gtk_entry_set_width_chars(GTK_ENTRY(d->cmd[i]), 14);
        gtk_entry_set_activates_default(GTK_ENTRY(d->text[i]), TRUE);
        gtk_entry_set_activates_default(GTK_ENTRY(d->cmd[i]), TRUE);
        d->remove[i] = gtk_button_new();
        gtk_button_set_image(GTK_BUTTON(d->remove[i]),
                             gtk_image_new_from_stock(GTK_STOCK_REMOVE, GTK_ICON_SIZE_MENU));
        gtk_widget_set_tooltip_text(d->remove[i], _("Remove this option"));
        /* Tab goes from field to field. */
        gtk_widget_set_can_focus(d->remove[i], FALSE);
        g_signal_connect(d->remove[i], "clicked", G_CALLBACK(on_remove_option), d);
        gtk_box_pack_start(GTK_BOX(d->row[i]), d->label[i], FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(d->row[i]), d->text[i], TRUE, TRUE, 0);
        gtk_box_pack_start(GTK_BOX(d->row[i]), gtk_label_new("!"), FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(d->row[i]), d->cmd[i], FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(d->row[i]), d->remove[i], FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(d->rows), d->row[i], FALSE, FALSE, 0);
    }
    d->add = icon_button(GTK_STOCK_ADD, _("Add an _option"));
    g_signal_connect(d->add, "clicked", G_CALLBACK(on_add_option), d);
    GtkWidget *add_box = gtk_hbox_new(FALSE, 0);
    gtk_box_pack_start(GTK_BOX(add_box), d->add, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(d->rows), add_box, FALSE, FALSE, 0);
    row(table, _("Options"), d->rows,
        _("The chat votes with !a, !b... or !1, !2... (those that are not another command) "
          "and with an option's own command, if you give it one. Each person has one vote; "
          "voting again changes it. kikarinhas refuses to start if an own command is "
          "already a command or an avatar name."));

    GtkWidget *time_box = gtk_hbox_new(FALSE, 6);
    int dur = from ? from->duration : 0;
    d->limit = check(_("Ends by itself after"), dur > 0);
    d->minutes = spin_at(0, 1440, dur > 0 ? dur / 60 : 2);
    d->seconds = spin_at(0, 59, dur > 0 ? dur % 60 : 0);
    gtk_box_pack_start(GTK_BOX(time_box), d->limit, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(time_box), d->minutes, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(time_box), gtk_label_new(_("min")), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(time_box), d->seconds, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(time_box), gtk_label_new(_("s")), FALSE, FALSE, 0);
    g_signal_connect(d->limit, "toggled", G_CALLBACK(on_limit), d);
    on_limit(GTK_TOGGLE_BUTTON(d->limit), d);
    row(table, _("Time"), time_box,
        _("Without a time limit it stays open until you click \"End now\" here."));
    d->counts = row(table, NULL,
                    check(_("Show the votes on the stream"), from ? from->show_counts : true),
                    _("Otherwise the stage shows only the options; the results stay here."));

    d->problem = gtk_label_new(NULL);
    gtk_misc_set_alignment(GTK_MISC(d->problem), 0, 0.5);
    gtk_label_set_line_wrap(GTK_LABEL(d->problem), TRUE);
    d->scroll = dialog_body(d->dlg, table);
    gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(d->dlg))), d->problem,
                       FALSE, FALSE, 6);
    d->n = from && from->n_options > 2 ? from->n_options : 2;
    gtk_widget_show_all(d->dlg);
    show_rows(d);
}

/* Asks kikarinhas to open the poll in the dialog; false (and why) if it
 * can't. */
static bool start(poll_dialog *d)
{
    gchar *q = g_strstrip(g_strdup(text_of(d->question)));
    bool empty_q = !q[0];
    if (empty_q) {
        g_free(q);
        problem(d, _("Write the question."));
        return false;
    }
    cJSON *r = request("start");
    cJSON_AddStringToObject(r, "question", q);
    g_free(q);
    cJSON *opts = cJSON_AddArrayToObject(r, "options");
    for (int i = 0; i < d->n; i++) {
        gchar *t = g_strstrip(g_strdup(text_of(d->text[i])));
        gchar *c = g_strstrip(g_strdup(text_of(d->cmd[i])));
        if (!t[0]) {
            g_free(t);
            g_free(c);
            cJSON_Delete(r);
            problem(d, _("Every option needs a text (or remove the empty ones)."));
            return false;
        }
        cJSON *o = cJSON_CreateObject();
        cJSON_AddStringToObject(o, "text", t);
        if (c[0])
            cJSON_AddStringToObject(o, "command", c);
        cJSON_AddItemToArray(opts, o);
        g_free(t);
        g_free(c);
    }
    int dur = 0;
    if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(d->limit))) {
        dur = spin_int(d->minutes) * 60 + spin_int(d->seconds);
        if (dur < 5) {
            cJSON_Delete(r);
            problem(d, _("A time limit of at least 5 seconds, or no limit."));
            return false;
        }
    }
    cJSON_AddNumberToObject(r, "duration", dur);
    cJSON_AddBoolToObject(r, "show_counts",
                          gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(d->counts)));
    char err[512];
    if (!send(d->e, r, err, sizeof err, NULL)) {
        problem(d, err);
        return false;
    }
    return true;
}

static void new_poll(editor *e, const kk_poll *from)
{
    poll_dialog d = {.e = e};
    build_dialog(&d, from);
    bool ok = false;
    while (!ok && gtk_dialog_run(GTK_DIALOG(d.dlg)) == GTK_RESPONSE_OK)
        ok = start(&d);
    gtk_widget_destroy(d.dlg);
    if (ok) {
        status(e, "%s", _("Poll started: it is on the stage."));
        GtkTreeSelection *sel = gtk_tree_view_get_selection(GTK_TREE_VIEW(e->polls->view));
        gtk_tree_selection_unselect_all(sel); /* the open one gets selected */
        refresh(e, true);
    }
}

static void on_new(GtkButton *b, gpointer ud)
{
    (void)b;
    new_poll(ud, NULL);
}

static void on_duplicate(GtkButton *b, gpointer ud)
{
    (void)b;
    editor *e = ud;
    const kk_poll *p = selected_poll(e->polls);
    if (p)
        new_poll(e, p);
}

/* ---- the tab -------------------------------------------------------------- */

static void text_column(GtkWidget *view, const char *title, int col, bool expand)
{
    GtkCellRenderer *r = gtk_cell_renderer_text_new();
    if (expand)
        g_object_set(r, "ellipsize", PANGO_ELLIPSIZE_END, NULL);
    GtkTreeViewColumn *c = gtk_tree_view_column_new_with_attributes(title, r, "text", col, NULL);
    gtk_tree_view_column_set_expand(c, expand);
    gtk_tree_view_column_set_resizable(c, TRUE);
    gtk_tree_view_append_column(GTK_TREE_VIEW(view), c);
}

static GtkWidget *scrolled_box(GtkWidget *child, int height)
{
    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_AUTOMATIC,
                                   GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(scroll), GTK_SHADOW_IN);
    gtk_container_add(GTK_CONTAINER(scroll), child);
    gtk_widget_set_size_request(scroll, -1, height);
    return scroll;
}

GtkWidget *polls_page(editor *e, const kk_config *cfg)
{
    polls_tab *t = calloc(1, sizeof *t);
    e->polls = t;

    GtkWidget *box = gtk_vbox_new(FALSE, 6);
    gtk_container_set_border_width(GTK_CONTAINER(box), 12);
    gtk_box_pack_start(GTK_BOX(box),
                       hint_label(_("The chat votes with !a, !b... (or !1, !2...) and the window's "
                                    "width splits into one part per option: each voter's avatar "
                                    "walks to their option's part and stays there until the poll "
                                    "ends. One poll open at a time; every poll stays in the "
                                    "history with its results, per platform. ● open now.")),
                       FALSE, FALSE, 0);

    t->store = gtk_list_store_new(N_PCOLS, G_TYPE_UINT64, G_TYPE_STRING, G_TYPE_STRING,
                                  G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);
    t->view = gtk_tree_view_new_with_model(GTK_TREE_MODEL(t->store));
    g_object_unref(t->store);
    text_column(t->view, "", P_STATE, false);
    text_column(t->view, _("Question"), P_QUESTION, true);
    text_column(t->view, _("Opened"), P_OPENED, false);
    text_column(t->view, _("Lasted"), P_LENGTH, false);
    text_column(t->view, _("Votes"), P_VOTES, false);
    text_column(t->view, _("Lives"), P_WHERE, false);
    g_signal_connect(gtk_tree_view_get_selection(GTK_TREE_VIEW(t->view)), "changed",
                     G_CALLBACK(on_selection), e);
    gtk_box_pack_start(GTK_BOX(box), scrolled_box(t->view, 160), TRUE, TRUE, 0);

    GtkWidget *buttons = gtk_hbox_new(FALSE, 6);
    GtkWidget *new_b = icon_button(GTK_STOCK_NEW, _("_New poll"));
    t->dup_b = icon_button(GTK_STOCK_COPY, _("New _from this one"));
    t->end_b = icon_button(GTK_STOCK_STOP, _("_End now"));
    t->del_b = icon_button(GTK_STOCK_DELETE, _("_Delete"));
    g_signal_connect(new_b, "clicked", G_CALLBACK(on_new), e);
    g_signal_connect(t->dup_b, "clicked", G_CALLBACK(on_duplicate), e);
    g_signal_connect(t->end_b, "clicked", G_CALLBACK(on_end), e);
    g_signal_connect(t->del_b, "clicked", G_CALLBACK(on_delete), e);
    gtk_box_pack_start(GTK_BOX(buttons), new_b, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(buttons), t->dup_b, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(buttons), t->end_b, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(buttons), t->del_b, FALSE, FALSE, 0);
    t->note = gtk_label_new(NULL);
    gtk_misc_set_alignment(GTK_MISC(t->note), 1, 0.5);
    gtk_box_pack_start(GTK_BOX(buttons), t->note, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(box), buttons, FALSE, FALSE, 0);

    t->details = gtk_label_new(NULL);
    gtk_label_set_line_wrap(GTK_LABEL(t->details), TRUE);
    gtk_label_set_selectable(GTK_LABEL(t->details), TRUE);
    gtk_widget_set_size_request(t->details, 820, -1);
    gtk_misc_set_alignment(GTK_MISC(t->details), 0, 0);
    gtk_box_pack_start(GTK_BOX(box), t->details, FALSE, FALSE, 6);
    t->results = gtk_tree_view_new();
    gtk_box_pack_start(GTK_BOX(box), scrolled_box(t->results, 150), TRUE, TRUE, 0);

    GtkWidget *tb = gtk_table_new(1, 2, FALSE);
    gtk_table_set_row_spacings(GTK_TABLE(tb), 6);
    gtk_table_set_col_spacings(GTK_TABLE(tb), 12);
    const char *raw = kk_ini_get(e->ini, "polls", "file");
    t->file = row(tb, _("History file"), entry(raw ? raw : cfg->polls_file),
                  _("Empty: ~/.local/share/kikarinhas/polls.json. Only changes when "
                    "kikarinhas restarts."));
    t->panel = row(tb, _("On the stage"),
                   check(_("Show the question and the options with their results"),
                         cfg->poll_panel),
                   _("Off: only the coloured floor of each option's part, and the voters' "
                     "avatars walking inside it."));
    t->align = gtk_combo_box_text_new();
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(t->align), _("centred"));
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(t->align), _("on the left"));
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(t->align), _("on the right"));
    gtk_combo_box_set_active(GTK_COMBO_BOX(t->align), cfg->poll_align);
    row(tb, _("Question"), t->align, NULL);
    t->y = spin(0, 90, 1, 0);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(t->y), cfg->poll_y);
    GtkWidget *y_box = gtk_hbox_new(FALSE, 6);
    gtk_box_pack_start(GTK_BOX(y_box), t->y, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(y_box), gtk_label_new("%"), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(y_box), gtk_label_new(_("of the window height, from the top")),
                       FALSE, FALSE, 0);
    row(tb, _("Cards start at"), y_box,
        _("Where the question and the options go: 0 at the very top."));
    t->font = font_entry(cfg->poll_font);
    t->size = spin(4, 200, 1, 1);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(t->size), cfg->poll_size);
    GtkWidget *font_box = gtk_hbox_new(FALSE, 6);
    gtk_box_pack_start(GTK_BOX(font_box), t->font, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(font_box), t->size, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(font_box), gtk_label_new("pt"), FALSE, FALSE, 0);
    row(tb, _("Font on the stage"), font_box, _("Empty: Sans."));
    t->linger = spin(0, 3600, 1, 0);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(t->linger), cfg->poll_results);
    GtkWidget *linger = gtk_hbox_new(FALSE, 6);
    gtk_box_pack_start(GTK_BOX(linger), t->linger, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(linger), gtk_label_new(_("seconds")), FALSE, FALSE, 0);
    row(tb, _("Final result stays"), linger,
        _("On the stage after the poll ends, the winner marked; 0: it goes at once."));
    gtk_box_pack_start(GTK_BOX(box), tb, FALSE, FALSE, 6);

    g_signal_connect(box, "map", G_CALLBACK(on_map), e);
    t->timer = g_timeout_add_seconds(2, on_timer, e);
    return box;
}

void polls_collect(editor *e)
{
    polls_tab *t = e->polls;
    kk_config d;
    kk_config_defaults(&d);
    put(e, "polls", "file", text_of(t->file), NULL);
    put_bool(e, "polls", "panel", t->panel, d.poll_panel);
    static const char *const aligns[] = {"center", "left", "right"};
    int al = gtk_combo_box_get_active(GTK_COMBO_BOX(t->align));
    put(e, "polls", "align", aligns[al >= 0 && al < 3 ? al : 0], aligns[d.poll_align]);
    put_num(e, "polls", "y", gtk_spin_button_get_value(GTK_SPIN_BUTTON(t->y)), d.poll_y);
    put(e, "polls", "font", text_of(t->font), NULL);
    put_num(e, "polls", "font_size", gtk_spin_button_get_value(GTK_SPIN_BUTTON(t->size)),
            d.poll_size);
    put_int(e, "polls", "results_seconds", spin_int(t->linger), (int)d.poll_results);
    kk_config_free(&d);
}

void polls_free(editor *e)
{
    polls_tab *t = e->polls;
    if (!t)
        return;
    if (t->timer)
        g_source_remove(t->timer);
    kk_polls_free(t->polls);
    free(t);
    e->polls = NULL;
}
