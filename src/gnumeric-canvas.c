/*
 * The Gnumeric Sheet widget.
 *
 * Author:
 *     Miguel de Icaza (miguel@kernel.org)
 */
#include <config.h>

#include <gnome.h>
#include <gdk/gdkkeysyms.h>
#include <gdk/gdkx.h>
#include <string.h>
#include "gnumeric.h"
#include "gnumeric-sheet.h"
#include "sheet-object.h"
#include "color.h"
#include "cursors.h"
#include "selection.h"
#include "utils.h"
#include "ranges.h"
#include "sheet-private.h"

#undef DEBUG_POSITIONS

#define CURSOR_COL(gsheet) (gsheet)->sheet_view->sheet->cursor_col
#define CURSOR_ROW(gsheet) (gsheet)->sheet_view->sheet->cursor_row

static GnomeCanvasClass *sheet_parent_class;

static void
gnumeric_sheet_destroy (GtkObject *object)
{
	GnumericSheet *gsheet;

	/* Add shutdown code here */
	gsheet = GNUMERIC_SHEET (object);

	if (GTK_OBJECT_CLASS (sheet_parent_class)->destroy)
		(*GTK_OBJECT_CLASS (sheet_parent_class)->destroy)(object);
}

static GnumericSheet *
gnumeric_sheet_create (SheetView *sheet_view, GtkWidget *entry)
{
	GnumericSheet *gsheet;
	GnomeCanvas   *canvas;

	gsheet = gtk_type_new (gnumeric_sheet_get_type ());
	canvas = GNOME_CANVAS (gsheet);

	gsheet->sheet_view = sheet_view;
	gsheet->top_col = 0;
	gsheet->top_row = 0;
	gsheet->entry   = entry;

	return gsheet;
}

void
gnumeric_sheet_get_cell_bounds (GnumericSheet *gsheet, int col, int row, int *x, int *y, int *w, int *h)
{
	Sheet *sheet;

	g_return_if_fail (gsheet != NULL);
	g_return_if_fail (GNUMERIC_IS_SHEET (gsheet));

	sheet = gsheet->sheet_view->sheet;

	*x = sheet_col_get_distance (sheet, gsheet->top_col, col);
	*y = sheet_row_get_distance (sheet, gsheet->top_row, row);

	*w = sheet_col_get_distance (sheet, col, col + 1);
	*h = sheet_row_get_distance (sheet, row, row + 1);
}

/*
 * gnumeric_sheet_cursor_set
 * @gsheet: The sheet
 * @col:    the column
 * @row:    the row
 *
 * This informs the GnumericSheet of the cursor position.  It is
 * used to sync the contents of the scrollbars with our position
 */
void
gnumeric_sheet_cursor_set (GnumericSheet *gsheet, int col, int row)
{
	GtkAdjustment *ha, *va;
	SheetView *sheet_view;

	g_return_if_fail (GNUMERIC_IS_SHEET (gsheet));

	sheet_view = gsheet->sheet_view;

	if (sheet_view->ha){
		ha = GTK_ADJUSTMENT (sheet_view->ha);
		va = GTK_ADJUSTMENT (sheet_view->va);
		ha->value = gsheet->top_col;
		va->value = gsheet->top_row;

#ifdef DEBUG_POSITIONS
		{
			char *top_str = g_strdup (cell_name (gsheet->top_col, gsheet->top_row));
			char *lv_str = g_strdup (cell_name (gsheet->last_visible_col, gsheet->last_visible_row));
			printf ("top=%s lv=%s\n", top_str, lv_str);
			g_free (top_str);
			g_free (lv_str);
		}
#endif

		gtk_adjustment_value_changed (ha);
		gtk_adjustment_value_changed (va);
	}

}

/*
 * gnumeric_sheet_set_selection:
 * @gsheet:	The sheet name
 * @ss:		The selection
 *
 * Set the current selection to cover the inclusive area delimited by
 * start_col, start_row, end_col and end_row.  The actual cursor is
 * placed at base_col, base_row
 */
void
gnumeric_sheet_set_selection (GnumericSheet *gsheet, SheetSelection const *ss)
{
	g_return_if_fail (gsheet != NULL);
	g_return_if_fail (ss != NULL);
	g_return_if_fail (GNUMERIC_IS_SHEET (gsheet));

	sheet_cursor_set (
		gsheet->sheet_view->sheet,
		ss->base.col, ss->base.row,
		ss->user.start.col, ss->user.start.row,
		ss->user.end.col, ss->user.end.row);
}

/*
 * move_cursor:
 * @gsheet:   The sheet where the cursor is located
 * @col:      The new column for the cursor.
 * @row:      The new row for the cursor.
 * @clear_selection: If set, clear the selection before moving
 *
 *   Moves the sheet cursor to a new location, it clears the selection,
 *   accepts any pending output on the editing line and moves the cell
 *   cursor.
 */
static void
move_cursor (GnumericSheet *gsheet, int col, int row, gboolean clear_selection)
{
	Sheet *sheet = gsheet->sheet_view->sheet;

	if (clear_selection)
		sheet_selection_reset_only (sheet);

	sheet_make_cell_visible (sheet, col, row);
	sheet_cursor_set (sheet, col, row, col, row, col, row);

	if (clear_selection)
		sheet_selection_append (sheet, col, row);
}

void
gnumeric_sheet_move_cursor (GnumericSheet *gsheet, int col, int row)
{
	g_return_if_fail (gsheet != NULL);
	g_return_if_fail (GNUMERIC_IS_SHEET (gsheet));

	move_cursor (gsheet, col, row, 1);
}

void
gnumeric_sheet_set_cursor_bounds (GnumericSheet *gsheet,
				  int start_col, int start_row,
				  int end_col,   int end_row)
{
	g_return_if_fail (gsheet != NULL);
	g_return_if_fail (GNUMERIC_IS_SHEET (gsheet));

	item_cursor_set_bounds (
		gsheet->item_cursor,
		start_col, start_row,
		end_col, end_row);
}

