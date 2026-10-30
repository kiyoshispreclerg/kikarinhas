/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The classic chat (see chatview.h). */
#include "chatview.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>

#include <pango/pangocairo.h>

#include "avatar.h" /* kk_bubble_emote: an emote image inside the text */
#include "image.h"

#define DEFAULT_FONT "Sans"
#define PAD 8   /* between the window edge and the messages, px */
#define INNER 6 /* between a message's box and its text, across */
/* And up and down: less without a box, so plain lines stay close. */
#define INNER_BOXED 6
#define INNER_PLAIN 2
#define GAP 4   /* between two messages */
#define MAX_TEXT 1000
#define MAX_DRAWN 256
/* Scrolled back and left alone this long, it goes back to the newest. */
#define SCROLL_RETURN 30.0
/* Seconds a message takes to fade out, at the end of its time. */
#define FADE_OUT 1.0
/* U+FFFC OBJECT REPLACEMENT CHARACTER: where an image goes. */
#define HOLE "\xEF\xBF\xBC"

typedef struct {
    uint64_t id;
    char *platform, *user_id, *name, *photo, *text, *amount;
    kk_emote *emotes; /* strings owned */
    int n_emotes;
    unsigned badges;
    kk_msg_kind kind;
    bool command, hidden;
    double t;    /* monotonic */
    time_t wall; /* for the time shown */

    /* Drawn at width made_for; NULL until needed. */
    cairo_surface_t *surf;
    int made_for, w, h;
    kk_bubble_emote slots[KK_BUBBLE_MAX_EMOTES];
    int n_slots;
    bool photo_wait; /* drawn without its picture, which is still loading */
} entry;

typedef struct {
    uint64_t id;
    kk_rect r;
} drawn;

struct kk_chatview {
    kk_emotes *images;
    kk_photos *photos;
    kk_chatview_config cfg;
    char *font_name, *bubble_font_name, *bg_path, *hide_users;
    PangoContext *pango;
    PangoFontDescription *font;
    int line_px;
    int width, height;

    entry *ring; /* history: n entries from head, oldest first */
    int cap, head, n;
    uint64_t next_id;

    char **ignored; /* user keys */
    int n_ignored;

    int scroll; /* px back from the newest */
    double last_scroll;
    uint64_t marked;

    kk_image bg;
    bool bg_tried;

    drawn shown[MAX_DRAWN];
    int n_shown;
    bool dirty;
    bool animating; /* emotes moving or loading in the last draw */
    bool waiting;   /* pictures loading in the last draw */
    double last_draw, next_change;
};

/* ---- history --------------------------------------------------------------- */

static entry *at(const kk_chatview *v, int i)
{
    return &v->ring[(v->head + i) % v->cap];
}

static void forget_drawing(entry *e)
{
    if (e->surf)
        cairo_surface_destroy(e->surf);
    e->surf = NULL;
    e->made_for = 0;
    e->photo_wait = false;
    for (int i = 0; i < e->n_slots; i++) {
        kk_bubble_emote_resolve(&e->slots[i], NULL);
        kk_emote_anim_unref(e->slots[i].anim);
    }
    e->n_slots = 0;
}

static void entry_free(entry *e)
{
    forget_drawing(e);
    free(e->platform);
    free(e->user_id);
    free(e->name);
    free(e->photo);
    free(e->text);
    free(e->amount);
    for (int i = 0; i < e->n_emotes; i++) {
        free((char *)e->emotes[i].id);
        free((char *)e->emotes[i].name);
        free((char *)e->emotes[i].url);
        free((char *)e->emotes[i].text);
    }
    free(e->emotes);
    memset(e, 0, sizeof *e);
}

static char *dup_or_null(const char *s)
{
    return s ? strdup(s) : NULL;
}

static bool entry_copy(entry *e, const kk_chat_msg *m)
{
    memset(e, 0, sizeof *e);
    e->platform = strdup(m->platform ? m->platform : "");
    e->user_id = strdup(m->user_id ? m->user_id : "");
    e->name = strdup(m->name ? m->name : "");
    e->text = strdup(m->text ? m->text : "");
    e->photo = dup_or_null(m->photo);
    e->amount = dup_or_null(m->amount);
    e->badges = m->badges;
    e->kind = m->kind;
    bool ok = e->platform && e->user_id && e->name && e->text && (e->amount || !m->amount) &&
              (e->photo || !m->photo);
    if (ok && m->n_emotes > 0) {
        e->emotes = calloc((size_t)m->n_emotes, sizeof *e->emotes);
        ok = e->emotes != NULL;
        for (int i = 0; ok && i < m->n_emotes; i++) {
            const kk_emote *s = &m->emotes[i];
            kk_emote *d = &e->emotes[e->n_emotes++];
            *d = *s;
            d->id = dup_or_null(s->id);
            d->name = dup_or_null(s->name);
            d->url = dup_or_null(s->url);
            d->text = dup_or_null(s->text);
            ok = (d->id || !s->id) && (d->name || !s->name) && (d->url || !s->url) &&
                 (d->text || !s->text);
        }
    }
    if (!ok)
        entry_free(e);
    return ok;
}

