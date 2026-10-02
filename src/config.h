/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef KK_CONFIG_H
#define KK_CONFIG_H

#include <stdbool.h>
#include <stddef.h>

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
 *              default, sa_dir, show_names = yes|no,
 *              name_position = below|above, show_bubbles = yes|no,
 *              name_font, name_size, bubble_font, bubble_size
 *   [chat]     youtube, demo, max, despawn, verbose, users
 *   [control]  socket = PATH|off
 *   [commands] shortcuts = yes|no, shortcut_cooldown = 5
 *   [command.NAME]
 *              action, data, aliases = a, b, cooldown, global_cooldown,
 *              role = anyone|member|mod|owner, enabled = yes|no
 *   [soundboard] enabled, volume = 100 (%), device = default,
 *              voices = 8, commands = yes
 *   [sound.NAME] file, aliases = a, b, volume = 100 (%)
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
} kk_config_command;

/* A sound-board clip, played by "!sound NAME" (or one of its aliases) and,
 * with [soundboard] commands = yes, also by "!NAME". */
typedef struct {
    char *name; /* lowercase */
    char *file; /* relative paths are from the config file's folder */
    char *aliases[KK_CONFIG_MAX_ALIASES];
    int n_aliases;
    int volume; /* percent, 0..400 */
} kk_config_sound;

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
    char *sa_dir;         /* NULL = search the Steam libraries */
    bool show_names;       /* name tag under (or over) each avatar */
    bool name_above;       /* false: below the feet; true: above the head */
    bool show_bubbles;     /* speech bubble with the chat message */
    char *name_font;       /* Pango family and style; NULL = "Sans Bold" */
    double name_size;      /* points */
    char *bubble_font;     /* NULL = "Sans" */
    double bubble_size;    /* points */
    /* [chat] */
    char *youtube;
    bool demo;
    int max_avatars;
    double despawn;
    bool verbose;
    char *users; /* NULL = default path */
    /* [control] */
    char *socket; /* NULL = default path, "" = off */
    /* [commands] */
    bool shortcuts;
    double shortcut_cd;
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

/* Setters used by the command line too; they return false on bad input. */
bool kk_config_set_str(char **dst, const char *value);
bool kk_config_set_show(kk_config *c, const char *list); /* "a, b" */
bool kk_parse_size(const char *s, int *w, int *h);
bool kk_parse_bool(const char *s, bool *out);
bool kk_parse_role(const char *s, kk_role *out);
const char *kk_role_name(kk_role r);
bool kk_parse_long(const char *s, long lo, long hi, long *out);
bool kk_parse_double(const char *s, double lo, double hi, double *out);

#endif
