/*
 * Sheet.c:  Implements the sheet management and per-sheet storage
 *
 * Author:
 *  Miguel de Icaza (miguel@gnu.org)
 *
 */
#include <config.h>
#include <ctype.h>
#include <gnome.h>
#include <string.h>
#include "gnumeric.h"
#include "workbook.h"
#include "workbook-edit.h"
#include "gnumeric-sheet.h"
#include "parse-util.h"
#include "gnumeric-util.h"
#include "eval.h"
#include "number-match.h"
#include "format.h"
#include "clipboard.h"
#include "selection.h"
#include "ranges.h"
#include "print-info.h"
#include "mstyle.h"
#include "application.h"
#include "command-context.h"
#include "commands.h"
#include "cellspan.h"
#include "cell-comment.h"
#include "sheet-private.h"
#include "expr-name.h"
#include "rendered-value.h"

#ifdef ENABLE_BONOBO
#    include "sheet-vector.h"
#    include <libgnorba/gnorba.h>
#endif

#undef DEBUG_CELL_FORMULA_LIST

#define GNUMERIC_SHEET_VIEW(p) GNUMERIC_SHEET (SHEET_VIEW(p)->sheet_view);

static void sheet_update_zoom_controls (Sheet *sheet);
static void sheet_redraw_partial_row (Sheet const *sheet, int const row,
				      int const start_col, int const end_col);

void
sheet_adjust_preferences (Sheet const *sheet)
{
	GList *l;

	for (l = sheet->sheet_views; l; l = l->next){
		SheetView *sheet_view = l->data;

		sheet_view_adjust_preferences (sheet_view);
	}
}

void
sheet_redraw_all (Sheet const *sheet)
{
	GList *l;

	for (l = sheet->sheet_views; l; l = l->next){
		SheetView *sheet_view = l->data;

		sheet_view_redraw_all (sheet_view);
	}
}

void
sheet_redraw_headers (Sheet const *sheet,
		      gboolean const col, gboolean const row,
		      Range const * r /* optional == NULL */)
{
	GList *l;

	for (l = sheet->sheet_views; l; l = l->next){
		SheetView *sheet_view = l->data;

		sheet_view_redraw_headers (sheet_view, col, row, r);
	}
}

static guint
cell_hash (gconstpointer key)
{
	Cell const *cell = key;

	return (cell->pos.row << 8) | cell->pos.col;
}

static gint
cell_compare (Cell const * a, Cell const * b)
{
	return (a->pos.row == b->pos.row && a->pos.col == b->pos.col);
}

void
sheet_rename (Sheet *sheet, const char *new_name)
{
	g_return_if_fail (sheet != NULL);
	g_return_if_fail (IS_SHEET (sheet));
	g_return_if_fail (new_name != NULL);

	g_free (sheet->name_quoted);
	g_free (sheet->name_unquoted);
	sheet->name_unquoted = g_strdup (new_name);
	sheet->name_quoted = sheet_name_quote (new_name);
}

SheetView *
sheet_new_sheet_view (Sheet *sheet)
{
	GtkWidget *sheet_view;
	
	g_return_val_if_fail (sheet != NULL, NULL);
	g_return_val_if_fail (IS_SHEET (sheet), NULL);

	sheet_view = sheet_view_new (sheet);
	gtk_object_ref (GTK_OBJECT (sheet_view));

	sheet->sheet_views = g_list_prepend (sheet->sheet_views, sheet_view);

	return SHEET_VIEW (sheet_view);
}

void
sheet_destroy_sheet_view (Sheet *sheet, SheetView *sheet_view)
{
	g_return_if_fail (sheet != NULL);
	g_return_if_fail (IS_SHEET (sheet));
	g_return_if_fail (sheet_view != NULL);
	g_return_if_fail (IS_SHEET_VIEW (sheet_view));
	
	sheet->sheet_views = g_list_remove (sheet->sheet_views, sheet_view);
	gtk_object_unref (GTK_OBJECT (sheet_view));
}

static void
sheet_init_default_styles (Sheet *sheet)
{
	/* measurements are in pts including the margins and far grid line.  */
	sheet_col_set_default_size_pts (sheet, 48);
	sheet_row_set_default_size_pts (sheet, 12.75, FALSE, FALSE);
}

/*
 * sheet_new
 * @wb              Workbook
 * @name            Unquoted name
 */
Sheet *
sheet_new (Workbook *wb, const char *name)
{
	GtkWidget *sheet_view;
	Sheet  *sheet;
	MStyle *mstyle;

	g_return_val_if_fail (wb != NULL, NULL);
	g_return_val_if_fail (name != NULL, NULL);

	sheet = g_new0 (Sheet, 1);
	sheet->priv = g_new0 (SheetPrivate, 1);
#ifdef ENABLE_BONOBO
	sheet->priv->corba_server = NULL;
	sheet->priv->sheet_vectors = NULL;
#endif

	/* Init, focus, and load handle setting these if/when necessary */
	sheet->priv->edit_pos_changed = FALSE;
	sheet->priv->selection_content_changed = FALSE;
	sheet->priv->recompute_visibility = FALSE;
	sheet->priv->recompute_spans = FALSE;
	sheet->priv->reposition_row_comment = SHEET_MAX_ROWS;
	sheet->priv->reposition_col_comment = SHEET_MAX_COLS;

	sheet->signature = SHEET_SIGNATURE;
	sheet->workbook = wb;
	sheet->name_unquoted = g_strdup (name);
	sheet->name_quoted = sheet_name_quote (name);
	sheet_create_styles (sheet);

	/*
	 * FIXME : Why choose -1. ?  This causes fonts scaled by -1 to be used
	 * during initialization.  Looks like a bug.
	 */
	sheet->last_zoom_factor_used = -1.0;
	sheet->cols.max_used = -1;
	sheet->rows.max_used = -1;
	sheet->solver_parameters.options.assume_linear_model = TRUE;
	sheet->solver_parameters.options.assume_non_negative = TRUE;
	sheet->solver_parameters.input_entry_str = g_strdup ("");
	sheet->solver_parameters.problem_type = SolverMaximize;
	sheet->solver_parameters.constraints = NULL;
	sheet->solver_parameters.target_cell = NULL;

	g_ptr_array_set_size (sheet->cols.info = g_ptr_array_new (), 
			      COLROW_SEGMENT_INDEX (SHEET_MAX_COLS-1)+1);
	g_ptr_array_set_size (sheet->rows.info = g_ptr_array_new (), 
			      COLROW_SEGMENT_INDEX (SHEET_MAX_ROWS-1)+1);
	sheet->print_info = print_info_new ();

	sheet->cell_hash  = g_hash_table_new (cell_hash,
					      (GCompareFunc)&cell_compare);
	sheet->deps       = dependency_data_new ();

	mstyle = mstyle_new_default ();
	sheet_style_attach (sheet, sheet_get_full_range (), mstyle);

	sheet_init_default_styles (sheet);

	sheet_view = GTK_WIDGET (sheet_new_sheet_view (sheet));

	sheet_selection_add (sheet, 0, 0);

	gtk_widget_show (sheet_view);

	sheet_set_zoom_factor (sheet, 1.0);

	sheet_corba_setup (sheet);

	sheet->pristine = TRUE;
	sheet->modified = FALSE;

	/* Init preferences */
	sheet->display_formulas = FALSE;
	sheet->display_zero = TRUE;
	sheet->show_grid = TRUE;
	sheet->show_col_header = TRUE;
	sheet->show_row_header = TRUE;

	return sheet;
}

static void
colrow_compute_pixels_from_pts (Sheet *sheet, ColRowInfo *info, gboolean const horizontal)
{
	double const scale =
	    sheet->last_zoom_factor_used *
	    application_display_dpi_get (horizontal) / 72.;

	/* 1) round to the nearest pixel
	 * 2) XL appears to scale including the margins & grid lines
	 *    but not the scale them.  So the size of the cell changes ???
	 */
	info->size_pixels = (int)(info->size_pts * scale + 0.5);
}
static void
colrow_compute_pts_from_pixels (Sheet *sheet, ColRowInfo *info, gboolean const horizontal)
{
	double const scale =
	    sheet->last_zoom_factor_used *
	    application_display_dpi_get (horizontal) / 72.;

	/* XL appears to scale including the margins & grid lines
	 * but not the scale them.  So the size of the cell changes ???
	 */
	info->size_pts = info->size_pixels / scale;
}

struct resize_colrow {
	Sheet *sheet;
	gboolean horizontal;
};

static gboolean
cb_colrow_compute_pixels_from_pts (ColRowInfo *info, void *data)
{
	struct resize_colrow *closure = data;
	colrow_compute_pixels_from_pts (closure->sheet, info, closure->horizontal);
	return FALSE;
}

/****************************************************************************/

static void
cb_recalc_span0 (gpointer key, gpointer value, gpointer flags)
{
	sheet_cell_calc_span (value, GPOINTER_TO_INT (flags));
}

void
sheet_calc_spans (Sheet const *sheet, SpanCalcFlags flags)
{
	g_hash_table_foreach (sheet->cell_hash, cb_recalc_span0, GINT_TO_POINTER (flags));
}

static Value *
cb_recalc_span1 (Sheet *sheet, int col, int row, Cell *cell, gpointer flags)
{
	sheet_cell_calc_span (cell, GPOINTER_TO_INT (flags));
	return NULL;
}

/**
 * sheet_range_calc_spans:
 * @sheet: The sheet,
 * @r:     the region to update.
 * @render_text: whether to re-render the text in cells
 * 
 * This is used to re-calculate cell dimensions and re-render
 * a cell's text. eg. if a format has changed we need to re-render
 * the cached version of the rendered text in the cell.
 **/
void
sheet_range_calc_spans (Sheet *sheet, Range r, SpanCalcFlags flags)
{
	sheet->modified = TRUE;

	/* Redraw the original region in case the span changes */
	sheet_redraw_cell_region (sheet,
				  r.start.col, r.start.row,
				  r.end.col, r.end.row);

	sheet_cell_foreach_range (sheet, TRUE,
				  r.start.col, r.start.row,
				  r.end.col, r.end.row,
				  cb_recalc_span1,
				  GINT_TO_POINTER (flags));

	/* Redraw the new region in case the span changes */
	sheet_redraw_cell_region (sheet,
				  r.start.col, r.start.row,
				  r.end.col, r.end.row);
}

void
sheet_cell_calc_span (Cell const *cell, SpanCalcFlags flags)
{
	CellSpanInfo const * span;
	int left, right;
	int min_col, max_col;
	gboolean render = (flags & SPANCALC_RE_RENDER);
	gboolean resize = (flags & SPANCALC_RESIZE);

	g_return_if_fail (cell != NULL);

	if (flags & SPANCALC_RENDER) {
		RenderedValue const *rv = cell->rendered_value;
		if (rv == NULL) {
			render = TRUE;
			resize = TRUE;
		} else if (rv->width_pixel == 0 && rv->width_pixel == 0)
			resize = TRUE;
	}

	if (render)
		cell_render_value ((Cell *)cell);

	if (resize)
		rendered_value_calc_size (cell);

	/* Calculate the span of the cell */
	cell_calc_span (cell, &left, &right);
	min_col = left;
	max_col = right;

	/* Is there an existing span ? */
	span = row_span_get (cell->row_info, cell->pos.col);
	if (span != NULL) {
		Cell const * const other = span->cell;
		int other_left, other_right;

		if (cell == other) {
			/* The existing span belonged to this cell */
			if (left != span->left || right != span->right) {
				cell_unregister_span (cell);
				cell_register_span (cell, left, right);
			}
			sheet_redraw_partial_row (cell->sheet, cell->row_info->pos,
						  left, right);
			return;
		}

		/* Do we need to redraw the original span because it shrank ? */
		if (min_col > span->left)
			min_col = span->left;
		if (max_col < span->right)
			max_col = span->right;

		/* A different cell used to span into this cell, respan that */
		cell_calc_span (other, &other_left, &other_right);
		cell_unregister_span (other);
		if (other_left != other_right)
			cell_register_span (other, other_left, other_right);

		/* Do we need to redraw the new span because it grew ? */
		if (min_col > other_left)
			min_col = other_left;
		if (max_col < other_right)
			max_col = other_right;
	}

	if (left != right)
		cell_register_span (cell, left, right);

	sheet_redraw_partial_row (cell->sheet, cell->row_info->pos,
				  min_col, max_col);
}

/****************************************************************************/

void
sheet_set_zoom_factor (Sheet *sheet, double const f)
{
	GList *l, *cl;
	struct resize_colrow closure;
	double factor, diff;

	g_return_if_fail (sheet != NULL);
	
	/* Bound zoom between 10% and 500% */
	factor = (f < .1) ? .1 : ((f > 5.) ? 5. : f);
	diff = sheet->last_zoom_factor_used - factor;

	if (-.0001 < diff && diff < .0001)
		return;

	sheet->last_zoom_factor_used = factor;

	/* First, the default styles */
	colrow_compute_pixels_from_pts (sheet, &sheet->rows.default_style, FALSE);
	colrow_compute_pixels_from_pts (sheet, &sheet->cols.default_style, TRUE);

	/* Then every column and row */
	closure.sheet = sheet;
	closure.horizontal = TRUE;
	col_row_foreach (&sheet->cols, 0, SHEET_MAX_COLS-1,
			 &cb_colrow_compute_pixels_from_pts, &closure);
	closure.horizontal = FALSE;
	col_row_foreach (&sheet->rows, 0, SHEET_MAX_ROWS-1,
			 &cb_colrow_compute_pixels_from_pts, &closure);

	for (l = sheet->sheet_views; l; l = l->next){
		SheetView *sheet_view = l->data;

		sheet_view_set_zoom_factor (sheet_view, factor);
	}

	for (cl = sheet->comment_list; cl; cl = cl->next){
		Cell *cell = cl->data;

		cell_comment_reposition (cell);
	}

	/*
	 * The font size does not scale linearly with the zoom factor
	 * we will need to recalculate the pixel sizes of all cells.
	 * We also need to render any cells which have not yet been
	 * rendered.
	 */
	sheet_calc_spans (sheet, SPANCALC_RESIZE|SPANCALC_RENDER);

	sheet_update_zoom_controls (sheet);
}

