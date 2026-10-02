/* SPDX-License-Identifier: GPL-3.0-or-later */
/* kikarinhas-config: the "Sons" tab, the sound board's list.
 *
 * Sounds play here through the same mixer as in kikarinhas (kk_audio), with
 * the same volumes, so what is heard while setting them up is what the
 * stream will hear. Files are decoded once, in the background (one per idle
 * moment), to show their length and to level them. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "audio.h"
#include "editor.h"
#include "i18n.h"
#include "sa.h"
#include "sample.h"

enum {
    S_NAME,
    S_FILE,
    S_ALIASES,
    S_VOLUME, /* int, percent */
    S_LENGTH, /* "1,5 s", "?" or the error */
    N_SCOLS,
};

struct sounds_tab {
    GtkListStore *store;
    GtkWidget *tree;
    GtkWidget *enabled, *volume, *commands, *voices, *device;
    char *loaded[512]; /* sound sections in the file when loaded/saved */
    int n_loaded;

    kk_audio *audio;
    char audio_device[256];
    guint pump_id, scan_id;
    GHashTable *cache; /* resolved path -> kk_sample* (NULL: failed) */
    char last_folder[KK_PATH_MAX];
};

static double now_s(void)
{
    return (double)g_get_monotonic_time() / 1e6;
}

/* Relative files are from the config file's folder, as in kikarinhas. */
static void resolve(editor *e, const char *file, char *out, size_t size)
{
    const char *slash = strrchr(e->path, '/');
    if (file[0] == '~')
        kk_pathf(out, size, "%s%s", g_get_home_dir(), file + 1);
    else if (file[0] != '/' && slash)
        kk_pathf(out, size, "%.*s/%s", (int)(slash - e->path), e->path, file);
    else
        kk_pathf(out, size, "%s", file);
}

/* The decoded file, from the cache or now. NULL (with err) if unplayable. */
static const kk_sample *sample_of(editor *e, const char *file, char *err, size_t size)
{
    sounds_tab *t = e->sounds;
    char path[KK_PATH_MAX];
    resolve(e, file, path, sizeof path);
    gpointer cached;
    if (g_hash_table_lookup_extended(t->cache, path, NULL, &cached)) {
        if (!cached)
            snprintf(err, size, _("can't play"));
        return cached;
    }
    kk_sample *s = g_new0(kk_sample, 1);
    if (kk_sample_load(s, path, err, size) < 0) {
        g_free(s);
        s = NULL;
    }
    g_hash_table_insert(t->cache, g_strdup(path), s);
    return s;
}

static void free_sample(gpointer p)
{
    if (p) {
        kk_sample_free(p);
        g_free(p);
    }
}

static void show_length(editor *e, GtkTreeIter *it)
{
    gchar *file;
    gtk_tree_model_get(GTK_TREE_MODEL(e->sounds->store), it, S_FILE, &file, -1);
    char err[256], len[300];
    const kk_sample *s = sample_of(e, file, err, sizeof err);
    if (s) {
        snprintf(len, sizeof len, "%.1f s", kk_sample_seconds(s));
        char *dot = strchr(len, '.'); /* the C locale is on for the file */
        if (dot)
            *dot = ',';
    } else
        snprintf(len, sizeof len, "%s", err);
    gtk_list_store_set(e->sounds->store, it, S_LENGTH, len, -1);
    g_free(file);
}

/* Decodes one file per idle moment, to fill the lengths without freezing. */
static gboolean scan_step(gpointer ud)
{
    editor *e = ud;
    GtkTreeModel *m = GTK_TREE_MODEL(e->sounds->store);
    GtkTreeIter it;
    for (bool more = gtk_tree_model_get_iter_first(m, &it); more;
         more = gtk_tree_model_iter_next(m, &it)) {
        gchar *len;
        gtk_tree_model_get(m, &it, S_LENGTH, &len, -1);
        bool todo = !len || !len[0];
        g_free(len);
        if (todo) {
            show_length(e, &it);
            return TRUE;
        }
    }
    e->sounds->scan_id = 0;
    return FALSE;
}

static void scan(editor *e)
{
    if (!e->sounds->scan_id)
        e->sounds->scan_id = g_idle_add(scan_step, e);
}

