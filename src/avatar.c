/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "avatar.h"

#include <ctype.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

/* Walk speed in px/s for moveSpeed 1 at scale 1. */
#define WALK_SPEED 30.0
/* Jump: gravity in px/s² and peak height relative to the sprite height. */
#define GRAVITY 1400.0
#define JUMP_HEIGHT 0.6
/* Gap between the feet and the name tag. */
#define TAG_GAP 2
/* Flying: what a bounce keeps of the speed, and the slowest landing that
 * still bounces (px/s). */
#define BOUNCE_KEEP 0.45
#define WALL_KEEP 0.6
#define MIN_BOUNCE 260.0
/* Held: how far it leans per px/s of sideways speed (radians), the most it
 * leans, and how fast it follows (1/s). */
#define SWING_PER_SPEED 0.0012
#define SWING_MAX 0.7
#define SWING_RATE 10.0
/* Slack around the sprite's pixels that still grabs it, on screen. */
#define HIT_SLACK 3.0

/* ---- size ---------------------------------------------------------------- */

static double cell_w(const kk_avatar *a)
{
    return a->sheet->cell_w * a->size;
}

static double cell_h(const kk_avatar *a)
{
    return a->sheet->cell_h * a->size;
}

double kk_avatar_width(const kk_avatar *a)
{
    return cell_w(a);
}

double kk_avatar_height(const kk_avatar *a)
{
    return (a->sheet->cell_h - a->sheet->foot_pad - a->sheet->head_pad) * a->size;
}

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
    double margin = cell_w(a) / 2.0;
    double lo = margin, hi = width - margin;
    if (hi <= lo) {
        enter_idle(a, rng);
        return;
    }
    /* Pick a destination at least a body length away when possible. */
    for (int tries = 0; tries < 4; tries++) {
        a->target = kk_rng_range(rng, lo, hi);
        if (fabs(a->target - a->x) >= cell_w(a))
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
    for (; i < sizeof lower - 1 && an->custom_name[i]; i++)
        lower[i] = (char)tolower((unsigned char)an->custom_name[i]);
    lower[i] = '\0';
    for (size_t k = 0; k < sizeof skip / sizeof skip[0]; k++)
        if (strstr(lower, skip[k]))
            return false;
    return true;
}

/* A custom animation named name (case-insensitive), or -1. */
static int named_row(const kk_avatar *a, const char *name)
{
    for (int r = KK_ANIM_CUSTOM1; r < a->def->n_anims; r++) {
        const char *cn = a->def->anims[r].custom_name;
        if (has_row(a, r) && cn && strcasecmp(cn, name) == 0)
            return r;
    }
    return -1;
}

static void play_emote_row(kk_avatar *a, int row)
{
    const kk_sa_anim *an = &a->def->anims[row];
    a->state = KK_ST_EMOTE;
    a->lift = 0.0;
    play(a, row, an->loops ? an->loop_count : 1, an->hold_last);
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
    play_emote_row(a, row);
    return true;
}

bool kk_avatar_emote(kk_avatar *a, const char *name, kk_rng *rng)
{
    if (a->state == KK_ST_JUMP || a->state == KK_ST_FLY || a->state == KK_ST_HELD)
        return false;
    if (!name)
        return enter_emote(a, rng);
    int row = named_row(a, name);
    if (row < 0)
        return false;
    play_emote_row(a, row);
    return true;
}

bool kk_avatar_sit(kk_avatar *a)
{
    if (!has_row(a, KK_ANIM_SIT) || a->state == KK_ST_JUMP || a->state == KK_ST_FLY ||
        a->state == KK_ST_HELD || a->state == KK_ST_SIT || a->state == KK_ST_SITTING)
        return false;
    a->state = KK_ST_SIT;
    play(a, KK_ANIM_SIT, 1, 0.0);
    return true;
}

void kk_avatar_approach(kk_avatar *a, const char *key, kk_action act)
{
    if (a->state == KK_ST_HELD)
        return;
    free(a->goal_key);
    a->goal_key = strdup(key);
    a->goal_act = act;
    a->arrived = false;
    a->state = KK_ST_APPROACH;
    a->timer = 12.0; /* give up after this */
    a->target = a->x;
    a->lift = 0.0;
    play(a, walk_row(a), 0, 0.0);
}

