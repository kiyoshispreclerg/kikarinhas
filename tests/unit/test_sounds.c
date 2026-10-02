/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Sound board (decoding, mixing, config, commands) and the audience list. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../harness.h"
#include "actions.h"
#include "audio.h"
#include "commands.h"
#include "config.h"
#include "ini.h"
#include "sa.h"
#include "sample.h"
#include "soundboard.h"
#include "users.h"
#include "util.h"

static void fixture_path(char *out, size_t size, const char *name)
{
    snprintf(out, size, "%s/%s", KK_FIXTURES_DIR, name);
}

/* ---- samples ------------------------------------------------------------- */

TEST(decodes_wav_ogg_and_mp3)
{
    /* 0.2 s of a 440 Hz sine at half scale, mono, 22050 Hz. */
    static const char *const files[] = {"tone.wav", "tone.ogg", "tone.mp3"};
    for (int i = 0; i < 3; i++) {
        char path[KK_PATH_MAX], err[256] = "";
        fixture_path(path, sizeof path, files[i]);
        kk_sample s;
        CHECK_MSG(kk_sample_load(&s, path, err, sizeof err) == 0, "%s: %s", files[i], err);
        /* Resampled to 48 kHz; mp3 encoders pad a little. */
        CHECK_MSG(fabs(kk_sample_seconds(&s) - 0.2) < 0.06, "%s: %.3f s", files[i],
                  kk_sample_seconds(&s));
        CHECK_MSG(s.peak > 0.4f && s.peak < 0.6f, "%s: pico %.3f", files[i], s.peak);
        /* A sine's RMS is peak / sqrt(2). */
        CHECK_MSG(fabs(s.loudness - 0.5 / sqrt(2.0)) < 0.05, "%s: rms %.3f", files[i],
                  s.loudness);
        /* Mono plays on both sides. */
        CHECK_INT_EQ(s.pcm[2000], s.pcm[2001]);
        kk_sample_free(&s);
    }
}

TEST(refuses_what_is_not_sound)
{
    char path[KK_PATH_MAX], err[256] = "";
    fixture_path(path, sizeof path, "sa_small.json");
    kk_sample s;
    CHECK_INT_EQ(kk_sample_load(&s, path, err, sizeof err), -1);
    CHECK_STR_HAS(err, "formato desconhecido");
    CHECK_INT_EQ(kk_sample_load(&s, "/nao/existe.ogg", err, sizeof err), -1);
}

TEST(resamples_and_levels)
{
    /* 1 s at 24 kHz, stereo, a square wave on the left only. Its RMS over
     * both channels is amplitude / sqrt(2). */
    int16_t *pcm = malloc(24000 * 2 * sizeof *pcm);
    kk_sample s;
    static const struct {
        int amplitude, volume;
    } cases[] = {
        {2000, 292},  /* quiet: 0.1259 / (2000/32768/sqrt 2) */
        {30000, 19},  /* blown out: brought down */
        {200, 400},   /* very quiet: as far as it goes */
    };
    for (int k = 0; k < 3; k++) {
        for (int i = 0; i < 24000; i++) {
            pcm[i * 2] = (int16_t)((i / 50) % 2 ? cases[k].amplitude : -cases[k].amplitude);
            pcm[i * 2 + 1] = 0;
        }
        CHECK(kk_sample_from_pcm(&s, pcm, 24000, 2, 24000) == 0);
        CHECK_INT_EQ(s.frames, 48000);
        CHECK_INT_EQ(s.pcm[1], 0);
        CHECK(fabs(s.peak - cases[k].amplitude / 32768.0) < 0.001);
        CHECK_MSG(abs(kk_sample_level(&s) - cases[k].volume) <= 1, "amplitude %d: %d%%",
                  cases[k].amplitude, kk_sample_level(&s));
        kk_sample_free(&s);
    }
    free(pcm);

    /* A loud peak limits the boost: never clip. */
    int16_t spiky[4800 * 2] = {0};
    for (int i = 0; i < 4800; i++)
        spiky[i * 2] = spiky[i * 2 + 1] = (int16_t)((i / 10) % 2 ? 300 : -300);
    spiky[1000] = 16384;
    CHECK(kk_sample_from_pcm(&s, spiky, 4800, 2, 48000) == 0);
    CHECK_INT_EQ(kk_sample_level(&s), 200);
    kk_sample_free(&s);

    /* Silence can't be levelled. */
    int16_t zeros[200] = {0};
    CHECK(kk_sample_from_pcm(&s, zeros, 100, 2, 48000) == 0);
    CHECK_INT_EQ(kk_sample_level(&s), 100);
    kk_sample_free(&s);
}

