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
#include "workbook.h"
#include "cell.h"
#include "selection.h"
#include "style.h"
#include "sheet-object.h"
#include "item-cursor.h"
#include "gnumeric-util.h"
#include "parse-util.h"
#include "selection.h"
#include "application.h"
#include "cellspan.h"
#include "cmd-edit.h"
#include "commands.h"

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

/*
 * Redraw selected range, do not honour spans
 */
void
sheet_view_redraw_cell_region (SheetView *sheet_view,
			       int start_col, int start_row,
			       int end_col, int end_row)
{
	GnumericSheet *gsheet;
	GnomeCanvas *canvas;
	Sheet *sheet = sheet_view->sheet;
	int x, y, w, h;

	g_return_if_fail (sheet_view != NULL);
	g_return_if_fail (IS_SHEET_VIEW (sheet_view));

	gsheet = GNUMERIC_SHEET (sheet_view->sheet_view);
	g_return_if_fail (GNUMERIC_IS_SHEET (gsheet));
	canvas = GNOME_CANVAS (gsheet);

	if ((end_col < gsheet->col.first) ||
	    (end_row < gsheet->row.first) ||
	    (start_col > gsheet->col.last_visible) ||
	    (start_row > gsheet->row.last_visible))
		return;

	/* Only draw those regions that are visible */
	start_col = MAX (gsheet->col.first, start_col);
	start_row = MAX (gsheet->row.first, start_row);
	end_col =  MIN (gsheet->col.last_visible, end_col);
	end_row =  MIN (gsheet->row.last_visible, end_row);

	x = sheet_col_get_distance_pixels (sheet, gsheet->col.first, start_col);
	y = sheet_row_get_distance_pixels (sheet, gsheet->row.first, start_row);
	w = sheet_col_get_distance_pixels (sheet, start_col, end_col+1);
	h = sheet_row_get_distance_pixels (sheet, start_row, end_row+1);

	x += canvas->layout.xoffset - canvas->zoom_xofs;
	y += canvas->layout.yoffset - canvas->zoom_yofs;

#if 0
	fprintf (stderr, "%s%d:", col_name(min_col), first_row+1);
	fprintf (stderr, "%s%d\n", col_name(max_col), last_row+1);
#endif

	/* redraw a border of 1 pixel around the region to handle thick borders
	 * NOTE the 2nd coordinates are excluded so add 1 extra (+1border +1include) */
	gnome_canvas_request_redraw (GNOME_CANVAS (gsheet),
				     x-1, y-1,
				     x+w+1+1, y+h+1+1);
}

void
sheet_view_redraw_headers (SheetView *sheet_view,
			   gboolean const col, gboolean const row,
			   Range const * r /* optional == NULL */)
{
	GnumericSheet *gsheet;
	g_return_if_fail (sheet_view != NULL);
	g_return_if_fail (IS_SHEET_VIEW (sheet_view));

	gsheet = GNUMERIC_SHEET (sheet_view->sheet_view);

	if (col) {
		int left = 0, right = INT_MAX-1;
		if (r != NULL) {
			int const size = r->end.col - r->start.col;
/* A rough heuristic guess of the number of when the
 * trade off point of redrawing all vs calculating the
 * redraw size is crossed */
#define COL_HEURISTIC	20
			if (-COL_HEURISTIC < size && size < COL_HEURISTIC) {
				left = gsheet->col_offset.first +
				    sheet_col_get_distance_pixels (sheet_view->sheet,
							    gsheet->col.first, r->start.col);
				right = left +
				    sheet_col_get_distance_pixels (sheet_view->sheet,
							    r->start.col, r->end.col+1);
			}
		}
		/* Request excludes the far coordinate.  Add 1 to include them */
		gnome_canvas_request_redraw (
			GNOME_CANVAS (sheet_view->col_canvas),
			left, 0, right+1, INT_MAX);
	}

	if (row) {
		int top = 0, bottom = INT_MAX-1;
		if (r != NULL) {
			int const size = r->end.row - r->start.row;
/* A rough heuristic guess of the number of when the
 * trade off point of redrawing all vs calculating the
 * redraw size is crossed */
#define ROW_HEURISTIC	50
			if (-ROW_HEURISTIC < size && size < ROW_HEURISTIC) {
				top = gsheet->row_offset.first +
				    sheet_row_get_distance_pixels (sheet_view->sheet,
							    gsheet->row.first, r->start.row);
				bottom = top +
				    sheet_row_get_distance_pixels (sheet_view->sheet,
							    r->start.row, r->end.row+1);
			}
		}
		/* Request excludes the far coordinate.  Add 1 to include them */
		gnome_canvas_request_redraw (
			GNOME_CANVAS (sheet_view->row_canvas),
			0, top, INT_MAX, bottom+1);
	}
}

