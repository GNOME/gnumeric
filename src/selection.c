/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * selection.c:  Manage selection regions.
 *
 * Author:
 *  Miguel de Icaza (miguel@gnu.org)
 *  Jody Goldberg (jody@gnome.org)
 *
 *  (C) 1999-2006 Jody Goldberg
 */
#include <gnumeric-config.h>
#include <glib/gi18n-lib.h>
#include "gnumeric.h"
#include "selection.h"

#include "sheet.h"
#include "sheet-view.h"
#include "sheet-merge.h"
#include "sheet-style.h"
#include "sheet-private.h"
#include "sheet-control.h"
#include "parse-util.h"
#include "clipboard.h"
#include "ranges.h"
#include "application.h"
#include "command-context.h"
#include "workbook-control.h"
#include "workbook-view.h"
#include "workbook-priv.h"
#include "commands.h"
#include "value.h"
#include "cell.h"

/**
 * sv_is_singleton_selected:
 * @sv :
 *
 * See if the 1st selected region is a singleton.
 *
 * Returns A GnmCellPos pointer if the selection is a singleton, and NULL if not.
 **/
GnmCellPos const *
sv_is_singleton_selected (SheetView const *sv)
{
	if (sv->cursor.move_corner.col == sv->cursor.base_corner.col &&
	    sv->cursor.move_corner.row == sv->cursor.base_corner.row)
		return &sv->cursor.move_corner;
	return NULL;
}

/**
 * sv_is_pos_selected :
 * @sv :
 * @col :
 * @row :
 *
 * Returns TRUE if the supplied position is selected in view @sv.
 **/
gboolean
sv_is_pos_selected (SheetView const *sv, int col, int row)
{
	GSList *ptr;
	GnmRange const *sr;

	for (ptr = sv->selections; ptr != NULL ; ptr = ptr->next) {
		sr = ptr->data;
		if (range_contains (sr, col, row))
			return TRUE;
	}
	return FALSE;
}

/**
 * sv_is_range_selected :
 * @sv :
 * @r  :
 *
 * Returns TRUE If @r overlaps with any part of the selection in @sv.
 **/
gboolean
sv_is_range_selected (SheetView const *sv, GnmRange const *r)
{
	GSList *ptr;
	GnmRange const *sr;

	for (ptr = sv->selections; ptr != NULL ; ptr = ptr->next){
		sr = ptr->data;
		if (range_overlap (sr, r))
			return TRUE;
	}
	return FALSE;
}

/**
 * sv_is_full_range_selected :
 *
 * @sv :
 * @r :
 *
 * Returns TRUE if all of @r is contained by the selection in @sv.
 **/
gboolean
sv_is_full_range_selected (SheetView const *sv, GnmRange const *r)
{
	GSList *ptr;
	GnmRange const *sr;

	for (ptr = sv->selections; ptr != NULL ; ptr = ptr->next) {
		sr = ptr->data;
		if (range_contained (r, sr))
			return TRUE;
	}
	return FALSE;
}

/*
 * sv_is_colrow_selected :
 * @sv : containing the selection
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
sv_is_colrow_selected (SheetView const *sv, int colrow, gboolean is_col)
{
	GSList *l;
	for (l = sv->selections; l != NULL; l = l->next) {
		GnmRange const *ss = l->data;

		if (is_col) {
			if (ss->start.row == 0 &&
			    ss->end.row >= gnm_sheet_get_max_rows (sv->sheet)-1 &&
			    ss->start.col <= colrow && colrow <= ss->end.col)
				return TRUE;
		} else {
			if (ss->start.col == 0 &&
			    ss->end.col >= gnm_sheet_get_max_cols (sv->sheet)-1 &&
			    ss->start.row <= colrow && colrow <= ss->end.row)
				return TRUE;
		}
	}
	return FALSE;
}

/**
 * sv_is_full_colrow_selected
 * @sv :
 * @is_cols :
 * @index :
 *
 * Returns TRUE if all of the selected cols/rows in the selection
 *	are fully selected and the selection contains the specified col.
 **/
gboolean
sv_is_full_colrow_selected (SheetView const *sv, gboolean is_cols, int index)
{
	GSList *l;
	gboolean found = FALSE;

	g_return_val_if_fail (IS_SHEET_VIEW (sv), FALSE);

	for (l = sv->selections; l != NULL; l = l->next){
		GnmRange const *r = l->data;
		if (is_cols) {
			if (r->start.row > 0 || r->end.row < gnm_sheet_get_max_rows (sv->sheet) - 1)
				return FALSE;
			if (r->start.col <= index && index <= r->end.col)
				found = TRUE;
		} else {
			if (r->start.col > 0 || r->end.col < gnm_sheet_get_max_cols (sv->sheet) - 1)
				return FALSE;
			if (r->start.row <= index && index <= r->end.row)
				found = TRUE;
		}
	}

	return found;
}