/* ---- playing -------------------------------------------------------------- */

static gboolean pump(gpointer ud)
{
    sounds_tab *t = ud;
    kk_audio_pump(t->audio, NULL, 0, now_s());
    if (kk_audio_active(t->audio))
        return TRUE;
    t->pump_id = 0;
    return FALSE;
}

static kk_audio *audio_for(editor *e)
{
    sounds_tab *t = e->sounds;
    const char *dev = text_of(t->device);
    if (!dev[0])
        dev = "default";
    if (!t->audio || strcmp(dev, t->audio_device) != 0) {
        kk_audio_free(t->audio);
        t->audio = kk_audio_new(dev, 8);
        snprintf(t->audio_device, sizeof t->audio_device, "%s", dev);
    }
    if (t->audio)
        kk_audio_set_volume(t->audio, gtk_range_get_value(GTK_RANGE(t->volume)) / 100.0);
    return t->audio;
}

static void play_row(editor *e, GtkTreeIter *it)
{
    sounds_tab *t = e->sounds;
    gchar *name, *file;
    int volume;
    gtk_tree_model_get(GTK_TREE_MODEL(t->store), it, S_NAME, &name, S_FILE, &file,
                       S_VOLUME, &volume, -1);
    char err[256];
    const kk_sample *s = sample_of(e, file, err, sizeof err);
    show_length(e, it);
    kk_audio *a = audio_for(e);
    if (!s)
        status(e, "%s: %s", file, err);
    else if (!a || kk_audio_play(a, s, volume / 100.0, now_s()) < 0)
        status(e, _("Could not open the audio device \"%s\"."), t->audio_device);
    else {
        status(e, _("Playing !%s (%d%%)."), name, volume);
        if (!t->pump_id)
            t->pump_id = g_timeout_add(20, pump, t);
        pump(t);
    }
    g_free(name);
    g_free(file);
}

/* Selected rows, as row references (they survive removals). */
static GList *selected_rows(editor *e)
{
    GtkTreeSelection *sel = gtk_tree_view_get_selection(GTK_TREE_VIEW(e->sounds->tree));
    GtkTreeModel *m;
    GList *paths = gtk_tree_selection_get_selected_rows(sel, &m), *refs = NULL;
    for (GList *p = paths; p; p = p->next)
        refs = g_list_append(refs, gtk_tree_row_reference_new(m, p->data));
    g_list_free_full(paths, (GDestroyNotify)gtk_tree_path_free);
    return refs;
}

static bool ref_iter(editor *e, GtkTreeRowReference *ref, GtkTreeIter *it)
{
    GtkTreePath *p = gtk_tree_row_reference_get_path(ref);
    bool ok = p && gtk_tree_model_get_iter(GTK_TREE_MODEL(e->sounds->store), it, p);
    gtk_tree_path_free(p);
    return ok;
}

static void on_play(GtkButton *b, gpointer ud)
{
    (void)b;
    editor *e = ud;
    GList *rows = selected_rows(e);
    GtkTreeIter it;
    if (!rows)
        status(e, _("Pick a sound in the list."));
    else if (ref_iter(e, rows->data, &it))
        play_row(e, &it);
    g_list_free_full(rows, (GDestroyNotify)gtk_tree_row_reference_free);
}

static void on_row_activated(GtkTreeView *v, GtkTreePath *p, GtkTreeViewColumn *c,
                             gpointer ud)
{
    (void)v;
    (void)c;
    editor *e = ud;
    GtkTreeIter it;
    if (gtk_tree_model_get_iter(GTK_TREE_MODEL(e->sounds->store), &it, p))
        play_row(e, &it);
}

static void on_stop(GtkButton *b, gpointer ud)
{
    (void)b;
    editor *e = ud;
    if (e->sounds->audio)
        kk_audio_stop_all(e->sounds->audio);
}

static void on_master_changed(GtkRange *r, gpointer ud)
{
    sounds_tab *t = ud;
    if (t->audio)
        kk_audio_set_volume(t->audio, gtk_range_get_value(r) / 100.0);
}

/* ---- levelling ----------------------------------------------------------- */

