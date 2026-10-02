/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "config.h"

#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "util.h"

#define CMD_PREFIX "command."
#define SOUND_PREFIX "sound."
#define MAX_NAME 64

static const char *const ACTIONS[] = {
    "avatar", "color", "gear", "jump", "sit", "dance",
    "emote", "hug", "attack", "sound", NULL,
};

/* Cooldowns follow Stream Avatars' defaults where it has one. */
#define CMD(n, ucd, gcd, ...)                                                  \
    {.name = (char *)n, .action = (char *)n, .aliases = {__VA_ARGS__},       \
     .n_aliases = (int)(sizeof((const char *[]){__VA_ARGS__}) /              \
                        sizeof(const char *)),                               \
     .user_cd = ucd, .global_cd = gcd, .role = KK_ROLE_ANYONE, .enabled = true}

static const kk_config_command DEFAULT_COMMANDS[] = {
    CMD("avatar", 5, 0, "personagem", "char"),
    CMD("color", 5, 0, "cor", "colour", "paleta", "palette"),
    CMD("gear", 5, 0, "item", "acessorio", "acessório", "equip"),
    CMD("jump", 3, 0, "pula", "pular"),
    CMD("sit", 10, 0, "senta", "sentar"),
    CMD("dance", 60, 0, "danca", "dança", "dancar", "dançar"),
    CMD("emote", 15, 0, "anim"),
    CMD("hug", 60, 0, "abraco", "abraço", "abracar", "abraçar"),
    CMD("attack", 120, 0, "ataque", "atacar", "bater"),
    CMD("sound", 30, 3, "som", "play", "sfx", "mesa"),
};
#undef CMD

const kk_config_command *kk_config_default_commands(int *n)
{
    *n = (int)(sizeof DEFAULT_COMMANDS / sizeof DEFAULT_COMMANDS[0]);
    return DEFAULT_COMMANDS;
}

const char *const *kk_config_actions(void)
{
    return ACTIONS;
}

/* ---- value parsers ------------------------------------------------------- */

bool kk_parse_long(const char *s, long lo, long hi, long *out)
{
    char *end;
    errno = 0;
    long v = strtol(s, &end, 10);
    if (errno || end == s || *end || v < lo || v > hi)
        return false;
    *out = v;
    return true;
}

bool kk_parse_double(const char *s, double lo, double hi, double *out)
{
    char *end;
    errno = 0;
    double v = strtod(s, &end);
    if (errno || end == s || *end || !isfinite(v) || v < lo || v > hi)
        return false;
    *out = v;
    return true;
}

bool kk_parse_size(const char *s, int *w, int *h)
{
    int a, b;
    char extra;
    if (sscanf(s, "%dx%d%c", &a, &b, &extra) != 2 || a < 1 || b < 1 ||
        a > 16384 || b > 16384)
        return false;
    *w = a;
    *h = b;
    return true;
}

bool kk_parse_bool(const char *s, bool *out)
{
    static const char *const yes[] = {"yes", "true", "on", "1", "sim", NULL};
    static const char *const no[] = {"no", "false", "off", "0", "não", "nao", NULL};
    for (int i = 0; yes[i]; i++)
        if (strcasecmp(s, yes[i]) == 0) {
            *out = true;
            return true;
        }
    for (int i = 0; no[i]; i++)
        if (strcasecmp(s, no[i]) == 0) {
            *out = false;
            return true;
        }
    return false;
}

static const char *const ROLES[] = {"anyone", "member", "mod", "owner"};

bool kk_parse_role(const char *s, kk_role *out)
{
    for (int i = 0; i < 4; i++)
        if (strcasecmp(s, ROLES[i]) == 0) {
            *out = (kk_role)i;
            return true;
        }
    return false;
}

const char *kk_role_name(kk_role r)
{
    return r >= KK_ROLE_ANYONE && r <= KK_ROLE_OWNER ? ROLES[r] : "anyone";
}

/* ---- life cycle ---------------------------------------------------------- */

