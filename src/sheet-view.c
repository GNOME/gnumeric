/*
 * sheet-view.c: Implements a view of the Sheet.
 *
 * Author:
 *    Miguel de Icaza (miguel@kernel.org)
 *
 */
#include <config.h>

#include <gnome.h>
#include <gdk/gdkkeysyms.h>
#include <gdk/gdkx.h>
#include <string.h>
#include "gnumeric.h"
#include "gnumeric-sheet.h"

static GtkTableClass *sheet_view_parent_class;

void
sheet_view_redraw_all (SheetView *sheet_view)
{
	g_return_if_fail (sheet_view != NULL);
	g_return_if_fail (IS_SHEET_VIEW (sheet_view));
	
	gnome_canvas_request_redraw (
		GNOME_CANVAS (sheet_view->sheet_view),
		0, 0, INT_MAX, INT_MAX);
	gnome_canvas_request_redraw (
		GNOME_CANVAS (sheet_view->col_canvas),
		0, 0, INT_MAX, INT_MAX);
	gnome_canvas_request_redraw (
		GNOME_CANVAS (sheet_view->row_canvas),
		0, 0, INT_MAX, INT_MAX);
}

void
sheet_view_redraw_cell_region (SheetView *sheet_view, int start_col, int start_row, int end_col, int end_row)
{
	GnumericSheet *gsheet;
	Sheet *sheet = sheet_view->sheet;
	int first_col, first_row, last_col, last_row;
	int col, row, min_col, max_col;
	int x, y, w, h;
	
	g_return_if_fail (sheet_view != NULL);
	g_return_if_fail (IS_SHEET_VIEW (sheet_view));

	gsheet = GNUMERIC_SHEET (sheet_view->sheet_view);
	g_return_if_fail (GNUMERIC_IS_SHEET (gsheet));

	if ((end_col < gsheet->top_col) ||
	    (end_row < gsheet->top_row) ||
	    (start_col > gsheet->last_visible_col) ||
	    (start_row > gsheet->last_visible_row))
		return;
	
	/* The region on which we care to redraw */
	first_col = MAX (gsheet->top_col, start_col);
	first_row = MAX (gsheet->top_row, start_row);
	last_col =  MIN (gsheet->last_visible_col, end_col);
	last_row =  MIN (gsheet->last_visible_row, end_row);

	/* Initial values for min/max column computation */
	min_col = first_col;
	max_col = last_col;

	/* Find the range of columns that require a redraw */
	for (col = first_col; col <= last_col; col++)
		for (row = first_row; row <= last_row; row++){
			Cell *cell;
			int  col1, col2;

			cell = sheet_cell_get (sheet, col, row);

			if (cell){
				cell_get_span (cell, &col1, &col2);

				min_col = MIN (col1, min_col);
				max_col = MAX (col2, max_col);
			}
			
		}

	/* Only draw those regions that are visible */
	min_col = MAX (MIN (first_col, min_col), gsheet->top_col);
	max_col = MIN (MAX (last_col, max_col), gsheet->last_visible_col);

	x = sheet_col_get_distance (sheet, gsheet->top_col, min_col);
	y = sheet_row_get_distance (sheet, gsheet->top_row, first_row);
	w = sheet_col_get_distance (sheet, min_col, max_col+1);
	h = sheet_row_get_distance (sheet, first_row, last_row+1);

	gnome_canvas_request_redraw (GNOME_CANVAS (gsheet),
				     x, y,
				     x+w, y+h);
}

void
sheet_view_row_set_selection (SheetView *sheet_view, ColRowInfo *ri)
{
	gnome_canvas_request_redraw (
		GNOME_CANVAS (sheet_view->row_canvas),
		0, 0, INT_MAX, INT_MAX);
}

void
sheet_view_col_set_selection (SheetView *sheet_view, ColRowInfo *ci)
{
	gnome_canvas_request_redraw (
		GNOME_CANVAS (sheet_view->col_canvas),
		0, 0, INT_MAX, INT_MAX);
}

