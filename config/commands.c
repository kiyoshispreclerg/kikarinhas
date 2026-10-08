/* SPDX-License-Identifier: GPL-3.0-or-later */
/* kikarinhas-config: the "Comandos" tab, the [commands] and [command.NAME]
 * sections.
 *
 * The list is an overview; a command is edited in a dialog (double-click,
 * or "Edit") that shows what its action needs: the text of a text command,
 * the picture of an image one with where it goes (and a small preview of
 * the window), the sound, the avatar, the player filter of !song... */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "editor.h"
#include "i18n.h"

enum {
    COL_ENABLED,
    COL_NAME,
    COL_ACTION, /* the key: "sound" */
    COL_ACTION_LABEL,
    COL_DATA,       /* as written in the file */
    COL_DATA_SHOWN, /* a short form for the list */
    COL_ALIASES,
    COL_CD,  /* double */
    COL_GCD, /* double */
    COL_ROLE, /* int kk_role */
    COL_ROLE_LABEL,
    COL_SECONDS, /* double, < 0 = auto */
    COL_X,
    COL_Y,
    COL_SCALE,
    COL_COST, /* long, points */
    COL_CUSTOM, /* not a built-in: name and action can change */
    N_COLS,
};

/* What each action does, in the words of the interface. */
static const struct {
    const char *key, *label;
} ACTION_LABELS[] = {
    {"avatar", N_("Change avatar")},
    {"color", N_("Change color (palette)")},
    {"gear", N_("Wear an accessory")},
    {"jump", N_("Jump")},
    {"sit", N_("Sit")},
    {"dance", N_("Dance")},
    {"emote", N_("Play an animation")},
    {"hug", N_("Hug someone")},
    {"attack", N_("Attack someone")},
    {"sound", N_("Play a sound")},
    {"help", N_("Show some commands (help)")},
    {"text", N_("Show a text in a bubble")},
    {"image", N_("Show a picture")},
    {"nowplaying", N_("Show the music playing")},
    {"points", N_("Show the points balance")},
    {"give", N_("Give points to someone")},
    {"award", N_("Create points for someone")},
    {"top", N_("Show the points ranking")},
    {"left", N_("Walk left")},
    {"right", N_("Walk right")},
    {"move", N_("Walk to a place")},
    {"stand", N_("Stand up")},
    {"freeze", N_("Freeze in place")},
    {"scale", N_("Change the size")},
    {"sling", N_("Slingshot the avatar")},
    {"bomb", N_("Throw a bomb")},
    {"spawn", N_("Spawn stars to catch")},
    {"roll", N_("Roll dice")},
    {"8ball", N_("Magic 8-ball")},
    {"slots", N_("Slot machine")},
    {"bet", N_("Roulette bet")},
};
#define N_ACTIONS (int)(sizeof ACTION_LABELS / sizeof ACTION_LABELS[0])

static const char *action_label(const char *key)
{
    for (int i = 0; i < N_ACTIONS; i++)
        if (strcmp(ACTION_LABELS[i].key, key) == 0)
            return _(ACTION_LABELS[i].label);
    return key;
}

static int action_index(const char *key)
{
    for (int i = 0; i < N_ACTIONS; i++)
        if (strcmp(ACTION_LABELS[i].key, key) == 0)
            return i;
    return 0;
}

static const kk_config_command *builtin(const char *name)
{
    int n;
    const kk_config_command *d = kk_config_default_commands(&n);
    for (int i = 0; i < n; i++)
        if (strcmp(d[i].name, name) == 0)
            return &d[i];
    return NULL;
}

/* ---- the list ------------------------------------------------------------ */

/* The list shows a picture by its file name and a text on one line. */
static void data_shown(char *out, size_t size, const char *action, const char *data)
{
    if (strcmp(action, "image") == 0) {
        const char *slash = strrchr(data, '/');
        snprintf(out, size, "%s", slash ? slash + 1 : data);
        return;
    }
    size_t n = 0;
    for (const char *p = data; *p && n + 1 < size; p++) {
        if (p[0] == '\\' && p[1] == 'n') {
            out[n++] = ' ';
            p++;
        } else {
            out[n++] = *p;
        }
    }
    out[n] = '\0';
}

static void set_row(editor *e, GtkTreeIter *it, const kk_config_command *k, bool custom)
{
    char aliases[512], shown[256];
    const char *data = k->data ? k->data : "";
    join(aliases, sizeof aliases, k->aliases, k->n_aliases);
    data_shown(shown, sizeof shown, k->action, data);
    gtk_list_store_set(e->commands, it, COL_ENABLED, k->enabled, COL_NAME, k->name,
                       COL_ACTION, k->action, COL_ACTION_LABEL, action_label(k->action),
                       COL_DATA, data, COL_DATA_SHOWN, shown, COL_ALIASES, aliases, COL_CD,
                       k->user_cd, COL_GCD, k->global_cd, COL_ROLE, (int)k->role,
                       COL_ROLE_LABEL, _(ROLE_LABELS[k->role]), COL_SECONDS,
                       k->params.seconds, COL_X, k->params.x, COL_Y, k->params.y, COL_SCALE,
                       k->params.scale, COL_COST, k->cost, COL_CUSTOM, custom, -1);
}

/* The row back as a command; free with free_row. */
static void get_row(editor *e, GtkTreeIter *it, kk_config_command *k, gboolean *custom,
                    gchar **aliases)
{
    int role;
    gboolean enabled;
    memset(k, 0, sizeof *k);
    gtk_tree_model_get(GTK_TREE_MODEL(e->commands), it, COL_ENABLED, &enabled, COL_NAME,
                       &k->name, COL_ACTION, &k->action, COL_DATA, &k->data, COL_ALIASES,
                       aliases, COL_CD, &k->user_cd, COL_GCD, &k->global_cd, COL_ROLE, &role,
                       COL_SECONDS, &k->params.seconds, COL_X, &k->params.x, COL_Y,
                       &k->params.y, COL_SCALE, &k->params.scale, COL_COST, &k->cost,
                       COL_CUSTOM, custom, -1);
    k->enabled = enabled;
    k->role = (kk_role)role;
}

static void free_row(kk_config_command *k, gchar *aliases)
{
    g_free(k->name);
    g_free(k->action);
    g_free(k->data);
    g_free(aliases);
}

