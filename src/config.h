/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef KK_CONFIG_H
#define KK_CONFIG_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "commands.h"
#include "ini.h"

/* Everything the user can set, from three layers: built-in defaults, then
 * ~/.config/kikarinhas/kikarinhas.ini, then the command line. Reloading
 * redoes all three, so command-line options keep winning.
 *
 * This module only knows data (no X, no avatars), so kikarinhas-config can
 * use it to show the defaults and to check what it writes.
 *
 * The file (every key is optional; see data/kikarinhas.ini):
 *
 *   [window]   mode = obs|desktop, size = 1280x720, fps = 30
 *   [avatars]  scale, ground = auto|N, count = auto|N, show = a, b
 *              default, sa_dir = PATH|off, paths = a, b,
 *              show_names = yes|no, name_idle_hide = yes|no,
 *              name_idle_seconds,
 *              name_position = below|above, show_bubbles = yes|no,
 *              bubble_seconds = auto|N,
 *              name_font, name_size, bubble_font, bubble_size
 *   [chat]     youtube = a, b, twitch = a, b, kick = a, b, odysee = a, b,
 *              third_party_emotes = yes|no,
 *              demo, max, despawn, verbose, users
 *   [control]  socket = PATH|off
 *   [commands] shortcuts = yes|no, shortcut_cooldown = 5, shortcut_cost = 0,
 *              help_bubbles = yes|no, help_count = 3,
 *              help_seconds = auto|N
 *   [command.NAME]
 *              action, data, aliases = a, b, cooldown, global_cooldown,
 *              role = anyone|member|trusted|mod|owner, enabled = yes|no,
 *              seconds = auto|N, x = 50, y = 50, scale = 100 (image),
 *              cost = 0 (points)
 *   [soundboard] enabled, volume = 100 (%), device = default,
 *              voices = 8, commands = yes
 *   [sound.NAME] file, aliases = a, b, volume = 100 (%),
 *              cost = N (points; unset: the sound command's)
 *   [timer.NAME] command = !logo, minutes = 10, messages = 0, viewers = 0,
 *              enabled = yes
 *   [points]   enabled, name = pontos, start = 0, message = 1,
 *              message_cooldown = 30, watch = 5, watch_minutes = 5,
 *              arrival = 10, paid = 100, member = 100, catch = 5,
 *              min_bet = 5, max_bet = 5000 (0: no limit),
 *              roulette_seconds = 20
 *   [emote_wall] enabled, duration = 5, size = 48,
 *              style = rise|bounce|fly,
 *              min_per_message = 3, combo_count = 3, combo_window = 10,
 *              max_per_message = 10, max_on_screen = 150,
 *              reactions = yes, reactions_per_icon = 1,
 *              blacklist = 😂, :_hello:, Kappa
 *   [danmaku]  enabled = no, seconds = 8, font = Sans Bold, size = 24,
 *              color = #ffffff, outline = #000000, paid_color = #ffd24a,
 *              member_color = #8ef09c, opacity = 100 (%),
 *              avoid_avatars = yes, max_on_screen = 100
 *
 * A [command.NAME] section changes a built-in command (only the keys given)
 * or, with action, makes a new one: "action = sound" + "data = buzina"
 * gives a !buzina that plays that sound (data, when set, is the argument
 * instead of what was typed). "aliases" replaces the list. Comments go on
 * their own lines: values may contain "#" (links). */

#define KK_CONFIG_MAX_SHOW 64
#define KK_CONFIG_MAX_ALIASES 8

typedef struct {
    char *name;   /* lowercase, without "!" */
    char *action; /* one of kk_config_actions() */
    char *data;   /* NULL or the action's argument (e.g. a sound) */
    char *aliases[KK_CONFIG_MAX_ALIASES];
    int n_aliases;
    double user_cd, global_cd;
    kk_role role;
    bool enabled;
    kk_cmd_params params; /* seconds, and the place of an image */
    long cost;            /* points */
} kk_config_command;

