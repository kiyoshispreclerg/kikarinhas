/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef KK_CONFIG_I18N_H
#define KK_CONFIG_I18N_H

/* gettext for kikarinhas-config. The msgids are the English texts; the
 * translations live in po/. kikarinhas itself has no translated text. */

#include <libintl.h>

#define GETTEXT_DOMAIN "kikarinhas"

#define _(s) gettext(s)
/* Marks a string to be extracted now and translated later, where it is used. */
#define N_(s) (s)

#endif
