/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef KK_OVERLAY_H
#define KK_OVERLAY_H

/* Pictures shown by chat commands (action = image): a file at a place and
 * size of the window, for some seconds, fading in and out. Animated GIFs
 * play. Each command has one slot: triggering it again while it shows
 * starts its time over. Files are read the first time they are shown and
 * kept. Drawn as a stage layer, over the avatars. */

#include <stdbool.h>

#include "commands.h"
#include "layer.h"

typedef struct kk_overlay kk_overlay;

kk_overlay *kk_overlay_new(void);
void kk_overlay_free(kk_overlay *o);
void kk_overlay_resize(kk_overlay *o, int width, int height);

/* Shows file under the slot key (the command name) with p's place, scale
 * and seconds (<= 0: 5 s). It starts on the next update. False if the file
 * can't be read. */
bool kk_overlay_show(kk_overlay *o, const char *key, const char *file,
                     const kk_cmd_params *p);
/* Like kk_overlay_show but for scenery: it stays (no end, no fades; a GIF keeps
 * playing) until kk_overlay_clear. It never loses its slot to a command's
 * picture. */
bool kk_overlay_pin(kk_overlay *o, const char *key, const char *file,
                    const kk_cmd_params *p);
/* Takes everything off the screen and forgets the files (on reload, so a
 * changed file is read again). */
void kk_overlay_clear(kk_overlay *o);

/* Moves time to now (monotonic seconds). */
void kk_overlay_update(kk_overlay *o, double now);

const kk_layer *kk_overlay_layer(kk_overlay *o);

/* For tests: pictures on screen. */
int kk_overlay_showing(const kk_overlay *o);

#endif
