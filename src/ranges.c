/*
 * ranges.c: various functions for common operations on cell ranges.
 *
 * Author:
 *   Jody Goldberg   (jody@gnome.org)
 *   Miguel de Icaza (miguel@gnu.org).
 *   Michael Meeks   (mmeeks@gnu.org).
 * Copyright (C) 2007-2009 Morten Welinder (terra@gnome.org)
 */
#include <gnumeric-config.h>
#include <gnumeric.h>
#include <ranges.h>

#include <commands.h>
#include <numbers.h>
#include <expr.h>
#include <expr-impl.h>
#include <expr-name.h>
#include <sheet.h>
#include <sheet-style.h>
#include <parse-util.h>
#include <value.h>
#include <cell.h>
#include <style.h>
#include <workbook.h>
#include <gnumeric-conf.h>

#include <stdlib.h>
#include <glib.h>
#include <glib/gi18n-lib.h>
#include <string.h>

#undef RANGE_DEBUG
#define UNICODE_ELLIPSIS "\xe2\x80\xa6"

/**
 * range_init_full_sheet:
 * @r: #GnmRange
 * @sheet: #Sheet
 *
 * Updates @r to fill @sheet in its entirety.
 *
 * Returns: (type void): @r
 */
GnmRange *
range_init_full_sheet (GnmRange *r, Sheet const *sheet)
{
	r->start.col = 0;
	r->start.row = 0;
	r->end.col = gnm_sheet_get_last_col (sheet);
	r->end.row = gnm_sheet_get_last_row (sheet);
	return r;
}

/**
 * range_init_cols:
 * @r: #GnmRange
 * @sheet: #Sheet
 * @start_col: Starting column
 * @end_col: Ending column
 *
 * Updates @r to span full columns @start_col through @end_col completely.
 *
 * Returns: (type void): @r
 */
GnmRange *
range_init_cols (GnmRange *r, Sheet const *sheet, int start_col, int end_col)
{
	r->start.col = start_col;
	r->start.row = 0;
	r->end.col = end_col;
	r->end.row = gnm_sheet_get_last_row (sheet);
	return r;
}

/**
 * range_init_rows:
 * @r: #GnmRange
 * @sheet: #Sheet
 * @start_row: Starting row
 * @end_row: Ending row
 *
 * Updates @r to span full rows @start_row through @end_row completely.
 *
 * Returns: (type void): @r
 */
GnmRange *
range_init_rows (GnmRange *r, Sheet const *sheet, int start_row, int end_row)
{
	r->start.col = 0;
	r->start.row = start_row;
	r->end.col = gnm_sheet_get_last_col (sheet);
	r->end.row = end_row;
	return r;
}

/**
 * range_init_invalid: (skip)
 * @r: #GnmRange
 *
 * Updates @r to a meaningless range
 *
 * Returns: (type void): @r
 */
GnmRange *
range_init_invalid (GnmRange *r)
{
	r->start.col = -1;
	r->start.row = -1;
	r->end.col = -2;
	r->end.row = -2;
	return r;
}

/**
 * range_init_rangeref:
 * @r: #GnmRange
 * @rr: #GnmRangeRef
 *
 * Updates @r to be the same as the range part of @rr.
 *
 * Returns: (type void): @r
 */
GnmRange *
range_init_rangeref (GnmRange *range, GnmRangeRef const *rr)
{
	g_return_val_if_fail (range != NULL && rr != NULL, NULL);

	range->start.col = rr->a.col;
	range->start.row = rr->a.row;
	range->end.col   = rr->b.col;
	range->end.row   = rr->b.row;
	return range;
}


/**
 * range_init_value:
 * @r: A #GnmRange to change
 * @v: A #GnmValue containing a cell range
 *
 * Updates @r to be the same as the cell range of @v.
 *
 * Returns: (type void): @r
 */
GnmRange *
range_init_value (GnmRange *range, GnmValue const *v)
{
	g_return_val_if_fail (range != NULL, NULL);
	g_return_val_if_fail (v != NULL && VALUE_IS_CELLRANGE (v), NULL);

	return range_init_rangeref (range, &v->v_range.cell);
}

/**
 * range_init_cellpos:
 * @r: A #GnmRange to change
 * @pos: A #GnmCellPos
 *
 * Updates @r to be the singleton range of @pos
 *
 * Returns: (type void): @r
 */
GnmRange *
range_init_cellpos (GnmRange *r, GnmCellPos const *pos)
{
	r->start = *pos;
	r->end = *pos;

	return r;
}

/**
 * range_init_cellpos_size:
 * @r: A #GnmRange to change
 * @start: A #GnmCellPos for the upper left corner of the desired range
 * @cols: number of columns
 * @rows: number of rows
 *
 * Updates @r to start at @start and spanning @cols columns and @rows rows.
 *
 * Returns: (type void): @r
 */
