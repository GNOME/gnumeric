/*
 * Clipboard.c: Implements the copy/paste operations
 *
 * Author:
 *  Miguel de Icaza (miguel@gnu.org)
 *
 */
#include <config.h>
#include <gnome.h>
#include <locale.h>
#include <string.h>
#include "gnumeric.h"
#include "gnumeric-util.h"
#include "clipboard.h"
#include "eval.h"
#include "render-ascii.h"

/*
 * Callback information.
 *
 * Passed trough the workbook structure.
 */
typedef struct {
	Sheet      *dest_sheet;
	CellRegion *region;
	int         dest_col, dest_row;
	int         paste_flags;
} clipboard_paste_closure_t;

/*
 * Pastes a cell in the spreadsheet
 */
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

static int
paste_cell_flags (Sheet *dest_sheet, int target_col, int target_row,
		  CellCopy *c_copy, int paste_flags)
{
	if (!(paste_flags & (PASTE_FORMULAS | PASTE_VALUES))){
		Cell *cell;
		
		cell = sheet_cell_get (dest_sheet,
				       target_col,
				       target_row);
		if (cell && c_copy->u.cell)
			cell_set_style (cell, c_copy->u.cell->style);
	} else {
		Cell *new_cell;
		
		if (c_copy->type == CELL_COPY_TYPE_CELL){
			new_cell = cell_copy (c_copy->u.cell);
			
			return paste_cell (
				dest_sheet, new_cell,
				target_col, target_row, paste_flags);
		} else {
			new_cell = sheet_cell_new (dest_sheet,
						   target_col, target_row);
			cell_set_text (new_cell, c_copy->u.text);
		}
	}

	return 0;
}

/*
 * do_clipboard_paste_cell_region:
 *
 * region:       a Cell Region that contains information to be pasted
 * dest_col:     initial column where cell pasting takes place
 * dest_row:     initial row where cell pasting takes place
 * paste_width:  boundary for pasting (in columns)
 * paste_height: boundar for pasting (in rows)
 * paste_flags:  controls what gets pasted (see clipboard.h for details
 */
