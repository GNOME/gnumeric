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

#define CURSOR_COL(gsheet) gsheet->sheet_view->sheet->cursor_col
#define CURSOR_ROW(gsheet) gsheet->sheet_view->sheet->cursor_row

/* Public colors: shared by all of our items in Gnumeric */

GdkColor gs_white, gs_black, gs_light_gray, gs_dark_gray, gs_red;


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
 *  @gsheet: The sheet
 *  @col:    the column
 *  @row:    the row
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
		ha->value = col;
		va->value = row;
		
		gtk_adjustment_value_changed (ha);
		gtk_adjustment_value_changed (va);
	}
}

/*
 * gnumeric_sheet_set_selection:
 *  @gsheet:    The sheet name
 *  @start_col: The starting column.
 *  @start_row: The starting row
 *  @end_col:   The end column
 *  @end_row:   The end row
 *
 * Set the current selection to cover the inclusive area delimited by
 * start_col, start_row, end_col and end_row.  The actual cursor is
 * placed at base_col, base_row
 */
void
gnumeric_sheet_set_selection (GnumericSheet *gsheet, SheetSelection *ss)
{
	g_return_if_fail (gsheet != NULL);
	g_return_if_fail (ss != NULL);
	g_return_if_fail (GNUMERIC_IS_SHEET (gsheet));

	sheet_cursor_set (
		gsheet->sheet_view->sheet,
		ss->base_col, ss->base_row,
		ss->start_col, ss->start_row,
		ss->end_col, ss->end_row);
}

/*
 * move_cursor:
 *   @Sheet:    The sheet where the cursor is located
 *   @col:      The new column for the cursor.
 *   @row:      The new row for the cursor.
 *   @clear_selection: If set, clear the selection before moving
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
 *
 * Moves the cursor count columns
 */
static void
move_cursor_horizontal (GnumericSheet *gsheet, int count)
{
	Sheet *sheet = gsheet->sheet_view->sheet;
	int new_left;
	
	new_left = sheet->cursor_col + count;

	if (new_left < 0)
		new_left = 0;
	if (new_left > SHEET_MAX_COLS-1)
		new_left = SHEET_MAX_COLS-1;
	
	move_cursor (gsheet, new_left, sheet->cursor_row, TRUE);
}

/*
 * move_cursor_vertical:
 *  @Sheet:  The sheet name
 *  @count:  number of units to move the cursor vertically
 *
 * Moves the cursor count rows
 */
static void
move_cursor_vertical (GnumericSheet *gsheet, int count)
{
	Sheet *sheet = gsheet->sheet_view->sheet;
	int new_top;

	new_top = CURSOR_ROW (gsheet) + count;

	if (new_top < 0)
		new_top = 0;
	if (new_top > SHEET_MAX_ROWS-1)
		new_top = SHEET_MAX_ROWS-1;
	
	move_cursor (gsheet, sheet->cursor_col, new_top, TRUE);
}

static void
move_horizontal_selection (GnumericSheet *gsheet, int count)
{
	sheet_selection_extend_horizontal (gsheet->sheet_view->sheet, count);
}

