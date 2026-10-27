/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Power-cut safety: .backup copies before overwriting, and loading from them. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "../harness.h"
#include "config.h"
#include "ini.h"
#include "users.h"
#include "util.h"

static char dir[] = "/tmp/kk-bak-XXXXXX";

static void put(const char *path, const char *text)
{
    FILE *f = fopen(path, "w");
    CHECK(f != NULL);
    fputs(text, f);
    fclose(f);
}

static bool has(const char *path, const char *needle)
{
    char *t = kk_read_file(path, NULL);
    bool r = t && strstr(t, needle);
    free(t);
    return r;
}

TEST(ini_save_keeps_the_previous_file_with_its_mode)
{
    char path[KK_PATH_MAX], bak[KK_PATH_MAX];
    CHECK(kk_pathf(path, sizeof path, "%s/a.ini", dir));
    CHECK(kk_pathf(bak, sizeof bak, "%s/a.ini.backup", dir));

    kk_ini *ini = kk_ini_new();
    CHECK(kk_ini_set(ini, "s", "k", "one") == 0);
    CHECK(kk_ini_save(ini, path) == 0);
    CHECK(!kk_file_exists(bak));

    CHECK(chmod(path, 0600) == 0);
    CHECK(kk_ini_set(ini, "s", "k", "two") == 0);
    CHECK(kk_ini_save(ini, path) == 0);
    CHECK(has(bak, "one") && !has(bak, "two") && has(path, "two"));
    struct stat st;
    CHECK(stat(bak, &st) == 0 && (st.st_mode & 0777) == 0600);
    kk_ini_free(ini);
}

TEST(corrupt_file_does_not_replace_a_good_backup)
{
    char path[KK_PATH_MAX], bak[KK_PATH_MAX];
    CHECK(kk_pathf(path, sizeof path, "%s/b.ini", dir));
    CHECK(kk_pathf(bak, sizeof bak, "%s/b.ini.backup", dir));
    put(bak, "[s]\nk = good\n");
    put(path, "");
    kk_ini *ini = kk_ini_new();
    CHECK(kk_ini_set(ini, "s", "k", "new") == 0);
    CHECK(kk_ini_save(ini, path) == 0);
    CHECK(has(bak, "good") && has(path, "new"));
    kk_ini_free(ini);
}

TEST(load_falls_back_to_the_backup)
{
    char path[KK_PATH_MAX], bak[KK_PATH_MAX];
    CHECK(kk_pathf(path, sizeof path, "%s/c.ini", dir));
    CHECK(kk_pathf(bak, sizeof bak, "%s/c.ini.backup", dir));
    put(bak, "[s]\nk = saved\n");

    /* Missing. */
    char *t = kk_read_file_or_backup(path, kk_ini_text_usable, NULL);
    CHECK(t && strstr(t, "saved"));
    free(t);
    /* Zeroed by a power cut. */
    put(path, "");
    t = kk_read_file_or_backup(path, kk_ini_text_usable, NULL);
    CHECK(t && strstr(t, "saved"));
    free(t);
    /* Fine: the file wins. */
    put(path, "[s]\nk = main\n");
    t = kk_read_file_or_backup(path, kk_ini_text_usable, NULL);
    CHECK(t && strstr(t, "main"));
    free(t);
    /* Neither: as before. */
    CHECK(kk_pathf(path, sizeof path, "%s/none.ini", dir));
    CHECK(kk_read_file_or_backup(path, kk_ini_text_usable, NULL) == NULL);
}

TEST(config_loads_from_the_backup)
{
    char path[KK_PATH_MAX], bak[KK_PATH_MAX];
    CHECK(kk_pathf(path, sizeof path, "%s/d.ini", dir));
    CHECK(kk_pathf(bak, sizeof bak, "%s/d.ini.backup", dir));
    put(bak, "[general]\n");
    kk_config c;
    kk_config_defaults(&c);
    bool found = false;
    CHECK(kk_config_load(&c, path, &found, NULL, NULL) == 0 && found);
    kk_config_free(&c);
}

TEST(users_save_and_recover)
{
    char path[KK_PATH_MAX], bak[KK_PATH_MAX];
    CHECK(kk_pathf(path, sizeof path, "%s/users.tsv", dir));
    CHECK(kk_pathf(bak, sizeof bak, "%s/users.tsv.backup", dir));

    kk_users *u = kk_users_open(path);
    kk_users_set(u, "yt:ana", KK_USER_AVATAR, "um");
    CHECK(kk_users_save(u) == 0);
    kk_users_set(u, "yt:ana", KK_USER_AVATAR, "dois");
    CHECK(kk_users_save(u) == 0);
    CHECK(has(bak, "um") && has(path, "dois"));
    kk_users_free(u);

    /* The main file is cut off by a power failure: the backup comes back. */
    put(path, "");
    u = kk_users_open(path);
    CHECK(kk_users_get(u, "yt:ana", KK_USER_AVATAR) != NULL);
    CHECK(strcmp(kk_users_get(u, "yt:ana", KK_USER_AVATAR), "um") == 0);
    kk_users_free(u);

    /* A file that does not end in a newline is not copied over the backup. */
    put(path, "yt:ana\tavatar=tru");
    u = kk_users_open(path);
    kk_users_set(u, "yt:bia", KK_USER_AVATAR, "x");
    CHECK(kk_users_save(u) == 0);
    CHECK(has(bak, "um"));
    kk_users_free(u);
}

int main(void)
{
    if (!mkdtemp(dir))
        return 1;
    RUN(ini_save_keeps_the_previous_file_with_its_mode);
    RUN(corrupt_file_does_not_replace_a_good_backup);
    RUN(load_falls_back_to_the_backup);
    RUN(config_loads_from_the_backup);
    RUN(users_save_and_recover);
    char cmd[64];
    snprintf(cmd, sizeof cmd, "rm -rf %s", dir);
    if (system(cmd) != 0)
        return 1;
    return harness_report();
}
