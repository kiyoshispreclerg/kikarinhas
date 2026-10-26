/* SPDX-License-Identifier: GPL-3.0-or-later */
/* kikarinhas-config: the "Espectadores" tab, everyone in users.tsv.
 *
 * Besides the avatar, the level each one had on their latest message (from
 * the chat badges) is shown, and "trusted" can be ticked by hand: commands
 * can then be limited to trusted people and up.
 *
 * While kikarinhas runs it owns that file (it rewrites it every 30 s), so
 * this tab asks it to save before reading and sends avatar changes through
 * the control socket; only when nobody answers does it write the file
 * itself. The avatar list comes from every avatar folder (editor_library),
 * read the first time the tab is shown. */
#include <gdk/gdkkeysyms.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "cJSON.h"
#include "editor.h"
#include "i18n.h"
#include "sa.h"
#include "users.h"

enum {
    A_KEY,
    A_NAME,
    A_PLATFORM,
    A_FIRST, /* int64 Unix time, 0 unknown */
    A_FIRST_TEXT,
    A_LAST,
    A_LAST_TEXT,
    A_AVATAR,
    A_LEVEL,   /* int kk_role from the badges */
    A_LEVEL_TEXT,
    A_TRUSTED, /* bool, set by hand */
    A_POINTS,  /* long; -1 = none yet */
    A_LIVE_URL,   /* link of the first live they came from, or "" */
    A_LIVE_TITLE, /* its title, or "" */
    A_LIVE_TEXT,  /* what the column shows: title, else link */
    A_LIVE_TIP,   /* tooltip markup: title and link */
    N_ACOLS,
};

/* One person's level, by kk_role (ROLE_LABELS are plurals, for commands). */
static const char *const LEVEL_LABELS[KK_ROLE_COUNT] = {
    N_("common"), N_("member"), N_("trusted person"), N_("moderator"), N_("owner"),
};

struct audience_tab {
    GtkListStore *store;
    GtkTreeModel *filter, *sort;
    GtkWidget *tree, *search, *live_search, *count;
    GtkTreeViewColumn *points_col; /* its double-click edits, not picks */
    bool shown; /* the list is read the first time the tab shows */
};

static void users_path(editor *e, char *out, size_t size)
{
    const char *s = text_of(e->users);
    if (s[0] == '~')
        kk_pathf(out, size, "%s%s", g_get_home_dir(), s + 1);
    else if (s[0])
        kk_pathf(out, size, "%s", s);
    else if (!kk_users_default_path(out, size))
        out[0] = '\0';
}

static void format_time(char *out, size_t size, long long t)
{
    struct tm tm;
    time_t tt = (time_t)t;
    if (t <= 0 || !localtime_r(&tt, &tm) || !strftime(out, size, _("%Y-%m-%d %H:%M"), &tm))
        snprintf(out, size, "—");
}

static const char *platform_label(const char *key, char *buf, size_t size)
{
    size_t n = strcspn(key, ":");
    if (n == 7 && strncmp(key, "youtube", 7) == 0)
        return "YouTube";
    if (n == 6 && strncmp(key, "twitch", 6) == 0)
        return "Twitch";
    if (n == 6 && strncmp(key, "odysee", 6) == 0)
        return "Odysee";
    if (n == 4 && strncmp(key, "kick", 4) == 0)
        return "Kick";
    snprintf(buf, size, "%.*s", (int)n, key);
    return buf;
}

static long long time_field(const kk_users *u, const char *key, kk_user_field f)
{
    const char *s = kk_users_get(u, key, f);
    return s ? strtoll(s, NULL, 10) : 0;
}

static void update_count(editor *e)
{
    audience_tab *t = e->audience;
    int all = gtk_tree_model_iter_n_children(GTK_TREE_MODEL(t->store), NULL);
    int shown = gtk_tree_model_iter_n_children(t->filter, NULL);
    char s[64];
    if (shown == all)
        snprintf(s, sizeof s, ngettext("%d viewer", "%d viewers", all), all);
    else
        snprintf(s, sizeof s, _("%d of %d viewers"), shown, all);
    gtk_label_set_text(GTK_LABEL(t->count), s);
}

