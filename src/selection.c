/*
 * selection.c:  Manage selection regions.
 *
 * Author:
 *  Miguel de Icaza (miguel@gnu.org)
 *  Jody Goldberg (jgoldberg@home.com)
 *
 */
#include <config.h>
#include "selection.h"
#include "gnumeric-sheet.h"
#include "utils.h"
#include "clipboard.h"
#include "ranges.h"
#include "gnumeric-util.h"

static void sheet_selection_change (Sheet *sheet,
				    SheetSelection *old,
				    SheetSelection *new);

/*
 * Quick utility routine to test intersect of line segments.
 * Returns : 4 --sA--sb--eb--eA--	a contains b
 *           3 --sA--sb--eA--eb--	overlap left
 *           2 --sb--sA--eA--eb--	b contains a
 *           1 --sb--sA--eb--eA--	overlap right
 *           0 if there is no intersection.
 */
static int
segments_intersect (int const s_a, int const e_a,
		    int const s_b, int const e_b)
{
	/* Assume s_a <= e_a and s_b <= e_b */
	if (e_a < s_b || e_b < s_a)
		return 0;

	if (s_a < s_b)
		return (e_a >= e_b) ? 4 : 3;

	/* We already know that s_a <= e_b */
	return (e_a <= e_b) ? 2 : 1;
}

static const char *
sheet_get_selection_name (Sheet const *sheet)
{
	SheetSelection const *ss = sheet->selections->data;
	static char buffer [10 + 2 * 4 * sizeof (int)];

	if (range_is_singleton (&ss->user))
		return cell_name (ss->user.start.col, ss->user.start.row);
	snprintf (buffer, sizeof (buffer), "%dLx%dC",
		  ss->user.end.row - ss->user.start.row + 1,
		  ss->user.end.col - ss->user.start.col + 1);
	return buffer;
}
 
static void
sheet_selection_changed_hook (Sheet *sheet)
{
	sheet_update_auto_expr (sheet);
	sheet_update_controls  (sheet);
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

	ss->base.col  = base_col;
	ss->base.row  = base_row;

	ss->user.start.col = start_col;
	ss->user.end.col   = end_col;
	ss->user.start.row = start_row;
	ss->user.end.row   = end_row;

	sheet->selections = g_list_prepend (sheet->selections, ss);

	sheet_accept_pending_input (sheet);
	sheet_load_cell_val (sheet);

	sheet_set_selection (sheet, ss);
	sheet_redraw_selection (sheet, ss);

	sheet_redraw_cols (sheet);
	sheet_redraw_rows (sheet);

	sheet_selection_changed_hook (sheet);
}

/**
 * If returns true selection is just one range.
 * If returns false, range data: indeterminate
 **/
Range const *
selection_first_range (Sheet const *sheet)
{
	SheetSelection *ss;
	GList *l;

	g_return_val_if_fail (sheet != NULL, 0);
	g_return_val_if_fail (IS_SHEET (sheet), 0);

	l = g_list_first (sheet->selections);
	if (!l || !l->data)
		return NULL;
	ss = l->data;
	if ((l = g_list_next (l)))
		return NULL;

	return &ss->user;
}

/*
 * selection_is_simple
 * @sheet : The sheet whose selection we are testing.
 * @command_name : A string naming the operation requiring a single range.
 *
 * This function tests to see if multiple ranges are selected.  If so it
 * produces a dialog box warning.
 *
 * TODO : Merge this with the proposed exception mechanism.
 */
gboolean
selection_is_simple (Sheet const *sheet, char const *command_name)
{
	char *msg;

	if (g_list_length (sheet->selections) == 1)
		return TRUE;

	msg = g_strconcat (
		_("The command `"),
		command_name,
		_("' cannot be performed with multiple selections"), NULL);
	gnumeric_notice (sheet->workbook, GNOME_MESSAGE_BOX_ERROR, msg);
	g_free (msg);

	return FALSE;
}

void
sheet_selection_append (Sheet *sheet, int col, int row)
{
	sheet_selection_append_range (sheet, col, row, col, row, col, row);
}

