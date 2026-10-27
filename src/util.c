/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "util.h"

#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "log.h"

kk_rect kk_rect_union(kk_rect a, kk_rect b)
{
    if (kk_rect_empty(a))
        return b;
    if (kk_rect_empty(b))
        return a;
    int x0 = a.x < b.x ? a.x : b.x;
    int y0 = a.y < b.y ? a.y : b.y;
    int x1 = a.x + a.w > b.x + b.w ? a.x + a.w : b.x + b.w;
    int y1 = a.y + a.h > b.y + b.h ? a.y + a.h : b.y + b.h;
    return (kk_rect){x0, y0, x1 - x0, y1 - y0};
}

void kk_rng_seed(kk_rng *r, uint64_t seed)
{
    r->s = seed ? seed : 0x9e3779b97f4a7c15ULL;
}

uint64_t kk_rng_next(kk_rng *r)
{
    r->s ^= r->s >> 12;
    r->s ^= r->s << 25;
    r->s ^= r->s >> 27;
    return r->s * 0x2545f4914f6cdd1dULL;
}

double kk_rng_range(kk_rng *r, double lo, double hi)
{
    double unit = (double)(kk_rng_next(r) >> 11) / (double)(1ULL << 53);
    return lo + (hi - lo) * unit;
}

int kk_rng_int(kk_rng *r, int n)
{
    return (int)(kk_rng_next(r) % (uint64_t)n);
}

char *kk_read_file(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    if (!f)
        return NULL;

    char *buf = NULL;
    size_t size = 0, cap = 0;
    for (;;) {
        if (cap - size < 65536) {
            cap = cap ? cap * 2 : 1 << 20;
            char *nb = realloc(buf, cap + 1);
            if (!nb) {
                free(buf);
                fclose(f);
                errno = ENOMEM;
                return NULL;
            }
            buf = nb;
        }
        size_t n = fread(buf + size, 1, cap - size, f);
        size += n;
        if (n == 0)
            break;
    }
    if (ferror(f)) {
        free(buf);
        fclose(f);
        errno = EIO;
        return NULL;
    }
    fclose(f);
    buf[size] = '\0';
    if (len)
        *len = size;
    return buf;
}

bool kk_file_exists(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

bool kk_dir_exists(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

bool kk_pathf(char *out, size_t size, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(out, size, fmt, ap);
    va_end(ap);
    if (n < 0 || (size_t)n >= size) {
        if (size)
            out[0] = '\0';
        return false;
    }
    return true;
}

void kk_make_parent_dirs(const char *path)
{
    char buf[KK_PATH_MAX];
    if (!kk_pathf(buf, sizeof buf, "%s", path))
        return;
    for (char *p = buf + 1; *p; p++)
        if (*p == '/') {
            *p = '\0';
            mkdir(buf, 0755);
            *p = '/';
        }
}

int kk_flush_sync(FILE *f)
{
    if (fflush(f) != 0 || ferror(f))
        return -1;
    return fsync(fileno(f));
}

char *kk_read_file_or_backup(const char *path, kk_text_check ok, size_t *len)
{
    size_t n = 0;
    char *text = kk_read_file(path, &n);
    int err = errno;
    if (text && (!ok || ok(text, n))) {
        if (len)
            *len = n;
        return text;
    }
    char bak[KK_PATH_MAX];
    size_t bn = 0;
    char *copy = NULL;
    if (kk_pathf(bak, sizeof bak, "%s.backup", path) && (copy = kk_read_file(bak, &bn)) &&
        (!ok || ok(copy, bn))) {
        kk_log_warn("%s %s; usando a cópia %s", path, text ? "está corrompido" : "não existe", bak);
        free(text);
        if (len)
            *len = bn;
        return copy;
    }
    free(copy);
    if (len)
        *len = n;
    errno = err;
    return text;
}

int kk_backup_file(const char *path, kk_text_check ok)
{
    char bak[KK_PATH_MAX], tmp[KK_PATH_MAX];
    if (!kk_pathf(bak, sizeof bak, "%s.backup", path) ||
        !kk_pathf(tmp, sizeof tmp, "%s.backup.tmp", path))
        return -1;
    struct stat st;
    if (stat(path, &st) < 0)
        return errno == ENOENT ? 0 : -1;
    if (!S_ISREG(st.st_mode))
        return 0;
    size_t len;
    char *text = kk_read_file(path, &len);
    if (!text)
        return -1;
    if (ok && !ok(text, len)) {
        kk_log_warn("%s parece corrompido; a cópia %s anterior foi mantida", path, bak);
        free(text);
        return 0;
    }
    /* Same mode as the original: it may hold tokens. */
    int fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC, st.st_mode & 0777);
    bool good = fd >= 0 && fchmod(fd, st.st_mode & 0777) == 0;
    for (size_t off = 0; good && off < len;) {
        ssize_t w = write(fd, text + off, len - off);
        if (w < 0 && errno == EINTR)
            continue;
        if (w < 0)
            good = false;
        else
            off += (size_t)w;
    }
    good = good && fsync(fd) == 0;
    int e = errno;
    if (fd >= 0)
        good = close(fd) == 0 && good;
    free(text);
    if (!good || rename(tmp, bak) < 0) {
        if (good)
            e = errno;
        kk_log_warn("não consegui fazer a cópia %s: %s", bak, strerror(e));
        remove(tmp);
        return -1;
    }
    return 0;
}

/* Days since 1970-01-01 of a proleptic Gregorian date (Howard Hinnant). */
static long long days_from_civil(long long y, unsigned m, unsigned d)
{
    y -= m <= 2;
    long long era = (y >= 0 ? y : y - 399) / 400;
    unsigned yoe = (unsigned)(y - era * 400);
    unsigned doy = (153 * (m + (m > 2 ? (unsigned)-3 : 9)) + 2) / 5 + d - 1;
    unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + (long long)doe - 719468;
}

bool kk_parse_iso_time(const char *s, long long *out)
{
    int y, mo, d, h, mi, sec, n = 0;
    if (sscanf(s, "%4d-%2d-%2dT%2d:%2d:%2d%n", &y, &mo, &d, &h, &mi, &sec, &n) != 6 ||
        mo < 1 || mo > 12 || d < 1 || d > 31 || h > 23 || mi > 59 || sec > 60)
        return false;
    s += n;
    if (*s == '.')
        for (s++; *s >= '0' && *s <= '9'; s++)
            ;
    long long off = 0;
    if (*s == '+' || *s == '-') {
        int oh, om;
        if (sscanf(s + 1, "%2d:%2d", &oh, &om) != 2)
            return false;
        off = (oh * 3600LL + om * 60LL) * (*s == '-' ? -1 : 1);
    } else if (*s && *s != 'Z') {
        return false;
    }
    *out = days_from_civil(y, (unsigned)mo, (unsigned)d) * 86400LL + h * 3600LL +
           mi * 60LL + sec - off;
    return true;
}