static bool name_taken(editor *e, const char *name, GtkTreeIter *except)
{
    GtkTreeModel *m = GTK_TREE_MODEL(e->commands);
    GtkTreeIter it;
    for (bool more = gtk_tree_model_get_iter_first(m, &it); more;
         more = gtk_tree_model_iter_next(m, &it)) {
        if (except && it.user_data == except->user_data)
            continue;
        gchar *n;
        gtk_tree_model_get(m, &it, COL_NAME, &n, -1);
        bool same = n && strcmp(n, name) == 0; /* a new row has no name yet */
        g_free(n);
        if (same)
            return true;
    }
    return false;
}

static bool selected(editor *e, GtkTreeIter *it)
{
    GtkTreeSelection *sel = gtk_tree_view_get_selection(GTK_TREE_VIEW(e->tree));
    return gtk_tree_selection_get_selected(sel, NULL, it);
}

/* ---- the dialog ---------------------------------------------------------- */

typedef struct {
    editor *e;
    bool custom;
    GtkWidget *dlg, *table, *problem;
    GtkWidget *name, *action, *enabled, *aliases, *cd, *gcd, *role, *cost;
    GtkWidget *data, *sound, *avatar, *text, *file, *x, *y, *scale, *sec_auto, *sec,
        *preview, *player;
    /* Rows shown or hidden with the action: label and widget. */
    GtkWidget *rows[16][2];
    int n_rows;
    int r_data, r_sound, r_avatar, r_text, r_file, r_place, r_preview, r_seconds, r_player;
    GdkPixbuf *pix; /* the picture, for the preview */
} dialog;

static int add_row(dialog *d, const char *label, GtkWidget *w, const char *hint)
{
    guint n;
    g_object_get(d->table, "n-rows", &n, NULL);
    gtk_table_resize(GTK_TABLE(d->table), n + 1, 2);
    GtkWidget *l = gtk_label_new(label);
    gtk_misc_set_alignment(GTK_MISC(l), 1, hint ? 0.1 : 0.5);
    gtk_table_attach(GTK_TABLE(d->table), l, 0, 1, n, n + 1, GTK_FILL, GTK_FILL, 0, 0);
    GtkWidget *cell = w;
    if (hint) {
        cell = gtk_vbox_new(FALSE, 2);
        GtkWidget *h = gtk_label_new(NULL);
        char *markup = g_markup_printf_escaped("<small>%s</small>", hint);
        gtk_label_set_markup(GTK_LABEL(h), markup);
        g_free(markup);
        gtk_label_set_line_wrap(GTK_LABEL(h), TRUE);
        gtk_widget_set_size_request(h, 420, -1);
        gtk_misc_set_alignment(GTK_MISC(h), 0, 0);
        gtk_widget_set_sensitive(h, FALSE);
        gtk_box_pack_start(GTK_BOX(cell), w, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(cell), h, FALSE, FALSE, 0);
    }
    gtk_table_attach(GTK_TABLE(d->table), cell, 1, 2, n, n + 1, GTK_EXPAND | GTK_FILL,
                     GTK_FILL, 0, 0);
    d->rows[d->n_rows][0] = l;
    d->rows[d->n_rows][1] = cell;
    return d->n_rows++;
}

static void show_row(dialog *d, int r, bool on)
{
    for (int i = 0; i < 2; i++) {
        if (on)
            gtk_widget_show_all(d->rows[r][i]);
        else
            gtk_widget_hide(d->rows[r][i]);
    }
}

static const char *current_action(dialog *d)
{
    int i = gtk_combo_box_get_active(GTK_COMBO_BOX(d->action));
    return ACTION_LABELS[i >= 0 && i < N_ACTIONS ? i : 0].key;
}

static bool is(const char *a, const char *b)
{
    return strcmp(a, b) == 0;
}

static void update_rows(dialog *d)
{
    const char *a = current_action(d);
    show_row(d, d->r_data, is(a, "color") || is(a, "gear") || is(a, "emote"));
    show_row(d, d->r_avatar, is(a, "avatar"));
    show_row(d, d->r_sound, is(a, "sound"));
    show_row(d, d->r_text, is(a, "text"));
    show_row(d, d->r_file, is(a, "image"));
    show_row(d, d->r_place, is(a, "image"));
    show_row(d, d->r_preview, is(a, "image"));
    show_row(d, d->r_player, is(a, "nowplaying"));
    show_row(d, d->r_seconds, is(a, "text") || is(a, "image") || is(a, "nowplaying"));
    gtk_window_resize(GTK_WINDOW(d->dlg), 1, 1); /* shrink to what shows */
}

static void on_action_changed(GtkComboBox *c, gpointer ud)
{
    (void)c;
    dialog *d = ud;
    update_rows(d);
    /* The avatar list takes a moment: only when an avatar command needs it. */
    if (is(current_action(d), "avatar") &&
        !gtk_tree_model_iter_n_children(gtk_combo_box_get_model(GTK_COMBO_BOX(d->avatar)),
                                        NULL)) {
        const kk_sa_library *lib = editor_library(d->e);
        for (int i = 0; lib && i < lib->count; i++)
            if (lib->avatars[i].image)
                gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(d->avatar),
                                               lib->avatars[i].key);
    }
}

static gchar *expand_home(const char *path)
{
    if (strncmp(path, "~/", 2) == 0)
        return g_strdup_printf("%s%s", g_get_home_dir(), path + 1);
    return g_strdup(path);
}

static gchar *tilde(const char *path)
{
    const char *home = g_get_home_dir();
    size_t n = strlen(home);
    if (strncmp(path, home, n) == 0 && path[n] == '/')
        return g_strdup_printf("~%s", path + n);
    return g_strdup(path);
}

static void load_preview(dialog *d)
{
    if (d->pix)
        g_object_unref(d->pix);
    d->pix = NULL;
    gchar *f = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(d->file));
    if (f)
        d->pix = gdk_pixbuf_new_from_file(f, NULL); /* a GIF gives its first frame */
    g_free(f);
    gtk_widget_queue_draw(d->preview);
}

static void on_file_set(GtkFileChooserButton *b, gpointer ud)
{
    (void)b;
    load_preview(ud);
}