GnmRange *
range_init_cellpos_size (GnmRange *r,
			 GnmCellPos const *start, int cols, int rows)
{
	r->start = *start;
	r->end.col = start->col + cols - 1;
	r->end.row = start->row + rows - 1;

	return r;
}

/**
 * range_init:
 * @r: A #GnmRange to change
 * @start_col: Column
 * @start_row: Row
 * @end_col: Column
 * @end_row: Row
 *
 * Updates @r to start at (@start_col,@start_row) and end
 * at (@end_col,@end_row).
 *
 * Returns: (type void): @r
 */
GnmRange *
range_init (GnmRange *r, int start_col, int start_row,
	    int end_col, int end_row)
{
	g_return_val_if_fail (r != NULL, r);

	r->start.col = start_col;
	r->start.row = start_row;
	r->end.col   = end_col;
	r->end.row   = end_row;

	return r;
}

/**
 * range_parse:
 * @r: #GnmRange
 * @text: text to parse
 * @ss: #GnmSheetSize describing the size of the sheet in which @r lives.
 *
 * Parse a simple range (no abs/rel refs, no sheet refs)
 * Store the result in @r.
 *
 * Returns: %TRUE on success.
 **/
gboolean
range_parse (GnmRange *r, char const *text, GnmSheetSize const *ss)
{
	text = cellpos_parse (text, ss, &r->start, FALSE);
	if (!text)
		return FALSE;

	if (*text == '\0') {
		r->end = r->start;
		return TRUE;
	}

	if (*text != ':')
		return FALSE;

	text = cellpos_parse (text + 1, ss, &r->end, TRUE);
	return text != NULL;
}

/**
 * range_list_destroy:
 * @ranges: (element-type GnmValue) (transfer full): a list of value ranges
 * to destroy.
 *
 * Destroys a list of ranges returned from parse_cell_range_list.  Note:
 * the element type here is GnmValue, not GnmRange.
 **/
void
range_list_destroy (GSList *ranges)
{
	g_slist_free_full (ranges, (GDestroyNotify)value_release);
}


/**
 * range_as_string:
 * @r: A #GnmRange
 *
 * Returns: (transfer none): a string representation of @src
 **/
char const *
range_as_string (GnmRange const *r)
{
	static char buffer[(6 + 4 * sizeof (long)) * 2 + 1];

	g_return_val_if_fail (r != NULL, "");

	sprintf (buffer, "%s%s",
		 col_name (r->start.col), row_name (r->start.row));

	if (r->start.col != r->end.col || r->start.row != r->end.row)
		sprintf (buffer + strlen(buffer), ":%s%s",
			 col_name (r->end.col), row_name (r->end.row));

	return buffer;
}

void
range_dump (GnmRange const *src, char const *suffix)
{
	/*
	 * keep these as 2 print statements, because
	 * col_name and row_name use a static buffer
	 */
	g_printerr ("%s%s",
		col_name (src->start.col),
		row_name (src->start.row));

	if (src->start.col != src->end.col ||
	    src->start.row != src->end.row)
		g_printerr (":%s%s",
			col_name (src->end.col),
			row_name (src->end.row));
	g_printerr ("%s", suffix);
}

#ifdef RANGE_DEBUG
static void
ranges_dump (GList *l, char const *txt)
{
	g_printerr ("%s: ", txt);
	for (; l; l = l->next) {
		range_dump (l->data, "");
		if (l->next)
			g_printerr (", ");
	}
	g_printerr ("\n");
}
#endif

/**
 * range_contained:
 * @a:
 * @b:
 *
 * Is @a totally contained by @b
 *
 * Return value:
 **/
gboolean
range_contained (GnmRange const *a, GnmRange const *b)
{
	if (a->start.row < b->start.row)
		return FALSE;

	if (a->end.row > b->end.row)
		return FALSE;

	if (a->start.col < b->start.col)
		return FALSE;

	if (a->end.col > b->end.col)
		return FALSE;

	return TRUE;
}

/**
 * range_split_ranges:
 * @hard: The region that is split against
 * @soft: The region that is split
 *
 * Splits soft into several chunks, and returns the still
 * overlapping remainder of soft as the first list item
 * (the central region in the pathological case).
 *
 * Returns: (element-type GnmRange) (transfer full): A list of fragments.
 **/
