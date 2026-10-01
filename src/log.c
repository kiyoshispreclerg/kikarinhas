/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "log.h"

#include <stdarg.h>
#include <stdio.h>

static void vlog(const char *level, const char *fmt, va_list ap)
{
    fprintf(stderr, "kikarinhas: %s", level);
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
}

void kk_log_info(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vlog("", fmt, ap);
    va_end(ap);
}

void kk_log_warn(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vlog("aviso: ", fmt, ap);
    va_end(ap);
}

void kk_log_error(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vlog("erro: ", fmt, ap);
    va_end(ap);
}
