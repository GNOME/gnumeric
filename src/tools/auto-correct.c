/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/*
 * auto-correct.c:
 *
 * Author:
 *        Jukka-Pekka Iivonen <jiivonen@hutcs.cs.hut.fi>
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
#include <gnumeric-i18n.h>
#include "gnumeric.h"
#include "auto-correct.h"

#include "dates.h"
#include "application.h"
#include "gutils.h"
#include <gsf/gsf-impl-utils.h>

#include <ctype.h>
#include <string.h>

static struct {
	gboolean init_caps	: 1;
	gboolean first_letter	: 1;
	gboolean names_of_days	: 1;
	gboolean replace	: 1;

	struct {
		GSList *first_letter;
		GSList *init_caps;
	} exceptions;
	
	guint notification_id;
} autocorrect;

#define AUTOCORRECT_DIRECTORY "/apps/gnumeric/autocorrect"
#define AUTOCORRECT_INIT_CAPS AUTOCORRECT_DIRECTORY "/init-caps"
#define AUTOCORRECT_INIT_CAPS_LIST AUTOCORRECT_DIRECTORY "/init-caps-list"
#define AUTOCORRECT_FIRST_LETTER AUTOCORRECT_DIRECTORY "/first-letter"
#define AUTOCORRECT_FIRST_LETTER_LIST AUTOCORRECT_DIRECTORY "/first-letter-list"
#define AUTOCORRECT_NAMES_OF_DAYS AUTOCORRECT_DIRECTORY "/names-of-days"
#define AUTOCORRECT_REPLACE AUTOCORRECT_DIRECTORY "/replace"

static void cb_autocorrect_update (GConfClient *gconf, guint cnxn_id,
				   GConfEntry *entry, gpointer ignore);

static void
autocorrect_clear (void)
{
	autocorrect_set_exceptions (AC_INIT_CAPS, NULL);
	autocorrect_set_exceptions (AC_FIRST_LETTER, NULL);
}

static void
autocorrect_load (void)
{
	GConfClient *client = application_get_gconf_client ();

	autocorrect.init_caps = gconf_client_get_bool (client,
		AUTOCORRECT_INIT_CAPS, NULL);
	autocorrect_set_exceptions (AC_INIT_CAPS, gconf_client_get_list (client,
		AUTOCORRECT_INIT_CAPS_LIST, GCONF_VALUE_STRING, NULL));

	autocorrect.first_letter = gconf_client_get_bool (client,
		AUTOCORRECT_FIRST_LETTER, NULL);
	autocorrect_set_exceptions (AC_FIRST_LETTER, gconf_client_get_list (client,
		AUTOCORRECT_FIRST_LETTER_LIST, GCONF_VALUE_STRING, NULL));

	autocorrect.names_of_days = gconf_client_get_bool (client,
		AUTOCORRECT_NAMES_OF_DAYS, NULL);
	autocorrect.replace = gconf_client_get_bool (client,
		AUTOCORRECT_REPLACE, NULL);
}

static void
autocorrect_init (void)
{
	if (autocorrect.notification_id != 0)
		return;

	autocorrect_load ();
	autocorrect.notification_id = gconf_client_notify_add (
		application_get_gconf_client (),
		AUTOCORRECT_DIRECTORY, cb_autocorrect_update,
		NULL, NULL, NULL);
	g_object_set_data_full (gnumeric_application_get_app (),
		"ToolsAutoCorrect", GINT_TO_POINTER (1),
		(GDestroyNotify) autocorrect_clear);
}

static void
cb_autocorrect_update (GConfClient *gconf, guint cnxn_id, GConfEntry *entry, 
		       gpointer ignore)
{
	autocorrect_clear ();
	autocorrect_load ();
}

void
autocorrect_store_config (void)
{
	GConfChangeSet *cs = gconf_change_set_new ();
	GSList *init_caps = autocorrect_get_exceptions (AC_INIT_CAPS);
	GSList *first_letter = autocorrect_get_exceptions (AC_FIRST_LETTER);

	gconf_change_set_set_bool (cs, AUTOCORRECT_INIT_CAPS,
		autocorrect.init_caps);
	gconf_change_set_set_list (cs, AUTOCORRECT_INIT_CAPS_LIST,
		GCONF_VALUE_STRING, init_caps);
	gconf_change_set_set_bool (cs, AUTOCORRECT_FIRST_LETTER,
		autocorrect.first_letter);
	gconf_change_set_set_list (cs, AUTOCORRECT_FIRST_LETTER_LIST,
	       GCONF_VALUE_STRING, first_letter);
	gconf_change_set_set_bool (cs, AUTOCORRECT_NAMES_OF_DAYS,
		autocorrect.names_of_days);
	gconf_change_set_set_bool (cs, AUTOCORRECT_REPLACE,
		autocorrect.replace);

	gconf_client_commit_change_set (application_get_gconf_client (),
					cs, FALSE, NULL);
	gconf_client_suggest_sync (application_get_gconf_client (), NULL);
	gconf_change_set_unref (cs);

	g_slist_foreach (init_caps, (GFunc)g_free, NULL);
	g_slist_free (init_caps);
	g_slist_foreach (first_letter, (GFunc)g_free, NULL);
	g_slist_free (first_letter);

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
 * Return a list of utf8 encoded strings.  Both the list and the content need to be freed.
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
		accum = g_slist_prepend (accum,
			g_ucs4_to_utf8 (ptr->data, -1, NULL, NULL, NULL));
	return g_slist_reverse (accum);
}

/**
 * autocorrect_set_exceptions :
 * @feature :
 * @list : A GSList of utf8 encoded strings.
 *
 **/