/* ---- mixer --------------------------------------------------------------- */

static kk_sample constant(int16_t v, size_t frames)
{
    int16_t *pcm = malloc(frames * 2 * sizeof *pcm);
    for (size_t i = 0; i < frames * 2; i++)
        pcm[i] = v;
    kk_sample s;
    kk_sample_from_pcm(&s, pcm, frames, 2, KK_SAMPLE_RATE);
    free(pcm);
    return s;
}

TEST(mixes_with_gain_and_clips)
{
    /* ALSA's "null" device swallows everything: no sound card needed. */
    kk_audio *a = kk_audio_new("null", 2);
    CHECK(a);
    kk_sample s1 = constant(10000, 100), s2 = constant(10000, 10);
    CHECK_INT_EQ(kk_audio_play(a, &s1, 0.5, 0), 0);
    int16_t out[40 * 2];
    kk_audio_mix(a, out, 5);
    CHECK_INT_EQ(out[0], 5000);
    CHECK_INT_EQ(out[9], 5000);

    kk_audio_set_volume(a, 2.0); /* master */
    kk_audio_play(a, &s2, 4.0, 0);
    kk_audio_mix(a, out, 5);
    CHECK_INT_EQ(out[0], 32767); /* 10000 + 80000, clipped */
    CHECK_INT_EQ(kk_audio_playing(a), 2);
    kk_audio_mix(a, out, 40); /* s2 ends after 5 more frames */
    CHECK_INT_EQ(out[2 * 4], 32767);
    CHECK_INT_EQ(out[2 * 5], 10000);
    CHECK_INT_EQ(kk_audio_playing(a), 1);

    /* Every voice busy: the oldest makes room. */
    kk_audio_play(a, &s2, 1.0, 0);
    kk_audio_play(a, &s2, 1.0, 0);
    CHECK_INT_EQ(kk_audio_playing(a), 2);
    kk_audio_stop_all(a);
    CHECK_INT_EQ(kk_audio_playing(a), 0);
    kk_audio_mix(a, out, 3);
    CHECK_INT_EQ(out[0], 0);
    kk_audio_free(a);
    kk_sample_free(&s1);
    kk_sample_free(&s2);
}

/* ---- config and commands ------------------------------------------------- */

static int n_warnings;
static char last_warning[512];

static void count_warning(void *ud, int line, const char *msg)
{
    (void)ud;
    (void)line;
    n_warnings++;
    snprintf(last_warning, sizeof last_warning, "%s", msg);
}

static kk_config load_text(const char *text)
{
    kk_config c;
    kk_config_defaults(&c);
    n_warnings = 0;
    kk_ini *ini = kk_ini_parse(text, count_warning, NULL);
    kk_config_apply(&c, ini, count_warning, NULL);
    kk_ini_free(ini);
    return c;
}