static void
sheet_selection_change (Sheet *sheet, SheetSelection *old, SheetSelection *new)
{
	if (range_equal (&old->user, &new->user))
		return;

	sheet_accept_pending_input (sheet);

	sheet_set_selection (sheet, new);
	sheet_selection_changed_hook (sheet);

	sheet_redraw_selection (sheet, old);
	sheet_redraw_selection (sheet, new);

	if (new->user.start.col != old->user.start.col ||
	    new->user.end.col != old->user.end.col ||
	    ((new->user.start.row == 0 && new->user.end.row == SHEET_MAX_ROWS-1) ^
	     (old->user.start.row == 0 &&
	      old->user.end.row == SHEET_MAX_ROWS-1)))
		sheet_redraw_cols (sheet);

	if (new->user.start.row != old->user.start.row ||
	    new->user.end.row != old->user.end.row ||
	    ((new->user.start.col == 0 && new->user.end.col == SHEET_MAX_COLS-1) ^
	     (old->user.start.col == 0 &&
	      old->user.end.col == SHEET_MAX_COLS-1)))
		sheet_redraw_rows (sheet);
}

/**
 * sheet_selection_extend_to:
 * @sheet: the sheet
 * @col:   column that gets covered
 * @row:   row that gets covered
 *
 * This extends the selection to cover col, row
 */
void
sheet_selection_extend_to (Sheet *sheet, int col, int row)
{
	SheetSelection *ss, old;

	g_return_if_fail (sheet != NULL);
	g_return_if_fail (IS_SHEET (sheet));

	g_assert (sheet->selections);

	ss = (SheetSelection *) sheet->selections->data;

	old = *ss;

	if (col < ss->base.col){
		ss->user.start.col = col;
		ss->user.end.col   = ss->base.col;
	} else {
		ss->user.start.col = ss->base.col;
		ss->user.end.col   = col;
	}

	if (row < ss->base.row){
		ss->user.end.row   = ss->base.row;
		ss->user.start.row = row;
	} else {
		ss->user.end.row   = row;
		ss->user.start.row = ss->base.row;
	}

	sheet_selection_change (sheet, &old, ss);
}

/**
 * sheet_select_all:
 * Sheet: The sheet
 *
 * Selects all of the cells in the sheet
 */
void
sheet_select_all (Sheet *sheet)
{
	g_return_if_fail (sheet != NULL);
	g_return_if_fail (IS_SHEET (sheet));

	sheet_selection_reset_only (sheet);
	sheet_make_cell_visible (sheet, 0, 0);
	sheet_cursor_move (sheet, 0, 0, FALSE, FALSE);
	sheet_selection_append_range (sheet, 0, 0, 0, 0,
		SHEET_MAX_COLS-1, SHEET_MAX_ROWS-1);

	/* Queue redraws for columns and rows */
	sheet_redraw_rows (sheet);
	sheet_redraw_cols (sheet);
}

int
sheet_is_all_selected (Sheet *sheet)
{
	SheetSelection *ss;
	GList *l;

	g_return_val_if_fail (sheet != NULL, FALSE);
	g_return_val_if_fail (IS_SHEET (sheet), FALSE);

	for (l = sheet->selections; l != NULL; l = l->next){
		ss = l->data;

		if (ss->user.start.col == 0 &&
		    ss->user.start.row == 0 &&
		    ss->user.end.col == SHEET_MAX_COLS-1 &&
		    ss->user.end.row == SHEET_MAX_ROWS-1)
			return TRUE;
	}
	return FALSE;
}
 
/**
 * sheet_selection_extend_horizontal:
 *
 * @sheet:  The Sheet *
 * @count:  units to extend the selection horizontally
 * @jump_to_boundaries : Jump to range boundaries.
 */
