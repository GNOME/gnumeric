/*
 * Global Gnome Font data structures.  To avoid duplicating this across
 * workbooks.
 *
 * Author:
 *   Miguel de Icaza (miguel@gnu.org)
 *
 */
#include <gnumeric-config.h>
#include "gnumeric.h"
#include "global-gnome-font.h"

#include <gdk/gdkpango.h>
#include <pango/pango-context.h>
#include <pango/pango-font.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

GList *gnumeric_font_family_list = NULL;
GList *gnumeric_point_size_list = NULL;
static PangoFontFamily **pango_families;

int const gnumeric_point_sizes [] = {
	4, 8, 9, 10, 11, 12, 14, 16, 18,
	20, 22, 24, 26, 28, 36, 48, 72,
	0
};

static int
compare_family_pointers_by_name (gconstpointer a, gconstpointer b)
{
	PangoFontFamily * const * const fa = a;
	PangoFontFamily * const * const fb = b;
	return g_utf8_collate (pango_font_family_get_name (*fa),
			       pango_font_family_get_name (*fb));
}

void
global_gnome_font_init (void)
{
	int n_families, i;

	pango_context_list_families (gdk_pango_context_get (),
		&pango_families, &n_families);
	qsort (pango_families, n_families, sizeof (*pango_families),
	       compare_family_pointers_by_name);

	for (i = 0 ; i < n_families ; i++)
		gnumeric_font_family_list = g_list_prepend (
			gnumeric_font_family_list,
			(gpointer) pango_font_family_get_name (pango_families[i]));

	gnumeric_font_family_list = g_list_reverse (gnumeric_font_family_list);

	for (i = 0; gnumeric_point_sizes [i] != 0; i++){
		char buffer [6];

		sprintf (buffer, "%d", gnumeric_point_sizes [i]);
		gnumeric_point_size_list = g_list_prepend (
			gnumeric_point_size_list,
			g_strdup (buffer));
	}
}

void
global_gnome_font_shutdown (void)
{
	GList *l;

	g_free (pango_families);
	pango_families = NULL;
	g_list_free (gnumeric_font_family_list);
	gnumeric_font_family_list = NULL;

	for (l = gnumeric_point_size_list; l; l = l->next)
		g_free (l->data);
	g_list_free (gnumeric_point_size_list);
	gnumeric_point_size_list = NULL;
}