void kk_avatar_stop(kk_avatar *a, kk_rng *rng)
{
    free(a->goal_key);
    a->goal_key = NULL;
    a->goal_act = KK_ACT_NONE;
    a->arrived = false;
    enter_idle(a, rng);
}

static void drop_goal(kk_avatar *a)
{
    free(a->goal_key);
    a->goal_key = NULL;
    a->goal_act = KK_ACT_NONE;
    a->arrived = false;
}

static int air_row(const kk_avatar *a)
{
    return has_row(a, KK_ANIM_JUMP) ? KK_ANIM_JUMP : idle_row(a);
}

void kk_avatar_jump(kk_avatar *a)
{
    if (a->state == KK_ST_JUMP || a->state == KK_ST_FLY || a->state == KK_ST_HELD)
        return;
    /* Talking interrupts sitting, emotes and approaches. */
    drop_goal(a);
    double h = cell_h(a) * JUMP_HEIGHT;
    a->vy = sqrt(2.0 * GRAVITY * h);
    a->state = KK_ST_JUMP;
    play(a, air_row(a), 1, 0.0);
}

static void launch(kk_avatar *a, double vx, double vy, double spin)
{
    drop_goal(a);
    a->vx = vx;
    a->vy = vy;
    a->spin = spin;
    if (fabs(vx) > 1.0)
        a->left = vx < 0;
    /* Thrown from the ground: off it, or the landing test fires at once. */
    if (a->lift <= 0.0)
        a->lift = 0.5;
    a->state = KK_ST_FLY;
    play(a, air_row(a), 1, 0.0);
}

void kk_avatar_launch(kk_avatar *a, double vx, double vy, double spin)
{
    if (a->state != KK_ST_HELD)
        launch(a, vx, vy, spin);
}

bool kk_avatar_flying(const kk_avatar *a)
{
    return a->state == KK_ST_FLY;
}

/* What it looks like while held: a custom animation made for it, if the
 * avatar has one, else the jump. */
static void play_hang(kk_avatar *a)
{
    static const char *const names[] = {"hang", "hanging", "held", "grab", "grabbed",
                                        "carry", "carried", "fall", "falling"};
    for (size_t i = 0; i < sizeof names / sizeof names[0]; i++) {
        int row = named_row(a, names[i]);
        if (row >= 0) {
            play(a, row, 0, 0.0);
            return;
        }
    }
    play(a, air_row(a), 1, 0.0);
}

void kk_avatar_hold(kk_avatar *a)
{
    drop_goal(a);
    a->state = KK_ST_HELD;
    a->vx = a->vy = a->spin = 0.0;
    a->target = a->x;
    play_hang(a);
}

bool kk_avatar_held(const kk_avatar *a)
{
    return a->state == KK_ST_HELD;
}

void kk_avatar_hold_at(kk_avatar *a, double x, double lift, const kk_view *v)
{
    if (a->state != KK_ST_HELD)
        return;
    double half = cell_w(a) / 2.0;
    a->x = fmax(half, fmin(v->width - half, x));
    a->lift = fmax(0.0, fmin(v->ground_y - cell_h(a), lift));
}

void kk_avatar_throw(kk_avatar *a, double vx, double vy)
{
    if (a->state != KK_ST_HELD)
        return;
    /* It faces the way it goes (launch turns it), and the angle is drawn
     * mirrored when facing left: keep the lean it has on screen. */
    bool left = fabs(vx) > 1.0 ? vx < 0 : a->left;
    if (left != a->left)
        a->angle = -a->angle;
    /* Tumbles the way it was thrown, faster the faster it goes. */
    double spin = fmax(-14.0, fmin(14.0, vx / 120.0));
    launch(a, vx, vy, left ? -spin : spin);
}

/* Carried around: leans against the way it moves, like something hanging,
 * and straightens up when it stops. */