void
sheet_selection_extend_horizontal (Sheet *sheet, int n, gboolean jump_to_boundaries)
{
	SheetSelection *ss;
	SheetSelection old_selection;

	g_return_if_fail (sheet != NULL);
	g_return_if_fail (IS_SHEET (sheet));

	ss = (SheetSelection *)sheet->selections->data;
	old_selection = *ss;

	if (ss->base.col < ss->user.end.col)
		ss->user.end.col =
		    sheet_find_boundary_horizontal (sheet,
						    ss->user.end.col, ss->user.end.row,
						    n, jump_to_boundaries);
	else if (ss->base.col > ss->user.start.col || n < 0)
		ss->user.start.col =
		    sheet_find_boundary_horizontal (sheet,
						    ss->user.start.col, ss->user.start.row,
						    n, jump_to_boundaries);
	else
		ss->user.end.col =
		    sheet_find_boundary_horizontal (sheet,
						    ss->user.end.col,  ss->user.end.row,
						    n, jump_to_boundaries);

	if (ss->user.end.col < ss->user.start.col) {
		int const tmp = ss->user.start.col;
		ss->user.start.col = ss->user.end.col;
		ss->user.end.col = tmp;
	}
	sheet_selection_change (sheet, &old_selection, ss);
}

/*
 * sheet_selection_extend_vertical
 * @sheet:  The Sheet *
 * @n:      units to extend the selection vertically
 * @jump_to_boundaries : Jump to range boundaries.
 */
void
sheet_selection_extend_vertical (Sheet *sheet, int n, gboolean jump_to_boundaries)
{
	SheetSelection *ss;
	SheetSelection old_selection;

	g_return_if_fail (sheet != NULL);
	g_return_if_fail (IS_SHEET (sheet));

	ss = (SheetSelection *)sheet->selections->data;
	old_selection = *ss;

	if (ss->base.row < ss->user.end.row)
		ss->user.end.row =
		    sheet_find_boundary_vertical (sheet,
						  ss->user.end.col, ss->user.end.row,
						  n, jump_to_boundaries);
	else if (ss->base.row > ss->user.start.row || n < 0)
		ss->user.start.row =
		    sheet_find_boundary_vertical (sheet,
						  ss->user.start.col, ss->user.start.row,
						  n, jump_to_boundaries);
	else
		ss->user.end.row =
		    sheet_find_boundary_vertical (sheet,
						  ss->user.end.col, ss->user.end.row,
						  n, jump_to_boundaries);

	if (ss->user.end.row < ss->user.start.row) {
		int const tmp = ss->user.start.row;
		ss->user.start.row = ss->user.end.row;
		ss->user.end.row = tmp;
	}
	sheet_selection_change (sheet, &old_selection, ss);
}

void
sheet_selection_set (Sheet *sheet, int start_col, int start_row, int end_col, int end_row)
{
	SheetSelection *ss;
	SheetSelection old_selection;

	g_return_if_fail (sheet != NULL);
	g_return_if_fail (IS_SHEET (sheet));

	ss = (SheetSelection *)sheet->selections->data;
	old_selection = *ss;

	ss->user.start.row = start_row;
	ss->user.end.row = end_row;
	ss->user.start.col = start_col;
	ss->user.end.col = end_col;

	sheet_selection_change (sheet, &old_selection, ss);
}

/**
 * sheet_selection_free
 * @sheet: the sheet
 *
 * Releases the selection associated with this sheet
 */
void
sheet_selection_free (Sheet *sheet)
{
	GList *list;

	for (list = sheet->selections; list; list = list->next){
		SheetSelection *ss = list->data;
		g_free (ss);
	}

	g_list_free (sheet->selections);
	sheet->selections = NULL;
}

/*
 * sheet_selection_reset
 * sheet:  The sheet
 *
 * Clears all of the selection ranges.
 * Warning: This does not set a new selection, this should
 * be taken care on the calling routine.
 */
void
sheet_selection_reset_only (Sheet *sheet)
{
	GList *list;

	g_return_if_fail (sheet != NULL);
	g_return_if_fail (IS_SHEET (sheet));

	for (list = sheet->selections; list; list = list->next){
		SheetSelection *ss = list->data;

		sheet_redraw_selection (sheet, ss);
	}
	sheet_selection_free (sheet);

	sheet->cursor_selection = NULL;

	/* Redraw column bar */
	sheet_redraw_cols (sheet);

	/* Redraw the row bar */
	sheet_redraw_rows (sheet);
}

/**
 * sheet_selection_reset:
 * sheet:  The sheet
 *
 * Clears all of the selection ranges and resets it to a
 * selection that only covers the cursor
 */
