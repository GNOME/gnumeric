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

/*
 * Renders a CellRegion (we only deal with Cell Regions of type
 * Cell, we do not render the text ones, as those can only happen
 * if we got the information from the X selection.
 */
char *
cell_region_render_ascii (CellRegion *cr)
{
	GString *all, *line;
	GList *l;
	char ***data, *return_val;
	int col, row;

	g_return_val_if_fail (cr != NULL, NULL);
	
	data = g_new0 (char **, cr->rows);

	for (row = 0; row < cr->rows; row++)
		data [row] = g_new0 (char *, cr->cols);
	
	for (l = cr->list; l; l = l->next){
		CellCopy *c_copy = l->data;
		char *v;
		
		if (c_copy->type != CELL_COPY_TYPE_TEXT)
			v = g_strdup (c_copy->u.text);
		else
			v = cell_get_text (c_copy->u.cell.cell);
		
		data [c_copy->row_offset][c_copy->col_offset] = v;
	}

	all = g_string_new (NULL);
	line = g_string_new (NULL);
	for (row = 0; row < cr->rows; row++){
		g_string_assign (line, "");
		
		for (col = 0; col < cr->cols; col++){
			if (data [row][col]){
				g_string_append (line, data [row][col]);
				g_free (data [row][col]);
			}
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