/* Keeps the newest cap entries in a ring of that size. */
static bool set_capacity(kk_chatview *v, int cap)
{
    if (cap == v->cap)
        return true;
    entry *ring = calloc((size_t)cap, sizeof *ring);
    if (!ring)
        return false;
    int drop = v->n > cap ? v->n - cap : 0;
    for (int i = 0; i < drop; i++)
        entry_free(at(v, i));
    for (int i = drop; i < v->n; i++)
        ring[i - drop] = *at(v, i);
    free(v->ring);
    v->ring = ring;
    v->cap = cap;
    v->head = 0;
    v->n -= drop;
    return true;
}

static entry *find(const kk_chatview *v, uint64_t id)
{
    for (int i = v->n - 1; i >= 0; i--)
        if (at(v, i)->id == id)
            return at(v, i);
    return NULL;
}

/* ---- setup --------------------------------------------------------------------- */

kk_chatview *kk_chatview_new(kk_emotes *emotes, kk_photos *photos)
{
    kk_chatview *v = calloc(1, sizeof *v);
    if (!v)
        return NULL;
    v->images = emotes;
    v->photos = photos;
    v->pango = pango_font_map_create_context(pango_cairo_font_map_get_default());
    v->width = v->height = 1;
    v->next_id = 1;
    kk_chatview_config c = {.size = 14, .text_color = 0xffffff, .name_color = 0xffffff,
                            .badge_colors = true, .outline_width = 2,
                            .paid_color = 0xffd24a, .member_color = 0x8ef09c,
                            .show_platform = true, .show_photo = true,
                            .hide_commands = true,
                            .history = 200};
    kk_chatview_configure(v, &c);
    if (!v->ring) {
        kk_chatview_free(v);
        return NULL;
    }
    return v;
}

static void release_bg(kk_chatview *v)
{
    kk_image_free(&v->bg);
    v->bg_tried = false;
}

void kk_chatview_release(kk_chatview *v)
{
    for (int i = 0; i < v->n; i++)
        forget_drawing(at(v, i));
    release_bg(v);
    v->n_shown = 0;
    v->dirty = true;
}

void kk_chatview_free(kk_chatview *v)
{
    if (!v)
        return;
    for (int i = 0; i < v->n; i++)
        entry_free(at(v, i));
    free(v->ring);
    for (int i = 0; i < v->n_ignored; i++)
        free(v->ignored[i]);
    free(v->ignored);
    release_bg(v);
    free(v->font_name);
    free(v->bubble_font_name);
    free(v->bg_path);
    free(v->hide_users);
    if (v->font)
        pango_font_description_free(v->font);
    g_object_unref(v->pango);
    free(v);
}

static void set_owned(char **dst, const char *s)
{
    free(*dst);
    *dst = dup_or_null(s);
}

static void measure_line(kk_chatview *v)
{
    PangoLayout *layout = pango_layout_new(v->pango);
    pango_layout_set_font_description(layout, v->font);
    pango_layout_set_text(layout, "Ág", -1);
    PangoRectangle logical;
    pango_layout_get_pixel_extents(layout, NULL, &logical);
    g_object_unref(layout);
    v->line_px = logical.height > 4 ? logical.height : 4;
}

void kk_chatview_configure(kk_chatview *v, const kk_chatview_config *cfg)
{
    bool bg_changed = cfg->background != v->cfg.background ||
                      (cfg->bg_image ? !v->bg_path || strcmp(cfg->bg_image, v->bg_path) != 0
                                     : v->bg_path != NULL);
    v->cfg = *cfg;
    set_owned(&v->font_name, cfg->font);
    set_owned(&v->bubble_font_name, cfg->bubble_font);
    set_owned(&v->bg_path, cfg->bg_image);
    set_owned(&v->hide_users, cfg->hide_users);
    v->cfg.font = v->cfg.bubble_font = v->cfg.bg_image = v->cfg.hide_users = NULL;
    if (v->cfg.history < 1)
        v->cfg.history = 1;
    set_capacity(v, v->cfg.history);

    const char *family = v->cfg.bubbles ? v->bubble_font_name : v->font_name;
    double size = v->cfg.bubbles ? v->cfg.bubble_size : v->cfg.size;
    if (v->font)
        pango_font_description_free(v->font);
    v->font = pango_font_description_from_string(family && family[0] ? family : DEFAULT_FONT);
    pango_font_description_set_size(
        v->font, (gint)lround((size > 0 ? size : v->cfg.bubbles ? 10 : 14) * PANGO_SCALE));
    measure_line(v);

    for (int i = 0; i < v->n; i++)
        forget_drawing(at(v, i));
    if (bg_changed)
        release_bg(v);
    v->dirty = true;
}

void kk_chatview_resize(kk_chatview *v, int width, int height)
{
    width = width > 1 ? width : 1;
    height = height > 1 ? height : 1;
    if (width != v->width || height != v->height)
        v->dirty = true;
    v->width = width;
    v->height = height;
}

/* ---- what is shown --------------------------------------------------------- */

/* Whether word (a name, maybe with "@") is in the comma/space separated
 * list, ignoring case. */
static bool listed(const char *list, const char *word)
{
    if (!list || !word[0])
        return false;
    size_t n = strlen(word);
    const char *p = list;
    while (*p) {
        while (*p == ',' || *p == ' ' || *p == '\t' || *p == '@')
            p++;
        const char *s = p;
        while (*p && *p != ',')
            p++;
        const char *e = p;
        while (e > s && (e[-1] == ' ' || e[-1] == '\t'))
            e--;
        if ((size_t)(e - s) == n && strncasecmp(s, word, n) == 0)
            return true;
    }
    return false;
}

