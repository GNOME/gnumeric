/*
 * selection.c:  Manage selection regions.
 *
 * Author:
 *  Miguel de Icaza (miguel@gnu.org)
 *  Jody Goldberg (jgoldberg@home.com)
 *
 *  (C) 1999, 2000 Jody Goldberg
 */
#include <config.h>
#include "selection.h"
#include "parse-util.h"
#include "clipboard.h"
#include "ranges.h"
#include "application.h"
#include "command-context.h"
#include "workbook-view.h"
#include "workbook.h"
#include "commands.h"
#include "sheet-control-gui.h"
#include "gnumeric-util.h"

/*
 * Quick utility routine to test intersect of line segments.
 * Returns : 5 sA == sb eA == eb	a == b
 *           4 --sA--sb--eb--eA--	a contains b
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

	if (s_a == s_b)
		return (e_a >= e_b) ? ((e_a == e_b) ? 5 : 4) : 2;
	if (e_a == e_b)
		return (s_a <= s_b) ? 4 : 2;

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
		return cell_pos_name (&ss->user.start);
	snprintf (buffer, sizeof (buffer), "%dLx%dC",
		  ss->user.end.row - ss->user.start.row + 1,
		  ss->user.end.col - ss->user.start.col + 1);
	return buffer;
}

/**
 * sheet_selection_add_range : prepends a new range to the selection list.
 *
 * @sheet          : sheet whose selection to append to.
 * @edit_{col,row} : cell to mark as the new edit cursor.
 * @base_{col,row} : stationary corner of the newly selected range.
 * @move_{col,row} : moving corner of the newly selected range.
 *
 * Prepends a range to the selection list and set the edit cursor.
 */
void
sheet_selection_add_range (Sheet *sheet,
			   int edit_col, int edit_row,
			   int base_col, int base_row,
			   int move_col, int move_row)
{
	SheetSelection *ss;

	g_return_if_fail (sheet != NULL);
	g_return_if_fail (IS_SHEET (sheet));

	/* Create and prepend new selection */
	ss = g_new0 (SheetSelection, 1);
	ss->user.start.col = MIN (base_col, move_col);
	ss->user.start.row = MIN (base_row, move_row);
	ss->user.end.col   = MAX (base_col, move_col);
	ss->user.end.row   = MAX (base_row, move_row);
	sheet->selections = g_list_prepend (sheet->selections, ss);

	/* Set the selection parameters, and redraw the old edit cursor */
	sheet_cursor_set (sheet,
			  edit_col, edit_row,
			  base_col, base_row,
			  move_col, move_row);

	/* Redraw the newly added range so that it get shown as selected */
	sheet_redraw_range (sheet, &ss->user);
	sheet_redraw_headers (sheet, TRUE, TRUE, &ss->user);

	/* Force update of the status area */
	sheet_flag_status_update_range (sheet, NULL /* force update */);
}

/**
 * Return 1st range.
 * Return NULL if there is more than 1 and @permit_complex is FALSE
 **/
Range const *
selection_first_range (Sheet const *sheet, gboolean const permit_complex)
{
	SheetSelection *ss;
	GList *l;

	g_return_val_if_fail (sheet != NULL, 0);
	g_return_val_if_fail (IS_SHEET (sheet), 0);

	l = g_list_first (sheet->selections);
	if (!l || !l->data)
		return NULL;
	ss = l->data;
	if ((l = g_list_next (l)) && !permit_complex)
		return NULL;

	return &ss->user;
}

/*
 * selection_is_simple
 * @wbc          : The calling context to report errors to (GUI or corba)
 * @sheet        : The sheet whose selection we are testing.
 * @command_name : A string naming the operation requiring a single range.
 *
 * This function tests to see if multiple ranges are selected.  If so it
 * produces a warning.
 */
gboolean
selection_is_simple (WorkbookControl *wbc, Sheet const *sheet,
		     char const *command_name)
{
	g_return_val_if_fail (sheet != NULL, FALSE);
	g_return_val_if_fail (IS_SHEET (sheet), FALSE);

	if (g_list_length (sheet->selections) == 1)
		return TRUE;

	gnumeric_error_invalid (COMMAND_CONTEXT (wbc), command_name,
			      _("cannot be performed with multiple selections"));

	return FALSE;
}

