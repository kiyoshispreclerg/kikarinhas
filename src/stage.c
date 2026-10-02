/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "stage.h"

#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>

#include "log.h"

#define TAG_FONT "Sans Bold"
#define TAG_SIZE 11.0
#define TAG_OUTLINE 3.0
#define TAG_ROWS 2
#define TAG_SPACING 4
#define BUBBLE_FONT "Sans"
#define BUBBLE_SIZE 10.0
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

/* Bubble with up to max_lines wrapped lines of plain text (never parsed as
 * markup), the first bold_len bytes in bold, on a fill-coloured body. */
static cairo_surface_t *render_text_bubble(kk_stage *s, const char *text,
                                           int bold_len, rgb fill, int max_lines)
{
    PangoLayout *layout = pango_layout_new(s->pango);
    pango_layout_set_font_description(layout, s->bubble_font);
    pango_layout_set_width(layout, BUBBLE_WIDTH * PANGO_SCALE);
    pango_layout_set_wrap(layout, PANGO_WRAP_WORD_CHAR);
    pango_layout_set_height(layout, -max_lines);
    pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_END);
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

/* A chat message: paid ones get the amount on top in bold and a gold body,
 * new members a green one. Nothing when bubbles are off. */
static cairo_surface_t *render_bubble(kk_stage *s, const kk_chat_msg *m)
{
    if (!s->cfg.show_bubbles)
        return NULL;
    char text[600];
    int bold_len = 0;
    if (m->kind == KK_MSG_PAID && m->amount && m->amount[0]) {
        snprintf(text, sizeof text, "%s%s%s", m->amount, m->text[0] ? "\n" : "",
                 m->text);
        bold_len = (int)strlen(m->amount);
    } else {
        snprintf(text, sizeof text, "%s", m->text);
    }
    rgb fill = {1.0, 1.0, 1.0};
    if (m->kind == KK_MSG_PAID)
        fill = (rgb){1.0, 0.84, 0.35};
    else if (m->kind == KK_MSG_MEMBER)
        fill = (rgb){0.62, 0.95, 0.68};
    return render_text_bubble(s, text, bold_len, fill, BUBBLE_LINES);
}

/* Fixed by the config, else long messages stay up longer, within limits. */
static double bubble_seconds(const kk_stage *s, const char *text)
{
    if (s->cfg.bubble_seconds > 0)
        return s->cfg.bubble_seconds;
    double t = 3.5 + 0.06 * (double)strlen(text);
    return t < 4.0 ? 4.0 : t > 12.0 ? 12.0 : t;
}

/* ---- setup --------------------------------------------------------------- */

/* family may be NULL/empty (use dflt); size <= 0 uses dflt_size. */
static PangoFontDescription *make_font(const char *family, const char *dflt,
                                       double size, double dflt_size)
{
    PangoFontDescription *f = pango_font_description_from_string(
        family && family[0] ? family : dflt);
    if (f)
        pango_font_description_set_size(
            f, (int)lround((size > 0 ? size : dflt_size) * PANGO_SCALE));
    return f;
}