/*
 * move_cursor_horizontal:
 *  @Sheet:  The sheet name
 *  @count:  number of units to move the cursor horizontally
 *  @jump_to_boundaries : skip from the start to the end of ranges
 *                       of filled or unfilled cells.
 *
 * Moves the cursor count columns
 */
static void
move_cursor_horizontal (GnumericSheet *gsheet, int count, gboolean jump_to_boundaries)
{
	Sheet *sheet = gsheet->sheet_view->sheet;
	int const new_col =
	    sheet_find_boundary_horizontal (sheet,
					    sheet->cursor_col,
					    sheet->cursor_row,
					    count, jump_to_boundaries);
	move_cursor (gsheet, new_col, CURSOR_ROW(gsheet), TRUE);
}

static void
move_horizontal_selection (GnumericSheet *gsheet, int count, gboolean jump_to_boundaries)
{
	sheet_selection_extend_horizontal (gsheet->sheet_view->sheet, count, jump_to_boundaries);
}

/*
 * move_cursor_vertical:
 *  @Sheet:  The sheet name
 *  @count:  number of units to move the cursor vertically
 *  @jump_to_boundaries : skip from the start to the end of ranges
 *                       of filled or unfilled cells.
 *
 * Moves the cursor count rows
 */
static void
move_cursor_vertical (GnumericSheet *gsheet, int count, gboolean jump_to_boundaries)
{
	Sheet *sheet = gsheet->sheet_view->sheet;
	int const new_row =
	    sheet_find_boundary_vertical (sheet,
					  sheet->cursor_col,
					  sheet->cursor_row,
					  count, jump_to_boundaries);
	move_cursor (gsheet, CURSOR_COL (gsheet), new_row, TRUE);
}

static void
move_vertical_selection (GnumericSheet *gsheet, int count, gboolean jump_to_boundaries)
{
	sheet_selection_extend_vertical (gsheet->sheet_view->sheet, count, jump_to_boundaries);
}

/*
 * gnumeric_sheet_can_move_cursor
 *  @gsheet:   the object
 *
 * Returns true if the cursor keys should be used to select
 * a cell range (if the cursor is in a spot in the expression
 * where it makes sense to have a cell reference), false if not.
 */
int
gnumeric_sheet_can_move_cursor (GnumericSheet *gsheet)
{
	GtkEntry *entry;
	int cursor_pos;

	g_return_val_if_fail (gsheet != NULL, FALSE);
	g_return_val_if_fail (GNUMERIC_IS_SHEET (gsheet), FALSE);

	if (!gsheet->sheet_view->sheet->editing)
		return FALSE;

	if (gsheet->item_editor && gsheet->selecting_cell)
		return TRUE;

	entry = GTK_ENTRY (gsheet->entry);
	cursor_pos = GTK_EDITABLE (entry)->current_pos;

	if (entry->text [0] != '=')
		return FALSE;
	if (cursor_pos == 0)
		return FALSE;

	switch (entry->text [cursor_pos-1]){
	case '=': case '-': case '*': case '/': case '^':
	case '+': case '&': case '(': case '%': case '!':
	case ':': case ',':
		return TRUE;
	}

	return FALSE;
}

static void
start_cell_selection_at (GnumericSheet *gsheet, int col, int row)
{
	GnomeCanvas *canvas = GNOME_CANVAS (gsheet);
	GnomeCanvasGroup *group = GNOME_CANVAS_GROUP (canvas->root);

	g_return_if_fail (gsheet->selecting_cell == FALSE);

	gsheet->selecting_cell = TRUE;
	gsheet->selection = ITEM_CURSOR (gnome_canvas_item_new (
		group,
		item_cursor_get_type (),
		"Sheet", gsheet->sheet_view->sheet,
		"Grid",  gsheet->item_grid,
		"Style", ITEM_CURSOR_ANTED, NULL));
	gsheet->selection->base_col = col;
	gsheet->selection->base_row = row;
	item_cursor_set_bounds (ITEM_CURSOR (gsheet->selection), col, row, col, row);

	gsheet->sel_cursor_pos = GTK_EDITABLE (gsheet->entry)->current_pos;
	gsheet->sel_text_len = 0;
}

static void
start_cell_selection (GnumericSheet *gsheet)
{
	Sheet *sheet = gsheet->sheet_view->sheet;

	start_cell_selection_at (gsheet, sheet->cursor_col, sheet->cursor_row);
}

void
gnumeric_sheet_start_cell_selection (GnumericSheet *gsheet, int col, int row)
{
	g_return_if_fail (gsheet != NULL);
	g_return_if_fail (GNUMERIC_IS_SHEET (gsheet));

	if (gsheet->selecting_cell)
		return;

	start_cell_selection_at (gsheet, col, row);
}

void
gnumeric_sheet_stop_cell_selection (GnumericSheet *gsheet)
{
	g_return_if_fail (gsheet != NULL);
	g_return_if_fail (GNUMERIC_IS_SHEET (gsheet));

	if (!gsheet->selecting_cell)
		return;

	gsheet->selecting_cell = FALSE;
	gtk_object_destroy (GTK_OBJECT (gsheet->selection));
	gsheet->selection = NULL;
}

void
gnumeric_sheet_create_editing_cursor (GnumericSheet *gsheet)
{
	GnomeCanvas *canvas = GNOME_CANVAS (gsheet);
	GnomeCanvasItem *item;
	Sheet *sheet;
	int col, row;

	sheet = gsheet->sheet_view->sheet;
	col = sheet->cursor_col;
	row = sheet->cursor_row;

	item = gnome_canvas_item_new (GNOME_CANVAS_GROUP(canvas->root),
				      item_edit_get_type (),
				      "ItemEdit::Sheet",    sheet,
				      "ItemEdit::Grid",     gsheet->item_grid,
				      "ItemEdit::Col",      col,
				      "ItemEdit::Row",      row,
				      "ItemEdit::GtkEntry", sheet->workbook->ea_input,
				      NULL);

	gsheet->item_editor = ITEM_EDIT (item);
}

