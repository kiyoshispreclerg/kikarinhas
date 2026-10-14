/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The "Danmaku" tab of kikarinhas-config: the [danmaku] section. Its
 * settings are greyed out while danmaku is off. */
#include <stdlib.h>

#include "editor.h"
#include "i18n.h"

struct danmaku_tab {
    GtkWidget *enabled, *options, *seconds, *font, *size, *color, *outline, *paid,
        *member, *opacity, *avoid, *max;
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

static GtkWidget *color_button(uint32_t c)
{
    GdkColor g = {0, (guint16)(((c >> 16) & 0xff) * 257), (guint16)(((c >> 8) & 0xff) * 257),
                  (guint16)((c & 0xff) * 257)};
    return gtk_color_button_new_with_color(&g);
}

/* Keeps w at its own size at the left of a table cell. */
static GtkWidget *left(GtkWidget *w)
{
    GtkWidget *box = gtk_hbox_new(FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), w, FALSE, FALSE, 0);
    return box;
}

static uint32_t color_of(GtkWidget *b)
{
    GdkColor g;
    gtk_color_button_get_color(GTK_COLOR_BUTTON(b), &g);
    return (uint32_t)(g.red >> 8) << 16 | (uint32_t)(g.green >> 8) << 8 | (uint32_t)(g.blue >> 8);
}

static void put_color(editor *e, const char *key, GtkWidget *b, uint32_t dflt)
{
    char v[8], d[8];
    kk_format_color(v, color_of(b));
    kk_format_color(d, dflt);
    put(e, "danmaku", key, v, d);
}

static void on_enabled(GtkToggleButton *b, gpointer options)
{
    gtk_widget_set_sensitive(GTK_WIDGET(options), gtk_toggle_button_get_active(b));
}

GtkWidget *danmaku_page(editor *e, const kk_config *cfg)
{
    danmaku_tab *w = calloc(1, sizeof *w);
    e->danmaku = w;

    GtkWidget *box = gtk_vbox_new(FALSE, 6);
    gtk_container_set_border_width(GTK_CONTAINER(box), 12);
    w->enabled = check(_("Chat messages fly across the screen (danmaku)"), cfg->danmaku_enabled);
    gtk_box_pack_start(GTK_BOX(box), w->enabled, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box),
                       hint_label(_("Only the text, from right to left, at a random height, "
                                    "like on Niconico. While it is on, chat messages get no "
                                    "speech bubble; the avatar still jumps. Help and text "
                                    "commands keep their bubbles.")),
                       FALSE, FALSE, 0);

    GtkWidget *t = gtk_table_new(1, 2, FALSE);
    gtk_table_set_row_spacings(GTK_TABLE(t), 6);
    gtk_table_set_col_spacings(GTK_TABLE(t), 12);
    w->options = t;

    w->seconds = spin_at(1, 60, 0.5, 1, cfg->danmaku_seconds);
    row(t, _("Crosses the screen in"), with_unit(w->seconds, _("seconds")),
        _("Every message takes this long, so longer ones go faster."));

    w->font = entry(cfg->danmaku_font);
    w->size = spin_at(4, 200, 1, 1, cfg->danmaku_size);
    GtkWidget *font = gtk_hbox_new(FALSE, 6);
    gtk_box_pack_start(GTK_BOX(font), w->font, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(font), w->size, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(font), gtk_label_new("pt"), FALSE, FALSE, 0);
    row(t, _("Font"), font,
        _("Pango family and style (e.g. Sans Bold) and size in points; empty: Sans Bold."));

    w->color = color_button(cfg->danmaku_color);
    w->outline = color_button(cfg->danmaku_outline);
    GtkWidget *colors = gtk_hbox_new(FALSE, 6);
    gtk_box_pack_start(GTK_BOX(colors), w->color, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(colors), gtk_label_new(_("outline")), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(colors), w->outline, FALSE, FALSE, 0);
    row(t, _("Text color"), colors, NULL);
    w->paid = color_button(cfg->danmaku_paid_color);
    row(t, _("Paid messages"), left(w->paid), _("Super Chats, bits, Kicks...; the amount goes first."));
    w->member = color_button(cfg->danmaku_member_color);
    row(t, _("New members"), left(w->member), NULL);
    w->opacity = spin_at(10, 100, 5, 0, cfg->danmaku_opacity);
    row(t, _("Opacity"), with_unit(w->opacity, "%"), NULL);

    w->avoid = row(t, NULL, check(_("Keep off the avatars when there is room"), cfg->danmaku_avoid),
                   _("Among the free lines, the ones that cross the avatars least; "
                     "with every line taken, it goes over them anyway."));
    w->max = spin_at(1, 1000, 1, 0, cfg->danmaku_max);
    row(t, _("At most"), with_unit(w->max, _("on screen at once")),
        _("Past this, new messages are not shown until there is room."));

    gtk_box_pack_start(GTK_BOX(box), t, FALSE, FALSE, 6);
    gtk_widget_set_sensitive(t, cfg->danmaku_enabled);
    g_signal_connect(w->enabled, "toggled", G_CALLBACK(on_enabled), t);

    GtkWidget *align = gtk_alignment_new(0, 0, 1, 0);
    gtk_container_add(GTK_CONTAINER(align), box);
    return align;
}

void danmaku_collect(editor *e)
{
    danmaku_tab *w = e->danmaku;
    kk_config d;
    kk_config_defaults(&d);
    put_bool(e, "danmaku", "enabled", w->enabled, d.danmaku_enabled);
    put_num(e, "danmaku", "seconds", gtk_spin_button_get_value(GTK_SPIN_BUTTON(w->seconds)),
            d.danmaku_seconds);
    put(e, "danmaku", "font", text_of(w->font), NULL);
    put_num(e, "danmaku", "size", gtk_spin_button_get_value(GTK_SPIN_BUTTON(w->size)),
            d.danmaku_size);
    put_color(e, "color", w->color, d.danmaku_color);
    put_color(e, "outline", w->outline, d.danmaku_outline);
    put_color(e, "paid_color", w->paid, d.danmaku_paid_color);
    put_color(e, "member_color", w->member, d.danmaku_member_color);
    put_int(e, "danmaku", "opacity", spin_int(w->opacity), d.danmaku_opacity);
    put_bool(e, "danmaku", "avoid_avatars", w->avoid, d.danmaku_avoid);
    put_int(e, "danmaku", "max_on_screen", spin_int(w->max), d.danmaku_max);
    kk_config_free(&d);
}

void danmaku_free(editor *e)
{
    free(e->danmaku);
    e->danmaku = NULL;
}