static void refresh(editor *e)
{
    audience_tab *t = e->audience;
    char reply[256], path[KK_PATH_MAX];
    /* The running kikarinhas may hold up to 30 s of news. */
    bool running = editor_request(e, "{\"type\":\"save\"}", reply, sizeof reply);
    editor_refresh_kikarinhas_version(e);
    users_path(e, path, sizeof path);
    gtk_list_store_clear(t->store);
    kk_users *u = path[0] ? kk_users_open(path) : NULL;
    if (!u) {
        status(e, _("Could not read the people file %s."), path);
        return;
    }
    for (int i = 0; i < kk_users_count(u); i++) {
        const char *key = kk_users_key(u, i);
        if (strncmp(key, "demo:", 5) == 0)
            continue;
        const char *name = kk_users_get(u, key, KK_USER_NAME);
        const char *avatar = kk_users_get(u, key, KK_USER_AVATAR);
        long long first = time_field(u, key, KK_USER_FIRST);
        long long last = time_field(u, key, KK_USER_LAST);
        char first_s[32], last_s[32], plat[32];
        format_time(first_s, sizeof first_s, first);
        format_time(last_s, sizeof last_s, last);
        const char *id = strchr(key, ':');
        const char *role_s = kk_users_get(u, key, KK_USER_ROLE);
        kk_role role = KK_ROLE_ANYONE;
        if (role_s)
            kk_parse_role(role_s, &role);
        const char *pts = kk_users_get(u, key, KK_USER_POINTS);
        const char *live_url = kk_users_get(u, key, KK_USER_LIVE_URL);
        const char *live_title = kk_users_get(u, key, KK_USER_LIVE_TITLE);
        live_url = live_url ? live_url : "";
        live_title = live_title ? live_title : "";
        gchar *e_url = g_markup_escape_text(live_url, -1);
        gchar *e_title = g_markup_escape_text(live_title, -1);
        gchar *tip = !live_url[0]     ? NULL
                     : live_title[0] ? g_strdup_printf("%s\n%s", e_title, e_url)
                                     : g_strdup(e_url);
        GtkTreeIter it;
        gtk_list_store_append(t->store, &it);
        gtk_list_store_set(t->store, &it, A_KEY, key, A_NAME,
                           name ? name : (id ? id + 1 : key), A_PLATFORM,
                           platform_label(key, plat, sizeof plat), A_FIRST,
                           (gint64)first, A_FIRST_TEXT, first_s, A_LAST, (gint64)last,
                           A_LAST_TEXT, last_s, A_AVATAR, avatar ? avatar : "", A_LEVEL,
                           (int)role, A_LEVEL_TEXT, _(LEVEL_LABELS[role]), A_TRUSTED,
                           kk_users_trusted(u, key), A_POINTS,
                           (glong)(pts ? strtol(pts, NULL, 10) : -1), A_LIVE_URL, live_url,
                           A_LIVE_TITLE, live_title, A_LIVE_TEXT,
                           live_title[0] ? live_title : live_url, A_LIVE_TIP, tip, -1);
        g_free(tip);
        g_free(e_url);
        g_free(e_title);
    }
    kk_users_free(u);
    update_count(e);
    status(e, "%s%s", path,
           running ? _(" (just read from the running kikarinhas)") : "");
}

static void on_refresh(GtkButton *b, gpointer ud)
{
    (void)b;
    refresh(ud);
}

/* ---- search -------------------------------------------------------------- */

static bool has(const char *hay, const char *needle_folded)
{
    if (!hay)
        return false;
    char *f = g_utf8_casefold(hay, -1);
    bool yes = strstr(f, needle_folded) != NULL;
    g_free(f);
    return yes;
}

static gboolean visible(GtkTreeModel *m, GtkTreeIter *it, gpointer ud)
{
    editor *e = ud;
    if (!e->audience || !e->audience->search)
        return TRUE;
    const char *q = gtk_entry_get_text(GTK_ENTRY(e->audience->search));
    const char *lq = gtk_entry_get_text(GTK_ENTRY(e->audience->live_search));
    bool yes = true;
    if (q[0]) {
        char *needle = g_utf8_casefold(q, -1);
        gchar *name, *key, *avatar;
        gtk_tree_model_get(m, it, A_NAME, &name, A_KEY, &key, A_AVATAR, &avatar, -1);
        yes = has(name, needle) || has(key, needle) || has(avatar, needle);
        g_free(name);
        g_free(key);
        g_free(avatar);
        g_free(needle);
    }
    if (yes && lq[0]) {
        char *needle = g_utf8_casefold(lq, -1);
        gchar *title, *url;
        gtk_tree_model_get(m, it, A_LIVE_TITLE, &title, A_LIVE_URL, &url, -1);
        yes = has(title, needle) || has(url, needle);
        g_free(title);
        g_free(url);
        g_free(needle);
    }
    return yes;
}

static void on_search(GtkEditable *ed, gpointer ud)
{
    (void)ed;
    editor *e = ud;
    gtk_tree_model_filter_refilter(GTK_TREE_MODEL_FILTER(e->audience->filter));
    update_count(e);
}

/* ---- changing someone's avatar ------------------------------------------- */

/* First time the tab is shown: read the list and the avatars. */
static void on_map(GtkWidget *w, gpointer ud)
{
    (void)w;
    editor *e = ud;
    if (e->audience->shown)
        return;
    e->audience->shown = true;
    refresh(e);
}

/* Writes the file directly: only when no kikarinhas is running. avatar
 * NULL keeps it; trusted and points < 0 keep theirs. */
