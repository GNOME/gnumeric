/* vim: set sw=8: */

/*
 * colrow.c: Utilities for Rows and Columns
 *
 * Copyright (C) 1999, 2000, 2001 Jody Goldberg (jgoldberg@home.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
 * USA
 */
#include <config.h>
#include "colrow.h"
#include "parse-util.h"
#include "selection.h"
#include "sheet.h"
#include "sheet-private.h"

/**
 * colrow_equal :
 * @a : ColRowInfo #1
 * @b : ColRowInfo #2
 *
 * Returns true if the infos are equivalent.
 */
gboolean
colrow_equal (ColRowInfo const *a, ColRowInfo const *b)
{
	if (a == NULL || b == NULL)
		return FALSE;

	return  a->size_pts	 == b->size_pts &&
		a->margin_a	 == b->margin_a &&
		a->margin_b	 == b->margin_b &&
		a->outline_level == b->outline_level &&
		a->is_collapsed	 == b->is_collapsed &&
		a->hard_size	 == b->hard_size &&
		a->visible	 == b->visible;
}

void
colrow_copy (ColRowInfo *dst, ColRowInfo const *src)
{
	dst->size_pts      = src->size_pts;
	dst->size_pixels   = src->size_pixels;
	dst->margin_a	   = src->margin_a;
	dst->margin_b 	   = src->margin_b;
	dst->outline_level = src->outline_level;
	dst->is_collapsed  = src->is_collapsed;
	dst->hard_size     = src->hard_size;
	dst->visible       = src->visible;
}