void
sheet_selection_add (Sheet *sheet, int col, int row)
{
	sheet_selection_add_range (sheet, col, row, col, row, col, row);
}


/**
 * sheet_selection_extend_to:
 * @sheet: the sheet
 * @col:   column that gets covered
 * @row:   row that gets covered
 *
 * This extends the selection to cover col, row
 * and updates the status areas.
 */
void
sheet_selection_extend_to (Sheet *sheet, int col, int row)
{
	char const *sel_descr;

	g_return_if_fail (sheet != NULL);
	g_return_if_fail (IS_SHEET (sheet));

	/* If nothing was going to change dont redraw */
	if (sheet->cursor.move_corner.col == col && sheet->cursor.move_corner.row == row)
		return;

	sheet_selection_set (sheet,
			     sheet->cursor.edit_pos.col,
			     sheet->cursor.edit_pos.row,
			     sheet->cursor.base_corner.col,
			     sheet->cursor.base_corner.row,
			     col, row);

	/*
	 * FIXME : Does this belong here ?
	 * This is a convenient place to put is so that move movements
	 * that change the selection also update the status region,
	 * but this is somewhat lower level that I want to do this.
	 */
	sheet_update (sheet);
	sel_descr = sheet_get_selection_name (sheet);
	WORKBOOK_FOREACH_VIEW (sheet->workbook, view,
	{
		if (wb_view_cur_sheet (view) == sheet) {
			WORKBOOK_VIEW_FOREACH_CONTROL(view, control,
				wb_control_selection_descr_set (control, sel_descr););
		}
	});
}

/*
 * sheet_selection_extend
 * @sheet              : Sheet to operate in.
 * @n                  : Units to extend the selection
 * @jump_to_boundaries : Move to transitions between cells and blanks,
 *                       or move in single steps.
 * @horizontal         : extend vertically or horizontally.
 */
void
sheet_selection_extend (Sheet *sheet, int n, gboolean jump_to_boundaries,
			gboolean const horizontal)
{
	/* Use a tmp variable so that the extend_to routine knows that the
	 * selection has actually changed.
	 */
	CellPos tmp = sheet->cursor.move_corner;

	if (horizontal)
		tmp.col = sheet_find_boundary_horizontal (sheet,
				tmp.col, tmp.row,
				n, jump_to_boundaries);
	else
		tmp.row = sheet_find_boundary_vertical (sheet,
				tmp.col, tmp.row,
				n, jump_to_boundaries);

	sheet_selection_extend_to (sheet, tmp.col, tmp.row);
	sheet_make_cell_visible (sheet, tmp.col, tmp.row);
}

gboolean
sheet_is_all_selected (Sheet const * const sheet)
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

gboolean
sheet_is_cell_selected (Sheet const * const sheet, int col, int row)
{
	GList *list;

	if (sheet->current_object != NULL || sheet->new_object != NULL)
		return FALSE;

	for (list = sheet->selections; list; list = list->next){
		SheetSelection const *ss = list->data;

		if (range_contains (&ss->user, col, row))
			return TRUE;
	}
	return FALSE;
}

void
sheet_selection_redraw (Sheet const *sheet)
{
	GList *sel;

	for (sel = sheet->selections; sel; sel = sel->next){
		SheetSelection const *ss = sel->data;
		Range const *r = &ss->user;
		GList *view;

		for (view = sheet->sheet_views; view; view = view->next){
			SheetControlGUI *sheet_view = view->data;

			sheet_view_redraw_cell_region (sheet_view,
				r->start.col, r->start.row,
				r->end.col, r->end.row);
		}
		sheet_redraw_headers (sheet, TRUE, TRUE, &ss->user);
	}
}

/*
 * TRUE : If the range overlaps with any part of the selection
 */
gboolean
sheet_is_range_selected (Sheet const * const sheet, Range const *r)
{
	GList *list;

	for (list = sheet->selections; list; list = list->next){
		SheetSelection const *ss = list->data;

		if (range_overlap (&ss->user, r))
			return TRUE;
	}
	return FALSE;
}