bool kk_config_set_str(char **dst, const char *value)
{
    char *v = value ? strdup(value) : NULL;
    if (value && !v)
        return false;
    free(*dst);
    *dst = v;
    return true;
}

static void command_free(kk_config_command *k)
{
    free(k->name);
    free(k->action);
    free(k->data);
    for (int i = 0; i < k->n_aliases; i++)
        free(k->aliases[i]);
}

static bool command_copy(kk_config_command *dst, const kk_config_command *src)
{
    *dst = (kk_config_command){
        .n_aliases = src->n_aliases,
        .user_cd = src->user_cd,
        .global_cd = src->global_cd,
        .role = src->role,
        .enabled = src->enabled,
    };
    bool ok = kk_config_set_str(&dst->name, src->name) &&
              kk_config_set_str(&dst->action, src->action) &&
              kk_config_set_str(&dst->data, src->data);
    for (int i = 0; i < src->n_aliases; i++)
        ok = kk_config_set_str(&dst->aliases[i], src->aliases[i]) && ok;
    return ok;
}

static void sound_free(kk_config_sound *s)
{
    free(s->name);
    free(s->file);
    for (int i = 0; i < s->n_aliases; i++)
        free(s->aliases[i]);
}

static kk_config_sound *add_sound(kk_config *c)
{
    kk_config_sound *ns = realloc(c->sounds, (size_t)(c->n_sounds + 1) * sizeof *ns);
    if (!ns)
        return NULL;
    c->sounds = ns;
    kk_config_sound *s = &ns[c->n_sounds++];
    memset(s, 0, sizeof *s);
    s->volume = 100;
    return s;
}

static bool sound_copy(kk_config_sound *dst, const kk_config_sound *src)
{
    *dst = (kk_config_sound){.n_aliases = src->n_aliases, .volume = src->volume};
    bool ok = kk_config_set_str(&dst->name, src->name) &&
              kk_config_set_str(&dst->file, src->file);
    for (int i = 0; i < src->n_aliases; i++)
        ok = kk_config_set_str(&dst->aliases[i], src->aliases[i]) && ok;
    return ok;
}

static kk_config_command *add_command(kk_config *c)
{
    kk_config_command *nc =
        realloc(c->commands, (size_t)(c->n_commands + 1) * sizeof *nc);
    if (!nc)
        return NULL;
    c->commands = nc;
    kk_config_command *k = &nc[c->n_commands++];
    memset(k, 0, sizeof *k);
    return k;
}

void kk_config_free(kk_config *c)
{
    for (int i = 0; i < c->n_show; i++)
        free(c->show[i]);
    free(c->default_avatar);
    free(c->sa_dir);
    free(c->name_font);
    free(c->bubble_font);
    free(c->youtube);
    free(c->users);
    free(c->socket);
    for (int i = 0; i < c->n_commands; i++)
        command_free(&c->commands[i]);
    free(c->commands);
    free(c->sound_device);
    for (int i = 0; i < c->n_sounds; i++)
        sound_free(&c->sounds[i]);
    free(c->sounds);
    memset(c, 0, sizeof *c);
}

void kk_config_defaults(kk_config *c)
{
    *c = (kk_config){
        .width = 1280,
        .height = 720,
        .fps = 30,
        .scale = 2.0,
        .ground = -1,
        .count = -1,
        .max_avatars = 30,
        .despawn = 300.0,
        .shortcuts = true,
        .shortcut_cd = 5.0,
        .sound_enabled = true,
        .sound_volume = 100,
        .sound_voices = 8,
        .sound_commands = true,
        .show_names = true,
        .show_bubbles = true,
        .name_size = 11,
        .bubble_size = 10,
    };
    int n;
    const kk_config_command *d = kk_config_default_commands(&n);
    for (int i = 0; i < n; i++) {
        kk_config_command *k = add_command(c);
        if (!k || !command_copy(k, &d[i]))
            return; /* out of memory: fewer commands, nothing worse */
    }
}

