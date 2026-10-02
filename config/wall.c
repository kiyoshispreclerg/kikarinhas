/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The "Emote wall" tab of kikarinhas-config: the [emote_wall] section. */
#include <stdlib.h>

#include "editor.h"
#include "i18n.h"

struct wall_tab {
    GtkWidget *enabled, *style, *duration, *size, *min_message, *combo,
        *combo_window, *max_message, *max_screen, *reactions, *per_icon,
        *blacklist;
};

/* Same order as kk_config_wall_style. */
static const char *const STYLE_NAMES[] = {"rise", "bounce", "fly"};
static const char *const STYLE_LABELS[] = {
    N_("Rise in waves"),
    N_("Bounce off the edges (DVD logo)"),
    N_("Fly across the screen"),
};

static GtkWidget *spin_at(double lo, double hi, double step, int digits, double v)
{
    GtkWidget *s = spin(lo, hi, step, digits);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(s), v);
    return s;
}

/* Widgets in a line, with plain text between them. */
static GtkWidget *line(GtkWidget *a, const char *between, GtkWidget *b,
                       const char *after)
{
    GtkWidget *box = gtk_hbox_new(FALSE, 6);
    gtk_box_pack_start(GTK_BOX(box), a, FALSE, FALSE, 0);
    if (between)
        gtk_box_pack_start(GTK_BOX(box), gtk_label_new(between), FALSE, FALSE, 0);
    if (b)
        gtk_box_pack_start(GTK_BOX(box), b, FALSE, FALSE, 0);
    if (after)
        gtk_box_pack_start(GTK_BOX(box), gtk_label_new(after), FALSE, FALSE, 0);
    return box;
}

GtkWidget *wall_page(editor *e, const kk_config *cfg)
{
    wall_tab *w = calloc(1, sizeof *w);
    e->wall = w;

    GtkWidget *table = gtk_table_new(1, 2, FALSE);
    gtk_table_set_row_spacings(GTK_TABLE(table), 6);
    gtk_table_set_col_spacings(GTK_TABLE(table), 12);
    gtk_container_set_border_width(GTK_CONTAINER(table), 12);

    w->enabled = row(table, NULL,
                     check(_("Emoji and emotes from the chat fly over the stage"),
                           cfg->wall_enabled),
                     _("Common emoji come from the colour emoji font (Noto Color Emoji); "
                       "YouTube member emotes and Twitch emotes (also BTTV, FFZ and 7TV) are "
                       "downloaded and kept in "
                       "~/.cache/kikarinhas/emotes."));

    w->style = gtk_combo_box_text_new();
    for (int i = 0; i < 3; i++)
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(w->style), _(STYLE_LABELS[i]));
    gtk_combo_box_set_active(GTK_COMBO_BOX(w->style), (int)cfg->wall_style);
    row(table, _("Effect"), w->style, NULL);

    w->duration = spin_at(0.5, 60, 0.5, 1, cfg->wall_duration);
    row(table, _("On screen for"), line(w->duration, _("seconds"), NULL, NULL), NULL);
    w->size = spin_at(8, 512, 1, 0, cfg->wall_size);
    row(table, _("Size"), line(w->size, _("pixels"), NULL, NULL), NULL);

    w->min_message = spin_at(0, 64, 1, 0, cfg->wall_min_message);
    row(table, _("Per message"), line(w->min_message, _("emoji or more"), NULL, NULL),
        _("A message with at least this many sends all of them up. 0: rule off."));
    w->combo = spin_at(0, 50, 1, 0, cfg->wall_combo);
    w->combo_window = spin_at(1, 600, 1, 0, cfg->wall_combo_window);
    row(table, _("Combo"),
        line(w->combo, _("messages with the same emoji within"), w->combo_window,
             _("seconds")),
        _("Starts a combo: those go up together, and so does every further message "
          "with it while it lasts. 0: rule off. One rule is enough; with both off, "
          "every emoji goes up."));
    w->max_message = spin_at(1, 64, 1, 0, cfg->wall_max_message);
    row(table, _("At most"), line(w->max_message, _("per message"), NULL, NULL), NULL);
    w->max_screen = spin_at(1, 1000, 1, 0, cfg->wall_max_screen);
    row(table, NULL, line(w->max_screen, _("on screen at once"), NULL, NULL), NULL);

    w->reactions = check(_("YouTube reactions (the floating hearts)"), cfg->wall_reactions);
    w->per_icon = spin_at(1, 1000, 1, 0, cfg->wall_reactions_per_icon);
    row(table, _("Reactions"), line(w->reactions, _("one icon every"), w->per_icon,
                                    _("reactions")),
        NULL);

    w->blacklist = entry(cfg->wall_blacklist);
    row(table, _("Never show"), w->blacklist,
        _("Emoji, shortcuts (:_hello:) or emote names, separated by commas."));

    GtkWidget *align = gtk_alignment_new(0, 0, 1, 0);
    gtk_container_add(GTK_CONTAINER(align), table);
    return align;
}

void wall_collect(editor *e)
{
    wall_tab *w = e->wall;
    kk_config d;
    kk_config_defaults(&d);
    put_bool(e, "emote_wall", "enabled", w->enabled, d.wall_enabled);
    int style = gtk_combo_box_get_active(GTK_COMBO_BOX(w->style));
    put(e, "emote_wall", "style", STYLE_NAMES[style >= 0 && style < 3 ? style : 0],
        STYLE_NAMES[d.wall_style]);
    put_num(e, "emote_wall", "duration",
            gtk_spin_button_get_value(GTK_SPIN_BUTTON(w->duration)), d.wall_duration);
    put_int(e, "emote_wall", "size", spin_int(w->size), d.wall_size);
    put_int(e, "emote_wall", "min_per_message", spin_int(w->min_message), d.wall_min_message);
    put_int(e, "emote_wall", "combo_count", spin_int(w->combo), d.wall_combo);
    put_num(e, "emote_wall", "combo_window",
            gtk_spin_button_get_value(GTK_SPIN_BUTTON(w->combo_window)), d.wall_combo_window);
    put_int(e, "emote_wall", "max_per_message", spin_int(w->max_message), d.wall_max_message);
    put_int(e, "emote_wall", "max_on_screen", spin_int(w->max_screen), d.wall_max_screen);
    put_bool(e, "emote_wall", "reactions", w->reactions, d.wall_reactions);
    put_int(e, "emote_wall", "reactions_per_icon", spin_int(w->per_icon),
            d.wall_reactions_per_icon);
    put(e, "emote_wall", "blacklist", text_of(w->blacklist), NULL);
    kk_config_free(&d);
}

void wall_free(editor *e)
{
    free(e->wall);
    e->wall = NULL;
}