ColRowInfo *
sheet_row_new (Sheet *sheet)
{
	ColRowInfo *ri = g_new (ColRowInfo, 1);

	g_return_val_if_fail (sheet != NULL, NULL);
	g_return_val_if_fail (IS_SHEET (sheet), NULL);

	*ri = sheet->rows.default_style;

	return ri;
}

ColRowInfo *
sheet_col_new (Sheet *sheet)
{
	ColRowInfo *ci = g_new (ColRowInfo, 1);

	g_return_val_if_fail (sheet != NULL, NULL);
	g_return_val_if_fail (IS_SHEET (sheet), NULL);

	*ci = sheet->cols.default_style;

	return ci;
}

void
sheet_col_add (Sheet *sheet, ColRowInfo *cp)
{
	int const col = cp->pos;
	ColRowInfo *** segment = (ColRowInfo ***)&COLROW_GET_SEGMENT (&(sheet->cols), col);

	g_return_if_fail (col >= 0);
	g_return_if_fail (col < SHEET_MAX_COLS);

	if (*segment == NULL)
		*segment = g_new0(ColRowInfo *, COLROW_SEGMENT_SIZE);
	(*segment)[COLROW_SUB_INDEX(col)] = cp;

	if (col > sheet->cols.max_used){
		sheet->cols.max_used = col;
		sheet->priv->resize_scrollbar = TRUE;
	}
}

void
sheet_row_add (Sheet *sheet, ColRowInfo *rp)
{
	int const row = rp->pos;
	ColRowInfo *** segment = (ColRowInfo ***)&COLROW_GET_SEGMENT(&(sheet->rows), row);

	g_return_if_fail (row >= 0);
	g_return_if_fail (row < SHEET_MAX_ROWS);

	if (*segment == NULL)
		*segment = g_new0(ColRowInfo *, COLROW_SEGMENT_SIZE);
	(*segment)[COLROW_SUB_INDEX(row)] = rp;

	if (rp->pos > sheet->rows.max_used){
		sheet->rows.max_used = row;
		sheet->priv->resize_scrollbar = TRUE;
	}
}

ColRowInfo *
sheet_col_get_info (Sheet const *sheet, int const col)
{
	ColRowInfo *ci = NULL;
	ColRowInfo ** segment;

	g_return_val_if_fail (col >= 0, NULL);
	g_return_val_if_fail (col < SHEET_MAX_COLS, NULL);

	segment = COLROW_GET_SEGMENT(&(sheet->cols), col);
	if (segment != NULL)
		ci = segment[COLROW_SUB_INDEX(col)];
	if (ci != NULL)
		return ci;

	return (ColRowInfo *) &sheet->cols.default_style;
}

ColRowInfo *
sheet_row_get_info (Sheet const *sheet, int const row)
{
	ColRowInfo *ri = NULL;
	ColRowInfo ** segment;

	g_return_val_if_fail (row >= 0, NULL);
	g_return_val_if_fail (row < SHEET_MAX_ROWS, NULL);

	segment = COLROW_GET_SEGMENT(&(sheet->rows), row);
	if (segment != NULL)
		ri = segment[COLROW_SUB_INDEX(row)];
	if (ri != NULL)
		return ri;

	return (ColRowInfo *) &sheet->rows.default_style;
}

static void
sheet_reposition_comments (Sheet const * const sheet,
			   int const start_col, int const start_row)
{
	GList *l;

	/* Move any cell comments */
	for (l = sheet->comment_list; l; l = l->next){
		Cell *cell = l->data;

		if (cell->col_info->pos >= start_col ||
		    cell->row_info->pos >= start_row)
			cell_comment_reposition (cell);
	}
}

/**
 * sheet_flag_status_update_cell:
 *    flag the sheet as requiring an update to the status display
 *    if the supplied cell location is the edit cursor, or part of the
 *    selected region.
 *
 * @sheet :
 * @pos :
 *
 * Will cause the format toolbar, the edit area, and the auto expressions to be
 * updated if appropriate.
 */
void
sheet_flag_status_update_cell (Sheet const *sheet, CellPos const *pos)
{
	/* if a part of the selected region changed value update
	 * the auto expressions
	 */
	if (sheet_is_cell_selected (sheet, pos->col, pos->row))
		sheet->priv->selection_content_changed = TRUE;

	/* If the edit cell changes value update the edit area
	 * and the format toolbar
	 */
	if (pos->col == sheet->cursor.edit_pos.col &&
	    pos->row == sheet->cursor.edit_pos.row)
		sheet->priv->edit_pos_changed = TRUE;
}

/**
 * sheet_flag_status_update_range:
 *    flag the sheet as requiring an update to the status display
 *    if the supplied cell location contains the edit cursor, or intersects of
 *    the selected region.
 *
 * @sheet :
 * @range : If NULL then force an update.
 *
 * Will cause the format toolbar, the edit area, and the auto expressions to be
 * updated if appropriate.
 */
void
sheet_flag_status_update_range (Sheet const *sheet,
				Range const *range)
{
	/* Force an update */
	if (range == NULL) {
		sheet->priv->selection_content_changed = TRUE;
		sheet->priv->edit_pos_changed = TRUE;
		return;
	}

	/* if a part of the selected region changed value update
	 * the auto expressions
	 */
	if (sheet_is_range_selected (sheet, range))
		sheet->priv->selection_content_changed = TRUE;

	/* If the edit cell changes value update the edit area
	 * and the format toolbar
	 */
	if (range_contains(range, sheet->cursor.edit_pos.col, sheet->cursor.edit_pos.row))
		sheet->priv->edit_pos_changed = TRUE;
}

/**
 * sheet_flag_selection_change :
 *    flag the sheet as requiring an update to the status display
 *
 * @sheet :
 *
 * Will cause auto expressions to be updated
 */
void
sheet_flag_selection_change (Sheet const *sheet)
{
	sheet->priv->selection_content_changed = TRUE;
}

/**
 * sheet_update_controls:
 *
 * This routine is run every time the selection has changed.  It checks
 * what the status of various toolbar feedback controls should be
 */
static void
sheet_update_controls (Sheet const *sheet)
{
	MStyle *mstyle;

	g_return_if_fail (sheet != NULL);

	mstyle = sheet_style_compute (sheet,
				      sheet->cursor.edit_pos.col,
				      sheet->cursor.edit_pos.row);

	workbook_feedback_set (sheet->workbook, mstyle);
	mstyle_unref (mstyle);
}		

/*
 * sheet_update : Should be called after a logical command has finished processing
 *    to request redraws for any pending events
 */
void
sheet_update (Sheet const *sheet)
{
	SheetPrivate *p;

	g_return_if_fail (sheet != NULL);

	p = sheet->priv;

	if (p->recompute_spans) {
		p->recompute_spans = FALSE;
		sheet_calc_spans (sheet, SPANCALC_RESIZE|SPANCALC_RENDER |
				  (p->recompute_visibility ?
				   SPANCALC_NO_DRAW : SPANCALC_SIMPLE));
	}

	if (p->reposition_row_comment < SHEET_MAX_ROWS ||
	    p->reposition_col_comment < SHEET_MAX_COLS) {
		sheet_reposition_comments (sheet,
					   p->reposition_row_comment,
					   p->reposition_col_comment);
		p->reposition_row_comment = SHEET_MAX_ROWS;
		p->reposition_col_comment = SHEET_MAX_COLS;
	}

	if (p->recompute_visibility) {
		/* TODO : There is room for some opimization
		 * We only need to force complete visibility recalculation
		 * (which we do in sheet_compute_visible_ranges)
		 * if a row or col before the start of the visible range.
		 * If we are REALLY smart we could even accumulate the size differential
		 * and use that.
		 */
		p->recompute_visibility = FALSE;
		p->resize_scrollbar = FALSE; /* compute_visible_ranges does this */
		sheet_compute_visible_ranges (sheet);
		sheet_update_cursor_pos (sheet);
		sheet_redraw_all (sheet);
	}

	if (p->resize_scrollbar) {
		GList *l;

		for (l = sheet->sheet_views; l; l = l->next){
			SheetView *sheet_view = l->data;
			sheet_view_scrollbar_config (sheet_view);
		}
		p->resize_scrollbar = FALSE;
	}

	/* Only manipulate the status line if we are not selecting a region */
	if (sheet->workbook->editing)
		return;

	if (sheet->priv->edit_pos_changed) {
		sheet->priv->edit_pos_changed = FALSE;
		workbook_edit_load_value (sheet);
		sheet_update_controls (sheet);
		workbook_set_region_status (sheet->workbook,
					    cell_pos_name (&sheet->cursor.edit_pos));
	}

	if (sheet->priv->selection_content_changed) {
		sheet->priv->selection_content_changed = FALSE;
		sheet_update_auto_expr (sheet);
	}
}

/*
 * sheet_compute_visible_ranges : Keeps the top left col/row the same and
 *     recalculates the visible boundaries.  Recalculates the pixel offsets
 *     of the top left corner then recalculates the visible boundaries.
 */
void
sheet_compute_visible_ranges (Sheet const *sheet)
{
	GList *l;

	for (l = sheet->sheet_views; l; l = l->next){
		GnumericSheet *gsheet = GNUMERIC_SHEET_VIEW (l->data);

		gnumeric_sheet_compute_visible_ranges (gsheet, TRUE);
	}
}

/**
 * sheet_cell_get:
 * @sheet:  The sheet where we want to locate the cell
 * @col:    the cell column
 * @row:    the cell row
 *
 * Return value: a (Cell *) containing the Cell, or NULL if
 * the cell does not exist
 */
inline Cell *
sheet_cell_get (Sheet const *sheet, int col, int row)
{
	Cell *cell;
	Cell cellpos;

	g_return_val_if_fail (sheet != NULL, NULL);
	g_return_val_if_fail (IS_SHEET (sheet), NULL);

	cellpos.pos.col = col;
	cellpos.pos.row = row;
	cell = g_hash_table_lookup (sheet->cell_hash, &cellpos);

	return cell;
}

/**
 * sheet_cell_fetch:
 * @sheet:  The sheet where we want to locate the cell
 * @col:    the cell column
 * @row:    the cell row
 *
 * Return value: a (Cell *) containing the Cell at col, row.
 * If no cell existed at that location before, it is created.
 */
inline Cell *
sheet_cell_fetch (Sheet *sheet, int col, int row)
{
	Cell *cell;

	g_return_val_if_fail (sheet != NULL, NULL);
	g_return_val_if_fail (IS_SHEET (sheet), NULL);

	cell = sheet_cell_get (sheet, col, row);
	if (!cell)
		cell = sheet_cell_new (sheet, col, row);

	return cell;
}

/**
 * sheet_get_extent_cb:
 * 
 * checks the cell to see if should be used to calculate sheet extent
 **/
static void
sheet_get_extent_cb (gpointer key, gpointer value, gpointer data)
{
	Cell *cell = (Cell *) value;
	
	if (!cell_is_blank (cell)) {
		Range *range = (Range *)data;
		CellSpanInfo const *span = NULL;
		int tmp;

		if (cell->row_info->pos < range->start.row)
			range->start.row = cell->row_info->pos;

		if (cell->row_info->pos > range->end.row)
			range->end.row = cell->row_info->pos;

		span = row_span_get (cell->row_info, cell->pos.col);
		tmp = (span != NULL) ? span->left : cell->pos.col;
		if (tmp < range->start.col)
			range->start.col = tmp;

		tmp = (span != NULL) ? span->right : cell->pos.col;
		if (tmp > range->end.col)
			range->end.col = tmp;
	}
}

/**
 * sheet_get_extent:
 * @sheet: the sheet
 * 
 * calculates the area occupied by cell data.
 * 
 * Return value: the range.
 **/
Range
sheet_get_extent (Sheet const *sheet)
{
	Range r;

	r.start.col = SHEET_MAX_COLS - 2;
	r.start.row = SHEET_MAX_ROWS - 2;
	r.end.col   = 0;
	r.end.row   = 0;

	g_return_val_if_fail (sheet != NULL, r);
	g_return_val_if_fail (IS_SHEET (sheet), r);

	g_hash_table_foreach(sheet->cell_hash, &sheet_get_extent_cb, &r);

	if (r.start.col >= SHEET_MAX_COLS - 2)
		r.start.col = 0;
	if (r.start.row >= SHEET_MAX_ROWS - 2)
		r.start.row = 0;
	if (r.end.col < 0)
		r.end.col = 0;
	if (r.end.row < 0)
		r.end.row = 0;

#if 0
	/*
	 * Disable until this is more intelligent.
	 * if a style is applied to rows 0->10 the default
	 * style gets split and a style that ranges from 11->MAX
	 * This screws printing and several of the export
	 * routines.
	 */
	sheet_style_get_extent (&r, sheet);
#endif

	/*
	 *  Print can't handle stuff outside these walls.
	 */
	if (r.end.col < 0)
		r.end.col = 0;
	if (r.start.col > r.end.col)
		r.start.col = r.end.col;
	if (r.start.col < 0)
		r.start.col = 0;

	if (r.end.row < 0)
		r.end.row = 0;
	if (r.start.row > r.end.row)
		r.start.row = r.end.row;
	if (r.start.row < 0)
		r.start.row = 0;

	return r;
}

/*
 * Callback for sheet_cell_foreach_range to find the maximum width
 * in a range.
 */
static Value *
cb_max_cell_width (Sheet *sheet, int col, int row, Cell *cell,
		   int *max)
{
	int const width = cell_rendered_width (cell);
	if (width > *max)
		*max = width;
	return NULL;
}

/**
 * sheet_col_size_fit_pixels:
 * @sheet: The sheet
 * @col: the column that we want to query
 *
 * This routine computes the ideal size for the column to make the contents all
 * cells in the column visible.
 *
 * Return : Maximum size in pixels INCLUDING margins and grid lines
 *          or 0 if there are no cells.
 */
