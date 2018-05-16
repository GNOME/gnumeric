/*
 * font.c
 *
 * Copyright (C) 1999 Rasca, Berlin
 * EMail: thron@gmx.de
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
#include <gnumeric.h>
#include "font.h"
#include <style.h>

static int
font_match (GnmStyle const *mstyle, char const **names)
{
	int i;
	char const *font_name;

	if (!mstyle)
		return 0;
	font_name = gnm_style_get_font_name (mstyle);

	g_return_val_if_fail (names != NULL, 0);
	g_return_val_if_fail (font_name != NULL, 0);

	for (i = 0; names[i]; i++) {
		if (g_ascii_strcasecmp (font_name, names[i]) == 0)
			return 1;
	}
	return 0;
}

/*
 */
int
font_is_monospaced (GnmStyle const *mstyle)
{
	char const *names[] = { "Courier", "fixed", NULL };

	return font_match (mstyle, names);
}

/*
 */
int
font_is_helvetica (GnmStyle const *mstyle)
{
	char const *names [] = { "Helvetica", NULL };

	return font_match (mstyle, names);
}

/*
 */
int
font_is_sansserif (GnmStyle const *mstyle)
{
	char const *names [] = { "helvetica", "avantgarde",
				 "neep", "blippo", "capri",
				 "clean", "fixed", NULL };

	return font_match (mstyle, names);
}

