/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef KK_EMOTES_H
#define KK_EMOTES_H

/* Emote images, ready to paint: one square ARGB surface of the same size
 * per emote, made once and shared by everything that shows it.
 *
 *  - Unicode emoji are drawn with Pango from the colour emoji font (Noto
 *    Color Emoji or whatever fontconfig picks for "emoji").
 *  - Platform emotes are PNGs downloaded through kk_http (never blocking)
 *    and kept on disk, under cache_dir/<platform>/, so a channel's emotes
 *    load instantly on the next live.
 *
 * Callers ask again until the state leaves LOADING; a surface handed out
 * stays valid until the next kk_emotes_get/set_size/free unless the caller
 * takes a reference (cairo_surface_reference), as the wall does. */

#include <stdbool.h>
#include <stddef.h>

#include <cairo.h>

#include "chat.h"
#include "http.h"

typedef struct kk_emotes kk_emotes;

typedef enum {
    KK_EMOTE_LOADING,
    KK_EMOTE_READY,
    KK_EMOTE_FAILED,
} kk_emote_state;

/* px: side of the images. cache_dir NULL: no disk cache. http NULL: only
 * Unicode emoji and the disk cache (tests). */
kk_emotes *kk_emotes_new(kk_http *http, const char *cache_dir, int px);
void kk_emotes_free(kk_emotes *e);

/* Changes the image size; everything is made again on demand. */
void kk_emotes_set_size(kk_emotes *e, int px);
int kk_emotes_size(const kk_emotes *e);

/* The image of em (em->url, or em->text for a Unicode emoji), starting its
 * download on the first call. *out is set when READY. */
kk_emote_state kk_emotes_get(kk_emotes *e, const char *platform,
                             const kk_emote *em, cairo_surface_t **out);

/* Where the image of a platform emote is kept on disk; false without a
 * cache dir. */
bool kk_emotes_file(const kk_emotes *e, const char *platform, const char *id,
                    char *out, size_t size);

/* ~/.cache/kikarinhas/emotes ($XDG_CACHE_HOME honoured). */
bool kk_emotes_default_dir(char *out, size_t size);

#endif
