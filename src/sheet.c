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
#ifdef ENABLE_BONOBO
#    include <libgnorba/gnorba.h>
#endif

#undef DEBUG_CELL_FORMULA_LIST

#define GNUMERIC_SHEET_VIEW(p) GNUMERIC_SHEET (SHEET_VIEW(p)->sheet_view);

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
	cri->data = NULL;
}

static void
sheet_init_default_styles (Sheet *sheet)
{
	/* Sizes seem to match excel */
	col_row_info_init (&sheet->default_col_style, 62.0);
	col_row_info_init (&sheet->default_row_style, 15.0);
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
cell_compare (gconstpointer a, gconstpointer b)
{
	const CellPos *ca, *cb;

	ca = (const CellPos *) a;
	cb = (const CellPos *) b;

	if (ca->row != cb->row)
		return 0;
	if (ca->col != cb->col)
		return 0;

	return 1;
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
	Sheet *sheet;

	g_return_val_if_fail (wb != NULL, NULL);
	g_return_val_if_fail (name != NULL, NULL);

	sheet = g_new0 (Sheet, 1);
	sheet->signature = SHEET_SIGNATURE;
	sheet->workbook = wb;
	sheet->name = g_strdup (name);
	sheet->last_zoom_factor_used = -1.0;
	sheet->max_col_used = 0;
	sheet->max_row_used = 0;
	sheet->print_info   = print_info_new ();

	sheet->cell_hash = g_hash_table_new (cell_hash, cell_compare);

	sheet_init_default_styles (sheet);

	/* Dummy initialization */
	if (0)
		sheet_init_dummy_stuff (sheet);

	sheet_view = GTK_WIDGET (sheet_new_sheet_view (sheet));

	sheet_selection_append (sheet, 0, 0);

	gtk_widget_show (sheet_view);

	sheet_set_zoom_factor (sheet, 1.0);

	sheet_corba_setup (sheet);
	
	return sheet;
}

static void
cell_hash_free_key (gpointer key, gpointer value, gpointer user_data)
{
	g_free (key);
}

void
sheet_foreach_col (Sheet *sheet, sheet_col_row_callback callback, void *user_data)
{
	GList *l = sheet->cols_info;

	/* Invoke the callback for the default style */
	(*callback)(sheet, &sheet->default_col_style, user_data);

	/* And then for the rest */
	while (l){
		(*callback)(sheet, l->data, user_data);
		l = l->next;
	}
}

void
sheet_foreach_row (Sheet *sheet, sheet_col_row_callback callback, void *user_data)
{
	GList *l = sheet->rows_info;

	/* Invoke the callback for the default style */
	(callback)(sheet, &sheet->default_row_style, user_data);

	/* And then for the rest */
	while (l){
		(*callback)(sheet, l->data, user_data);
		l = l->next;
	}
}

static void
sheet_compute_col_row_new_size (Sheet *sheet, ColRowInfo *ci, void *data)
{
	double pix_per_unit = sheet->last_zoom_factor_used;

	ci->pixels = (ci->units + ci->margin_a_pt + ci->margin_b_pt) * pix_per_unit;

	/*
	 * Ensure that there is at least 1 pixel around every cell so that we
	 * can mark the current cell
	 */
	ci->margin_a = MAX(ci->margin_a_pt * pix_per_unit, 1);
	ci->margin_b = MAX(ci->margin_b_pt * pix_per_unit, 1);
}

static Value *
zoom_cell_style (Sheet *sheet, int col, int row, Cell *cell, void *user_data)
{
/*	StyleFont *sf;*/

	/*
	 * If the size is already set, skip
	 */
/*	if (cell->style->font->scale == sheet->last_zoom_factor_used)
		return NULL;

	sf = style_font_new_from (cell->style->font, sheet->last_zoom_factor_used);
	cell_set_font_from_style (cell, sf);
	style_font_unref (sf);*/
	g_warning ("FIXME: unimplemented zoom_cell_style");

	return NULL;
}

void
sheet_set_zoom_factor (Sheet *sheet, double factor)
{
	GList *l, *cl;

	sheet->last_zoom_factor_used = factor;

	/* First, the default styles */
	sheet_compute_col_row_new_size (sheet, &sheet->default_row_style, NULL);
	sheet_compute_col_row_new_size (sheet, &sheet->default_col_style, NULL);

	/* Then every column and row */
	sheet_foreach_col (sheet, sheet_compute_col_row_new_size, NULL);
	sheet_foreach_row (sheet, sheet_compute_col_row_new_size, NULL);

	for (l = sheet->sheet_views; l; l = l->next){
		SheetView *sheet_view = l->data;

		sheet_view_set_zoom_factor (sheet_view, factor);
	}

	for (cl = sheet->comment_list; cl; cl = cl->next){
		Cell *cell = cl->data;

		cell_comment_reposition (cell);
	}

	/*
	 * Scale the fonts for every cell
	 */
	sheet_cell_foreach_range (
		sheet, TRUE, 0, 0, SHEET_MAX_COLS, SHEET_MAX_ROWS,
		zoom_cell_style, sheet);

	/*
	 * Scale the internal font styles
	 */
	for (l = sheet->style_list; l; l = l->next) {
		g_warning ("Internal font scaling broken");
/*		StyleRegion *sr = l->data;
		Style *style = sr->style;
		StyleFont *scaled;
		
		if (!(style->valid_flags & STYLE_FONT))
			continue;

		scaled = style_font_new_from (style->font, factor);
		style_font_unref (style->font);
		style->font = scaled;*/
	}
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

	*ri = sheet->default_row_style;
	row_init_span (ri);

	return ri;
}

ColRowInfo *
sheet_col_new (Sheet *sheet)
{
	ColRowInfo *ci = g_new (ColRowInfo, 1);

	g_return_val_if_fail (sheet != NULL, NULL);
	g_return_val_if_fail (IS_SHEET (sheet), NULL);

	*ci = sheet->default_col_style;
	ci->data = NULL;

	return ci;
}


static gint
CRsort (gconstpointer a, gconstpointer b)
{
	const ColRowInfo *ia = (const ColRowInfo *) a;
	const ColRowInfo *ib = (const ColRowInfo *) b;

	return (ia->pos - ib->pos);
}

void
sheet_col_add (Sheet *sheet, ColRowInfo *cp)
{
	if (cp->pos > sheet->max_col_used){
		GList *l;

		sheet->max_col_used = cp->pos;

		for (l = sheet->sheet_views; l; l = l->next){
			SheetView *sheet_view = l->data;
			GtkAdjustment *ha = GTK_ADJUSTMENT (sheet_view->ha);

			if (sheet->max_col_used > ha->upper){
				ha->upper = sheet->max_col_used;
				gtk_adjustment_changed (ha);
			}
		}
	}

	sheet->cols_info = g_list_insert_sorted (sheet->cols_info, cp, CRsort);
}

void
sheet_row_add (Sheet *sheet, ColRowInfo *rp)
{
	if (rp->pos > sheet->max_row_used){
		GList *l;

		sheet->max_row_used = rp->pos;

		for (l = sheet->sheet_views; l; l = l->next){
			SheetView *sheet_view = l->data;
			GtkAdjustment *va = GTK_ADJUSTMENT (sheet_view->va);

			if (sheet->max_row_used > va->upper){
				va->upper = sheet->max_row_used;
				gtk_adjustment_changed (va);
			}
		}
	}
	sheet->rows_info = g_list_insert_sorted (sheet->rows_info, rp, CRsort);
}

ColRowInfo *
sheet_col_get_info (Sheet *sheet, int col)
{
	GList *l = sheet->cols_info;

	for (; l; l = l->next){
		ColRowInfo *ci = l->data;

		if (ci->pos == col)
			return ci;
	}

	return &sheet->default_col_style;
}

ColRowInfo *
sheet_row_get_info (Sheet *sheet, int row)
{
	GList *l = sheet->rows_info;

	for (; l; l = l->next){
		ColRowInfo *ri = l->data;

		if (ri->pos == row)
			return ri;
	}

	return &sheet->default_row_style;
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
	double pix = sheet->last_zoom_factor_used;

	info->units  = (info->pixels -
			(info->margin_a + info->margin_b + 1)) / pix;
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

void
sheet_row_info_set_height (Sheet *sheet, ColRowInfo *ri, int height, gboolean height_set_by_user)
{
	g_return_if_fail (sheet != NULL);
	g_return_if_fail (IS_SHEET (sheet));
	g_return_if_fail (ri != NULL);

	if (height_set_by_user)
		ri->hard_size = 1;

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
	if (ri == &sheet->default_row_style){
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
	if (ri == &sheet->default_col_style){
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
	if (ci == &sheet->default_col_style){
		ci = sheet_col_new (sheet);
		ci->pos = col;
		sheet_col_add (sheet, ci);
	}

	sheet_col_set_internal_width (sheet, ci, width);
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
	GList *l;
	int max = 0;
	int margins;
	
	g_return_val_if_fail (sheet != NULL, 0);
	g_return_val_if_fail (IS_SHEET (sheet), 0);
	g_return_val_if_fail (col >= 0, 0);
	g_return_val_if_fail (col < SHEET_MAX_COLS, 0);

	ci = sheet_col_get_info (sheet, col);

	/*
	 * If ci == sheet->default_col_style then it means
	 * no cells have been allocated here
	 */
	if (ci == &sheet->default_col_style)
		return ci->pixels;

	margins = ci->margin_a + ci->margin_b;
	
	for (l = ci->data; l; l = l->next){
		Cell *cell = l->data;
		int width;
			
		width = cell->width + margins;
		
		if (width > max)
			max = width;
	}

	return max;
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
	GList *clist;
	int max = 0;
	int margins;
	int height;
	
	g_return_val_if_fail (sheet != NULL, 0);
	g_return_val_if_fail (IS_SHEET (sheet), 0);
	g_return_val_if_fail (row >= 0, 0);
	g_return_val_if_fail (row < SHEET_MAX_ROWS, 0);

	ri = sheet_row_get_info (sheet, row);

	/*
	 * If ri == sheet->default_row_style then it means
	 * no cells have been allocated here
	 */
	if (ri == &sheet->default_row_style)
		return ri->pixels;

	margins = ri->margin_a + ri->margin_b;

	/*
	 * Now, we need to scan all columns, as the list of rows
	 * does not contains pointers to the cells.
	 */
	for (clist = sheet->cols_info; clist; clist = clist->next){
		ColRowInfo *ci = clist->data;
		Cell *cell;
		
		cell = sheet_cell_get (sheet, ci->pos, row);
		if (!cell)
			continue;

		height = cell->height + margins;
		
		if (height > max)
			max = height;
	}

	return max;
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
	GList *l, *cells;
	Cell *cell;

	cells = NULL;
	for (l = sheet->rows_info; l; l = l->next){
		ColRowInfo *ri = l->data;

		if (!(cell = sheet_cell_get (sheet, col, ri->pos)))
			cell = row_cell_get_displayed_at (ri, col);

		if (cell)
			cells = g_list_prepend (cells, cell);
	}

	/* No spans, just return */
	if (!cells)
		return;

	/* Unregister those cells that touched this column */
	for (l = cells; l; l = l->next){
		int left, right;

		cell = l->data;

		cell_unregister_span (cell);
		cell_get_span (cell, &left, &right);
		if (left != right)
			cell_register_span (cell, left, right);
	}

	g_list_free (cells);
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
	if (ci == &sheet->default_col_style){
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

static inline int
col_row_distance (GList *list, int from, int to, int default_pixels)
{
	ColRowInfo *cri;
	int pixels = 0, n = 0;
	GList *l;

	if (to == from)
		return 0;

	n = to - from;

	for (l = list; l; l = l->next){
		cri = l->data;

		if (cri->pos >= to)
			break;

		if (cri->pos >= from){
			n--;
			pixels += cri->pixels;
		}
	}
	pixels += n * default_pixels;

	return pixels;
}

/**
 * sheet_col_get_distance:
 *
 * Return the number of pixels between from_col to to_col
 */
int
sheet_col_get_distance (Sheet const *sheet, int from_col, int to_col)
{
	g_assert (from_col <= to_col);
	g_assert (sheet != NULL);

	return col_row_distance (sheet->cols_info, from_col, to_col, sheet->default_col_style.pixels);
}

static inline double
col_row_unit_distance (GList *list, int from, int to, double default_units, double default_margins)
{
	ColRowInfo *cri;
	double units = 0;
	int n = 0;
	GList *l;
	
	if (to == from)
		return 0;

	n = to - from;
	
	for (l = list; l; l = l->next){
		cri = l->data;
		
		if (cri->pos >= to)
			break;
		
		if (cri->pos >= from){
			n--;
			units += cri->units + cri->margin_a_pt + cri->margin_b_pt;
		}
	}
	units += n * default_units + n * default_margins;
	
	return units;
}

/**
 * sheet_col_get_unit_distance:
 *
 * Return the number of points between from_col to to_col
 */
double
sheet_col_get_unit_distance (Sheet const *sheet, int from_col, int to_col)
{
	g_assert (from_col <= to_col);
	g_assert (sheet != NULL);

	return col_row_unit_distance (sheet->cols_info, from_col, to_col,
				      sheet->default_col_style.units,
				      sheet->default_col_style.margin_a_pt +
				      sheet->default_col_style.margin_b_pt);
}

/**
 * sheet_row_get_unit_distance:
 *
 * Return the number of points between from_row to to_row
 */
double
sheet_row_get_unit_distance (Sheet const *sheet, int from_row, int to_row)
{
	g_assert (from_row <= to_row);
	g_assert (sheet != NULL);

	return col_row_unit_distance (sheet->rows_info, from_row, to_row,
				      sheet->default_row_style.units,
				      sheet->default_row_style.margin_a +
				      sheet->default_row_style.margin_b);
}

/**
 * sheet_row_get_distance:
 *
 * Return the number of pixels between from_row to to_row
 */
int
sheet_row_get_distance (Sheet const *sheet, int from_row, int to_row)
{
	g_assert (from_row <= to_row);
	g_assert (sheet != NULL);

	return col_row_distance (sheet->rows_info, from_row, to_row, sheet->default_row_style.pixels);
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


void
sheet_set_text (Sheet *sheet, int col, int row, const char *str)
{
	GList *l;
	Cell *cell;
	char *text;
	int  text_set = FALSE;

	g_return_if_fail (sheet != NULL);
	g_return_if_fail (IS_SHEET (sheet));

	cell = sheet_cell_get (sheet, col, row);

	if (!cell)
		cell = sheet_cell_new (sheet, col, row);

	text = gtk_entry_get_text (GTK_ENTRY (sheet->workbook->ea_input));

	if (*text == '@'){
		char *new_text = g_strdup (text);

		*new_text = '=';
		gtk_entry_set_text (GTK_ENTRY (sheet->workbook->ea_input), new_text);
		g_free (new_text);
	}

	/*
	 * Figure out if a format matches, and for sanity compare that to
	 * a rendered version of the text, if they compare equally, then
	 * use that.
	 */
	if (*text != '=') {
		char *end, *format;
		float_t v;

		strtod (text, &end);
		if (end != text && *end == 0) {
			/*
			 * It is a number -- remain in General format.  Note
			 * that we would other wise actually set a "0" format
			 * for integers and that it would stick.
			 */
		} else if (format_match (text, &v, &format)) {
			if (!CELL_IS_FORMAT_SET (cell))
				cell_set_format_simple (cell, format);
			cell_set_value (cell, value_new_float (v));
			text_set = TRUE;
		}
	}

	if (!text_set)
		cell_set_text (cell, text);

	for (l = sheet->sheet_views; l; l = l->next){
		GnumericSheet *gsheet = GNUMERIC_SHEET_VIEW (l->data);

		gnumeric_sheet_destroy_editing_cursor (gsheet);
	}

	workbook_recalc (sheet->workbook);

}

void
sheet_set_current_value (Sheet *sheet)
{
	char *str;

	g_return_if_fail (sheet != NULL);
	g_return_if_fail (IS_SHEET (sheet));

	str = gtk_entry_get_text (GTK_ENTRY (sheet->workbook->ea_input));
	sheet_set_text (sheet, sheet->cursor_col, sheet->cursor_row, str);
}

static void
sheet_stop_editing (Sheet *sheet)
{
	sheet->editing = FALSE;

	if (sheet->editing_saved_text){
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
}

void
sheet_load_cell_val (Sheet *sheet)
{
	GtkEntry *entry;
	Cell *cell;

	g_return_if_fail (sheet != NULL);
	g_return_if_fail (IS_SHEET (sheet));

	entry = GTK_ENTRY (sheet->workbook->ea_input);
	cell = sheet_cell_get (sheet, sheet->cursor_col, sheet->cursor_row);

	if (cell){
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

	if (blankp)
		gtk_entry_set_text (GTK_ENTRY (sheet->workbook->ea_input), "");

	if (cursorp)
		for (l = sheet->sheet_views; l; l = l->next){
			GnumericSheet *gsheet = GNUMERIC_SHEET_VIEW (l->data);
			gnumeric_sheet_create_editing_cursor (gsheet);
		}

	sheet->editing = TRUE;
	cell = sheet_cell_get (sheet, sheet->cursor_col, sheet->cursor_row);
	if (cell){
		char *text;

		text = cell_get_text (cell);
		sheet->editing_saved_text = string_get (text);
		g_free (text);

		sheet->editing_cell = cell;
		if (blankp)
			cell_set_text (cell, "");
	}
}

/****************************************************************************/

/*typedef struct
{
	gboolean bold;
	gboolean bold_common;

	gboolean italic;
	gboolean italic_common;

	GnomeFont *font;
	gboolean font_common;

	double font_size;
	gboolean font_size_common;

	gboolean first;
} range_homogeneous_style_p;

static Value *
cell_is_homogeneous (Sheet *sheet, int col, int row,
		     Cell *cell, void *user_data)
{
	range_homogeneous_style_p *accum = user_data;

	if (accum->first) {
		accum->bold = cell->style->font->is_bold;
		accum->italic = cell->style->font->is_italic;
		accum->font = cell->style->font->font;
		accum->first = FALSE;
		accum->font_size = cell->style->font->size;
	} else {
		if (accum->italic != cell->style->font->is_italic)
			accum->italic_common = FALSE;
		if (accum->bold != cell->style->font->is_bold)
			accum->bold_common = FALSE;
		if (accum->font != cell->style->font->font)
			accum->font_common = FALSE;

		if ((accum->bold_common == FALSE) &&
		    (accum->italic_common == FALSE) &&
		    (accum->font_common == FALSE))
			return value_terminate();
	}
	return NULL;
}*/

static void
range_is_homogeneous(Sheet *sheet, 
		     int start_col, int start_row,
		     int end_col,   int end_row,
		     void *closure)
{
	/*
	 * FIXME : Only check existing cells for now.  In when styles are
	 * redone this will need rethinking.
	 */
/*	sheet_cell_foreach_range (sheet, TRUE,
				  start_col, start_row, end_col, end_row,
				  &cell_is_homogeneous, closure);*/
	g_warning ("Fixme\n");
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
/*	range_homogeneous_style_p closure;
	int flags;
	
	memset (&closure, 0, sizeof (closure));
	closure.first = TRUE;
	closure.bold_common = TRUE;
	closure.italic_common = TRUE;
	closure.font_common = TRUE;
	closure.font_size_common = TRUE;

*//* Double counting is ok, don't bother breaking up the regions *//*
	selection_apply (sheet, &range_is_homogeneous,
			 TRUE, &closure);

	if (closure.first) {
								   *//*
		 * If no cells are on the selection, use the first cell
		 * in the range to compute the values
		 *//*
		SheetSelection const * const ss = sheet->selections->data;
		Style *style = sheet_style_compute (sheet, ss->user.start.col,
						    ss->user.start.row, NULL);
		closure.bold = style->font->is_bold;
		closure.italic = style->font->is_italic;
		closure.font = style->font->font;
		closure.font_size = style->font->size;

		flags = WORKBOOK_FEEDBACK_BOLD |
			WORKBOOK_FEEDBACK_ITALIC |
			WORKBOOK_FEEDBACK_FONT_SIZE |
			WORKBOOK_FEEDBACK_FONT;
		
		style_destroy (style);
	} else 
		flags = (closure.bold_common ? WORKBOOK_FEEDBACK_BOLD : 0) |
			(closure.italic_common ? WORKBOOK_FEEDBACK_ITALIC : 0) |
			(closure.font_size_common ? WORKBOOK_FEEDBACK_FONT_SIZE : 0) |
			(closure.font_common ? WORKBOOK_FEEDBACK_FONT : 0);
	
	if (flags == 0)
		return;
	
	workbook_feedback_set (sheet->workbook, flags,
			       closure.italic,
			       closure.bold,
			       closure.font_size,
			       closure.font);*/
	g_warning ("implement sheet_update_controls");
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
	gboolean find_first = cell_is_blank(sheet_cell_get (sheet, start_col, row));
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
		else if (new_col > SHEET_MAX_COLS-1)
			new_col = SHEET_MAX_COLS-1;
		else if (jump_to_boundaries) {
			keep_looking = (cell_is_blank( sheet_cell_get (sheet, new_col, row)) == find_first);
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

	sheet_make_cell_visible (sheet, new_col, row);
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
	gboolean find_first = cell_is_blank(sheet_cell_get (sheet, col, start_row));
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
			keep_looking = (cell_is_blank( sheet_cell_get (sheet, col, new_row)) == find_first);
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

	sheet_make_cell_visible (sheet, col, new_row);
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
			       int const end_row, int const end_col)
{
	ArrayRef *a;
	gboolean valid = TRUE;
	gboolean single;
	int r, c;

	if (start_row > 0 || end_row < SHEET_MAX_ROWS)
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
			walk_boundaries (0, 0, SHEET_MAX_COLS, SHEET_MAX_ROWS,
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
 * Returns an allocated column:  either an existing one, or a fresh copy
 */
ColRowInfo *
sheet_col_get (Sheet *sheet, int pos)
{
	GList *clist;
	ColRowInfo *col;

	g_return_val_if_fail (sheet != NULL, NULL);
	g_return_val_if_fail (IS_SHEET (sheet), NULL);

	for (clist = sheet->cols_info; clist; clist = clist->next){
		col = (ColRowInfo *) clist->data;

		if (col->pos == pos)
			return col;
	}
	col = sheet_col_new (sheet);
	col->pos = pos;
	sheet_col_add (sheet, col);

	return col;
}

/**
 * sheet_row_get:
 *
 * Returns an allocated row:  either an existing one, or a fresh copy
 */
ColRowInfo *
sheet_row_get (Sheet *sheet, int pos)
{
	GList *rlist;
	ColRowInfo *row;

	g_return_val_if_fail (sheet != NULL, NULL);
	g_return_val_if_fail (IS_SHEET (sheet), NULL);

	for (rlist = sheet->rows_info; rlist; rlist = rlist->next){
		row = (ColRowInfo *) rlist->data;

		if (row->pos == pos)
			return row;
	}
	row = sheet_row_new (sheet);
	row->pos = pos;
	sheet_row_add (sheet, row);

	return row;
}

static int
gen_row_blanks (Sheet *sheet, int col, int start_row, int end_row,
		sheet_cell_foreach_callback callback, void *closure)
{
	int row;

	for (row = 0; row < end_row; row++)
		if (!(*callback)(sheet, col, row, NULL, closure))
			return FALSE;

	return TRUE;
}

static int
gen_col_blanks (Sheet *sheet, int start_col, int end_col,
		int start_row, int end_row,
		sheet_cell_foreach_callback callback, void *closure)
{
	int col;

	for (col = 0; col < end_col; col++)
		if (!gen_row_blanks (sheet, col, start_row, end_row, callback, closure))
			return FALSE;

	return TRUE;
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
Cell *
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
	GList *col;
	GList *row;
	int    last_col_gen = -1, last_row_gen = -1;
	Value *cont;

	g_return_val_if_fail (sheet != NULL, NULL);
	g_return_val_if_fail (IS_SHEET (sheet), NULL);
	g_return_val_if_fail (callback != NULL, NULL);

	if (start_col > end_col)
		SWAP_INT (start_col, end_col);

	if (start_row > end_row)
		SWAP_INT (start_row, end_row);

	col = sheet->cols_info;
	for (; col; col = col->next){
		ColRowInfo *ci = col->data;

		if (ci->pos < start_col)
			continue;
		if (ci->pos > end_col)
			break;

		if (!only_existing){
			if ((last_col_gen > 0) && (ci->pos != last_col_gen+1))
				if (!gen_col_blanks (sheet, last_col_gen, ci->pos,
						     start_row, end_row, callback,
						     closure))
				    return value_terminate();

			if (ci->pos > start_col)
				if (!gen_col_blanks (sheet, start_col, ci->pos,
						     start_row, end_row, callback,
						     closure))
					return value_terminate();
		}
		last_col_gen = ci->pos;

		last_row_gen = -1;
		for (row = (GList *) ci->data; row; row = row->next){
			Cell *cell = (Cell *) row->data;
			int  row_pos = cell->row->pos;

			if (row_pos < start_row)
				continue;

			if (row_pos > end_row)
				break;

			if (!only_existing){
				if (last_row_gen > 0){
					if (row_pos != last_row_gen+1)
						if (!gen_row_blanks (sheet, ci->pos,
								     last_row_gen,
								     row_pos,
								     callback,
								     closure))
							return value_terminate();
				}
				if (row_pos > start_row){
					if (!gen_row_blanks (sheet, ci->pos,
							     row_pos, start_row,
							     callback, closure))
						return value_terminate();
				}
			}
			cont = (*callback)(sheet, ci->pos, row_pos, cell, closure);
			if (cont != NULL)
				return cont;
		}
	}
	return NULL;
}

static Value *
fail_if_not_selected (Sheet *sheet, int col, int row, Cell *cell, void *user_data)
{
	if (!sheet_selection_is_cell_selected (sheet, col, row))
		return value_terminate();
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

static gint
CRowSort (gconstpointer a, gconstpointer b)
{
	const Cell *ca = (const Cell *) a;
	const Cell *cb = (const Cell *) b;

	return ca->row->pos - cb->row->pos;
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
	GList *tmp;

	cell->sheet = sheet;
	cell->col   = sheet_col_get (sheet, col);
	cell->row   = sheet_row_get (sheet, row);

	cell_realize (cell);

/*	if (!cell->style){
		int flags;

		cell->style = sheet_style_compute (sheet, col, row, &flags);*/

		g_warning ("FIXME: sort out cell FORMAT_SET flag");
/*		if (flags & STYLE_FORMAT)
		cell->flags |= CELL_FORMAT_SET;
		}*/
	cell_calc_dimensions (cell);

	sheet_cell_add_to_hash (sheet, cell);
	if ((tmp = g_list_last (cell->col->data)) &&
	    CRowSort (tmp->data, cell) < 0)
		(void)g_list_append (tmp, cell);
	else
		cell->col->data = g_list_insert_sorted (cell->col->data, cell,
							&CRowSort);
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

	deps = cell_get_dependencies (sheet, cell->col->pos, cell->row->pos);
	if (deps)
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
	cell->col->data = g_list_remove (cell->col->data, cell);

	sheet_redraw_cell_region (sheet,
				  cell->col->pos, cell->row->pos,
				  cell->col->pos, cell->row->pos);
}

/**
 * sheet_cell_remove_to_eot:
 *
 * Removes all of the cells from CELL_LIST point on.
 */
static void
sheet_cell_remove_to_eot (Sheet *sheet, GList *cell_list)
{
	while (cell_list){
		Cell *cell = cell_list->data;

		if (cell->parsed_node)
			sheet_cell_formula_unlink (cell);

		sheet_cell_remove_from_hash (sheet, cell);
		cell_destroy (cell);
	}
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

/**
 * sheet_col_destroy:
 *
 * Destroys a ColRowInfo from the Sheet with all of its cells
 */
static void
sheet_col_destroy (Sheet *sheet, ColRowInfo *ci)
{
	GList *l;
	g_return_if_fail (ci);

	for (l = ci->data; l; l = l->next){
		Cell *cell = l->data;
		l->data = NULL;
		sheet_cell_remove_internal (sheet, cell);
		cell_destroy (cell);
	}

	sheet->cols_info = g_list_remove (sheet->cols_info, ci);
	g_list_free (ci->data);
	ci->data = NULL;
	g_free (ci);
}

/*
 * Destroys a row ColRowInfo
 */
static void
sheet_row_destroy (Sheet *sheet, ColRowInfo *ri)
{
	sheet->rows_info = g_list_remove (sheet->rows_info, ri);
	row_destroy_span (ri);

	g_free (ri);
}

static void
sheet_destroy_styles (Sheet *sheet)
{
	GList *l;

	for (l = sheet->style_list; l; l = l->next) {
		StyleRegion *sr = l->data;

		mstyle_destroy (sr->style);
		g_free (sr);
	}
	g_list_free (sheet->style_list);
	sheet->style_list = NULL;
}

static void
sheet_destroy_columns_and_rows (Sheet *sheet)
{
	while (sheet->cols_info)
		sheet_col_destroy (sheet, sheet->cols_info->data);
	sheet->cols_info = NULL;

	while (sheet->rows_info)
		sheet_row_destroy (sheet, sheet->rows_info->data);
	sheet->rows_info = NULL;
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

	if (sheet->dependency_hash != NULL) {
		if (g_hash_table_size (sheet->dependency_hash) != 0)
			g_warning ("Dangling dependencies");
		g_hash_table_destroy (sheet->dependency_hash);
		sheet->dependency_hash = NULL;
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

	sheet_destroy_columns_and_rows (sheet);
	sheet_destroy_styles (sheet);

	g_hash_table_foreach (sheet->cell_hash, cell_hash_free_key, NULL);
	g_hash_table_destroy (sheet->cell_hash);

	expr_name_clean_sheet (sheet);

	if (sheet->dependency_hash)
		g_hash_table_destroy (sheet->dependency_hash);

	sheet->signature = 0;
	g_free (sheet);
}


struct sheet_clear_region_callback_data
{
	int start_col, start_row, end_col, end_row;
	GList *l;
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

	cb->l = g_list_prepend (cb->l, cell);

	/* TODO TODO TODO : Where to deal with a user creating
	 * several adjacent regions to clear ??
	 */

	/* Flag an attempt to delete a subset of an array */
	if (cell->parsed_node && cell->parsed_node->oper == OPER_ARRAY){
		ArrayRef * ref = &cell->parsed_node->u.array;
		if ((col - ref->x) < cb->start_col)
			return value_terminate();
		if ((row - ref->y) < cb->start_row)
			return value_terminate();
		if ((col - ref->x + ref->cols -1) > cb->end_col)
			return value_terminate();
		if ((row - ref->y + ref->rows -1) > cb->end_row)
			return value_terminate();
	}
	return NULL;
}

/**
 * sheet_clear_region:
 *
 * Clears are region of cells
 *
 * We assemble a list of cells to destroy, since we will be making changes
 * to the structure being manipulated by the sheet_cell_foreach_range routine
 */
void
sheet_clear_region (Sheet *sheet, int start_col, int start_row, int end_col, int end_row,
		    void *closure)
{
	struct sheet_clear_region_callback_data cb;
	GList *l;

	g_return_if_fail (sheet != NULL);
	g_return_if_fail (IS_SHEET (sheet));
	g_return_if_fail (start_col <= end_col);
	g_return_if_fail (start_row <= end_row);

	/* Queue a redraw for the cells being removed */
	sheet_redraw_cell_region (sheet, start_col, start_row, end_col, end_row);

	cb.start_col = start_col;
	cb.start_row = start_row;
	cb.end_col = end_col;
	cb.end_row = end_row;
	cb.l = NULL;
	if (sheet_cell_foreach_range ( sheet, TRUE,
				       start_col, start_row, end_col, end_row,
				       assemble_clear_cell_list, &cb) == NULL){
		for (l = cb.l; l; l = l->next){
			Cell *cell = l->data;

			sheet_cell_remove (sheet, cell);
			cell_destroy (cell);
		}
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

static void
sheet_move_column (Sheet *sheet, ColRowInfo *ci, int new_column)
{
	GList *rows, *column_cells, *l;

	/* remove the cells */
	column_cells = NULL;
	for (rows = ci->data; rows; rows = rows->next){
		Cell *cell = rows->data;

		sheet_cell_remove_from_hash (sheet, cell);
		column_cells = g_list_prepend (column_cells, cell);
	}

	/* Update the column position */
	ci->pos = new_column;

	/* Insert the cells back */
	for (l = column_cells; l; l = l->next){
		Cell *cell = l->data;

		sheet_cell_add_to_hash (sheet, cell);

		cell_relocate (cell, 0, 0);
	}
	g_list_free (column_cells);
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
	return value_terminate();
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
	GList   *cur_col, *deps;
	int   col_count;

	g_return_if_fail (sheet != NULL);
	g_return_if_fail (IS_SHEET (sheet));
	g_return_if_fail (count != 0);

	col_count = g_list_length (sheet->cols_info);
	if (col_count == 0)
		return;

	/* 0. Walk cells in displaced col and ensure arrays aren't divided. */
	if (col > 0)	/* No need to test leftmost column */
		if (sheet_cell_foreach_range (sheet, TRUE, col, 0,
					      col, SHEET_MAX_ROWS,
					      &avoid_dividing_array_horizontal,
					      NULL) != NULL){
			gnumeric_no_modify_array_notice (sheet->workbook);
			return;
		}

	/* FIXME: we should probably invalidate the rightmost `count' columns here.  */

	/* Fixup all references to point at cells' new location.  */
	workbook_fixup_references (sheet->workbook, sheet, col, 0, count, 0);

	/* 1. Start scaning from the last column toward the goal column
	 *    moving all of the cells to their new location
	 */
	cur_col = g_list_nth (sheet->cols_info, col_count - 1);

	do {
		ColRowInfo *ci;
		int new_column;

		ci = cur_col->data;
		if (ci->pos < col)
			break;

		/* 1.1 Move every cell on this column count positions */
		new_column = ci->pos + count;

		if (new_column > SHEET_MAX_COLS-1){
			sheet_col_destroy (sheet, ci);

			/* Skip to next */
			cur_col = cur_col->prev;
			continue;
		}

		sheet_move_column (sheet, ci, new_column);

		/* 1.4 Go to the next column */
		cur_col = cur_col->prev;
	} while (cur_col);

	/* 2. Recompute dependencies */
	deps = region_get_dependencies (sheet, col, 0, SHEET_MAX_COLS-1, SHEET_MAX_ROWS-1);
	cell_queue_recalc_list (deps, TRUE);
	workbook_recalc (sheet->workbook);

	/* 3. Redraw */
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
	GList *cols, *deps, *destroy_list, *l;

	g_return_if_fail (sheet != NULL);
	g_return_if_fail (IS_SHEET (sheet));
	g_return_if_fail (count != 0);

	/* Is there any work to do? */
	if (g_list_length (sheet->cols_info) == 0)
		return;

	if (!range_check_for_partial_array (sheet, 0, col, 
					    SHEET_MAX_ROWS-1, col+count-1))
	{
		gnumeric_no_modify_array_notice (sheet->workbook);
		return;
	}
	/* Invalidate all references to cells being deleted.  */
	workbook_invalidate_references (sheet->workbook, sheet, col, 0, count, 0);

	/* Fixup all references to point at cells' new location.  */
	workbook_fixup_references (sheet->workbook, sheet, col, 0, -count, 0);

	/* Assemble the list of columns to destroy */
	destroy_list = NULL;
	for (cols = sheet->cols_info; cols; cols = cols->next){
		ColRowInfo *ci = cols->data;

		if (ci->pos < col)
			continue;

		if (ci->pos > col+count-1)
			break;

		destroy_list = g_list_prepend (destroy_list, ci);
	}

	for (l = destroy_list; l; l = l->next) {
		ColRowInfo *ci = l->data;

		sheet_col_destroy (sheet, ci);
	}
	g_list_free (destroy_list);

	for (cols = sheet->cols_info; cols; cols = cols->next){
		ColRowInfo *ci = cols->data;

		if (ci->pos < col)
			continue;

		g_assert (ci->pos > col+count-1);
		sheet_move_column (sheet, ci, ci->pos-count);
	}

	/* Recompute dependencies */
	deps = region_get_dependencies (sheet, col, 0, SHEET_MAX_COLS-1, SHEET_MAX_ROWS-1);
	cell_queue_recalc_list (deps, TRUE);
	workbook_recalc (sheet->workbook);

	sheet_redraw_all (sheet);
}

/**
 * colrow_closest_above:
 *
 * Returns the closest column (from above) to pos
 */
static GList *
colrow_closest_above (GList *l, int pos)
{
	for (; l; l = l->next){
		ColRowInfo *info = l->data;

		if (info->pos >= pos)
			return l;
	}
	return NULL;
}

/**
 * sheet_shift_row:
 * @sheet the sheet
 * @row   row where the shifting takes place
 * @col   first column
 * @count numbers of columns to shift.  negative numbers will
 *        delete count columns, positive number will insert
 *        count columns.
 */
void
sheet_shift_row (Sheet *sheet, int col, int row, int count)
{
	GList *cur_col, *deps, *l, *l2, *cell_list;
	int   col_count, new_column;

	g_return_if_fail (sheet != NULL);
	g_return_if_fail (IS_SHEET (sheet));
	g_return_if_fail (count != 0);

	col_count = g_list_length (sheet->cols_info);

	if (count < 0){
		sheet_clear_region (sheet, col, row, col - count - 1, row, NULL);
		cur_col = colrow_closest_above (sheet->cols_info, col);
	} else
		cur_col = g_list_nth (sheet->cols_info, col_count - 1);


	/* If nothing interesting found, return */
	if (cur_col == NULL)
		return;

	cell_list = NULL;
	do {
		ColRowInfo *ci;

		ci = cur_col->data;
		if (count > 0){
			if (ci->pos < col)
				break;
		} else {
			if (ci->pos < col){
				cur_col = cur_col->next;
				continue;
			}
		}

		new_column = ci->pos + count;

		/* Search for this row */
		for (l = ci->data; l; l = l->next){
			Cell *cell = l->data;

			if (cell->row->pos > row)
				break;

			if (cell->row->pos < row)
				continue;

			cell_list = g_list_prepend (cell_list, cell);
		}

		/* Advance to the next column */
		if (count > 0)
			cur_col = cur_col->prev;
		else
			cur_col = cur_col->next;
	} while (cur_col);


	/* Now relocate the cells */
	l = l2 = g_list_nth (cell_list, g_list_length (cell_list)-1);
	for (; l; l = l->prev){
		Cell *cell = l->data;

		new_column = cell->col->pos + count;

		/* If it overflows, remove it */
		if (new_column > SHEET_MAX_COLS-1){
			sheet_cell_remove (sheet, cell);
			cell_destroy (cell);
			break;
		}

		/* Relocate the cell */
		sheet_cell_remove (sheet, cell);
		sheet_cell_add (sheet, cell, new_column, row);
		cell_relocate (cell, 0, 0);
	}
	g_list_free (l2);

	/* Check the dependencies and recompute them */
	deps = region_get_dependencies (sheet, col, row, SHEET_MAX_COLS-1, row);
	cell_queue_recalc_list (deps, TRUE);
	workbook_recalc (sheet->workbook);

	sheet_redraw_all (sheet);
}

void
sheet_shift_rows (Sheet *sheet, int col, int start_row, int end_row, int count)
{
	int i;

	g_return_if_fail (sheet != NULL);
	g_return_if_fail (IS_SHEET (sheet));
	g_return_if_fail (count != 0);
	g_return_if_fail (start_row <= end_row);

	for (i = start_row; i <= end_row; i++)
		sheet_shift_row (sheet, col, i, count);
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
	return value_terminate();
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
	GList *cell_store, *cols, *l, *rows, *deps, *destroy_list;

	g_return_if_fail (sheet != NULL);
	g_return_if_fail (IS_SHEET (sheet));
	g_return_if_fail (count != 0);

	cell_store = NULL;

	/* 0. Walk cells in displaced row and ensure arrays aren't divided. */
	if (row > 0)	/* No need to test top row */
		if (sheet_cell_foreach_range (sheet, TRUE, 0, row,
					      SHEET_MAX_COLS, row,
					      &avoid_dividing_array_vertical,
					      NULL) != NULL){
			gnumeric_no_modify_array_notice (sheet->workbook);
			return;
		}

	/* FIXME: we should probably invalidate the bottom `count' rows here.  */

	/* Fixup all references to point at cells' new location.  */
	workbook_fixup_references (sheet->workbook, sheet, 0, row, 0, count);

	/* 1. Walk every column, see which cells are out of range */
	for (cols = sheet->cols_info; cols; cols = cols->next){
		ColRowInfo *ci = cols->data;
		GList *cells;

		for (cells = ci->data; cells; cells = cells->next){
			Cell *cell = cells->data;

			if (cell->row->pos < row)
				continue;

			/* If the new position is out of range, destroy the cell */
			if (cell->row->pos + count > SHEET_MAX_ROWS-1){
				sheet_cell_remove_to_eot (sheet, cells);

				/* Remove any trace of the tail that just got deleted */
				if (cells->prev)
					cells->prev->next = NULL;

				g_list_free (cells);
				break;
			}

			/* At this point we now we can move the cell safely */
			sheet_cell_remove_from_hash (sheet, cell);

			/* Keep track of it */
			cell_store = g_list_prepend (cell_store, cell);
		}
	}

	/* 2. Relocate the row information pointers, destroy overflowed rows */
	destroy_list = NULL;
	for (rows = sheet->rows_info; rows; rows = rows->next){
		ColRowInfo *ri = rows->data;

		if (ri->pos < row)
			continue;

		if (ri->pos + count > SHEET_MAX_ROWS-1){
			destroy_list = g_list_prepend (destroy_list, ri);
			continue;
		}

		ri->pos += count;
	}

	/* Destroy those row infos that are gone */
	for (l = destroy_list; l; l = l->next){
		ColRowInfo *ri = l->data;

		sheet_row_destroy (sheet, ri);
	}
	g_list_free (destroy_list);

	/* 3. Put back the moved cells in their new spot */
	for (l = cell_store; l; l = l->next){
		Cell *cell = l->data;

		sheet_cell_add_to_hash (sheet, cell);

		cell_relocate (cell, 0, 0);
	}

	g_list_free (cell_store);

	/* 4. Recompute any changes required */
	deps = region_get_dependencies (sheet, 0, row, SHEET_MAX_COLS-1, SHEET_MAX_ROWS-1);
	cell_queue_recalc_list (deps, TRUE);
	workbook_recalc (sheet->workbook);

	/* 5. Redraw everything */
	sheet_redraw_all (sheet);
}

/**
 * sheet_delete_row:
 * @sheet   The sheet
 * @row     At which position we want to delete
 * @count   The number of rows to be deleted
 */
void
sheet_delete_row (Sheet *sheet, int row, int count)
{
	GList *destroy_list, *cols, *rows, *cell_store, *deps, *l;

	g_return_if_fail (sheet != NULL);
	g_return_if_fail (IS_SHEET (sheet));
	g_return_if_fail (count != 0);

	if (!range_check_for_partial_array (sheet, row, 0, 
					    row+count-1, SHEET_MAX_COLS-1))
	{
		gnumeric_no_modify_array_notice (sheet->workbook);
		return;
	}

	/* Invalidate all references to cells being deleted.  */
	workbook_invalidate_references (sheet->workbook, sheet, 0, row, 0, count);

	/* Fixup all references to point at cells' new location.  */
	workbook_fixup_references (sheet->workbook, sheet, 0, row, 0, -count);

	/* 1. Remove cells from hash tables and grab all dangling rows */
	cell_store = NULL;
	for (cols = sheet->cols_info; cols; cols = cols->next){
		ColRowInfo *ci = cols->data;
		GList *cells;

		destroy_list = NULL;
		for (cells = ci->data; cells; cells = cells->next){
			Cell *cell = cells->data;

			if (cell->row->pos < row)
				continue;

			if (cell->parsed_node)
				sheet_cell_formula_unlink (cell);

			sheet_cell_remove_from_hash (sheet, cell);

			if (cell->row->pos >= row && cell->row->pos <= row+count-1){
				destroy_list = g_list_prepend (destroy_list, cell);
				continue;
			}

			cell_store = g_list_prepend (cell_store, cell);
		}

		/* Destroy the cells in the range */
		for (l = destroy_list; l; l = l->next){
			Cell *cell = l->data;

			cell->col->data = g_list_remove (cell->col->data, cell);
			cell_destroy (cell);
		}

		g_list_free (destroy_list);
	}

	/* 2. Relocate row information pointers, destroy unused rows */
	destroy_list = NULL;
	for (rows = sheet->rows_info; rows; rows = rows->next){
		ColRowInfo *ri = rows->data;

		if (ri->pos < row)
			continue;

		if (ri->pos >= row && ri->pos <= row+count-1){
			destroy_list = g_list_prepend (destroy_list, ri);
			continue;
		}
		ri->pos -= count;
	}
	for (l = destroy_list; l; l = l->next){
		ColRowInfo *ri = l->data;

		sheet_row_destroy (sheet, ri);
	}
	g_list_free (destroy_list);

	/* 3. Put back the cells at their new location */
	for (l = cell_store; l; l = l->next){
		Cell *cell = l->data;

		sheet_cell_add_to_hash (sheet, cell);

		cell_relocate (cell, 0, 0);
	}
	g_list_free (cell_store);

	/* 4. Recompute dependencies */
	deps = region_get_dependencies (sheet, 0, row, SHEET_MAX_COLS-1, SHEET_MAX_ROWS-1);
	cell_queue_recalc_list (deps, TRUE);
	workbook_recalc (sheet->workbook);

	/* 5. Redraw everything */
	sheet_redraw_all (sheet);
}

/**
 * sheet_shift_col:
 * @sheet the sheet
 * @row   first row
 * @col   column where the shifting takes place
 * @count numbers of rows to shift.  a negative numbers will
 *        delete count rows, positive number will insert
 *        count rows.
 */
void
sheet_shift_col (Sheet *sheet, int col, int row, int count)
{
	GList *row_list, *cur_row, *deps, *cell_list, *l;
	ColRowInfo *ci;
	int row_count;

	g_return_if_fail (sheet != NULL);
	g_return_if_fail (IS_SHEET (sheet));
	g_return_if_fail (count != 0);

	ci = sheet_col_get_info (sheet, col);

	/* Check if the column did exist, if not, then shift_col is a no-op */
	if (ci->pos != col)
		return;

	if (count < 0){
		sheet_clear_region (sheet, col, row, col, row -count - 1, NULL);
		ci = sheet_col_get_info (sheet, col);
		row_list = ci->data;
		cur_row = colrow_closest_above (row_list, row);
	} else {
		row_list = ci->data;
		row_count = g_list_length (row_list);
		cur_row = g_list_nth (row_list, row_count-1);
	}

	/* If nothing interesting found, return */
	if (cur_row == NULL)
		return;

	cell_list = NULL;
	do {
		Cell *cell = cur_row->data;
		int new_row;

		if (count > 0){
			if (cell->row->pos < row)
				break;
		} else {
			if (cell->row->pos < row){
				cur_row = cur_row->next;
				continue;
			}
		}

		new_row = cell->row->pos + count;

		cell_list = g_list_prepend (cell_list, cell);

		/* Advance to next row */
		if (count > 0)
			cur_row = cur_row->prev;
		else
			cur_row = cur_row->next;
	} while (cur_row);

	/* Relocate the cells */
	l = g_list_nth (cell_list, g_list_length (cell_list)-1);

	for (; l; l = l->prev){
		Cell *cell = l->data;
		int old_pos = cell->row->pos;
		int new_row = old_pos + count;

		sheet_cell_remove (sheet, cell);

		/* if it overflows */
		if (new_row > SHEET_MAX_ROWS-1){
			cell_destroy (cell);
			continue;
		}

		sheet_cell_add (sheet, cell, col, new_row);
		cell_relocate (cell, 0, 0);
	}
	g_list_free (cell_list);

	/* Recompute dependencies on the changed data */
	deps = region_get_dependencies (sheet, col, row, col, SHEET_MAX_ROWS-1);
	cell_queue_recalc_list (deps, TRUE);
	workbook_recalc (sheet->workbook);

	sheet_redraw_all (sheet);
}

/**
 * sheet_shift_cols:
 * @sheet the sheet
 * @start_col first column
 * @end_col   end column
 * @row       first row where the shifting takes place.
 * @count     numbers of rows to shift.  a negative numbers will
 *            delete count rows, positive number will insert
 *            count rows.
 */
void
sheet_shift_cols (Sheet *sheet, int start_col, int end_col, int row, int count)
{
	int i;

	g_return_if_fail (sheet != NULL);
	g_return_if_fail (IS_SHEET (sheet));
	g_return_if_fail (count != 0);

	for (i = start_col; i <= end_col; i++)
		sheet_shift_col (sheet, i, row, count);
}

void
sheet_style_attach (Sheet *sheet, Range range,
		    MStyle *style)
{
	StyleRegion *sr;

	g_return_if_fail (sheet != NULL);
	g_return_if_fail (IS_SHEET (sheet));
	g_return_if_fail (style != NULL);
	g_return_if_fail (range.start.col <= range.end.col);
	g_return_if_fail (range.start.row <= range.end.row);

	/* FIXME: Some serious work needs to be done here */
	sr = g_new (StyleRegion, 1);
	sr->range = range;
	sr->style = style;

	sheet->style_list = g_list_prepend (sheet->style_list, sr);
}

void
sheet_style_attach_old (Sheet *sheet,
			int    start_col, int start_row,
			int    end_col,   int end_row,
			MStyle  *style)
{
	StyleRegion *sr;

	g_warning ("Deprecated");
	g_return_if_fail (sheet != NULL);
	g_return_if_fail (IS_SHEET (sheet));
	g_return_if_fail (style != NULL);
	g_return_if_fail (start_col <= end_col);
	g_return_if_fail (start_row <= end_row);

	sr = g_new (StyleRegion, 1);
	sr->range.start.col = start_col;
	sr->range.start.row = start_row;
	sr->range.end.col = end_col;
	sr->range.end.row = end_row;
	sr->style = style;

	sheet->style_list = g_list_prepend (sheet->style_list, sr);
}

/**
 * sheet_style_compute:
 * @sheet:	 Which sheet we are looking up
 * @col:	 column
 * @row:	 row
 * @non_default: A pointer where we store the attributes
 *               the cell has which are not part of the
 *               default style.
 */
Style *
sheet_style_compute (Sheet const *sheet, int col, int row)
{
	GList *l;
	Style *style;
	GList *style_list;
	g_return_val_if_fail (sheet != NULL, NULL);
	g_return_val_if_fail (IS_SHEET (sheet), NULL);

	style_list = NULL;
	/* Look in the styles applied to the sheet */
	for (l = sheet->style_list; l; l = l->next) {
		StyleRegion *sr = l->data;
		if (range_contains (&sr->range, col, row)) {
/*			range_dump (&sr->range);
			mstyle_dump (sr->style);*/
			style_list = g_list_prepend (style_list,
						     sr->style);
		}
	}
	style_list = g_list_reverse (style_list);

	style = render_merge (style_list);
	g_list_free (style_list);

	return style;
}

Style *
sheet_style_compute_blank (Sheet const *sheet, int col, int row)
{
	GList *l;
	Style *style;
	GList *style_list;
	g_return_val_if_fail (sheet != NULL, NULL);
	g_return_val_if_fail (IS_SHEET (sheet), NULL);

	style_list = NULL;
	/* Look in the styles applied to the sheet */
	for (l = sheet->style_list; l; l = l->next) {
		StyleRegion *sr = l->data;
		if (range_contains (&sr->range, col, row)) {
			range_dump (&sr->range);
			mstyle_dump (sr->style);
			style_list = g_list_prepend (style_list,
						     sr->style);
		}
	}
	style_list = g_list_reverse (style_list);

	style = render_merge_blank (style_list);
	g_list_free (style_list);

	return style;
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
	int  col, row;
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

	for (; l; l = l->next){
		SheetSelection *ss = l->data;

		for (col = ss->user.start.col; col <= ss->user.end.col; col++)
			for (row = ss->user.start.row; row <=
			     ss->user.end.row; row++)
				sheet_set_text (sheet, col, row, str);
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

/* Can remove sheet since local references have NULL sheet */
char *
cellref_name (CellRef *cell_ref, int eval_col, int eval_row)
{
	static char buffer [sizeof (long) * 4 + 4];
	char *p = buffer;
	int col, row;

	if (cell_ref->col_relative)
		col = eval_col + cell_ref->col;
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
		row = eval_row + cell_ref->row;
	else {
		*p++ = '$';
		row = cell_ref->row;
	}

	sprintf (p, "%d", row+1);

	/* If it is a non-local reference, add the path to the external sheet */
	if (cell_ref->sheet == NULL)
		return g_strdup (buffer);
	else {
		Sheet *sheet = cell_ref->sheet;
		char *s;

		if (strchr (sheet->name, ' '))
			s = g_strconcat ("\"", sheet->name, "\"!", buffer, NULL);
		else
			s = g_strconcat (sheet->name, "!", buffer, NULL);

		return s;
	}
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
