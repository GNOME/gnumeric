/*
 * Clipboard.c: Implements the copy/paste operations
 *
 * Author:
 *  Miguel de Icaza (miguel@gnu.org)
 */
#include <config.h>
#include <gnome.h>
#include <locale.h>
#include <string.h>
#include <ctype.h>
#include "gnumeric.h"
#include "gnumeric-util.h"
#include "clipboard.h"
#include "eval.h"
#include "selection.h"
#include "application.h"
#include "render-ascii.h"
#include "workbook.h"
#include "workbook-view.h"
#include "ranges.h"

#include "dialog-stf.h"
#include "stf-parse.h"

/*
 * Callback information.
 *
 * Passed through the workbook structure.
 */
typedef struct {
	Sheet      *dest_sheet;
	CellRegion *region;
	int         dest_col, dest_row;
	int         paste_flags;
} clipboard_paste_closure_t;

/**
 * paste_cell:
 * @dest_sheet:  The sheet where the pasting will be done
 * @new_cell:    The cell to paste.
 * @target_col:  Column to put the cell into
 * @target_row:  Row to put the cell into.
 * @paste_flags: Bit mask that describes the paste options.
 *
 * Pastes a cell in the spreadsheet
 */
static void
paste_cell (Sheet *dest_sheet, Cell *new_cell,
	    int target_col, int target_row,
	    int paste_flags)
{
	g_return_if_fail (target_col < SHEET_MAX_COLS);
	g_return_if_fail (target_row < SHEET_MAX_ROWS);

	sheet_cell_add (dest_sheet, new_cell, target_col, target_row);

	if (!(paste_flags & PASTE_FORMULAS)) {
		if (new_cell->parsed_node) {
			expr_tree_unref (new_cell->parsed_node);
			new_cell->parsed_node = NULL;
		}
	}

	if (new_cell->parsed_node) {
		if (paste_flags & PASTE_FORMULAS) {
			cell_relocate (new_cell, TRUE);
			cell_content_changed (new_cell);
		} else
			cell_make_value (new_cell);
	}

	if (new_cell->value)
		cell_render_value (new_cell);

	sheet_redraw_cell_region (dest_sheet,
				  target_col, target_row,
				  target_col, target_row);
}

static void
paste_cell_flags (Sheet *dest_sheet, int target_col, int target_row,
		  CellCopy *c_copy, int paste_flags)
{
	if (!(paste_flags & (PASTE_FORMULAS | PASTE_VALUES))){
		Range r;

		r.start.col = target_col;
		r.start.row = target_row;
		r.end.col   = target_col;
		r.end.row   = target_row;
	} else {
		Cell *new_cell;

		if (c_copy->type != CELL_COPY_TYPE_TEXT) {
			Range r;

			new_cell = cell_copy (c_copy->u.cell);

			r.start.col = target_col;
			r.start.row = target_row;
			r.end.col   = target_col;
			r.end.row   = target_row;

			paste_cell (dest_sheet, new_cell,
				    target_col, target_row, paste_flags);
		} else {
			new_cell = sheet_cell_new (dest_sheet,
						   target_col, target_row);
			cell_set_text (new_cell, c_copy->u.text);
		}
	}
}