void
sheet_view_update_cursor_pos (SheetView *sheet_view)
{
	GList *l;
	GnumericSheet *gsheet;

	g_return_if_fail (sheet_view != NULL);
	g_return_if_fail (IS_SHEET_VIEW (sheet_view));

	gsheet = GNUMERIC_SHEET (sheet_view->sheet_view);

	/* Repsition the selection cursor */
	item_cursor_reposition (gsheet->item_cursor);

	/* Reposition the animated cursors */
	for (l = sheet_view->anted_cursors; l; l = l->next)
		item_cursor_reposition (ITEM_CURSOR (l->data));
}

void
sheet_view_set_zoom_factor (SheetView *sheet_view, double factor)
{
	GnumericSheet *gsheet;
	ItemBar *col_item, *row_item;
	int h, w;

	g_return_if_fail (sheet_view != NULL);
	g_return_if_fail (IS_SHEET_VIEW (sheet_view));

	gsheet = GNUMERIC_SHEET (sheet_view->sheet_view);
	col_item = ITEM_BAR (sheet_view->col_item);
	row_item = ITEM_BAR (sheet_view->row_item);

	/* Set pixels_per_unit before the font.  The item bars look here for the number */
	gnome_canvas_set_pixels_per_unit (GNOME_CANVAS (gsheet), factor);

	/* resize the header fonts */
	item_bar_fonts_init (col_item);
	item_bar_fonts_init (row_item);

	/*
	 * Use the size of the bold header font to size the free dimensions
	 * No need to zoom, the size of the font takes that into consideration.
	 */

	/* 2 pixels above and below */
	h = 2 + 2 + style_font_get_height (col_item->bold_font);
	/* 5 pixels left and right plus the width of the widest string I can think of */
	w = 5 + 5 + gdk_string_width (style_font_gdk_font (col_item->bold_font), "88888");

	gtk_widget_set_usize (GTK_WIDGET (sheet_view->col_canvas), -1, h);
	gnome_canvas_set_scroll_region (GNOME_CANVAS (sheet_view->col_canvas), 0, 0,
					1000000*factor, h);

	gtk_widget_set_usize (GTK_WIDGET (sheet_view->row_canvas), w, -1);
	gnome_canvas_set_scroll_region (GNOME_CANVAS (sheet_view->row_canvas), 0, 0,
					w, 1200000*factor);

	/* Recalibrate the starting offsets */
	gsheet->col_offset.first =
	    sheet_col_get_distance_pixels (sheet_view->sheet, 0, gsheet->col.first);
	gsheet->row_offset.first =
	    sheet_row_get_distance_pixels (sheet_view->sheet, 0, gsheet->row.first);

	if (GTK_WIDGET_REALIZED (gsheet))
		/* Ensure that the current cell remains visible when we zoom */
		gnumeric_sheet_make_cell_visible
			(gsheet,
			 sheet_view->sheet->cursor.edit_pos.col,
			 sheet_view->sheet->cursor.edit_pos.row,
			 TRUE);

	sheet_view_update_cursor_pos (sheet_view);
}

static void
canvas_bar_realized (GtkWidget *widget, gpointer data)
{
	gdk_window_set_back_pixmap (GTK_LAYOUT (widget)->bin_window, NULL, FALSE);
}