static void
destroy_item_editor (GnumericSheet *gsheet)
{
	g_return_if_fail (gsheet->item_editor);

	gtk_object_destroy (GTK_OBJECT (gsheet->item_editor));

	gsheet->item_editor = NULL;
}

void
gnumeric_sheet_destroy_editing_cursor (GnumericSheet *gsheet)
{
	g_return_if_fail (gsheet != NULL);
	g_return_if_fail (GNUMERIC_IS_SHEET (gsheet));

	gnumeric_sheet_stop_cell_selection (gsheet);

	if (!gsheet->item_editor)
		return;

	destroy_item_editor (gsheet);
}

void
gnumeric_sheet_stop_editing (GnumericSheet *gsheet)
{
	g_return_if_fail (gsheet != NULL);
	g_return_if_fail (GNUMERIC_IS_SHEET (gsheet));

	if (!gsheet->item_editor)
		return;

	destroy_item_editor (gsheet);
}

static void
selection_remove_selection_string (GnumericSheet *gsheet)
{
	gtk_editable_delete_text (GTK_EDITABLE (gsheet->entry),
				  gsheet->sel_cursor_pos,
				  gsheet->sel_cursor_pos+gsheet->sel_text_len);
}

static void
selection_insert_selection_string (GnumericSheet *gsheet)
{
	ItemCursor *sel = gsheet->selection;
	char buffer [20];
	int pos;

	/* Get the new selection string */
	strcpy (buffer, cell_name (sel->pos.start.col, sel->pos.start.row));
	if (!range_is_singleton (&sel->pos)) {
		strcat (buffer, ":");
		strcat (buffer, cell_name (sel->pos.end.col, sel->pos.end.row));
	}
	gsheet->sel_text_len = strlen (buffer);

	pos = gsheet->sel_cursor_pos;
	gtk_editable_insert_text (GTK_EDITABLE (gsheet->entry),
				  buffer, strlen (buffer),
				  &pos);

	/* Set the cursor at the end.  It looks nicer */
	gtk_editable_set_position (GTK_EDITABLE (gsheet->entry),
				   gsheet->sel_cursor_pos +
				   gsheet->sel_text_len);
}

/*
 * Invoked by Item-Grid to extend the cursor selection
 */
void
gnumeric_sheet_selection_extend (GnumericSheet *gsheet, int col, int row)
{
	ItemCursor *ic;

	g_return_if_fail (gsheet != NULL);
	g_return_if_fail (GNUMERIC_IS_SHEET (gsheet));
	g_return_if_fail (gsheet->selecting_cell);
	g_return_if_fail (col < SHEET_MAX_COLS);
	g_return_if_fail (row < SHEET_MAX_ROWS);

	ic = gsheet->selection;

	selection_remove_selection_string (gsheet);
	item_cursor_set_bounds (ic,
				MIN (ic->base_col, col),
				MIN (ic->base_row, row),
				MAX (ic->base_col, col),
				MAX (ic->base_row, row));
	selection_insert_selection_string (gsheet);
}

/*
 * Invoked by Item-Grid to place the selection cursor on a specific
 * spot.
 */
void
gnumeric_sheet_selection_cursor_place (GnumericSheet *gsheet, int col, int row)
{
	ItemCursor *ic;

	g_return_if_fail (gsheet != NULL);
	g_return_if_fail (GNUMERIC_IS_SHEET (gsheet));
	g_return_if_fail (gsheet->selecting_cell);
	g_return_if_fail (col < SHEET_MAX_COLS);
	g_return_if_fail (row < SHEET_MAX_ROWS);

	ic = gsheet->selection;

	selection_remove_selection_string (gsheet);
	item_cursor_set_bounds (ic, col, row, col, row);
	selection_insert_selection_string (gsheet);
}

static void
selection_cursor_move_horizontal (GnumericSheet *gsheet, int dir, gboolean jump_to_boundaries)
{
	ItemCursor *ic;

	g_return_if_fail (dir == -1 || dir == 1);

	if (!gsheet->selecting_cell)
		start_cell_selection (gsheet);

	ic = gsheet->selection;
	ic->base_col = sheet_find_boundary_horizontal (gsheet->sheet_view->sheet,
						       ic->base_col, ic->base_row,
						       dir, jump_to_boundaries);

	selection_remove_selection_string (gsheet);
	item_cursor_set_bounds (ic,
				ic->base_col,
				ic->base_row,
				ic->base_col,
				ic->base_row);
	selection_insert_selection_string (gsheet);
}

static void
selection_cursor_move_vertical (GnumericSheet *gsheet, int dir, gboolean jump_to_boundaries)
{
	ItemCursor *ic;

	g_return_if_fail (dir == -1 || dir == 1);

	if (!gsheet->selecting_cell)
		start_cell_selection (gsheet);

	ic = gsheet->selection;
	ic->base_row = sheet_find_boundary_vertical (gsheet->sheet_view->sheet,
						     ic->base_col, ic->base_row,
						     dir, jump_to_boundaries);

	selection_remove_selection_string (gsheet);
	item_cursor_set_bounds (ic,
				ic->base_col,
				ic->base_row,
				ic->base_col,
				ic->base_row);
	selection_insert_selection_string (gsheet);
}

