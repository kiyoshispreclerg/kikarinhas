/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "commands.h"

#include <ctype.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define MAX_ALIASES 8
#define MAX_WORD 64
#define MAX_ARGS 256

typedef struct {
    char *names[1 + MAX_ALIASES]; /* [0] is the canonical name */
    int n_names;
    kk_cmd_fn fn;
    char *data;
    double user_cd, global_cd;
    kk_role role;
    double global_until;
} command;

/* When someone may use a command again, keyed by hash(command, person). */
typedef struct {
    uint64_t hash;
    double until;
} cooldown;

struct kk_commands {
    void *ud;
    command *cmds;
    int count, cap;
    kk_cmd_fn fallback;
    double fallback_cd;
    cooldown *cds;
    int n_cds, cap_cds;
};

kk_commands *kk_commands_new(void *ud)
{
    kk_commands *c = calloc(1, sizeof *c);
    if (c)
        c->ud = ud;
    return c;
}

void kk_commands_free(kk_commands *c)
{
    if (!c)
        return;
    for (int i = 0; i < c->count; i++) {
        for (int k = 0; k < c->cmds[i].n_names; k++)
            free(c->cmds[i].names[k]);
        free(c->cmds[i].data);
    }
    free(c->cmds);
    free(c->cds);
    free(c);
}

static command *find(kk_commands *c, const char *word)
{
    for (int i = 0; i < c->count; i++)
        for (int k = 0; k < c->cmds[i].n_names; k++)
            if (strcmp(c->cmds[i].names[k], word) == 0)
                return &c->cmds[i];
    return NULL;
}

int kk_commands_add(kk_commands *c, const char *name, kk_cmd_fn fn,
                    const char *data, double user_cooldown,
                    double global_cooldown, kk_role role)
{
    command *cmd = find(c, name);
    if (cmd && strcmp(cmd->names[0], name) != 0)
        return -1; /* the word is someone else's alias */
    if (!cmd) {
        if (c->count == c->cap) {
            int cap = c->cap ? c->cap * 2 : 16;
            command *nc = realloc(c->cmds, (size_t)cap * sizeof *nc);
            if (!nc)
                return -1;
            c->cmds = nc;
            c->cap = cap;
        }
        cmd = &c->cmds[c->count];
        memset(cmd, 0, sizeof *cmd);
        if (!(cmd->names[0] = strdup(name)))
            return -1;
        cmd->n_names = 1;
        c->count++;
    }
    free(cmd->data);
    cmd->data = data ? strdup(data) : NULL;
    cmd->fn = fn;
    cmd->user_cd = user_cooldown;
    cmd->global_cd = global_cooldown;
    cmd->role = role;
    return 0;
}

int kk_commands_alias(kk_commands *c, const char *name, const char *alias)
{
    command *cmd = find(c, name);
    if (!cmd || find(c, alias) || cmd->n_names == 1 + MAX_ALIASES)
        return -1;
    if (!(cmd->names[cmd->n_names] = strdup(alias)))
        return -1;
    cmd->n_names++;
    return 0;
}

void kk_commands_set_fallback(kk_commands *c, kk_cmd_fn fn,
                              double user_cooldown)
{
    c->fallback = fn;
    c->fallback_cd = user_cooldown;
}

kk_role kk_role_of(unsigned badges)
{
    if (badges & KK_BADGE_OWNER)
        return KK_ROLE_OWNER;
    if (badges & KK_BADGE_MOD)
        return KK_ROLE_MOD;
    if (badges & KK_BADGE_MEMBER)
        return KK_ROLE_MEMBER;
    return KK_ROLE_ANYONE;
}

/* ---- cooldowns ----------------------------------------------------------- */

static uint64_t hash2(const char *a, const char *b)
{
    uint64_t h = 1469598103934665603ULL;
    for (; *a; a++)
        h = (h ^ (unsigned char)*a) * 1099511628211ULL;
    h = (h ^ 0xff) * 1099511628211ULL;
    for (; *b; b++)
        h = (h ^ (unsigned char)*b) * 1099511628211ULL;
    return h;
}