static GnomeCanvas *
new_canvas_bar (SheetView *sheet_view, GtkOrientation o, GnomeCanvasItem **itemp)
{
	GtkWidget *canvas =
	    gnome_canvas_new ();
	GnomeCanvasGroup *group =
	    GNOME_CANVAS_GROUP (GNOME_CANVAS (canvas)->root);
	GnomeCanvasItem *item =
	    gnome_canvas_item_new (group,
				   item_bar_get_type (),
				   "ItemBar::SheetView", sheet_view,
				   "ItemBar::Orientation", o,
				   NULL);

	gtk_signal_connect (GTK_OBJECT (canvas), "realize",
			    (GtkSignalFunc) canvas_bar_realized,
			    NULL);

	*itemp = item;
	gtk_widget_show (canvas);

	return GNOME_CANVAS(canvas);
}

/* Manages the scrollbar dimensions and paging parameters. */
void
sheet_view_scrollbar_config (SheetView const *sheet_view)
{
	GtkAdjustment *va = GTK_ADJUSTMENT (sheet_view->va);
	GtkAdjustment *ha = GTK_ADJUSTMENT (sheet_view->ha);
	GnumericSheet *gsheet = GNUMERIC_SHEET (sheet_view->sheet_view);
	Sheet         *sheet = sheet_view->sheet;
	int const last_col = gsheet->col.last_full;
	int const last_row = gsheet->row.last_full;

	va->upper = MAX (MAX (last_row,
			      sheet_view->sheet->rows.max_used),
			 MAX (sheet->cursor.move_corner.row,
			      sheet->cursor.base_corner.row));
	va->page_size = last_row - gsheet->row.first;
	va->value = gsheet->row.first;
	va->step_increment = va->page_increment =
	    va->page_size / 2;

	ha->upper = MAX (MAX (last_col,
			      sheet_view->sheet->cols.max_used),
			 MAX (sheet->cursor.move_corner.col,
			      sheet->cursor.base_corner.col));
	ha->page_size = last_col - gsheet->col.first;
	ha->value = gsheet->col.first;
	ha->step_increment = ha->page_increment =
	    ha->page_size / 2;

	gtk_adjustment_changed (va);
	gtk_adjustment_changed (ha);
}

#if 0
/*
 * sheet_view_make_edit_pos_visible
 * @sheet_view  Sheet view
 *
 * Make the cell at the edit position visible.
 *
 * To be called from the "size_allocate" signal handler when the geometry of a
 * new sheet view has been configured. 
 */
static void
sheet_view_make_edit_pos_visible (SheetView const *sheet_view)
{
	GnumericSheet *gsheet = GNUMERIC_SHEET (sheet_view->sheet_view);

	gnumeric_sheet_make_cell_visible
		(gsheet,
		 sheet_view->sheet->cursor.edit_pos.col,
		 sheet_view->sheet->cursor.edit_pos.row,
		 TRUE);

}
#endif

static void
sheet_view_size_allocate (GtkWidget *widget, GtkAllocation *alloc, SheetView *sheet_view)
{
#if 0
	/* FIXME
	 * When a new sheet is added this is called and if the edit cell was not visible
	 * we change the scroll position even though to the user the size did not change
	 * and there is no reason for the scrolling to jump.
	 *
	 * Can we somehow do this only if the edit pos was visible initially ?
	 */
	sheet_view_make_edit_pos_visible (sheet_view);
#endif
	sheet_view_scrollbar_config (sheet_view);
}

static void
sheet_view_col_selection_changed (ItemBar *item_bar, int col, int modifiers, SheetView *sheet_view)
{
	Sheet *sheet = sheet_view->sheet;
	GnumericSheet *gsheet = GNUMERIC_SHEET (sheet_view->sheet_view);

	if (modifiers){
		if ((modifiers & GDK_SHIFT_MASK) && sheet->selections){
			SheetSelection *ss = sheet->selections->data;
			int start_col, end_col;

			start_col = MIN (ss->user.start.col, col);
			end_col = MAX (ss->user.end.col, col);

			sheet_selection_set (sheet,
					     start_col, gsheet->row.first,
					     start_col, 0,
					     end_col, SHEET_MAX_ROWS-1);
			return;
		}

		if (!(modifiers & GDK_CONTROL_MASK))
			sheet_selection_reset_only (sheet);

		sheet_selection_add_range (sheet,
					   col, gsheet->row.first,
					   col, 0,
					   col, SHEET_MAX_ROWS-1);
	} else
		sheet_selection_extend_to (sheet, col, SHEET_MAX_ROWS - 1);

	/* The edit pos, and the selection may have changed */
	sheet_update (sheet);
}