void
sheet_view_redraw_columns (SheetView *sheet_view)
{
	g_return_if_fail (sheet_view != NULL);
	g_return_if_fail (IS_SHEET_VIEW (sheet_view));

	gnome_canvas_request_redraw (
		GNOME_CANVAS (sheet_view->col_canvas),
		0, 0, INT_MAX, INT_MAX);
}

void
sheet_view_redraw_rows (SheetView *sheet_view)
{
	g_return_if_fail (sheet_view != NULL);
	g_return_if_fail (IS_SHEET_VIEW (sheet_view));

	gnome_canvas_request_redraw (
		GNOME_CANVAS (sheet_view->row_canvas),
		0, 0, INT_MAX, INT_MAX);
}

void
sheet_view_set_zoom_factor (SheetView *sheet_view, double factor)
{
	g_return_if_fail (sheet_view != NULL);
	g_return_if_fail (IS_SHEET_VIEW (sheet_view));
	
	gnome_canvas_set_pixels_per_unit (GNOME_CANVAS (sheet_view->sheet_view), factor);
	gnome_canvas_set_pixels_per_unit (GNOME_CANVAS (sheet_view->col_canvas), factor);
	gnome_canvas_set_pixels_per_unit (GNOME_CANVAS (sheet_view->row_canvas), factor);
	
	gnome_canvas_scroll_to (GNOME_CANVAS (sheet_view->sheet_view), 0, 0);
	gnome_canvas_scroll_to (GNOME_CANVAS (sheet_view->col_canvas), 0, 0);
	gnome_canvas_scroll_to (GNOME_CANVAS (sheet_view->row_canvas), 0, 0);
}

static void
canvas_bar_realized (GtkWidget *widget, gpointer data)
{
	gdk_window_set_back_pixmap (GTK_LAYOUT (widget)->bin_window, NULL, FALSE);
}

static GnomeCanvas *
new_canvas_bar (SheetView *sheet_view, GtkOrientation o, GnomeCanvasItem **itemp)
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
				      "ItemBar::SheetView", sheet_view,
				      "ItemBar::Orientation", o,
				      "ItemBar::First", 0,
				      NULL);

	*itemp = GNOME_CANVAS_ITEM (item);
	gtk_widget_show (canvas);
	
	return GNOME_CANVAS (canvas);
}

static void
sheet_view_size_allocate (GtkWidget *widget, GtkAllocation *alloc, SheetView *sheet_view)
{
	GtkAdjustment *va = GTK_ADJUSTMENT (sheet_view->va);
	GtkAdjustment *ha = GTK_ADJUSTMENT (sheet_view->ha);
	GnumericSheet *gsheet = GNUMERIC_SHEET (sheet_view->sheet_view);
	int last_col = gsheet->last_full_col;
	int last_row = gsheet->last_full_row;

	va->upper = MAX (last_row, sheet_view->sheet->max_row_used);
	va->page_size = last_row - gsheet->top_row;
	
	ha->upper = MAX (last_col, sheet_view->sheet->max_col_used);
	ha->page_size = last_col - gsheet->top_col;

	gtk_adjustment_changed (va);
	gtk_adjustment_changed (ha);
}

static void
sheet_view_col_selection_changed (ItemBar *item_bar, int column, int reset, SheetView *sheet_view)
{	
	ColRowInfo *ci;
	Sheet *sheet = sheet_view->sheet;
	
	ci = sheet_col_get (sheet, column);
	
	if (reset){
		sheet_cursor_set (sheet, column, 0, column, SHEET_MAX_ROWS - 1);
		sheet_selection_reset_only (sheet);
		sheet_selection_append_range (sheet,
					      column, 0,
					      column, 0,
					      column, SHEET_MAX_ROWS-1);
		sheet_col_set_selection (sheet, ci, 1);
	} else
		sheet_selection_col_extend_to (sheet, column);
}