/* A sound-board clip, played by "!sound NAME" (or one of its aliases) and,
 * with [soundboard] commands = yes, also by "!NAME". */
typedef struct {
    char *name; /* lowercase */
    char *file; /* relative paths are from the config file's folder */
    char *aliases[KK_CONFIG_MAX_ALIASES];
    int n_aliases;
    int volume; /* percent, 0..400 */
    long cost;  /* points; < 0: the sound command's */
} kk_config_sound;

/* A command run by itself now and then (timers.c): when every condition
 * set is met (minutes since the last run, chat messages since then, people
 * on screen); minutes or messages must be set. */
typedef struct {
    char *name;    /* lowercase */
    char *command; /* as typed in the chat: "!sound buzina" */
    double minutes;
    int messages;
    int viewers;
    bool enabled;
} kk_config_timer;

typedef enum {
    KK_CONFIG_WALL_RISE,
    KK_CONFIG_WALL_BOUNCE,
    KK_CONFIG_WALL_FLY,
} kk_config_wall_style;

typedef struct {
    /* [window] */
    bool desktop;
    int width, height;
    int fps;
    /* [avatars] */
    double scale;
    int ground; /* -1 = room for the name tags */
    int count;  /* random avatars outside the chat; -1 = 6 without a chat */
    char *show[KK_CONFIG_MAX_SHOW];
    int n_show;
    char *default_avatar; /* NULL = one per person */
    char *sa_dir;         /* NULL = search the Steam libraries, "" = off */
    char *avatar_paths;   /* more folders, comma separated (kk_library) */
    bool show_names;       /* name tag under (or over) each avatar */
    bool name_above;       /* false: below the feet; true: above the head */
    bool name_idle_hide;   /* show the name only while there is activity */
    int name_idle_seconds; /* quiet time before the name hides */
    bool show_bubbles;     /* speech bubble with the chat message */
    double bubble_seconds; /* how long it stays; -1 = by length */
    char *name_font;       /* Pango family and style; NULL = "Sans Bold" */
    double name_size;      /* points */
    char *bubble_font;     /* NULL = "Sans" */
    double bubble_size;    /* points */
    /* [chat] */
    char *youtube;      /* lives, channels: a list (kk_config_next_target) */
    char *twitch;       /* channel names or links: a list too */
    char *kick;         /* channel names or links */
    char *odysee;       /* links, @channels, claim ids */
    bool extra_emotes;  /* BTTV/FFZ/7TV on Twitch, 7TV on Kick */
    bool demo;
    int max_avatars;
    double despawn;
    bool verbose;
    char *users; /* NULL = default path */
    /* [control] */
    char *socket; /* NULL = default path, "" = off */
    /* [commands] */
    bool help_bubbles; /* !help bubbles, even with show_bubbles = no */
    int help_count;    /* commands listed per help bubble */
    double help_seconds; /* how long it stays; -1 = by the number listed */
    bool shortcuts;
    double shortcut_cd;
    long shortcut_cost;
    kk_config_command *commands;
    int n_commands;
    /* [soundboard] */
    bool sound_enabled;
    int sound_volume; /* master, percent */
    char *sound_device; /* NULL = ALSA "default" */
    int sound_voices;   /* sounds at once */
    bool sound_commands;
    kk_config_sound *sounds;
    int n_sounds;
    /* [emote_wall] */
    bool wall_enabled;
    double wall_duration;    /* seconds on screen */
    int wall_size;           /* pixels */
    kk_config_wall_style wall_style;
    int wall_min_message;    /* emotes in one message; 0 = rule off */
    int wall_combo;          /* messages with the same emote; 0 = rule off */
    double wall_combo_window; /* seconds */
    int wall_max_message;
    int wall_max_screen;
    bool wall_reactions;
    int wall_reactions_per_icon;
    char *wall_blacklist;    /* comma-separated; NULL = none */
    /* [danmaku]: messages fly across instead of bubbles (danmaku.h) */
    bool danmaku_enabled;
    double danmaku_seconds;  /* to cross the window */
    char *danmaku_font;      /* NULL = "Sans Bold" */
    double danmaku_size;     /* points */
    uint32_t danmaku_color, danmaku_outline, danmaku_paid_color,
        danmaku_member_color; /* 0xRRGGBB */
    int danmaku_opacity;     /* percent */
    bool danmaku_avoid;      /* keep off the avatars when it can */
    int danmaku_max;         /* on screen at once */
    /* [timer.NAME] */
    kk_config_timer *timers;
    int n_timers;
    /* [points] (see points.h) */
    bool points_enabled;
    char *points_name;       /* what they are called in bubbles; NULL = "pontos" */
    long points_start;
    long points_message;
    double points_message_cd;
    long points_watch;
    double points_watch_minutes;
    long points_arrival;
    long points_paid;
    long points_member;
    long points_catch;       /* each collectible of !spawn */
    long points_min_bet, points_max_bet; /* !slots and !bet */
    double roulette_seconds; /* a !bet round takes bets this long */
} kk_config;