static void on_place_changed(GtkSpinButton *s, gpointer ud)
{
    (void)s;
    gtk_widget_queue_draw(((dialog *)ud)->preview);
}

/* The window (its size from the Window tab) with the picture where it will
 * be, at its size. */
static gboolean on_preview_expose(GtkWidget *w, GdkEventExpose *ev, gpointer ud)
{
    (void)ev;
    dialog *d = ud;
    GtkAllocation a;
    gtk_widget_get_allocation(w, &a);
    double win_w = spin_int(d->e->width), win_h = spin_int(d->e->height);
    double k = fmin((a.width - 2) / win_w, (a.height - 2) / win_h);
    double ox = (a.width - win_w * k) / 2, oy = (a.height - win_h * k) / 2;
    cairo_t *cr = gdk_cairo_create(gtk_widget_get_window(w));
    cairo_rectangle(cr, ox, oy, win_w * k, win_h * k);
    cairo_set_source_rgb(cr, 0.16, 0.16, 0.2);
    cairo_fill_preserve(cr);
    cairo_set_source_rgb(cr, 0.5, 0.5, 0.55);
    cairo_set_line_width(cr, 1);
    cairo_stroke(cr);
    cairo_rectangle(cr, ox, oy, win_w * k, win_h * k);
    cairo_clip(cr);
    double s = gtk_spin_button_get_value(GTK_SPIN_BUTTON(d->scale)) / 100.0;
    double cx = ox + win_w * k * gtk_spin_button_get_value(GTK_SPIN_BUTTON(d->x)) / 100.0;
    double cy = oy + win_h * k * gtk_spin_button_get_value(GTK_SPIN_BUTTON(d->y)) / 100.0;
    if (d->pix) {
        double pw = gdk_pixbuf_get_width(d->pix), ph = gdk_pixbuf_get_height(d->pix);
        cairo_translate(cr, cx - pw * s * k / 2, cy - ph * s * k / 2);
        cairo_scale(cr, s * k, s * k);
        gdk_cairo_set_source_pixbuf(cr, d->pix, 0, 0);
        cairo_paint(cr);
    } else {
        cairo_arc(cr, cx, cy, 4, 0, 2 * M_PI);
        cairo_set_source_rgb(cr, 1, 0.4, 0.6);
        cairo_fill(cr);
    }
    cairo_destroy(cr);
    return TRUE;
}

static GtkWidget *spin_at(double lo, double hi, double step, int digits, double v)
{
    GtkWidget *s = spin(lo, hi, step, digits);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(s), v);
    return s;
}

static GtkWidget *hbox_of(GtkWidget *a, const char *t1, GtkWidget *b, const char *t2)
{
    GtkWidget *box = gtk_hbox_new(FALSE, 6);
    gtk_box_pack_start(GTK_BOX(box), a, FALSE, FALSE, 0);
    if (t1)
        gtk_box_pack_start(GTK_BOX(box), gtk_label_new(t1), FALSE, FALSE, 0);
    if (b)
        gtk_box_pack_start(GTK_BOX(box), b, FALSE, FALSE, 0);
    if (t2)
        gtk_box_pack_start(GTK_BOX(box), gtk_label_new(t2), FALSE, FALSE, 0);
    return box;
}

static void on_auto_toggled(GtkToggleButton *b, gpointer spinner)
{
    gtk_widget_set_sensitive(GTK_WIDGET(spinner), !gtk_toggle_button_get_active(b));
}

/* "\n" in the file is a line break in the text box, and back. */
static gchar *text_from_data(const char *data)
{
    GString *s = g_string_new(NULL);
    for (const char *p = data; *p; p++) {
        if (p[0] == '\\' && p[1] == 'n') {
            g_string_append_c(s, '\n');
            p++;
        } else {
            g_string_append_c(s, *p);
        }
    }
    return g_string_free(s, FALSE);
}

static gchar *data_from_text(const char *text)
{
    GString *s = g_string_new(NULL);
    for (const char *p = text; *p; p++) {
        if (*p == '\n')
            g_string_append(s, "\\n");
        else if (*p != '\r')
            g_string_append_c(s, *p);
    }
    return g_strstrip(g_string_free(s, FALSE));
}

static void combo_entry_set(GtkWidget *combo, const char *text)
{
    gtk_entry_set_text(GTK_ENTRY(gtk_bin_get_child(GTK_BIN(combo))), text);
}

static const char *combo_entry_get(GtkWidget *combo)
{
    return gtk_entry_get_text(GTK_ENTRY(gtk_bin_get_child(GTK_BIN(combo))));
}