TEST(config_sounds)
{
    kk_config c = load_text("[soundboard]\nvolume = 80\ndevice = null\nvoices = 3\n"
                            "commands = no\n"
                            "[sound.Buzina]\nfile = /x/buzina.ogg\naliases = buz, !CORNETA\n"
                            "volume = 150\n"
                            "[sound.semarquivo]\nvolume = 10\n"
                            "[sound.alto]\nfile = a.wav\nvolume = 900\n");
    CHECK_INT_EQ(n_warnings, 2); /* no file; volume over 400 */
    CHECK_INT_EQ(c.sound_volume, 80);
    CHECK_STR_EQ(c.sound_device, "null");
    CHECK_INT_EQ(c.sound_voices, 3);
    CHECK(!c.sound_commands);
    CHECK_INT_EQ(c.n_sounds, 2);
    const kk_config_sound *s = kk_config_find_sound(&c, "!Corneta");
    CHECK(s);
    CHECK_STR_EQ(s->name, "buzina");
    CHECK_STR_EQ(s->file, "/x/buzina.ogg");
    CHECK_INT_EQ(s->volume, 150);
    CHECK(kk_config_find_sound(&c, "buz") == s);
    CHECK_INT_EQ(kk_config_find_sound(&c, "alto")->volume, 100);
    CHECK(kk_config_find_sound(&c, "nada") == NULL);

    kk_config copy;
    CHECK(kk_config_copy(&copy, &c));
    kk_config_free(&c);
    CHECK_STR_EQ(kk_config_find_sound(&copy, "buz")->file, "/x/buzina.ogg");
    CHECK_STR_EQ(copy.sound_device, "null");
    kk_config_free(&copy);
}

TEST(sound_names_from_files)
{
    char n[64];
    CHECK(kk_config_sound_name(n, sizeof n, "/a/b/Buzina Alta!.OGG"));
    CHECK_STR_EQ(n, "buzina_alta_");
    CHECK(kk_config_sound_name(n, sizeof n, "bem-te-vi.mp3"));
    CHECK_STR_EQ(n, "bem-te-vi");
    CHECK(kk_config_sound_name(n, sizeof n, ".ogg")); /* a dot file keeps its name */
    CHECK(!kk_config_sound_name(n, sizeof n, "/a/"));
}

TEST(relative_sound_files_follow_the_config)
{
    char dir[] = "/tmp/kk-test-XXXXXX";
    CHECK(mkdtemp(dir));
    char path[KK_PATH_MAX];
    snprintf(path, sizeof path, "%s/k.ini", dir);
    FILE *f = fopen(path, "w");
    fputs("[sound.a]\nfile = sons/a.ogg\n[sound.b]\nfile = /abs/b.ogg\n", f);
    fclose(f);
    kk_config c;
    kk_config_defaults(&c);
    bool found;
    CHECK_INT_EQ(kk_config_load(&c, path, &found, NULL, NULL), 0);
    char want[KK_PATH_MAX];
    snprintf(want, sizeof want, "%s/sons/a.ogg", dir);
    CHECK_STR_EQ(c.sounds[0].file, want);
    CHECK_STR_EQ(c.sounds[1].file, "/abs/b.ogg");
    kk_config_free(&c);
    remove(path);
    rmdir(dir);
}

static int played;
static char played_name[64];

static bool fake_sound(void *ud, const kk_chat_msg *m, const char *sound)
{
    (void)ud;
    (void)m;
    played++;
    snprintf(played_name, sizeof played_name, "%s", sound);
    return strcmp(sound, "quebrado") != 0;
}

static kk_cmd_result say(kk_commands *c, const char *who, const char *text, double now)
{
    kk_chat_msg m = {.platform = "t", .user_id = who, .name = who, .text = text};
    return kk_commands_handle(c, &m, who, now);
}

