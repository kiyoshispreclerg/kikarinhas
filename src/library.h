/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef KK_LIBRARY_H
#define KK_LIBRARY_H

/* Where the avatars come from. Several folders are read and merged into one
 * library, in this order (on a repeated key, the first one wins, so your
 * own avatars can replace Stream Avatars' ones):
 *  1. kikarinhas' own folder, $XDG_DATA_HOME/kikarinhas/avatars, if it
 *     exists;
 *  2. the folders of [avatars] paths, in the order given;
 *  3. the Stream Avatars data folder ([avatars] sa_dir, or found in the
 *     Steam libraries), unless sa_dir is "off".
 * Each folder may be a data folder (streamavatars_json.txt), a pack
 * (data.txt) or a folder of those (see kk_sa_load_dir).
 *
 * If none of that yields an avatar (a new install), the pack of default
 * avatars that comes with the program is used (kk_library_default_dir);
 * it is never mixed in with the others, so it can't change which avatar
 * anyone gets by default.
 *
 * Shared by kikarinhas and kikarinhas-config, which shows them all. */

#include <stdbool.h>
#include <stddef.h>

#include "sa.h"

/* $XDG_DATA_HOME/kikarinhas/avatars. */
bool kk_library_own_dir(char *out, size_t size);

/* The folder of the default avatars (data/default-avatars): next to the
 * installed program (../share/kikarinhas/default-avatars from its folder),
 * in the source tree (../data/default-avatars from build/), or in the
 * data folder it was built for. False if none exists. */
bool kk_library_default_dir(char *out, size_t size);

/* Calls fn for each folder to read, in order, with "~/" expanded; is_sa
 * marks the Stream Avatars one. sa_dir: NULL = search, "" = off; paths: a
 * comma-separated list (NULL = none). Returns how many. */
int kk_library_each_dir(const char *sa_dir, const char *paths,
                        void (*fn)(void *ud, const char *dir, bool is_sa),
                        void *ud);

/* Loads every folder into lib. sa_used gets the Stream Avatars folder that
 * was read ("" if none). With verbose, logs how many avatars came from
 * each. Returns -1 if no avatar was found anywhere. */
int kk_library_load(kk_sa_library *lib, const char *sa_dir, const char *paths,
                    char *sa_used, size_t size, bool verbose);
/* The same with the folder to fall back on given (NULL: none). */
int kk_library_load_with_default(kk_sa_library *lib, const char *sa_dir,
                                 const char *paths, const char *fallback,
                                 char *sa_used, size_t size, bool verbose);

#endif
