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
sheet_redraw_all (Sheet const *sheet)
{
	GList *l;

	for (l = sheet->sheet_views; l; l = l->next){
		SheetView *sheet_view = l->data;

		sheet_view_redraw_all (sheet_view);
	}
}

void
sheet_redraw_cols (Sheet const *sheet)
{
	GList *l;

	for (l = sheet->sheet_views; l; l = l->next){
		SheetView *sheet_view = l->data;

		sheet_view_redraw_columns (sheet_view);
	}
}

void
sheet_redraw_rows (Sheet const *sheet)
{
	GList *l;

	for (l = sheet->sheet_views; l; l = l->next){
		SheetView *sheet_view = l->data;

		sheet_view_redraw_rows (sheet_view);
	}
}

static void
col_row_info_init (ColRowInfo *cri, double points)
{
	cri->pos = -1;
	cri->units = points;
	cri->margin_a_pt = 1.0;
	cri->margin_b_pt = 1.0;

	cri->pixels = 0;
	cri->margin_a = 0;
	cri->margin_b = 0;

	cri->spans = NULL;
}

static void
sheet_init_default_styles (Sheet *sheet)
{
	/* Sizes seem to match excel */
	col_row_info_init (&sheet->cols.default_style, 62.0);
	col_row_info_init (&sheet->rows.default_style, 15.0);
}

