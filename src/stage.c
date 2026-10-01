/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "stage.h"

#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "log.h"

#define TAG_FONT "Sans Bold 11"
#define TAG_OUTLINE 3.0
#define TAG_ROWS 2
#define TAG_SPACING 4
#define BUBBLE_FONT "Sans 10"
#define BUBBLE_WIDTH 220 /* max text width, px */
#define BUBBLE_LINES 4
#define BUBBLE_PAD 6.0
#define BUBBLE_RADIUS 8.0
#define BUBBLE_TAIL 7.0
/* Damage rectangles closer than this are merged into one. */
#define MERGE_SLACK 8

typedef struct {
    double r, g, b;
} rgb;

/* ---- name tags and bubbles ----------------------------------------------- */

static rgb tag_color(unsigned badges)
{
    if (badges & KK_BADGE_OWNER)
        return (rgb){1.0, 0.82, 0.25};
    if (badges & KK_BADGE_MOD)
        return (rgb){0.55, 0.72, 1.0};
    if (badges & KK_BADGE_MEMBER)
        return (rgb){0.45, 0.92, 0.55};
    return (rgb){1.0, 1.0, 1.0};
}

static cairo_surface_t *render_tag(kk_stage *s, const char *text, rgb color)
{
    PangoLayout *layout = pango_layout_new(s->pango);
    pango_layout_set_font_description(layout, s->tag_font);
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
    cairo_set_source_rgb(cr, color.r, color.g, color.b);
    cairo_fill(cr);
    cairo_destroy(cr);
    g_object_unref(layout);
    return surf;
}

static void bubble_path(cairo_t *cr, double w, double h)
{
    double r = BUBBLE_RADIUS, x = 0.5, y = 0.5, bw = w - 1, bh = h - 1 - BUBBLE_TAIL;
    double mid = w / 2;
    cairo_new_path(cr);
    cairo_arc(cr, x + bw - r, y + r, r, -M_PI / 2, 0);
    cairo_arc(cr, x + bw - r, y + bh - r, r, 0, M_PI / 2);
    cairo_line_to(cr, mid + BUBBLE_TAIL, y + bh);
    cairo_line_to(cr, mid, y + bh + BUBBLE_TAIL);
    cairo_line_to(cr, mid - BUBBLE_TAIL / 2, y + bh);
    cairo_arc(cr, x + r, y + bh - r, r, M_PI / 2, M_PI);
    cairo_arc(cr, x + r, y + r, r, M_PI, 3 * M_PI / 2);
    cairo_close_path(cr);
}

/* Bubble with up to BUBBLE_LINES wrapped lines; paid messages get the
 * amount on top in bold and a gold body, new members a green one. */