TEST(each_sound_is_a_command_sharing_the_cooldown)
{
    kk_config c = load_text("[command.sound]\ncooldown = 10\nglobal_cooldown = 2\n"
                            "[sound.buzina]\nfile = x.ogg\naliases = buz\n"
                            "[sound.jump]\nfile = y.ogg\n"
                            "[sound.quebrado]\nfile = z.ogg\n");
    kk_actions actions = {.sound = fake_sound};
    kk_commands *cmds = kk_commands_new(&actions);
    n_warnings = 0;
    CHECK_INT_EQ(kk_actions_register(cmds, &c, count_warning, NULL), 1);
    CHECK_STR_HAS(last_warning, "jump"); /* !jump stays the jump */

    played = 0;
    CHECK_INT_EQ(say(cmds, "a", "!buz", 0), KK_CMD_RAN);
    CHECK_STR_EQ(played_name, "buzina");
    /* Same person, other sound or !som: the same 10 s wait. */
    CHECK_INT_EQ(say(cmds, "a", "!som buzina", 5), KK_CMD_COOLDOWN);
    CHECK_INT_EQ(say(cmds, "a", "!buzina", 9), KK_CMD_COOLDOWN);
    /* Someone else: only the 2 s for everybody. */
    CHECK_INT_EQ(say(cmds, "b", "!buzina", 1), KK_CMD_COOLDOWN);
    CHECK_INT_EQ(say(cmds, "b", "!som buz", 2.5), KK_CMD_RAN);
    CHECK_INT_EQ(say(cmds, "a", "!buzina", 10.5), KK_CMD_RAN);
    /* A sound that fails starts no cooldown. */
    CHECK_INT_EQ(say(cmds, "c", "!quebrado", 20), KK_CMD_FAILED);
    CHECK_INT_EQ(say(cmds, "c", "!buzina", 20), KK_CMD_RAN);
    CHECK_INT_EQ(played, 5);
    kk_commands_free(cmds);

    /* commands = no: only through !som. */
    kk_config_free(&c);
    c = load_text("[soundboard]\ncommands = no\n[sound.buzina]\nfile = x.ogg\n");
    cmds = kk_commands_new(&actions);
    kk_actions_register(cmds, &c, NULL, NULL);
    kk_commands_set_fallback(cmds, NULL, 0);
    CHECK_INT_EQ(say(cmds, "a", "!buzina", 0), KK_CMD_NONE);
    CHECK_INT_EQ(say(cmds, "a", "!som buzina", 0), KK_CMD_RAN);
    kk_commands_free(cmds);
    kk_config_free(&c);
}

static char help_lines[2048];
static int help_n;

static void collect_help(const char *text, void *ud)
{
    (void)ud;
    help_n++;
    snprintf(help_lines + strlen(help_lines), sizeof help_lines - strlen(help_lines),
             "[%s]", text);
}

static int help_for(kk_commands *c, kk_role role)
{
    help_lines[0] = '\0';
    help_n = 0;
    kk_commands_each_help(c, role, collect_help, NULL);
    return help_n;
}