/* Called for each problem found; line is 0 when not from the file. */
typedef void (*kk_config_warn_fn)(void *ud, int line, const char *msg);

void kk_config_defaults(kk_config *c);
void kk_config_free(kk_config *c);
/* Deep copy; false without memory. */
bool kk_config_copy(kk_config *dst, const kk_config *src);

/* Applies the keys of ini on top of c. Unknown keys and bad values are
 * reported and skipped; the rest still applies. */
void kk_config_apply(kk_config *c, const kk_ini *ini, kk_config_warn_fn warn,
                     void *ud);
/* Reads path and applies it. A missing file is fine (returns 0, *found
 * false); -1 if it exists but can't be read. */
int kk_config_load(kk_config *c, const char *path, bool *found,
                   kk_config_warn_fn warn, void *ud);

/* $XDG_CONFIG_HOME/kikarinhas/kikarinhas.ini. */
bool kk_config_default_path(char *out, size_t size);

/* The built-in commands, in the order shown to people. */
const kk_config_command *kk_config_default_commands(int *n);
/* Names usable in "action =", NULL-terminated. */
const char *const *kk_config_actions(void);

kk_config_command *kk_config_find_command(kk_config *c, const char *name);
/* By name or alias, ignoring ASCII case and a leading "!". */
const kk_config_sound *kk_config_find_sound(const kk_config *c, const char *word);
/* "Buzina Alta.ogg" -> "buzina_alta": a command-safe name; false if
 * nothing usable is left. */
bool kk_config_sound_name(char *out, size_t size, const char *file);

/* youtube, twitch, kick and odysee name several lives at once, separated by commas or
 * white space. Copies the item at *p to out and moves *p past it; false
 * when there is none left. Items that don't fit in out are skipped. */
bool kk_config_next_target(const char **p, char *out, size_t size);
/* Adds item at the end of the list in *list (NULL = empty). */
bool kk_config_append(char **list, const char *item);

/* Setters used by the command line too; they return false on bad input. */
bool kk_config_set_str(char **dst, const char *value);
bool kk_config_set_show(kk_config *c, const char *list); /* "a, b" */
bool kk_parse_size(const char *s, int *w, int *h);
bool kk_parse_bool(const char *s, bool *out);
bool kk_parse_role(const char *s, kk_role *out);
const char *kk_role_name(kk_role r);
bool kk_parse_long(const char *s, long lo, long hi, long *out);
bool kk_parse_double(const char *s, double lo, double hi, double *out);
/* "#rrggbb", "rrggbb" or "#rgb" to 0xRRGGBB. */
bool kk_parse_color(const char *s, uint32_t *out);
/* 0xRRGGBB to "#rrggbb" (out has room for 8 bytes). */
void kk_format_color(char out[8], uint32_t c);

#endif