static bool write_directly(editor *e, const char *key, const char *avatar, int trusted,
                           long points)
{
    char path[KK_PATH_MAX];
    users_path(e, path, sizeof path);
    kk_users *u = path[0] ? kk_users_open(path) : NULL;
    if (!u)
        return false;
    if (avatar) {
        kk_users_set(u, key, KK_USER_AVATAR, avatar);
        kk_users_set(u, key, KK_USER_PALETTE, NULL); /* palettes belong to an avatar */
    }
    if (trusted >= 0)
        kk_users_set(u, key, KK_USER_TRUSTED, trusted ? "yes" : NULL);
    if (points >= 0) {
        char s[32];
        snprintf(s, sizeof s, "%ld", points);
        kk_users_set(u, key, KK_USER_POINTS, s);
    }
    bool ok = kk_users_save(u) == 0;
    kk_users_free(u);
    return ok;
}

typedef enum { SENT_OK, SENT_REFUSED, NOT_RUNNING } send_result;

/* Sends a request of the given type (made by the caller, freed here) to the
 * running kikarinhas. On SENT_REFUSED, the status line already says why. */
static send_result send_request(editor *e, cJSON *req, const char *type)
{
    cJSON_AddStringToObject(req, "type", type);
    char *line = cJSON_PrintUnformatted(req);
    cJSON_Delete(req);
    char reply[512];
    bool sent = line && editor_request(e, line, reply, sizeof reply);
    free(line);
    if (!sent)
        return NOT_RUNNING;
    cJSON *rep = cJSON_Parse(reply);
    bool ok = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(rep, "ok"));
    const char *err = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(rep, "error"));
    if (!ok)
        status(e, _("kikarinhas refused: %s"), err ? err : reply);
    cJSON_Delete(rep);
    return ok ? SENT_OK : SENT_REFUSED;
}

static send_result send_user(editor *e, cJSON *req)
{
    return send_request(e, req, "set_user");
}

/* The store row under a path of the sorted view. */
static bool store_iter(audience_tab *t, const gchar *path, GtkTreeIter *it)
{
    GtkTreeIter sit, fit;
    GtkTreePath *p = gtk_tree_path_new_from_string(path);
    bool found = gtk_tree_model_get_iter(t->sort, &sit, p);
    gtk_tree_path_free(p);
    if (!found)
        return false;
    gtk_tree_model_sort_convert_iter_to_child_iter(GTK_TREE_MODEL_SORT(t->sort), &fit, &sit);
    gtk_tree_model_filter_convert_iter_to_child_iter(GTK_TREE_MODEL_FILTER(t->filter), it,
                                                     &fit);
    return true;
}

static void on_trusted_toggled(GtkCellRendererToggle *r, gchar *path, gpointer ud)
{
    editor *e = ud;
    audience_tab *t = e->audience;
    GtkTreeIter it;
    if (!store_iter(t, path, &it))
        return;
    bool on = !gtk_cell_renderer_toggle_get_active(r);
    gchar *key, *name;
    gtk_tree_model_get(GTK_TREE_MODEL(t->store), &it, A_KEY, &key, A_NAME, &name, -1);
    cJSON *req = cJSON_CreateObject();
    cJSON_AddStringToObject(req, "user", key);
    cJSON_AddBoolToObject(req, "trusted", on);
    send_result sr = send_user(e, req);
    if (sr == SENT_OK || (sr == NOT_RUNNING && write_directly(e, key, NULL, on, -1))) {
        gtk_list_store_set(t->store, &it, A_TRUSTED, on, -1);
        status(e, on ? _("%s is now trusted.") : _("%s is no longer trusted."), name);
    } else if (sr == NOT_RUNNING) {
        status(e, _("Could not write the people file."));
    }
    g_free(key);
    g_free(name);
}

static void points_text(GtkTreeViewColumn *c, GtkCellRenderer *r, GtkTreeModel *m,
                        GtkTreeIter *it, gpointer ud)
{
    (void)c;
    (void)ud;
    glong v;
    gtk_tree_model_get(m, it, A_POINTS, &v, -1);
    char s[32];
    snprintf(s, sizeof s, v >= 0 ? "%ld" : "—", (long)v);
    g_object_set(r, "text", s, NULL);
}

static void on_points_edited(GtkCellRendererText *r, gchar *path, gchar *text, gpointer ud)
{
    (void)r;
    editor *e = ud;
    audience_tab *t = e->audience;
    GtkTreeIter it;
    long v;
    g_strstrip(text);
    if (!store_iter(t, path, &it))
        return;
    if (!kk_parse_long(text, 0, 1000000000L, &v)) {
        status(e, _("Invalid points: \"%s\" (0 to 1000000000)."), text);
        return;
    }
    gchar *key, *name;
    gtk_tree_model_get(GTK_TREE_MODEL(t->store), &it, A_KEY, &key, A_NAME, &name, -1);
    cJSON *req = cJSON_CreateObject();
    cJSON_AddStringToObject(req, "user", key);
    cJSON_AddNumberToObject(req, "points", (double)v);
    send_result sr = send_user(e, req);
    if (sr == SENT_OK || (sr == NOT_RUNNING && write_directly(e, key, NULL, -1, v))) {
        gtk_list_store_set(t->store, &it, A_POINTS, (glong)v, -1);
        status(e, _("%s now has %ld points."), name, v);
    } else if (sr == NOT_RUNNING) {
        status(e, _("Could not write the people file."));
    }
    g_free(key);
    g_free(name);
}

