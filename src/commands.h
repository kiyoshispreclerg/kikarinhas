/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef KK_COMMANDS_H
#define KK_COMMANDS_H

#include <stdbool.h>

#include "chat.h"

/* Chat commands: "!word args". A command has a handler, optional data (e.g.
 * the sound a !som variant plays), aliases, a minimum role, and two
 * cooldowns: per person and for everybody. The table is filled at start
 * (built-in commands, later the config file), so new commands, renames and
 * aliases need no code changes. "！" (full width) works as a prefix too.
 *
 * When the word is not a command, an optional fallback handler gets the
 * whole text after the prefix: that is how "!pikachu" picks an avatar. */

typedef enum {
    KK_ROLE_ANYONE,
    KK_ROLE_MEMBER,
    KK_ROLE_MOD,
    KK_ROLE_OWNER,
} kk_role;

typedef struct {
    const kk_chat_msg *msg;
    const char *user_key; /* "platform:id" */
    const char *name;     /* canonical command name ("" for the fallback) */
    const char *args;     /* trimmed text after the command word */
    const char *data;     /* the command's data, or NULL */
    void *ud;             /* given to kk_commands_new */
} kk_cmd_call;

/* Returns true if it did something; only then do cooldowns start. */
typedef bool (*kk_cmd_fn)(const kk_cmd_call *call);

typedef enum {
    KK_CMD_NONE,     /* not a command: show it as a normal message */
    KK_CMD_RAN,
    KK_CMD_COOLDOWN, /* swallowed: still cooling down */
    KK_CMD_DENIED,   /* role too low */
    KK_CMD_FAILED,   /* handler did nothing (bad argument, ...) */
} kk_cmd_result;

typedef struct kk_commands kk_commands;

kk_commands *kk_commands_new(void *ud);
void kk_commands_free(kk_commands *c);

/* name is lowercase, without "!". Re-adding a name replaces it. */
int kk_commands_add(kk_commands *c, const char *name, kk_cmd_fn fn,
                    const char *data, double user_cooldown,
                    double global_cooldown, kk_role role);
/* True if word is a command name or alias. */
bool kk_commands_has(kk_commands *c, const char *word);
/* Cooldowns of name are shared with every command of the same group (e.g.
 * each sound's own !command and !sound itself). */
int kk_commands_set_group(kk_commands *c, const char *name, const char *group);
/* Extra word for an existing command. Fails if the word is taken. */
int kk_commands_alias(kk_commands *c, const char *name, const char *alias);
void kk_commands_set_fallback(kk_commands *c, kk_cmd_fn fn,
                              double user_cooldown);

kk_cmd_result kk_commands_handle(kk_commands *c, const kk_chat_msg *msg,
                                 const char *user_key, double now);

/* Role from chat badges. */
kk_role kk_role_of(unsigned badges);

#endif
