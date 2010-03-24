/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/*
 * auto-correct.c:
 *
 * Authors:
 *        Jukka-Pekka Iivonen <jiivonen@hutcs.cs.hut.fi>
 *        Morten Welinder (UTF-8).
 *
 * (C) Copyright 2000, 2001 by Jukka-Pekka Iivonen <iivonen@iki.fi>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <gnumeric-config.h>
#include <glib/gi18n-lib.h>
#include "gnumeric.h"
#include "auto-correct.h"

#include "application.h"
#include "gutils.h"
#include "gnumeric-gconf.h"
#include "parse-util.h"
#include <goffice/goffice.h>
#include <gsf/gsf-impl-utils.h>
#include <string.h>


/*
 * Utility to replace a single character in an UTF-8 string.
 */
static char *
replace1 (const char *src, int keepbytes, const char *mid, const char *tail)
{
	int midlen = strlen (mid);
	char *dst = g_new (char, strlen (src) + midlen + 2);
	char *p = dst;

	memcpy (p, src, keepbytes);
	p += keepbytes;

	strcpy (p, mid);
	p += midlen;

	strcpy (p, tail);
	return dst;
}


static char *
autocorrect_initial_caps (const char *src)
{
	enum State {
		S_waiting_for_word_begin,
		S_waiting_for_whitespace,
		S_seen_one_caps,
		S_seen_two_caps
	};

	enum State state = S_waiting_for_word_begin;
	char *res = NULL;
	const char *p;

	if (gnm_expr_char_start_p (src))
		return NULL;

	for (p = src; *p; p = g_utf8_next_char (p)) {
		gunichar c = g_utf8_get_char (p);

		switch (state) {
		case S_waiting_for_word_begin:
			if (g_unichar_isupper (c))
				state = S_seen_one_caps;
			else if (g_unichar_isalpha (c))
				state = S_waiting_for_whitespace;
			break;

		case S_waiting_for_whitespace:
			if (g_unichar_isspace (c))
				state = S_waiting_for_word_begin;
			break;

		case S_seen_one_caps:
			if (g_unichar_isupper (c))
				state = S_seen_two_caps;
			else
				state = S_waiting_for_whitespace;
			break;

		case S_seen_two_caps:
			state = S_waiting_for_whitespace;

			if (g_unichar_islower (c)) {
				const char *target = g_utf8_prev_char (p);
				const char *begin = g_utf8_prev_char (target);
				GSList *l;
				char *newres, *lotext;
				gboolean exception_found = FALSE;

				for (l = gnm_conf_get_autocorrect_init_caps_list (); l; l = l->next) {
					const char *except = l->data;
					if (strncmp (begin, except, strlen (except)) == 0) {
						exception_found = TRUE;
						break;
					}
				}

				if (!exception_found) {
					const char *q;
					for (q = g_utf8_next_char (p);
					     *q && !g_unichar_isspace (g_utf8_get_char (q));
					     q = g_utf8_next_char (q)) {
						if (g_unichar_isupper
						    (g_utf8_get_char (q))) {
							exception_found = TRUE;
							break;
						}
					}
				}

				if (!exception_found) {
					lotext = g_utf8_strdown (target, 1);
					newres = replace1 (src, target - src, lotext, p);
					g_free (lotext);
					p = newres + (p - src);
					g_free (res);
					src = res = newres;
				}
			}
			break;

#ifndef DEBUG_SWITCH_ENUM
		default:
			g_assert_not_reached ();
#endif
		}
	}

	return res;
}


static char *
autocorrect_first_letter (G_GNUC_UNUSED const char *src)
{
	/* Sorry, not implemented.  I got tired.  */
#if 0
	for (s = ucommand; *s; s = p+1) {
	skip_first_letter:
		/* Attempt to find the end of a sentence. */
		for (p = s; *p != '\0' &&
			     !(g_unichar_ispunct (*p) &&
			       NULL == g_unichar_strchr (not_punct, *p)) ; p++)
			;
		if (*p == '\0')
			break;

		while (g_unichar_isspace(*s))
			++s;
		if (g_unichar_islower (*s) && (s == ucommand || g_unichar_isspace (s[-1]))) {
			GSList const *cur = gnm_conf_get_autocorrect_first_letter_list ();

			for ( ; cur != NULL; cur = cur->next) {
				gunichar *t, *c = cur->data;
				gint l = g_unichar_strlen (c);
				gint spaces = 0;

				for (t = s - 1; t >= ucommand; t--)
					if (g_unichar_isspace (*t))
						++spaces;
					else
						break;
				if (s - ucommand > l + spaces &&
				    g_unichar_strncmp (s-l-spaces, c, l) == 0) {
					s = p + 1;
					goto skip_first_letter;
				}
			}
				*s = g_unichar_toupper (*s);
		}
	}
#endif
	return NULL;
}


static char *
autocorrect_names_of_days (const char *src)
{
	/* English, except for lower case.  */
	static char const * const days[7] = {
		"monday", "tuesday", "wednesday", "thursday",
		"friday", "saturday", "sunday"
	};

	char *res = NULL;
	int i;

	for (i = 0; i < 7; i++) {
		const char *day = days[i];
		const char *pos = strstr (src, day);
		if (pos) {
			char *newres = g_strdup (src);
			/* It's ASCII...  */
			newres[pos - src] += ('A' - 'a');
			g_free (res);
			src = res = newres;
			continue;
		}
	}

	return res;
}


/*
 * NOTE: If in the future this is extended with methods that insert or
 * delete characters (bytes to be precise), the there might need to be
 * rich text corrections.
 */
char *
autocorrect_tool (char const *src)
{
	char *res = NULL;

        if (gnm_conf_get_autocorrect_init_caps ()) {
		char *res2 = autocorrect_initial_caps (src);
		if (res2) {
			g_free (res);
			src = res = res2;
		}
	}

	if (gnm_conf_get_autocorrect_first_letter ()) {
		char *res2 = autocorrect_first_letter (src);
		if (res2) {
			g_free (res);
			src = res = res2;
		}
	}

	if (gnm_conf_get_autocorrect_names_of_days ()) {
		char *res2 = autocorrect_names_of_days (src);
		if (res2) {
			g_free (res);
			src = res = res2;
		}
	}

	if (!res) res = g_strdup (src);
	return res;
}