static cairo_surface_t *render_bubble(kk_stage *s, const kk_chat_msg *m)
{
    PangoLayout *layout = pango_layout_new(s->pango);
    pango_layout_set_font_description(layout, s->bubble_font);
    pango_layout_set_width(layout, BUBBLE_WIDTH * PANGO_SCALE);
    pango_layout_set_wrap(layout, PANGO_WRAP_WORD_CHAR);
    pango_layout_set_height(layout, -BUBBLE_LINES);
    pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_END);

    /* Plain text plus attributes: chat text is never parsed as markup. */
    char text[600];
    int bold_len = 0;
    if (m->kind == KK_MSG_PAID && m->amount && m->amount[0]) {
        snprintf(text, sizeof text, "%s%s%s", m->amount, m->text[0] ? "\n" : "",
                 m->text);
        bold_len = (int)strlen(m->amount);
    } else {
        snprintf(text, sizeof text, "%s", m->text);
    }
    pango_layout_set_text(layout, text, -1);
    if (bold_len) {
        PangoAttrList *attrs = pango_attr_list_new();
        PangoAttribute *bold = pango_attr_weight_new(PANGO_WEIGHT_BOLD);
        bold->start_index = 0;
        bold->end_index = (guint)bold_len;
        pango_attr_list_insert(attrs, bold);
        pango_layout_set_attributes(layout, attrs);
        pango_attr_list_unref(attrs);
    }

    PangoRectangle ink, logical;
    pango_layout_get_pixel_extents(layout, &ink, &logical);
    int w = logical.width + 2 * (int)BUBBLE_PAD + 1;
    int h = logical.height + 2 * (int)BUBBLE_PAD + (int)BUBBLE_TAIL + 1;
    if (w < 3 * BUBBLE_TAIL + 2 * BUBBLE_RADIUS)
        w = (int)(3 * BUBBLE_TAIL + 2 * BUBBLE_RADIUS);

    rgb fill = {1.0, 1.0, 1.0};
    if (m->kind == KK_MSG_PAID)
        fill = (rgb){1.0, 0.84, 0.35};
    else if (m->kind == KK_MSG_MEMBER)
        fill = (rgb){0.62, 0.95, 0.68};

    cairo_surface_t *surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
    cairo_t *cr = cairo_create(surf);
    bubble_path(cr, w, h);
    cairo_set_source_rgba(cr, fill.r, fill.g, fill.b, 0.94);
    cairo_fill_preserve(cr);
    cairo_set_line_width(cr, 1.0);
    cairo_set_source_rgba(cr, 0.1, 0.08, 0.12, 0.45);
    cairo_stroke(cr);

    pango_cairo_update_layout(cr, layout);
    cairo_move_to(cr, BUBBLE_PAD - logical.x, BUBBLE_PAD - logical.y);
    cairo_set_source_rgb(cr, 0.1, 0.09, 0.12);
    pango_cairo_show_layout(cr, layout);
    cairo_destroy(cr);
    g_object_unref(layout);
    return surf;
}

/* Long messages stay up longer, within limits. */
static double bubble_seconds(const char *text)
{
    double t = 3.5 + 0.06 * (double)strlen(text);
    return t < 4.0 ? 4.0 : t > 12.0 ? 12.0 : t;
}

/* ---- setup --------------------------------------------------------------- */

int kk_stage_init(kk_stage *s, const kk_sa_library *lib,
                  const kk_stage_config *cfg)
{
    memset(s, 0, sizeof *s);
    s->lib = lib;
    s->cfg = *cfg;
    kk_rng_seed(&s->rng, cfg->seed);

    size_t n = lib->count > 0 ? (size_t)lib->count : 1;
    s->sheets = calloc(n, sizeof *s->sheets);
    s->sheet_state = calloc(n, sizeof *s->sheet_state);
    s->usable = calloc(n, sizeof *s->usable);
    if (!s->sheets || !s->sheet_state || !s->usable)
        return -1;
    for (int i = 0; i < lib->count; i++) {
        const kk_sa_avatar *a = &lib->avatars[i];
        if (a->image && (a->anims[KK_ANIM_IDLE].frames ||
                         a->anims[KK_ANIM_WALK].frames))
            s->usable[s->n_usable++] = i;
    }

    s->pango = pango_font_map_create_context(pango_cairo_font_map_get_default());
    s->tag_font = pango_font_description_from_string(TAG_FONT);
    s->bubble_font = pango_font_description_from_string(BUBBLE_FONT);

    if (s->cfg.ground_margin < 0) {
        /* CJK fallback fonts are taller than Latin ones: measure both. */
        cairo_surface_t *probe = render_tag(s, "Ág日本語", (rgb){1, 1, 1});
        int h = cairo_image_surface_get_height(probe);
        s->cfg.ground_margin = TAG_ROWS * (h - 2) + 4;
        cairo_surface_destroy(probe);
    }
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
    free(s->usable);
    if (s->tag_font)
        pango_font_description_free(s->tag_font);
    if (s->bubble_font)
        pango_font_description_free(s->bubble_font);
    if (s->pango)
        g_object_unref(s->pango);
    memset(s, 0, sizeof *s);
}