/**
 * sv_selection_col_type :
 * @sv :
 * @col :
 *
 * Returns How much of column @col is selected in @sv.
 **/
ColRowSelectionType
sv_selection_col_type (SheetView const *sv, int col)
{
	GSList *ptr;
	GnmRange const *sr;
	int ret = COL_ROW_NO_SELECTION;

	g_return_val_if_fail (IS_SHEET_VIEW (sv), COL_ROW_NO_SELECTION);

	if (sv->selections == NULL)
		return COL_ROW_NO_SELECTION;

	for (ptr = sv->selections; ptr != NULL; ptr = ptr->next) {
		sr = ptr->data;

		if (sr->start.col > col || sr->end.col < col)
			continue;

		if (sr->start.row == 0 &&
		    sr->end.row == gnm_sheet_get_max_rows (sv->sheet)-1)
			return COL_ROW_FULL_SELECTION;

		ret = COL_ROW_PARTIAL_SELECTION;
	}

	return ret;
}

/**
 * sv_selection_row_type :
 * @sv :
 * @col :
 *
 * Returns How much of column @col is selected in @sv.
 **/
ColRowSelectionType
sv_selection_row_type (SheetView const *sv, int row)
{
	GSList *ptr;
	GnmRange const *sr;
	int ret = COL_ROW_NO_SELECTION;

	g_return_val_if_fail (IS_SHEET_VIEW (sv), COL_ROW_NO_SELECTION);

	if (sv->selections == NULL)
		return COL_ROW_NO_SELECTION;

	for (ptr = sv->selections; ptr != NULL; ptr = ptr->next) {
		sr = ptr->data;

		if (sr->start.row > row || sr->end.row < row)
			continue;

		if (sr->start.col == 0 &&
		    sr->end.col == gnm_sheet_get_max_cols (sv->sheet)-1)
			return COL_ROW_FULL_SELECTION;

		ret = COL_ROW_PARTIAL_SELECTION;
	}

	return ret;
}

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
 * sv_menu_enable_insert :
 * @sv :
 * @col :
 * @row :
 *
 * control whether or not it is ok to insert cols or rows.  An internal routine
 * used by the selection mechanism to avoid erasing the entire sheet when
 * inserting the wrong dimension.
 */
static void
sv_menu_enable_insert (SheetView *sv, gboolean col, gboolean row)
{
	int flags = 0;

	g_return_if_fail (IS_SHEET_VIEW (sv));

	if (sv->enable_insert_cols != col) {
		flags |= MS_INSERT_COLS;
		sv->enable_insert_cols = col;
	}
	if (sv->enable_insert_rows != row) {
		flags |= MS_INSERT_ROWS;
		sv->enable_insert_rows = row;
	}
	if (sv->enable_insert_cells != (col|row)) {
		flags |= MS_INSERT_CELLS;
		sv->enable_insert_cells = (col|row);
	}

	/* during initialization it does not matter */
	if (!flags || sv->sheet == NULL)
		return;

	WORKBOOK_VIEW_FOREACH_CONTROL(sv_wbv (sv), wbc,
		wb_control_menu_state_update (wbc, flags););
}

/**
 * selection_first_range
 * @sv		 : The #SheetView whose selection we are testing.
 * @wbc          : The calling context to report errors to (GUI or corba)
 * @command_name : A string naming the operation requiring a single range.
 *
 * Returns the first range, if a control is supplied it displays an error if
 *    there is more than one range.
 **/
GnmRange const *
selection_first_range (SheetView const *sv,
		       GOCmdContext *cc, char const *cmd_name)
{
	GnmRange const *r;
	GSList *l;

	g_return_val_if_fail (IS_SHEET_VIEW (sv), NULL);

	l = sv->selections;

	g_return_val_if_fail (l != NULL && l->data != NULL, NULL);

	r = l->data;
	if (cc != NULL && l->next != NULL) {
		GError *msg = g_error_new (go_error_invalid(), 0,
			_("%s does not support multiple ranges"), cmd_name);
		go_cmd_context_error (cc, msg);
		g_error_free (msg);
		return NULL;
	}

	return r;
}

/**
 * sv_selection_extend_to:
 *
 * @sv: the sheet
 * @col:   column that gets covered (negative indicates all cols)
 * @row:   row that gets covered (negative indicates all rows)
 *
 * This extends the selection to cover col, row and updates the status areas.
 */
