/* vim: set sw=8: */
/*
 * selection.c:  Manage selection regions.
 *
 * Author:
 *  Miguel de Icaza (miguel@gnu.org)
 *  Jody Goldberg (jgoldberg@home.com)
 *
 *  (C) 1999-2001 Jody Goldberg
 */
#include <config.h>
#include "selection.h"
#include "sheet.h"
#include "parse-util.h"
#include "clipboard.h"
#include "ranges.h"
#include "application.h"
#include "command-context.h"
#include "workbook-control.h"
#include "workbook-view.h"
#include "workbook.h"
#include "commands.h"
#include "value.h"
#include "cell.h"
#include "sheet-merge.h"
#include "sheet-private.h"
#include "sheet-control.h"
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

/**
 * selection_first_range
 * @sheet        : The sheet whose selection we are testing.
 * @wbc          : The calling context to report errors to (GUI or corba)
 * @command_name : A string naming the operation requiring a single range.
 *
 * Returns the first range, if a control is supplied it displays an error if
 *    there is more than one range.
 */
Range const *
selection_first_range (Sheet const *sheet, 
		       WorkbookControl *wbc, char const *cmd_name)
{
	Range const *r;
	GList *l;

	g_return_val_if_fail (IS_SHEET (sheet), NULL);

	l = g_list_first (sheet->selections);
	g_return_val_if_fail (l != NULL && l->data != NULL, NULL);

	r = l->data;
	if (wbc != NULL && l->next != NULL) {
		gnumeric_error_invalid (COMMAND_CONTEXT (wbc), cmd_name,
			_("cannot be performed with multiple ranges selected"));
		return NULL;
	}

	return r;
}

/**
 * sheet_selection_extend_to:
 *
 * @sheet: the sheet
 * @col:   column that gets covered (negative indicates all cols)
 * @row:   row that gets covered (negative indicates all rows)
 *
 * This extends the selection to cover col, row and updates the status areas.
 */
void
sheet_selection_extend_to (Sheet *sheet, int col, int row)
{
	int base_col, base_row;

	g_return_if_fail (IS_SHEET (sheet));

	if (col < 0) {
		base_col = 0;
		col = SHEET_MAX_COLS - 1;
	} else
		base_col = sheet->cursor.base_corner.col;
	if (row < 0) {
		base_row = 0;
		row = SHEET_MAX_ROWS - 1;
	} else
		base_row = sheet->cursor.base_corner.row;

	/* If nothing was going to change dont redraw */
	if (sheet->cursor.move_corner.col == col &&
	    sheet->cursor.move_corner.row == row &&
	    sheet->cursor.base_corner.col == base_col &&
	    sheet->cursor.base_corner.row == base_row)
		return;

	sheet_selection_set (sheet, sheet->edit_pos.col, sheet->edit_pos.row,
			     base_col, base_row, col, row); 

	/*
	 * FIXME : Does this belong here ?
	 * This is a convenient place to put it so that changes to the
	 * selection also update the status region, but this is somewhat lower
	 * level that I want to do this.
	 */
	sheet_update (sheet);
	WORKBOOK_FOREACH_VIEW (sheet->workbook, view,
	{
		if (wb_view_cur_sheet (view) == sheet)
			wb_view_selection_desc (view, FALSE, NULL);
	});
}

gboolean
sheet_is_all_selected (Sheet const *sheet)
{
	Range *ss;
	GList *l;

	g_return_val_if_fail (IS_SHEET (sheet), FALSE);

	for (l = sheet->selections; l != NULL; l = l->next){
		ss = l->data;

		if (ss->start.col == 0 &&
		    ss->start.row == 0 &&
		    ss->end.col == SHEET_MAX_COLS-1 &&
		    ss->end.row == SHEET_MAX_ROWS-1)
			return TRUE;
	}
	return FALSE;
}

gboolean
sheet_is_cell_selected (Sheet const *sheet, int col, int row)
{
	GList *list;

	for (list = sheet->selections; list; list = list->next){
		Range const *ss = list->data;

		if (range_contains (ss, col, row))
			return TRUE;
	}
	return FALSE;
}

