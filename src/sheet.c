/*
 * Sheet.c:  Implements the sheet management and per-sheet storage
 * (C) 1998 The Free Software Foundation
 *
 * Author:
 *  Miguel de Icaza (miguel@gnu.org)
 *
 */
#include <config.h>
#include <gnome.h>
#include "gnumeric.h"
#include "gnumeric-sheet.h"
#include "utils.h"
#include "gnumeric-util.h"

static void sheet_selection_col_extend_to (Sheet *sheet, int col);
static void sheet_selection_row_extend_to (Sheet *sheet, int row);

void
sheet_redraw_all (Sheet *sheet)
{
	gnome_canvas_request_redraw (
		GNOME_CANVAS (sheet->sheet_view),
		0, 0, INT_MAX, INT_MAX);
}

static void
sheet_init_default_styles (Sheet *sheet)
{
	/* The default column style */
	sheet->default_col_style.pos        = -1;
	sheet->default_col_style.style      = style_new ();
	sheet->default_col_style.units      = 80;
	sheet->default_col_style.pixels     = 0;
	sheet->default_col_style.margin_a   = 1;
	sheet->default_col_style.margin_b   = 1;
	sheet->default_col_style.selected   = 0;
	sheet->default_col_style.data       = NULL;

	/* The default row style */
	sheet->default_row_style.pos      = -1;
	sheet->default_row_style.style    = style_new ();
	sheet->default_row_style.units    = 20;
	sheet->default_row_style.pixels   = 0;
	sheet->default_row_style.margin_a = 1;
	sheet->default_row_style.margin_b = 1;
	sheet->default_row_style.selected = 0;
	sheet->default_row_style.data     = NULL;
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
		rp->selected = 0;
		sheet_row_add (sheet, rp);
	}
}

static void
canvas_bar_realized (GtkWidget *widget, gpointer data)
{
	/* MIGUEL: look at this */
	gdk_window_set_back_pixmap (GTK_LAYOUT (widget)->bin_window, NULL, FALSE);
}

static GtkWidget *
new_canvas_bar (Sheet *sheet, GtkOrientation o, GnomeCanvasItem **itemp)
{
	GnomeCanvasGroup *group;
	GnomeCanvasItem *item;
	GtkWidget *canvas;
	int w, h;
	
	canvas = gnome_canvas_new ();

	gtk_signal_connect (GTK_OBJECT (canvas), "realize",
			    (GtkSignalFunc) canvas_bar_realized,
			    NULL);

	/* FIXME: need to set the correct scrolling regions.  This will do for now */

	if (o == GTK_ORIENTATION_VERTICAL){
		w = 60;
		h = 1;
		gnome_canvas_set_scroll_region (GNOME_CANVAS (canvas), 0, 0, w, 1000000);
	} else {
		w = 1;
		h = 20;
		gnome_canvas_set_scroll_region (GNOME_CANVAS (canvas), 0, 0, 1000000, h);
	}
	gnome_canvas_set_size (GNOME_CANVAS (canvas), w, h);
	group = GNOME_CANVAS_GROUP (GNOME_CANVAS (canvas)->root);
	item = gnome_canvas_item_new (group,
				      item_bar_get_type (),
				      "ItemBar::Sheet", sheet,
				      "ItemBar::Orientation", o,
				      "ItemBar::First", 0,
				      NULL);

	*itemp = GNOME_CANVAS_ITEM (item);
	gtk_widget_show (canvas);
	return canvas;
}

static void
sheet_col_selection_changed (ItemBar *item_bar, int column, int reset, Sheet *sheet)
{
	ColRowInfo *ci;

	ci = sheet_col_get (sheet, column);
	
	if (reset){
		gnumeric_sheet_cursor_set (GNUMERIC_SHEET (sheet->sheet_view), column, 0);
		sheet_selection_clear (sheet);
		sheet_selection_append_range (sheet,
					      column, 0,
					      column, 0,
					      column, SHEET_MAX_ROWS-1);
		sheet_col_set_selection (sheet, ci, 1);
	} else
		sheet_selection_col_extend_to (sheet, column);
}

static void
sheet_col_size_changed (ItemBar *item_bar, int col, int width, Sheet *sheet)
{
	sheet_col_set_width (sheet, col, width);
	gnumeric_sheet_compute_visible_ranges (GNUMERIC_SHEET (sheet->sheet_view));
}

static void
sheet_row_selection_changed (ItemBar *item_bar, int row, int reset, Sheet *sheet)
{
	ColRowInfo *ri;

	ri = sheet_row_get (sheet, row);

	if (reset){
		gnumeric_sheet_cursor_set (GNUMERIC_SHEET (sheet->sheet_view), 0, row);
		sheet_selection_clear (sheet);
		sheet_selection_append_range (sheet,
					      0, row,
					      0, row,
					      SHEET_MAX_COLS-1, row);
		sheet_row_set_selection (sheet, ri, 1);
	} else
		sheet_selection_row_extend_to (sheet, row);
}