/* Gives the person at it the avatar key ("" = back to the one picked for
 * them), through the running kikarinhas or, without it, in the file. */
static void set_avatar(editor *e, GtkTreeIter *it, const char *avatar)
{
    audience_tab *t = e->audience;
    gchar *key, *name, *old;
    gtk_tree_model_get(GTK_TREE_MODEL(t->store), it, A_KEY, &key, A_NAME, &name, A_AVATAR,
                       &old, -1);
    if (strcmp(old, avatar) != 0) {
        cJSON *req = cJSON_CreateObject();
        cJSON_AddStringToObject(req, "user", key);
        cJSON_AddStringToObject(req, "avatar", avatar);
        send_result sr = send_user(e, req);
        const char *shown = avatar[0] ? avatar : _("random");
        if (sr == SENT_OK) {
            gtk_list_store_set(t->store, it, A_AVATAR, avatar, -1);
            status(e, _("%s is now %s (already in effect in the running kikarinhas)."), name,
                   shown);
        } else if (sr == SENT_REFUSED) {
            /* the status line says why */
        } else if (write_directly(e, key, avatar, -1, -1)) {
            gtk_list_store_set(t->store, it, A_AVATAR, avatar, -1);
            status(e, _("%s is now %s (saved to the file; takes effect next time)."), name,
                   shown);
        } else {
            status(e, _("Could not write the people file."));
        }
    }
    g_free(key);
    g_free(name);
    g_free(old);
}

/* Double-click on someone: pick their avatar by its picture. */
static void on_row_activated(GtkTreeView *v, GtkTreePath *path, GtkTreeViewColumn *col,
                             gpointer ud)
{
    (void)v;
    editor *e = ud;
    audience_tab *t = e->audience;
    if (col == t->points_col)
        return; /* that double-click edits the points */
    GtkTreeIter sit, fit, it;
    if (!gtk_tree_model_get_iter(t->sort, &sit, path))
        return;
    gtk_tree_model_sort_convert_iter_to_child_iter(GTK_TREE_MODEL_SORT(t->sort), &fit, &sit);
    gtk_tree_model_filter_convert_iter_to_child_iter(GTK_TREE_MODEL_FILTER(t->filter), &it,
                                                     &fit);
    gchar *name, *avatar;
    gtk_tree_model_get(GTK_TREE_MODEL(t->store), &it, A_NAME, &name, A_AVATAR, &avatar, -1);
    char title[256];
    snprintf(title, sizeof title, _("Avatar of %s"), name);
    gchar *chosen = editor_pick_avatar(e, title, avatar);
    if (chosen)
        set_avatar(e, &it, chosen);
    g_free(chosen);
    g_free(name);
    g_free(avatar);
}

/* ---- removing people ----------------------------------------------------- */

/* The keys of the selected rows (g_free'd by the array) and a reference to
 * each row of the store, in the same order. */
static GPtrArray *selected_keys(audience_tab *t, GPtrArray **refs)
{
    GPtrArray *keys = g_ptr_array_new_with_free_func(g_free);
    *refs = g_ptr_array_new_with_free_func((GDestroyNotify)gtk_tree_row_reference_free);
    GtkTreeModel *m;
    GList *rows = gtk_tree_selection_get_selected_rows(
        gtk_tree_view_get_selection(GTK_TREE_VIEW(t->tree)), &m);
    for (GList *l = rows; l; l = l->next) {
        GtkTreeIter sit, fit, it;
        if (!gtk_tree_model_get_iter(t->sort, &sit, l->data))
            continue;
        gtk_tree_model_sort_convert_iter_to_child_iter(GTK_TREE_MODEL_SORT(t->sort), &fit, &sit);
        gtk_tree_model_filter_convert_iter_to_child_iter(GTK_TREE_MODEL_FILTER(t->filter), &it,
                                                         &fit);
        gchar *key;
        gtk_tree_model_get(GTK_TREE_MODEL(t->store), &it, A_KEY, &key, -1);
        g_ptr_array_add(keys, key);
        GtkTreePath *sp = gtk_tree_model_get_path(GTK_TREE_MODEL(t->store), &it);
        g_ptr_array_add(*refs, gtk_tree_row_reference_new(GTK_TREE_MODEL(t->store), sp));
        gtk_tree_path_free(sp);
    }
    g_list_free_full(rows, (GDestroyNotify)gtk_tree_path_free);
    return keys;
}

/* The running kikarinhas takes them in batches (a request line has a limit);
 * without it the file is rewritten here. */
