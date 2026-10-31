/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef KK_POLLS_H
#define KK_POLLS_H

/* Polls: the streamer asks a question with a few options (from the
 * configurator, over the socket), the chat votes with "!a"/"!1" or each
 * option's own shortcut, and the avatars of the voters gather on the stage
 * in the part of the window of their option (pollview.h draws that).
 *
 * Every poll is kept, closed ones too, in a JSON file next to the people
 * file (~/.local/share/kikarinhas/polls.json): question, options, votes per
 * platform, when it opened and closed and which lives it ran on, to look
 * at later. Only one is open at a time. kikarinhas writes the file; the
 * configurator reads it (and edits it only while kikarinhas is not
 * running). Who voted for what is kept only while the poll is open, so a
 * restart doesn't let people vote twice; it goes away when it closes. */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "cJSON.h"

#define KK_POLL_MAX_OPTIONS 10
#define KK_POLL_MAX_WORD 48 /* bytes of a shortcut, without "!" */

typedef struct {
    char *text;
    char *shortcut; /* lowercase, without "!"; NULL: none */
    int *votes;     /* one per platform of the poll */
} kk_poll_option;

/* A live votes came from, as the connectors report it. */
typedef struct {
    char *platform, *url, *title; /* title may be NULL */
} kk_poll_live;

typedef struct {
    char *key; /* "platform:id"; NULL: free slot */
    int option;
} kk_poll_voter;

typedef struct {
    uint64_t id;
    char *question;
    kk_poll_option options[KK_POLL_MAX_OPTIONS];
    int n_options;
    int duration;     /* seconds; 0: open until ended by hand */
    bool show_counts; /* votes on the stream, not only here */
    /* Which words vote besides the shortcuts: "!a".."!j", "!1".."!10".
     * Decided when it opens, by what is free of other commands. */
    bool letters, numbers;
    long long opened, closed; /* Unix time; closed 0 while open */
    char **platforms; /* where votes came from, in order of the first vote */
    int n_platforms;
    kk_poll_live *lives;
    int n_lives;
    char **channels; /* the lives being read when it opened: "youtube @canal" */
    int n_channels;
    kk_poll_voter *voters; /* hash table, only while open */
    int n_voters, cap_voters;
} kk_poll;

typedef struct kk_polls kk_polls;

/* Reads path (a missing file is an empty history; a broken one falls back
 * to its backup). NULL only without memory. With path NULL nothing is ever
 * written (tests). */
kk_polls *kk_polls_open(const char *path);
void kk_polls_free(kk_polls *ps);
/* Writes the file if something changed since the last time (temp file +
 * fsync + rename, the old one kept as .backup). 0 also when nothing to do. */
int kk_polls_save(kk_polls *ps);
bool kk_polls_dirty(const kk_polls *ps);
/* Bumps on every change (votes included), for whoever draws them. */
unsigned kk_polls_serial(const kk_polls *ps);

/* ~/.local/share/kikarinhas/polls.json (XDG_DATA_HOME). */
bool kk_polls_default_path(char *out, size_t size);

/* Oldest first. */
int kk_polls_count(const kk_polls *ps);
const kk_poll *kk_polls_at(const kk_polls *ps, int i);
const kk_poll *kk_polls_find(const kk_polls *ps, uint64_t id);
/* The open one, or NULL. */
const kk_poll *kk_polls_active(const kk_polls *ps);

/* Whether "!word" (lowercase, without "!") already means something else:
 * a command, an avatar name... */
typedef bool (*kk_poll_taken_fn)(void *ud, const char *word);

/* A new poll from what the configurator sends:
 *   {"question":"...","options":[{"text":"Sim","command":"sim"},...],
 *    "duration":60,"show_counts":true}
 * Checks everything (2 to KK_POLL_MAX_OPTIONS options with text, shortcuts
 * that are one word, not repeated, not "a"/"1"... of the poll itself and
 * not taken) and decides whether letters and numbers vote. On a problem
 * returns NULL with a message (pt-BR) in err. */
kk_poll *kk_poll_from_request(const cJSON *req, kk_poll_taken_fn taken, void *ud, char *err,
                              size_t size);
void kk_poll_free(kk_poll *p);

/* Opens p (taken over, also on failure) at now. channels (may be NULL) are
 * the lives being read. Fails if one is already open. Returns the poll as
 * stored, or NULL. */
const kk_poll *kk_polls_start(kk_polls *ps, kk_poll *p, long long now,
                              const char *const *channels, int n_channels);
/* Closes the open poll at now; false if none is. */
bool kk_polls_close(kk_polls *ps, long long now);
/* True when the open poll has a time limit and it has passed. */
bool kk_polls_due(const kk_polls *ps, long long now);
/* When the open poll ends by itself (0: it doesn't). */
long long kk_poll_ends_at(const kk_poll *p);
/* Forgets a closed poll; false if there is none with id or it is open. */
bool kk_polls_remove(kk_polls *ps, uint64_t id);

/* The option text votes for ("!b", "!2", "!sim"...), or -1. */
int kk_poll_match(const kk_poll *p, const char *text);
/* What to type for option i, as shown: "!a" (or "!1"), else the shortcut;
 * shortcut gets the shortcut when there is one besides (else ""). */
void kk_poll_words(const kk_poll *p, int i, char *main_word, size_t size, char *shortcut,
                   size_t ssize);

/* key ("platform:id") votes for option on the open poll; voting again
 * changes the vote. Returns the option voted before (-1: none), or -2 if
 * there is no open poll or option is out of range. */
int kk_polls_vote(kk_polls *ps, const char *key, int option);
/* What key voted on p, or -1. */
int kk_poll_vote_of(const kk_poll *p, const char *key);
/* Notes a live of the open poll (no-op without one, or if known). */
void kk_polls_note_live(kk_polls *ps, const char *platform, const char *url,
                        const char *title);

/* Votes: of option i on platform pf (index in p->platforms; -1: all), and
 * of all options on pf (-1: everything). */
int kk_poll_votes(const kk_poll *p, int i, int pf);
int kk_poll_total(const kk_poll *p, int pf);

/* The poll as JSON (the file's format); voters only if asked. */
cJSON *kk_poll_to_json(const kk_poll *p, bool voters);

#endif