static void
move_vertical_selection (GnumericSheet *gsheet, int count)
{
	sheet_selection_extend_vertical (gsheet->sheet_view->sheet, count);
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
	GtkEntry *entry = GTK_ENTRY (gsheet->entry);
	int cursor_pos = GTK_EDITABLE (entry)->current_pos;

	if (gsheet->selecting_cell)
		return TRUE;
	
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
	strcpy (buffer, cell_name (sel->start_col, sel->start_row));
	if (!(sel->start_col == sel->end_col && sel->start_row == sel->end_row)){
		strcat (buffer, ":");
		strcat (buffer, cell_name (sel->end_col, sel->end_row));
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
selection_cursor_move_horizontal (GnumericSheet *gsheet, int dir)
{
	ItemCursor *ic;

	g_return_if_fail (dir == -1 || dir == 1);
	
	if (!gsheet->selecting_cell)
		start_cell_selection (gsheet);

	ic = gsheet->selection;
	if (dir == -1){
		if (ic->start_col == 0)
			return;
	} else {
		if (ic->end_col + 1 > (SHEET_MAX_COLS-1))
			return;
	}
	
	selection_remove_selection_string (gsheet);
	item_cursor_set_bounds (ic, 
				ic->start_col + dir,
				ic->start_row, 
				ic->end_col + dir,
				ic->end_row);
	selection_insert_selection_string (gsheet);
}

static void
selection_cursor_move_vertical (GnumericSheet *gsheet, int dir)
{
	ItemCursor *ic;

	g_return_if_fail (dir == -1 || dir == 1);

	if (!gsheet->selecting_cell)
		start_cell_selection (gsheet);

	ic = gsheet->selection;
	if (dir == -1){
		if (ic->start_row == 0)
			return;
	} else {
		if (ic->end_row + 1 > (SHEET_MAX_ROWS-1))
			return;
	}

	if (!gsheet->selecting_cell)
		start_cell_selection (gsheet);
	item_cursor_set_bounds (ic,
				ic->start_col,
				ic->start_row + dir,
				ic->end_col,
				ic->end_row + dir);
	selection_remove_selection_string (gsheet);
	selection_insert_selection_string (gsheet);
}

static void
selection_expand_horizontal (GnumericSheet *gsheet, int dir)
{
	ItemCursor *ic;

	g_return_if_fail (dir == -1 || dir == 1);

	if (!gsheet->selecting_cell){
		selection_cursor_move_vertical (gsheet, dir);
		return;
	}

	ic = gsheet->selection;
	if (ic->end_col == SHEET_MAX_COLS-1)
			return;

	if (dir == -1 && ic->start_col == ic->end_col)
		return;
	
	selection_remove_selection_string (gsheet);
	item_cursor_set_bounds (ic,
				ic->start_col,
				ic->start_row,
				ic->end_col  + dir,
				ic->end_row);
	selection_insert_selection_string (gsheet);
}

static void
selection_expand_vertical (GnumericSheet *gsheet, int dir)
{
	ItemCursor *ic;

	g_return_if_fail (dir == -1 || dir == 1);

	if (!gsheet->selecting_cell){
		selection_cursor_move_vertical (gsheet, dir);
		return;
	}

	ic = gsheet->selection;
	
	if (ic->end_row == SHEET_MAX_ROWS-1)
		return;

	if (dir == -1 && ic->start_row == ic->end_row)
		return;
	
	selection_remove_selection_string (gsheet);
	item_cursor_set_bounds (ic,
				ic->start_col,
				ic->start_row,
				ic->end_col,
				ic->end_row + dir);
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
	void (*movefn_horizontal) (GnumericSheet *, int);
	void (*movefn_vertical)   (GnumericSheet *, int);
	int  cursor_move = gnumeric_sheet_can_move_cursor (gsheet);
	
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
		(*movefn_horizontal)(gsheet, -1);
		break;

	case GDK_KP_Right:
	case GDK_Right:
		(*movefn_horizontal)(gsheet, 1);
		break;

	case GDK_KP_Up:
	case GDK_Up:
		(*movefn_vertical)(gsheet, -1);
		break;

	case GDK_KP_Down:
	case GDK_Down:
		(*movefn_vertical)(gsheet, 1);
		break;

	case GDK_KP_Page_Up:
	case GDK_Page_Up:
	        (*movefn_vertical)(gsheet, -(gsheet->last_visible_row-gsheet->top_row));
		break;

	case GDK_KP_Page_Down:
	case GDK_Page_Down:
	        (*movefn_vertical)(gsheet, gsheet->last_visible_row-gsheet->top_row);
		break;

	case GDK_KP_Home:
	case GDK_Home:
		if ((event->state & GDK_CONTROL_MASK) != 0){
			sheet_make_cell_visible (sheet, 0, 0);
			sheet_cursor_move (sheet, 0, 0);
			break;
		} else
			(*movefn_horizontal)(gsheet, -CURSOR_COL(gsheet));
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
				sheet_fill_selection_with (sheet, text);
				g_free (text);
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
		/* fall down */

	default:
		if (!gsheet->item_editor){
			if ((event->state & (GDK_MOD1_MASK|GDK_CONTROL_MASK)) != 0)
				return 0;
			
			if ((event->keyval >= 0x20 && event->keyval <= 0xff) ||
			    (event->keyval >= GDK_KP_Add && event->keyval <= GDK_KP_9))
				sheet_start_editing_at_cursor (sheet);
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
		sheet_object_destroy (sheet->current_object);
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

	return widget;
}

gnumeric_sheet_pattern_t gnumeric_sheet_patterns [GNUMERIC_SHEET_PATTERNS] = {
	{ N_("75%"),
	  { 0xee, 0xbb, 0xee, 0xbb, 0xee, 0xbb, 0xee, 0xbb } },

	{ N_("50%"),
	  { 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55 } },

	{ N_("25%"),
	  { 0x11, 0x44, 0x11, 0x44, 0x11, 0x44, 0x11, 0x44 } },

	{ N_("12%"),
	  { 0x01, 0x08, 0x40, 0x02, 0x10, 0x80, 0x04, 0x20 } },
	
	{ N_("6%"),
	  { 0x80, 0x00, 0x04, 0x00, 0x80, 0x00, 0x04, 0x00 } },

	{ N_("Horizontal lines"),
	  { 0xff, 0x00, 0xff, 0x00, 0xff, 0x00, 0xff, 0x00 } },

	{ N_("Vertical lines"),
	  { 0x55, 0xaa, 0x55, 0xaa, 0x55, 0xaa, 0x55, 0xaa } },

	{ N_("Diagonal lines"),
	  { 0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80 } },

	{ N_("Diagonal lines"),
	  { 0x80, 0x40, 0x20, 0x10, 0x08, 0x04, 0x02, 0x01 } },

	{ N_("Diagonal lines"),
	  { 0x11, 0x22, 0x44, 0x88, 0x11, 0x22, 0x44, 0x88 } },

	{ N_("Diagonal lines"),
	  { 0x88, 0x44, 0x22, 0x11, 0x88, 0x44, 0x22, 0x11 } },

	{ N_("Crossed diagonals"),
	  { 0x81, 0x42, 0x24, 0x18, 0x18, 0x24, 0x42, 0x81 } },

	{ N_("Bricks"),
	  { 0x80, 0x80, 0x80, 0xff, 0x04, 0x04, 0x04, 0xff } },
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
			if (col == gsheet->top_row)
				gsheet->last_full_row = gsheet->top_row;
			else
				gsheet->last_full_row = row - 1;
		}
		pixels = cb;
		row++;
	} while (pixels < height);
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

	distance = gnumeric_sheet_bar_set_top_row (gsheet, new_top_row);
	gnome_canvas_get_scroll_offsets (GNOME_CANVAS (gsheet), &x, NULL);
	gnome_canvas_scroll_to (GNOME_CANVAS (gsheet), x, distance);
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

	distance = gnumeric_sheet_bar_set_top_col (gsheet, new_top_col);
	gnome_canvas_get_scroll_offsets (GNOME_CANVAS (gsheet), NULL, &y);
	gnome_canvas_scroll_to (GNOME_CANVAS (gsheet), distance, y);
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

static gint
gnumeric_button_press (GtkWidget *widget, GdkEventButton *event)
{
	if (!GTK_WIDGET_HAS_FOCUS (widget))
		gtk_widget_grab_focus (widget);

	return FALSE;
}

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