GSList *
range_split_ranges (GnmRange const *hard, GnmRange const *soft)
{
	/*
	 * There are lots of cases so think carefully.
	 *
	 * Original Methodology ( approximately )
	 *	a) Get a vertex: is it contained ?
	 *	b) Yes: split so it isn't
	 *	c) Continue for all vertices.
	 *
	 * NB. We prefer to have long columns at the expense
	 *     of long rows.
	 */
	GSList *split  = NULL;
	GnmRange *middle, *sp;
	gboolean split_left  = FALSE;
	gboolean split_right = FALSE;

	g_return_val_if_fail (range_overlap (hard, soft), NULL);

	middle = g_new (GnmRange, 1);
	*middle = *soft;

	/* Split off left entirely */
	if (hard->start.col > soft->start.col) {
		sp = g_new (GnmRange, 1);
		sp->start.col = soft->start.col;
		sp->start.row = soft->start.row;
		sp->end.col   = hard->start.col - 1;
		sp->end.row   = soft->end.row;
		split = g_slist_prepend (split, sp);

		middle->start.col = hard->start.col;
		split_left = TRUE;
	} /* else shared edge */

	/* Split off right entirely */
	if (hard->end.col < soft->end.col) {
		sp = g_new (GnmRange, 1);
		sp->start.col = hard->end.col + 1;
		sp->start.row = soft->start.row;
		sp->end.col   = soft->end.col;
		sp->end.row   = soft->end.row;
		split = g_slist_prepend (split, sp);

		middle->end.col = hard->end.col;
		split_right = TRUE;
	}  /* else shared edge */

	/* Top */
	if (split_left && split_right) {
		if (hard->start.row > soft->start.row) {
			/* The top middle bit */
			sp = g_new (GnmRange, 1);
			sp->start.col = hard->start.col;
			sp->start.row = soft->start.row;
			sp->end.col   = hard->end.col;
			sp->end.row   = hard->start.row - 1;
			split = g_slist_prepend (split, sp);

			middle->start.row = hard->start.row;
		} /* else shared edge */
	} else if (split_left) {
		if (hard->start.row > soft->start.row) {
			/* The top middle + right bits */
			sp = g_new (GnmRange, 1);
			sp->start.col = hard->start.col;
			sp->start.row = soft->start.row;
			sp->end.col   = soft->end.col;
			sp->end.row   = hard->start.row - 1;
			split = g_slist_prepend (split, sp);

			middle->start.row = hard->start.row;
		} /* else shared edge */
	} else if (split_right) {
		if (hard->start.row > soft->start.row) {
			/* The top middle + left bits */
			sp = g_new (GnmRange, 1);
			sp->start.col = soft->start.col;
			sp->start.row = soft->start.row;
			sp->end.col   = hard->end.col;
			sp->end.row   = hard->start.row - 1;
			split = g_slist_prepend (split, sp);

			middle->start.row = hard->start.row;
		} /* else shared edge */
	} else {
		if (hard->start.row > soft->start.row) {
			/* Hack off the top bit */
			sp = g_new (GnmRange, 1);
			sp->start.col = soft->start.col;
			sp->start.row = soft->start.row;
			sp->end.col   = soft->end.col;
			sp->end.row   = hard->start.row - 1;
			split = g_slist_prepend (split, sp);

			middle->start.row = hard->start.row;
		} /* else shared edge */
	}

	/* Bottom */
	if (split_left && split_right) {
		if (hard->end.row < soft->end.row) {
			/* The bottom middle bit */
			sp = g_new (GnmRange, 1);
			sp->start.col = hard->start.col;
			sp->start.row = hard->end.row + 1;
			sp->end.col   = hard->end.col;
			sp->end.row   = soft->end.row;
			split = g_slist_prepend (split, sp);

			middle->end.row = hard->end.row;
		} /* else shared edge */
	} else if (split_left) {
		if (hard->end.row < soft->end.row) {
			/* The bottom middle + right bits */
			sp = g_new (GnmRange, 1);
			sp->start.col = hard->start.col;
			sp->start.row = hard->end.row + 1;
			sp->end.col   = soft->end.col;
			sp->end.row   = soft->end.row;
			split = g_slist_prepend (split, sp);

			middle->end.row = hard->end.row;
		} /* else shared edge */
	} else if (split_right) {
		if (hard->end.row < soft->end.row) {
			/* The bottom middle + left bits */
			sp = g_new (GnmRange, 1);
			sp->start.col = soft->start.col;
			sp->start.row = hard->end.row + 1;
			sp->end.col   = hard->end.col;
			sp->end.row   = soft->end.row;
			split = g_slist_prepend (split, sp);

			middle->end.row = hard->end.row;
		} /* else shared edge */
	} else {
		if (hard->end.row < soft->end.row) {
			/* Hack off the bottom bit */
			sp = g_new (GnmRange, 1);
			sp->start.col = soft->start.col;
			sp->start.row = hard->end.row + 1;
			sp->end.col   = soft->end.col;
			sp->end.row   = soft->end.row;
			split = g_slist_prepend (split, sp);

			middle->end.row = hard->end.row;
		} /* else shared edge */
	}

	return g_slist_prepend (split, middle);
}

/**
 * gnm_range_dup:
 * @r: Source range to copy
 *
 * Copies the @r range.
 *
 * Returns: (transfer full): A copy of the GnmRange.
 **/