bool kk_config_copy(kk_config *dst, const kk_config *src)
{
    *dst = *src;
    dst->default_avatar = dst->sa_dir = dst->youtube = dst->users =
        dst->socket = NULL;
    dst->name_font = dst->bubble_font = NULL;
    dst->commands = NULL;
    dst->n_commands = 0;
    dst->sound_device = NULL;
    dst->sounds = NULL;
    dst->n_sounds = 0;
    bool ok = true;
    memset(dst->show, 0, sizeof dst->show);
    for (int i = 0; i < src->n_show; i++)
        ok = kk_config_set_str(&dst->show[i], src->show[i]) && ok;
    ok = kk_config_set_str(&dst->default_avatar, src->default_avatar) && ok;
    ok = kk_config_set_str(&dst->sa_dir, src->sa_dir) && ok;
    ok = kk_config_set_str(&dst->name_font, src->name_font) && ok;
    ok = kk_config_set_str(&dst->bubble_font, src->bubble_font) && ok;
    ok = kk_config_set_str(&dst->youtube, src->youtube) && ok;
    ok = kk_config_set_str(&dst->users, src->users) && ok;
    ok = kk_config_set_str(&dst->socket, src->socket) && ok;
    for (int i = 0; i < src->n_commands && ok; i++) {
        kk_config_command *k = add_command(dst);
        ok = k && command_copy(k, &src->commands[i]);
    }
    ok = ok && kk_config_set_str(&dst->sound_device, src->sound_device);
    for (int i = 0; i < src->n_sounds && ok; i++) {
        kk_config_sound *s = add_sound(dst);
        ok = s && sound_copy(s, &src->sounds[i]);
    }
    return ok;
}

kk_config_command *kk_config_find_command(kk_config *c, const char *name)
{
    for (int i = 0; i < c->n_commands; i++)
        if (strcmp(c->commands[i].name, name) == 0)
            return &c->commands[i];
    return NULL;
}

static bool word_eq(const char *a, const char *b)
{
    if (*a == '!')
        a++;
    return strcasecmp(a, b) == 0;
}

const kk_config_sound *kk_config_find_sound(const kk_config *c, const char *word)
{
    for (int i = 0; i < c->n_sounds; i++) {
        const kk_config_sound *s = &c->sounds[i];
        if (word_eq(word, s->name))
            return s;
        for (int k = 0; k < s->n_aliases; k++)
            if (word_eq(word, s->aliases[k]))
                return s;
    }
    return NULL;
}

static kk_config_sound *find_sound_by_name(kk_config *c, const char *name)
{
    for (int i = 0; i < c->n_sounds; i++)
        if (strcmp(c->sounds[i].name, name) == 0)
            return &c->sounds[i];
    return NULL;
}

bool kk_config_sound_name(char *out, size_t size, const char *file)
{
    const char *base = strrchr(file, '/');
    base = base ? base + 1 : file;
    const char *dot = strrchr(base, '.');
    size_t n = dot && dot != base ? (size_t)(dot - base) : strlen(base);
    size_t o = 0;
    for (size_t i = 0; i < n && o + 1 < size && o + 1 < MAX_NAME; i++) {
        unsigned char ch = (unsigned char)base[i];
        if (isspace(ch) || ch == '!' || ch == ',' || ch == '[' || ch == ']' || ch == '=')
            ch = '_';
        out[o++] = (char)tolower(ch);
    }
    out[o] = '\0';
    return o > 0;
}

static void remove_command(kk_config *c, int i)
{
    command_free(&c->commands[i]);
    memmove(&c->commands[i], &c->commands[i + 1],
            (size_t)(c->n_commands - i - 1) * sizeof *c->commands);
    c->n_commands--;
}

/* ---- lists --------------------------------------------------------------- */

