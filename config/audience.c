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
    N_ACOLS,
};

/* One person's level, by kk_role (ROLE_LABELS are plurals, for commands). */
static const char *const LEVEL_LABELS[KK_ROLE_COUNT] = {
    N_("common"), N_("member"), N_("trusted person"), N_("moderator"), N_("owner"),
};

struct audience_tab {
    GtkListStore *store;
    GtkTreeModel *filter, *sort;
    GtkWidget *tree, *search, *count;
    GtkListStore *avatars; /* avatar keys, for the combo */
    bool avatars_loaded;
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
        GtkTreeIter it;
        gtk_list_store_append(t->store, &it);
        gtk_list_store_set(t->store, &it, A_KEY, key, A_NAME,
                           name ? name : (id ? id + 1 : key), A_PLATFORM,
                           platform_label(key, plat, sizeof plat), A_FIRST,
                           (gint64)first, A_FIRST_TEXT, first_s, A_LAST, (gint64)last,
                           A_LAST_TEXT, last_s, A_AVATAR, avatar ? avatar : "", A_LEVEL,
                           (int)role, A_LEVEL_TEXT, _(LEVEL_LABELS[role]), A_TRUSTED,
                           kk_users_trusted(u, key), -1);
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
    if (!q[0])
        return TRUE;
    char *needle = g_utf8_casefold(q, -1);
    gchar *name, *key, *avatar;
    gtk_tree_model_get(m, it, A_NAME, &name, A_KEY, &key, A_AVATAR, &avatar, -1);
    bool yes = has(name, needle) || has(key, needle) || has(avatar, needle);
    g_free(name);
    g_free(key);
    g_free(avatar);
    g_free(needle);
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

static void load_avatars(editor *e)
{
    audience_tab *t = e->audience;
    if (t->avatars_loaded)
        return;
    t->avatars_loaded = true;
    const kk_sa_library *lib = editor_library(e);
    for (int i = 0; lib && i < lib->count; i++) {
        if (!lib->avatars[i].image)
            continue;
        GtkTreeIter it;
        gtk_list_store_append(t->avatars, &it);
        gtk_list_store_set(t->avatars, &it, 0, lib->avatars[i].key, -1);
    }
}

/* First time the tab is shown: read the list and the avatars. */
static void on_map(GtkWidget *w, gpointer ud)
{
    (void)w;
    editor *e = ud;
    if (e->audience->avatars_loaded)
        return;
    refresh(e);
    load_avatars(e);
}

/* Writes the file directly: only when no kikarinhas is running. avatar
 * NULL keeps it; trusted < 0 keeps it. */
static bool write_directly(editor *e, const char *key, const char *avatar, int trusted)
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
    bool ok = kk_users_save(u) == 0;
    kk_users_free(u);
    return ok;
}

typedef enum { SENT_OK, SENT_REFUSED, NOT_RUNNING } send_result;

/* Sends a set_user request (made by the caller, freed here) to the running
 * kikarinhas. On SENT_REFUSED, the status line already says why. */
static send_result send_user(editor *e, cJSON *req)
{
    cJSON_AddStringToObject(req, "type", "set_user");
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
    if (sr == SENT_OK || (sr == NOT_RUNNING && write_directly(e, key, NULL, on))) {
        gtk_list_store_set(t->store, &it, A_TRUSTED, on, -1);
        status(e, on ? _("%s is now trusted.") : _("%s is no longer trusted."), name);
    } else if (sr == NOT_RUNNING) {
        status(e, _("Could not write the people file."));
    }
    g_free(key);
    g_free(name);
}

/* The list's spelling of name (any case), or NULL. Without a list (no
 * avatars found), any name is taken: kikarinhas checks it. */
static gchar *known_avatar(audience_tab *t, const char *name)
{
    GtkTreeModel *m = GTK_TREE_MODEL(t->avatars);
    GtkTreeIter it;
    if (!gtk_tree_model_get_iter_first(m, &it))
        return name[0] ? g_strdup(name) : NULL;
    do {
        gchar *k;
        gtk_tree_model_get(m, &it, 0, &k, -1);
        if (g_ascii_strcasecmp(k, name) == 0)
            return k;
        g_free(k);
    } while (gtk_tree_model_iter_next(m, &it));
    return NULL;
}

