/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Avatars from several folders: layouts, merging, the order of folders. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "../harness.h"
#include "library.h"
#include "sa.h"
#include "util.h"

#define KIT KK_FIXTURES_DIR "/../../docs/avatares/kit-exemplo"

static char tmp[64];

static void write_file(const char *dir, const char *name, const char *text)
{
    char path[KK_PATH_MAX];
    snprintf(path, sizeof path, "%s/%s", dir, name);
    mkdir(dir, 0700);
    FILE *f = fopen(path, "w");
    if (f) {
        fputs(text, f);
        fclose(f);
    }
}

static void parse(kk_sa_library *lib, const char *json, const char *dir)
{
    cJSON *root = cJSON_Parse(json);
    kk_sa_parse(lib, root, dir);
    cJSON_Delete(root);
}

static int set_named(const kk_sa_library *lib, const char *key)
{
    for (int i = 0; i < lib->n_sets; i++)
        if (strcmp(lib->sets[i].key, key) == 0)
            return i;
    return -1;
}

TEST(library_data_folder)
{
    kk_sa_library lib;
    int rc = kk_sa_load_dir(&lib, KIT);
    const kk_sa_avatar *a = kk_sa_find(&lib, "bloco");
    bool dir_ok = a && a->dir && strcmp(a->dir, KIT) == 0;
    int sets = lib.n_sets;
    kk_sa_free(&lib);
    CHECK_INT_EQ(rc, 0);
    CHECK(a != NULL);
    CHECK(dir_ok);
    CHECK_INT_EQ(sets, 2);
}

/* Set indexes and piece ids of the second library move past the first's;
 * a key both have stays with the first. */
TEST(library_merge)
{
    kk_sa_library dst, src;
    parse(&dst,
          "{\"gear\":{\"hats\":{\"gearPiece\":{\"cap\":{}}},"
          "\"chairs\":{\"gearPiece\":{\"kart\":{},\"stool\":{}}}},"
          "\"avatarData\":{\"bloco\":{\"name\":\"Bloco\",\"CanUseGear\":[\"chairs\"]}}}",
          "/first");
    parse(&src,
          "{\"gear\":{\"capes\":{\"gearPiece\":{\"red\":{}}}},"
          "\"avatar\":{\"bloco\":{\"name\":\"Outro bloco\"},"
          "\"hero\":{\"CanUseGear\":[\"capes\"]}}}",
          "/second");
    int pieces_before = dst.n_pieces;
    int added = kk_sa_merge(&dst, &src);
    const kk_sa_avatar *hero = kk_sa_find(&dst, "hero");
    const kk_sa_avatar *bloco = kk_sa_find(&dst, "bloco");
    int capes = set_named(&dst, "capes"), chairs = set_named(&dst, "chairs");
    int hero_set = hero && hero->n_gear_sets ? hero->gear_sets[0] : -2;
    int bloco_set = bloco && bloco->n_gear_sets ? bloco->gear_sets[0] : -2;
    int red_id = capes >= 0 ? dst.sets[capes].pieces[0].id : -1;
    bool kept_first = bloco && strcmp(bloco->name, "Bloco") == 0 &&
                      strcmp(bloco->dir, "/first") == 0;
    int count = dst.count, n_pieces = dst.n_pieces, src_count = src.count;
    kk_sa_free(&dst);
    CHECK_INT_EQ(added, 1);
    CHECK_INT_EQ(count, 2);
    CHECK_INT_EQ(src_count, 0);
    CHECK(kept_first);
    CHECK_INT_EQ(capes, 2);
    CHECK_INT_EQ(hero_set, capes);
    CHECK_INT_EQ(bloco_set, chairs);
    CHECK_INT_EQ(red_id, pieces_before);
    CHECK_INT_EQ(n_pieces, pieces_before + 1);
}

/* A folder of packs: data.txt or streamavatars_json.txt, one level down,
 * in name order. */
TEST(library_folder_of_packs)
{
    char a[96], b[96], junk[96];
    snprintf(a, sizeof a, "%s/a_pack", tmp);
    snprintf(b, sizeof b, "%s/b_pack", tmp);
    snprintf(junk, sizeof junk, "%s/notes", tmp);
    write_file(b, "data.txt",
               "{\"avatar\":{\"same\":{\"name\":\"From B\"},\"only_b\":{}}}");
    write_file(a, "streamavatars_json.txt",
               "\xEF\xBB\xBF{\"avatarData\":{\"same\":{\"name\":\"From A\"}}}");
    write_file(junk, "readme.txt", "not a pack");
    kk_sa_library lib;
    int rc = kk_sa_load_dir(&lib, tmp);
    const kk_sa_avatar *same = kk_sa_find(&lib, "same");
    bool from_a = same && strcmp(same->name, "From A") == 0;
    bool has_b = kk_sa_find(&lib, "only_b") != NULL;
    int count = lib.count;
    kk_sa_free(&lib);
    kk_sa_library none;
    int rc_none = kk_sa_load_dir(&none, junk);
    kk_sa_free(&none);
    CHECK_INT_EQ(rc, 0);
    CHECK_INT_EQ(count, 2);
    CHECK(from_a);
    CHECK(has_b);
    CHECK_INT_EQ(rc_none, -1);
}