static void
selection_expand_horizontal (GnumericSheet *gsheet, int n, gboolean jump_to_boundaries)
{
	ItemCursor *ic;
	int start_col, end_col;

	g_return_if_fail (n == -1 || n == 1);

	if (!gsheet->selecting_cell){
		selection_cursor_move_horizontal (gsheet, n, jump_to_boundaries);
		return;
	}

	ic = gsheet->selection;
	start_col = ic->pos.start.col;
	end_col = ic->pos.end.col;

	if (ic->base_col < end_col)
		end_col =
		    sheet_find_boundary_horizontal (gsheet->sheet_view->sheet,
						    end_col, ic->pos.end.row,
						    n, jump_to_boundaries);
	else if (ic->base_col > start_col || n < 0)
		start_col =
		    sheet_find_boundary_horizontal (gsheet->sheet_view->sheet,
						    start_col, ic->pos.start.row,
						    n, jump_to_boundaries);
	else
		end_col =
		    sheet_find_boundary_horizontal (gsheet->sheet_view->sheet,
						    end_col,  ic->pos.end.row,
						    n, jump_to_boundaries);

	if (end_col < start_col) {
		int const tmp = start_col;
		start_col = end_col;
		end_col = tmp;
	}

	selection_remove_selection_string (gsheet);
	item_cursor_set_bounds (ic,
				start_col,
				ic->pos.start.row,
				end_col,
				ic->pos.end.row);
	selection_insert_selection_string (gsheet);
}

static void
selection_expand_vertical (GnumericSheet *gsheet, int n, gboolean jump_to_boundaries)
{
	ItemCursor *ic;
	int start_row, end_row;

	g_return_if_fail (n == -1 || n == 1);

	if (!gsheet->selecting_cell){
		selection_cursor_move_vertical (gsheet, n, jump_to_boundaries);
		return;
	}

	ic = gsheet->selection;
	start_row = ic->pos.start.row;
	end_row = ic->pos.end.row;

	if (ic->base_row < end_row)
		end_row =
		    sheet_find_boundary_vertical (gsheet->sheet_view->sheet,
						  ic->pos.end.col, end_row,
						  n, jump_to_boundaries);
	else if (ic->base_row > start_row || n < 0)
		start_row =
		    sheet_find_boundary_vertical (gsheet->sheet_view->sheet,
						  ic->pos.start.col, start_row,
						  n, jump_to_boundaries);
	else
		end_row =
		    sheet_find_boundary_vertical (gsheet->sheet_view->sheet,
						  ic->pos.end.col,  end_row,
						  n, jump_to_boundaries);

	if (end_row < start_row) {
		int const tmp = start_row;
		start_row = end_row;
		end_row = tmp;
	}

	selection_remove_selection_string (gsheet);
	item_cursor_set_bounds (ic,
				ic->pos.start.col,
				start_row,
				ic->pos.end.col,
				end_row);
	selection_insert_selection_string (gsheet);
}

/*
 * key press event handler for the gnumeric sheet for the sheet mode
 */