/**
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
do_clipboard_paste_cell_region (CommandContext *context,
				CellRegion *region, Sheet *dest_sheet,
				int dest_col,       int dest_row,
				int paste_width,    int paste_height,
				int paste_flags)
{
	CellCopyList *l;
	GList        *deps;
	int col, row, col_inc, row_inc;

	/* clear the region where we will paste */
	if (paste_flags & (PASTE_VALUES | PASTE_FORMULAS))
		sheet_clear_region (context, dest_sheet,
				    dest_col, dest_row,
				    dest_col + paste_width - 1,
				    dest_row + paste_height - 1,
				    CLEAR_VALUES|CLEAR_COMMENTS);

	/* If no operations are defined, we clear the area */
	if (!(paste_flags & PASTE_OPER_MASK))
		sheet_redraw_cell_region (dest_sheet,
					  dest_col, dest_row,
					  dest_col + paste_width - 1,
					  dest_row + paste_height - 1);

	/* Paste each element */
	if (paste_flags & PASTE_TRANSPOSE) {
		col_inc = region->rows;
		row_inc = region->cols;
	} else {
		col_inc = region->cols;
		row_inc = region->rows;
	}

	for (col = 0; col < paste_width; col += col_inc) {
		for (row = 0; row < paste_height; row += row_inc) {
			for (l = region->list; l; l = l->next) {
				CellCopy *c_copy = l->data;
				int target_col, target_row;

				if (paste_flags & PASTE_TRANSPOSE) {
					target_col = col + dest_col + c_copy->row_offset;
					target_row = row + dest_row + c_copy->col_offset;
				} else {
					target_col = col + dest_col + c_copy->col_offset;
					target_row = row + dest_row + c_copy->row_offset;
				}

				if (target_col > dest_col + paste_width - 1)
					continue;

				if (target_row > dest_row + paste_height - 1)
					continue;

				paste_cell_flags (dest_sheet, target_col, target_row,
						  c_copy, paste_flags);
			}
		}
	}

	deps = sheet_region_get_deps (dest_sheet,
				      dest_col, dest_row,
				      dest_col + paste_width - 1,
				      dest_row + paste_height - 1);

	if (deps)
		cell_queue_recalc_list (deps, TRUE);
}

static CellRegion *
x_selection_to_cell_region (char const * data, int len)
{
	DialogStfResult_t *dialogresult;
	CellRegion *cr = NULL;
	CellRegion *crerr;
	
	crerr         = g_new (CellRegion, 1);
	crerr->list   = NULL;
	crerr->cols   = -1;
	crerr->rows   = 0;
	crerr->styles = NULL;
	
	/* End of FIXME */
	
	if (!stf_parse_convert_to_unix (data)) {
	
		g_free ( (char*) data);	
		g_warning (_("Error while trying to pre-convert clipboard data"));
		return crerr;
	}

	if (!stf_parse_is_valid_data (data)) {

		g_free ( (char*) data);
		g_warning (_("This data on the clipboard does not seem to be valid text"));
		return crerr;
	}

	dialogresult = dialog_stf (NULL, "clipboard", data);

	if (dialogresult != NULL) {
		GSList *iterator;
		int col, rowcount;
	
		cr = stf_parse_region (dialogresult->parseoptions, dialogresult->newstart);
		
		if (cr == NULL) {

			g_free ( (char*) data);
			g_warning (_("Parse error while trying to parse data into cellregion"));
			return crerr;
		}

		iterator = dialogresult->formats;
		col = 0;
		rowcount = stf_parse_get_rowcount (dialogresult->parseoptions, dialogresult->newstart);
		while (iterator) {
			StyleRegion *region = g_new (StyleRegion, 1);
			MStyle *style = mstyle_new ();
			Range range;

			mstyle_set_format (style, iterator->data);

			range.start.col = col;
			range.start.row = 0;
			range.end.col   = col;
			range.end.row   = rowcount;
		
			region->style = style;
			region->range  = range;

			/* FIXME : I Wonder who actually frees these StyleRegions, I am not
			 * sure if this is done automatically... (I think it should though)
			 * my observation is that neither sheet_paste_selection nor sheet_style_attach_list
			 * frees these structs...
			 * IS THIS A MEMORY LEAK?
			 */
			cr->styles = g_list_prepend (cr->styles, region);
			
			iterator = g_slist_next (iterator);

			col++;
		}

		dialog_stf_result_free (dialogresult);
	}
	else {
	
		return crerr;
	}

	g_free (crerr);
	
	return cr;
}

/**
 * sheet_paste_selection:
 *
 */
