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
#include "selection.h"
#include "sheet-object.h"
#include "item-cursor.h"
#include "gnumeric-util.h"
#include "utils.h"
#include "selection.h"

/* Sizes for column/row headers */

#define COLUMN_HEADER_HEIGHT 20
#define ROW_HEADER_WIDTH 60

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
	GnomeCanvas *canvas;
	Sheet *sheet = sheet_view->sheet;
	int first_col, first_row, last_col, last_row;
	int col, row, min_col, max_col;
	int x, y, w, h;
	
	g_return_if_fail (sheet_view != NULL);
	g_return_if_fail (IS_SHEET_VIEW (sheet_view));

	gsheet = GNUMERIC_SHEET (sheet_view->sheet_view);
	g_return_if_fail (GNUMERIC_IS_SHEET (gsheet));
	canvas = GNOME_CANVAS (gsheet);

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

	x += canvas->layout.xoffset - canvas->zoom_xofs;
	y += canvas->layout.yoffset - canvas->zoom_yofs;
	gnome_canvas_request_redraw (GNOME_CANVAS (gsheet),
				     x, y,
				     x+w, y+h);
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
	
	/* Set pixels_per_unit before the font.  The item bars look here for the number */
	gnome_canvas_set_pixels_per_unit (GNOME_CANVAS (sheet_view->sheet_view), factor);
	
	/* resize the header fonts */
	item_bar_fonts_init (ITEM_BAR (sheet_view->col_item));
	item_bar_fonts_init (ITEM_BAR (sheet_view->row_item));

	gtk_widget_set_usize (GTK_WIDGET (sheet_view->col_canvas),
			      -1, COLUMN_HEADER_HEIGHT * factor);
	gnome_canvas_set_scroll_region (GNOME_CANVAS (sheet_view->col_canvas), 0, 0,
					1000000*factor, COLUMN_HEADER_HEIGHT*factor);

	gtk_widget_set_usize (GTK_WIDGET (sheet_view->row_canvas),
			      ROW_HEADER_WIDTH * factor, -1);
	gnome_canvas_set_scroll_region (GNOME_CANVAS (sheet_view->row_canvas), 0, 0,
					ROW_HEADER_WIDTH*factor, 1200000*factor);

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
		w = ROW_HEADER_WIDTH;
		h = -1;
		gnome_canvas_set_scroll_region (GNOME_CANVAS (canvas), 0, 0, w, 1200000);
	} else {
		w = -1;
		h = COLUMN_HEADER_HEIGHT;
		gnome_canvas_set_scroll_region (GNOME_CANVAS (canvas), 0, 0, 1000000, h);
	}
	gtk_widget_set_usize (canvas, w, h);
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

/* Manages the scrollbar dimensions and paging parameters. */
void
sheet_view_scrollbar_config (SheetView const *sheet_view)
{
	GtkAdjustment *va = GTK_ADJUSTMENT (sheet_view->va);
	GtkAdjustment *ha = GTK_ADJUSTMENT (sheet_view->ha);
	GnumericSheet *gsheet = GNUMERIC_SHEET (sheet_view->sheet_view);
	Sheet         *sheet = sheet_view->sheet;
	int const last_col = gsheet->last_full_col;
	int const last_row = gsheet->last_full_row;

	va->upper = MAX (MAX (last_row,
			      sheet_view->sheet->rows.max_used),
			 sheet->cursor_row);
	va->page_size = last_row - gsheet->top_row;
	va->step_increment = va->page_increment =
	    va->page_size / 2;
	
	ha->upper = MAX (MAX (last_col,
			      sheet_view->sheet->cols.max_used),
			 sheet->cursor_col);
	ha->page_size = last_col - gsheet->top_col;
	ha->step_increment = ha->page_increment =
	    ha->page_size / 2;

	gtk_adjustment_changed (va);
	gtk_adjustment_changed (ha);
}

static void
sheet_view_size_allocate (GtkWidget *widget, GtkAllocation *alloc, SheetView *sheet_view)
{
	sheet_view_scrollbar_config (sheet_view);
}