void
sv_selection_extend_to (SheetView *sv, int col, int row)
{
	int base_col, base_row;

	if (col < 0) {
		base_col = 0;
		col = gnm_sheet_get_max_cols (sv->sheet) - 1;
	} else
		base_col = sv->cursor.base_corner.col;
	if (row < 0) {
		base_row = 0;
		row = gnm_sheet_get_max_rows (sv->sheet) - 1;
	} else
		base_row = sv->cursor.base_corner.row;

	/* If nothing was going to change dont redraw */
	if (sv->cursor.move_corner.col == col &&
	    sv->cursor.move_corner.row == row &&
	    sv->cursor.base_corner.col == base_col &&
	    sv->cursor.base_corner.row == base_row)
		return;

	sv_selection_set (sv, &sv->edit_pos, base_col, base_row, col, row);

	/*
	 * FIXME : Does this belong here ?
	 * This is a convenient place to put it so that changes to the
	 * selection also update the status region, but this is somewhat lower
	 * level that I want to do this.
	 */
	sheet_update (sv->sheet);
	WORKBOOK_FOREACH_VIEW (sv->sheet->workbook, view, {
		if (wb_view_cur_sheet (view) == sv->sheet)
			wb_view_selection_desc (view, FALSE, NULL);
	});
}

static void
sheet_selection_set_internal (SheetView *sv,
			      GnmCellPos const *edit,
			      int base_col, int base_row,
			      int move_col, int move_row,
			      gboolean just_add_it)
{
	GSList *list;
	GnmRange *ss;
	GnmRange old_sel, new_sel;
	gboolean do_cols, do_rows;

	g_return_if_fail (sv->selections != NULL);

	new_sel.start.col = MIN(base_col, move_col);
	new_sel.start.row = MIN(base_row, move_row);
	new_sel.end.col = MAX(base_col, move_col);
	new_sel.end.row = MAX(base_row, move_row);

	g_return_if_fail (range_is_sane (&new_sel));

	if (sv->sheet != NULL) /* beware initialization */
		gnm_sheet_merge_find_container (sv->sheet, &new_sel);
	ss = (GnmRange *)sv->selections->data;
	if (!just_add_it && range_equal (ss, &new_sel))
		return;

	old_sel = *ss;
	*ss = new_sel;

	/* Set the cursor boundary */
	sv_cursor_set (sv, edit,
		base_col, base_row,
		move_col, move_row, ss);

	if (just_add_it) {
		sv_redraw_range	(sv, &new_sel);
		sv_redraw_headers (sv, TRUE, TRUE, &new_sel);
		goto set_menu_flags;
	}

	if (range_overlap (&old_sel, &new_sel)) {
		GSList *ranges, *l;
		/*
		 * Compute the blocks that need to be repainted: those that
		 * are in the complement of the intersection.
		 */
		ranges = range_fragment (&old_sel, &new_sel);

		for (l = ranges->next; l; l = l->next)
			sv_redraw_range	(sv, l->data);
		range_fragment_free (ranges);
	} else {
		sv_redraw_range (sv, &old_sel);
		sv_redraw_range (sv, &new_sel);
	}

	/* Has the entire row been selected/unselected */
	if ((new_sel.start.row == 0 && new_sel.end.row == gnm_sheet_get_max_rows (sv->sheet)-1) ^
	    (old_sel.start.row == 0 && old_sel.end.row == gnm_sheet_get_max_rows (sv->sheet)-1)) {
		GnmRange tmp = range_union (&new_sel, &old_sel);
		sv_redraw_headers (sv, TRUE, FALSE, &tmp);
	} else {
		GnmRange tmp = new_sel;
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
			sv_redraw_headers (sv, TRUE, FALSE, &tmp);
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
			sv_redraw_headers (sv, TRUE, FALSE, &tmp);
		}
	}

	/* Has the entire col been selected/unselected */
	if ((new_sel.start.col == 0 && new_sel.end.col == gnm_sheet_get_max_cols (sv->sheet)-1) ^
	    (old_sel.start.col == 0 && old_sel.end.col == gnm_sheet_get_max_cols (sv->sheet)-1)) {
		GnmRange tmp = range_union (&new_sel, &old_sel);
		sv_redraw_headers (sv, FALSE, TRUE, &tmp);
	} else {
		GnmRange tmp = new_sel;
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
			sv_redraw_headers (sv, FALSE, TRUE, &tmp);
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
			sv_redraw_headers (sv, FALSE, TRUE, &tmp);
		}
	}

