/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Commands, the users file and the Stream Avatars gear/palette data. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../harness.h"
#include "commands.h"
#include "sa.h"
#include "users.h"
#include "util.h"

static char *fixture(const char *name)
{
    char path[KK_PATH_MAX];
    snprintf(path, sizeof path, "%s/%s", KK_FIXTURES_DIR, name);
    char *s = kk_read_file(path, NULL);
    if (!s) {
        fprintf(stderr, "missing fixture %s\n", path);
        exit(2);
    }
    return s;
}

/* ---- commands ------------------------------------------------------------ */

static int calls;
static char last_name[64], last_args[256], last_data[64];
static bool handler_result = true;

static bool record(const kk_cmd_call *c)
{
    calls++;
    snprintf(last_name, sizeof last_name, "%s", c->name);
    snprintf(last_args, sizeof last_args, "%s", c->args);
    snprintf(last_data, sizeof last_data, "%s", c->data ? c->data : "");
    return handler_result;
}

static kk_cmd_result send(kk_commands *c, const char *text, unsigned badges,
                          const char *who, double now)
{
    kk_chat_msg m = {.platform = "t", .user_id = who, .name = who, .text = text,
                     .badges = badges};
    return kk_commands_handle(c, &m, who, now);
}

TEST(commands_aliases_and_args)
{
    kk_commands *c = kk_commands_new(NULL);
    kk_commands_add(c, "sound", record, NULL, 0, 0, KK_ROLE_ANYONE);
    CHECK_INT_EQ(kk_commands_alias(c, "sound", "som"), 0);
    CHECK_INT_EQ(kk_commands_alias(c, "sound", "play"), 0);
    /* A word can't belong to two commands. */
    kk_commands_add(c, "dance", record, NULL, 0, 0, KK_ROLE_ANYONE);
    int dup = kk_commands_alias(c, "dance", "som");

    calls = 0;
    kk_cmd_result r1 = send(c, "  !SOM   buzina alta  ", 0, "a", 0);
    kk_cmd_result r2 = send(c, "！play x", 0, "b", 0); /* full-width ! */
    kk_cmd_result r3 = send(c, "som sem exclamação", 0, "a", 0);
    kk_cmd_result r4 = send(c, "! espaço", 0, "a", 0);
    kk_commands_free(c);

    CHECK_INT_EQ(dup, -1);
    CHECK_INT_EQ(r1, KK_CMD_RAN);
    CHECK_INT_EQ(r2, KK_CMD_RAN);
    CHECK_INT_EQ(r3, KK_CMD_NONE);
    CHECK_INT_EQ(r4, KK_CMD_NONE);
    CHECK_INT_EQ(calls, 2);
    CHECK_STR_EQ(last_name, "sound");
    CHECK_STR_EQ(last_args, "x");
}

TEST(commands_cooldowns)
{
    kk_commands *c = kk_commands_new(NULL);
    kk_commands_add(c, "sound", record, "buzina", 30, 3, KK_ROLE_ANYONE);
    calls = 0;
    kk_cmd_result a0 = send(c, "!sound", 0, "ana", 100);
    kk_cmd_result b1 = send(c, "!sound", 0, "bia", 101);  /* global 3 s */
    kk_cmd_result b4 = send(c, "!sound", 0, "bia", 104);
    kk_cmd_result a10 = send(c, "!sound", 0, "ana", 110); /* user 30 s */
    kk_cmd_result own = send(c, "!sound", KK_BADGE_OWNER, "ana", 111);
    /* The owner run at 111 restarted ana's own 30 s. */
    kk_cmd_result a131 = send(c, "!sound", 0, "ana", 131);
    kk_cmd_result a142 = send(c, "!sound", 0, "ana", 142);
    kk_commands_free(c);

    CHECK_INT_EQ(a0, KK_CMD_RAN);
    CHECK_STR_EQ(last_data, "buzina");
    CHECK_INT_EQ(b1, KK_CMD_COOLDOWN);
    CHECK_INT_EQ(b4, KK_CMD_RAN);
    CHECK_INT_EQ(a10, KK_CMD_COOLDOWN);
    CHECK_INT_EQ(own, KK_CMD_RAN); /* the owner is never kept waiting */
    CHECK_INT_EQ(a131, KK_CMD_COOLDOWN);
    CHECK_INT_EQ(a142, KK_CMD_RAN);
    CHECK_INT_EQ(calls, 4);
}