static void
sheet_view_col_selection_changed (ItemBar *item_bar, int column, int modifiers, SheetView *sheet_view)
{	
	Sheet *sheet = sheet_view->sheet;
	
	/* Ensure that col row exists, ignore result */
	sheet_col_fetch (sheet, column);
	
	if (modifiers){
		if ((modifiers & GDK_SHIFT_MASK) && sheet->selections){
			SheetSelection *ss = sheet->selections->data;
			int start_col, end_col;
			
			start_col = MIN (ss->base.col, column);
			end_col = MAX (ss->base.col, column);
			
			sheet_selection_set (sheet,
					     start_col, 0,
					     end_col, SHEET_MAX_ROWS-1);
			return;
		}

		sheet_cursor_move (sheet, column, sheet->cursor_row, FALSE, FALSE);
		if (!(modifiers & GDK_CONTROL_MASK))
			sheet_selection_reset_only (sheet);

		sheet_selection_append_range (sheet,
					      column, 0,
					      column, 0,
					      column, SHEET_MAX_ROWS-1);
	} else
		sheet_selection_extend_to (sheet, column, SHEET_MAX_ROWS - 1);
}

static void
sheet_view_col_size_changed (ItemBar *item_bar, int col, int width, SheetView *sheet_view)
{
	Sheet *sheet = sheet_view->sheet;
	ItemBarSelectionType type;

	type = sheet_col_selection_type (sheet, col);

 	if (type == ITEM_BAR_FULL_SELECTION) {
		int i = sheet->cols.max_used;
		for (;i >= 0 ; --i) {
 			ColRowInfo *ci = sheet_col_get (sheet, i);
			if (ci == NULL)
				continue;
 
 			if (sheet_col_selection_type (sheet, ci->pos) == ITEM_BAR_FULL_SELECTION)
 				sheet_col_set_width (sheet, ci->pos, width);
 		}
	} else
 		sheet_col_set_width (sheet, col, width);
	
	gnumeric_sheet_compute_visible_ranges (GNUMERIC_SHEET (sheet_view->sheet_view));
}

static void
sheet_view_row_selection_changed (ItemBar *item_bar, int row, int modifiers, SheetView *sheet_view)
{
	Sheet *sheet = sheet_view->sheet;

	/* Ensure that the row exists, ignore result */
	sheet_row_fetch (sheet, row);
	
	if (modifiers){
		if ((modifiers & GDK_SHIFT_MASK) && sheet->selections){
			SheetSelection *ss = sheet->selections->data;
			int start_row, end_row;
			
			start_row = MIN (ss->base.row, row);
			end_row = MAX (ss->base.row, row);
			
			sheet_selection_set (sheet,
					     0, start_row,
					     SHEET_MAX_COLS-1, end_row);
			return;
		}

		sheet_cursor_move (sheet, sheet->cursor_col, row, FALSE, FALSE);
		if (!(modifiers & GDK_CONTROL_MASK))
 			sheet_selection_reset_only (sheet);
	
		sheet_selection_append_range (sheet,
					      0, row,
					      0, row,
					      SHEET_MAX_COLS-1, row);
	} else
		sheet_selection_extend_to (sheet, SHEET_MAX_COLS-1, row);
}

static void
sheet_view_row_size_changed (ItemBar *item_bar, int row, int height, SheetView *sheet_view)
{
	Sheet *sheet = sheet_view->sheet;
	ItemBarSelectionType type;

	type = sheet_row_selection_type (sheet, row);

	if (type == ITEM_BAR_FULL_SELECTION) {
		int i;
		for (i = sheet->rows.max_used; i >= 0 ; --i) {
 			ColRowInfo *ri = sheet_row_get (sheet, i);
			if (ri == NULL)
				continue;
			
			if (sheet_row_selection_type (sheet, ri->pos) == ITEM_BAR_FULL_SELECTION)
					sheet_row_set_height (sheet, ri->pos, height, TRUE);
		}
	} else
		sheet_row_set_height (sheet, row, height, TRUE);
}

static void
button_select_all (GtkWidget *the_button, SheetView *sheet_view)
{
	sheet_select_all (sheet_view->sheet);
}

static void
vertical_scroll_change (GtkAdjustment *adj, SheetView *sheet_view)
{
	if (sheet_view->tip) {
		char buffer [20 + sizeof (long) * 4];
		snprintf (buffer, sizeof (buffer), _("Row: %d"), (int) adj->value + 1);
		gtk_label_set_text (GTK_LABEL (sheet_view->tip), buffer);
	}
}

