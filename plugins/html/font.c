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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <gnome.h>
#include "config.h"
#include "font.h"

/*
 */
int
font_is_monospaced (Style *style)
{
	char *name[] = {"Courier", "fixed", NULL};
	int i;

	if (!style)
		return 0;
	if (!style->font)
		return 0;
	if (!style->font->font_name)
		return 0;

	/* printf ("%s\n", style->font->font_name); */
	for (i = 0; name[i]; i++) {
		if (strcmp (name[i], style->font->font_name) == 0)
			return 1;
	}
	return 0;
}

/*
 */
int
font_is_helvetica (Style *style)
{
	if (!style)
		return 0;
	if (!style->font)
		return 0;
	if (!style->font->font_name)
		return 0;

	if (strcmp ("Helvetica", style->font->font_name) == 0)
		return 1;
	return 0;
}

/*
 */
int
font_is_sansserif (Style *style)
{
	int i;
	char *name[] = {
		"helvetica","avantgarde","neep","blippo","capri","clean","fixed",NULL};

	if (!style)
		return 0;
	if (!style->font)
		return 0;
	if (!style->font->font_name)
		return 0;

	for (i = 0; name[i]; i++) {
		if (strcasecmp (style->font->font_name, name[i]) == 0)
			return 1;
	}
	return 0;
}

/*
 */
int
font_get_size (Style *style)
{
	if (!style)
		return 0;
	if (!style->font)
		return 0;

	/* printf ("%f %f\n", style->font->size, style->font->scale); */
	return ((int)style->font->size);
}