void
sheet_selection_reset (Sheet *sheet)
{
	g_return_if_fail (sheet != NULL);
	g_return_if_fail (IS_SHEET (sheet));

	sheet_selection_reset_only (sheet);
	sheet_selection_append (sheet, sheet->cursor_col, sheet->cursor_row);
}

int
sheet_selection_is_cell_selected (Sheet *sheet, int col, int row)
{
	GList *list = sheet->selections;

	for (list = sheet->selections; list; list = list->next){
		SheetSelection const *ss = list->data;

		if (range_contains (&ss->user, col, row))
			return 1;
	}
	return 0;
}

/*
 * assemble_cell_list: A callback for sheet_cell_foreach_range
 * intented to assemble a list of cells in a region.
 *
 * The closure parameter should be a pointer to a GList.
 */
static Value *
assemble_cell_list (Sheet *sheet, int col, int row, Cell *cell, void *user_data)
{
	GList **l = (GList **) user_data;

	*l = g_list_prepend (*l, cell);
	return NULL;
}

static void
assemble_selection_list (Sheet *sheet, 
			 int start_col, int start_row,
			 int end_col,   int end_row,
			 void *closure)
{
	sheet_cell_foreach_range (
		sheet, TRUE,
		start_col, start_row,
		end_col, end_row,
		&assemble_cell_list, closure);
}

CellList *
sheet_selection_to_list (Sheet *sheet)
{
	/* selection_apply will check all necessary invariants. */
	CellList *list = NULL;

	selection_apply (sheet, &assemble_selection_list, FALSE, &list);

	return list;
}

void
sheet_cell_list_free (CellList *cell_list)
{
	g_list_free (cell_list);
}

static void
reference_append (GString *result_str, CellPos const *pos)
{
	char *row_string = g_strdup_printf ("%d", pos->row);

	g_string_append_c (result_str, '$');
	g_string_append (result_str, col_name (pos->col));
	g_string_append_c (result_str, '$');
	g_string_append (result_str, row_string);

	g_free (row_string);
}

char *
sheet_selection_to_string (Sheet *sheet, gboolean include_sheet_name_prefix)
{
	GString *result_str;
	GList   *selections;
	char    *result;
	
	g_return_val_if_fail (sheet != NULL, NULL);
	g_return_val_if_fail (IS_SHEET (sheet), NULL);
	g_return_val_if_fail (sheet->selections, NULL);

	result_str = g_string_new ("");
	for (selections = sheet->selections; selections; selections = selections->next){
		SheetSelection *ss = selections->data;

		if (*result_str->str)
			g_string_append_c (result_str, ',');
		
		if (include_sheet_name_prefix){
			g_string_append_c (result_str, '\'');
			g_string_append (result_str, sheet->name);
			g_string_append (result_str, "'!");
		}

		reference_append (result_str, &ss->user.start);
		if ((ss->user.start.col != ss->user.end.col) ||
		    (ss->user.start.row != ss->user.end.row)){
			g_string_append_c (result_str, ':');
			reference_append (result_str, &ss->user.end);
		}
	}

	result = result_str->str;
	g_string_free (result_str, FALSE);
	return result;
}

gboolean
sheet_selection_copy (Sheet *sheet)
{
	SheetSelection *ss;
	g_return_val_if_fail (sheet != NULL, FALSE);
	g_return_val_if_fail (IS_SHEET (sheet), FALSE);
	g_return_val_if_fail (sheet->selections, FALSE);

	if (!selection_is_simple (sheet, _("copy")))
		return FALSE;

	ss = sheet->selections->data;

	if (sheet->workbook->clipboard_contents)
		clipboard_release (sheet->workbook->clipboard_contents);

	sheet->workbook->clipboard_contents = clipboard_copy_cell_range (
		sheet,
		ss->user.start.col, ss->user.start.row,
		ss->user.end.col, ss->user.end.row);

	return TRUE;
}