static double user_until(const kk_commands *c, uint64_t h)
{
    for (int i = 0; i < c->n_cds; i++)
        if (c->cds[i].hash == h)
            return c->cds[i].until;
    return 0.0;
}

static void set_user_until(kk_commands *c, uint64_t h, double until, double now)
{
    for (int i = 0; i < c->n_cds; i++)
        if (c->cds[i].hash == h) {
            c->cds[i].until = until;
            return;
        }
    /* Drop expired entries before growing. */
    if (c->n_cds == c->cap_cds) {
        int kept = 0;
        for (int i = 0; i < c->n_cds; i++)
            if (c->cds[i].until > now)
                c->cds[kept++] = c->cds[i];
        c->n_cds = kept;
    }
    if (c->n_cds == c->cap_cds) {
        int cap = c->cap_cds ? c->cap_cds * 2 : 64;
        cooldown *nc = realloc(c->cds, (size_t)cap * sizeof *nc);
        if (!nc)
            return;
        c->cds = nc;
        c->cap_cds = cap;
    }
    c->cds[c->n_cds++] = (cooldown){h, until};
}

/* ---- dispatch ------------------------------------------------------------ */

/* Skips "!" or the full-width "！" (EF BC 81). NULL if neither. */
static const char *after_prefix(const char *text)
{
    while (isspace((unsigned char)*text))
        text++;
    if (*text == '!')
        return text + 1;
    if (strncmp(text, "\xEF\xBC\x81", 3) == 0)
        return text + 3;
    return NULL;
}

static void trim_copy(char *dst, size_t size, const char *src)
{
    while (isspace((unsigned char)*src))
        src++;
    size_t n = strlen(src);
    while (n && isspace((unsigned char)src[n - 1]))
        n--;
    if (n >= size)
        n = size - 1;
    memcpy(dst, src, n);
    dst[n] = '\0';
}

static void lower_ascii(char *s)
{
    for (; *s; s++)
        *s = (char)tolower((unsigned char)*s);
}

kk_cmd_result kk_commands_handle(kk_commands *c, const kk_chat_msg *msg,
                                 const char *user_key, double now)
{
    const char *p = after_prefix(msg->text);
    if (!p || !*p || isspace((unsigned char)*p))
        return KK_CMD_NONE;

    char word[MAX_WORD];
    size_t n = strcspn(p, " \t");
    if (n >= sizeof word)
        return KK_CMD_NONE;
    memcpy(word, p, n);
    word[n] = '\0';
    lower_ascii(word);

    char args[MAX_ARGS];
    kk_role role = kk_role_of(msg->badges);
    command *cmd = find(c, word);
    kk_cmd_call call = {.msg = msg, .user_key = user_key, .ud = c->ud, .args = args};
    uint64_t h;

    if (cmd) {
        if (role < cmd->role)
            return KK_CMD_DENIED;
        h = hash2(cmd->names[0], user_key);
        /* The channel owner is never kept waiting. */
        if (role != KK_ROLE_OWNER &&
            (now < cmd->global_until || now < user_until(c, h)))
            return KK_CMD_COOLDOWN;
        trim_copy(args, sizeof args, p + n);
        call.name = cmd->names[0];
        call.data = cmd->data;
        if (!cmd->fn(&call))
            return KK_CMD_FAILED;
        cmd->global_until = now + cmd->global_cd;
        set_user_until(c, h, now + cmd->user_cd, now);
        return KK_CMD_RAN;
    }

    if (!c->fallback)
        return KK_CMD_NONE;
    /* "!agnes tachyon": the whole text is the argument. */
    trim_copy(args, sizeof args, p);
    lower_ascii(args);
    call.name = "";
    h = hash2("", user_key);
    /* Can't tell yet whether "!xyz" is a name: while cooling down, let it
     * through as a normal message rather than swallow it. */
    if (role != KK_ROLE_OWNER && now < user_until(c, h))
        return KK_CMD_NONE;
    if (!c->fallback(&call))
        return KK_CMD_NONE; /* "!xyz" that means nothing: a normal message */
    set_user_until(c, h, now + c->fallback_cd, now);
    return KK_CMD_RAN;
}
