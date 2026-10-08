/* SPDX-License-Identifier: GPL-3.0-or-later */
/* kikarinhas-config: the "Pontos" tab, every way to earn or spend points.
 *
 * Earning is the [points] section; spending is each command's cost and each
 * sound's (the same rows as the Commands and Sounds tabs, shown here with
 * only their price, editable) plus the "!name" shortcut's. */
#include <stdio.h>
#include <stdlib.h>

#include "editor.h"
#include "i18n.h"

struct points_tab {
    GtkWidget *enabled, *name, *start, *message, *message_cd, *watch, *watch_min, *arrival,
        *paid, *member, *shortcut, *catch_pts, *min_bet, *max_bet, *roulette;
};

static GtkWidget *spin_at(double lo, double hi, double step, int digits, double v)
{
    GtkWidget *s = spin(lo, hi, step, digits);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(s), v);
    return s;
}

static GtkWidget *points_spin(long v)
{
    return spin_at(0, 1000000000, 1, 0, (double)v);
}

/* Widgets in a line, with plain text between them. */
static GtkWidget *line(GtkWidget *a, const char *between, GtkWidget *b, const char *after)
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

static GtkWidget *framed_list(GtkWidget *tree, const char *title)
{
    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_NEVER,
                                   GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(scroll), GTK_SHADOW_IN);
    gtk_container_add(GTK_CONTAINER(scroll), tree);
    gtk_widget_set_size_request(scroll, -1, 160);
    GtkWidget *frame = gtk_frame_new(title);
    gtk_container_set_border_width(GTK_CONTAINER(scroll), 6);
    gtk_container_add(GTK_CONTAINER(frame), scroll);
    return frame;
}

GtkWidget *points_page(editor *e, const kk_config *cfg)
{
    points_tab *t = e->points = calloc(1, sizeof *t);
    GtkWidget *box = gtk_vbox_new(FALSE, 6);
    gtk_container_set_border_width(GTK_CONTAINER(box), 12);

    GtkWidget *table = gtk_table_new(1, 2, FALSE);
    gtk_table_set_row_spacings(GTK_TABLE(table), 6);
    gtk_table_set_col_spacings(GTK_TABLE(table), 12);
    t->enabled = row(table, NULL, check(_("Viewers earn and spend points"), cfg->points_enabled),
                     _("Balances are kept in the people file and shown in the Viewers tab, "
                       "where they can be changed by hand. Off: every command is free."));
    t->name = row(table, _("Called"), entry(cfg->points_name ? cfg->points_name : "pontos"),
                  _("How the chat bubbles call them (pontos, moedas, cookies...)."));
    t->start = points_spin(cfg->points_start);
    row(table, _("New viewers start with"), line(t->start, _("points"), NULL, NULL), NULL);

    t->message = points_spin(cfg->points_message);
    t->message_cd = spin_at(0, 86400, 1, 0, cfg->points_message_cd);
    row(table, _("Per message"),
        line(t->message, _("points, at most once every"), t->message_cd, _("seconds")),
        _("Earning: 0 turns a way off."));
    t->watch = points_spin(cfg->points_watch);
    t->watch_min = spin_at(0.5, 1440, 0.5, 1, cfg->points_watch_minutes);
    row(table, _("Watching"), line(t->watch, _("points every"), t->watch_min, _("minutes")),
        _("While their avatar is on screen (they talked within the time set in Chat, "
          "\"Leaves after\")."));
    t->arrival = points_spin(cfg->points_arrival);
    row(table, _("Arrival"), line(t->arrival, _("points for the first message of the session"),
                                  NULL, NULL),
        NULL);
    t->paid = points_spin(cfg->points_paid);
    row(table, _("Super Chat, bits"), line(t->paid, _("points per paid message"), NULL, NULL),
        NULL);
    t->member = points_spin(cfg->points_member);
    row(table, _("New member"), line(t->member, _("points (also subscriptions and gifts)"),
                                     NULL, NULL),
        NULL);
    t->catch_pts = points_spin(cfg->points_catch);
    row(table, _("Catching a star"),
        line(t->catch_pts, _("points (a gem: three times as many)"), NULL, NULL),
        _("The stars of !spawn float above the avatars: whoever jumps into one takes it."));
    t->shortcut = points_spin(cfg->shortcut_cost);
    row(table, _("Shortcut \"!name\""), line(t->shortcut, _("points"), NULL, NULL),
        _("Spending: what changing avatar, accessory or palette by name costs (\"!pikachu\")."));
    t->min_bet = points_spin(cfg->points_min_bet);
    t->max_bet = points_spin(cfg->points_max_bet);
    row(table, _("Bets"), line(t->min_bet, _("to"), t->max_bet, _("points")),
        _("What !slots and !bet accept; a maximum of 0 means no limit."));
    t->roulette = spin_at(5, 600, 1, 0, cfg->roulette_seconds);
    row(table, _("Roulette"), line(t->roulette, _("seconds taking bets, then it spins"), NULL,
                                   NULL),
        NULL);
    gtk_box_pack_start(GTK_BOX(box), table, FALSE, FALSE, 0);

    GtkWidget *lists = gtk_hbox_new(TRUE, 12);
    gtk_box_pack_start(GTK_BOX(lists), framed_list(commands_cost_view(e), _("Commands")), TRUE,
                       TRUE, 0);
    gtk_box_pack_start(GTK_BOX(lists), framed_list(sounds_cost_view(e), _("Sounds")), TRUE, TRUE,
                       0);
    gtk_box_pack_start(GTK_BOX(box), lists, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(box),
                       hint_label(_("Double-click a cost to change it. Points are taken only "
                                    "when the command does something; the channel owner never "
                                    "pays. A sound \"(as !sound)\" costs what !sound costs. "
                                    "!points shows the balance, !give @name N gives some away, "
                                    "!award @name N (moderators) creates them and !top shows "
                                    "the ranking.")),
                       FALSE, FALSE, 0);
    return box;
}

