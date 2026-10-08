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

/* Levels, lowest first. Member, mod and owner come from the chat badges of
 * each message; trusted is given by hand (kikarinhas-config, Viewers tab) to
 * people the streamer vouches for, and ranks above paying members, as
 * "regulars" do in other chat bots. */
typedef enum {
    KK_ROLE_ANYONE,
    KK_ROLE_MEMBER,
    KK_ROLE_TRUSTED,
    KK_ROLE_MOD,
    KK_ROLE_OWNER,
    KK_ROLE_COUNT,
} kk_role;

typedef struct kk_commands kk_commands;

/* Settings some actions read besides data (an image's place, how long a
 * bubble stays...). Every command has them, with these defaults. */
typedef struct {
    double seconds;     /* on screen; <= 0: the action decides */
    double x, y;        /* percent of the window, where an image's centre goes */
    double scale;       /* percent of an image's own size */
} kk_cmd_params;

#define KK_CMD_PARAMS_INIT {.seconds = -1, .x = 50, .y = 50, .scale = 100}
#define KK_CMD_PARAMS_DEFAULT ((kk_cmd_params)KK_CMD_PARAMS_INIT)

typedef struct {
    const kk_chat_msg *msg;
    const char *user_key; /* "platform:id" */
    const char *name;     /* canonical command name ("" for the fallback) */
    const char *args;     /* trimmed text after the command word */
    const char *data;     /* the command's data, or NULL */
    kk_role role;         /* the sender's level (badges and trust) */
    const kk_cmd_params *params; /* never NULL */
    void *ud;             /* given to kk_commands_new */
    kk_commands *commands; /* the table the command was found in */
} kk_cmd_call;

/* Returns true if it did something; only then do cooldowns start. */
typedef bool (*kk_cmd_fn)(const kk_cmd_call *call);

typedef enum {
    KK_CMD_NONE,     /* not a command: show it as a normal message */
    KK_CMD_RAN,
    KK_CMD_COOLDOWN, /* swallowed: still cooling down */
    KK_CMD_DENIED,   /* role too low */
    KK_CMD_POOR,     /* not enough points (kk_commands_last_cost says how many) */
    KK_CMD_FAILED,   /* handler did nothing (bad argument, ...) */
} kk_cmd_result;

kk_commands *kk_commands_new(void *ud);
/* What was given to kk_commands_new. */
void *kk_commands_ud(const kk_commands *c);
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
/* Replaces the params of name (copied). */
int kk_commands_set_params(kk_commands *c, const char *name, const kk_cmd_params *p);
/* Extra word for an existing command. Fails if the word is taken. */
int kk_commands_alias(kk_commands *c, const char *name, const char *alias);
/* A short usage line ("!hug [@name]") for the help command to show. Commands
 * without one are not listed. */
int kk_commands_set_help(kk_commands *c, const char *name, const char *text);
/* Calls fn with the help line of every command a person of role may use. */
void kk_commands_each_help(const kk_commands *c, kk_role role,
                           void (*fn)(const char *text, void *ud), void *ud);
void kk_commands_set_fallback(kk_commands *c, kk_cmd_fn fn,
                              double user_cooldown);
/* What the fallback costs. Someone without the points gets their "!xyz"
 * shown as a message: it may not have been a shortcut at all. */
void kk_commands_set_fallback_cost(kk_commands *c, long cost);

/* Points: a command can cost some, checked before it runs and taken only
 * when it did something; the channel owner never pays. Without a wallet,
 * costs are ignored. arg names a cost for one argument (e.g. one sound of
 * "!sound NAME", by the first word of data or of what was typed); NULL sets
 * the command's own. */
typedef long (*kk_balance_fn)(void *ud, const char *user_key);
typedef void (*kk_charge_fn)(void *ud, const char *user_key, long cost);
void kk_commands_set_wallet(kk_commands *c, kk_balance_fn balance, kk_charge_fn charge,
                            void *ud);
int kk_commands_set_cost(kk_commands *c, const char *name, const char *arg, long cost);
/* The cost of the last command handled (after KK_CMD_POOR: what it lacked
 * the points for). */
long kk_commands_last_cost(const kk_commands *c);
/* What name costs (its own cost), for the help list; 0 if free. */
long kk_commands_cost(const kk_commands *c, const char *name);

/* Tells whether user_key was marked as trusted; without it nobody is. */
typedef bool (*kk_trust_fn)(void *ud, const char *user_key);
void kk_commands_set_trust(kk_commands *c, kk_trust_fn fn, void *ud);
/* The level of a message's sender: badges, raised to trusted if marked. */
kk_role kk_commands_role(const kk_commands *c, const kk_chat_msg *msg,
                         const char *user_key);

kk_cmd_result kk_commands_handle(kk_commands *c, const kk_chat_msg *msg,
                                 const char *user_key, double now);

/* Role from chat badges. */
kk_role kk_role_of(unsigned badges);

#endif
