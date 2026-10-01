/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "avatar.h"

#include <ctype.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

/* Walk speed in px/s for moveSpeed 1 at scale 1. */
#define WALK_SPEED 30.0
/* Jump: gravity in px/s² and peak height relative to the sprite height. */
#define GRAVITY 1400.0
#define JUMP_HEIGHT 0.6
/* Gap between the feet and the name tag. */
#define TAG_GAP 2

/* ---- animation ----------------------------------------------------------- */

static int row_frames(const kk_avatar *a, int row)
{
    if (row < 0 || row >= KK_ANIM_MAX || row >= a->sheet->rows)
        return 0;
    int n = a->def->anims[row].frames;
    return n < a->sheet->cols ? n : a->sheet->cols;
}

static bool has_row(const kk_avatar *a, int row)
{
    return row_frames(a, row) > 0;
}

/* Starts playing row. max_loops 0 loops forever; otherwise the last frame
 * is held once done (plus hold seconds). */
static void play(kk_avatar *a, int row, int max_loops, double hold)
{
    a->anim = (kk_anim){.row = row, .max_loops = max_loops, .hold = hold};
}

static double row_fps(const kk_avatar *a, int row)
{
    return row >= 0 && row < KK_ANIM_MAX ? a->def->anims[row].fps : 0.0;
}

static void anim_update(kk_avatar *a, double dt)
{
    kk_anim *an = &a->anim;
    int n = row_frames(a, an->row);
    if (an->done) {
        an->hold -= dt;
        return;
    }
    if (n <= 1) {
        an->frame = 0;
        if (an->max_loops)
            an->done = true;
        return;
    }
    double spf = 1.0 / row_fps(a, an->row);
    an->t += dt;
    while (an->t >= spf) {
        an->t -= spf;
        if (++an->frame < n)
            continue;
        an->loops++;
        if (an->max_loops && an->loops >= an->max_loops) {
            an->frame = n - 1;
            an->done = true;
            return;
        }
        an->frame = 0;
    }
}

static bool anim_finished(const kk_avatar *a)
{
    return a->anim.done && a->anim.hold <= 0.0;
}

/* ---- behaviour ----------------------------------------------------------- */

static int idle_row(const kk_avatar *a)
{
    return has_row(a, KK_ANIM_IDLE) ? KK_ANIM_IDLE : KK_ANIM_WALK;
}

static int walk_row(const kk_avatar *a)
{
    return has_row(a, KK_ANIM_WALK) ? KK_ANIM_WALK : KK_ANIM_IDLE;
}

static void enter_idle(kk_avatar *a, kk_rng *rng)
{
    a->state = KK_ST_IDLE;
    a->timer = kk_rng_range(rng, 1.5, 5.0);
    int row = idle_row(a);
    /* Without an idle row, stand still on the first walk frame. */
    play(a, row, row == KK_ANIM_IDLE ? 0 : 1, 0.0);
}

static void enter_walk(kk_avatar *a, int width, kk_rng *rng)
{
    double margin = a->sheet->cell_w / 2.0;
    double lo = margin, hi = width - margin;
    if (hi <= lo) {
        enter_idle(a, rng);
        return;
    }
    /* Pick a destination at least a body length away when possible. */
    for (int tries = 0; tries < 4; tries++) {
        a->target = kk_rng_range(rng, lo, hi);
        if (fabs(a->target - a->x) >= a->sheet->cell_w)
            break;
    }
    a->left = a->target < a->x;
    a->state = KK_ST_WALK;
    play(a, walk_row(a), 0, 0.0);
}

/* Custom animations that make sense as a random emote. */
static bool emote_ok(const kk_sa_anim *an)
{
    static const char *const skip[] = {"death", "die", "dead", "attack",
                                       "hit",   "hurt", "damage"};
    if (an->frames <= 0)
        return false;
    if (!an->custom_name)
        return true;
    char lower[64];
    size_t i = 0;
    for (; an->custom_name[i] && i < sizeof lower - 1; i++)
        lower[i] = (char)tolower((unsigned char)an->custom_name[i]);
    lower[i] = '\0';
    for (size_t k = 0; k < sizeof skip / sizeof skip[0]; k++)
        if (strstr(lower, skip[k]))
            return false;
    return true;
}

