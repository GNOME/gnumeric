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
#include "cell-comment.h"
#include "command-context.h"
#include "commands.h"

#include "xml-io.h"
#include "value.h"

#include "dialog-stf.h"
#include "stf-parse.h"


/* The name of our clipboard atom and the 'magic' info number */
#define GNUMERIC_ATOM_NAME "GNUMERIC_CLIPBOARD_XML"
#define GNUMERIC_ATOM_INFO 2000

/* The name of the TARGETS atom (don't change unless you know what you are doing!) */
#define TARGETS_ATOM_NAME "TARGETS"

/**
 * paste_cell_flags: Pastes a cell in the spreadsheet
 *
 * @dest_sheet:  The sheet where the pasting will be done
 * @new_cell:    A new cell (not linked into the sheet, or wb->expr_list)
 * @target_col:  Column to put the cell into
 * @target_row:  Row to put the cell into.
 * @paste_flags: Bit mask that describes the paste options.
 */
static void
paste_cell_flags (Sheet *dest_sheet, int target_col, int target_row,
		  CellCopy *c_copy, int paste_flags)
{
	if ((paste_flags & (PASTE_FORMULAS | PASTE_VALUES))){
		if (c_copy->type == CELL_COPY_TYPE_CELL) {
			Cell *new_cell = cell_copy (c_copy->u.cell);

			/* Cell can not be linked in yet, but it needs an accurate location */
			new_cell->sheet	   = dest_sheet;
			new_cell->col_info = sheet_col_fetch (dest_sheet, target_col);
			new_cell->row_info = sheet_row_fetch (dest_sheet, target_row);

			if (cell_has_expr (new_cell)) {
				if (paste_flags & PASTE_FORMULAS) {
					cell_relocate (new_cell, TRUE, FALSE);
					/* FIXME : do this at a range level too */
					sheet_cell_changed (new_cell);
				} else
					cell_make_value (new_cell);
			} else {
				g_return_if_fail (new_cell->value != NULL);

				cell_render_value (new_cell);
			}

			sheet_cell_insert (dest_sheet, new_cell, target_col, target_row);
		} else {
			Cell *new_cell = sheet_cell_new (dest_sheet,
							 target_col, target_row);

			if (c_copy->u.text)
				sheet_cell_set_text (new_cell, c_copy->u.text);

			if (c_copy->type == CELL_COPY_TYPE_TEXT_AND_COMMENT && c_copy->comment)
				cell_set_comment (new_cell, c_copy->comment);
		}
	}
}

/**
 * clipboard_paste_region: Main entry point for the paste code.
 * @context : The context for error handling.
 * @pt : Where to paste the values.
 * @content : The CellRegion to paste.
 *
 * Pastes the supplied CellRegion (@content) into the supplied
 * PasteTarget (@pt).  This operation is not undoable.  It does not auto grow
 * the destination if the target is a singleton.  This is a simple interface to
 * paste a region.
 */