TEST(commands_failed_handler_starts_no_cooldown)
{
    kk_commands *c = kk_commands_new(NULL);
    kk_commands_add(c, "avatar", record, NULL, 60, 0, KK_ROLE_ANYONE);
    handler_result = false;
    kk_cmd_result r1 = send(c, "!avatar naoexiste", 0, "a", 0);
    handler_result = true;
    kk_cmd_result r2 = send(c, "!avatar pikachu", 0, "a", 1);
    kk_commands_free(c);
    CHECK_INT_EQ(r1, KK_CMD_FAILED);
    CHECK_INT_EQ(r2, KK_CMD_RAN);
}

TEST(commands_roles)
{
    kk_commands *c = kk_commands_new(NULL);
    kk_commands_add(c, "limpar", record, NULL, 0, 0, KK_ROLE_MOD);
    kk_cmd_result viewer = send(c, "!limpar", 0, "a", 0);
    kk_cmd_result member = send(c, "!limpar", KK_BADGE_MEMBER, "a", 0);
    kk_cmd_result mod = send(c, "!limpar", KK_BADGE_MOD, "a", 0);
    kk_cmd_result owner = send(c, "!limpar", KK_BADGE_OWNER, "a", 0);
    kk_commands_free(c);
    CHECK_INT_EQ(viewer, KK_CMD_DENIED);
    CHECK_INT_EQ(member, KK_CMD_DENIED);
    CHECK_INT_EQ(mod, KK_CMD_RAN);
    CHECK_INT_EQ(owner, KK_CMD_RAN);
}

TEST(commands_fallback)
{
    kk_commands *c = kk_commands_new(NULL);
    kk_commands_add(c, "jump", record, NULL, 0, 0, KK_ROLE_ANYONE);
    kk_commands_set_fallback(c, record, 5);
    calls = 0;
    kk_cmd_result r1 = send(c, "!Agnes Tachyon", 0, "a", 0);
    char args1[256];
    snprintf(args1, sizeof args1, "%s", last_args);
    kk_cmd_result r2 = send(c, "!pikachu", 0, "a", 1); /* cooling: shown as text */
    handler_result = false;
    kk_cmd_result r3 = send(c, "!xyz", 0, "b", 1); /* not a name */
    handler_result = true;
    kk_commands_free(c);
    CHECK_INT_EQ(r1, KK_CMD_RAN);
    CHECK_STR_EQ(args1, "agnes tachyon");
    CHECK_INT_EQ(r2, KK_CMD_NONE);
    CHECK_INT_EQ(r3, KK_CMD_NONE);
}

/* ---- users file ---------------------------------------------------------- */

TEST(users_roundtrip)
{
    char path[] = "/tmp/kk_users_XXXXXX";
    int fd = mkstemp(path);
    CHECK(fd >= 0);
    const char *seed = "# comentário\n"
                       "youtube:UC1\tavatar=pikachu\tfuture=keep me\n"
                       "twitch:9\tpalette=blue\n";
    CHECK(write(fd, seed, strlen(seed)) == (ssize_t)strlen(seed));
    close(fd);

    kk_users *u = kk_users_open(path);
    CHECK(u != NULL);
    CHECK_INT_EQ(kk_users_count(u), 2);
    CHECK_STR_EQ(kk_users_get(u, "youtube:UC1", KK_USER_AVATAR), "pikachu");
    CHECK(kk_users_get(u, "youtube:UC1", KK_USER_GEAR) == NULL);
    CHECK(!kk_users_dirty(u));
    kk_users_set(u, "youtube:UC1", KK_USER_AVATAR, "pikachu"); /* same value */
    CHECK(!kk_users_dirty(u));
    kk_users_set(u, "youtube:UC1", KK_USER_GEAR, "hats/cap,chairs/kart");
    kk_users_set(u, "twitch:9", KK_USER_PALETTE, NULL);
    kk_users_set(u, "demo:x", KK_USER_AVATAR, "tab\there");
    CHECK(kk_users_dirty(u));
    CHECK_INT_EQ(kk_users_save(u), 0);
    kk_users_free(u);

    u = kk_users_open(path);
    char *text = kk_read_file(path, NULL);
    unlink(path);
    CHECK(u != NULL && text != NULL);
    CHECK_STR_EQ(kk_users_get(u, "youtube:UC1", KK_USER_GEAR), "hats/cap,chairs/kart");
    /* Unknown fields survive; emptied records are dropped. */
    CHECK_STR_HAS(text, "future=keep me");
    CHECK(strstr(text, "twitch:9") == NULL);
    CHECK_STR_EQ(kk_users_get(u, "demo:x", KK_USER_AVATAR), "tab here");
    free(text);
    kk_users_free(u);
}