static int level_rows(editor *e, GList *rows, int *failed)
{
    int n = 0;
    *failed = 0;
    for (GList *r = rows; r; r = r->next) {
        GtkTreeIter it;
        if (!ref_iter(e, r->data, &it))
            continue;
        gchar *file;
        gtk_tree_model_get(GTK_TREE_MODEL(e->sounds->store), &it, S_FILE, &file, -1);
        char err[256];
        const kk_sample *s = sample_of(e, file, err, sizeof err);
        g_free(file);
        show_length(e, &it);
        if (!s) {
            (*failed)++;
            continue;
        }
        gtk_list_store_set(e->sounds->store, &it, S_VOLUME, kk_sample_level(s), -1);
        n++;
    }
    return n;
}

static void level(editor *e, GList *rows)
{
    GdkCursor *busy = gdk_cursor_new(GDK_WATCH);
    gdk_window_set_cursor(gtk_widget_get_window(e->window), busy);
    while (gtk_events_pending())
        gtk_main_iteration();
    int failed, n = level_rows(e, rows, &failed);
    gdk_window_set_cursor(gtk_widget_get_window(e->window), NULL);
    gdk_cursor_unref(busy);
    if (failed)
        status(e, ngettext("%d sound leveled; %d cannot be played (see the Length column).",
                           "%d sounds leveled; %d cannot be played (see the Length column).", n),
               n, failed);
    else
        status(e, ngettext("%d sound leveled. Listen and adjust if needed; then Save.",
                           "%d sounds leveled. Listen and adjust if needed; then Save.", n),
               n);
}

static void on_level_selected(GtkButton *b, gpointer ud)
{
    (void)b;
    editor *e = ud;
    GList *rows = selected_rows(e);
    if (!rows)
        status(e, _("Pick the sounds in the list (Ctrl or Shift for several)."));
    else
        level(e, rows);
    g_list_free_full(rows, (GDestroyNotify)gtk_tree_row_reference_free);
}

static gboolean collect_ref(GtkTreeModel *m, GtkTreePath *p, GtkTreeIter *it, gpointer ud)
{
    (void)it;
    GList **rows = ud;
    *rows = g_list_append(*rows, gtk_tree_row_reference_new(m, p));
    return FALSE;
}

static void on_level_all(GtkButton *b, gpointer ud)
{
    (void)b;
    editor *e = ud;
    GList *rows = NULL;
    gtk_tree_model_foreach(GTK_TREE_MODEL(e->sounds->store), collect_ref, &rows);
    level(e, rows);
    g_list_free_full(rows, (GDestroyNotify)gtk_tree_row_reference_free);
}

/* ---- the list ------------------------------------------------------------ */

static bool sound_taken(editor *e, const char *name, GtkTreeIter *except)
{
    GtkTreeModel *m = GTK_TREE_MODEL(e->sounds->store);
    GtkTreeIter it;
    for (bool more = gtk_tree_model_get_iter_first(m, &it); more;
         more = gtk_tree_model_iter_next(m, &it)) {
        if (except && it.user_data == except->user_data)
            continue;
        gchar *n;
        gtk_tree_model_get(m, &it, S_NAME, &n, -1);
        bool same = g_ascii_strcasecmp(n, name) == 0;
        g_free(n);
        if (same)
            return true;
    }
    return false;
}

static void add_row(editor *e, const char *name, const char *file,
                    const char *aliases, int volume)
{
    GtkTreeIter it;
    gtk_list_store_append(e->sounds->store, &it);
    gtk_list_store_set(e->sounds->store, &it, S_NAME, name, S_FILE, file, S_ALIASES,
                       aliases, S_VOLUME, volume, S_LENGTH, "", -1);
}

/* A free name from base: "buzina", "buzina2"... */
static void unique_name(editor *e, const char *base, char *out, size_t size)
{
    snprintf(out, size, "%s", base);
    for (int i = 2; sound_taken(e, out, NULL); i++)
        snprintf(out, size, "%.50s%d", base, i);
}

static bool iter_at(editor *e, const gchar *path, GtkTreeIter *it)
{
    GtkTreePath *p = gtk_tree_path_new_from_string(path);
    bool ok = gtk_tree_model_get_iter(GTK_TREE_MODEL(e->sounds->store), it, p);
    gtk_tree_path_free(p);
    return ok;
}

