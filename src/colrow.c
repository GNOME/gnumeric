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
#include "sheet.h"
#include "sheet-private.h"

/**
 * col_row_foreach:
 * @sheet	the sheet
 * @infos	The Row or Column collection.
 * @start	start position (inclusive)
 * @end		stop column (inclusive)
 * @callback	A callback function which should return TRUE to stop
 *              the iteration.
 * @user_data	A bagage pointer.
 *
 * Iterates through the existing rows or columns within the range supplied.
 * Currently only support left -> right iteration.  If a callback returns
 * TRUE iteration stops.
 */
gboolean
col_row_foreach (ColRowCollection const *infos, int first, int last,
		 col_row_callback callback, void *user_data)
{
	int i;

	/* TODO : Do we need to support right -> left as an option */
	if (last > infos->max_used)
		last = infos->max_used;

	i = first;
	while (i <= last) {
		ColRowInfo * const * segment = COLROW_GET_SEGMENT (infos, i);
		int sub = COLROW_SUB_INDEX(i);

		i += COLROW_SEGMENT_SIZE - sub;
		if (segment == NULL)
			continue;

		for (; sub < COLROW_SEGMENT_SIZE; ++sub) {
			ColRowInfo * info = segment[sub];
			if (info != NULL && (*callback)(info, user_data))
				return TRUE;
		}
	}
	return FALSE;
}

/*****************************************************************************/

typedef struct _ColRowIndex
{
	int first, last;
} ColRowIndex;

ColRowSizeList *
col_row_size_list_destroy (ColRowIndexList *list)
{
	ColRowIndexList *ptr;
	for (ptr = list; ptr != NULL ; ptr = ptr->next)
		g_free (ptr->data);
	g_slist_free (list);
	return NULL;
}
ColRowIndexList *
col_row_index_list_destroy (ColRowIndexList *list)
{
	ColRowIndexList *ptr;
	for (ptr = list; ptr != NULL ; ptr = ptr->next)
		g_free (ptr->data);
	g_slist_free (list);
	return NULL;
}

ColRowIndexList *
col_row_get_index_list (int first, int last, ColRowIndexList *list)
{
	ColRowIndex *tmp;
	if (list != NULL) {
		tmp = list->data;

		if (tmp->last == (first-1)) {
			tmp->last = last;
			return list;
		}
	}

	tmp = g_new (ColRowIndex, 1);
	tmp->first = first;
	tmp->last = last;

	return g_slist_prepend (list, tmp);
}

double *
col_row_save_sizes (Sheet *sheet, gboolean const is_cols, int first, int last)
{
	int i;
	double *res = NULL;

	g_return_val_if_fail (sheet != NULL, NULL);
	g_return_val_if_fail (first <= last, NULL);

	res = g_new (double, last-first+1);

	for (i = first ; i <= last ; ++i) {
		ColRowInfo *info = is_cols
		    ? sheet_col_get_info (sheet, i)
		    : sheet_row_get_info (sheet, i);

		g_return_val_if_fail (info != NULL, NULL); /* be anal, and leak */

		if (info->pos != -1) {
			res[i-first] = info->size_pts;
			if (info->hard_size)
				res[i-first] *= -1.;
		} else
			res[i-first] = 0.;
	}
	return res;
}

ColRowSizeList *
col_row_set_sizes (Sheet *sheet, gboolean const is_cols,
		   ColRowIndexList *src, int new_size)
{
	int i;
	ColRowSizeList *res = NULL;
	ColRowIndexList *ptr;

	for (ptr = src; ptr != NULL ; ptr = ptr->next) {
		ColRowIndex *index = ptr->data;
		res = g_slist_prepend (res,
			col_row_save_sizes (sheet, is_cols, index->first, index->last));

		for (i = index->first ; i <= index->last ; ++i) {
			int tmp = new_size;
			if (tmp < 0)
				tmp = (is_cols)
					? sheet_col_size_fit_pixels (sheet, i)
					: sheet_row_size_fit_pixels (sheet, i);

			if (tmp <= 0)
				continue;

			if (is_cols)
				sheet_col_set_size_pixels (sheet, i, tmp, TRUE);
			else
				sheet_row_set_size_pixels (sheet, i, tmp, TRUE);
		}
	}

	return res;
}

/*
 * NOTE : this is a low level routine it does not redraw or
 *        reposition objects
 */
void
col_row_restore_sizes (Sheet *sheet, gboolean const is_cols,
		       int first, int last, double *sizes)
{
	int i;

	g_return_if_fail (sizes != NULL);
	g_return_if_fail (sheet != NULL);
	g_return_if_fail (first <= last);

	for (i = first ; i <= last ; ++i) {
		gboolean hard_size = FALSE;

		/* Reset to the default */
		if (sizes[i-first] == 0.) {
			ColRowCollection *infos = is_cols ? &(sheet->cols) : &(sheet->rows);
			ColRowInfo ***segment =
				(ColRowInfo ***)&COLROW_GET_SEGMENT(infos, i);
			int const sub = COLROW_SUB_INDEX (i);
			ColRowInfo *cri = NULL;
			if (*segment != NULL) {
				cri = (*segment)[sub];
				if (cri != NULL) {
					(*segment)[sub] = NULL;
					g_free (cri);
				}
			}
		} else {
			if (sizes[i-first] < 0.) {
				hard_size = TRUE;
				sizes[i-first] *= -1.;
			}
			if (is_cols)
				sheet_col_set_size_pts (sheet, i, sizes[i-first], hard_size);
			else
				sheet_row_set_size_pts (sheet, i, sizes[i-first], hard_size);
		}
	}

	/* Notify sheet of pending update */
	sheet->priv->recompute_visibility = TRUE;
	if (is_cols) {
		sheet->priv->recompute_spans = TRUE;
		if (sheet->priv->reposition_col_comment > first)
			sheet->priv->reposition_col_comment = first;
	} else {
		if (sheet->priv->reposition_row_comment > first)
			sheet->priv->reposition_row_comment = first;
	}

	g_free (sizes);
}