GnmRange *
gnm_range_dup (GnmRange const *r)
{
	return go_memdup (r, sizeof (*r));
}

/**
 * range_fragment:
 * @a: First #GnmRange
 * @b: Second #GnmRange
 *
 * Fragments the ranges into totally non-overlapping regions,
 *
 * Returns: (element-type GnmRange) (transfer full): A list of fragmented
 * ranges or at minimum simply @a and @b.
 **/
GSList *
range_fragment (GnmRange const *a, GnmRange const *b)
{
	GSList *split, *ans = NULL;

	split = range_split_ranges (a, b);
	ans   = g_slist_concat (ans, split);

	split = range_split_ranges (b, a);
	if (split) {
		g_free (split->data);
		split = g_slist_remove (split, split->data);
	}
	ans = g_slist_concat (ans, split);

	return ans;
}

/**
 * range_intersection:
 * @r: intersection range
 * @a: First #GnmRange
 * @b: Second #GnmRange
 *
 * This computes the intersection of two ranges; on a Venn
 * diagram this would be A (upside down U) B.
 * If the ranges do not intersect, false is returned an the
 * values of @r are unpredictable.
 *
 * NB. totally commutative
 *
 * Return value: %TRUE if the ranges intersect, %FALSE otherwise
 **/
gboolean
range_intersection (GnmRange *r, GnmRange const *a, GnmRange const *b)
{
	if (!range_overlap (a, b)) {
		*r = *a; // Something
		return FALSE;
	}

	r->start.col = MAX (a->start.col, b->start.col);
	r->start.row = MAX (a->start.row, b->start.row);

	r->end.col = MIN (a->end.col, b->end.col);
	r->end.row = MIN (a->end.row, b->end.row);

	return TRUE;
}

/**
 * range_normalize:
 * @src: a range
 *
 * Ensures that start <= end for rows and cols.
 **/
void
range_normalize (GnmRange *src)
{
	int tmp;

	tmp = src->end.col;
	if (src->start.col > tmp) {
		src->end.col = src->start.col;
		src->start.col = tmp;
	}
	tmp = src->end.row;
	if (src->start.row > tmp) {
		src->end.row = src->start.row;
		src->start.row = tmp;
	}
}

/**
 * range_union:
 * @a: range a
 * @b: range b
 *
 * This computes the union; on a Venn
 * diagram this would be A U B
 * NB. totally commutative. Also, this may
 * include cells not in either range since
 * it must return a #GnmRange.
 *
 * Return value: the union
 **/
GnmRange
range_union (GnmRange const *a, GnmRange const *b)
{
	GnmRange ans;

	if (a->start.col < b->start.col)
		ans.start.col = a->start.col;
	else
		ans.start.col = b->start.col;

	if (a->end.col > b->end.col)
		ans.end.col   = a->end.col;
	else
		ans.end.col   = b->end.col;

	if (a->start.row < b->start.row)
		ans.start.row = a->start.row;
	else
		ans.start.row = b->start.row;

	if (a->end.row > b->end.row)
		ans.end.row   = a->end.row;
	else
		ans.end.row   = b->end.row;

	return ans;
}

/**
 * range_is_singleton:
 * @r: the range.
 *
 * Returns: %TRUE if @r is a single-cell range.
 */
gboolean
range_is_singleton (GnmRange const *r)
{
	return r->start.col == r->end.col && r->start.row == r->end.row;
}

/**
 * range_is_full:
 * @r: the range.
 * @sheet: the sheet in which @r lives
 * @horiz: %TRUE to check for a horizontal full ref (all _cols_); %FALSE
 * to check for a vertical full ref (all _rows_).
 *
 * This determines whether @r completely spans a sheet in the dimension
 * specified by @horiz.
 *
 * Returns: %TRUE if it is infinite, %FALSE otherwise.
 **/
gboolean
range_is_full (GnmRange const *r, Sheet const *sheet, gboolean horiz)
{
	if (horiz)
		return (r->start.col <= 0 &&
			r->end.col >= gnm_sheet_get_last_col (sheet));
	else
		return (r->start.row <= 0 &&
			r->end.row >= gnm_sheet_get_last_row (sheet));
}

/**
 * range_clip_to_finite:
 * @range:
 * @sheet: the sheet in which @range lives
 *
 * Clip the range to the area of the sheet with content.
 * if the range reaches the edge
 *
 * The idea here is that users may select a whole column or row when they
 * really are only concerned with the extent of the sheet.
 * On the otehr hand, if users select any smaller region they probably
 * intend to select just that.
 *
 * WARNING THIS IS EXPENSIVE!
 **/
void
range_clip_to_finite (GnmRange *range, Sheet *sheet)
{
	GnmRange extent;

	/* FIXME : This seems expensive.  We should see if there is a faster
	 * way of doing this.  possibly using a flag for content changes, and
	 * using the current values as a cache
	 */
	extent = sheet_get_extent (sheet, FALSE, TRUE);
	if (range->end.col >= gnm_sheet_get_max_cols (sheet) - 1)
		range->end.col = extent.end.col;
	if (range->end.row >= gnm_sheet_get_max_rows (sheet) - 1)
		range->end.row = extent.end.row;
}

