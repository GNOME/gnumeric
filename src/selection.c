/*
 * selection.c:  Manage selection regions.
 *
 * Author:
 *  Miguel de Icaza (miguel@gnu.org)
 *
 */
#include "selection.h"
#include "gnumeric-sheet.h"
#include "utils.h"
#include "clipboard.h"
#include "gnumeric-util.h"

/* FIXME : why do we need this ?  split things better so that we can avoid it */
#define GNUMERIC_SHEET_VIEW(p) GNUMERIC_SHEET (SHEET_VIEW(p)->sheet_view);

#if 0
/*
This is some of the new code to break regions into non-intesecting areas.
I will enable it after the code transfer.
*/

/* Quick utility routine to test intersect of line segments.
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

static SheetSelection *
SheetSelection_copy (SheetSelection const * src)
{
	SheetSelection * res;
	res = g_new (SheetSelection, 1);

	res->start_col = src->start_col;
	res->end_col   = src->end_col;
	res->start_row = src->start_row;
	res->end_row   = src->end_row;
	return res;
}

static void
SheetSelection_dump (SheetSelection const * src)
{
	/* keep these as 2 print statements, because
	 * col_name uses a static buffer */
	fprintf (stderr, "%s%d",
		col_name(src->start_col),
		src->start_row + 1);
	fprintf (stderr, ":%s%d\n",
		col_name(src->end_col),
		src->end_row + 1);
}