static void on_name_edited(GtkCellRendererText *r, gchar *path, gchar *text, gpointer ud)
{
    (void)r;
    editor *e = ud;
    GtkTreeIter it;
    char name[64];
    const char *s = text[0] == '!' ? text + 1 : text;
    if (!iter_at(e, path, &it))
        return;
    if (!s[0] || strlen(s) >= sizeof name || strpbrk(s, " \t!,[]=")) {
        status(e, _("Invalid sound name: a single word, no spaces."));
        return;
    }
    for (size_t i = 0; i <= strlen(s); i++)
        name[i] = (char)g_ascii_tolower(s[i]);
    if (sound_taken(e, name, &it)) {
        status(e, _("A sound \"%s\" already exists."), name);
        return;
    }
    gtk_list_store_set(e->sounds->store, &it, S_NAME, name, -1);
}

static void on_aliases_edited(GtkCellRendererText *r, gchar *path, gchar *text, gpointer ud)
{
    (void)r;
    editor *e = ud;
    GtkTreeIter it;
    if (iter_at(e, path, &it))
        gtk_list_store_set(e->sounds->store, &it, S_ALIASES, g_strstrip(text), -1);
}

static void on_volume_edited(GtkCellRendererText *r, gchar *path, gchar *text, gpointer ud)
{
    (void)r;
    editor *e = ud;
    GtkTreeIter it;
    long v;
    g_strstrip(text);
    size_t n = strlen(text);
    if (n && text[n - 1] == '%')
        text[n - 1] = '\0';
    if (!kk_parse_long(text, 0, 400, &v)) {
        status(e, _("Invalid volume: 0 to 400%%."));
        return;
    }
    if (iter_at(e, path, &it))
        gtk_list_store_set(e->sounds->store, &it, S_VOLUME, (int)v, -1);
}

static void volume_text(GtkTreeViewColumn *c, GtkCellRenderer *r, GtkTreeModel *m,
                        GtkTreeIter *it, gpointer ud)
{
    (void)c;
    (void)ud;
    int v;
    gtk_tree_model_get(m, it, S_VOLUME, &v, -1);
    char s[16];
    snprintf(s, sizeof s, "%d%%", v);
    g_object_set(r, "text", s, NULL);
}

static void on_add(GtkButton *b, gpointer ud)
{
    (void)b;
    editor *e = ud;
    sounds_tab *t = e->sounds;
    GtkWidget *d = gtk_file_chooser_dialog_new(
        _("Add sounds"), GTK_WINDOW(e->window), GTK_FILE_CHOOSER_ACTION_OPEN,
        _("_Cancel"), GTK_RESPONSE_CANCEL, _("_Add"), GTK_RESPONSE_ACCEPT, NULL);
    gtk_file_chooser_set_select_multiple(GTK_FILE_CHOOSER(d), TRUE);
    GtkFileFilter *f = gtk_file_filter_new();
    gtk_file_filter_set_name(f, _("Sounds (wav, ogg, mp3)"));
    static const char *const pats[] = {"*.wav", "*.WAV", "*.ogg", "*.OGG",
                                       "*.oga", "*.mp3", "*.MP3"};
    for (size_t i = 0; i < sizeof pats / sizeof pats[0]; i++)
        gtk_file_filter_add_pattern(f, pats[i]);
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(d), f);
    char sa[KK_PATH_MAX], sa_sounds[KK_PATH_MAX];
    if (t->last_folder[0])
        gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(d), t->last_folder);
    else if (editor_sa_dir(e, sa, sizeof sa) &&
             kk_pathf(sa_sounds, sizeof sa_sounds, "%s/sounds", sa))
        gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(d), sa_sounds);

    if (gtk_dialog_run(GTK_DIALOG(d)) == GTK_RESPONSE_ACCEPT) {
        GSList *files = gtk_file_chooser_get_filenames(GTK_FILE_CHOOSER(d));
        int n = 0;
        for (GSList *l = files; l; l = l->next) {
            char base[64], name[64];
            if (!kk_config_sound_name(base, sizeof base, l->data))
                continue;
            unique_name(e, base, name, sizeof name);
            add_row(e, name, l->data, "", 100);
            n++;
        }
        gchar *folder = gtk_file_chooser_get_current_folder(GTK_FILE_CHOOSER(d));
        if (folder)
            snprintf(t->last_folder, sizeof t->last_folder, "%s", folder);
        g_free(folder);
        g_slist_free_full(files, g_free);
        status(e, ngettext("%d sound added. The name is the chat command (!name); "
                           "double-click a cell to change it.",
                           "%d sounds added. The name is the chat command (!name); "
                           "double-click a cell to change it.", n),
               n);
        scan(e);
    }
    gtk_widget_destroy(d);
}