struct closure_colrow_resize {
	gboolean	 is_cols;
	ColRowIndexList *selection;
};

static gboolean
cb_colrow_resize (Sheet *sheet, Range const *r, gpointer user_data)
{
	struct closure_colrow_resize *info = user_data;
	int first, last;

	if (info->is_cols) {
		if (r->start.row != 0 || r->end.row < SHEET_MAX_ROWS - 1)
			return TRUE;
		first = r->start.col;
		last = r->end.col;
	} else {
		if (r->start.col != 0 || r->end.col < SHEET_MAX_COLS - 1)
			return TRUE;
		first = r->start.row;
		last = r->end.row;
	}

	info->selection = col_row_get_index_list (first, last, info->selection);
	return TRUE;
}

static void
sheet_view_col_size_changed (ItemBar *item_bar, int col, int new_width,
			     SheetView *sheet_view)
{
	Sheet		*sheet = sheet_view->sheet;
	ColRowIndexList *selection = NULL;
	ItemBarSelectionType const type = sheet_col_selection_type (sheet, col);

	/*
	 * If the column that changed size is completely selected (top to
	 * bottom) then resize all other columns which are fully selected too.
	 */
 	if (type == ITEM_BAR_FULL_SELECTION) {
		struct closure_colrow_resize	closure;
		closure.is_cols = TRUE;
		closure.selection = NULL;
		selection_foreach_range (sheet, &cb_colrow_resize, &closure);
		selection = closure.selection;
	} else
		selection = col_row_get_index_list (col, col, NULL);

	cmd_resize_row_col (workbook_command_context_gui (sheet->workbook),
			    sheet, TRUE, selection, new_width);
}

static void
sheet_view_row_selection_changed (ItemBar *item_bar, int row, int modifiers, SheetView *sheet_view)
{
	Sheet *sheet = sheet_view->sheet;
	GnumericSheet *gsheet = GNUMERIC_SHEET (sheet_view->sheet_view);

	if (modifiers){
		if ((modifiers & GDK_SHIFT_MASK) && sheet->selections){
			SheetSelection *ss = sheet->selections->data;
			int start_row, end_row;

			start_row = MIN (ss->user.start.row, row);
			end_row = MAX (ss->user.end.row, row);

			sheet_selection_set (sheet,
					     gsheet->col.first, start_row,
					     0, start_row,
					     SHEET_MAX_COLS-1, end_row);
			return;
		}

		if (!(modifiers & GDK_CONTROL_MASK))
 			sheet_selection_reset_only (sheet);

		sheet_selection_add_range (sheet,
					   gsheet->col.first, row,
					   0, row,
					   SHEET_MAX_COLS-1, row);
	} else
		sheet_selection_extend_to (sheet, SHEET_MAX_COLS-1, row);

	/* The edit pos, and the selection may have changed */
	sheet_update (sheet);
}

static void
sheet_view_row_size_changed (ItemBar *item_bar, int row, int new_height,
			     SheetView *sheet_view)
{
	Sheet		*sheet = sheet_view->sheet;
	ColRowIndexList *selection = NULL;
	ItemBarSelectionType const type = sheet_row_selection_type (sheet, row);

	/*
	 * If the row that changed size is completely selected (left to
	 * right) then resize all other rows which are fully selected too.
	 */
 	if (type == ITEM_BAR_FULL_SELECTION) {
		struct closure_colrow_resize	closure;
		closure.is_cols = FALSE;
		closure.selection = NULL;
		selection_foreach_range (sheet, &cb_colrow_resize, &closure);
		selection = closure.selection;
	} else
		selection = col_row_get_index_list (row, row, NULL);

	cmd_resize_row_col (workbook_command_context_gui (sheet->workbook),
			    sheet, FALSE, selection, new_height);
}