static bool forget_people(editor *e, GPtrArray *keys)
{
    enum { BATCH = 400 };
    for (guint from = 0; from < keys->len; from += BATCH) {
        cJSON *req = cJSON_CreateObject();
        cJSON *arr = cJSON_AddArrayToObject(req, "users");
        for (guint i = from; i < keys->len && i < from + BATCH; i++)
            cJSON_AddItemToArray(arr, cJSON_CreateString(keys->pdata[i]));
        send_result sr = send_request(e, req, "remove_users");
        if (sr == SENT_OK)
            continue;
        if (sr == SENT_REFUSED)
            return false;
        char path[KK_PATH_MAX];
        users_path(e, path, sizeof path);
        kk_users *u = path[0] ? kk_users_open(path) : NULL;
        if (!u)
            return false;
        for (guint i = from; i < keys->len; i++)
            kk_users_remove(u, keys->pdata[i]);
        bool ok = kk_users_save(u) == 0;
        kk_users_free(u);
        return ok; /* the rest went along in that write */
    }
    return true;
}

static void on_select_all(GtkButton *b, gpointer ud)
{
    (void)b;
    editor *e = ud;
    gtk_tree_selection_select_all(gtk_tree_view_get_selection(GTK_TREE_VIEW(e->audience->tree)));
    gtk_widget_grab_focus(e->audience->tree);
}

static void on_remove(GtkWidget *w, gpointer ud)
{
    (void)w;
    editor *e = ud;
    audience_tab *t = e->audience;
    GPtrArray *refs, *keys = selected_keys(t, &refs);
    if (keys->len == 0) {
        status(e, _("Select the people to remove first (Select all picks every one shown)."));
    } else {
        char q[256];
        snprintf(q, sizeof q,
                 ngettext("Remove %u person from the list?", "Remove %u people from the list?",
                          keys->len),
                 keys->len);
        GtkWidget *d = gtk_message_dialog_new(GTK_WINDOW(gtk_widget_get_toplevel(t->tree)),
                                              GTK_DIALOG_MODAL, GTK_MESSAGE_QUESTION,
                                              GTK_BUTTONS_NONE, "%s", q);
        gtk_message_dialog_format_secondary_text(
            GTK_MESSAGE_DIALOG(d), "%s",
            _("Their avatar choice, points and dates are forgotten. Export the list first "
              "if you may want them back."));
        gtk_dialog_add_buttons(GTK_DIALOG(d), GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
                               GTK_STOCK_REMOVE, GTK_RESPONSE_OK, NULL);
        gtk_dialog_set_default_response(GTK_DIALOG(d), GTK_RESPONSE_CANCEL);
        bool yes = gtk_dialog_run(GTK_DIALOG(d)) == GTK_RESPONSE_OK;
        gtk_widget_destroy(d);
        if (yes && forget_people(e, keys)) {
            for (guint i = 0; i < refs->len; i++) {
                GtkTreePath *p = gtk_tree_row_reference_get_path(refs->pdata[i]);
                GtkTreeIter it;
                if (p && gtk_tree_model_get_iter(GTK_TREE_MODEL(t->store), &it, p))
                    gtk_list_store_remove(t->store, &it);
                gtk_tree_path_free(p);
            }
            update_count(e);
            status(e, ngettext("%u person removed.", "%u people removed.", keys->len), keys->len);
        } else if (yes) {
            status(e, _("Could not remove them from the people file."));
        }
    }
    g_ptr_array_free(keys, TRUE);
    g_ptr_array_free(refs, TRUE);
}

/* ---- export and import --------------------------------------------------- */

static GtkWidget *file_dialog(audience_tab *t, const char *title, GtkFileChooserAction action,
                              const char *accept_stock)
{
    GtkWidget *d = gtk_file_chooser_dialog_new(
        title, GTK_WINDOW(gtk_widget_get_toplevel(t->tree)), action, GTK_STOCK_CANCEL,
        GTK_RESPONSE_CANCEL, accept_stock, GTK_RESPONSE_ACCEPT, NULL);
    GtkFileFilter *f = gtk_file_filter_new();
    gtk_file_filter_set_name(f, _("People lists (*.tsv)"));
    gtk_file_filter_add_pattern(f, "*.tsv");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(d), f);
    GtkFileFilter *all = gtk_file_filter_new();
    gtk_file_filter_set_name(all, _("All files"));
    gtk_file_filter_add_pattern(all, "*");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(d), all);
    return d;
}

/* Every person with every field, to a file of the user's choice: a backup to
 * bring them back with Import. */
static void on_export(GtkButton *b, gpointer ud)
{
    (void)b;
    editor *e = ud;
    audience_tab *t = e->audience;
    GtkWidget *d = file_dialog(t, _("Export the people"), GTK_FILE_CHOOSER_ACTION_SAVE,
                               GTK_STOCK_SAVE);
    gtk_file_chooser_set_do_overwrite_confirmation(GTK_FILE_CHOOSER(d), TRUE);
    char name[64];
    time_t now = time(NULL);
    struct tm tm;
    if (!localtime_r(&now, &tm) ||
        !strftime(name, sizeof name, "kikarinhas-viewers-%Y-%m-%d.tsv", &tm))
        snprintf(name, sizeof name, "kikarinhas-viewers.tsv");
    gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(d), name);
    if (gtk_dialog_run(GTK_DIALOG(d)) == GTK_RESPONSE_ACCEPT) {
        gchar *file = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(d));
        char reply[256], path[KK_PATH_MAX];
        editor_request(e, "{\"type\":\"save\"}", reply, sizeof reply); /* its news first */
        users_path(e, path, sizeof path);
        kk_users *u = path[0] ? kk_users_open(path) : NULL;
        if (u && kk_users_export(u, file) == 0)
            status(e, ngettext("%d person exported to %s.", "%d people exported to %s.",
                               kk_users_count(u)),
                   kk_users_count(u), file);
        else
            status(e, _("Could not write %s."), file);
        kk_users_free(u);
        g_free(file);
    }
    gtk_widget_destroy(d);
}