static void
sheet_row_size_changed (ItemBar *item_bar, int row, int height, Sheet *sheet)
{
	sheet_row_set_height (sheet, row, height);
	gnumeric_sheet_compute_visible_ranges (GNUMERIC_SHEET (sheet->sheet_view));
}

static guint
cell_hash (gconstpointer key)
{
	CellPos *ca = (CellPos *) key;

	return (ca->row << 8) | ca->col;
}

static gint
cell_compare (gconstpointer a, gconstpointer b)
{
	CellPos *ca, *cb;

	ca = (CellPos *) a;
	cb = (CellPos *) b;

	if (ca->row != cb->row)
		return 0;
	if (ca->col != cb->col)
		return 0;
	
	return 1;
}

static void
button_select_all (GtkWidget *the_button, Sheet *sheet)
{
	sheet_select_all (sheet);
}

Sheet *
sheet_new (Workbook *wb, char *name)
{
	int rows_shown, cols_shown;
	GtkWidget *select_all;
	Sheet *sheet;

	rows_shown = cols_shown = 40;
	
	sheet = g_new0 (Sheet, 1);
	sheet->signature = SHEET_SIGNATURE;
	sheet->workbook = wb;
	sheet->name = g_strdup (name);
	sheet->last_zoom_factor_used = -1.0;
	sheet->toplevel = gtk_table_new (0, 0, 0);
	sheet->max_col_used = cols_shown;
	sheet->max_row_used = rows_shown;

	sheet->cell_hash = g_hash_table_new (cell_hash, cell_compare);
	sheet_init_default_styles (sheet);
	
	/* Dummy initialization */
	if (0)
		sheet_init_dummy_stuff (sheet);

	/* Column canvas */
	sheet->col_canvas = new_canvas_bar (sheet, GTK_ORIENTATION_HORIZONTAL, &sheet->col_item);
	gtk_table_attach (GTK_TABLE (sheet->toplevel), sheet->col_canvas,
			  1, 2, 0, 1,
			  GTK_EXPAND | GTK_FILL | GTK_SHRINK,
			  GTK_FILL,
			  0, 0);
	gtk_signal_connect (GTK_OBJECT (sheet->col_item), "selection_changed",
			    GTK_SIGNAL_FUNC (sheet_col_selection_changed),
			    sheet);
	gtk_signal_connect (GTK_OBJECT (sheet->col_item), "size_changed",
			    GTK_SIGNAL_FUNC (sheet_col_size_changed),
			    sheet);
	/* Row canvas */
	sheet->row_canvas = new_canvas_bar (sheet, GTK_ORIENTATION_VERTICAL, &sheet->row_item);
	gtk_table_attach (GTK_TABLE (sheet->toplevel), sheet->row_canvas,
			  0, 1, 1, 2,
			  GTK_FILL,
			  GTK_EXPAND | GTK_FILL | GTK_SHRINK,
			  0, 0);
	gtk_signal_connect (GTK_OBJECT (sheet->row_item), "selection_changed",
			    GTK_SIGNAL_FUNC (sheet_row_selection_changed),
			    sheet);
	gtk_signal_connect (GTK_OBJECT (sheet->row_item), "size_changed",
			    GTK_SIGNAL_FUNC (sheet_row_size_changed),
			    sheet);

	/* Create the gnumeric sheet and set the initial selection */
	sheet->sheet_view = gnumeric_sheet_new (sheet,
						ITEM_BAR (sheet->col_item),
						ITEM_BAR (sheet->row_item));
	sheet_selection_append (sheet, 0, 0);
	
	gtk_widget_show (sheet->sheet_view);
	gtk_widget_show (sheet->toplevel);
	
	gtk_table_attach (GTK_TABLE (sheet->toplevel), sheet->sheet_view,
			  1, 2, 1, 2,
			  GTK_EXPAND | GTK_FILL | GTK_SHRINK,
			  GTK_EXPAND | GTK_FILL | GTK_SHRINK,
			  0, 0);

	/* The select-all button */
	select_all = gtk_button_new ();
	gtk_table_attach (GTK_TABLE (sheet->toplevel), select_all,
			  0, 1, 0, 1,
			  GTK_FILL,
			  GTK_FILL,
			  0, 0);
	gtk_signal_connect (GTK_OBJECT (select_all), "clicked",
			    GTK_SIGNAL_FUNC (button_select_all), sheet);
	
	/* Scroll bars and their adjustments */
	sheet->va = gtk_adjustment_new (0.0, 0.0, sheet->max_row_used, 1.0, rows_shown, 1.0);
	sheet->ha = gtk_adjustment_new (0.0, 0.0, sheet->max_col_used, 1.0, cols_shown, 1.0);
	
	sheet->hs = gtk_hscrollbar_new (GTK_ADJUSTMENT (sheet->ha));
	sheet->vs = gtk_vscrollbar_new (GTK_ADJUSTMENT (sheet->va));

	/* Attach the horizontal scroll */
	gtk_table_attach (GTK_TABLE (sheet->toplevel), sheet->hs,
			  1, 2, 2, 3,
			  GTK_EXPAND | GTK_FILL | GTK_SHRINK,
			  GTK_FILL,
			  0, 0);

	/* Attach the vertical scroll */
	gtk_table_attach (GTK_TABLE (sheet->toplevel), sheet->vs,
			  2, 3, 1, 2,
			  GTK_FILL,
			  GTK_EXPAND | GTK_FILL | GTK_SHRINK,
			  0, 0);
	
	sheet_set_zoom_factor (sheet, 1.0);
	return sheet;
}