void
selection_append_range (Sheet *sheet,
			int start_col, int start_row,
			int end_col,   int end_row)
{
	SheetSelection *ss, *tmp, orig;
	GList *l, *next, *proposed;

	g_return_if_fail (sheet != NULL);
	g_return_if_fail (IS_SHEET (sheet));

	ss = g_new0 (SheetSelection, 1);

	ss->start_col = start_col;
	ss->end_col   = end_col;
	ss->start_row = start_row;
	ss->end_row   = end_row;

	orig = *ss; /* NOTE : copy struct */

	fprintf (stderr, "\n%d %d %d %d\n",
		 start_col, start_row, end_col, end_row);
	SheetSelection_dump (ss);

	fprintf(stderr, "=========================================\n ");
	/*
	 * Run through all the selection regions to see if any of
	 * the proposed regions overlap
	 */
	proposed = g_list_prepend (NULL, ss);
	for (l = sheet->selections; l != NULL; l = next) {
		/* The set of regions that do not interset with b or
		 * its predecessors */
		GList *clear = NULL;
		int i = 0;

		SheetSelection *b = l->data;
		fprintf(stderr, "\nTest %d = ", ++i);
		SheetSelection_dump (b);

		/* prefetch in case the element gets removed */
		next = l->next;


		/* run through the proposed regions and handle any that
		 * overlap with the current selection region
		 */
		while (proposed != NULL) {
			int row_intersect, col_intersect;

			/* pop the 1st element off the list */
			SheetSelection *a = proposed->data;

			fprintf(stderr, "proposed = ");
			SheetSelection_dump (a);

			proposed = g_list_remove (proposed, a);

			col_intersect =
				segments_intersect (a->start_col, a->end_col,
						    b->start_col, b->end_col);

			fprintf (stderr, "Col intersect = %d\n", col_intersect);
			/* No intersection */
			if (col_intersect == 0) {
				clear = g_list_prepend (clear, a);
				continue;
			}

			row_intersect =
				segments_intersect (a->start_row, a->end_row,
						    b->start_row, b->end_row);
			fprintf (stderr, "Row intersect = %d\n", row_intersect);
			/* No intersection */
			if (row_intersect == 0) {
				clear = g_list_prepend (clear, a);
				continue;
			}

			/* Cross product of intersection cases */
			switch (col_intersect) {
			case 4 : /* a contains b */
				switch (row_intersect) {
				case 4 : /* a contains b */
					/* Old region contained by new region */

					/* remove old region from selection */
					sheet->selections = g_list_remove(sheet->selections, b);
					break;

				case 3 : /* overlap top */
					/* Shrink existing range */
					b->end_row = a->start_row - 1;
					break;

				case 2 : /* b contains a */
					/* Split existing range */
					if (b->start_col > 0) {
						tmp = SheetSelection_copy (a);
						tmp->end_col = b->start_col - 1;
						clear = g_list_prepend (clear, tmp);
					}
					/* Fall through to do bottom segment */

				case 1 : /* overlap bottom */
					/* Shrink existing range */
					a->start_col = b->end_col + 1;
					break;

				default :
					g_assert_not_reached();
				};
				break;

			case 3 : /* overlap left */
				switch (row_intersect) {
				case 4 : /* a contains b */
					/* Shrink old region */
					b->start_col = a->end_col + 1;
					break;

				case 3 : /* overlap top */
					/* Split region */
					if (b->start_row > 0) {
						tmp = SheetSelection_copy (a);
						tmp->start_col = b->start_col;
						tmp->end_row = b->start_row - 1;
						clear = g_list_prepend (clear, tmp);
					}
					/* fall through */

				case 2 : /* b contains a */
					/* shrink the left segment */
					a->end_col = b->start_col - 1;
					break;

				case 1 : /* overlap bottom */
					/* Split region */
					if (b->end_row < (SHEET_MAX_ROWS-1)) {
						tmp = SheetSelection_copy (a);
						tmp->start_col = b->start_col;
						tmp->start_row = b->end_row + 1;
						clear = g_list_prepend (clear, tmp);
					}

					/* shrink the left segment */
					if (b->start_col == 0) {
						g_free(a);
						continue;
					}
					a->end_col = b->start_col - 1;
					break;

				default :
					g_assert_not_reached();
				};
				break;

			case 2 : /* b contains a */
				switch (row_intersect) {
				case 4 : /* a contains b */
					/* Split region */
					tmp = SheetSelection_copy (a);
					tmp->start_row = b->end_row + 1;
					clear = g_list_prepend (clear, tmp);
					/* fall through */

				case 3 : /* overlap top */
					/* shrink the top segment */
					a->end_row = b->start_row - 1;
					break;

				case 2 : /* b contains a */
					/* remove the selection */
					g_free (a);
					continue;

				case 1 : /* overlap bottom */
					/* shrink the top segment */
					a->start_row = b->end_row + 1;
					break;

				default :
					g_assert_not_reached();
				};
				break;

			case 1 : /* overlap right */
				switch (row_intersect) {
				case 4 : /* a contains b */
					/* Shrink old region */
					b->end_col = a->start_col - 1;
					break;

				case 3 : /* overlap top */
					/* Split region */
					tmp = SheetSelection_copy (a);
					tmp->end_col = b->end_col;
					tmp->end_row = b->start_row - 1;
					/* fall through */

				case 2 : /* b contains a */
					/* shrink the right segment */
					a->start_col = b->end_col + 1;
					break;

				case 1 : /* overlap bottom */
					/* Split region */
					tmp = SheetSelection_copy (a);
					tmp->end_col = b->end_col;
					tmp->start_row = b->end_row + 1;

					/* shrink the right segment */
					a->start_col = b->end_col + 1;
					break;

				default :
					g_assert_not_reached();
				};
				break;

			};
			/* Be careful putting code here one of the cases skips this */

			/* continue checking the new region for intersections */
			clear = g_list_prepend (clear, a);
		}
		proposed = clear;
	}

	/* Catch attempts to select something twice */
	if (proposed == NULL)
		return;

	sheet->selections = g_list_concat (sheet->selections, proposed);

	sheet_accept_pending_input (sheet);

	for (l = sheet->sheet_views; l; l = l->next){
		GnumericSheet *gsheet = GNUMERIC_SHEET_VIEW (l->data);

		gnumeric_sheet_set_selection (gsheet, &orig);
	}
	sheet_redraw_selection (sheet, &orig);

	sheet_redraw_cols (sheet);
	sheet_redraw_rows (sheet);

	sheet_selection_changed_hook (sheet);
}
#endif
int
sheet_selection_equal (SheetSelection *a, SheetSelection *b)
{
	if (a->start_col != b->start_col)
		return 0;
	if (a->start_row != b->start_row)
		return 0;

	if (a->end_col != b->end_col)
		return 0;
	if (a->end_row != b->end_row)
		return 0;
	return 1;
}
 