/* Calls fn for each trimmed, non-empty item of a comma-separated list. */
static bool each_item(const char *list, bool (*fn)(void *ud, const char *item),
                      void *ud)
{
    while (*list) {
        size_t n = strcspn(list, ",");
        const char *s = list;
        size_t len = n;
        while (len && isspace((unsigned char)*s)) {
            s++;
            len--;
        }
        while (len && isspace((unsigned char)s[len - 1]))
            len--;
        if (len) {
            char item[256];
            if (len >= sizeof item)
                return false;
            memcpy(item, s, len);
            item[len] = '\0';
            if (!fn(ud, item))
                return false;
        }
        list += n;
        if (*list == ',')
            list++;
    }
    return true;
}

static bool add_show(void *ud, const char *item)
{
    kk_config *c = ud;
    if (c->n_show == KK_CONFIG_MAX_SHOW)
        return false;
    c->show[c->n_show] = NULL;
    if (!kk_config_set_str(&c->show[c->n_show], item))
        return false;
    c->n_show++;
    return true;
}

bool kk_config_set_show(kk_config *c, const char *list)
{
    for (int i = 0; i < c->n_show; i++) {
        free(c->show[i]);
        c->show[i] = NULL;
    }
    c->n_show = 0;
    return each_item(list, add_show, c);
}

/* Command words: lowercase ASCII, no "!", no spaces. */
static bool normalize_word(char *out, size_t size, const char *in)
{
    if (*in == '!')
        in++;
    size_t n = strlen(in);
    if (n == 0 || n >= size || n >= MAX_NAME)
        return false;
    for (size_t i = 0; i < n; i++) {
        if (isspace((unsigned char)in[i]) || in[i] == '!')
            return false;
        out[i] = (char)tolower((unsigned char)in[i]);
    }
    out[n] = '\0';
    return true;
}

static bool add_alias(void *ud, const char *item)
{
    kk_config_command *k = ud;
    char word[MAX_NAME];
    if (k->n_aliases == KK_CONFIG_MAX_ALIASES || !normalize_word(word, sizeof word, item))
        return false;
    k->aliases[k->n_aliases] = NULL;
    if (!kk_config_set_str(&k->aliases[k->n_aliases], word))
        return false;
    k->n_aliases++;
    return true;
}

static bool set_aliases(kk_config_command *k, const char *list)
{
    for (int i = 0; i < k->n_aliases; i++)
        free(k->aliases[i]);
    k->n_aliases = 0;
    return each_item(list, add_alias, k);
}

static bool add_sound_alias(void *ud, const char *item)
{
    kk_config_sound *s = ud;
    char word[MAX_NAME];
    if (s->n_aliases == KK_CONFIG_MAX_ALIASES || !normalize_word(word, sizeof word, item))
        return false;
    s->aliases[s->n_aliases] = NULL;
    if (!kk_config_set_str(&s->aliases[s->n_aliases], word))
        return false;
    s->n_aliases++;
    return true;
}

static bool set_sound_aliases(kk_config_sound *s, const char *list)
{
    for (int i = 0; i < s->n_aliases; i++)
        free(s->aliases[i]);
    s->n_aliases = 0;
    return each_item(list, add_sound_alias, s);
}

/* ---- applying a file ----------------------------------------------------- */

typedef struct {
    kk_config_warn_fn fn;
    void *ud;
    int line;
    const char *section, *key, *value;
} warner;

static void warnf(const warner *w, const char *fmt, ...)
    __attribute__((format(printf, 2, 3)));

static void warnf(const warner *w, const char *fmt, ...)
{
    if (!w->fn)
        return;
    char msg[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof msg, fmt, ap);
    va_end(ap);
    w->fn(w->ud, w->line, msg);
}

static void bad_value(const warner *w, const char *expected)
{
    warnf(w, "[%s] %s: valor inválido \"%s\" (%s)", w->section, w->key,
          w->value, expected);
}

static void unknown_key(const warner *w)
{
    warnf(w, "[%s] chave desconhecida: %s", w->section, w->key);
}

static bool key_is(const warner *w, const char *key)
{
    return strcasecmp(w->key, key) == 0;
}