set_menu_flags:
	sv_flag_selection_change (sv);

	/*
	 * Now see if there is some selection which selects a
	 * whole row, a whole column or the whole sheet and de-activate
	 * insert row/cols and the flags accordingly.
	 */
	do_rows = do_cols = TRUE;
	for (list = sv->selections; list && (do_cols || do_rows); list = list->next) {
		GnmRange const *r = list->data;

		if (do_cols && range_is_full (r, TRUE))
			do_cols = FALSE;
		if (do_rows && range_is_full (r, FALSE))
			do_rows = FALSE;
	}
	sv_menu_enable_insert (sv, do_cols, do_rows);

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
	 *		WORKBOOK_VIEW_FOREACH_CONTROL(view, wbc,
	 *			wb_control_menu_state_update (wbc, sheet, MS_SHOWHIDE_DETAIL););
	 *      }
	 * });
	 */
}

void
sv_selection_set (SheetView *sv, GnmCellPos const *edit,
		  int base_col, int base_row,
		  int move_col, int move_row)
{
	g_return_if_fail (IS_SHEET_VIEW (sv));

	sheet_selection_set_internal (sv, edit,
		base_col, base_row,
		move_col, move_row, FALSE);
}

/**
 * sv_selection_add_full :
 * @sv             : #SheetView whose selection is append to.
 * @edit_{col,row} : cell to mark as the new edit cursor.
 * @base_{col,row} : stationary corner of the newly selected range.
 * @move_{col,row} : moving corner of the newly selected range.
 *
 * Prepends a range to the selection list and sets the edit position.
 **/
void
sv_selection_add_full (SheetView *sv,
		       int edit_col, int edit_row,
		       int base_col, int base_row,
		       int move_col, int move_row)
{
	GnmRange *ss;
	GnmCellPos edit;

	g_return_if_fail (IS_SHEET_VIEW (sv));

	/* Create and prepend new selection */
	ss = g_new0 (GnmRange, 1);
	sv->selections = g_slist_prepend (sv->selections, ss);
	edit.col = edit_col;
	edit.row = edit_row;
	sheet_selection_set_internal (sv, &edit,
		base_col, base_row,
		move_col, move_row, TRUE);
}

void
sv_selection_add_range (SheetView *sv, GnmRange const *r)
{
	sv_selection_add_full (sv, r->start.col, r->start.row,
		r->start.col, r->start.row, r->end.col, r->end.row);
}
void
sv_selection_add_pos (SheetView *sv, int col, int row)
{
	sv_selection_add_full (sv, col, row, col, row, col, row);
}

/**
 * sv_selection_free
 * @sv: #SheetView
 *
 * Releases the selection associated with @sv
 *
 * WARNING: This does not set a new selection and leaves the view in an
 *		INVALID STATE.
 **/
void
sv_selection_free (SheetView *sv)
{
	GSList *list;

	for (list = sv->selections; list; list = list->next)
		g_free (list->data);
	g_slist_free (sv->selections);
	sv->selections = NULL;
}

/**
 * sv_selection_reset :
 * @sv:  The sheet view
 *
 * Releases the selection associated with @sv , and forces a redraw of the
 * previously selected regions and headers.
 *
 * WARNING: This does not set a new selection and leaves the view in an
 *		INVALID STATE.
 **/
void
sv_selection_reset (SheetView *sv)
{
	GSList *list, *tmp;

	g_return_if_fail (IS_SHEET_VIEW (sv));

	/* Empty the sheets selection */
	list = sv->selections;
	sv->selections = NULL;

	/* Redraw the grid, & headers for each region */
	for (tmp = list; tmp; tmp = tmp->next){
		GnmRange *ss = tmp->data;
		sv_redraw_range (sv, ss);
		sv_redraw_headers (sv, TRUE, TRUE, ss);
		g_free (ss);
	}

	g_slist_free (list);

	/* Make sure we re-enable the insert col/row and cell menu items */
	sv_menu_enable_insert (sv, TRUE, TRUE);
}

/**
 * selection_get_ranges:
 * @sv: #SheetView
 * @allow_intersection : Divide the selection into nonoverlapping subranges.
 *
 * Caller is responsible for free the list and the content.
 **/