static bool is_ignored(const kk_chatview *v, const entry *e)
{
    size_t lp = strlen(e->platform);
    for (int i = 0; i < v->n_ignored; i++) {
        const char *k = v->ignored[i];
        if (strncmp(k, e->platform, lp) == 0 && k[lp] == ':' && strcmp(k + lp + 1, e->user_id) == 0)
            return true;
    }
    return false;
}

static bool visible(const kk_chatview *v, const entry *e)
{
    if (e->hidden || (v->cfg.hide_commands && e->command))
        return false;
    const char *name = e->name[0] == '@' ? e->name + 1 : e->name;
    return !listed(v->hide_users, name) && !is_ignored(v, e);
}

/* ---- drawing a message ----------------------------------------------------- */

typedef struct {
    double r, g, b;
} rgb;

static rgb rgb_of(uint32_t c)
{
    return (rgb){((c >> 16) & 0xff) / 255.0, ((c >> 8) & 0xff) / 255.0, (c & 0xff) / 255.0};
}

/* Name colours: the stage's tag colours on a dark or clear background, a
 * darker take of them inside the white bubbles. */
static rgb name_rgb(const kk_chatview *v, unsigned badges)
{
    bool b = v->cfg.bubbles;
    if (v->cfg.badge_colors || b) {
        if (badges & KK_BADGE_OWNER)
            return b ? (rgb){0.62, 0.42, 0.0} : (rgb){1.0, 0.82, 0.25};
        if (badges & KK_BADGE_MOD)
            return b ? (rgb){0.15, 0.33, 0.78} : (rgb){0.55, 0.72, 1.0};
        if (badges & KK_BADGE_MEMBER)
            return b ? (rgb){0.1, 0.52, 0.2} : (rgb){0.45, 0.92, 0.55};
    }
    return b ? (rgb){0.36, 0.2, 0.46} : rgb_of(v->cfg.name_color);
}

static void attr_color(PangoAttrList *attrs, size_t from, size_t to, rgb c)
{
    PangoAttribute *a = pango_attr_foreground_new((guint16)(c.r * 65535), (guint16)(c.g * 65535),
                                                  (guint16)(c.b * 65535));
    a->start_index = (guint)from;
    a->end_index = (guint)to;
    pango_attr_list_insert(attrs, a);
}

static void attr_bold(PangoAttrList *attrs, size_t from, size_t to)
{
    PangoAttribute *a = pango_attr_weight_new(PANGO_WEIGHT_BOLD);
    a->start_index = (guint)from;
    a->end_index = (guint)to;
    pango_attr_list_insert(attrs, a);
}

static void attr_hole(PangoAttrList *attrs, size_t at_byte, int px)
{
    PangoRectangle r = {0, -px * PANGO_SCALE * 4 / 5, px * PANGO_SCALE, px * PANGO_SCALE};
    PangoAttribute *shape = pango_attr_shape_new(&r, &r);
    shape->start_index = (guint)at_byte;
    shape->end_index = (guint)at_byte + 3;
    pango_attr_list_insert(attrs, shape);
}

/* Appends s to out (size bytes, used so far); false if it doesn't fit. */
static bool append(char *out, size_t size, size_t *used, const char *s, size_t len)
{
    if (*used + len + 1 > size)
        return false;
    memcpy(out + *used, s, len);
    *used += len;
    out[*used] = '\0';
    return true;
}

/* The message text with its image emotes as holes: idx gets where they are
 * and e->slots what goes in them. */
static void message_text(kk_chatview *v, entry *e, char *out, size_t size, size_t *used,
                         int *idx)
{
    size_t len = strlen(e->text), pos = 0;
    for (int i = 0; v->images && i < e->n_emotes && e->n_slots < KK_BUBBLE_MAX_EMOTES; i++) {
        const kk_emote *em = &e->emotes[i];
        if (!em->url || em->len == 0 || em->start < pos || em->start + em->len > len ||
            *used + (em->start - pos) + 3 + 1 > size)
            continue;
        kk_emote_anim *anim;
        kk_emote_state st = kk_emotes_get_anim(v->images, e->platform, em, &anim);
        if (st == KK_EMOTE_FAILED)
            continue;
        append(out, size, used, e->text + pos, em->start - pos);
        idx[e->n_slots] = (int)*used;
        append(out, size, used, HOLE, 3);
        pos = em->start + em->len;
        kk_bubble_emote *s = &e->slots[e->n_slots++];
        *s = (kk_bubble_emote){.size = v->line_px};
        if (st == KK_EMOTE_READY) {
            s->anim = kk_emote_anim_ref(anim);
        } else {
            s->platform = strdup(e->platform);
            s->id = strdup(em->id ? em->id : "");
            s->url = strdup(em->url);
        }
    }
    size_t rest = len - pos;
    if (rest > size - *used - 1)
        rest = size - *used - 1;
    append(out, size, used, e->text + pos, rest);
}