static void
horizontal_scroll_change (GtkAdjustment *adj, SheetView *sheet_view)
{
	if (sheet_view->tip) {
		char buffer [20 + sizeof (long) * 4];
		snprintf (buffer, sizeof (buffer), _("Column: %s"), col_name (adj->value));
		gtk_label_set_text (GTK_LABEL (sheet_view->tip), buffer);
	}
}

static int
horizontal_scroll_event (GtkScrollbar *scroll, GdkEvent *event, SheetView *sheet_view)
{
	if (event->type == GDK_BUTTON_PRESS){
		sheet_view->tip = gnumeric_create_tooltip ();
		horizontal_scroll_change (GTK_ADJUSTMENT (sheet_view->ha), sheet_view);
		gnumeric_position_tooltip (sheet_view->tip, 1);
		gtk_widget_show_all (gtk_widget_get_toplevel (sheet_view->tip));
	}
	else if (event->type == GDK_BUTTON_RELEASE)
	{
		GnumericSheet  *gsheet = GNUMERIC_SHEET (sheet_view->sheet_view);
		int col;
		
		gtk_widget_destroy (gtk_widget_get_toplevel (sheet_view->tip));
		sheet_view->tip = NULL;

		col = GTK_ADJUSTMENT (sheet_view->ha)->value;

		gnumeric_sheet_set_top_col (gsheet, col);
		/* NOTE : Excel does not move the cursor, just scrolls the sheet. */
	}
	
	return FALSE;
}

static int
vertical_scroll_event (GtkScrollbar *scroll, GdkEvent *event, SheetView *sheet_view)
{
	if (event->type == GDK_BUTTON_PRESS){
		sheet_view->tip = gnumeric_create_tooltip ();
		vertical_scroll_change (GTK_ADJUSTMENT (sheet_view->va), sheet_view);
		gnumeric_position_tooltip (sheet_view->tip, 0);
		gtk_widget_show_all (gtk_widget_get_toplevel (sheet_view->tip));
	}
	else if (event->type == GDK_BUTTON_RELEASE)
	{
		GnumericSheet  *gsheet = GNUMERIC_SHEET (sheet_view->sheet_view);
		int row;
		
		gtk_widget_destroy (gtk_widget_get_toplevel (sheet_view->tip));
		sheet_view->tip = NULL;

		row = GTK_ADJUSTMENT (sheet_view->va)->value;
		
		gnumeric_sheet_set_top_row (gsheet, row);
		/* NOTE : Excel does not move the cursor, just scrolls the sheet. */
	}

	return FALSE;
}

static void
sheet_view_init (SheetView *sheet_view)
{
	GtkTable *table = GTK_TABLE (sheet_view);
	
	table->homogeneous = FALSE;
	gtk_table_resize (table, 4, 4);
}