static void build_dialog(dialog *d, const kk_config_command *k, const char *title)
{
    editor *e = d->e;
    d->dlg = gtk_dialog_new_with_buttons(title, GTK_WINDOW(e->window),
                                         GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                         NULL, NULL);
    gtk_dialog_add_button(GTK_DIALOG(d->dlg), _("_Cancel"), GTK_RESPONSE_CANCEL);
    gtk_dialog_add_button(GTK_DIALOG(d->dlg), _("_OK"), GTK_RESPONSE_OK);
    gtk_dialog_set_default_response(GTK_DIALOG(d->dlg), GTK_RESPONSE_OK);
    d->table = gtk_table_new(1, 2, FALSE);
    gtk_table_set_row_spacings(GTK_TABLE(d->table), 6);
    gtk_table_set_col_spacings(GTK_TABLE(d->table), 12);
    gtk_container_set_border_width(GTK_CONTAINER(d->table), 12);

    char name[80];
    snprintf(name, sizeof name, "%s", k->name);
    d->name = entry(name);
    gtk_widget_set_sensitive(d->name, d->custom);
    add_row(d, _("Command"), d->name,
            d->custom ? _("One word, without \"!\": what people type in the chat.") : NULL);

    d->action = gtk_combo_box_text_new();
    for (int i = 0; i < N_ACTIONS; i++)
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(d->action),
                                       _(ACTION_LABELS[i].label));
    gtk_combo_box_set_active(GTK_COMBO_BOX(d->action), action_index(k->action));
    gtk_widget_set_sensitive(d->action, d->custom);
    add_row(d, _("Action"), d->action, NULL);

    d->enabled = check(_("Active"), k->enabled);
    add_row(d, "", d->enabled, NULL);
    char aliases[512];
    join(aliases, sizeof aliases, k->aliases, k->n_aliases);
    d->aliases = entry(aliases);
    add_row(d, _("Aliases"), d->aliases, _("Other words for the same command, separated by commas."));

    d->cd = spin_at(0, 86400, 1, 0, k->user_cd);
    d->gcd = spin_at(0, 86400, 1, 0, k->global_cd);
    add_row(d, _("Cooldown"),
            hbox_of(d->cd, _("s per person;"), d->gcd, _("s for everyone")),
            _("The channel owner never waits."));
    d->role = gtk_combo_box_text_new();
    for (int i = 0; i < KK_ROLE_COUNT; i++)
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(d->role), _(ROLE_LABELS[i]));
    gtk_combo_box_set_active(GTK_COMBO_BOX(d->role), (int)k->role);
    add_row(d, _("Who can"), d->role,
            _("The lowest level allowed; trusted people are marked in the Viewers tab."));
    d->cost = spin_at(0, 1000000000, 1, 0, (double)k->cost);
    add_row(d, _("Cost"), hbox_of(d->cost, _("points"), NULL, NULL),
            _("Taken only when the command does something; the channel owner never pays. "
              "See the Points tab."));

    /* What depends on the action. */
    const char *data = k->data ? k->data : "";
    d->data = entry(data);
    d->r_data = add_row(d, _("Fixed argument"), d->data,
                        _("Empty: whatever the person types after the command."));
    d->avatar = gtk_combo_box_text_new_with_entry();
    combo_entry_set(d->avatar, is(k->action, "avatar") ? data : "");
    d->r_avatar = add_row(d, _("Avatar"), d->avatar,
                          _("Empty: whatever the person types (!avatar NAME); filled in, "
                            "this command always turns into that avatar."));
    d->sound = gtk_combo_box_text_new_with_entry();
    GtkTreeModel *sm = sounds_model(e);
    GtkTreeIter it;
    for (bool more = gtk_tree_model_get_iter_first(sm, &it); more;
         more = gtk_tree_model_iter_next(sm, &it)) {
        gchar *sname;
        gtk_tree_model_get(sm, &it, 0, &sname, -1);
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(d->sound), sname);
        g_free(sname);
    }
    combo_entry_set(d->sound, is(k->action, "sound") ? data : "");
    d->r_sound = add_row(d, _("Sound"), d->sound,
                         _("A sound of the Sounds tab. Empty: !sound NAME, whatever the "
                           "person asks for."));

    d->text = gtk_text_view_new();
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(d->text), GTK_WRAP_WORD_CHAR);
    gchar *t = text_from_data(is(k->action, "text") ? data : "");
    gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(d->text)), t, -1);
    g_free(t);
    GtkWidget *tscroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(tscroll), GTK_POLICY_NEVER,
                                   GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(tscroll), GTK_SHADOW_IN);
    gtk_widget_set_size_request(tscroll, 420, 80);
    gtk_container_add(GTK_CONTAINER(tscroll), d->text);
    d->r_text = add_row(d, _("Text"), tscroll,
                        _("Shown in a bubble over the avatar of who asked (links, rules...). "
                          "{name} becomes their name."));

    d->file = gtk_file_chooser_button_new(_("Picture"), GTK_FILE_CHOOSER_ACTION_OPEN);
    GtkFileFilter *f = gtk_file_filter_new();
    gtk_file_filter_set_name(f, _("Pictures (PNG, JPEG, GIF, BMP)"));
    static const char *const pats[] = {"*.png", "*.PNG", "*.jpg", "*.JPG", "*.jpeg",
                                       "*.JPEG", "*.gif", "*.GIF", "*.bmp", "*.BMP"};
    for (size_t i = 0; i < sizeof pats / sizeof pats[0]; i++)
        gtk_file_filter_add_pattern(f, pats[i]);
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(d->file), f);
    if (is(k->action, "image") && data[0]) {
        gchar *full = expand_home(data);
        gtk_file_chooser_set_filename(GTK_FILE_CHOOSER(d->file), full);
        g_free(full);
    }
    g_signal_connect(d->file, "file-set", G_CALLBACK(on_file_set), d);
    d->r_file = add_row(d, _("Picture"), d->file,
                        _("PNG, JPEG, BMP or GIF (animated ones play)."));
    d->x = spin_at(-100, 200, 1, 0, k->params.x);
    d->y = spin_at(-100, 200, 1, 0, k->params.y);
    d->scale = spin_at(1, 10000, 5, 0, k->params.scale);
    GtkWidget *place = hbox_of(d->x, "% ×", d->y, "%;");
    gtk_box_pack_start(GTK_BOX(place), gtk_label_new(_("size")), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(place), d->scale, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(place), gtk_label_new("%"), FALSE, FALSE, 0);
    d->r_place = add_row(d, _("Centre at"), place,
                         /* xgettext:no-c-format */
                         _("Where the middle of the picture goes, in % of the window "
                           "(50 × 50 is the centre), and its size in % of its own."));
    for (GtkWidget **s = (GtkWidget *[]){d->x, d->y, d->scale, NULL}; *s; s++)
        g_signal_connect(*s, "value-changed", G_CALLBACK(on_place_changed), d);
    d->preview = gtk_drawing_area_new();
    gtk_widget_set_size_request(d->preview, 288, 162);
    g_signal_connect(d->preview, "expose-event", G_CALLBACK(on_preview_expose), d);
    d->r_preview = add_row(d, _("Preview"), d->preview, NULL);

    d->player = entry(is(k->action, "nowplaying") ? data : "");
    d->r_player = add_row(d, _("Only players named"), d->player,
                          _("Part of the player's name (spotify, vlc...). Empty: any player, "
                            "browsers included."));

    d->sec_auto = gtk_check_button_new_with_label(_("automatic"));
    d->sec = spin_at(1, 600, 1, 0, k->params.seconds > 0 ? k->params.seconds : 5);
    g_signal_connect(d->sec_auto, "toggled", G_CALLBACK(on_auto_toggled), d->sec);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(d->sec_auto), k->params.seconds <= 0);
    on_auto_toggled(GTK_TOGGLE_BUTTON(d->sec_auto), d->sec);
    d->r_seconds = add_row(d, _("On screen for"), hbox_of(d->sec_auto, NULL, d->sec, _("seconds")),
                           _("Automatic: a bubble by the length of its text, a picture 5 s."));

    /* Enter in a one-line field means OK. */
    for (GtkWidget **w = (GtkWidget *[]){d->name, d->aliases, d->data, d->player, NULL}; *w; w++)
        gtk_entry_set_activates_default(GTK_ENTRY(*w), TRUE);
    d->problem = gtk_label_new(NULL);
    gtk_misc_set_alignment(GTK_MISC(d->problem), 0, 0.5);
    GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(d->dlg));
    gtk_box_pack_start(GTK_BOX(content), d->table, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(content), d->problem, FALSE, FALSE, 6);
    g_signal_connect(d->action, "changed", G_CALLBACK(on_action_changed), d);
    gtk_widget_show_all(d->dlg);
    on_action_changed(GTK_COMBO_BOX(d->action), d);
    load_preview(d);
}