/* ---- Stream Avatars gear and palettes ------------------------------------ */

static kk_sa_library lib;

static void load_small(void)
{
    char *text = fixture("sa_small.json");
    cJSON *root = cJSON_Parse(text);
    free(text);
    kk_sa_parse(&lib, root, "/nonexistent");
    cJSON_Delete(root);
}

TEST(sa_gear_sets)
{
    CHECK_INT_EQ(lib.n_sets, 2);
    CHECK_INT_EQ(lib.n_pieces, 3);
    const kk_sa_avatar *b = kk_sa_find(&lib, "BLOCO");
    CHECK(b != NULL);
    CHECK_INT_EQ(b->n_gear_sets, 2);

    int set;
    const kk_sa_piece *crown = kk_sa_find_piece(&lib, b, "Crown", &set);
    CHECK(crown != NULL);
    CHECK_STR_EQ(lib.sets[set].key, "hats");
    CHECK_INT_EQ(crown->z, 7); /* own z */
    CHECK(crown->animated);
    CHECK_INT_EQ((int)crown->ppu, 2);
    const kk_sa_piece *cap = kk_sa_find_piece(&lib, b, "cap", &set);
    CHECK(cap != NULL);
    CHECK_INT_EQ(cap->z, 3); /* the set's z */
    CHECK(cap->flips);
    const kk_sa_piece *kart = kk_sa_piece_by_path(&lib, "chairs/kart", &set);
    CHECK(kart != NULL);
    CHECK_INT_EQ(kart->z, -10); /* behind the avatar */

    const kk_sa_avatar *plain = kk_sa_find(&lib, "semgear");
    CHECK(kk_sa_find_piece(&lib, plain, "cap", &set) == NULL);
}

TEST(sa_gear_pivots)
{
    const kk_sa_avatar *b = kk_sa_find(&lib, "bloco");
    int hats = -1, chairs = -1;
    for (int i = 0; i < lib.n_sets; i++) {
        if (!strcmp(lib.sets[i].key, "hats"))
            hats = i;
        if (!strcmp(lib.sets[i].key, "chairs"))
            chairs = i;
    }
    int crown = 1, cap = 0;
    float x, y;
    /* A piece's own pivot replaces the set's. */
    CHECK(kk_sa_gear_pivot(b, hats, crown, KK_ANIM_IDLE, 0, &x, &y));
    CHECK_INT_EQ((int)x, -3);
    CHECK_INT_EQ((int)y, 9);
    CHECK(kk_sa_gear_pivot(b, hats, cap, KK_ANIM_IDLE, 0, &x, &y));
    CHECK_INT_EQ((int)x, 1);
    CHECK_INT_EQ((int)y, 2);
    /* Missing means (0, 0). */
    CHECK(kk_sa_gear_pivot(b, hats, cap, KK_ANIM_IDLE, 1, &x, &y));
    CHECK_INT_EQ((int)x, 0);
    CHECK(kk_sa_gear_pivot(b, hats, cap, KK_ANIM_WALK, 0, &x, &y));
    CHECK_INT_EQ((int)x, 4);
    /* The chair is parked out of sight while standing, shown when sitting. */
    CHECK(!kk_sa_gear_pivot(b, chairs, 0, KK_ANIM_IDLE, 0, &x, &y));
    CHECK(kk_sa_gear_pivot(b, chairs, 0, KK_ANIM_SIT, 0, &x, &y));
    CHECK_INT_EQ((int)y, -2);
}