void kk_stage_resize(kk_stage *s, int width, int height)
{
    s->width = width;
    s->height = height;
}

static kk_view view(const kk_stage *s)
{
    return (kk_view){.ground_y = s->height - s->cfg.ground_margin,
                     .width = s->width};
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
                                def->frame_h, s->cfg.scale / def->ppu,
                                def->smooth, def->n_anims, max_frames(def)) == 0;
        s->sheet_state[i] = ok ? 1 : -1;
    }
    return s->sheet_state[i] == 1 ? &s->sheets[i] : NULL;
}

static kk_avatar *add_avatar(kk_stage *s, const kk_sa_avatar *def,
                             const char *label, const char *user_id,
                             unsigned badges)
{
    const kk_sheet *sheet = kk_stage_sheet(s, def);
    if (!sheet)
        return NULL;
    if (s->count == s->cap) {
        int cap = s->cap ? s->cap * 2 : 16;
        kk_avatar *na = realloc(s->avatars, (size_t)cap * sizeof *na);
        if (!na)
            return NULL;
        s->avatars = na;
        s->cap = cap;
    }

    double half = sheet->cell_w / 2.0;
    double x = s->width > sheet->cell_w
                   ? kk_rng_range(&s->rng, half, s->width - half)
                   : s->width / 2.0;
    kk_avatar *a = &s->avatars[s->count++];
    kk_avatar_init(a, def, sheet, label, user_id,
                   render_tag(s, label, tag_color(badges)), x, s->cfg.scale,
                   &s->rng);
    return a;
}

int kk_stage_spawn(kk_stage *s, const kk_sa_avatar *def, const char *label)
{
    if (!add_avatar(s, def, label, NULL, 0)) {
        kk_log_warn("avatar \"%s\" sem imagem utilizável", def->name);
        return -1;
    }
    return 0;
}

static void remove_avatar(kk_stage *s, int i)
{
    s->removed = kk_rect_union(s->removed, s->avatars[i].drawn);
    kk_avatar_free(&s->avatars[i]);
    s->avatars[i] = s->avatars[--s->count];
}

/* ---- chat ---------------------------------------------------------------- */

static uint32_t fnv1a(const char *s)
{
    uint32_t h = 2166136261u;
    for (; *s; s++)
        h = (h ^ (unsigned char)*s) * 16777619u;
    return h;
}

/* Same person, same avatar (until phase 3 lets people choose). */
static kk_avatar *spawn_chatter(kk_stage *s, const char *key,
                                const kk_chat_msg *m)
{
    if (s->cfg.default_avatar)
        return add_avatar(s, s->cfg.default_avatar, m->name, key, m->badges);
    if (s->n_usable == 0)
        return NULL;
    uint32_t start = fnv1a(key) % (uint32_t)s->n_usable;
    for (int k = 0; k < s->n_usable; k++) {
        const kk_sa_avatar *def =
            &s->lib->avatars[s->usable[(start + (uint32_t)k) % (uint32_t)s->n_usable]];
        kk_avatar *a = add_avatar(s, def, m->name, key, m->badges);
        if (a)
            return a;
    }
    return NULL;
}

void kk_stage_chat(kk_stage *s, const kk_chat_msg *m)
{
    char key[256];
    snprintf(key, sizeof key, "%s:%s", m->platform, m->user_id);

    kk_avatar *a = NULL;
    for (int i = 0; i < s->count && !a; i++)
        if (s->avatars[i].user_id && strcmp(s->avatars[i].user_id, key) == 0)
            a = &s->avatars[i];

    if (!a) {
        /* Full: the chatter silent for longest makes room. */
        int chatters = 0, oldest = -1;
        for (int i = 0; i < s->count; i++) {
            if (!s->avatars[i].user_id)
                continue;
            chatters++;
            if (oldest < 0 || s->avatars[i].quiet > s->avatars[oldest].quiet)
                oldest = i;
        }
        if (chatters >= s->cfg.max_avatars && oldest >= 0)
            remove_avatar(s, oldest);
        a = spawn_chatter(s, key, m);
        if (!a)
            return;
    }

    kk_avatar_jump(a);
    bool has_text = m->text[0] || (m->kind == KK_MSG_PAID && m->amount && m->amount[0]);
    kk_avatar_say(a, has_text ? render_bubble(s, m) : NULL,
                  bubble_seconds(m->text));
}