static void
cell_hash_free_key (gpointer key, gpointer value, gpointer user_data)
{
	g_free (key);
}
	
void
sheet_destroy (Sheet *sheet)
{
	g_assert (sheet != NULL);
	g_return_if_fail (IS_SHEET (sheet)); 

	sheet_selection_clear (sheet);
	g_free (sheet->name);
	
	style_destroy (sheet->default_row_style.style);
	style_destroy (sheet->default_col_style.style);

	g_hash_table_foreach (sheet->cell_hash, cell_hash_free_key, NULL);
	gtk_widget_destroy (sheet->toplevel);
	
	sheet->signature = 0;
	g_free (sheet);
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

	ci->pixels = (ci->units * pix_per_unit) +
		ci->margin_a + ci->margin_b + 1;
}

/*
 * Updates the values used for display after a zoom change
 */
static void
sheet_reconfigure_zoom (Sheet *sheet)
{
	GnomeCanvas *gnumeric_canvas = GNOME_CANVAS (sheet->sheet_view);
	double pixels_per_unit = gnumeric_canvas->pixels_per_unit;
	
	if (pixels_per_unit == sheet->last_zoom_factor_used)
		return;

	sheet->last_zoom_factor_used = pixels_per_unit;
	sheet_foreach_col (sheet, sheet_compute_col_row_new_size, NULL);
	sheet_foreach_row (sheet, sheet_compute_col_row_new_size, NULL);
	g_warning ("Need to recompute string lenghts of cells\n");
}

void
sheet_set_zoom_factor (Sheet *sheet, double factor)
{
	gnome_canvas_set_pixels_per_unit (GNOME_CANVAS (sheet->sheet_view), factor);
	gnome_canvas_set_pixels_per_unit (GNOME_CANVAS (sheet->col_canvas), factor);
	gnome_canvas_set_pixels_per_unit (GNOME_CANVAS (sheet->row_canvas), factor);
	sheet_reconfigure_zoom (sheet);
	gnome_canvas_scroll_to (GNOME_CANVAS (sheet->sheet_view), 0, 0);
	gnome_canvas_scroll_to (GNOME_CANVAS (sheet->col_canvas), 0, 0);
	gnome_canvas_scroll_to (GNOME_CANVAS (sheet->row_canvas), 0, 0);
}

ColRowInfo *
sheet_duplicate_colrow (ColRowInfo *original)
{
	ColRowInfo *info = g_new (ColRowInfo, 1);

	*info = *original;
	
	info->style = style_duplicate (original->style);
	
	return info;
}

ColRowInfo *
sheet_row_new (Sheet *sheet)
{
	return sheet_duplicate_colrow (&sheet->default_row_style);
}

ColRowInfo *
sheet_col_new (Sheet *sheet)
{
	return sheet_duplicate_colrow (&sheet->default_col_style);
}


static gint
CRsort (gconstpointer a, gconstpointer b)
{
	ColRowInfo *ia = (ColRowInfo *) a;
	ColRowInfo *ib = (ColRowInfo *) b;

	return (ia->pos - ib->pos);
}

void
sheet_col_add (Sheet *sheet, ColRowInfo *cp)
{
	sheet->cols_info = g_list_insert_sorted (sheet->cols_info, cp, CRsort);
}