int kk_stage_init(kk_stage *s, const kk_sa_library *lib,
                  const kk_stage_config *cfg)
{
    memset(s, 0, sizeof *s);
    s->lib = lib;
    s->cfg = *cfg;
    s->cfg.name_font = s->cfg.bubble_font = NULL; /* only used below */
    kk_rng_seed(&s->rng, cfg->seed);

    size_t n = lib->count > 0 ? (size_t)lib->count : 1;
    s->sheets = calloc(n, sizeof *s->sheets);
    s->sheet_state = calloc(n, sizeof *s->sheet_state);
    s->usable = calloc(n, sizeof *s->usable);
    size_t np = lib->n_pieces > 0 ? (size_t)lib->n_pieces : 1;
    s->gear_sheets = calloc(np, sizeof *s->gear_sheets);
    s->gear_state = calloc(np, sizeof *s->gear_state);
    if (!s->sheets || !s->sheet_state || !s->usable || !s->gear_sheets ||
        !s->gear_state)
        return -1;
    for (int i = 0; i < lib->count; i++) {
        const kk_sa_avatar *a = &lib->avatars[i];
        if (a->image && (a->anims[KK_ANIM_IDLE].frames ||
                         a->anims[KK_ANIM_WALK].frames))
            s->usable[s->n_usable++] = i;
    }

    s->pango = pango_font_map_create_context(pango_cairo_font_map_get_default());
    s->tag_font = make_font(cfg->name_font, TAG_FONT, cfg->name_size, TAG_SIZE);
    s->bubble_font = make_font(cfg->bubble_font, BUBBLE_FONT, cfg->bubble_size,
                               BUBBLE_SIZE);
    if (!s->tag_font || !s->bubble_font)
        return -1;

    if (s->cfg.ground_margin < 0) {
        if (s->cfg.show_names && !s->cfg.name_above) {
            /* CJK fallback fonts are taller than Latin ones: measure both. */
            cairo_surface_t *probe = render_tag(s, "Ág日本語", (rgb){1, 1, 1});
            int h = cairo_image_surface_get_height(probe);
            s->cfg.ground_margin = TAG_ROWS * (h - 2) + 4;
            cairo_surface_destroy(probe);
        } else {
            s->cfg.ground_margin = 4;
        }
    }
    return 0;
}

void kk_stage_set_bubble_font(kk_stage *s, const char *font, double size)
{
    PangoFontDescription *f = make_font(font, BUBBLE_FONT, size, BUBBLE_SIZE);
    if (!f)
        return;
    pango_font_description_free(s->bubble_font);
    s->bubble_font = f;
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
    while (s->palette_sheets) {
        kk_palette_sheet *ps = s->palette_sheets;
        s->palette_sheets = ps->next;
        kk_sheet_free(&ps->sheet);
        free(ps);
    }
    if (s->gear_sheets)
        for (int i = 0; i < s->lib->n_pieces; i++)
            kk_sheet_free(&s->gear_sheets[i]);
    free(s->gear_sheets);
    free(s->gear_state);
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
                     .width = s->width,
                     .show_names = s->cfg.show_names,
                     .name_above = s->cfg.name_above};
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
                                def->smooth, def->n_anims, max_frames(def),
                                NULL) == 0;
        s->sheet_state[i] = ok ? 1 : -1;
    }
    return s->sheet_state[i] == 1 ? &s->sheets[i] : NULL;
}

/* The avatar's sheet in palette (-1 = original), loaded once. */
static const kk_sheet *look_sheet(kk_stage *s, const kk_sa_avatar *def,
                                  int palette)
{
    if (palette < 0 || palette >= def->n_palettes)
        return kk_stage_sheet(s, def);
    int idx = (int)(def - s->lib->avatars);
    for (kk_palette_sheet *ps = s->palette_sheets; ps; ps = ps->next)
        if (ps->avatar == idx && ps->palette == palette)
            return ps->ok ? &ps->sheet : NULL;

    kk_palette_sheet *ps = calloc(1, sizeof *ps);
    if (!ps)
        return NULL;
    ps->avatar = idx;
    ps->palette = palette;
    const kk_sa_palette *pal = &def->palettes[palette];
    kk_recolor rc = {def->main_colors, pal->colors,
                     pal->n < def->n_main_colors ? pal->n : def->n_main_colors};
    ps->ok = def->image && def->n_anims > 0 &&
             kk_sheet_load(&ps->sheet, def->image, def->frame_w, def->frame_h,
                           s->cfg.scale / def->ppu, def->smooth, def->n_anims,
                           max_frames(def), &rc) == 0;
    ps->next = s->palette_sheets;
    s->palette_sheets = ps;
    return ps->ok ? &ps->sheet : NULL;
}