void
sheet_selection_redraw (Sheet const *sheet)
{
	GList *sel;

	for (sel = sheet->selections; sel; sel = sel->next){
		Range const *r = sel->data;

		SHEET_FOREACH_CONTROL (sheet, control,
			sc_redraw_region (control,
				r->start.col, r->start.row,
				r->end.col, r->end.row);
			sc_redraw_headers (control, TRUE, TRUE, r););
	}
}

/*
 * TRUE : If the range overlaps with any part of the selection
 */
gboolean
sheet_is_range_selected (Sheet const *sheet, Range const *r)
{
	GList *list;

	for (list = sheet->selections; list; list = list->next){
		Range const *ss = list->data;

		if (range_overlap (ss, r))
			return TRUE;
	}
	return FALSE;
}

/*
 * TRUE : If entire range is contained in the selection
 */
gboolean
sheet_is_full_range_selected (Sheet const *sheet, Range const *r)
{
	GList *list;

	for (list = sheet->selections; list; list = list->next){
		Range const *ss = list->data;

		if (range_contained (r, ss))
			return TRUE;
	}
	return FALSE;
}

static void
sheet_selection_set_internal (Sheet *sheet,
			      int edit_col, int edit_row,
			      int base_col, int base_row,
			      int move_col, int move_row,
			      gboolean just_add_it)
{
	GList *list;
	Range *ss;
	Range old_sel, new_sel;
	gboolean do_cols, do_rows;

	g_return_if_fail (IS_SHEET (sheet));
	g_return_if_fail (sheet->selections != NULL);

	new_sel.start.col = MIN(base_col, move_col);
	new_sel.start.row = MIN(base_row, move_row);
	new_sel.end.col = MAX(base_col, move_col);
	new_sel.end.row = MAX(base_row, move_row);

	g_return_if_fail (range_is_sane (&new_sel));

	sheet_merge_find_container (sheet, &new_sel);
	ss = (Range *)sheet->selections->data;
	if (!just_add_it && range_equal (ss, &new_sel))
		return;

	old_sel = *ss;
	*ss = new_sel;

	/* Set the cursor boundary */
	sheet_cursor_set (sheet,
			  edit_col, edit_row,
			  base_col, base_row,
			  move_col, move_row, ss);

	if (just_add_it) {
		sheet_redraw_range (sheet, &new_sel);
		sheet_redraw_headers (sheet, TRUE, TRUE, &new_sel);
		goto set_menu_flags;
	}

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
		Range tmp = range_union (&new_sel, &old_sel);
		sheet_redraw_headers (sheet, TRUE, FALSE, &tmp);
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
		Range tmp = range_union (&new_sel, &old_sel);
		sheet_redraw_headers (sheet, FALSE, TRUE, &tmp);
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
	
set_menu_flags:
	sheet_flag_selection_change (sheet);

	/*
	 * Now see if there is some selection which selects a
	 * whole row, a whole column or the whole sheet and de-activate
	 * insert row/cols and the flags accordingly.
	 */
	do_rows = do_cols = TRUE;
	for (list = sheet->selections; list && (do_cols || do_rows); list = list->next) {
		Range const *r = list->data;

		if (do_cols && range_is_full (r, TRUE))
			do_cols = FALSE;
		if (do_rows && range_is_full (r, FALSE))
			do_rows = FALSE;
	}
	sheet_menu_state_enable_insert (sheet, do_cols, do_rows);

	/*
	 * FIXME: Enable/disable the show/hide detail menu items here.
	 * We can only do this when the data structures have improved, currently
	 * checking for this will be to slow.
	 * Once it works, use this code :
	 *
	 * sheet->priv->enable_showhide_detail = ....
	 *
	 * WORKBOOK_FOREACH_VIEW (sheet->workbook, view, {
	 *	if (sheet == wb_view_cur_sheet (view)) {
	 *	        WORKBOOK_VIEW_FOREACH_CONTROL(view, wbc,
	 *	                wb_control_menu_state_update (wbc, sheet, MS_SHOWHIDE_DETAIL););
 	 *      }
	 * });
	 */
}

