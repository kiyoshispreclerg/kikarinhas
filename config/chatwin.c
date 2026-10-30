/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The "Chat window" tab of kikarinhas-config: the [chat_window] section and
 * a button that shows or hides the window in the running kikarinhas. The
 * look of the bubbles style comes from the Avatars tab, so the own-style
 * settings are greyed out while it is chosen. */
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "editor.h"
#include "i18n.h"

struct chatwin_tab {
    GtkWidget *show, *open, *width, *height, *resizable, *above, *style, *font, *size,
        *text, *name, *badges, *outline, *outline_w, *paid, *member, *background, *bg_color,
        *bg_image, *bg_mode, *platform, *photo, *time, *commands, *hide_users, *fade, *newest,
        *history, *hl_card, *hl_x, *hl_y, *hl_width, *hl_font, *hl_size, *hl_seconds,
        *hl_spot;
    GtkWidget *own_rows[8]; /* the own style's settings, greyed out together */
    int n_own;
};

static GtkWidget *spin_at(double lo, double hi, double step, int digits, double v)
{
    GtkWidget *s = spin(lo, hi, step, digits);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(s), v);
    return s;
}

static GtkWidget *with_unit(GtkWidget *w, const char *unit)
{
    GtkWidget *box = gtk_hbox_new(FALSE, 6);
    gtk_box_pack_start(GTK_BOX(box), w, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), gtk_label_new(unit), FALSE, FALSE, 0);
    return box;
}

/* c is 0xRRGGBB, or 0xRRGGBBAA with alpha. */
static GtkWidget *color_button(uint32_t c, bool alpha)
{
    uint32_t rgb = alpha ? c >> 8 : c;
    GdkColor g = {0, (guint16)(((rgb >> 16) & 0xff) * 257), (guint16)(((rgb >> 8) & 0xff) * 257),
                  (guint16)((rgb & 0xff) * 257)};
    GtkWidget *b = gtk_color_button_new_with_color(&g);
    if (alpha) {
        gtk_color_button_set_use_alpha(GTK_COLOR_BUTTON(b), TRUE);
        gtk_color_button_set_alpha(GTK_COLOR_BUTTON(b), (guint16)((c & 0xff) * 257));
    }
    return b;
}

static uint32_t color_of(GtkWidget *b)
{
    GdkColor g;
    gtk_color_button_get_color(GTK_COLOR_BUTTON(b), &g);
    return (uint32_t)(g.red >> 8) << 16 | (uint32_t)(g.green >> 8) << 8 | (uint32_t)(g.blue >> 8);
}

/* Keeps w at its own size at the left of a table cell. */
static GtkWidget *left(GtkWidget *w)
{
    GtkWidget *box = gtk_hbox_new(FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), w, FALSE, FALSE, 0);
    return box;
}

static GtkWidget *combo(const char *const *items, int n, int active)
{
    GtkWidget *c = gtk_combo_box_text_new();
    for (int i = 0; i < n; i++)
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(c), _(items[i]));
    gtk_combo_box_set_active(GTK_COMBO_BOX(c), active);
    return c;
}

static int active(GtkWidget *c)
{
    return gtk_combo_box_get_active(GTK_COMBO_BOX(c));
}

static void put_color(editor *e, const char *key, GtkWidget *b, uint32_t dflt)
{
    char v[8], d[8];
    kk_format_color(v, color_of(b));
    kk_format_color(d, dflt);
    put(e, "chat_window", key, v, d);
}

/* ---- the show/hide button -------------------------------------------------- */

static void show_state(chatwin_tab *w, bool known, bool open)
{
    gtk_button_set_label(GTK_BUTTON(w->show),
                         known && open ? _("Hide the chat window") : _("Show the chat window"));
}

/* Sends action ("state", "toggle"...) to kikarinhas; false if it is not
 * running. */
static bool request(editor *e, const char *action, bool *open)
{
    char line[96], reply[512];
    snprintf(line, sizeof line, "{\"type\":\"chat_window\",\"action\":\"%s\"}", action);
    if (!editor_request(e, line, reply, sizeof reply))
        return false;
    cJSON *r = cJSON_Parse(reply);
    *open = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(r, "open"));
    cJSON_Delete(r);
    return true;
}