/**
 * range_width:
 * @r: #GnmRange
 *
 * Returns: width of @r.
 */
int
range_width (GnmRange const *r)
{
	g_return_val_if_fail (r != NULL, 0);
	return ABS (r->end.col - r->start.col) + 1;
}

/**
 * range_height:
 * @r: #GnmRange
 *
 * Returns: height of @r.
 */
int
range_height (GnmRange const *r)
{
	g_return_val_if_fail (r != NULL, 0);
	return ABS (r->end.row - r->start.row) + 1;
}

/**
 * range_translate:
 * @range:
 * @sheet: the sheet in which @range lives
 * @col_offset:
 * @row_offset:
 *
 * Translate the range and return %TRUE if it is invalidated.
 *
 * Return: %TRUE if the range is no longer valid.
 **/
gboolean
range_translate (GnmRange *range, Sheet const *sheet, int col_offset, int row_offset)
{
	/*
	 * FIXME: we should probably check for overflow without actually
	 * performing it.
	 */
	range->start.col += col_offset;
	range->end.col   += col_offset;
	range->start.row += row_offset;
	range->end.row   += row_offset;

	/* check for completely out of bounds */
	if (range->start.col >= gnm_sheet_get_max_cols (sheet) || range->start.col < 0 ||
	    range->start.row >= gnm_sheet_get_max_rows (sheet) || range->start.row < 0 ||
	    range->end.col >= gnm_sheet_get_max_cols (sheet) || range->end.col < 0 ||
	    range->end.row >= gnm_sheet_get_max_rows (sheet) || range->end.row < 0)
		return TRUE;

	return FALSE;
}

/**
 * range_ensure_sanity:
 * @range: the range to check
 * @sheet: the sheet in which @range lives
 *
 * Silently clip a range to ensure that it does not contain areas
 * outside the valid bounds.  Does NOT fix inverted ranges.
 **/
void
range_ensure_sanity (GnmRange *range, Sheet const *sheet)
{
	range->start.col = MAX (0, range->start.col);
	range->end.col = MIN (range->end.col, gnm_sheet_get_last_col (sheet));

	range->start.row = MAX (0, range->start.row);
	range->end.row = MIN (range->end.row, gnm_sheet_get_last_row (sheet));
}

/**
 * range_is_sane:
 * @range: the range to check
 *
 * Generate warnings if the range is out of bounds or inverted.
 **/
gboolean
range_is_sane (GnmRange const *range)
{
	g_return_val_if_fail (range != NULL, FALSE);
	g_return_val_if_fail (range->start.col >= 0, FALSE);
	g_return_val_if_fail (range->end.col >= range->start.col, FALSE);
	g_return_val_if_fail (range->end.col <= G_MAXINT / 2, FALSE);
	g_return_val_if_fail (range->start.row >= 0, FALSE);
	g_return_val_if_fail (range->end.row >= range->start.row, FALSE);
	g_return_val_if_fail (range->end.row <= G_MAXINT / 2, FALSE);

	return TRUE;
}

/**
 * range_transpose:
 * @range: The range.
 * @sheet: the sheet in which @range lives
 * @origin: The box to transpose inside
 *
 * Effectively mirrors the ranges in 'boundary' around a
 * leading diagonal projected from offset.
 *
 * Return value: whether we clipped the range.
 **/
gboolean
range_transpose (GnmRange *range, Sheet const *sheet, GnmCellPos const *origin)
{
	gboolean clipped = FALSE;
	GnmRange src;
	int t;
	int last_col = gnm_sheet_get_last_col (sheet);
	int last_row = gnm_sheet_get_last_row (sheet);

	g_return_val_if_fail (range != NULL, TRUE);

	src = *range;

	/* Start col */
	t = origin->col + (src.start.row - origin->row);
	if (t > last_col) {
		clipped = TRUE;
		range->start.col = last_col;
	} else if (t < 0) {
		clipped = TRUE;
		range->start.col = 0;
	}
		range->start.col = t;

	/* Start row */
	t = origin->row + (src.start.col - origin->col);
	if (t > last_row) {
		clipped = TRUE;
		range->start.row = last_row;
	} else if (t < 0) {
		clipped = TRUE;
		range->start.row = 0;
	}
		range->start.row = t;


	/* End col */
	t = origin->col + (src.end.row - origin->row);
	if (t > last_col) {
		clipped = TRUE;
		range->end.col = last_col;
	} else if (t < 0) {
		clipped = TRUE;
		range->end.col = 0;
	}
		range->end.col = t;

	/* End row */
	t = origin->row + (src.end.col - origin->col);
	if (t > last_row) {
		clipped = TRUE;
		range->end.row = last_row;
	} else if (t < 0) {
		clipped = TRUE;
		range->end.row = 0;
	}
		range->end.row = t;

	g_assert (range_valid (range));

	return clipped;
}