static void rounded_rect(cairo_t *cr, double x, double y, double w, double h, double r)
{
    cairo_new_path(cr);
    cairo_arc(cr, x + w - r, y + r, r, -M_PI / 2, 0);
    cairo_arc(cr, x + w - r, y + h - r, r, 0, M_PI / 2);
    cairo_arc(cr, x + r, y + h - r, r, M_PI / 2, M_PI);
    cairo_arc(cr, x + r, y + r, r, M_PI, 3 * M_PI / 2);
    cairo_close_path(cr);
}

/* ---- platform icons and pictures ---------------------------------------- */

/* The platforms' marks, simplified and drawn as paths so they stay sharp at
 * any size: each fills a px square at (x, y). */

static void icon_youtube(cairo_t *cr, double x, double y, double px)
{
    double h = px * 0.72;
    rounded_rect(cr, x, y + (px - h) / 2, px, h, h * 0.28);
    cairo_set_source_rgb(cr, 1.0, 0.0, 0.2);
    cairo_fill(cr);
    double cx = x + px / 2, cy = y + px / 2, t = h * 0.42;
    cairo_move_to(cr, cx - t * 0.42, cy - t / 2);
    cairo_line_to(cr, cx + t * 0.58, cy);
    cairo_line_to(cr, cx - t * 0.42, cy + t / 2);
    cairo_close_path(cr);
    cairo_set_source_rgb(cr, 1, 1, 1);
    cairo_fill(cr);
}

/* The glitch: a speech bubble with two eyes, on a 2400 x 2800 grid. */
static void icon_twitch(cairo_t *cr, double x, double y, double px)
{
    double k = px / 2800.0;
    cairo_save(cr);
    cairo_translate(cr, x + (px - 2400 * k) / 2, y);
    cairo_scale(cr, k, k);
    static const double outer[][2] = {{500, 0},     {0, 500},     {0, 2300},  {600, 2300},
                                      {600, 2800},  {1100, 2300}, {1500, 2300}, {2400, 1400},
                                      {2400, 0}};
    static const double inner[][2] = {{2200, 1300}, {1800, 1700}, {1400, 1700}, {1050, 2050},
                                      {1050, 1700}, {600, 1700},  {600, 200},   {2200, 200}};
    for (size_t i = 0; i < sizeof outer / sizeof outer[0]; i++)
        cairo_line_to(cr, outer[i][0], outer[i][1]);
    cairo_close_path(cr);
    cairo_set_source_rgb(cr, 0x91 / 255.0, 0x46 / 255.0, 1.0);
    cairo_fill(cr);
    for (size_t i = 0; i < sizeof inner / sizeof inner[0]; i++)
        cairo_line_to(cr, inner[i][0], inner[i][1]);
    cairo_close_path(cr);
    cairo_set_source_rgb(cr, 1, 1, 1);
    cairo_fill(cr);
    cairo_rectangle(cr, 1700, 550, 200, 600);
    cairo_rectangle(cr, 1150, 550, 200, 600);
    cairo_set_source_rgb(cr, 0x91 / 255.0, 0x46 / 255.0, 1.0);
    cairo_fill(cr);
    cairo_restore(cr);
}

/* A blocky K on green, on a 10 x 10 grid. */
static void icon_kick(cairo_t *cr, double x, double y, double px)
{
    rounded_rect(cr, x, y, px, px, px * 0.2);
    cairo_set_source_rgb(cr, 0x53 / 255.0, 0xfc / 255.0, 0x18 / 255.0);
    cairo_fill(cr);
    static const double blocks[][4] = {
        {2, 2, 2, 6}, {4, 4, 1.2, 2}, {5, 3, 1.2, 1.6}, {5, 5.4, 1.2, 1.6},
        {6, 2, 2, 1.6}, {6, 6.4, 2, 1.6},
    };
    double u = px / 10;
    for (size_t i = 0; i < sizeof blocks / sizeof blocks[0]; i++)
        cairo_rectangle(cr, x + blocks[i][0] * u, y + blocks[i][1] * u, blocks[i][2] * u,
                        blocks[i][3] * u);
    cairo_set_source_rgb(cr, 0.04, 0.04, 0.04);
    cairo_fill(cr);
}

/* A pink-to-orange disc with a ring. */
static void icon_odysee(cairo_t *cr, double x, double y, double px)
{
    cairo_pattern_t *g = cairo_pattern_create_linear(x, y, x + px, y + px);
    cairo_pattern_add_color_stop_rgb(g, 0, 0xef / 255.0, 0x19 / 255.0, 0x70 / 255.0);
    cairo_pattern_add_color_stop_rgb(g, 1, 0xf7 / 255.0, 0x79 / 255.0, 0x37 / 255.0);
    cairo_new_path(cr);
    cairo_arc(cr, x + px / 2, y + px / 2, px / 2, 0, 2 * M_PI);
    cairo_set_source(cr, g);
    cairo_fill(cr);
    cairo_pattern_destroy(g);
    cairo_arc(cr, x + px / 2, y + px / 2, px * 0.2, 0, 2 * M_PI);
    cairo_set_line_width(cr, px * 0.12);
    cairo_set_source_rgb(cr, 1, 1, 1);
    cairo_stroke(cr);
}