TEST(help_lists_what_a_person_may_use)
{
    kk_config c = load_text("[command.attack]\nrole = mod\n[command.hug]\nenabled = no\n"
                            "[command.pika]\naction = avatar\ndata = pikachu\n"
                            "[sound.buzina]\nfile = x.ogg\naliases = buz\n"
                            "[sound.tom]\nfile = y.ogg\n");
    CHECK_INT_EQ(c.help_count, 3);
    CHECK(c.help_bubbles);
    kk_actions actions = {.sound = fake_sound};
    kk_commands *cmds = kk_commands_new(&actions);
    CHECK_INT_EQ(kk_actions_register(cmds, &c, NULL, NULL), 0);
    CHECK(actions.help_bubbles);
    CHECK_INT_EQ(actions.help_count, 3);

    help_for(cmds, KK_ROLE_ANYONE);
    CHECK_STR_HAS(help_lines, "[!avatar NOME]");
    CHECK_STR_HAS(help_lines, "[!emote [NOME]]");
    CHECK_STR_HAS(help_lines, "[!pika]");   /* fixed data: no argument */
    CHECK_STR_HAS(help_lines, "[!buzina]"); /* the name, not each alias */
    CHECK_STR_HAS(help_lines, "[!tom]");
    CHECK(!strstr(help_lines, "buz]"));
    CHECK(!strstr(help_lines, "!sound")); /* the sounds are listed instead */
    CHECK(!strstr(help_lines, "!help"));
    CHECK(!strstr(help_lines, "!hug"));    /* disabled */
    CHECK(!strstr(help_lines, "!attack")); /* needs mod */
    int anyone = help_n;
    CHECK_INT_EQ(help_for(cmds, KK_ROLE_MOD), anyone + 1);
    CHECK_STR_HAS(help_lines, "[!attack [@nome]]");
    kk_commands_free(cmds);
    kk_config_free(&c);

    /* Without a command per sound, !sound NAME is the way in. */
    c = load_text("[soundboard]\ncommands = no\n[sound.buzina]\nfile = x.ogg\n");
    cmds = kk_commands_new(&actions);
    kk_actions_register(cmds, &c, NULL, NULL);
    help_for(cmds, KK_ROLE_ANYONE);
    CHECK_STR_HAS(help_lines, "[!sound NOME]");
    CHECK(!strstr(help_lines, "[!buzina]"));
    kk_commands_free(cmds);
    kk_config_free(&c);

    c = load_text("[commands]\nhelp_bubbles = no\nhelp_count = 7\n");
    CHECK(!c.help_bubbles);
    CHECK_INT_EQ(c.help_count, 7);
    CHECK(c.help_seconds < 0 && c.bubble_seconds < 0); /* auto */
    kk_config_free(&c);
    c = load_text("[avatars]\nbubble_seconds = 8\n[commands]\nhelp_seconds = 2.5\n");
    CHECK(c.bubble_seconds == 8);
    CHECK(c.help_seconds == 2.5);
    kk_config_free(&c);
    c = load_text("[avatars]\nbubble_seconds = auto\n[commands]\nhelp_seconds = 0\n");
    CHECK(c.bubble_seconds < 0);
    CHECK_INT_EQ(n_warnings, 1); /* below 0.5 s */
    CHECK(c.help_seconds < 0);
    kk_config_free(&c);
    c = load_text("[commands]\nhelp_count = 0\n");
    CHECK_INT_EQ(n_warnings, 1);
    CHECK_INT_EQ(c.help_count, 3);
    kk_config_free(&c);
}

TEST(soundboard_plays_by_name_or_alias)
{
    char tone[KK_PATH_MAX];
    fixture_path(tone, sizeof tone, "tone.ogg");
    char text[KK_PATH_MAX * 2];
    snprintf(text, sizeof text,
             "[soundboard]\ndevice = null\n"
             "[sound.tom]\nfile = %s\naliases = la\n"
             "[sound.sumido]\nfile = /nao/existe.ogg\n",
             tone);
    kk_config c = load_text(text);
    kk_soundboard *sb = kk_soundboard_new();
    kk_soundboard_configure(sb, &c);
    CHECK(kk_soundboard_play(sb, "tom", 0));
    CHECK(kk_soundboard_play(sb, "LA", 0)); /* decoded once, played twice */
    CHECK(!kk_soundboard_play(sb, "nenhum", 0));
    CHECK(!kk_soundboard_play(sb, "sumido", 0));
    kk_soundboard_pump(sb, NULL, 0, 0.1);

    c.sound_enabled = false;
    kk_soundboard_configure(sb, &c);
    CHECK(!kk_soundboard_play(sb, "tom", 0));
    kk_soundboard_free(sb);
    kk_config_free(&c);
}

typedef struct {
    char names[4][32], files[4][KK_PATH_MAX];
    int volumes[4], n;
} sa_sounds;

static void on_sa_sound(void *ud, const kk_sa_sound *s)
{
    sa_sounds *ss = ud;
    if (ss->n == 4)
        return;
    snprintf(ss->names[ss->n], 32, "%s", s->name);
    snprintf(ss->files[ss->n], KK_PATH_MAX, "%s", s->file ? s->file : "");
    ss->volumes[ss->n++] = s->volume;
}