int
sheet_col_size_fit_pixels (Sheet *sheet, int col)
{
	int max = -1;
	ColRowInfo *ci = sheet_col_get (sheet, col);
	if (ci == NULL)
		return 0;

	sheet_cell_foreach_range (sheet, TRUE,
				  col, 0,
				  col, SHEET_MAX_ROWS-1,
				  (ForeachCellCB)&cb_max_cell_width, &max);

	/* Reset to the default width if the column was empty */
	if (max <= 0)
		return 0;

	/* Cell width does not include margins or far grid line*/
	max += ci->margin_a + ci->margin_b + 1;
	return max;
}

/*
 * Callback for sheet_cell_foreach_range to find the maximum height
 * in a range.
 */
static Value *
cb_max_cell_height (Sheet *sheet, int col, int row, Cell *cell,
		   int *max)
{
	int const height = cell_rendered_height (cell);
	if (height > *max)
		*max = height;
	return NULL;
}

/**
 * sheet_row_size_fit_pixels:
 * @sheet: The sheet
 * @col: the row that we want to query
 *
 * This routine computes the ideal size for the row to make all data fit
 * properly.
 *
 * Return : Maximum size in pixels INCLUDING margins and grid lines
 *          or 0 if there are no cells.
 */
int
sheet_row_size_fit_pixels (Sheet *sheet, int row)
{
	int max = -1;
	ColRowInfo *ri = sheet_row_get (sheet, row);
	if (ri == NULL)
		return 0;

	sheet_cell_foreach_range (sheet, TRUE,
				  0, row,
				  SHEET_MAX_COLS-1, row,
				  (ForeachCellCB)&cb_max_cell_height, &max);

	/* Reset to the default width if the column was empty */
	if (max <= 0)
		return 0;

	/* Cell height does not include margins or far grid line */
	max += ri->margin_a + ri->margin_b + 1;
	return max;
}

struct recalc_span_closure {
	Sheet *sheet;
	int col;
};

static gboolean
cb_recalc_spans_in_col (ColRowInfo *ri, gpointer user)
{
	struct recalc_span_closure *closure = user;
	int const col = closure->col;
	int left, right;
	CellSpanInfo const * span = row_span_get (ri, col);

	if (span) {
		/* If there is an existing span see if it changed */
		Cell const * const cell = span->cell;
		cell_calc_span (cell, &left, &right);
		if (left != span->left || right != span->right) {
			cell_unregister_span (cell);
			cell_register_span (cell, left, right);
		}
	} else {
		/* If there is a cell see if it started to span */
		Cell const * const cell = sheet_cell_get (closure->sheet, col, ri->pos);
		if (cell) {
			cell_calc_span (cell, &left, &right);
			if (left != right)
				cell_register_span (cell, left, right);
		}
	}

	return FALSE;
}

/**
 * sheet_recompute_spans_for_col:
 * @sheet: the sheet
 * @col:   The column that changed
 *
 * This routine recomputes the column span for the cells that touches
 * the column.
 */
void
sheet_recompute_spans_for_col (Sheet *sheet, int col)
{
	struct recalc_span_closure closure;
	closure.sheet = sheet;
	closure.col = col;

	col_row_foreach (&sheet->rows, 0, SHEET_MAX_ROWS-1,
			 &cb_recalc_spans_in_col, &closure);
}

void
sheet_update_auto_expr (Sheet const *sheet)
{
	Value *v;
	Workbook *wb = sheet->workbook;

	g_return_if_fail (sheet != NULL);
	g_return_if_fail (IS_SHEET (sheet));

	/* defaults */
	v = NULL;

	if (wb->auto_expr) {
		static CellPos const cp = {0,0};
		EvalPos ep;
		v = eval_expr (eval_pos_init (&ep, (Sheet *)sheet, &cp),
			       wb->auto_expr, EVAL_STRICT);
		if (v) {
			char *s;

			s = value_get_as_string (v);
			workbook_auto_expr_label_set (wb, s);
			g_free (s);
			value_release (v);
		} else
			workbook_auto_expr_label_set (wb, _("Internal ERROR"));
	}
}

/****************************************************************************/

/*
 * Callback for sheet_cell_foreach_range to assign some text
 * to a range.
 */
typedef struct {
	StyleFormat *format;
	String     *entered_text;
	Value      *val;
	ExprTree   *expr;
} closure_set_cell_value;

static Value *
cb_set_cell_content (Sheet *sheet, int col, int row, Cell *cell,
		     closure_set_cell_value *info)
{
	if (cell == NULL)
		cell = sheet_cell_new (sheet, col, row);
	if (info->expr != NULL)
		cell_set_expr (cell, info->expr, info->format);
	else
		cell_set_text_and_value (cell, info->entered_text,
					 value_duplicate (info->val),
					 info->format);
	return NULL;
}

/**
 * sheet_range_set_text :
 *
 * @pos : The position from which to parse an expression.
 * @r  :  The range to fill
 * @str : The text to be parsed and assigned.
 *
 * Does NOT check for array division.
 * Does NOT redraw
 * Does NOT generate spans.
 */
void
sheet_range_set_text (EvalPos const *pos, Range const *r, char const *str)
{
	closure_set_cell_value	closure;

	g_return_if_fail (pos != NULL);
	g_return_if_fail (r != NULL);
	g_return_if_fail (str != NULL);

	closure.format =
		parse_text_value_or_expr (pos, str, &closure.val, &closure.expr,
					  NULL /* TODO : Use edit_pos format ?? */);

	/* Remember the entered text for values */
	closure.entered_text = (closure.val != NULL) ? string_get (str) : NULL;

	/* Store the parsed result creating any cells necessary */
	sheet_cell_foreach_range (pos->sheet, FALSE,
				  r->start.col, r->start.row,
				  r->end.col, r->end.row,
				  (ForeachCellCB)&cb_set_cell_content,
				  &closure);

	if (closure.format)
		style_format_unref (closure.format);

	if (closure.val) {
		value_release (closure.val);
		string_unref (closure.entered_text);
	} else
		expr_tree_unref (closure.expr);

	sheet_flag_status_update_range (pos->sheet, r);
}

void
sheet_cell_set_text (Cell *cell, char const *str)
{
	StyleFormat *format;
	Value *val;
	ExprTree *expr;
	EvalPos pos;

	g_return_if_fail (str != NULL);
	g_return_if_fail (cell != NULL);
	g_return_if_fail (!cell_is_partial_array (cell));

	format = parse_text_value_or_expr (eval_pos_init_cell (&pos, cell),
					   str, &val, &expr,
					   cell_get_format (cell));
	if (expr != NULL) {
		cell_set_expr (cell, expr, format);
		expr_tree_unref (expr);
	} else {
		String *string = string_get (str);
		cell_set_text_and_value (cell, string, val, format);
		string_unref (string);
		sheet_cell_calc_span (cell, SPANCALC_RESIZE);
		cell_content_changed (cell);
	}
	if (format)
		style_format_unref (format);
	sheet_flag_status_update_cell (cell->sheet, &cell->pos);
}

void
sheet_cell_set_expr (Cell *cell, ExprTree *expr)
{
	/* No need to do anything until recalc */
	cell_set_expr (cell, expr, NULL);
	sheet_flag_status_update_cell (cell->sheet, &cell->pos);
}

/*
 * sheet_cell_set_value : Stores (WITHOUT COPYING) the supplied value.  It marks the
 *          sheet as dirty.
 *
 * The value is rendered, spans are calculated, and the rendered string
 * is stored as if that is what the user had entered.  It queues a redraw
 * and checks to see if the edit region or selection content changed.
 *
 * If an optional format is supplied it is stored for later use.
 *
 * NOTE : This DOES check for array partitioning.
 */
void
sheet_cell_set_value (Cell *cell, Value *v, StyleFormat *opt_fmt)
{
	cell_set_value (cell, v, opt_fmt);
	sheet_cell_calc_span (cell, SPANCALC_RESIZE);
	cell_content_changed (cell);
	sheet_flag_status_update_cell (cell->sheet, &cell->pos);
}

/**
 * sheet_update_zoom_controls:
 *
 * This routine is run every time the zoom has changed.  It checks
 * what the status of various toolbar feedback controls should be
 *
 * FIXME: This will at some point become a sheet view function.
 */
static void
sheet_update_zoom_controls (Sheet *sheet)
{
	g_return_if_fail (sheet != NULL);

	if (sheet == sheet->workbook->current_sheet)
		workbook_zoom_feedback_set (sheet->workbook,
					    sheet->last_zoom_factor_used);
}		

int
sheet_col_selection_type (Sheet const *sheet, int col)
{
	SheetSelection *ss;
	GList *l;
	int ret = ITEM_BAR_NO_SELECTION;

	g_return_val_if_fail (sheet != NULL, ITEM_BAR_NO_SELECTION);
	g_return_val_if_fail (IS_SHEET (sheet), ITEM_BAR_NO_SELECTION);

	if (sheet->selections == NULL)
		return ITEM_BAR_NO_SELECTION;

	for (l = sheet->selections; l != NULL; l = l->next){
		ss = l->data;

		if (ss->user.start.col > col ||
		    ss->user.end.col < col)
			continue;

		if (ss->user.start.row == 0 &&
		    ss->user.end.row == SHEET_MAX_ROWS-1)
			return ITEM_BAR_FULL_SELECTION;

		ret = ITEM_BAR_PARTIAL_SELECTION;
	}

	return ret;
}

int
sheet_row_selection_type (Sheet const *sheet, int row)
{
	SheetSelection *ss;
	GList *l;
	int ret = ITEM_BAR_NO_SELECTION;

	g_return_val_if_fail (sheet != NULL, ITEM_BAR_NO_SELECTION);
	g_return_val_if_fail (IS_SHEET (sheet), ITEM_BAR_NO_SELECTION);

	if (sheet->selections == NULL)
		return ITEM_BAR_NO_SELECTION;

	for (l = sheet->selections; l != NULL; l = l->next){
		ss = l->data;

		if (ss->user.start.row > row ||
		    ss->user.end.row < row)
			continue;

		if (ss->user.start.col == 0 &&
		    ss->user.end.col == SHEET_MAX_COLS-1)
			return ITEM_BAR_FULL_SELECTION;

		ret = ITEM_BAR_PARTIAL_SELECTION;
	}

	return ret;
}

/****************************************************************************/

/*
 * This routine is used to queue the redraw regions for the
 * cell region specified.
 *
 * It is usually called before a change happens to a region,
 * and after the change has been done to queue the regions
 * for the old contents and the new contents.
 */
void
sheet_redraw_cell_region (Sheet const *sheet,
			  int start_col, int start_row,
			  int end_col,   int end_row)
{
	GList *l;
	int row, min_col = start_col, max_col = end_col;

	g_return_if_fail (sheet != NULL);
	g_return_if_fail (IS_SHEET (sheet));
	g_return_if_fail (start_col <= end_col);
	g_return_if_fail (start_row <= end_row);

	/*
	 * Check the first and last columns for spans
	 * and extend the region to include the maximum extent.
	 */
	for (row = start_row; row <= end_row; row++){
		ColRowInfo const * const ri = sheet_row_get (sheet, row);

		if (ri != NULL) {
			CellSpanInfo const * span0 =
			    row_span_get (ri, start_col);

			if (span0 != NULL) {
				min_col = MIN (span0->left, min_col);
				max_col = MAX (span0->right, max_col);
			}
			if (start_col != end_col) {
				CellSpanInfo const * span1 =
					row_span_get (ri, end_col);

				if (span1 != NULL) {
					min_col = MIN (span1->left, min_col);
					max_col = MAX (span1->right, max_col);
				}
			}
			/* skip segments with no cells */
		} else if (row == COLROW_SEGMENT_START (row)) {
			ColRowInfo const * const * const segment =
				COLROW_GET_SEGMENT(&(sheet->rows), row);
			if (segment == NULL)
				row = COLROW_SEGMENT_END (row);
		}
	}

	for (l = sheet->sheet_views; l; l = l->next){
		SheetView *sheet_view = l->data;

		sheet_view_redraw_cell_region (
			sheet_view,
			min_col, start_row,
			max_col, end_row);
	}
}

void
sheet_redraw_range (Sheet const *sheet, Range const *range)
{
	g_return_if_fail (sheet != NULL);
	g_return_if_fail (IS_SHEET (sheet));
	g_return_if_fail (range != NULL);

	sheet_redraw_cell_region (sheet,
				  range->start.col, range->start.row,
				  range->end.col, range->end.row);
}

static void
sheet_redraw_partial_row (Sheet const *sheet, int const row,
			  int const start_col, int const end_col)
{
	GList *l;
	for (l = sheet->sheet_views; l; l = l->next)
		sheet_view_redraw_cell_region (l->data,
					       start_col, row, end_col, row);
}

void
sheet_redraw_cell (Cell const *cell)
{
	CellSpanInfo const * span;
	int start_col, end_col;

	g_return_if_fail (cell != NULL);

	start_col = end_col = cell->col_info->pos;
	span = row_span_get (cell->row_info, start_col);

	if (span) {
		start_col = span->left;
		end_col = span->right;
	}

	sheet_redraw_partial_row (cell->sheet, cell->row_info->pos,
				  start_col, end_col);
}

/****************************************************************************/

int
sheet_row_check_bound (int row, int diff)
{
	int new_val = row + diff;

	if (new_val < 0)
		return 0;
	if (new_val >= SHEET_MAX_ROWS)
		return SHEET_MAX_ROWS - 1;

	return new_val;
}

int
sheet_col_check_bound (int col, int diff)
{
	int new_val = col + diff;

	if (new_val < 0)
		return 0;
	if (new_val >= SHEET_MAX_COLS)
		return SHEET_MAX_COLS - 1;

	return new_val;
}

static gboolean
sheet_col_is_hidden (Sheet *sheet, int const col)
{
	ColRowInfo const * const res = sheet_col_get (sheet, col);
	return (res != NULL && !res->visible);
}

/*
 * sheet_find_boundary_horizontal
 *
 * Calculate the column index for the column which is @n units
 * from @start_col doing bounds checking.  If @jump_to_boundaries is
 * TRUE @n must be 1 and the jump is to the edge of the logical range.
 *
 * @sheet:  The Sheet *
 * @start_col	: The column from which to begin searching.
 * @row		: The row in which to search for the edge of the range.
 * @n:      units to extend the selection vertically
 * @jump_to_boundaries : Jump to range boundaries.
 */