/* letter (len bytes), in white in the middle of the px square at (x, y). */
static void centred_letter(kk_chatview *v, cairo_t *cr, double x, double y, double px,
                           const char *letter, size_t len)
{
    PangoLayout *layout = pango_layout_new(v->pango);
    PangoFontDescription *f = pango_font_description_from_string("Sans Bold");
    pango_font_description_set_absolute_size(f, px * 0.62 * PANGO_SCALE);
    pango_layout_set_font_description(layout, f);
    pango_layout_set_text(layout, letter, (int)len);
    PangoRectangle ink;
    pango_layout_get_pixel_extents(layout, &ink, NULL);
    pango_cairo_update_layout(cr, layout);
    cairo_move_to(cr, x + (px - ink.width) / 2.0 - ink.x, y + (px - ink.height) / 2.0 - ink.y);
    cairo_set_source_rgb(cr, 1, 1, 1);
    pango_cairo_show_layout(cr, layout);
    pango_font_description_free(f);
    g_object_unref(layout);
}

static void draw_icon(kk_chatview *v, cairo_t *cr, double x, double y, double px,
                      const char *platform)
{
    cairo_save(cr);
    if (strcmp(platform, "youtube") == 0) {
        icon_youtube(cr, x, y, px);
    } else if (strcmp(platform, "twitch") == 0) {
        icon_twitch(cr, x, y, px);
    } else if (strcmp(platform, "kick") == 0) {
        icon_kick(cr, x, y, px);
    } else if (strcmp(platform, "odysee") == 0) {
        icon_odysee(cr, x, y, px);
    } else {
        /* Bridges and the demo: a grey tile with the first letter. */
        rounded_rect(cr, x + 0.5, y + 0.5, px - 1, px - 1, px / 4.0);
        cairo_set_source_rgb(cr, 0.45, 0.45, 0.45);
        cairo_fill(cr);
        char up[2] = {(char)toupper((unsigned char)platform[0]), '\0'};
        centred_letter(v, cr, x, y, px, up[0] ? up : "?", 1);
    }
    cairo_restore(cr);
}

/* The picture in a circle; without one, a circle in a colour of the name
 * with its initial. */
static void draw_photo(kk_chatview *v, cairo_t *cr, double x, double y, double px,
                       const entry *e, kk_emote_anim *photo)
{
    cairo_save(cr);
    cairo_new_path(cr);
    cairo_arc(cr, x + px / 2, y + px / 2, px / 2, 0, 2 * M_PI);
    if (photo) {
        cairo_clip(cr);
        cairo_surface_t *f = photo->frames[0];
        int w = cairo_image_surface_get_width(f), h = cairo_image_surface_get_height(f);
        double k = px / (w < h ? w : h);
        cairo_translate(cr, x + (px - w * k) / 2, y + (px - h * k) / 2);
        cairo_scale(cr, k, k);
        cairo_set_source_surface(cr, f, 0, 0);
        cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_GOOD);
        cairo_paint(cr);
    } else {
        static const uint32_t colors[] = {0xd9534f, 0xe67e22, 0xc9a227, 0x4caf50,
                                          0x1abc9c, 0x3498db, 0x7e57c2, 0xd63384};
        uint32_t hash = 2166136261u;
        for (const char *s = e->user_id; *s; s++)
            hash = (hash ^ (unsigned char)*s) * 16777619u;
        rgb c = rgb_of(colors[hash % (sizeof colors / sizeof colors[0])]);
        cairo_set_source_rgb(cr, c.r, c.g, c.b);
        cairo_fill(cr);
        const char *name = e->name[0] == '@' && e->name[1] ? e->name + 1 : e->name;
        gunichar c0 = g_utf8_get_char_validated(name, -1);
        char up[8];
        if (name[0] && c0 != (gunichar)-1 && c0 != (gunichar)-2)
            centred_letter(v, cr, x, y, px, up,
                           (size_t)g_unichar_to_utf8(g_unichar_toupper(c0), up));
    }
    cairo_restore(cr);
}