static bool enter_emote(kk_avatar *a, kk_rng *rng)
{
    int rows[KK_ANIM_MAX], n = 0;
    for (int r = KK_ANIM_CUSTOM1; r < a->def->n_anims; r++)
        if (has_row(a, r) && emote_ok(&a->def->anims[r]))
            rows[n++] = r;
    if (n == 0)
        return false;
    int row = rows[kk_rng_int(rng, n)];
    const kk_sa_anim *an = &a->def->anims[row];
    a->state = KK_ST_EMOTE;
    play(a, row, an->loops ? an->loop_count : 1, an->hold_last);
    return true;
}

void kk_avatar_jump(kk_avatar *a)
{
    if (a->state == KK_ST_JUMP)
        return;
    double h = a->sheet->cell_h * JUMP_HEIGHT;
    a->vy = sqrt(2.0 * GRAVITY * h);
    a->state = KK_ST_JUMP;
    play(a, has_row(a, KK_ANIM_JUMP) ? KK_ANIM_JUMP : idle_row(a), 1, 0.0);
}

static void pick_next(kk_avatar *a, int width, kk_rng *rng)
{
    double r = kk_rng_range(rng, 0.0, 1.0);
    if (r < 0.55) {
        enter_walk(a, width, rng);
    } else if (r < 0.70 && has_row(a, KK_ANIM_SIT)) {
        a->state = KK_ST_SIT;
        play(a, KK_ANIM_SIT, 1, 0.0);
    } else if (r < 0.80) {
        kk_avatar_jump(a);
    } else if (r < 0.90 && enter_emote(a, rng)) {
        /* emote started */
    } else {
        enter_idle(a, rng);
    }
}

static void set_bubble(kk_avatar *a, cairo_surface_t *bubble, double seconds)
{
    if (a->bubble)
        cairo_surface_destroy(a->bubble);
    a->bubble = bubble;
    a->bubble_w = bubble ? cairo_image_surface_get_width(bubble) : 0;
    a->bubble_h = bubble ? cairo_image_surface_get_height(bubble) : 0;
    a->bubble_left = seconds;
    a->bubble_serial++;
}

void kk_avatar_say(kk_avatar *a, cairo_surface_t *bubble, double seconds)
{
    set_bubble(a, bubble, seconds);
    a->quiet = 0.0;
}

void kk_avatar_update(kk_avatar *a, double dt, int width, kk_rng *rng)
{
    anim_update(a, dt);
    a->quiet += dt;
    if (a->bubble) {
        a->bubble_left -= dt;
        if (a->bubble_left <= 0.0)
            set_bubble(a, NULL, 0.0);
    }

    switch (a->state) {
    case KK_ST_IDLE:
        a->timer -= dt;
        if (a->timer <= 0.0)
            pick_next(a, width, rng);
        break;
    case KK_ST_WALK: {
        double step = a->speed * dt;
        if (fabs(a->target - a->x) <= step) {
            a->x = a->target;
            enter_idle(a, rng);
        } else {
            a->x += a->left ? -step : step;
        }
        break;
    }
    case KK_ST_SIT:
        if (anim_finished(a)) {
            a->state = KK_ST_SITTING;
            a->timer = kk_rng_range(rng, 3.0, 9.0);
        }
        break;
    case KK_ST_SITTING:
        a->timer -= dt;
        if (a->timer <= 0.0) {
            if (has_row(a, KK_ANIM_STAND)) {
                a->state = KK_ST_STAND;
                play(a, KK_ANIM_STAND, 1, 0.0);
            } else {
                enter_idle(a, rng);
            }
        }
        break;
    case KK_ST_STAND:
    case KK_ST_EMOTE:
        if (anim_finished(a))
            enter_idle(a, rng);
        break;
    case KK_ST_JUMP:
        a->lift += a->vy * dt;
        a->vy -= GRAVITY * dt;
        if (a->lift <= 0.0 && a->vy < 0.0) {
            a->lift = 0.0;
            a->vy = 0.0;
            enter_idle(a, rng);
        }
        break;
    }

    /* The window may have shrunk under the avatar. */
    double half = a->sheet->cell_w / 2.0;
    if (a->x > width - half)
        a->x = fmax(half, width - half);
}

/* ---- geometry and painting ----------------------------------------------- */