void
autocorrect_set_exceptions (AutoCorrectFeature feature, GSList const *list)
{
	GSList **res, *accum = NULL;
	
	switch (feature) {
	case AC_INIT_CAPS : res = &autocorrect.exceptions.init_caps; break;
	case AC_FIRST_LETTER :res = &autocorrect.exceptions.first_letter; break;
	default :
		g_warning ("Invalid autocorrect feature %d.", feature);
		return;
	};

	for (; list; list = list->next)
		accum = g_slist_prepend (accum,
			g_utf8_to_ucs4 (list->data, -1, NULL, NULL, NULL));
	accum = g_slist_reverse (accum);
		
	g_slist_foreach (*res, (GFunc)g_free, NULL);
	g_slist_free (*res);
	*res = accum;	   
}

static char const * const autocorrect_day [] = {
        /* English */
        "monday", "tuesday", "wednesday", "thursday",
	"friday", "saturday", "sunday", NULL
};

char *
autocorrect_tool (char const *command)
{
        gunichar *s, *p;
	gunichar *ucommand = g_utf8_to_ucs4 (command, -1, NULL, NULL, NULL);
	gint i, len;
	static gunichar const not_punct[] = {
		'~', '@', '#', '$', '%',
		'^', '&', '*', '(', ')',
		'[', ']', '{', '}', '<',
		'>', ',', '/', '_', '-',
		'+', '=', '`', '\'', '\"', '\\'
	};

	autocorrect_init ();

	len = strlen ((char *)ucommand);

        if (autocorrect.init_caps) {
		for (s = ucommand; *s; s++) {
		skip_ic_correct:
			if (g_unichar_isupper (s[0]) && g_unichar_isupper (s[1])) {
				if (g_unichar_islower (s[2])) {
					GSList *c = autocorrect.exceptions.init_caps;
					while (c != NULL) {
						gunichar const *a = c->data;
						if (g_unichar_strncmp (s, a, g_unichar_strlen (a)) == 0) {
							s++;
							goto skip_ic_correct;
						}
						c = c->next;
					}
					s[1] = g_unichar_tolower (s[1]);
				} else
					while (*s && !g_unichar_isspace(*s))
						++s;
			}
		}
	}

	if (autocorrect.first_letter) {
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
	}

	if (autocorrect.names_of_days)
		for (i = 0; day_long[i] != NULL; i++) {
			char const *day = _(day_long [i]) + 1;
			for (s = ucommand ; NULL != (s = (gunichar *)g_unichar_strstr_utf8 (s, day)) ; s++)
				if (s > ucommand &&
				    (s-1 == ucommand || g_unichar_isspace (s[-2])))
					s[-1] = g_unichar_toupper (s[-1]);
		}

	command = g_ucs4_to_utf8 (ucommand, -1, NULL, NULL, NULL);
	g_free (ucommand);
	return (char *)command;
}