int
sheet_find_boundary_horizontal (Sheet *sheet, int start_col, int row,
				int count, gboolean jump_to_boundaries)
{
	gboolean find_nonblank = cell_is_blank (sheet_cell_get (sheet, start_col, row));
	int new_col = start_col, prev_col = start_col;
	gboolean keep_looking = FALSE;
	int iterations = 0;

	g_return_val_if_fail (sheet != NULL, start_col);
	/* Jumping to bounds requires steping cell by cell */
	g_return_val_if_fail (count == 1 || count == -1 || !jump_to_boundaries, start_col);

	do {
		new_col += count;
		++iterations;
		keep_looking = FALSE;

		if (new_col < 0)
			return 0;
		if (new_col >= SHEET_MAX_COLS)
			return SHEET_MAX_COLS-1;
		if (jump_to_boundaries) {
			if (new_col > sheet->cols.max_used) {
				if (count > 0)
					return (find_nonblank || iterations == 1) ? SHEET_MAX_COLS-1 : prev_col;
				new_col = sheet->cols.max_used;
			}
			keep_looking = (cell_is_blank (sheet_cell_get (sheet, new_col, row)) == find_nonblank);
			if (keep_looking)
				prev_col = new_col;
			else if (!find_nonblank) {
				/*
				 * Handle special case where we are on the last
				 * non-null cell
				 */
				if (iterations == 1)
					keep_looking = find_nonblank = TRUE;
				else
					new_col = prev_col;
			}
		}
	} while (keep_looking || sheet_col_is_hidden (sheet, new_col));

	return new_col;
}

static gboolean
sheet_row_is_hidden (Sheet *sheet, int const row)
{
	ColRowInfo const * const res = sheet_row_get (sheet, row);
	return (res != NULL && !res->visible);
}

/*
 * sheet_find_boundary_vertical
 *
 * Calculate the row index for the row which is @n units
 * from @start_row doing bounds checking.  If @jump_to_boundaries is
 * TRUE @n must be 1 and the jump is to the edge of the logical range.
 *
 * @sheet:  The Sheet *
 * @col		: The col in which to search for the edge of the range.
 * @start_row	: The row from which to begin searching.
 * @n:      units to extend the selection vertically
 * @jump_to_boundaries : Jump to range boundaries.
 */
int
sheet_find_boundary_vertical (Sheet *sheet, int col, int start_row,
			      int count, gboolean jump_to_boundaries)
{
	gboolean find_nonblank = cell_is_blank (sheet_cell_get (sheet, col, start_row));
	int new_row = start_row, prev_row = start_row;
	gboolean keep_looking = FALSE;
	int iterations = 0;

	/* Jumping to bounds requires steping cell by cell */
	g_return_val_if_fail (count == 1 || count == -1 || !jump_to_boundaries, start_row);

	do {
		new_row += count;
		++iterations;
		keep_looking = FALSE;

		if (new_row < 0)
			return 0;
		if (new_row > SHEET_MAX_ROWS-1)
			return SHEET_MAX_ROWS-1;
		if (jump_to_boundaries) {
			if (new_row > sheet->rows.max_used) {
				if (count > 0)
					return (find_nonblank || iterations == 1) ? SHEET_MAX_ROWS-1 : prev_row;
				new_row = sheet->rows.max_used;
			}

			keep_looking = (cell_is_blank (sheet_cell_get (sheet, col, new_row)) == find_nonblank);
			if (keep_looking)
				prev_row = new_row;
			else if (!find_nonblank) {
				/*
				 * Handle special case where we are on the last
				 * non-null cell
				 */ 
				if (iterations == 1)
					keep_looking = find_nonblank = TRUE;
				else
					new_row = prev_row;
			}
		}
	} while (keep_looking || sheet_row_is_hidden (sheet, new_row));

	return new_row;
}

static ExprArray const*
sheet_is_cell_array (Sheet const *sheet, int const col, int const row)
{
	return cell_is_array (sheet_cell_get (sheet, col, row));
}

typedef enum {
	CHECK_AND_LOAD_START = 1,
	CHECK_END = 2,
	LOAD_END  = 4
} ArrayCheckFlags;

typedef struct _ArrayCheckData {
	Sheet const *sheet;
	int flags;
	int start, end;
} ArrayCheckData;

static gboolean
cb_check_array_horizontal (ColRowInfo *col, void *user)
{
	ArrayCheckData *data = user;
	ExprArray const *a = NULL;

	if (data->flags & CHECK_AND_LOAD_START) {
		if ((a = sheet_is_cell_array (data->sheet, col->pos, data->start)) != NULL)
			if (a->y != 0)		/* Top */
				return TRUE;
	}
	if (data->flags & LOAD_END)
		a = sheet_is_cell_array (data->sheet, col->pos, data->end);

	if (data->flags & CHECK_END)
		return (a != NULL) && (a->y != (a->rows-1));	/* Bottom */
	return FALSE;
}

static gboolean
cb_check_array_vertical (ColRowInfo *row, void *user)
{
	ArrayCheckData *data = user;
	ExprArray const *a = NULL;

	if (data->flags & CHECK_AND_LOAD_START) {
		if ((a = sheet_is_cell_array (data->sheet, data->start, row->pos)) != NULL)
			if (a->x != 0)		/* Left */
				return TRUE;
	}
	if (data->flags & LOAD_END)
		a = sheet_is_cell_array (data->sheet, data->end, row->pos);

	if (data->flags & CHECK_END)
		return (a != NULL) && (a->x != (a->cols-1));	/* Right */
	return FALSE;
}

/**
 * sheet_range_splits_array : * Check the outer edges of range to ensure that
 *         if an array is within it then the entire array is within the range.
 *
 * returns TRUE is an array would be split.
 */
gboolean
sheet_range_splits_array (Sheet const *sheet, Range const *r)
{
	ArrayCheckData closure;

	g_return_val_if_fail (r->start.col <= r->end.col, FALSE);
	g_return_val_if_fail (r->start.row <= r->end.row, FALSE);

	closure.sheet = sheet;

	closure.start = r->start.row;
	closure.end = r->end.row;
	if (closure.start <= 0) {
		closure.flags = (closure.end < sheet->rows.max_used)
			? CHECK_END | LOAD_END
			: 0;
	} else {
		closure.flags = (closure.start == closure.end)
			? CHECK_AND_LOAD_START | CHECK_END
			: CHECK_AND_LOAD_START | CHECK_END | LOAD_END;
	}

	if (closure.flags && col_row_foreach (&sheet->cols,
					      r->start.col, r->end.col,
					      &cb_check_array_horizontal,
					      &closure))
		return TRUE;
	
	closure.start = r->start.col;
	closure.end = r->end.col;
	if (closure.start <= 0) {
		closure.flags = (closure.end < sheet->cols.max_used)
			? CHECK_END | LOAD_END
			: 0;
	} else {
		closure.flags = (closure.start == closure.end)
			? CHECK_AND_LOAD_START | CHECK_END
			: CHECK_AND_LOAD_START | CHECK_END | LOAD_END;
	}

	return closure.flags &&
		col_row_foreach (&sheet->rows, r->start.row, r->end.row,
				 &cb_check_array_vertical, &closure);
}

/**
 * sheet_col_get:
 *
 * Returns an allocated column:  either an existing one, or NULL
 */
ColRowInfo *
sheet_col_get (Sheet const *sheet, int const pos)
{
	ColRowInfo *ci = NULL;
	ColRowInfo ** segment;

	g_return_val_if_fail (sheet != NULL, NULL);
	g_return_val_if_fail (IS_SHEET (sheet), NULL);
	g_return_val_if_fail (pos < SHEET_MAX_COLS, NULL);
	g_return_val_if_fail (pos >= 0, NULL);

	segment = COLROW_GET_SEGMENT (&(sheet->cols), pos);
	if (segment != NULL)
		ci = segment [COLROW_SUB_INDEX(pos)];
	return ci;
}
/**
 * sheet_col_get:
 *
 * Returns an allocated column:  either an existing one, or a fresh copy
 */
ColRowInfo *
sheet_col_fetch (Sheet *sheet, int pos)
{
	ColRowInfo * res = sheet_col_get (sheet, pos);
	if (res == NULL)
		if ((res = sheet_col_new (sheet)) != NULL) {
			res->pos = pos;
			sheet_col_add (sheet, res);
		}
	return res;
}

/**
 * sheet_row_get:
 *
 * Returns an allocated row:  either an existing one, or NULL
 */
ColRowInfo *
sheet_row_get (Sheet const *sheet, int const pos)
{
	ColRowInfo *ri = NULL;
	ColRowInfo ** segment;

	g_return_val_if_fail (sheet != NULL, NULL);
	g_return_val_if_fail (IS_SHEET (sheet), NULL);
	g_return_val_if_fail (pos < SHEET_MAX_ROWS, NULL);
	g_return_val_if_fail (pos >= 0, NULL);

	segment = COLROW_GET_SEGMENT (&(sheet->rows), pos);
	if (segment != NULL)
		ri = segment [COLROW_SUB_INDEX(pos)];
	return ri;
}

/**
 * sheet_row_fetch:
 *
 * Returns an allocated row:  either an existing one, or a fresh copy
 */
ColRowInfo *
sheet_row_fetch (Sheet *sheet, int pos)
{
	ColRowInfo * res = sheet_row_get (sheet, pos);
	if (res == NULL)
		if ((res = sheet_row_new (sheet)) != NULL) {
			res->pos = pos;
			sheet_row_add (sheet, res);
		}
	return res;
}

#define SWAP_INT(a,b) do { int t; t = a; a = b; b = t; } while (0)

/**
 * sheet_cell_foreach_range:
 *
 * For each existing cell in the range specified, invoke the
 * callback routine.  If the only_existing flag is passed, then
 * callbacks are only invoked for existing cells.
 *
 * Return value:
 *    non-NULL on error, or value_terminate() if some invoked routine requested
 *    to stop (by returning non-NULL).
 */
Value *
sheet_cell_foreach_range (Sheet *sheet, gboolean only_existing,
			  int start_col, int start_row,
			  int end_col, int end_row,
			  ForeachCellCB callback,
			  void *closure)
{
	int i, j;
	Value *cont;

	g_return_val_if_fail (sheet != NULL, NULL);
	g_return_val_if_fail (IS_SHEET (sheet), NULL);
	g_return_val_if_fail (callback != NULL, NULL);

	if (start_col > end_col)
		SWAP_INT (start_col, end_col);

	if (start_row > end_row)
		SWAP_INT (start_row, end_row);

	if (only_existing) {
		if (end_col > sheet->cols.max_used)
			end_col = sheet->cols.max_used;
		if (end_row > sheet->rows.max_used)
			end_row = sheet->rows.max_used;
	}

	for (i = start_col; i <= end_col ; ++i) {
		ColRowInfo *ci = sheet_col_get (sheet, i);

		if (ci == NULL) {
			if (only_existing) {
				/* skip segments with no cells */
				if (i == COLROW_SEGMENT_START (i)) {
					ColRowInfo const * const * const segment =
						COLROW_GET_SEGMENT(&(sheet->cols), i);
					if (segment == NULL)
						i = COLROW_SEGMENT_END(i);
				}
			} else {
				for (j = start_row; j <= end_row ; ++j) {
					cont = (*callback)(sheet, i, j, NULL, closure);
					if (cont != NULL)
						return cont;
				}
			}

			continue;
		}

		for (j = start_row; j <= end_row ; ++j) {
			ColRowInfo *ri = sheet_row_get (sheet, j);
			Cell *cell = NULL;

			if (ri != NULL)
				cell = sheet_cell_get (sheet, i, j);

			if (cell == NULL && only_existing) {
				/* skip segments with no cells */
				if (j == COLROW_SEGMENT_START (j)) {
					ColRowInfo const * const * const segment =
						COLROW_GET_SEGMENT(&(sheet->rows), j);
					if (segment == NULL)
						j = COLROW_SEGMENT_END(j);
				}
				continue;
			}

			cont = (*callback)(sheet, i, j, cell, closure);
			if (cont != NULL)
				return cont;
		}
	}
	return NULL;
}

static Value *
fail_if_not_selected (Sheet *sheet, int col, int row, Cell *cell, void *user_data)
{
	if (!sheet_is_cell_selected (sheet, col, row))
		return value_terminate ();
	else
		return NULL;
}

/**
 * sheet_is_region_empty:
 * @sheet: sheet to check
 * @start_col: starting column
 * @start_row: starting row
 * @end_col:   end column
 * @end_row:   end row
 *
 * Returns TRUE if the specified region of the @sheet does not
 * contain any cells.
 *
 * FIXME: Perhaps this routine should be extended to allow testing for specific
 * features of a cell rather than just the existance of the cell.
 */
gboolean
sheet_is_region_empty_or_selected (Sheet *sheet, int start_col, int start_row, int end_col, int end_row)
{
	g_return_val_if_fail (sheet != NULL, TRUE);
	g_return_val_if_fail (IS_SHEET (sheet), TRUE);
	g_return_val_if_fail (start_col <= end_col, TRUE);
	g_return_val_if_fail (start_row <= end_row, TRUE);

	return sheet_cell_foreach_range (
		sheet, TRUE, start_col, start_row, end_col, end_row,
		fail_if_not_selected, NULL) == NULL;

}

/**
 * sheet_cell_add_to_hash:
 * @sheet The sheet where the cell is inserted
 * @cell  The cell, it should already have col/pos pointers
 *        initialized pointing to the correct ColRowInfo
 */
static void
sheet_cell_add_to_hash (Sheet *sheet, Cell *cell)
{
	g_return_if_fail (cell->col_info != NULL);
	g_return_if_fail (cell->col_info->pos < SHEET_MAX_COLS);
	g_return_if_fail (cell->row_info != NULL);
	g_return_if_fail (cell->row_info->pos < SHEET_MAX_ROWS);
	g_return_if_fail (!cell_is_linked (cell));

	cell->cell_flags |= CELL_IN_SHEET_LIST;
	cell->pos.col = cell->col_info->pos;
	cell->pos.row = cell->row_info->pos;

	g_hash_table_insert (sheet->cell_hash, cell, cell);
}

