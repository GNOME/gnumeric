/*
 * sheet-control-gui.c: Implements a graphic control for a sheet.
 *
 * Author:
 *    Miguel de Icaza (miguel@kernel.org)
 *    Jody Goldberg (jgoldberg@home.com)
 */
#include <config.h>

#include "sheet-control-gui.h"
#include "item-bar.h"
#include "gnumeric-sheet.h"
#include "workbook.h"
#include "workbook-view.h"
#include "workbook-cmd-format.h"
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
#include "clipboard.h"
#include "dialogs.h"

#include <gdk/gdkkeysyms.h>
#include <gdk/gdkx.h>
#include <string.h>

static GtkTableClass *sheet_view_parent_class;

void
sheet_view_redraw_all (SheetControlGUI *scg)
{
	g_return_if_fail (IS_SHEET_CONTROL_GUI (scg));

	gnome_canvas_request_redraw (
		GNOME_CANVAS (scg->canvas),
		0, 0, INT_MAX, INT_MAX);
	gnome_canvas_request_redraw (
		GNOME_CANVAS (scg->col_canvas),
		0, 0, INT_MAX, INT_MAX);
	gnome_canvas_request_redraw (
		GNOME_CANVAS (scg->row_canvas),
		0, 0, INT_MAX, INT_MAX);
}

/*
 * Redraw selected range, do not honour spans
 */
void
sheet_view_redraw_cell_region (SheetControlGUI *scg,
			       int start_col, int start_row,
			       int end_col, int end_row)
{
	GnumericSheet *gsheet;
	GnomeCanvas *canvas;
	Sheet *sheet = scg->sheet;
	int x, y, w, h;

	g_return_if_fail (IS_SHEET_CONTROL_GUI (scg));

	gsheet = GNUMERIC_SHEET (scg->canvas);
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
sheet_view_redraw_headers (SheetControlGUI *scg,
			   gboolean const col, gboolean const row,
			   Range const * r /* optional == NULL */)
{
	GnumericSheet *gsheet;

	g_return_if_fail (IS_SHEET_CONTROL_GUI (scg));

	gsheet = GNUMERIC_SHEET (scg->canvas);

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
				    sheet_col_get_distance_pixels (scg->sheet,
							    gsheet->col.first, r->start.col);
				right = left +
				    sheet_col_get_distance_pixels (scg->sheet,
							    r->start.col, r->end.col+1);
			}
		}
		/* Request excludes the far coordinate.  Add 1 to include them */
		gnome_canvas_request_redraw (
			GNOME_CANVAS (scg->col_canvas),
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
				    sheet_row_get_distance_pixels (scg->sheet,
							    gsheet->row.first, r->start.row);
				bottom = top +
				    sheet_row_get_distance_pixels (scg->sheet,
							    r->start.row, r->end.row+1);
			}
		}
		/* Request excludes the far coordinate.  Add 1 to include them */
		gnome_canvas_request_redraw (
			GNOME_CANVAS (scg->row_canvas),
			0, top, INT_MAX, bottom+1);
	}
}

void
sheet_view_update_cursor_pos (SheetControlGUI *scg)
{
	GList *l;
	GnumericSheet *gsheet;

	g_return_if_fail (IS_SHEET_CONTROL_GUI (scg));

	gsheet = GNUMERIC_SHEET (scg->canvas);

	/* Repsition the selection cursor */
	item_cursor_reposition (gsheet->item_cursor);

	/* Reposition the animated cursors */
	for (l = scg->anted_cursors; l; l = l->next)
		item_cursor_reposition (ITEM_CURSOR (l->data));
}

void
sheet_view_set_zoom_factor (SheetControlGUI *scg, double factor)
{
	GnumericSheet *gsheet;
	ItemBar *col_item, *row_item;
	int h, w;

	g_return_if_fail (IS_SHEET_CONTROL_GUI (scg));

	gsheet = GNUMERIC_SHEET (scg->canvas);
	col_item = ITEM_BAR (scg->col_item);
	row_item = ITEM_BAR (scg->row_item);

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

	gtk_widget_set_usize (GTK_WIDGET (scg->col_canvas), -1, h);
	gnome_canvas_set_scroll_region (GNOME_CANVAS (scg->col_canvas), 0, 0,
					GNUMERIC_SHEET_FACTOR_X * factor, h);

	gtk_widget_set_usize (GTK_WIDGET (scg->row_canvas), w, -1);
	gnome_canvas_set_scroll_region (GNOME_CANVAS (scg->row_canvas), 0, 0,
					w, GNUMERIC_SHEET_FACTOR_Y * factor);

	/* Recalibrate the starting offsets */
	gsheet->col_offset.first =
	    sheet_col_get_distance_pixels (scg->sheet, 0, gsheet->col.first);
	gsheet->row_offset.first =
	    sheet_row_get_distance_pixels (scg->sheet, 0, gsheet->row.first);

	if (GTK_WIDGET_REALIZED (gsheet))
		/* Ensure that the current cell remains visible when we zoom */
		gnumeric_sheet_make_cell_visible
			(gsheet,
			 scg->sheet->cursor.edit_pos.col,
			 scg->sheet->cursor.edit_pos.row,
			 TRUE);

	sheet_view_update_cursor_pos (scg);
}