/* ---- update -------------------------------------------------------------- */

static int compare_x(const void *pa, const void *pb)
{
    const kk_avatar *a = *(kk_avatar *const *)pa, *b = *(kk_avatar *const *)pb;
    return (a->x > b->x) - (a->x < b->x);
}

/* Left to right, each tag takes the first row where it does not touch the
 * previous tag; if none is free it overlaps on row 0. */
static void assign_tag_rows(kk_stage *s)
{
    kk_avatar *stack[256];
    kk_avatar **order = s->count <= 256 ? stack : malloc((size_t)s->count * sizeof *order);
    if (!order)
        return;
    for (int i = 0; i < s->count; i++)
        order[i] = &s->avatars[i];
    qsort(order, (size_t)s->count, sizeof *order, compare_x);

    int right[TAG_ROWS];
    for (int r = 0; r < TAG_ROWS; r++)
        right[r] = INT_MIN / 2;
    for (int i = 0; i < s->count; i++) {
        kk_avatar *a = order[i];
        int left = (int)lround(a->x) - a->tag_w / 2;
        int row = 0;
        while (row < TAG_ROWS && left < right[row] + TAG_SPACING)
            row++;
        if (row == TAG_ROWS)
            row = 0;
        a->tag_row = row;
        if (left + a->tag_w > right[row])
            right[row] = left + a->tag_w;
    }
    if (order != stack)
        free(order);
}

void kk_stage_update(kk_stage *s, double dt)
{
    for (int i = 0; i < s->count; i++)
        kk_avatar_update(&s->avatars[i], dt, s->width, &s->rng);
    for (int i = s->count - 1; i >= 0; i--)
        if (s->avatars[i].user_id && s->avatars[i].quiet > s->cfg.despawn &&
            !s->avatars[i].bubble)
            remove_avatar(s, i);
    assign_tag_rows(s);
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
    kk_view v = view(s);

    s->n_damage = 0;
    if (full) {
        add_damage(s, (kk_rect){0, 0, s->width, s->height});
    } else {
        add_damage(s, s->removed);
        for (int i = 0; i < s->count; i++) {
            kk_avatar *a = &s->avatars[i];
            if (!kk_avatar_changed(a, &v))
                continue;
            add_damage(s, a->drawn);
            add_damage(s, kk_avatar_bounds(a, &v));
        }
    }
    s->removed = (kk_rect){0, 0, 0, 0};

    for (int d = 0; d < s->n_damage; d++) {
        kk_rect r = s->damage[d];
        cairo_save(cr);
        cairo_rectangle(cr, r.x, r.y, r.w, r.h);
        cairo_clip(cr);
        cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
        cairo_paint(cr);
        cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
        for (int pass = 0; pass < 2; pass++)
            for (int i = 0; i < s->count; i++) {
                const kk_avatar *a = &s->avatars[i];
                if (!kk_rect_intersects(kk_avatar_bounds(a, &v), r))
                    continue;
                if (pass == 0)
                    kk_avatar_draw_body(a, cr, &v);
                else
                    kk_avatar_draw_bubble(a, cr, &v);
            }
        cairo_restore(cr);
    }

    for (int i = 0; i < s->count; i++)
        kk_avatar_mark_drawn(&s->avatars[i], &v);

    *rects = s->damage;
    return s->n_damage;
}