/* Renders e at the current width. */
static bool render(kk_chatview *v, entry *e)
{
    forget_drawing(e);
    char text[MAX_TEXT + 256];
    size_t used = 0;
    text[0] = '\0';
    PangoAttrList *attrs = pango_attr_list_new();
    int icon = -1, photo = -1;
    int icon_px = v->line_px * 4 / 5, photo_px = v->line_px;
    if (v->cfg.show_platform) {
        icon = (int)used;
        append(text, sizeof text, &used, HOLE " ", 4);
        attr_hole(attrs, (size_t)icon, icon_px);
    }
    if (v->cfg.show_photo) {
        photo = (int)used;
        append(text, sizeof text, &used, HOLE " ", 4);
        attr_hole(attrs, (size_t)photo, photo_px);
    }
    if (v->cfg.show_time) {
        char hhmm[16];
        struct tm tm;
        localtime_r(&e->wall, &tm);
        size_t from = used;
        strftime(hhmm, sizeof hhmm, "%H:%M ", &tm);
        append(text, sizeof text, &used, hhmm, strlen(hhmm));
        PangoAttribute *a = pango_attr_foreground_alpha_new(65535 * 6 / 10);
        a->start_index = (guint)from;
        a->end_index = (guint)used;
        pango_attr_list_insert(attrs, a);
    }
    size_t from = used;
    append(text, sizeof text, &used, e->name, strlen(e->name));
    attr_bold(attrs, from, used);
    attr_color(attrs, from, used, name_rgb(v, e->badges));
    if (e->kind == KK_MSG_PAID && e->amount && e->amount[0]) {
        append(text, sizeof text, &used, " ", 1);
        from = used;
        append(text, sizeof text, &used, e->amount, strlen(e->amount));
        attr_bold(attrs, from, used);
    }
    if (e->text[0])
        append(text, sizeof text, &used, ": ", 2);
    int idx[KK_BUBBLE_MAX_EMOTES];
    message_text(v, e, text, sizeof text, &used, idx);
    for (int i = 0; i < e->n_slots; i++)
        attr_hole(attrs, (size_t)idx[i], v->line_px);

    int width = v->width - 2 * PAD;
    if (width < 40)
        width = 40;
    PangoLayout *layout = pango_layout_new(v->pango);
    pango_layout_set_font_description(layout, v->font);
    pango_layout_set_width(layout, (width - 2 * INNER) * PANGO_SCALE);
    pango_layout_set_wrap(layout, PANGO_WRAP_WORD_CHAR);
    pango_layout_set_text(layout, text, -1);
    pango_layout_set_attributes(layout, attrs);
    pango_attr_list_unref(attrs);
    PangoRectangle logical;
    pango_layout_get_pixel_extents(layout, NULL, &logical);

    bool tinted = e->kind != KK_MSG_TEXT;
    int inner_y = v->cfg.bubbles || tinted ? INNER_BOXED : INNER_PLAIN;
    e->w = width;
    e->h = logical.height + 2 * inner_y;
    e->surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, e->w, e->h);
    cairo_t *cr = cairo_create(e->surf);
    double ox = INNER - logical.x, oy = inner_y - logical.y;

    rgb tint = rgb_of(e->kind == KK_MSG_PAID ? v->cfg.paid_color : v->cfg.member_color);
    if (v->cfg.bubbles) {
        rgb fill = tinted ? tint : (rgb){1, 1, 1};
        rounded_rect(cr, 0.5, 0.5, e->w - 1, e->h - 1, 8);
        cairo_set_source_rgba(cr, fill.r, fill.g, fill.b, 0.94);
        cairo_fill_preserve(cr);
        cairo_set_line_width(cr, 1.0);
        cairo_set_source_rgba(cr, 0.1, 0.08, 0.12, 0.45);
        cairo_stroke(cr);
    } else if (tinted) {
        rounded_rect(cr, 0, 0, e->w, e->h, 6);
        cairo_set_source_rgba(cr, tint.r, tint.g, tint.b, 0.3);
        cairo_fill(cr);
        cairo_rectangle(cr, 0, 2, 3, e->h - 4);
        cairo_set_source_rgb(cr, tint.r, tint.g, tint.b);
        cairo_fill(cr);
    }

    pango_cairo_update_layout(cr, layout);
    if (!v->cfg.bubbles && v->cfg.outline_width > 0) {
        rgb o = rgb_of(v->cfg.outline);
        cairo_move_to(cr, ox, oy);
        pango_cairo_layout_path(cr, layout);
        cairo_set_line_width(cr, 2.0 * v->cfg.outline_width);
        cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
        cairo_set_source_rgb(cr, o.r, o.g, o.b);
        cairo_stroke(cr);
    }
    rgb fg = v->cfg.bubbles ? (rgb){0.1, 0.09, 0.12} : rgb_of(v->cfg.text_color);
    cairo_move_to(cr, ox, oy);
    cairo_set_source_rgb(cr, fg.r, fg.g, fg.b);
    pango_cairo_show_layout(cr, layout);

    PangoRectangle pos;
    if (icon >= 0) {
        pango_layout_index_to_pos(layout, icon, &pos);
        draw_icon(v, cr, ox + pos.x / PANGO_SCALE,
                  oy + pos.y / PANGO_SCALE + (pos.height / PANGO_SCALE - icon_px) / 2.0,
                  icon_px, e->platform);
    }
    if (photo >= 0) {
        kk_emote_anim *pic = NULL;
        kk_emote_state st = v->photos ? kk_photos_get(v->photos, e->platform, e->user_id,
                                                      e->name, e->photo, &pic)
                                      : KK_EMOTE_FAILED;
        pango_layout_index_to_pos(layout, photo, &pos);
        draw_photo(v, cr, ox + pos.x / PANGO_SCALE,
                   oy + pos.y / PANGO_SCALE + (pos.height / PANGO_SCALE - photo_px) / 2.0,
                   photo_px, e, st == KK_EMOTE_READY ? pic : NULL);
        e->photo_wait = st == KK_EMOTE_LOADING;
    }
    for (int i = 0; i < e->n_slots; i++) {
        pango_layout_index_to_pos(layout, idx[i], &pos);
        e->slots[i].x = (int)ox + pos.x / PANGO_SCALE;
        e->slots[i].y = (int)oy + pos.y / PANGO_SCALE + (pos.height / PANGO_SCALE - v->line_px) / 2;
    }
    cairo_destroy(cr);
    g_object_unref(layout);
    e->made_for = v->width;
    if (cairo_surface_status(e->surf) != CAIRO_STATUS_SUCCESS) {
        forget_drawing(e);
        return false;
    }
    return true;
}

static bool ready(kk_chatview *v, entry *e)
{
    return (e->surf && e->made_for == v->width) || render(v, e);
}

