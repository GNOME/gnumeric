/*
 * Sheet.c:  Implements the sheet management and per-sheet storage
 * (C) 1998 The Free Software Foundation
 *
 * Author:
 *  Miguel de Icaza (miguel@gnu.org)
 *
 */
#include <gnome.h>
#include "gnumeric.h"
#include "gnumeric-sheet.h"

static void
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
	sheet->default_col_style.units      = 40;
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

static GtkWidget *
new_canvas_bar (Sheet *sheet, GtkOrientation o, GnomeCanvasItem **itemp)
{
	GnomeCanvasGroup *group;
	GnomeCanvasItem *item;
	GtkWidget *canvas;
	int w, h;
	
	canvas = gnome_canvas_new (
		gtk_widget_get_default_visual (),
		gtk_widget_get_default_colormap ());

	if (o == GTK_ORIENTATION_VERTICAL){
		w = 60;
		h = 1;
	} else {
		w = 1;
		h = 20;
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
sheet_col_selection_changed (ItemBar *item_bar, int column, Sheet *sheet)
{
	printf ("Sheet signal: Column %d selection changed\n", column);
}

static void
sheet_col_size_changed (ItemBar *item_bar, int col, int width, Sheet *sheet)
{
	sheet_col_set_width (sheet, col, width);
	gnumeric_sheet_compute_visible_ranges (GNUMERIC_SHEET (sheet->sheet_view));
}

static void
sheet_row_selection_changed (ItemBar *item_bar, int row, Sheet *sheet)
{
	printf ("Sheet signal: Row %d selection changed\n", row);
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
	CellRef *ca = (CellRef *) key;

	return (ca->row << 8) | ca->col;
}

static gint
cell_compare (gconstpointer a, gconstpointer b)
{
	CellRef *ca, *cb;

	ca = (CellRef *) a;
	cb = (CellRef *) b;

	if (ca->row != cb->row)
		return 0;
	if (ca->col != cb->col)
		return 0;
	
	return 1;
}

Sheet *
sheet_new (Workbook *wb, char *name)
{
	Sheet *sheet;
	int rows_shown, cols_shown;

	rows_shown = cols_shown = 40;
	
	sheet = g_new0 (Sheet, 1);
	sheet->parent_workbook = wb;
	sheet->name = g_strdup (name);
	sheet->last_zoom_factor_used = -1.0;
	sheet->toplevel = gtk_table_new (0, 0, 0);
	sheet->max_col_used = cols_shown;
	sheet->max_row_used = rows_shown;

	sheet->cell_hash = g_hash_table_new (cell_hash, cell_compare);
	
	sheet_init_default_styles (sheet);
	
	/* Dummy initialization */
	sheet_init_dummy_stuff (sheet);

	/* Create the gnumeric sheet and set the initial selection */
	sheet->sheet_view = gnumeric_sheet_new (sheet);
	sheet_selection_append (sheet, 0, 0);
	
	gtk_widget_show (sheet->sheet_view);
	gtk_widget_show (sheet->toplevel);
	
	gtk_table_attach (GTK_TABLE (sheet->toplevel), sheet->sheet_view,
			  1, 2, 1, 2,
			  GTK_FILL | GTK_EXPAND, GTK_FILL | GTK_EXPAND, 0, 0);

	/* Column canvas */
	sheet->col_canvas = new_canvas_bar (sheet, GTK_ORIENTATION_HORIZONTAL, &sheet->col_item);
	gtk_table_attach (GTK_TABLE (sheet->toplevel), sheet->col_canvas,
			  1, 2, 0, 1,
			  GTK_FILL | GTK_EXPAND, 0, 0, 0);
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
			  0, GTK_FILL | GTK_EXPAND, 0, 0);
	gtk_signal_connect (GTK_OBJECT (sheet->row_item), "selection_changed",
			    GTK_SIGNAL_FUNC (sheet_row_selection_changed),
			    sheet);
	gtk_signal_connect (GTK_OBJECT (sheet->row_item), "size_changed",
			    GTK_SIGNAL_FUNC (sheet_row_size_changed),
			    sheet);

	/* Scroll bars and their adjustments */
	sheet->va = gtk_adjustment_new (0.0, 0.0, sheet->max_row_used, 1.0, rows_shown, 1.0);
	sheet->ha = gtk_adjustment_new (0.0, 0.0, sheet->max_col_used, 1.0, cols_shown, 1.0);
	
	sheet->hs = gtk_hscrollbar_new (GTK_ADJUSTMENT (sheet->ha));
	sheet->vs = gtk_vscrollbar_new (GTK_ADJUSTMENT (sheet->va));

	/* Attach the horizontal scroll */
	gtk_table_attach (GTK_TABLE (sheet->toplevel), sheet->hs,
			  1, 2, 2, 3,
			  GTK_FILL | GTK_EXPAND, 0, 0, 0);

	/* Attach the vertical scroll */
	gtk_table_attach (GTK_TABLE (sheet->toplevel), sheet->vs,
			  2, 3, 1, 2,
			  0, GTK_FILL | GTK_EXPAND, 0, 0);
	
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
	
	sheet_selection_clear (sheet);
	g_free (sheet->name);
	
	style_destroy (sheet->default_row_style.style);
	style_destroy (sheet->default_col_style.style);

	g_hash_table_foreach (sheet->cell_hash, cell_hash_free_key, NULL);
	gtk_widget_destroy (sheet->toplevel);
	
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
sheet_selection_append (Sheet *sheet, int col, int row)
{
	SheetSelection *ss;

	g_return_if_fail (sheet != NULL);
	
	ss = g_new0 (SheetSelection, 1);

	ss->base_col  = col;
	ss->base_row  = row;

	ss->start_col = col;
	ss->end_col   = col;
	ss->start_row = row;
	ss->end_row   = row;
	
	sheet->selections = g_list_prepend (sheet->selections, ss);

	gnumeric_sheet_accept_pending_output (GNUMERIC_SHEET (sheet->sheet_view));
	gnumeric_sheet_load_cell_val (GNUMERIC_SHEET (sheet->sheet_view));
	
	gnumeric_sheet_set_selection (GNUMERIC_SHEET (sheet->sheet_view), ss);
	sheet_redraw_selection (sheet, ss);
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

	sheet_redraw_selection (sheet, &old_selection);
	sheet_redraw_selection (sheet, ss);
}

void
sheet_redraw_cell_region (Sheet *sheet, int start_col, int start_row,
			  int end_col, int end_row)
{
	GnumericSheet *gsheet;
	int x, y, w, h;
	
	g_return_if_fail (sheet != NULL);

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
	GList *list = sheet->selections;

	g_return_if_fail (sheet != NULL);

	gsheet = GNUMERIC_SHEET (sheet->sheet_view);
	
	for (list = sheet->selections; list; list = list->next){
		SheetSelection *ss = list->data;

		sheet_redraw_selection (sheet, ss);
		g_free (ss);
	}
	g_list_free (sheet->selections);
	sheet->selections = NULL;
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
 * Returns an allocated column:  either an existing one, or a fresh copy
 */
ColRowInfo *
sheet_col_get (Sheet *sheet, int pos)
{
	GList *clist;
	ColRowInfo *col;

	g_return_val_if_fail (sheet != NULL, NULL);
	
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

static void
gen_row_blanks (Sheet *sheet, int col, int start_row, int end_row,
		sheet_cell_foreach_callback callback, void *closure)
{
	int row;

	for (row = 0; row < end_row; row++)
		(*callback)(sheet, col, row, NULL, closure);
}

static void
gen_col_blanks (Sheet *sheet, int start_col, int end_col,
		int start_row, int end_row,
		sheet_cell_foreach_callback callback, void *closure)
{
	int col;
       
	for (col = 0; col < end_col; col++)
		gen_row_blanks (sheet, col, start_row, end_row, callback, closure);
}

Cell *
sheet_cell_get (Sheet *sheet, int col, int row)
{
	Cell *cell;
	CellRef cellref;
	
	g_return_val_if_fail (sheet != NULL, NULL);

	cellref.col = col;
	cellref.row = row;
	cell = g_hash_table_lookup (sheet->cell_hash, &cellref);

	return cell;
}

/*
 * For each existing cell in the range specified, invoke the
 * callback routine.  If the only_existing flag is passed, then
 * callbacks are only invoked for existing cells.
 */
void
sheet_cell_foreach_range (Sheet *sheet, int only_existing,
			  int start_col, int start_row,
			  int end_col, int end_row,
			  sheet_cell_foreach_callback callback,
			  void *closure)
{
	GList *col;
	GList *row;
	int   last_col_gen = -1, last_row_gen = -1;

	g_return_if_fail (sheet != NULL);
	g_return_if_fail (callback != NULL);
	
	col = sheet->cols_info;
	for (; col; col = col->next){
		ColRowInfo *ci = col->data;

		if (ci->pos < start_col)
			continue;
		if (ci->pos > end_col)
			break;

		if (!only_existing){
			if ((last_col_gen > 0) && (ci->pos != last_col_gen+1))
				gen_col_blanks (sheet, last_col_gen, ci->pos,
						start_row, end_row, callback,
						closure);
					
			if (ci->pos > start_col)
				gen_col_blanks (sheet, start_col, ci->pos,
						start_row, end_row, callback,
						closure);
		}
		last_col_gen = ci->pos;

		last_row_gen = -1;
		for (row = (GList *) ci->data; row; row = row->data){
			ColRowInfo *ri = row->data;

			if (ri->pos < start_row)
				continue;

			if (ri->pos > end_row)
				break;

			if (!only_existing){
				if (last_row_gen > 0){
					if (ri->pos != last_row_gen+1)
						gen_row_blanks (sheet, ci->pos,
								last_row_gen,
								ri->pos,
								callback,
								closure);
				}
				if (ri->pos > start_row)
					gen_row_blanks (sheet, ci->pos,
							ri->pos, start_row,
							callback, closure);
			}
			(*callback)(sheet, ci->pos, ri->pos, (Cell *) ri->data, closure);
		}
	}
}

Style *
sheet_style_compute (Sheet *sheet, int col, int row)
{
	g_return_val_if_fail (sheet != NULL, NULL);
	
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

	return cb->row->pos - ca->row->pos;
}

Cell *
sheet_cell_new (Sheet *sheet, int col, int row)
{
	Cell *cell;
	CellRef *cellref;
	
	g_return_val_if_fail (sheet != NULL, NULL);

	cell = g_new0 (Cell, 1);
	cell->col   = sheet_col_get (sheet, col);
	cell->row   = sheet_row_get (sheet, row);
	cell->style = sheet_style_compute (sheet, col, row);

	cellref = g_new0 (CellRef, 1);
	cellref->col = col;
	cellref->row = row;
	
	g_hash_table_insert (sheet->cell_hash, cellref, cell);
	cell->col->data = g_list_insert_sorted (cell->col->data, cell, CRowSort);

	return cell;
}

void
cell_set_text (Cell *cell, char *text)
{
	GdkFont *font;

	g_return_if_fail (cell != NULL);
	g_return_if_fail (text != NULL);

	/* The value entered */
	if (cell->entered_text)
		g_free (cell->entered_text);
	cell->entered_text = g_strdup (text);

	/* The computed text, for now, just the same */
	{
		cell->parsed_node = NULL;
		if (cell->text)
			g_free (cell->text);
		cell->text = g_strdup (text);
	}

	/* No default color */
	cell->flags = 0;
	
	font = cell->style->font->font;
	cell->width = gdk_text_width (font, cell->text, strlen (cell->text));
	cell->height = font->ascent + font->descent;
}