static gint
gnumeric_sheet_key_mode_sheet (GnumericSheet *gsheet, GdkEventKey *event)
{
	Sheet *sheet = gsheet->sheet_view->sheet;
	Workbook *wb = sheet->workbook;
	void (*movefn_horizontal) (GnumericSheet *, int, gboolean);
	void (*movefn_vertical)   (GnumericSheet *, int, gboolean);
	gboolean const cursor_move = gnumeric_sheet_can_move_cursor (gsheet);
	gboolean const jump_to_bounds = event->state & GDK_CONTROL_MASK;

	if ((event->state & GDK_SHIFT_MASK) != 0){
		if (cursor_move){
			movefn_horizontal = selection_expand_horizontal;
			movefn_vertical = selection_expand_vertical;
		} else {
			movefn_horizontal = move_horizontal_selection;
			movefn_vertical   = move_vertical_selection;
		}
	} else {
		if (cursor_move){
			movefn_horizontal = selection_cursor_move_horizontal;
			movefn_vertical   = selection_cursor_move_vertical;
		} else {
			movefn_horizontal = move_cursor_horizontal;
			movefn_vertical = move_cursor_vertical;
		}
	}

	/* Ignore a few keys (to avoid the selection cursor to be killed
	 * in some cases
	 */
	if (cursor_move){
		switch (event->keyval){
		case GDK_Shift_L:   case GDK_Shift_R:
		case GDK_Alt_L:     case GDK_Alt_R:
		case GDK_Control_L: case GDK_Control_R:
			return 1;
		}
	}

	/*
	 * The following sequences do not trigger an editor-start
	 * but if the editor is running we forward the events to it.
	 */
	if (!gsheet->item_editor){

		if ((event->state & GDK_CONTROL_MASK) != 0) {

			switch (event->keyval) {

			case GDK_space:
				sheet_selection_reset_only (sheet);
				sheet_selection_append_range (
					sheet,
					sheet->cursor_col, sheet->cursor_row,
					sheet->cursor_col, 0,
					sheet->cursor_col, SHEET_MAX_ROWS-1);
				sheet_redraw_all (sheet);
				return 1;
			}
		}

		if ((event->state & GDK_SHIFT_MASK) != 0){

			switch (event->keyval) {
				/* select row */
			case GDK_space:
				sheet_selection_reset_only (sheet);
				sheet_selection_append_range (
					sheet,
					sheet->cursor_col, sheet->cursor_row,
					0, sheet->cursor_row,
					SHEET_MAX_COLS-1, sheet->cursor_row);
				sheet_redraw_all (sheet);
				return 1;
			}
		}

	}

	switch (event->keyval){
	case GDK_KP_Left:
	case GDK_Left:
		(*movefn_horizontal)(gsheet, -1, jump_to_bounds);
		break;

	case GDK_KP_Right:
	case GDK_Right:
		(*movefn_horizontal)(gsheet, 1, jump_to_bounds);
		break;

	case GDK_KP_Up:
	case GDK_Up:
		(*movefn_vertical)(gsheet, -1, jump_to_bounds);
		break;

	case GDK_KP_Down:
	case GDK_Down:
		(*movefn_vertical)(gsheet, 1, jump_to_bounds);
		break;

	case GDK_KP_Page_Up:
	case GDK_Page_Up:
		if ((event->state & GDK_CONTROL_MASK) != 0)
			gtk_notebook_prev_page (GTK_NOTEBOOK (wb->notebook));
		else
			(*movefn_vertical)(gsheet, -(gsheet->last_visible_row-gsheet->top_row), FALSE);
		break;

	case GDK_KP_Page_Down:
	case GDK_Page_Down:
		if ((event->state & GDK_CONTROL_MASK) != 0)
			gtk_notebook_next_page (GTK_NOTEBOOK (wb->notebook));
		else
			(*movefn_vertical)(gsheet, gsheet->last_visible_row-gsheet->top_row, FALSE);
		break;

	case GDK_KP_Home:
	case GDK_Home:
		if ((event->state & GDK_CONTROL_MASK) != 0){
			sheet_make_cell_visible (sheet, 0, 0);
			sheet_cursor_move (sheet, 0, 0, TRUE, TRUE);
			break;
		} else
			(*movefn_horizontal)(gsheet, -CURSOR_COL(gsheet), FALSE);
		break;

	case GDK_KP_Delete:
	case GDK_Delete:
		sheet_selection_clear (sheet);
		break;

	case GDK_KP_Enter:
	case GDK_Return:
		if ((event->state & GDK_CONTROL_MASK) != 0){
			if (gsheet->item_editor){
				Cell *cell;
				char *text;
				gboolean const is_array =
				    (event->state & GDK_SHIFT_MASK);

				sheet_accept_pending_input (sheet);
				cell = sheet_cell_get (sheet,
						       sheet->cursor_col,
						       sheet->cursor_row);

				/* I am assuming sheet_accept_pending_input
				 * will always create the cell with the given
				 * input (based on the fact that we had an
				 * gsheet->item_editor when we entered this
				 * part of the code
				 */
				g_return_val_if_fail (cell != NULL, 1);
				text = cell_get_text (cell);
				sheet_fill_selection_with (sheet, text,
							   is_array);
				g_free (text);
			} else {
				gtk_widget_grab_focus (gsheet->entry);
			}
			return 1;
		}
		/* fall down */

	case GDK_Tab:
	{
		int col, row;
		int walking_selection;
		int direction, horizontal;

		/* Figure out the direction */
		direction = (event->state & GDK_SHIFT_MASK) ? 0 : 1;
		horizontal = (event->keyval == GDK_Tab) ? 1 : 0;

		walking_selection = sheet_selection_walk_step (
			sheet, direction, horizontal,
			sheet->cursor_col, sheet->cursor_row,
			&col, &row);
		move_cursor (gsheet, col, row, walking_selection == 0);
		break;
	}

	case GDK_Escape:
		sheet_cancel_pending_input (sheet);
		break;

	case GDK_F2:
		gtk_window_set_focus (GTK_WINDOW (wb->toplevel), wb->ea_input);
		sheet_start_editing_at_cursor (sheet, FALSE, FALSE);
		/* fall down */

	default:
		if (!gsheet->item_editor){
			if ((event->state & (GDK_MOD1_MASK|GDK_CONTROL_MASK)) != 0)
				return 0;

			if ((event->keyval >= 0x20 && event->keyval <= 0xff) ||
			    (event->keyval >= GDK_KP_Add && event->keyval <= GDK_KP_9))
				sheet_start_editing_at_cursor (sheet, TRUE, TRUE);
		}
		gnumeric_sheet_stop_cell_selection (gsheet);

		/* Forward the keystroke to the input line */
		return gtk_widget_event (gsheet->entry, (GdkEvent *) event);
	}

	return TRUE;
}

static gint
gnumeric_sheet_key_mode_object (GnumericSheet *gsheet, GdkEventKey *event)
{
	Sheet *sheet = gsheet->sheet_view->sheet;

	switch (event->keyval){
	case GDK_Escape:
		sheet_set_mode_type (sheet, SHEET_MODE_SHEET);
		break;

	case GDK_BackSpace:
	case GDK_Delete:
		gtk_object_destroy (GTK_OBJECT (sheet->current_object));
		sheet->current_object = NULL;
		sheet_set_mode_type (sheet, SHEET_MODE_SHEET);
		break;

	default:
		return FALSE;
	}
	return TRUE;
}

static gint
gnumeric_sheet_key (GtkWidget *widget, GdkEventKey *event)
{
	GnumericSheet *gsheet = GNUMERIC_SHEET (widget);
	Sheet *sheet = gsheet->sheet_view->sheet;

	switch (sheet->mode){
	case SHEET_MODE_SHEET:
		return gnumeric_sheet_key_mode_sheet (gsheet, event);

	case SHEET_MODE_OBJECT_SELECTED:
		return gnumeric_sheet_key_mode_object (gsheet, event);

	default:
		return FALSE;
	}
}

static void
gnumeric_sheet_drag_data_get (GtkWidget *widget,
			      GdkDragContext *context,
			      GtkSelectionData *selection_data,
			      guint info,
			      guint time)
{
#ifdef ENABLE_BONOBO
	GnomeMoniker *moniker;
	Sheet *sheet = GNUMERIC_SHEET (widget)->sheet_view->sheet;
	Workbook *wb = sheet->workbook;
	char *s;
	
	if (wb->filename == NULL)
		workbook_save (wb);
	if (wb->filename == NULL)
		return;
	
	moniker = gnome_moniker_new ();
	gnome_moniker_set_server (
		moniker,
		"IDL:GNOME:Gnumeric:Workbook:1.0",
		wb->filename);

	gnome_moniker_append_item_name (
		moniker, "Sheet1");
	s = gnome_moniker_get_as_string (moniker);
	gtk_object_destroy (GTK_OBJECT (moniker));

	gtk_selection_data_set (selection_data, selection_data->target, 8, s, strlen (s)+1);
#endif
}