void
sheet_row_add (Sheet *sheet, ColRowInfo *rp)
{
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

static void
colrow_set_units (Sheet *sheet,ColRowInfo *info)
{
	double pix = sheet->last_zoom_factor_used;
	
	info->units  = (info->pixels -
			(info->margin_a + info->margin_b + 1)) / pix;
}

void
sheet_row_set_height (Sheet *sheet, int row, int height)
{
	ColRowInfo *ri;
	int add = 0;
	
	ri = sheet_row_get_info (sheet, row);
	if (ri == &sheet->default_row_style){
		ri = sheet_duplicate_colrow (ri);
		add = 1;
	}

	ri->pos = row;
	ri->pixels = height;
	colrow_set_units (sheet, ri);
	if (add)
		sheet_row_add (sheet, ri);
	sheet_redraw_all (sheet);
}

void
sheet_col_set_width (Sheet *sheet, int col, int width)
{
	ColRowInfo *ci;
	int add = 0;
	
	ci = sheet_col_get_info (sheet, col);
	if (ci == &sheet->default_col_style){
		ci = sheet_duplicate_colrow (ci);
		add = 1;
	}

	ci->pixels = width;
	ci->pos = col;
	colrow_set_units (sheet, ci);
	if (add)
		sheet_col_add (sheet, ci);

	sheet_redraw_all (sheet);
}

/*
 * Return the number of pixels between from_col to to_col
 */
int
sheet_col_get_distance (Sheet *sheet, int from_col, int to_col)
{
	ColRowInfo *ci;
	int pixels = 0, i;

	g_assert (sheet);
	g_assert (from_col <= to_col);

	/* This can be optimized, yes, but the implementation
	 * of the ColRowInfo sets is going to change anyways
	 */
	for (i = from_col; i < to_col; i++){
		ci = sheet_col_get_info (sheet, i);
		pixels += ci->pixels;
	}
	return pixels;
}

/*
 * Return the number of pixels between from_row to to_row
 */
int
sheet_row_get_distance (Sheet *sheet, int from_row, int to_row)
{
	ColRowInfo *ri;
	int pixels = 0, i;

	g_assert (from_row <= to_row);
	
	/* This can be optimized, yes, but the implementation
	 * of the RowInfo, ColInfo sets is going to change anyways
	 */
	for (i = from_row; i < to_row; i++){
		ri = sheet_row_get_info (sheet, i);
		pixels += ri->pixels;
	}
	return pixels;
}

void
sheet_get_cell_bounds (Sheet *sheet, ColType col, RowType row, int *x, int *y, int *w, int *h)
{
	GnumericSheet *gsheet;

	g_return_if_fail (sheet != NULL);
	g_return_if_fail (IS_SHEET (sheet)); 
	g_return_if_fail (GNUMERIC_IS_SHEET (sheet->sheet_view));
	
	gsheet = GNUMERIC_SHEET (sheet->sheet_view);
	*x = sheet_col_get_distance (sheet, gsheet->top_col, col);
	*y = sheet_row_get_distance (sheet, gsheet->top_row, row);

	*w = sheet_col_get_distance (sheet, col, col + 1);
	*h = sheet_row_get_distance (sheet, row, row + 1);
}

int
sheet_selection_equal (SheetSelection *a, SheetSelection *b)
{
	if (a->start_col != b->start_col)
		return 0;
	if (a->start_row != b->start_row)
		return 0;
	
	if (a->end_col != b->end_col)
		return 0;
	if (a->end_row != b->end_row)
		return 0;
	return 1;
}

void
sheet_update_auto_expr (Sheet *sheet)
{
	Workbook *wb = sheet->workbook;
	Value *v;
	char  *error;

	g_return_if_fail (sheet != NULL);
	g_return_if_fail (IS_SHEET (sheet));
		
	/* defaults */
	v = NULL;
	error = "ERROR";
	if (wb->auto_expr)
		v = eval_expr (sheet, wb->auto_expr, 0, 0, &error);
	
	if (v){
		char *s;

		s = value_string (v);
		workbook_auto_expr_label_set (wb, s);
		g_free (s);
		value_release (v);
	} else
		workbook_auto_expr_label_set (wb, error);
}

static char *
sheet_get_selection_name (Sheet *sheet)
{
	SheetSelection *ss = sheet->selections->data;
	static char buffer [40];
	
	if (ss->start_col == ss->end_col && ss->start_row == ss->end_row){
		return cell_name (ss->start_col, ss->start_row);
	} else {
		snprintf (buffer, sizeof (buffer), "%dLx%dC",
			  ss->end_row - ss->start_row + 1,
			  ss->end_col - ss->start_col + 1);
		return buffer;
	}
}

static void
sheet_selection_changed_hook (Sheet *sheet)
{
	sheet_update_auto_expr (sheet);
	workbook_set_region_status (sheet->workbook, sheet_get_selection_name (sheet));
}

void
sheet_selection_append_range (Sheet *sheet,
			      int base_col,  int base_row,
			      int start_col, int start_row,
			      int end_col,   int end_row)
{
	SheetSelection *ss;

	g_return_if_fail (sheet != NULL);
	g_return_if_fail (IS_SHEET (sheet)); 
	
	ss = g_new0 (SheetSelection, 1);

	ss->base_col  = base_col;
	ss->base_row  = base_row;

	ss->start_col = start_col;
	ss->end_col   = end_col;
	ss->start_row = start_row;
	ss->end_row   = end_row;
	
	sheet->selections = g_list_prepend (sheet->selections, ss);

	gnumeric_sheet_accept_pending_output (GNUMERIC_SHEET (sheet->sheet_view));
	gnumeric_sheet_load_cell_val (GNUMERIC_SHEET (sheet->sheet_view));
	
	gnumeric_sheet_set_selection (GNUMERIC_SHEET (sheet->sheet_view), ss);
	sheet_redraw_selection (sheet, ss);

	sheet_selection_changed_hook (sheet);
}

void
sheet_selection_append (Sheet *sheet, int col, int row)
{
	sheet_selection_append_range (sheet, col, row, col, row, col, row);
}

/*
 * sheet_selection_extend_to
 * @sheet: the sheet
 * @col:   column that gets covered
 * @row:   row that gets covered
 *
 * This extends the selection to cover col, row
 */
void
sheet_selection_extend_to (Sheet *sheet, int col, int row)
{
	SheetSelection *ss, old_selection;

	g_return_if_fail (sheet != NULL);
	g_return_if_fail (IS_SHEET (sheet));
	
	g_assert (sheet->selections);

	ss = (SheetSelection *) sheet->selections->data;

	old_selection = *ss;
	
	if (col < ss->base_col){
		ss->start_col = col;
		ss->end_col   = ss->base_col;
	} else {
		ss->start_col = ss->base_col;
		ss->end_col   = col;
	}

	if (row < ss->base_row){
		ss->end_row   = ss->base_row;
		ss->start_row = row;
	} else {
		ss->end_row   = row;
		ss->start_row = ss->base_row;
	}

	gnumeric_sheet_set_selection (GNUMERIC_SHEET (sheet->sheet_view), ss);
	sheet_selection_changed_hook (sheet);
	
	sheet_redraw_selection (sheet, &old_selection);
	sheet_redraw_selection (sheet, ss);
}

/*
 * sheet_selection_col_extend_to
 * @sheet: the sheet
 * @col:   column that gets covered
 *
 * Special version of sheet_selection_extend_to that
 * is used by the column marking to keep the
 * ColRowInfo->selected flag in sync
 */
void
sheet_selection_col_extend_to (Sheet *sheet, int col)
{
	SheetSelection *ss, old_selection;
	GList *cols;
	int max_col, min_col, state;
		
	g_return_if_fail (sheet != NULL);
	g_return_if_fail (IS_SHEET (sheet));

	g_assert (sheet->selections);

	ss = (SheetSelection *) sheet->selections->data;
	old_selection = *ss;

	sheet_selection_extend_to (sheet, col, SHEET_MAX_ROWS-1);

	min_col = MIN (old_selection.start_col, ss->start_col);
	max_col = MAX (old_selection.end_col, ss->end_col);
	
	for (cols = sheet->cols_info; cols; cols = cols->next){
		ColRowInfo *ci = cols->data;

		if (ci->pos < min_col)
			continue;
		if (ci->pos > max_col)
			break;

		if ((ci->pos < ss->start_col) || 
		    (ci->pos > ss->end_col))
			state = 0;
		else
			state = 1;
		sheet_col_set_selection (sheet, ci, state);
	}
	sheet_selection_changed_hook (sheet);
}

/*
 * sheet_selection_row_extend_to
 * @sheet: the sheet
 * @row:   row that gets covered
 *
 * Special version of sheet_selection_extend_to that
 * is used by the row marking to keep the ColRowInfo->selected
 * flag in sync. 
 */
static void
sheet_selection_row_extend_to (Sheet *sheet, int row)
{
	SheetSelection *ss, old_selection;
	GList *rows;
	int min_row, max_row, state;
	
	g_return_if_fail (sheet != NULL);
	g_return_if_fail (IS_SHEET (sheet));

	g_assert (sheet->selections);

	ss = (SheetSelection *) sheet->selections->data;
	old_selection = *ss;

	sheet_selection_extend_to (sheet, SHEET_MAX_COLS-1, row);

	min_row = MIN (old_selection.start_row, ss->start_row);
	max_row = MAX (old_selection.end_row, ss->end_row);
	
	for (rows = sheet->rows_info; rows; rows = rows->next){
		ColRowInfo *ri = rows->data;

		if (ri->pos < min_row)
			continue;
		if (ri->pos > max_row)
			break;

		if ((ri->pos < ss->start_col) ||
		    (ri->pos > ss->end_col))
			state = 0;
		else
			state = 1;

		sheet_row_set_selection (sheet, ri, state);
	}
	sheet_selection_changed_hook (sheet);
}

void
sheet_row_set_selection (Sheet *sheet, ColRowInfo *ri, int value)
{
	g_return_if_fail (sheet != NULL);
	g_return_if_fail (IS_SHEET (sheet));
	g_return_if_fail (ri != NULL);
	
	if (ri->selected == value)
		return;
	
	ri->selected = value;
	gnome_canvas_request_redraw (GNOME_CANVAS (sheet->row_canvas),
		0, 0, INT_MAX, INT_MAX);
}

void
sheet_col_set_selection (Sheet *sheet, ColRowInfo *ci, int value)
{
	g_return_if_fail (sheet != NULL);
	g_return_if_fail (IS_SHEET (sheet));
	g_return_if_fail (ci != NULL);
	
	if (ci->selected == value)
		return;
	
	ci->selected = value;
	gnome_canvas_request_redraw (GNOME_CANVAS (sheet->col_canvas),
		0, 0, INT_MAX, INT_MAX);
}

/* sheet_select_all
 * Sheet: The sheet
 *
 * Selects all of the cells in the sheet
 */
void
sheet_select_all (Sheet *sheet)
{
	GList *l;
	
	g_return_if_fail (sheet != NULL);
	g_return_if_fail (IS_SHEET (sheet));

	sheet_selection_clear_only (sheet);
	gnumeric_sheet_cursor_set (GNUMERIC_SHEET (sheet->sheet_view), 0, 0);
	sheet_selection_append_range (sheet, 0, 0, 0, 0,
		SHEET_MAX_COLS-1, SHEET_MAX_ROWS-1);

	for (l = sheet->cols_info; l; l = l->next){
		ColRowInfo *ci = (ColRowInfo *)l->data;

		sheet_col_set_selection (sheet, ci, 1);
	}

	for (l = sheet->rows_info; l; l = l->next){
		ColRowInfo *ri = (ColRowInfo *)l->data;

		sheet_row_set_selection (sheet, ri, 1);
	}
}

void
sheet_redraw_cell_region (Sheet *sheet, int start_col, int start_row,
			  int end_col, int end_row)
{
	GnumericSheet *gsheet;
	int x, y, w, h;
	
	g_return_if_fail (sheet != NULL);
	g_return_if_fail (IS_SHEET (sheet)); 

	gsheet = GNUMERIC_SHEET (sheet->sheet_view);
	g_return_if_fail (GNUMERIC_IS_SHEET (gsheet));
	
	x = sheet_col_get_distance (sheet, gsheet->top_col, start_col);
	y = sheet_row_get_distance (sheet, gsheet->top_row, start_row);
	w = sheet_col_get_distance (sheet, start_col, end_col+1);
	h = sheet_col_get_distance (sheet, start_col, end_col+1);

	gnome_canvas_request_redraw (GNOME_CANVAS (gsheet), x, y, x+w, y+h);
}

void
sheet_redraw_selection (Sheet *sheet, SheetSelection *ss)
{
	g_return_if_fail (sheet != NULL);
	g_return_if_fail (IS_SHEET (sheet)); 
	g_return_if_fail (ss != NULL);
	
	sheet_redraw_cell_region (sheet,
				  ss->start_col, ss->start_row,
				  ss->end_col, ss->end_row);
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

static void
sheet_selection_change (Sheet *sheet, SheetSelection *old, SheetSelection *new)
{
	GnumericSheet *gsheet = GNUMERIC_SHEET (sheet->sheet_view);
	
	if (sheet_selection_equal (old, new))
		return;
		
	gnumeric_sheet_accept_pending_output (gsheet);
	sheet_redraw_selection (sheet, old);
	sheet_redraw_selection (sheet, new);
	sheet_selection_changed_hook (sheet);
	
	gnumeric_sheet_set_selection (gsheet, new);
}

/*
 * sheet_selection_extend_horizontal
 * @sheet:  The Sheet *
 * @count:  units to extend the selection horizontally
 */
void
sheet_selection_extend_horizontal (Sheet *sheet, int n)
{
	SheetSelection *ss;
	SheetSelection old_selection;

	/* FIXME: right now we only support units (1 or -1)
	 * to fix this we need to account for the fact that
	 * the selection boundary might change and adjust
	 * appropiately
	 */
	 
	g_return_if_fail (sheet != NULL);
	g_return_if_fail (IS_SHEET (sheet)); 
	g_return_if_fail ((n == 1 || n == -1));
	
	ss = (SheetSelection *)sheet->selections->data;
	old_selection = *ss;
	
	if (ss->base_col < ss->end_col)
		ss->end_col = sheet_col_check_bound (ss->end_col, n);
	else if (ss->base_col > ss->start_col)
		ss->start_col = sheet_col_check_bound (ss->start_col, n);
	else {
		if (n > 0)
			ss->end_col = sheet_col_check_bound (ss->end_col, 1);
		else
			ss->start_col = sheet_col_check_bound (ss->start_col, -1);
	}

	sheet_selection_change (sheet, &old_selection, ss);
}

/*
 * sheet_selection_extend_vertical
 * @sheet:  The Sheet *
 * @n:      units to extend the selection vertically
 */
void
sheet_selection_extend_vertical (Sheet *sheet, int n)
{
	SheetSelection *ss;
	SheetSelection old_selection;
	
	g_return_if_fail (sheet != NULL);
	g_return_if_fail (IS_SHEET (sheet)); 
	g_return_if_fail ((n == 1 || n == -1));
	
	ss = (SheetSelection *)sheet->selections->data;
	old_selection = *ss;
	
	if (ss->base_row < ss->end_row)
		ss->end_row = sheet_row_check_bound (ss->end_row, n);
	else if (ss->base_row > ss->start_row)
		ss->start_row = sheet_row_check_bound (ss->start_row, n);
	else {
		if (n > 0)
			ss->end_row = sheet_row_check_bound (ss->end_row, 1);
		else
			ss->start_row = sheet_row_check_bound (ss->start_row, -1);
	}

	sheet_selection_change (sheet, &old_selection, ss);
}

/*
 * Clear the selection from the columns or rows
 */
static int
clean_bar_selection (GList *list)
{
	int cleared = 0;
	
	for (; list; list = list->next){
		ColRowInfo *info = (ColRowInfo *)list->data;

		if (info->selected){
			info->selected = 0;
			cleared++;
		}
	}
	return cleared;
}

/*
 * sheet_selection_clear
 * sheet:  The sheet
 *
 * Clears all of the selection ranges.
 * Warning: This does not set a new selection, this should
 * be taken care on the calling routine. 
 */
void
sheet_selection_clear_only (Sheet *sheet)
{
	GnumericSheet *gsheet;
	GList *list = sheet->selections;

	g_return_if_fail (sheet != NULL);
	g_return_if_fail (IS_SHEET (sheet)); 

	gsheet = GNUMERIC_SHEET (sheet->sheet_view);
	
	for (list = sheet->selections; list; list = list->next){
		SheetSelection *ss = list->data;

		sheet_redraw_selection (sheet, ss);
		g_free (ss);
	}
	g_list_free (sheet->selections);
	sheet->selections = NULL;
	sheet->walk_info.current = NULL;
		
	/* Unselect the column bar */
	if (clean_bar_selection (sheet->cols_info) > 0)
		gnome_canvas_request_redraw (GNOME_CANVAS (sheet->col_canvas),
					     0, 0, INT_MAX, INT_MAX);

	/* Unselect the row bar */
	if (clean_bar_selection (sheet->rows_info) > 0)
		gnome_canvas_request_redraw (GNOME_CANVAS (sheet->row_canvas),
					     0, 0, INT_MAX, INT_MAX);
	
}

/*
 * sheet_selection_clear
 * sheet:  The sheet
 *
 * Clears all of the selection ranges and resets it to a
 * selection that only covers the cursor
 */
void
sheet_selection_clear (Sheet *sheet)
{
	GnumericSheet *gsheet;

	g_return_if_fail (sheet != NULL);
	g_return_if_fail (IS_SHEET (sheet)); 
	
	gsheet = GNUMERIC_SHEET (sheet->sheet_view);
	
	sheet_selection_clear_only (sheet);
	sheet_selection_append (sheet, gsheet->cursor_col, gsheet->cursor_row);
}

int
sheet_selection_is_cell_selected (Sheet *sheet, int col, int row)
{
	GList *list = sheet->selections;

	for (list = sheet->selections; list; list = list->next){
		SheetSelection *ss = list->data;

		if ((ss->start_col <= col) && (col <= ss->end_col) &&
		    (ss->start_row <= row) && (row <= ss->end_row))
			return 1;
	}
	return 0;
}

/*
 * walk_boundaries: implements the decitions for walking a region
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
		if (ss->start_col == ss->end_col && ss->start_row == ss->end_row){
			walk_boundaries (0, 0, SHEET_MAX_COLS, SHEET_MAX_ROWS,
					 inc_x, inc_y, current_col, current_row,
					 new_col, new_row);
			return FALSE;
		}
	}

	if (!sheet->walk_info.current)
		sheet->walk_info.current = sheet->selections->data;

	ss = sheet->walk_info.current;

	overflow = walk_boundaries_wrapped (
		ss->start_col, ss->start_row,
		ss->end_col,   ss->end_row,
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
		sheet->walk_info.current = ss;
		
		if (forward){
			*new_col = ss->start_col;
			*new_row = ss->start_row;
		} else {
			*new_col = ss->end_col;
			*new_row = ss->end_row;
		}
	}
	return TRUE;
}

/*
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

/*
 * Returns an allocated column:  either an existing one, or a fresh copy
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

/*
 * sheet_cell_get
 * @sheet:  The sheet where we want to locate the cell
 * @col:    the cell column
 * @row:    the cell row
 *
 * Return value: a (Cell *) containing the Cell, or NULL if
 * the cell does not exist
 */
Cell *
sheet_cell_get (Sheet *sheet, int col, int row)
{
	Cell *cell;
	CellPos cellref;
	
	g_return_val_if_fail (sheet != NULL, NULL);
	g_return_val_if_fail (IS_SHEET (sheet), NULL); 

	cellref.col = col;
	cellref.row = row;
	cell = g_hash_table_lookup (sheet->cell_hash, &cellref);

	return cell;
}

/*
 * For each existing cell in the range specified, invoke the
 * callback routine.  If the only_existing flag is passed, then
 * callbacks are only invoked for existing cells.
 *
 * Return value:
 *    FALSE if some invoked routine requested to stop (by returning FALSE). 
 */
int
sheet_cell_foreach_range (Sheet *sheet, int only_existing,
			  int start_col, int start_row,
			  int end_col, int end_row,
			  sheet_cell_foreach_callback callback,
			  void *closure)
{
	GList *col;
	GList *row;
	int   last_col_gen = -1, last_row_gen = -1;
	int   cont;

	g_return_val_if_fail (sheet != NULL, FALSE);
	g_return_val_if_fail (IS_SHEET (sheet), FALSE); 
	g_return_val_if_fail (callback != NULL, FALSE);
	
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
				    return FALSE;
					
			if (ci->pos > start_col)
				if (!gen_col_blanks (sheet, start_col, ci->pos,
						     start_row, end_row, callback,
						     closure))
					return FALSE;
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
							return FALSE;
				}
				if (row_pos > start_row){
					if (!gen_row_blanks (sheet, ci->pos,
							     row_pos, start_row,
							     callback, closure))
						return FALSE;
				}
			}
			cont = (*callback)(sheet, ci->pos, row_pos, cell, closure);
			if (cont == FALSE)
				return FALSE;
		}
	}
	return TRUE;
}

