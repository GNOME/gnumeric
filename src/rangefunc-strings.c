/*
 * rangefunc-strings.c: String Functions on ranges.
 *
 * Authors:
 *   Andreas J. Guelzow  <aguelzow@taliesin.ca>
 */

#include <gnumeric-config.h>
#include "gnumeric.h"
#include "rangefunc-strings.h"

#include <string.h>

static void
cb_concatenate (char const *text, GString *str)
{
	g_string_append (str, text); 
}

int 
range_concatenate (GSList *data, char **res) 
{
	GString *str = g_string_new ("");

	g_slist_foreach (data, (GFunc) cb_concatenate, str);

	*res = g_string_free (str, FALSE);
	return 0;
}

