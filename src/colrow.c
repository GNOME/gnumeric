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
 * col_row_equal :
 * @a : ColRowInfo #1
 * @b : ColRowInfo #2
 *
 * Returns true if the infos are equivalent.
 */
gboolean
col_row_equal (ColRowInfo const *a, ColRowInfo const *b)
{
	if (a == NULL || b == NULL)
		return FALSE;

	return
	    a->size_pts  == b->size_pts &&
	    a->margin_a  == b->margin_a &&
	    a->margin_b  == b->margin_b &&
	    a->hard_size == b->hard_size &&
	    a->visible   == b->visible;
}

void
col_row_copy (ColRowInfo *dst, ColRowInfo const *src)
{
	dst->margin_a    = src->margin_a;
	dst->margin_b    = src->margin_b;
	dst->size_pts    = src->size_pts;
	dst->size_pixels = src->size_pixels;
	dst->hard_size   = src->hard_size;
	dst->visible     = src->visible;
}

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
		ColRowSegment const *segment = COLROW_GET_SEGMENT (infos, i);
		int sub = COLROW_SUB_INDEX(i);

		i += COLROW_SEGMENT_SIZE - sub;
		if (segment == NULL)
			continue;

		for (; sub < COLROW_SEGMENT_SIZE; ++sub) {
			ColRowInfo *info = segment->info[sub];
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
col_row_size_list_destroy (ColRowSizeList *list)
{
	ColRowSizeList *ptr;
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
	g_list_free (list);
	return NULL;
}

static gint
colrow_index_compare (ColRowIndex const * a, ColRowIndex const * b)
{
	return a->first - b->first;
}

/**
 * col_row_get_index_list :
 *
 * Build an ordered list of pairs doing intelligent merging
 * of overlapping regions.
 */
ColRowIndexList *
col_row_get_index_list (int first, int last, ColRowIndexList *list)
{
	ColRowIndex *tmp, *prev;
	GList *ptr;

	tmp = g_new (ColRowIndex, 1);
	tmp->first = first;
	tmp->last = last;

	list = g_list_insert_sorted (list, tmp,
				     (GCompareFunc)&colrow_index_compare);

	prev = list->data;
	for (ptr = list->next ; ptr != NULL ; ) {
		tmp = ptr->data;

		/* at the end of existing segment or contained */
		if (prev->last+1 >= tmp->first) {
			GList *next = ptr->next;
			if (prev->last < tmp->last)
				prev->last = tmp->last;
			list = g_list_remove_link (list, ptr);
			ptr = next;
		} else {
			ptr = ptr->next;
			prev = tmp;
		}
	}
	return list;
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

struct resize_closure
{
	Sheet *sheet;
	int new_size;
	gboolean is_cols;
};

static gboolean
cb_set_colrow_size (ColRowInfo *info, void *userdata)
{
	struct resize_closure const *c = userdata;
	if (c->is_cols)
		sheet_col_set_size_pixels (c->sheet, info->pos, c->new_size, TRUE);
	else
		sheet_row_set_size_pixels (c->sheet, info->pos, c->new_size, TRUE);
	return FALSE;
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

		/* FIXME :
		 * If we are changing the size of more than half of the rows/col to
		 * something specific (not autosize) we should change the default
		 * row/col size instead.  However, it is unclear how to handle
		 * hard sizing.
		 *
		 * we need better management of rows/cols.  Currently if they are all
		 * defined calculation speed grinds to a halt.
		 */
		if (new_size >= 0 && index->first == 0 &&
		    index->last == ((is_cols ?SHEET_MAX_COLS:SHEET_MAX_ROWS)-1)) {
			struct resize_closure closure;
			double *def = g_new(double, 1);

			res = g_slist_prepend (res, def);

			if (is_cols) {
				*def = sheet_col_get_default_size_pts (sheet);
				sheet_col_set_default_size_pixels (sheet, new_size);
			} else {
				*def = sheet_row_get_default_size_pts (sheet);
				sheet_row_set_default_size_pixels (sheet, new_size);
			}
			closure.sheet = sheet;
			closure.new_size = new_size;
			closure.is_cols = is_cols;
			col_row_foreach (&sheet->cols, 0, SHEET_MAX_COLS,
					 &cb_set_colrow_size, &closure);
			return res;
		}

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
			ColRowSegment *segment = COLROW_GET_SEGMENT(infos, i);
			int const sub = COLROW_SUB_INDEX (i);
			ColRowInfo *cri = NULL;
			if (segment != NULL) {
				cri = segment->info[sub];
				if (cri != NULL) {
					segment->info[sub] = NULL;
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
		if (sheet->priv->reposition_objects.col > first)
			sheet->priv->reposition_objects.col = first;
	} else {
		if (sheet->priv->reposition_objects.row > first)
			sheet->priv->reposition_objects.row = first;
	}

	g_free (sizes);
}

void
col_row_restore_sizes_group (Sheet *sheet, gboolean const is_cols,
			     ColRowIndexList *selection,
			     ColRowSizeList *saved_sizes,
			     int old_size)
{
	ColRowSizeList *ptr = saved_sizes;
	while (selection != NULL && ptr != NULL) {
		ColRowIndex *index = selection->data;

		if (old_size >= 0 && index->first == 0 &&
		    index->last == ((is_cols ?SHEET_MAX_COLS:SHEET_MAX_ROWS)-1)) {
			double *old_def = ptr->data;

			if (is_cols)
				sheet_col_set_default_size_pts (sheet, *old_def);
			else
				sheet_row_set_default_size_pts (sheet, *old_def);

			ptr = ptr->next;
			g_free (old_def);
		}

		col_row_restore_sizes (sheet, is_cols,
				       index->first, index->last,
				       ptr->data);

		selection = selection->next;
		ptr = ptr->next;
	}
	g_slist_free (saved_sizes);
}

static gboolean
cb_autofit_height (ColRowInfo *info, void *sheet)
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
			 &cb_autofit_height, sheet);
}
/*****************************************************************************/

struct row_col_visiblity
{
	gboolean is_cols, visible;
	ColRowVisList *elements;
};

static void
cb_row_col_visibility (Sheet *sheet, Range const *r,
		       void *closure)
{
	struct row_col_visiblity * const dat = closure;
	gboolean const visible = dat->visible;
	ColRowInfo * (*fetch) (Sheet *sheet, int pos);
	int i, j, end;

	if (dat->is_cols) {
		i = r->start.col;
		end = r->end.col;
		fetch = &sheet_col_fetch;
	} else {
		i = r->start.row;
		end = r->end.row;
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
 * col_row_set_visibility_list :
 *
 * This is the high level command that is wrapped by undo and redo.
 * It should not be called by other commands.
 */
void
col_row_set_visibility_list (Sheet *sheet, gboolean const is_cols,
			     gboolean const visible, ColRowVisList *list)
{
	/* Trivial optimization */
	if (list == NULL)
		return;

	for (; list != NULL ; list = list->next) {
		int min_col, max_col;
		ColRowIndex *info = list->data;
		col_row_set_visibility (sheet, is_cols, visible,
					info->first, info->last);
		sheet_regen_adjacent_spans (sheet,
					    info->first, 0,
					    info->last, SHEET_MAX_ROWS-1,
					    &min_col, &max_col);
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