static void
gnumeric_sheet_filenames_dropped (GtkWidget        *widget,
				  GdkDragContext   *context,
				  gint              x,
				  gint              y,
				  GtkSelectionData *selection_data,
				  guint             info,
				  guint             time,
				  GnumericSheet    *gsheet)
{
	GList *names, *tmp_list;

	names = gnome_uri_list_extract_filenames ((char *)selection_data->data);
	tmp_list = names;

	while (tmp_list) {
		Workbook *new_wb;

		if ((new_wb = workbook_try_read (tmp_list->data)))
			gtk_widget_show (new_wb->toplevel);
		else
		        sheet_object_drop_file (gsheet, x, y, tmp_list->data);

		tmp_list = tmp_list->next;
	}
}

GtkWidget *
gnumeric_sheet_new (SheetView *sheet_view, ItemBar *colbar, ItemBar *rowbar)
{
	GnomeCanvasItem *item;
	GnumericSheet *gsheet;
	GnomeCanvas   *gsheet_canvas;
	GnomeCanvasGroup *gsheet_group;
	GtkWidget *widget;
	GtkWidget *entry;
	Sheet     *sheet;
	Workbook  *workbook;
	static GtkTargetEntry drag_types[] = {
		{ "text/uri-list", 0, 0 },
	};
	static gint n_drag_types = sizeof (drag_types) / sizeof (drag_types [0]);

	g_return_val_if_fail (sheet_view  != NULL, NULL);
	g_return_val_if_fail (IS_SHEET_VIEW (sheet_view), NULL);
	g_return_val_if_fail (colbar != NULL, NULL);
	g_return_val_if_fail (rowbar != NULL, NULL);
	g_return_val_if_fail (IS_ITEM_BAR (colbar), NULL);
	g_return_val_if_fail (IS_ITEM_BAR (rowbar), NULL);

	sheet = sheet_view->sheet;
	workbook = sheet->workbook;

	entry = workbook->ea_input;
	gsheet = gnumeric_sheet_create (sheet_view, entry);

	/* FIXME: figure out some real size for the canvas scrolling region */
	gnome_canvas_set_scroll_region (GNOME_CANVAS (gsheet), 0, 0, 1000000, 1000000);

	/* handy shortcuts */
	gsheet_canvas = GNOME_CANVAS (gsheet);
	gsheet_group = GNOME_CANVAS_GROUP (gsheet_canvas->root);

	gsheet->colbar = colbar;
	gsheet->rowbar = rowbar;

	/* The grid */
	item = gnome_canvas_item_new (gsheet_group,
				      item_grid_get_type (),
				      "ItemGrid::SheetView", sheet_view,
				      NULL);
	gsheet->item_grid = ITEM_GRID (item);

	/* The cursor */
	item = gnome_canvas_item_new (gsheet_group,
				      item_cursor_get_type (),
				      "ItemCursor::Sheet", sheet,
				      "ItemCursor::Grid", gsheet->item_grid,
				      NULL);
	gsheet->item_cursor = ITEM_CURSOR (item);
	item_cursor_set_bounds (gsheet->item_cursor, 0, 0, 1, 1);

	widget = GTK_WIDGET (gsheet);

	gtk_signal_connect (
		GTK_OBJECT (widget), "drag_data_get",
		GTK_SIGNAL_FUNC (gnumeric_sheet_drag_data_get), NULL);

	gtk_drag_dest_set (widget,
			   GTK_DEST_DEFAULT_ALL,
			   drag_types, n_drag_types,
			   GDK_ACTION_COPY);

	gtk_signal_connect (GTK_OBJECT (widget),
			    "drag_data_received",
			    GTK_SIGNAL_FUNC (gnumeric_sheet_filenames_dropped),
			    widget);

	return widget;
}

gnumeric_sheet_pattern_t gnumeric_sheet_patterns [GNUMERIC_SHEET_PATTERNS] = {
	{ N_("Solid"),
	  { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 } },

        /* xgettext:no-c-format */
	{ N_("75%"),
	  { 0xbb, 0xee, 0xbb, 0xee, 0xbb, 0xee, 0xbb, 0xee } },

        /* xgettext:no-c-format */
	{ N_("50%"),
	  { 0xaa, 0x55, 0xaa, 0x55, 0xaa, 0x55, 0xaa, 0x55 } },

        /* xgettext:no-c-format */
	{ N_("25%"),
	  { 0x22, 0x88, 0x22, 0x88, 0x22, 0x88, 0x22, 0x88 } },

        /* xgettext:no-c-format */
	{ N_("12.5%"),
	  { 0x88, 0x00, 0x22, 0x00, 0x88, 0x00, 0x22, 0x00 } },

        /* xgettext:no-c-format */
	{ N_("6.25%"),
	  { 0x20, 0x00, 0x02, 0x00, 0x20, 0x00, 0x02, 0x00 } },

	{ N_("Horizontal Stripe"),
	  { 0x00, 0x00, 0xff, 0xff, 0x00, 0x00, 0xff, 0xff } },

	{ N_("Vertical Stripe"),
	  { 0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x33 } },

	{ N_("Reverse Diagonal Stripe"),
	  { 0xcc, 0x66, 0x33, 0x99, 0xcc, 0x66, 0x33, 0x99 } },

	{ N_("Diagonal Stripe"),
	  { 0x33, 0x66, 0xcc, 0x99, 0x33, 0x66, 0xcc, 0x99 } },

	{ N_("Diagonal Crosshatch"),
	  { 0x99, 0x66, 0x66, 0x99, 0x99, 0x66, 0x66, 0x99 } },

	{ N_("Thick Diagonal Crosshatch"),
	  { 0xff, 0x66, 0xff, 0x99, 0xff, 0x66, 0xff, 0x99 } },

	{ N_("Thin Horizontal Stripe"),
	  { 0x00, 0x00, 0xff, 0x00, 0x00, 0x00, 0xff, 0x00  } },

	{ N_("Thin Vertical Stripe"),
	  { 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22 } },

	{ N_("Thin Reverse Diagonal Stripe"),
	  { 0x11, 0x22, 0x44, 0x88, 0x11, 0x22, 0x44, 0x88 } },

	{ N_("Thin Diagonal Stripe"),
	  { 0x88, 0x44, 0x22, 0x11, 0x88, 0x44, 0x22, 0x11 } },

	{ N_("Thin Crosshatch"),
	  { 0x22, 0x22, 0xff, 0x22, 0x22, 0x22, 0xff, 0x22 } },

	{ N_("Thin Diagonal Crosshatch"),
	  { 0x88, 0x55, 0x22, 0x55, 0x88, 0x55, 0x22, 0x55, } },
};