static void on_remove(GtkButton *b, gpointer ud)
{
    (void)b;
    editor *e = ud;
    GList *rows = selected_rows(e);
    int n = 0;
    for (GList *r = rows; r; r = r->next) {
        GtkTreeIter it;
        if (ref_iter(e, r->data, &it)) {
            gtk_list_store_remove(e->sounds->store, &it);
            n++;
        }
    }
    g_list_free_full(rows, (GDestroyNotify)gtk_tree_row_reference_free);
    if (n)
        status(e, ngettext("%d sound removed from the list (the file stays where it is).",
                           "%d sounds removed from the list (the files stay where they are).", n),
               n);
    else
        status(e, _("Pick the sounds in the list."));
}

typedef struct {
    editor *e;
    int added, known, missing;
} import_ctx;

static void on_sa_sound(void *ud, const kk_sa_sound *s)
{
    import_ctx *ic = ud;
    char name[64];
    if (!s->file) {
        ic->missing++;
        return;
    }
    if (!kk_config_sound_name(name, sizeof name, s->name) ||
        sound_taken(ic->e, name, NULL)) {
        ic->known++;
        return;
    }
    add_row(ic->e, name, s->file, "", s->volume);
    ic->added++;
}

static void on_import(GtkButton *b, gpointer ud)
{
    (void)b;
    editor *e = ud;
    char sa[KK_PATH_MAX];
    if (!editor_sa_dir(e, sa, sizeof sa)) {
        status(e, _("Could not find Stream Avatars; set its folder in the Avatars tab."));
        return;
    }
    import_ctx ic = {.e = e};
    if (kk_sa_read_sounds(sa, on_sa_sound, &ic) < 0) {
        status(e, _("Could not read the Stream Avatars sound list in %s."), sa);
        return;
    }
    status(e, _("%d sound(s) imported from Stream Avatars, with their volumes; "
                "%d were already in the list, %d without a file."),
           ic.added, ic.known, ic.missing);
    scan(e);
}

/* ---- the page ------------------------------------------------------------ */

static GtkWidget *button(const char *stock, const char *label, GCallback cb, editor *e)
{
    GtkWidget *b = icon_button(stock, label);
    g_signal_connect(b, "clicked", cb, e);
    return b;
}

static void remember_loaded(editor *e)
{
    sounds_tab *t = e->sounds;
    for (int i = 0; i < t->n_loaded; i++)
        g_free(t->loaded[i]);
    t->n_loaded = 0;
    GtkTreeModel *m = GTK_TREE_MODEL(t->store);
    GtkTreeIter it;
    for (bool more = gtk_tree_model_get_iter_first(m, &it); more && t->n_loaded < 512;
         more = gtk_tree_model_iter_next(m, &it))
        gtk_tree_model_get(m, &it, S_NAME, &t->loaded[t->n_loaded++], -1);
}