GSList *
selection_get_ranges (SheetView const *sv, gboolean allow_intersection)
{
	GSList  *l;
	GSList *proposed = NULL;

#undef DEBUG_SELECTION
#ifdef DEBUG_SELECTION
	g_printerr ("============================\n");
#endif

	/*
	 * Run through all the selection regions to see if any of
	 * the proposed regions overlap.  Start the search with the
	 * single user proposed segment and accumulate distict regions.
	 */
	for (l = sv->selections; l != NULL; l = l->next) {
		GnmRange const *r = l->data;

		/* The set of regions that do not interset with b or
		 * its predecessors */
		GSList *clear = NULL;
		GnmRange *tmp, *b = range_dup (r);

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
			GnmRange *a = proposed->data;
			proposed = g_slist_remove (proposed, a);

			/* The region was already subsumed completely by previous
			 * elements */
			if (b == NULL) {
				clear = g_slist_prepend (clear, a);
				continue;
			}

#ifdef DEBUG_SELECTION
			g_printerr ("a = ");
			range_dump (a, "; b = ");
			range_dump (b, "\n");
#endif

			col_intersect =
				segments_intersect (a->start.col, a->end.col,
						    b->start.col, b->end.col);

#ifdef DEBUG_SELECTION
			g_printerr ("col = %d\na = %s", col_intersect, col_name(a->start.col));
			if (a->start.col != a->end.col)
				g_printerr (" -> %s", col_name(a->end.col));
			g_printerr ("\nb = %s", col_name(b->start.col));
			if (b->start.col != b->end.col)
				g_printerr (" -> %s\n", col_name(b->end.col));
			else
				g_printerr ("\n");
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
			g_printerr ("row = %d\na = %s", row_intersect, row_name (a->start.row));
			if (a->start.row != a->end.row)
				g_printerr (" -> %s", row_name (a->end.row));
			g_printerr ("\nb = %s", row_name (b->start.row));
			if (b->start.row != b->end.row)
				g_printerr (" -> %s\n", row_name (b->end.row));
			else
				g_printerr ("\n");
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
				}
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
					if (b->end.row < (gnm_sheet_get_max_rows (sv->sheet)-1)) {
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
				}
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
				}
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
				}
				break;
			}

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
 * sv_selection_apply:
 * @sv: #SheetView
 * @func:  The function to apply.
 * @allow_intersection : Call the routine for the non-intersecting subregions.
 * @closure : A parameter to pass to each invocation of @func.
 *
 * Applies the specified function for all ranges in the selection.  Optionally
 * select whether to use the high level potentially over lapped ranges, rather
 * than the smaller system created non-intersection regions.
 */
void
sv_selection_apply (SheetView *sv, SelectionApplyFunc const func,
		    gboolean allow_intersection,
		    void * closure)
{
	GSList *l;
	GSList *proposed = NULL;

	g_return_if_fail (IS_SHEET_VIEW (sv));

	if (allow_intersection) {
		for (l = sv->selections; l != NULL; l = l->next) {
			GnmRange const *ss = l->data;

			(*func) (sv, ss, closure);
		}
	} else {
		proposed = selection_get_ranges (sv, allow_intersection);
		while (proposed != NULL) {
			/* pop the 1st element off the list */
			GnmRange *r = proposed->data;
			proposed = g_slist_remove (proposed, r);

#ifdef DEBUG_SELECTION
			range_dump (r, "\n");
#endif

			(*func) (sv, r, closure);
			g_free (r);
		}
	}
}

typedef struct {
	GString *str;
	gboolean include_sheet_name_prefix;
} selection_to_string_closure;

static void
cb_range_to_string (SheetView *sv, GnmRange const *r, void *closure)
{
	GnmConventionsOut out;
	GnmRangeRef rr;
	GnmParsePos pp;
	selection_to_string_closure *res = closure;

	if (res->str->len)
		g_string_append_c (res->str, ',');

	if (res->include_sheet_name_prefix)
		g_string_append_printf (res->str, "%s!", sv->sheet->name_quoted);

	out.accum = res->str;
	out.pp = parse_pos_init_sheet (&pp, sv->sheet);
	out.convs = sheet_get_conventions (sv->sheet);

	gnm_cellref_init (&rr.a, NULL, r->start.col, r->start.row, FALSE);
	gnm_cellref_init (&rr.b, NULL, r->end.col, r->end.row, FALSE);
	rangeref_as_string (&out, &rr);
}

char *
selection_to_string (SheetView *sv, gboolean include_sheet_name_prefix)
{
	char    *output;
	selection_to_string_closure res;

	res.str = g_string_new (NULL);
	res.include_sheet_name_prefix = include_sheet_name_prefix;

	/* sv_selection_apply will check all necessary invariants. */
	sv_selection_apply (sv, &cb_range_to_string, TRUE, &res);

	output = res.str->str;
	g_string_free (res.str, FALSE);
	return output;
}

/**
 * sv_selection_foreach :
 * @sv : The whose selection is being iterated.
 * @range_cb : A function to call for each selected range.
 * @user_data :
 *
 * Iterate through the ranges in a selection.
 * NOTE : The function assumes that the callback routine does NOT change the
 * selection list.  This can be changed in the future if it is a requirement.
 */