void
sheet_selection_set (Sheet *sheet,
		     int edit_col, int edit_row,
		     int base_col, int base_row,
		     int move_col, int move_row)
{
	SheetSelection *ss;
	Range old_sel, new_sel;

	g_return_if_fail (sheet != NULL);
	g_return_if_fail (IS_SHEET (sheet));
	g_return_if_fail (sheet->selections != NULL);

	ss = (SheetSelection *)sheet->selections->data;

	new_sel.start.col = MIN(base_col, move_col);
	new_sel.start.row = MIN(base_row, move_row);
	new_sel.end.col = MAX(base_col, move_col);
	new_sel.end.row = MAX(base_row, move_row);

	if (range_equal (&ss->user, &new_sel))
		return;

	old_sel = ss->user;
	ss->user = new_sel;

	/* Set the cursor boundary */
	sheet_cursor_set (sheet,
			  edit_col, edit_row,
			  base_col, base_row,
			  move_col, move_row);

	if (range_overlap (&old_sel, &new_sel)) {
		GList *ranges, *l;
		/*
		 * Compute the blocks that need to be repainted: those that
		 * are in the complement of the intersection.
		 */
		ranges = range_fragment (&old_sel, &new_sel);

		for (l = ranges->next; l; l = l->next){
			Range *range = l->data;

			sheet_redraw_range (sheet, range);
		}
		range_fragment_free (ranges);
	} else {
		sheet_redraw_range (sheet, &old_sel);
		sheet_redraw_range (sheet, &new_sel);
	}

	/* Has the entire row been selected/unselected */
	if ((new_sel.start.row == 0 && new_sel.end.row == SHEET_MAX_ROWS-1) ^
	    (old_sel.start.row == 0 && old_sel.end.row == SHEET_MAX_ROWS-1)) {
		sheet_redraw_headers (sheet, TRUE, FALSE, &new_sel);
	} else {
		Range tmp = new_sel;
		int diff;

		diff = new_sel.start.col - old_sel.start.col;
		if (diff != 0) {
			if (diff > 0) {
				tmp.start.col = old_sel.start.col;
				tmp.end.col = new_sel.start.col;
			} else {
				tmp.end.col = old_sel.start.col;
				tmp.start.col = new_sel.start.col;
			}
			sheet_redraw_headers (sheet, TRUE, FALSE, &tmp);
		}
		diff = new_sel.end.col - old_sel.end.col;
		if (diff != 0) {
			if (diff > 0) {
				tmp.start.col = old_sel.end.col;
				tmp.end.col = new_sel.end.col;
			} else {
				tmp.end.col = old_sel.end.col;
				tmp.start.col = new_sel.end.col;
			}
			sheet_redraw_headers (sheet, TRUE, FALSE, &tmp);
		}
	}

	/* Has the entire col been selected/unselected */
	if ((new_sel.start.col == 0 && new_sel.end.col == SHEET_MAX_COLS-1) ^
	    (old_sel.start.col == 0 && old_sel.end.col == SHEET_MAX_COLS-1)) {
		sheet_redraw_headers (sheet, FALSE, TRUE, &new_sel);
	} else {
		Range tmp = new_sel;
		int diff;

		diff = new_sel.start.row - old_sel.start.row;
		if (diff != 0) {
			if (diff > 0) {
				tmp.start.row = old_sel.start.row;
				tmp.end.row = new_sel.start.row;
			} else {
				tmp.end.row = old_sel.start.row;
				tmp.start.row = new_sel.start.row;
			}
			sheet_redraw_headers (sheet, FALSE, TRUE, &tmp);
		}

		diff = new_sel.end.row - old_sel.end.row;
		if (diff != 0) {
			if (diff > 0) {
				tmp.start.row = old_sel.end.row;
				tmp.end.row = new_sel.end.row;
			} else {
				tmp.end.row = old_sel.end.row;
				tmp.start.row = new_sel.end.row;
			}
			sheet_redraw_headers (sheet, FALSE, TRUE, &tmp);
		}
	}

	sheet_flag_selection_change (sheet);
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
 * WARNING: This does not set a new selection this should
 * be taken care on the calling routine.
 */
void
sheet_selection_reset_only (Sheet *sheet)
{
	GList *list, *tmp;

	g_return_if_fail (sheet != NULL);
	g_return_if_fail (IS_SHEET (sheet));

	/* Empty the sheets selection */
	list = sheet->selections;
	sheet->selections = NULL;

	/* Redraw the grid, & headers for each region */
	for (tmp = list; tmp; tmp = tmp->next){
		SheetSelection *ss = tmp->data;

		sheet_redraw_range (sheet, &ss->user);
		sheet_redraw_headers (sheet, TRUE, TRUE, &ss->user);
		g_free (ss);
	}

	g_list_free (list);
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
			g_string_append (result_str, sheet->name_quoted);
			g_string_append_c (result_str, '!');
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

void
sheet_selection_ant (Sheet *sheet)
{
	GList *l;

	g_return_if_fail (sheet != NULL);
	g_return_if_fail (IS_SHEET (sheet));

	for (l = sheet->sheet_views; l; l = l->next){
		SheetControlGUI *sheet_view = l->data;

		sheet_view_selection_ant (sheet_view);
	}
}

void
sheet_selection_unant (Sheet *sheet)
{
	GList *l;

	g_return_if_fail (sheet != NULL);
	g_return_if_fail (IS_SHEET (sheet));

	for (l = sheet->sheet_views; l; l = l->next){
		SheetControlGUI *sheet_view = l->data;

		sheet_view_selection_unant (sheet_view);
	}
}

gboolean
sheet_selection_copy (WorkbookControl *wbc, Sheet *sheet)
{
	SheetSelection *ss;
	g_return_val_if_fail (sheet != NULL, FALSE);
	g_return_val_if_fail (IS_SHEET (sheet), FALSE);
	g_return_val_if_fail (sheet->selections, FALSE);

	/* FIXME FIXME : disable copying part of an array */

	if (!selection_is_simple (wbc, sheet, _("copy")))
		return FALSE;

	ss = sheet->selections->data;

	application_clipboard_copy (wbc, sheet, &ss->user);

	return TRUE;
}

gboolean
sheet_selection_cut (WorkbookControl *wbc, Sheet *sheet)
{
	SheetSelection *ss;

	/*
	 * 'cut' is a poor description of what we're
	 * doing here.  'move' would be a better
	 * approximation.  The key portion of this process is that
	 * the range being moved has all
	 * 	- references to it adjusted to the new site.
	 * 	- relative references from it adjusted.
	 *
	 * NOTE : This command DOES NOT MOVE ANYTHING !
	 *        We only store the src, paste does the move.
	 */

	g_return_val_if_fail (sheet != NULL, FALSE);
	g_return_val_if_fail (IS_SHEET (sheet), FALSE);
	g_return_val_if_fail (sheet->selections, FALSE);

	if (!selection_is_simple (wbc, sheet, _("cut")))
		return FALSE;

	ss = sheet->selections->data;
	if (sheet_range_splits_array (sheet, &ss->user)) {
		gnumeric_error_splits_array (COMMAND_CONTEXT (wbc), ("cut"));
		return FALSE;
	}

	application_clipboard_cut (wbc, sheet, &ss->user);

	return TRUE;
}

/**
 * selection_get_ranges:
 * @sheet: the sheet.
 * @allow_intersection : Divide the selection into nonoverlappign subranges.
 */
GSList *
selection_get_ranges (Sheet *sheet, gboolean const allow_intersection)
{
	GList  *l;
	GSList *proposed = NULL;

#undef DEBUG_SELECTION
#ifdef DEBUG_SELECTION
	fprintf (stderr, "============================\n");
#endif

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
		Range *tmp, *b = range_copy (&ss->user);

		if (allow_intersection) {
			proposed = g_slist_prepend (proposed, b);
			continue;
		}

		/* run through the proposed regions and handle any that
		 * overlap with the current selection region
		 */
		while (proposed != NULL) {
			int row_intersect, col_intersect;

			/* pop the 1st element off the list */
			Range *a = proposed->data;
			proposed = g_slist_remove (proposed, a);

			/* The region was already subsumed completely by previous
			 * elements */
			if (b == NULL) {
				clear = g_slist_prepend (clear, a);
				continue;
			}

#ifdef DEBUG_SELECTION
			fprintf (stderr, "a = ");
			range_dump (a);
			fprintf (stderr, "; b = ");
			range_dump (b);
			fprintf (stderr, "\n");
#endif

			col_intersect =
				segments_intersect (a->start.col, a->end.col,
						    b->start.col, b->end.col);

#ifdef DEBUG_SELECTION
			fprintf (stderr, "col = %d\na = %s", col_intersect, col_name(a->start.col));
			if (a->start.col != a->end.col)
				fprintf (stderr, " -> %s", col_name(a->end.col));
			fprintf (stderr, "\nb = %s", col_name(b->start.col));
			if (b->start.col != b->end.col)
				fprintf (stderr, " -> %s\n", col_name(b->end.col));
			else
				fputc ('\n', stderr);
#endif

			/* No intersection */
			if (col_intersect == 0) {
				clear = g_slist_prepend (clear, a);
				continue;
			}

			row_intersect =
				segments_intersect (a->start.row, a->end.row,
						    b->start.row, b->end.row);
#ifdef DEBUG_SELECTION
			fprintf (stderr, "row = %d\na = %d", row_intersect, a->start.row +1);
			if (a->start.row != a->end.row)
				fprintf (stderr, " -> %d", a->end.row +1);
			fprintf (stderr, "\nb = %d", b->start.row +1);
			if (b->start.row != b->end.row)
				fprintf (stderr, " -> %d\n", b->end.row +1);
			else
				fputc ('\n', stderr);
#endif

			/* No intersection */
			if (row_intersect == 0) {
				clear = g_slist_prepend (clear, a);
				continue;
			}

			/* Simplify our lives by allowing equality to work in our favour */
			if (col_intersect == 5) {
				if (row_intersect == 5)
					row_intersect = 4;
				if (row_intersect == 4 || row_intersect == 2)
					col_intersect = row_intersect;
				else
					col_intersect = 4;
			} else if (row_intersect == 5) {
				if (col_intersect == 4 || col_intersect == 2)
					row_intersect = col_intersect;
				else
					row_intersect = 4;
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
					b->start.row = a->end.row + 1;
					break;

				case 2 : /* b contains a */
					if (a->end.col == b->end.col) {
						/* Shrink existing range */
						a->end.col = b->start.col - 1;
						break;
					}
					if (a->start.col != b->start.col) {
						/* Split existing range */
						tmp = range_copy (a);
						tmp->end.col = b->start.col - 1;
						clear = g_slist_prepend (clear, tmp);
					}
					/* Shrink existing range */
					a->start.col = b->end.col + 1;
					break;

				case 1 : /* overlap bottom */
					/* Shrink existing range */
					a->start.row = b->end.row + 1;
					break;

				default :
					g_assert_not_reached ();
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
						tmp = range_copy (a);
						tmp->start.col = b->start.col;
						tmp->end.row = b->start.row - 1;
						clear = g_slist_prepend (clear, tmp);
					}
					/* fall through */

				case 2 : /* b contains a */
					/* shrink the left segment */
					a->end.col = b->start.col - 1;
					break;

				case 1 : /* overlap bottom */
					/* Split region */
					if (b->end.row < (SHEET_MAX_ROWS-1)) {
						tmp = range_copy (a);
						tmp->start.col = b->start.col;
						tmp->start.row = b->end.row + 1;
						clear = g_slist_prepend (clear, tmp);
					}

					/* shrink the left segment */
					if (b->start.col == 0) {
						g_free (a);
						a = NULL;
						continue;
					}
					a->end.col = b->start.col - 1;
					break;

				default :
					g_assert_not_reached ();
				};
				break;

			case 2 : /* b contains a */
				switch (row_intersect) {
				case 3 : /* overlap top */
					/* shrink the top segment */
					a->end.row = b->start.row - 1;
					break;

				case 2 : /* b contains a */
					/* remove the selection */
					g_free (a);
					a = NULL;
					continue;

				case 4 : /* a contains b */
					if (a->end.row == b->end.row) {
						/* Shrink existing range */
						a->end.row = b->start.row - 1;
						break;
					}
					if (a->start.row != b->start.row) {
						/* Split region */
						tmp = range_copy (a);
						tmp->end.row = b->start.row - 1;
						clear = g_slist_prepend (clear, tmp);
					}
					/* fall through */

				case 1 : /* overlap bottom */
					/* shrink the top segment */
					a->start.row = b->end.row + 1;
					break;

				default :
					g_assert_not_reached ();
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
					tmp = range_copy (a);
					tmp->end.col = b->end.col;
					tmp->end.row = b->start.row - 1;
					clear = g_slist_prepend (clear, tmp);
					/* fall through */

				case 2 : /* b contains a */
					/* shrink the right segment */
					a->start.col = b->end.col + 1;
					break;

				case 1 : /* overlap bottom */
					/* Split region */
					tmp = range_copy (a);
					tmp->end.col = b->end.col;
					tmp->start.row = b->end.row + 1;

					/* shrink the right segment */
					a->start.col = b->end.col + 1;
					break;

				default :
					g_assert_not_reached ();
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

	return proposed;
}

/**
 * selection_check_for_array:
 * @sheet: the sheet.
 * @selection : A list of ranges to check.
 *
 * Checks to see if any of the ranges are partial arrays.
 */
gboolean
selection_check_for_array (Sheet const * sheet, GSList const *selection)
{
	GSList const *l;

	/* Check for array subdivision */
	for (l = selection; l != NULL; l = l->next) {
		Range const *r = l->data;
		if (sheet_range_splits_array (sheet, r))
			return TRUE;
	}
	return FALSE;
}

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

			(*func) (sheet, &ss->user, closure);
		}
	} else {
		proposed = selection_get_ranges (sheet, allow_intersection);
		while (proposed != NULL) {
			/* pop the 1st element off the list */
			Range *r = proposed->data;
			proposed = g_slist_remove (proposed, r);

#ifdef DEBUG_SELECTION
			range_dump (r);
			fprintf (stderr, "\n");
#endif

			(*func) (sheet, r, closure);
			g_free (r);
		}
	}
}