void kk_avatar_init(kk_avatar *a, const kk_sa_avatar *def, const kk_sheet *sheet,
                    const char *label, const char *user_id,
                    cairo_surface_t *tag, double x, double scale, kk_rng *rng)
{
    memset(a, 0, sizeof *a);
    a->def = def;
    a->sheet = sheet;
    a->label = strdup(label);
    a->user_id = user_id ? strdup(user_id) : NULL;
    a->tag = tag;
    a->tag_w = cairo_image_surface_get_width(tag);
    a->tag_h = cairo_image_surface_get_height(tag);
    a->x = x;
    a->speed = WALK_SPEED * scale * def->move_speed;
    a->left = kk_rng_int(rng, 2);
    a->drawn_frame = -1;
    enter_idle(a, rng);
    /* Desynchronise avatars spawned together. */
    a->timer = kk_rng_range(rng, 0.2, 3.0);
}

void kk_avatar_free(kk_avatar *a)
{
    free(a->label);
    free(a->user_id);
    if (a->tag)
        cairo_surface_destroy(a->tag);
    if (a->bubble)
        cairo_surface_destroy(a->bubble);
}

static void sprite_origin(const kk_avatar *a, const kk_view *v, int *x, int *y)
{
    *x = (int)lround(a->x) - a->sheet->cell_w / 2;
    *y = v->ground_y - (a->sheet->cell_h - a->sheet->foot_pad) -
         (int)lround(a->lift);
}

static void tag_origin(const kk_avatar *a, const kk_view *v, int *x, int *y)
{
    *x = (int)lround(a->x) - a->tag_w / 2;
    *y = v->ground_y + TAG_GAP + a->tag_row * (a->tag_h - 2) -
         (int)lround(a->lift);
}

/* Above the head, kept inside the window. */
static void bubble_origin(const kk_avatar *a, const kk_view *v, int *x, int *y)
{
    int sx, sy;
    sprite_origin(a, v, &sx, &sy);
    *x = (int)lround(a->x) - a->bubble_w / 2;
    if (*x + a->bubble_w > v->width)
        *x = v->width - a->bubble_w;
    if (*x < 0)
        *x = 0;
    *y = sy + a->sheet->head_pad - a->bubble_h;
    if (*y < 0)
        *y = 0;
}

kk_rect kk_avatar_bounds(const kk_avatar *a, const kk_view *v)
{
    int sx, sy, tx, ty;
    sprite_origin(a, v, &sx, &sy);
    tag_origin(a, v, &tx, &ty);
    kk_rect r = kk_rect_union(
        (kk_rect){sx, sy, a->sheet->cell_w, a->sheet->cell_h},
        (kk_rect){tx, ty, a->tag_w, a->tag_h});
    if (a->bubble) {
        int bx, by;
        bubble_origin(a, v, &bx, &by);
        r = kk_rect_union(r, (kk_rect){bx, by, a->bubble_w, a->bubble_h});
    }
    return r;
}

bool kk_avatar_changed(const kk_avatar *a, const kk_view *v)
{
    return a->anim.frame != a->drawn_frame || a->anim.row != a->drawn_row ||
           a->left != a->drawn_left || a->bubble_serial != a->drawn_serial ||
           !kk_rect_equal(kk_avatar_bounds(a, v), a->drawn);
}

void kk_avatar_draw_body(const kk_avatar *a, cairo_t *cr, const kk_view *v)
{
    int sx, sy, tx, ty;
    sprite_origin(a, v, &sx, &sy);
    tag_origin(a, v, &tx, &ty);

    kk_sheet_draw(a->sheet, cr, a->anim.row, a->anim.frame, a->left, sx, sy);
    cairo_set_source_surface(cr, a->tag, tx, ty);
    cairo_paint(cr);
}

void kk_avatar_draw_bubble(const kk_avatar *a, cairo_t *cr, const kk_view *v)
{
    if (!a->bubble)
        return;
    int bx, by;
    bubble_origin(a, v, &bx, &by);
    cairo_set_source_surface(cr, a->bubble, bx, by);
    cairo_paint(cr);
}

void kk_avatar_mark_drawn(kk_avatar *a, const kk_view *v)
{
    a->drawn = kk_avatar_bounds(a, v);
    a->drawn_frame = a->anim.frame;
    a->drawn_row = a->anim.row;
    a->drawn_left = a->left;
    a->drawn_serial = a->bubble_serial;
}