static void problem(dialog *d, const char *msg)
{
    char *markup = g_markup_printf_escaped("<span foreground=\"#c01c28\">%s</span>", msg);
    gtk_label_set_markup(GTK_LABEL(d->problem), markup);
    g_free(markup);
}

/* Reads the dialog into the row; false (and why, in the dialog) if
 * something is missing. */
static bool apply_dialog(dialog *d, GtkTreeIter *it, bool is_new)
{
    editor *e = d->e;
    const char *a = current_action(d);
    char name[64];
    const char *s = text_of(d->name);
    if (*s == '!')
        s++;
    size_t n = strlen(s);
    if (n == 0 || n >= sizeof name || strpbrk(s, " \t!")) {
        problem(d, _("Invalid command name: use a single word, no spaces."));
        return false;
    }
    for (size_t i = 0; i <= n; i++)
        name[i] = (char)g_ascii_tolower(s[i]);
    if (name_taken(e, name, is_new ? NULL : it)) {
        char msg[128];
        snprintf(msg, sizeof msg, _("A command !%s already exists."), name);
        problem(d, msg);
        return false;
    }

    gchar *data = NULL;
    if (is(a, "text")) {
        GtkTextBuffer *b = gtk_text_view_get_buffer(GTK_TEXT_VIEW(d->text));
        GtkTextIter i0, i1;
        gtk_text_buffer_get_bounds(b, &i0, &i1);
        gchar *t = gtk_text_buffer_get_text(b, &i0, &i1, FALSE);
        data = data_from_text(t);
        g_free(t);
    } else if (is(a, "image")) {
        gchar *f = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(d->file));
        data = f ? tilde(f) : g_strdup("");
        g_free(f);
    } else if (is(a, "avatar")) {
        data = g_strstrip(g_strdup(combo_entry_get(d->avatar)));
    } else if (is(a, "sound")) {
        data = g_strstrip(g_strdup(combo_entry_get(d->sound)));
    } else if (is(a, "nowplaying")) {
        data = g_strstrip(g_strdup(text_of(d->player)));
    } else if (is(a, "color") || is(a, "gear") || is(a, "emote")) {
        data = g_strstrip(g_strdup(text_of(d->data)));
    } else {
        data = g_strdup("");
    }
    const char *missing = NULL;
    if (is(a, "text") && !data[0])
        missing = _("Write the text to show.");
    else if (is(a, "image") && !data[0])
        missing = _("Pick the picture to show.");
    else if (d->custom && is(a, "sound") && !data[0])
        missing = _("Pick the sound to play.");
    if (missing) {
        problem(d, missing);
        g_free(data);
        return false;
    }

    kk_config_command k = {
        .name = name,
        .action = (char *)a,
        .data = data,
        .enabled = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(d->enabled)),
        .user_cd = gtk_spin_button_get_value(GTK_SPIN_BUTTON(d->cd)),
        .global_cd = gtk_spin_button_get_value(GTK_SPIN_BUTTON(d->gcd)),
        .role = (kk_role)gtk_combo_box_get_active(GTK_COMBO_BOX(d->role)),
        .cost = (long)gtk_spin_button_get_value(GTK_SPIN_BUTTON(d->cost)),
        .params = {
            .seconds = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(d->sec_auto))
                           ? -1
                           : gtk_spin_button_get_value(GTK_SPIN_BUTTON(d->sec)),
            .x = gtk_spin_button_get_value(GTK_SPIN_BUTTON(d->x)),
            .y = gtk_spin_button_get_value(GTK_SPIN_BUTTON(d->y)),
            .scale = gtk_spin_button_get_value(GTK_SPIN_BUTTON(d->scale)),
        },
    };
    set_row(e, it, &k, d->custom);
    /* Aliases go in as typed: the list keeps the text, the file check says
     * if one is bad. */
    gchar *aliases = g_strstrip(g_strdup(text_of(d->aliases)));
    gtk_list_store_set(e->commands, it, COL_ALIASES, aliases, -1);
    g_free(aliases);
    g_free(data);
    return true;
}

