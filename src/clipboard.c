/*
 * Clipboard.c: Implements the copy/paste operations
 *
 * Author:
 *  MIguel de Icaza (miguel@gnu.org)
 *
 */
#include <config.h>
#include <gnome.h>
#include "gnumeric.h"
#include "gnumeric-util.h"
#include "clipboard.h"
#include "eval.h"
#include "render-ascii.h"

static gint
x_selection_clear (GtkWidget *widget, GdkEventSelection *event, Workbook *wb)
{
	wb->have_x_selection = FALSE;

	return TRUE;
}

static void
x_selection_handler (GtkWidget *widget, GtkSelectionData *selection_data, gpointer data)
{
	Workbook *wb = data;
	char *rendered_selection;
	
	g_assert (wb->clipboard_contents);

	rendered_selection = cell_region_render_ascii (wb->clipboard_contents);
	
	gtk_selection_data_set (
		selection_data, GDK_SELECTION_TYPE_STRING, 8,
		rendered_selection, strlen (rendered_selection));
}

void
x_clipboard_bind_workbook (Workbook *wb)
{
	wb->have_x_selection = FALSE;
	
	gtk_signal_connect (
		GTK_OBJECT (wb->toplevel), "selection_clear_event",
		GTK_SIGNAL_FUNC(x_selection_clear), wb);

	gtk_selection_add_handler (
		wb->toplevel,
		GDK_SELECTION_PRIMARY, GDK_SELECTION_TYPE_STRING,
		x_selection_handler, wb);
}

/*
 * clipboard_export_cell_region:
 *
 * This routine exports a CellRegion to the X selection
 */
static void
clipboard_export_cell_region (Workbook *wb, CellRegion *region)
{
	wb->have_x_selection = gtk_selection_owner_set (
		current_workbook->toplevel,
		GDK_SELECTION_PRIMARY,
		GDK_CURRENT_TIME);
}

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

/*
 * clipboard_copy_cell_range:
 *
 * Entry point to the clipboard copy code
 */
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

	clipboard_export_cell_region (sheet->workbook, c.r);
	
	return c.r;
}

static int
paste_cell (Sheet *dest_sheet, Cell *new_cell, int target_col, int target_row, int paste_flags)
{
	sheet_cell_add (dest_sheet, new_cell, target_col, target_row);
	
	if (!(paste_flags & PASTE_FORMULAS)){
		if (new_cell->parsed_node){
			expr_tree_unref (new_cell->parsed_node);
			new_cell->parsed_node = NULL;
		}
	}
	
	if (new_cell->parsed_node){
		if (paste_flags & PASTE_FORMULAS)
			cell_formula_relocate (new_cell, target_col, target_row);
		else 
			cell_make_value (new_cell);
	}

	cell_render_value (new_cell);
	
	if (!(paste_flags & PASTE_FORMULAS)){
		string_unref (new_cell->entered_text);
		new_cell->entered_text = string_ref (new_cell->text);
	}

	sheet_redraw_cell_region (dest_sheet,
				  target_col, target_row,
				  target_col, target_row);

	return new_cell->parsed_node != 0;
}

void
clipboard_paste_region (CellRegion *region, Sheet *dest_sheet,
			int dest_col,    int dest_row,
			int paste_width, int paste_height,
			int paste_flags)
{
	CellCopyList *l;
	GList *deps;
	int formulas = 0;
	int col, row;

	g_return_if_fail (region != NULL);
	g_return_if_fail (dest_sheet != NULL);
	g_return_if_fail (IS_SHEET (dest_sheet));

	/* Clear the region where we will paste */
	if (paste_flags & (PASTE_VALUES | PASTE_FORMULAS))
		sheet_clear_region (dest_sheet,
				    dest_col, dest_row,
				    dest_col + paste_width - 1,
				    dest_row + paste_height - 1);

	/* If no operations are defined, we clear the area */
	if (!(paste_flags & PASTE_OP_MASK))
		sheet_redraw_cell_region (dest_sheet,
					  dest_col, dest_row,
					  dest_col + paste_width - 1,
					  dest_row + paste_height - 1);
	
	/* Paste each element */
	for (col = 0; col < paste_width; col += region->cols){
		for (row = 0; row < paste_height; row += region->rows){
			for (l = region->list; l; l = l->next){
				CellCopy *c_copy = l->data;
				Cell *new_cell;
				int target_col, target_row;
				
				target_col = col + dest_col + c_copy->col_offset;
				target_row = row + dest_row + c_copy->row_offset;

				if (target_col > dest_col + paste_width - 1)
					continue;

				if (target_row > dest_row + paste_height - 1)
					continue;

				if (!(paste_flags & (PASTE_FORMULAS | PASTE_VALUES))){
					Cell *cell;
					
					cell = sheet_cell_get (dest_sheet,
							       target_col,
							       target_row);
					if (cell && c_copy->cell)
						cell_set_style (cell, c_copy->cell->style);
				} else {
					new_cell = cell_copy (c_copy->cell);
				
					formulas |= paste_cell (
						dest_sheet, new_cell,
						target_col, target_row, paste_flags);
				}
			}
		}
	}
	
	deps = region_get_dependencies (
		dest_sheet,
		dest_col, dest_row,
		dest_col + paste_width - 1,
		dest_row + paste_height -1);

	if (deps){
		cell_queue_recalc_list (deps);
		formulas = 1;
	}

	/* Trigger a recompute if required */
	if (formulas)
		workbook_recalc (dest_sheet->workbook);
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