static void
sheet_paste_selection (CommandContext *context, Sheet *sheet,
		       CellRegion *content, SheetSelection *ss,
		       clipboard_paste_closure_t *pc)
{
	int        paste_height, paste_width;
	int        end_col, end_row;

	/* If 'cols' is set to -1 then there is _nothing_ to paste */
	if (content->cols == -1)
		return;
	
	/* Compute the bigger bounding box (selection u clipboard-region) */
	if (ss->user.end.col - ss->user.start.col + 1 > content->cols)
		paste_width = ss->user.end.col - ss->user.start.col + 1;
	else
		paste_width = content->cols;

	if (ss->user.end.row - ss->user.start.row + 1 > content->rows)
		paste_height = ss->user.end.row - ss->user.start.row + 1;
	else
		paste_height = content->rows;

	if (pc->dest_col + paste_width > SHEET_MAX_COLS)
		paste_width = SHEET_MAX_COLS - pc->dest_col;
	if (pc->dest_row + paste_height > SHEET_MAX_ROWS)
		paste_height = SHEET_MAX_ROWS - pc->dest_row;

	if (pc->paste_flags & PASTE_TRANSPOSE) {
		int t;

		end_col = pc->dest_col + paste_height - 1;
		end_row = pc->dest_row + paste_width - 1;

		/* Swap the paste dimensions for transposing */
		t = paste_height;
		paste_height = paste_width;
		paste_width = t;
	} else {
		end_col = pc->dest_col + paste_width - 1;
		end_row = pc->dest_row + paste_height - 1;
	}

	/* Move the styles on here so we get correct formats before recalc */
	if (pc->paste_flags & PASTE_FORMATS) {
		Range boundary;

		boundary.start.col = pc->dest_col;
		boundary.end.col   = pc->dest_col + paste_width;
		boundary.start.row = pc->dest_row;
		boundary.end.row   = pc->dest_row + paste_height;
		sheet_style_attach_list (sheet, content->styles, &boundary.start,
					 (pc->paste_flags & PASTE_TRANSPOSE));

		sheet_style_optimize (sheet, boundary);
	}

	/* Do the actual paste operation */
	do_clipboard_paste_cell_region (context,
		content,      sheet,
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
}

/**
 * x_selection_received:
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

	ss = pc->dest_sheet->selections->data;

	/* Did X provide any selection? */
	if (sel->length < 0){
		content = pc->region;
		if (!content)
			return;
	} else
		content = x_selection_to_cell_region (sel->data, sel->length);

	sheet_paste_selection (workbook_command_context_gui (wb),
			       pc->dest_sheet, content, ss, pc);

	/* Release the resources we used */
	if (sel->length >= 0)
		clipboard_release (content);

	/* Remove our used resources */
	if (wb->clipboard_paste_callback_data != NULL) {
		g_free (wb->clipboard_paste_callback_data);
		wb->clipboard_paste_callback_data = NULL;
	}

	workbook_recalc (wb);
}

/**
 * x_selection_handler:
 *
 * Callback invoked when another application requests we render the selection.
 */
static void
x_selection_handler (GtkWidget *widget, GtkSelectionData *selection_data, guint info, guint time, gpointer data)
{
	gboolean content_needs_free = FALSE;
	CellRegion *clipboard = application_clipboard_contents_get ();
	char *rendered_selection;

	/* If the region was marked for a cut we need to copy it for pasting
	 * then clear it
	 */
	if (clipboard == NULL) {
		Sheet *sheet = application_clipboard_sheet_get ();
		Range const *a = application_clipboard_area_get ();

		g_return_if_fail (sheet != NULL);
		g_return_if_fail (a != NULL);

		content_needs_free = TRUE;
		clipboard =
		    clipboard_copy_cell_range (sheet,
					       a->start.col, a->start.row,
					       a->end.col,   a->end.row);

		/* Clear the region that was pasted into another application */
		sheet_clear_region (workbook_command_context_gui (sheet->workbook),
				    sheet,
				    a->start.col, a->start.row,
				    a->end.col,   a->end.row,
				    CLEAR_VALUES|CLEAR_COMMENTS);
	}

	g_return_if_fail (clipboard != NULL);

	rendered_selection = cell_region_render_ascii (clipboard);

	gtk_selection_data_set (
		selection_data, GDK_SELECTION_TYPE_STRING, 8,
		rendered_selection, strlen (rendered_selection));
	g_free (rendered_selection);

	if (content_needs_free) {
		clipboard_release (clipboard);
		application_clipboard_clear ();
	}
}

/**
 * x_selection_clear:
 *
 * Callback for the "we lost the X selection" signal
 */
static gint
x_selection_clear (GtkWidget *widget, GdkEventSelection *event, Workbook *wb)
{
	application_clipboard_clear ();

	return TRUE;
}

