#include <gnome.h>
#include "gnumeric.h"
#include "gnumeric-sheet.h"

static void
sheet_init_dummy_stuff (Sheet *sheet)
{
	int x, y;
	ColRowInfo c, *cp, *rp;

	c.pos        = 0;
	c.style      = NULL;
	c.units      = 40;
	c.pixels     = 0;
	c.margin_a   = 0;
	c.margin_b   = 0;

	sheet->default_col_style = c;

	/* Initialize some of the columns */
	for (x = 0; x < 40; x += 2){
		cp = g_new0 (ColRowInfo, 1);

		*cp = c;
		cp->pos = x;
		cp->units = (x+1) * 30;
		sheet->cols_info = g_list_append (sheet->cols_info, cp);
	}

	/* Rows, we keep them consistent for now */
	sheet->default_row_style.pos      = 0;
	sheet->default_row_style.style    = NULL;
	sheet->default_row_style.units    = 20;
	sheet->default_row_style.pixels   = 0;
	sheet->default_row_style.margin_a = 0;
	sheet->default_row_style.margin_b = 0;

	for (y = 0; y < 6; y += 2){
		rp = g_new0 (ColRowInfo, 1);

		*rp = sheet->default_row_style;
		rp->pos = y;
		rp->units = (20 * (y + 1));
		sheet->rows_info = g_list_append (sheet->rows_info, rp);
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
	item = gnome_canvas_item_new (GNOME_CANVAS (canvas), group,
				      item_bar_get_type (),
				      "ItemBar::Sheet", sheet,
				      "ItemBar::Orientation", o,
				      "ItemBar::First", 0,
				      NULL);
	*itemp = GNOME_CANVAS_ITEM (item);
	gtk_widget_show (canvas);
	return canvas;
}

Sheet *
sheet_new (Workbook *wb, char *name)
{
	Sheet *sheet;

	sheet = g_new0 (Sheet, 1);
	sheet->parent_workbook = wb;
	sheet->name = g_strdup (name);
	sheet->last_zoom_factor_used = -1.0;
	sheet->toplevel = gtk_table_new (0, 0, 0);
		
	/* Dummy initialization */
	sheet_init_dummy_stuff (sheet);
	
	sheet->sheet_view = gnumeric_sheet_new (sheet);
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
	
	/* Row canvas */
	sheet->row_canvas = new_canvas_bar (sheet, GTK_ORIENTATION_VERTICAL, &sheet->row_item);
	gtk_table_attach (GTK_TABLE (sheet->toplevel), sheet->row_canvas,
			  0, 1, 1, 2,
			  0, GTK_FILL | GTK_EXPAND, 0, 0);
	
	sheet_set_zoom_factor (sheet, 1.0);
	return sheet;
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
sheet_get_col_info (Sheet *sheet, int col)
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
sheet_get_row_info (Sheet *sheet, int row)
{
	GList *l = sheet->rows_info;

	for (; l; l = l->next){
		ColRowInfo *ri = l->data;

		if (ri->pos == row)
			return ri;
	}

	return &sheet->default_row_style;
}

/*
 * Return the number of pixels between from_col to to_col
 */
int
sheet_col_get_distance (Sheet *sheet, int from_col, int to_col)
{
	ColRowInfo *ci;
	int pixels = 0, i;

	g_assert (from_col <= to_col);

	/* This can be optimized, yes, but the implementation
	 * of the ColRowInfo sets is going to change anyways
	 */
	for (i = from_col; i < to_col; i++){
		ci = sheet_get_col_info (sheet, i);
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
		ri = sheet_get_row_info (sheet, i);
		pixels += ri->pixels;
	}
	return pixels;
}

void
sheet_get_cell_bounds (Sheet *sheet, ColType col, RowType row, int *x, int *y, int *w, int *h)
{
	GnumericSheet *gsheet = GNUMERIC_SHEET (sheet->sheet_view);
	
	*x = sheet_col_get_distance (sheet, gsheet->top_col, col);
	*y = sheet_row_get_distance (sheet, gsheet->top_row, row);

	*w = sheet_col_get_distance (sheet, col, col + 1);
	*h = sheet_row_get_distance (sheet, row, row + 1);
}
