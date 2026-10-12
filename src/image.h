/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef KK_IMAGE_H
#define KK_IMAGE_H

/* Pictures from files (PNG, JPEG, GIF, BMP) as Cairo surfaces, ready to
 * paint: premultiplied ARGB32, one surface per frame. Animated GIFs keep
 * every frame and its delay. */

#include <stdbool.h>
#include <stddef.h>

#include <cairo.h>

typedef struct {
    cairo_surface_t **frames;
    int *delay_ms; /* per frame; NULL for a still picture */
    int n;
    int w, h;
    int total_ms;  /* one loop of the animation */
} kk_image;

/* Reads path into img. False (and img empty) if it can't, or if it would
 * take too much memory (a long, big GIF). */
bool kk_image_load(kk_image *img, const char *path);
/* Same from bytes in memory (animated emotes). */
bool kk_image_decode(kk_image *img, const void *data, size_t len);
void kk_image_free(kk_image *img);

/* The first frame of a picture in memory (PNG, JPEG, GIF, BMP), or NULL:
 * for image emotes that only come animated (Kick's GIFs). */
cairo_surface_t *kk_image_decode_still(const void *data, size_t len);

/* The frame to show t seconds after it started, looping. */
cairo_surface_t *kk_image_frame(const kk_image *img, double t);
/* Index of that frame (to tell when it changes). */
int kk_image_frame_index(const kk_image *img, double t);

#endif
