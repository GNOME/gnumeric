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
	GString *all, *line;
	GList *l;
	char ***data, *return_val;
	int col, row;

	g_assert (cr != NULL);
	data = g_new0 (char **, cr->rows);

	for (row = 0; row < cr->rows; row++)
		data [row] = g_new0 (char *, cr->cols);
	
	for (l = cr->list; l; l = l->next){
		CellCopy *c_copy = l->data;

		data [c_copy->row_offset][c_copy->col_offset] = CELL_TEXT_GET (c_copy->cell);
	}

	all = g_string_new (NULL);
	line = g_string_new (NULL);
	for (row = 0; row < cr->rows; row++){
		g_string_assign (line, "");
		
		for (col = 0; col < cr->cols; col++){
			if (data [row][col])
				g_string_append (line, data [row][col]);
			g_string_append_c (line, '\t');
		}
		g_string_append (all, line->str);
		g_string_append_c (all, '\n');
	}

	return_val = g_strdup (all->str);

	/* Release, everything we used */
	g_string_free (line, TRUE);
	g_string_free (all, TRUE);

	for (row = 0; row < cr->rows; row++)
		g_free (data [row]);
	g_free (data);

	return return_val;
}