static void
sheet_view_col_size_changed (ItemBar *item_bar, int col, int width, SheetView *sheet_view)
{
	Sheet *sheet = sheet_view->sheet;
	
	sheet_col_set_width (sheet, col, width);
	gnumeric_sheet_compute_visible_ranges (GNUMERIC_SHEET (sheet_view->sheet_view));
}

static void
sheet_view_row_selection_changed (ItemBar *item_bar, int row, int reset, SheetView *sheet_view)
{
	ColRowInfo *ri;
	Sheet *sheet = sheet_view->sheet;
	
	ri = sheet_row_get (sheet, row);

	if (reset){
		sheet_cursor_set (sheet, 0, row, SHEET_MAX_COLS-1, row);
		sheet_selection_reset_only (sheet);
		sheet_selection_append_range (sheet,
					      0, row,
					      0, row,
					      SHEET_MAX_COLS-1, row);
		sheet_row_set_selection (sheet, ri, 1);
	} else
		sheet_selection_row_extend_to (sheet, row);
}

static void
sheet_view_row_size_changed (ItemBar *item_bar, int row, int height, SheetView *sheet_view)
{
	Sheet *sheet = sheet_view->sheet;
	
	sheet_row_set_height (sheet, row, height, TRUE);
	gnumeric_sheet_compute_visible_ranges (GNUMERIC_SHEET (sheet_view->sheet_view));
}

static void
button_select_all (GtkWidget *the_button, SheetView *sheet_view)
{
	sheet_select_all (sheet_view->sheet);
}

static void
set_tip_label (SheetView *sheet_view, char *format, GtkAdjustment *adj, int horizontal)
{
	char buffer [40];

	if (horizontal)
		snprintf (buffer, sizeof (buffer), format, col_name (adj->value));
	else
		snprintf (buffer, sizeof (buffer), format, (int) adj->value + 1);

	gtk_label_set (GTK_LABEL (sheet_view->tip), buffer);
}

static void
vertical_scroll_change (GtkAdjustment *adj, SheetView *sheet_view)
{
	if (sheet_view->tip)
		set_tip_label (sheet_view, _("Row: %d"), adj, 0);
}

static void
horizontal_scroll_change (GtkAdjustment *adj, SheetView *sheet_view)
{
	if (sheet_view->tip)
		set_tip_label (sheet_view, _("Column: %s"), adj, 1);
}

static GtkWidget *
create_tip (void)
{
	GtkWidget *tip, *label;

	tip = gtk_window_new (GTK_WINDOW_POPUP);
	label = gtk_label_new ("");
	
	gtk_container_add (GTK_CONTAINER (tip), label);
	
	return label;
}

static void
position_tooltip (SheetView *sheet_view, int horizontal)
{
	GtkRequisition req;
	int  x, y;

	gtk_widget_size_request (sheet_view->tip, &req);
	gdk_window_get_pointer (NULL, &x, &y, NULL);
	if (horizontal){
		x = x - req.width/2;
		y = y - req.height - 20;
	} else {
		x = x - req.width - 20;
		y = y - req.height/2;
	}
	gtk_widget_set_uposition (gtk_widget_get_toplevel (sheet_view->tip), x, y);
}

static int
horizontal_scroll_event (GtkScrollbar *scroll, GdkEvent *event, SheetView *sheet_view)
{
	if (event->type == GDK_BUTTON_PRESS){
		sheet_view->tip = create_tip ();
		set_tip_label (sheet_view, _("Column: %s"), GTK_ADJUSTMENT (sheet_view->ha), 1);
		position_tooltip (sheet_view, 1);
		gtk_widget_show_all (gtk_widget_get_toplevel (sheet_view->tip));
	} else if (event->type == GDK_BUTTON_RELEASE){
		SheetSelection *ss = sheet_view->sheet->selections->data;
		
		gtk_widget_destroy (gtk_widget_get_toplevel (sheet_view->tip));
		sheet_view->tip = NULL;

		sheet_cursor_move (sheet_view->sheet, GTK_ADJUSTMENT (sheet_view->ha)->value, ss->start_row);
	}
	
	return FALSE;
}

