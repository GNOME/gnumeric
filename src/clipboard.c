/*
 * Clipboard.c: Implements the copy/paste operations
 * (C) 1998 The Free Software Foundation.
 *
 * Author:
 *  MIguel de Icaza (miguel@gnu.org)
 *
 */
#include <config.h>
#include <gnome.h>
#include "gnumeric.h"
#include "clipboard.h"

typedef struct {
	int        base_col, base_row;
	CellRegion *r;
} append_cell_closure_t;

int
clipboard_append_cell (Sheet *sheet, int col, int row, Cell *cell, void *user_data)
{
	append_cell_closure_t *c = user_data;
	CellCopy *copy;

	copy = g_new (CellCopy, 1);

	copy->cell = cell_copy (cell);
	copy->col_offset  = col - c->base_col;
	copy->row_offset  = col - c->base_col;
	
	/* Now clear the traces and dependencies on the copied Cell */
	copy->cell->col   = NULL;
	copy->cell->row   = NULL;
	copy->cell->sheet = NULL;
	
	c->r->list = g_list_prepend (c->r->list, copy);

	return TRUE;
}

CellRegion *
clipboard_copy_cell_range (Sheet *sheet, int start_col, int start_row, int end_col, int end_row)
{
	append_cell_closure_t c;

	g_return_if_fail (sheet != NULL);
	g_return_if_fail (IS_SHEET (sheet));
	g_return_if_fail (start_col <= end_col);
	g_return_if_fail (start_row <= end_row);
	
	c.r = g_new (CellRegion, 1);

	c.base_col = start_col;
	c.base_row = start_row;
	c.r->cols = end_col - start_col + 2;
	c.r->rows = end_row - start_row + 2;
	
	sheet_cell_foreach_range (
		sheet, 1, start_col, start_row, end_col, end_row,
		clipboard_append_cell, r);
				  
}

void
clipboard_paste_region (CellRegion *region, Sheet *dest_sheet, int dest_col, int_dest_row)
{
	sheet_clear_region (sheet,
			    dest_col, dest_row,
			    dest_col + region->cols,
			    dest_row + region->row);
}
