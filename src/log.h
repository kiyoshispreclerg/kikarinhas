/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef KK_LOG_H
#define KK_LOG_H

/* Messages go to stderr prefixed with "kikarinhas: ". */
void kk_log_info(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void kk_log_warn(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void kk_log_error(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

#endif