typedef struct
{
	GString *str;
	gboolean include_sheet_name_prefix;
} selection_to_string_closure;

static void
cb_range_to_string (Sheet *sheet, Range const *r, void *closure)
{
	selection_to_string_closure * res = closure;

	if (*res->str->str)
		g_string_append_c (res->str, ',');

	if (res->include_sheet_name_prefix)
		g_string_sprintfa (res->str, "%s!", sheet->name_quoted);

	g_string_sprintfa (res->str, "$%s$%d",
			   col_name (r->start.col), r->start.row+1);
	if ((r->start.col != r->end.col) || (r->start.row != r->end.row))
		g_string_sprintfa (res->str, ":$%s$%d",
				   col_name (r->end.col), r->end.row+1);
}

char *
selection_to_string (Sheet *sheet, gboolean include_sheet_name_prefix)
{
	char    *output;
	selection_to_string_closure res;

	res.str = g_string_new ("");
	res.include_sheet_name_prefix = include_sheet_name_prefix;

	/* selection_apply will check all necessary invariants. */
	selection_apply (sheet, &cb_range_to_string, TRUE, &res);

	output = res.str->str;
	g_string_free (res.str, FALSE);
	return output;
}

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

/*
 * selection_foreach_range :
 * @sheet : The whose selection is being iterated.
 * @from_start : Iterate from start to end or end to start.
 * @range_cb : A function to call for each sheet.
 * @user_data :
 *
 */