void
sheet_cell_insert (Sheet *sheet, Cell *cell, int col, int row)
{
	cell->sheet = sheet;
	cell->col_info   = sheet_col_fetch (sheet, col);
	cell->row_info   = sheet_row_fetch (sheet, row);

	sheet_cell_add_to_hash (sheet, cell);
	cell_add_dependencies (cell);
	cell_realize (cell);

	if (!cell_needs_recalc(cell))
		sheet_cell_calc_span (cell, SPANCALC_RESIZE);
}

Cell *
sheet_cell_new (Sheet *sheet, int col, int row)
{
	Cell *cell;

	g_return_val_if_fail (sheet != NULL, NULL);
	g_return_val_if_fail (IS_SHEET (sheet), NULL);

	cell = g_new0 (Cell, 1);

	cell->sheet = sheet;
	cell->col_info   = sheet_col_fetch (sheet, col);
	cell->row_info   = sheet_row_fetch (sheet, row);
	cell->value      = value_new_empty ();

	sheet_cell_add_to_hash (sheet, cell);
	return cell;
}

static void
sheet_cell_remove_from_hash (Sheet *sheet, Cell *cell)
{
	Cell  cellpos;

	g_return_if_fail (cell_is_linked (cell));

	cellpos.pos.col = cell->col_info->pos;
	cellpos.pos.row = cell->row_info->pos;

	cell_unregister_span   (cell);
	cell_drop_dependencies (cell);

	g_hash_table_remove (sheet->cell_hash, &cellpos);
	cell->cell_flags &= ~CELL_IN_SHEET_LIST;
}

/**
 * sheet_cell_remove_simple : Remove the cell from the web of depenancies of a
 *        sheet.  Do NOT redraw or free the cell.
 */
void
sheet_cell_remove_simple (Sheet *sheet, Cell *cell)
{
	GList *deps;

	if (cell_needs_recalc(cell))
		eval_unqueue_cell (cell);

	if (cell_has_expr (cell))
		sheet_cell_expr_unlink (cell);

	deps = cell_get_dependencies (cell);
	if (deps)
		eval_queue_list (deps, TRUE);

	sheet_cell_remove_from_hash (sheet, cell);

	cell_unrealize (cell);
}

/**
 * sheet_cell_remove_simple : Remove the cell from the web of depenancies of a
 *        sheet.  Do NOT free the cell, optionally redraw it.
 */
void
sheet_cell_remove (Sheet *sheet, Cell *cell, gboolean redraw)
{
	g_return_if_fail (sheet != NULL);
	g_return_if_fail (cell != NULL);
	g_return_if_fail (IS_SHEET (sheet));

	/* Queue a redraw on the region used by the cell being removed */
	if (redraw) {
		sheet_redraw_cell_region (sheet,
					  cell->pos.col, cell->pos.row,
					  cell->pos.col, cell->pos.row);
		sheet_flag_status_update_cell (sheet, &cell->pos);
	}

	sheet_cell_remove_simple (sheet, cell);
	cell_destroy (cell);
}

void
sheet_cell_comment_link (Cell *cell)
{
	Sheet *sheet;

	g_return_if_fail (cell != NULL);
	g_return_if_fail (cell->sheet != NULL);

	sheet = cell->sheet;

	sheet->comment_list = g_list_prepend (sheet->comment_list, cell);
}

void
sheet_cell_comment_unlink (Cell *cell)
{
	Sheet *sheet;

	g_return_if_fail (cell != NULL);
	g_return_if_fail (cell->sheet != NULL);
	g_return_if_fail (cell->comment != NULL);

	sheet = cell->sheet;
	sheet->comment_list = g_list_remove (sheet->comment_list, cell);
}

void
sheet_cell_expr_link (Cell *cell)
{
	Sheet *sheet;

	g_return_if_fail (cell != NULL);
	g_return_if_fail (cell_has_expr (cell));
	g_return_if_fail (!cell_expr_is_linked (cell));

	sheet = cell->sheet;

#ifdef DEBUG_CELL_FORMULA_LIST
	if (g_list_find (sheet->workbook->formula_cell_list, cell)) {
		/* Anything that shows here is a bug.  */
		g_warning ("Cell %s %p re-linked\n", cell_name (cell), cell);
		return;
	}
#endif

	sheet->workbook->formula_cell_list =
		g_list_prepend (sheet->workbook->formula_cell_list, cell);
	cell_add_dependencies (cell);
	cell->cell_flags |= CELL_IN_EXPR_LIST;
}

void
sheet_cell_expr_unlink (Cell *cell)
{
	Sheet *sheet;

	g_return_if_fail (cell != NULL);
	g_return_if_fail (cell_has_expr (cell));
	g_return_if_fail (cell_expr_is_linked (cell));

	cell->cell_flags &= ~CELL_IN_EXPR_LIST;

	sheet = cell->sheet;
	if (sheet == NULL)
		return;

	cell_drop_dependencies (cell);
	sheet->workbook->formula_cell_list = g_list_remove (sheet->workbook->formula_cell_list, cell);

	/* Just an optimization to avoid an expensive list lookup */
	if (cell->cell_flags & CELL_QUEUED_FOR_RECALC)
		eval_unqueue_cell (cell);
}

/**
 * sheet_expr_unlink : An internal routine to remove all expressions
 *      associated with a given sheet from the workbook wide expression list.
 *
 * WARNING : This is a dangerous internal function.  it leaves the cells in an
 *	invalid state.  It is intended for use by sheet_destroy_contents.
 */
static void
sheet_expr_unlink (Sheet *sheet)
{
	GList *ptr, *next, *queue;
	Workbook *wb;

	g_return_if_fail (sheet != NULL);
	g_return_if_fail (IS_SHEET (sheet));

	wb = sheet->workbook;
	queue = wb->formula_cell_list;
	for (ptr = queue; ptr != NULL ; ptr = next) {
		Cell *cell = ptr->data;
		next = ptr->next;

		if (cell->sheet == sheet) {
			cell->cell_flags &= ~CELL_IN_EXPR_LIST;
			queue = g_list_remove_link (queue, ptr);
			g_list_free_1 (ptr);
		}
	}
	wb->formula_cell_list = queue;
}

/*
 * Callback for sheet_cell_foreach_range to remove a set of cells.
 */
static Value *
cb_free_cell (Sheet *sheet, int col, int row, Cell *cell, void *user_data)
{
	sheet_cell_remove_simple (sheet, cell);
	cell_destroy (cell);
	return NULL;
}

/**
 * sheet_col_destroy:
 *
 * Destroys a ColRowInfo from the Sheet with all of its cells
 */
static void
sheet_col_destroy (Sheet *sheet, int const col, gboolean free_cells)
{
	ColRowInfo ***segment = (ColRowInfo ***)&COLROW_GET_SEGMENT(&(sheet->cols), col);
	int const sub = COLROW_SUB_INDEX (col);
	ColRowInfo *ci = NULL;

	if (*segment == NULL)
		return;
	ci = (*segment)[sub];
	if (ci == NULL)
		return;

	if (free_cells)
		sheet_cell_foreach_range (sheet, TRUE,
					  col, 0,
					  col, SHEET_MAX_ROWS-1,
					  &cb_free_cell, NULL);

	(*segment)[sub] = NULL;
	g_free (ci);

	/* Use >= just in case things are screwed up */
	if (col >= sheet->cols.max_used) {
		int i = col;
		while (--i >= 0 && sheet_col_get (sheet, i) == NULL)
		    ;
		sheet->cols.max_used = i;
	}
}

/*
 * Destroys a row ColRowInfo
 */
static void
sheet_row_destroy (Sheet *sheet, int const row, gboolean free_cells)
{
	ColRowInfo ***segment = (ColRowInfo ***)&COLROW_GET_SEGMENT(&(sheet->rows), row);
	int const sub = COLROW_SUB_INDEX(row);
	ColRowInfo *ri = NULL;
	if (*segment == NULL)
		return;
	ri = (*segment)[sub];
	if (ri == NULL)
		return;

	if (free_cells)
		sheet_cell_foreach_range (sheet, TRUE,
					  0, row,
					  SHEET_MAX_COLS-1, row,
					  &cb_free_cell, NULL);

	/* Rows have span lists, destroy them too */
	row_destroy_span (ri);

	(*segment)[sub] = NULL;
	g_free (ri);

	/* Use >= just in case things are screwed up */
	if (row >= sheet->rows.max_used) {
		int i = row;
		while (--i >= 0 && sheet_row_get (sheet, i) == NULL)
		    ;
		sheet->rows.max_used = i;
	}
}

static gboolean
cb_remove_allcells (gpointer key, gpointer value, gpointer flags)
{
	Cell *cell = value;
	cell_drop_dependencies (cell);

	cell->cell_flags &= ~CELL_IN_SHEET_LIST;
	cell->sheet = NULL;
	cell_destroy (cell);
	return TRUE;
}

void
sheet_destroy_contents (Sheet *sheet)
{
	/* Save these because they are going to get zeroed. */
	int const max_col = sheet->cols.max_used;
	int const max_row = sheet->rows.max_used;

	int i;
	gpointer tmp;

	/* Clear the row spans 1st */
	for (i = sheet->rows.max_used; i >= 0 ; --i)
		row_destroy_span (sheet_row_get (sheet, i));

	/* Remove any pending recalcs */
	eval_unqueue_sheet (sheet);

	/* Unlink expressions from the workbook expr list */
	sheet_expr_unlink (sheet);

	/* Remove all the cells */
	g_hash_table_foreach_remove (sheet->cell_hash, &cb_remove_allcells, NULL);

	/* Delete in ascending order to avoid decrementing max_used each time */
	for (i = 0; i <= max_col; ++i)
		sheet_col_destroy (sheet, i, FALSE);

	for (i = 0; i <= max_row; ++i)
		sheet_row_destroy (sheet, i, FALSE);
	
	/* Free segments too */
	for (i = COLROW_SEGMENT_INDEX (max_col); i >= 0 ; --i)
		if ((tmp = g_ptr_array_index (sheet->cols.info, i)) != NULL) {
			g_free (tmp);
			g_ptr_array_index (sheet->cols.info, i) = NULL;
		}
	for (i = COLROW_SEGMENT_INDEX (max_row); i >= 0 ; --i)
		if ((tmp = g_ptr_array_index (sheet->rows.info, i)) != NULL) {
			g_free (tmp);
			g_ptr_array_index (sheet->rows.info, i) = NULL;
		}
}

/**
 * sheet_destroy:
 * @sheet: the sheet to destroy
 *
 * Destroys a Sheet.
 *
 * Please note that you need to unattach this sheet before
 * calling this routine or you will get a warning.
 */
void
sheet_destroy (Sheet *sheet)
{
	GList *l;

	g_assert (sheet != NULL);
	g_return_if_fail (IS_SHEET (sheet));

	if (sheet->print_info) {
		print_info_free (sheet->print_info);
		sheet->print_info = NULL;
	}	

	if (sheet->objects) {
		g_warning ("Reminder: need to destroy SheetObjects");
	}

	sheet_selection_free (sheet);

	g_free (sheet->name_quoted);
	g_free (sheet->name_unquoted);
	g_free (sheet->solver_parameters.input_entry_str);

	for (l = sheet->sheet_views; l; l = l->next){
		SheetView *sheet_view = l->data;

		gtk_object_unref (GTK_OBJECT (sheet_view));
	}
	g_list_free (sheet->sheet_views);
	sheet->sheet_views = NULL;
	
	g_list_free (sheet->comment_list);
	sheet->comment_list = NULL;

#ifdef ENABLE_BONOBO
	sheet_vectors_shutdown (sheet);
#endif
	sheet_deps_destroy (sheet);
	expr_name_invalidate_refs_sheet (sheet);
	sheet_destroy_contents (sheet);
	sheet->names = expr_name_list_destroy (sheet->names);

	/* Clear the cliboard to avoid dangling references to the deleted sheet */
	if (sheet == application_clipboard_sheet_get ())
		application_clipboard_clear ();

	sheet_destroy_styles (sheet);

	g_hash_table_destroy (sheet->cell_hash);

	sheet->signature = 0;

	g_free (sheet->priv);
	g_free (sheet);
}

/*****************************************************************************/

/*
 * cb_empty_cell: A callback for sheet_cell_foreach_range
 *     removes/clear all of the cells in the specified region.
 *     Does NOT queue a redraw.
 */
static Value *
cb_empty_cell (Sheet *sheet, int col, int row, Cell *cell, gpointer flag)
{
	/* If there is a comment keep the cell */
	if (cell_has_comment(cell) && GPOINTER_TO_INT(flag))
		cell_set_value (cell, value_new_empty (), NULL);
	else
		sheet_cell_remove (sheet, cell, FALSE /* no redraw */);
	return NULL;
}

static Value *
cb_clear_cell_comments (Sheet *sheet, int col, int row, Cell *cell,
			void *user_data)
{
	cell_comment_destroy (cell);
	/* If the value is empty remove the cell.  It was only here to
	 * place hold for the cell
	 */
	if (!cell_has_expr(cell) && cell_is_blank (cell))
		sheet_cell_remove (sheet, cell, FALSE /* no redraw */);

	return NULL;
}

/**
 * sheet_clear_region:
 *
 * Clears are region of cells
 *
 * @clear_flags : If this is TRUE then styles are erased.
 *
 * We assemble a list of cells to destroy, since we will be making changes
 * to the structure being manipulated by the sheet_cell_foreach_range routine
 */