static const kk_sheet *gear_sheet(kk_stage *s, const kk_sa_piece *p)
{
    if (s->gear_state[p->id] == 0) {
        bool ok = p->image && p->w > 0 && p->h > 0 &&
                  kk_sheet_load(&s->gear_sheets[p->id], p->image, p->w, p->h,
                                s->cfg.scale / p->ppu, false, 0, 0, NULL) == 0;
        s->gear_state[p->id] = ok ? 1 : -1;
    }
    return s->gear_state[p->id] == 1 ? &s->gear_sheets[p->id] : NULL;
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

/* ---- looks --------------------------------------------------------------- */

static const char *user_key(const kk_avatar *a)
{
    return a->user_id;
}

static void save_field(kk_stage *s, const kk_avatar *a, kk_user_field f,
                       const char *value)
{
    if (s->cfg.users && user_key(a))
        kk_users_set(s->cfg.users, user_key(a), f, value);
}

/* "set/piece,set/piece" of what a wears. */
static void save_gear(kk_stage *s, const kk_avatar *a)
{
    char buf[1024];
    size_t n = 0;
    buf[0] = '\0';
    for (int i = 0; i < a->n_gear && n < sizeof buf; i++) {
        int w = snprintf(buf + n, sizeof buf - n, "%s%s/%s", i ? "," : "",
                         s->lib->sets[a->gear[i].set].key, a->gear[i].piece->key);
        if (w < 0 || (size_t)w >= sizeof buf - n)
            break;
        n += (size_t)w;
    }
    save_field(s, a, KK_USER_GEAR, buf);
}

static bool wear(kk_stage *s, kk_avatar *a, int set, const kk_sa_piece *p)
{
    const kk_sa_gear_set *gs = &s->lib->sets[set];
    int index = (int)(p - gs->pieces);
    const kk_sheet *sh = gear_sheet(s, p);
    if (!sh)
        return false;
    kk_avatar_wear(a, set, p, index, sh);
    return true;
}

bool kk_stage_set_avatar(kk_stage *s, kk_avatar *a, const kk_sa_avatar *def)
{
    /* A palette belongs to one avatar: switching drops it. */
    const kk_sheet *sh = kk_stage_sheet(s, def);
    if (!sh)
        return false;
    kk_avatar_set_look(a, def, sh, -1, &s->rng);
    save_field(s, a, KK_USER_AVATAR, def->key);
    save_field(s, a, KK_USER_PALETTE, NULL);
    save_gear(s, a);
    return true;
}

bool kk_stage_set_palette(kk_stage *s, kk_avatar *a, int palette)
{
    const kk_sheet *sh = look_sheet(s, a->def, palette);
    if (!sh)
        return false;
    kk_avatar_set_look(a, a->def, sh, palette, &s->rng);
    save_field(s, a, KK_USER_PALETTE,
               palette >= 0 ? a->def->palettes[palette].key : NULL);
    return true;
}

bool kk_stage_wear(kk_stage *s, kk_avatar *a, const char *piece_name)
{
    int set;
    const kk_sa_piece *p = kk_sa_find_piece(s->lib, a->def, piece_name, &set);
    if (!p || !wear(s, a, set, p))
        return false;
    save_gear(s, a);
    return true;
}

void kk_stage_unwear_all(kk_stage *s, kk_avatar *a)
{
    kk_avatar_unwear(a, -1);
    save_gear(s, a);
}

/* Saved palette and gear, applied to a fresh avatar. */
static void apply_saved(kk_stage *s, kk_avatar *a)
{
    if (!s->cfg.users || !a->user_id)
        return;
    const char *pal = kk_users_get(s->cfg.users, a->user_id, KK_USER_PALETTE);
    int pi = pal ? kk_sa_find_palette(a->def, pal) : -1;
    const kk_sheet *sh = pi >= 0 ? look_sheet(s, a->def, pi) : NULL;
    if (sh)
        kk_avatar_set_look(a, a->def, sh, pi, &s->rng);

    const char *gear = kk_users_get(s->cfg.users, a->user_id, KK_USER_GEAR);
    char buf[1024];
    if (!gear || !kk_pathf(buf, sizeof buf, "%s", gear))
        return;
    char *save;
    for (char *path = strtok_r(buf, ",", &save); path;
         path = strtok_r(NULL, ",", &save)) {
        int set;
        const kk_sa_piece *p = kk_sa_piece_by_path(s->lib, path, &set);
        bool allowed = false;
        for (int k = 0; p && k < a->def->n_gear_sets; k++)
            allowed = allowed || a->def->gear_sets[k] == set;
        if (allowed)
            wear(s, a, set, p);
    }
}

/* ---- chat ---------------------------------------------------------------- */

/* Saved choice, else the configured default, else one picked by hashing the
 * id (same person, same avatar). */
static kk_avatar *spawn_chatter(kk_stage *s, const char *key,
                                const kk_chat_msg *m)
{
    const char *saved =
        s->cfg.users ? kk_users_get(s->cfg.users, key, KK_USER_AVATAR) : NULL;
    const kk_sa_avatar *def = saved ? kk_sa_find(s->lib, saved) : NULL;
    if (!def)
        def = s->cfg.default_avatar;
    kk_avatar *a = def ? add_avatar(s, def, m->name, key, m->badges) : NULL;

    if (!a && s->n_usable > 0) {
        uint32_t start = fnv1a(key) % (uint32_t)s->n_usable;
        for (int k = 0; k < s->n_usable && !a; k++) {
            def = &s->lib->avatars[s->usable[(start + (uint32_t)k) %
                                             (uint32_t)s->n_usable]];
            a = add_avatar(s, def, m->name, key, m->badges);
        }
    }
    if (a)
        apply_saved(s, a);
    return a;
}

kk_avatar *kk_stage_chatter(kk_stage *s, const kk_chat_msg *m)
{
    char key[256];
    snprintf(key, sizeof key, "%s:%s", m->platform, m->user_id);
    /* The fake chat is not an audience worth remembering. */
    if (s->cfg.users && strcmp(m->platform, "demo") != 0)
        kk_users_seen(s->cfg.users, key, m->name, (long long)time(NULL));

    for (int i = 0; i < s->count; i++)
        if (s->avatars[i].user_id && strcmp(s->avatars[i].user_id, key) == 0) {
            s->avatars[i].quiet = 0.0;
            return &s->avatars[i];
        }

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
    return spawn_chatter(s, key, m);
}

void kk_stage_say(kk_stage *s, kk_avatar *a, const kk_chat_msg *m)
{
    kk_avatar_jump(a);
    bool has_text = m->text[0] || (m->kind == KK_MSG_PAID && m->amount && m->amount[0]);
    kk_avatar_say(a, has_text ? render_bubble(s, m) : NULL,
                  bubble_seconds(s, m->text));
}

void kk_stage_help_bubble(kk_stage *s, kk_avatar *a, const char *text,
                          int title_len, int lines, double seconds)
{
    kk_avatar_jump(a);
    kk_avatar_say(a, render_text_bubble(s, text, title_len, (rgb){0.78, 0.89, 1.0},
                                        lines),
                  seconds);
}

kk_avatar *kk_stage_find_by_name(kk_stage *s, const char *name)
{
    if (name[0] == '@')
        name++;
    if (!name[0])
        return NULL;
    for (int i = 0; i < s->count; i++)
        if (strcasecmp(s->avatars[i].label, name) == 0)
            return &s->avatars[i];
    return NULL;
}

kk_avatar *kk_stage_random_other(kk_stage *s, const kk_avatar *not)
{
    if (s->count < 2)
        return NULL;
    int k = kk_rng_int(&s->rng, s->count - 1);
    kk_avatar *b = &s->avatars[k];
    return b == not ? &s->avatars[s->count - 1] : b;
}

bool kk_stage_interact(kk_stage *s, kk_avatar *a, kk_avatar *b, kk_action act)
{
    (void)s;
    if (!b || a == b || !b->user_id)
        return false;
    kk_avatar_approach(a, b->user_id, act);
    return true;
}

static kk_avatar *find_key(kk_stage *s, const char *key)
{
    for (int i = 0; i < s->count; i++)
        if (s->avatars[i].user_id && strcmp(s->avatars[i].user_id, key) == 0)
            return &s->avatars[i];
    return NULL;
}

static cairo_surface_t *heart_bubble(kk_stage *s)
{
    kk_chat_msg m = {.text = "❤️", .kind = KK_MSG_TEXT};
    return render_bubble(s, &m);
}

/* Keeps approaching avatars heading for their goal and performs the action
 * when they get there. */
static void run_interactions(kk_stage *s)
{
    for (int i = 0; i < s->count; i++) {
        kk_avatar *a = &s->avatars[i];
        if (a->state != KK_ST_APPROACH || !a->goal_key)
            continue;
        kk_avatar *b = find_key(s, a->goal_key);
        if (!b) {
            kk_avatar_stop(a, &s->rng);
            continue;
        }
        /* Stand beside the other one, on the side we come from. */
        double gap = (a->sheet->cell_w + b->sheet->cell_w) * 0.3;
        a->target = b->x + (a->x < b->x ? -gap : gap);
        if (!a->arrived)
            continue;

        kk_action act = a->goal_act;
        kk_avatar_stop(a, &s->rng);
        a->left = b->x < a->x;
        if (act == KK_ACT_HUG) {
            kk_avatar_jump(a);
            kk_avatar_jump(b);
            kk_avatar_say(a, heart_bubble(s), 3.0);
        } else {
            if (!kk_avatar_emote(a, "attack", &s->rng))
                kk_avatar_jump(a);
            b->left = a->x < b->x;
            if (!kk_avatar_emote(b, "hurt", &s->rng) &&
                !kk_avatar_emote(b, "death", &s->rng))
                kk_avatar_jump(b);
        }
    }
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
    run_interactions(s);
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
    /* Layers may report areas partly off screen. */
    int x1 = r.x + r.w, y1 = r.y + r.h;
    r.x = r.x < 0 ? 0 : r.x;
    r.y = r.y < 0 ? 0 : r.y;
    r.w = (x1 > s->width ? s->width : x1) - r.x;
    r.h = (y1 > s->height ? s->height : y1) - r.y;
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

static void layer_damage(void *to, kk_rect r)
{
    add_damage(to, r);
}

bool kk_stage_add_layer(kk_stage *s, const kk_layer *l)
{
    if (s->n_layers == KK_MAX_LAYERS)
        return false;
    s->layers[s->n_layers++] = l;
    return true;
}

void kk_stage_remove_layer(kk_stage *s, const kk_layer *l)
{
    for (int i = 0; i < s->n_layers; i++)
        if (s->layers[i] == l) {
            memmove(&s->layers[i], &s->layers[i + 1],
                    (size_t)(s->n_layers - i - 1) * sizeof s->layers[0]);
            s->n_layers--;
            return;
        }
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
        for (int i = 0; i < s->n_layers; i++)
            s->layers[i]->damage(s->layers[i]->ud, layer_damage, s);
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
        for (int i = 0; i < s->n_layers; i++)
            s->layers[i]->draw(s->layers[i]->ud, cr, r);
        cairo_restore(cr);
    }

    for (int i = 0; i < s->count; i++)
        kk_avatar_mark_drawn(&s->avatars[i], &v);
    for (int i = 0; i < s->n_layers; i++)
        s->layers[i]->painted(s->layers[i]->ud);

    *rects = s->damage;
    return s->n_damage;
}