gboolean
selection_foreach_range (Sheet *sheet, gboolean from_start,
			 gboolean (*range_cb) (Sheet *sheet,
					       Range const *range,
					       gpointer user_data),
			 gpointer user_data)
{
	GList *l;

	g_return_val_if_fail (sheet != NULL, FALSE);
	g_return_val_if_fail (IS_SHEET (sheet), FALSE);

	if (from_start) 
		for (l = sheet->selections; l != NULL; l = l->next) {
			SheetSelection *ss = l->data;
			if (!range_cb (sheet, &ss->user, user_data))
				return FALSE;
		}
	else
		for (l = g_list_last (sheet->selections); l != NULL; l = l->prev) {
			SheetSelection *ss = l->data;
			if (!range_cb (sheet, &ss->user, user_data))
				return FALSE;
		}
	return TRUE;
}

/*
 * walk_boundaries : Iterates through a region by row then column.
 *
 * returns TRUE if the cursor leaves the boundary region.
 */
static gboolean
walk_boundaries (Range const * const bound,
		 int const inc_x, int const inc_y,
		 CellPos const * const current,
		 CellPos       * const result)
{
	if (current->row + inc_y > bound->end.row){
		if (current->col + 1 > bound->end.col)
			goto overflow;

		result->row = bound->start.row;
		result->col = current->col + 1;
		return FALSE;
	}

	if (current->row + inc_y < bound->start.row){
		if (current->col - 1 < bound->start.col)
			goto overflow;

		result->row = bound->end.row;
		result->col = current->col - 1;
		return FALSE;
	}

	if (current->col + inc_x > bound->end.col){
		if (current->row + 1 > bound->end.row)
			goto overflow;

		result->row = current->row + 1;
		result->col = bound->start.col;
		return FALSE;
	}

	if (current->col + inc_x < bound->start.col){
		if (current->row - 1 < bound->start.row)
			goto overflow;
		result->row = current->row - 1;
		result->col = bound->end.col;
		return FALSE;
	}

	result->row = current->row + inc_y;
	result->col = current->col + inc_x;
	return FALSE;

overflow:
	*result = *current;
	return TRUE;
}