static int
vertical_scroll_event (GtkScrollbar *scroll, GdkEvent *event, SheetView *sheet_view)
{
	if (event->type == GDK_BUTTON_PRESS){
		sheet_view->tip = create_tip ();
		set_tip_label (sheet_view, _("Row: %d"), GTK_ADJUSTMENT (sheet_view->va), 0);
		position_tooltip (sheet_view, 0);
		gtk_widget_show_all (gtk_widget_get_toplevel (sheet_view->tip));
	} else if (event->type == GDK_BUTTON_RELEASE){
		SheetSelection *ss = sheet_view->sheet->selections->data;
		
		gtk_widget_destroy (gtk_widget_get_toplevel (sheet_view->tip));
		sheet_view->tip = NULL;

		sheet_cursor_move (sheet_view->sheet, ss->start_col, GTK_ADJUSTMENT (sheet_view->va)->value);
	}

	return FALSE;
}

static void
sheet_view_construct (SheetView *sheet_view)
{
	GnomeCanvasGroup *root_group;
	GtkTable  *table = GTK_TABLE (sheet_view);
	GtkWidget *select_all;
	Sheet *sheet = sheet_view->sheet;
	
	/* Column canvas */
	sheet_view->col_canvas = new_canvas_bar (sheet_view, GTK_ORIENTATION_HORIZONTAL, &sheet_view->col_item);
	gtk_table_attach (table, GTK_WIDGET (sheet_view->col_canvas),
			  1, 2, 0, 1,
			  GTK_EXPAND | GTK_FILL | GTK_SHRINK,
			  GTK_FILL,
			  0, 0);
	gtk_signal_connect (GTK_OBJECT (sheet_view->col_item), "selection_changed",
			    GTK_SIGNAL_FUNC (sheet_view_col_selection_changed),
			    sheet_view);
	gtk_signal_connect (GTK_OBJECT (sheet_view->col_item), "size_changed",
			    GTK_SIGNAL_FUNC (sheet_view_col_size_changed),
			    sheet_view);

	/* Row canvas */
	sheet_view->row_canvas = new_canvas_bar (sheet_view, GTK_ORIENTATION_VERTICAL, &sheet_view->row_item);
	
	gtk_table_attach (table, GTK_WIDGET (sheet_view->row_canvas),
			  0, 1, 1, 2,
			  GTK_FILL, GTK_EXPAND | GTK_FILL | GTK_SHRINK,
			  0, 0);
	gtk_signal_connect (GTK_OBJECT (sheet_view->row_item), "selection_changed",
			    GTK_SIGNAL_FUNC (sheet_view_row_selection_changed),
			    sheet_view);
	gtk_signal_connect (GTK_OBJECT (sheet_view->row_item), "size_changed",
			    GTK_SIGNAL_FUNC (sheet_view_row_size_changed),
			    sheet_view);


	/* Create the gnumeric sheet canvas */
	sheet_view->sheet_view = gnumeric_sheet_new (
		sheet_view,
		ITEM_BAR (sheet_view->col_item),
		ITEM_BAR (sheet_view->row_item));
	
	gtk_signal_connect_after (
		GTK_OBJECT (sheet_view), "size_allocate",
		GTK_SIGNAL_FUNC (sheet_view_size_allocate), sheet_view);

	/* Create the object group inside the GnumericSheet */
	root_group = GNOME_CANVAS_GROUP (
		GNOME_CANVAS (sheet_view->sheet_view)->root);
	sheet_view->object_group = gnome_canvas_item_new (
		root_group,
		gnome_canvas_group_get_type (),
		"x", 0.0,
		"y", 0.0,
		NULL);
		
	/* Attach the GnumericSheet */
	gtk_table_attach (table, sheet_view->sheet_view,
			  1, 2, 1, 2,
			  GTK_EXPAND | GTK_FILL | GTK_SHRINK,
			  GTK_EXPAND | GTK_FILL | GTK_SHRINK,
			  0, 0);
	gtk_widget_show (sheet_view->sheet_view);

	/* The select-all button */
	select_all = gtk_button_new ();
	gtk_table_attach (table, select_all, 0, 1, 0, 1, GTK_FILL, GTK_FILL, 0, 0);
	gtk_signal_connect (GTK_OBJECT (select_all), "clicked",
			    GTK_SIGNAL_FUNC (button_select_all), sheet_view);
	
	/* Scroll bars and their adjustments */
	sheet_view->va = gtk_adjustment_new (0.0, 0.0, sheet->max_row_used, 1.0, 0.0, 1.0);
	sheet_view->ha = gtk_adjustment_new (0.0, 0.0, sheet->max_col_used, 1.0, 0.0, 1.0);
	sheet_view->hs = gtk_hscrollbar_new (GTK_ADJUSTMENT (sheet_view->ha));
	sheet_view->vs = gtk_vscrollbar_new (GTK_ADJUSTMENT (sheet_view->va));

	gtk_signal_connect (GTK_OBJECT (sheet_view->ha), "value_changed",
			    GTK_SIGNAL_FUNC (horizontal_scroll_change), sheet_view);
	gtk_signal_connect (GTK_OBJECT (sheet_view->va), "value_changed",
			    GTK_SIGNAL_FUNC (vertical_scroll_change), sheet_view);
	gtk_signal_connect (GTK_OBJECT (sheet_view->hs), "event",
			    GTK_SIGNAL_FUNC (horizontal_scroll_event), sheet_view);
	gtk_signal_connect (GTK_OBJECT (sheet_view->vs), "event",
			    GTK_SIGNAL_FUNC (vertical_scroll_event), sheet_view);
	
	/* Attach the horizontal scroll */
	gtk_table_attach (table, sheet_view->hs,
			  1, 2, 2, 3,
			  GTK_EXPAND | GTK_FILL | GTK_SHRINK,
			  GTK_FILL,
			  0, 0);

	/* Attach the vertical scroll */
	gtk_table_attach (table, sheet_view->vs,
			  2, 3, 1, 2,
			  GTK_FILL,
			  GTK_EXPAND | GTK_FILL | GTK_SHRINK,
			  0, 0);
	
	
}