void
col_row_restore_sizes_group (Sheet *sheet, gboolean const is_cols,
			     ColRowIndexList *selection,
			     ColRowSizeList *saved_sizes)
{
	ColRowSizeList *ptr = saved_sizes;
	while (selection != NULL && ptr != NULL) {
		ColRowIndex *index = selection->data;

		col_row_restore_sizes (sheet, is_cols,
				       index->first, index->last,
				       ptr->data);

		selection = selection->next;
		ptr = saved_sizes->next;
	}
	g_slist_free (saved_sizes);
}

static gboolean
cb_set_row_height (ColRowInfo *info, void *sheet)
{
	/* If the size was not set by the user then auto resize */
	if (!info->hard_size) {
		int const new_size = sheet_row_size_fit_pixels (sheet, info->pos);
		if (new_size > 0)
			sheet_row_set_size_pixels (sheet, info->pos, new_size, FALSE);
	}
	return FALSE;
}

/**
 * rows_height_update
 * @sheet:  The sheet,
 * @range:  The range whose rows should be resized.
 *
 * Use this function having changed the font size to auto
 * resize the row heights to make the text fit nicely.
 **/
void
rows_height_update (Sheet *sheet, Range const * range)
{
	/* FIXME : this needs to check font sizes and contents rather than
	 * just contents.  Empty cells will cause resize also
	 */
	col_row_foreach (&sheet->rows, range->start.row, range->end.row,
			 &cb_set_row_height, sheet);
}
/*****************************************************************************/

struct row_col_visiblity
{
	gboolean is_cols, visible;
	ColRowVisList *elements;
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

	if (dat->is_cols) {
		i = start_col;
		end = end_col;
		fetch = &sheet_col_fetch;
	} else {
		i = start_row;
		end = end_row;
		fetch = &sheet_row_fetch;
	}

	/* Find the begining of a segment that will be toggled */
	for (;i <= end ; ++i) {
		ColRowInfo *cri = (*fetch) (sheet, i);
		if ((visible == 0) != (cri->visible == 0)) {
			ColRowIndex *res = g_new(ColRowIndex, 1);

			/* Find the end */
			for (j = i+1; j <= end ; ++j) {
				ColRowInfo * cri = (*fetch) (sheet, j);
				if ((visible == 0) == (cri->visible == 0))
					break;
			}
			res->first = i;
			res->last = j - 1;

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
 * @is_cols: A flag indicating whether this it is a column or a row.
 * @is_visible: Should we unhide or hide the cols/rows.
 *
 * Searches the selection list and generates a list of index,count
 * pairs of row/col ranges that need to be hidden or unhiden.
 */
ColRowVisList *
col_row_get_visiblity_toggle (Sheet *sheet, gboolean const is_cols,
			      gboolean const visible)
{
	struct row_col_visiblity closure;
	closure.is_cols = is_cols;
	closure.visible = visible;
	closure.elements = NULL;

	selection_apply (sheet, &cb_row_col_visibility, FALSE, &closure);

	return closure.elements;
}

ColRowVisList *
col_row_vis_list_destroy (ColRowVisList *list)
{
	ColRowVisList *ptr;
	for (ptr = list; ptr != NULL ; ptr = ptr->next)
		g_free (ptr->data);
	g_slist_free (list);
	return NULL;
}

/*
 * col_row_set_visiblity :
 *
 * This is the high level command that is wrapped by undo and redo.
 * It should not be called by other commands.
 */
void
col_row_set_visiblity (Sheet *sheet, gboolean const is_cols,
		       gboolean const visible, ColRowVisList *list)
{
	/* Trivial optimization */
	if (list == NULL)
		return;

	for (; list != NULL ; list = list->next) {
		ColRowIndex *info = list->data;
		col_row_set_visibility (sheet, is_cols, visible,
					info->first, info->last);
	}

	sheet_redraw_all (sheet);
	sheet_redraw_headers (sheet, TRUE, TRUE, NULL);
}

/**
 * col_row_set_visibility:
 * @sheet	: the sheet
 * @is_cols	: Are we dealing with rows or columns.
 * @visible	: Make things visible or invisible.
 * @first	: The index of the first row/col (inclusive)
 * @last	: The index of the last row/col (inclusive)
 *
 * Change the visibility of the selected range of contiguous rows/cols.
 */
void
col_row_set_visibility (Sheet *sheet, gboolean const is_cols, gboolean const visible,
			int first, int last)
{
	int i;
	g_return_if_fail (sheet != NULL);

	for (i = first; i <= last ; ++i) {
		ColRowInfo * const cri = is_cols
		    ? sheet_col_fetch (sheet, i)
		    : sheet_row_fetch (sheet, i);

		if ((visible == 0) != (cri->visible == 0)) {
			cri->visible = visible;
			sheet->priv->recompute_visibility = TRUE;
		}
	}
}

