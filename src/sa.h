/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef KK_SA_H
#define KK_SA_H

/* Reader for Stream Avatars avatar definitions.
 *
 * Only the user's own installation is read; nothing is copied or
 * redistributed. Format (observed, see PLANO.md):
 *  - data/streamavatars_json.txt (UTF-8 with BOM): "avatarData" maps a key to
 *    an avatar; the image is data/avatars/<name>.png (case varies).
 *  - The image is a grid of width x height cells. Row = fixed animation slot
 *    (idle, walk, sit, stand, jump, custom1, custom2...), empty slots keep
 *    their row; column = frame. Checked against 194 installed avatars.
 * Fields like "loginDetails" in the same file hold tokens: never touched. */

#include <stdbool.h>

#include "util.h"

enum {
    KK_ANIM_IDLE,
    KK_ANIM_WALK,
    KK_ANIM_SIT,
    KK_ANIM_STAND,
    KK_ANIM_JUMP,
    KK_ANIM_CUSTOM1, /* customN is row KK_ANIM_CUSTOM1 + N - 1 */
    KK_ANIM_MAX = 32,
};

typedef struct {
    int frames;        /* 0 = slot unused */
    double fps;
    bool loops;        /* custom animations: repeat loop_count times */
    int loop_count;
    double hold_last;  /* seconds to hold the last frame when done */
    char *custom_name; /* e.g. "dance"; NULL for the base slots */
} kk_sa_anim;

typedef struct {
    char *key;  /* key in avatarData, lowercase */
    char *name; /* display name ("name" field) */
    char *image; /* full path of the spritesheet PNG; NULL if not found */
    int frame_w, frame_h;
    double ppu;  /* pixelsPerUnit: on-screen size = frame / ppu * scale */
    bool smooth; /* bilinearFilter; otherwise pixel art (nearest) */
    double move_speed;
    int n_anims; /* slots in use: highest used row + 1 */
    kk_sa_anim anims[KK_ANIM_MAX];
} kk_sa_avatar;

typedef struct {
    kk_sa_avatar *avatars; /* sorted by key */
    int count;
} kk_sa_library;

/* Looks for the Stream Avatars data folder in the Steam libraries listed in
 * libraryfolders.vdf (Proton prefix of app 665300). Writes it to out. */
bool kk_sa_find_data_dir(char *out, size_t size);

/* Loads every avatar from <data_dir>/streamavatars_json.txt. */
int kk_sa_load(kk_sa_library *lib, const char *data_dir);
void kk_sa_free(kk_sa_library *lib);

/* Case-insensitive lookup by key or display name. */
const kk_sa_avatar *kk_sa_find(const kk_sa_library *lib, const char *name);

#endif