static void
button_select_all (GtkWidget *the_button, SheetView *sheet_view)
{
	cmd_select_all (sheet_view->sheet);
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

	} else if (event->type == GDK_BUTTON_RELEASE) {
		GnumericSheet  *gsheet = GNUMERIC_SHEET (sheet_view->sheet_view);
		int col;

		/* A button release can be generated without a press by people
		 * with mouse wheels */
		if (sheet_view->tip) {
			gtk_widget_destroy (gtk_widget_get_toplevel (sheet_view->tip));
			sheet_view->tip = NULL;
		}

		col = GTK_ADJUSTMENT (sheet_view->ha)->value;

		gnumeric_sheet_set_left_col (gsheet, col);
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

	} else if (event->type == GDK_BUTTON_RELEASE) {
		GnumericSheet  *gsheet = GNUMERIC_SHEET (sheet_view->sheet_view);
		int row;

		/* A button release can be generated without a press by people
		 * with mouse wheels */
		if (sheet_view->tip) {
			gtk_widget_destroy (gtk_widget_get_toplevel (sheet_view->tip));
			sheet_view->tip = NULL;
		}

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

	sheet_view->slide_handler = NULL;
	sheet_view->slide_data = NULL;
	sheet_view->sliding = -1;
	table->homogeneous = FALSE;
	gtk_table_resize (table, 4, 4);
}