typedef struct {
    char dirs[4][KK_PATH_MAX];
    bool sa[4];
    int n;
} seen;

static void see(void *ud, const char *dir, bool is_sa)
{
    seen *s = ud;
    if (s->n < 4) {
        snprintf(s->dirs[s->n], sizeof s->dirs[s->n], "%s", dir);
        s->sa[s->n++] = is_sa;
    }
}

/* Own folder (if it exists), then paths, then the Stream Avatars one. */
TEST(library_order)
{
    char own[96], home_b[96];
    snprintf(own, sizeof own, "%s/kikarinhas/avatars", tmp);
    snprintf(home_b, sizeof home_b, "%s/b dir", tmp);
    setenv("XDG_DATA_HOME", tmp, 1);
    setenv("HOME", tmp, 1);
    seen s1 = {0}, s2 = {0};
    kk_library_each_dir("", " /a , ~/b dir ,", see, &s1); /* own folder missing */
    char kk[96];
    snprintf(kk, sizeof kk, "%s/kikarinhas", tmp);
    mkdir(kk, 0700);
    mkdir(own, 0700);
    kk_library_each_dir("/sa", NULL, see, &s2);
    rmdir(own);
    rmdir(kk);
    CHECK_INT_EQ(s1.n, 2);
    CHECK_STR_EQ(s1.dirs[0], "/a");
    CHECK_STR_EQ(s1.dirs[1], home_b);
    CHECK_INT_EQ(s2.n, 2);
    CHECK_STR_EQ(s2.dirs[0], own);
    CHECK(!s2.sa[0]);
    CHECK_STR_EQ(s2.dirs[1], "/sa");
    CHECK(s2.sa[1]);
}

#define DEFAULTS KK_FIXTURES_DIR "/../../data/default-avatars"

TEST(default_pack_loads_and_is_valid)
{
    kk_sa_library lib;
    CHECK_INT_EQ(kk_sa_load_dir(&lib, DEFAULTS), 0);
    CHECK_INT_EQ(lib.count, 5);
    const char *const names[] = {"gato", "slime", "robo", "fantasma", "cogumelo"};
    for (int i = 0; i < 5; i++) {
        const kk_sa_avatar *a = kk_sa_find(&lib, names[i]);
        CHECK(a != NULL);
        CHECK(a->n_palettes >= 2);
        CHECK(a->n_gear_sets == 3);
        CHECK(a->anims[KK_ANIM_IDLE].frames > 0 && a->anims[KK_ANIM_WALK].frames > 0);
        CHECK(a->anims[KK_ANIM_SIT].frames > 0 && a->anims[KK_ANIM_JUMP].frames > 0);
    }
    kk_sa_free(&lib);
}

TEST(default_pack_only_when_nothing_else)
{
    char used[KK_PATH_MAX];
    kk_sa_library lib;
    /* No folder of the user's, no Stream Avatars: the default pack. */
    CHECK_INT_EQ(kk_library_load_with_default(&lib, "", NULL, DEFAULTS, used, sizeof used, false), 0);
    CHECK_INT_EQ(lib.count, 5);
    kk_sa_free(&lib);
    /* Something found: the pack stays out, so nobody's default avatar moves. */
    CHECK_INT_EQ(kk_library_load_with_default(&lib, "", KIT, DEFAULTS, used, sizeof used, false), 0);
    CHECK_INT_EQ(lib.count, 1);
    CHECK(kk_sa_find(&lib, "bloco") != NULL && kk_sa_find(&lib, "gato") == NULL);
    kk_sa_free(&lib);
    /* A folder that holds nothing counts as nothing. */
    char empty[KK_PATH_MAX];
    snprintf(empty, sizeof empty, "%s/empty", tmp);
    mkdir(empty, 0700);
    CHECK_INT_EQ(kk_library_load_with_default(&lib, "", empty, DEFAULTS, used, sizeof used, false), 0);
    CHECK_INT_EQ(lib.count, 5);
    kk_sa_free(&lib);
    /* No fallback and nothing else: an error, as before. */
    CHECK_INT_EQ(kk_library_load_with_default(&lib, "", NULL, NULL, used, sizeof used, false), -1);
    kk_sa_free(&lib);
}

int main(void)
{
    snprintf(tmp, sizeof tmp, "/tmp/kk_library_XXXXXX");
    if (!mkdtemp(tmp))
        return 2;
    RUN(library_data_folder);
    RUN(library_merge);
    RUN(library_folder_of_packs);
    RUN(library_order);
    RUN(default_pack_loads_and_is_valid);
    RUN(default_pack_only_when_nothing_else);
    int failed = harness_report();
    if (!failed) { /* a failing run keeps the files to look at */
        char cmd[128];
        snprintf(cmd, sizeof cmd, "rm -rf '%s'", tmp);
        if (system(cmd) != 0)
            return 1;
    }
    return failed;
}