static void
do_clipboard_paste_cell_region (CellRegion *region, Sheet *dest_sheet,
				int dest_col,       int dest_row,
				int paste_width,    int paste_height,
				int paste_flags)
{
	CellCopyList *l;
	GList *deps;
	int formulas = 0;
	int col, row;

	/* clear the region where we will paste */
	if (paste_flags & (PASTE_VALUES | PASTE_FORMULAS))
		sheet_clear_region (dest_sheet,
				    dest_col, dest_row,
				    dest_col + paste_width - 1,
				    dest_row + paste_height - 1);

	/* If no operations are defined, we clear the area */
	if (!(paste_flags & PASTE_OPER_MASK))
		sheet_redraw_cell_region (dest_sheet,
					  dest_col, dest_row,
					  dest_col + paste_width - 1,
					  dest_row + paste_height - 1);
	
	/* Paste each element */
	for (col = 0; col < paste_width; col += region->cols){
		for (row = 0; row < paste_height; row += region->rows){
			for (l = region->list; l; l = l->next){
				CellCopy *c_copy = l->data;
				int target_col, target_row;
				
				target_col = col + dest_col + c_copy->col_offset;
				target_row = row + dest_row + c_copy->row_offset;

				if (target_col > dest_col + paste_width - 1)
					continue;

				if (target_row > dest_row + paste_height - 1)
					continue;

				formulas |= paste_cell_flags (
					dest_sheet, target_col, target_row,
					c_copy, paste_flags);
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

static GList *
new_node (GList *list, char *data, char *p, int col, int row)
{
	CellCopy *c_copy;
	char *text;

	text = g_malloc (p-data+1);
	text = strncpy (text, data, p-data);
	text [p-data] = 0;
	
	c_copy = g_new (CellCopy, 1);
	c_copy->type = CELL_COPY_TYPE_TEXT;
	c_copy->col_offset = col;
	c_copy->row_offset = row;
	c_copy->u.text = text;
	
	return g_list_prepend (list, c_copy);
}

/*
 * Creates a CellRegion based on the X selection
 *
 * We use \t, ; and "," as cell separators
 * \n is a line separator
 */
static CellRegion *
x_selection_to_cell_region (char *data, int len)
{
	CellRegion *cr;
	int cols = 1, cur_col = 0;
	int rows = 0;
	GList *list = NULL;
	char *p = data;

	for (;len >= 0; len--, p++){
		if (*p == '\t' || *p == ';' || *p == ',' || *p == '\n'){
			if (p != data)
				list = new_node (list, data, p, cur_col, rows);
			
			if (*p == '\n'){
				rows++;
				cur_col = 0;
			} else {
				if (cur_col > cols)
					cols = cur_col;
				cur_col++;
			}

			data = p+1;
		}
	}

	/* Handle the remainings */
	if (p != data)
		list = new_node (list, data, p, cur_col, rows);

	/* Return the CellRegion */
	cr = g_new (CellRegion, 1);	
	cr->list = list;
	cr->cols = cols ? cols : 1;
	cr->rows = rows + 1;

	return cr;
}

/*
 * x_selection_received
 *
 * Invoked when the selection has been received by our application.
 * This is triggered by a call we do to gtk_selection_convert.
 */
static void
x_selection_received (GtkWidget *widget, GtkSelectionData *sel, guint time, gpointer data)
{
	SheetSelection *ss;
	Workbook       *wb = data;
	clipboard_paste_closure_t *pc = wb->clipboard_paste_callback_data;
	CellRegion *content;
	int        paste_height, paste_width;
	int        end_col, end_row;

	ss = pc->dest_sheet->selections->data;
	
	/* Did X provide any selection? */
	if (sel->length < 0){
		content = pc->region;
		if (!content)
			return;
	} else
		content = x_selection_to_cell_region (sel->data, sel->length);

	/* Compute the bigger bounding box (selection u clipboard-region) */
	if (ss->end_col - ss->start_col + 1 > content->cols)
		paste_width = ss->end_col - ss->start_col + 1;
	else
		paste_width = content->cols;

	if (ss->end_row - ss->start_row + 1 > content->rows)
		paste_height = ss->end_row - ss->start_row + 1;
	else
		paste_height = content->rows;

	end_col = pc->dest_col + paste_width - 1;
	end_row = pc->dest_row + paste_height - 1;

	/* Do the actual paste operation */
	do_clipboard_paste_cell_region (
		content,      pc->dest_sheet,
		pc->dest_col, pc->dest_row,
		paste_width,  paste_height,
		pc->paste_flags);
	
	sheet_cursor_set (pc->dest_sheet,
			  pc->dest_col, pc->dest_row,
			  pc->dest_col, pc->dest_row,
			  end_col,      end_row);

	sheet_selection_reset_only (pc->dest_sheet);
	sheet_selection_append (pc->dest_sheet, pc->dest_col, pc->dest_row);
	sheet_selection_extend_to (pc->dest_sheet, end_col, end_row);

	/* Release the resources we used */
	if (sel->length >= 0)
		clipboard_release (content);

	/* Remove our used resources */
	g_free (wb->clipboard_paste_callback_data);
	wb->clipboard_paste_callback_data = NULL;
}

/*
 * x_selection_handler:
 *
 * Callback invoked when another application requests we render the selection.
 */
static void
x_selection_handler (GtkWidget *widget, GtkSelectionData *selection_data, guint info, guint time, gpointer data)
{
	Workbook *wb = (Workbook *) data;
	char *rendered_selection;
	
	g_assert (wb->clipboard_contents);

	rendered_selection = cell_region_render_ascii (wb->clipboard_contents);
	
	gtk_selection_data_set (
		selection_data, GDK_SELECTION_TYPE_STRING, 8,
		rendered_selection, strlen (rendered_selection));
}

/*
 * x_selection_clear:
 *
 * Callback for the "we lost the X selection" signal
 */
static gint
x_selection_clear (GtkWidget *widget, GdkEventSelection *event, Workbook *wb)
{
	wb->have_x_selection = FALSE;

	return TRUE;
}

/*
 * x_clipboard_bind_workbook
 *
 * Binds the signals related to the X selection to the Workbook
 */
void
x_clipboard_bind_workbook (Workbook *wb)
{
	wb->have_x_selection = FALSE;
	
	gtk_signal_connect (
		GTK_OBJECT (wb->toplevel), "selection_clear_event",
		GTK_SIGNAL_FUNC(x_selection_clear), wb);

	gtk_signal_connect (
		GTK_OBJECT (wb->toplevel), "selection_received",
		GTK_SIGNAL_FUNC(x_selection_received), wb);

	gtk_signal_connect (
		GTK_OBJECT (wb->toplevel), "selection_get",
		GTK_SIGNAL_FUNC(x_selection_handler), wb);

	gtk_selection_add_target (
		wb->toplevel,
		GDK_SELECTION_PRIMARY, GDK_SELECTION_TYPE_STRING, 0);
}

/*
 * clipboard_export_cell_region:
 *
 * This routine exports a CellRegion to the X selection
 */
static void
clipboard_export_cell_region (Workbook *wb)
{
	wb->have_x_selection = gtk_selection_owner_set (
		wb->toplevel,
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

	copy->type = CELL_COPY_TYPE_CELL;
	copy->u.cell = cell_copy (cell);
	copy->col_offset  = col - c->base_col;
	copy->row_offset  = row - c->base_row;
	
	/* Now clear the traces and dependencies on the copied Cell */
	copy->u.cell->col   = NULL;
	copy->u.cell->row   = NULL;
	copy->u.cell->sheet = NULL;
	
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

	clipboard_export_cell_region (sheet->workbook);
	
	return c.r;
}

/*
 * Main entry point for the paste code
 */
void
clipboard_paste_region (CellRegion *region, Sheet *dest_sheet,
			int dest_col,       int dest_row,
			int paste_flags,    guint32 time)
{
	clipboard_paste_closure_t *data;
	
	g_return_if_fail (dest_sheet != NULL);
	g_return_if_fail (IS_SHEET (dest_sheet));

	if (dest_sheet->workbook->clipboard_paste_callback_data){
		g_free (dest_sheet->workbook->clipboard_paste_callback_data);	
		dest_sheet->workbook->clipboard_paste_callback_data = NULL;
	}

	/*
	 * OK, we dont have the X selection, so we try to get it:
	 * we setup all of the parameters for the callback in
	 * case the X selection fails and we have to fallback to
	 * using our internal selection
	 */
	data = g_new (clipboard_paste_closure_t, 1);
	dest_sheet->workbook->clipboard_paste_callback_data = data;

	data->region       = region;
	data->dest_sheet   = dest_sheet;
	data->dest_col     = dest_col;
	data->dest_row     = dest_row;
	data->paste_flags  = paste_flags;

	/*
	 * If we own the selection, there is no need to ask X for
	 * the selection: we do the paste from our internal buffer.
	 *
	 * This is better as we have all sorts of useful information
	 * to paste from in this case (instead of the simplistic text
	 * we get from X
	 */
	if (dest_sheet->workbook->have_x_selection){
		GtkSelectionData sel;

		sel.length = -1;
		x_selection_received (dest_sheet->workbook->toplevel, &sel,
				      0, dest_sheet->workbook);
		return;
	}

	/* Now, trigger a grab of the X selection */
	gtk_selection_convert (
		dest_sheet->workbook->toplevel, GDK_SELECTION_PRIMARY,
		GDK_TARGET_STRING, time);
}

/*
 * Destroys the contents of a CellRegion
 */
void
clipboard_release (CellRegion *region)
{
	CellCopyList *l;

	g_return_if_fail (region != NULL);
	
	for (l = region->list; l; l = l->next){
		CellCopy *this_cell = l->data;
		
		if (this_cell->type == CELL_COPY_TYPE_CELL)
			cell_destroy (this_cell->u.cell);
		else
			g_free (this_cell->u.text);
		g_free (this_cell);
	}

	g_list_free (region->list);
	g_free (region);
}