gboolean
sv_selection_foreach (SheetView *sv,
			 gboolean (*range_cb) (SheetView *sv,
					       GnmRange const *range,
					       gpointer user_data),
			 gpointer user_data)
{
	GSList *l;

	g_return_val_if_fail (IS_SHEET_VIEW (sv), FALSE);

	for (l = sv->selections; l != NULL; l = l->next) {
		GnmRange *ss = l->data;
		if (!range_cb (sv, ss, user_data))
			return FALSE;
	}
	return TRUE;
}

/* A protected sheet can limit whether locked and unlocked cells can be
 * selected */
gboolean
sheet_selection_is_allowed (Sheet const *sheet, GnmCellPos const *pos)
{
	GnmStyle const *style;

	if (!sheet->is_protected)
		return TRUE;
	style = sheet_style_get	(sheet, pos->col, pos->row);
	if (gnm_style_get_contents_locked (style))
		return sheet->protected_allow.select_locked_cells;
	else
		return sheet->protected_allow.select_unlocked_cells;
}

/**
 * walk_boundaries : Iterates through a region by row then column.
 *
 * @sv : The sheet being iterated in
 * @bound : The bounding range
 * @forward : iterate forward or backwards
 * @horizontal : across then down
 * @smart_merge: iterate into merged cells only at their corners
 * @res : The result.
 *
 * returns TRUE if the cursor leaves the boundary region.
 **/