gboolean
sheet_selection_cut (Sheet *sheet)
{
	SheetSelection *ss;

	/* FIXME FIXME FIXME
	 * This code should be replaced completely.
	 * 'cut' is a poor description of what we're
	 * doing here.  'move' would be a better
	 * approximation.  The key portion of this process is that
	 * the range being moved has all
	 * 	- references to it adjusted to the new site.
	 * 	- relative references from it adjusted.
	 */

	g_return_val_if_fail (sheet != NULL, FALSE);
	g_return_val_if_fail (IS_SHEET (sheet), FALSE);
	g_return_val_if_fail (sheet->selections, FALSE);

	if (!selection_is_simple (sheet, _("cut")))
		return FALSE;

	ss = sheet->selections->data;

	if (sheet->workbook->clipboard_contents)
		clipboard_release (sheet->workbook->clipboard_contents);

	sheet->workbook->clipboard_contents = clipboard_copy_cell_range (
		sheet,
		ss->user.start.col, ss->user.start.row,
		ss->user.end.col, ss->user.end.row);

	sheet_clear_region (sheet, ss->user.start.col, ss->user.start.row,
			    ss->user.end.col, ss->user.end.row, NULL);

	return TRUE;
}

static gboolean
find_a_clipboard (Workbook *wb, gpointer data)
{
	CellRegion **cr = data;

	if (wb->clipboard_contents){
		*cr = wb->clipboard_contents;
		return FALSE;
	}

	return TRUE;
}

static CellRegion *
find_workbook_with_clipboard (Sheet *sheet)
{
	CellRegion *cr = NULL;

	if (sheet->workbook->clipboard_contents)
		return sheet->workbook->clipboard_contents;

	workbook_foreach (find_a_clipboard, &cr);

	return cr;
}

void
sheet_selection_paste (Sheet *sheet, int dest_col, int dest_row,
		       int paste_flags, guint32 time)
{
	CellRegion *content;

	g_return_if_fail (sheet != NULL);
	g_return_if_fail (IS_SHEET (sheet));
	g_return_if_fail (sheet->selections);

	content = find_workbook_with_clipboard (sheet);

	if (content)
		if (!selection_is_simple (sheet, _("paste")))
			return;

	clipboard_paste_region (content, sheet, dest_col, dest_row,
				paste_flags, time);
}

/* TODO TODO TODO : Remove these and just call the functions directly */

/**
 * sheet_selection_clear:
 * @sheet:  The sheet where we operate
 *
 * Removes the contents and styles.
 **/
void
sheet_selection_clear (Sheet *sheet)
{
	selection_apply (sheet, &sheet_clear_region, TRUE, NULL);
}

/**
 * sheet_selection_clear_content:
 * @sheet:  The sheet where we operate
 *
 * Removes the contents of all the cells in the current selection.
 **/
void
sheet_selection_clear_content (Sheet *sheet)
{
	selection_apply (sheet, &sheet_clear_region_content, TRUE, NULL);
}

/**
 * sheet_selection_clear_comments:
 * @sheet:  The sheet where we operate
 *
 * Removes all of the comments on the range of selected cells.
 **/
void
sheet_selection_clear_comments (Sheet *sheet)
{
	selection_apply (sheet, &sheet_clear_region_comments, TRUE, NULL);
}

/**
 * sheet_selection_clear_formats:
 * @sheet:  The sheet where we operate
 *
 * Removes all formating
 **/
void
sheet_selection_clear_formats (Sheet *sheet)
{
	selection_apply (sheet, &sheet_clear_region_formats, TRUE, NULL);
}

/****************************************************************************/

/**
 * selection_apply:
 * @sheet: the sheet.
 * @func:  The function to apply.
 * @allow_intersection : Call the routine for the non-intersecting subregions.
 * @closure : A parameter to pass to each invocation of @func.
 *
 * Applies the specified function for all ranges in the selection.  Optionally
 * select whether to use the high level potentially over lapped ranges, rather
 * than the smaller system created non-intersection regions.
 */