/* Opens the dialog for the row at it (NULL: a new command). */
static void edit(editor *e, GtkTreeIter *it)
{
    kk_config_command k;
    gchar *aliases = NULL;
    gboolean custom = TRUE;
    char base[16], name[32];
    if (it) {
        get_row(e, it, &k, &custom, &aliases);
        /* The row keeps aliases as a string; the dialog wants a list. */
        gchar **parts = g_strsplit(aliases, ",", KK_CONFIG_MAX_ALIASES);
        for (k.n_aliases = 0; parts[k.n_aliases]; k.n_aliases++)
            k.aliases[k.n_aliases] = g_strstrip(parts[k.n_aliases]);
        dialog d = {.e = e, .custom = custom};
        char title[128];
        snprintf(title, sizeof title, _("Command !%s"), k.name);
        build_dialog(&d, &k, title);
        while (gtk_dialog_run(GTK_DIALOG(d.dlg)) == GTK_RESPONSE_OK)
            if (apply_dialog(&d, it, false))
                break;
        if (d.pix)
            g_object_unref(d.pix);
        gtk_widget_destroy(d.dlg);
        g_free(parts); /* the strings were the aliases, freed below */
        for (int i = 0; i < k.n_aliases; i++)
            g_free(k.aliases[i]);
        free_row(&k, aliases);
        return;
    }
    snprintf(base, sizeof base, "%s", _("new"));
    snprintf(name, sizeof name, "%s", base);
    for (int i = 2; name_taken(e, name, NULL); i++)
        snprintf(name, sizeof name, "%s%d", base, i);
    k = (kk_config_command){.name = name, .action = (char *)"text", .user_cd = 30,
                            .enabled = true, .params = KK_CMD_PARAMS_DEFAULT};
    dialog d = {.e = e, .custom = true};
    build_dialog(&d, &k, _("New command"));
    GtkTreeIter nit;
    gtk_list_store_append(e->commands, &nit);
    bool ok = false;
    while (!ok && gtk_dialog_run(GTK_DIALOG(d.dlg)) == GTK_RESPONSE_OK)
        ok = apply_dialog(&d, &nit, true);
    if (!ok)
        gtk_list_store_remove(e->commands, &nit);
    if (d.pix)
        g_object_unref(d.pix);
    gtk_widget_destroy(d.dlg);
}

static void on_row_activated(GtkTreeView *v, GtkTreePath *p, GtkTreeViewColumn *c, gpointer ud)
{
    (void)v;
    (void)c;
    editor *e = ud;
    GtkTreeIter it;
    if (gtk_tree_model_get_iter(GTK_TREE_MODEL(e->commands), &it, p))
        edit(e, &it);
}

static void on_edit(GtkButton *b, gpointer ud)
{
    (void)b;
    editor *e = ud;
    GtkTreeIter it;
    if (selected(e, &it))
        edit(e, &it);
    else
        status(e, _("Pick a command in the list."));
}

static void on_add(GtkButton *b, gpointer ud)
{
    (void)b;
    edit(ud, NULL);
}

static void on_toggle_enabled(GtkCellRendererToggle *r, gchar *path, gpointer ud)
{
    editor *e = ud;
    GtkTreeIter it;
    if (gtk_tree_model_get_iter_from_string(GTK_TREE_MODEL(e->commands), &it, path))
        gtk_list_store_set(e->commands, &it, COL_ENABLED,
                           !gtk_cell_renderer_toggle_get_active(r), -1);
}

static void on_remove(GtkButton *b, gpointer ud)
{
    (void)b;
    editor *e = ud;
    GtkTreeIter it;
    if (!selected(e, &it)) {
        status(e, _("Pick a command in the list."));
        return;
    }
    gboolean custom;
    gtk_tree_model_get(GTK_TREE_MODEL(e->commands), &it, COL_CUSTOM, &custom, -1);
    if (!custom) {
        status(e, _("Default commands cannot be removed: untick \"Active\" to turn one off."));
        return;
    }
    gtk_list_store_remove(e->commands, &it);
}

static void on_restore(GtkButton *b, gpointer ud)
{
    (void)b;
    editor *e = ud;
    GtkTreeIter it;
    if (!selected(e, &it)) {
        status(e, _("Pick a command in the list."));
        return;
    }
    gchar *name;
    gtk_tree_model_get(GTK_TREE_MODEL(e->commands), &it, COL_NAME, &name, -1);
    const kk_config_command *d = builtin(name);
    g_free(name);
    if (!d) {
        status(e, _("Only default commands have default values."));
        return;
    }
    set_row(e, &it, d, false);
}

static void seconds_text(GtkTreeViewColumn *c, GtkCellRenderer *r, GtkTreeModel *m,
                         GtkTreeIter *it, gpointer col)
{
    (void)c;
    double v;
    gtk_tree_model_get(m, it, GPOINTER_TO_INT(col), &v, -1);
    char s[32];
    snprintf(s, sizeof s, "%g s", v);
    g_object_set(r, "text", s, NULL);
}

/* "—" for free commands, so the paid ones stand out. */
static void cost_text(GtkTreeViewColumn *c, GtkCellRenderer *r, GtkTreeModel *m,
                      GtkTreeIter *it, gpointer ud)
{
    (void)c;
    (void)ud;
    glong v;
    gtk_tree_model_get(m, it, COL_COST, &v, -1);
    char s[32];
    snprintf(s, sizeof s, v > 0 ? "%ld" : "—", (long)v);
    g_object_set(r, "text", s, NULL);
}

static void text_column(editor *e, const char *title, int col, bool expand)
{
    GtkCellRenderer *r = gtk_cell_renderer_text_new();
    GtkTreeViewColumn *c = gtk_tree_view_column_new_with_attributes(title, r, NULL, NULL);
    if (col == COL_CD || col == COL_GCD) {
        gtk_tree_view_column_set_cell_data_func(c, r, seconds_text, GINT_TO_POINTER(col),
                                                NULL);
    } else if (col == COL_COST) {
        gtk_tree_view_column_set_cell_data_func(c, r, cost_text, NULL, NULL);
    } else {
        gtk_tree_view_column_add_attribute(c, r, "text", col);
    }
    if (expand) {
        g_object_set(r, "ellipsize", PANGO_ELLIPSIZE_END, NULL);
        gtk_tree_view_column_set_expand(c, TRUE);
    }
    gtk_tree_view_column_set_resizable(c, TRUE);
    gtk_tree_view_append_column(GTK_TREE_VIEW(e->tree), c);
}

static void remember_custom(editor *e)
{
    for (int i = 0; i < e->n_loaded_custom; i++)
        g_free(e->loaded_custom[i]);
    e->n_loaded_custom = 0;
    GtkTreeModel *m = GTK_TREE_MODEL(e->commands);
    GtkTreeIter it;
    for (bool more = gtk_tree_model_get_iter_first(m, &it); more && e->n_loaded_custom < 256;
         more = gtk_tree_model_iter_next(m, &it)) {
        gboolean custom;
        gchar *name;
        gtk_tree_model_get(m, &it, COL_CUSTOM, &custom, COL_NAME, &name, -1);
        if (custom)
            e->loaded_custom[e->n_loaded_custom++] = name;
        else
            g_free(name);
    }
}