static gboolean
walk_boundaries (SheetView const *sv, GnmRange const * const bound,
		 gboolean const forward, gboolean const horizontal,
		 gboolean const smart_merge, GnmCellPos * const res)
{
	ColRowInfo const *cri;
	int const step = forward ? 1 : -1;
	GnmCellPos pos = sv->edit_pos_real;
	GnmRange const *merge;

	*res = pos;
loop :
	merge = gnm_sheet_merge_contains_pos (sv->sheet, &pos);
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

	cri = sheet_col_get (sv->sheet, pos.col);
	if (cri != NULL && !cri->visible)
		goto loop;
	cri = sheet_row_get (sv->sheet, pos.row);
	if (cri != NULL && !cri->visible)
		goto loop;

	if (!sheet_selection_is_allowed (sv->sheet, &pos))
		goto loop;

	if (smart_merge) {
		merge = gnm_sheet_merge_contains_pos (sv->sheet, &pos);
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

/**
 * sv_selection_walk_step :
 * @sv : #SheetView
 * @forward :
 * @horizontal :
 *
 * Move the edit_pos of @sv 1 step according to @forward and @horizontal.  The
 * behavior depends several factors
 *	- How many ranges are selected
 *	- The shape of the selected ranges
 *	- Previous movements (A sequence of tabs followed by an enter can jump
 *		to the 1st col).
 **/
void
sv_selection_walk_step (SheetView *sv, gboolean forward, gboolean horizontal)
{
	int selections_count;
	GnmCellPos destination;
	GnmRange const *ss;
	gboolean is_singleton = FALSE;

	g_return_if_fail (IS_SHEET_VIEW (sv));
	g_return_if_fail (sv->selections != NULL);

	ss = sv->selections->data;
	selections_count = g_slist_length (sv->selections);

	/* If there is no selection besides the cursor iterate through the
	 * entire sheet.  Move the cursor and selection as we go.  Ignore
	 * wrapping.  At that scale it is irrelevant.  */
	if (selections_count == 1) {
		if (range_is_singleton (ss))
			is_singleton = TRUE;
		else if (ss->start.col == sv->edit_pos.col &&
			 ss->start.row == sv->edit_pos.row) {
			GnmRange const *merge = gnm_sheet_merge_is_corner (sv->sheet,
				&sv->edit_pos);
			if (merge != NULL && range_equal (merge, ss))
				is_singleton = TRUE;
		}
	}

	if (is_singleton) {
		int const first_tab_col = sv->first_tab_col;
		int const cur_col = sv->edit_pos.col;
		GnmRange bound;

		/* Interesting :  Normally we bound the movement to the current
		 *	col/row.  However, if a sheet is protected, and
		 *	differentiates between selecting locked vs
		 *	unlocked cells, then we do not bound things, and allow
		 *	movement to any cell that is acceptable. */
		if (sv->sheet->is_protected &&
		    (sv->sheet->protected_allow.select_locked_cells ^
		     sv->sheet->protected_allow.select_unlocked_cells))
			range_init_full_sheet (&bound);
		else if (horizontal)
			range_init_rows (&bound, ss->start.row, ss->start.row);
		else
			range_init_cols (&bound, ss->start.col, ss->start.col);

		/* Ignore attempts to move outside the boundary region */
		if (!walk_boundaries (sv, &bound, forward, horizontal,
				      FALSE, &destination)) {

			/* <Enter> after some tabs jumps to the first col we tabbed from */
			if (forward && !horizontal && first_tab_col >= 0)
				destination.col = first_tab_col;

			sv_selection_set (sv, &destination,
					  destination.col, destination.row,
					  destination.col, destination.row);
			sv_make_cell_visible (sv, sv->edit_pos.col,
					      sv->edit_pos.row, FALSE);
			if (horizontal)
				sv->first_tab_col = (first_tab_col < 0 || cur_col < first_tab_col) ? cur_col : first_tab_col;
		}
		return;
	}

	if (walk_boundaries (sv, ss, forward, horizontal,
			     TRUE, &destination)) {
		if (forward) {
			GSList *tmp = g_slist_last (sv->selections);
			sv->selections = g_slist_concat (tmp,
				g_slist_remove_link (sv->selections, tmp));
			ss = sv->selections->data;
			destination = ss->start;
		} else {
			GSList *tmp = sv->selections;
			sv->selections = g_slist_concat (
				g_slist_remove_link (sv->selections, tmp),
				tmp);
			ss = sv->selections->data;
			destination = ss->end;
		}
		if (selections_count != 1)
			sv_cursor_set (sv, &destination,
				       ss->start.col, ss->start.row,
				       ss->end.col, ss->end.row, NULL);
	}

	sv_set_edit_pos (sv, &destination);
	sv_make_cell_visible (sv, destination.col, destination.row, FALSE);
}

#include <goffice/graph/gog-series.h>
#include <goffice/graph/gog-plot-impl.h>
#include <goffice/data/go-data.h>
#include <expr.h>
#include <graph.h>

/* characterize a vector based on the last non-blank cell in the range.
 * optionally expand the vector to merge multiple string vectors */
static gboolean
characterize_vec (Sheet *sheet, GnmRange *vector,
		  gboolean as_cols, gboolean expand_text)
{
	GnmCell *cell;
	GnmValue const *v;
	GnmRange tmp;
	int dx = 0, dy = 0;
	gboolean is_string = FALSE;

	while (1) {
		tmp = *vector;
		if (!sheet_range_trim (sheet, &tmp, as_cols, !as_cols)) {
			cell = sheet_cell_get (sheet, tmp.end.col+dx, tmp.end.row+dy);
			if (cell == NULL)
				return is_string;
			gnm_cell_eval (cell);
			v = cell->value;

			if (v == NULL || !VALUE_IS_STRING(v))
				return is_string;
			is_string = TRUE;
			if (!expand_text)
				return TRUE;
			if (as_cols) {
				if (vector->end.col >= gnm_sheet_get_max_cols (sheet)-1)
					return TRUE;
				vector->end.col += dx;
				dx = 1;
			} else {
				if (vector->end.row >= gnm_sheet_get_max_rows (sheet)-1)
					return TRUE;
				vector->end.row += dy;
				dy = 1;
			}
		} else
			return is_string;
	}

	return is_string; /* NOTREACHED */
}

void
sv_selection_to_plot (SheetView *sv, GogPlot *go_plot)
{
	GSList *ptr, *sels;
	GnmRange const *r;
	int num_cols, num_rows;

	Sheet *sheet = sv_sheet (sv);
	GnmCellRef header;
	GogPlot *plot = go_plot;
	GogPlotDesc const *desc;
	GogSeries *series;
	gboolean is_string_vec, first_series = TRUE, first_value_dim = TRUE;
	unsigned i, count, cur_dim = 0, num_series = 1;
	gboolean has_header, as_cols;

	gboolean default_to_cols;

	/* Use the total number of cols vs rows in all of the selected regions.
	 * We can not use just one in case one of the others happens to be the transpose
	 * eg select A1 + A:B would default_to_cols = FALSE, then produce a vector for each row */
	num_cols = num_rows = 0;
	for (ptr = sv->selections; ptr != NULL ; ptr = ptr->next) {
		r = ptr->data;
		num_cols += range_width (r);
		num_rows += range_height (r);
	}

	/* Excel docs claim that rows == cols uses rows */
	default_to_cols = (num_cols < num_rows);

	desc = gog_plot_description (plot);
	series = gog_plot_new_series (plot);

	header.sheet = sheet;
	header.col_relative = header.row_relative = FALSE;


/* FIXME : a cheesy quick implementation */
	cur_dim = desc->series.num_dim - 1;
	if (desc->series.dim[cur_dim].val_type == GOG_DIM_MATRIX) {
		/* Here, only the first range is used. It is assumed it is large enough
		to retrieve the axis data and the matrix z values. We probably should raise
		an error condition if it is not the case */
		/* selections are in reverse order so walk them backwards */
		GSList const *ptr = g_slist_last (sv->selections);
		GnmRange vector = *((GnmRange const *)ptr->data);
		int start_row = vector.start.row;
		int start_col = vector.start.col;
		int end_row = vector.end.row;
		int end_col = vector.end.col;
		/* check if we need X and Y axis labels */
		if (desc->series.num_dim > 1) {
			/* first row will be used as X labels */
			start_row ++;
			if (desc->series.num_dim > 2) {
				/* first column will be used as Y labels */
				start_col ++;
				vector.start.row = start_row;
				vector.start.col = vector.end.col = start_col - 1;
				gog_series_set_dim (series, cur_dim - 1,
					gnm_go_data_vector_new_expr (sheet,
						gnm_expr_top_new_constant (
							value_new_cellrange_r (sheet, &vector))), NULL);
			}
			vector.start.row = vector.end.row  = start_row - 1;
			vector.start.col = start_col;
			vector.end.col = end_col;
			/* we assume that there are at most three dims (X, Y and Z) */
			gog_series_set_dim (series, 0,
				gnm_go_data_vector_new_expr (sheet,
					gnm_expr_top_new_constant (
						value_new_cellrange_r (sheet, &vector))), NULL);
		}
		vector.start.row = start_row;
		vector.end.row = end_row;
		gog_series_set_dim (series, cur_dim,
			gnm_go_data_matrix_new_expr (sheet,
				gnm_expr_top_new_constant (
					value_new_cellrange_r (sheet, &vector))), NULL);
		return;
	}

	/* selections are in reverse order so walk them backwards */
	cur_dim = 0;
	sels = ptr = g_slist_reverse (g_slist_copy (sv->selections));
	for (; ptr != NULL; ptr = ptr->next) {
		GnmRange vector = *((GnmRange const *)ptr->data);

		/* Special case the handling of a vector rather than a range.
		 * it should stay in its orientation,  only ranges get split */
		as_cols = (vector.start.col == vector.end.col || default_to_cols);
		has_header = sheet_range_has_heading (sheet, &vector, as_cols, TRUE);
		header.col = vector.start.col;
		header.row = vector.start.row;

		if (as_cols) {
			if (has_header)
				vector.start.row++;
			count = vector.end.col - vector.start.col;
			vector.end.col = vector.start.col;
		} else {
			if (has_header)
				vector.start.col++;
			count = vector.end.row - vector.start.row;
			vector.end.row = vector.start.row;
		}

		for (i = 0 ; i <= count ; ) {
			if (cur_dim >= desc->series.num_dim) {
				if (num_series >= desc->num_series_max)
					break;

				series = gog_plot_new_series (plot);
				first_series = FALSE;
				first_value_dim = TRUE;
				cur_dim = 0;
				num_series++;
			}

			/* skip over shared dimensions already assigned */
			while (cur_dim < desc->series.num_dim &&
			       !first_series && desc->series.dim[cur_dim].is_shared)
				++cur_dim;

			while (cur_dim < desc->series.num_dim && desc->series.dim[cur_dim].priority == GOG_SERIES_ERRORS)
				++cur_dim;
			if (cur_dim >= desc->series.num_dim)
				continue;

			is_string_vec = characterize_vec (sheet, &vector, as_cols,
				desc->series.dim[cur_dim].val_type == GOG_DIM_LABEL);
			while ((desc->series.dim[cur_dim].val_type == GOG_DIM_LABEL && !is_string_vec) ||
			       (desc->series.dim[cur_dim].val_type == GOG_DIM_VALUE && is_string_vec)) {
				if (desc->series.dim[cur_dim].priority == GOG_SERIES_REQUIRED)
					goto skip;
				cur_dim++;
			}

			gog_series_set_dim (series, cur_dim,
				gnm_go_data_vector_new_expr (sheet,
					gnm_expr_top_new_constant (
						value_new_cellrange_r (sheet, &vector))), NULL);

			if (has_header && first_value_dim &&
			    desc->series.dim[cur_dim].val_type == GOG_DIM_VALUE) {
				first_value_dim = FALSE;
				gog_series_set_name (series,
					GO_DATA_SCALAR (gnm_go_data_scalar_new_expr (sheet,
						gnm_expr_top_new (gnm_expr_new_cellref (&header)))), NULL);
			}

			cur_dim++;
skip :

			if (as_cols) {
				i += range_width (&vector);
				header.col = vector.start.col = ++vector.end.col;
			} else {
				i += range_height (&vector);
				header.row = vector.start.row = ++vector.end.row;
			}
		}
	}

	g_slist_free (sels);

#warning TODO is last series is incomplete try to shift data out of optional dimensions
}