TEST(sa_palettes)
{
    const kk_sa_avatar *b = kk_sa_find(&lib, "bloco");
    CHECK_INT_EQ(b->n_main_colors, 2);
    CHECK_INT_EQ(b->main_colors[0], 0xFFFF0000);
    int p = kk_sa_find_palette(b, "BLUE");
    CHECK_INT_EQ(p, 0);
    CHECK_INT_EQ(b->palettes[p].colors[0], 0xFF0000FF);
    CHECK_INT_EQ(kk_sa_find_palette(b, "verde"), -1);
    /* custom1 is "dance"; the legacy empty "dance" slot is ignored. */
    CHECK_STR_EQ(b->anims[KK_ANIM_CUSTOM1].custom_name, "dance");
    CHECK_INT_EQ(b->anims[KK_ANIM_CUSTOM1].loop_count, 2);
}

typedef struct {
    char keys[8][64], avatars[8][32], palettes[8][32], gear[8][64], names[8][32];
    long long first[8], last[8];
    int n;
} users_seen;

static void on_user(void *ud, const kk_sa_user *u)
{
    users_seen *s = ud;
    if (s->n == 8)
        return;
    snprintf(s->keys[s->n], 64, "%s", u->key);
    snprintf(s->avatars[s->n], 32, "%s", u->avatar ? u->avatar : "");
    snprintf(s->palettes[s->n], 32, "%s", u->palette ? u->palette : "");
    snprintf(s->gear[s->n], 64, "%s", u->n_gear ? u->gear[0] : "");
    snprintf(s->names[s->n], 32, "%s", u->name ? u->name : "");
    s->first[s->n] = u->first;
    s->last[s->n] = u->last;
    s->n++;
}

TEST(sa_users_import)
{
    char *text = fixture("sa_small.json");
    cJSON *root = cJSON_Parse(text);
    free(text);
    users_seen seen = {0};
    int n = kk_sa_parse_users(root, on_user, &seen);
    cJSON_Delete(root);

    /* Unknown prefixes and people with neither choices nor a name are
     * skipped; someone with only a name comes for the audience list. */
    CHECK_INT_EQ(n, 3);
    CHECK_STR_EQ(seen.keys[0], "youtube:UCaaaaaaaaaaaaaaaaaaaaaa");
    CHECK_STR_EQ(seen.avatars[0], "bloco");
    CHECK_STR_EQ(seen.palettes[0], "blue");
    CHECK_STR_EQ(seen.gear[0], "hats/crown");
    CHECK_STR_EQ(seen.names[0], "Fulana");
    CHECK_INT_EQ(seen.first[0], 1704175445); /* -03:00 taken into account */
    CHECK_INT_EQ(seen.last[0], 1706954400);
    CHECK_STR_EQ(seen.keys[1], "twitch:123456");
    CHECK_STR_EQ(seen.palettes[1], "");
    CHECK_STR_EQ(seen.names[1], "");
    CHECK_INT_EQ(seen.first[1], 0);
    CHECK_STR_EQ(seen.keys[2], "youtube:UCcccccccccccccccccccccc");
    CHECK_STR_EQ(seen.avatars[2], "");
    CHECK_STR_EQ(seen.names[2], "Só Olhando");
    CHECK_INT_EQ(seen.first[2], 0);
    CHECK_INT_EQ(seen.last[2], 1704067199);
}

int main(void)
{
    RUN(commands_aliases_and_args);
    RUN(commands_cooldowns);
    RUN(commands_failed_handler_starts_no_cooldown);
    RUN(commands_roles);
    RUN(commands_fallback);
    RUN(users_roundtrip);
    load_small();
    RUN(sa_gear_sets);
    RUN(sa_gear_pivots);
    RUN(sa_palettes);
    RUN(sa_users_import);
    kk_sa_free(&lib);
    return harness_report();
}
