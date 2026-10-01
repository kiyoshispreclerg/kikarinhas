/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "stage.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "log.h"

#define TAG_FONT "Sans Bold 11"
#define TAG_OUTLINE 3.0
/* Damage rectangles closer than this are merged into one. */
#define MERGE_SLACK 8

/* ---- name tags ----------------------------------------------------------- */

static cairo_surface_t *render_tag(kk_stage *s, const char *text)
{
    PangoLayout *layout = pango_layout_new(s->pango);
    pango_layout_set_font_description(layout, s->font);
    pango_layout_set_text(layout, text, -1);

    PangoRectangle ink, logical;
    pango_layout_get_pixel_extents(layout, &ink, &logical);
    int pad = (int)ceil(TAG_OUTLINE);
    int w = logical.width + 2 * pad, h = logical.height + 2 * pad;

    cairo_surface_t *surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
    cairo_t *cr = cairo_create(surf);
    pango_cairo_update_layout(cr, layout);
    cairo_move_to(cr, pad - logical.x, pad - logical.y);
    pango_cairo_layout_path(cr, layout);
    cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
    cairo_set_line_width(cr, TAG_OUTLINE);
    cairo_set_source_rgba(cr, 0.08, 0.05, 0.1, 0.85);
    cairo_stroke_preserve(cr);
    cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
    cairo_fill(cr);
    cairo_destroy(cr);
    g_object_unref(layout);
    return surf;
}

/* ---- setup --------------------------------------------------------------- */

int kk_stage_init(kk_stage *s, const kk_sa_library *lib, double scale,
                  int ground_margin, uint64_t seed)
{
    memset(s, 0, sizeof *s);
    s->lib = lib;
    s->scale = scale;
    kk_rng_seed(&s->rng, seed);

    size_t n = lib->count > 0 ? (size_t)lib->count : 1;
    s->sheets = calloc(n, sizeof *s->sheets);
    s->sheet_state = calloc(n, sizeof *s->sheet_state);
    if (!s->sheets || !s->sheet_state)
        return -1;

    s->pango = pango_font_map_create_context(pango_cairo_font_map_get_default());
    s->font = pango_font_description_from_string(TAG_FONT);

    if (ground_margin < 0) {
        cairo_surface_t *probe = render_tag(s, "Ág");
        ground_margin = cairo_image_surface_get_height(probe) + 4;
        cairo_surface_destroy(probe);
    }
    s->ground_margin = ground_margin;
    return 0;
}

void kk_stage_free(kk_stage *s)
{
    for (int i = 0; i < s->count; i++)
        kk_avatar_free(&s->avatars[i]);
    free(s->avatars);
    if (s->sheets)
        for (int i = 0; i < s->lib->count; i++)
            kk_sheet_free(&s->sheets[i]);
    free(s->sheets);
    free(s->sheet_state);
    if (s->font)
        pango_font_description_free(s->font);
    if (s->pango)
        g_object_unref(s->pango);
    memset(s, 0, sizeof *s);
}

void kk_stage_resize(kk_stage *s, int width, int height)
{
    s->width = width;
    s->height = height;
}

static int ground_y(const kk_stage *s)
{
    return s->height - s->ground_margin;
}

static int max_frames(const kk_sa_avatar *a)
{
    int m = 0;
    for (int i = 0; i < a->n_anims; i++)
        if (a->anims[i].frames > m)
            m = a->anims[i].frames;
    return m;
}

const kk_sheet *kk_stage_sheet(kk_stage *s, const kk_sa_avatar *def)
{
    int i = (int)(def - s->lib->avatars);
    if (s->sheet_state[i] == 0) {
        /* Rows and columns no animation uses are dropped to save memory. */
        bool ok = def->image && def->n_anims > 0 &&
                  kk_sheet_load(&s->sheets[i], def->image, def->frame_w,
                                def->frame_h, s->scale / def->ppu, def->smooth,
                                def->n_anims, max_frames(def)) == 0;
        s->sheet_state[i] = ok ? 1 : -1;
    }
    return s->sheet_state[i] == 1 ? &s->sheets[i] : NULL;
}

int kk_stage_spawn(kk_stage *s, const kk_sa_avatar *def, const char *label)
{
    const kk_sheet *sheet = kk_stage_sheet(s, def);
    if (!sheet) {
        kk_log_warn("avatar \"%s\" sem imagem utilizável", def->name);
        return -1;
    }
    if (s->count == s->cap) {
        int cap = s->cap ? s->cap * 2 : 16;
        kk_avatar *na = realloc(s->avatars, (size_t)cap * sizeof *na);
        if (!na)
            return -1;
        s->avatars = na;
        s->cap = cap;
    }

    double half = sheet->cell_w / 2.0;
    double x = s->width > sheet->cell_w
                   ? kk_rng_range(&s->rng, half, s->width - half)
                   : s->width / 2.0;
    kk_avatar_init(&s->avatars[s->count++], def, sheet, label,
                   render_tag(s, label), x, s->scale, &s->rng);
    return 0;
}

void kk_stage_update(kk_stage *s, double dt)
{
    for (int i = 0; i < s->count; i++)
        kk_avatar_update(&s->avatars[i], dt, s->width, &s->rng);
}

/* ---- damage and painting ------------------------------------------------- */

static bool near(kk_rect a, kk_rect b)
{
    kk_rect grown = {a.x - MERGE_SLACK, a.y - MERGE_SLACK,
                     a.w + 2 * MERGE_SLACK, a.h + 2 * MERGE_SLACK};
    return kk_rect_intersects(grown, b);
}

static void add_damage(kk_stage *s, kk_rect r)
{
    if (kk_rect_empty(r))
        return;
    /* Merge with every rectangle it touches, repeating as the union grows. */
    for (int i = 0; i < s->n_damage;) {
        if (near(s->damage[i], r)) {
            r = kk_rect_union(r, s->damage[i]);
            s->damage[i] = s->damage[--s->n_damage];
            i = 0;
        } else {
            i++;
        }
    }
    if (s->n_damage == KK_MAX_DAMAGE) {
        for (int i = 0; i < s->n_damage; i++)
            r = kk_rect_union(r, s->damage[i]);
        s->n_damage = 0;
    }
    s->damage[s->n_damage++] = r;
}

int kk_stage_render(kk_stage *s, cairo_t *cr, bool full,
                    const kk_rect **rects)
{
    int gy = ground_y(s);

    s->n_damage = 0;
    if (full) {
        add_damage(s, (kk_rect){0, 0, s->width, s->height});
    } else {
        for (int i = 0; i < s->count; i++) {
            kk_avatar *a = &s->avatars[i];
            if (!kk_avatar_changed(a, gy))
                continue;
            add_damage(s, a->drawn);
            add_damage(s, kk_avatar_bounds(a, gy));
        }
    }

    for (int d = 0; d < s->n_damage; d++) {
        kk_rect r = s->damage[d];
        cairo_save(cr);
        cairo_rectangle(cr, r.x, r.y, r.w, r.h);
        cairo_clip(cr);
        cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
        cairo_paint(cr);
        cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
        for (int i = 0; i < s->count; i++)
            if (kk_rect_intersects(kk_avatar_bounds(&s->avatars[i], gy), r))
                kk_avatar_draw(&s->avatars[i], cr, gy);
        cairo_restore(cr);
    }

    for (int i = 0; i < s->count; i++)
        kk_avatar_mark_drawn(&s->avatars[i], gy);

    *rects = s->damage;
    return s->n_damage;
}
