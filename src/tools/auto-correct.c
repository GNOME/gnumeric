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
#include "gnumeric.h"
#include "auto-correct.h"
#include "dates.h"

#include <ctype.h>
#include <string.h>
#include <libgnome/gnome-config.h>
#include <libgnome/gnome-i18n.h>

static struct {
	gboolean init_caps	: 1;
	gboolean first_letter	: 1;
	gboolean names_of_days	: 1;
	gboolean replace	: 1;

	struct {
		GList *first_letter;
		GList *init_caps;
	} exceptions;
} autocorrect;

void
autocorrect_init (void)
{
	gnome_config_push_prefix ("Gnumeric/AutoCorrect/");
	autocorrect.init_caps = gnome_config_get_bool ("init_caps=true");
	autocorrect.first_letter = gnome_config_get_bool ("first_letter=true");
	autocorrect.names_of_days = gnome_config_get_bool ("names_of_days=true");
	autocorrect.replace = gnome_config_get_bool ("replace=true");
	gnome_config_pop_prefix ();

	/* TODO */
	autocorrect.exceptions.first_letter = NULL;
	autocorrect.exceptions.init_caps = NULL;
}

void
autocorrect_store_config (void)
{
	gnome_config_push_prefix ("Gnumeric/AutoCorrect/");
	gnome_config_set_bool ("init_caps", autocorrect.init_caps);
	gnome_config_set_bool ("first_letter", autocorrect.first_letter);
	gnome_config_set_bool ("names_of_days", autocorrect.names_of_days);
	gnome_config_set_bool ("replace", autocorrect.replace);
	gnome_config_pop_prefix ();
}

gboolean
autocorrect_get_feature (AutoCorrectFeature feature)
{
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

GList *
autocorrect_get_exceptions (AutoCorrectFeature feature)
{
	switch (feature) {
	case AC_INIT_CAPS :	return autocorrect.exceptions.init_caps;
	case AC_FIRST_LETTER :	return autocorrect.exceptions.first_letter;
	default :
		g_warning ("Invalid autocorrect feature %d.", feature);
	};
	return NULL;
}

void
autocorrect_set_exceptions (AutoCorrectFeature feature, GList *list)
{
	switch (feature) {
	case AC_INIT_CAPS :    autocorrect.exceptions.init_caps = list;	   break;
	case AC_FIRST_LETTER : autocorrect.exceptions.first_letter = list; break;
	default :
		g_warning ("Invalid autocorrect feature %d.", feature);
	};
}

static char const * const autocorrect_day [] = {
        /* English */
        "monday", "tuesday", "wednesday", "thursday",
	"friday", "saturday", "sunday", NULL
};

char *
autocorrect_tool (char const *command)
{
        unsigned char *s;
	unsigned char *ucommand = (unsigned char *)g_strdup (command);
	gint i, len;

	len = strlen ((char *)ucommand);

        if (autocorrect.init_caps) {
		for (s = ucommand; *s; s++) {
		skip_ic_correct:
			if (isupper (*s) && isupper (s[1])) {
				if (islower (s[2])) {
					GList *c = autocorrect.exceptions.init_caps;
					while (c != NULL) {
						guchar *a = (guchar *)c->data;
						if (strncmp (s, a, strlen (a))
						    == 0) {
							s++;
							goto skip_ic_correct;
						}
						c = c->next;
					}
					s[1] = tolower (s[1]);
				} else
					while (!isspace(*s))
						++s;
			}
		}
	}

	if (autocorrect.first_letter) {
		unsigned char *p;

		for (s = ucommand; *s; s = p+1) {
			static char const * const not_punct = "~@#$%^&*()[]{}<>,/_-+=`\'\"\\";
		skip_first_letter:
			/* We need to find the end of a sentence assume ',' is not */
			for (p = s; *p != '\0' &&
			     !(ispunct (*p) && NULL == strchr (not_punct, *p)) ; p++)
				;
			if (*p == '\0')
				break;

			while (isspace(*s))
				++s;
			if (islower (*s) && (s == ucommand || isspace (s[-1]))) {
				GList *cur = autocorrect.exceptions.first_letter;

				for ( ; cur != NULL; cur = cur->next) {
					guchar *t, *c = (guchar *)cur->data;
					gint  l = strlen ((char *)c);
					gint  spaces = 0;

					for (t = s - 1; t >= ucommand; t--)
						if (isspace (*t))
							++spaces;
						else
							break;
					if (s - ucommand > l + spaces &&
					    strncmp(s-l-spaces, c, l) == 0) {
						s = p + 1;
						goto skip_first_letter;
					}
				}
				*s = toupper (*s);
			}
		}
	}

	if (autocorrect.names_of_days)
		for (i = 0; day_long[i] != NULL; i++) {
			char const *day = _(day_long [i]) + 1;
			s = ucommand;
			while (NULL != (s = (unsigned char *)strstr ((char *)s, day))) {
				if (s > ucommand &&
				    (s-1 == ucommand || isspace (s[-2])))
					s[-1] = toupper (s[-1]);
				s++;
			}
		}

	return (char *)ucommand;
}
