/*
 * colrow.c: Utilities for Rows and Columns
 *
 * Author:
 *    Miguel de Icaza (miguel@gnu.org).
 *    Jody Goldberg (jgoldberg@home.org)
 *
 * (C) 1998, 1999, 2000 Miguel de Icaza, Jody Goldberg
 */
#include <config.h>
#include "colrow.h"
#include "selection.h"

struct row_col_visiblity
{
	gboolean is_col, visible;
	ColRowVisList elements;
};

struct pair_int
{
	int index, count;
};

static void
cb_row_col_visibility (Sheet *sheet,
		       int start_col, int start_row,
		       int end_col,   int end_row,
		       void *closure)
{
	struct row_col_visiblity * const dat = closure;
	gboolean const visible = dat->visible;
	ColRowInfo * (*fetch) (Sheet *sheet, int pos);
	int i, j, end;

	if (dat->is_col) {
		i = start_col;
		end = end_col;
		fetch = &sheet_col_fetch;
	} else
	{
		i = start_row;
		end = end_row;
		fetch = &sheet_row_fetch;
	}

	/* Find the begining of a segment that will be toggled */
	while (i <= end) {
		ColRowInfo *cri = (*fetch) (sheet, i++);
		if (visible ^ cri->visible) {
			struct pair_int *res = g_new(struct pair_int, 1);

			/* Find the end */
			for (j = i; j <= end ;) {
				ColRowInfo * cri = (*fetch) (sheet, j++);
				if (visible ^ cri->visible)
					break;
			}
			res->index = i - 1;
			res->count = j - i + 1;

#if 0
			printf ("%d %d\n", res->index, res->count);
#endif
			dat->elements = g_slist_prepend (dat->elements, res);
			i = j;
		}
	}
}

/*
 * col_row_get_visiblity_toggle :
 * @sheet : The sheet whose selection we are interested in.
 * @is_col: A flag indicating whether this it is a column or a row.
 * @is_visible: Should we unhide or hide the cols/rows.
 *
 * Searches the selection list and generates a list of index,count
 * pairs of row/col ranges that need to be hidden or unhiden.
 */
ColRowVisList
col_row_get_visiblity_toggle (Sheet *sheet, gboolean const is_col,
			      gboolean const visible)
{
	struct row_col_visiblity closure;
	closure.is_col = is_col;
	closure.visible = visible;
	closure.elements = NULL;

	selection_apply (sheet, &cb_row_col_visibility, FALSE, &closure);

	return closure.elements;
}

ColRowVisList
col_row_vis_list_destroy (ColRowVisList list)
{
	while (list != NULL) {
		g_free (list->data);
		list = g_slist_remove (list, list->data);
	}
	return NULL;
}

/*
 * col_row_set_visiblity :
 *
 * This is the high level command that is wrapped by undo and redo.
 * It should not be called by other commands.
 */
void
col_row_set_visiblity (Sheet *sheet, gboolean const is_col,
		       gboolean const visible, ColRowVisList list)
{
	/* Trivial optimization */
	if (list == NULL)
		return;

	for (; list != NULL ; list = list->next) {
		struct pair_int *info = list->data;
		sheet_row_col_visible (sheet, is_col, visible,
				       info->index, info->count);
	}

	sheet_redraw_all (sheet);
	sheet_redraw_headers (sheet, TRUE, TRUE, NULL);
}