gboolean
gnm_range_equal (const GnmRange *a, const GnmRange *b)
{
	return range_equal (a, b);
}

/**
 * gnm_range_compare:
 * @a: first range
 * @b: second range
 *
 * Returns: a value that is negative if range @a comes before range @b;
 * zero if the two ranges are equal; positive if range @a comes after
 * range @b.  The order imposed is lexicographical by starting row,
 * then column, then ending row, then column.  In other words, the order
 * is A1, B1, A2, B2.
 */
int
gnm_range_compare (GnmRange const *a, GnmRange const *b)
{
	int i = 0;
	if (!i) i = a->start.row - b->start.row;
	if (!i) i = a->start.col - b->start.col;
	if (!i) i = a->end.row - b->end.row;
	if (!i) i = a->end.col - b->end.col;
	return i;
}

// Alternative with order A1, A2, B1, B2
static int
gnm_range_compare_alt (GnmRange const *a, GnmRange const *b)
{
	int i = 0;
	if (!i) i = a->start.col - b->start.col;
	if (!i) i = a->start.row - b->start.row;
	if (!i) i = a->end.col - b->end.col;
	if (!i) i = a->end.row - b->end.row;
	return i;
}


GnmSheetRange *
gnm_sheet_range_new (Sheet *sheet, GnmRange const *r)
{
	GnmSheetRange *gr;

	g_return_val_if_fail (IS_SHEET (sheet), NULL);
	g_return_val_if_fail (r != NULL, NULL);

	gr = g_new0 (GnmSheetRange, 1);
	gr->sheet = sheet;
	gr->range = *r;

	return gr;
}

GnmSheetRange *
gnm_sheet_range_dup (GnmSheetRange const *sr)
{
	g_return_val_if_fail (sr != NULL, NULL);
	return gnm_sheet_range_new (sr->sheet, &sr->range);
}

/**
 * gnm_sheet_range_from_value:
 * @r: #GnmSheetRange to change
 * @v: #GnmValue containing a cell range.
 *
 * Convert @v into a GnmSheetRange.
 *
 * Returns: %TRUE
 **/
gboolean
gnm_sheet_range_from_value (GnmSheetRange *r, GnmValue const *v)
{
	g_return_val_if_fail (VALUE_IS_CELLRANGE (v), FALSE);

	r->sheet = v->v_range.cell.a.sheet;
	range_init_value (&r->range, v);

	return TRUE;
}

void
gnm_sheet_range_free (GnmSheetRange *gr)
{
	g_free (gr);
}

GType
gnm_sheet_range_get_type (void)
{
	static GType t = 0;

	if (t == 0)
		t = g_boxed_type_register_static ("GnmSheetRange",
			 (GBoxedCopyFunc)gnm_sheet_range_dup,
			 (GBoxedFreeFunc)gnm_sheet_range_free);
	return t;
}

gboolean
gnm_sheet_range_equal (const GnmSheetRange *a,
		       const GnmSheetRange *b)
{
	return a->sheet == b->sheet && range_equal (&a->range, &b->range);
}

gboolean
gnm_sheet_range_overlap (GnmSheetRange const *a, GnmSheetRange const *b)
{
	g_return_val_if_fail (a != NULL, FALSE);
	g_return_val_if_fail (b != NULL, FALSE);

	if (a->sheet == b->sheet && range_overlap (&a->range, &b->range))
		return TRUE;

	return FALSE;
}

char *
global_range_name (Sheet const *sheet, GnmRange const *r)
{
	char const * the_range_name = range_as_string (r);

	if (sheet == NULL)
		return g_strdup (the_range_name);

	return g_strdup_printf ("%s!%s", sheet->name_quoted, the_range_name);
}

/**
 * undo_cell_pos_name:
 * @sheet:
 * @pos:
 *
 * Returns the range name depending on the preference setting.
 **/
char *
undo_cell_pos_name (Sheet const *sheet, GnmCellPos const *pos)
{
	GnmRange r;
	r.end = r.start = *pos;
	return undo_range_name (sheet, &r);
}

/**
 * undo_range_name:
 * @sheet:
 * @r:
 *
 * Returns the range name depending on the preference setting.
 **/