static void on_avatar_edited(GtkCellRendererText *r, gchar *path, gchar *text, gpointer ud)
{
    (void)r;
    editor *e = ud;
    audience_tab *t = e->audience;
    GtkTreeIter it;
    if (!store_iter(t, path, &it) || !text[0])
        return;
    gchar *key, *name, *old;
    gtk_tree_model_get(GTK_TREE_MODEL(t->store), &it, A_KEY, &key, A_NAME, &name,
                       A_AVATAR, &old, -1);
    /* Typed names must be one of the list (as written there). */
    gchar *known = known_avatar(t, g_strstrip(text));
    if (!known) {
        status(e, _("There is no avatar \"%s\" in the avatar folders."), text);
        goto out;
    }
    text = known;
    if (strcmp(old, text) == 0)
        goto out;

    cJSON *req = cJSON_CreateObject();
    cJSON_AddStringToObject(req, "user", key);
    cJSON_AddStringToObject(req, "avatar", text);
    send_result sr = send_user(e, req);
    if (sr == SENT_OK) {
        gtk_list_store_set(t->store, &it, A_AVATAR, text, -1);
        status(e, _("%s is now %s (already in effect in the running kikarinhas)."), name, text);
    } else if (sr == SENT_REFUSED) {
        /* the status line says why */
    } else if (write_directly(e, key, text, -1)) {
        gtk_list_store_set(t->store, &it, A_AVATAR, text, -1);
        status(e, _("%s is now %s (saved to the file; takes effect next time)."), name, text);
    } else {
        status(e, _("Could not write the people file."));
    }
out:
    g_free(known);
    g_free(key);
    g_free(name);
    g_free(old);
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
    GtkWidget *reload = icon_button(GTK_STOCK_REFRESH, _("_Refresh"));
    g_signal_connect(reload, "clicked", G_CALLBACK(on_refresh), e);
    gtk_box_pack_start(GTK_BOX(top), reload, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), top, FALSE, FALSE, 0);

    t->store = gtk_list_store_new(N_ACOLS, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING,
                                  G_TYPE_INT64, G_TYPE_STRING, G_TYPE_INT64,
                                  G_TYPE_STRING, G_TYPE_STRING, G_TYPE_INT, G_TYPE_STRING,
                                  G_TYPE_BOOLEAN);
    t->filter = gtk_tree_model_filter_new(GTK_TREE_MODEL(t->store), NULL);
    gtk_tree_model_filter_set_visible_func(GTK_TREE_MODEL_FILTER(t->filter), visible, e,
                                           NULL);
    t->sort = gtk_tree_model_sort_new_with_model(t->filter);
    gtk_tree_sortable_set_sort_column_id(GTK_TREE_SORTABLE(t->sort), A_LAST,
                                         GTK_SORT_DESCENDING);
    t->avatars = gtk_list_store_new(1, G_TYPE_STRING);

    t->tree = gtk_tree_view_new_with_model(t->sort);
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

    GtkCellRenderer *combo = gtk_cell_renderer_combo_new();
    g_object_set(combo, "model", t->avatars, "text-column", 0, "has-entry", TRUE,
                 "editable", TRUE, NULL);
    g_signal_connect(combo, "edited", G_CALLBACK(on_avatar_edited), e);
    GtkTreeViewColumn *c =
        gtk_tree_view_column_new_with_attributes(_("Avatar"), combo, "text", A_AVATAR, NULL);
    gtk_tree_view_column_set_resizable(c, TRUE);
    gtk_tree_view_column_set_sort_column_id(c, A_AVATAR);
    gtk_tree_view_column_set_min_width(c, 160);
    gtk_tree_view_append_column(GTK_TREE_VIEW(t->tree), c);

    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_AUTOMATIC,
                                   GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(scroll), GTK_SHADOW_IN);
    gtk_container_add(GTK_CONTAINER(scroll), t->tree);
    gtk_box_pack_start(GTK_BOX(box), scroll, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(box),
                       hint_label(_("Double-click the avatar to change it (you can type the "
                                    "name). With kikarinhas running, the change takes effect at "
                                    "once. Empty avatar: a random one, always the same. Level: "
                                    "what the chat badges said on the latest message. Trusted: "
                                    "ticked by hand, for commands limited to trusted people and "
                                    "up. People from Stream Avatars without a name or dates: run "
                                    "kikarinhas --import-sa-users once.")),
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
    g_object_unref(t->avatars);
    g_free(t);
    e->audience = NULL;
}