static void
gnumeric_sheet_realize (GtkWidget *widget)
{
	GnumericSheet *gsheet = GNUMERIC_SHEET (widget);
	GdkWindow *window;
	int i;

	if (GTK_WIDGET_CLASS (sheet_parent_class)->realize)
		(*GTK_WIDGET_CLASS (sheet_parent_class)->realize)(widget);

	window = widget->window;
	gdk_window_set_back_pixmap (GTK_LAYOUT (widget)->bin_window, NULL, FALSE);

	for (i = 0; i < GNUMERIC_SHEET_PATTERNS; i++)
		gsheet->patterns [i] = gdk_bitmap_create_from_data (
			window, gnumeric_sheet_patterns [i].pattern, 8, 8);

	cursor_set (window, GNUMERIC_CURSOR_FAT_CROSS);
}

void
gnumeric_sheet_compute_visible_ranges (GnumericSheet *gsheet)
{
	GnomeCanvas   *canvas = GNOME_CANVAS (gsheet);
	int pixels, col, row, width, height;

	/* Find out the last visible col and the last full visible column */
	pixels = 0;
	col = gsheet->top_col;
	width = GTK_WIDGET (canvas)->allocation.width;

	do {
		ColRowInfo *ci;
		int cb;

		ci = sheet_col_get_info (gsheet->sheet_view->sheet, col);
		cb = pixels + ci->pixels;

		if (cb == width){
			gsheet->last_visible_col = col;
			gsheet->last_full_col = col;
		} if (cb > width){
			gsheet->last_visible_col = col;
			if (col == gsheet->top_col)
				gsheet->last_full_col = gsheet->top_col;
			else
				gsheet->last_full_col = col - 1;
		}
		pixels = cb;
		col++;
	} while (pixels < width);

	/* Find out the last visible row and the last fully visible row */
	pixels = 0;
	row = gsheet->top_row;
	height = GTK_WIDGET (canvas)->allocation.height;
	do {
		ColRowInfo *ri;
		int cb;

		ri = sheet_row_get_info (gsheet->sheet_view->sheet, row);
		cb = pixels + ri->pixels;

		if (cb == height){
			gsheet->last_visible_row = row;
			gsheet->last_full_row = row;
		} if (cb > height){
			gsheet->last_visible_row = row;
			if (row == gsheet->top_row)
				gsheet->last_full_row = gsheet->top_row;
			else
				gsheet->last_full_row = row - 1;
		}
		pixels = cb;
		row++;
	} while (pixels < height);

	/* Update the scrollbar sizes */
	sheet_view_scrollbar_config (gsheet->sheet_view);
}

static int
gnumeric_sheet_bar_set_top_row (GnumericSheet *gsheet, int new_top_row)
{
	GnomeCanvas *rowc;
	Sheet *sheet;
	int row_distance;
	int x;

	g_return_val_if_fail (gsheet != NULL, 0);
	g_return_val_if_fail (new_top_row >= 0 && new_top_row <= SHEET_MAX_ROWS-1, 0);

	rowc = GNOME_CANVAS_ITEM (gsheet->rowbar)->canvas;
	sheet = gsheet->sheet_view->sheet;
	gsheet->top_row = new_top_row;
	row_distance = sheet_row_get_distance (sheet, 0, gsheet->top_row);

	gnome_canvas_get_scroll_offsets (rowc, &x, NULL);
	gnome_canvas_scroll_to (rowc, x, row_distance);

	return row_distance;
}

void
gnumeric_sheet_set_top_row (GnumericSheet *gsheet, int new_top_row)
{
	int distance, x;

	g_return_if_fail (gsheet != NULL);
	g_return_if_fail (new_top_row >= 0 && new_top_row <= SHEET_MAX_ROWS-1);

	if (gsheet->top_row != new_top_row) {
		distance = gnumeric_sheet_bar_set_top_row (gsheet, new_top_row);
		gnome_canvas_get_scroll_offsets (GNOME_CANVAS (gsheet), &x, NULL);
		gnumeric_sheet_compute_visible_ranges (gsheet);
		gnome_canvas_scroll_to (GNOME_CANVAS (gsheet), x, distance);
	}
}

static int
gnumeric_sheet_bar_set_top_col (GnumericSheet *gsheet, int new_top_col)
{
	GnomeCanvas *colc;
	Sheet *sheet;
	int col_distance;
	int y;

	g_return_val_if_fail (gsheet != NULL, 0);
	g_return_val_if_fail (new_top_col >= 0 && new_top_col <= SHEET_MAX_COLS-1, 0);

	colc = GNOME_CANVAS_ITEM (gsheet->colbar)->canvas;
	sheet = gsheet->sheet_view->sheet;

	gsheet->top_col = new_top_col;
	col_distance = sheet_col_get_distance (sheet, 0, gsheet->top_col);

	gnome_canvas_get_scroll_offsets (colc, NULL, &y);
	gnome_canvas_scroll_to (colc, col_distance, y);

	return col_distance;
}