GtkWidget *sounds_page(editor *e, const kk_config *cfg)
{
    sounds_tab *t = e->sounds = g_new0(sounds_tab, 1);
    t->cache = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, free_sample);

    GtkWidget *box = gtk_vbox_new(FALSE, 6);
    gtk_container_set_border_width(GTK_CONTAINER(box), 12);

    GtkWidget *top = gtk_hbox_new(FALSE, 12);
    t->enabled = check(_("Sound board on"), cfg->sound_enabled);
    t->commands = check(_("Each sound is also a command (!name)"), cfg->sound_commands);
    gtk_box_pack_start(GTK_BOX(top), t->enabled, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(top), t->commands, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), top, FALSE, FALSE, 0);

    GtkWidget *mid = gtk_hbox_new(FALSE, 6);
    gtk_box_pack_start(GTK_BOX(mid), gtk_label_new(_("Overall volume")), FALSE, FALSE, 0);
    t->volume = gtk_hscale_new_with_range(0, 400, 5);
    gtk_scale_set_value_pos(GTK_SCALE(t->volume), GTK_POS_RIGHT);
    gtk_scale_set_digits(GTK_SCALE(t->volume), 0);
    gtk_range_set_value(GTK_RANGE(t->volume), cfg->sound_volume);
    gtk_widget_set_size_request(t->volume, 220, -1);
    g_signal_connect(t->volume, "value-changed", G_CALLBACK(on_master_changed), t);
    gtk_box_pack_start(GTK_BOX(mid), t->volume, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(mid), gtk_label_new("%"), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(mid), gtk_label_new(_("At the same time")), FALSE, FALSE, 6);
    t->voices = spin(1, 64, 1, 0);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(t->voices), cfg->sound_voices);
    gtk_box_pack_start(GTK_BOX(mid), t->voices, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(mid), gtk_label_new(_("ALSA output")), FALSE, FALSE, 6);
    t->device = entry(cfg->sound_device);
    gtk_entry_set_width_chars(GTK_ENTRY(t->device), 12);
    gtk_widget_set_tooltip_text(t->device, _("Empty: \"default\" (PipeWire or "
                                             "PulseAudio, if present)"));
    gtk_box_pack_start(GTK_BOX(mid), t->device, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), mid, FALSE, FALSE, 0);

    t->store = gtk_list_store_new(N_SCOLS, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING,
                                  G_TYPE_INT, G_TYPE_STRING);
    for (int i = 0; i < cfg->n_sounds; i++) {
        const kk_config_sound *s = &cfg->sounds[i];
        char aliases[512];
        join(aliases, sizeof aliases, s->aliases, s->n_aliases);
        add_row(e, s->name, s->file, aliases, s->volume);
    }
    remember_loaded(e);
    t->tree = gtk_tree_view_new_with_model(GTK_TREE_MODEL(t->store));
    g_object_unref(t->store);
    gtk_tree_view_set_rules_hint(GTK_TREE_VIEW(t->tree), TRUE);
    gtk_tree_selection_set_mode(gtk_tree_view_get_selection(GTK_TREE_VIEW(t->tree)),
                                GTK_SELECTION_MULTIPLE);
    gtk_tree_view_set_search_column(GTK_TREE_VIEW(t->tree), S_NAME);
    g_signal_connect(t->tree, "row-activated", G_CALLBACK(on_row_activated), e);

    static const struct {
        const char *title;
        int col;
        GCallback edited;
    } cols[] = {
        {N_("Command"), S_NAME, G_CALLBACK(on_name_edited)},
        {N_("Aliases"), S_ALIASES, G_CALLBACK(on_aliases_edited)},
        {N_("Volume"), S_VOLUME, G_CALLBACK(on_volume_edited)},
        {N_("Length"), S_LENGTH, NULL},
        {N_("File"), S_FILE, NULL},
    };
    for (size_t i = 0; i < sizeof cols / sizeof cols[0]; i++) {
        GtkCellRenderer *r = gtk_cell_renderer_text_new();
        GtkTreeViewColumn *c;
        if (cols[i].col == S_VOLUME) {
            c = gtk_tree_view_column_new_with_attributes(cols[i].title, r, NULL);
            gtk_tree_view_column_set_cell_data_func(c, r, volume_text, NULL, NULL);
            g_object_set(r, "xalign", 1.0, NULL);
        } else {
            c = gtk_tree_view_column_new_with_attributes(_(cols[i].title), r, "text",
                                                         cols[i].col, NULL);
        }
        if (cols[i].edited) {
            g_object_set(r, "editable", TRUE, NULL);
            g_signal_connect(r, "edited", cols[i].edited, e);
        }
        if (cols[i].col == S_FILE)
            g_object_set(r, "ellipsize", PANGO_ELLIPSIZE_START, NULL);
        gtk_tree_view_column_set_resizable(c, TRUE);
        gtk_tree_view_column_set_sort_column_id(c, cols[i].col);
        gtk_tree_view_column_set_expand(c, cols[i].col == S_FILE);
        gtk_tree_view_append_column(GTK_TREE_VIEW(t->tree), c);
    }

    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_AUTOMATIC,
                                   GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(scroll), GTK_SHADOW_IN);
    gtk_container_add(GTK_CONTAINER(scroll), t->tree);
    gtk_box_pack_start(GTK_BOX(box), scroll, TRUE, TRUE, 0);

    gtk_box_pack_start(GTK_BOX(box),
                       hint_label(_("In the chat: !sound NAME (or !NAME). Double-click a "
                                    "row to listen at its volume. Volume from 0 to 400%; "
                                    "\"Level\" measures each sound and sets the volume so "
                                    "they all sound alike, without clipping.")),
                       FALSE, FALSE, 0);

    GtkWidget *buttons = gtk_hbox_new(FALSE, 6);
    gtk_box_pack_start(GTK_BOX(buttons), button(GTK_STOCK_ADD, _("_Add…"), G_CALLBACK(on_add), e),
                       FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(buttons),
                       button(GTK_STOCK_REMOVE, _("_Remove"), G_CALLBACK(on_remove), e), FALSE,
                       FALSE, 0);
    gtk_box_pack_start(GTK_BOX(buttons),
                       button(GTK_STOCK_MEDIA_PLAY, _("_Play"), G_CALLBACK(on_play), e),
                       FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(buttons),
                       button(GTK_STOCK_MEDIA_STOP, _("_Stop"), G_CALLBACK(on_stop), e),
                       FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(buttons),
                       button(NULL, _("_Level selected"), G_CALLBACK(on_level_selected), e),
                       FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(buttons),
                       button(NULL, _("Level _all"), G_CALLBACK(on_level_all), e), FALSE,
                       FALSE, 0);
    gtk_box_pack_end(GTK_BOX(buttons),
                     button(NULL, _("_Import from Stream Avatars"), G_CALLBACK(on_import), e),
                     FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), buttons, FALSE, FALSE, 0);

    scan(e);
    return box;
}