static void
sheet_view_construct (SheetView *sheet_view)
{
	GnomeCanvasGroup *root_group;
	GtkTable  *table = GTK_TABLE (sheet_view);
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
	sheet_view->object_group = GNOME_CANVAS_GROUP (
		gnome_canvas_item_new (
			root_group,
			gnome_canvas_group_get_type (),
			"x", 0.0,
			"y", 0.0,
			NULL));
		
	/* Attach the GnumericSheet */
	gtk_table_attach (table, sheet_view->sheet_view,
			  1, 2, 1, 2,
			  GTK_EXPAND | GTK_FILL | GTK_SHRINK,
			  GTK_EXPAND | GTK_FILL | GTK_SHRINK,
			  0, 0);
	gtk_widget_show (sheet_view->sheet_view);

	/*
	 * The selection group
	 */
	sheet_view->selection_group = GNOME_CANVAS_GROUP (
		gnome_canvas_item_new (
			root_group,
			gnome_canvas_group_get_type (),
			"x", 0.0,
			"y", 0.0,
			NULL));
	
	/* The select-all button */
	sheet_view->select_all = gtk_button_new ();
	GTK_WIDGET_UNSET_FLAGS (sheet_view->select_all, GTK_CAN_FOCUS);
	gtk_table_attach (table, sheet_view->select_all, 0, 1, 0, 1, GTK_FILL, GTK_FILL, 0, 0);
	gtk_signal_connect (GTK_OBJECT (sheet_view->select_all), "clicked",
			    GTK_SIGNAL_FUNC (button_select_all), sheet_view);
	
	/* Scroll bars and their adjustments */
	sheet_view->va = gtk_adjustment_new (0.0, 0.0, sheet->rows.max_used, 1.0, 1.0, 1.0);
	sheet_view->ha = gtk_adjustment_new (0.0, 0.0, sheet->cols.max_used, 1.0, 1.0, 1.0);
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

void
sheet_view_set_header_visibility (SheetView *sheet_view,
				  gboolean col_headers_visible,
				  gboolean row_headers_visible)
{
	g_return_if_fail (sheet_view != NULL);
	g_return_if_fail (IS_SHEET_VIEW (sheet_view));

	if (col_headers_visible){
		if (!GTK_WIDGET_VISIBLE (GTK_WIDGET (sheet_view->col_canvas)))
			gtk_widget_show (GTK_WIDGET (sheet_view->col_canvas));
	} else {
		if (GTK_WIDGET_VISIBLE (GTK_WIDGET (sheet_view->col_canvas)))
			gtk_widget_hide (GTK_WIDGET (sheet_view->col_canvas));
	}
	
	if (row_headers_visible){
		if (!GTK_WIDGET_VISIBLE (GTK_WIDGET (sheet_view->row_canvas)))
			gtk_widget_show (GTK_WIDGET (sheet_view->row_canvas));
	} else {
		if (GTK_WIDGET_VISIBLE (GTK_WIDGET (sheet_view->row_canvas)))
			gtk_widget_hide (GTK_WIDGET (sheet_view->row_canvas));
	}
}

/* This seems unused comment it out for now */
#if 0
static void
sheet_view_scrollbar_display (SheetView *sheet_view,
			      gboolean show_col_scrollbar,
			      gboolean show_row_scrollbar)
{
	g_return_if_fail (sheet_view != NULL);
	g_return_if_fail (IS_SHEET_VIEW (sheet_view));

	if (show_col_scrollbar){
		if (!GTK_WIDGET_VISIBLE (sheet_view->hs))
			gtk_widget_show (sheet_view->hs);
	} else {
		if (GTK_WIDGET_VISIBLE (sheet_view->hs))
			gtk_widget_hide (sheet_view->hs);
	}
	
	if (show_row_scrollbar){
		if (!GTK_WIDGET_VISIBLE (sheet_view->vs))
			gtk_widget_show (sheet_view->vs);
	} else {
		if (GTK_WIDGET_VISIBLE (sheet_view->vs))
			gtk_widget_hide (sheet_view->vs);
	}
}
#endif

GtkWidget *
sheet_view_new (Sheet *sheet)
{
	SheetView *sheet_view;

	sheet_view = gtk_type_new (sheet_view_get_type ());
	sheet_view->sheet = sheet;
	sheet_view->tip = NULL;

	sheet_view_construct (sheet_view);
	
	return GTK_WIDGET (sheet_view);
}

static void
sheet_view_destroy (GtkObject *object)
{
	SheetView *sheet_view = SHEET_VIEW (object);

	/* Add shutdown code here */
	if (sheet_view->tip)
		gtk_object_unref (GTK_OBJECT (sheet_view->tip));
	
	if (GTK_OBJECT_CLASS (sheet_view_parent_class)->destroy)
		(*GTK_OBJECT_CLASS (sheet_view_parent_class)->destroy)(object);
}

static void
sheet_view_class_init (SheetViewClass *Class)
{
	GtkObjectClass *object_class;

	object_class = (GtkObjectClass *) Class;

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
		     
void
sheet_view_hide_cursor (SheetView *sheet_view)
{
	GnumericSheet *gsheet = GNUMERIC_SHEET (sheet_view->sheet_view);

	item_cursor_set_visibility (gsheet->item_cursor, FALSE);
}

void
sheet_view_show_cursor (SheetView *sheet_view)
{
	GnumericSheet *gsheet = GNUMERIC_SHEET (sheet_view->sheet_view);

	item_cursor_set_visibility (gsheet->item_cursor, TRUE);
}

#define TRIANGLE_WIDTH 6

static GnomeCanvasPoints *
sheet_view_comment_get_points (SheetView *sheet_view, int col, int row)
{
	GnomeCanvasPoints *points;
	int x, y, i;

	points = gnome_canvas_points_new (3);

	x = sheet_col_get_distance (sheet_view->sheet, 0, col+1);
	y = 1+sheet_row_get_distance (sheet_view->sheet, 0, row);

	points->coords [0] = x - TRIANGLE_WIDTH;
	points->coords [1] = y;
	points->coords [2] = x;
	points->coords [3] = y;
	points->coords [4] = x;
	points->coords [5] = y + TRIANGLE_WIDTH;

	for (i = 0; i < 3; i++){
		gnome_canvas_c2w (GNOME_CANVAS (sheet_view->sheet_view),
				  points->coords [i*2],
				  points->coords [i*2+1],
				  &(points->coords [i*2]),
				  &(points->coords [i*2+1]));
	}
	return points;
}

GnomeCanvasItem *
sheet_view_comment_create_marker (SheetView *sheet_view, int col, int row)
{
	GnomeCanvasPoints *points;
	GnomeCanvasGroup *group;
	GnomeCanvasItem *i;
	
	g_return_val_if_fail (sheet_view != NULL, NULL);
	g_return_val_if_fail (IS_SHEET_VIEW (sheet_view), NULL);

	group = GNOME_CANVAS_GROUP (GNOME_CANVAS (sheet_view->sheet_view)->root);
	points = sheet_view_comment_get_points (sheet_view, col, row);

	i = gnome_canvas_item_new (
		group, gnome_canvas_polygon_get_type (),
		"points",     points,
		"fill_color", "red",
		NULL);

	return i;
}

void
sheet_view_comment_relocate (SheetView *sheet_view, int col, int row, GnomeCanvasItem *o)
{
	GnomeCanvasPoints *points;

	g_return_if_fail (sheet_view != NULL);
	g_return_if_fail (IS_SHEET_VIEW (sheet_view));
	g_return_if_fail (o != NULL);
	g_return_if_fail (GNOME_IS_CANVAS_ITEM (o));

	points = sheet_view_comment_get_points (sheet_view, col, row);

	gnome_canvas_item_set (o, "points", points, NULL);
}

void
sheet_view_selection_unant (SheetView *sheet_view)
{
	GList *l;

	g_return_if_fail (sheet_view != NULL);
	g_return_if_fail (IS_SHEET_VIEW (sheet_view));

	if (sheet_view->anted_cursors == NULL)
		return;

	for (l = sheet_view->anted_cursors; l; l = l->next)
		gtk_object_destroy (GTK_OBJECT (l->data));

	g_list_free (sheet_view->anted_cursors);
	sheet_view->anted_cursors = NULL;
}

void
sheet_view_selection_ant (SheetView *sheet_view)
{
	GnomeCanvasGroup *group;
	ItemGrid *grid;
	GList *l;
	
	g_return_if_fail (sheet_view != NULL);
	g_return_if_fail (IS_SHEET_VIEW (sheet_view));
	
	if (sheet_view->anted_cursors)
		sheet_view_selection_unant (sheet_view);

	group = sheet_view->selection_group;
	grid = GNUMERIC_SHEET (sheet_view->sheet_view)->item_grid;
	
	for (l = sheet_view->sheet->selections; l; l = l->next){
		SheetSelection *ss = l->data;
		ItemCursor *item_cursor;
		
		item_cursor = ITEM_CURSOR (gnome_canvas_item_new (
			group, item_cursor_get_type (),
			"Sheet", sheet_view->sheet,
			"Grid",  grid,
			"Style", ITEM_CURSOR_ANTED,
			NULL));
		item_cursor_set_bounds (
			item_cursor,
			ss->user.start.col, ss->user.start.row,
			ss->user.end.col, ss->user.end.row);

		sheet_view->anted_cursors = g_list_prepend (sheet_view->anted_cursors, item_cursor);
	}
}


#if 0
#ifdef ENABLE_BONOBO
void
sheet_view_insert_object (SheetView *sheet_view, GnomeObjectClient *object)
{
/*	GtkWidget *view;*/

	/*
	 * Commented out because the new_view api changed and it isn't
	 * used anyways.
	 */
	   
	/* view = gnome_bonobo_object_new_view (object); */
	g_warning ("Stick this into the SheetView");
}
#endif
#endif