static void swing(kk_avatar *a, double dt)
{
    if (dt <= 0.0)
        return;
    double k = fmin(1.0, dt * SWING_RATE);
    a->vx += ((a->x - a->target) / dt - a->vx) * k;
    a->target = a->x;
    double lean = fmax(-SWING_MAX, fmin(SWING_MAX, a->vx * SWING_PER_SPEED));
    /* The drawing mirrors the angle of an avatar facing left. */
    if (a->left)
        lean = -lean;
    a->angle += (lean - a->angle) * k;
    if (fabs(a->angle) < 0.002)
        a->angle = 0.0;
}

bool kk_avatar_walk_to(kk_avatar *a, double x, int width)
{
    if (a->frozen || a->state == KK_ST_JUMP || a->state == KK_ST_FLY ||
        a->state == KK_ST_HELD)
        return false;
    double half = cell_w(a) / 2.0;
    x = fmax(half, fmin(width - half, x));
    if (fabs(x - a->x) < 1.0)
        return false;
    drop_goal(a);
    a->lift = 0.0;
    a->target = x;
    a->left = x < a->x;
    a->state = KK_ST_WALK;
    play(a, walk_row(a), 0, 0.0);
    return true;
}

bool kk_avatar_stand(kk_avatar *a)
{
    if (a->state != KK_ST_SIT && a->state != KK_ST_SITTING)
        return false;
    if (has_row(a, KK_ANIM_STAND)) {
        a->state = KK_ST_STAND;
        play(a, KK_ANIM_STAND, 1, 0.0);
    } else {
        a->state = KK_ST_IDLE;
        a->timer = 1.0;
        play(a, idle_row(a), idle_row(a) == KK_ANIM_IDLE ? 0 : 1, 0.0);
    }
    return true;
}

void kk_avatar_set_size(kk_avatar *a, double size)
{
    a->size = fmax(KK_AVATAR_MIN_SIZE, fmin(KK_AVATAR_MAX_SIZE, size));
}

