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
static char *
font_get_component (char *font, int idx)
{
	char *comp, *p, *ep;
	int len, i;

	if (!font)
		return NULL;
	p = font;
	if (*p == '-')
		p++;
	for (i = 0; i < idx; i++) {
		p = strchr (p, '-');
		if (!p)
			break;
		if (*(p+1) != '-')
			p++;
	}
	if (!p) {
		return NULL;
	}
	len = strlen (p);
	ep = strchr (p, '-');
	if (ep)
		len = ep - p;
	if (!len)
		return NULL;
	comp = g_malloc (len+1);
	if (!comp)
		return NULL;
	comp[len] = '\0';
	memcpy (comp, p, len);
	return comp;
}

/*
 */
int
font_is_monospaced (Style *style)
{
	char *comp;
	int rc = 0;

	if (!style)
		return 0;

	comp = font_get_component (style->font->font_name, 10);
	/* printf ("%s\n", style->font->font_name); */
	if (!comp)
		return 0;
	if (*comp == 'c' || *comp == 'm')
		rc = 1;
	g_free (comp);
	return rc;
}

/*
 */
int
font_is_helvetica (Style *style)
{
	char *comp;
	int rc = 0;

	if (!style)
		return 0;

	comp = font_get_component (style->font->font_name, 1);
	/* printf ("%s\n", style->font->font_name); */
	if (!comp)
		return 0;
	if (strcasecmp (comp, "helvetica") == 0)
		rc = 1;
	g_free (comp);
	return rc;
}

/*
 */
int
font_is_sansserif (Style *style)
{
	char *comp;
	int rc = 0, i;
	char *name[] = {
		"helvetica","avantgarde","neep","blippo","capri","clean","fixed",NULL};

	if (!style)
		return 0;

	comp = font_get_component (style->font->font_name, 1);
	if (!comp)
		return 0;
	for (i = 0; name[i]; i++) {
		if (strcasecmp (comp, name[i]) == 0) {
			rc = 1;
			break;
		}
	}
	g_free (comp);
	return rc;
}

/*
 */
int
font_get_size (Style *style)
{
	int size, div = 0;
	char *comp;

	if (!style)
		return 0;

	comp = font_get_component (style->font->font_name, 6);
	if (!comp || (*comp == '*')) {
		if (comp)
			g_free (comp);
		comp = font_get_component (style->font->font_name, 7);
		div = 10;
	}
	if (!comp || (*comp == '*')) {
		if (comp)
			g_free (comp);
		return 0;
	}
	size = atoi (comp);
	g_free (comp);
	if (div)
		size = size / div;
	return size;
}