void
clipboard_paste_region (CommandContext *context,
			PasteTarget const *pt,
			CellRegion *content)
{
	int tmp;
	int repeat_horizontal, repeat_vertical;
	int dst_cols = pt->range.end.col - pt->range.start.col + 1;
	int dst_rows = pt->range.end.row - pt->range.start.row + 1;
	int src_cols = content->cols;
	int src_rows = content->rows;

	if (pt->paste_flags & PASTE_TRANSPOSE) {
		int tmp = src_cols;
		src_cols = src_rows;
		src_rows = tmp;
	}

	/* calculate the tiling */
	repeat_horizontal = dst_cols/src_cols;
	if (repeat_horizontal * src_cols != dst_cols) {
		char *msg = g_strdup_printf (
			_("destination does not have an even multiple of source columns (%d vs %d)\n\n"
			  "Try selecting a single cell or an area of the same shape and size."),
			dst_cols, src_cols);
		gnumeric_error_invalid (context, _("Unable to paste"), msg);
		g_free (msg);
		return;
	}

	repeat_vertical = dst_rows/src_rows;
	if (repeat_vertical * src_rows != dst_rows) {
		char *msg = g_strdup_printf (
			_("destination does not have an even multiple of source rows (%d vs %d)\n\n"
			  "Try selecting a single cell or an area of the same shape and size."),
			dst_rows, src_rows);
		gnumeric_error_invalid (context, _("Unable to paste"), msg);
		g_free (msg);
		return;
	}

	if ((pt->range.start.col + dst_cols) >= SHEET_MAX_COLS ||
	    (pt->range.start.row + dst_rows) >= SHEET_MAX_ROWS) {
		gnumeric_error_invalid (context, _("Unable to paste"), 
					_("result passes the sheet boundary"));
		return;
	}

	tmp = 0;
	/* clear the region where we will paste */
	if (pt->paste_flags & (PASTE_VALUES|PASTE_FORMULAS))
		tmp = CLEAR_VALUES|CLEAR_COMMENTS;
	if (pt->paste_flags & PASTE_FORMATS)
		tmp |= CLEAR_FORMATS;
	if (tmp) {
		int const dst_col = pt->range.start.col;
		int const dst_row = pt->range.start.row;
		sheet_clear_region (context, pt->sheet,
				    dst_col, dst_row,
				    dst_col + dst_cols - 1,
				    dst_row + dst_rows - 1,
				    tmp);
	}

	for (tmp = repeat_vertical; repeat_horizontal-- > 0 ; repeat_vertical = tmp)
		while (repeat_vertical-- > 0) {
			int const left = repeat_horizontal * src_cols + pt->range.start.col;
			int const top = repeat_vertical * src_rows + pt->range.start.row;
			CellCopyList *l;

			/* Move the styles on here so we get correct formats before recalc */
			if (pt->paste_flags & PASTE_FORMATS) {
				Range boundary = pt->range;

				boundary.start.col = left;
				boundary.start.row = top;
				boundary.end.col   = left + src_cols;
				boundary.end.row   = top + src_rows;
				sheet_style_attach_list (pt->sheet, content->styles, &boundary.start,
							 (pt->paste_flags & PASTE_TRANSPOSE));

				sheet_style_optimize (pt->sheet, boundary);
			}

			for (l = content->list; l; l = l->next) {
				CellCopy *c_copy = l->data;
				int target_col = left;
				int target_row = top;

				if (pt->paste_flags & PASTE_TRANSPOSE) {
					target_col += c_copy->row_offset;
					target_row += c_copy->col_offset;
				} else {
					target_col += c_copy->col_offset;
					target_row += c_copy->row_offset;
				}

				paste_cell_flags (pt->sheet, target_col, target_row,
						  c_copy, pt->paste_flags);
			}
		}

        if (pt->paste_flags & (PASTE_FORMULAS|PASTE_VALUES)) {
		GList *deps = sheet_region_get_deps (pt->sheet,
						     pt->range.start.col,
						     pt->range.start.row,
						     pt->range.end.col,
						     pt->range.end.row);
		if (deps)
			eval_queue_list (deps, TRUE);
	}
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
 * clipboard_copy_range:
 *
 * Entry point to the clipboard copy code
 */
CellRegion *
clipboard_copy_range (Sheet *sheet, Range const *r)
{
	append_cell_closure_t c;

	g_return_val_if_fail (sheet != NULL, NULL);
	g_return_val_if_fail (IS_SHEET (sheet), NULL);
	g_return_val_if_fail (r->start.col <= r->end.col, NULL);
	g_return_val_if_fail (r->start.row <= r->end.row, NULL);

	c.r = g_new0 (CellRegion, 1);

	c.base_col = r->start.col;
	c.base_row = r->start.row;
	c.r->cols = r->end.col - r->start.col + 1;
	c.r->rows = r->end.row - r->start.row + 1;

	sheet_cell_foreach_range ( sheet, TRUE,
		r->start.col, r->start.row,
		r->end.col, r->end.row,
		clipboard_prepend_cell, &c);

	c.r->styles = sheet_get_styles_in_range (sheet, r);

	/* reverse the list so that upper left corner is first */
	c.r->list = g_list_reverse (c.r->list);

	return c.r;
}

/**
 * clipboard_paste :
 * @content:     a Cell Region that contains information to be pasted
 * @time:        Time at which the event happened.
 */
void
clipboard_paste (CommandContext *context, PasteTarget const *pt, guint32 time)
{
	PasteTarget *new_pt;
	CellRegion *content;

	g_return_if_fail (pt != NULL);
	g_return_if_fail (pt->sheet != NULL);
	g_return_if_fail (IS_SHEET (pt->sheet));

	content = application_clipboard_contents_get ();

	/*
	 * If we own the selection, there is no need to ask X for
	 * the selection: we do the paste from our internal buffer.
	 *
	 * This is better as we have all sorts of useful information
	 * to paste from in this case (instead of the simplistic text
	 * we get from X)
	 */
	if (content) {
		cmd_paste_copy (context, pt, content);
		return;
	}

	/*
	 * OK, we dont have the X selection, so we try to get it:
	 * Now, trigger a grab of the X selection.
	 * This will callback x_selection_received
	 */
	if (pt->sheet->workbook->clipboard_paste_callback_data != NULL)
		g_free (pt->sheet->workbook->clipboard_paste_callback_data);

	new_pt = g_new (PasteTarget, 1);
	*new_pt = *pt;
	pt->sheet->workbook->clipboard_paste_callback_data = new_pt;

	/* Query the formats */
	gtk_selection_convert (pt->sheet->workbook->toplevel, GDK_SELECTION_PRIMARY,
			       gdk_atom_intern (TARGETS_ATOM_NAME, FALSE), time);
}

/*
 * Destroys the contents of a CellRegion
 */
void
clipboard_release (CellRegion *content)
{
	CellCopyList *l;

	g_return_if_fail (content != NULL);

	for (l = content->list; l; l = l->next){
		CellCopy *this_cell = l->data;

		if (this_cell->type == CELL_COPY_TYPE_CELL) {
			/* The cell is not really in the rows or columns */
			this_cell->u.cell->sheet = NULL;
			this_cell->u.cell->row_info = NULL;
			this_cell->u.cell->col_info = NULL;
			cell_destroy (this_cell->u.cell);
		} else {

			if (this_cell->type == CELL_COPY_TYPE_TEXT_AND_COMMENT)
				if (this_cell->comment != NULL)
					g_free (this_cell->comment);

			if (this_cell->u.text)
				g_free (this_cell->u.text);
		}

		g_free (this_cell);
	}
	if (content->styles != NULL) {
		sheet_style_list_destroy (content->styles);
		content->styles = NULL;
	}

	g_list_free (content->list);
	content->list = NULL;

	g_free (content);
}

PasteTarget*
paste_target_init (PasteTarget *pt, Sheet *sheet, Range const *r, int flags)
{
	pt->sheet = sheet;
	pt->range = *r;
	pt->paste_flags = flags;
	return pt;
}

static CellRegion *
x_selection_to_cell_region (CommandContext *context, char const * data,
			    int len)
{
	DialogStfResult_t *dialogresult;
	CellRegion *cr = NULL;
	CellRegion *crerr;

	crerr         = g_new (CellRegion, 1);
	crerr->list   = NULL;
	crerr->cols   = -1;
	crerr->rows   = -1;
	crerr->styles = NULL;

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

	dialogresult = stf_dialog (context, "clipboard", data);

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
			StyleRegion *content = g_new (StyleRegion, 1);
			MStyle *style = mstyle_new ();
			Range range;

			mstyle_set_format (style, iterator->data);

			range.start.col = col;
			range.start.row = 0;
			range.end.col   = col;
			range.end.row   = rowcount;

			content->style = style;
			content->range  = range;

			cr->styles = g_list_prepend (cr->styles, content);

			iterator = g_slist_next (iterator);

			col++;
		}

		stf_dialog_result_free (dialogresult);
	}
	else {

		return crerr;
	}

	g_free (crerr);

	return cr;
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
	Workbook       *wb = data;
	Sheet	       *sheet = NULL;
	CommandContext *context = workbook_command_context_gui (wb);
	GdkAtom atom_targets  = gdk_atom_intern (TARGETS_ATOM_NAME, FALSE);
	GdkAtom atom_gnumeric = gdk_atom_intern (GNUMERIC_ATOM_NAME, FALSE);
	PasteTarget *pt = wb->clipboard_paste_callback_data;
	CellRegion *content = NULL;
	gboolean region_pastable = FALSE;
	gboolean free_closure = FALSE;

	if (sel->target == atom_targets) { /* The data is a list of atoms */
		GdkAtom *atoms = (GdkAtom *) sel->data;
		gboolean gnumeric_format;
		int atom_count = (sel->length / sizeof (GdkAtom));
		int i;

		/* Nothing on clipboard? */
		if (sel->length < 0) {

			if (wb->clipboard_paste_callback_data != NULL) {
				g_free (wb->clipboard_paste_callback_data);
				wb->clipboard_paste_callback_data = NULL;
			}
			return;
		}

		/*
		 * Iterate over the atoms and try to find the gnumeric atom
		 */
		gnumeric_format = FALSE;
		for (i = 0; i < atom_count; i++) {

			if (atoms[i] == atom_gnumeric) {

				/* Hooya! other app == gnumeric */
				gnumeric_format = TRUE;

				break;
			}
		}

		/* NOTE : We don't release the date resources
		 * (wb->clipboard_paste_callback_data), the
		 * reason for this is that we will actually call ourself
		 * again (indirectly trough the gtk_selection_convert
		 * and that call _will_ free the data (and also needs it).
		 * So we won't release anything.
		 */

		/* If another instance of gnumeric put this data on the clipboard
		 * request the data in gnumeric XML format. If not, just
		 * request it in string format
		 */
		if (gnumeric_format)
			gtk_selection_convert (wb->toplevel, GDK_SELECTION_PRIMARY,
					       atom_gnumeric, time);
		else
			gtk_selection_convert (wb->toplevel, GDK_SELECTION_PRIMARY,
					       GDK_SELECTION_TYPE_STRING, time);

	} else if (sel->target == atom_gnumeric) { /* The data is the gnumeric specific XML interchange format */

		if (gnumeric_xml_read_selection_clipboard (context, &content, sel->data) == 0)
			region_pastable = TRUE;

	} else {  /* The data is probably in String format */
		region_pastable = TRUE;

		/* Did X provide any selection? */
		if (sel->length < 0) {
			region_pastable = FALSE;
			free_closure = TRUE;
		} else
			content = x_selection_to_cell_region (context,
							      sel->data, sel->length);
	}

	if (region_pastable) {
		/*
		 * if the conversion from the X selection -> a cellregion
		 * was canceled this may have content sized -1,-1
		 */
		if (content->cols > 0 && content->rows > 0)
			cmd_paste_copy (context, pt, content);

		/* Release the resources we used */
		if (sel->length >= 0)
			clipboard_release (content);
	}

	if (region_pastable || free_closure) {
		/* Remove our used resources */
		if (wb->clipboard_paste_callback_data != NULL) {
			g_free (wb->clipboard_paste_callback_data);
			wb->clipboard_paste_callback_data = NULL;
		}
	}

	/* Be careful to release memory BEFORE update and recalc.
	 * this will be more friendly in low memory environments
	 */
	if (region_pastable) {
		workbook_recalc (wb);
		sheet_update (sheet);
	}
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
	GdkAtom atom_gnumeric = gdk_atom_intern (GNUMERIC_ATOM_NAME, FALSE);
	Sheet *sheet = application_clipboard_sheet_get ();
	Range const *a = application_clipboard_area_get ();

	/*
	 * Not sure how to handle this, not sure what purpose this has has
	 * (sheet being NULL). I think it is here to indicate that the selection
	 * just has been cut.
	 */
	if (!sheet)
		return;

	/*
	 * If the content was marked for a cut we need to copy it for pasting
	 * we clear it later on, because if the other application (the one that
	 * requested we render the data) is another instance of gnumeric
	 * we need the selection to remain "intact" (not cleared) so we can
	 * render it to the Gnumeric XML clipboard format
	 */
	if (clipboard == NULL) {

		g_return_if_fail (sheet != NULL);
		g_return_if_fail (a != NULL);

		content_needs_free = TRUE;
		clipboard = clipboard_copy_range (sheet, a);
	}

	g_return_if_fail (clipboard != NULL);

	/*
	 * Check weather the other application wants gnumeric XML format
	 * in fact we only have to check the 'info' variable, however
	 * to be absolutely sure I check if the atom checks out too
	 */
	if (selection_data->target == atom_gnumeric && info == 2000) {
		CommandContext *context = workbook_command_context_gui (sheet->workbook);
		xmlChar *buffer;
		int buffer_size;

		gnumeric_xml_write_selection_clipboard (context, sheet, &buffer, &buffer_size);

		gtk_selection_data_set (selection_data, GDK_SELECTION_TYPE_STRING, 8,
					(char *) buffer, buffer_size);

		g_free (buffer);
	} else {
		char *rendered_selection = cell_region_render_ascii (clipboard);

		gtk_selection_data_set (selection_data, GDK_SELECTION_TYPE_STRING, 8,
					rendered_selection, strlen (rendered_selection));

		g_free (rendered_selection);
	}

	/*
	 * If this was a CUT operation we need to clear the content that was pasted
	 * into another application and release the stuff on the clipboard
	 */
	if (content_needs_free) {

		sheet_clear_region (workbook_command_context_gui (sheet->workbook),
				    sheet,
				    a->start.col, a->start.row,
				    a->end.col,   a->end.row,
				    CLEAR_VALUES|CLEAR_COMMENTS);

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
	GtkTargetEntry targets;

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

	gtk_signal_connect (
		GTK_OBJECT (wb->toplevel), "selection_get",
		GTK_SIGNAL_FUNC (x_selection_handler), NULL);

	gtk_selection_add_target (
		wb->toplevel,
		GDK_SELECTION_PRIMARY, GDK_SELECTION_TYPE_STRING, 0);

	/*
	 * Our specific Gnumeric XML clipboard interchange type
	 */
	targets.target = GNUMERIC_ATOM_NAME;

	/* This is not useful, but we have to set it to something: */
	targets.flags  = GTK_TARGET_SAME_WIDGET;
	targets.info   = GNUMERIC_ATOM_INFO;

	gtk_selection_add_targets (wb->toplevel,
				   GDK_SELECTION_PRIMARY,
				   &targets, 1);
}

