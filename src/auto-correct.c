/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/*
 * auto-correct.c:
 *
 * Author:
 *        Jukka-Pekka Iivonen <jiivonen@hutcs.cs.hut.fi>
 *
 * (C) Copyright 2000, 2001 by Jukka-Pekka Iivonen <iivonen@iki.fi>
 */
#include <config.h>
#include "auto-correct.h"

#include <ctype.h>
#include <string.h>
#include <libgnome/gnome-defs.h>
#include <libgnome/gnome-config.h>

static struct {
	gboolean init_caps	: 1;
	gboolean first_letter	: 1;
	gboolean names_of_days	: 1;
	gboolean caps_lock	: 1;
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
	autocorrect.caps_lock = gnome_config_get_bool ("caps_lock=true");
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
	gnome_config_set_bool ("caps_lock", autocorrect.caps_lock);
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
	case AC_CAPS_LOCK :	return autocorrect.caps_lock;
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
	case AC_CAPS_LOCK :	autocorrect.caps_lock = val;	break;
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

/* Add the name of the days on your language if they are always capitalized.
 */
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

	len = strlen (ucommand);

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
		skip_first_letter:
			p = strchr(s, '.');
			if (p == NULL)
				break;
			while (isspace(*s))
				++s;
			if (islower (*s) && (s == ucommand || isspace (s[-1]))) {
				GList *cur = autocorrect.exceptions.first_letter;

				for ( ; cur != NULL; cur = cur->next) {
					guchar *t, *c = (guchar *)cur->data;
					gint  l = strlen (c);
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
		for (i = 0; autocorrect_day[i] != NULL; i++) {
			do {
				s = strstr (ucommand, autocorrect_day[i]);
				if (s != NULL)
					*s = toupper (*s);
			} while (s != NULL);
		}

	if (autocorrect.caps_lock)
		if (len > 1 && islower (ucommand[0]) && isupper (ucommand[1]))
			for (i = 0; i < len; i++)
				if (isalpha (ucommand[i])) {
					if (isupper (ucommand[i]))
						ucommand[i] = tolower (ucommand[i]);
					else
						ucommand[i] = toupper (ucommand[i]);
				}

	return ucommand;
}