/**
 * colrow_foreach:
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
colrow_foreach (ColRowCollection const *infos, int first, int last,
		ColRowHandler callback, void *user_data)
{
	int i;

	/* TODO : Do we need to support right -> left as an option */
	if (last > infos->max_used)
		last = infos->max_used;

	i = first;
	while (i <= last) {
		ColRowSegment const *segment = COLROW_GET_SEGMENT (infos, i);
		int sub = COLROW_SUB_INDEX(i);
		int inner_last;

		inner_last = (COLROW_SEGMENT_INDEX (last) == COLROW_SEGMENT_INDEX (i))
			? COLROW_SUB_INDEX (last)+1 : COLROW_SEGMENT_SIZE;
		i += COLROW_SEGMENT_SIZE - sub;
		if (segment == NULL)
			continue;

		for (; sub < inner_last; ++sub) {
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

typedef struct {
	int    length;
	double size;
} SavedSize;

ColRowRLESizeList *
colrow_rle_size_list_destroy (ColRowSizeList *list)
{
	GSList *ptr;
	for (ptr = list; ptr != NULL; ptr = ptr->next)
		g_free (ptr->data);
	g_slist_free (list);
	return NULL;
}

ColRowSizeList *
colrow_size_list_destroy (ColRowSizeList *list)
{
	ColRowSizeList *ptr;
	for (ptr = list; ptr != NULL ; ptr = ptr->next)
		colrow_rle_size_list_destroy ((ColRowSizeList *) ptr->data);
	g_slist_free (list);
	return NULL;
}

ColRowIndexList *
colrow_index_list_destroy (ColRowIndexList *list)
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

static void
colrow_string_build (GString *str, int index, gboolean const is_cols)
{	
	if (is_cols)
		g_string_append (str, col_name (index));
	else {
		char *s = g_strdup_printf ("%d", index + 1);
			
		g_string_append (str, s);
		g_free (s);
	}
}

/*
 * colrow_index_list_to_string: Convert an index list into a string.
 *                              The result must be freed by the caller.
 *                              It will be something like : A-B, F-G
 *
 * @list: The list
 * @is_cols: Column index list or row index list?
 * @is_single: If non-null this will be set to TRUE if there's only a single col/row involved.
 */
GString *
colrow_index_list_to_string (ColRowIndexList *list, gboolean const is_cols, gboolean *is_single)
{
	ColRowIndexList *ptr;
	GString *result;
	gboolean single = TRUE;

	g_return_val_if_fail (list != NULL, NULL);

	result = g_string_new ("");
	for (ptr = list; ptr != NULL; ptr = ptr->next) {
		ColRowIndex *index = ptr->data;
		
		colrow_string_build (result, index->first, is_cols);
		if (index->last != index->first) {
			g_string_append (result, "-");
			colrow_string_build (result, index->last, is_cols);
			single = FALSE;
		}
		
		if (ptr->next) {
			g_string_append (result, ", ");
			single = FALSE;
		}
	}
	
	if (is_single)
		*is_single = single;
		
	return result;
}

/**
 * colrow_get_index_list :
 *
 * Build an ordered list of pairs doing intelligent merging
 * of overlapping regions.
 */
ColRowIndexList *
colrow_get_index_list (int first, int last, ColRowIndexList *list)
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

ColRowRLESizeList *
colrow_save_sizes (Sheet *sheet, gboolean const is_cols, int first, int last)
{
	int                i;
	ColRowRLESizeList *list = NULL;
	SavedSize         *ss;
	double             size, run_size = 0.;
	int                run_length;

	g_return_val_if_fail (sheet != NULL, NULL);
	g_return_val_if_fail (first <= last, NULL);

	for (i = first; i <= last; ++i) {
		ColRowInfo *info = is_cols
		    ? sheet_col_get_info (sheet, i)
		    : sheet_row_get_info (sheet, i);

		g_return_val_if_fail (info != NULL, NULL); /* be anal, and leak */
		
		if (info->pos != -1) {
			size = info->size_pts;
			if (info->hard_size)
				size *= -1.;
		} else
			size = 0.;

		/* Initialize the run_size in the first loop */
		if (i == first) {
			run_size   = size;
			run_length = 1;
			continue;
		}
			
		/*
		 * If the size does not equal the size of
		 * the current run then the current run has ended
		 */
		if (size != run_size) {
			ss         = g_new0 (SavedSize, 1);
			ss->length = run_length;
			ss->size   = run_size;
			list = g_slist_prepend (list, ss);

			run_size   = size;
			run_length = 1;
		} else
			++run_length;
	}

	/* Store the final run */
	ss         = g_new0 (SavedSize, 1);
	ss->length = run_length;
	ss->size   = run_size;
	list = g_slist_prepend (list, ss);

	list = g_slist_reverse (list);
	return list;
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
	if (info->visible) {
		struct resize_closure const *c = userdata;

		if (c->is_cols)
			sheet_col_set_size_pixels (c->sheet, info->pos,
						   c->new_size, TRUE);
		else
			sheet_row_set_size_pixels (c->sheet, info->pos,
						   c->new_size, TRUE);
	}
	return FALSE;
}

ColRowSizeList *
colrow_set_sizes (Sheet *sheet, gboolean const is_cols,
		  ColRowIndexList *src, int new_size)
{
	int i;
	ColRowSizeList *res = NULL;
	ColRowIndexList *ptr;

	for (ptr = src; ptr != NULL ; ptr = ptr->next) {
		ColRowIndex *index = ptr->data;
		res = g_slist_prepend (res,
			colrow_save_sizes (sheet, is_cols, index->first, index->last));

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
			colrow_foreach (&sheet->cols, 0, SHEET_MAX_COLS,
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
colrow_restore_sizes (Sheet *sheet, gboolean const is_cols,
		      int first, int last, ColRowRLESizeList *sizes)
{
	GSList *l;
	int i, offset = first;

	g_return_if_fail (sizes != NULL);
	g_return_if_fail (sheet != NULL);
	g_return_if_fail (first <= last);

	for (l = sizes; l != NULL; l = l->next) {
		SavedSize const *ss = l->data;

		for (i = offset; i < offset + ss->length; i++) {
			gboolean hard_size = FALSE;
			double   size      = ss->size;

			if (size == 0.) {
				ColRowCollection *infos = is_cols ? &(sheet->cols) : &(sheet->rows);
				ColRowSegment *segment = COLROW_GET_SEGMENT(infos, i);
				if (segment != NULL) {
					int const sub = COLROW_SUB_INDEX (i);
					ColRowInfo *cri = segment->info[sub];
					if (cri != NULL) {
						segment->info[sub] = NULL;
						g_free (cri);
					}
				}
			} else {
				if (size < 0.) {
					hard_size = TRUE;
					size *= -1.;
				}
				if (is_cols)
					sheet_col_set_size_pts (sheet, i, size, hard_size);
				else
					sheet_row_set_size_pts (sheet, i, size, hard_size);
			}
		}
		offset += ss->length;
		
		g_free (ss);
	}

	colrow_rle_size_list_destroy (sizes);

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
}

void
colrow_restore_sizes_group (Sheet *sheet, gboolean const is_cols,
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

		colrow_restore_sizes (sheet, is_cols,
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
	colrow_foreach (&sheet->rows, range->start.row, range->end.row,
			&cb_autofit_height, sheet);
}
/*****************************************************************************/

struct colrow_visiblity
{
	gboolean is_cols, visible;
	ColRowVisList *elements;
};

static gint
colrow_index_cmp (ColRowIndex const *a, ColRowIndex const *b)
{
	/* We can be very simplistic here because the ranges never overlap */
	return b->first - a->first;
}

static void
colrow_visibility (Sheet const *sheet, struct colrow_visiblity * const dat,
		   int first, int last, gboolean honour_collapse)
{
	int i;
	gboolean const visible = dat->visible;
	ColRowInfo * (*get) (Sheet const *sheet, int pos) = (dat->is_cols)
		? &sheet_col_get : &sheet_row_get;

	/* Find the end of a segment that will be toggled */
	for (i = last; i >= first; --i) {
		int j;
		ColRowIndex *res;
		ColRowInfo const *cri = (*get) (sheet, i);

		if (cri == NULL) {
			if (visible != 0)
				continue;
		} else if ((visible != 0) == (cri->visible != 0))
			continue;

		/* Find the begining */
		for (j = i; j >= first ; --j) {
			cri = (*get) (sheet, j);
			if (cri == NULL) {
				if (visible != 0)
					break;
			} else if ((visible != 0) == (cri->visible != 0))
				break;
			else if (cri->is_collapsed)
				break;
		}
		res = g_new (ColRowIndex, 1);
		res->first = (j > first) ? j : first;
		res->last = i;
#if 0
		printf ("%d %d\n", res->index, res->count);
#endif
		dat->elements = g_slist_insert_sorted (dat->elements, res,
					(GCompareFunc)colrow_index_cmp);

		if (visible && cri != NULL && cri->is_collapsed) {
			i = colrow_find_outline_bound (
				sheet, dat->is_cols, j,
				cri->outline_level+1, FALSE);
		} else
			i = j;
	}
}

ColRowVisList *
colrow_get_outline_toggle (Sheet const *sheet, gboolean is_cols, gboolean visible,
			   int first, int last)
{
	struct colrow_visiblity closure;
	closure.is_cols = is_cols;
	closure.visible = visible;
	closure.elements = NULL;

	colrow_visibility (sheet, &closure, first, last, TRUE);
	return closure.elements;
}

static void
cb_colrow_visibility (Sheet *sheet, Range const *r,
		       void *closure)
{
	struct colrow_visiblity * const dat = (struct colrow_visiblity *)closure;
	int first, last;

	if (dat->is_cols) {
		first = r->start.col;
		last = r->end.col;
	} else {
		first = r->start.row;
		last = r->end.row;
	}
	colrow_visibility (sheet, dat, first, last, FALSE);
}
/*
 * colrow_get_visiblity_toggle :
 * @sheet : The sheet whose selection we are interested in.
 * @is_cols: A flag indicating whether this it is a column or a row.
 * @is_visible: Should we unhide or hide the cols/rows.
 *
 * Searches the selection list and generates a list of index,count
 * pairs of row/col ranges that need to be hidden or unhiden.
 *
 * NOTE : leave sheet non-const until we have a const version of
 *        selection_apply.
 */
ColRowVisList *
colrow_get_visiblity_toggle (Sheet *sheet, gboolean is_cols,
			     gboolean visible)
{
	struct colrow_visiblity closure;
	closure.is_cols = is_cols;
	closure.visible = visible;
	closure.elements = NULL;

	selection_apply (sheet, &cb_colrow_visibility, FALSE, &closure);

	return closure.elements;
}

ColRowVisList *
colrow_vis_list_destroy (ColRowVisList *list)
{
	ColRowVisList *ptr;
	for (ptr = list; ptr != NULL ; ptr = ptr->next)
		g_free (ptr->data);
	g_slist_free (list);
	return NULL;
}

/*
 * colrow_set_visibility_list :
 *
 * This is the high level command that is wrapped by undo and redo.
 * It should not be called by other commands.
 */
void
colrow_set_visibility_list (Sheet *sheet, gboolean const is_cols,
			    gboolean const visible, ColRowVisList *list)
{
	ColRowVisList *ptr;

	/* Trivial optimization */
	if (list == NULL)
		return;

	for (ptr = list; ptr != NULL ; ptr = ptr->next) {
		ColRowIndex *info = ptr->data;
		colrow_set_visibility (sheet, is_cols, visible,
				       info->first, info->last);
	}

	if (is_cols)
		for (ptr = list; ptr != NULL ; ptr = ptr->next) {
			int min_col, max_col;
			ColRowIndex *info = ptr->data;
			sheet_regen_adjacent_spans (sheet,
				info->first, 0,
				info->last, SHEET_MAX_ROWS-1,
				&min_col, &max_col);
		}

	sheet_redraw_all (sheet);
	sheet_redraw_headers (sheet, TRUE, TRUE, NULL);
}

/**
 * colrow_adjust_outline_dir 
 * @colrows :
 * @pre_or_post :
 */
void
colrow_adjust_outline_dir (ColRowCollection *colrows, gboolean pre_or_post)
{
	/* excel pisses me off */
	g_warning ("TODO : write me");
}

/**
 * colrow_find_outline_bound :
 *
 * find the next/prev col/row at the designated depth starting from the
 * supplied @index.
 */
int
colrow_find_outline_bound (Sheet const *sheet, gboolean is_cols,
			   int index, int depth, gboolean inc)
{
	ColRowInfo * (*get) (Sheet const *sheet, int pos) = is_cols
		? &sheet_col_get : &sheet_row_get;
	int const max = is_cols ? SHEET_MAX_COLS : SHEET_MAX_ROWS;
	int const step = inc ? 1 : -1;

	while (1) {
		ColRowInfo const *cri;
		int const next = index + step;
		
		if (next < 0 || next >= max)
			return index;
		cri = (*get) (sheet, next);
		if (cri == NULL || cri->outline_level < depth)
			return index;
		index = next;
	}

	return index;
}

/**
 * colrow_find_adjacent_visible:
 * @sheet: Sheet to search on.
 * @is_col: If true find next column else find next row.
 * @index: The col/row index to start at.
 * @forward: If set seek forward otherwise seek backwards.
 *  
 * Return value: The index of the next visible col/row or -1 if
 *               there are no more visible cols/rows left.
 **/
int
colrow_find_adjacent_visible (Sheet *sheet, gboolean const is_col,
			      int const index, gboolean forward)
{
	int const max    = is_col ? SHEET_MAX_COLS : SHEET_MAX_ROWS;
	int i            = index; /* To avoid trouble at edges */

	do {
		ColRowInfo * const cri = is_col
			? sheet_col_fetch (sheet, i)
			: sheet_row_fetch (sheet, i);
			
		if (cri->visible)
			return i;

		if (forward) {
			if (++i >= max) {
				i       = index;
				forward = FALSE;
			}
		} else
			i--;

	} while (i > 0);

	return -1;
}

/**
 * colrow_set_visibility:
 * @sheet	: the sheet
 * @is_cols	: Are we dealing with rows or columns.
 * @visible	: Make things visible or invisible.
 * @first	: The index of the first row/col (inclusive)
 * @last	: The index of the last row/col (inclusive)
 *
 * Change the visibility of the selected range of contiguous rows/cols.
 */
void
colrow_set_visibility (Sheet *sheet, gboolean const is_cols,
		       gboolean const visible, int first, int last)
{
	int i, prev_outline = 0;
	gboolean prev_changed = FALSE;
	g_return_if_fail (sheet != NULL);

	for (i = first; i <= last ; ++i) {
		ColRowInfo * const cri = is_cols
			? sheet_col_fetch (sheet, i)
			: sheet_row_fetch (sheet, i);

		if (prev_changed && prev_outline > cri->outline_level && !visible)
			cri->is_collapsed = FALSE;

		prev_changed = (visible == 0) != (cri->visible == 0);
		if (prev_changed) {
			cri->visible = visible;
			prev_outline = cri->outline_level;
			sheet->priv->recompute_visibility = TRUE;
		}
	}
	if (prev_changed &&
	    ((is_cols && i < SHEET_MAX_COLS) ||
	     (!is_cols && i < SHEET_MAX_ROWS))) {
		ColRowInfo * const cri = is_cols
			? sheet_col_fetch (sheet, i)
			: sheet_row_fetch (sheet, i);

		if (prev_outline > cri->outline_level)
			cri->is_collapsed = !visible;
	}
}