/* ---- adding, scrolling, marking ------------------------------------------- */

uint64_t kk_chatview_add(kk_chatview *v, const kk_chat_msg *m, bool command, double now)
{
    if (v->n == v->cap) {
        entry_free(at(v, 0));
        v->head = (v->head + 1) % v->cap;
        v->n--;
    }
    entry *e = at(v, v->n);
    if (!entry_copy(e, m))
        return 0;
    e->id = v->next_id++;
    e->command = command;
    e->t = now;
    e->wall = time(NULL);
    v->n++;
    if (visible(v, e)) {
        v->dirty = true;
        /* Scrolled back: keep the same messages in view. */
        if (v->scroll > 0 && v->n_shown > 0 && ready(v, e))
            v->scroll += e->h + GAP;
    }
    return e->id;
}

bool kk_chatview_get(const kk_chatview *v, uint64_t id, kk_chat_msg *out)
{
    const entry *e = find(v, id);
    if (!e)
        return false;
    *out = (kk_chat_msg){.platform = e->platform, .user_id = e->user_id, .name = e->name,
                         .photo = e->photo, .text = e->text, .amount = e->amount,
                         .emotes = e->emotes, .n_emotes = e->n_emotes, .badges = e->badges,
                         .kind = e->kind};
    return true;
}

void kk_chatview_scroll(kk_chatview *v, int px, double now)
{
    if (v->cfg.newest_top)
        px = -px; /* the wheel goes the way the list does */
    int s = v->scroll + px;
    v->scroll = s < 0 ? 0 : s; /* the far end is clamped when drawing */
    v->last_scroll = now;
    v->dirty = true;
}

bool kk_chatview_scrolled(const kk_chatview *v)
{
    return v->scroll > 0;
}

void kk_chatview_follow(kk_chatview *v)
{
    if (v->scroll > 0)
        v->dirty = true;
    v->scroll = 0;
}

uint64_t kk_chatview_hit(const kk_chatview *v, int x, int y)
{
    for (int i = 0; i < v->n_shown; i++) {
        kk_rect r = v->shown[i].r;
        if (x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h)
            return v->shown[i].id;
    }
    return 0;
}

void kk_chatview_set_marked(kk_chatview *v, uint64_t id)
{
    if (v->marked != id)
        v->dirty = true;
    v->marked = id;
}

uint64_t kk_chatview_marked(const kk_chatview *v)
{
    return v->marked;
}

void kk_chatview_hide(kk_chatview *v, uint64_t id)
{
    entry *e = find(v, id);
    if (e && !e->hidden) {
        e->hidden = true;
        forget_drawing(e);
        v->dirty = true;
    }
}

void kk_chatview_ignore(kk_chatview *v, const char *user_key)
{
    for (int i = 0; i < v->n_ignored; i++)
        if (strcmp(v->ignored[i], user_key) == 0)
            return;
    char **n = realloc(v->ignored, (size_t)(v->n_ignored + 1) * sizeof *n);
    if (!n)
        return;
    v->ignored = n;
    if ((v->ignored[v->n_ignored] = strdup(user_key)) != NULL)
        v->n_ignored++;
    v->dirty = true;
}

/* ---- painting ------------------------------------------------------------------ */

/* Emotes still loading get their image when it arrives. True if any did. */
static bool resolve_emotes(kk_chatview *v, entry *e)
{
    bool changed = false;
    for (int k = 0; v->images && k < e->n_slots; k++) {
        kk_bubble_emote *s = &e->slots[k];
        if (s->anim || !s->url)
            continue;
        kk_emote em = {.id = s->id, .url = s->url};
        kk_emote_anim *anim;
        kk_emote_state st = kk_emotes_get_anim(v->images, s->platform, &em, &anim);
        if (st != KK_EMOTE_LOADING) {
            kk_bubble_emote_resolve(s, st == KK_EMOTE_READY ? anim : NULL);
            changed = true;
        }
    }
    return changed;
}

/* A picture that was loading and now isn't: e is drawn again with it (or
 * with the initial, if it failed). True if so. */
static bool resolve_photo(kk_chatview *v, entry *e)
{
    kk_emote_anim *pic;
    if (!e->photo_wait || !v->photos ||
        kk_photos_get(v->photos, e->platform, e->user_id, e->name, e->photo, &pic) ==
            KK_EMOTE_LOADING)
        return false;
    forget_drawing(e);
    return true;
}

bool kk_chatview_dirty(kk_chatview *v, double now)
{
    if (v->scroll > 0 && now - v->last_scroll > SCROLL_RETURN) {
        v->scroll = 0;
        v->dirty = true;
    }
    if (v->dirty || now >= v->next_change)
        return true;
    if (!v->animating && !v->waiting)
        return false;
    bool changed = false;
    for (int i = 0; i < v->n_shown; i++) {
        entry *e = find(v, v->shown[i].id);
        if (e) {
            changed |= resolve_emotes(v, e);
            changed |= resolve_photo(v, e);
        }
    }
    /* Moving emotes: about 20 frames a second is plenty for a chat. */
    return changed || (v->animating && now - v->last_draw >= 0.05);
}

