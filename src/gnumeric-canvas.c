/*
 * The Gnumeric Sheet widget.
 *
 * Author:
 *     Miguel de Icaza (miguel@kernel.org)
 */
#include <config.h>

#include "gnumeric-sheet.h"
#include "item-bar.h"
#include "item-cursor.h"
#include "item-edit.h"
#include "item-grid.h"
#include "sheet-control-gui.h"
#include "gnumeric-util.h"
#include "color.h"
#include "selection.h"
#include "parse-util.h"
#include "ranges.h"
#include "application.h"
#include "workbook-view.h"
#include "workbook-edit.h"
#include "workbook-control-gui-priv.h"
#include "workbook.h"
#include "commands.h"

#ifdef ENABLE_BONOBO
#  include "sheet-object-container.h"
#endif
#include <gal/widgets/e-cursors.h>

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
gnumeric_sheet_create (SheetControlGUI *sheet_view)
{
	GnumericSheet *gsheet;
	GnomeCanvas   *canvas;

	gsheet = gtk_type_new (gnumeric_sheet_get_type ());
	canvas = GNOME_CANVAS (gsheet);

	gsheet->sheet_view = sheet_view;
	gsheet->row.first = gsheet->row.last_full = gsheet->row.last_visible = 0;
	gsheet->col.first = gsheet->col.last_full = gsheet->col.last_visible = 0;
	gsheet->row_offset.first = gsheet->row_offset.last_full = gsheet->row_offset.last_visible = 0;
	gsheet->col_offset.first = gsheet->col_offset.last_full = gsheet->col_offset.last_visible = 0;

	return gsheet;
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

	/*
	 * Please note that the order here is important, as
	 * the sheet_make_cell_visible call might scroll the
	 * canvas, you should do all of your screen changes
	 * in an atomic fashion.
	 *
	 * The code at some point did do the selection change
	 * after the sheet moved, causing flicker -mig
	 *
	 * If you dont know what this means, just mail me.
	 */

	if (clear_selection)
		sheet_selection_reset_only (sheet);

	/* Set the cursor BEFORE making it visible to decrease flicker */
	workbook_finish_editing (gsheet->sheet_view->wbcg, TRUE);
	sheet_cursor_set (sheet, col, row, col, row, col, row);
	sheet_make_cell_visible (sheet, col, row);

	if (clear_selection)
		sheet_selection_add (sheet, col, row);
}

void
gnumeric_sheet_move_cursor (GnumericSheet *gsheet, int col, int row)
{
	g_return_if_fail (gsheet != NULL);
	g_return_if_fail (GNUMERIC_IS_SHEET (gsheet));

	move_cursor (gsheet, col, row, TRUE);
}

void
gnumeric_sheet_set_cursor_bounds (GnumericSheet *gsheet,
				  int start_col, int start_row,
				  int end_col,   int end_row)
{
	g_return_if_fail (gsheet != NULL);
	g_return_if_fail (GNUMERIC_IS_SHEET (gsheet));
	g_return_if_fail (start_row <= end_row);
	g_return_if_fail (start_col <= end_col);

	item_cursor_set_bounds (
		gsheet->item_cursor,
		start_col, start_row,
		end_col, end_row);
}

/**
 * move_cursor_horizontal:
 *
 * @gsheet : The sheet
 * @count  : Number of units to move the cursor horizontally
 * @jump_to_boundaries: skip from the start to the end of ranges
 *                       of filled or unfilled cells.
 *
 * Moves the cursor count columns
 */
static void
move_cursor_horizontal (GnumericSheet *gsheet, int count,
			gboolean jump_to_boundaries)
{
	Sheet *sheet = gsheet->sheet_view->sheet;
	int const new_col =
	    sheet_find_boundary_horizontal (sheet,
					    sheet->cursor.edit_pos.col,
					    sheet->cursor.edit_pos.row,
					    count, jump_to_boundaries);
	move_cursor (gsheet, new_col, sheet->cursor.edit_pos.row, TRUE);
}

static void
move_horizontal_selection (GnumericSheet *gsheet,
			   int count, gboolean jump_to_boundaries)
{
	sheet_selection_extend (gsheet->sheet_view->sheet,
				count, jump_to_boundaries, TRUE);
}

/**
 * move_cursor_vertical:
 *
 * @gsheet : The sheet
 * @count  : Number of units to move the cursor vertically
 * @jump_to_boundaries: skip from the start to the end of ranges
 *                       of filled or unfilled cells.
 *
 * Moves the cursor count rows
 */
static void
move_cursor_vertical (GnumericSheet *gsheet, int count,
		      gboolean jump_to_boundaries)
{
	Sheet *sheet = gsheet->sheet_view->sheet;
	int const new_row =
	    sheet_find_boundary_vertical (sheet,
					  sheet->cursor.edit_pos.col,
					  sheet->cursor.edit_pos.row,
					  count, jump_to_boundaries);
	move_cursor (gsheet, sheet->cursor.edit_pos.col, new_row, TRUE);
}

static void
move_vertical_selection (GnumericSheet *gsheet,
			 int count, gboolean jump_to_boundaries)
{
	sheet_selection_extend (gsheet->sheet_view->sheet,
				count, jump_to_boundaries, FALSE);
}

/*
 * gnumeric_sheet_can_select_expr_range
 *  @gsheet:   the object
 *
 * Returns true if the cursor keys should be used to select
 * a cell range (if the cursor is in a spot in the expression
 * where it makes sense to have a cell reference), false if not.
 */