static void on_show(GtkButton *b, gpointer ud)
{
    (void)b;
    editor *e = ud;
    bool open = false;
    if (!request(e, "toggle", &open)) {
        status(e, _("kikarinhas is not running: the chat window opens with it (Open with "
                    "kikarinhas, or --chat-window)."));
        show_state(e->chatwin, false, false);
    } else {
        status(e, open ? _("Chat window open. In OBS: Window Capture, \"Kikarinhas — chat\".")
                       : _("Chat window closed."));
        show_state(e->chatwin, true, open);
    }
    editor_refresh_kikarinhas_version(e);
}

/* The button tells what it will do, so ask when the tab shows up. */
static void on_map(GtkWidget *widget, gpointer ud)
{
    (void)widget;
    editor *e = ud;
    bool open = false;
    bool known = request(e, "state", &open);
    show_state(e->chatwin, known, open);
}

/* ---- greying out ------------------------------------------------------------ */

static void on_style(GtkComboBox *c, gpointer ud)
{
    chatwin_tab *w = ud;
    for (int i = 0; i < w->n_own; i++)
        gtk_widget_set_sensitive(w->own_rows[i], gtk_combo_box_get_active(c) == 0);
}

static void on_background(GtkComboBox *c, gpointer ud)
{
    chatwin_tab *w = ud;
    int bg = gtk_combo_box_get_active(c);
    gtk_widget_set_sensitive(w->bg_color, bg == 1);
    gtk_widget_set_sensitive(w->bg_image, bg == 2);
    gtk_widget_set_sensitive(w->bg_mode, bg == 2);
}

/* ---- the page ---------------------------------------------------------------- */