void sounds_collect(editor *e)
{
    sounds_tab *t = e->sounds;
    put_bool(e, "soundboard", "enabled", t->enabled, true);
    put_bool(e, "soundboard", "commands", t->commands, true);
    put_int(e, "soundboard", "volume", (int)gtk_range_get_value(GTK_RANGE(t->volume)), 100);
    put_int(e, "soundboard", "voices", spin_int(t->voices), 8);
    put(e, "soundboard", "device", text_of(t->device), NULL);

    GtkTreeModel *m = GTK_TREE_MODEL(t->store);
    GtkTreeIter it;
    for (int i = 0; i < t->n_loaded; i++)
        if (!sound_taken(e, t->loaded[i], NULL)) {
            char section[128];
            snprintf(section, sizeof section, "sound.%s", t->loaded[i]);
            kk_ini_remove_section(e->ini, section);
        }
    for (bool more = gtk_tree_model_get_iter_first(m, &it); more;
         more = gtk_tree_model_iter_next(m, &it)) {
        gchar *name, *file, *aliases;
        int volume;
        gtk_tree_model_get(m, &it, S_NAME, &name, S_FILE, &file, S_ALIASES, &aliases,
                           S_VOLUME, &volume, -1);
        char section[128];
        snprintf(section, sizeof section, "sound.%s", name);
        put(e, section, "file", file, NULL);
        put(e, section, "aliases", aliases, NULL);
        put_int(e, section, "volume", volume, 100);
        g_free(name);
        g_free(file);
        g_free(aliases);
    }
    remember_loaded(e);
}

void sounds_free(editor *e)
{
    sounds_tab *t = e->sounds;
    if (!t)
        return;
    if (t->pump_id)
        g_source_remove(t->pump_id);
    if (t->scan_id)
        g_source_remove(t->scan_id);
    kk_audio_free(t->audio); /* before the samples it may still play */
    g_hash_table_destroy(t->cache);
    for (int i = 0; i < t->n_loaded; i++)
        g_free(t->loaded[i]);
    g_free(t);
    e->sounds = NULL;
}