void
selection_apply (Sheet *sheet, SelectionApplyFunc const func,
		 gboolean allow_intersection,
		 void * closure)
{
	GList *l;
	GSList *proposed = NULL;

	g_return_if_fail (sheet != NULL);
	g_return_if_fail (IS_SHEET (sheet));

	if (allow_intersection) {
		for (l = sheet->selections; l != NULL; l = l->next) {
			SheetSelection const *ss = l->data;

			(*func) (sheet,
				 ss->user.start.col, ss->user.start.row,
				 ss->user.end.col, ss->user.end.row,
				 closure);
		}
		return;
	}

	/*
	 * Run through all the selection regions to see if any of
	 * the proposed regions overlap.  Start the search with the
	 * single user proposed segment and accumulate distict regions.
	 */
	for (l = sheet->selections; l != NULL; l = l->next) {
		SheetSelection const *ss = l->data;

		/* The set of regions that do not interset with b or
		 * its predecessors */
		GSList *clear = NULL;
		Range *tmp, *b = range_duplicate (&ss->user);

		/* run through the proposed regions and handle any that
		 * overlap with the current selection region
		 */
		while (proposed != NULL) {
			int row_intersect, col_intersect;

			/* pop the 1st element off the list */
			Range *a = proposed->data;
			proposed = g_slist_remove (proposed, a);

			col_intersect =
				segments_intersect (a->start.col, a->end.col,
						    b->start.col, b->end.col);

			/* No intersection */
			if (col_intersect == 0) {
				clear = g_slist_prepend (clear, a);
				continue;
			}

			row_intersect =
				segments_intersect (a->start.row, a->end.row,
						    b->start.row, b->end.row);
			/* No intersection */
			if (row_intersect == 0) {
				clear = g_slist_prepend (clear, a);
				continue;
			}

			/* Cross product of intersection cases */
			switch (col_intersect) {
			case 4 : /* a contains b */
				switch (row_intersect) {
				case 4 : /* a contains b */
					/* Old region contained by new region */

					/* remove old region */
					g_free (b);
					b = NULL;
					break;

				case 3 : /* overlap top */
					/* Shrink existing range */
					b->end.row = a->start.row - 1;
					break;

				case 2 : /* b contains a */
					/* Split existing range */
					if (b->start.col > 0) {
						tmp = range_duplicate (a);
						tmp->end.col = b->start.col - 1;
						clear = g_slist_prepend (clear,
									 tmp);
					}
					/* Fall through to do bottom segment */

				case 1 : /* overlap bottom */
					/* Shrink existing range */
					a->start.col = b->end.col + 1;
					break;

				default :
					g_assert_not_reached();
				};
				break;

			case 3 : /* overlap left */
				switch (row_intersect) {
				case 4 : /* a contains b */
					/* Shrink old region */
					b->start.col = a->end.col + 1;
					break;

				case 3 : /* overlap top */
					/* Split region */
					if (b->start.row > 0) {
						tmp = range_duplicate (a);
						tmp->start.col = b->start.col;
						tmp->end.row = b->start.row - 1;
						clear = g_slist_prepend (clear,
									 tmp);
					}
					/* fall through */

				case 2 : /* b contains a */
					/* shrink the left segment */
					a->end.col = b->start.col - 1;
					break;

				case 1 : /* overlap bottom */
					/* Split region */
					if (b->end.row < (SHEET_MAX_ROWS-1)) {
						tmp = range_duplicate (a);
						tmp->start.col = b->start.col;
						tmp->start.row = b->end.row + 1;
						clear = g_slist_prepend (clear,
									 tmp);
					}

					/* shrink the left segment */
					if (b->start.col == 0) {
						g_free(a);
						continue;
					}
					a->end.col = b->start.col - 1;
					break;

				default :
					g_assert_not_reached();
				};
				break;

			case 2 : /* b contains a */
				switch (row_intersect) {
				case 4 : /* a contains b */
					/* Split region */
					tmp = range_duplicate (a);
					tmp->start.row = b->end.row + 1;
					clear = g_slist_prepend (clear, tmp);
					/* fall through */

				case 3 : /* overlap top */
					/* shrink the top segment */
					a->end.row = b->start.row - 1;
					break;

				case 2 : /* b contains a */
					/* remove the selection */
					g_free (a);
					continue;

				case 1 : /* overlap bottom */
					/* shrink the top segment */
					a->start.row = b->end.row + 1;
					break;

				default :
					g_assert_not_reached();
				};
				break;

			case 1 : /* overlap right */
				switch (row_intersect) {
				case 4 : /* a contains b */
					/* Shrink old region */
					b->end.col = a->start.col - 1;
					break;

				case 3 : /* overlap top */
					/* Split region */
					tmp = range_duplicate (a);
					tmp->end.col = b->end.col;
					tmp->end.row = b->start.row - 1;
					/* fall through */

				case 2 : /* b contains a */
					/* shrink the right segment */
					a->start.col = b->end.col + 1;
					break;

				case 1 : /* overlap bottom */
					/* Split region */
					tmp = range_duplicate (a);
					tmp->end.col = b->end.col;
					tmp->start.row = b->end.row + 1;

					/* shrink the right segment */
					a->start.col = b->end.col + 1;
					break;

				default :
					g_assert_not_reached();
				};
				break;

			};

			/* WARNING : * Be careful putting code here.
			 * Some of the cases skips this */

			/* continue checking the new region for intersections */
			clear = g_slist_prepend (clear, a);
		}
		proposed = (b != NULL) ? g_slist_prepend (clear, b) : clear;
	}

	while (proposed != NULL) {
		/* pop the 1st element off the list */
		Range *r = proposed->data;
		proposed = g_slist_remove (proposed, r);

		(*func) (sheet,
			 r->start.col, r->start.row,
			 r->end.col, r->end.row,
			 closure);
		g_free (r);
	}
}