void points_collect(editor *e)
{
    points_tab *t = e->points;
    kk_config d;
    kk_config_defaults(&d);
    put_bool(e, "points", "enabled", t->enabled, d.points_enabled);
    put(e, "points", "name", text_of(t->name), "pontos");
    put_int(e, "points", "start", spin_int(t->start), (int)d.points_start);
    put_int(e, "points", "message", spin_int(t->message), (int)d.points_message);
    put_num(e, "points", "message_cooldown",
            gtk_spin_button_get_value(GTK_SPIN_BUTTON(t->message_cd)), d.points_message_cd);
    put_int(e, "points", "watch", spin_int(t->watch), (int)d.points_watch);
    put_num(e, "points", "watch_minutes",
            gtk_spin_button_get_value(GTK_SPIN_BUTTON(t->watch_min)), d.points_watch_minutes);
    put_int(e, "points", "arrival", spin_int(t->arrival), (int)d.points_arrival);
    put_int(e, "points", "paid", spin_int(t->paid), (int)d.points_paid);
    put_int(e, "points", "member", spin_int(t->member), (int)d.points_member);
    put_int(e, "points", "catch", spin_int(t->catch_pts), (int)d.points_catch);
    put_int(e, "points", "min_bet", spin_int(t->min_bet), (int)d.points_min_bet);
    put_int(e, "points", "max_bet", spin_int(t->max_bet), (int)d.points_max_bet);
    put_num(e, "points", "roulette_seconds",
            gtk_spin_button_get_value(GTK_SPIN_BUTTON(t->roulette)), d.roulette_seconds);
    put_int(e, "commands", "shortcut_cost", spin_int(t->shortcut), (int)d.shortcut_cost);
    kk_config_free(&d);
}

void points_free(editor *e)
{
    free(e->points);
    e->points = NULL;
}