void
sheet_clear_region (CommandContext *context, Sheet *sheet,
		    int start_col, int start_row,
		    int end_col, int end_row,
		    int clear_flags)
{
	Range r;
	int min_col, max_col;

	g_return_if_fail (sheet != NULL);
	g_return_if_fail (IS_SHEET (sheet));
	g_return_if_fail (start_col <= end_col);
	g_return_if_fail (start_row <= end_row);

	r.start.col = start_col;
	r.start.row = start_row;
	r.end.col = end_col;
	r.end.row = end_row;

	if (clear_flags & CLEAR_VALUES && !(clear_flags & CLEAR_NOCHECKARRAY) &&
	    sheet_range_splits_array (sheet, &r)) {
		gnumeric_error_splits_array (context, _("Clear"));
		return;
	}

	/* Queue a redraw for cells being modified */
	if (clear_flags & (CLEAR_VALUES|CLEAR_FORMATS))
		sheet_redraw_cell_region (sheet,
					  start_col, start_row,
					  end_col, end_row);

	/* Clear the style in the region (new_default will ref the style for us). */
	if (clear_flags & CLEAR_FORMATS) {
		sheet_style_attach (sheet, r, mstyle_new_default ());
		sheet_range_calc_spans (sheet, r, SPANCALC_RE_RENDER|SPANCALC_RESIZE);
		rows_height_update (sheet, &r);
	}

	if (clear_flags & CLEAR_COMMENTS)
		sheet_cell_foreach_range (sheet, TRUE,
					  start_col, start_row,
					  end_col,   end_row,
					  cb_clear_cell_comments, NULL);

	min_col = start_col;
	max_col = end_col;

	if (clear_flags & CLEAR_VALUES) {
		int i, row, col[2];
		gboolean test[2];

		/* Remove or empty the cells depending on
		 * whether or not there are comments
		 */
		sheet_cell_foreach_range (sheet, TRUE,
					  start_col, start_row, end_col, end_row,
					  &cb_empty_cell,
					  GINT_TO_POINTER(!(clear_flags & CLEAR_COMMENTS)));

		/*
		 * Regen the spans from adjacent cells that may now be
		 * able to continue.
		 */
		test[0] = (start_col > 0);
		test[1] = (end_col < SHEET_MAX_ROWS-1);
		col[0] = start_col - 1;
		col[1] = end_col + 1;
		for (row = start_row; row <= end_row ; ++row) {
			ColRowInfo const *ri = sheet_row_get (sheet, row);

			if (ri == NULL) {
				/* skip segments with no cells */
				if (row == COLROW_SEGMENT_START (row)) {
					ColRowInfo const * const * const segment =
						COLROW_GET_SEGMENT(&(sheet->rows), row);
					if (segment == NULL)
						row = COLROW_SEGMENT_END(row);
				}
				continue;
			}

			for (i = 2 ; i-- > 0 ; ) {
				int left, right;
				CellSpanInfo const *span = NULL;
				Cell const *cell;

				if (!test[i])
					continue;

				cell = sheet_cell_get (sheet, col[i], ri->pos);
				if (cell == NULL) {
					span = row_span_get (ri, col[i]);
					if (span == NULL)
						continue;
					cell = span->cell;
				}

				cell_calc_span (cell, &left, &right);
				if (span) {
					if (left != span->left || right != span->right) {
						cell_unregister_span (cell);
						cell_register_span (cell, left, right);
					}
				} else if (left != right)
					cell_register_span (cell, left, right);

				/* We would not need to redraw the old span, just the new one */
				if (min_col > left)
					min_col = left;
				if (max_col < right)
					max_col = right;
			}
		}

		sheet_flag_status_update_range (sheet, &r);
	}

	/* Always redraw */
	sheet_redraw_cell_region (sheet,
				  min_col, start_row,
				  max_col, end_row);
}

/*****************************************************************************/

void
sheet_make_cell_visible (Sheet *sheet, int col, int row)
{
	GList *l;

	g_return_if_fail (sheet != NULL);
	g_return_if_fail (IS_SHEET (sheet));

	for (l = sheet->sheet_views; l; l = l->next){
		GnumericSheet *gsheet = GNUMERIC_SHEET_VIEW (l->data);

		gnumeric_sheet_make_cell_visible (gsheet, col, row, FALSE);
	}
}

void
sheet_set_edit_pos (Sheet *sheet, int col, int row)
{
	int old_col, old_row;

	g_return_if_fail (sheet != NULL);
	g_return_if_fail (IS_SHEET (sheet));

	old_col = sheet->cursor.edit_pos.col;
	old_row = sheet->cursor.edit_pos.row;

	if (old_col != col || old_row != row) {
		sheet->priv->edit_pos_changed = TRUE;

		/* Redraw before change */
		sheet_redraw_cell_region (sheet,
					  old_col, old_row, old_col, old_row);
		sheet->cursor.edit_pos.col = col;
		sheet->cursor.edit_pos.row = row;

		/* Redraw after change */
		sheet_redraw_cell_region (sheet, col, row, col, row);
	}
}

void
sheet_cursor_set (Sheet *sheet,
		  int edit_col, int edit_row,
		  int base_col, int base_row,
		  int move_col, int move_row)
{
	GList *l;

	g_return_if_fail (sheet != NULL);
	g_return_if_fail (IS_SHEET (sheet));

	/* FIXME : this should not be here */
	workbook_finish_editing (sheet->workbook, TRUE);

#if 0
	fprintf (stderr, "extend to %s%d\n", col_name(col), row+1);
	fprintf (stderr, "edit %s%d\n", col_name(sheet->cursor.edit_pos.col), sheet->cursor.edit_pos.row+1);
	fprintf (stderr, "base %s%d\n", col_name(sheet->cursor.base_corner.col), sheet->cursor.base_corner.row+1);
	fprintf (stderr, "move %s%d\n", col_name(sheet->cursor.move_corner.col), sheet->cursor.move_corner.row+1);
#endif

#if 0
	/* Be sure that the edit_pos is contained in the new_sel area */
	if (sheet->cursor.edit_pos.col < ss->user.start.col)
		sheet->cursor.edit_pos.col = ss->user.start.col;
	else if (sheet->cursor.edit_pos.col > ss->user.end.col)
		sheet->cursor.edit_pos.col = ss->user.end.col;

	if (sheet->cursor.edit_pos.row < ss->user.start.row)
		sheet->cursor.edit_pos.row = ss->user.start.row;
	else if (sheet->cursor.edit_pos.row > ss->user.end.row)
		sheet->cursor.edit_pos.row = ss->user.end.row;
#endif

	/* Change the edit position */
	sheet_set_edit_pos (sheet, edit_col, edit_row);

	sheet->cursor.base_corner.col = base_col;
	sheet->cursor.base_corner.row = base_row;
	sheet->cursor.move_corner.col = move_col;
	sheet->cursor.move_corner.row = move_row;

	for (l = sheet->sheet_views; l; l = l->next){
		GnumericSheet *gsheet = GNUMERIC_SHEET_VIEW (l->data);

		gnumeric_sheet_set_cursor_bounds ( gsheet,
			MIN (base_col, move_col),
			MIN (base_row, move_row),
			MAX (base_col, move_col),
			MAX (base_row, move_row));
	}
}

void
sheet_update_cursor_pos (Sheet const *sheet)
{
	GList *l;

	g_return_if_fail (sheet != NULL);
	g_return_if_fail (IS_SHEET (sheet));

	for (l = sheet->sheet_views; l; l = l->next) {
		SheetView *sheet_view = l->data;
		sheet_view_update_cursor_pos (sheet_view);
	}
}

void
sheet_hide_cursor (Sheet *sheet)
{
	GList *l;

	for (l = sheet->sheet_views; l; l = l->next){
		SheetView *sheet_view = l->data;

		sheet_view_hide_cursor (sheet_view);
	}
}

void
sheet_show_cursor (Sheet *sheet)
{
	GList *l;

	for (l = sheet->sheet_views; l; l = l->next){
		SheetView *sheet_view = l->data;

		sheet_view_show_cursor (sheet_view);
	}
}

/**
 * sheet_name_quote:
 * @name_unquoted: Unquoted name
 * 
 * Quotes the sheet name for use with sheet_new, sheet_rename
 * 
 * Return value: a safe sheet name.
 *
 **/
char *
sheet_name_quote (const char *name_unquoted)
{
	int         i, j, quotes_embedded = 0;
	gboolean    needs_quotes;
	static char quote_chr [] = { '=', '<', '>', '+', '-', ' ', '^', '&', '%', ':', '\0' };

	g_return_val_if_fail (name_unquoted != NULL, NULL);

	needs_quotes = isdigit (*name_unquoted);
	if (!needs_quotes)
		for (i = 0, quotes_embedded = 0; name_unquoted [i]; i++) {
			for (j = 0; quote_chr [j]; j++) 
				if (name_unquoted [i] == quote_chr [j])
					needs_quotes = TRUE;
			if (name_unquoted [i] == '"')
				quotes_embedded++;
		}

	if (needs_quotes) {
		int  len_quoted = strlen (name_unquoted) + quotes_embedded + 3;
		char  *ret = g_malloc (len_quoted);
		const char *src;
		char  *dst;

		*ret = '"';
		for (src = name_unquoted, dst = ret + 1; *src; src++, dst++) {
			if (*src == '"')
				*dst++ = '\\';
			*dst = *src;
		}
		*dst++ = '"';
		*dst = '\0';
		
		return ret;
	} else
		return g_strdup (name_unquoted);
}

void
sheet_mark_clean (Sheet *sheet)
{
	g_return_if_fail (sheet != NULL);
	g_return_if_fail (IS_SHEET (sheet));

	if (sheet->modified)
		sheet->pristine = FALSE;

	sheet->modified = FALSE;
}

void
sheet_set_dirty (Sheet *sheet, gboolean is_dirty)
{
	g_return_if_fail (sheet != NULL);
	g_return_if_fail (IS_SHEET (sheet));

	if (sheet->modified)
		sheet->pristine = FALSE;

	sheet->modified = is_dirty;
}

/**
 * sheet_is_pristine:
 * @sheet: 
 * 
 * Sees if the sheet has ever been touched.
 * 
 * Return value: TRUE if it is perfectly clean.
 **/
gboolean
sheet_is_pristine (Sheet *sheet)
{
	g_return_val_if_fail (sheet != NULL, FALSE);
	g_return_val_if_fail (IS_SHEET (sheet), FALSE);

	return sheet->pristine && !sheet->modified;
}

/**
 * sheet_lookup_by_name:
 * @name:  a sheet name.
 *
 * This routine parses @name for a reference to another sheet
 * in this workbook.  If this fails, it will try to parse a
 * filename in @name and load the given workbook and lookup
 * the sheet name from that workbook.
 *
 * The routine might return NULL.
 */
Sheet *
sheet_lookup_by_name (Workbook *wb, const char *name)
{
	Sheet *sheet;

	g_return_val_if_fail (wb != NULL, NULL);

	/*
	 * FIXME: currently we only try to lookup the sheet name
	 * inside the workbook, we need to lookup external files as
	 * well.
	 */
	sheet = workbook_sheet_lookup (wb, name);

	if (sheet)
		return sheet;

	return NULL;
}

/****************************************************************************/

/*
 * Callback for sheet_cell_foreach_range to remove a cell and
 * put it in a temporary list.
 */
static Value *
cb_collect_cell (Sheet *sheet, int col, int row, Cell *cell,
		 void *user_data)
{
	GList ** l = user_data;

	sheet_cell_remove_from_hash (sheet, cell);
	*l = g_list_prepend (*l, cell);
	return NULL;
}

/*
 * Callback for sheet_cell_foreach_range to test whether a cell is in an
 * array-formula to the right of the leftmost column.
 */
static Value *
avoid_dividing_array_horizontal (Sheet *sheet, int col, int row, Cell *cell,
				 void *user_data)
{
	if (cell_is_array (cell) && cell->u.expression->array.x > 0)
		return value_terminate ();
	return NULL;
}

/*
 * Callback for sheet_cell_foreach_range to test whether a cell is in an
 * array-formula below the top line.
 */
static Value *
avoid_dividing_array_vertical (Sheet *sheet, int col, int row, Cell *cell,
			       void *user_data)
{
	if (cell_is_array (cell) && cell->u.expression->array.y > 0)
		return value_terminate ();
	return NULL;
}

/*
 * 1) collects the cells in the source rows/cols
 * 2) Moves the headers to their new location
 * 3) replaces the cells in their new location
 */
static void
colrow_move (Sheet *sheet,
	     int start_col, int start_row,
	     int end_col, int end_row,
	     ColRowCollection *info_collection,
	     int const old_pos, int const new_pos)
{
	ColRowInfo **segment = COLROW_GET_SEGMENT(info_collection, old_pos);
	ColRowInfo *info = (segment != NULL) ?
		segment[COLROW_SUB_INDEX(old_pos)] : NULL;

	GList *cells = NULL;
	Cell  *cell;

	g_return_if_fail (old_pos >= 0);
	g_return_if_fail (new_pos >= 0);

	if (info == NULL)
		return;

	/* Collect the cells */
	sheet_cell_foreach_range (sheet, TRUE,
				  start_col, start_row,
				  end_col, end_row,
				  &cb_collect_cell, &cells);

	/* Reverse the list so that we start at the top left
	 * which makes things easier for arrays.
	 */
	cells = g_list_reverse (cells);

	/* Update the position */
	segment [COLROW_SUB_INDEX (old_pos)] = NULL;
	info->pos = new_pos;

	/* TODO : Figure out a way to merge these functions */
	if (info_collection == &sheet->cols)
		sheet_col_add (sheet, info);
	else
		sheet_row_add (sheet, info);

	/* Insert the cells back */
	for (; cells != NULL ; cells = g_list_remove (cells, cell)) {
		cell = cells->data;

		sheet_cell_add_to_hash (sheet, cell);
		cell_relocate (cell, NULL);
		cell_content_changed (cell);
	}
}

/**
 * sheet_insert_cols:
 * @sheet   The sheet
 * @col     At which position we want to insert
 * @count   The number of columns to be inserted
 */