static void
canvas_bar_realized (GtkWidget *widget, gpointer data)
{
	gdk_window_set_back_pixmap (GTK_LAYOUT (widget)->bin_window, NULL, FALSE);
}

static GnomeCanvas *
new_canvas_bar (SheetControlGUI *scg, GtkOrientation o, GnomeCanvasItem **itemp)
{
	GtkWidget *canvas =
	    gnome_canvas_new ();
	GnomeCanvasGroup *group =
	    GNOME_CANVAS_GROUP (GNOME_CANVAS (canvas)->root);
	GnomeCanvasItem *item =
	    gnome_canvas_item_new (group,
				   item_bar_get_type (),
				   "ItemBar::SheetControlGUI", scg,
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
sheet_view_scrollbar_config (SheetControlGUI const *scg)
{
	GtkAdjustment *va = GTK_ADJUSTMENT (scg->va);
	GtkAdjustment *ha = GTK_ADJUSTMENT (scg->ha);
	GnumericSheet *gsheet = GNUMERIC_SHEET (scg->canvas);
	Sheet         *sheet = scg->sheet;
	int const last_col = gsheet->col.last_full;
	int const last_row = gsheet->row.last_full;

	va->upper = MAX (MAX (last_row,
			      scg->sheet->rows.max_used),
			 MAX (sheet->cursor.move_corner.row,
			      sheet->cursor.base_corner.row));
	va->page_size = last_row - gsheet->row.first;
	va->value = gsheet->row.first;
	va->step_increment = va->page_increment =
	    va->page_size / 2;

	ha->upper = MAX (MAX (last_col,
			      scg->sheet->cols.max_used),
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
 * @scg  Sheet view
 *
 * Make the cell at the edit position visible.
 *
 * To be called from the "size_allocate" signal handler when the geometry of a
 * new sheet view has been configured.
 */
static void
sheet_view_make_edit_pos_visible (SheetControlGUI const *scg)
{
	GnumericSheet *gsheet = GNUMERIC_SHEET (scg->scg);

	gnumeric_sheet_make_cell_visible
		(gsheet,
		 scg->sheet->cursor.edit_pos.col,
		 scg->sheet->cursor.edit_pos.row,
		 TRUE);

}
#endif

static void
sheet_view_size_allocate (GtkWidget *widget, GtkAllocation *alloc, SheetControlGUI *scg)
{
#if 0
	/* FIXME
	 * When a new sheet is added this is called and if the edit cell was not visible
	 * we change the scroll position even though to the user the size did not change
	 * and there is no reason for the scrolling to jump.
	 *
	 * Can we somehow do this only if the edit pos was visible initially ?
	 */
	sheet_view_make_edit_pos_visible (scg);
#endif
	sheet_view_scrollbar_config (scg);
}

static void
sheet_view_col_selection_changed (ItemBar *item_bar, int col, int modifiers, SheetControlGUI *scg)
{
	Sheet *sheet = scg->sheet;
	GnumericSheet *gsheet = GNUMERIC_SHEET (scg->canvas);

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

static void
sheet_view_col_size_changed (ItemBar *item_bar, int col, int new_size_pixels,
			     SheetControlGUI *scg)
{
	Sheet *sheet = scg->sheet;

	/*
	 * If all cols in the selection are completely selected (top to bottom)
	 * then resize all of them, otherwise just resize the selected col.
	 */
 	if (!sheet_selection_full_cols (sheet, col)) {
		ColRowIndexList *sel = col_row_get_index_list (col, col, NULL);
		cmd_resize_row_col (WORKBOOK_CONTROL (scg->wbcg),
				    sheet, TRUE, sel, new_size_pixels);
	} else
		workbook_cmd_format_column_width (WORKBOOK_CONTROL (scg->wbcg),
						  sheet, new_size_pixels);
}

static void
sheet_view_row_selection_changed (ItemBar *item_bar, int row, int modifiers, SheetControlGUI *scg)
{
	Sheet *sheet = scg->sheet;
	GnumericSheet *gsheet = GNUMERIC_SHEET (scg->canvas);

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
sheet_view_row_size_changed (ItemBar *item_bar, int row, int new_size_pixels,
			     SheetControlGUI *scg)
{
	Sheet *sheet = scg->sheet;

	/*
	 * If all rows in the selection are completely selected (left to right)
	 * then resize all of them, otherwise just resize the selected row.
	 */
 	if (!sheet_selection_full_rows (sheet, row)) {
		ColRowIndexList *sel = col_row_get_index_list (row, row, NULL);
		cmd_resize_row_col (WORKBOOK_CONTROL (scg->wbcg),
				    sheet, FALSE, sel, new_size_pixels);
	} else
		workbook_cmd_format_row_height (WORKBOOK_CONTROL (scg->wbcg),
						sheet, new_size_pixels);
}

/***************************************************************************/


static void
button_select_all (GtkWidget *the_button, SheetControlGUI *scg)
{
	cmd_select_all (scg->sheet);
}

static void
vertical_scroll_change (GtkAdjustment *adj, SheetControlGUI *scg)
{
	if (scg->tip) {
		char buffer [20 + sizeof (long) * 4];
		snprintf (buffer, sizeof (buffer), _("Row: %d"), (int) adj->value + 1);
		gtk_label_set_text (GTK_LABEL (scg->tip), buffer);
	}
}

static void
horizontal_scroll_change (GtkAdjustment *adj, SheetControlGUI *scg)
{
	if (scg->tip) {
		char buffer [20 + sizeof (long) * 4];
		snprintf (buffer, sizeof (buffer), _("Column: %s"), col_name (adj->value));
		gtk_label_set_text (GTK_LABEL (scg->tip), buffer);
	}
}

static int
horizontal_scroll_event (GtkScrollbar *scroll, GdkEvent *event, SheetControlGUI *scg)
{
	if (event->type == GDK_BUTTON_PRESS){
		scg->tip = gnumeric_create_tooltip ();
		horizontal_scroll_change (GTK_ADJUSTMENT (scg->ha), scg);
		gnumeric_position_tooltip (scg->tip, 1);
		gtk_widget_show_all (gtk_widget_get_toplevel (scg->tip));

	} else if (event->type == GDK_BUTTON_RELEASE) {
		GnumericSheet  *gsheet = GNUMERIC_SHEET (scg->canvas);
		int col;

		/* A button release can be generated without a press by people
		 * with mouse wheels */
		if (scg->tip) {
			gtk_widget_destroy (gtk_widget_get_toplevel (scg->tip));
			scg->tip = NULL;
		}

		col = GTK_ADJUSTMENT (scg->ha)->value;

		gnumeric_sheet_set_left_col (gsheet, col);
		/* NOTE : Excel does not move the cursor, just scrolls the sheet. */
	}

	return FALSE;
}

static int
vertical_scroll_event (GtkScrollbar *scroll, GdkEvent *event, SheetControlGUI *scg)
{
	if (event->type == GDK_BUTTON_PRESS){
		scg->tip = gnumeric_create_tooltip ();
		vertical_scroll_change (GTK_ADJUSTMENT (scg->va), scg);
		gnumeric_position_tooltip (scg->tip, 0);
		gtk_widget_show_all (gtk_widget_get_toplevel (scg->tip));

	} else if (event->type == GDK_BUTTON_RELEASE) {
		GnumericSheet  *gsheet = GNUMERIC_SHEET (scg->canvas);
		int row;

		/* A button release can be generated without a press by people
		 * with mouse wheels */
		if (scg->tip) {
			gtk_widget_destroy (gtk_widget_get_toplevel (scg->tip));
			scg->tip = NULL;
		}

		row = GTK_ADJUSTMENT (scg->va)->value;

		gnumeric_sheet_set_top_row (gsheet, row);
		/* NOTE : Excel does not move the cursor, just scrolls the sheet. */
	}

	return FALSE;
}

static void
sheet_view_init (SheetControlGUI *scg)
{
	GtkTable *table = GTK_TABLE (scg);

	scg->sheet = NULL;
	scg->slide_handler = NULL;
	scg->slide_data = NULL;
	scg->sliding = -1;
	table->homogeneous = FALSE;
	gtk_table_resize (table, 4, 4);
}

static void
sheet_view_construct (SheetControlGUI *scg)
{
	GnomeCanvasGroup *root_group;
	GtkTable  *table = GTK_TABLE (scg);
	Sheet *sheet = scg->sheet;
	int i;

	/* Column canvas */
	scg->col_canvas = new_canvas_bar (scg, GTK_ORIENTATION_HORIZONTAL, &scg->col_item);
	gtk_table_attach (table, GTK_WIDGET (scg->col_canvas),
			  1, 2, 0, 1,
			  GTK_EXPAND | GTK_FILL | GTK_SHRINK,
			  GTK_FILL,
			  0, 0);
	gtk_signal_connect (GTK_OBJECT (scg->col_item), "selection_changed",
			    GTK_SIGNAL_FUNC (sheet_view_col_selection_changed),
			    scg);
	gtk_signal_connect (GTK_OBJECT (scg->col_item), "size_changed",
			    GTK_SIGNAL_FUNC (sheet_view_col_size_changed),
			    scg);

	/* Row canvas */
	scg->row_canvas = new_canvas_bar (scg, GTK_ORIENTATION_VERTICAL, &scg->row_item);

	gtk_table_attach (table, GTK_WIDGET (scg->row_canvas),
			  0, 1, 1, 2,
			  GTK_FILL, GTK_EXPAND | GTK_FILL | GTK_SHRINK,
			  0, 0);
	gtk_signal_connect (GTK_OBJECT (scg->row_item), "selection_changed",
			    GTK_SIGNAL_FUNC (sheet_view_row_selection_changed),
			    scg);
	gtk_signal_connect (GTK_OBJECT (scg->row_item), "size_changed",
			    GTK_SIGNAL_FUNC (sheet_view_row_size_changed),
			    scg);

	/* Create the gnumeric sheet canvas */
	scg->canvas = gnumeric_sheet_new (
		scg,
		ITEM_BAR (scg->col_item),
		ITEM_BAR (scg->row_item));

	gtk_signal_connect_after (
		GTK_OBJECT (scg), "size_allocate",
		GTK_SIGNAL_FUNC (sheet_view_size_allocate), scg);

	/* Create the object group inside the GnumericSheet */
	root_group = GNOME_CANVAS_GROUP (
		GNOME_CANVAS (scg->canvas)->root);
	scg->object_group = GNOME_CANVAS_GROUP (
		gnome_canvas_item_new (
			root_group,
			gnome_canvas_group_get_type (),
			"x", 0.0,
			"y", 0.0,
			NULL));

	/* Attach the GnumericSheet */
	gtk_table_attach (table, scg->canvas,
			  1, 2, 1, 2,
			  GTK_EXPAND | GTK_FILL | GTK_SHRINK,
			  GTK_EXPAND | GTK_FILL | GTK_SHRINK,
			  0, 0);
	gtk_widget_show (scg->canvas);

	i = sizeof (scg->control_points)/sizeof(GnomeCanvasItem *);
	while (i-- > 0)
		scg->control_points[i] = NULL;

	/*
	 * The selection group
	 */
	scg->selection_group = GNOME_CANVAS_GROUP (
		gnome_canvas_item_new (
			root_group,
			gnome_canvas_group_get_type (),
			"x", 0.0,
			"y", 0.0,
			NULL));

	/* The select-all button */
	scg->select_all_btn = gtk_button_new ();
	GTK_WIDGET_UNSET_FLAGS (scg->select_all_btn, GTK_CAN_FOCUS);
	gtk_table_attach (table, scg->select_all_btn, 0, 1, 0, 1, GTK_FILL, GTK_FILL, 0, 0);
	gtk_signal_connect (GTK_OBJECT (scg->select_all_btn), "clicked",
			    GTK_SIGNAL_FUNC (button_select_all), scg);

	/* Scroll bars and their adjustments */
	scg->va = gtk_adjustment_new (0.0, 0.0, sheet->rows.max_used, 1.0, 1.0, 1.0);
	scg->ha = gtk_adjustment_new (0.0, 0.0, sheet->cols.max_used, 1.0, 1.0, 1.0);
	scg->hs = gtk_hscrollbar_new (GTK_ADJUSTMENT (scg->ha));
	scg->vs = gtk_vscrollbar_new (GTK_ADJUSTMENT (scg->va));

	gtk_signal_connect (GTK_OBJECT (scg->ha), "value_changed",
			    GTK_SIGNAL_FUNC (horizontal_scroll_change), scg);
	gtk_signal_connect (GTK_OBJECT (scg->va), "value_changed",
			    GTK_SIGNAL_FUNC (vertical_scroll_change), scg);
	gtk_signal_connect (GTK_OBJECT (scg->hs), "event",
			    GTK_SIGNAL_FUNC (horizontal_scroll_event), scg);
	gtk_signal_connect (GTK_OBJECT (scg->vs), "event",
			    GTK_SIGNAL_FUNC (vertical_scroll_event), scg);

	/* Attach the horizontal scroll */
	gtk_table_attach (table, scg->hs,
			  1, 2, 2, 3,
			  GTK_EXPAND | GTK_FILL | GTK_SHRINK,
			  GTK_FILL,
			  0, 0);

	/* Attach the vertical scroll */
	gtk_table_attach (table, scg->vs,
			  2, 3, 1, 2,
			  GTK_FILL,
			  GTK_EXPAND | GTK_FILL | GTK_SHRINK,
			  0, 0);
	sheet_view_set_zoom_factor (scg, 1.);
}

void
sheet_view_set_header_visibility (SheetControlGUI *scg,
				  gboolean col_headers_visible,
				  gboolean row_headers_visible)
{
	g_return_if_fail (IS_SHEET_CONTROL_GUI (scg));

	if (col_headers_visible){
		if (!GTK_WIDGET_VISIBLE (GTK_WIDGET (scg->col_canvas)))
			gtk_widget_show (GTK_WIDGET (scg->col_canvas));
	} else {
		if (GTK_WIDGET_VISIBLE (GTK_WIDGET (scg->col_canvas)))
			gtk_widget_hide (GTK_WIDGET (scg->col_canvas));
	}

	if (row_headers_visible){
		if (!GTK_WIDGET_VISIBLE (GTK_WIDGET (scg->row_canvas)))
			gtk_widget_show (GTK_WIDGET (scg->row_canvas));
	} else {
		if (GTK_WIDGET_VISIBLE (GTK_WIDGET (scg->row_canvas)))
			gtk_widget_hide (GTK_WIDGET (scg->row_canvas));
	}
}

GtkWidget *
sheet_view_new (Sheet *sheet)
{
	SheetControlGUI *scg;

	scg = gtk_type_new (sheet_view_get_type ());
	scg->sheet = sheet;
	scg->tip = NULL;

	sheet_view_construct (scg);

	return GTK_WIDGET (scg);
}

static void
sheet_view_destroy (GtkObject *object)
{
	SheetControlGUI *scg = SHEET_CONTROL_GUI (object);

	/* Add shutdown code here */
	if (scg->tip)
		gtk_object_unref (GTK_OBJECT (scg->tip));

	if (scg->sheet)
		sheet_detach_sheet_view (scg);

	/* FIXME : Should we be pedantic and
	 * 1) clear the control points
	 * 2) remove ourselves from the sheets list of views ?
	 */
	if (GTK_OBJECT_CLASS (sheet_view_parent_class)->destroy)
		(*GTK_OBJECT_CLASS (sheet_view_parent_class)->destroy)(object);
}

static void
sheet_view_class_init (SheetControlGUIClass *Class)
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
			"SheetControlGUI",
			sizeof (SheetControlGUI),
			sizeof (SheetControlGUIClass),
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
sheet_view_hide_cursor (SheetControlGUI *scg)
{
	GnumericSheet *gsheet = GNUMERIC_SHEET (scg->canvas);

	item_cursor_set_visibility (gsheet->item_cursor, FALSE);
}

void
sheet_view_show_cursor (SheetControlGUI *scg)
{
	GnumericSheet *gsheet = GNUMERIC_SHEET (scg->canvas);

	item_cursor_set_visibility (gsheet->item_cursor, TRUE);
}

#define TRIANGLE_WIDTH 6

static GnomeCanvasPoints *
sheet_view_comment_get_points (SheetControlGUI *scg, int col, int row)
{
	GnomeCanvasPoints *points;
	int x, y, i;

	points = gnome_canvas_points_new (3);

	x = sheet_col_get_distance_pixels (scg->sheet, 0, col+1);
	y = 1+sheet_row_get_distance_pixels (scg->sheet, 0, row);

	points->coords [0] = x - TRIANGLE_WIDTH;
	points->coords [1] = y;
	points->coords [2] = x;
	points->coords [3] = y;
	points->coords [4] = x;
	points->coords [5] = y + TRIANGLE_WIDTH;

	for (i = 0; i < 3; i++){
		gnome_canvas_c2w (GNOME_CANVAS (scg->canvas),
				  points->coords [i*2],
				  points->coords [i*2+1],
				  &(points->coords [i*2]),
				  &(points->coords [i*2+1]));
	}
	return points;
}

GnomeCanvasItem *
sheet_view_comment_create_marker (SheetControlGUI *scg, int col, int row)
{
	GnomeCanvasPoints *points;
	GnomeCanvasGroup *group;
	GnomeCanvasItem *i;

	g_return_val_if_fail (IS_SHEET_CONTROL_GUI (scg), NULL);

	group = GNOME_CANVAS_GROUP (GNOME_CANVAS (scg->canvas)->root);
	points = sheet_view_comment_get_points (scg, col, row);

	i = gnome_canvas_item_new (
		group, gnome_canvas_polygon_get_type (),
		"points",     points,
		"fill_color", "red",
		NULL);
	gnome_canvas_points_free (points);

	return i;
}

void
sheet_view_comment_relocate (SheetControlGUI *scg, int col, int row, GnomeCanvasItem *o)
{
	GnomeCanvasPoints *points;

	g_return_if_fail (IS_SHEET_CONTROL_GUI (scg));
	g_return_if_fail (o != NULL);
	g_return_if_fail (GNOME_IS_CANVAS_ITEM (o));

	points = sheet_view_comment_get_points (scg, col, row);

	gnome_canvas_item_set (o, "points", points, NULL);
	gnome_canvas_points_free (points);
}

void
sheet_view_selection_unant (SheetControlGUI *scg)
{
	GList *l;

	g_return_if_fail (IS_SHEET_CONTROL_GUI (scg));

	if (scg->anted_cursors == NULL)
		return;

	for (l = scg->anted_cursors; l; l = l->next)
		gtk_object_destroy (GTK_OBJECT (l->data));

	g_list_free (scg->anted_cursors);
	scg->anted_cursors = NULL;
}

void
sheet_view_selection_ant (SheetControlGUI *scg)
{
	GnomeCanvasGroup *group;
	ItemGrid *grid;
	GList *l;

	g_return_if_fail (IS_SHEET_CONTROL_GUI (scg));

	if (scg->anted_cursors)
		sheet_view_selection_unant (scg);

	group = scg->selection_group;
	grid = GNUMERIC_SHEET (scg->canvas)->item_grid;

	for (l = scg->sheet->selections; l; l = l->next){
		SheetSelection *ss = l->data;
		ItemCursor *item_cursor;

		item_cursor = ITEM_CURSOR (gnome_canvas_item_new (
			group, item_cursor_get_type (),
			"SheetControlGUI", scg,
			"Grid",  grid,
			"Style", ITEM_CURSOR_ANTED,
			NULL));
		item_cursor_set_bounds (
			item_cursor,
			ss->user.start.col, ss->user.start.row,
			ss->user.end.col, ss->user.end.row);

		scg->anted_cursors = g_list_prepend (scg->anted_cursors, item_cursor);
	}
}

void
sheet_view_adjust_preferences (SheetControlGUI *scg)
{
	Sheet *sheet = scg->sheet;
	WorkbookView *wbv = wb_control_view (WORKBOOK_CONTROL (scg->wbcg));

	if (sheet->show_col_header)
		gtk_widget_show (GTK_WIDGET (scg->col_canvas));
	else
		gtk_widget_hide (GTK_WIDGET (scg->col_canvas));

	if (sheet->show_row_header)
		gtk_widget_show (GTK_WIDGET (scg->row_canvas));
	else
		gtk_widget_hide (GTK_WIDGET (scg->row_canvas));

	if (sheet->show_col_header && sheet->show_row_header)
		gtk_widget_show (scg->select_all_btn);
	else
		gtk_widget_hide (scg->select_all_btn);

	if (wbv->show_horizontal_scrollbar)
		gtk_widget_show (scg->hs);
	else
		gtk_widget_hide (scg->hs);

	if (wbv->show_vertical_scrollbar)
		gtk_widget_show (scg->vs);
	else
		gtk_widget_hide (scg->vs);
}

StyleFont *
sheet_view_get_style_font (const Sheet *sheet, MStyle const * const mstyle)
{
	/* Scale the font size by the average scaling factor for the
	 * display.  72dpi is base size
	 */
	double const res  = application_dpi_to_pixels ();

	/* When previewing sheet can == NULL */
	double const zoom = (sheet) ? sheet->last_zoom_factor_used : 1.;

	return mstyle_get_font (mstyle, zoom * res);
}

/*****************************************************************************/

void
sheet_view_stop_sliding (SheetControlGUI *scg)
{
	if (scg->sliding == -1)
		return;

	gtk_timeout_remove (scg->sliding);
	scg->slide_handler = NULL;
	scg->slide_data = NULL;
	scg->sliding = -1;
}

static gint
sheet_view_sliding_callback (gpointer data)
{
	SheetControlGUI *scg = data;
	GnumericSheet *gsheet = GNUMERIC_SHEET (scg->canvas);
	gboolean change = FALSE;
	int col, row;

	col = scg->sliding_col;
	row = scg->sliding_row;

	if (scg->sliding_x < 0){
		if (gsheet->col.first){
			change = TRUE;
			if (scg->sliding_x >= -50)
				col = 1;
			else if (scg->sliding_x >= -100)
				col = 3;
			else
				col = 20;
			col = gsheet->col.first - col;
			if (col < 0)
				col = 0;
		} else
			col = 0;
	}

	if (scg->sliding_x > 0){
		if (gsheet->col.last_full < SHEET_MAX_COLS-1){
			change = TRUE;
			if (scg->sliding_x <= 50)
				col = 1;
			else if (scg->sliding_x <= 100)
				col = 3;
			else
				col = 20;
			col = gsheet->col.last_visible + col;
			if (col >= SHEET_MAX_COLS)
				col = SHEET_MAX_COLS-1;
		} else
			col = SHEET_MAX_COLS-1;
	}

	if (scg->sliding_y < 0){
		if (gsheet->row.first){
			change = TRUE;
			if (scg->sliding_y >= -30)
				row = 1;
			else if (scg->sliding_y >= -60)
				row = 25;
			else if (scg->sliding_y >= -100)
				row = 250;
			else
				row = 5000;
			row = gsheet->row.first - row;
			if (row < 0)
				row = 0;
		} else
			row = 0;
	}
	if (scg->sliding_y > 0){
		if (gsheet->row.last_full < SHEET_MAX_ROWS-1){
			change = TRUE;
			if (scg->sliding_y <= 30)
				row = 1;
			else if (scg->sliding_y <= 60)
				row = 25;
			else if (scg->sliding_y <= 100)
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
		sheet_view_stop_sliding (scg);
		return TRUE;
	}

	if (scg->slide_handler == NULL ||
	    (*scg->slide_handler) (scg, col, row, scg->slide_data))
		gnumeric_sheet_make_cell_visible (gsheet, col, row, FALSE);

	return TRUE;
}

gboolean
sheet_view_start_sliding (SheetControlGUI *scg,
			  SheetControlGUISlideHandler slide_handler,
			  gpointer user_data,
			  int col, int row, int dx, int dy)
{
	GnumericSheet *gsheet = GNUMERIC_SHEET (scg->canvas);

	/* Do not slide off the edge */
	if (((dx == 0) ||
	     (dx < 0 && gsheet->col.first == 0) ||
	     (dx > 0 && gsheet->col.last_full >= SHEET_MAX_COLS-1)) &&
	    ((dy == 0) ||
	     (dy < 0 && gsheet->row.first == 0) ||
	     (dy > 0 && gsheet->row.last_full >= SHEET_MAX_ROWS-1))) {
		sheet_view_stop_sliding (scg);
		return FALSE;
	}

	scg->slide_handler = slide_handler;
	scg->slide_data = user_data;
	scg->sliding_x = dx;
	scg->sliding_y = dy;
	scg->sliding_col = col;
	scg->sliding_row = row;

	if (scg->sliding == -1) {
		sheet_view_sliding_callback (scg);

		scg->sliding = gtk_timeout_add (
			200, sheet_view_sliding_callback, scg);
	}
	return TRUE;
}

#if 0
#ifdef ENABLE_BONOBO
void
sheet_view_insert_object (SheetControlGUI *scg, BonoboObjectClient *object)
{
/*	GtkWidget *view;*/

	/*
	 * Commented out because the new_view api changed and it isn't
	 * used anyways.
	 */

	/* view = gnome_bonobo_object_new_view (object); */
	g_warning ("Stick this into the SheetControlGUI");
}
#endif
#endif

/***************************************************************************/

enum {
	CONTEXT_CUT	= 1,
	CONTEXT_COPY,
	CONTEXT_PASTE,
	CONTEXT_PASTE_SPECIAL,
	CONTEXT_INSERT,
	CONTEXT_DELETE,
	CONTEXT_CLEAR_CONTENT,
	CONTEXT_FORMAT_CELL,
	CONTEXT_COL_WIDTH,
	CONTEXT_COL_HIDE,
	CONTEXT_COL_UNHIDE,
	CONTEXT_ROW_HEIGHT,
	CONTEXT_ROW_HIDE,
	CONTEXT_ROW_UNHIDE
};
static gboolean
context_menu_hander (GnumericPopupMenuElement const *element,
		     gpointer user_data)
{
	SheetControlGUI *sheet_view = user_data;
	Sheet *sheet = sheet_view->sheet;
	WorkbookControlGUI *wbcg = sheet_view->wbcg;
	WorkbookControl *wbc = WORKBOOK_CONTROL (wbcg);

	g_return_val_if_fail (element != NULL, TRUE);
	g_return_val_if_fail (sheet != NULL, TRUE);

	switch (element->index) {
	case CONTEXT_CUT :
		sheet_selection_cut (wbc, sheet);
		break;
	case CONTEXT_COPY :
		sheet_selection_copy (wbc, sheet);
		break;
	case CONTEXT_PASTE :
		cmd_paste_to_selection (wbc, sheet, PASTE_DEFAULT);
		break;
	case CONTEXT_PASTE_SPECIAL : {
		int flags = dialog_paste_special (wbcg);
		if (flags != 0)
			cmd_paste_to_selection (wbc, sheet, flags);
		break;
	}
	case CONTEXT_INSERT :
		dialog_insert_cells (wbcg, sheet);
		break;
	case CONTEXT_DELETE :
		dialog_delete_cells (wbcg, sheet);
		break;
	case CONTEXT_CLEAR_CONTENT :
		cmd_clear_selection (wbc, sheet, CLEAR_VALUES);
		break;
	case CONTEXT_FORMAT_CELL :
		dialog_cell_format (wbcg, sheet, FD_CURRENT);
		break;
	case CONTEXT_COL_WIDTH :
		sheet_dialog_set_column_width (NULL, wbcg);
		break;
	case CONTEXT_COL_HIDE :
		cmd_hide_selection_rows_cols (wbc, sheet, TRUE, FALSE);
		break;
	case CONTEXT_COL_UNHIDE :
		cmd_hide_selection_rows_cols (wbc, sheet, TRUE, TRUE);
		break;
	case CONTEXT_ROW_HEIGHT :
		sheet_dialog_set_row_height (NULL, wbcg);
		break;
	case CONTEXT_ROW_HIDE :
		cmd_hide_selection_rows_cols (wbc, sheet, FALSE, FALSE);
		break;
	case CONTEXT_ROW_UNHIDE :
		cmd_hide_selection_rows_cols (wbc, sheet, FALSE, TRUE);
		break;
	default :
		break;
	};
	return TRUE;
}

void
scg_context_menu (SheetControlGUI *sheet_view, GdkEventButton *event,
		  gboolean is_col, gboolean is_row)
{
	enum {
		CONTEXT_IGNORE_FOR_ROWS = 1,
		CONTEXT_IGNORE_FOR_COLS = 2
	};
	enum {
		CONTEXT_ENABLE_PASTE_SPECIAL = 1,
	};

	static GnumericPopupMenuElement const popup_elements[] = {
		{ N_("Cu_t"),           GNOME_STOCK_MENU_CUT,
		    0, 0, CONTEXT_CUT },
		{ N_("_Copy"),          GNOME_STOCK_MENU_COPY,
		    0, 0, CONTEXT_COPY },
		{ N_("_Paste"),         GNOME_STOCK_MENU_PASTE,
		    0, 0, CONTEXT_PASTE },
		{ N_("Paste _Special"),	NULL,
		    0, CONTEXT_ENABLE_PASTE_SPECIAL, CONTEXT_PASTE_SPECIAL },

		{ "", NULL, 0, 0, 0 },

		{ N_("_Insert..."),	NULL,
		    0, 0, CONTEXT_INSERT },
		{ N_("_Delete..."),	NULL,
		    0, 0, CONTEXT_DELETE },
		{ N_("Clear Co_ntents"),NULL,
		    0, 0, CONTEXT_CLEAR_CONTENT },

		{ "", NULL, 0, 0, 0 },

		{ N_("_Format Cells..."),GNOME_STOCK_MENU_PREF,
		    0, 0, CONTEXT_FORMAT_CELL },

		/* Column specific (Note some labels duplicate row labels) */
		{ N_("Column _Width..."),NULL,
		    CONTEXT_IGNORE_FOR_COLS, 0, CONTEXT_COL_WIDTH },
		{ N_("_Hide"),		 NULL,
		    CONTEXT_IGNORE_FOR_COLS, 0, CONTEXT_COL_HIDE },
		{ N_("_Unhide"),	 NULL,
		    CONTEXT_IGNORE_FOR_COLS, 0, CONTEXT_COL_UNHIDE },

		/* Row specific (Note some labels duplicate col labels) */
		{ N_("_Row Height..."),	 NULL,
		    CONTEXT_IGNORE_FOR_ROWS, 0, CONTEXT_ROW_HEIGHT },
		{ N_("_Hide"),		 NULL,
		    CONTEXT_IGNORE_FOR_ROWS, 0, CONTEXT_ROW_HIDE },
		{ N_("_Unhide"),	 NULL,
		    CONTEXT_IGNORE_FOR_ROWS, 0, CONTEXT_ROW_UNHIDE },

		{ NULL, NULL, 0, 0, 0 },
	};

	/* row and column specific operations */
	int const display_filter =
		(is_col ? CONTEXT_IGNORE_FOR_COLS : 0) |
		(is_row ? CONTEXT_IGNORE_FOR_ROWS : 0);

	/*
	 * Paste special does not apply to cut cells.  Enable
	 * when there is nothing in the local clipboard, or when
	 * the clipboard has the results of a copy.
	 */
	int const sensitivity_filter =
	    (application_clipboard_is_empty () ||
	    (application_clipboard_contents_get () != NULL))
	? CONTEXT_ENABLE_PASTE_SPECIAL : 0;

	gnumeric_create_popup_menu (popup_elements, &context_menu_hander,
				    sheet_view, display_filter,
				    sensitivity_filter, event);
}

/***************************************************************************/

void
scg_visible_spans_regen (SheetControlGUI *scg)
{
}