static void pick_next(kk_avatar *a, int width, kk_rng *rng)
{
    double r = kk_rng_range(rng, 0.0, 1.0);
    /* Frozen: stays put, though it still fidgets in place. */
    if (a->frozen && r < 0.55) {
        enter_idle(a, rng);
        return;
    }
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

void kk_bubble_emote_resolve(kk_bubble_emote *e, kk_emote_anim *anim)
{
    free(e->platform);
    free(e->id);
    free(e->url);
    e->platform = e->id = e->url = NULL;
    if (anim)
        e->anim = kk_emote_anim_ref(anim);
}

static void clear_bubble_emotes(kk_avatar *a)
{
    for (int i = 0; i < a->n_bubble_emotes; i++) {
        kk_bubble_emote *e = &a->bubble_emotes[i];
        kk_bubble_emote_resolve(e, NULL);
        kk_emote_anim_unref(e->anim);
        e->anim = NULL;
    }
    a->n_bubble_emotes = 0;
}

static void set_bubble(kk_avatar *a, cairo_surface_t *bubble, double seconds)
{
    clear_bubble_emotes(a);
    a->bubble_age = 0.0;
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

void kk_avatar_say_emotes(kk_avatar *a, cairo_surface_t *bubble, double seconds,
                          const kk_bubble_emote *slots, int n)
{
    set_bubble(a, bubble, seconds);
    a->quiet = 0.0;
    for (int i = 0; i < n && i < KK_BUBBLE_MAX_EMOTES; i++)
        a->bubble_emotes[a->n_bubble_emotes++] = slots[i];
}

/* Flying: both axes, off the walls and the ceiling, bouncing on the
 * ground until too slow to. */
static void fly(kk_avatar *a, double dt, const kk_view *v, kk_rng *rng)
{
    double half = cell_w(a) / 2.0;
    a->x += a->vx * dt;
    a->lift += a->vy * dt;
    a->vy -= GRAVITY * dt;
    a->angle = fmod(a->angle + a->spin * dt, 2.0 * M_PI);
    if (a->x < half && a->vx < 0) {
        a->x = half;
        a->vx = -a->vx * WALL_KEEP;
        a->spin = -a->spin;
    } else if (a->x > v->width - half && a->vx > 0) {
        a->x = v->width - half;
        a->vx = -a->vx * WALL_KEEP;
        a->spin = -a->spin;
    }
    double top = v->ground_y - cell_h(a);
    if (a->lift > top && a->vy > 0) {
        a->lift = fmax(0.0, top);
        a->vy = -a->vy * WALL_KEEP;
    }
    if (a->lift > 0.0 || a->vy >= 0.0)
        return;
    a->lift = 0.0;
    if (-a->vy * BOUNCE_KEEP >= MIN_BOUNCE) {
        a->vy = -a->vy * BOUNCE_KEEP;
        a->vx *= BOUNCE_KEEP;
        a->spin *= BOUNCE_KEEP;
        return;
    }
    a->vx = a->vy = a->spin = a->angle = 0.0;
    enter_idle(a, rng);
}

void kk_avatar_update(kk_avatar *a, double dt, const kk_view *v, kk_rng *rng)
{
    int width = v->width;
    anim_update(a, dt);
    a->quiet += dt;
    a->clock += dt;
    double gear_fps = 0.0;
    for (int i = 0; i < a->n_gear; i++)
        if (a->gear[i].piece->animated && a->gear[i].piece->fps > gear_fps)
            gear_fps = a->gear[i].piece->fps;
    a->gear_tick = gear_fps > 0.0 ? (int)(a->clock * gear_fps) : 0;
    if (a->bubble) {
        a->bubble_age += dt;
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
        double step = a->speed * a->size * dt;
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
    case KK_ST_APPROACH: {
        a->timer -= dt;
        double gap = a->target - a->x, step = a->speed * a->size * dt;
        if (a->timer <= 0.0) {
            kk_avatar_stop(a, rng);
        } else if (fabs(gap) <= step) {
            a->x = a->target;
            a->arrived = true; /* the stage acts and calls kk_avatar_stop */
        } else {
            a->left = gap < 0;
            a->x += a->left ? -step : step;
        }
        break;
    }
    case KK_ST_JUMP:
        a->lift += a->vy * dt;
        a->vy -= GRAVITY * dt;
        if (a->lift <= 0.0 && a->vy < 0.0) {
            a->lift = 0.0;
            a->vy = 0.0;
            enter_idle(a, rng);
        }
        break;
    case KK_ST_FLY:
        fly(a, dt, v, rng);
        break;
    case KK_ST_HELD:
        swing(a, dt);
        break;
    }

    /* The window may have shrunk under the avatar. */
    double half = cell_w(a) / 2.0;
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
    a->size = 1.0;
    a->speed = WALK_SPEED * scale * def->move_speed;
    a->left = kk_rng_int(rng, 2);
    a->drawn_frame = -1;
    a->palette = -1;
    enter_idle(a, rng);
    /* Desynchronise avatars spawned together. */
    a->timer = kk_rng_range(rng, 0.2, 3.0);
}

void kk_avatar_set_look(kk_avatar *a, const kk_sa_avatar *def,
                        const kk_sheet *sheet, int palette, kk_rng *rng)
{
    bool same = a->def == def;
    /* speed = WALK_SPEED * scale * moveSpeed: swap the moveSpeed factor. */
    a->speed = a->speed / a->def->move_speed * def->move_speed;
    a->def = def;
    a->sheet = sheet;
    a->palette = palette;
    if (same)
        return;
    /* Keep only the gear the new avatar can wear. */
    int kept = 0;
    for (int i = 0; i < a->n_gear; i++) {
        bool ok = false;
        for (int k = 0; k < def->n_gear_sets; k++)
            ok = ok || def->gear_sets[k] == a->gear[i].set;
        if (ok)
            a->gear[kept++] = a->gear[i];
    }
    a->n_gear = kept;
    if (a->state == KK_ST_HELD) {
        play_hang(a); /* still in hand, in the new look */
        return;
    }
    a->lift = 0.0;
    a->vx = a->vy = a->spin = a->angle = 0.0;
    enter_idle(a, rng);
}

void kk_avatar_wear(kk_avatar *a, int set, const kk_sa_piece *piece,
                    int piece_index, const kk_sheet *sheet)
{
    kk_worn w = {set, piece, piece_index, sheet};
    int i = 0;
    while (i < a->n_gear && a->gear[i].set != set)
        i++;
    if (i == a->n_gear) {
        if (a->n_gear == KK_MAX_GEAR)
            return;
        a->n_gear++;
    }
    a->gear[i] = w;
    /* Drawn in z order: keep the list sorted. */
    for (int k = 1; k < a->n_gear; k++)
        for (int j = k; j > 0 && a->gear[j - 1].piece->z > a->gear[j].piece->z; j--) {
            kk_worn t = a->gear[j];
            a->gear[j] = a->gear[j - 1];
            a->gear[j - 1] = t;
        }
}

void kk_avatar_unwear(kk_avatar *a, int set)
{
    int kept = 0;
    for (int i = 0; i < a->n_gear; i++)
        if (set >= 0 && a->gear[i].set != set)
            a->gear[kept++] = a->gear[i];
    a->n_gear = kept;
}

void kk_avatar_free(kk_avatar *a)
{
    free(a->goal_key);
    free(a->label);
    free(a->user_id);
    if (a->tag)
        cairo_surface_destroy(a->tag);
    clear_bubble_emotes(a);
    if (a->bubble)
        cairo_surface_destroy(a->bubble);
}

static void sprite_origin(const kk_avatar *a, const kk_view *v, int *x, int *y)
{
    *x = (int)lround(a->x - cell_w(a) / 2.0);
    *y = v->ground_y - (int)lround((a->sheet->cell_h - a->sheet->foot_pad) * a->size) -
         (int)lround(a->lift);
}

/* Where the sprite is and how big, before any tumbling. */
static kk_rect sprite_rect(const kk_avatar *a, const kk_view *v)
{
    int sx, sy;
    sprite_origin(a, v, &sx, &sy);
    return (kk_rect){sx, sy, (int)ceil(cell_w(a)), (int)ceil(cell_h(a))};
}

/* The middle of the sprite: what it tumbles around. */
static void sprite_centre(const kk_avatar *a, const kk_view *v, double *cx, double *cy)
{
    int sx, sy;
    sprite_origin(a, v, &sx, &sy);
    *cx = sx + cell_w(a) / 2.0;
    *cy = sy + cell_h(a) / 2.0;
}

/* The tag is shown unless names are off, or they only show with activity
 * and this avatar has been quiet for too long (a bubble counts as activity). */
static bool tag_visible(const kk_avatar *a, const kk_view *v)
{
    if (!v->show_names)
        return false;
    return v->name_idle <= 0 || a->quiet < v->name_idle || a->bubble;
}

/* Below the feet (default), or above the head when v->name_above. Either
 * way, stacked by row so neighbours' tags don't overlap. */
static void tag_origin(const kk_avatar *a, const kk_view *v, int *x, int *y)
{
    *x = (int)lround(a->x) - a->tag_w / 2;
    if (v->name_above) {
        int sx, sy;
        sprite_origin(a, v, &sx, &sy);
        *y = sy + (int)lround(a->sheet->head_pad * a->size) - TAG_GAP - a->tag_h -
             a->tag_row * (a->tag_h - 2);
    } else {
        *y = v->ground_y + TAG_GAP + a->tag_row * (a->tag_h - 2) -
             (int)lround(a->lift);
    }
}

/* Above the head, kept inside the window; above the name tag too, when one
 * is shown there. */
static void bubble_origin(const kk_avatar *a, const kk_view *v, int *x, int *y)
{
    int sx, sy;
    sprite_origin(a, v, &sx, &sy);
    *x = (int)lround(a->x) - a->bubble_w / 2;
    if (*x + a->bubble_w > v->width)
        *x = v->width - a->bubble_w;
    if (*x < 0)
        *x = 0;
    int above_tag = tag_visible(a, v) && v->name_above
                        ? TAG_GAP + a->tag_h + a->tag_row * (a->tag_h - 2)
                        : 0;
    *y = sy + (int)lround(a->sheet->head_pad * a->size) - above_tag - a->bubble_h;
    if (*y < 0)
        *y = 0;
}

/* Where gear i goes on this frame and which of its cells to show. False if
 * hidden (no image, or its set parked out of sight on this frame). */
static bool gear_place(const kk_avatar *a, const kk_view *v, int i,
                       kk_rect *r, int *row, int *col)
{
    const kk_worn *w = &a->gear[i];
    const kk_sheet *gs = w->sheet;
    if (!gs)
        return false;
    float px, py;
    if (!kk_sa_gear_pivot(a->def, w->set, w->piece_index, a->anim.row,
                          a->anim.frame, &px, &py))
        return false;

    if (w->piece->aligned) {
        /* Same grid as the avatar: follow its animation. */
        *row = a->anim.row;
        *col = a->anim.frame;
        if (*row >= gs->rows || *col >= gs->cols)
            return false;
    } else {
        *row = 0;
        *col = w->piece->animated ? (int)(a->clock * w->piece->fps) % gs->cols : 0;
    }

    /* Feet = bottom centre of the avatar cell; the piece hangs from its own
     * bottom centre; pivots are avatar pixels with y up (see sa.h). */
    int sx, sy;
    sprite_origin(a, v, &sx, &sy);
    double k = (double)a->sheet->cell_w / a->def->frame_w * a->size;
    double dx = px * k;
    if (a->left)
        dx = -dx;
    double cx = sx + cell_w(a) / 2.0 + dx;
    double bottom = sy + cell_h(a) - py * k;
    double gw = gs->cell_w * a->size, gh = gs->cell_h * a->size;
    *r = (kk_rect){(int)lround(cx - gw / 2.0), (int)lround(bottom - gh), (int)ceil(gw),
                   (int)ceil(gh)};
    return true;
}

static void draw_gear(const kk_avatar *a, cairo_t *cr, const kk_view *v,
                      bool behind)
{
    for (int i = 0; i < a->n_gear; i++) {
        if ((a->gear[i].piece->z < 0) != behind)
            continue;
        kk_rect r;
        int row, col;
        if (gear_place(a, v, i, &r, &row, &col))
            kk_sheet_draw_zoom(a->gear[i].sheet, cr, row, col,
                               a->left && a->gear[i].piece->flips, r.x, r.y, a->size);
    }
}

kk_rect kk_avatar_bounds(const kk_avatar *a, const kk_view *v)
{
    kk_rect r = sprite_rect(a, v);
    for (int i = 0; i < a->n_gear; i++) {
        kk_rect g;
        int row, col;
        if (gear_place(a, v, i, &g, &row, &col))
            r = kk_rect_union(r, g);
    }
    if (a->angle != 0.0) {
        /* Tumbling: whatever the angle, the body stays inside the circle
         * through its farthest corner. */
        double cx, cy;
        sprite_centre(a, v, &cx, &cy);
        double dx = fmax(fabs(r.x - cx), fabs(r.x + r.w - cx));
        double dy = fmax(fabs(r.y - cy), fabs(r.y + r.h - cy));
        int rad = (int)ceil(hypot(dx, dy)) + 1;
        r = (kk_rect){(int)floor(cx) - rad, (int)floor(cy) - rad, 2 * rad + 1, 2 * rad + 1};
    }
    if (tag_visible(a, v)) {
        int tx, ty;
        tag_origin(a, v, &tx, &ty);
        r = kk_rect_union(r, (kk_rect){tx, ty, a->tag_w, a->tag_h});
    }
    if (a->bubble) {
        int bx, by;
        bubble_origin(a, v, &bx, &by);
        r = kk_rect_union(r, (kk_rect){bx, by, a->bubble_w, a->bubble_h});
    }
    return r;
}

bool kk_avatar_hit(const kk_avatar *a, const kk_view *v, int x, int y)
{
    double px = x + 0.5, py = y + 0.5;
    if (a->angle != 0.0) {
        /* Back to the upright sprite: turn the point the other way. */
        double cx, cy;
        sprite_centre(a, v, &cx, &cy);
        double t = a->left ? a->angle : -a->angle;
        double dx = px - cx, dy = py - cy;
        px = cx + dx * cos(t) - dy * sin(t);
        py = cy + dx * sin(t) + dy * cos(t);
    }
    int sx, sy;
    sprite_origin(a, v, &sx, &sy);
    double lx = (px - sx) / a->size, ly = (py - sy) / a->size;
    double r = HIT_SLACK / a->size;
    for (int j = -1; j <= 1; j++)
        for (int i = -1; i <= 1; i++)
            if (kk_sheet_opaque(a->sheet, a->anim.row, a->anim.frame, a->left,
                                lx + i * r, ly + j * r))
                return true;
    return false;
}

/* Which frame each emote of the bubble is on (and whether it has loaded):
 * the bubble repaints when this changes. */
static unsigned emote_sig(const kk_avatar *a)
{
    unsigned sig = 0;
    for (int i = 0; i < a->n_bubble_emotes; i++) {
        const kk_emote_anim *m = a->bubble_emotes[i].anim;
        sig = sig * 131u + (m ? (unsigned)kk_emote_anim_index(m, a->bubble_age) + 1u : 0u);
    }
    return sig;
}

bool kk_avatar_changed(const kk_avatar *a, const kk_view *v)
{
    return a->anim.frame != a->drawn_frame || a->anim.row != a->drawn_row ||
           a->left != a->drawn_left || a->angle != a->drawn_angle ||
           a->bubble_serial != a->drawn_serial ||
           emote_sig(a) != a->drawn_emote_sig ||
           a->gear_tick != a->drawn_gear_tick ||
           !kk_rect_equal(kk_avatar_bounds(a, v), a->drawn);
}

void kk_avatar_draw_body(const kk_avatar *a, cairo_t *cr, const kk_view *v)
{
    int sx, sy;
    sprite_origin(a, v, &sx, &sy);

    cairo_save(cr);
    if (a->angle != 0.0) {
        double cx, cy;
        sprite_centre(a, v, &cx, &cy);
        cairo_translate(cr, cx, cy);
        cairo_rotate(cr, a->left ? -a->angle : a->angle);
        cairo_translate(cr, -cx, -cy);
    }
    draw_gear(a, cr, v, true);
    kk_sheet_draw_zoom(a->sheet, cr, a->anim.row, a->anim.frame, a->left, sx, sy, a->size);
    draw_gear(a, cr, v, false);
    cairo_restore(cr);
    if (tag_visible(a, v)) {
        int tx, ty;
        tag_origin(a, v, &tx, &ty);
        cairo_set_source_surface(cr, a->tag, tx, ty);
        cairo_paint(cr);
    }
}

void kk_avatar_draw_bubble(const kk_avatar *a, cairo_t *cr, const kk_view *v)
{
    if (!a->bubble)
        return;
    int bx, by;
    bubble_origin(a, v, &bx, &by);
    cairo_set_source_surface(cr, a->bubble, bx, by);
    cairo_paint(cr);
    for (int i = 0; i < a->n_bubble_emotes; i++) {
        const kk_bubble_emote *e = &a->bubble_emotes[i];
        if (!e->anim)
            continue;
        cairo_surface_t *f = kk_emote_anim_frame(e->anim, a->bubble_age);
        double k = (double)e->size / cairo_image_surface_get_width(f);
        cairo_save(cr);
        cairo_translate(cr, bx + e->x, by + e->y);
        cairo_scale(cr, k, k);
        cairo_set_source_surface(cr, f, 0, 0);
        cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_GOOD);
        cairo_paint(cr);
        cairo_restore(cr);
    }
}

void kk_avatar_mark_drawn(kk_avatar *a, const kk_view *v)
{
    a->drawn = kk_avatar_bounds(a, v);
    a->drawn_frame = a->anim.frame;
    a->drawn_row = a->anim.row;
    a->drawn_left = a->left;
    a->drawn_angle = a->angle;
    a->drawn_serial = a->bubble_serial;
    a->drawn_emote_sig = emote_sig(a);
    a->drawn_gear_tick = a->gear_tick;
}