/* Initialize some of the columns and rows, to test the display engine */
static void
sheet_init_dummy_stuff (Sheet *sheet)
{
	ColRowInfo *cp, *rp;
	int x, y;

	for (x = 0; x < 40; x += 2){
		cp = sheet_row_new (sheet);
		cp->pos = x;
		cp->units = (x+1) * 30;
		sheet_col_add (sheet, cp);
	}

	for (y = 0; y < 6; y += 2){
		rp = sheet_row_new (sheet);
		rp->pos = y;
		rp->units = (20 * (y + 1));
		sheet_row_add (sheet, rp);
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

Sheet *
sheet_new (Workbook *wb, const char *name)
{
	GtkWidget *sheet_view;
	Sheet  *sheet;
	MStyle *mstyle;

	g_return_val_if_fail (wb != NULL, NULL);
	g_return_val_if_fail (name != NULL, NULL);

	sheet = g_new0 (Sheet, 1);
	sheet->signature = SHEET_SIGNATURE;
	sheet->workbook = wb;
	sheet->name = g_strdup (name);
	sheet_create_styles (sheet);
	sheet->last_zoom_factor_used = -1.0;
	sheet->cols.max_used = -1;
	sheet->rows.max_used = -1;

	g_ptr_array_set_size (sheet->cols.info = g_ptr_array_new (), 
			      COLROW_SEGMENT_INDEX (SHEET_MAX_COLS-1)+1);
	g_ptr_array_set_size (sheet->rows.info = g_ptr_array_new (), 
			      COLROW_SEGMENT_INDEX (SHEET_MAX_ROWS-1)+1);
	sheet->print_info = print_info_new ();

	sheet->cell_hash = g_hash_table_new (cell_hash,
					     (GCompareFunc)&cell_compare);

	mstyle = mstyle_new_default ();
	sheet_style_attach (sheet, sheet_get_full_range (), mstyle);

	sheet_init_default_styles (sheet);

	/* Dummy initialization */
	if (0)
		sheet_init_dummy_stuff (sheet);

	sheet_view = GTK_WIDGET (sheet_new_sheet_view (sheet));

	sheet_selection_append (sheet, 0, 0);

	gtk_widget_show (sheet_view);

	sheet_set_zoom_factor (sheet, 1.0);

	sheet_corba_setup (sheet);

	sheet_set_dirty (sheet, FALSE);

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
sheet_compute_col_row_new_size (Sheet *sheet, ColRowInfo *ci, void *data)
{
	double pix_per_unit = sheet->last_zoom_factor_used;

	ci->pixels = (ci->units + ci->margin_a_pt + ci->margin_b_pt) * pix_per_unit;

	/*
	 * Ensure that there is at least 1 pixel around every cell so that we
	 * can mark the current cell
	 */
	ci->margin_a = MAX (ci->margin_a_pt * pix_per_unit, 1);
	ci->margin_b = MAX (ci->margin_b_pt * pix_per_unit, 1);

	return FALSE;
}

void
sheet_set_zoom_factor (Sheet *sheet, double factor)
{
	GList *l, *cl;
	double const diff = sheet->last_zoom_factor_used - factor;

	if (-.0001 < diff && diff < .0001)
		return;

	sheet->last_zoom_factor_used = factor;

	/* First, the default styles */
	sheet_compute_col_row_new_size (sheet, &sheet->rows.default_style, NULL);
	sheet_compute_col_row_new_size (sheet, &sheet->cols.default_style, NULL);

	/* Then every column and row */
	sheet_foreach_colrow (sheet, &sheet->cols, 0, SHEET_MAX_COLS-1,
			      &sheet_compute_col_row_new_size, NULL);
	sheet_foreach_colrow (sheet, &sheet->rows, 0, SHEET_MAX_ROWS-1,
			      &sheet_compute_col_row_new_size, NULL);

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

/*
 * Duplicates a column or row
 */
ColRowInfo *
sheet_duplicate_colrow (ColRowInfo *original)
{
	ColRowInfo *info = g_new (ColRowInfo, 1);

	*info = *original;
	
	return info;
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
			GtkAdjustment *ha = GTK_ADJUSTMENT (sheet_view->ha);

			if (sheet->cols.max_used > ha->upper){
				ha->upper = col;
				gtk_adjustment_changed (ha);
			}
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
			GtkAdjustment *va = GTK_ADJUSTMENT (sheet_view->va);

			if (sheet->rows.max_used > va->upper){
				va->upper = row;
				gtk_adjustment_changed (va);
			}
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

void
sheet_compute_visible_ranges (Sheet const *sheet)
{
	GList *l;

	for (l = sheet->sheet_views; l; l = l->next){
		GnumericSheet *gsheet = GNUMERIC_SHEET_VIEW (l->data);

		gnumeric_sheet_compute_visible_ranges (gsheet);
	}
}

static void
colrow_set_units (Sheet *sheet, ColRowInfo *info)
{
	double const pix = sheet->last_zoom_factor_used;

	info->units = (info->pixels -
		       (info->margin_a + info->margin_b - 1)) / pix;
}

static void
sheet_reposition_comments_from_row (Sheet *sheet, int row)
{
	GList *l;

	/* Move any cell comments */
	for (l = sheet->comment_list; l; l = l->next){
		Cell *cell = l->data;

		if (cell->row->pos >= row)
			cell_comment_reposition (cell);
	}
}

static void
sheet_reposition_comments_from_col (Sheet *sheet, int col)
{
	GList *l;

	/* Move any cell comments */
	for (l = sheet->comment_list; l; l = l->next){
		Cell *cell = l->data;

		if (cell->col->pos >= col)
			cell_comment_reposition (cell);
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

void
sheet_row_info_set_height (Sheet *sheet, ColRowInfo *ri, int height, gboolean height_set_by_user)
{
	g_return_if_fail (sheet != NULL);
	g_return_if_fail (IS_SHEET (sheet));
	g_return_if_fail (ri != NULL);

	if (height_set_by_user)
		ri->hard_size = TRUE;

	ri->pixels = height;
	colrow_set_units (sheet, ri);

	sheet_compute_visible_ranges (sheet);

	sheet_reposition_comments_from_row (sheet, ri->pos);
	sheet_redraw_all (sheet);
}

/**
 * sheet_row_set_height:
 * @sheet:		The sheet
 * @row:		The row
 * @height:		The desired height
 * @height_set_by_user: TRUE if this was done by a user (ie, user manually
 *                      set the width)
 *
 * Sets the height of a row in terms of the total visible space (as opossite
 * to the internal required space, which does not include the margins).
 */
void
sheet_row_set_height (Sheet *sheet, int row, int height, gboolean height_set_by_user)
{
	ColRowInfo *ri;
	int add = 0;

	g_return_if_fail (sheet != NULL);
	g_return_if_fail (IS_SHEET (sheet));

	ri = sheet_row_get_info (sheet, row);
	if (ri == &sheet->rows.default_style){
		ri = sheet_row_new (sheet);
		ri->pos = row;
		add = 1;
	}

	sheet_row_info_set_height (sheet, ri, height, height_set_by_user);

	if (add)
		sheet_row_add (sheet, ri);
}

/**
 * sheet_row_set_internal_height:
 * @sheet:		The sheet
 * @row:		The row info.  
 * @height:		The desired height
 *
 * Sets the height of a row in terms of the internal required space (the total
 * size of the row will include the margins.
 */
void
sheet_row_set_internal_height (Sheet *sheet, ColRowInfo *ri, double height)
{
	double pix;

	g_return_if_fail (sheet != NULL);
	g_return_if_fail (IS_SHEET (sheet));
	g_return_if_fail (ri != NULL);

	pix = sheet->last_zoom_factor_used;

	if (ri->units == height)
		return;

	ri->units = height;
	ri->pixels = (ri->units * pix) + (ri->margin_a + ri->margin_b - 1);

	sheet_compute_visible_ranges (sheet);
	sheet_reposition_comments_from_row (sheet, ri->pos);
	sheet_redraw_all (sheet);
}

/**
 * sheet_col_set_internal_width:
 * @sheet:		The sheet
 * @col:		The col info.  
 * @width:		The desired width
 *
 * Sets the width of a column in terms of the internal required space (the total
 * size of the column will include the margins.
 */
void
sheet_col_set_internal_width (Sheet *sheet, ColRowInfo *ci, double width)
{
	double pix;

	g_return_if_fail (sheet != NULL);
	g_return_if_fail (IS_SHEET (sheet));
	g_return_if_fail (ci != NULL);

	pix = sheet->last_zoom_factor_used;

	if (ci->units == width)
		return;

	ci->units = width;
	ci->pixels = (ci->units * pix) + (ci->margin_a + ci->margin_b - 1);

	sheet_compute_visible_ranges (sheet);
	sheet_reposition_comments_from_col (sheet, ci->pos);
	sheet_redraw_all (sheet);
}

void
sheet_row_set_height_units (Sheet *sheet, int row, double height, gboolean height_set_by_user)
{
	ColRowInfo *ri;
	
	g_return_if_fail (sheet != NULL);
	g_return_if_fail (IS_SHEET (sheet));
	g_return_if_fail (height > 0.0);

	ri = sheet_row_get_info (sheet, row);
	if (ri == &sheet->rows.default_style){
		ri = sheet_row_new (sheet);
		ri->pos = row;
		sheet_row_add (sheet, ri);
	}

	if (height_set_by_user)
		ri->hard_size = TRUE;
	
	sheet_row_set_internal_height (sheet, ri, height);
}

void
sheet_col_set_width_units (Sheet *sheet, int col, double width)
{
	ColRowInfo *ci;
	
	g_return_if_fail (sheet != NULL);
	g_return_if_fail (IS_SHEET (sheet));
	g_return_if_fail (width > 0.0);

	ci = sheet_col_get_info (sheet, col);
	if (ci == &sheet->cols.default_style){
		ci = sheet_col_new (sheet);
		ci->pos = col;
		sheet_col_add (sheet, ci);
	}

	sheet_col_set_internal_width (sheet, ci, width);
}

/*
 * Callback for sheet_cell_foreach_range to find the maximum width
 * in a range.
 */
static Value *
cb_max_cell_width (Sheet *sheet, int col, int row, Cell *cell,
		   int *max)
{
	int const width = cell->width;
	if (width > *max)
		*max = width;
	return NULL;
}

/**
 * sheet_col_size_fit:
 * @sheet: The sheet
 * @col: the column that we want to query
 *
 * This routine computes the ideal size for the column to make all data fit
 * properly.  Return value is in pixels
 */
int
sheet_col_size_fit (Sheet *sheet, int col)
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
		return ci->pixels;

	sheet_cell_foreach_range (sheet, TRUE,
				  col, 0,
				  col, SHEET_MAX_ROWS-1,
				  (sheet_cell_foreach_callback)&cb_max_cell_width, &max);

	/* Reset to the default width if the column was empty */
	if (max < 0)
		max = sheet->cols.default_style.pixels;
	else
		/* No need to scale width by zoom factor, that was already done */
		max += ci->margin_a + ci->margin_b;

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
	int const height = cell->height;
	if (height > *max)
		*max = height;
	return NULL;
}

/**
 * sheet_row_size_fit:
 * @sheet: The sheet
 * @col: the row that we want to query
 *
 * This routine computes the ideal size for the row to make all data fit
 * properly.  Return value is in pixels
 */
int
sheet_row_size_fit (Sheet *sheet, int row)
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
		return ri->pixels;

	sheet_cell_foreach_range (sheet, TRUE,
				  0, row,
				  SHEET_MAX_COLS-1, row,
				  (sheet_cell_foreach_callback)&cb_max_cell_height, &max);

	/* Reset to the default width if the column was empty */
	if (max < 0)
		max = sheet->rows.default_style.pixels;
	else
		/* No need to scale height by zoom factor, that was already done */
		max += ri->margin_a + ri->margin_b;

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
static void
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
		cell_get_span (cell, &left, &right);
		if (left != right)
			cell_register_span (cell, left, right);
	}

	g_list_free (dat.cells);
}

void
sheet_col_info_set_width (Sheet *sheet, ColRowInfo *ci, int width)
{
	g_return_if_fail (sheet != NULL);
	g_return_if_fail (IS_SHEET (sheet));
	g_return_if_fail (ci != NULL);


	ci->pixels = width;
	colrow_set_units (sheet, ci);

	sheet_compute_visible_ranges (sheet);
	sheet_redraw_all (sheet);
}

void
sheet_col_set_width (Sheet *sheet, int col, int width)
{
	ColRowInfo *ci;
	int add = 0;

	g_return_if_fail (sheet != NULL);
	g_return_if_fail (IS_SHEET (sheet));

	ci = sheet_col_get_info (sheet, col);
	if (ci == NULL)
		return;

	if (ci == &sheet->cols.default_style){
		ci = sheet_col_new (sheet);
		ci->pos = col;
		add = 1;
	}

	sheet_col_info_set_width (sheet, ci, width);

	if (add)
		sheet_col_add (sheet, ci);

	/* Compute the spans */
	sheet_recompute_spans_for_col (sheet, col);

	/* Move any cell comments */
	sheet_reposition_comments_from_col (sheet, col);
}

/**
 * sheet_col_get_distance:
 *
 * Return the number of pixels between from_col to to_col
 */
int
sheet_col_get_distance (Sheet const *sheet, int from, int to)
{
	int i, pixels = 0;

	g_assert (from <= to);
	g_assert (sheet != NULL);

	/* Do not use sheet_foreach_colrow, it ignores empties */
	for (i = from ; i < to ; ++i) {
		ColRowInfo const *ci = sheet_col_get_info (sheet, i);
		pixels += ci->pixels;
	}

	return pixels;
}

/**
 * sheet_row_get_distance:
 *
 * Return the number of pixels between from_row to to_row
 */
int
sheet_row_get_distance (Sheet const *sheet, int from, int to)
{
	int i, pixels = 0;

	g_assert (from <= to);
	g_assert (sheet != NULL);

	/* Do not use sheet_foreach_colrow, it ignores empties */
	for (i = from ; i < to ; ++i) {
		ColRowInfo const *ri = sheet_row_get_info (sheet, i);
		pixels += ri->pixels;
	}

	return pixels;
}

/**
 * sheet_col_get_unit_distance:
 *
 * Return the number of points between from_col to to_col
 */
double
sheet_col_get_unit_distance (Sheet const *sheet, int from, int to)
{
	double units = 0;
	int i;

	g_assert (sheet != NULL);
	g_assert (from <= to);

	/* Do not use sheet_foreach_colrow, it ignores empties */
	for (i = from ; i < to ; ++i) {
		ColRowInfo const *ci = sheet_col_get_info (sheet, i);

		/* I do not know why we subtract 1, but it is required to get
		 * things to match.
		 *
		 * We take to floor of the width to get capture the rounding the
		 * effect of rounding to pixels.
		 */
		units += (int)(ci->units + ci->margin_a_pt + ci->margin_b_pt) - 1;
	}
	
	return units;
}

/**
 * sheet_row_get_unit_distance:
 *
 * Return the number of points between from_row to to_row
 */
double
sheet_row_get_unit_distance (Sheet const *sheet, int from, int to)
{
	double units = 0;
	int i;

	g_assert (sheet != NULL);
	g_assert (from <= to);

	/* Do not use sheet_foreach_colrow, it ignores empties */
	for (i = from ; i < to ; ++i) {
		ColRowInfo const *ri = sheet_row_get_info (sheet, i);

		/* I do not know why we subtract 1, but it is required to get
		 * things to match.
		 *
		 * We take to floor of the width to get capture the rounding the
		 * effect of rounding to pixels.
		 */
		units += (int)(ri->units + ri->margin_a_pt + ri->margin_b_pt) - 1;
	}
	
	return units;
}

void
sheet_update_auto_expr (Sheet *sheet)
{
	Value *v;
	Workbook *wb = sheet->workbook;
	FunctionEvalInfo ei;

	g_return_if_fail (sheet != NULL);
	g_return_if_fail (IS_SHEET (sheet));

	/* defaults */
	v = NULL;
	func_eval_info_init (&ei, sheet, 0, 0);

	if (wb->auto_expr)
		v = eval_expr (&ei, wb->auto_expr);

	if (v) {
		char *s;

		s = value_get_as_string (v);
		workbook_auto_expr_label_set (wb, s);
		g_free (s);
		value_release (v);
	} else
		workbook_auto_expr_label_set (wb, _("Internal ERROR"));
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

static void
sheet_set_text (Sheet *sheet, char const *text, Range const * r)
{
	g_return_if_fail (sheet != NULL);
	g_return_if_fail (IS_SHEET (sheet));

	/*
	 * Figure out if a format matches, and for sanity compare that to
	 * a rendered version of the text, if they compare equally, then
	 * use that.
	 */
	if (*text != '=') {
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

void
sheet_set_current_value (Sheet *sheet)
{
	GList *l;
	char *str;
	Range r;

	g_return_if_fail (sheet != NULL);
	g_return_if_fail (IS_SHEET (sheet));

	str = gtk_entry_get_text (GTK_ENTRY (sheet->workbook->ea_input));
	g_return_if_fail (str != NULL);

	if (*str == '@'){
		char *new_text = g_strdup (str);

		*new_text = *str = '=';
		gtk_entry_set_text (GTK_ENTRY (sheet->workbook->ea_input), new_text);
		g_free (new_text);
	}

	r.start.col = r.end.col = sheet->cursor_col;
	r.start.row = r.end.row = sheet->cursor_row;
	sheet_set_text (sheet, str, &r);

	for (l = sheet->sheet_views; l; l = l->next){
		GnumericSheet *gsheet = GNUMERIC_SHEET_VIEW (l->data);

		gnumeric_sheet_destroy_editing_cursor (gsheet);
	}

	workbook_recalc (sheet->workbook);
}

static void
sheet_stop_editing (Sheet *sheet)
{
	sheet->editing = FALSE;

	if (sheet->editing_saved_text) {
		string_unref (sheet->editing_saved_text);
		sheet->editing_saved_text = NULL;
		sheet->editing_cell = NULL;
	}
}

void
sheet_accept_pending_input (Sheet *sheet)
{
	GList *l;

	g_return_if_fail (sheet != NULL);
	g_return_if_fail (IS_SHEET (sheet));

	if (!sheet->editing)
		return;

	sheet_set_current_value (sheet);

	/*
	 * If user was editing on the input line, get the focus back
	 */
	workbook_focus_current_sheet (sheet->workbook);
	
	for (l = sheet->sheet_views; l; l = l->next){
		GnumericSheet *gsheet = GNUMERIC_SHEET_VIEW (l->data);

		gnumeric_sheet_stop_editing (gsheet);
	}
	sheet_stop_editing (sheet);
}

void
sheet_cancel_pending_input (Sheet *sheet)
{
	GList *l;

	g_return_if_fail (sheet != NULL);
	g_return_if_fail (IS_SHEET (sheet));

	if (!sheet->editing)
		return;

	if (sheet->editing_cell) {
		const char *oldtext = sheet->editing_saved_text->str;
		
		gtk_entry_set_text (GTK_ENTRY (sheet->workbook->ea_input), oldtext);
		cell_set_text (sheet->editing_cell, oldtext);
	}
	sheet_stop_editing (sheet);

	for (l = sheet->sheet_views; l; l = l->next){
		GnumericSheet *gsheet = GNUMERIC_SHEET_VIEW (l->data);

		gnumeric_sheet_destroy_editing_cursor (gsheet);
	}
	workbook_recalc (sheet->workbook);
}

/**
 * Load the edit line with the value of the cell under the cursor
 * for @sheet.
 */
void
sheet_load_cell_val (Sheet *sheet)
{
	GtkEntry *entry;
	Cell *cell;

	g_return_if_fail (sheet != NULL);
	g_return_if_fail (IS_SHEET (sheet));

	entry = GTK_ENTRY (sheet->workbook->ea_input);
	cell = sheet_cell_get (sheet, sheet->cursor_col, sheet->cursor_row);

	if (cell) {
		char *text;

		text = cell_get_text (cell);
		gtk_entry_set_text (entry, text);
		g_free (text);
	} else
		gtk_entry_set_text (entry, "");
}

/**
 * sheet_start_editing_at_cursor:
 *
 * @sheet:    The sheet to be edited.
 * @blankp:   If true, erase current cell contents first.  If false, leave the
 *            contents alone.
 * @cursorp:  If true, create an editing cursor in the sheet itself.  (If
 *            false, the text will be editing in the edit box above the sheet,
 *            but this is not handled by this function.)
 *
 * Initiate editing of a cell in the sheet.  Note that we have two modes of
 * editing: (1) in-cell editing when you just start typing, and (2) above-
 * sheet editing when you hit F2.
 */
void
sheet_start_editing_at_cursor (Sheet *sheet, gboolean blankp, gboolean cursorp)
{
	GList *l;
	Cell  *cell;

	g_return_if_fail (sheet != NULL);
	g_return_if_fail (IS_SHEET (sheet));

	application_clipboard_clear ();
	
	if (blankp)
		gtk_entry_set_text (GTK_ENTRY (sheet->workbook->ea_input), "");

	if (cursorp)
		for (l = sheet->sheet_views; l; l = l->next){
			GnumericSheet *gsheet = GNUMERIC_SHEET_VIEW (l->data);
			gnumeric_sheet_create_editing_cursor (gsheet);
		}

	sheet->editing = TRUE;
	cell = sheet_cell_get (sheet, sheet->cursor_col, sheet->cursor_row);
	if (cell) {
		char *text;

		text = cell_get_text (cell);
		sheet->editing_saved_text = string_get (text);
		g_free (text);

		sheet->editing_cell = cell;
		if (blankp)
			cell_set_text (cell, "");
	}
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
	/* FIXME: hack for now, should be 'cursor cell' of selection */
	MStyle *mstyle;

	g_return_if_fail (sheet != NULL);

	mstyle = sheet_style_compute (sheet, sheet->cursor_col,
				      sheet->cursor_row);

	workbook_feedback_set (sheet->workbook, mstyle);
	mstyle_unref (mstyle);
}		

int
sheet_col_selection_type (Sheet const *sheet, int col)
{
	SheetSelection *ss;
	GList *l;
	int ret = ITEM_BAR_NO_SELECTION;

	g_return_val_if_fail (sheet != NULL, FALSE);
	g_return_val_if_fail (IS_SHEET (sheet), FALSE);

	if (sheet->selections == NULL){
		if (col == sheet->cursor_col)
			return ITEM_BAR_PARTIAL_SELECTION;
		return ret;
	}

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

	g_return_val_if_fail (sheet != NULL, FALSE);
	g_return_val_if_fail (IS_SHEET (sheet), FALSE);

	if (sheet->selections == NULL){
		if (row == sheet->cursor_row)
			return ITEM_BAR_PARTIAL_SELECTION;
		return ret;
	}

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
sheet_redraw_selection (Sheet const *sheet, SheetSelection const *ss)
{
	g_return_if_fail (sheet != NULL);
	g_return_if_fail (IS_SHEET (sheet));
	g_return_if_fail (ss != NULL);

	sheet_redraw_cell_region (sheet,
				  ss->user.start.col, ss->user.start.row,
				  ss->user.end.col, ss->user.end.row);
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
			new_col = 0;
		else if (new_col >= SHEET_MAX_COLS)
			new_col = SHEET_MAX_COLS-1;
		else if (jump_to_boundaries) {
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
	} while (keep_looking);

	return new_col;
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
			new_row = 0;
		else if (new_row > SHEET_MAX_ROWS-1)
			new_row = SHEET_MAX_ROWS-1;
		else if (jump_to_boundaries) {
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
	} while (keep_looking);

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
static gboolean
range_check_for_partial_array (Sheet *sheet,
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

/*
 * walk_boundaries: implements the decisions for walking a region
 * returns TRUE if the cursor left the boundary region
 */
static int
walk_boundaries (int lower_col,   int lower_row,
		 int upper_col,   int upper_row,
		 int inc_x,       int inc_y,
		 int current_col, int current_row,
		 int *new_col,    int *new_row)
{
	if (current_row + inc_y > upper_row ||
	    current_col + inc_x > upper_col){
		*new_row = current_row;
		*new_col = current_col;
		return TRUE;
	} else {
		if (current_row + inc_y < lower_row ||
		    current_col + inc_x < lower_col){
			*new_row = current_row;
			*new_col = current_col;
			return TRUE;
		} else {
			*new_row = current_row + inc_y;
			*new_col = current_col + inc_x;
		}
	}
	return FALSE;
}

/*
 * walk_boundaries: implements the decitions for walking a region
 * returns TRUE if the cursor left the boundary region.  This
 * version implements wrapping on the regions.
 */
static int
walk_boundaries_wrapped (int lower_col,   int lower_row,
			 int upper_col,   int upper_row,
			 int inc_x,       int inc_y,
			 int current_col, int current_row,
			 int *new_col,    int *new_row)
{
	if (current_row + inc_y > upper_row){
		if (current_col + 1 > upper_col)
			goto overflow;

		*new_row = lower_row;
		*new_col = current_col + 1;
		return FALSE;
	}

	if (current_row + inc_y < lower_row){
		if (current_col - 1 < lower_col)
			goto overflow;

		*new_row = upper_row;
		*new_col = current_col - 1;
		return FALSE;
	}

	if (current_col + inc_x > upper_col){
		if (current_row + 1 > upper_row)
			goto overflow;

		*new_row = current_row + 1;
		*new_col = lower_col;
		return FALSE;
	}

	if (current_col + inc_x < lower_col){
		if (current_row - 1 < lower_row)
			goto overflow;
		*new_row = current_row - 1;
		*new_col = upper_col;
		return FALSE;
	}

	*new_row = current_row + inc_y;
	*new_col = current_col + inc_x;
	return FALSE;

overflow:
	*new_row = current_row;
	*new_col = current_col;
	return TRUE;
}

int
sheet_selection_walk_step (Sheet *sheet, int forward, int horizontal,
			   int current_col, int current_row,
			   int *new_col, int *new_row)
{
	SheetSelection *ss;
	int inc_x = 0, inc_y = 0;
	int selections_count, diff, overflow;;

	diff = forward ? 1 : -1;

	if (horizontal)
		inc_x = diff;
	else
		inc_y = diff;

	selections_count = g_list_length (sheet->selections);

	if (selections_count == 1){
		ss = sheet->selections->data;

		/* If there is no selection besides the cursor, plain movement */
		if (ss->user.start.col == ss->user.end.col && ss->user.start.row == ss->user.end.row){
			walk_boundaries (0, 0, SHEET_MAX_COLS-1, SHEET_MAX_ROWS-1,
					 inc_x, inc_y, current_col, current_row,
					 new_col, new_row);
			return FALSE;
		}
	}

	if (!sheet->cursor_selection)
		sheet->cursor_selection = sheet->selections->data;

	ss = sheet->cursor_selection;

	overflow = walk_boundaries_wrapped (
		ss->user.start.col, ss->user.start.row,
		ss->user.end.col,   ss->user.end.row,
		inc_x, inc_y, current_col, current_row,
		new_col, new_row);

	if (overflow){
		int p;

		p = g_list_index (sheet->selections, ss);
		p += diff;
		if (p < 0)
			p = selections_count - 1;
		else if (p == selections_count)
			p = 0;

		ss = g_list_nth (sheet->selections, p)->data;
		sheet->cursor_selection = ss;

		if (forward){
			*new_col = ss->user.start.col;
			*new_row = ss->user.start.row;
		} else {
			*new_col = ss->user.end.col;
			*new_row = ss->user.end.row;
		}
	}
	return TRUE;
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
 * sheet_row_get:
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
		cell_get_span (cell_on_spot, &left, &right);
		if (left != right)
			cell_register_span (cell_on_spot, left, right);
	}
	cell_get_span (cell, &left, &right);
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
	cell_calc_dimensions (cell);

	sheet_cell_add_to_hash (sheet, cell);
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

	cell_unregister_span (cell);
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

	for (l = sheet->sheet_views; l; l = l->next){
		SheetView *sheet_view = l->data;

		gtk_object_unref (GTK_OBJECT (sheet_view));
	}
	g_list_free (sheet->sheet_views);
	sheet->sheet_views = NULL;
	
	g_list_free (sheet->comment_list);
	sheet->comment_list = NULL;

	sheet_destroy_contents (sheet);

	if (sheet->dependency_hash != NULL) {
		if (g_hash_table_size (sheet->dependency_hash) != 0)
			g_warning ("Dangling dependencies");
		g_hash_table_destroy (sheet->dependency_hash);
		sheet->dependency_hash = NULL;
	}

	sheet_destroy_styles (sheet);

	g_hash_table_destroy (sheet->cell_hash);

	expr_name_clean_sheet (sheet);

	if (sheet->dependency_hash)
		g_hash_table_destroy (sheet->dependency_hash);

	sheet->signature = 0;
	g_free (sheet);
}


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

/**
 * sheet_clear_region:
 *
 * Clears are region of cells
 *
 * @keepStyles : If this is non-null then styles are not erased.
 *
 * We assemble a list of cells to destroy, since we will be making changes
 * to the structure being manipulated by the sheet_cell_foreach_range routine
 */
void
sheet_clear_region (Sheet *sheet, int start_col, int start_row, int end_col, int end_row,
		    void *keepStyles)
{
	struct sheet_clear_region_callback_data cb;
	GList *l;

	g_return_if_fail (sheet != NULL);
	g_return_if_fail (IS_SHEET (sheet));
	g_return_if_fail (start_col <= end_col);
	g_return_if_fail (start_row <= end_row);

	/* Queue a redraw for the cells being removed */
	sheet_redraw_cell_region (sheet, start_col, start_row, end_col, end_row);

	cb.r.start.col = start_col;
	cb.r.start.row = start_row;
	cb.r.end.col = end_col;
	cb.r.end.row = end_row;
	cb.l = NULL;

	/* Clear the style in the region (new_default will ref the style for us). */
	if (!keepStyles)
		sheet_style_attach (sheet, cb.r, mstyle_new_default ());

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
		workbook_recalc (sheet->workbook);
	} else 
		gnumeric_no_modify_array_notice (sheet->workbook);
	g_list_free (cb.l);
}

static Value *
clear_cell_content (Sheet *sheet, int col, int row, Cell *cell, void *user_data)
{
	cell_set_text (cell, "");

	return NULL;
}

/**
 * sheet_clear_region_content:
 * @sheet:     The sheet on which we operate
 * @start_col: starting column
 * @start_row: starting row
 * @end_col:   end column
 * @end_row:   end row
 *
 * Clears the contents in a region of cells
 */
void
sheet_clear_region_content (Sheet *sheet, int start_col, int start_row, int end_col, int end_row,
			    void *closure)
{
	g_return_if_fail (sheet != NULL);
	g_return_if_fail (IS_SHEET (sheet));
	g_return_if_fail (start_col <= end_col);
	g_return_if_fail (start_row <= end_row);

	/* Queue a redraw for the region being redrawn */
	sheet_redraw_cell_region (sheet, start_col, start_row, end_col, end_row);

	sheet_cell_foreach_range (
		sheet, TRUE,
		start_col, start_row,
		end_col, end_row,
		clear_cell_content, NULL);

	workbook_recalc (sheet->workbook);
}

static Value *
clear_cell_comments (Sheet *sheet, int col, int row, Cell *cell, void *user_data)
{
	cell_comment_destroy (cell);
	return NULL;
}

/**
 * sheet_clear_region_comments:
 * @sheet:     The sheet on which we operate
 * @start_col: starting column
 * @start_row: starting row
 * @end_col:   end column
 * @end_row:   end row
 *
 * Removes all of the comments in the cells in the specified range.
 **/
void
sheet_clear_region_comments (Sheet *sheet, int start_col, int start_row, int end_col, int end_row,
			     void *closure)
{
	g_return_if_fail (sheet != NULL);
	g_return_if_fail (IS_SHEET (sheet));
	g_return_if_fail (start_col <= end_col);
	g_return_if_fail (start_row <= end_row);

	/* Queue a redraw for the region being redrawn */
	sheet_redraw_cell_region (sheet, start_col, start_row, end_col, end_row);

	sheet_cell_foreach_range (
		sheet, TRUE,
		start_col, start_row,
		end_col,   end_row,
		clear_cell_comments, NULL);
}

static Value *
clear_cell_format (Sheet *sheet, int col, int row, Cell *cell, void *user_data)
{
	cell_set_format (cell, "General");
	return NULL;
}

void
sheet_clear_region_formats (Sheet *sheet, int start_col, int start_row, int end_col, int end_row,
			    void *closure)
{
	g_return_if_fail (sheet != NULL);
	g_return_if_fail (IS_SHEET (sheet));
	g_return_if_fail (start_col <= end_col);
	g_return_if_fail (start_row <= end_row);

	/* Queue a draw for the region being modified */
	sheet_redraw_cell_region (sheet, start_col, start_row, end_col, end_row);
	sheet_cell_foreach_range (
		sheet, TRUE,
		start_col, start_row,
		end_col, end_row,
		clear_cell_format, NULL);
}

void
sheet_make_cell_visible (Sheet *sheet, int col, int row)
{
	GList *l;

	g_return_if_fail (sheet != NULL);
	g_return_if_fail (IS_SHEET (sheet));

	for (l = sheet->sheet_views; l; l = l->next){
		GnumericSheet *gsheet = GNUMERIC_SHEET_VIEW (l->data);

		gnumeric_sheet_make_cell_visible (gsheet, col, row);
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

	sheet_accept_pending_input (sheet);

	old_row = sheet->cursor_col;
	old_col = sheet->cursor_row;
	sheet->cursor_col = col;
	sheet->cursor_row = row;

	for (l = sheet->sheet_views; l; l = l->next){
		GnumericSheet *gsheet = GNUMERIC_SHEET_VIEW (l->data);

		gnumeric_sheet_cursor_set (gsheet, col, row);
		gnumeric_sheet_set_cursor_bounds (gsheet, col, row, col, row);
	}
	sheet_load_cell_val (sheet);

	if (old_row != row || old_col != col) {
		if (clear_selection)
			sheet_selection_reset_only (sheet);
		if (add_dest_to_selection)
			sheet_selection_append (sheet, col, row);
	}
}

void
sheet_cursor_set (Sheet *sheet, int base_col, int base_row, int start_col, int start_row, int end_col, int end_row)
{
	GList *l;

	g_return_if_fail (sheet != NULL);
	g_return_if_fail (IS_SHEET (sheet));
	g_return_if_fail (start_col <= end_col);
	g_return_if_fail (start_row <= end_row);

	sheet_accept_pending_input (sheet);

	sheet->cursor_col = base_col;
	sheet->cursor_row = base_row;

	for (l = sheet->sheet_views; l; l = l->next){
		GnumericSheet *gsheet = GNUMERIC_SHEET_VIEW (l->data);

		gnumeric_sheet_cursor_set (gsheet, base_col, base_row);
		gnumeric_sheet_set_cursor_bounds (
			gsheet,
			start_col, start_row,
			end_col, end_row);
	}
	sheet_load_cell_val (sheet);
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
sheet_fill_selection_with (Sheet *sheet, const char *str,
			   gboolean const is_array)
{
	GList *l;

	g_return_if_fail (sheet != NULL);
	g_return_if_fail (IS_SHEET (sheet));
	g_return_if_fail (str != NULL);

	/* Check for array subdivision */
	for (l = sheet->selections; l; l = l->next)
	{
		SheetSelection *ss = l->data;
		if (!range_check_for_partial_array (sheet,
						    ss->user.start.row,ss->user.start.col,
						    ss->user.end.row, ss->user.end.col)) {
			gnumeric_no_modify_array_notice (sheet->workbook);
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
		char *s;

		if (strchr (sheet->name, ' '))
			s = g_strconcat ("\"", sheet->name, "\"!", buffer, NULL);
		else
			s = g_strconcat (sheet->name, "!", buffer, NULL);

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

	while (**in && isdigit (**in)) {
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

	sheet->modified = FALSE;
}

void
sheet_set_dirty (Sheet *sheet, gboolean is_dirty)
{
	g_return_if_fail (sheet != NULL);
	g_return_if_fail (IS_SHEET (sheet));

	sheet->modified = is_dirty;
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
sheet_insert_object (Sheet *sheet, char *repoid)
{
#ifdef ENABLE_BONOBO
	GnomeClientSite *client_site;
	GnomeObjectClient *object_server;

	g_return_if_fail (sheet != NULL);
	g_return_if_fail (IS_SHEET (sheet));
	g_return_if_fail (repoid != NULL);

	if (strncmp (repoid, "moniker_url:", 12) == 0)
		object_server = gnome_object_activate (repoid, 0);
	else
		object_server = gnome_object_activate_with_repo_id (NULL, repoid, 0, NULL);
	
	if (!object_server){
		char *msg;

		msg = g_strdup_printf (_("I was not able to activate object %s"), repoid);

		gnumeric_notice (sheet->workbook, GNOME_MESSAGE_BOX_ERROR, msg);
		g_free (msg);
		return;
	}

	client_site = gnome_client_site_new (sheet->workbook->gnome_container);
	gnome_container_add (sheet->workbook->gnome_container, GNOME_OBJECT (client_site));

	if (!gnome_client_site_bind_embeddable (client_site, object_server)){
		gnumeric_notice (sheet->workbook, GNOME_MESSAGE_BOX_ERROR,
				 _("I was unable to the bind object"));
		gtk_object_unref (GTK_OBJECT (object_server));
		gtk_object_unref (GTK_OBJECT (client_site));
		return;
	}

#endif
}

void
sheet_set_selection (Sheet *sheet, SheetSelection const *ss)
{
	GList *l = sheet->sheet_views;
	for (; l != NULL; l = l->next) {
		GnumericSheet *gsheet = GNUMERIC_SHEET_VIEW (l->data);
		gnumeric_sheet_set_selection (gsheet, ss);
	}
}

/****************************************************************************/

/*
 * FIXME FIXME FIXME : 1999/Sept/29 when the style changes are done we need
 *  to figure out how row/col/cell movement will apply to that.
 */

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
	Cell *cell;

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
 * sheet_insert_col:
 * @sheet   The sheet
 * @col     At which position we want to insert
 * @count   The number of columns to be inserted
 */
void
sheet_insert_col (Sheet *sheet, int col, int count)
{
	struct expr_relocate_info reloc_info;
	GList *deps;
	int   i;

	g_return_if_fail (sheet != NULL);
	g_return_if_fail (IS_SHEET (sheet));
	g_return_if_fail (count != 0);

	/* Is there any work to do? */
	if (sheet->cols.max_used < 0)
		return;

	/* 0. Walk cells in displaced col and ensure arrays aren't divided. */
	if (col > 0)	/* No need to test leftmost column */
		if (sheet_cell_foreach_range (sheet, TRUE, col, 0,
					      col, SHEET_MAX_ROWS-1,
					      &avoid_dividing_array_horizontal,
					      NULL) != NULL){
			gnumeric_no_modify_array_notice (sheet->workbook);
			return;
		}

	/* TODO TODO : Walk the right edge to make sure nothing is split
	 * due to over run.
	 */

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
	workbook_expr_relocate (sheet->workbook, &reloc_info);

	/* 3. Move the columns to their new location (From right to left) */
	for (i = sheet->cols.max_used; i >= col ; --i)
		colrow_move (sheet, i, 0, i, SHEET_MAX_ROWS-1,
			     &sheet->cols, i, i + count);

	/* 4. Slide all the StyleRegion's right */
	sheet_style_insert_colrow (sheet, col, count, TRUE);

	/* 5. Recompute dependencies */
	deps = region_get_dependencies (sheet,
					col, 0,
					SHEET_MAX_COLS-1, SHEET_MAX_ROWS-1);
	cell_queue_recalc_list (deps, TRUE);
	workbook_recalc (sheet->workbook);

	/* 6. Redraw */
	sheet_redraw_all (sheet);
}

/*
 * sheet_delete_col
 * @sheet   The sheet
 * @col     At which position we want to start deleting columns
 * @count   The number of columns to be deleted
 */
void
sheet_delete_col (Sheet *sheet, int col, int count)
{
	struct expr_relocate_info reloc_info;
	GList *deps;
	int i;

	g_return_if_fail (sheet != NULL);
	g_return_if_fail (IS_SHEET (sheet));
	g_return_if_fail (count != 0);

	/* Is there any work to do? */
	if (sheet->cols.max_used < 0)
		return;

	/* 0. Walk cells in deleted cols and ensure arrays aren't divided. */
	if (!range_check_for_partial_array (sheet, 0, col, 
					    SHEET_MAX_ROWS-1, col+count-1))
	{
		gnumeric_no_modify_array_notice (sheet->workbook);
		return;
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
	workbook_expr_relocate (sheet->workbook, &reloc_info);

	/* 3. Fix references to and from the cells which are moving */
	reloc_info.origin.start.col = col+count;
	reloc_info.origin.end.col = SHEET_MAX_COLS-1;
	reloc_info.col_offset = -count;
	reloc_info.row_offset = 0;
	workbook_expr_relocate (sheet->workbook, &reloc_info);

	/* 4. Move the columns to their new location (from left to right) */
	for (i = col + count ; i <= sheet->cols.max_used; ++i)
		colrow_move (sheet, i, 0, i, SHEET_MAX_ROWS-1,
			     &sheet->cols, i, i-count);

	/* 5. Slide all the StyleRegion's up */
	sheet_style_delete_colrow (sheet, col, count, TRUE);

	/* 6. Recompute dependencies */
	deps = region_get_dependencies (sheet,
					col, 0,
					SHEET_MAX_COLS-1, SHEET_MAX_ROWS-1);
	cell_queue_recalc_list (deps, TRUE);
	workbook_recalc (sheet->workbook);

	/* 7. Redraw */
	sheet_redraw_all (sheet);
}

/**
 * sheet_insert_row:
 * @sheet   The sheet
 * @row     At which position we want to insert
 * @count   The number of rows to be inserted
 */
void
sheet_insert_row (Sheet *sheet, int row, int count)
{
	struct expr_relocate_info reloc_info;
	GList *deps;
	int   i;

	g_return_if_fail (sheet != NULL);
	g_return_if_fail (IS_SHEET (sheet));
	g_return_if_fail (count != 0);

	/* Is there any work to do? */
	if (sheet->rows.max_used < 0)
		return;

	/* 0. Walk cells in displaced row and ensure arrays aren't divided. */
	if (row > 0)	/* No need to test leftmost column */
		if (sheet_cell_foreach_range (sheet, TRUE,
					      0, row,
					      SHEET_MAX_COLS-1, row,
					      &avoid_dividing_array_vertical,
					      NULL) != NULL){
			gnumeric_no_modify_array_notice (sheet->workbook);
			return;
		}

	/* TODO TODO : Walk the right edge to make sure nothing is split
	 * due to over run.
	 */

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
	workbook_expr_relocate (sheet->workbook, &reloc_info);

	/* 3. Move the rows to their new location (from bottom to top) */
	for (i = sheet->rows.max_used; i >= row ; --i)
		colrow_move (sheet, 0, i, SHEET_MAX_COLS-1, i,
			     &sheet->rows, i, i+count);

	/* 4. Slide all the StyleRegion's right */
	sheet_style_insert_colrow (sheet, row, count, FALSE);

	/* 5. Recompute dependencies */
	deps = region_get_dependencies (sheet,
					0, row,
					SHEET_MAX_COLS-1, SHEET_MAX_ROWS-1);
	cell_queue_recalc_list (deps, TRUE);
	workbook_recalc (sheet->workbook);

	/* 6. Redraw */
	sheet_redraw_all (sheet);
}

/*
 * sheet_delete_row
 * @sheet   The sheet
 * @row     At which position we want to start deleting rows
 * @count   The number of rows to be deleted
 */
void
sheet_delete_row (Sheet *sheet, int row, int count)
{
	struct expr_relocate_info reloc_info;
	GList *deps;
	int i;

	g_return_if_fail (sheet != NULL);
	g_return_if_fail (IS_SHEET (sheet));
	g_return_if_fail (count != 0);

	/* Is there any work to do? */
	if (sheet->rows.max_used < 0)
		return;

	/* 0. Walk cells in deleted rows and ensure arrays aren't divided. */
	if (!range_check_for_partial_array (sheet, row, 0, 
					    row+count-1, SHEET_MAX_COLS-1))
	{
		gnumeric_no_modify_array_notice (sheet->workbook);
		return;
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
	workbook_expr_relocate (sheet->workbook, &reloc_info);

	/* 3. Fix references to and from the cells which are moving */
	reloc_info.origin.start.row = row + count;
	reloc_info.origin.end.row = SHEET_MAX_ROWS-1;
	reloc_info.col_offset = 0;
	reloc_info.row_offset = -count;
	workbook_expr_relocate (sheet->workbook, &reloc_info);

	/* 4. Move the rows to their new location (from top to bottom) */
	for (i = row + count ; i <= sheet->rows.max_used; ++i)
		colrow_move (sheet, 0, i, SHEET_MAX_COLS-1, i,
			     &sheet->rows, i, i-count);

	/* 5. Slide all the StyleRegion's up */
	sheet_style_delete_colrow (sheet, row, count, FALSE);

	/* 6. Recompute dependencies */
	deps = region_get_dependencies (sheet,
					0, row,
					SHEET_MAX_COLS-1, SHEET_MAX_ROWS-1);
	cell_queue_recalc_list (deps, TRUE);
	workbook_recalc (sheet->workbook);

	/* 7. Redraw */
	sheet_redraw_all (sheet);
}

/*
 * Excel does not support paste special from a 'cut' will we want to
 * do that ?
 */
void
sheet_move_range (struct expr_relocate_info const * rinfo)
{
	GList *deps, *cells = NULL;
	Cell  *cell;
	gboolean inter_sheet_formula;
	int	dummy;

	g_return_if_fail (rinfo->origin_sheet != NULL);
	g_return_if_fail (IS_SHEET (rinfo->origin_sheet));
	g_return_if_fail (rinfo->target_sheet != NULL);
	g_return_if_fail (IS_SHEET (rinfo->target_sheet));

	/* 0. Walk cells in the moving range and ensure arrays aren't divided. */

	/* 1. Fix references to and from the cells which are moving */
	/* All we really want is the workbook so either sheet will do */
	workbook_expr_relocate (rinfo->origin_sheet->workbook, rinfo);

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
	/* Pass a pointer, any pointer, rather than NULL. That flags
	 * not to clear styles */
	sheet_clear_region (rinfo->target_sheet, 
			    rinfo->origin.start.col + rinfo->col_offset,
			    rinfo->origin.start.row + rinfo->row_offset,
			    rinfo->origin.end.col + rinfo->col_offset,
			    rinfo->origin.end.row + rinfo->row_offset,
			    &dummy);

	/* Insert the cells back */
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

	/* 5. Slide styles */
	sheet_style_relocate (rinfo);

	/* 6. Recompute dependencies */
	/* TODO : What region needs this ? the source or the target ? */
	deps = region_get_dependencies (rinfo->target_sheet,
					0, 0,
					SHEET_MAX_COLS-1, SHEET_MAX_ROWS-1);
	cell_queue_recalc_list (deps, TRUE);
	workbook_recalc (rinfo->target_sheet->workbook);

	/* 7. Redraw */
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
sheet_shift_rows (Sheet *sheet, int col, int start_row, int end_row, int count)
{
	struct expr_relocate_info rinfo;
	rinfo.origin.start.col = col;
	rinfo.origin.start.row = start_row;
	rinfo.origin.end.col = SHEET_MAX_COLS-1;
	rinfo.origin.end.row = end_row;
	rinfo.origin_sheet = rinfo.target_sheet = sheet;
	rinfo.col_offset = count;
	rinfo.row_offset = 0;

	sheet_move_range (&rinfo);
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
sheet_shift_cols (Sheet *sheet, int start_col, int end_col, int row, int count)
{
	struct expr_relocate_info rinfo;
	rinfo.origin.start.col = start_col;
	rinfo.origin.start.row = row;
	rinfo.origin.end.col = end_col;
	rinfo.origin.end.row = SHEET_MAX_ROWS-1;
	rinfo.origin_sheet = rinfo.target_sheet = sheet;
	rinfo.col_offset = 0;
	rinfo.row_offset = count;

	sheet_move_range (&rinfo);
}