gboolean
gnumeric_sheet_can_select_expr_range (GnumericSheet *gsheet)
{
	WorkbookControlGUI const *wbcg;

	g_return_val_if_fail (gsheet != NULL, FALSE);
	g_return_val_if_fail (GNUMERIC_IS_SHEET (gsheet), FALSE);

	wbcg = gsheet->sheet_view->wbcg;
	if (workbook_edit_entry_redirect_p (wbcg))
		return TRUE;

	if (!wbcg->editing)
		return FALSE;

	if (gsheet->selecting_cell)
		return TRUE;

	return workbook_editing_expr (wbcg);
}

static void
selection_remove_selection_string (GnumericSheet *gsheet)
{
	WorkbookControlGUI const *wbcg = gsheet->sheet_view->wbcg;

	if (gsheet->sel_text_len <= 0)
		return;

	gtk_editable_delete_text (
		GTK_EDITABLE (workbook_get_entry_logical (wbcg)),
		gsheet->sel_cursor_pos,
		gsheet->sel_cursor_pos + gsheet->sel_text_len);
}

static void
selection_insert_selection_string (GnumericSheet *gsheet)
{
	ItemCursor *sel = gsheet->sel_cursor;
	Sheet const *sheet = gsheet->sheet_view->sheet;
	WorkbookControlGUI const *wbcg = gsheet->sheet_view->wbcg;
	GtkEditable *editable = GTK_EDITABLE (workbook_get_entry_logical (wbcg));
	gboolean const inter_sheet = (sheet != wbcg->editing_sheet);
	char *buffer;
	int pos;

	/* Get the new selection string */
	buffer = g_strdup_printf ("%s%s%s%d",
				  wbcg->select_abs_col ? "$" : "",
				  col_name (sel->pos.start.col),
				  wbcg->select_abs_row ? "$" : "",
				  sel->pos.start.row+1);

	if (!range_is_singleton (&sel->pos)) {
		char *tmp = g_strdup_printf ("%s:%s%s%s%d",
					      buffer,
					      wbcg->select_abs_col ? "$": "",
					      col_name (sel->pos.end.col),
					      wbcg->select_abs_row ? "$": "",
					      sel->pos.end.row+1);
		g_free (buffer);
		buffer = tmp;
	}

	if (inter_sheet) {
		char *tmp = g_strdup_printf ("%s!%s", sheet->name_quoted,
					     buffer);
		g_free (buffer);
		buffer = tmp;
	}

	gsheet->sel_text_len = strlen (buffer);
	pos = gsheet->sel_cursor_pos;

	gtk_editable_set_position (editable, pos);
	gtk_editable_insert_text (editable, buffer,
				  gsheet->sel_text_len,
				  &pos);

	g_free (buffer);

	/* Set the cursor at the end.  It looks nicer */
	gtk_editable_set_position (editable,
				   gsheet->sel_cursor_pos +
				   gsheet->sel_text_len);
}

static void
start_cell_selection_at (GnumericSheet *gsheet, int col, int row)
{
	GnomeCanvas *canvas = GNOME_CANVAS (gsheet);
	GnomeCanvasGroup *group = GNOME_CANVAS_GROUP (canvas->root);
	Sheet *sheet = gsheet->sheet_view->sheet;
	WorkbookControlGUI *wbcg = gsheet->sheet_view->wbcg;

	g_return_if_fail (gsheet->selecting_cell == FALSE);

	/* Hide the primary cursor while the range selection cursor is visible 
	 * and we are selecting on a different sheet than the expr being edited
	 */
	if (sheet != wb_control_cur_sheet (WORKBOOK_CONTROL (wbcg)))
		item_cursor_set_visibility (gsheet->item_cursor, FALSE);

	gsheet->selecting_cell = TRUE;
	gsheet->sel_cursor = ITEM_CURSOR (gnome_canvas_item_new (
		group,
		item_cursor_get_type (),
		"SheetControlGUI", gsheet->sheet_view,
		"Grid",  gsheet->item_grid,
		"Style", ITEM_CURSOR_ANTED, NULL));
	item_cursor_set_spin_base (gsheet->sel_cursor, col, row);
	item_cursor_set_bounds (ITEM_CURSOR (gsheet->sel_cursor), col, row, col, row);

	/* If we are selecting a range on a different sheet this may be NULL */
	if (gsheet->item_editor)
		item_edit_disable_highlight (ITEM_EDIT (gsheet->item_editor));

	gsheet->sel_cursor_pos = GTK_EDITABLE (workbook_get_entry_logical (wbcg))->current_pos;
	gsheet->sel_text_len = 0;
}

