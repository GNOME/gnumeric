/*
 * Sheet.c:  Implements the sheet management and per-sheet storage
 *
 * Author:
 *  Miguel de Icaza (miguel@gnu.org)
 *  Jody Goldberg (jgoldbeg@home.com)
 */
#include <config.h>
#include <ctype.h>
#include <gnome.h>
#include <string.h>
#include "gnumeric.h"
#include "command-context.h"
#include "sheet-control-gui.h"
#include "workbook-control.h"
#include "workbook-view.h"
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
#include "commands.h"
#include "cellspan.h"
#include "dependent.h"
#include "sheet-private.h"
#include "expr-name.h"
#include "rendered-value.h"
#include "sheet-object.h"
#include "sheet-object-impl.h"
#include "sheet-object-cell-comment.h"

#define GNUMERIC_SHEET_CONTROL_GUI(p) GNUMERIC_SHEET (SHEET_CONTROL_GUI(p)->canvas)

static void sheet_redraw_partial_row (Sheet const *sheet, int const row,
				      int const start_col, int const end_col);

void
sheet_adjust_preferences (Sheet const *sheet)
{
	SHEET_FOREACH_CONTROL (sheet, control,
		sheet_view_adjust_preferences (control););
}

void
sheet_redraw_all (Sheet const *sheet)
{
	SHEET_FOREACH_CONTROL (sheet, control,
		sheet_view_redraw_all (control););
}

void
sheet_redraw_headers (Sheet const *sheet,
		      gboolean col, gboolean row,
		      Range const *r /* optional == NULL */)
{
	SHEET_FOREACH_CONTROL (sheet, control,
		sheet_view_redraw_headers (control, col, row, r););
}

static guint
cellpos_hash (CellPos const *key)
{
	return (key->row << 8) | key->col;
}

static gint
cellpos_cmp (CellPos const * a, CellPos const * b)
{
	return (a->row == b->row && a->col == b->col);
}

void
sheet_rename (Sheet *sheet, const char *new_name)
{
	g_return_if_fail (IS_SHEET (sheet));
	g_return_if_fail (new_name != NULL);

	g_free (sheet->name_quoted);
	g_free (sheet->name_unquoted);
	sheet->name_unquoted = g_strdup (new_name);
	sheet->name_quoted = sheet_name_quote (new_name);
}

SheetControlGUI *
sheet_new_sheet_view (Sheet *sheet)
{
	GtkWidget *sheet_view;
	int base_col, base_row, move_col, move_row;

	g_return_val_if_fail (IS_SHEET (sheet), NULL);

	sheet_view = sheet_view_new (sheet);

	base_col = sheet->cursor.base_corner.col;
	base_row = sheet->cursor.base_corner.row;
	move_col = sheet->cursor.move_corner.col;
	move_row = sheet->cursor.move_corner.row;

	gnumeric_sheet_set_cursor_bounds (GNUMERIC_SHEET_CONTROL_GUI (sheet_view),
		MIN (base_col, move_col),
		MIN (base_row, move_row),
		MAX (base_col, move_col),
		MAX (base_row, move_row));
	sheet->s_controls = g_list_prepend (sheet->s_controls, sheet_view);

	return SHEET_CONTROL_GUI (sheet_view);
}

void
sheet_detach_sheet_view (SheetControlGUI *sheet_view)
{
	g_return_if_fail (IS_SHEET_CONTROL_GUI (sheet_view));
	g_return_if_fail (IS_SHEET (sheet_view->sheet));

	sheet_view->sheet->s_controls = g_list_remove (sheet_view->sheet->s_controls, sheet_view);
	sheet_view->sheet = NULL;
}

static void
sheet_init_default_styles (Sheet *sheet)
{
	/* measurements are in pts including the margins and far grid line.  */
	sheet_col_set_default_size_pts (sheet, 48);
	sheet_row_set_default_size_pts (sheet, 12.75);
}

/*
 * sheet_new
 * @wb              Workbook
 * @name            Unquoted name
 */