gboolean
sheet_insert_cols (CommandContext *context, Sheet *sheet,
		   int col, int count, GSList **reloc_storage)
{
	ExprRelocateInfo reloc_info;
	int   i;

	g_return_val_if_fail (reloc_storage != NULL, TRUE);

	*reloc_storage = NULL;

	g_return_val_if_fail (sheet != NULL, TRUE);
	g_return_val_if_fail (IS_SHEET (sheet), TRUE);
	g_return_val_if_fail (count != 0, TRUE);

	/* 0. Walk cells in displaced col and ensure arrays aren't divided. */
	if (col > 0)	/* No need to test leftmost column */
		if (sheet_cell_foreach_range (sheet, TRUE, col, 0,
					      col, SHEET_MAX_ROWS-1,
					      &avoid_dividing_array_horizontal,
					      NULL) != NULL){
			gnumeric_error_splits_array (context, _("Insert Columns"));
			return TRUE;
		}

	/* Walk the right edge to make sure nothing is split due to over run.  */
	if (sheet_cell_foreach_range (sheet, TRUE, SHEET_MAX_COLS-count, 0,
				      SHEET_MAX_COLS-1, SHEET_MAX_ROWS-1,
				      &avoid_dividing_array_horizontal,
				      NULL) != NULL){
		gnumeric_error_splits_array (context, _("Insert Columns"));
		return TRUE;
	}

	/* 1. Delete all columns (and their cells) that will fall off the end */
	for (i = sheet->cols.max_used; i >= SHEET_MAX_COLS - count ; --i)
		sheet_col_destroy (sheet, i, TRUE);

	/* 2. Fix references to and from the cells which are moving */
	reloc_info.origin.start.col = col;
	reloc_info.origin.start.row = 0;
	reloc_info.origin.end.col = SHEET_MAX_COLS-1;
	reloc_info.origin.end.row = SHEET_MAX_ROWS-1;
	reloc_info.origin_sheet = reloc_info.target_sheet = sheet;
	reloc_info.col_offset = count;
	reloc_info.row_offset = 0;
	*reloc_storage = workbook_expr_relocate (sheet->workbook, &reloc_info);

	/* 3. Move the columns to their new location (From right to left) */
	for (i = sheet->cols.max_used; i >= col ; --i)
		colrow_move (sheet, i, 0, i, SHEET_MAX_ROWS-1,
			     &sheet->cols, i, i + count);

	/* 4. Slide all the StyleRegion's right */
	sheet_style_insert_colrow (sheet, col, count, TRUE);

	/* 5. Recompute dependencies */
	sheet_recalc_dependencies (sheet);

	/* 6. Notify sheet of pending updates */
	sheet->priv->recompute_visibility = TRUE;
	sheet->priv->recompute_spans = TRUE;
	sheet_flag_status_update_range (sheet, &reloc_info.origin);
	if (sheet->priv->reposition_col_comment > col)
		sheet->priv->reposition_col_comment = col;

	return FALSE;
}

/*
 * sheet_delete_cols
 * @sheet   The sheet
 * @col     At which position we want to start deleting columns
 * @count   The number of columns to be deleted
 */
gboolean
sheet_delete_cols (CommandContext *context, Sheet *sheet,
		   int col, int count, GSList **reloc_storage)
{
	ExprRelocateInfo reloc_info;
	int i;

	g_return_val_if_fail (reloc_storage != NULL, TRUE);
	
	*reloc_storage = NULL;

	g_return_val_if_fail (sheet != NULL, TRUE);
	g_return_val_if_fail (IS_SHEET (sheet), TRUE);
	g_return_val_if_fail (count != 0, TRUE);

	reloc_info.origin.start.col = col;
	reloc_info.origin.start.row = 0;
	reloc_info.origin.end.col = col + count - 1;
	reloc_info.origin.end.row = SHEET_MAX_ROWS-1;
	reloc_info.origin_sheet = reloc_info.target_sheet = sheet;
	reloc_info.col_offset = SHEET_MAX_COLS; /* send them to infinity */
	reloc_info.row_offset = SHEET_MAX_ROWS; /*   to force invalidation */

	/* 0. Walk cells in deleted cols and ensure arrays aren't divided. */
	if (sheet_range_splits_array (sheet, &reloc_info.origin)) {
		gnumeric_error_splits_array (context, _("Delete Columns"));
		return TRUE;
	}

	/* 1. Delete all columns (and their cells) that will fall off the end */
	for (i = col + count ; --i >= col; )
		sheet_col_destroy (sheet, i, TRUE);

	/* 2. Invalidate references to the cells in the delete columns */
	*reloc_storage = workbook_expr_relocate (sheet->workbook, &reloc_info);

	/* 3. Fix references to and from the cells which are moving */
	reloc_info.origin.start.col = col+count;
	reloc_info.origin.end.col = SHEET_MAX_COLS-1;
	reloc_info.col_offset = -count;
	reloc_info.row_offset = 0;
	*reloc_storage = g_slist_concat (*reloc_storage,
					 workbook_expr_relocate (sheet->workbook,
								 &reloc_info));

	/* 4. Move the columns to their new location (from left to right) */
	for (i = col + count ; i <= sheet->cols.max_used; ++i)
		colrow_move (sheet, i, 0, i, SHEET_MAX_ROWS-1,
			     &sheet->cols, i, i-count);

	/* 5. Slide all the StyleRegion's up */
	sheet_style_delete_colrow (sheet, col, count, TRUE);

	/* 6. Recompute dependencies */
	sheet_recalc_dependencies (sheet);

	/* 7. Notify sheet of pending updates */
	sheet->priv->recompute_visibility = TRUE;
	sheet->priv->recompute_spans = TRUE;
	sheet_flag_status_update_range (sheet, &reloc_info.origin);
	if (sheet->priv->reposition_col_comment > col)
		sheet->priv->reposition_col_comment = col;

	return FALSE;
}

/**
 * sheet_insert_rows:
 * @sheet   The sheet
 * @row     At which position we want to insert
 * @count   The number of rows to be inserted
 */
gboolean
sheet_insert_rows (CommandContext *context, Sheet *sheet,
		   int row, int count, GSList **reloc_storage)
{
	ExprRelocateInfo reloc_info;
	int   i;

	g_return_val_if_fail (reloc_storage != NULL, TRUE);
	
	*reloc_storage = NULL;

	g_return_val_if_fail (sheet != NULL, TRUE);
	g_return_val_if_fail (IS_SHEET (sheet), TRUE);
	g_return_val_if_fail (count != 0, TRUE);

	/* 0. Walk cells in displaced row and ensure arrays aren't divided. */
	if (row > 0)	/* No need to test leftmost column */
		if (sheet_cell_foreach_range (sheet, TRUE,
					      0, row,
					      SHEET_MAX_COLS-1, row,
					      &avoid_dividing_array_vertical,
					      NULL) != NULL) {
			gnumeric_error_splits_array (context, _("Insert Rows"));
			return TRUE;
		}

	/* Walk the lower edge to make sure nothing is split due to over run.  */
	if (sheet_cell_foreach_range (sheet, TRUE, 0, SHEET_MAX_ROWS-count,
				      SHEET_MAX_COLS-1, SHEET_MAX_ROWS-1,
				      &avoid_dividing_array_vertical,
				      NULL) != NULL){
		gnumeric_error_splits_array (context, _("Insert Rows"));
		return TRUE;
	}

	/* 1. Delete all rows (and their cells) that will fall off the end */
	for (i = sheet->rows.max_used; i >= SHEET_MAX_ROWS - count ; --i)
		sheet_row_destroy (sheet, i, TRUE);

	/* 2. Fix references to and from the cells which are moving */
	reloc_info.origin.start.col = 0;
	reloc_info.origin.start.row = row;
	reloc_info.origin.end.col = SHEET_MAX_COLS-1;
	reloc_info.origin.end.row = SHEET_MAX_ROWS-1;
	reloc_info.origin_sheet = reloc_info.target_sheet = sheet;
	reloc_info.col_offset = 0;
	reloc_info.row_offset = count;
	*reloc_storage = workbook_expr_relocate (sheet->workbook, &reloc_info);

	/* 3. Move the rows to their new location (from bottom to top) */
	for (i = sheet->rows.max_used; i >= row ; --i)
		colrow_move (sheet, 0, i, SHEET_MAX_COLS-1, i,
			     &sheet->rows, i, i+count);

	/* 4. Slide all the StyleRegion's right */
	sheet_style_insert_colrow (sheet, row, count, FALSE);

	/* 5. Recompute dependencies */
	sheet_recalc_dependencies (sheet);

	/* 6. Notify sheet of pending updates */
	sheet->priv->recompute_visibility = TRUE;
	sheet->priv->recompute_spans = TRUE;
	sheet_flag_status_update_range (sheet, &reloc_info.origin);
	if (sheet->priv->reposition_row_comment > row)
		sheet->priv->reposition_row_comment = row;

	return FALSE;
}

/*
 * sheet_delete_rows
 * @sheet   The sheet
 * @row     At which position we want to start deleting rows
 * @count   The number of rows to be deleted
 */
gboolean
sheet_delete_rows (CommandContext *context, Sheet *sheet,
		   int row, int count, GSList **reloc_storage)
{
	ExprRelocateInfo reloc_info;
	int i;

	g_return_val_if_fail (reloc_storage != NULL, TRUE);
	
	*reloc_storage = NULL;

	g_return_val_if_fail (sheet != NULL, TRUE);
	g_return_val_if_fail (IS_SHEET (sheet), TRUE);
	g_return_val_if_fail (count != 0, TRUE);

	reloc_info.origin.start.col = 0;
	reloc_info.origin.start.row = row;
	reloc_info.origin.end.col = SHEET_MAX_COLS-1;
	reloc_info.origin.end.row = row + count - 1;
	reloc_info.origin_sheet = reloc_info.target_sheet = sheet;
	reloc_info.col_offset = SHEET_MAX_COLS; /* send them to infinity */
	reloc_info.row_offset = SHEET_MAX_ROWS; /*   to force invalidation */

	/* 0. Walk cells in deleted rows and ensure arrays aren't divided. */
	if (sheet_range_splits_array (sheet, &reloc_info.origin)) {
		gnumeric_error_splits_array (context, _("Delete Rows"));
		return TRUE;
	}

	/* 1. Delete all cols (and their cells) that will fall off the end */
	for (i = row + count ; --i >= row; )
		sheet_row_destroy (sheet, i, TRUE);

	/* 2. Invalidate references to the cells in the delete columns */
	*reloc_storage = workbook_expr_relocate (sheet->workbook, &reloc_info);

	/* 3. Fix references to and from the cells which are moving */
	reloc_info.origin.start.row = row + count;
	reloc_info.origin.end.row = SHEET_MAX_ROWS-1;
	reloc_info.col_offset = 0;
	reloc_info.row_offset = -count;
	*reloc_storage = g_slist_concat (*reloc_storage,
					 workbook_expr_relocate (sheet->workbook,
								 &reloc_info));

	/* 4. Move the rows to their new location (from top to bottom) */
	for (i = row + count ; i <= sheet->rows.max_used; ++i)
		colrow_move (sheet, 0, i, SHEET_MAX_COLS-1, i,
			     &sheet->rows, i, i-count);

	/* 5. Slide all the StyleRegion's up */
	sheet_style_delete_colrow (sheet, row, count, FALSE);

	/* 6. Recompute dependencies */
	sheet_recalc_dependencies (sheet);

	/* 7. Notify sheet of pending update */
	sheet->priv->recompute_visibility = TRUE;
	sheet->priv->recompute_spans = TRUE;
	sheet_flag_status_update_range (sheet, &reloc_info.origin);
	if (sheet->priv->reposition_row_comment > row)
		sheet->priv->reposition_row_comment = row;

	return FALSE;
}

void
sheet_move_range (CommandContext *context,
		  ExprRelocateInfo const *rinfo,
		  GSList **reloc_storage)
{
	GList *cells = NULL;
	Cell  *cell;
	Range  dst;
	gboolean inter_sheet_expr, out_of_range;

	g_return_if_fail (rinfo != NULL);
	g_return_if_fail (rinfo->origin_sheet != NULL);
	g_return_if_fail (IS_SHEET (rinfo->origin_sheet));
	g_return_if_fail (rinfo->target_sheet != NULL);
	g_return_if_fail (IS_SHEET (rinfo->target_sheet));

	dst = rinfo->origin;
	out_of_range = range_translate (&dst,
					rinfo->col_offset, rinfo->row_offset);

	/* Redraw the src region in case anything was spanning */
	sheet_redraw_cell_region (rinfo->origin_sheet,
				  rinfo->origin.start.col,
				  rinfo->origin.start.row,
				  rinfo->origin.end.col,
				  rinfo->origin.end.row);

	/* 1. invalidate references to any cells in the destination range that
	 * are not shared with the src.  This must be done before the references
	 * to from the src range are adjusted because they will point into
	 * the destinatin.
	 */
	*reloc_storage = NULL;
	if (!out_of_range) {
		GList *invalid;
		ExprRelocateInfo reloc_info;

		/*
		 * we DO need to be careful about invalidating references to the old
		 * content of the destination region.  We only invalidate references
		 * to regions that are actually lost.
		 *
		 * Handle dst cells being pasted over
		 */
		if (range_overlap (&rinfo->origin, &dst)) {
			invalid = range_split_ranges (&rinfo->origin, &dst, NULL);
		} else {
			Range *r = g_new (Range, 1);
			*r = dst;
			invalid = g_list_append (NULL, r);
		}

		reloc_info.origin_sheet = reloc_info.target_sheet = rinfo->target_sheet;;
		reloc_info.col_offset = SHEET_MAX_COLS; /* send to infinity */
		reloc_info.row_offset = SHEET_MAX_ROWS; /*   to force invalidation */

		while (invalid) {
			Range *r = invalid->data;
			invalid = g_list_remove (invalid, r);
			if (!range_overlap (r, &rinfo->origin)) {
				reloc_info.origin = *r;
				*reloc_storage = g_slist_concat (*reloc_storage,
					workbook_expr_relocate (rinfo->target_sheet->workbook, &reloc_info));
			}
			g_free (r);
		}

		/*
		 * DO NOT handle src cells moving out the bounds.
		 * that is handled elsewhere.
		 */
	}

	/* 2. Fix references to and from the cells which are moving */
	*reloc_storage = g_slist_concat (*reloc_storage,
		workbook_expr_relocate (rinfo->origin_sheet->workbook, rinfo));

	/* 3. Collect the cells */
	sheet_cell_foreach_range (rinfo->origin_sheet, TRUE,
				  rinfo->origin.start.col,
				  rinfo->origin.start.row,
				  rinfo->origin.end.col,
				  rinfo->origin.end.row,
				  &cb_collect_cell, &cells);
	/* Reverse list so that we start at the top left (simplifies arrays). */
	cells = g_list_reverse (cells);

	/* 4. Clear the target area & invalidate references to it */
	if (!out_of_range)
		/*
		 * we can clear content but not styles from the destination
		 * region without worrying if it overlaps with the source,
		 * because we have already extracted the content.
		 */
		sheet_clear_region (context, rinfo->target_sheet, 
				    dst.start.col, dst.start.row,
				    dst.end.col, dst.end.row,
				    CLEAR_VALUES|CLEAR_COMMENTS); /* Do not to clear styles */

	/* 5. Slide styles BEFORE the cells so that spans get computed properly */
	sheet_style_relocate (rinfo);

	/* 6. Insert the cells back */
	for (; cells != NULL ; cells = g_list_remove (cells, cell)) {
		cell = cells->data;

		/* check for out of bounds and delete if necessary */
		if ((cell->col_info->pos + rinfo->col_offset) >= SHEET_MAX_COLS ||
		    (cell->row_info->pos + rinfo->row_offset) >= SHEET_MAX_ROWS) {
			if (cell_has_expr (cell))
				sheet_cell_expr_unlink (cell);
			cell_unrealize (cell);
			cell_destroy (cell);
			continue;
		}

		/* Inter sheet movement requires the moving the expression too */
		inter_sheet_expr  = (cell->sheet != rinfo->target_sheet &&
				     cell_has_expr (cell));
		if (inter_sheet_expr)
			sheet_cell_expr_unlink (cell);

		/* Update the location */
		sheet_cell_insert (rinfo->target_sheet, cell,
				   cell->col_info->pos + rinfo->col_offset,
				   cell->row_info->pos + rinfo->row_offset);

		if (inter_sheet_expr)
			sheet_cell_expr_link (cell);

		/* Move comments */
		cell_relocate (cell, NULL);
		cell_content_changed (cell);
	}

	/* 7. Recompute dependencies */
	sheet_recalc_dependencies (rinfo->target_sheet);

	/* 8. Notify sheet of pending update */
	rinfo->origin_sheet->priv->recompute_spans = TRUE;
	sheet_flag_status_update_range (rinfo->origin_sheet, &rinfo->origin);
}