static void
start_cell_selection (GnumericSheet *gsheet)
{
	Sheet *sheet = gsheet->sheet_view->sheet;

	start_cell_selection_at (gsheet, sheet->cursor.edit_pos.col, sheet->cursor.edit_pos.row);
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
gnumeric_sheet_stop_cell_selection (GnumericSheet *gsheet, gboolean const clear_string)
{
	g_return_if_fail (gsheet != NULL);
	g_return_if_fail (GNUMERIC_IS_SHEET (gsheet));

	if (!gsheet->selecting_cell)
		return;

	if (clear_string)
		selection_remove_selection_string (gsheet);
	gsheet->selecting_cell = FALSE;
	gtk_object_destroy (GTK_OBJECT (gsheet->sel_cursor));
	gsheet->sel_cursor = NULL;

	/* If we are selecting a range on a different sheet this may be NULL */
	if (gsheet->item_editor)
		item_edit_enable_highlight (ITEM_EDIT (gsheet->item_editor));

	/* Make the primary cursor visible again */
	item_cursor_set_visibility (gsheet->item_cursor, TRUE);
}

void
gnumeric_sheet_create_editing_cursor (GnumericSheet *gsheet)
{
	GnomeCanvas *canvas = GNOME_CANVAS (gsheet);
	GnomeCanvasItem *item;

	g_return_if_fail (gsheet->item_editor == NULL);

	item = gnome_canvas_item_new (GNOME_CANVAS_GROUP (canvas->root),
				      item_edit_get_type (),
				      "ItemEdit::Grid",     gsheet->item_grid,
				      NULL);

	gsheet->item_editor = ITEM_EDIT (item);
}

void
gnumeric_sheet_stop_editing (GnumericSheet *gsheet)
{
	g_return_if_fail (gsheet != NULL);
	g_return_if_fail (GNUMERIC_IS_SHEET (gsheet));

	gnumeric_sheet_stop_cell_selection (gsheet, FALSE);

	if (gsheet->item_editor != NULL) {
		gtk_object_destroy (GTK_OBJECT (gsheet->item_editor));
		gsheet->item_editor = NULL;
	}
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

	ic = gsheet->sel_cursor;

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

	ic = gsheet->sel_cursor;

	selection_remove_selection_string (gsheet);
	item_cursor_set_bounds (ic, col, row, col, row);
	selection_insert_selection_string (gsheet);
}

void
gnumeric_sheet_selection_cursor_base (GnumericSheet *gsheet, int col, int row)
{
	ItemCursor *ic;

	g_return_if_fail (gsheet != NULL);
	g_return_if_fail (GNUMERIC_IS_SHEET (gsheet));
	g_return_if_fail (gsheet->selecting_cell);
	g_return_if_fail (col < SHEET_MAX_COLS);
	g_return_if_fail (row < SHEET_MAX_ROWS);

	ic = gsheet->sel_cursor;
	item_cursor_set_spin_base (ic, col, row);
}

static void
selection_cursor_move_horizontal (GnumericSheet *gsheet, int dir, gboolean jump_to_boundaries)
{
	ItemCursor *ic;

	g_return_if_fail (dir == -1 || dir == 1);

	if (!gsheet->selecting_cell)
		start_cell_selection (gsheet);

	ic = gsheet->sel_cursor;
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

	/* Ensure that the corner is visible */
	gnumeric_sheet_make_cell_visible (gsheet, ic->base_col, ic->base_row, FALSE);
}

static void
selection_cursor_move_vertical (GnumericSheet *gsheet, int dir, gboolean jump_to_boundaries)
{
	ItemCursor *ic;

	g_return_if_fail (dir == -1 || dir == 1);

	if (!gsheet->selecting_cell)
		start_cell_selection (gsheet);

	ic = gsheet->sel_cursor;
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

	/* Ensure that the corner is visible */
	gnumeric_sheet_make_cell_visible (gsheet, ic->base_col, ic->base_row, FALSE);
}

static void
selection_expand_horizontal (GnumericSheet *gsheet, int n, gboolean jump_to_boundaries)
{
	ItemCursor *ic;
	int start_col, end_col;

	g_return_if_fail (n == -1 || n == 1);

	if (!gsheet->selecting_cell) {
		selection_cursor_move_horizontal (gsheet, n, jump_to_boundaries);
		return;
	}

	ic = gsheet->sel_cursor;
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

	/* Ensure that the corner is visible */
	gnumeric_sheet_make_cell_visible (gsheet, ic->base_col, ic->base_row, FALSE);
}

static void
selection_expand_vertical (GnumericSheet *gsheet, int n, gboolean jump_to_boundaries)
{
	ItemCursor *ic;
	int start_row, end_row;

	g_return_if_fail (n == -1 || n == 1);

	if (!gsheet->selecting_cell) {
		selection_cursor_move_vertical (gsheet, n, jump_to_boundaries);
		return;
	}

	ic = gsheet->sel_cursor;
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

	/* Ensure that the corner is visible */
	gnumeric_sheet_make_cell_visible (gsheet, ic->base_col, ic->base_row, FALSE);
}

/*
 * key press event handler for the gnumeric sheet for the sheet mode
 */
static gint
gnumeric_sheet_key_mode_sheet (GnumericSheet *gsheet, GdkEventKey *event)
{
	Sheet *sheet = gsheet->sheet_view->sheet;
	WorkbookControlGUI *wbcg = gsheet->sheet_view->wbcg;
	void (*movefn_horizontal) (GnumericSheet *, int, gboolean);
	void (*movefn_vertical)   (GnumericSheet *, int, gboolean);
	gboolean const select_expr_range = gnumeric_sheet_can_select_expr_range (gsheet);
	gboolean const jump_to_bounds = event->state & GDK_CONTROL_MASK;

	if (event->state & GDK_SHIFT_MASK) {
		if (select_expr_range) {
			movefn_horizontal = selection_expand_horizontal;
			movefn_vertical = selection_expand_vertical;
		} else {
			movefn_horizontal = move_horizontal_selection;
			movefn_vertical   = move_vertical_selection;
		}
	} else {
		if (select_expr_range) {
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
	if (select_expr_range) {
		switch (event->keyval) {
		case GDK_Shift_L:   case GDK_Shift_R:
		case GDK_Alt_L:     case GDK_Alt_R:
		case GDK_Control_L: case GDK_Control_R:
			return 1;
		}
	}

	/*
	 * Magic : Some of these are accelerators,
	 * we need to catch them before entering because they appear to be printable
	 */
	if (!wbcg->editing && event->keyval == GDK_space &&
	    (event->state & (GDK_SHIFT_MASK|GDK_CONTROL_MASK)))
		return FALSE;

	switch (event->keyval) {
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
			gtk_notebook_prev_page (GTK_NOTEBOOK (wbcg->notebook));
		else if ((event->state & GDK_MOD1_MASK) == 0)
			(*movefn_vertical)(gsheet, -(gsheet->row.last_visible-gsheet->row.first), FALSE);
		else
			(*movefn_horizontal)(gsheet, -(gsheet->col.last_visible-gsheet->col.first), FALSE);
		break;

	case GDK_KP_Page_Down:
	case GDK_Page_Down:
		if ((event->state & GDK_CONTROL_MASK) != 0)
			gtk_notebook_next_page (GTK_NOTEBOOK (wbcg->notebook));
		else if ((event->state & GDK_MOD1_MASK) == 0)
			(*movefn_vertical)(gsheet, gsheet->row.last_visible-gsheet->row.first, FALSE);
		else
			(*movefn_horizontal)(gsheet, gsheet->col.last_visible-gsheet->col.first, FALSE);
		break;

	case GDK_KP_Home:
	case GDK_Home:
		if ((event->state & GDK_CONTROL_MASK) != 0)
			move_cursor (gsheet, 0, 0, TRUE);
		else
			(*movefn_horizontal)(gsheet, -sheet->cursor.edit_pos.col, FALSE);
		break;

	case GDK_KP_Delete:
	case GDK_Delete:
		cmd_clear_selection (WORKBOOK_CONTROL (wbcg), sheet, CLEAR_VALUES);
		break;

	/*
	 * NOTE : Keep these in sync with the condition
	 *        for tabs.
	 */
	case GDK_KP_Enter:
	case GDK_Return:
		if (wbcg->editing &&
		    (event->state == GDK_CONTROL_MASK ||
		     event->state == (GDK_CONTROL_MASK|GDK_SHIFT_MASK) ||
		     event->state == GDK_MOD1_MASK))
			/* Forward the keystroke to the input line */
			return gtk_widget_event (
				GTK_WIDGET (workbook_get_entry_logical (wbcg)),
				(GdkEvent *) event);
		/* fall down */

	case GDK_Tab:
	case GDK_ISO_Left_Tab:
	case GDK_KP_Tab:
	{
		/* Figure out the direction */
		gboolean const direction = (event->state & GDK_SHIFT_MASK) ? FALSE : TRUE;
		gboolean const horizontal = (event->keyval == GDK_KP_Enter ||
					     event->keyval == GDK_Return) ? FALSE : TRUE;

		/* Be careful to restore the editing sheet if we are editing */
		if (wbcg->editing)
			sheet = wbcg->editing_sheet;
		workbook_finish_editing (wbcg, TRUE);
		sheet_selection_walk_step (sheet, direction, horizontal);
		break;
	}

	case GDK_Escape:
		workbook_finish_editing (wbcg, FALSE);
		application_clipboard_unant ();
		break;

	case GDK_F4:
		if (wbcg->editing && gsheet->sel_cursor) {
			selection_remove_selection_string (gsheet);
			wbcg->select_abs_row = (wbcg->select_abs_row == wbcg->select_abs_col);
			wbcg->select_abs_col = !wbcg->select_abs_col;
			selection_insert_selection_string (gsheet);
		}
		break;

	case GDK_F2:
		workbook_start_editing_at_cursor (wbcg, FALSE, FALSE);
		/* fall down */

	default:
		if (!wbcg->editing) {
			if ((event->state & (GDK_MOD1_MASK|GDK_CONTROL_MASK)) != 0)
				return 0;

			/* If the character is not printable do not start editing */
			if (event->length == 0)
				return 0;

			workbook_start_editing_at_cursor (wbcg, TRUE, TRUE);
		}
		gnumeric_sheet_stop_cell_selection (gsheet, FALSE);

		/* Forward the keystroke to the input line */
		return gtk_widget_event (GTK_WIDGET (workbook_get_entry_logical (wbcg)),
					 (GdkEvent *) event);
	}
	if (!wbcg->editing)
		sheet_update (sheet);

	return TRUE;
}

static gint
gnumeric_sheet_key_mode_object (GnumericSheet *gsheet, GdkEventKey *event)
{
	Sheet *sheet = gsheet->sheet_view->sheet;

	switch (event->keyval) {
	case GDK_Escape:
		sheet_mode_edit	(sheet);
		application_clipboard_unant ();
		break;

	case GDK_BackSpace: /* Ick! */
	case GDK_KP_Delete:
	case GDK_Delete:
		gtk_object_destroy (GTK_OBJECT (sheet->current_object));
		break;

	default:
		return FALSE;
	}
	return TRUE;
}

static gint
gnumeric_sheet_key_press (GtkWidget *widget, GdkEventKey *event)
{
	GnumericSheet *gsheet = GNUMERIC_SHEET (widget);
	Sheet *sheet = gsheet->sheet_view->sheet;

	if (sheet->current_object != NULL ||
	    sheet->new_object != NULL)
		return gnumeric_sheet_key_mode_object (gsheet, event);
	return gnumeric_sheet_key_mode_sheet (gsheet, event);
}

static gint
gnumeric_sheet_key_release (GtkWidget *widget, GdkEventKey *event)
{
	GnumericSheet *gsheet = GNUMERIC_SHEET (widget);
	Sheet *sheet = gsheet->sheet_view->sheet;

	/*
	 * The status_region normally displays the current edit_pos
	 * When we extend the selection it changes to displaying the size of
	 * the selected region while we are selecting.  When the shift key
	 * is released, or the mouse button is release we need to reset
	 * to displaying the edit pos.
	 */
	if (sheet->current_object == NULL &&
	    (event->keyval == GDK_Shift_L || event->keyval == GDK_Shift_R))
		wb_control_selection_descr_set (
			WORKBOOK_CONTROL (gsheet->sheet_view->wbcg),
			cell_pos_name (&sheet->cursor.edit_pos));

	return FALSE;
}

/* Focus in handler for the canvas */
static gint
gnumeric_sheet_focus_in (GtkWidget *widget, GdkEventFocus *event)
{
	GnumericSheet *gsheet = GNUMERIC_SHEET (widget);
	if (gsheet->ic)
		gdk_im_begin (gsheet->ic, gsheet->canvas.layout.bin_window);
	return FALSE;
}

/* Focus out handler for the canvas */
static gint
gnumeric_sheet_focus_out (GtkWidget *widget, GdkEventFocus *event)
{
	gdk_im_end ();
	return FALSE;
}

static void
gnumeric_sheet_drag_data_get (GtkWidget *widget,
			      GdkDragContext *context,
			      GtkSelectionData *selection_data,
			      guint info,
			      guint time)
{
#if 0
	BonoboMoniker *moniker;
	Sheet *sheet = GNUMERIC_SHEET (widget)->sheet_view->sheet;
	Workbook *wb = sheet->workbook;
	char *s;
	WorkbookControl *wbc = WORKBOOK_CONTROL (gsheet->sheet_view->wbcg);

	if (wb->filename == NULL)
		workbook_save (wbc, wb);
	if (wb->filename == NULL)
		return;

	moniker = bonobo_moniker_new ();
	bonobo_moniker_set_server (
		moniker,
		"IDL:GNOME:Gnumeric:Workbook:1.0",
		wb->filename);

	bonobo_moniker_append_item_name (
		moniker, "Sheet1");
	s = bonobo_moniker_get_as_string (moniker);
	gtk_object_destroy (GTK_OBJECT (moniker));

	gtk_selection_data_set (selection_data, selection_data->target, 8, s, strlen (s)+1);
#endif
}

/*
 * gnumeric_sheet_filenames_dropped :
 */
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
	WorkbookControl *wbc = WORKBOOK_CONTROL (gsheet->sheet_view->wbcg);

	names = gnome_uri_list_extract_filenames ((char *)selection_data->data);

	for (tmp_list = names; tmp_list != NULL ; tmp_list = tmp_list->next) {
		WorkbookView *new_wb = workbook_try_read (wbc, tmp_list->data);

		if (new_wb == NULL) {
#ifdef ENABLE_BONOBO
			/* If it wasn't a workbook, see if we have a control for it */
			SheetObject *so = sheet_object_container_new_file (
				gsheet->sheet_view->sheet, tmp_list->data);
			if (so != NULL)
				sheet_mode_create_object (so);
#endif
		}
	}
}

GtkWidget *
gnumeric_sheet_new (SheetControlGUI *sheet_view, ItemBar *colbar, ItemBar *rowbar)
{
	GnomeCanvasItem *item;
	GnumericSheet *gsheet;
	GnomeCanvas   *gsheet_canvas;
	GnomeCanvasGroup *gsheet_group;
	GtkWidget *widget;
	Sheet     *sheet;
	Workbook  *workbook;
	static GtkTargetEntry drag_types[] = {
		{ "text/uri-list", 0, 0 },
	};
	static gint n_drag_types = sizeof (drag_types) / sizeof (drag_types [0]);

	g_return_val_if_fail (sheet_view  != NULL, NULL);
	g_return_val_if_fail (IS_SHEET_CONTROL_GUI (sheet_view), NULL);
	g_return_val_if_fail (colbar != NULL, NULL);
	g_return_val_if_fail (rowbar != NULL, NULL);
	g_return_val_if_fail (IS_ITEM_BAR (colbar), NULL);
	g_return_val_if_fail (IS_ITEM_BAR (rowbar), NULL);

	sheet = sheet_view->sheet;
	workbook = sheet->workbook;

	gsheet = gnumeric_sheet_create (sheet_view);

	/* FIXME: figure out some real size for the canvas scrolling region */
	gnome_canvas_set_scroll_region (GNOME_CANVAS (gsheet), 0, 0,
					GNUMERIC_SHEET_FACTOR_X, GNUMERIC_SHEET_FACTOR_Y);

	/* handy shortcuts */
	gsheet_canvas = GNOME_CANVAS (gsheet);
	gsheet_group = GNOME_CANVAS_GROUP (gsheet_canvas->root);

	gsheet->colbar = colbar;
	gsheet->rowbar = rowbar;

	/* The grid */
	item = gnome_canvas_item_new (gsheet_group,
				      item_grid_get_type (),
				      "ItemGrid::SheetControlGUI", sheet_view,
				      NULL);
	gsheet->item_grid = ITEM_GRID (item);

	/* The cursor */
	item = gnome_canvas_item_new (gsheet_group,
				      item_cursor_get_type (),
				      "ItemCursor::SheetControlGUI", sheet_view,
				      "ItemCursor::Grid", gsheet->item_grid,
				      NULL);
	gsheet->item_cursor = ITEM_CURSOR (item);

	/* Set the cursor in A1 */
	item_cursor_set_bounds (gsheet->item_cursor, 0, 0, 0, 0);

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

static void
gnumeric_sheet_realize (GtkWidget *widget)
{
	GdkWindow *window;
	GnumericSheet *gsheet;

	if (GTK_WIDGET_CLASS (sheet_parent_class)->realize)
		(*GTK_WIDGET_CLASS (sheet_parent_class)->realize)(widget);

	window = widget->window;
	gdk_window_set_back_pixmap (GTK_LAYOUT (widget)->bin_window, NULL, FALSE);

	e_cursor_set (window, E_CURSOR_FAT_CROSS);

	gsheet = GNUMERIC_SHEET (widget);
	if (gdk_im_ready () && (gsheet->ic_attr = gdk_ic_attr_new ()) != NULL) {
		GdkEventMask mask;
		GdkICAttr *attr = gsheet->ic_attr;
		GdkICAttributesType attrmask = GDK_IC_ALL_REQ;
		GdkIMStyle style;
		GdkIMStyle supported_style = GDK_IM_PREEDIT_NONE |
			GDK_IM_PREEDIT_NOTHING |
			GDK_IM_STATUS_NONE |
			GDK_IM_STATUS_NOTHING;

		attr->style = style = gdk_im_decide_style (supported_style);
		attr->client_window = gsheet->canvas.layout.bin_window;

		gsheet->ic = gdk_ic_new (attr, attrmask);
		if (gsheet->ic != NULL) {
			mask = gdk_window_get_events (attr->client_window);
			mask |= gdk_ic_get_events (gsheet->ic);
			gdk_window_set_events (attr->client_window, mask);

			if (GTK_WIDGET_HAS_FOCUS (widget))
				gdk_im_begin (gsheet->ic, attr->client_window);
		} else
			g_warning ("Can't create input context.");
	}
}

static void
gnumeric_sheet_unrealize (GtkWidget *widget)
{
	GnumericSheet *gsheet;

	gsheet = GNUMERIC_SHEET (widget);
	g_return_if_fail (gsheet != NULL);

	if (gsheet->ic) {
		gdk_ic_destroy (gsheet->ic);
		gsheet->ic = NULL;
	}
	if (gsheet->ic_attr) {
		gdk_ic_attr_destroy (gsheet->ic_attr);
		gsheet->ic_attr = NULL;
	}

	(*GTK_WIDGET_CLASS (sheet_parent_class)->unrealize)(widget);
}

/*
 * gnumeric_sheet_compute_visible_ranges : Keeps the top left col/row the same and
 *     recalculates the visible boundaries.
 *
 * @full_recompute :
 *       if TRUE recompute the pixel offsets of the top left row/col
 *       else assumes that the pixel offsets of the top left have not changed.
 */
void
gnumeric_sheet_compute_visible_ranges (GnumericSheet *gsheet,
				       gboolean const full_recompute)
{
	Sheet const * const sheet = gsheet->sheet_view->sheet;
	GnomeCanvas   *canvas = GNOME_CANVAS (gsheet);
	int pixels, col, row, width, height;

	/* When col/row sizes change we need to do a full recompute */
	if (full_recompute) {
		GnomeCanvas *canvas;

		gsheet->col_offset.first =
		    sheet_col_get_distance_pixels (sheet, 0, gsheet->col.first);
		canvas = GNOME_CANVAS_ITEM (gsheet->colbar)->canvas;
		gnome_canvas_scroll_to (canvas, gsheet->col_offset.first, 0);

		gsheet->row_offset.first =
		    sheet_row_get_distance_pixels (sheet, 0, gsheet->row.first);
		canvas = GNOME_CANVAS_ITEM (gsheet->rowbar)->canvas;
		gnome_canvas_scroll_to (canvas, 0, gsheet->row_offset.first);

		gnome_canvas_scroll_to (GNOME_CANVAS (gsheet),
					gsheet->col_offset.first,
					gsheet->row_offset.first);
	}

	/* Find out the last visible col and the last full visible column */
	pixels = 0;
	col = gsheet->col.first;
	width = GTK_WIDGET (canvas)->allocation.width;

	do {
		ColRowInfo const * const ci = sheet_col_get_info (sheet, col);
		if (ci->visible) {
			int const bound = pixels + ci->size_pixels;

			if (bound == width) {
				gsheet->col.last_visible = col;
				gsheet->col.last_full = col;
				break;
			}
			if (bound > width) {
				gsheet->col.last_visible = col;
				if (col == gsheet->col.first)
					gsheet->col.last_full = gsheet->col.first;
				else
					gsheet->col.last_full = col - 1;
				break;
			}
			pixels = bound;
		}
		++col;
	} while (pixels < width && col < SHEET_MAX_COLS);

	if (col >= SHEET_MAX_COLS) {
		gsheet->col.last_visible = SHEET_MAX_COLS-1;
		gsheet->col.last_full = SHEET_MAX_COLS-1;
	}

	/* Find out the last visible row and the last fully visible row */
	pixels = 0;
	row = gsheet->row.first;
	height = GTK_WIDGET (canvas)->allocation.height;
	do {
		ColRowInfo const * const ri = sheet_row_get_info (sheet, row);
		if (ri->visible) {
			int const bound = pixels + ri->size_pixels;

			if (bound == height) {
				gsheet->row.last_visible = row;
				gsheet->row.last_full = row;
				break;
			}
			if (bound > height) {
				gsheet->row.last_visible = row;
				if (row == gsheet->row.first)
					gsheet->row.last_full = gsheet->row.first;
				else
					gsheet->row.last_full = row - 1;
				break;
			}
			pixels = bound;
		}
		++row;
	} while (pixels < height && row < SHEET_MAX_ROWS);

	if (row >= SHEET_MAX_ROWS) {
		gsheet->row.last_visible = SHEET_MAX_ROWS-1;
		gsheet->row.last_full = SHEET_MAX_ROWS-1;
	}

	/* Update the scrollbar sizes */
	sheet_view_scrollbar_config (gsheet->sheet_view);

	/* Force the cursor to update its bounds relative to the new visible region */
	item_cursor_reposition (gsheet->item_cursor);
}

static int
gnumeric_sheet_bar_set_top_row (GnumericSheet *gsheet, int new_first_row)
{
	GnomeCanvas *rowc;
	Sheet *sheet;
	int row_distance;
	int x;

	g_return_val_if_fail (gsheet != NULL, 0);
	g_return_val_if_fail (gsheet->item_grid != NULL, 0);
	g_return_val_if_fail (0 <= new_first_row && new_first_row < SHEET_MAX_ROWS, 0);

	rowc = GNOME_CANVAS_ITEM (gsheet->rowbar)->canvas;
	sheet = gsheet->sheet_view->sheet;
	row_distance = gsheet->row_offset.first +=
	    sheet_row_get_distance_pixels (sheet, gsheet->row.first, new_first_row);
	gsheet->row.first = new_first_row;

	/* Scroll the row headers */
	gnome_canvas_get_scroll_offsets (rowc, &x, NULL);
	gnome_canvas_scroll_to (rowc, x, row_distance);

	return row_distance;
}

void
gnumeric_sheet_set_top_row (GnumericSheet *gsheet, int new_first_row)
{
	g_return_if_fail (gsheet != NULL);
	g_return_if_fail (0 <= new_first_row && new_first_row < SHEET_MAX_ROWS);

	if (gsheet->row.first != new_first_row) {
		int x;
		GnomeCanvas * const canvas =
		    GNOME_CANVAS(gsheet);
		int const distance =
		    gnumeric_sheet_bar_set_top_row (gsheet, new_first_row);

		gnumeric_sheet_compute_visible_ranges (gsheet, FALSE);

		/* Scroll the cell canvas */
		gnome_canvas_get_scroll_offsets (canvas, &x, NULL);
		gnome_canvas_scroll_to (canvas, x, distance);
	}
}

static int
gnumeric_sheet_bar_set_left_col (GnumericSheet *gsheet, int new_first_col)
{
	GnomeCanvas *colc;
	Sheet *sheet;
	int col_distance;
	int y;

	g_return_val_if_fail (gsheet != NULL, 0);
	g_return_val_if_fail (gsheet->item_grid != NULL, 0);
	g_return_val_if_fail (0 <= new_first_col && new_first_col < SHEET_MAX_COLS, 0);

	colc = GNOME_CANVAS_ITEM (gsheet->colbar)->canvas;
	sheet = gsheet->sheet_view->sheet;

	col_distance = gsheet->col_offset.first +=
	    sheet_col_get_distance_pixels (sheet, gsheet->col.first, new_first_col);
	gsheet->col.first = new_first_col;

	/* Scroll the column headers */
	gnome_canvas_get_scroll_offsets (colc, NULL, &y);
	gnome_canvas_scroll_to (colc, col_distance, y);

	return col_distance;
}

void
gnumeric_sheet_set_left_col (GnumericSheet *gsheet, int new_first_col)
{
	g_return_if_fail (gsheet != NULL);
	g_return_if_fail (0 <= new_first_col && new_first_col < SHEET_MAX_COLS);

	if (gsheet->col.first != new_first_col) {
		int y;
		GnomeCanvas * const canvas =
		    GNOME_CANVAS(gsheet);
		int const distance =
		    gnumeric_sheet_bar_set_left_col (gsheet, new_first_col);

		gnumeric_sheet_compute_visible_ranges (gsheet, FALSE);

		/* Scroll the cell canvas */
		gnome_canvas_get_scroll_offsets (canvas, NULL, &y);
		gnome_canvas_scroll_to (canvas, distance, y);
	}
}

/*
 * gnumeric_sheet_make_cell_visible
 * @gsheet        sheet widget
 * @col           column
 * @row           row
 * @force_scroll  force a scroll
 *
 * Ensure that cell (col, row) is visible.
 * Sheet is scrolled if cell is outside viewport.
 *
 * Avoid calling this before the canvas is realized:
 * We do not know the visible area, and would unconditionally scroll the cell
 * to the top left of the viewport.
 */
void
gnumeric_sheet_make_cell_visible (GnumericSheet *gsheet, int col, int row,
				  gboolean const force_scroll)
{
	GnomeCanvas *canvas;
	Sheet *sheet;
	int   did_change = 0;
	int   new_first_col, new_first_row;
	int   col_distance, row_distance;

	g_return_if_fail (gsheet != NULL);
	g_return_if_fail (GNUMERIC_IS_SHEET (gsheet));
	g_return_if_fail (col >= 0);
	g_return_if_fail (row >= 0);
	g_return_if_fail (col < SHEET_MAX_COLS);
	g_return_if_fail (row < SHEET_MAX_ROWS);

	sheet = gsheet->sheet_view->sheet;
	canvas = GNOME_CANVAS (gsheet);

	/* Find the new gsheet->col.first */
	if (col < gsheet->col.first) {
		new_first_col = col;
	} else if (col > gsheet->col.last_full) {
		int width = GTK_WIDGET (canvas)->allocation.width;
		int first_col;

		for (first_col = col; first_col > 0; --first_col) {
			ColRowInfo const * const ci = sheet_col_get_info (sheet, first_col);
			if (ci->visible) {
				width -= ci->size_pixels;
				if (width < 0)
					break;
			}
		}
		new_first_col = first_col+1;
	} else
		new_first_col = gsheet->col.first;

	/* Find the new gsheet->row.first */
	if (row < gsheet->row.first) {
		new_first_row = row;
	} else if (row > gsheet->row.last_full) {
		int height = GTK_WIDGET (canvas)->allocation.height;
		int first_row;

		for (first_row = row; first_row > 0; --first_row) {
			ColRowInfo const * const ri = sheet_row_get_info (sheet, first_row);
			if (ri->visible) {
				height -= ri->size_pixels;
				if (height < 0)
					break;
			}
		}
		new_first_row = first_row+1;
	} else
		new_first_row = gsheet->row.first;

	/* Determine if scrolling is required */
	gnome_canvas_get_scroll_offsets (GNOME_CANVAS (gsheet), &col_distance, &row_distance);

	if (gsheet->col.first != new_first_col || force_scroll) {
		if (force_scroll) {
			/* Clear the offsets in case col/row size changed */
			gsheet->col_offset.first = 0;
			gsheet->col.first = 0;
		}
		col_distance = gnumeric_sheet_bar_set_left_col (gsheet, new_first_col);
		did_change = 1;
	}

	if (gsheet->row.first != new_first_row || force_scroll) {
		if (force_scroll) {
			/* Clear the offsets in case col/row size changed */
			gsheet->row_offset.first = 0;
			gsheet->row.first = 0;
		}
		row_distance = gnumeric_sheet_bar_set_top_row (gsheet, new_first_row);
		did_change = 1;
	}

	if (!did_change && !force_scroll)
		return;

	gnumeric_sheet_compute_visible_ranges (gsheet, FALSE);

	gnome_canvas_scroll_to (GNOME_CANVAS (gsheet), col_distance, row_distance);
}

static void
gnumeric_sheet_size_allocate (GtkWidget *widget, GtkAllocation *allocation)
{
	(*GTK_WIDGET_CLASS (sheet_parent_class)->size_allocate)(widget, allocation);

	gnumeric_sheet_compute_visible_ranges (GNUMERIC_SHEET (widget), FALSE);
}

static void
gnumeric_sheet_class_init (GnumericSheetClass *Class)
{
	GtkObjectClass *object_class;
	GtkWidgetClass *widget_class;
	GnomeCanvasClass *canvas_class;

	object_class = (GtkObjectClass *) Class;
	widget_class = (GtkWidgetClass *) Class;
	canvas_class = (GnomeCanvasClass *) Class;

	sheet_parent_class = gtk_type_class (gnome_canvas_get_type ());

	/* Method override */
	object_class->destroy = gnumeric_sheet_destroy;

	widget_class->realize           = gnumeric_sheet_realize;
	widget_class->unrealize		= gnumeric_sheet_unrealize;
 	widget_class->size_allocate     = gnumeric_sheet_size_allocate;
	widget_class->key_press_event   = gnumeric_sheet_key_press;
	widget_class->key_release_event = gnumeric_sheet_key_release;
	widget_class->focus_in_event	= gnumeric_sheet_focus_in;
	widget_class->focus_out_event	= gnumeric_sheet_focus_out;
}

static void
gnumeric_sheet_init (GnumericSheet *gsheet)
{
	GnomeCanvas *canvas = GNOME_CANVAS (gsheet);

	gsheet->ic = NULL;
	gsheet->ic_attr = NULL;

	GTK_WIDGET_SET_FLAGS (canvas, GTK_CAN_FOCUS);
	GTK_WIDGET_SET_FLAGS (canvas, GTK_CAN_DEFAULT);
}

GtkType
gnumeric_sheet_get_type (void)
{
	static GtkType gnumeric_sheet_type = 0;

	if (!gnumeric_sheet_type) {
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

/*
 * gnumeric_sheet_find_col: return the column containing pixel x
 */
int
gnumeric_sheet_find_col (GnumericSheet *gsheet, int x, int *col_origin)
{
	Sheet *sheet = gsheet->sheet_view->sheet;
	int col   = gsheet->col.first;
	int pixel = gsheet->col_offset.first;

	if (x < pixel) {
		while (col > 0) {
			ColRowInfo *ci = sheet_col_get_info (sheet, --col);
			if (ci->visible) {
				pixel -= ci->size_pixels;
				if (x >= pixel) {
					if (col_origin)
						*col_origin = pixel;
					return col;
				}
			}
		}
		if (col_origin)
			*col_origin = 1; /* there is a 1 pixel edge at the left */
		return 0;
	}

	do {
		ColRowInfo *ci = sheet_col_get_info (sheet, col);
		if (ci->visible) {
			int const tmp = ci->size_pixels;
			if (x <= pixel + tmp) {
				if (col_origin)
					*col_origin = pixel;
				return col;
			}
			pixel += tmp;
		}
	} while (++col < SHEET_MAX_COLS);
	if (col_origin)
		*col_origin = pixel;
	return SHEET_MAX_COLS-1;
}

/*
 * gnumeric_sheet_find_row: return the row where y belongs to
 */
int
gnumeric_sheet_find_row (GnumericSheet *gsheet, int y, int *row_origin)
{
	Sheet *sheet = gsheet->sheet_view->sheet;
	int row   = gsheet->row.first;
	int pixel = gsheet->row_offset.first;

	if (y < pixel) {
		while (row > 0) {
			ColRowInfo *ri = sheet_row_get_info (sheet, --row);
			if (ri->visible) {
				pixel -= ri->size_pixels;
				if (y >= pixel) {
					if (row_origin)
						*row_origin = pixel;
					return row;
				}
			}
		}
		if (row_origin)
			*row_origin = 1; /* there is a 1 pixel edge on top */
		return 0;
	}

	do {
		ColRowInfo *ri = sheet_row_get_info (sheet, row);
		if (ri->visible) {
			int const tmp = ri->size_pixels;
			if (pixel <= y && y <= pixel + tmp) {
				if (row_origin)
					*row_origin = pixel;
				return row;
			}
			pixel += tmp;
		}
	} while (++row < SHEET_MAX_ROWS);
	if (row_origin)
		*row_origin = pixel;
	return SHEET_MAX_ROWS-1;
}