static const char *
sheet_get_selection_name (Sheet *sheet)
{
	SheetSelection *ss = sheet->selections->data;
	static char buffer [10 + 2 * 4 * sizeof (int)];

	if (ss->start_col == ss->end_col && ss->start_row == ss->end_row){
		return cell_name (ss->start_col, ss->start_row);
	} else {
		snprintf (buffer, sizeof (buffer), "%dLx%dC",
			  ss->end_row - ss->start_row + 1,
			  ss->end_col - ss->start_col + 1);
		return buffer;
	}
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
	GList *l;

	g_return_if_fail (sheet != NULL);
	g_return_if_fail (IS_SHEET (sheet));

	ss = g_new0 (SheetSelection, 1);

	ss->base_col  = base_col;
	ss->base_row  = base_row;

	ss->start_col = start_col;
	ss->end_col   = end_col;
	ss->start_row = start_row;
	ss->end_row   = end_row;

	sheet->selections = g_list_prepend (sheet->selections, ss);

	sheet_accept_pending_input (sheet);
	sheet_load_cell_val (sheet);

	for (l = sheet->sheet_views; l; l = l->next){
		GnumericSheet *gsheet = GNUMERIC_SHEET_VIEW (l->data);

		gnumeric_sheet_set_selection (gsheet, ss);
	}
	sheet_redraw_selection (sheet, ss);

	sheet_redraw_cols (sheet);
	sheet_redraw_rows (sheet);

	sheet_selection_changed_hook (sheet);
}

/**
 * If returns true selection is just one range.
 * If returns false, range data: indeterminate
 **/
int
sheet_selection_first_range (Sheet *sheet,
			      int *base_col,  int *base_row,
			      int *start_col, int *start_row,
			      int *end_col,   int *end_row)
{
	SheetSelection *ss;
	GList *l;

	g_return_val_if_fail (sheet != NULL, 0);
	g_return_val_if_fail (IS_SHEET (sheet), 0);

	if (!sheet->selections)
		return 0;

	l = g_list_first (sheet->selections);
	if (!l || !l->data)
		return 0;

	ss = l->data;
	*base_col = ss->base_col;
	*base_row = ss->base_row;
	*start_col = ss->start_col;
	*start_row = ss->start_row;
	*end_col = ss->end_col;
	*end_row = ss->end_row;

	if ((l = g_list_next (l)))
		return 0;
	return 1;
}

void
sheet_selection_append (Sheet *sheet, int col, int row)
{
	sheet_selection_append_range (sheet, col, row, col, row, col, row);
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
	SheetSelection *ss, old_selection;
	GList *l;

	g_return_if_fail (sheet != NULL);
	g_return_if_fail (IS_SHEET (sheet));

	g_assert (sheet->selections);

	ss = (SheetSelection *) sheet->selections->data;

	old_selection = *ss;

	if (col < ss->base_col){
		ss->start_col = col;
		ss->end_col   = ss->base_col;
	} else {
		ss->start_col = ss->base_col;
		ss->end_col   = col;
	}

	if (row < ss->base_row){
		ss->end_row   = ss->base_row;
		ss->start_row = row;
	} else {
		ss->end_row   = row;
		ss->start_row = ss->base_row;
	}

	for (l = sheet->sheet_views; l; l = l->next){
		GnumericSheet *gsheet = GNUMERIC_SHEET_VIEW (l->data);

		gnumeric_sheet_set_selection (gsheet, ss);
	}

	sheet_selection_changed_hook (sheet);

	sheet_redraw_selection (sheet, &old_selection);
	sheet_redraw_selection (sheet, ss);

	if (ss->start_col != old_selection.start_col ||
	    ss->end_col != old_selection.end_col ||
	    ((ss->start_row == 0 && ss->end_row == SHEET_MAX_ROWS-1) ^
	     (old_selection.start_row == 0 &&
	      old_selection.end_row == SHEET_MAX_ROWS-1)))
		sheet_redraw_cols (sheet);

	if (ss->start_row != old_selection.start_row ||
	    ss->end_row != old_selection.end_row ||
	    ((ss->start_col == 0 && ss->end_col == SHEET_MAX_COLS-1) ^
	     (old_selection.start_col == 0 &&
	      old_selection.end_col == SHEET_MAX_COLS-1)))
		sheet_redraw_rows (sheet);
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

		if (ss->start_col == 0 &&
		    ss->start_row == 0 &&
		    ss->end_col == SHEET_MAX_COLS-1 &&
		    ss->end_row == SHEET_MAX_ROWS-1)
			return TRUE;
	}
	return FALSE;
}
 