void
sheet_selection_set (Sheet *sheet,
		     int edit_col, int edit_row,
		     int base_col, int base_row,
		     int move_col, int move_row)
{
	sheet_selection_set_internal (sheet,
				      edit_col, edit_row,
				      base_col, base_row,
				      move_col, move_row, FALSE);
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
	Range *ss;

	g_return_if_fail (IS_SHEET (sheet));

	/* Create and prepend new selection */
	ss = g_new0 (Range, 1);
	sheet->selections = g_list_prepend (sheet->selections, ss);
	sheet_selection_set_internal (sheet,
				      edit_col, edit_row,
				      base_col, base_row,
				      move_col, move_row, TRUE);
}

void
sheet_selection_add (Sheet *sheet, int col, int row)
{
	sheet_selection_add_range (sheet, col, row, col, row, col, row);
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
		Range *ss = list->data;
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
sheet_selection_reset (Sheet *sheet)
{
	GList *list, *tmp;

	g_return_if_fail (IS_SHEET (sheet));

	/* Empty the sheets selection */
	list = sheet->selections;
	sheet->selections = NULL;

	/* Redraw the grid, & headers for each region */
	for (tmp = list; tmp; tmp = tmp->next){
		Range *ss = tmp->data;

		sheet_redraw_range (sheet, ss);
		sheet_redraw_headers (sheet, TRUE, TRUE, ss);
		g_free (ss);
	}

	g_list_free (list);

	/* Make sure we re-enable the insert col/row and cell menu items */
	sheet_menu_state_enable_insert (sheet, TRUE, TRUE);
}

static void
reference_append (GString *result_str, CellPos const *pos)
{
	g_string_append_c (result_str, '$');
	g_string_append (result_str, col_name (pos->col));
	g_string_append_c (result_str, '$');
	g_string_append (result_str, row_name (pos->row));
}

char *
sheet_selection_to_string (Sheet *sheet, gboolean include_sheet_name_prefix)
{
	GString *result_str;
	GList   *selections;
	char    *result;

	g_return_val_if_fail (IS_SHEET (sheet), NULL);
	g_return_val_if_fail (sheet->selections, NULL);

	result_str = g_string_new ("");
	for (selections = sheet->selections; selections; selections = selections->next){
		Range *ss = selections->data;

		if (*result_str->str)
			g_string_append_c (result_str, ',');

		if (include_sheet_name_prefix){
			g_string_append (result_str, sheet->name_quoted);
			g_string_append_c (result_str, '!');
		}

		reference_append (result_str, &ss->start);
		if ((ss->start.col != ss->end.col) ||
		    (ss->start.row != ss->end.row)){
			g_string_append_c (result_str, ':');
			reference_append (result_str, &ss->end);
		}
	}

	result = result_str->str;
	g_string_free (result_str, FALSE);
	return result;
}

gboolean
sheet_selection_copy (WorkbookControl *wbc, Sheet *sheet)
{
	Range const *sel;

	if (!(sel = selection_first_range (sheet, wbc, _("Copy"))))
		return FALSE;

	application_clipboard_copy (wbc, sheet, sel);

	return TRUE;
}

gboolean
sheet_selection_cut (WorkbookControl *wbc, Sheet *sheet)
{
	Range const *sel;

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
	g_return_val_if_fail (IS_SHEET (sheet), FALSE);

	if (!(sel = selection_first_range (sheet, wbc, _("Cut"))))
		return FALSE;

	if (sheet_range_splits_region (sheet, sel, NULL, wbc, _("Cut")))
		return FALSE;

	application_clipboard_cut (wbc, sheet, sel);

	return TRUE;
}

/**
 * selection_get_ranges:
 * @sheet: the sheet.
 * @allow_intersection : Divide the selection into nonoverlapping subranges.
 */
GSList *
selection_get_ranges (Sheet const *sheet, gboolean allow_intersection)
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
		Range const *r = l->data;

		/* The set of regions that do not interset with b or
		 * its predecessors */
		GSList *clear = NULL;
		Range *tmp, *b = range_dup (r);

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
			range_dump (a, "; b = ");
			range_dump (b, "\n");
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
			fprintf (stderr, "row = %d\na = %s", row_intersect, row_name (a->start.row));
			if (a->start.row != a->end.row)
				fprintf (stderr, " -> %s", row_name (a->end.row));
			fprintf (stderr, "\nb = %s", row_name (b->start.row));
			if (b->start.row != b->end.row)
				fprintf (stderr, " -> %s\n", row_name (b->end.row));
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
						tmp = range_dup (a);
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
						tmp = range_dup (a);
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
						tmp = range_dup (a);
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
						tmp = range_dup (a);
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
					tmp = range_dup (a);
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
					tmp = range_dup (a);
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

	g_return_if_fail (IS_SHEET (sheet));

	if (allow_intersection) {
		for (l = sheet->selections; l != NULL; l = l->next) {
			Range const *ss = l->data;

			(*func) (sheet, ss, closure);
		}
	} else {
		proposed = selection_get_ranges (sheet, allow_intersection);
		while (proposed != NULL) {
			/* pop the 1st element off the list */
			Range *r = proposed->data;
			proposed = g_slist_remove (proposed, r);

#ifdef DEBUG_SELECTION
			range_dump (r, "\n");
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

	g_string_sprintfa (res->str, "$%s$%s",
			   col_name (r->start.col), row_name (r->start.row));
	if ((r->start.col != r->end.col) || (r->start.row != r->end.row))
		g_string_sprintfa (res->str, ":$%s$%s",
				   col_name (r->end.col), row_name (r->end.row));
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
 * @sheet : containing the selection
 * @colrow: The column or row number we are interested in.
 * @is_col: A flag indicating whether this it is a column or a row.
 *
 * Searches the selection list to see whether the entire col/row specified is
 * contained by the section regions.  Since the selection is stored as the set
 * overlapping user specifed regions we can safely search for the range directly.
 *
 * Eventually to be completely correct and deal with the case of someone manually
 * selection an entire col/row, in separate chunks,  we will need to do something
 * more advanced.
 */
gboolean
selection_contains_colrow (Sheet const *sheet, int colrow, gboolean is_col)
{
	GList *l;
	for (l = sheet->selections; l != NULL; l = l->next) {
		Range const *ss = l->data;

		if (is_col) {
			if (ss->start.row == 0 &&
			    ss->end.row >= SHEET_MAX_ROWS-1 &&
			    ss->start.col <= colrow && colrow <= ss->end.col)
				return TRUE;
		} else {
			if (ss->start.col == 0 &&
			    ss->end.col >= SHEET_MAX_COLS-1 &&
			    ss->start.row <= colrow && colrow <= ss->end.row)
				return TRUE;
		}
	}
	return FALSE;
}

/**
 * selection_foreach_range :
 * @sheet : The whose selection is being iterated.
 * @from_start : Iterate from start to end or end to start.
 * @range_cb : A function to call for each sheet.
 * @user_data :
 *
 * Iterate through the ranges in a selection.
 * NOTE : The function assumes that the callback routine does NOT change the
 * selection list.  This can be changed in the future if it is a requirement.
 */
gboolean
selection_foreach_range (Sheet *sheet, gboolean from_start,
			 gboolean (*range_cb) (Sheet *sheet,
					       Range const *range,
					       gpointer user_data),
			 gpointer user_data)
{
	GList *l;

	g_return_val_if_fail (IS_SHEET (sheet), FALSE);

	if (from_start)
		for (l = sheet->selections; l != NULL; l = l->next) {
			Range *ss = l->data;
			if (!range_cb (sheet, ss, user_data))
				return FALSE;
		}
	else
		for (l = g_list_last (sheet->selections); l != NULL; l = l->prev) {
			Range *ss = l->data;
			if (!range_cb (sheet, ss, user_data))
				return FALSE;
		}
	return TRUE;
}

/*
 * walk_boundaries : Iterates through a region by row then column.
 *
 * @sheet : The sheet being iterated in
 * @bound : The bounding range
 * @forward : iterate forward or backwards
 * @horizontal : across then down
 * @smart_merge: iterate into merged cells only at their corners
 * @res : The result.
 *
 * returns TRUE if the cursor leaves the boundary region.
 */
static gboolean
walk_boundaries (Sheet const *sheet, Range const * const bound,
		 gboolean const forward, gboolean const horizontal,
		 gboolean const smart_merge, CellPos * const res)
{
	ColRowInfo const *cri;
	int const step = forward ? 1 : -1;
	CellPos pos = sheet->edit_pos_real;
	Range const *merge;

	*res = pos;
loop :
	merge = sheet_merge_contains_pos (sheet, &pos);
	if (horizontal) {
		if (merge != NULL)
			pos.col = (forward) ? merge->end.col : merge->start.col;
		if (pos.col + step > bound->end.col) {
			if (pos.row + 1 > bound->end.row)
				return TRUE;
			pos.row++;
			pos.col = bound->start.col;
		} else if (pos.col + step < bound->start.col) {
			if (pos.row - 1 < bound->start.row)
				return TRUE;
			pos.row--;
			pos.col = bound->end.col;
		} else
			pos.col += step;
	} else {
		if (merge != NULL)
			pos.row = (forward) ? merge->end.row : merge->start.row;
		if (pos.row + step > bound->end.row) {
			if (pos.col + 1 > bound->end.col)
				return TRUE;
			pos.row = bound->start.row;
			pos.col++;
		} else if (pos.row + step < bound->start.row) {
			if (pos.col - 1 < bound->start.col)
				return TRUE;
			pos.row = bound->end.row;
			pos.col--;
		} else
			pos.row += step;
	}

	cri = sheet_col_get (sheet, pos.col);
	if (cri != NULL && !cri->visible)
		goto loop;
	cri = sheet_row_get (sheet, pos.row);
	if (cri != NULL && !cri->visible)
		goto loop;

	if (smart_merge) {
		merge = sheet_merge_contains_pos (sheet, &pos);
		if (merge != NULL) {
			if (forward) {
				if (pos.col != merge->start.col ||
				    pos.row != merge->start.row)
					goto loop;
			} else if (horizontal) {
				if (pos.col != merge->end.col ||
				    pos.row != merge->start.row)
					goto loop;
			} else {
				if (pos.col != merge->start.col ||
				    pos.row != merge->end.row)
					goto loop;
			}
		}
	}

	*res = pos;
	return FALSE;
}

void
sheet_selection_walk_step (Sheet *sheet,
			   gboolean forward,
			   gboolean horizontal)
{
	int selections_count;
	CellPos destination;
	Range const *ss;
	gboolean is_singleton = FALSE;

	g_return_if_fail (IS_SHEET (sheet));
	g_return_if_fail (sheet->selections != NULL);

	ss = sheet->selections->data;
	selections_count = g_list_length (sheet->selections);

	/* If there is no selection besides the cursor
	 * iterate through the entire sheet.  Move the
	 * cursor and selection as we go.
	 * Ignore wrapping.  At that scale it is irrelevant.
	 */
	if (selections_count == 1) {
		if (ss->start.col == ss->end.col &&
		    ss->start.row == ss->end.row)
			is_singleton = TRUE;
		else if (ss->start.col == sheet->edit_pos.col &&
			 ss->start.row == sheet->edit_pos.row) {
			Range const *merge = sheet_merge_is_corner (sheet,
				&sheet->edit_pos);
			if (merge != NULL && range_equal (merge, ss))
				is_singleton = TRUE;
		}
	}
			
	if (is_singleton) {
		Range full_sheet;
		if (horizontal) {
			full_sheet.start.col = 0;
			full_sheet.end.col   = SHEET_MAX_COLS-1;
			full_sheet.start.row =
				full_sheet.end.row   = ss->start.row;
		} else {
			full_sheet.start.col =
				full_sheet.end.col   = ss->start.col;
			full_sheet.start.row = 0;
			full_sheet.end.row   = SHEET_MAX_ROWS-1;
		}

		/* Ignore attempts to move outside the boundary region */
		if (!walk_boundaries (sheet, &full_sheet, forward, horizontal,
				      FALSE, &destination)) {
			sheet_selection_set (sheet,
					     destination.col, destination.row,
					     destination.col, destination.row,
					     destination.col, destination.row);
			sheet_make_cell_visible (sheet, sheet->edit_pos.col,
						 sheet->edit_pos.row);
		}
		return;
	}

	if (walk_boundaries (sheet, ss, forward, horizontal,
			     TRUE, &destination)) {
		if (forward) {
			GList *tmp = g_list_last (sheet->selections);
			sheet->selections =
			    g_list_concat (tmp,
					   g_list_remove_link (sheet->selections,
							       tmp));
			ss = sheet->selections->data;
			destination = ss->start;
		} else {
			GList *tmp = sheet->selections;
			sheet->selections =
			    g_list_concat (g_list_remove_link (sheet->selections,
							       tmp),
					   tmp);
			ss = sheet->selections->data;
			destination = ss->end;
		}
		if (selections_count != 1)
			sheet_cursor_set (sheet,
					  destination.col, destination.row,
					  ss->start.col, ss->start.row,
					  ss->end.col, ss->end.row, NULL);
	}

	sheet_set_edit_pos (sheet, destination.col, destination.row);
	sheet_make_cell_visible (sheet, destination.col, destination.row);
}

ColRowSelectionType
sheet_col_selection_type (Sheet const *sheet, int col)
{
	GList *l;
	int ret = COL_ROW_NO_SELECTION;

	g_return_val_if_fail (IS_SHEET (sheet), COL_ROW_NO_SELECTION);

	if (sheet->selections == NULL)
		return COL_ROW_NO_SELECTION;

	for (l = sheet->selections; l != NULL; l = l->next){
		Range *ss = l->data;

		if (ss->start.col > col ||
		    ss->end.col < col)
			continue;

		if (ss->start.row == 0 &&
		    ss->end.row == SHEET_MAX_ROWS-1)
			return COL_ROW_FULL_SELECTION;

		ret = COL_ROW_PARTIAL_SELECTION;
	}

	return ret;
}

ColRowSelectionType
sheet_row_selection_type (Sheet const *sheet, int row)
{
	GList *l;
	int ret = COL_ROW_NO_SELECTION;

	g_return_val_if_fail (IS_SHEET (sheet), COL_ROW_NO_SELECTION);

	if (sheet->selections == NULL)
		return COL_ROW_NO_SELECTION;

	for (l = sheet->selections; l != NULL; l = l->next) {
		Range *ss = l->data;

		if (ss->start.row > row ||
		    ss->end.row < row)
			continue;

		if (ss->start.col == 0 &&
		    ss->end.col == SHEET_MAX_COLS-1)
			return COL_ROW_FULL_SELECTION;

		ret = COL_ROW_PARTIAL_SELECTION;
	}

	return ret;
}

/**
 * sheet_selection_full_cols :
 * @sheet :
 * @is_cols :
 * @index :
 *
 * returns TRUE if all of the selected cols/rows in the selection
 * 	are fully selected and the selection contains the specified col.
 */
gboolean
sheet_selection_full_cols_rows (Sheet const *sheet, gboolean is_cols, int index)
{
	GList *l;
	gboolean found = FALSE;

	g_return_val_if_fail (IS_SHEET (sheet), FALSE);

	for (l = sheet->selections; l != NULL; l = l->next){
		Range const *r = l->data;
		if (is_cols) {
			if (r->start.row > 0 || r->end.row < SHEET_MAX_ROWS - 1)
				return FALSE;
			if (r->start.col <= index && index <= r->end.col)
				found = TRUE;
		} else {
			if (r->start.col > 0 || r->end.col < SHEET_MAX_COLS - 1)
				return FALSE;
			if (r->start.row <= index && index <= r->end.row)
				found = TRUE;
		}
	}

	return found;
}