static void
sheet_view_init (SheetView *sheet_view)
{
	GtkTable *table = GTK_TABLE (sheet_view);

	table->homogeneous = FALSE;
	gtk_table_resize (table, 4, 4);
}

GtkWidget *
sheet_view_new (Sheet *sheet)
{
	SheetView *sheet_view;

	sheet_view = gtk_type_new (sheet_view_get_type ());
	sheet_view->sheet = sheet;
	sheet_view_construct (sheet_view);
	
	return GTK_WIDGET (sheet_view);
}

static void
sheet_view_destroy (GtkObject *object)
{
	SheetView *sheet_view = SHEET_VIEW (object);

	/* Add shutdown code here */
	if (sheet_view->tip)
		gtk_object_destroy (GTK_OBJECT (sheet_view->tip));
	
	if (GTK_OBJECT_CLASS (sheet_view_parent_class)->destroy)
		(*GTK_OBJECT_CLASS (sheet_view_parent_class)->destroy)(object);
}

static void
sheet_view_class_init (SheetViewClass *class)
{
	GtkObjectClass *object_class;

	object_class = (GtkObjectClass *) class;

	sheet_view_parent_class = gtk_type_class (gtk_table_get_type ());
	
	object_class->destroy = sheet_view_destroy;
}

GtkType
sheet_view_get_type (void)
{
	static GtkType sheet_view_type = 0;

	if (!sheet_view_type){
		GtkTypeInfo sheet_view_info = {
			"SheetView",
			sizeof (SheetView),
			sizeof (SheetViewClass),
			(GtkClassInitFunc) sheet_view_class_init,
			(GtkObjectInitFunc) sheet_view_init,
			NULL, /* reserved 1 */
			NULL, /* reserved 2 */
			(GtkClassInitFunc) NULL
		};

		sheet_view_type = gtk_type_unique (gtk_table_get_type (), &sheet_view_info);
	}

	return sheet_view_type;
}
		     