Sheet *
sheet_new (Workbook *wb, const char *name)
{
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
	sheet->priv->edit_pos.location_changed = FALSE;
	sheet->priv->edit_pos.content_changed  = FALSE;
	sheet->priv->edit_pos.format_changed   = FALSE;

	sheet->priv->selection_content_changed = FALSE;
	sheet->priv->recompute_visibility = FALSE;
	sheet->priv->recompute_spans = FALSE;
	sheet->priv->reposition_objects.row = SHEET_MAX_ROWS;
	sheet->priv->reposition_objects.col = SHEET_MAX_COLS;

	sheet->signature = SHEET_SIGNATURE;
	sheet->workbook = wb;
	sheet->name_unquoted = g_strdup (name);
	sheet->name_quoted = sheet_name_quote (name);
	sheet_create_styles (sheet);

	sheet->sheet_objects = NULL;

	sheet->last_zoom_factor_used = 1.0;
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

	sheet->list_merged = NULL;
	sheet->hash_merged = g_hash_table_new ((GHashFunc)&cellpos_hash,
					       (GCompareFunc)&cellpos_cmp);

	if (getenv ("GNUMERIC_DEBUG_MERGE")) {
		/* some quick tests for merged cell support */
		Range r1 = { { 3,3 }, { 5,5 } };
		Range r2 = { { 7,4 }, { 9,6 } };
		Range r3 = { { 11,3 }, { 13,5 } };
		sheet_region_merge (NULL, sheet, &r1);
		sheet_region_merge (NULL, sheet, &r2);
		sheet_region_merge (NULL, sheet, &r3);
	}

	sheet->deps	 = dependency_data_new ();
	sheet->cell_hash = g_hash_table_new ((GHashFunc)&cellpos_hash,
					     (GCompareFunc)&cellpos_cmp);

	mstyle = mstyle_new_default ();
	sheet_style_attach (sheet, sheet_get_full_range (), mstyle);

	sheet_init_default_styles (sheet);

	sheet_selection_add (sheet, 0, 0);

	/* Force the zoom change inorder to initialize things */
	sheet_set_zoom_factor (sheet, 1.0, TRUE, TRUE);

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
cb_recalc_span0 (gpointer ignored, gpointer value, gpointer flags)
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

	/* Render & Size any unrendered cells */
	if ((flags & SPANCALC_RENDER) && cell->rendered_value == NULL)
		render = TRUE;

	if (render)
		cell_render_value ((Cell *)cell, TRUE);
	else if (resize)
		rendered_value_calc_size (cell);

	if (sheet_region_is_merge_cell (cell->base.sheet, &cell->pos)) {
		sheet_redraw_cell (cell);
		return;
	}

	/* Calculate the span of the cell */
	cell_calc_span (cell, &left, &right);
	min_col = left;
	max_col = right;

	/* Is there an existing span ? */
	span = row_span_get (cell->row_info, cell->pos.col);
	if (span != NULL) {
		Cell const * const other = span->cell;
		int other_left, other_right;

		/* The existing span belonged to this cell */
		if (cell == other) {
			if (left != span->left || right != span->right) {
				cell_unregister_span (cell);
				cell_register_span (cell, left, right);
			}
			sheet_redraw_partial_row (cell->base.sheet, cell->pos.row,
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

	sheet_redraw_partial_row (cell->base.sheet, cell->pos.row,
				  min_col, max_col);
}

/****************************************************************************/

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

	WORKBOOK_FOREACH_VIEW (sheet->workbook, view,
	{
		if (wb_view_cur_sheet (view) == sheet) {
			WORKBOOK_VIEW_FOREACH_CONTROL(view, control,
				wb_control_zoom_feedback (control););
		}
	});
}

/**
 * sheet_set_zoom_factor : Change the zoom factor.
 * @sheet : The sheet
 * @f : The new zoom
 * @force : Force the zoom to change irrespective of its current value.
 *          Most callers will want to say FALSE.
 * @respan : recalculate the spans.
 */
void
sheet_set_zoom_factor (Sheet *sheet, double f, gboolean force, gboolean update)
{
	struct resize_colrow closure;
	double factor;

	g_return_if_fail (sheet != NULL);

	/* Bound zoom between 10% and 500% */
	factor = (f < .1) ? .1 : ((f > 5.) ? 5. : f);
	if (!force) {
		double const diff = sheet->last_zoom_factor_used - factor;

		if (-.0001 < diff && diff < .0001)
			return;
	}

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

	SHEET_FOREACH_CONTROL (sheet, control,
		sheet_view_set_zoom_factor (control, factor););

	/*
	 * The font size does not scale linearly with the zoom factor
	 * we will need to recalculate the pixel sizes of all cells.
	 * We also need to render any cells which have not yet been
	 * rendered.
	 */
	if (update) {
		sheet->priv->recompute_spans = TRUE;
		sheet->priv->reposition_objects.col =
			sheet->priv->reposition_objects.row = 0;
		sheet_update_only_grid (sheet);

		if (sheet->workbook)
			sheet_update_zoom_controls (sheet);
	}
}

ColRowInfo *
sheet_row_new (Sheet *sheet)
{
	ColRowInfo *ri = g_new (ColRowInfo, 1);

	g_return_val_if_fail (IS_SHEET (sheet), NULL);

	*ri = sheet->rows.default_style;

	return ri;
}

ColRowInfo *
sheet_col_new (Sheet *sheet)
{
	ColRowInfo *ci = g_new (ColRowInfo, 1);

	g_return_val_if_fail (IS_SHEET (sheet), NULL);

	*ci = sheet->cols.default_style;

	return ci;
}

void
sheet_col_add (Sheet *sheet, ColRowInfo *cp)
{
	int const col = cp->pos;
	ColRowSegment **segment = (ColRowSegment **)&COLROW_GET_SEGMENT (&(sheet->cols), col);

	g_return_if_fail (col >= 0);
	g_return_if_fail (col < SHEET_MAX_COLS);

	if (*segment == NULL)
		*segment = g_new0 (ColRowSegment, 1);
	(*segment)->info[COLROW_SUB_INDEX(col)] = cp;

	if (col > sheet->cols.max_used){
		sheet->cols.max_used = col;
		sheet->priv->resize_scrollbar = TRUE;
	}
}

void
sheet_row_add (Sheet *sheet, ColRowInfo *rp)
{
	int const row = rp->pos;
	ColRowSegment **segment = (ColRowSegment **)&COLROW_GET_SEGMENT (&(sheet->rows), row);

	g_return_if_fail (row >= 0);
	g_return_if_fail (row < SHEET_MAX_ROWS);

	if (*segment == NULL)
		*segment = g_new0 (ColRowSegment, 1);
	(*segment)->info[COLROW_SUB_INDEX(row)] = rp;

	if (rp->pos > sheet->rows.max_used){
		sheet->rows.max_used = row;
		sheet->priv->resize_scrollbar = TRUE;
	}
}

ColRowInfo *
sheet_col_get_info (Sheet const *sheet, int const col)
{
	ColRowInfo    *ci = NULL;
	ColRowSegment *segment;

	g_return_val_if_fail (col >= 0, NULL);
	g_return_val_if_fail (col < SHEET_MAX_COLS, NULL);

	segment = COLROW_GET_SEGMENT (&(sheet->cols), col);
	if (segment != NULL) {
		ci = segment->info[COLROW_SUB_INDEX(col)];
		if (ci != NULL)
			return ci;
	}

	return (ColRowInfo *) &sheet->cols.default_style;
}

ColRowInfo *
sheet_row_get_info (Sheet const *sheet, int const row)
{
	ColRowInfo    *ri = NULL;
	ColRowSegment *segment;

	g_return_val_if_fail (row >= 0, NULL);
	g_return_val_if_fail (row < SHEET_MAX_ROWS, NULL);

	segment = COLROW_GET_SEGMENT(&(sheet->rows), row);
	if (segment != NULL) {
		ri = segment->info[COLROW_SUB_INDEX(row)];
		if (ri != NULL)
			return ri;
	}

	return (ColRowInfo *) &sheet->rows.default_style;
}

static void
sheet_reposition_objects (Sheet const *sheet, CellPos const *pos)
{
	GList *ptr;

	g_return_if_fail (IS_SHEET (sheet));

	for (ptr = sheet->sheet_objects; ptr != NULL ; ptr = ptr->next )
		sheet_object_position (SHEET_OBJECT (ptr->data), pos);
}

/**
 * sheet_flag_status_update_cell:
 *    flag the sheet as requiring an update to the status display
 *    if the supplied cell location is the edit cursor, or part of the
 *    selected region.
 *
 * @cell : The cell that has changed.
 *
 * Will cause the format toolbar, the edit area, and the auto expressions to be
 * updated if appropriate.
 */
void
sheet_flag_status_update_cell (Cell const *cell)
{
	Sheet const *sheet = cell->base.sheet;
	CellPos const *pos = &cell->pos;

	/* if a part of the selected region changed value update
	 * the auto expressions
	 */
	if (sheet_is_cell_selected (sheet, pos->col, pos->row))
		sheet->priv->selection_content_changed = TRUE;

	/* If the edit cell changes value update the edit area
	 * and the format toolbar
	 */
	if (pos->col == sheet->cursor.edit_pos.col &&
	    pos->row == sheet->cursor.edit_pos.row) {
		sheet->priv->edit_pos.content_changed =
		sheet->priv->edit_pos.format_changed  = TRUE;
	}
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
sheet_flag_status_update_range (Sheet const *sheet, Range const *range)
{
	/* Force an update */
	if (range == NULL) {
		sheet->priv->selection_content_changed = TRUE;
		sheet->priv->edit_pos.location_changed =
		sheet->priv->edit_pos.content_changed =
		sheet->priv->edit_pos.format_changed = TRUE;
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
	if (range_contains(range, sheet->cursor.edit_pos.col, sheet->cursor.edit_pos.row)) {
		sheet->priv->edit_pos.content_changed =
		sheet->priv->edit_pos.format_changed = TRUE;
	}
}

/**
 * sheet_flag_format_update_range :
 * @sheet : The sheet being changed
 * @range : the range that is changing.
 *
 * Flag format changes that will require updating the format indicators.
 */
void
sheet_flag_format_update_range (Sheet const *sheet, Range const *range)
{
	if (range_contains(range, sheet->cursor.edit_pos.col, sheet->cursor.edit_pos.row))
		sheet->priv->edit_pos.format_changed = TRUE;
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
 * sheet_update_only_grid :
 *
 * Should be called after a logical command has finished processing
 * to request redraws for any pending events
 */
void
sheet_update_only_grid (Sheet const *sheet)
{
	SheetPrivate *p;

	g_return_if_fail (sheet != NULL);

	p = sheet->priv;

	if (p->recompute_spans) {
		p->recompute_spans = FALSE;
		/* FIXME : I would prefer to use SPANCALC_RENDER rather than
		 * RE_RENDER.  It only renders those cells which are not
		 * rendered.  The trouble is that when a col changes size we
		 * need to rerender, but currently nothing marks that.
		 *
		 * hmm, that suggests an approach.  maybe I can install a per
		 * col flag.  Then add a flag clearing loop after the
		 * sheet_calc_span.
		 */
		sheet_calc_spans (sheet, SPANCALC_RESIZE|SPANCALC_RE_RENDER |
				  (p->recompute_visibility ?
				   SPANCALC_NO_DRAW : SPANCALC_SIMPLE));
	}

	if (p->reposition_objects.row < SHEET_MAX_ROWS ||
	    p->reposition_objects.col < SHEET_MAX_COLS) {
		sheet_reposition_objects (sheet, &p->reposition_objects);
		p->reposition_objects.row = SHEET_MAX_ROWS;
		p->reposition_objects.col = SHEET_MAX_COLS;
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
		SHEET_FOREACH_CONTROL (sheet, control,
			sheet_view_scrollbar_config (control););
		p->resize_scrollbar = FALSE;
	}
}

/**
 * sheet_update:
 *
 * Should be called after a logical command has finished processing to request
 * redraws for any pending events, and to update the various status regions
 */
void
sheet_update (Sheet const *sheet)
{
	SheetPrivate *p;

	g_return_if_fail (sheet != NULL);

	sheet_update_only_grid (sheet);

	p = sheet->priv;

	if (p->edit_pos.content_changed) {
		p->edit_pos.content_changed = FALSE;
		WORKBOOK_FOREACH_VIEW (sheet->workbook, view,
		{
			if (wb_view_cur_sheet (view) == sheet)
				wb_view_edit_line_set (view, NULL);
		});
	}

	if (p->edit_pos.format_changed) {
		p->edit_pos.format_changed = FALSE;
		WORKBOOK_FOREACH_VIEW (sheet->workbook, view,
		{
			if (wb_view_cur_sheet (view) == sheet)
				wb_view_format_feedback (view, TRUE);
		});
	}

	/* FIXME : decide whether to do this here or in workbook view */
	if (p->edit_pos.location_changed) {
		char const *new_pos = cell_pos_name (&sheet->cursor.edit_pos);

		p->edit_pos.location_changed = FALSE;
		WORKBOOK_FOREACH_VIEW (sheet->workbook, view,
		{
			if (wb_view_cur_sheet (view) == sheet) {
				WORKBOOK_VIEW_FOREACH_CONTROL(view, control,
					wb_control_selection_descr_set (control, new_pos););
			}
		});
	}

	if (p->selection_content_changed) {
		p->selection_content_changed = FALSE;
		WORKBOOK_FOREACH_VIEW (sheet->workbook, view,
		{
			if (wb_view_cur_sheet (view) == sheet)
				wb_view_auto_expr_recalc (view, TRUE);
		});
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

	for (l = sheet->s_controls; l; l = l->next){
		GnumericSheet *gsheet = GNUMERIC_SHEET_CONTROL_GUI (l->data);

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
Cell *
sheet_cell_get (Sheet const *sheet, int col, int row)
{
	Cell *cell;
	CellPos pos;

	g_return_val_if_fail (IS_SHEET (sheet), NULL);

	pos.col = col;
	pos.row = row;
	cell = g_hash_table_lookup (sheet->cell_hash, &pos);

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
Cell *
sheet_cell_fetch (Sheet *sheet, int col, int row)
{
	Cell *cell;

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
sheet_get_extent_cb (gpointer ignored, gpointer value, gpointer data)
{
	Cell *cell = (Cell *) value;

	if (!cell_is_blank (cell)) {
		Range *range = (Range *)data;
		CellSpanInfo const *span = NULL;
		int tmp;

		if (cell->pos.row < range->start.row)
			range->start.row = cell->pos.row;

		if (cell->pos.row > range->end.row)
			range->end.row = cell->pos.row;

		/* FIXME : check for merged cells too */
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

	/* Extend region to include styles that are visible in blank cells */
	sheet_style_get_extent (&r, sheet);

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
	int width;

	g_return_val_if_fail (cell->rendered_value != NULL, NULL);

	/* Dynamic cells must be rerendered */
	if (cell->rendered_value->dynamic_width)
		cell_render_value (cell, FALSE);

	width = cell_rendered_width (cell);
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
	ColRowInfo const *ri = sheet_row_get (sheet, row);
	if (ri == NULL)
		return 0;

	sheet_cell_foreach_range (sheet, TRUE,
				  0, row,
				  SHEET_MAX_COLS-1, row,
				  (ForeachCellCB)&cb_max_cell_height, &max);

	/* Reset to the default width if the column was empty */
	if (max <= 0)
		return 0;

	/* Cell height does not include margins or bottom grid line */
	max += ri->margin_a + ri->margin_b + 1;

	/* FIXME FIXME FIXME : HACK HACK HACK
	 * if the height is 1 pixel larger than the minimum required
	 * do not bother to resize.  The current font kludges cause a
	 * problem because the 9pt font font that we display @ 96dpi is a 12
	 * pixel font.  Where as the row height was calculated using windows
	 * which uses a 10pt font @96 dpi and displays a 13pixel font.
	 *
	 * As a result the default row height is 1 pixel too large for the
	 * font.  When we run this test things then resize 1 pixel smaller for
	 * no apparent reason.
	 */
	if (ri->size_pixels == (max+1))
		return 0;
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

/****************************************************************************/

/*
 * Callback for sheet_cell_foreach_range to assign some text
 * to a range.
 */
typedef struct {
	StyleFormat *format;
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
		cell_set_value (cell, value_duplicate (info->val),
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

	closure.format = parse_text_value_or_expr (pos, str,
		&closure.val, &closure.expr,
		NULL /* TODO : Use edit_pos format ?? */);

	/* Store the parsed result creating any cells necessary */
	sheet_cell_foreach_range (pos->sheet, FALSE,
				  r->start.col, r->start.row,
				  r->end.col, r->end.row,
				  (ForeachCellCB)&cb_set_cell_content,
				  &closure);

	if (closure.format)
		style_format_unref (closure.format);

	if (closure.val)
		value_release (closure.val);
	else
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
	char *cformat;

	g_return_if_fail (str != NULL);
	g_return_if_fail (cell != NULL);
	g_return_if_fail (!cell_is_partial_array (cell));

	cformat = cell_get_format (cell);
	format = parse_text_value_or_expr (eval_pos_init_cell (&pos, cell),
					   str, &val, &expr,
					   cformat);
	g_free (cformat);

	if (expr != NULL) {
		cell_set_expr (cell, expr, format);
		expr_tree_unref (expr);
	} else {
		cell_set_value (cell, val, format);
		sheet_cell_calc_span (cell, SPANCALC_RESIZE | SPANCALC_RENDER);
		cell_content_changed (cell);
	}
	if (format)
		style_format_unref (format);
	sheet_flag_status_update_cell (cell);
}

void
sheet_cell_set_expr (Cell *cell, ExprTree *expr)
{
	/* No need to do anything until recalc */
	cell_set_expr (cell, expr, NULL);
	cell_content_changed (cell);
	sheet_flag_status_update_cell (cell);
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
	sheet_cell_calc_span (cell, SPANCALC_RESIZE | SPANCALC_RENDER);
	cell_content_changed (cell);
	sheet_flag_status_update_cell (cell);
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
	int row, min_col = start_col, max_col = end_col;

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
			ColRowSegment const * const segment =
				COLROW_GET_SEGMENT(&(sheet->rows), row);
			if (segment == NULL)
				row = COLROW_SEGMENT_END (row);
		}
	}

	/* FIXME : expand the rectangle to contain any merged regions */
	SHEET_FOREACH_CONTROL (sheet, control,
		sheet_view_redraw_cell_region ( control,
			min_col, start_row, max_col, end_row););
}

void
sheet_redraw_range (Sheet const *sheet, Range const *range)
{
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
	SHEET_FOREACH_CONTROL (sheet, control,
		sheet_view_redraw_cell_region (control,
			start_col, row, end_col, row););
}

void
sheet_redraw_cell (Cell const *cell)
{
	CellSpanInfo const * span;
	int start_col, end_col;
	Range const *merged;

	g_return_if_fail (cell != NULL);

	merged = sheet_region_is_merge_cell (cell->base.sheet, &cell->pos);
	if (merged != NULL) {
		SHEET_FOREACH_CONTROL (cell->base.sheet, control,
			sheet_view_redraw_cell_region (control,
				merged->start.col, merged->start.row,
				merged->end.col, merged->end.row););
	}

	start_col = end_col = cell->pos.col;
	span = row_span_get (cell->row_info, start_col);

	if (span) {
		start_col = span->left;
		end_col = span->right;
	}

	sheet_redraw_partial_row (cell->base.sheet, cell->pos.row,
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
	gboolean find_nonblank = sheet_is_cell_empty (sheet, start_col, row);
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
			keep_looking = (sheet_is_cell_empty (sheet, new_col, row) == find_nonblank);
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
	gboolean find_nonblank = sheet_is_cell_empty (sheet, col, start_row);
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

			keep_looking = (sheet_is_cell_empty (sheet, col, new_row) == find_nonblank);
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
	ColRowSegment *segment;

	g_return_val_if_fail (IS_SHEET (sheet), NULL);
	g_return_val_if_fail (pos < SHEET_MAX_COLS, NULL);
	g_return_val_if_fail (pos >= 0, NULL);

	segment = COLROW_GET_SEGMENT (&(sheet->cols), pos);
	if (segment != NULL)
		return segment->info [COLROW_SUB_INDEX(pos)];
	return NULL;
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
	ColRowSegment *segment;

	g_return_val_if_fail (IS_SHEET (sheet), NULL);
	g_return_val_if_fail (pos < SHEET_MAX_ROWS, NULL);
	g_return_val_if_fail (pos >= 0, NULL);

	segment = COLROW_GET_SEGMENT (&(sheet->rows), pos);
	if (segment != NULL)
		return segment->info [COLROW_SUB_INDEX(pos)];
	return NULL;
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
 *
 * NOTE: between 0.56 and 0.57, the traversal order changed.  The order is now
 *
 *        1    2    3
 *        4    5    6
 *        7    8    9
 *
 * (This appears to be the order in which XL looks at the values of ranges.)
 * If your code depends on any particular ordering, please add a very visible
 * comment near the call.
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

	for (i = start_row; i <= end_row; ++i) {
		ColRowInfo *ci = sheet_row_get (sheet, i);

		if (ci == NULL) {
			if (only_existing) {
				/* skip segments with no cells */
				if (i == COLROW_SEGMENT_START (i)) {
					ColRowSegment const *segment =
						COLROW_GET_SEGMENT(&(sheet->rows), i);
					if (segment == NULL)
						i = COLROW_SEGMENT_END(i);
				}
			} else {
				for (j = start_col; j <= end_col; ++j) {
					cont = (*callback)(sheet, j, i, NULL, closure);
					if (cont != NULL)
						return cont;
				}
			}

			continue;
		}

		for (j = start_col; j <= end_col; ++j) {
			ColRowInfo *ri = sheet_col_get (sheet, j);
			Cell *cell = NULL;

			if (ri != NULL)
				cell = sheet_cell_get (sheet, j, i);

			if (cell == NULL && only_existing) {
				/* skip segments with no cells */
				if (j == COLROW_SEGMENT_START (j)) {
					ColRowSegment const *segment =
						COLROW_GET_SEGMENT(&(sheet->cols), j);
					if (segment == NULL)
						j = COLROW_SEGMENT_END(j);
				}
				continue;
			}

			cont = (*callback)(sheet, j, i, cell, closure);
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
 * sheet_is_region_empty_or_selected:
 * @sheet: sheet to check
 * @start_col: starting column
 * @start_row: starting row
 * @end_col:   end column
 * @end_row:   end row
 *
 * Returns TRUE if the specified region of the @sheet does not
 * contain any cells that are not selected.
 *
 * FIXME: Perhaps this routine should be extended to allow testing for specific
 * features of a cell rather than just the existance of the cell.
 */
gboolean
sheet_is_region_empty_or_selected (Sheet *sheet, Range const *r)
{
	g_return_val_if_fail (IS_SHEET (sheet), TRUE);

	return sheet_cell_foreach_range (
		sheet, TRUE, r->start.col, r->start.row, r->end.col, r->end.row,
		fail_if_not_selected, NULL) == NULL;

}

static Value *
fail_if_exist (Sheet *sheet, int col, int row, Cell *cell, void *user_data)
{
	return cell_is_blank (cell) ? NULL : value_terminate ();
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
 * contain any cells
 */
gboolean
sheet_is_region_empty (Sheet *sheet, Range const *r)
{
	g_return_val_if_fail (IS_SHEET (sheet), TRUE);

	return sheet_cell_foreach_range (
		sheet, TRUE, r->start.col, r->start.row, r->end.col, r->end.row,
		fail_if_exist, NULL) == NULL;
}

gboolean
sheet_is_cell_empty (Sheet *sheet, int col, int row)
{
	Cell const *cell = sheet_cell_get (sheet, col, row);
	return cell_is_blank (cell);
}

/**
 * sheet_cell_add_to_hash:
 * @sheet The sheet where the cell is inserted
 * @cell  The cell, it should already have col/pos pointers
 *        initialized pointing to the correct ColRowInfo
 *
 * Cell::pos must be valid before this is called.  The position is used as the
 * hash key.
 */
static void
sheet_cell_add_to_hash (Sheet *sheet, Cell *cell)
{
	g_return_if_fail (cell->pos.col < SHEET_MAX_COLS);
	g_return_if_fail (cell->pos.row < SHEET_MAX_ROWS);
	g_return_if_fail (!cell_is_linked (cell));

	cell->base.flags |= CELL_IN_SHEET_LIST;
	cell->col_info   = sheet_col_fetch (sheet, cell->pos.col);
	cell->row_info   = sheet_row_fetch (sheet, cell->pos.row);

	g_hash_table_insert (sheet->cell_hash, &cell->pos, cell);
}

void
sheet_cell_insert (Sheet *sheet, Cell *cell, int col, int row, gboolean recalc_span)
{
	cell->base.sheet = sheet;
	cell->pos.col = col;
	cell->pos.row = row;

	sheet_cell_add_to_hash (sheet, cell);
	cell_add_dependencies (cell);

	if (recalc_span && !cell_needs_recalc (cell))
		sheet_cell_calc_span (cell, SPANCALC_RESIZE | SPANCALC_RENDER);
}

Cell *
sheet_cell_new (Sheet *sheet, int col, int row)
{
	Cell *cell;

	g_return_val_if_fail (IS_SHEET (sheet), NULL);

	cell = g_new0 (Cell, 1);

	cell->base.sheet   = sheet;
	cell->base.flags = DEPENDENT_CELL;
	cell->pos.col = col;
	cell->pos.row = row;
	cell->value   = value_new_empty ();

	sheet_cell_add_to_hash (sheet, cell);
	return cell;
}

static void
sheet_cell_remove_from_hash (Sheet *sheet, Cell *cell)
{
	g_return_if_fail (cell_is_linked (cell));

	cell_unregister_span   (cell);
	cell_drop_dependencies (cell);

	g_hash_table_remove (sheet->cell_hash, &cell->pos);
	cell->base.flags &= ~CELL_IN_SHEET_LIST;
}

/**
 * sheet_cell_remove_simple : Remove the cell from the web of depenancies of a
 *        sheet.  Do NOT redraw or free the cell.
 */
void
sheet_cell_remove_simple (Sheet *sheet, Cell *cell)
{
	GList *deps;

	if (cell_needs_recalc (cell))
		dependent_unqueue_recalc (CELL_TO_DEP (cell));

	if (cell_has_expr (cell))
		dependent_unlink (CELL_TO_DEP (cell), &cell->pos);

	deps = cell_get_dependencies (cell);
	if (deps)
		dependent_queue_recalc_list (deps, TRUE);

	sheet_cell_remove_from_hash (sheet, cell);
#warning Remove objects
}

/**
 * sheet_cell_remove_simple : Remove the cell from the web of depenancies of a
 *        sheet.  Do NOT free the cell, optionally redraw it.
 */
void
sheet_cell_remove (Sheet *sheet, Cell *cell, gboolean redraw)
{
	g_return_if_fail (cell != NULL);
	g_return_if_fail (IS_SHEET (sheet));

	/* Queue a redraw on the region used by the cell being removed */
	if (redraw) {
		sheet_redraw_cell_region (sheet,
					  cell->pos.col, cell->pos.row,
					  cell->pos.col, cell->pos.row);
		sheet_flag_status_update_cell (cell);
	}

	sheet_cell_remove_simple (sheet, cell);
	cell_destroy (cell);
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
	ColRowSegment **segment = (ColRowSegment **)&COLROW_GET_SEGMENT(&(sheet->cols), col);
	int const sub = COLROW_SUB_INDEX (col);
	ColRowInfo *ci = NULL;

	if (*segment == NULL)
		return;
	ci = (*segment)->info[sub];
	if (ci == NULL)
		return;

	if (free_cells)
		sheet_cell_foreach_range (sheet, TRUE,
					  col, 0,
					  col, SHEET_MAX_ROWS-1,
					  &cb_free_cell, NULL);

	(*segment)->info[sub] = NULL;
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
	ColRowSegment **segment = (ColRowSegment **)&COLROW_GET_SEGMENT(&(sheet->rows), row);
	int const sub = COLROW_SUB_INDEX(row);
	ColRowInfo *ri = NULL;
	if (*segment == NULL)
		return;
	ri = (*segment)->info[sub];
	if (ri == NULL)
		return;

	if (free_cells)
		sheet_cell_foreach_range (sheet, TRUE,
					  0, row,
					  SHEET_MAX_COLS-1, row,
					  &cb_free_cell, NULL);

	/* Rows have span lists, destroy them too */
	row_destroy_span (ri);

	(*segment)->info[sub] = NULL;
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
cb_remove_allcells (gpointer ignored, gpointer value, gpointer flags)
{
	Cell *cell = value;
	cell_drop_dependencies (cell);

	cell->base.flags &= ~CELL_IN_SHEET_LIST;
	cell->base.sheet = NULL;
	cell_destroy (cell);
	return TRUE;
}


static gboolean
cb_dummy_remove_func (void *key, void *value, void *user)
{
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

	if (sheet->list_merged) {
		GSList *l;

		for (l = sheet->list_merged; l; l = l->next) {
			Range *r;
			r = l->data;
			g_free (r);
		}
		g_slist_free (sheet->list_merged);
		sheet->list_merged = NULL;

		/*
		 * Remove everything.  This is faster than removing the
		 * list elements as we see them.
		 */
		g_hash_table_foreach_remove (sheet->hash_merged,
					     cb_dummy_remove_func,
					     NULL);
	}

	/* Clear the row spans 1st */
	for (i = sheet->rows.max_used; i >= 0 ; --i)
		row_destroy_span (sheet_row_get (sheet, i));

	/* Remove any pending recalcs */
	dependent_unqueue_recalc_sheet (sheet);

	/* Unlink expressions from the workbook expr list */
	dependent_unlink_sheet (sheet);

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
	g_return_if_fail (IS_SHEET (sheet));

	if (sheet->print_info) {
		print_info_free (sheet->print_info);
		sheet->print_info = NULL;
	}

	if (sheet->sheet_objects) {
		/* The list is changed as we remove */
		GList *objs = g_list_copy (sheet->sheet_objects);
		GList *ptr;
		for (ptr = objs; ptr != NULL ; ptr = ptr->next) {
			SheetObject *so = SHEET_OBJECT (ptr->data);
			if (so != NULL)
				gtk_object_unref (GTK_OBJECT (so));
		}
		g_list_free (objs);
		if (sheet->sheet_objects != NULL)
			g_warning ("There is a problem with sheet objects");
	}

	sheet_selection_free (sheet);

	g_free (sheet->name_quoted);
	g_free (sheet->name_unquoted);
	g_free (sheet->solver_parameters.input_entry_str);

	SHEET_FOREACH_CONTROL (sheet, control,
		gtk_object_unref (GTK_OBJECT (control)););
	g_list_free (sheet->s_controls);
	sheet->s_controls = NULL;

	sheet_deps_destroy (sheet);
	expr_name_invalidate_refs_sheet (sheet);
	sheet_destroy_contents (sheet);
	sheet->names = expr_name_list_destroy (sheet->names);

	g_hash_table_destroy (sheet->hash_merged);
	sheet->hash_merged = NULL;
	g_assert (sheet->list_merged == NULL);

	/* Clear the cliboard to avoid dangling references to the deleted sheet */
	if (sheet == application_clipboard_sheet_get ())
		application_clipboard_clear (TRUE);

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
 *
 * WARNING : This does NOT regenerate spans that were interupted by
 * this cell and can now continue.
 */
static Value *
cb_empty_cell (Sheet *sheet, int col, int row, Cell *cell, gpointer flag)
{
#if 0
	/* TODO : here and other places flag a need to update the
	 * row/col maxima.
	 */
	if (row >= sheet->rows.max_used || col >= sheet->cols.max_used) { }
#endif

	sheet_cell_remove (sheet, cell, FALSE /* no redraw */);
	return NULL;
}

/**
 * sheet_regen_adjacent_spans :
 *
 * @sheet : The sheet to operate on
 * @start_col :
 * @start_row :
 * @end_col :
 * @end_row :
 *
 * When a region has been cleared the adjacent spans may be able to
 * expand into the newly cleared area.
 */
void
sheet_regen_adjacent_spans (Sheet *sheet,
			    int start_col, int start_row,
			    int end_col, int end_row,
			    int *min_col, int *max_col)
{
	int i, row, col[2];
	gboolean test[2];

	*min_col = start_col;
	*max_col = end_col;

	test[0] = (start_col > 0);
	test[1] = (end_col < SHEET_MAX_ROWS-1);
	col[0] = start_col - 1;
	col[1] = end_col + 1;
	for (row = start_row; row <= end_row ; ++row) {
		ColRowInfo const *ri = sheet_row_get (sheet, row);

		if (ri == NULL) {
			/* skip segments with no cells */
			if (row == COLROW_SEGMENT_START (row)) {
				ColRowSegment const *segment =
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
			if (*min_col > left)
				*min_col = left;
			if (*max_col < right)
				*max_col = right;
		}
	}
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
sheet_clear_region (WorkbookControl *wbc, Sheet *sheet,
		    int start_col, int start_row,
		    int end_col, int end_row,
		    int clear_flags)
{
	Range r;
	int min_col, max_col;

	g_return_if_fail (IS_SHEET (sheet));
	g_return_if_fail (start_col <= end_col);
	g_return_if_fail (start_row <= end_row);

	r.start.col = start_col;
	r.start.row = start_row;
	r.end.col = end_col;
	r.end.row = end_row;

	if (clear_flags & CLEAR_VALUES && !(clear_flags & CLEAR_NOCHECKARRAY) &&
	    sheet_range_splits_array (sheet, &r)) {
		gnumeric_error_splits_array (COMMAND_CONTEXT (wbc),
					     _("Clear"));
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

	if (clear_flags & CLEAR_COMMENTS) {
		GList *comments, *ptr;

		comments = sheet_get_objects (sheet, &r, CELL_COMMENT_TYPE);
		for (ptr = comments ; ptr != NULL ;ptr = ptr->next)
			gtk_object_destroy (GTK_OBJECT (ptr->data));
		g_list_free (comments);
	}

	min_col = start_col;
	max_col = end_col;

	if (clear_flags & CLEAR_VALUES) {
		/* Remove or empty the cells depending on
		 * whether or not there are comments
		 */
		sheet_cell_foreach_range (sheet, TRUE,
					  start_col, start_row, end_col, end_row,
					  &cb_empty_cell,
					  GINT_TO_POINTER(!(clear_flags & CLEAR_COMMENTS)));

		sheet_regen_adjacent_spans (sheet,
					    start_col, start_row,
					    end_col, end_row,
					    &min_col, &max_col);

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

	g_return_if_fail (IS_SHEET (sheet));

	for (l = sheet->s_controls; l; l = l->next){
		GnumericSheet *gsheet = GNUMERIC_SHEET_CONTROL_GUI (l->data);

		gnumeric_sheet_make_cell_visible (gsheet, col, row, FALSE);
	}
}

void
sheet_set_edit_pos (Sheet *sheet, int col, int row)
{
	int old_col, old_row;

	g_return_if_fail (IS_SHEET (sheet));

	old_col = sheet->cursor.edit_pos.col;
	old_row = sheet->cursor.edit_pos.row;

	if (old_col != col || old_row != row) {
		sheet->priv->edit_pos.location_changed =
		sheet->priv->edit_pos.content_changed =
		sheet->priv->edit_pos.format_changed = TRUE;

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

	g_return_if_fail (IS_SHEET (sheet));

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

	for (l = sheet->s_controls; l; l = l->next){
		GnumericSheet *gsheet = GNUMERIC_SHEET_CONTROL_GUI (l->data);

		gnumeric_sheet_set_cursor_bounds (gsheet,
			MIN (base_col, move_col),
			MIN (base_row, move_row),
			MAX (base_col, move_col),
			MAX (base_row, move_row));
	}
}

void
sheet_update_cursor_pos (Sheet const *sheet)
{
	g_return_if_fail (IS_SHEET (sheet));

	SHEET_FOREACH_CONTROL (sheet, control,
		sheet_view_update_cursor_pos (control););
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
	static char quote_chr [] =
	{ '=', '<', '>', '(', ')', '+', '-', ' ', '^', '&', '%', ':', '\0' };

	g_return_val_if_fail (name_unquoted != NULL, NULL);

	needs_quotes = isdigit ((unsigned char)*name_unquoted);
	if (!needs_quotes)
		for (i = 0, quotes_embedded = 0; name_unquoted [i]; i++) {
			for (j = 0; quote_chr [j]; j++)
				if (name_unquoted [i] == quote_chr [j])
					needs_quotes = TRUE;
			if (name_unquoted [i] == '\'')
				quotes_embedded++;
		}

	if (needs_quotes) {
		int  len_quoted = strlen (name_unquoted) + quotes_embedded + 3;
		char  *ret = g_malloc (len_quoted);
		const char *src;
		char  *dst;

		*ret = '\'';
		for (src = name_unquoted, dst = ret + 1; *src; src++, dst++) {
			if (*src == '\'')
				*dst++ = '\\';
			*dst = *src;
		}
		*dst++ = '\'';
		*dst = '\0';

		return ret;
	} else
		return g_strdup (name_unquoted);
}

void
sheet_mark_clean (Sheet *sheet)
{
	g_return_if_fail (IS_SHEET (sheet));

	if (sheet->modified)
		sheet->pristine = FALSE;

	sheet->modified = FALSE;
}

void
sheet_set_dirty (Sheet *sheet, gboolean is_dirty)
{
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
	sheet = workbook_sheet_by_name (wb, name);

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
	if (cell_is_array (cell) && cell->base.expression->array.x > 0)
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
	if (cell_is_array (cell) && cell->base.expression->array.y > 0)
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
	gboolean const is_cols = (info_collection == &sheet->cols);
	ColRowSegment *segment = COLROW_GET_SEGMENT(info_collection, old_pos);
	ColRowInfo *info = (segment != NULL) ?
		segment->info [COLROW_SUB_INDEX(old_pos)] : NULL;

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
	segment->info [COLROW_SUB_INDEX (old_pos)] = NULL;
	info->pos = new_pos;

	/* TODO : Figure out a way to merge these functions */
	if (is_cols)
		sheet_col_add (sheet, info);
	else
		sheet_row_add (sheet, info);

	/* Insert the cells back */
	for (; cells != NULL ; cells = g_list_remove (cells, cell)) {
		cell = cells->data;

		if (is_cols)
			cell->pos.col = new_pos;
		else
			cell->pos.row = new_pos;

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
sheet_insert_cols (WorkbookControl *wbc, Sheet *sheet,
		   int col, int count, GSList **reloc_storage)
{
	ExprRelocateInfo reloc_info;
	int   i;

	g_return_val_if_fail (reloc_storage != NULL, TRUE);

	*reloc_storage = NULL;

	g_return_val_if_fail (IS_SHEET (sheet), TRUE);
	g_return_val_if_fail (count != 0, TRUE);

	/* 0. Walk cells in displaced col and ensure arrays aren't divided. */
	if (col > 0)	/* No need to test leftmost column */
		if (sheet_cell_foreach_range (sheet, TRUE, col, 0,
					      col, SHEET_MAX_ROWS-1,
					      &avoid_dividing_array_horizontal,
					      NULL) != NULL){
			gnumeric_error_splits_array (COMMAND_CONTEXT (wbc),
						     _("Insert Columns"));
			return TRUE;
		}

	/* Walk the right edge to make sure nothing is split due to over run.  */
	if (sheet_cell_foreach_range (sheet, TRUE, SHEET_MAX_COLS-count, 0,
				      SHEET_MAX_COLS-1, SHEET_MAX_ROWS-1,
				      &avoid_dividing_array_horizontal,
				      NULL) != NULL){
		gnumeric_error_splits_array (COMMAND_CONTEXT (wbc),
					     _("Insert Columns"));
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

	/* 4. Slide the StyleRegions and SheetObjects to the right */
	sheet_relocate_objects (&reloc_info);
	sheet_style_insert_colrow (sheet, col, count, TRUE);

	/* 5. Recompute dependencies */
	sheet_recalc_dependencies (sheet);

	/* 6. Notify sheet of pending updates */
	sheet->priv->recompute_visibility = TRUE;
	sheet->priv->recompute_spans = TRUE;
	sheet_flag_status_update_range (sheet, &reloc_info.origin);
	if (sheet->priv->reposition_objects.col > col)
		sheet->priv->reposition_objects.col = col;

	return FALSE;
}

/*
 * sheet_delete_cols
 * @sheet   The sheet
 * @col     At which position we want to start deleting columns
 * @count   The number of columns to be deleted
 */
gboolean
sheet_delete_cols (WorkbookControl *wbc, Sheet *sheet,
		   int col, int count, GSList **reloc_storage)
{
	ExprRelocateInfo reloc_info;
	int i;

	g_return_val_if_fail (reloc_storage != NULL, TRUE);

	*reloc_storage = NULL;

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
		gnumeric_error_splits_array (COMMAND_CONTEXT (wbc),
					     _("Delete Columns"));
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

	/* 5. Slide the StyleRegions and SheetObjects left */
	sheet_relocate_objects (&reloc_info);
	sheet_style_delete_colrow (sheet, col, count, TRUE);

	/* 6. Recompute dependencies */
	sheet_recalc_dependencies (sheet);

	/* 7. Notify sheet of pending updates */
	sheet->priv->recompute_visibility = TRUE;
	sheet->priv->recompute_spans = TRUE;
	sheet_flag_status_update_range (sheet, &reloc_info.origin);
	if (sheet->priv->reposition_objects.col > col)
		sheet->priv->reposition_objects.col = col;

	return FALSE;
}

/**
 * sheet_insert_rows:
 * @sheet   The sheet
 * @row     At which position we want to insert
 * @count   The number of rows to be inserted
 */
gboolean
sheet_insert_rows (WorkbookControl *wbc, Sheet *sheet,
		   int row, int count, GSList **reloc_storage)
{
	ExprRelocateInfo reloc_info;
	int   i;

	g_return_val_if_fail (reloc_storage != NULL, TRUE);

	*reloc_storage = NULL;

	g_return_val_if_fail (IS_SHEET (sheet), TRUE);
	g_return_val_if_fail (count != 0, TRUE);

	/* 0. Walk cells in displaced row and ensure arrays aren't divided. */
	if (row > 0)	/* No need to test leftmost column */
		if (sheet_cell_foreach_range (sheet, TRUE,
					      0, row,
					      SHEET_MAX_COLS-1, row,
					      &avoid_dividing_array_vertical,
					      NULL) != NULL) {
			gnumeric_error_splits_array (COMMAND_CONTEXT (wbc),
						     _("Insert Rows"));
			return TRUE;
		}

	/* Walk the lower edge to make sure nothing is split due to over run.  */
	if (sheet_cell_foreach_range (sheet, TRUE, 0, SHEET_MAX_ROWS-count,
				      SHEET_MAX_COLS-1, SHEET_MAX_ROWS-1,
				      &avoid_dividing_array_vertical,
				      NULL) != NULL){
		gnumeric_error_splits_array (COMMAND_CONTEXT (wbc),
					     _("Insert Rows"));
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

	/* 4. Slide the StyleRegions and SheetObjects down */
	sheet_relocate_objects (&reloc_info);
	sheet_style_insert_colrow (sheet, row, count, FALSE);

	/* 5. Recompute dependencies */
	sheet_recalc_dependencies (sheet);

	/* 6. Notify sheet of pending updates */
	sheet->priv->recompute_visibility = TRUE;
	sheet->priv->recompute_spans = TRUE;
	sheet_flag_status_update_range (sheet, &reloc_info.origin);
	if (sheet->priv->reposition_objects.row > row)
		sheet->priv->reposition_objects.row = row;

	return FALSE;
}

/*
 * sheet_delete_rows
 * @sheet   The sheet
 * @row     At which position we want to start deleting rows
 * @count   The number of rows to be deleted
 */
gboolean
sheet_delete_rows (WorkbookControl *wbc, Sheet *sheet,
		   int row, int count, GSList **reloc_storage)
{
	ExprRelocateInfo reloc_info;
	int i;

	g_return_val_if_fail (reloc_storage != NULL, TRUE);

	*reloc_storage = NULL;

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
		gnumeric_error_splits_array (COMMAND_CONTEXT (wbc),
					     _("Delete Rows"));
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

	/* 5. Slide the StyleRegions and SheetObjects up */
	sheet_relocate_objects (&reloc_info);
	sheet_style_delete_colrow (sheet, row, count, FALSE);

	/* 6. Recompute dependencies */
	sheet_recalc_dependencies (sheet);

	/* 7. Notify sheet of pending update */
	sheet->priv->recompute_visibility = TRUE;
	sheet->priv->recompute_spans = TRUE;
	sheet_flag_status_update_range (sheet, &reloc_info.origin);
	if (sheet->priv->reposition_objects.row > row)
		sheet->priv->reposition_objects.row = row;

	return FALSE;
}

void
sheet_move_range (WorkbookControl *wbc,
		  ExprRelocateInfo const *rinfo,
		  GSList **reloc_storage)
{
	GList *cells = NULL;
	Cell  *cell;
	Range  dst;
	gboolean inter_sheet_expr, out_of_range;

	g_return_if_fail (rinfo != NULL);
	g_return_if_fail (IS_SHEET (rinfo->origin_sheet));
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
		 * We need to be careful about invalidating references to the old
		 * content of the destination region.  We only invalidate references
		 * to regions that are actually lost.  However, this care is
		 * only necessary if the source and target sheets are the same.
		 *
		 * Handle dst cells being pasted over
		 */
		if (rinfo->origin_sheet == rinfo->target_sheet &&
		    range_overlap (&rinfo->origin, &dst))
			invalid = range_split_ranges (&rinfo->origin, &dst, NULL);
		else
			invalid = g_list_append (NULL, range_copy (&dst));

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
		sheet_clear_region (wbc, rinfo->target_sheet,
				    dst.start.col, dst.start.row,
				    dst.end.col, dst.end.row,
				    CLEAR_VALUES); /* Do not to clear styles, or objects */

	/* 5. Slide styles BEFORE the cells so that spans get computed properly */
	sheet_style_relocate (rinfo);

	/* 6. Insert the cells back */
	for (; cells != NULL ; cells = g_list_remove (cells, cell)) {
		cell = cells->data;

		/* check for out of bounds and delete if necessary */
		if ((cell->pos.col + rinfo->col_offset) >= SHEET_MAX_COLS ||
		    (cell->pos.row + rinfo->row_offset) >= SHEET_MAX_ROWS) {
			if (cell_has_expr (cell))
				dependent_unlink (CELL_TO_DEP (cell), &cell->pos);
#warning Remove objects
			cell_destroy (cell);
			continue;
		}

		/* Inter sheet movement requires the moving the expression too */
		inter_sheet_expr  = (cell->base.sheet != rinfo->target_sheet &&
				     cell_has_expr (cell));
		if (inter_sheet_expr)
			dependent_unlink (CELL_TO_DEP (cell), &cell->pos);

		/* Update the location */
		sheet_cell_insert (rinfo->target_sheet, cell,
				   cell->pos.col + rinfo->col_offset,
				   cell->pos.row + rinfo->row_offset, TRUE);

		if (inter_sheet_expr)
			dependent_link (CELL_TO_DEP (cell), &cell->pos);

		cell_relocate (cell, NULL);
		cell_content_changed (cell);
	}

	/* 7. Move objects in the range */
	sheet_relocate_objects (rinfo);

	/* 8. Recompute dependencies */
	sheet_recalc_dependencies (rinfo->target_sheet);

	/* 9. Notify sheet of pending update */
	rinfo->origin_sheet->priv->recompute_spans = TRUE;
	sheet_flag_status_update_range (rinfo->origin_sheet, &rinfo->origin);
}

static void
sheet_col_row_default_init (Sheet *sheet, double units, int margin_a, int margin_b,
			    gboolean is_horizontal, gboolean is_pts)
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
	if (is_pts) {
		cri->size_pts = units;
		colrow_compute_pixels_from_pts (sheet, cri, is_horizontal);
	} else {
		cri->size_pixels = units;
		colrow_compute_pts_from_pixels (sheet, cri, is_horizontal);
	}
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

	g_return_if_fail (IS_SHEET (sheet));
	g_return_if_fail (width_pts > 0.0);

	ci = sheet_col_fetch (sheet, col);
	ci->hard_size |= set_by_user;
	if (ci->size_pts == width_pts)
		return;

	ci->size_pts = width_pts;
	colrow_compute_pixels_from_pts (sheet, ci, TRUE);

	sheet->priv->recompute_visibility = TRUE;
	sheet->priv->recompute_spans = TRUE;
	if (sheet->priv->reposition_objects.col > col)
		sheet->priv->reposition_objects.col = col;
}

void
sheet_col_set_size_pixels (Sheet *sheet, int col, int width_pixels,
			   gboolean set_by_user)
{
	ColRowInfo *ci;

	g_return_if_fail (IS_SHEET (sheet));
	g_return_if_fail (width_pixels > 0.0);

	ci = sheet_col_fetch (sheet, col);
	ci->hard_size |= set_by_user;
	if (ci->size_pixels == width_pixels)
		return;

	ci->size_pixels = width_pixels;
	colrow_compute_pts_from_pixels (sheet, ci, TRUE);

	sheet->priv->recompute_visibility = TRUE;
	sheet->priv->recompute_spans = TRUE;
	if (sheet->priv->reposition_objects.col > col)
		sheet->priv->reposition_objects.col = col;
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

int
sheet_col_get_default_size_pixels (Sheet const *sheet)
{
	ColRowInfo const *ci;

	g_assert (sheet != NULL);

	ci = &sheet->cols.default_style;
	return  ci->size_pixels;
}

void
sheet_col_set_default_size_pts (Sheet *sheet, double width_pts)
{
	sheet_col_row_default_init (sheet, width_pts, 2, 2, TRUE, TRUE);
	sheet->priv->recompute_visibility = TRUE;
	sheet->priv->recompute_spans = TRUE;
	sheet->priv->reposition_objects.col = 0;
}
void
sheet_col_set_default_size_pixels (Sheet *sheet, int width_pixels)
{
	sheet_col_row_default_init (sheet, width_pixels, 2, 2, TRUE, FALSE);
	sheet->priv->recompute_visibility = TRUE;
	sheet->priv->recompute_spans = TRUE;
	sheet->priv->reposition_objects.col = 0;
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
		ColRowSegment const *segment =
			COLROW_GET_SEGMENT(&(sheet->rows), i);

		if (segment != NULL) {
			ColRowInfo const *ri = segment->info [COLROW_SUB_INDEX(i)];
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
		ColRowSegment const *segment =
			COLROW_GET_SEGMENT(&(sheet->rows), i);

		if (segment != NULL) {
			ColRowInfo const *ri = segment->info[COLROW_SUB_INDEX(i)];
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

	g_return_if_fail (IS_SHEET (sheet));
	g_return_if_fail (height_pts > 0.0);

	ri = sheet_row_fetch (sheet, row);
	ri->hard_size |= set_by_user;
	if (ri->size_pts == height_pts)
		return;

	ri->size_pts = height_pts;
	colrow_compute_pixels_from_pts (sheet, ri, FALSE);

	sheet->priv->recompute_visibility = TRUE;
	if (sheet->priv->reposition_objects.row > row)
		sheet->priv->reposition_objects.row = row;
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
 */
void
sheet_row_set_size_pixels (Sheet *sheet, int row, int height_pixels,
			   gboolean set_by_user)
{
	ColRowInfo *ri;

	g_return_if_fail (IS_SHEET (sheet));
	g_return_if_fail (height_pixels > 0);

	ri = sheet_row_fetch (sheet, row);
	ri->hard_size |= set_by_user;
	if (ri->size_pixels == height_pixels)
		return;

	ri->size_pixels = height_pixels;
	colrow_compute_pts_from_pixels (sheet, ri, FALSE);

	sheet->priv->recompute_visibility = TRUE;
	if (sheet->priv->reposition_objects.row > row)
		sheet->priv->reposition_objects.row = row;
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

int
sheet_row_get_default_size_pixels (Sheet const *sheet)
{
	ColRowInfo const *ci;

	g_assert (sheet != NULL);

	ci = &sheet->rows.default_style;
	return  ci->size_pixels;
}

void
sheet_row_set_default_size_pts (Sheet *sheet, double height_pts)
{
	sheet_col_row_default_init (sheet, height_pts, 1, 0, FALSE, TRUE);
	sheet->priv->recompute_visibility = TRUE;
	sheet->priv->reposition_objects.row = 0;
}
void
sheet_row_set_default_size_pixels (Sheet *sheet, int height_pixels)
{
	sheet_col_row_default_init (sheet, height_pixels, 1, 0, FALSE, FALSE);
	sheet->priv->recompute_visibility = TRUE;
	sheet->priv->reposition_objects.row = 0;
}

/****************************************************************************/

void
sheet_create_edit_cursor (Sheet *sheet)
{
	GList *l;

	for (l = sheet->s_controls; l; l = l->next){
		GnumericSheet *gsheet = GNUMERIC_SHEET_CONTROL_GUI (l->data);
		gnumeric_sheet_create_editing_cursor (gsheet);
	}
}
void
sheet_stop_editing (Sheet *sheet)
{
	GList *l;
	for (l = sheet->s_controls; l; l = l->next){
		GnumericSheet *gsheet = GNUMERIC_SHEET_CONTROL_GUI (l->data);

		gnumeric_sheet_stop_editing (gsheet);
	}
}

void
sheet_destroy_cell_select_cursor (Sheet *sheet, gboolean clear_string)
{
	GList *l;
	for (l = sheet->s_controls; l; l = l->next){
		GnumericSheet *gsheet = GNUMERIC_SHEET_CONTROL_GUI (l->data);

		gnumeric_sheet_stop_cell_selection (gsheet, clear_string);
	}
}

typedef struct
{
	gboolean is_column;
	Sheet *sheet;
} closure_clone_colrow;

static gboolean
sheet_clone_colrow_info_item (ColRowInfo *info, void *user_data)
{
	ColRowInfo *new_colrow;
	closure_clone_colrow * closure = user_data;

	if (closure->is_column)
		new_colrow = sheet_col_new (closure->sheet);
	else
		new_colrow = sheet_row_new (closure->sheet);

	new_colrow->pos         = info->pos;
	new_colrow->margin_a    = info->margin_a;
	new_colrow->margin_b    = info->margin_b;
	new_colrow->hard_size   = info->hard_size;
	new_colrow->visible     = info->visible;

	if (closure->is_column) {
		sheet_col_add (closure->sheet, new_colrow);
		sheet_col_set_size_pts (closure->sheet, new_colrow->pos, info->size_pts, new_colrow->hard_size);
	} else {
		sheet_row_add (closure->sheet, new_colrow);
		sheet_row_set_size_pts (closure->sheet, new_colrow->pos, info->size_pts, new_colrow->hard_size);
	}

	return FALSE;
}

static void
sheet_clone_colrow_info (Sheet const *src, Sheet *dst)
{
	closure_clone_colrow closure;

	closure.sheet = dst;
	closure.is_column = TRUE;
	col_row_foreach (&src->cols, 0, SHEET_MAX_COLS-1,
			 &sheet_clone_colrow_info_item, &closure);
	closure.is_column = FALSE;
	col_row_foreach (&src->rows, 0, SHEET_MAX_ROWS-1,
			 &sheet_clone_colrow_info_item, &closure);
}


static void
sheet_clone_style_region (Sheet *sheet, StyleRegion const *region)
{
	g_return_if_fail (region->style != NULL);

	sheet_style_attach (sheet, region->range, mstyle_copy(region->style));
}

static void
sheet_clone_styles (Sheet const *src, Sheet *dst)
{
	GList *style_regions, *ptr;

	style_regions = sheet_get_style_list (src);
	style_regions = g_list_reverse (style_regions);

	if (!style_regions)
		return;

	for (ptr = style_regions; ptr != NULL; ptr = ptr->next) {
		StyleRegion const *region = ptr->data;
		sheet_clone_style_region (dst, region);
	}

	g_list_free (style_regions);
}

static void
sheet_clone_selection (Sheet const *src, Sheet *dst)
{
	GList *selections, *ptr;

	if (src->selections == NULL)
		return;

	/* A new sheet has A1 selected by default */
	sheet_selection_reset_only (dst);

	selections = g_list_copy (src->selections);
	selections = g_list_reverse (selections);
	for (ptr = selections ; ptr != NULL && ptr->next != NULL ; ptr = ptr->next) {
		Range const *range = ptr->data;
		g_return_if_fail (range != NULL);
		sheet_selection_add_range (dst,
					   range->start.col, range->start.row,
					   range->start.col, range->start.row,
					   range->end.col,   range->end.row);
	}
	g_list_free (selections);
	sheet_selection_add_range (dst,
				   src->cursor.edit_pos.col,
				   src->cursor.edit_pos.row,
				   src->cursor.base_corner.col,
				   src->cursor.base_corner.row,
				   src->cursor.move_corner.col,
				   src->cursor.move_corner.row);
}

static void
sheet_clone_names (Sheet const *src, Sheet *dst)
{
	GList *names = g_list_copy (src->names);;

	if (src->names == NULL)
		return;

#if 0	/* Feature not implemented, not cloning it yet. */
	for (; names; names = names->next) {
		NamedExpression *expresion = names->data;
		gchar *text;
		gchar *error;
		g_return_if_fail (expresion != NULL);
		text = expr_name_value (expresion);
		if (!expr_name_create (dst->workbook, dst, expresion->name->str, text, &error))
			g_warning ("Could not create expression. Sheet.c :%i, Error %s",
				   __LINE__, error);
	}
#endif
	g_free (names);
}

static void
sheet_clone_objects (Sheet const *src, Sheet *dst)
{
	/* TODO: Clone objects */
}

static void
cb_sheet_cell_copy (gpointer unused, gpointer key, gpointer new_sheet_param)
{
	Cell const *cell = key;
	Sheet *dst = (Sheet *) new_sheet_param;
	Cell  *new_cell;
	gboolean is_expr;

	g_return_if_fail (dst != NULL);
	g_return_if_fail (cell != NULL);

	is_expr = cell_has_expr (cell);
	if (is_expr) {
		ExprArray const* array = cell_is_array (cell);
		if (array != NULL) {
			if (array->x == 0 && array->y == 0) {
				ExprTree *expr = array->corner.func.expr;
				expr_tree_ref (expr);
				cell_set_array_formula (dst,
							cell->pos.row, cell->pos.col,
							cell->pos.row + array->rows-1,
							cell->pos.col + array->cols-1,
							expr,

							/* FIXME : We should copy the values */
							TRUE);
			}
			return;
		}
	}

	new_cell = cell_copy (cell);
	sheet_cell_insert (dst, new_cell,
			   cell->pos.col, cell->pos.row, FALSE);
	if (is_expr)
		dependent_link (CELL_TO_DEP (new_cell), &cell->pos);
}

static void
sheet_clone_cells (Sheet const *src, Sheet *dst)
{
	g_hash_table_foreach (src->cell_hash, &cb_sheet_cell_copy, dst);
}

Sheet *
sheet_duplicate	(Sheet const *src)
{
	Workbook *wb;
	Sheet *dst;
	char *name;

	g_return_val_if_fail (src != NULL, NULL);
	g_return_val_if_fail (src->workbook !=NULL, NULL);

	wb = src->workbook;
	name = workbook_sheet_get_free_name (wb, src->name_unquoted,
					     TRUE, TRUE);
	dst = sheet_new (wb, name);
	g_free (name);

        /* Copy the print info */
	print_info_free (dst->print_info);
	dst->print_info = print_info_copy (src->print_info);

	sheet_clone_styles         (src, dst);
	sheet_clone_colrow_info    (src, dst);
	sheet_clone_selection      (src, dst);
	sheet_clone_names          (src, dst);
	sheet_clone_objects        (src, dst);
	sheet_clone_cells          (src, dst);

	/* Copy the solver */
	solver_lp_copy (&src->solver_parameters, dst);

	/* Force a respan and rerender */
	sheet_set_zoom_factor (dst, src->last_zoom_factor_used, TRUE, TRUE);

	sheet_set_dirty (dst, TRUE);
	sheet_redraw_all (dst);

	return dst;
}

/***********************************************************************************/

static gint
range_row_cmp (Range const *a, Range const *b)
{
	int tmp = b->start.row - a->start.row;
	if (tmp == 0)
		tmp = a->start.col - b->start.col; /* YES I DO MEAN a - b */
	return tmp;
}

/**
 * sheet_region_merge :
 *
 * @cc : command context
 * @sheet : the sheet which will contain the region
 * @src : The region to merge
 *
 * Add a range to the list of merge targets.  Checks for array spliting
 * returns TRUE if there was an error.
 */
gboolean
sheet_region_merge (CommandContext *cc, Sheet *sheet, Range const *range)
{
	GSList *test;
	Range  *r_copy;

	g_return_val_if_fail (IS_SHEET (sheet), TRUE);
	g_return_val_if_fail (range != NULL, TRUE);

	if (sheet_range_splits_array (sheet, range)) {
		gnumeric_error_splits_array (cc, _("Merge"));
		return TRUE;
	}

	test = sheet_region_get_merged (sheet, range);
	if (test != NULL) {
		gnumeric_error_invalid (cc,
			_("There is already a merged region that intersects"),
			range_name (range));
		g_slist_free (test);
		return TRUE;
	}

	r_copy = range_copy (range);
	g_hash_table_insert (sheet->hash_merged, &r_copy->start, r_copy);

	/* Store in order from bottom to top then LEFT TO RIGHT (by start coord) */
	sheet->list_merged = g_slist_insert_sorted (sheet->list_merged, r_copy,
						    (GCompareFunc)range_row_cmp);

	return FALSE;
}

/**
 * sheet_region_unmerge :
 *
 * @cc    : command context
 * @sheet : the sheet which will contain the region
 * @range     : The region
 *
 * Remove a merged range.
 * returns TRUE if there was an error.
 */
gboolean
sheet_region_unmerge (CommandContext *cc, Sheet *sheet, Range const *range)
{
	Range *r_copy;

	g_return_val_if_fail (IS_SHEET (sheet), TRUE);
	g_return_val_if_fail (range != NULL, TRUE);

	r_copy = g_hash_table_lookup (sheet->hash_merged, &range->start);

	g_return_val_if_fail (r_copy != NULL, TRUE);
	g_return_val_if_fail (range_equal (range, r_copy), TRUE);

	g_hash_table_remove (sheet->hash_merged, &r_copy->start);
	sheet->list_merged = g_slist_remove (sheet->list_merged, r_copy);
	g_free (r_copy);

	return FALSE;
}

/**
 * sheet_region_get_merged :
 *
 * Returns a list of the merged regions that overlap the target region.
 * The list is ordered from top to bottom and RIGHT TO LEFT (by start coord).
 * Caller is responsible for freeing the list, but not the content.
 */
GSList *
sheet_region_get_merged (Sheet *sheet, Range const *range)
{
	GSList *ptr, *res = NULL;

	g_return_val_if_fail (IS_SHEET (sheet), NULL);
	g_return_val_if_fail (range != NULL, NULL);

	for (ptr = sheet->list_merged ; ptr != NULL ; ptr = ptr->next) {
		Range * const test = ptr->data;

		if (range_overlap (range, test))
			res = g_slist_prepend (res, test);
	}

	return res;
}

/**
 * sheet_region_adjacent_merge
 * @sheet : The sheet to look in.
 * @pos : the cell to test for adjacent regions.
 * @left : the return for a region on the left
 * @right : the return for a region on the right
 *
 * Returns the nearest regions to either side of @pos.
 */
void
sheet_region_adjacent_merge (Sheet const *sheet, CellPos const *pos,
			     Range const **left, Range const **right)
{
	GSList *ptr;

	g_return_if_fail (IS_SHEET (sheet));
	g_return_if_fail (pos != NULL);

	*left = *right = NULL;
	for (ptr = sheet->list_merged ; ptr != NULL ; ptr = ptr->next) {
		Range const * const test = ptr->data;
		if (test->start.row <= pos->row && pos->row <= test->end.row) {
			int const diff = test->end.col - pos->col;

			g_return_if_fail (diff != 0);

			if (diff < 0) {
				if (*left == NULL || (*left)->end.col < test->end.col)
					*left = test;
			} else {
				if (*right == NULL || (*right)->start.col > test->start.col)
					*right = test;
			}
		}
	}
}

/**
 * sheet_region_is_merge_cell :
 *
 * @sheet :
 * @pos : cellpos if top left corner
 *
 * Returns a Range pointer if the @pos is the topleft of a merged region.
 * The pointer should NOT be freed by the caller.
 */
Range const *
sheet_region_is_merge_cell (Sheet const *sheet, CellPos const *pos)
{
	g_return_val_if_fail (IS_SHEET (sheet), NULL);
	g_return_val_if_fail (pos != NULL, NULL);

	return g_hash_table_lookup (sheet->hash_merged, pos);
}