static void get_int(const warner *w, int *dst, long lo, long hi,
                    bool auto_ok)
{
    long v;
    if (auto_ok && strcasecmp(w->value, "auto") == 0)
        *dst = -1;
    else if (kk_parse_long(w->value, lo, hi, &v))
        *dst = (int)v;
    else
        bad_value(w, auto_ok ? "\"auto\" ou um número inteiro"
                             : "um número inteiro");
}

static void get_double(const warner *w, double *dst, double lo, double hi)
{
    if (!kk_parse_double(w->value, lo, hi, dst))
        bad_value(w, "um número");
}

static void get_bool(const warner *w, bool *dst)
{
    if (!kk_parse_bool(w->value, dst))
        bad_value(w, "yes ou no");
}

/* Empty means "unset" for optional strings. */
static void get_str(const warner *w, char **dst)
{
    kk_config_set_str(dst, w->value[0] ? w->value : NULL);
}

/* Like get_str, with "~/" meaning the home folder. */
static void get_path(const warner *w, char **dst)
{
    const char *home = getenv("HOME");
    char path[KK_PATH_MAX];
    if (strncmp(w->value, "~/", 2) != 0 || !home) {
        get_str(w, dst);
    } else if (!kk_pathf(path, sizeof path, "%s%s", home, w->value + 1)) {
        bad_value(w, "caminho comprido demais");
    } else {
        kk_config_set_str(dst, path);
    }
}

static void apply_window(kk_config *c, const warner *w)
{
    if (key_is(w, "mode")) {
        if (strcasecmp(w->value, "obs") == 0)
            c->desktop = false;
        else if (strcasecmp(w->value, "desktop") == 0)
            c->desktop = true;
        else
            bad_value(w, "obs ou desktop");
    } else if (key_is(w, "size")) {
        if (!kk_parse_size(w->value, &c->width, &c->height))
            bad_value(w, "LxA, ex.: 1920x1080");
    } else if (key_is(w, "fps")) {
        get_int(w, &c->fps, 1, 240, false);
    } else {
        unknown_key(w);
    }
}

static void apply_avatars(kk_config *c, const warner *w)
{
    if (key_is(w, "scale"))
        get_double(w, &c->scale, 0.1, 16.0);
    else if (key_is(w, "ground"))
        get_int(w, &c->ground, 0, 16384, true);
    else if (key_is(w, "count"))
        get_int(w, &c->count, 0, 1000, true);
    else if (key_is(w, "show")) {
        if (!kk_config_set_show(c, w->value))
            bad_value(w, "nomes separados por vírgula");
    } else if (key_is(w, "default"))
        get_str(w, &c->default_avatar);
    else if (key_is(w, "sa_dir"))
        get_path(w, &c->sa_dir);
    else if (key_is(w, "show_names"))
        get_bool(w, &c->show_names);
    else if (key_is(w, "name_position")) {
        if (strcasecmp(w->value, "below") == 0)
            c->name_above = false;
        else if (strcasecmp(w->value, "above") == 0)
            c->name_above = true;
        else
            bad_value(w, "below ou above");
    } else if (key_is(w, "show_bubbles"))
        get_bool(w, &c->show_bubbles);
    else if (key_is(w, "name_font"))
        get_str(w, &c->name_font);
    else if (key_is(w, "name_size"))
        get_double(w, &c->name_size, 4, 200);
    else if (key_is(w, "bubble_font"))
        get_str(w, &c->bubble_font);
    else if (key_is(w, "bubble_size"))
        get_double(w, &c->bubble_size, 4, 200);
    else
        unknown_key(w);
}

static void apply_chat(kk_config *c, const warner *w)
{
    int v;
    if (key_is(w, "youtube"))
        get_str(w, &c->youtube);
    else if (key_is(w, "demo"))
        get_bool(w, &c->demo);
    else if (key_is(w, "max"))
        get_int(w, &c->max_avatars, 1, 1000, false);
    else if (key_is(w, "despawn")) {
        v = (int)c->despawn;
        get_int(w, &v, 5, 86400, false);
        c->despawn = v;
    } else if (key_is(w, "verbose"))
        get_bool(w, &c->verbose);
    else if (key_is(w, "users"))
        get_path(w, &c->users);
    else
        unknown_key(w);
}