static void paint_background(kk_chatview *v, cairo_t *cr)
{
    cairo_save(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    if (v->cfg.background == KK_CHATVIEW_BG_COLOR) {
        uint32_t c = v->cfg.bg_color;
        cairo_set_source_rgba(cr, (c >> 24) / 255.0, ((c >> 16) & 0xff) / 255.0,
                              ((c >> 8) & 0xff) / 255.0, (c & 0xff) / 255.0);
    } else {
        cairo_set_source_rgba(cr, 0, 0, 0, 0);
    }
    cairo_paint(cr);
    cairo_restore(cr);
    if (v->cfg.background != KK_CHATVIEW_BG_IMAGE || !v->bg_path)
        return;
    if (!v->bg_tried) {
        v->bg_tried = true;
        kk_image_load(&v->bg, v->bg_path);
    }
    if (v->bg.n < 1)
        return;
    cairo_surface_t *s = v->bg.frames[0];
    cairo_save(cr);
    if (v->cfg.bg_tile) {
        cairo_set_source_surface(cr, s, 0, 0);
        cairo_pattern_set_extend(cairo_get_source(cr), CAIRO_EXTEND_REPEAT);
    } else {
        cairo_scale(cr, (double)v->width / v->bg.w, (double)v->height / v->bg.h);
        cairo_set_source_surface(cr, s, 0, 0);
        cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_GOOD);
        cairo_pattern_set_extend(cairo_get_source(cr), CAIRO_EXTEND_PAD);
    }
    cairo_paint(cr);
    cairo_restore(cr);
}

static void paint_entry(kk_chatview *v, cairo_t *cr, entry *e, int x, int y, double alpha,
                        double now)
{
    cairo_push_group(cr);
    if (e->id == v->marked) {
        /* Discreet: it goes to the stream too. */
        rounded_rect(cr, x - 4, y, e->w + 8, e->h, 6);
        cairo_set_source_rgba(cr, 1.0, 0.82, 0.29, 0.22);
        cairo_fill(cr);
        cairo_rectangle(cr, x - 4, y + 2, 3, e->h - 4);
        cairo_set_source_rgb(cr, 1.0, 0.82, 0.29);
        cairo_fill(cr);
    }
    cairo_set_source_surface(cr, e->surf, x, y);
    cairo_paint(cr);
    for (int k = 0; k < e->n_slots; k++) {
        const kk_bubble_emote *s = &e->slots[k];
        if (!s->anim)
            continue;
        cairo_surface_t *f = kk_emote_anim_frame(s->anim, now - e->t);
        double sc = (double)s->size / cairo_image_surface_get_width(f);
        cairo_save(cr);
        cairo_translate(cr, x + s->x, y + s->y);
        cairo_scale(cr, sc, sc);
        cairo_set_source_surface(cr, f, 0, 0);
        cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_GOOD);
        cairo_paint(cr);
        cairo_restore(cr);
    }
    cairo_pop_group_to_source(cr);
    cairo_paint_with_alpha(cr, alpha);
}

void kk_chatview_draw(kk_chatview *v, cairo_t *cr, double now)
{
    paint_background(v, cr);
    v->n_shown = 0;
    v->animating = false;
    v->waiting = false;
    v->dirty = false;
    v->last_draw = now;
    v->next_change = INFINITY;

    /* How far back one can scroll: everything shown, newest to oldest. */
    int room = v->height - 2 * PAD, total = 0;
    for (int i = v->n - 1; i >= 0; i--) {
        entry *e = at(v, i);
        if (visible(v, e) && ready(v, e))
            total += e->h + GAP;
    }
    int max_scroll = total - GAP - room;
    if (v->scroll > max_scroll)
        v->scroll = max_scroll > 0 ? max_scroll : 0;
    bool fading = v->cfg.fade > 0 && v->scroll == 0;

    bool top = v->cfg.newest_top;
    int y = top ? PAD - v->scroll : v->height - PAD + v->scroll;
    for (int i = v->n - 1; i >= 0; i--) {
        entry *e = at(v, i);
        if (!visible(v, e) || !ready(v, e))
            continue;
        double alpha = 1.0;
        if (fading) {
            double left = v->cfg.fade - (now - e->t);
            if (left <= 0)
                break; /* older ones are gone too */
            alpha = left < FADE_OUT ? left / FADE_OUT : 1.0;
            double start = e->t + v->cfg.fade - FADE_OUT;
            v->next_change = fmin(v->next_change, start > now ? start : now);
        }
        int ey = top ? y : y - e->h;
        y = top ? y + e->h + GAP : y - e->h - GAP;
        if (top ? ey >= v->height : ey + e->h <= 0)
            break;
        if (top ? ey + e->h <= 0 : ey >= v->height)
            continue; /* scrolled out on the newest side */
        resolve_emotes(v, e);
        paint_entry(v, cr, e, PAD, ey, alpha, now);
        v->waiting |= e->photo_wait;
        for (int k = 0; k < e->n_slots; k++)
            v->animating |= !e->slots[k].anim ? e->slots[k].url != NULL
                                              : e->slots[k].anim->n > 1;
        if (v->n_shown < MAX_DRAWN)
            v->shown[v->n_shown++] = (drawn){e->id, {PAD, ey, e->w, e->h}};
    }
}

int kk_chatview_count(const kk_chatview *v)
{
    return v->n;
}

int kk_chatview_drawn(const kk_chatview *v)
{
    return v->n_shown;
}