enum { IMPORT_SKIP = 1, IMPORT_OVERWRITE };

/* Adds the people of an exported file; for the ones already in the list, the
 * user says whether they stay or are overwritten. */
static void on_import(GtkButton *b, gpointer ud)
{
    (void)b;
    editor *e = ud;
    audience_tab *t = e->audience;
    GtkWidget *d = file_dialog(t, _("Import people"), GTK_FILE_CHOOSER_ACTION_OPEN,
                               GTK_STOCK_OPEN);
    gchar *file = gtk_dialog_run(GTK_DIALOG(d)) == GTK_RESPONSE_ACCEPT
                      ? gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(d))
                      : NULL;
    gtk_widget_destroy(d);
    if (!file)
        return;

    GtkWidget *q = gtk_message_dialog_new(GTK_WINDOW(gtk_widget_get_toplevel(t->tree)),
                                          GTK_DIALOG_MODAL, GTK_MESSAGE_QUESTION,
                                          GTK_BUTTONS_NONE, "%s",
                                          _("What to do with people who are already in the list?"));
    gtk_message_dialog_format_secondary_text(
        GTK_MESSAGE_DIALOG(q), "%s",
        _("Skip: they stay as they are now and only the missing people are added. "
          "Overwrite: everything about them (avatar, points, dates, first live...) "
          "becomes what the file says."));
    gtk_dialog_add_buttons(GTK_DIALOG(q), GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL, _("_Skip"),
                           IMPORT_SKIP, _("_Overwrite"), IMPORT_OVERWRITE, NULL);
    gtk_dialog_set_default_response(GTK_DIALOG(q), IMPORT_SKIP);
    int choice = gtk_dialog_run(GTK_DIALOG(q));
    gtk_widget_destroy(q);
    if (choice != IMPORT_SKIP && choice != IMPORT_OVERWRITE) {
        g_free(file);
        return;
    }
    bool overwrite = choice == IMPORT_OVERWRITE;

    int added = 0, replaced = 0, skipped = 0;
    bool done = false;
    cJSON *req = cJSON_CreateObject();
    cJSON_AddStringToObject(req, "type", "import_users");
    cJSON_AddStringToObject(req, "path", file);
    cJSON_AddBoolToObject(req, "overwrite", overwrite);
    char *line = cJSON_PrintUnformatted(req);
    cJSON_Delete(req);
    char reply[512];
    if (line && editor_request(e, line, reply, sizeof reply)) {
        cJSON *rep = cJSON_Parse(reply);
        if (cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(rep, "ok"))) {
            added = (int)cJSON_GetNumberValue(cJSON_GetObjectItemCaseSensitive(rep, "added"));
            replaced = (int)cJSON_GetNumberValue(cJSON_GetObjectItemCaseSensitive(rep, "replaced"));
            skipped = (int)cJSON_GetNumberValue(cJSON_GetObjectItemCaseSensitive(rep, "skipped"));
            done = true;
        } else {
            const char *err =
                cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(rep, "error"));
            status(e, _("kikarinhas refused: %s"), err ? err : reply);
        }
        cJSON_Delete(rep);
        if (!done) {
            free(line);
            g_free(file);
            return;
        }
    } else {
        char path[KK_PATH_MAX];
        users_path(e, path, sizeof path);
        kk_users *u = path[0] ? kk_users_open(path) : NULL;
        kk_users_import_stats st;
        if (u && kk_users_import(u, file, overwrite, &st) == 0 && kk_users_save(u) == 0) {
            added = st.added;
            replaced = st.replaced;
            skipped = st.skipped;
            done = true;
        }
        kk_users_free(u);
    }
    free(line);
    if (done) {
        refresh(e);
        status(e, _("Imported: %d new, %d overwritten, %d skipped."), added, replaced, skipped);
    } else {
        status(e, _("Could not import %s."), file);
    }
    g_free(file);
}

static gboolean on_key(GtkWidget *w, GdkEventKey *ev, gpointer ud)
{
    if (ev->keyval == GDK_Delete || ev->keyval == GDK_KP_Delete) {
        on_remove(w, ud);
        return TRUE;
    }
    return FALSE;
}

/* ---- the page ------------------------------------------------------------ */