GtkWidget *commands_page(editor *e, const kk_config *cfg)
{
    GtkWidget *box = gtk_vbox_new(FALSE, 6);
    gtk_container_set_border_width(GTK_CONTAINER(box), 12);

    GtkWidget *top = gtk_hbox_new(FALSE, 6);
    e->shortcuts = check(_("Shortcut \"!name\" for an avatar, accessory or palette;"), cfg->shortcuts);
    e->shortcut_cd = spin_at(0, 86400, 1, 0, cfg->shortcut_cd);
    gtk_box_pack_start(GTK_BOX(top), e->shortcuts, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(top), gtk_label_new(_("cooldown")), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(top), e->shortcut_cd, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(top), gtk_label_new("s"), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), top, FALSE, FALSE, 0);

    GtkWidget *help = gtk_hbox_new(FALSE, 6);
    e->help_bubbles = check(_("Help bubble (!help), even with message bubbles off;"),
                            cfg->help_bubbles);
    e->help_count = spin_at(1, 20, 1, 0, cfg->help_count);
    gtk_box_pack_start(GTK_BOX(help), e->help_bubbles, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(help), gtk_label_new(_("random commands per bubble")),
                       FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(help), e->help_count, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(help), gtk_label_new(_("staying")), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(help),
                       auto_spin(&e->help_auto, &e->help_secs, 1, 600,
                                 cfg->help_seconds < 0 ? -1 : (int)(cfg->help_seconds + 0.5)),
                       FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), help, FALSE, FALSE, 0);

    e->commands = gtk_list_store_new(N_COLS, G_TYPE_BOOLEAN, G_TYPE_STRING, G_TYPE_STRING,
                                     G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING,
                                     G_TYPE_STRING, G_TYPE_DOUBLE, G_TYPE_DOUBLE, G_TYPE_INT,
                                     G_TYPE_STRING, G_TYPE_DOUBLE, G_TYPE_DOUBLE,
                                     G_TYPE_DOUBLE, G_TYPE_DOUBLE, G_TYPE_LONG,
                                     G_TYPE_BOOLEAN);
    for (int i = 0; i < cfg->n_commands; i++) {
        kk_config_command k = cfg->commands[i];
        bool custom = builtin(k.name) == NULL;
        /* A picture as written in the file (relative, "~/"), not resolved. */
        char section[128];
        snprintf(section, sizeof section, "command.%s", k.name);
        const char *raw = kk_ini_get(e->ini, section, "data");
        if (raw && is(k.action, "image"))
            k.data = (char *)raw;
        GtkTreeIter it;
        gtk_list_store_append(e->commands, &it);
        set_row(e, &it, &k, custom);
    }
    remember_custom(e);
    e->tree = gtk_tree_view_new_with_model(GTK_TREE_MODEL(e->commands));
    g_object_unref(e->commands);
    gtk_tree_view_set_rules_hint(GTK_TREE_VIEW(e->tree), TRUE);
    g_signal_connect(e->tree, "row-activated", G_CALLBACK(on_row_activated), e);

    GtkCellRenderer *t = gtk_cell_renderer_toggle_new();
    g_signal_connect(t, "toggled", G_CALLBACK(on_toggle_enabled), e);
    gtk_tree_view_append_column(GTK_TREE_VIEW(e->tree),
                                gtk_tree_view_column_new_with_attributes(
                                    _("Active"), t, "active", COL_ENABLED, NULL));
    text_column(e, _("Command"), COL_NAME, false);
    text_column(e, _("Action"), COL_ACTION_LABEL, false);
    text_column(e, _("Data"), COL_DATA_SHOWN, true);
    text_column(e, _("Aliases"), COL_ALIASES, true);
    text_column(e, _("Cooldown"), COL_CD, false);
    text_column(e, _("Global"), COL_GCD, false);
    text_column(e, _("Who can"), COL_ROLE_LABEL, false);
    text_column(e, _("Cost"), COL_COST, false);

    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_AUTOMATIC,
                                   GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(scroll), GTK_SHADOW_IN);
    gtk_container_add(GTK_CONTAINER(scroll), e->tree);
    gtk_box_pack_start(GTK_BOX(box), scroll, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(box),
                       hint_label(_("Double-click a command (or Edit) to change it. New "
                                    "commands can show a text (links, rules), a picture on "
                                    "the screen, play a sound, change avatar... The channel "
                                    "owner never waits.")),
                       FALSE, FALSE, 0);

    GtkWidget *buttons = gtk_hbutton_box_new();
    gtk_button_box_set_layout(GTK_BUTTON_BOX(buttons), GTK_BUTTONBOX_START);
    gtk_box_set_spacing(GTK_BOX(buttons), 6);
    GtkWidget *add = icon_button(GTK_STOCK_ADD, _("_Add"));
    GtkWidget *ed = icon_button(GTK_STOCK_EDIT, _("_Edit"));
    GtkWidget *rm = icon_button(GTK_STOCK_REMOVE, _("_Remove"));
    GtkWidget *restore = gtk_button_new_with_mnemonic(_("Restore _defaults"));
    g_signal_connect(add, "clicked", G_CALLBACK(on_add), e);
    g_signal_connect(ed, "clicked", G_CALLBACK(on_edit), e);
    g_signal_connect(rm, "clicked", G_CALLBACK(on_remove), e);
    g_signal_connect(restore, "clicked", G_CALLBACK(on_restore), e);
    gtk_container_add(GTK_CONTAINER(buttons), add);
    gtk_container_add(GTK_CONTAINER(buttons), ed);
    gtk_container_add(GTK_CONTAINER(buttons), rm);
    gtk_container_add(GTK_CONTAINER(buttons), restore);
    gtk_box_pack_start(GTK_BOX(box), buttons, FALSE, FALSE, 0);
    return box;
}

void commands_each_name(editor *e, void (*fn)(const char *name, void *ud), void *ud)
{
    GtkTreeModel *m = GTK_TREE_MODEL(e->commands);
    GtkTreeIter it;
    for (bool more = gtk_tree_model_get_iter_first(m, &it); more;
         more = gtk_tree_model_iter_next(m, &it)) {
        gchar *name;
        gboolean enabled;
        gtk_tree_model_get(m, &it, COL_NAME, &name, COL_ENABLED, &enabled, -1);
        if (enabled && name)
            fn(name, ud);
        g_free(name);
    }
}

