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

static int
clipboard_append_cell (Sheet *sheet, int col, int row, Cell *cell, void *user_data)
{
	append_cell_closure_t *c = user_data;
	CellCopy *copy;

	copy = g_new (CellCopy, 1);

	copy->cell = cell_copy (cell);
	copy->col_offset  = col - c->base_col;
	copy->row_offset  = row - c->base_row;
	
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

	g_return_val_if_fail (sheet != NULL, NULL);
	g_return_val_if_fail (IS_SHEET (sheet), NULL);
	g_return_val_if_fail (start_col <= end_col, NULL);
	g_return_val_if_fail (start_row <= end_row, NULL);
	
	c.r = g_new0 (CellRegion, 1);

	c.base_col = start_col;
	c.base_row = start_row;
	c.r->cols = end_col - start_col + 1;
	c.r->rows = end_row - start_row + 1;
	
	sheet_cell_foreach_range (
		sheet, 1, start_col, start_row, end_col, end_row,
		clipboard_append_cell, &c);

	return c.r;
}

void
clipboard_paste_region (CellRegion *region, Sheet *dest_sheet, int dest_col, int dest_row, int paste_flags)
{
	CellCopyList *l;
	int paste_formulas = paste_flags & PASTE_FORMULAS;
	int paste_formats = paste_formulas & PASTE_FORMATS;
	
	g_return_if_fail (region != NULL);
	g_return_if_fail (dest_sheet != NULL);
	g_return_if_fail (IS_SHEET (dest_sheet));

	/* Clear the region where we will paste */
	sheet_clear_region (dest_sheet,
			    dest_col, dest_row,
			    dest_col + region->cols - 1,
			    dest_row + region->rows - 1);

	/* Paste each element */
	for (l = region->list; l; l = l->next){
		CellCopy *c_copy = l->data;
		Cell *new_cell;

		/* FIXME: create a cell_copy_flags that uses
		 * the bits more or less like paste_flags
		 */
		new_cell = cell_copy (c_copy->cell);
		
		sheet_cell_add (dest_sheet, new_cell,
				dest_col + c_copy->col_offset,
				dest_row + c_copy->row_offset);
	}
}

void
clipboard_release (CellRegion *region)
{
	CellCopyList *l;

	g_return_if_fail (region != NULL);
	
	l = region->list;

	for (; l; l = l->next){
		CellCopy *this_cell = l->data;

		cell_destroy (this_cell->cell);
		g_free (this_cell);
	}
	g_list_free (region->list);
	g_free (region);
}