void
gnumeric_sheet_set_top_col (GnumericSheet *gsheet, int new_top_col)
{
	int distance, y;

	g_return_if_fail (gsheet != NULL);
	g_return_if_fail (new_top_col >= 0 && new_top_col <= SHEET_MAX_COLS-1);

	if (gsheet->top_col != new_top_col) {
		distance = gnumeric_sheet_bar_set_top_col (gsheet, new_top_col);
		gnome_canvas_get_scroll_offsets (GNOME_CANVAS (gsheet), NULL, &y);
		gnumeric_sheet_compute_visible_ranges (gsheet);
		gnome_canvas_scroll_to (GNOME_CANVAS (gsheet), distance, y);
	}
}

void
gnumeric_sheet_make_cell_visible (GnumericSheet *gsheet, int col, int row)
{
	GnomeCanvas *canvas;
	Sheet *sheet;
	int   did_change = 0;
	int   new_top_col, new_top_row;
	int   col_distance, row_distance;

	g_return_if_fail (gsheet != NULL);
	g_return_if_fail (GNUMERIC_IS_SHEET (gsheet));
	g_return_if_fail (col >= 0);
	g_return_if_fail (row >= 0);
	g_return_if_fail (col < SHEET_MAX_COLS);
	g_return_if_fail (row < SHEET_MAX_ROWS);

	sheet = gsheet->sheet_view->sheet;
	canvas = GNOME_CANVAS (gsheet);

	/* Find the new gsheet->top_col */
	if (col < gsheet->top_col){
		new_top_col = col;
	} else if (col > gsheet->last_full_col){
		ColRowInfo *ci;
		int width = GTK_WIDGET (canvas)->allocation.width;
		int allocated = 0;
		int first_col;

		for (first_col = col; first_col > 0; first_col--){
			ci = sheet_col_get_info (sheet, first_col);

			if (allocated + ci->pixels > width)
				break;
			allocated += ci->pixels;
		}
		new_top_col = first_col+1;
	} else
		new_top_col = gsheet->top_col;

	/* Find the new gsheet->top_row */
	if (row < gsheet->top_row){
		new_top_row = row;
	} else if (row > gsheet->last_full_row){
		ColRowInfo *ri;
		int height = GTK_WIDGET (canvas)->allocation.height;
		int allocated = 0;
		int first_row;

		for (first_row = row; first_row > 0; first_row--){
			ri = sheet_row_get_info (sheet, first_row);

			if (allocated + ri->pixels > height)
				break;
			allocated += ri->pixels;
		}
		new_top_row = first_row+1;
	} else
		new_top_row = gsheet->top_row;

	/* Determine if scrolling is required */

	gnome_canvas_get_scroll_offsets (GNOME_CANVAS (gsheet), &col_distance, &row_distance);

	if (gsheet->top_col != new_top_col){
		col_distance = gnumeric_sheet_bar_set_top_col (gsheet, new_top_col);
		did_change = 1;
	}

	if (gsheet->top_row != new_top_row){
		row_distance = gnumeric_sheet_bar_set_top_row (gsheet, new_top_row);
		did_change = 1;
	}

	if (!did_change)
		return;

	gnumeric_sheet_compute_visible_ranges (gsheet);

	gnome_canvas_scroll_to (GNOME_CANVAS (gsheet), col_distance, row_distance);
}

static void
gnumeric_size_allocate (GtkWidget *widget, GtkAllocation *allocation)
{
	(*GTK_WIDGET_CLASS (sheet_parent_class)->size_allocate)(widget, allocation);

	gnumeric_sheet_compute_visible_ranges (GNUMERIC_SHEET (widget));
}

#if 0
static gint
gnumeric_button_press (GtkWidget *widget, GdkEventButton *event)
{
	if (!GTK_WIDGET_HAS_FOCUS (widget))
		gtk_widget_grab_focus (widget);

	return FALSE;
}
#endif

static void
gnumeric_sheet_class_init (GnumericSheetClass *class)
{
	GtkObjectClass *object_class;
	GtkWidgetClass *widget_class;
	GnomeCanvasClass *canvas_class;

	object_class = (GtkObjectClass *) class;
	widget_class = (GtkWidgetClass *) class;
	canvas_class = (GnomeCanvasClass *) class;

	sheet_parent_class = gtk_type_class (gnome_canvas_get_type ());

	/* Method override */
	object_class->destroy = gnumeric_sheet_destroy;

	widget_class->realize              = gnumeric_sheet_realize;
 	widget_class->size_allocate        = gnumeric_size_allocate;
	widget_class->key_press_event      = gnumeric_sheet_key;
}

static void
gnumeric_sheet_init (GnumericSheet *gsheet)
{
	GnomeCanvas *canvas = GNOME_CANVAS (gsheet);

	GTK_WIDGET_SET_FLAGS (canvas, GTK_CAN_FOCUS);
	GTK_WIDGET_SET_FLAGS (canvas, GTK_CAN_DEFAULT);
}

GtkType
gnumeric_sheet_get_type (void)
{
	static GtkType gnumeric_sheet_type = 0;

	if (!gnumeric_sheet_type){
		GtkTypeInfo gnumeric_sheet_info = {
			"GnumericSheet",
			sizeof (GnumericSheet),
			sizeof (GnumericSheetClass),
			(GtkClassInitFunc) gnumeric_sheet_class_init,
			(GtkObjectInitFunc) gnumeric_sheet_init,
			NULL, /* reserved 1 */
			NULL, /* reserved 2 */
			(GtkClassInitFunc) NULL
		};

		gnumeric_sheet_type = gtk_type_unique (gnome_canvas_get_type (), &gnumeric_sheet_info);
	}

	return gnumeric_sheet_type;
}