static void apply_control(kk_config *c, const warner *w)
{
    if (!key_is(w, "socket"))
        unknown_key(w);
    else if (strcasecmp(w->value, "off") == 0 || strcasecmp(w->value, "no") == 0)
        kk_config_set_str(&c->socket, "");
    else
        get_path(w, &c->socket);
}

static void apply_commands(kk_config *c, const warner *w)
{
    if (key_is(w, "shortcuts"))
        get_bool(w, &c->shortcuts);
    else if (key_is(w, "shortcut_cooldown"))
        get_double(w, &c->shortcut_cd, 0, 86400);
    else
        unknown_key(w);
}

static void apply_soundboard(kk_config *c, const warner *w)
{
    if (key_is(w, "enabled"))
        get_bool(w, &c->sound_enabled);
    else if (key_is(w, "volume"))
        get_int(w, &c->sound_volume, 0, 400, false);
    else if (key_is(w, "device"))
        get_str(w, &c->sound_device);
    else if (key_is(w, "voices"))
        get_int(w, &c->sound_voices, 1, 64, false);
    else if (key_is(w, "commands"))
        get_bool(w, &c->sound_commands);
    else
        unknown_key(w);
}

static void apply_sound(kk_config *c, const warner *w, const char *name)
{
    kk_config_sound *s = find_sound_by_name(c, name);
    if (!s && (!(s = add_sound(c)) || !kk_config_set_str(&s->name, name)))
        return;
    if (key_is(w, "file"))
        get_path(w, &s->file);
    else if (key_is(w, "aliases")) {
        if (!set_sound_aliases(s, w->value))
            bad_value(w, "até 8 palavras separadas por vírgula");
    } else if (key_is(w, "volume"))
        get_int(w, &s->volume, 0, 400, false);
    else
        unknown_key(w);
}

static bool is_action(const char *s)
{
    for (int i = 0; ACTIONS[i]; i++)
        if (strcmp(ACTIONS[i], s) == 0)
            return true;
    return false;
}

static void apply_command(kk_config *c, const warner *w, const char *name)
{
    kk_config_command *k = kk_config_find_command(c, name);
    if (!k) {
        if (!(k = add_command(c)) || !kk_config_set_str(&k->name, name))
            return;
        k->enabled = true;
    }
    if (key_is(w, "action")) {
        char a[MAX_NAME];
        if (normalize_word(a, sizeof a, w->value) && is_action(a))
            kk_config_set_str(&k->action, a);
        else
            bad_value(w, "avatar, color, gear, jump, sit, dance, emote, hug, "
                         "attack ou sound");
    } else if (key_is(w, "data")) {
        get_str(w, &k->data);
    } else if (key_is(w, "aliases")) {
        if (!set_aliases(k, w->value))
            bad_value(w, "até 8 palavras separadas por vírgula");
    } else if (key_is(w, "cooldown")) {
        get_double(w, &k->user_cd, 0, 86400);
    } else if (key_is(w, "global_cooldown")) {
        get_double(w, &k->global_cd, 0, 86400);
    } else if (key_is(w, "role")) {
        if (!kk_parse_role(w->value, &k->role))
            bad_value(w, "anyone, member, mod ou owner");
    } else if (key_is(w, "enabled")) {
        get_bool(w, &k->enabled);
    } else {
        unknown_key(w);
    }
}