char *
undo_range_name (Sheet const *sheet, GnmRange const *r)
{
	char const *the_range_name = range_as_string (r);

	if (sheet != NULL && gnm_conf_get_undo_show_sheet_name ()) {
		GString *str  = g_string_new (NULL);
		gboolean truncated = FALSE;

		g_string_printf (str, "%s!%s", sheet->name_quoted, the_range_name);
		gnm_cmd_trunc_descriptor (str, &truncated);

		if (!truncated)
			return g_string_free (str, FALSE);

		g_string_printf (str, UNICODE_ELLIPSIS "!%s", the_range_name);
		gnm_cmd_trunc_descriptor (str, &truncated);

		if (!truncated)
			return g_string_free (str, FALSE);
		g_string_free (str, TRUE);
	}

	return g_string_free
		(gnm_cmd_trunc_descriptor (g_string_new (the_range_name), NULL), FALSE);
}


/*
 * Create range list name, but don't exceed max_width.
 * Returns: %TRUE iff the name is complete.
 */
static gboolean
range_list_name_try (GString *names, char const *sheet, GSList const *ranges)
{
	GSList const *l;
	char const *n = range_as_string (ranges->data);
	gboolean truncated;

	if (sheet == NULL)
		g_string_assign (names, n);
	else
		g_string_printf (names, "%s!%s", sheet, n);

	gnm_cmd_trunc_descriptor (names, &truncated);

	if (truncated)
		return FALSE;

	for (l = ranges->next; l != NULL; l = l->next) {
		n = range_as_string (l->data);

		if (sheet == NULL)
			g_string_append_printf (names, ", %s", n);
		else
			g_string_append_printf (names, ", %s!%s",
						sheet, n);

		gnm_cmd_trunc_descriptor (names, &truncated);

		if (truncated)
			return FALSE;
	}

	/* Have we reached the end? */
	return TRUE;
}


/**
 * undo_range_list_name:
 * @sheet:
 * @ranges: (element-type GnmRange): list of ranges
 *
 * Returns: the range list name depending on the preference setting.
 * (The result will be something like: "A1:C3, D4:E5"). The string will be
 * truncated to max_descriptor_width.
 **/
char *
undo_range_list_name (Sheet const *sheet, GSList const *ranges)
{
	GString *names_with_sheet = NULL, *names_with_ellipsis, *names;

	g_return_val_if_fail (ranges != NULL, NULL);

	/* With the sheet name. */
	if (sheet != NULL && gnm_conf_get_undo_show_sheet_name ()) {
		names_with_sheet = g_string_new (NULL);
		if (range_list_name_try (names_with_sheet, sheet->name_quoted, ranges)) {
			/* We have reached the end, return the data from names. */
			return g_string_free (names_with_sheet, FALSE);
		}
		names_with_ellipsis = g_string_new (NULL);
		if (range_list_name_try (names_with_ellipsis, UNICODE_ELLIPSIS, ranges)) {
			/* We have reached the end, return the data from names. */
			g_string_free (names_with_sheet, TRUE);
			return g_string_free (names_with_ellipsis, FALSE);
		}
		g_string_free (names_with_ellipsis, TRUE);
	}

	/* Without the sheet name. */
	names = g_string_new (NULL);
	if (range_list_name_try (names, NULL, ranges)) {
		/* We have reached the end, return the data from names. */
		if (names_with_sheet != NULL)
			g_string_free (names_with_sheet, TRUE);
		return g_string_free (names, FALSE);
	}

	/* We have to use a truncated version. */
	if (names_with_sheet != NULL) {
		g_string_free (names, TRUE);
		return g_string_free (names_with_sheet, FALSE);
	}
	return g_string_free (names, FALSE);
}

/**
 * global_range_list_parse:
 * @sheet: Sheet where the range specification is relatively parsed to
 * @str: a range or list of ranges to parse (ex: "A1", "A1:B1,C2,Sheet2!D2:D4")
 *
 * Parses a list of ranges, relative to the @sheet and returns a list with the
 * results.
 *
 * Returns: (element-type GnmValue) (transfer full): a #GSList of #GnmValue.
 **/
GSList *
global_range_list_parse (Sheet *sheet, char const *str)
{
	GnmParsePos  pp;
	GnmExprTop const *texpr;
	GSList   *ranges = NULL;
	GnmValue	 *v;

	g_return_val_if_fail (IS_SHEET (sheet), NULL);
	g_return_val_if_fail (str != NULL, NULL);

	texpr = gnm_expr_parse_str (str,
		 parse_pos_init_sheet (&pp, sheet),
		 GNM_EXPR_PARSE_FORCE_EXPLICIT_SHEET_REFERENCES |
		 GNM_EXPR_PARSE_PERMIT_MULTIPLE_EXPRESSIONS |
		 GNM_EXPR_PARSE_UNKNOWN_NAMES_ARE_STRINGS,
		 NULL, NULL);

	if (texpr != NULL)  {
		if (GNM_EXPR_GET_OPER (texpr->expr) == GNM_EXPR_OP_SET) {
			GnmExpr const *expr = texpr->expr;
			int i;
			for (i = 0; i < expr->set.argc; i++) {
				v = gnm_expr_get_range (expr->set.argv[i]);
				if (v == NULL) {
					range_list_destroy (ranges);
					ranges = NULL;
					break;
				} else
					ranges = g_slist_prepend (ranges, v);
			}
		} else {
			v = gnm_expr_top_get_range (texpr);
			if (v != NULL)
				ranges = g_slist_prepend (ranges, v);
		}
		gnm_expr_top_unref (texpr);
	}

	return g_slist_reverse (ranges);
}