void
sheet_selection_walk_step (Sheet *sheet,
			   gboolean const forward,
			   gboolean const horizontal)
{
	int const diff = forward ? 1 : -1;
	int inc_x = 0, inc_y = 0;
	int selections_count;
	CellPos current, destination;
	SheetSelection const *ss;

	g_return_if_fail (sheet != NULL);
	g_return_if_fail (sheet->selections != NULL);

	current = sheet->cursor.edit_pos;

	if (horizontal)
		inc_x = diff;
	else
		inc_y = diff;

	ss = sheet->selections->data;
	selections_count = g_list_length (sheet->selections);

	/* If there is no selection besides the cursor
	 * iterate through the entire sheet.  Move the
	 * cursor and selection as we go.
	 * Ignore wrapping.  At that scale it is irrelevant.
	 */
	if (selections_count == 1 &&
	    ss->user.start.col == ss->user.end.col &&
	    ss->user.start.row == ss->user.end.row) {
		Range full_sheet;
		if (horizontal) {
			full_sheet.start.col = 0;
			full_sheet.end.col   = SHEET_MAX_COLS-1;
			full_sheet.start.row =
				full_sheet.end.row   = ss->user.start.row;
		} else {
			full_sheet.start.col =
				full_sheet.end.col   = ss->user.start.col;
			full_sheet.start.row = 0;
			full_sheet.end.row   = SHEET_MAX_ROWS-1;
		}

		/* Ignore attempts to move outside the boundary region */
		if (!walk_boundaries (&full_sheet, inc_x, inc_y,
				      &current, &destination)) {
			sheet_selection_set (sheet,
					     destination.col, destination.row,
					     destination.col, destination.row,
					     destination.col, destination.row);
			sheet_make_cell_visible (sheet, destination.col, destination.row);
		}
		return;
	}

	if (walk_boundaries (&ss->user, inc_x, inc_y, &current, &destination)) {
		if (forward) {
			GList *tmp = g_list_last (sheet->selections);
			sheet->selections =
			    g_list_concat (tmp,
					   g_list_remove_link (sheet->selections,
							       tmp));
			ss = sheet->selections->data;
			destination = ss->user.start;
		} else {
			GList *tmp = sheet->selections;
			sheet->selections =
			    g_list_concat (g_list_remove_link (sheet->selections,
							       tmp),
					   tmp);
			ss = sheet->selections->data;
			destination = ss->user.end;
		}
		if (selections_count != 1)
			sheet_cursor_set (sheet,
					  destination.col, destination.row,
					  ss->user.start.col, ss->user.start.row,
					  ss->user.end.col, ss->user.end.row);
	}

	sheet_set_edit_pos (sheet, destination.col, destination.row);
	sheet_make_cell_visible (sheet, destination.col, destination.row);
}