GtkWidget *chatwin_page(editor *e, const kk_config *cfg)
{
    chatwin_tab *w = calloc(1, sizeof *w);
    e->chatwin = w;

    GtkWidget *box = gtk_vbox_new(FALSE, 6);
    gtk_container_set_border_width(GTK_CONTAINER(box), 12);
    gtk_box_pack_start(GTK_BOX(box),
                       hint_label(_("Every live's chat in one list, in a window of its own: to "
                                    "read during the stream and for OBS to capture (Window "
                                    "Capture, \"Kikarinhas — chat\", with Allow transparency) "
                                    "instead of a browser-source chat. Close it with the "
                                    "window's X; what it drew is freed, the messages are kept.")),
                       FALSE, FALSE, 0);
    w->show = gtk_button_new_with_label(_("Show the chat window"));
    gtk_box_pack_start(GTK_BOX(box), left(w->show), FALSE, FALSE, 0);
    g_signal_connect(w->show, "clicked", G_CALLBACK(on_show), e);
    g_signal_connect(box, "map", G_CALLBACK(on_map), e);

    GtkWidget *t = gtk_table_new(1, 2, FALSE);
    gtk_table_set_row_spacings(GTK_TABLE(t), 6);
    gtk_table_set_col_spacings(GTK_TABLE(t), 12);

    w->open = row(t, NULL, check(_("Open with kikarinhas"), cfg->chatwin_open), NULL);
    GtkWidget *size = gtk_hbox_new(FALSE, 6);
    w->width = spin_at(100, 8192, 10, 0, cfg->chatwin_width);
    w->height = spin_at(100, 8192, 10, 0, cfg->chatwin_height);
    gtk_box_pack_start(GTK_BOX(size), w->width, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(size), gtk_label_new("×"), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(size), w->height, FALSE, FALSE, 0);
    row(t, _("Size"), size, NULL);
    w->resizable = row(t, NULL, check(_("Resizable"), cfg->chatwin_resizable),
                       _("Off: always this size, which helps keep the OBS capture in place. "
                         "On: a size given with the mouse lasts until kikarinhas closes."));
    w->above = row(t, NULL, check(_("Keep over the other windows"), cfg->chatwin_above), NULL);

    static const char *const styles[] = {N_("Own (below)"), N_("Like the speech bubbles")};
    w->style = combo(styles, 2, cfg->chatwin_bubbles ? 1 : 0);
    row(t, _("Style"), left(w->style),
        _("Like the bubbles: each message in a white balloon, with the bubble font of the "
          "Avatars tab."));

    /* The own style: greyed out with the bubbles'. */
    w->font = entry(cfg->chatwin_font);
    w->size = spin_at(4, 200, 1, 1, cfg->chatwin_size);
    GtkWidget *font = gtk_hbox_new(FALSE, 6);
    gtk_box_pack_start(GTK_BOX(font), w->font, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(font), w->size, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(font), gtk_label_new("pt"), FALSE, FALSE, 0);
    w->own_rows[w->n_own++] = row(t, _("Font"), font,
                                  _("Pango family and style (e.g. Sans) and size; empty: Sans."));
    w->text = color_button(cfg->chatwin_text_color, false);
    w->own_rows[w->n_own++] = row(t, _("Text color"), left(w->text), NULL);
    w->name = color_button(cfg->chatwin_name_color, false);
    w->badges = check(_("owner, mods and members in the name tags' colours"),
                      cfg->chatwin_badge_colors);
    GtkWidget *names = gtk_hbox_new(FALSE, 6);
    gtk_box_pack_start(GTK_BOX(names), w->name, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(names), w->badges, FALSE, FALSE, 0);
    w->own_rows[w->n_own++] = row(t, _("Names"), names, NULL);
    w->outline = color_button(cfg->chatwin_outline, false);
    w->outline_w = spin_at(0, 20, 1, 0, cfg->chatwin_outline_width);
    GtkWidget *outline = gtk_hbox_new(FALSE, 6);
    gtk_box_pack_start(GTK_BOX(outline), w->outline, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(outline), with_unit(w->outline_w, "px"), FALSE, FALSE, 0);
    w->own_rows[w->n_own++] =
        row(t, _("Outline"), outline,
            _("Around the letters, so they read over anything on a transparent background; 0: "
              "none."));
    w->paid = color_button(cfg->chatwin_paid_color, false);
    w->own_rows[w->n_own++] = row(t, _("Paid messages"), left(w->paid),
                                  _("Super Chats, bits, Kicks...: a tinted box."));
    w->member = color_button(cfg->chatwin_member_color, false);
    w->own_rows[w->n_own++] = row(t, _("New members"), left(w->member), NULL);

    static const char *const backgrounds[] = {N_("Transparent"), N_("Color"), N_("Picture")};
    w->background = combo(backgrounds, 3, (int)cfg->chatwin_background);
    row(t, _("Background"), left(w->background), NULL);
    w->bg_color = color_button(cfg->chatwin_bg_color, true);
    row(t, _("Background color"), left(w->bg_color),
        _("With transparency: OBS captures it too (Allow transparency)."));
    w->bg_image = gtk_file_chooser_button_new(_("Picture"), GTK_FILE_CHOOSER_ACTION_OPEN);
    GtkFileFilter *f = gtk_file_filter_new();
    gtk_file_filter_set_name(f, _("Pictures (PNG, JPEG, GIF, BMP)"));
    static const char *const pats[] = {"*.png", "*.PNG", "*.jpg", "*.JPG", "*.jpeg",
                                       "*.JPEG", "*.gif", "*.GIF", "*.bmp", "*.BMP"};
    for (size_t i = 0; i < sizeof pats / sizeof pats[0]; i++)
        gtk_file_filter_add_pattern(f, pats[i]);
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(w->bg_image), f);
    if (cfg->chatwin_bg_image)
        gtk_file_chooser_set_filename(GTK_FILE_CHOOSER(w->bg_image), cfg->chatwin_bg_image);
    static const char *const modes[] = {N_("Stretched to the window"), N_("Repeated (tiles)")};
    w->bg_mode = combo(modes, 2, cfg->chatwin_bg_tile ? 1 : 0);
    GtkWidget *picture = gtk_hbox_new(FALSE, 6);
    gtk_box_pack_start(GTK_BOX(picture), w->bg_image, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(picture), w->bg_mode, FALSE, FALSE, 0);
    row(t, _("Background picture"), picture, NULL);

    w->platform = row(t, _("Show"),
                      check(_("the platform's icon"), cfg->chatwin_show_platform), NULL);
    w->photo = row(t, NULL,
                   check(_("the viewer's profile picture"), cfg->chatwin_show_photo),
                   _("As on the platform. Twitch, Kick and Odysee need a request for each "
                     "person; it is kept for a week."));
    w->time = row(t, NULL, check(_("the time of each message"), cfg->chatwin_show_time), NULL);
    w->commands = row(t, _("Hide"), check(_("chat commands (!dance, !hug...)"),
                                          cfg->chatwin_hide_commands),
                      NULL);
    w->hide_users = row(t, _("Hide people"), entry(cfg->chatwin_hide_users),
                        _("Names separated by commas, e.g. bots: Nightbot, StreamElements."));
    w->fade = spin_at(0, 3600, 5, 0, cfg->chatwin_fade);
    row(t, _("Messages go after"), with_unit(w->fade, _("seconds")),
        _("0: they stay. Scrolling back shows them all again."));
    static const char *const newest[] = {N_("At the bottom"), N_("At the top")};
    w->newest = combo(newest, 2, cfg->chatwin_newest_top ? 1 : 0);
    row(t, _("New messages"), left(w->newest), NULL);
    w->history = spin_at(10, 5000, 10, 0, cfg->chatwin_history);
    row(t, _("Keeps"), with_unit(w->history, _("messages")),
        _("Also while the window is closed, so it opens with the chat so far."));

    gtk_box_pack_start(GTK_BOX(box), t, FALSE, FALSE, 6);

    /* The highlighted comment. */
    GtkWidget *title = gtk_label_new(NULL);
    gchar *markup = g_markup_printf_escaped("<b>%s</b>", _("Highlighted comment"));
    gtk_label_set_markup(GTK_LABEL(title), markup);
    g_free(markup);
    gtk_misc_set_alignment(GTK_MISC(title), 0, 0.5);
    gtk_box_pack_start(GTK_BOX(box), title, FALSE, FALSE, 6);
    gtk_box_pack_start(GTK_BOX(box),
                       hint_label(_("Click a message in the chat window to show it big on the "
                                    "stage, with a spotlight on its author's avatar (who comes "
                                    "back if they had left); click it again to take it away. "
                                    "In the chat window it only gets a discreet mark.")),
                       FALSE, FALSE, 0);
    GtkWidget *ht = gtk_table_new(1, 2, FALSE);
    gtk_table_set_row_spacings(GTK_TABLE(ht), 6);
    gtk_table_set_col_spacings(GTK_TABLE(ht), 12);
    w->hl_card = row(ht, NULL, check(_("Show the comment in a card on the stage"), cfg->hl_card),
                     NULL);
    w->hl_x = spin_at(0, 100, 1, 0, cfg->hl_x);
    w->hl_y = spin_at(0, 100, 1, 0, cfg->hl_y);
    GtkWidget *where = gtk_hbox_new(FALSE, 6);
    gtk_box_pack_start(GTK_BOX(where), gtk_label_new("x"), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(where), with_unit(w->hl_x, "%"), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(where), gtk_label_new("y"), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(where), with_unit(w->hl_y, "%"), FALSE, FALSE, 0);
    row(ht, _("Centre at"), where,
        _("Of the stage window: x 50 and y 25 is the middle, a quarter of the way down."));
    w->hl_width = spin_at(10, 100, 5, 0, cfg->hl_width);
    row(ht, _("Up to"),
        /* xgettext:no-c-format */
        with_unit(w->hl_width, _("% of the window width")), NULL);
    w->hl_font = entry(cfg->hl_font);
    w->hl_size = spin_at(4, 200, 1, 1, cfg->hl_size);
    GtkWidget *hfont = gtk_hbox_new(FALSE, 6);
    gtk_box_pack_start(GTK_BOX(hfont), w->hl_font, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(hfont), w->hl_size, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(hfont), gtk_label_new("pt"), FALSE, FALSE, 0);
    row(ht, _("Font"), hfont, NULL);
    w->hl_seconds = spin_at(0, 3600, 5, 0, cfg->hl_seconds);
    row(ht, _("Goes after"), with_unit(w->hl_seconds, _("seconds")),
        _("0: stays until clicked again."));
    w->hl_spot = row(ht, NULL, check(_("Spotlight on the author's avatar"), cfg->hl_spotlight),
                     NULL);
    gtk_box_pack_start(GTK_BOX(box), ht, FALSE, FALSE, 6);

    g_signal_connect(w->style, "changed", G_CALLBACK(on_style), w);
    on_style(GTK_COMBO_BOX(w->style), w);
    g_signal_connect(w->background, "changed", G_CALLBACK(on_background), w);
    on_background(GTK_COMBO_BOX(w->background), w);

    GtkWidget *align = gtk_alignment_new(0, 0, 1, 0);
    gtk_container_add(GTK_CONTAINER(align), box);
    return align;
}