static void
sheet_selection_change (Sheet *sheet, SheetSelection *old, SheetSelection *new)
{
	GList *l;

	if (sheet_selection_equal (old, new))
		return;

	sheet_accept_pending_input (sheet);
	sheet_redraw_selection (sheet, old);
	sheet_redraw_selection (sheet, new);
	sheet_selection_changed_hook (sheet);

	for (l = sheet->sheet_views; l; l = l->next){
		GnumericSheet *gsheet = GNUMERIC_SHEET_VIEW (l->data);

		gnumeric_sheet_set_selection (gsheet, new);
	}

	if (new->start_col != old->start_col ||
	    new->end_col != old->end_col ||
	    ((new->start_row == 0 && new->end_row == SHEET_MAX_ROWS-1) ^
	     (old->start_row == 0 &&
	      old->end_row == SHEET_MAX_ROWS-1)))
		sheet_redraw_cols (sheet);

	if (new->start_row != old->start_row ||
	    new->end_row != old->end_row ||
	    ((new->start_col == 0 && new->end_col == SHEET_MAX_COLS-1) ^
	     (old->start_col == 0 &&
	      old->end_col == SHEET_MAX_COLS-1)))
		sheet_redraw_rows (sheet);
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

	if (ss->base_col < ss->end_col)
		ss->end_col =
		    sheet_find_boundary_horizontal (sheet,
						    ss->end_col, ss->end_row,
						    n, jump_to_boundaries);
	else if (ss->base_col > ss->start_col || n < 0)
		ss->start_col =
		    sheet_find_boundary_horizontal (sheet,
						    ss->start_col, ss->start_row,
						    n, jump_to_boundaries);
	else
		ss->end_col =
		    sheet_find_boundary_horizontal (sheet,
						    ss->end_col,  ss->end_row,
						    n, jump_to_boundaries);

	if (ss->end_col < ss->start_col) {
		int const tmp = ss->start_col;
		ss->start_col = ss->end_col;
		ss->end_col = tmp;
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

	if (ss->base_row < ss->end_row)
		ss->end_row =
		    sheet_find_boundary_vertical (sheet,
						  ss->end_col, ss->end_row,
						  n, jump_to_boundaries);
	else if (ss->base_row > ss->start_row || n < 0)
		ss->start_row =
		    sheet_find_boundary_vertical (sheet,
						  ss->start_col, ss->start_row,
						  n, jump_to_boundaries);
	else
		ss->end_row =
		    sheet_find_boundary_vertical (sheet,
						  ss->end_col, ss->end_row,
						  n, jump_to_boundaries);

	if (ss->end_row < ss->start_row) {
		int const tmp = ss->start_row;
		ss->start_row = ss->end_row;
		ss->end_row = tmp;
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

	ss->start_row = start_row;
	ss->end_row = end_row;
	ss->start_col = start_col;
	ss->end_col = end_col;

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

	sheet->walk_info.current = NULL;

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
		SheetSelection *ss = list->data;

		if ((ss->start_col <= col) && (col <= ss->end_col) &&
		    (ss->start_row <= row) && (row <= ss->end_row)){
			return 1;
		}
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

CellList *
sheet_selection_to_list (Sheet *sheet)
{
	GList *selections;
	CellList *list;

	g_return_val_if_fail (sheet != NULL, NULL);
	g_return_val_if_fail (IS_SHEET (sheet), NULL);
	g_return_val_if_fail (sheet->selections, NULL);

	list = NULL;
	for (selections = sheet->selections; selections; selections = selections->next){
		SheetSelection *ss = selections->data;

		sheet_cell_foreach_range (
			sheet, TRUE,
			ss->start_col, ss->start_row,
			ss->end_col, ss->end_row,
			assemble_cell_list, &list);
	}

	return list;
}

static void
reference_append (GString *result_str, int col, int row)
{
	char *row_string = g_strdup_printf ("%d", row);

	g_string_append_c (result_str, '$');
	g_string_append (result_str, col_name (col));
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

		if ((ss->start_col != ss->end_col) ||
		    (ss->start_row != ss->end_row)){
			reference_append (result_str, ss->start_col, ss->end_row);
			g_string_append_c (result_str, ':');
			reference_append (result_str, ss->end_col, ss->end_row);
		} else
			reference_append (result_str, ss->start_col, ss->start_row);
	}

	result = result_str->str;
	g_string_free (result_str, FALSE);
	return result;
}

void
sheet_selection_clear (Sheet *sheet)
{
	GList *l;

	g_return_if_fail (sheet != NULL);
	g_return_if_fail (IS_SHEET (sheet));

	for (l = sheet->selections; l; l = l->next){
		SheetSelection *ss = l->data;

		sheet_clear_region (sheet,
				    ss->start_col, ss->start_row,
				    ss->end_col, ss->end_row);
	}
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
	GList *l;

	g_return_if_fail (sheet != NULL);
	g_return_if_fail (IS_SHEET (sheet));

	for (l = sheet->selections; l; l = l->next){
		SheetSelection *ss = l->data;

		sheet_clear_region_content (sheet,
					    ss->start_col, ss->start_row,
					    ss->end_col, ss->end_row);
	}
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
	GList *l;

	g_return_if_fail (sheet != NULL);
	g_return_if_fail (IS_SHEET (sheet));

	for (l = sheet->selections; l; l = l->next){
		SheetSelection *ss = l->data;

		sheet_clear_region_comments (sheet,
					     ss->start_col, ss->start_row,
					     ss->end_col, ss->end_row);
	}
}

void
sheet_selection_clear_formats (Sheet *sheet)
{
	GList *l;

	g_return_if_fail (sheet != NULL);
	g_return_if_fail (IS_SHEET (sheet));

	for (l = sheet->selections; l; l = l->next){
		SheetSelection *ss = l->data;

		sheet_clear_region_formats (sheet,
					    ss->start_col, ss->start_row,
					    ss->end_col, ss->end_row);
	}
}

gboolean
sheet_verify_selection_simple (Sheet *sheet, const char *command_name)
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

gboolean
sheet_selection_copy (Sheet *sheet)
{
	SheetSelection *ss;
	g_return_val_if_fail (sheet != NULL, FALSE);
	g_return_val_if_fail (IS_SHEET (sheet), FALSE);
	g_return_val_if_fail (sheet->selections, FALSE);

	if (!sheet_verify_selection_simple (sheet, _("copy")))
		return FALSE;

	ss = sheet->selections->data;

	if (sheet->workbook->clipboard_contents)
		clipboard_release (sheet->workbook->clipboard_contents);

	sheet->workbook->clipboard_contents = clipboard_copy_cell_range (
		sheet,
		ss->start_col, ss->start_row,
		ss->end_col, ss->end_row, FALSE);

	return TRUE;
}

gboolean
sheet_selection_cut (Sheet *sheet)
{
	SheetSelection *ss;

	g_return_val_if_fail (sheet != NULL, FALSE);
	g_return_val_if_fail (IS_SHEET (sheet), FALSE);
	g_return_val_if_fail (sheet->selections, FALSE);

	if (!sheet_verify_selection_simple (sheet, _("cut")))
		return FALSE;

	ss = sheet->selections->data;

	if (sheet->workbook->clipboard_contents)
		clipboard_release (sheet->workbook->clipboard_contents);

	sheet->workbook->clipboard_contents = clipboard_copy_cell_range (
		sheet,
		ss->start_col, ss->start_row,
		ss->end_col, ss->end_row, TRUE);

	sheet_clear_region (sheet, ss->start_col, ss->start_row, ss->end_col, ss->end_row);

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
sheet_selection_paste (Sheet *sheet, int dest_col, int dest_row, int paste_flags, guint32 time)
{
	CellRegion *content;

	g_return_if_fail (sheet != NULL);
	g_return_if_fail (IS_SHEET (sheet));
	g_return_if_fail (sheet->selections);

	content = find_workbook_with_clipboard (sheet);

	if (content)
		if (!sheet_verify_selection_simple (sheet, _("paste")))
			return;

	clipboard_paste_region (content, sheet, dest_col, dest_row, paste_flags, time);
}