/**
 * global_range_list_foreach:
 * @gr_list: (element-type GnmRange): list of ranges.
 * @ep:
 * @flags:
 * @handler: (scope call):
 * @closure: user data.
 *
 * Returns: (transfer none):
 **/
GnmValue *
global_range_list_foreach (GSList *gr_list, GnmEvalPos const *ep,
			   CellIterFlags flags,
			   CellIterFunc  handler,
			   gpointer	 closure)
{
	GnmValue *v;
	for (; gr_list != NULL; gr_list = gr_list->next) {
		v = workbook_foreach_cell_in_range (ep, gr_list->data,
			flags, handler, closure);
		if (v != NULL)
			return v;
	}

	return NULL;
}


/**
 * global_range_contained:
 * @sheet: The calling context #Sheet for references without sheet.
 * @a: A #GnmValue representing a range
 * @b: A #GnmValue representing a range
 *
 * Returns: %TRUE if @a is contained in @b.  We do not handle 3d ranges
 **/
gboolean
global_range_contained (Sheet const *sheet, GnmValue const *a, GnmValue const *b)
{
	Sheet const *target;

	g_return_val_if_fail (a != NULL, FALSE);
	g_return_val_if_fail (b != NULL, FALSE);

	if (!VALUE_IS_CELLRANGE (a) || !VALUE_IS_CELLRANGE (b))
		return FALSE;

	target = eval_sheet (a->v_range.cell.a.sheet, sheet);
	if (target != eval_sheet (a->v_range.cell.b.sheet, sheet))
		return FALSE;

	if (target != eval_sheet (b->v_range.cell.a.sheet, sheet) ||
	    target != eval_sheet (b->v_range.cell.b.sheet, sheet))
		return FALSE;

	if (a->v_range.cell.a.row < b->v_range.cell.a.row)
		return FALSE;

	if (a->v_range.cell.b.row > b->v_range.cell.b.row)
		return FALSE;

	if (a->v_range.cell.a.col < b->v_range.cell.a.col)
		return FALSE;

	if (a->v_range.cell.b.col > b->v_range.cell.b.col)
		return FALSE;

	return TRUE;
}

GType
gnm_range_get_type (void)
{
	static GType t = 0;

	if (t == 0)
		t = g_boxed_type_register_static ("GnmRange",
			 (GBoxedCopyFunc)gnm_range_dup,
			 (GBoxedFreeFunc)g_free);
	return t;
}

static gboolean
merge_ranges (GnmRange *a, GnmRange const *b)
{
	if (a->start.row == b->start.row &&
	    a->end.row == b->end.row &&
	    a->end.col + 1 >= b->start.col) {
		// "a" is just left of "b", possibly with overlap
		a->end.col = MAX (a->end.col, b->end.col);
		return TRUE;
	}

	if (a->start.col == b->start.col &&
	    a->end.col == b->end.col &&
	    a->end.row + 1 >= b->start.row) {
		// "a" is just on top of "b", possibly with overlap
		a->end.row = MAX (a->end.row, b->end.row);
		return TRUE;
	}

	if (range_contained (b, a)) {
		// "b" is inside "a"
		return TRUE;
	}

	// Punt.
	return FALSE;
}

static gboolean
try_merge_pair (GArray *arr, unsigned ui1, unsigned ui2)
{
	GnmRange *ra = &g_array_index (arr, GnmRange, ui1);
	GnmRange *rb = &g_array_index (arr, GnmRange, ui2);

	if (merge_ranges (ra, rb)) {
		g_array_remove_index (arr, ui2);
		return TRUE;
	} else
		return FALSE;
}

/**
 * gnm_range_simplify:
 * @arr: (element-type GnmRange) (inout): array of ranges to simplify
 *
 * Simplifies the array of ranges by merging small ranges into larger ones.
 */
void
gnm_range_simplify (GArray *arr)
{
	unsigned ui;

	if (arr->len < 2)
		return;

	g_array_sort (arr, (GCompareFunc) gnm_range_compare);
	// Two cheap passes through the ranges.
	for (ui = arr->len - 1; ui > 0; ui--)
		try_merge_pair (arr, ui - 1, ui);
	for (ui = arr->len - 1; ui > 0; ui--)
		try_merge_pair (arr, ui - 1, ui);

	g_array_sort (arr, (GCompareFunc) gnm_range_compare_alt);
	for (ui = arr->len - 1; ui > 0; ui--)
		try_merge_pair (arr, ui - 1, ui);
}
