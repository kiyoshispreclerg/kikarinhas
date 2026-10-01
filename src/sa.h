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
#include <stdint.h>

#include "cJSON.h"
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

/* A gear piece (hat, sword, ...). Placement, observed and checked by eye on
 * several avatars: the reference point is the bottom centre of the avatar
 * cell (its feet), the piece hangs from its own bottom centre, offsets are
 * avatar pixels with y up, and a piece's own pivot replaces its set's. */
typedef struct {
    char *key;   /* pieceName, lowercase */
    char *image; /* full path; NULL if not found */
    int w, h;    /* cell size in the image */
    double ppu;
    int z;       /* < 0: behind the avatar */
    bool animated; /* frames side by side, played at fps */
    bool aligned;  /* same grid as the avatar sheet: frame follows the avatar */
    bool flips;    /* mirrored when the avatar faces left */
    double fps;
    int id;        /* index over all pieces of all sets */
} kk_sa_piece;

typedef struct {
    char *key; /* set name, lowercase */
    kk_sa_piece *pieces;
    int n_pieces;
} kk_sa_gear_set;

#define KK_PIVOT_HIDDEN (-500.0f) /* sets parked far below mean "not here" */

typedef struct {
    uint8_t row, col; /* animation slot and frame */
    int16_t set;      /* index in the library's sets */
    int16_t piece;    /* index in the set, or -1 for the set's own pivot */
    float x, y;       /* avatar pixels, y up */
} kk_sa_pivot;

typedef struct {
    char *key; /* palette name, lowercase */
    uint32_t *colors; /* ARGB, same order as the main palette */
    int n;
} kk_sa_palette;

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

    int16_t *gear_sets; /* sets this avatar can wear (CanUseGear) */
    int n_gear_sets;
    kk_sa_pivot *pivots; /* sparse; missing set pivot = (0, 0) */
    int n_pivots;

    uint32_t *main_colors; /* ARGB colours that palettes replace */
    int n_main_colors;
    kk_sa_palette *palettes;
    int n_palettes;
} kk_sa_avatar;

typedef struct {
    kk_sa_avatar *avatars; /* sorted by key */
    int count;
    kk_sa_gear_set *sets;
    int n_sets;
    int n_pieces; /* over all sets */
} kk_sa_library;

/* Looks for the Stream Avatars data folder in the Steam libraries listed in
 * libraryfolders.vdf (Proton prefix of app 665300). Writes it to out. */
bool kk_sa_find_data_dir(char *out, size_t size);

/* Loads every avatar and gear set from <data_dir>/streamavatars_json.txt. */
int kk_sa_load(kk_sa_library *lib, const char *data_dir);
/* Same from an already parsed document (data_dir locates the images). */
int kk_sa_parse(kk_sa_library *lib, const cJSON *root, const char *data_dir);
void kk_sa_free(kk_sa_library *lib);

/* Case-insensitive lookup by key or display name. */
const kk_sa_avatar *kk_sa_find(const kk_sa_library *lib, const char *name);

/* Gear piece by name among the sets av can wear; *set gets its set index. */
const kk_sa_piece *kk_sa_find_piece(const kk_sa_library *lib,
                                    const kk_sa_avatar *av, const char *name,
                                    int *set);
/* Same, by "set/piece" as stored in the users file. */
const kk_sa_piece *kk_sa_piece_by_path(const kk_sa_library *lib,
                                       const char *path, int *set);

/* Palette index of av by name, or -1. */
int kk_sa_find_palette(const kk_sa_avatar *av, const char *name);

/* Pivot of a gear piece on frame (row, col): the piece's own, else its set's,
 * else (0, 0). Returns false if the set is parked out of sight. */
bool kk_sa_gear_pivot(const kk_sa_avatar *av, int set, int piece, int row,
                      int col, float *x, float *y);

/* Who wears what in Stream Avatars ("userData"), for a one-off import. */
typedef struct {
    const char *key;     /* "youtube:UC..." or "twitch:123" */
    const char *avatar;  /* NULL if none */
    const char *palette; /* NULL if none */
    const char *gear[16]; /* "set/piece" */
    int n_gear;
} kk_sa_user;

typedef void (*kk_sa_user_cb)(void *ud, const kk_sa_user *u);
int kk_sa_read_users(const char *data_dir, kk_sa_user_cb cb, void *ud);
/* Same from an already parsed document. Returns how many users. */
int kk_sa_parse_users(const cJSON *root, kk_sa_user_cb cb, void *ud);

#endif