static void
col_row_info_init (Sheet *sheet, double pts, int margin_a, int margin_b,
		   gboolean is_horizontal)
{
	ColRowInfo *cri = is_horizontal
	    ? &sheet->cols.default_style
	    : &sheet->rows.default_style;

	cri->pos = -1;
	cri->margin_a = margin_a;
	cri->margin_b = margin_b;
	cri->hard_size = FALSE;
	cri->visible = TRUE;
	cri->spans = NULL;
	cri->size_pts = pts;
	colrow_compute_pixels_from_pts (sheet, cri, is_horizontal);
}

/************************************************************************/
/* Col width support routines.
 */

/**
 * sheet_col_get_distance_pixels:
 *
 * Return the number of pixels between from_col to to_col
 * measured from the upper left corner.
 */
int
sheet_col_get_distance_pixels (Sheet const *sheet, int from, int to)
{
	int i, pixels = 0;
	int sign = 1;

	g_assert (sheet != NULL);

	if (from > to) {
		int const tmp = to;
		to = from;
		from = tmp;
		sign = -1;
	}

	/* Do not use col_row_foreach, it ignores empties */
	for (i = from ; i < to ; ++i) {
		ColRowInfo const *ci = sheet_col_get_info (sheet, i);
		if (ci->visible)
			pixels += ci->size_pixels;
	}

	return pixels*sign;
}

/**
 * sheet_col_get_distance_pts:
 *
 * Return the number of points between from_col to to_col
 * measured from the upper left corner.
 */
double
sheet_col_get_distance_pts (Sheet const *sheet, int from, int to)
{
	double units = 0;
	int i;
	int sign = 1;

	g_assert (sheet != NULL);

	if (from > to) {
		int const tmp = to;
		to = from;
		from = tmp;
		sign = -1;
	}

	/* Do not use col_row_foreach, it ignores empties */
	for (i = from ; i < to ; ++i) {
		ColRowInfo const *ci = sheet_col_get_info (sheet, i);
		if (ci->visible)
			units += ci->size_pts;
	}
	
	return units*sign;
}

/**
 * sheet_col_set_size_pts:
 * @sheet:	 The sheet
 * @col:	 The col
 * @widtht_pts:	 The desired widtht in pts
 * @set_by_user: TRUE if this was done by a user (ie, user manually
 *               set the width)
 *
 * Sets width of a col in pts, INCLUDING left and right margins, and the far
 * grid line.  This is a low level internal routine.  It does NOT redraw,
 * or reposition objects.
 */
void
sheet_col_set_size_pts (Sheet *sheet, int col, double width_pts,
			 gboolean set_by_user)
{
	ColRowInfo *ci;
	
	g_return_if_fail (sheet != NULL);
	g_return_if_fail (IS_SHEET (sheet));
	g_return_if_fail (width_pts > 0.0);

	ci = sheet_col_fetch (sheet, col);
	if (ci->size_pts == width_pts)
		return;

	ci->hard_size |= set_by_user;
	ci->size_pts = width_pts;
	colrow_compute_pixels_from_pts (sheet, ci, TRUE);

	sheet->priv->recompute_visibility = TRUE;
	sheet->priv->recompute_spans = TRUE;
	if (sheet->priv->reposition_col_comment > col)
		sheet->priv->reposition_col_comment = col;
}

void
sheet_col_set_size_pixels (Sheet *sheet, int col, int width_pixels,
			   gboolean set_by_user)
{
	ColRowInfo *ci;

	g_return_if_fail (sheet != NULL);
	g_return_if_fail (IS_SHEET (sheet));
	g_return_if_fail (width_pixels > 0.0);

	ci = sheet_col_fetch (sheet, col);
	if (ci->size_pixels == width_pixels)
		return;

	ci->hard_size |= set_by_user;
	ci->size_pixels = width_pixels;
	colrow_compute_pts_from_pixels (sheet, ci, TRUE);

	sheet->priv->recompute_visibility = TRUE;
	sheet->priv->recompute_spans = TRUE;
	if (sheet->priv->reposition_col_comment > col)
		sheet->priv->reposition_col_comment = col;
}

/**
 * sheet_col_get_default_size_pts:
 *
 * Return the default number of pts in a column, including margins.
 * This function returns the raw sum, no rounding etc.
 */
double
sheet_col_get_default_size_pts (Sheet const *sheet)
{
	ColRowInfo const *ci;

	g_assert (sheet != NULL);
 
	ci = &sheet->cols.default_style;
	return  ci->size_pts;
}

void
sheet_col_set_default_size_pts (Sheet *sheet, double width_pts)
{
	col_row_info_init (sheet, width_pts, 2, 2, TRUE);
}

/**************************************************************************/
/* Row height support routines
 */

/**
 * sheet_row_get_distance_pixels:
 *
 * Return the number of pixels between from_row to to_row
 * measured from the upper left corner.
 */
int
sheet_row_get_distance_pixels (Sheet const *sheet, int from, int to)
{
	int const default_size = sheet->rows.default_style.size_pixels;
	int pixels = 0, sign = 1;
	int i;

	g_assert (sheet != NULL);

	if (from > to) {
		int const tmp = to;
		to = from;
		from = tmp;
		sign = -1;
	}

	g_return_val_if_fail (from >= 0, 1.);
	g_return_val_if_fail (to <= SHEET_MAX_ROWS, 1.);

	/* Do not use col_row_foreach, it ignores empties.
	 * Optimize this so that long jumps are not quite so horrific
	 * for performance.
	 */
	for (i = from ; i < to ; ++i) {
		ColRowInfo const * const * const segment =
			COLROW_GET_SEGMENT(&(sheet->rows), i);

		if (segment != NULL) {
			ColRowInfo const *ri = segment[COLROW_SUB_INDEX(i)];
			if (ri == NULL)
				pixels += default_size;
			else if (ri->visible)
				pixels += ri->size_pixels;
		} else {
			int segment_end = COLROW_SEGMENT_END(i)+1;
			if (segment_end > to)
				segment_end = to;
			pixels += default_size * (segment_end - i);
			i = segment_end-1;
		}
	}

	return pixels*sign;
}

/**
 * sheet_row_get_distance_pts:
 *
 * Return the number of points between from_row to to_row
 * measured from the upper left corner.
 */
double
sheet_row_get_distance_pts (Sheet const *sheet, int from, int to)
{
	double const default_size = sheet->rows.default_style.size_pts;
	double pts = 0., sign = 1.;
	int i;

	g_assert (sheet != NULL);

	if (from > to) {
		int const tmp = to;
		to = from;
		from = tmp;
		sign = -1.;
	}

	g_return_val_if_fail (from >= 0, 1.);
	g_return_val_if_fail (to <= SHEET_MAX_ROWS, 1.);

	/* Do not use col_row_foreach, it ignores empties.
	 * Optimize this so that long jumps are not quite so horrific
	 * for performance.
	 */
	for (i = from ; i < to ; ++i) {
		ColRowInfo const * const * const segment =
			COLROW_GET_SEGMENT(&(sheet->rows), i);

		if (segment != NULL) {
			ColRowInfo const *ri = segment[COLROW_SUB_INDEX(i)];
			if (ri == NULL)
				pts += default_size;
			else if (ri->visible)
				pts += ri->size_pts;
		} else {
			int segment_end = COLROW_SEGMENT_END(i)+1;
			if (segment_end > to)
				segment_end = to;
			pts += default_size * (segment_end - i);
			i = segment_end-1;
		}
	}

	return pts*sign;
}

/**
 * sheet_row_set_size_pts:
 * @sheet:	 The sheet
 * @row:	 The row
 * @height_pts:	 The desired height in pts
 * @set_by_user: TRUE if this was done by a user (ie, user manually
 *               set the height)
 *
 * Sets height of a row in pts, INCLUDING top and bottom margins, and the lower
 * grid line.  This is a low level internal routine.  It does NOT redraw,
 * or reposition objects.
 */
void
sheet_row_set_size_pts (Sheet *sheet, int row, double height_pts,
			gboolean set_by_user)
{
	ColRowInfo *ri;
	
	g_return_if_fail (sheet != NULL);
	g_return_if_fail (IS_SHEET (sheet));
	g_return_if_fail (height_pts > 0.0);

	ri = sheet_row_fetch (sheet, row);
	if (ri->size_pts == height_pts)
		return;

	ri->hard_size |= set_by_user;
	ri->size_pts = height_pts;
	colrow_compute_pixels_from_pts (sheet, ri, FALSE);

	sheet->priv->recompute_visibility = TRUE;
	if (sheet->priv->reposition_row_comment > row)
		sheet->priv->reposition_row_comment = row;
}

/**
 * sheet_row_set_size_pixels:
 * @sheet:	 The sheet
 * @row:	 The row
 * @height:	 The desired height
 * @set_by_user: TRUE if this was done by a user (ie, user manually
 *                      set the width)
 *
 * Sets height of a row in pixels, INCLUDING top and bottom margins, and the lower
 * grid line.
 *
 * FIXME : This should not be calling redraw or its relatives.
 *         We should store the fact that objects need moving and take care of
 *         that in redraw.
 */
void
sheet_row_set_size_pixels (Sheet *sheet, int row, int height_pixels,
			   gboolean set_by_user)
{
	ColRowInfo *ri;

	g_return_if_fail (sheet != NULL);
	g_return_if_fail (IS_SHEET (sheet));
	g_return_if_fail (height_pixels > 0);

	ri = sheet_row_fetch (sheet, row);
	if (ri->size_pixels == height_pixels)
		return;

	ri->hard_size |= set_by_user;
	ri->size_pixels = height_pixels;
	colrow_compute_pts_from_pixels (sheet, ri, FALSE);

	sheet->priv->recompute_visibility = TRUE;
	if (sheet->priv->reposition_row_comment > row)
		sheet->priv->reposition_row_comment = row;
}

/**
 * sheet_row_get_default_size_pts:
 *
 * Return the default number of units in a row, including margins.
 * This function returns the raw sum, no rounding etc.
 */
double
sheet_row_get_default_size_pts (Sheet const *sheet)
{
	ColRowInfo const *ci;

	g_assert (sheet != NULL);
 
	ci = &sheet->rows.default_style;
	return  ci->size_pts;
}

void
sheet_row_set_default_size_pts (Sheet *sheet, double height_pts,
				gboolean thick_a, gboolean thick_b)
{
	/* There are an addition few pixels above due the the fonts ascent */
	/* Why XL chooses to be asymetric I don't know */
	int const a = thick_a ? 2 : 1;
	int const b = thick_b ? 1 : 0;
	col_row_info_init (sheet, height_pts, a, b, FALSE);
}

void
sheet_cell_changed (Cell *cell)
{
#ifdef ENABLE_BONOBO
	sheet_vectors_cell_changed (cell);
#endif
}

void
sheet_create_edit_cursor (Sheet *sheet)
{
	GList *l;

	for (l = sheet->sheet_views; l; l = l->next){
		GnumericSheet *gsheet = GNUMERIC_SHEET_VIEW (l->data);
		gnumeric_sheet_create_editing_cursor (gsheet);
	}
}
void
sheet_stop_editing (Sheet *sheet)
{
	GList *l;
	for (l = sheet->sheet_views; l; l = l->next){
		GnumericSheet *gsheet = GNUMERIC_SHEET_VIEW (l->data);

		gnumeric_sheet_stop_editing (gsheet);
	}
}

void
sheet_destroy_cell_select_cursor (Sheet *sheet, gboolean clear_string)
{
	GList *l;
	for (l = sheet->sheet_views; l; l = l->next){
		GnumericSheet *gsheet = GNUMERIC_SHEET_VIEW (l->data);

		gnumeric_sheet_stop_cell_selection (gsheet, clear_string);
	}
}
