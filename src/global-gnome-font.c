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

#include <string.h>
#include <stdio.h>
#include <libgnomeprint/gnome-font.h>

GList *gnumeric_font_family_list = NULL;
GList *gnumeric_point_size_list = NULL;

int const gnumeric_point_sizes [] = {
	4, 8, 9, 10, 11, 12, 14, 16, 18,
	20, 22, 24, 26, 28, 36, 48, 72,
	0
};

void
global_gnome_font_init (void)
{
	int i;
	GList *l, *ll;

	l = gnome_font_family_list ();

	for (ll = l; ll; ll = ll->next){
		gnumeric_font_family_list = g_list_insert_sorted (
			gnumeric_font_family_list, g_strdup (ll->data), (GCompareFunc)strcmp);
	}
	gnome_font_family_list_free (l);

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

	for (l = gnumeric_font_family_list; l; l = l->next)
		g_free (l->data);

	g_list_free (gnumeric_font_family_list);

	for (l = gnumeric_point_size_list; l; l = l->next){
		g_free (l->data);
	}
	g_list_free (gnumeric_point_size_list);
}

