/*
 * search.c:  Search-and-replace for Gnumeric.
 *
 * Author:
 *   Morten Welinder (terra@diku.dk)
 *
 */

#include <config.h>
#include <gnome.h>
#include "search.h"

/* ------------------------------------------------------------------------- */

SearchReplace *
search_replace_new (void)
{
	return g_new0 (SearchReplace, 1);
}

/* ------------------------------------------------------------------------- */

void
search_replace_free (SearchReplace *sr)
{
	g_free (sr->search_text);
	g_free (sr->replace_text);
	g_free (sr);
}

/* ------------------------------------------------------------------------- */

void
search_replace (Workbook *wb, const SearchReplace *sr)
{
	g_warning ("Umplemented.");
}

/* ------------------------------------------------------------------------- */