GtkWidget *audience_page(editor *e, const kk_config *cfg)
{
    (void)cfg;
    audience_tab *t = e->audience = g_new0(audience_tab, 1);
    GtkWidget *box = gtk_vbox_new(FALSE, 6);
    gtk_container_set_border_width(GTK_CONTAINER(box), 12);
    g_signal_connect(box, "map", G_CALLBACK(on_map), e);

    GtkWidget *top = gtk_hbox_new(FALSE, 6);
    gtk_box_pack_start(GTK_BOX(top), gtk_label_new(_("Search")), FALSE, FALSE, 0);
    t->search = gtk_entry_new();
    g_signal_connect(t->search, "changed", G_CALLBACK(on_search), e);
    gtk_box_pack_start(GTK_BOX(top), t->search, TRUE, TRUE, 0);
    t->count = gtk_label_new(NULL);
    gtk_box_pack_start(GTK_BOX(top), t->count, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(top), gtk_label_new(_("First live")), FALSE, FALSE, 0);
    t->live_search = gtk_entry_new();
    gtk_entry_set_width_chars(GTK_ENTRY(t->live_search), 18);
    gtk_widget_set_tooltip_text(t->live_search,
                                _("Only people whose first live has this in its title or link"));
    g_signal_connect(t->live_search, "changed", G_CALLBACK(on_search), e);
    gtk_box_pack_start(GTK_BOX(top), t->live_search, FALSE, FALSE, 0);
    GtkWidget *reload = icon_button(GTK_STOCK_REFRESH, _("_Refresh"));
    g_signal_connect(reload, "clicked", G_CALLBACK(on_refresh), e);
    gtk_box_pack_start(GTK_BOX(top), reload, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), top, FALSE, FALSE, 0);

    t->store = gtk_list_store_new(N_ACOLS, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING,
                                  G_TYPE_INT64, G_TYPE_STRING, G_TYPE_INT64,
                                  G_TYPE_STRING, G_TYPE_STRING, G_TYPE_INT, G_TYPE_STRING,
                                  G_TYPE_BOOLEAN, G_TYPE_LONG, G_TYPE_STRING, G_TYPE_STRING,
                                  G_TYPE_STRING, G_TYPE_STRING);
    t->filter = gtk_tree_model_filter_new(GTK_TREE_MODEL(t->store), NULL);
    gtk_tree_model_filter_set_visible_func(GTK_TREE_MODEL_FILTER(t->filter), visible, e,
                                           NULL);
    t->sort = gtk_tree_model_sort_new_with_model(t->filter);
    gtk_tree_sortable_set_sort_column_id(GTK_TREE_SORTABLE(t->sort), A_LAST,
                                         GTK_SORT_DESCENDING);

    t->tree = gtk_tree_view_new_with_model(t->sort);
    gtk_tree_selection_set_mode(gtk_tree_view_get_selection(GTK_TREE_VIEW(t->tree)),
                                GTK_SELECTION_MULTIPLE);
    g_signal_connect(t->tree, "key-press-event", G_CALLBACK(on_key), e);
    gtk_tree_view_set_rules_hint(GTK_TREE_VIEW(t->tree), TRUE);
    gtk_tree_view_set_search_entry(GTK_TREE_VIEW(t->tree), GTK_ENTRY(t->search));

    static const struct {
        const char *title;
        int text, sort;
    } cols[] = {
        {N_("Name"), A_NAME, A_NAME},
        {N_("Platform"), A_PLATFORM, A_PLATFORM},
        {N_("First seen"), A_FIRST_TEXT, A_FIRST},
        {N_("Latest"), A_LAST_TEXT, A_LAST},
        {N_("Level"), A_LEVEL_TEXT, A_LEVEL},
    };
    for (size_t i = 0; i < sizeof cols / sizeof cols[0]; i++) {
        GtkCellRenderer *r = gtk_cell_renderer_text_new();
        GtkTreeViewColumn *c =
            gtk_tree_view_column_new_with_attributes(_(cols[i].title), r, "text", cols[i].text, NULL);
        if (cols[i].text == A_NAME) {
            g_object_set(r, "ellipsize", PANGO_ELLIPSIZE_END, NULL);
            gtk_tree_view_column_set_expand(c, TRUE);
        }
        gtk_tree_view_column_set_resizable(c, TRUE);
        gtk_tree_view_column_set_sort_column_id(c, cols[i].sort);
        gtk_tree_view_append_column(GTK_TREE_VIEW(t->tree), c);
    }
    GtkCellRenderer *tr = gtk_cell_renderer_toggle_new();
    g_signal_connect(tr, "toggled", G_CALLBACK(on_trusted_toggled), e);
    GtkTreeViewColumn *tc =
        gtk_tree_view_column_new_with_attributes(_("Trusted"), tr, "active", A_TRUSTED, NULL);
    gtk_tree_view_column_set_sort_column_id(tc, A_TRUSTED);
    gtk_tree_view_append_column(GTK_TREE_VIEW(t->tree), tc);

    GtkCellRenderer *pr = gtk_cell_renderer_text_new();
    g_object_set(pr, "editable", TRUE, "xalign", 1.0, NULL);
    g_signal_connect(pr, "edited", G_CALLBACK(on_points_edited), e);
    GtkTreeViewColumn *pc = gtk_tree_view_column_new_with_attributes(_("Points"), pr, NULL, NULL);
    gtk_tree_view_column_set_cell_data_func(pc, pr, points_text, NULL, NULL);
    gtk_tree_view_column_set_sort_column_id(pc, A_POINTS);
    gtk_tree_view_append_column(GTK_TREE_VIEW(t->tree), pc);
    t->points_col = pc;
    g_signal_connect(t->tree, "row-activated", G_CALLBACK(on_row_activated), e);

    GtkCellRenderer *ar = gtk_cell_renderer_text_new();
    GtkTreeViewColumn *c =
        gtk_tree_view_column_new_with_attributes(_("Avatar"), ar, "text", A_AVATAR, NULL);
    gtk_tree_view_column_set_resizable(c, TRUE);
    gtk_tree_view_column_set_sort_column_id(c, A_AVATAR);
    gtk_tree_view_column_set_min_width(c, 160);
    gtk_tree_view_append_column(GTK_TREE_VIEW(t->tree), c);

    GtkCellRenderer *lr = gtk_cell_renderer_text_new();
    g_object_set(lr, "ellipsize", PANGO_ELLIPSIZE_END, NULL);
    GtkTreeViewColumn *lc =
        gtk_tree_view_column_new_with_attributes(_("First live"), lr, "text", A_LIVE_TEXT, NULL);
    gtk_tree_view_column_set_resizable(lc, TRUE);
    gtk_tree_view_column_set_sort_column_id(lc, A_LIVE_TEXT);
    gtk_tree_view_column_set_min_width(lc, 160);
    gtk_tree_view_column_set_max_width(lc, 320);
    gtk_tree_view_append_column(GTK_TREE_VIEW(t->tree), lc);
    gtk_tree_view_set_tooltip_column(GTK_TREE_VIEW(t->tree), A_LIVE_TIP);

    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_AUTOMATIC,
                                   GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(scroll), GTK_SHADOW_IN);
    gtk_container_add(GTK_CONTAINER(scroll), t->tree);
    gtk_box_pack_start(GTK_BOX(box), scroll, TRUE, TRUE, 0);

    GtkWidget *bar = gtk_hbox_new(FALSE, 6);
    GtkWidget *all = gtk_button_new_with_mnemonic(_("Select _all"));
    gtk_widget_set_tooltip_text(all, _("Selects everyone shown (the search and the filter apply). "
                                       "Ctrl+A does the same; Ctrl+click and Shift+click pick "
                                       "some."));
    g_signal_connect(all, "clicked", G_CALLBACK(on_select_all), e);
    gtk_box_pack_start(GTK_BOX(bar), all, FALSE, FALSE, 0);
    GtkWidget *rm = icon_button(GTK_STOCK_REMOVE, _("Re_move selected"));
    gtk_widget_set_tooltip_text(rm, _("Removes the selected people from the list (Delete key)."));
    g_signal_connect(rm, "clicked", G_CALLBACK(on_remove), e);
    gtk_box_pack_start(GTK_BOX(bar), rm, FALSE, FALSE, 0);
    GtkWidget *imp = gtk_button_new_with_mnemonic(_("_Import..."));
    gtk_widget_set_tooltip_text(imp, _("Adds the people of a list saved with Export."));
    g_signal_connect(imp, "clicked", G_CALLBACK(on_import), e);
    gtk_box_pack_end(GTK_BOX(bar), imp, FALSE, FALSE, 0);
    GtkWidget *exp = gtk_button_new_with_mnemonic(_("E_xport..."));
    gtk_widget_set_tooltip_text(exp, _("Saves the whole list, with everything about each person, "
                                       "to a file: a backup in case you remove the wrong ones."));
    g_signal_connect(exp, "clicked", G_CALLBACK(on_export), e);
    gtk_box_pack_end(GTK_BOX(bar), exp, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), bar, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box),
                       hint_label(_("Double-click someone to pick their avatar by its picture "
                                    "(with a search box). With kikarinhas running, the change "
                                    "takes effect at once. Empty avatar: a random one, always "
                                    "the same. Level: what the chat badges said on the latest "
                                    "message. Trusted: ticked by hand, for commands limited to "
                                    "trusted people and up. Points: double-click to change the "
                                    "balance. First live: the first live they were seen in (link and, when "
                                    "known, title; hover for both); the field next to the search "
                                    "filters by it. Export saves the whole list, with everything about "
                                    "each person, to a file; Import brings it back, skipping or "
                                    "overwriting whoever is already in the list. People from Stream "
                                    "Avatars without a name or dates: run kikarinhas --import-sa-users once.")),
                       FALSE, FALSE, 0);

    return box;
}

void audience_free(editor *e)
{
    audience_tab *t = e->audience;
    if (!t)
        return;
    g_object_unref(t->sort);
    g_object_unref(t->filter);
    g_object_unref(t->store);
    g_free(t);
    e->audience = NULL;
}