/*****************************************************************************/

typedef struct
{
	GString *str;
	gboolean include_sheet_name_prefix;
} selection_to_string_closure;

static void
range_to_string (Sheet *sheet, 
		 int start_col, int start_row,
		 int end_col,   int end_row,
		 void *closure)
{
	selection_to_string_closure * res = closure;

	if (*res->str->str)
		g_string_append_c (res->str, ',');

	if (res->include_sheet_name_prefix)
		g_string_sprintfa (res->str, "\'%s\'!", sheet->name);

	g_string_sprintfa (res->str, "$%s$%d",
			   col_name(start_col), start_row+1);
	if ((start_col != end_col) || (start_row != end_row))
		g_string_sprintfa (res->str, ":$%s$%d",
				   col_name(end_col), end_row+1);
}

char *
selection_to_string (Sheet *sheet, gboolean include_sheet_name_prefix)
{
	char    *output;
	selection_to_string_closure res;

	res.str = g_string_new ("");
	res.include_sheet_name_prefix = include_sheet_name_prefix;

	/* selection_apply will check all necessary invariants. */
	selection_apply (sheet, &range_to_string, TRUE, &res);

	output = res.str->str;
	g_string_free (res.str, FALSE);
	return output;
}

/*****************************************************************************/

/*
 * sheet_selection_to_list :
 * @sheet : The whose selection we are interested in.
 *
 * Assembles a unique CellList of all the existing cells in the selection.
 * Overlapping selection regions are handled.
 */
CellList *
selection_to_list (Sheet *sheet, gboolean allow_intersection)
{
	/* selection_apply will check all necessary invariants. */
	CellList *list = NULL;
	selection_apply (sheet, &assemble_selection_list,
			 allow_intersection, &list);
	return list;
}

/*****************************************************************************/

/*
 * selection_contains_colrow :
 * @sheet : The whose selection we are interested in.
 * @colrow: The column or row number we are interested in.
 * @is_col: A flag indicating whether this it is a column or a row.
 *
 * Searches the selection list to see whether the entire col/row specified is
 * contained by the section regions.  Since the selection is stored as the set
 * overlapping user specifed regions we can safely search for the range directly.
 *
 * Eventually to be completely correct and deal with the case of someone manually
 * selection an entire col/row, in seperate chunks,  we will need to do something
 * more advanced.
 */
gboolean
selection_contains_colrow (Sheet *sheet, int colrow, gboolean is_col)
{
	GList *l;
	for (l = sheet->selections; l != NULL; l = l->next) {
		SheetSelection const *ss = l->data;

		if (is_col) {
			if (ss->user.start.row == 0 &&
			    ss->user.end.row >= SHEET_MAX_ROWS-1 &&
			    ss->user.start.col <= colrow && colrow <= ss->user.end.col)
				return TRUE;
		} else {
			if (ss->user.start.col == 0 &&
			    ss->user.end.col >= SHEET_MAX_COLS-1 &&
			    ss->user.start.row <= colrow && colrow <= ss->user.end.row)
				return TRUE;
		}
	}
	return FALSE;
}