ColRowSelectionType
sheet_col_selection_type (Sheet const *sheet, int col)
{
	SheetSelection *ss;
	GList *l;
	int ret = COL_ROW_NO_SELECTION;

	g_return_val_if_fail (sheet != NULL, COL_ROW_NO_SELECTION);
	g_return_val_if_fail (IS_SHEET (sheet), COL_ROW_NO_SELECTION);

	if (sheet->selections == NULL ||
	    sheet->new_object != NULL ||
	    sheet->current_object != NULL)
		return COL_ROW_NO_SELECTION;

	for (l = sheet->selections; l != NULL; l = l->next){
		ss = l->data;

		if (ss->user.start.col > col ||
		    ss->user.end.col < col)
			continue;

		if (ss->user.start.row == 0 &&
		    ss->user.end.row == SHEET_MAX_ROWS-1)
			return COL_ROW_FULL_SELECTION;

		ret = COL_ROW_PARTIAL_SELECTION;
	}

	return ret;
}

ColRowSelectionType
sheet_row_selection_type (Sheet const *sheet, int row)
{
	SheetSelection *ss;
	GList *l;
	int ret = COL_ROW_NO_SELECTION;

	g_return_val_if_fail (sheet != NULL, COL_ROW_NO_SELECTION);
	g_return_val_if_fail (IS_SHEET (sheet), COL_ROW_NO_SELECTION);

	if (sheet->selections == NULL ||
	    sheet->new_object != NULL ||
	    sheet->current_object != NULL)
		return COL_ROW_NO_SELECTION;

	for (l = sheet->selections; l != NULL; l = l->next){
		ss = l->data;

		if (ss->user.start.row > row ||
		    ss->user.end.row < row)
			continue;

		if (ss->user.start.col == 0 &&
		    ss->user.end.col == SHEET_MAX_COLS-1)
			return COL_ROW_FULL_SELECTION;

		ret = COL_ROW_PARTIAL_SELECTION;
	}

	return ret;
}

