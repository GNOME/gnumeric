/*
 * render-ascii.c: Renders a cell region into ascii text
 *
 * Author:
 *  MIguel de Icaza (miguel@gnu.org)
 *
 */
#include <config.h>
#include <gnome.h>
#include "gnumeric.h"
#include "gnumeric-util.h"
#include "render-ascii.h"

char *
cell_region_render_ascii (CellRegion *cr)
{
	int col, row;
	char ***data;
	GList *l;
	GString *all;

	g_assert (cr != NULL);
	data = g_new0 (char **, cr->rows);

	for (row = 0; row < cr->rows; row++)
		data [row] = g_new0 (char *, cr->cols);
	
	for (l = cr->list; l; l = l->next){
		CellCopy *c_copy = l->data;

		data [c_copy->row_offset][c_copy->col_offset] = CELL_TEXT_GET (c_copy->cell);
	}

	all = g_string_new (NULL);
	for (row = 0; row < cr->rows; row++){
		GString *str;

		str = g_string_new (NULL);

		for (col = 0; col < cr->cols; col++){
			if (data [row][col])
				g_string_append (str, data [row][col]);
			g_string_append_c (str, '\t');
		}
		g_string_append (all, str->str);
		g_string_append_c (all, '\n');
	}

	return all->str;
}


