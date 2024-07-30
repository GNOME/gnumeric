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
 * along with this program; if not, see <https://www.gnu.org/licenses/>.
 */

#include <gnumeric-config.h>
#include <glib/gi18n-lib.h>
#include <gnumeric.h>
#include <tools/auto-correct.h>

#include <application.h>
#include <gutils.h>
#include <gnumeric-conf.h>
#include <parse-util.h>
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
					if (g_str_has_prefix (begin, except)) {
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

static gboolean
autocorrect_first_letter_exception (const char *start, const char *end)
{
	GSList *l = gnm_conf_get_autocorrect_first_letter_list ();
	char *text;

	if (l == NULL)
		return FALSE;

	text = g_strndup (start, end - start + 1);

	for (; l != NULL; l = l->next) {
		if (g_str_has_suffix(text, l->data)) {
			g_free (text);
			return TRUE;
		}
	}

	g_free (text);
	return FALSE;
}


static gboolean
autocorrect_first_letter_trigger (gunichar this_char)
{
	if (!g_unichar_ispunct (this_char))
		return FALSE;

	return (
		this_char == 0x0021 ||
		this_char == 0x002e ||
		this_char == 0x003f ||
		this_char == 0x037e ||
		this_char == 0x0589 ||
		this_char == 0x061f ||
		this_char == 0x0700 ||
		this_char == 0x0701 ||
		this_char == 0x0702 ||
		this_char == 0x1362 ||
		this_char == 0x1367 ||
		this_char == 0x1368 ||
		this_char == 0x166e ||
		this_char == 0x1803 ||
		this_char == 0x1809 ||
		this_char == 0x1944 ||
		this_char == 0x1945 ||
		this_char == 0x203c ||
		this_char == 0x203d ||
		this_char == 0x2047 ||
		this_char == 0x2048 ||
		this_char == 0x2049 ||
		this_char == 0x3002 ||
		this_char == 0xfe52 ||
		this_char == 0xfe56 ||
		this_char == 0xfe57 ||
		this_char == 0xff01 ||
		this_char == 0xff0e ||
		this_char == 0xff1f ||
		this_char == 0xff61
		);
}

static char *
autocorrect_first_letter (const char *src)
{
	const char * last_end = NULL;
	const char *last_copy = src;
	const char *this;
	GString *gstr = NULL;
	gboolean seen_text = FALSE;
	gboolean seen_white = FALSE;

	for (this = src; '\0' != *this; this = g_utf8_next_char (this)) {
		gunichar this_char = g_utf8_get_char (this);

		seen_text = seen_text || g_unichar_isalpha (this_char);

		if (seen_text && autocorrect_first_letter_trigger (this_char))
			last_end = this;
		else if ((last_end != NULL) && g_unichar_isspace (this_char))
			seen_white = TRUE;
		else if ((last_end != NULL) && !g_unichar_isspace (this_char)) {
			if (seen_white) {
				gunichar new = g_unichar_totitle (this_char);

				if ((this_char != new) &&
				    !autocorrect_first_letter_exception (src, last_end)) {
					if (gstr == NULL)
						gstr = g_string_new (NULL);
					g_string_append_len (gstr, last_copy,
							     this - last_copy);
					g_string_append_unichar (gstr, new);
					last_copy = g_utf8_next_char (this);
				}
				seen_white = FALSE;
			}
			last_end = NULL;
		}
	}

	if (gstr != NULL) {
		g_string_append_len (gstr, last_copy,
				     strlen (last_copy));
		return g_string_free (gstr, FALSE);
	}

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