TEST(imports_stream_avatars_sounds)
{
    char path[KK_PATH_MAX];
    fixture_path(path, sizeof path, "sa_small.json");
    char *text = kk_read_file(path, NULL);
    cJSON *root = cJSON_Parse(text);
    free(text);
    sa_sounds got = {0};
    CHECK_INT_EQ(kk_sa_parse_sounds(root, KK_FIXTURES_DIR, on_sa_sound, &got), 2);
    cJSON_Delete(root);
    CHECK_STR_EQ(got.names[0], "tom");
    fixture_path(path, sizeof path, "tone.ogg");
    CHECK_STR_EQ(got.files[0], path);
    CHECK_INT_EQ(got.volumes[0], 75); /* between 0.5 and 1.0 */
    CHECK_STR_EQ(got.names[1], "sumido");
    CHECK_STR_EQ(got.files[1], "");
    CHECK_INT_EQ(got.volumes[1], 80);
}

/* ---- audience ------------------------------------------------------------ */

TEST(users_remember_name_and_dates)
{
    char dir[] = "/tmp/kk-test-XXXXXX";
    CHECK(mkdtemp(dir));
    char path[KK_PATH_MAX];
    snprintf(path, sizeof path, "%s/users.tsv", dir);
    kk_users *u = kk_users_open(path);
    kk_users_set(u, "youtube:UCx", KK_USER_AVATAR, "pikachu");
    kk_users_seen(u, "youtube:UCx", "Fulana", 1000);
    kk_users_seen(u, "youtube:UCx", "Fulana\tde Tal", 2000);
    CHECK_STR_EQ(kk_users_get(u, "youtube:UCx", KK_USER_FIRST), "1000");
    CHECK_STR_EQ(kk_users_get(u, "youtube:UCx", KK_USER_LAST), "2000");
    CHECK_INT_EQ(kk_users_save(u), 0);
    kk_users_free(u);

    u = kk_users_open(path);
    CHECK_INT_EQ(kk_users_count(u), 1);
    CHECK_STR_EQ(kk_users_key(u, 0), "youtube:UCx");
    CHECK(kk_users_key(u, 1) == NULL);
    CHECK_STR_EQ(kk_users_get(u, "youtube:UCx", KK_USER_NAME), "Fulana de Tal");
    CHECK_STR_EQ(kk_users_get(u, "youtube:UCx", KK_USER_AVATAR), "pikachu");
    CHECK_STR_EQ(kk_users_get(u, "youtube:UCx", KK_USER_FIRST), "1000");
    kk_users_free(u);
    remove(path);
    rmdir(dir);
}

TEST(iso_times)
{
    long long t;
    CHECK(kk_parse_iso_time("1970-01-01T00:00:00Z", &t));
    CHECK_INT_EQ(t, 0);
    CHECK(kk_parse_iso_time("2000-02-29T12:00:00.9999999+02:30", &t));
    CHECK_INT_EQ(t, 951825600 - 9000);
    CHECK(kk_parse_iso_time("2024-01-02T03:04:05", &t));
    CHECK_INT_EQ(t, 1704164645);
    CHECK(!kk_parse_iso_time("ontem", &t));
    CHECK(!kk_parse_iso_time("2024-13-01T00:00:00Z", &t));
    CHECK(!kk_parse_iso_time("2024-01-01T00:00:00 lixo", &t));
}

int main(void)
{
    RUN(decodes_wav_ogg_and_mp3);
    RUN(refuses_what_is_not_sound);
    RUN(resamples_and_levels);
    RUN(mixes_with_gain_and_clips);
    RUN(config_sounds);
    RUN(sound_names_from_files);
    RUN(relative_sound_files_follow_the_config);
    RUN(each_sound_is_a_command_sharing_the_cooldown);
    RUN(help_lists_what_a_person_may_use);
    RUN(soundboard_plays_by_name_or_alias);
    RUN(imports_stream_avatars_sounds);
    RUN(users_remember_name_and_dates);
    RUN(iso_times);
    return harness_report();
}
