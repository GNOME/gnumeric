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
#include "gnumeric-sheet.h"
#include "utils.h"
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
#include "sheet-private.h"

#ifdef ENABLE_BONOBO
#    include <libgnorba/gnorba.h>
#endif

#undef DEBUG_CELL_FORMULA_LIST

#define GNUMERIC_SHEET_VIEW(p) GNUMERIC_SHEET (SHEET_VIEW(p)->sheet_view);

/* The size, mask, and shift must be kept in sync */
#define COLROW_SEGMENT_SIZE	0x80
#define COLROW_SUB_INDEX(i)	((i) & 0x7f)
#define COLROW_SEGMENT_INDEX(i)	((i) >> 7)
#define COLROW_GET_SEGMENT(seg_array, i) \
	(g_ptr_array_index ((seg_array)->info, COLROW_SEGMENT_INDEX(i)))

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
	const CellPos *ca = (const CellPos *) key;

	return (ca->row << 8) | ca->col;
}

static gint
cell_compare (CellPos const * a, CellPos const * b)
{
	return (a->row == b->row && a->col == b->col);
}

void
sheet_rename (Sheet *sheet, const char *new_name)
{
	g_return_if_fail (sheet != NULL);
	g_return_if_fail (IS_SHEET (sheet));
	g_return_if_fail (new_name != NULL);

	g_free (sheet->name);
	sheet->name = g_strdup (new_name);
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

Sheet *
sheet_new (Workbook *wb, const char *name)
{
	GtkWidget *sheet_view;
	Sheet  *sheet;
	MStyle *mstyle;

	g_return_val_if_fail (wb != NULL, NULL);
	g_return_val_if_fail (name != NULL, NULL);

	sheet = g_new0 (Sheet, 1);
	sheet->private = g_new0 (SheetPrivate, 1);
	sheet->signature = SHEET_SIGNATURE;
	sheet->workbook = wb;
	sheet->name = g_strdup (name);
	sheet_create_styles (sheet);
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

/**
 * sheet_foreach_colrow:
 * @sheet	the sheet
 * @infos	The Row or Column collection.
 * @start	start position (inclusive)
 * @end		stop column (inclusive)
 * @callback	A callback function which should return TRUE to stop
 *              the iteration.
 * @user_data	A bagage pointer.
 *
 * Iterates through the existing rows or columns within the range supplied.
 * Currently only support left -> right iteration.  If a callback returns
 * TRUE iteration stops.
 */
void
sheet_foreach_colrow (Sheet *sheet, ColRowCollection *infos,
		      int start, int stop,
		      sheet_col_row_callback callback, void *user_data)
{
	int i;

	/* TODO : Do we need to support right -> left as an option */
	if (stop > infos->max_used)
		stop = infos->max_used;

	i = start;
	while (i <= stop) {
		ColRowInfo **segment = COLROW_GET_SEGMENT (infos, i);
		int sub = COLROW_SUB_INDEX(i);

		i += COLROW_SEGMENT_SIZE - sub;
		if (segment != NULL)
			for (; sub < COLROW_SEGMENT_SIZE; ++sub) {
				ColRowInfo * info = segment[sub];
				if (info != NULL)
					if ((*callback)(sheet, info, user_data))
						return;
			}
	}
}

static gboolean
colrow_compute_pixels_from_pts (Sheet *sheet, ColRowInfo *info, void *data)
{
	double const scale =
	    sheet->last_zoom_factor_used *
	    application_display_dpi_get ((gboolean)data) / 72.;

	/* 1) round to the nearest pixel
	 * 2) XL appears to scale including the margins & grid lines
	 *    but not the scale them.  So the size of the cell changes ???
	 */
	info->size_pixels = (int)(info->size_pts * scale + 0.5);

	return FALSE;
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

void
sheet_set_zoom_factor (Sheet *sheet, double const f)
{
	GList *l, *cl;

	/* Bound zoom between 10% and 500% */
	double const factor = (f < .1) ? .1 : ((f > 5.) ? 5. : f);
	double const diff = sheet->last_zoom_factor_used - factor;

	if (-.0001 < diff && diff < .0001)
		return;

	sheet->last_zoom_factor_used = factor;

	/* First, the default styles */
	colrow_compute_pixels_from_pts (sheet, &sheet->rows.default_style, (void*)FALSE);
	colrow_compute_pixels_from_pts (sheet, &sheet->cols.default_style, (void*)TRUE);

	/* Then every column and row */
	sheet_foreach_colrow (sheet, &sheet->rows, 0, SHEET_MAX_ROWS-1,
			      &colrow_compute_pixels_from_pts, (void*)FALSE);
	sheet_foreach_colrow (sheet, &sheet->cols, 0, SHEET_MAX_COLS-1,
			      &colrow_compute_pixels_from_pts, (void*)TRUE);

	for (l = sheet->sheet_views; l; l = l->next){
		SheetView *sheet_view = l->data;

		sheet_view_set_zoom_factor (sheet_view, factor);
	}

	for (cl = sheet->comment_list; cl; cl = cl->next){
		Cell *cell = cl->data;

		cell_comment_reposition (cell);
	}

	/*
	 * FIXME: this slugs zoom performance and should not
	 * be neccessary if we get rendering right IMHO.
	 */
	sheet_cells_update (sheet, sheet_get_full_range (), FALSE);
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
		GList *l;

		sheet->cols.max_used = col;

		for (l = sheet->sheet_views; l; l = l->next){
			SheetView *sheet_view = l->data;
			sheet_view_scrollbar_config (sheet_view);
		}
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
		GList *l;

		sheet->rows.max_used = row;

		for (l = sheet->sheet_views; l; l = l->next){
			SheetView *sheet_view = l->data;
			sheet_view_scrollbar_config (sheet_view);
		}
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

		if (cell->col->pos >= start_col ||
		    cell->row->pos >= start_row)
			cell_comment_reposition (cell);
	}
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

	p = sheet->private;

	if (p->reposition_row_comment < SHEET_MAX_ROWS ||
	    p->reposition_col_comment < SHEET_MAX_COLS) {
		sheet_reposition_comments (sheet,
					   p->reposition_row_comment,
					   p->reposition_col_comment);
		p->reposition_row_comment = SHEET_MAX_COLS;
		p->reposition_col_comment = SHEET_MAX_ROWS;
	}

	if (p->recompute_visibility) {
		/* TODO : There is room for some opimization
		 * We only need to force complete visibility recalculation
		 * (which we do in sheet_compute_visible_ranges by passing TRUE)
		 * if a row or col before the start of the visible range.
		 * If we are REALLY smart we could even accumulate the size differential
		 * and use that.
		 */
		p->recompute_visibility = FALSE;
		sheet_compute_visible_ranges (sheet);
		sheet_redraw_all (sheet);
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
	CellPos cellpos;

	g_return_val_if_fail (sheet != NULL, NULL);
	g_return_val_if_fail (IS_SHEET (sheet), NULL);

	cellpos.col = col;
	cellpos.row = row;
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
	
	if (cell && !cell_is_blank (cell)) {
		Range *range = (Range *)data;
		CellSpanInfo const *span = NULL;
		int tmp;

		if (cell->row->pos < range->start.row)
			range->start.row = cell->row->pos;

		if (cell->row->pos > range->end.row)
			range->end.row = cell->row->pos;

		span = row_span_get (cell->row, cell->row->pos);
		tmp = (span != NULL) ? span->left : cell->col->pos;
		if (tmp < range->start.col)
			range->start.col = tmp;

		tmp = (span != NULL) ? span->right : cell->col->pos;
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

	g_hash_table_foreach(sheet->cell_hash, sheet_get_extent_cb, &r);

	if (r.start.col >= SHEET_MAX_COLS - 2)
		r.start.col = 0;
	if (r.start.row >= SHEET_MAX_ROWS - 2)
		r.start.row = 0;
	if (r.end.col < 0)
		r.end.col = 0;
	if (r.end.row < 0)
		r.end.row = 0;

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
	int const width = cell->width_pixel;
	if (width > *max)
		*max = width;
	return NULL;
}

/**
 * sheet_col_size_fit_pixels:
 * @sheet: The sheet
 * @col: the column that we want to query
 *
 * This routine computes the ideal size for the column to make all data fit
 * properly.  Return value is in size_pixels
 */
int
sheet_col_size_fit_pixels (Sheet *sheet, int col)
{
	ColRowInfo *ci;
	int max = -1;
	
	g_return_val_if_fail (sheet != NULL, 0);
	g_return_val_if_fail (IS_SHEET (sheet), 0);

	ci = sheet_col_get_info (sheet, col);
	if (ci == NULL)
		return 0;

	/*
	 * If ci == sheet->cols.default_style then it means
	 * no cells have been allocated here
	 */
	if (ci == &sheet->cols.default_style)
		return ci->size_pixels;

	sheet_cell_foreach_range (sheet, TRUE,
				  col, 0,
				  col, SHEET_MAX_ROWS-1,
				  (sheet_cell_foreach_callback)&cb_max_cell_width, &max);

	/* Reset to the default width if the column was empty */
	if (max < 0)
		max = sheet->cols.default_style.size_pixels;
	else
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
	int const height = cell->height_pixel;
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
 * Return : Size in pixels INCLUDING the margins and grid lines.
 */
int
sheet_row_size_fit_pixels (Sheet *sheet, int row)
{
	ColRowInfo *ri;
	int max = -1;
	
	g_return_val_if_fail (sheet != NULL, 0);
	g_return_val_if_fail (IS_SHEET (sheet), 0);

	ri = sheet_row_get_info (sheet, row);
	if (ri == NULL)
		return 0;

	/*
	 * If ri == sheet->rows.default_style then it means
	 * no cells have been allocated here
	 */
	if (ri == &sheet->rows.default_style)
		return ri->size_pixels;

	sheet_cell_foreach_range (sheet, TRUE,
				  0, row,
				  SHEET_MAX_COLS-1, row,
				  (sheet_cell_foreach_callback)&cb_max_cell_height, &max);

	/* Reset to the default width if the column was empty */
	if (max < 0)
		max = sheet->rows.default_style.size_pixels;
	else
		/* Cell height does not include margins or far grid line */
		max += ri->margin_a + ri->margin_b + 1;

	return max;
}

typedef struct {
	int col;
	GList * cells;
} closure_cells_in_col;

static gboolean
cb_collect_cells_in_col (Sheet *sheet, ColRowInfo *ri, closure_cells_in_col *dat)
{
	Cell * cell = row_cell_get_displayed_at (ri, dat->col);
	if (cell)
		dat->cells = g_list_prepend (dat->cells, cell);
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
	GList *l;
	closure_cells_in_col dat;
	dat.col = col;
	dat.cells = NULL;

	sheet_foreach_colrow (sheet, &sheet->rows,
			      0, SHEET_MAX_ROWS-1,
			      (sheet_col_row_callback) &cb_collect_cells_in_col,
			      &dat);

	/* No spans, just return */
	if (!dat.cells)
		return;

	/* Unregister those cells that touched this column */
	for (l = dat.cells; l; l = l->next){
		Cell * cell = l->data;
		int left, right;

		cell_unregister_span (cell);
		cell_calculate_span (cell, &left, &right);
		if (left != right)
			cell_register_span (cell, left, right);
	}

	g_list_free (dat.cells);
}

void
sheet_update_auto_expr (Sheet *sheet)
{
	Value *v;
	Workbook *wb = sheet->workbook;

	g_return_if_fail (sheet != NULL);
	g_return_if_fail (IS_SHEET (sheet));

	/* defaults */
	v = NULL;

	if (wb->auto_expr) {
		EvalPosition pos;
		eval_pos_init (&pos, sheet, 0, 0);
		v = eval_expr (&pos, wb->auto_expr);
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

/*
 * Callback for sheet_cell_foreach_range to assign some text
 * to a range.
 */
static Value *
cb_set_cell_text (Sheet *sheet, int col, int row, Cell *cell,
		  void *user_data)
{
	if (cell == NULL)
		cell = sheet_cell_new (sheet, col, row);
	cell_set_text (cell, user_data);
	return NULL;
}

typedef struct {
	char * format;
	float_t val;
} closure_set_cell_value;

static Value *
cb_set_cell_value (Sheet *sheet, int col, int row, Cell *cell,
		  void *user_data)
{
	MStyle                 *mstyle;
	MStyle                 *old_mstyle;
	closure_set_cell_value *info = user_data;

	if (cell == NULL)
		cell = sheet_cell_new (sheet, col, row);

	old_mstyle = sheet_style_compute (sheet, col, row);

	/*
	 * FIXME: what if they want it to be General ?
	 */
	if (!strcmp (mstyle_get_format (old_mstyle)->format,
		     "General")) {
		mstyle = mstyle_new ();
		mstyle_set_format (mstyle, info->format);
		sheet_style_attach_single (sheet, col, row, mstyle);
	}

	cell_set_value (cell, value_new_float (info->val));
	return NULL;
}

void
sheet_set_text (Sheet *sheet, char const *text, Range const * r)
{
	g_return_if_fail (sheet != NULL);
	g_return_if_fail (IS_SHEET (sheet));

	/* If its not a formula see if there is a prefered format. */
	if (!gnumeric_char_start_expr_p (*text) || text[1] == '\0' || text[0] == '\'') {
		closure_set_cell_value	closure;
		char *end;

		strtod (text, &end);
		if (end != text && *end == 0) {
			/*
			 * It is a number -- remain in General format.  Note
			 * that we would otherwise actually set a "0" format
			 * for integers and that it would stick.
			 */
		} else if (format_match (text, &closure.val, &closure.format)) {
			sheet_cell_foreach_range (sheet, FALSE,
						  r->start.col, r->start.row,
						  r->end.col, r->end.row,
						  &cb_set_cell_value, &closure);
			return;
		}
	}
	sheet_cell_foreach_range (sheet, FALSE,
				  r->start.col, r->start.row,
				  r->end.col, r->end.row,
				  &cb_set_cell_text, (void *)text);
}

/**
 * Load the edit line with the value of the cell under the cursor
 * for @sheet.
 */
void
sheet_load_cell_val (Sheet *sheet)
{
	GtkEntry *entry;
	Cell     *cell;
	char     *text;

	g_return_if_fail (sheet != NULL);
	g_return_if_fail (IS_SHEET (sheet));

	entry = GTK_ENTRY (sheet->workbook->ea_input);
	cell = sheet_cell_get (sheet,
			       sheet->cursor.edit_pos.col,
			       sheet->cursor.edit_pos.row);

	if (cell)
		text = cell_get_text (cell);
	else
		text = g_strdup ("");

	gtk_entry_set_text (entry, text);

	/* FIXME : Nothing uses this ???? */
	gtk_signal_emit_by_name (GTK_OBJECT (sheet->workbook), "cell_changed",
				 sheet, text,
				 sheet->cursor.edit_pos.col,
				 sheet->cursor.edit_pos.row);

	g_free (text);
}

/**
 * sheet_update_controls:
 *
 * This routine is run every time the selection has changed.  It checks
 * what the status of various toolbar feedback controls should be
 */
void
sheet_update_controls (Sheet *sheet)
{
	MStyle *mstyle;

	g_return_if_fail (sheet != NULL);

	mstyle = sheet_style_compute (sheet,
				      sheet->cursor.edit_pos.col,
				      sheet->cursor.edit_pos.row);

	workbook_feedback_set (sheet->workbook, mstyle);
	mstyle_unref (mstyle);
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

	g_return_if_fail (sheet != NULL);
	g_return_if_fail (IS_SHEET (sheet));

	for (l = sheet->sheet_views; l; l = l->next){
		SheetView *sheet_view = l->data;

		sheet_view_redraw_cell_region (
			sheet_view,
			start_col, start_row,
			end_col, end_row);
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
	gboolean find_first = cell_is_blank (sheet_cell_get (sheet, start_col, row));
	int new_col = start_col, prev_col = start_col;
	gboolean keep_looking = FALSE;
	int iterations = 0;

	g_return_val_if_fail (sheet != NULL, start_col);
	/* Jumping to bounds requires steping cell by cell */
	g_return_val_if_fail (count == 1 || count == -1 || !jump_to_boundaries, start_col);

	do
	{
		new_col += count;
		++iterations;
		keep_looking = FALSE;

		if (new_col < 0)
			return 0;
		if (new_col >= SHEET_MAX_COLS)
			return SHEET_MAX_COLS-1;
		if (jump_to_boundaries) {
			keep_looking = (cell_is_blank (sheet_cell_get (sheet, new_col, row)) == find_first);
			if (keep_looking)
				prev_col = new_col;
			else if (!find_first) {
				/*
				 * Handle special case where we are on the last
				 * non-null cell
				 */
				if (iterations == 1)
					keep_looking = find_first = TRUE;
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
	gboolean find_first = cell_is_blank (sheet_cell_get (sheet, col, start_row));
	int new_row = start_row, prev_row = start_row;
	gboolean keep_looking = FALSE;
	int iterations = 0;

	/* Jumping to bounds requires steping cell by cell */
	g_return_val_if_fail (count == 1 || count == -1 || !jump_to_boundaries, start_row);

	do
	{
		new_row += count;
		++iterations;
		keep_looking = FALSE;

		if (new_row < 0)
			return 0;
		if (new_row > SHEET_MAX_ROWS-1)
			return SHEET_MAX_ROWS-1;
		if (jump_to_boundaries) {
			keep_looking = (cell_is_blank (sheet_cell_get (sheet, col, new_row)) == find_first);
			if (keep_looking)
				prev_row = new_row;
			else if (!find_first) {
				/*
				 * Handle special case where we are on the last
				 * non-null cell
				 */ 
				if (iterations == 1)
					keep_looking = find_first = TRUE;
				else
					new_row = prev_row;
			}
		}
	} while (keep_looking || sheet_row_is_hidden (sheet, new_row));

	return new_row;
}

static ArrayRef *
sheet_is_cell_array (Sheet *sheet, int const col, int const row)
{
	Cell * const cell = sheet_cell_get (sheet, col, row);

	if (cell != NULL &&
	    cell->parsed_node != NULL &&
	    cell->parsed_node->oper == OPER_ARRAY)
		return &cell->parsed_node->u.array;

	return NULL;
}

/*
 * Check the outer edges of range to ensure that if an
 * array is within it then the entire array is within the range.
 *
 * returns FALSE is an array would be divided.
 */
gboolean
sheet_check_for_partial_array (Sheet *sheet,
			       int const start_row, int const start_col,
			       int end_row, int end_col)
{
	ArrayRef *a;
	gboolean valid = TRUE;
	gboolean single;
	int r, c;

	if (end_col > sheet->cols.max_used)
		end_col = sheet->cols.max_used;
	if (end_row > sheet->rows.max_used)
		end_row = sheet->rows.max_used;

	if (start_row > 0 || end_row < SHEET_MAX_ROWS-1)
	{
		/* Check top & bottom */
		single = (start_row == end_row);
		for (c = start_col; c <= end_col && valid; ++c){
			if ((a = sheet_is_cell_array (sheet, c, start_row)) != NULL)
				valid &= (a->y == 0);		/* Top */
			if (!single)
				a = sheet_is_cell_array (sheet, c, end_row);
			if (a != NULL)
				valid &= (a->y == (a->rows-1));	/* Bottom */
		}
	}

	if (start_col <= 0 && end_col >= SHEET_MAX_COLS-1)
		return valid; /* No need to check */

	/* Check left & right */
	single = (start_col == end_col);
	for (r = start_row; r <= end_row && valid; ++r){
		if ((a = sheet_is_cell_array (sheet, start_col, r)) != NULL)
			valid &= (a->x == 0);		/* Left */
		if ((a=sheet_is_cell_array (sheet, end_col, r)))
			valid &= (a->x == (a->cols-1)); /* Right */
	}

	return valid;
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
sheet_cell_foreach_range (Sheet *sheet, int only_existing,
			  int start_col, int start_row,
			  int end_col, int end_row,
			  sheet_cell_foreach_callback callback,
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
			if (!only_existing)
				for (j = start_row; j <= end_row ; ++j) {
					cont = (*callback)(sheet, i, j, NULL, closure);
					if (cont != NULL)
						return cont;
				}

			continue;
		}

		for (j = start_row; j <= end_row ; ++j) {
			ColRowInfo *ri = sheet_row_get (sheet, j);
			Cell * cell = NULL;

			if (ri != NULL)
				cell = sheet_cell_get (sheet, i, j);

			if (cell != NULL || !only_existing) {
				cont = (*callback)(sheet, i, j, cell, closure);
				if (cont != NULL)
					return cont;
			}
		}
	}
	return NULL;
}

static Value *
fail_if_not_selected (Sheet *sheet, int col, int row, Cell *cell, void *user_data)
{
	if (!sheet_selection_is_cell_selected (sheet, col, row))
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
	CellPos *cellpos;
	Cell *cell_on_spot;
	int left, right;

	g_return_if_fail (cell->col != NULL);
	g_return_if_fail (cell->col->pos < SHEET_MAX_COLS);
	g_return_if_fail (cell->row != NULL);
	g_return_if_fail (cell->row->pos < SHEET_MAX_ROWS);

	/* See if another cell was displaying in our spot */
	cell_on_spot = row_cell_get_displayed_at (cell->row, cell->col->pos);
	if (cell_on_spot)
		cell_unregister_span (cell_on_spot);

	cellpos = g_new (CellPos, 1);
	cellpos->col = cell->col->pos;
	cellpos->row = cell->row->pos;

	g_hash_table_insert (sheet->cell_hash, cellpos, cell);

	/*
	 * Now register the sizes of our cells
	 */
	if (cell_on_spot){
		cell_calculate_span (cell_on_spot, &left, &right);
		if (left != right)
			cell_register_span (cell_on_spot, left, right);
	}
	cell_calculate_span (cell, &left, &right);
	if (left != right)
		cell_register_span (cell, left, right);
}

void
sheet_cell_add (Sheet *sheet, Cell *cell, int col, int row)
{
	cell->sheet = sheet;
	cell->col   = sheet_col_fetch (sheet, col);
	cell->row   = sheet_row_fetch (sheet, row);

	cell_realize (cell);
	cell_calc_dimensions  (cell);

	sheet_cell_add_to_hash (sheet, cell);

	cell_add_dependencies (cell);
}

Cell *
sheet_cell_new (Sheet *sheet, int col, int row)
{
	Cell *cell;

	g_return_val_if_fail (sheet != NULL, NULL);
	g_return_val_if_fail (IS_SHEET (sheet), NULL);

	cell = g_new0 (Cell, 1);

	sheet_cell_add (sheet, cell, col, row);
	return cell;
}

static void
sheet_cell_remove_from_hash (Sheet *sheet, Cell *cell)
{
	CellPos  cellpos;
	gpointer original_key;

	cellpos.col = cell->col->pos;
	cellpos.row = cell->row->pos;

	cell_unregister_span   (cell);
	cell_drop_dependencies (cell);

	if (g_hash_table_lookup_extended (sheet->cell_hash, &cellpos, &original_key, NULL)) {
		g_hash_table_remove (sheet->cell_hash, &cellpos);
		g_free (original_key);
	}
	else
		g_warning ("Cell not in hash table |\n");
}

static void
sheet_cell_remove_internal (Sheet *sheet, Cell *cell)
{
	GList *deps;

	if (cell->parsed_node)
		sheet_cell_formula_unlink (cell);

	deps = cell_get_dependencies (cell);
	cell_queue_recalc_list (deps, TRUE);

	sheet_cell_remove_from_hash (sheet, cell);

	cell_unrealize (cell);
}

void
sheet_cell_remove (Sheet *sheet, Cell *cell)
{
	g_return_if_fail (sheet != NULL);
	g_return_if_fail (cell != NULL);
	g_return_if_fail (IS_SHEET (sheet));

	/* Queue a redraw on the region used by the cell being removed */
	sheet_redraw_cell_region (sheet,
				  cell->col->pos, cell->row->pos,
				  cell->col->pos, cell->row->pos);

	sheet_cell_remove_internal (sheet, cell);

	sheet_redraw_cell_region (sheet,
				  cell->col->pos, cell->row->pos,
				  cell->col->pos, cell->row->pos);
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
sheet_cell_formula_link (Cell *cell)
{
	Sheet *sheet;

	g_return_if_fail (cell != NULL);
	g_return_if_fail (cell->parsed_node != NULL);

	sheet = cell->sheet;

#ifdef DEBUG_CELL_FORMULA_LIST
	if (g_list_find (sheet->workbook->formula_cell_list, cell)) {
		/* Anything that shows here is a bug.  */
		g_warning ("Cell %s %p re-linked\n",
			   cell_name (cell->col->pos, cell->row->pos),
			   cell);
		return;
	}
#endif

	sheet->workbook->formula_cell_list =
		g_list_prepend (sheet->workbook->formula_cell_list, cell);
	cell_add_dependencies (cell);
}

void
sheet_cell_formula_unlink (Cell *cell)
{
	Sheet *sheet;

	g_return_if_fail (cell != NULL);
	g_return_if_fail (cell->parsed_node != NULL);

	sheet = cell->sheet;
	g_return_if_fail (sheet != NULL); /* Catch use of deleted cell */

	cell_drop_dependencies (cell);
	sheet->workbook->formula_cell_list = g_list_remove (sheet->workbook->formula_cell_list, cell);

	/* Just an optimization to avoid an expensive list lookup */
	if (cell->flags & CELL_QUEUED_FOR_RECALC)
		cell_unqueue_from_recalc (cell);
}

/*
 * Callback for sheet_cell_foreach_range to remove a set of cells.
 */
static Value *
cb_free_cell (Sheet *sheet, int col, int row, Cell *cell, void *user_data)
{
	sheet_cell_remove_internal (sheet, cell);
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

	/* Delete in ascending order to avoid decrementing max_used each time */
	for (i = 0; i <= max_col; ++i)
		sheet_col_destroy (sheet, i, TRUE);

	for (i = 0; i <= max_row; ++i)
		sheet_row_destroy (sheet, i, TRUE);
	
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

	g_free (sheet->name);
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
	sheet_destroy_contents (sheet);

	/* Clear the cliboard to avoid dangling references to the deleted sheet */
	if (sheet == application_clipboard_sheet_get ())
		application_clipboard_clear ();

	dependency_data_destroy (sheet);
	sheet->deps = NULL;

	sheet_destroy_styles (sheet);

	g_hash_table_destroy (sheet->cell_hash);

	expr_name_clean_sheet (sheet);

	sheet->signature = 0;

	g_free (sheet->private);
	g_free (sheet);
}

/*****************************************************************************/

struct sheet_clear_region_callback_data
{
	Range	 r;
	GList	*l;
};

/*
 * assemble_cell_list: A callback for sheet_cell_foreach_range
 * intented to assemble a list of cells in a region to be cleared.
 *
 * The closure parameter should be a pointer to a
 * sheet_clear_region_callback_data.
 */
static Value *
assemble_clear_cell_list (Sheet *sheet, int col, int row, Cell *cell,
			  void *user_data)
{
	struct sheet_clear_region_callback_data * cb =
	    (struct sheet_clear_region_callback_data *) user_data;

	/* TODO TODO TODO : Where to deal with a user creating
	 * several adjacent regions to clear ??
	 */

	/* Flag an attempt to delete a subset of an array */
	if (cell->parsed_node && cell->parsed_node->oper == OPER_ARRAY){
		ArrayRef * ref = &cell->parsed_node->u.array;
		if ((col - ref->x) < cb->r.start.col)
			return value_terminate ();
		if ((row - ref->y) < cb->r.start.row)
			return value_terminate ();
		if ((col - ref->x + ref->cols -1) > cb->r.end.col)
			return value_terminate ();
		if ((row - ref->y + ref->rows -1) > cb->r.end.row)
			return value_terminate ();
	}

	cb->l = g_list_prepend (cb->l, cell);
	return NULL;
}

static Value *
cb_clear_cell_comments (Sheet *sheet, int col, int row, Cell *cell, void *user_data)
{
	cell_comment_destroy (cell);
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
		    int const start_col, int const start_row,
		    int const end_col, int const end_row,
		    int const clear_flags)
{
	struct sheet_clear_region_callback_data cb;

	g_return_if_fail (sheet != NULL);
	g_return_if_fail (IS_SHEET (sheet));
	g_return_if_fail (start_col <= end_col);
	g_return_if_fail (start_row <= end_row);

	cb.r.start.col = start_col;
	cb.r.start.row = start_row;
	cb.r.end.col = end_col;
	cb.r.end.row = end_row;

	/* Queue a redraw for cells being modified */
	if (clear_flags & (CLEAR_VALUES|CLEAR_FORMATS))
		sheet_redraw_cell_region (sheet,
					  start_col, start_row,
					  end_col, end_row);

	/* Clear the style in the region (new_default will ref the style for us). */
	if (clear_flags & CLEAR_FORMATS)
		sheet_style_attach (sheet, cb.r, mstyle_new_default ());

	if (clear_flags & CLEAR_COMMENTS)
		sheet_cell_foreach_range (sheet, TRUE,
					  start_col, start_row,
					  end_col,   end_row,
					  cb_clear_cell_comments, NULL);

	if (clear_flags & CLEAR_VALUES) {
		GList *l;
		cb.l = NULL;

		if (sheet_cell_foreach_range (sheet, TRUE,
					      start_col, start_row, end_col, end_row,
					      assemble_clear_cell_list, &cb) == NULL) {
			cb.l = g_list_reverse (cb.l);
			cell_freeze_redraws ();

			for (l = cb.l; l; l = l->next){
				Cell *cell = l->data;

				sheet_cell_remove (sheet, cell);
				cell_destroy (cell);
			}
			cell_thaw_redraws ();
		} else 
		    gnumeric_error_splits_array (context);

		g_list_free (cb.l);
	}
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

/**
 * sheet_cursor_move:
 * @sheet: Which sheet's cursor to move
 * @col:   destination column
 * @row:   destination row
 * @clear_selection:       Clear the old selection if we move.
 * @add_dest_to_selection: Add the new cursor location to the
 *                         selection if we move.
 *
 * Adjusts the cursor location for the specified sheet, optionaly
 * clearing the old selection and adding the new cursor to the selection.
 * Be careful when manging the selection when the 'move' returns to the
 * current location.
 */
void
sheet_cursor_move (Sheet *sheet, int col, int row,
		   gboolean clear_selection, gboolean add_dest_to_selection)
{
	GList *l;
	int old_row, old_col;

	g_return_if_fail (sheet != NULL);
	g_return_if_fail (IS_SHEET (sheet));

	workbook_finish_editing (sheet->workbook, TRUE);

	old_col = sheet->cursor.edit_pos.col;
	old_row = sheet->cursor.edit_pos.row;
	sheet->cursor.edit_pos.col = col;
	sheet->cursor.edit_pos.row = row;

	for (l = sheet->sheet_views; l; l = l->next){
		GnumericSheet *gsheet = GNUMERIC_SHEET_VIEW (l->data);

		gnumeric_sheet_set_cursor_bounds (gsheet, col, row, col, row);
	}
	sheet_load_cell_val (sheet);

	if (old_row != row || old_col != col) {
		if (clear_selection)
			sheet_selection_reset_only (sheet);
		if (add_dest_to_selection)
			sheet_selection_add (sheet, col, row);
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

	workbook_finish_editing (sheet->workbook, TRUE);

	/* Redraw the old edit cell */
	if (sheet->cursor.edit_pos.col != edit_col ||
	    sheet->cursor.edit_pos.row != edit_row) {
		sheet_redraw_cell_region (sheet,
					  sheet->cursor.edit_pos.col,
					  sheet->cursor.edit_pos.row,
					  sheet->cursor.edit_pos.col,
					  sheet->cursor.edit_pos.row);
		sheet->cursor.edit_pos.col = edit_col;
		sheet->cursor.edit_pos.row = edit_row;
		sheet_load_cell_val (sheet);
	}

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

/**
 * sheet_fill_selection_with:
 * @sheet:	 Which sheet we are operating on.
 * @str:	 The text to fill the selection with.
 * @is_array:    A flag to differentiate between filling and array formulas.
 *
 * Checks to ensure that none of the ranges being filled contain a subset of
 * an array-formula.
 */
void
sheet_fill_selection_with (CommandContext *context, Sheet *sheet,
			   const char *str, gboolean const is_array)
{
	GList *l;

	g_return_if_fail (sheet != NULL);
	g_return_if_fail (IS_SHEET (sheet));
	g_return_if_fail (str != NULL);

	/* Check for array subdivision */
	for (l = sheet->selections; l; l = l->next)
	{
		SheetSelection *ss = l->data;
		if (!sheet_check_for_partial_array (sheet,
						    ss->user.start.row,ss->user.start.col,
						    ss->user.end.row, ss->user.end.col)) {
			gnumeric_error_splits_array (context);
			return;
		}
	}

	/*
	 * Only enter an array formula if
	 *   1) the text is a formula
	 *   2) It's entered as an array formula
	 *   3) There is only one 1 selection
	 */
	l = sheet->selections;
	if (*str == '=' && is_array && l != NULL && l->next == NULL) {
		char *error_string = NULL;
		SheetSelection *ss = l->data;
		ParsePosition pp;
		ExprTree *expr =
			expr_parse_string (str + 1,
					   parse_pos_init (&pp, sheet->workbook,
							   ss->user.start.col,
							   ss->user.start.row),
					   NULL, &error_string);
		if (expr) {
			cell_set_array_formula (sheet,
						ss->user.start.row, ss->user.start.col,
						ss->user.end.row, ss->user.end.col,
						expr);
			workbook_recalc (sheet->workbook);
			return;
		}

		/* Fall through and paste the error string */
		str = error_string;

		g_return_if_fail (str != NULL);
	}

	cell_freeze_redraws ();
	for (; l; l = l->next){
		SheetSelection *ss = l->data;
		sheet_set_text (sheet, str, &ss->user);
	}
	cell_thaw_redraws ();
	workbook_recalc (sheet->workbook);
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
 * sheet_quote_name:
 * @sheet: 
 * 
 * Quotes the sheet name for expressions ( if neccessary ),
 * FIXME: if this is slow, we can easily cache the 'quote' flag
 * on the sheet, and update it when we set the name.
 * 
 * Return value: a safe sheet name.
 **/
char *
sheet_quote_name (Sheet *sheet)
{
	int         i, j, quote;
	char       *name;
	static char quote_chr [] = { '=', '<', '>', '+', '-', ' ', '^', '&', '%', '\0' };

	g_return_val_if_fail (sheet != NULL, NULL);
	g_return_val_if_fail (sheet->name != NULL, NULL);

	name  = sheet->name;
	quote = FALSE;
	for (i = 0; name [i]; i++) {
		for (j = 0; quote_chr [j]; j++)
			if (name [i] == quote_chr [j])
				quote = TRUE;
	}

	if (quote)
		return g_strconcat ("\"", sheet->name, "\"", NULL);
	else
		return g_strdup (sheet->name);
}

/* Can remove sheet since local references have NULL sheet */
char *
cellref_name (CellRef *cell_ref, ParsePosition const *pp)
{
	static char buffer [sizeof (long) * 4 + 4];
	char *p = buffer;
	int col, row;
	Sheet *sheet = cell_ref->sheet;

	if (cell_ref->col_relative)
		col = pp->col + cell_ref->col;
	else {
		*p++ = '$';
		col = cell_ref->col;
	}

	if (col <= 'Z'-'A'){
		*p++ = col + 'A';
	} else {
		int a = col / ('Z'-'A'+1);
		int b = col % ('Z'-'A'+1);

		*p++ = a + 'A' - 1;
		*p++ = b + 'A';
	}
	if (cell_ref->row_relative)
		row = pp->row + cell_ref->row;
	else {
		*p++ = '$';
		row = cell_ref->row;
	}

	sprintf (p, "%d", row+1);

	/* If it is a non-local reference, add the path to the external sheet */
	if (sheet != NULL) {
		char *s, *name;
	        
		name = sheet_quote_name (sheet);
		s = g_strconcat (name, "!", buffer, NULL);
		g_free (name);

		if (sheet->workbook != pp->wb) {
			char * n = g_strconcat ("[", sheet->workbook->filename, "]", s, NULL);
			g_free (s);
			s = n;
		}
		return s;
	} else
		return g_strdup (buffer);
}

gboolean
cellref_a1_get (CellRef *out, const char *in, int parse_col, int parse_row)
{
	int col = 0;
	int row = 0;

	g_return_val_if_fail (in != NULL, FALSE);
	g_return_val_if_fail (out != NULL, FALSE);

	/* Try to parse a column */
	if (*in == '$'){
		out->col_relative = FALSE;
		in++;
	} else
		out->col_relative = TRUE;

	if (!(toupper (*in) >= 'A' && toupper (*in) <= 'Z'))
		return FALSE;

	col = toupper (*in++) - 'A';
	
	if (toupper (*in) >= 'A' && toupper (*in) <= 'Z')
		col = (col+1) * ('Z'-'A'+1) + toupper (*in++) - 'A';

	/* Try to parse a row */
	if (*in == '$'){
		out->row_relative = FALSE;
		in++;
	} else
		out->row_relative = TRUE;
	
	if (!(*in >= '1' && *in <= '9'))
		return FALSE;

	while (isdigit ((unsigned char)*in)){
		row = row * 10 + *in - '0';
		in++;
	}
	if (row > SHEET_MAX_ROWS)
		return FALSE;
	row--;

	if (*in) /* We havn't hit the end yet */
		return FALSE;

	/* Setup the cell reference information */
	if (out->row_relative)
		out->row = row - parse_row;
	else
		out->row = row;

	if (out->col_relative)
		out->col = col - parse_col;
	else
		out->col = col;

	out->sheet = NULL;

	return TRUE;
}

static gboolean
r1c1_get_item (int *num, unsigned char *rel, const char * *const in)
{
	gboolean neg = FALSE;

	if (**in == '[') {
		(*in)++;
		*rel = TRUE;
		if (!**in)
			return FALSE;

		if (**in == '+')
			(*in)++;
		else if (**in == '-') {
			neg = TRUE;
			(*in)++;
		}
	}
	*num = 0;

	while (**in && isdigit ((unsigned char)**in)) {
		*num = *num * 10 + **in - '0';
		(*in)++;
	}

	if (neg)
		*num = -*num;

	if (**in == ']')
		(*in)++;

	return TRUE;
}

gboolean
cellref_r1c1_get (CellRef *out, const char *in, int parse_col, int parse_row)
{
	g_return_val_if_fail (in != NULL, FALSE);
	g_return_val_if_fail (out != NULL, FALSE);

	out->row_relative = FALSE;
	out->col_relative = FALSE;
	out->col = parse_col;
	out->row = parse_row;
	out->sheet = NULL;

	if (!*in)
		return FALSE;

	while (*in) {
		if (*in == 'R') {
			in++;
			if (!r1c1_get_item (&out->row, &out->row_relative, &in))
				return FALSE;
		} else if (*in == 'C') {
			in++;
			if (!r1c1_get_item (&out->col, &out->col_relative, &in))
				return FALSE;
		} else
			return FALSE;
	}

	out->col--;
	out->row--;
	return TRUE;
}

/**
 * cellref_get:
 * @out: destination CellRef
 * @in: reference description text, no leading or trailing
 *      whitespace allowed.
 * 
 * Converts the char * representation of a Cell reference into
 * an internal representation.
 * 
 * Return value: TRUE if no format errors found.
 **/
gboolean
cellref_get (CellRef *out, const char *in, int parse_col, int parse_row)
{
	g_return_val_if_fail (in != NULL, FALSE);
	g_return_val_if_fail (out != NULL, FALSE);

	if (cellref_a1_get (out, in, parse_col, parse_row))
		return TRUE;
	else
		return cellref_r1c1_get (out, in, parse_col, parse_row);
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

void
sheet_insert_object (Sheet *sheet, char *goadid)
{
#ifdef ENABLE_BONOBO
	BonoboClientSite *client_site;
	BonoboObjectClient *object_server;

	g_return_if_fail (sheet != NULL);
	g_return_if_fail (IS_SHEET (sheet));
	g_return_if_fail (goadid != NULL);

	if (strncmp (goadid, "moniker_url:", 12) == 0)
		object_server = bonobo_object_activate (goadid, 0);
	else
		object_server = bonobo_object_activate_with_goad_id (NULL, goadid, 0, NULL);
	
	if (!object_server){
		char *msg;

		msg = g_strdup_printf (_("I was not able to activate object %s"), goadid);

		gnumeric_notice (sheet->workbook, GNOME_MESSAGE_BOX_ERROR, msg);
		g_free (msg);
		return;
	}

	client_site = bonobo_client_site_new (sheet->workbook->bonobo_container);
	bonobo_container_add (sheet->workbook->bonobo_container, BONOBO_OBJECT (client_site));

	if (!bonobo_client_site_bind_embeddable (client_site, object_server)){
		gnumeric_notice (sheet->workbook, GNOME_MESSAGE_BOX_ERROR,
				 _("I was unable to the bind object"));
		gtk_object_unref (GTK_OBJECT (object_server));
		gtk_object_unref (GTK_OBJECT (client_site));
		return;
	}

#endif
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
	if (cell == NULL ||
	    cell->parsed_node == NULL ||
	    cell->parsed_node->oper != OPER_ARRAY ||
	    cell->parsed_node->u.array.x <= 0)
		return NULL;
	return value_terminate ();
}

/*
 * Callback for sheet_cell_foreach_range to test whether a cell is in an
 * array-formula below the top line.
 */
static Value *
avoid_dividing_array_vertical (Sheet *sheet, int col, int row, Cell *cell,
			       void *user_data)
{
	if (cell == NULL ||
	    cell->parsed_node == NULL ||
	    cell->parsed_node->oper != OPER_ARRAY ||
	    cell->parsed_node->u.array.y <= 0)
		return NULL;
	return value_terminate ();
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
		cell_relocate (cell, FALSE);
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
			gnumeric_error_splits_array (context);
			return TRUE;
		}

	/* Walk the right edge to make sure nothing is split due to over run.  */
	if (sheet_cell_foreach_range (sheet, TRUE, SHEET_MAX_COLS-count, 0,
				      SHEET_MAX_COLS-1, SHEET_MAX_ROWS-1,
				      &avoid_dividing_array_horizontal,
				      NULL) != NULL){
		gnumeric_error_splits_array (context);
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

	/* 6. Notify sheet of pending update */
	sheet->private->recompute_visibility = TRUE;
	if (sheet->private->reposition_col_comment > col)
		sheet->private->reposition_col_comment = col;

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

	/* 0. Walk cells in deleted cols and ensure arrays aren't divided. */
	if (!sheet_check_for_partial_array (sheet, 0, col, 
					    SHEET_MAX_ROWS-1, col+count-1))
	{
		gnumeric_error_splits_array (context);
		return TRUE;
	}

	/* 1. Delete all columns (and their cells) that will fall off the end */
	for (i = col + count ; --i >= col; )
		sheet_col_destroy (sheet, i, TRUE);

	/* 2. Invalidate references to the cells in the delete columns */
	reloc_info.origin.start.col = col;
	reloc_info.origin.start.row = 0;
	reloc_info.origin.end.col = col + count - 1;
	reloc_info.origin.end.row = SHEET_MAX_ROWS-1;
	reloc_info.origin_sheet = reloc_info.target_sheet = sheet;
	reloc_info.col_offset = SHEET_MAX_COLS; /* send them to infinity */
	reloc_info.row_offset = SHEET_MAX_ROWS; /*   to force invalidation */
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

	/* 7. Notify sheet of pending update */
	sheet->private->recompute_visibility = TRUE;
	if (sheet->private->reposition_col_comment > col)
		sheet->private->reposition_col_comment = col;

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
			gnumeric_error_splits_array (context);
			return TRUE;
		}

	/* Walk the lower edge to make sure nothing is split due to over run.  */
	if (sheet_cell_foreach_range (sheet, TRUE, 0, SHEET_MAX_ROWS-count,
				      SHEET_MAX_COLS-1, SHEET_MAX_ROWS-1,
				      &avoid_dividing_array_vertical,
				      NULL) != NULL){
		gnumeric_error_splits_array (context);
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

	/* 6. Notify sheet of pending update */
	sheet->private->recompute_visibility = TRUE;
	if (sheet->private->reposition_row_comment > row)
		sheet->private->reposition_row_comment = row;

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

	/* 0. Walk cells in deleted rows and ensure arrays aren't divided. */
	if (!sheet_check_for_partial_array (sheet, row, 0, 
					    row+count-1, SHEET_MAX_COLS-1))
	{
		gnumeric_error_splits_array (context);
		return TRUE;
	}

	/* 1. Delete all cols (and their cells) that will fall off the end */
	for (i = row + count ; --i >= row; )
		sheet_row_destroy (sheet, i, TRUE);

	/* 2. Invalidate references to the cells in the delete columns */
	reloc_info.origin.start.col = 0;
	reloc_info.origin.start.row = row;
	reloc_info.origin.end.col = SHEET_MAX_COLS-1;
	reloc_info.origin.end.row = row + count - 1;
	reloc_info.origin_sheet = reloc_info.target_sheet = sheet;
	reloc_info.col_offset = SHEET_MAX_COLS; /* send them to infinity */
	reloc_info.row_offset = SHEET_MAX_ROWS; /*   to force invalidation */
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
	sheet->private->recompute_visibility = TRUE;
	if (sheet->private->reposition_row_comment > row)
		sheet->private->reposition_row_comment = row;

	return FALSE;
}

/*
 * Excel does not support paste special from a 'cut' will we want to
 * do that ?
 */
void
sheet_move_range (CommandContext *context,
		  ExprRelocateInfo const * rinfo)
{
	GList *cells = NULL;
	Cell  *cell;
	gboolean inter_sheet_formula;

	g_return_if_fail (rinfo != NULL);
	g_return_if_fail (rinfo->origin_sheet != NULL);
	g_return_if_fail (IS_SHEET (rinfo->origin_sheet));
	g_return_if_fail (rinfo->target_sheet != NULL);
	g_return_if_fail (IS_SHEET (rinfo->target_sheet));

	/* 0. Walk cells in the moving range and ensure arrays aren't divided. */

	/* 1. Fix references to and from the cells which are moving */
	/* All we really want is the workbook so either sheet will do */

	/* FIXME : Avoid leaking and free the reloc list for now.  When undo
	 * for paste_cut is ready we will need this list
	 */
	workbook_expr_unrelocate_free (
		workbook_expr_relocate (rinfo->origin_sheet->workbook, rinfo));

	/* 2. Collect the cells */
	sheet_cell_foreach_range (rinfo->origin_sheet, TRUE,
				  rinfo->origin.start.col,
				  rinfo->origin.start.row,
				  rinfo->origin.end.col,
				  rinfo->origin.end.row,
				  &cb_collect_cell, &cells);

	/* 3. Reverse the list so that we start at the top left which makes
	 * things easier for arrays.
	 */
	cells = g_list_reverse (cells);

	/* 4. Clear the target area */
	sheet_clear_region (context,
			    rinfo->target_sheet, 
			    rinfo->origin.start.col + rinfo->col_offset,
			    rinfo->origin.start.row + rinfo->row_offset,
			    rinfo->origin.end.col + rinfo->col_offset,
			    rinfo->origin.end.row + rinfo->row_offset,
			    CLEAR_VALUES|CLEAR_COMMENTS); /* Do not to clear styles */

	/* 5. Slide styles BEFORE the cells so that spans get computed properly */
	sheet_style_relocate (rinfo);

	/* 6. Insert the cells back */
	for (; cells != NULL ; cells = g_list_remove (cells, cell)) {
		cell = cells->data;

		/* check for out of bounds and delete if necessary */
		if ((cell->col->pos + rinfo->col_offset) >= SHEET_MAX_COLS ||
		    (cell->row->pos + rinfo->row_offset) >= SHEET_MAX_ROWS) {
			if (cell->parsed_node)
				sheet_cell_formula_unlink (cell);
			cell_unrealize (cell);
			cell_destroy (cell);
			continue;
		}

		/* Inter sheet movement requires the moving the formula too */
		inter_sheet_formula  = (cell->sheet != rinfo->target_sheet
					&& cell->parsed_node);
		if (inter_sheet_formula)
			sheet_cell_formula_unlink (cell);

		/* Update the location */
		sheet_cell_add (rinfo->target_sheet, cell,
				cell->col->pos + rinfo->col_offset,
				cell->row->pos + rinfo->row_offset);

		if (inter_sheet_formula)
			sheet_cell_formula_link (cell);

		/* Move comments */
		cell_relocate (cell, FALSE);
	}

	/* 7. Recompute dependencies */
	sheet_recalc_dependencies (rinfo->target_sheet);

	/* 8. Recalc & Redraw */
	workbook_recalc (rinfo->target_sheet->workbook);
	sheet_redraw_all (rinfo->target_sheet);
}

/**
 * sheet_shift_rows:
 * @sheet	the sheet
 * @col		column marking the start of the shift
 * @start_row	first row
 * @end_row	end row
 * @count	numbers of columns to shift.  negative numbers will
 *		delete count columns, positive number will insert
 *		count columns.
 *
 * Takes the cells in the region (col,start_row):(MAX_COL,end_row)
 * and copies them @count units (possibly negative) to the right.
 */

void
sheet_shift_rows (CommandContext *context, Sheet *sheet,
		  int col, int start_row, int end_row, int count)
{
	ExprRelocateInfo rinfo;
	rinfo.origin.start.col = col;
	rinfo.origin.start.row = start_row;
	rinfo.origin.end.col = SHEET_MAX_COLS-1;
	rinfo.origin.end.row = end_row;
	rinfo.origin_sheet = rinfo.target_sheet = sheet;
	rinfo.col_offset = count;
	rinfo.row_offset = 0;

	sheet_move_range (context, &rinfo);
}

/**
 * sheet_shift_cols:
 * @sheet	the sheet
 * @start_col	first column
 * @end_col	end column
 * @row		row marking the start of the shift
 * @count	numbers of rows to shift.  a negative numbers will
 *		delete count rows, positive number will insert
 *		count rows.
 *
 * Takes the cells in the region (start_col,row):(end_col,MAX_ROW)
 * and copies them @count units (possibly negative) downwards.
 */
void
sheet_shift_cols (CommandContext *context, Sheet *sheet,
		  int start_col, int end_col, int row, int count)
{
	ExprRelocateInfo rinfo;
	rinfo.origin.start.col = start_col;
	rinfo.origin.start.row = row;
	rinfo.origin.end.col = end_col;
	rinfo.origin.end.row = SHEET_MAX_ROWS-1;
	rinfo.origin_sheet = rinfo.target_sheet = sheet;
	rinfo.col_offset = 0;
	rinfo.row_offset = count;

	sheet_move_range (context, &rinfo);
}

double *
sheet_save_row_col_sizes (Sheet *sheet, gboolean const is_cols,
			  int index, int count)
{
	int i;
	double *res = NULL;

	g_return_val_if_fail (sheet != NULL, NULL);
	g_return_val_if_fail (count > 0, NULL);

	res = g_new (double, count);

	for (i = 0 ; i < count ; ++i) {
		ColRowInfo *info = is_cols
		    ? sheet_col_get_info (sheet, index + i)
		    : sheet_row_get_info (sheet, index + i);

		g_return_val_if_fail (info != NULL, NULL); /* be anal, and leak */

		if (info->pos != -1) {
			res[i] = info->size_pts;
			if (info->hard_size)
				res[i] *= -1.;
		} else
			res[i] = 0.;
	}
	return res;
}

/*
 * NOTE : this is a low level routine it does not redraw or
 *        reposition objects
 */
void
sheet_restore_row_col_sizes (Sheet *sheet, gboolean const is_cols,
			     int index, int count, double *sizes)
{
	int i;

	g_return_if_fail (sizes != NULL);
	g_return_if_fail (sheet != NULL);
	g_return_if_fail (count > 0);

	for (i = 0 ; i < count ; ++i) {
		gboolean hard_size = FALSE;

		/* Reset to the default */
		if (sizes[i] == 0.) {
			ColRowCollection *infos = is_cols ? &(sheet->cols) : &(sheet->rows);
			ColRowInfo ***segment =
				(ColRowInfo ***)&COLROW_GET_SEGMENT(infos, index+i);
			int const sub = COLROW_SUB_INDEX (index+i);
			ColRowInfo *cri = NULL;
			if (*segment != NULL) {
				cri = (*segment)[sub];
				if (cri != NULL) {
					(*segment)[sub] = NULL;
					g_free (cri);
				}
			}
		} else
		{
			if (sizes[i] < 0.) {
				hard_size = TRUE;
				sizes[i] *= -1.;
			}
			if (is_cols)
				sheet_col_set_size_pts (sheet, index+i, sizes[i], hard_size);
			else
				sheet_row_set_size_pts (sheet, index+i, sizes[i], hard_size);
		}
	}

	/* Notify sheet of pending update */
	sheet->private->recompute_visibility = TRUE;
	if (is_cols) {
		if (sheet->private->reposition_col_comment > index)
			sheet->private->reposition_col_comment = index;
	} else {
		if (sheet->private->reposition_row_comment > index)
			sheet->private->reposition_row_comment = index;
	}

	g_free (sizes);
}

/**
 * sheet_row_col_visible:
 * @sheet	: the sheet
 * @is_col	: Are we dealing with rows or columns.
 * @visible	: Make things visible or invisible.
 * @index	: The index of the first row/col.
 * @count	: The number of rows/cols to change the state.
 *
 * Change the visibility of the selected range of contiguous rows/cols.
 */
void
sheet_row_col_visible (Sheet *sheet, gboolean const is_col, gboolean const visible,
		       int index, int count)
{
	g_return_if_fail (sheet != NULL);

	while (--count >= 0) {
		ColRowInfo * const cri = is_col
		    ? sheet_col_fetch (sheet, index++)
		    : sheet_row_fetch (sheet, index++);

		if (visible ^ cri->visible)
			cri->visible = visible;
	}
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
	colrow_compute_pixels_from_pts (sheet, cri, (void *)is_horizontal);
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

	if (from > to)
	{
		int const tmp = to;
		to = from;
		from = tmp;
		sign = -1;
	}

	/* Do not use sheet_foreach_colrow, it ignores empties */
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

	if (from > to)
	{
		int const tmp = to;
		to = from;
		from = tmp;
		sign = -1;
	}


	/* Do not use sheet_foreach_colrow, it ignores empties */
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
	colrow_compute_pixels_from_pts (sheet, ci, (void*)TRUE);

	sheet->private->recompute_visibility = TRUE;
	if (sheet->private->reposition_col_comment > col)
		sheet->private->reposition_col_comment = col;
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

	sheet->private->recompute_visibility = TRUE;
	if (sheet->private->reposition_col_comment > col)
		sheet->private->reposition_col_comment = col;
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
	int i, pixels = 0;
	int sign = 1;

	g_assert (sheet != NULL);

	if (from > to)
	{
		int const tmp = to;
		to = from;
		from = tmp;
		sign = -1;
	}

	/* Do not use sheet_foreach_colrow, it ignores empties */
	for (i = from ; i < to ; ++i) {
		ColRowInfo const *ri = sheet_row_get_info (sheet, i);
		if (ri->visible)
			pixels += ri->size_pixels;
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
	double units = 0;
	int i;
	int sign = 1;

	g_assert (sheet != NULL);

	if (from > to)
	{
		int const tmp = to;
		to = from;
		from = tmp;
		sign = -1;
	}

	/* Do not use sheet_foreach_colrow, it ignores empties */
	for (i = from ; i < to ; ++i) {
		ColRowInfo const *ri = sheet_row_get_info (sheet, i);
		if (ri->visible)
			units += ri->size_pts;
	}
	
	return units*sign;
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
	colrow_compute_pixels_from_pts (sheet, ri, (void*)FALSE);

	sheet->private->recompute_visibility = TRUE;
	if (sheet->private->reposition_row_comment > row)
		sheet->private->reposition_row_comment = row;
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

	sheet->private->recompute_visibility = TRUE;
	if (sheet->private->reposition_row_comment > row)
		sheet->private->reposition_row_comment = row;
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
sheet_destroy_edit_cursor (Sheet *sheet)
{
	GList *l;
	for (l = sheet->sheet_views; l; l = l->next){
		GnumericSheet *gsheet = GNUMERIC_SHEET_VIEW (l->data);

		gnumeric_sheet_destroy_editing_cursor (gsheet);
	}
}

void
sheet_destroy_cell_select_cursor (Sheet *sheet)
{
	GList *l;
	for (l = sheet->sheet_views; l; l = l->next){
		GnumericSheet *gsheet = GNUMERIC_SHEET_VIEW (l->data);

		gnumeric_sheet_stop_cell_selection (gsheet, TRUE);
	}
}
