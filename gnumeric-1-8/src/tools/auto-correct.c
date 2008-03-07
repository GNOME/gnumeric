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
#include <goffice/utils/go-format.h>
#include <goffice/utils/go-glib-extras.h>
#include <gsf/gsf-impl-utils.h>
#include <string.h>

static struct {
	gboolean init_caps;
	gboolean first_letter;
	gboolean names_of_days;
	gboolean replace;

	struct {
		GSList *first_letter;
		GSList *init_caps;
	} exceptions;

	guint notification_id;
} autocorrect;

#define AUTOCORRECT_DIRECTORY		"autocorrect"
#define AUTOCORRECT_INIT_CAPS		"init-caps"
#define AUTOCORRECT_INIT_CAPS_LIST	"init-caps-list"
#define AUTOCORRECT_FIRST_LETTER	"first-letter"
#define AUTOCORRECT_FIRST_LETTER_LIST	"first-letter-list"
#define AUTOCORRECT_NAMES_OF_DAYS	"names-of-days"
#define AUTOCORRECT_REPLACE		"replace"

static void
autocorrect_clear (void)
{
	autocorrect_set_exceptions (AC_INIT_CAPS, NULL);
	autocorrect_set_exceptions (AC_FIRST_LETTER, NULL);
}

static void
autocorrect_load (void)
{
	GOConfNode *node = go_conf_get_node (gnm_conf_get_root (), AUTOCORRECT_DIRECTORY);

	autocorrect.init_caps = go_conf_load_bool (node, AUTOCORRECT_INIT_CAPS, TRUE);
	autocorrect_set_exceptions (AC_INIT_CAPS,
		go_conf_load_str_list (node, AUTOCORRECT_INIT_CAPS_LIST));
	autocorrect.first_letter = go_conf_load_bool (node, AUTOCORRECT_FIRST_LETTER, TRUE);
	autocorrect_set_exceptions (AC_FIRST_LETTER,
		go_conf_load_str_list (node, AUTOCORRECT_FIRST_LETTER_LIST));
	autocorrect.names_of_days = go_conf_load_bool (node, AUTOCORRECT_NAMES_OF_DAYS, TRUE);
	autocorrect.replace = go_conf_load_bool (node, AUTOCORRECT_REPLACE, TRUE);
	go_conf_free_node (node);
}

static void
cb_autocorrect_update (GOConfNode *node, gchar const *key, gpointer data)
{
	autocorrect_clear ();
	autocorrect_load ();
}

static void
autocorrect_init (void)
{
	GOConfNode *node;

	if (autocorrect.notification_id != 0)
		return;

	autocorrect_load ();
	node = go_conf_get_node (gnm_conf_get_root (), AUTOCORRECT_DIRECTORY);
	autocorrect.notification_id = go_conf_add_monitor (
		node, AUTOCORRECT_DIRECTORY, &cb_autocorrect_update, NULL);
	go_conf_free_node (node);
	g_object_set_data_full (gnm_app_get_app (),
		"ToolsAutoCorrect", GINT_TO_POINTER (1),
		(GDestroyNotify) autocorrect_clear);
}

void
autocorrect_store_config (void)
{
	GOConfNode *node = go_conf_get_node (gnm_conf_get_root (), AUTOCORRECT_DIRECTORY);

	go_conf_set_bool (node, AUTOCORRECT_INIT_CAPS,
		autocorrect.init_caps);
	go_conf_set_str_list (node, AUTOCORRECT_INIT_CAPS_LIST,
		autocorrect.exceptions.init_caps);
	go_conf_set_bool (node, AUTOCORRECT_FIRST_LETTER,
		autocorrect.first_letter);
	go_conf_set_str_list (node, AUTOCORRECT_FIRST_LETTER_LIST,
	       autocorrect.exceptions.first_letter);
	go_conf_set_bool (node, AUTOCORRECT_NAMES_OF_DAYS,
		autocorrect.names_of_days);
	go_conf_set_bool (node, AUTOCORRECT_REPLACE,
		autocorrect.replace);
	go_conf_sync (node);
	go_conf_free_node (node);
}

gboolean
autocorrect_get_feature (AutoCorrectFeature feature)
{
	autocorrect_init ();

	switch (feature) {
	case AC_INIT_CAPS :	return autocorrect.init_caps;
	case AC_FIRST_LETTER :	return autocorrect.first_letter;
	case AC_NAMES_OF_DAYS :	return autocorrect.names_of_days;
	case AC_REPLACE :	return autocorrect.replace;
	default :
		g_warning ("Invalid autocorrect feature %d.", feature);
	};
	return TRUE;
}

void
autocorrect_set_feature (AutoCorrectFeature feature, gboolean val)
{
	switch (feature) {
	case AC_INIT_CAPS :	autocorrect.init_caps = val;	break;
	case AC_FIRST_LETTER :	autocorrect.first_letter = val;	break;
	case AC_NAMES_OF_DAYS :	autocorrect.names_of_days = val;break;
	case AC_REPLACE :	autocorrect.replace = val;	break;
	default :
		g_warning ("Invalid autocorrect feature %d.", feature);
	};
}

/**
 * autocorrect_get_exceptions :
 * @feature :
 *
 * Return a list of UTF-8 encoded strings.  Both the list and the content need to be freed.
 **/
GSList *
autocorrect_get_exceptions (AutoCorrectFeature feature)
{
	GSList *ptr, *accum;

	autocorrect_init ();

	switch (feature) {
	case AC_INIT_CAPS :    ptr = autocorrect.exceptions.init_caps; break;
	case AC_FIRST_LETTER : ptr = autocorrect.exceptions.first_letter; break;
	default :
		g_warning ("Invalid autocorrect feature %d.", feature);
		return NULL;
	};

	for (accum = NULL; ptr != NULL; ptr = ptr->next)
		accum = g_slist_prepend (accum, g_strdup (ptr->data));
	return g_slist_reverse (accum);
}

/**
 * autocorrect_set_exceptions :
 * @feature :
 * @list : A GSList of UTF-8 encoded strings.
 *
 **/
void
autocorrect_set_exceptions (AutoCorrectFeature feature, GSList const *list)
{
	GSList **res, *accum = NULL;

	switch (feature) {
	case AC_INIT_CAPS: res = &autocorrect.exceptions.init_caps; break;
	case AC_FIRST_LETTER: res = &autocorrect.exceptions.first_letter; break;
	default :
		g_warning ("Invalid autocorrect feature %d.", feature);
		return;
	};

	for (; list; list = list->next)
		accum = g_slist_prepend (accum, g_strdup (list->data));
	accum = g_slist_reverse (accum);

	go_slist_free_custom (*res, g_free);
	*res = accum;
}

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

				for (l = autocorrect.exceptions.init_caps; l; l = l->next) {
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
			GSList const *cur = autocorrect.exceptions.first_letter;

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

	autocorrect_init ();

        if (autocorrect.init_caps) {
		char *res2 = autocorrect_initial_caps (src);
		if (res2) {
			g_free (res);
			src = res = res2;
		}
	}

	if (autocorrect.first_letter) {
		char *res2 = autocorrect_first_letter (src);
		if (res2) {
			g_free (res);
			src = res = res2;
		}
	}

	if (autocorrect.names_of_days) {
		char *res2 = autocorrect_names_of_days (src);
		if (res2) {
			g_free (res);
			src = res = res2;
		}
	}

	if (!res) res = g_strdup (src);
	return res;
}