void kk_config_apply(kk_config *c, const kk_ini *ini, kk_config_warn_fn warn,
                     void *ud)
{
    warner w = {.fn = warn, .ud = ud};
    int it = 0;
    while (kk_ini_next(ini, &it, &w.section, &w.key, &w.value, &w.line)) {
        const char *s = w.section;
        if (strcasecmp(s, "window") == 0) {
            apply_window(c, &w);
        } else if (strcasecmp(s, "avatars") == 0) {
            apply_avatars(c, &w);
        } else if (strcasecmp(s, "chat") == 0) {
            apply_chat(c, &w);
        } else if (strcasecmp(s, "control") == 0) {
            apply_control(c, &w);
        } else if (strcasecmp(s, "commands") == 0) {
            apply_commands(c, &w);
        } else if (strcasecmp(s, "soundboard") == 0) {
            apply_soundboard(c, &w);
        } else if (strncasecmp(s, SOUND_PREFIX, strlen(SOUND_PREFIX)) == 0) {
            char name[MAX_NAME];
            if (normalize_word(name, sizeof name, s + strlen(SOUND_PREFIX)))
                apply_sound(c, &w, name);
            else
                warnf(&w, "[%s]: nome de som inválido", s);
        } else if (strncasecmp(s, CMD_PREFIX, strlen(CMD_PREFIX)) == 0) {
            char name[MAX_NAME];
            if (normalize_word(name, sizeof name, s + strlen(CMD_PREFIX)))
                apply_command(c, &w, name);
            else
                warnf(&w, "[%s]: nome de comando inválido", s);
        } else if (!s[0]) {
            warnf(&w, "%s fora de uma seção", w.key);
        } else {
            warnf(&w, "[%s]: seção desconhecida", s);
        }
    }

    /* New commands need an action. */
    for (int i = c->n_commands - 1; i >= 0; i--)
        if (!c->commands[i].action) {
            char section[MAX_NAME + sizeof CMD_PREFIX];
            snprintf(section, sizeof section, CMD_PREFIX "%s", c->commands[i].name);
            w.line = 0;
            int it2 = 0;
            const char *sec, *key, *val;
            int line;
            while (kk_ini_next(ini, &it2, &sec, &key, &val, &line))
                if (strcasecmp(sec, section) == 0) {
                    w.line = line;
                    break;
                }
            warnf(&w, "[%s]: comando novo sem \"action\"; ignorado", section);
            remove_command(c, i);
        }
    for (int i = c->n_sounds - 1; i >= 0; i--)
        if (!c->sounds[i].file) {
            w.line = 0;
            warnf(&w, "[" SOUND_PREFIX "%s]: som sem \"file\"; ignorado", c->sounds[i].name);
            sound_free(&c->sounds[i]);
            memmove(&c->sounds[i], &c->sounds[i + 1],
                    (size_t)(c->n_sounds - i - 1) * sizeof *c->sounds);
            c->n_sounds--;
        }
}

static void warn_line(void *ud, int line, const char *msg)
{
    const warner *w = ud;
    if (w->fn)
        w->fn(w->ud, line, msg);
}

int kk_config_load(kk_config *c, const char *path, bool *found,
                   kk_config_warn_fn warn, void *ud)
{
    *found = false;
    char *text = kk_read_file(path, NULL);
    if (!text)
        return errno == ENOENT ? 0 : -1;
    *found = true;
    warner w = {.fn = warn, .ud = ud};
    kk_ini *ini = kk_ini_parse(text, warn_line, &w);
    free(text);
    if (!ini)
        return -1;
    kk_config_apply(c, ini, warn, ud);
    kk_ini_free(ini);

    /* Sound files may be given relative to the config file. */
    const char *slash = strrchr(path, '/');
    for (int i = 0; slash && i < c->n_sounds; i++) {
        char full[KK_PATH_MAX];
        if (c->sounds[i].file[0] != '/' &&
            kk_pathf(full, sizeof full, "%.*s/%s", (int)(slash - path), path,
                     c->sounds[i].file))
            kk_config_set_str(&c->sounds[i].file, full);
    }
    return 0;
}

bool kk_config_default_path(char *out, size_t size)
{
    const char *xdg = getenv("XDG_CONFIG_HOME");
    if (xdg && xdg[0] == '/')
        return kk_pathf(out, size, "%s/kikarinhas/kikarinhas.ini", xdg);
    const char *home = getenv("HOME");
    return home && kk_pathf(out, size, "%s/.config/kikarinhas/kikarinhas.ini", home);
}