/*
 * sheet_selection_full_cols :
 *
 * @sheet :
 *
 * returns TRUE if all of the selected cols in the selection fully selected ?
 */
gboolean
sheet_selection_full_cols (Sheet const *sheet, int col)
{
	GList *l;
	gboolean found = FALSE;

	g_return_val_if_fail (sheet != NULL, FALSE);
	g_return_val_if_fail (IS_SHEET (sheet), FALSE);

	for (l = sheet->selections; l != NULL; l = l->next){
		SheetSelection const *ss = l->data;
		Range const *r = &ss->user;
		if (r->start.row != 0 || r->end.row < SHEET_MAX_ROWS - 1)
			return FALSE;
		if (r->start.col <= col && col <= r->end.col)
			found = TRUE;
	}

	return found;
}
/*
 * sheet_selection_full_rows :
 *
 * @sheet :
 *
 * returns TRUE if all of the selected rows in the selection fully selected ?
 */
gboolean
sheet_selection_full_rows (Sheet const *sheet, int row)
{
	GList *l;
	gboolean found = FALSE;

	g_return_val_if_fail (sheet != NULL, FALSE);
	g_return_val_if_fail (IS_SHEET (sheet), FALSE);

	for (l = sheet->selections; l != NULL; l = l->next){
		SheetSelection const *ss = l->data;
		Range const *r = &ss->user;
		if (r->start.col != 0 || r->end.col < SHEET_MAX_COLS - 1)
			return FALSE;
		if (r->start.row <= row && row <= r->end.row)
			found = TRUE;
	}

	return found;
}