void chatwin_collect(editor *e)
{
    chatwin_tab *w = e->chatwin;
    kk_config d;
    kk_config_defaults(&d);
    const char *s = "chat_window";
    put_bool(e, s, "open", w->open, d.chatwin_open);
    char size[32], dsize[32];
    snprintf(size, sizeof size, "%dx%d", spin_int(w->width), spin_int(w->height));
    snprintf(dsize, sizeof dsize, "%dx%d", d.chatwin_width, d.chatwin_height);
    put(e, s, "size", size, dsize);
    put_bool(e, s, "resizable", w->resizable, d.chatwin_resizable);
    put_bool(e, s, "above", w->above, d.chatwin_above);
    put(e, s, "style", active(w->style) == 1 ? "bubbles" : "own", "own");
    put(e, s, "font", text_of(w->font), NULL);
    put_num(e, s, "font_size", gtk_spin_button_get_value(GTK_SPIN_BUTTON(w->size)),
            d.chatwin_size);
    put_color(e, "text_color", w->text, d.chatwin_text_color);
    put_color(e, "name_color", w->name, d.chatwin_name_color);
    put_bool(e, s, "badge_colors", w->badges, d.chatwin_badge_colors);
    put_color(e, "outline", w->outline, d.chatwin_outline);
    put_int(e, s, "outline_width", spin_int(w->outline_w), d.chatwin_outline_width);
    put_color(e, "paid_color", w->paid, d.chatwin_paid_color);
    put_color(e, "member_color", w->member, d.chatwin_member_color);
    static const char *const bgs[] = {"transparent", "color", "image"};
    int bg = active(w->background);
    put(e, s, "background", bgs[bg < 0 || bg > 2 ? 0 : bg], "transparent");
    char rgba[10], drgba[10];
    kk_format_rgba(rgba, color_of(w->bg_color) << 8 |
                             gtk_color_button_get_alpha(GTK_COLOR_BUTTON(w->bg_color)) >> 8);
    kk_format_rgba(drgba, d.chatwin_bg_color);
    put(e, s, "background_color", rgba, drgba);
    gchar *file = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(w->bg_image));
    put(e, s, "background_image", file ? file : "", NULL);
    g_free(file);
    put(e, s, "background_mode", active(w->bg_mode) == 1 ? "tile" : "stretch", "stretch");
    put_bool(e, s, "show_platform", w->platform, d.chatwin_show_platform);
    put_bool(e, s, "show_photo", w->photo, d.chatwin_show_photo);
    put_bool(e, s, "show_time", w->time, d.chatwin_show_time);
    put_bool(e, s, "hide_commands", w->commands, d.chatwin_hide_commands);
    put(e, s, "hide_users", text_of(w->hide_users), NULL);
    put_int(e, s, "fade", spin_int(w->fade), (int)d.chatwin_fade);
    put(e, s, "newest", active(w->newest) == 1 ? "top" : "bottom", "bottom");
    put_int(e, s, "history", spin_int(w->history), d.chatwin_history);
    s = "highlight";
    put_bool(e, s, "card", w->hl_card, d.hl_card);
    put_num(e, s, "x", gtk_spin_button_get_value(GTK_SPIN_BUTTON(w->hl_x)), d.hl_x);
    put_num(e, s, "y", gtk_spin_button_get_value(GTK_SPIN_BUTTON(w->hl_y)), d.hl_y);
    put_num(e, s, "width", gtk_spin_button_get_value(GTK_SPIN_BUTTON(w->hl_width)), d.hl_width);
    put(e, s, "font", text_of(w->hl_font), NULL);
    put_num(e, s, "font_size", gtk_spin_button_get_value(GTK_SPIN_BUTTON(w->hl_size)),
            d.hl_size);
    put_num(e, s, "seconds", gtk_spin_button_get_value(GTK_SPIN_BUTTON(w->hl_seconds)),
            d.hl_seconds);
    put_bool(e, s, "spotlight", w->hl_spot, d.hl_spotlight);
    kk_config_free(&d);
}

void chatwin_free(editor *e)
{
    free(e->chatwin);
    e->chatwin = NULL;
}