/**
 * x_clipboard_bind_workbook:
 *
 * Binds the signals related to the X selection to the Workbook
 * and initialized the clipboard data structures for the Workbook.
 */
void
x_clipboard_bind_workbook (Workbook *wb)
{
	wb->clipboard_paste_callback_data = NULL;

	gtk_signal_connect (
		GTK_OBJECT (wb->toplevel), "selection_clear_event",
		GTK_SIGNAL_FUNC (x_selection_clear), wb);

	gtk_signal_connect (
		GTK_OBJECT (wb->toplevel), "selection_received",
		GTK_SIGNAL_FUNC (x_selection_received), wb);

	gtk_signal_connect (
		GTK_OBJECT (wb->toplevel), "selection_get",
		GTK_SIGNAL_FUNC (x_selection_handler), NULL);

	gtk_selection_add_target (
		wb->toplevel,
		GDK_SELECTION_PRIMARY, GDK_SELECTION_TYPE_STRING, 0);
}

typedef struct {
	int        base_col, base_row;
	CellRegion *r;
} append_cell_closure_t;

static Value *
clipboard_prepend_cell (Sheet *sheet, int col, int row, Cell *cell, void *user_data)
{
	append_cell_closure_t *c = user_data;
	CellCopy *copy;

	copy = g_new (CellCopy, 1);
	copy->type       = CELL_COPY_TYPE_CELL;
	copy->u.cell     = cell_copy (cell);
	copy->col_offset = col - c->base_col;
	copy->row_offset = row - c->base_row;

	c->r->list = g_list_prepend (c->r->list, copy);

	return NULL;
}

/**
 * clipboard_copy_cell_range:
 *
 * Entry point to the clipboard copy code
 */
CellRegion *
clipboard_copy_cell_range (Sheet *sheet,
			   int start_col, int start_row,
			   int end_col, int end_row)
{
	append_cell_closure_t c;
	Range                 r;

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
		sheet, TRUE, start_col, start_row, end_col, end_row,
		clipboard_prepend_cell, &c);

	range_init (&r, start_col, start_row, end_col, end_row);
	c.r->styles = sheet_get_styles_in_range (sheet, &r);

	/* reverse the list so that upper left corner is first */
	c.r->list = g_list_reverse (c.r->list);

	return c.r;
}

/**
 * clipboard_paste_region:
 * @region:      A cell region
 * @dest_sheet:  Destination sheet
 * @dest_col:    Column where we should paste the region in dest_sheet
 * @dest_row:    Row where we should paste the region in dest_sheet
 * @paste_flags: Paste flags
 * @time:        Time at which the event happened.
 *
 * Main entry point for the paste code
 */
void
clipboard_paste_region (CommandContext *context,
			CellRegion *region, Sheet *dest_sheet,
			int dest_col,       int dest_row,
			int paste_flags,    guint32 time)
{
	clipboard_paste_closure_t *data;

	g_return_if_fail (dest_sheet != NULL);
	g_return_if_fail (IS_SHEET (dest_sheet));

	data = g_new (clipboard_paste_closure_t, 1);
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
	if (region) {
		sheet_paste_selection (context, dest_sheet,
				       region, dest_sheet->selections->data, data);
		g_free (data);
		return;
	}

	/*
	 * OK, we dont have the X selection, so we try to get it:
	 *
	 * Now, trigger a grab of the X selection.
	 *
	 * This will callback x_selection_received
	 */
	if (dest_sheet->workbook->clipboard_paste_callback_data != NULL)
		g_free (dest_sheet->workbook->clipboard_paste_callback_data);
	dest_sheet->workbook->clipboard_paste_callback_data = data;
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

		if (this_cell->type != CELL_COPY_TYPE_TEXT) {
			/* The cell is not really in the rows or columns */
			this_cell->u.cell->sheet = NULL;
			this_cell->u.cell->row = NULL;
			this_cell->u.cell->col = NULL;
			cell_destroy (this_cell->u.cell);
		} else
			g_free (this_cell->u.text);
		g_free (this_cell);
	}
	if (region->styles != NULL) {
		sheet_style_list_destroy (region->styles);
		region->styles = NULL;
	}

	g_list_free (region->list);
	region->list = NULL;

	g_free (region);
}