static void
sheet_view_construct (SheetView *sheet_view)
{
	GnomeCanvasGroup *root_group;
	GtkTable  *table = GTK_TABLE (sheet_view);
	Sheet *sheet = sheet_view->sheet;
	int i;

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

	for (i = 8; i-- > 0 ; )
		sheet_view->control_points[i] = NULL;

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
	sheet_view->select_all_btn = gtk_button_new ();
	GTK_WIDGET_UNSET_FLAGS (sheet_view->select_all_btn, GTK_CAN_FOCUS);
	gtk_table_attach (table, sheet_view->select_all_btn, 0, 1, 0, 1, GTK_FILL, GTK_FILL, 0, 0);
	gtk_signal_connect (GTK_OBJECT (sheet_view->select_all_btn), "clicked",
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
	sheet_view_set_zoom_factor (sheet_view, 1.);
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

	/* FIXME : Should we be pedantic and
	 * 1) clear the control points
	 * 2) remove ourselves from the sheets list of views ?
	 */
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

	x = sheet_col_get_distance_pixels (sheet_view->sheet, 0, col+1);
	y = 1+sheet_row_get_distance_pixels (sheet_view->sheet, 0, row);

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
	gnome_canvas_points_free (points);
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
			"SheetView", sheet_view,
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

void
sheet_view_adjust_preferences (SheetView *sheet_view)
{
	Sheet *sheet = sheet_view->sheet;
	Workbook *wb = sheet->workbook;

	if (sheet->show_col_header)
		gtk_widget_show (GTK_WIDGET (sheet_view->col_canvas));
	else
		gtk_widget_hide (GTK_WIDGET (sheet_view->col_canvas));

	if (sheet->show_row_header)
		gtk_widget_show (GTK_WIDGET (sheet_view->row_canvas));
	else
		gtk_widget_hide (GTK_WIDGET (sheet_view->row_canvas));

	if (sheet->show_col_header && sheet->show_row_header)
		gtk_widget_show (sheet_view->select_all_btn);
	else
		gtk_widget_hide (sheet_view->select_all_btn);

	if (wb->show_horizontal_scrollbar)
		gtk_widget_show (sheet_view->hs);
	else
		gtk_widget_hide (sheet_view->hs);

	if (wb->show_vertical_scrollbar)
		gtk_widget_show (sheet_view->vs);
	else
		gtk_widget_hide (sheet_view->vs);
}

StyleFont *
sheet_view_get_style_font (const Sheet *sheet, MStyle const * const mstyle)
{
	/* Scale the font size by the average scaling factor for the
	 * display.  72dpi is base size
	 */
	double const res  = application_dpi_to_pixels ();
	double zoom;

	/*
	 * If sheet is NULL we default to 1
	 */
	if (sheet)
		zoom = sheet->last_zoom_factor_used;
	else
		zoom = 1;
		
		
	return mstyle_get_font (mstyle, zoom * res);
}

/*****************************************************************************/

void
sheet_view_stop_sliding (SheetView *sheet_view)
{
	if (sheet_view->sliding == -1)
		return;

	gtk_timeout_remove (sheet_view->sliding);
	sheet_view->slide_handler = NULL;
	sheet_view->slide_data = NULL;
	sheet_view->sliding = -1;
}

static gint
sheet_view_sliding_callback (gpointer data)
{
	SheetView *sheet_view = data;
	GnumericSheet *gsheet = GNUMERIC_SHEET (sheet_view->sheet_view);
	gboolean change = FALSE;
	int col, row;

	col = sheet_view->sliding_col;
	row = sheet_view->sliding_row;

	if (sheet_view->sliding_x < 0){
		if (gsheet->col.first){
			change = TRUE;
			if (sheet_view->sliding_x >= -50)
				col = 1;
			else if (sheet_view->sliding_x >= -100)
				col = 3;
			else
				col = 20;
			col = gsheet->col.first - col;
			if (col < 0)
				col = 0;
		} else
			col = 0;
	}

	if (sheet_view->sliding_x > 0){
		if (gsheet->col.last_full < SHEET_MAX_COLS-1){
			change = TRUE;
			if (sheet_view->sliding_x <= 50)
				col = 1;
			else if (sheet_view->sliding_x <= 100)
				col = 3;
			else
				col = 20;
			col = gsheet->col.last_visible + col;
			if (col >= SHEET_MAX_COLS)
				col = SHEET_MAX_COLS-1;
		} else
			col = SHEET_MAX_COLS-1;
	}

	if (sheet_view->sliding_y < 0){
		if (gsheet->row.first){
			change = TRUE;
			if (sheet_view->sliding_y >= -30)
				row = 1;
			else if (sheet_view->sliding_y >= -60)
				row = 25;
			else if (sheet_view->sliding_y >= -100)
				row = 250;
			else
				row = 5000;
			row = gsheet->row.first - row;
			if (row < 0)
				row = 0;
		} else
			row = 0;
	}
	if (sheet_view->sliding_y > 0){
		if (gsheet->row.last_full < SHEET_MAX_ROWS-1){
			change = TRUE;
			if (sheet_view->sliding_y <= 30)
				row = 1;
			else if (sheet_view->sliding_y <= 60)
				row = 25;
			else if (sheet_view->sliding_y <= 100)
				row = 250;
			else
				row = 5000;
			row = gsheet->row.last_visible + row;
			if (row >= SHEET_MAX_ROWS)
				row = SHEET_MAX_ROWS-1;
		} else
			row = SHEET_MAX_ROWS-1;
	}

	if (!change) {
		sheet_view_stop_sliding (sheet_view);
		return TRUE;
	}

	if (sheet_view->slide_handler == NULL ||
	    (*sheet_view->slide_handler) (sheet_view, col, row, sheet_view->slide_data))
		gnumeric_sheet_make_cell_visible (gsheet, col, row, FALSE);

	return TRUE;
}

gboolean
sheet_view_start_sliding (SheetView *sheet_view,
			  SheetViewSlideHandler slide_handler,
			  gpointer user_data,
			  int col, int row, int dx, int dy)
{
	GnumericSheet *gsheet = GNUMERIC_SHEET (sheet_view->sheet_view);

	/* Do not slide off the edge */
	if (((dx == 0) ||
	     (dx < 0 && gsheet->col.first == 0) ||
	     (dx > 0 && gsheet->col.last_full >= SHEET_MAX_COLS-1)) &&
	    ((dy == 0) ||
	     (dy < 0 && gsheet->row.first == 0) ||
	     (dy > 0 && gsheet->row.last_full >= SHEET_MAX_ROWS-1))) {
		sheet_view_stop_sliding (sheet_view);
		return FALSE;
	}

	sheet_view->slide_handler = slide_handler;
	sheet_view->slide_data = user_data;
	sheet_view->sliding_x = dx;
	sheet_view->sliding_y = dy;
	sheet_view->sliding_col = col;
	sheet_view->sliding_row = row;

	if (sheet_view->sliding == -1) {
		sheet_view_sliding_callback (sheet_view);

		sheet_view->sliding = gtk_timeout_add (
			200, sheet_view_sliding_callback, sheet_view);
	}
	return TRUE;
}

#if 0
#ifdef ENABLE_BONOBO
void
sheet_view_insert_object (SheetView *sheet_view, BonoboObjectClient *object)
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