/* ---- costs, for the Points tab ------------------------------------------- */

static void on_cost_edited(GtkCellRendererText *r, gchar *path, gchar *text, gpointer ud)
{
    (void)r;
    editor *e = ud;
    GtkTreeIter it;
    long v = 0;
    g_strstrip(text);
    if (text[0] && !kk_parse_long(text, 0, 1000000000L, &v)) {
        status(e, _("Invalid cost: \"%s\" (a number of points)."), text);
        return;
    }
    if (gtk_tree_model_get_iter_from_string(GTK_TREE_MODEL(e->commands), &it, path))
        gtk_list_store_set(e->commands, &it, COL_COST, (glong)v, -1);
}

GtkWidget *commands_cost_view(editor *e)
{
    GtkWidget *tree = gtk_tree_view_new_with_model(GTK_TREE_MODEL(e->commands));
    gtk_tree_view_set_rules_hint(GTK_TREE_VIEW(tree), TRUE);
    GtkCellRenderer *r = gtk_cell_renderer_text_new();
    gtk_tree_view_append_column(GTK_TREE_VIEW(tree), gtk_tree_view_column_new_with_attributes(
                                                         _("Command"), r, "text", COL_NAME, NULL));
    r = gtk_cell_renderer_text_new();
    g_object_set(r, "ellipsize", PANGO_ELLIPSIZE_END, NULL);
    GtkTreeViewColumn *c =
        gtk_tree_view_column_new_with_attributes(_("Action"), r, "text", COL_ACTION_LABEL, NULL);
    gtk_tree_view_column_set_expand(c, TRUE);
    gtk_tree_view_append_column(GTK_TREE_VIEW(tree), c);
    r = gtk_cell_renderer_text_new();
    g_object_set(r, "editable", TRUE, "xalign", 1.0, NULL);
    g_signal_connect(r, "edited", G_CALLBACK(on_cost_edited), e);
    c = gtk_tree_view_column_new_with_attributes(_("Cost"), r, NULL, NULL);
    gtk_tree_view_column_set_cell_data_func(c, r, cost_text, NULL, NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(tree), c);
    return tree;
}

/* ---- saving -------------------------------------------------------------- */

static void save_command(editor *e, GtkTreeIter *it)
{
    kk_config_command k;
    gboolean custom;
    gchar *aliases;
    get_row(e, it, &k, &custom, &aliases);
    char section[128];
    snprintf(section, sizeof section, "command.%s", k.name);
    const kk_config_command *d = custom ? NULL : builtin(k.name);

    char d_aliases[512] = "", d_cd[32] = "", d_gcd[32] = "", cd[32], gcd[32];
    fmt_num(cd, sizeof cd, k.user_cd);
    fmt_num(gcd, sizeof gcd, k.global_cd);
    if (d) {
        join(d_aliases, sizeof d_aliases, d->aliases, d->n_aliases);
        fmt_num(d_cd, sizeof d_cd, d->user_cd);
        fmt_num(d_gcd, sizeof d_gcd, d->global_cd);
    }
    if (custom)
        put(e, section, "action", k.action, NULL);
    put(e, section, "data", k.data, NULL);
    /* An empty alias list must be written, or the defaults come back. */
    const char *had = kk_ini_get(e->ini, section, "aliases");
    if (!aliases[0] && d && d->n_aliases) {
        if (!had || had[0]) /* rewriting it would still change the line */
            kk_ini_set(e->ini, section, "aliases", "");
    }
    else {
        put(e, section, "aliases", aliases, d ? d_aliases : NULL);
    }
    put(e, section, "cooldown", cd, d ? d_cd : "0");
    put(e, section, "global_cooldown", gcd, d ? d_gcd : "0");
    put(e, section, "role", kk_role_name(k.role), kk_role_name(d ? d->role : KK_ROLE_ANYONE));
    put(e, section, "enabled", k.enabled ? "yes" : "no", "yes");
    char secs[32];
    if (k.params.seconds > 0)
        fmt_num(secs, sizeof secs, k.params.seconds);
    else
        snprintf(secs, sizeof secs, "auto");
    put(e, section, "seconds", secs, "auto");
    put_num(e, section, "x", k.params.x, 50);
    put_num(e, section, "y", k.params.y, 50);
    put_num(e, section, "scale", k.params.scale, 100);
    /* Against the built-in's own price (!bomb has one), else 0. */
    char cost[32], d_cost[32];
    snprintf(cost, sizeof cost, "%ld", k.cost);
    snprintf(d_cost, sizeof d_cost, "%ld", d ? d->cost : 0);
    put(e, section, "cost", cost, d_cost);
    free_row(&k, aliases);
}

void commands_collect(editor *e)
{
    kk_config d;
    kk_config_defaults(&d);
    put_bool(e, "commands", "help_bubbles", e->help_bubbles, d.help_bubbles);
    put_auto(e, "commands", "help_seconds", e->help_auto, e->help_secs);
    put_int(e, "commands", "help_count", spin_int(e->help_count), d.help_count);
    put_bool(e, "commands", "shortcuts", e->shortcuts, d.shortcuts);
    put_num(e, "commands", "shortcut_cooldown",
            gtk_spin_button_get_value(GTK_SPIN_BUTTON(e->shortcut_cd)), d.shortcut_cd);
    kk_config_free(&d);

    /* Commands that were removed or renamed lose their section. */
    for (int i = 0; i < e->n_loaded_custom; i++)
        if (!name_taken(e, e->loaded_custom[i], NULL)) {
            char section[128];
            snprintf(section, sizeof section, "command.%s", e->loaded_custom[i]);
            kk_ini_remove_section(e->ini, section);
        }
    GtkTreeIter it;
    for (bool more = gtk_tree_model_get_iter_first(GTK_TREE_MODEL(e->commands), &it); more;
         more = gtk_tree_model_iter_next(GTK_TREE_MODEL(e->commands), &it))
        save_command(e, &it);
    /* What is in the file now counts as loaded. */
    remember_custom(e);
}

void commands_free(editor *e)
{
    for (int i = 0; i < e->n_loaded_custom; i++)
        g_free(e->loaded_custom[i]);
    e->n_loaded_custom = 0;
}