Style *
sheet_style_compute (Sheet *sheet, int col, int row)
{
	g_return_val_if_fail (sheet != NULL, NULL);
	g_return_val_if_fail (IS_SHEET (sheet), NULL); 
	
	/* FIXME: This should compute the style based on the
	 * story of the styles applied to the worksheet, the
	 * sheet, the column and the row and return that.
	 *
	 * for now, we just return the col style
	 */
	return style_duplicate (sheet_col_get_info (sheet, col)->style);
}

static gint
CRowSort (gconstpointer a, gconstpointer b)
{
	Cell *ca = (Cell *) a;
	Cell *cb = (Cell *) b;

	return ca->row->pos - cb->row->pos;
}

void
sheet_cell_add (Sheet *sheet, Cell *cell, int col, int row)
{
	CellPos *cellref;
	
	cell->sheet = sheet;
	cell->col   = sheet_col_get (sheet, col);
	cell->row   = sheet_row_get (sheet, row);
	cell->style = sheet_style_compute (sheet, col, row);

	cell->width = cell->col->margin_a + cell->col->margin_b;
	
	cellref = g_new0 (CellPos, 1);
	cellref->col = col;
	cellref->row = row;
	
	g_hash_table_insert (sheet->cell_hash, cellref, cell);
	cell->col->data = g_list_insert_sorted (cell->col->data, cell, CRowSort);

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

void
sheet_cell_remove (Sheet *sheet, Cell *cell)
{
	CellPos cellref;

	g_return_if_fail (sheet != NULL);
	g_return_if_fail (cell != NULL);
	g_return_if_fail (IS_SHEET (sheet));

	cellref.col = cell->col->pos;
	cellref.row = cell->row->pos;

	if (cell->parsed_node)
		sheet_cell_formula_unlink (cell);

	g_hash_table_remove (sheet->cell_hash, &cellref);
	cell->col->data = g_list_remove (cell->col->data, cell);

	sheet_redraw_cell_region (sheet,
				  cellref.col, cellref.row, 
				  cellref.col, cellref.row);
}

void
sheet_cell_formula_link (Cell *cell)
{
	Sheet *sheet = cell->sheet;

	sheet->formula_cell_list = g_list_prepend (sheet->formula_cell_list, cell);
}

void
sheet_cell_formula_unlink (Cell *cell)
{
	Sheet *sheet = cell->sheet;
	
	sheet->formula_cell_list = g_list_remove (sheet->formula_cell_list, cell);
}

static int
assemble_cell (Sheet *sheet, int col, int row, Cell *cell, void *user_data)
{
	GList **l = (GList **) user_data;

	*l = g_list_prepend (*l, cell);
	return TRUE;
}

/*
 * Clears are region of cells
 *
 * We assemble a list of cells to destroy, since we will be making changes
 * to the structure being manipulated by the sheet_cell_foreach_range routine
 */
void
sheet_clear_region (Sheet *sheet, int start_col, int start_row, int end_col, int end_row)
{
	GList *destroyable_cells, *l;
	
	g_return_if_fail (sheet != NULL);
	g_return_if_fail (IS_SHEET (sheet));
	g_return_if_fail (start_col <= end_col);
	g_return_if_fail (start_row <= end_row);

	destroyable_cells = NULL;
	sheet_cell_foreach_range (
		sheet, TRUE,
		start_col, start_row,
		end_col, end_row,
		assemble_cell, &destroyable_cells);

	for (l = destroyable_cells; l; l = l->next){
		Cell *cell = l->data;
		
		sheet_cell_remove (sheet, cell);
		cell_destroy (cell);
	}
	g_list_free (destroyable_cells);
}

void
sheet_selection_copy (Sheet *sheet)
{
	SheetSelection *ss;
	g_return_if_fail (sheet != NULL);
	g_return_if_fail (IS_SHEET (sheet));

	if (g_list_length (sheet->selections) != 1){
		gnumeric_notice (_("Can not copy non-contiguous selections"));
		return;
	}
	
	ss = sheet->selections->data;

	if (sheet->workbook->clipboard_contents)
		clipboard_release (sheet->workbook->clipboard_contents);

	sheet->workbook->clipboard_contents = clipboard_copy_cell_range (
		sheet,
		ss->start_col, ss->start_row,
		ss->end_col, ss->end_row);
}

void
sheet_selection_cut (Sheet *sheet)
{
	SheetSelection *ss;
	
	g_return_if_fail (sheet != NULL);
	g_return_if_fail (IS_SHEET (sheet));

	if (g_list_length (sheet->selections) != 1){
		gnumeric_notice (_("Can not cut non-contiguous selections"));
		return;
	}

	ss = sheet->selections->data;

	sheet_selection_copy (sheet);
	sheet_clear_region (sheet, ss->start_col, ss->start_row, ss->end_col, ss->end_row);
}

void
sheet_selection_paste (Sheet *sheet, int dest_col, int dest_row, int paste_flags)
{
	CellRegion *content;
	g_return_if_fail (sheet != NULL);
	g_return_if_fail (IS_SHEET (sheet));

	content = sheet->workbook->clipboard_contents;
	
	if (!content)
		return;
	
	clipboard_paste_region (content, sheet, dest_col, dest_row, paste_flags);
}

