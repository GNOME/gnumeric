/* vim: set sw=8: */
/*
 * ranges.c: various functions for common operations on cell ranges.
 *
 * Author:
 *   Miguel de Icaza (miguel@gnu.org).
 *   Michael Meeks   (mmeeks@gnu.org).
 */
#include <config.h>
#include "ranges.h"
#include "numbers.h"
#include "expr.h"
#include "expr-name.h"
#include "sheet.h"
#include "sheet-style.h"
#include "parse-util.h"
#include "value.h"
#include "cell.h"
#include "style.h"
#include "workbook.h"

#include <stdlib.h>
#include <stdio.h>
#include <glib.h>
#include <string.h>
#include <string.h>
#include <ctype.h>

#undef RANGE_DEBUG

Range *
range_init (Range *r, int start_col, int start_row,
	    int end_col, int end_row)
{
	g_return_val_if_fail (r != NULL, r);

	r->start.col = start_col;
	r->start.row = start_row;
	r->end.col   = end_col;
	r->end.row   = end_row;

	return r;
}

Range *
range_init_full_sheet (Range *r)
{
	r->start.col = 0;
	r->start.row = 0;
	r->end.col = SHEET_MAX_COLS - 1;
	r->end.row = SHEET_MAX_ROWS - 1;
	return r;
}

/**
 * range_parse:
 * @sheet: the sheet where the cell range is evaluated
 * @range: a range specification (ex: "A1", "A1:C3").
 * @strict: if FALSE, allow extra characters after range text.
 *
 * Returns a (Value *) of type VALUE_CELLRANGE if the @range was
 * succesfully parsed or NULL on failure.
 */
Value *
range_parse (Sheet *sheet, char const *range, gboolean strict)
{
	CellRef a, b;
	int n_read;

	g_return_val_if_fail (range != NULL, FALSE);

	a.col_relative = 0;
	b.col_relative = 0;
	a.row_relative = 0;
	b.row_relative = 0;

	if (!parse_cell_name (range, &a.col, &a.row, FALSE, &n_read))
		return FALSE;

	a.sheet = sheet;

	if (range[n_read] == ':') {
		if (!parse_cell_name (&range[n_read+1], &b.col, &b.row, strict, NULL))
			return FALSE;
		b.sheet = sheet;
	} else if (strict && range[n_read])
		return FALSE;
	else
		b = a;

	/*
	 * We can dummy out the calling cell because we know that both
	 * refs use the same mode.  This will handle inversions.
	 */
	return value_new_cellrange (&a, &b, 0, 0);
}

/* Pulled from dialog-analysis-tools.c
 * Should be merged with range_parse
 */
gboolean
parse_range (char const *text, int *start_col, int *start_row,
	     int *end_col, int *end_row)
{
	int len;

	if (!parse_cell_name (text, start_col, start_row, FALSE, &len))
		return FALSE;
	if (text [len] == '\0') {
		*end_col = *start_col;
		*end_row = *start_row;
		return TRUE;
	}
	return (text[len] == ':' &&
		parse_cell_name (text + len + 1, end_col, end_row, TRUE, NULL));
}

/**
 * range_list_destroy:
 * @ranges: a list of value ranges to destroy.
 *
 * Destroys a list of ranges returned from parse_cell_range_list
 */
void
range_list_destroy (GSList *ranges)
{
	GSList *l;

	for (l = ranges; l; l = l->next){
		Value *value = l->data;

		value_release (value);
	}
	g_slist_free (ranges);
}


/**
 * range_adjacent:
 * @a: First range
 * @b: Second range
 *
 * Detects whether a range of similar size is adjacent
 * to the other range. Similarity is determined by having
 * a shared side of equal length. NB. this will clearly
 * give odd results for overlapping regions.
 *
 * Return value: if they share a side of equal length
 **/
gboolean
range_adjacent (Range const *a, Range const *b)
{
	g_return_val_if_fail (a != NULL, FALSE);
	g_return_val_if_fail (b != NULL, FALSE);

	if ((a->start.col == b->start.col) &&
	    (a->end.col   == b->end.col))
		return (a->end.row + 1 == b->start.row ||
			b->end.row + 1 == a->start.row);

	if ((a->start.row == b->start.row) &&
	    (a->end.row   == b->end.row))
		return (a->end.col + 1 == b->start.col ||
			b->end.col + 1 == a->start.col);

	return FALSE;
}

/**
 * range_merge:
 * @a: Range a.
 * @b: Range b.
 *
 * This routine coalesces two adjacent regions, eg.
 * (A1, B1) would return A1:B1 or (A1:B2, C1:D2)) would
 * give A1:D2. NB. it is imperative that the regions are
 * actualy adjacent or unexpected results will ensue.
 *
 * Fully commutative.
 *
 * Return value: the merged range.
 **/
Range
range_merge (Range const *a, Range const *b)
{
	Range ans;

	ans.start.col = 0;
	ans.start.row = 0;
	ans.end.col   = 0;
	ans.end.row   = 0;

	g_return_val_if_fail (a != NULL, ans);
	g_return_val_if_fail (b != NULL, ans);

/*      Useful perhaps but kills performance */
/*	g_return_val_if_fail (range_adjacent (a, b), ans); */

	if (a->start.row < b->start.row) {
		ans.start.row = a->start.row;
		ans.end.row   = b->end.row;
	} else {
		ans.start.row = b->start.row;
		ans.end.row   = a->end.row;
	}

	if (a->start.col < b->start.col) {
		ans.start.col = a->start.col;
		ans.end.col   = b->end.col;
	} else {
		ans.start.col = b->start.col;
		ans.end.col   = a->end.col;
	}

	return ans;
}

char const *
range_name (Range const *src)
{
	static char buffer[(2 + 4 * sizeof (long)) * 2 + 1];

	if (src->start.col != src->end.col ||
	    src->start.row != src->end.row) {
		char *name_col = g_strdup (col_name (src->start.col));
		char *name_row = g_strdup (row_name (src->start.row));
		sprintf (buffer, "%s%s:%s%s", name_col, name_row,
			 col_name (src->end.col), row_name (src->end.row));
		g_free (name_row);
		g_free (name_col);
	} else {
		sprintf (buffer, "%s%s",
			 col_name (src->start.col),
			 row_name (src->start.row));
	}

	return buffer;
}

/**
 * range_has_header:
 * @sheet: Sheet to check
 * @src: Range to check
 * @top: Flag
 *
 * This routine takes a sheet and a range and checks for a header row
 * in the range.  If top is true it looks for a header row from the top
 * and if false it looks for a header col from the left
 *
 * Return value: Whether or not the range has a header
 **/
gboolean
range_has_header (Sheet const *sheet, Range const *src, gboolean top)
{
	Cell *ca, *cb;
	Value *valuea, *valueb;
	MStyle *stylea, *styleb;
	int length, i;

	/* There is only one row or col */
	if (top) {
		if (src->end.row <= src->start.row) {
			return FALSE;
		}
		length = src->end.col - src->start.col + 1;
	} else {
		if (src->end.col <= src->start.col) {
			return FALSE;
		}
		length = src->end.row - src->start.row + 1;
	}

	for (i = 0; i<length; i++) {
		if (top) {
			ca = sheet_cell_get (sheet, src->start.col + i,
					     src->start.row);
			cb = sheet_cell_get (sheet, src->start.col + i,
					     src->start.row + 1);
		} else {
			ca = sheet_cell_get (sheet, src->start.col,
					     src->start.row + i);
			cb = sheet_cell_get (sheet, src->start.col + 1,
					     src->start.row + i);
		}


		if (!ca || !cb) {
			continue;
		}

		/* Look for value differences */
		valuea = ca->value;
		valueb = cb->value;

		if (VALUE_IS_NUMBER (valuea)) {
			if (!VALUE_IS_NUMBER (valueb)) {
				return TRUE;
			}
		} else {
			if (valuea->type != valueb->type) {
				return TRUE;
			}
		}

		/* Look for style differences */
		stylea = cell_get_mstyle (ca);
		styleb = cell_get_mstyle (cb);

		if (!mstyle_equal (stylea, styleb))
			return TRUE;
	}

	return FALSE;
}

void
range_dump (Range const *src, char const *suffix)
{
	/*
	 * keep these as 2 print statements, because
	 * col_name and row_name use a static buffer
	 */
	fprintf (stderr, "%s%s",
		col_name (src->start.col),
		row_name (src->start.row));

	if (src->start.col != src->end.col ||
	    src->start.row != src->end.row)
		fprintf (stderr, ":%s%s",
			col_name (src->end.col),
			row_name (src->end.row));
	fprintf (stderr, suffix);
}

#ifdef RANGE_DEBUG
static void
ranges_dump (GList *l, char const *txt)
{
	fprintf (stderr, "%s: ", txt);
	for (; l; l = l->next) {
		range_dump (l->data, "");
		if (l->next)
			fprintf (stderr, ", ");
	}
	fprintf (stderr, "\n");
}
#endif

/**
 * range_contained:
 * @a:
 * @b:
 *
 * Is @a totaly contained by @b
 *
 * Return value:
 **/
gboolean
range_contained (Range const *a, Range const *b)
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
 * @copy_fn: the function used to create the copies or NULL.
 *
 * Splits soft into several chunks, and returns the still
 * overlapping remainder of soft as the first list item
 * ( the central region in the pathalogical case ).
 *
 * Return value: A list of fragments.
 **/
GList *
range_split_ranges (Range const *hard, Range const *soft,
		    RangeCopyFn copy_fn)
{
	/*
	 * There are lots of cases so think carefully.
	 *
	 * Original Methodology ( approximately )
	 *	a) Get a vertex: is it contained ?
	 *	b) Yes: split so it isn't
	 *	c) Continue for all verticees.
	 *
	 * NB. We prefer to have long columns at the expense
	 *     of long rows.
	 */
	GList *split  = NULL;
	Range *middle, *sp;
	gboolean split_left  = FALSE;
	gboolean split_right = FALSE;

	g_return_val_if_fail (range_overlap (hard, soft), NULL);

	if (copy_fn)
		middle = copy_fn (soft);
	else {
		middle = g_new (Range, 1);
		*middle = *soft;
	}

	/* Split off left entirely */
	if (hard->start.col > soft->start.col) {
		if (copy_fn)
			sp = copy_fn (middle);
		else
			sp = g_new (Range, 1);
		sp->start.col = soft->start.col;
		sp->start.row = soft->start.row;
		sp->end.col   = hard->start.col - 1;
		sp->end.row   = soft->end.row;
		split = g_list_prepend (split, sp);

		middle->start.col = hard->start.col;
		split_left = TRUE;
	} /* else shared edge */

	/* Split off right entirely */
	if (hard->end.col < soft->end.col) {
		if (copy_fn)
			sp = copy_fn (middle);
		else
			sp = g_new (Range, 1);
		sp->start.col = hard->end.col + 1;
		sp->start.row = soft->start.row;
		sp->end.col   = soft->end.col;
		sp->end.row   = soft->end.row;
		split = g_list_prepend (split, sp);

		middle->end.col = hard->end.col;
		split_right = TRUE;
	}  /* else shared edge */

	/* Top */
	if (split_left && split_right) {
		if (hard->start.row > soft->start.row) {
			/* The top middle bit */
			if (copy_fn)
				sp = copy_fn (middle);
			else
				sp = g_new (Range, 1);
			sp->start.col = hard->start.col;
			sp->start.row = soft->start.row;
			sp->end.col   = hard->end.col;
			sp->end.row   = hard->start.row - 1;
			split = g_list_prepend (split, sp);

			middle->start.row = hard->start.row;
		} /* else shared edge */
	} else if (split_left) {
		if (hard->start.row > soft->start.row) {
			/* The top middle + right bits */
			if (copy_fn)
				sp = copy_fn (middle);
			else
				sp = g_new (Range, 1);
			sp->start.col = hard->start.col;
			sp->start.row = soft->start.row;
			sp->end.col   = soft->end.col;
			sp->end.row   = hard->start.row - 1;
			split = g_list_prepend (split, sp);

			middle->start.row = hard->start.row;
		} /* else shared edge */
	} else if (split_right) {
		if (hard->start.row > soft->start.row) {
			/* The top middle + left bits */
			if (copy_fn)
				sp = copy_fn (middle);
			else
				sp = g_new (Range, 1);
			sp->start.col = soft->start.col;
			sp->start.row = soft->start.row;
			sp->end.col   = hard->end.col;
			sp->end.row   = hard->start.row - 1;
			split = g_list_prepend (split, sp);

			middle->start.row = hard->start.row;
		} /* else shared edge */
	} else {
		if (hard->start.row > soft->start.row) {
			/* Hack off the top bit */
			if (copy_fn)
				sp = copy_fn (middle);
			else
				sp = g_new (Range, 1);
			sp->start.col = soft->start.col;
			sp->start.row = soft->start.row;
			sp->end.col   = soft->end.col;
			sp->end.row   = hard->start.row - 1;
			split = g_list_prepend (split, sp);

			middle->start.row = hard->start.row;
		} /* else shared edge */
	}

	/* Bottom */
	if (split_left && split_right) {
		if (hard->end.row < soft->end.row) {
			/* The bottom middle bit */
			if (copy_fn)
				sp = copy_fn (middle);
			else
				sp = g_new (Range, 1);
			sp->start.col = hard->start.col;
			sp->start.row = hard->end.row + 1;
			sp->end.col   = hard->end.col;
			sp->end.row   = soft->end.row;
			split = g_list_prepend (split, sp);

			middle->end.row = hard->end.row;
		} /* else shared edge */
	} else if (split_left) {
		if (hard->end.row < soft->end.row) {
			/* The bottom middle + right bits */
			if (copy_fn)
				sp = copy_fn (middle);
			else
				sp = g_new (Range, 1);
			sp->start.col = hard->start.col;
			sp->start.row = hard->end.row + 1;
			sp->end.col   = soft->end.col;
			sp->end.row   = soft->end.row;
			split = g_list_prepend (split, sp);

			middle->end.row = hard->end.row;
		} /* else shared edge */
	} else if (split_right) {
		if (hard->end.row < soft->end.row) {
			/* The bottom middle + left bits */
			if (copy_fn)
				sp = copy_fn (middle);
			else
				sp = g_new (Range, 1);
			sp->start.col = soft->start.col;
			sp->start.row = hard->end.row + 1;
			sp->end.col   = hard->end.col;
			sp->end.row   = soft->end.row;
			split = g_list_prepend (split, sp);

			middle->end.row = hard->end.row;
		} /* else shared edge */
	} else {
		if (hard->end.row < soft->end.row) {
			/* Hack off the bottom bit */
			if (copy_fn)
				sp = copy_fn (middle);
			else
				sp = g_new (Range, 1);
			sp->start.col = soft->start.col;
			sp->start.row = hard->end.row + 1;
			sp->end.col   = soft->end.col;
			sp->end.row   = soft->end.row;
			split = g_list_prepend (split, sp);

			middle->end.row = hard->end.row;
		} /* else shared edge */
	}

	return g_list_prepend (split, middle);
}

/**
 * range_dup:
 * @a: Source range to copy
 *
 * Copies the @a range.
 *
 * Return value: A copy of the Range.
 */
Range *
range_dup (Range const *a)
{
	Range *r = g_new (Range, 1);
	*r = *a;
	return r;
}

/**
 * range_fragment:
 * @a: Range a
 * @b: Range b
 * @copy_fn: Optional copy fn.
 *
 * Fragments the ranges into totaly overlapping regions,
 * optional copy_fn ( NULL ) for default, copies the ranges.
 * NB. commutative.
 *
 * Return value: A list of fragmented ranges or at minimum
 * simply a and b.
 **/
GList *
range_fragment (Range const *a, Range const *b)
{
	GList *split, *ans = NULL;

	split = range_split_ranges (a, b, NULL);
	ans   = g_list_concat (ans, split);

	split = range_split_ranges (b, a, NULL);
	if (split) {
		g_free (split->data);
		split = g_list_remove (split, split->data);
	}
	ans = g_list_concat (ans, split);

	return ans;
}

void
range_fragment_free (GList *fragments)
{
	GList *l = fragments;

	for (l = fragments; l; l = g_list_next (l))
		g_free (l->data);

	g_list_free (fragments);
}

/**
 * range_intersection:
 * @r: intersection range
 * @a: range a
 * @b: range b
 *
 * This computes the intersection of two ranges; on a Venn
 * diagram this would be A (upside down U) B.
 * If the ranges do not intersect, false is returned an the
 * values of r are unpredictable.
 *
 * NB. totally commutative
 *
 * Return value: True if the ranges intersect, false otherwise
 **/
gboolean
range_intersection (Range *r, Range const *a, Range const *b)
{
	g_return_val_if_fail (range_overlap (a, b), FALSE);

	r->start.col = MAX (a->start.col, b->start.col);
	r->start.row = MAX (a->start.row, b->start.row);

	r->end.col = MIN (a->end.col, b->end.col);
	r->end.row = MIN (a->end.row, b->end.row);

	return TRUE;
}

/**
 * range_normalize:
 * @a: a range
 *
 * Ensures that start <= end for rows and cols.
 **/
void
range_normalize (Range *src)
{
	int tmp;

	tmp = src->end.col;
	if (src->start.col >= tmp) {
		src->end.col = src->start.col;
		src->start.col = tmp;
	}
	tmp = src->end.row;
	if (src->start.row >= tmp) {
		src->end.row = src->start.row;
		src->start.row = tmp;
	}
}

/**
 * range_trim:
 * @sheet: sheet cells are contained on
 * @range: range to trim empty cells from
 * @cols: trim from right, vs bottom
 * 
 * This removes empty rows/cols from the
 * right hand or bottom edges of the range
 * depending on the value of @location.
 * 
 * WARNING! FOR LARGE RANGES THIS IS EXPENSIVE!
 * 
 * Return value: TRUE if the range was totally empty, else FALSE.
 **/
gboolean
range_trim (Sheet const *sheet, Range *range, gboolean cols)
{
	int start, *move;

	/* Setup the pointers to the fields which will
	 * be changed (to remove the empty cells)
	 */
	if (cols) {
		start = range->start.col;
		move = &range->end.col;
		range->start.col = *move;
	} else {
		start = range->start.row;
		move = &range->end.row;
		range->start.row = *move;
	}

	for (; *move >= start ; (*move)--)
		if (!sheet_is_region_empty ((Sheet *)sheet, range))
			break;

	if (cols)
		range->start.col = start;
	else
		range->start.row = start;

	return *move < start;
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
 * it must return a Range.
 *
 * Return value: the union
 **/
Range
range_union (Range const *a, Range const *b)
{
	Range ans;

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

gboolean
range_is_singleton (Range const *r)
{
	return r->start.col == r->end.col && r->start.row == r->end.row;
}

/**
 * range_is_full:
 * @r: the range.
 *
 *  This determines whether @r completely spans a sheet
 * in the specified dimension.
 *
 * Return value: TRUE if it is infinite else FALSE
 **/
gboolean
range_is_full (Range const *r, gboolean is_cols)
{
	if (is_cols)
		return (r->start.col <= 0 && r->end.col >= SHEET_MAX_COLS - 1);
	else
		return (r->start.row <= 0 && r->end.row >= SHEET_MAX_ROWS - 1);
}

/**
 * range_is_infinite:
 * @r: the range.
 *
 *  This determines whether @r completely spans a sheet
 * in either dimension ( semi-infinite )
 *
 * Return value: TRUE if it is infinite else FALSE
 **/
gboolean
range_is_infinite (Range const *r)
{
	return range_is_full (r, TRUE) || range_is_full (r, TRUE);
}

/**
 * range_clip_to_finite :
 * @range :
 * @sheet :
 *
 * Clip the range to the area of the sheet with content.
 * WARNING THIS IS EXPENSIVE!
 */
void
range_clip_to_finite (Range *range, Sheet *sheet)
{
	Range extent;

	/* FIXME : This seems expensive.  We should see if there is a faster
	 * way of doing this.  possibly using a flag for content changes, and
	 * using the current values as a cache
	 */
	extent = sheet_get_extent (sheet, FALSE);
       	if (range->end.col >= SHEET_MAX_COLS - 2)
		range->end.col = extent.end.col;
	if (range->end.row >= SHEET_MAX_ROWS - 2)
		range->end.row = extent.end.row;
}

int
range_width (Range const *r)
{
	g_return_val_if_fail (r != NULL, 0);
	return ABS (r->end.col - r->start.col) + 1;
}

int
range_height (Range const *r)
{
	g_return_val_if_fail (r != NULL, 0);
	return ABS (r->end.row - r->start.row) + 1;
}

/**
 * range_translate:
 * @range:
 * @col_offset:
 * @row_offset:
 *
 * Translate the range and return TRUE if it is invalidated.
 *
 * return TRUE if the range is no longer valid.
 **/
gboolean
range_translate (Range *range, int col_offset, int row_offset)
{
	range->start.col += col_offset;
	range->end.col   += col_offset;
	range->start.row += row_offset;
	range->end.row   += row_offset;

	/* check for completely out of bounds */
	if (range->start.col >= SHEET_MAX_COLS || range->start.col < 0 ||
	    range->start.row >= SHEET_MAX_ROWS || range->start.row < 0 ||
	    range->end.col >= SHEET_MAX_COLS || range->end.col < 0 ||
	    range->end.row >= SHEET_MAX_ROWS || range->end.row < 0)
		return TRUE;

	return FALSE;
}

/**
 * range_ensure_sanity :
 * @range : the range to check
 *
 * Silently clip a range to ensure that it does not contain areas
 * outside the valid bounds.  Does NOT fix inverted ranges.
 */
void
range_ensure_sanity (Range *range)
{
	if (range->start.col < 0)
		range->start.col = 0;
	if (range->end.col >= SHEET_MAX_COLS)
		range->end.col = SHEET_MAX_COLS-1;
	if (range->start.row < 0)
		range->start.row = 0;
	if (range->end.row >= SHEET_MAX_ROWS)
		range->end.row = SHEET_MAX_ROWS-1;
}

/**
 * range_is_sane :
 * @range : the range to check
 *
 * Generate warnings if the range is out of bounds or inverted.
 */
gboolean
range_is_sane (Range const *range)
{
	g_return_val_if_fail (range != NULL, FALSE);
	g_return_val_if_fail (range->start.col >= 0, FALSE);
	g_return_val_if_fail (range->end.col >= range->start.col, FALSE);
	g_return_val_if_fail (range->end.col < SHEET_MAX_COLS, FALSE);
	g_return_val_if_fail (range->start.row >= 0, FALSE);
	g_return_val_if_fail (range->end.row >= range->start.row, FALSE);
	g_return_val_if_fail (range->end.row < SHEET_MAX_ROWS, FALSE);

	return TRUE;
}

/**
 * range_transpose:
 * @range: The range.
 * @boundary: The box to transpose inside
 *
 *   Effectively mirrors the ranges in 'boundary' around a
 * leading diagonal projected from offset.
 *
 * Return value: whether we clipped the range.
 **/
gboolean
range_transpose (Range *range, CellPos const *origin)
{
	gboolean clipped = FALSE;
	Range    src;
	int      t;

	g_return_val_if_fail (range != NULL, TRUE);

	src = *range;

	/* Start col */
	t = origin->col + (src.start.row - origin->row);
	if (t > SHEET_MAX_COLS - 1) {
		clipped = TRUE;
		range->start.col = SHEET_MAX_COLS - 1;
	} else if (t < 0) {
		clipped = TRUE;
		range->start.col = 0;
	}
		range->start.col = t;

	/* Start row */
	t = origin->row + (src.start.col - origin->col);
	if (t > SHEET_MAX_COLS - 1) {
		clipped = TRUE;
		range->start.row = SHEET_MAX_ROWS - 1;
	} else if (t < 0) {
		clipped = TRUE;
		range->start.row = 0;
	}
		range->start.row = t;


	/* End col */
	t = origin->col + (src.end.row - origin->row);
	if (t > SHEET_MAX_COLS - 1) {
		clipped = TRUE;
		range->end.col = SHEET_MAX_COLS - 1;
	} else if (t < 0) {
		clipped = TRUE;
		range->end.col = 0;
	}
		range->end.col = t;

	/* End row */
	t = origin->row + (src.end.col - origin->col);
	if (t > SHEET_MAX_COLS - 1) {
		clipped = TRUE;
		range->end.row = SHEET_MAX_ROWS - 1;
	} else if (t < 0) {
		clipped = TRUE;
		range->end.row = 0;
	}
		range->end.row = t;

	g_assert (range_valid (range));

	return clipped;
}

GlobalRange *
global_range_new (Sheet *sheet, Range const *r)
{
	GlobalRange *gr = g_new0 (GlobalRange, 1);

	g_return_val_if_fail (IS_SHEET (sheet), NULL);
	g_return_val_if_fail (r != NULL, NULL);
	
	gr->sheet = sheet;
	gr->range = *r;

	return gr;
}

void
global_range_free (GlobalRange *gr)
{
	g_return_if_fail (gr != NULL);
	
	g_free (gr);
}

gboolean
global_range_overlap (GlobalRange const *a, GlobalRange const *b)
{
	g_return_val_if_fail (a != NULL, FALSE);
	g_return_val_if_fail (b != NULL, FALSE);

	if (a->sheet == b->sheet && range_overlap (&a->range, &b->range))
		return TRUE;
		
	return FALSE;
}

GlobalRange *
global_range_dup (GlobalRange const *src)
{
	g_return_val_if_fail (src != NULL, NULL);

	return global_range_new (src->sheet, &src->range);
}

/**
 * global_range_parse:
 * @sheet: the sheet where the cell range is evaluated. This really only needed if
 *         the range given does not include a sheet specification.
 * @str: a range specification (ex: "A1", "A1:C3", "Sheet1!A1:C3).
 *
 * Returns a (Value *) of type VALUE_CELLRANGE if the @range was
 * succesfully parsed or NULL on failure.
 */
Value *
global_range_parse (Sheet *sheet, char const *str)
{
	Value    *value = NULL;
	ExprTree *expr = NULL;
	ParsePos  pp;

	parse_pos_init (&pp, sheet->workbook, sheet, 0, 0);
	expr = expr_parse_string (str, &pp, NULL, NULL);
	if (expr != NULL)  {
		value = expr_tree_get_range (expr);
		expr_tree_unref (expr);
	}
	return value;
}

/**
 * global_range_list_parse:
 * @sheet: Sheet where the range specification is relatively parsed to
 * @range_spec: a range or list of ranges to parse (ex: "A1", "A1:B1,C2,Sheet2!D2:D4")
 *
 * Parses a list of ranges, relative to the @sheet and returns a list with the
 * results.
 *
 * Returns a GSList containing Values of type VALUE_CELLRANGE, or NULL on failure
 */
GSList *
global_range_list_parse (Sheet *sheet, char const *range_spec)
{

	char *copy, *range_copy, *r;
	GSList *ranges = NULL;

	g_return_val_if_fail (IS_SHEET (sheet), NULL);
	g_return_val_if_fail (range_spec != NULL, NULL);

	range_copy = copy = g_strdup (range_spec);

	while ((r = strtok (range_copy, ",")) != NULL){
		Value *v;

		v = global_range_parse (sheet, r);
		if (!v){
			range_list_destroy (ranges);
			g_free (copy);
			return NULL;
		}

		ranges = g_slist_prepend (ranges, v);
		range_copy = NULL;
	}

	g_free (copy);

	return g_slist_reverse (ranges);
}

Value *
global_range_list_foreach (GSList *gr_list, EvalPos const *ep,
			   gboolean	 only_existing,
			   ForeachCellCB handler,
			   gpointer	 closure)
{
	Value *v;
	for (; gr_list != NULL; gr_list = gr_list->next) {
		v = workbook_foreach_cell_in_range (ep, gr_list->data,
			only_existing, handler, closure);
		if (v != NULL)
			return v;
	}

	return NULL;
}


/**
 * global_range_contained:
 * @a:
 * @b:
 *
 * return true if a is containde in b
 * we do not handle 3d ranges
 **/
gboolean  
global_range_contained (Value *a, Value *b)
{
	if ((a->type != VALUE_CELLRANGE) || (b->type != VALUE_CELLRANGE)) 
		return FALSE;

	if (a->v_range.cell.a.sheet != a->v_range.cell.b.sheet)
		return FALSE;

	if ((a->v_range.cell.a.sheet != b->v_range.cell.a.sheet) 
	    && (a->v_range.cell.a.sheet != b->v_range.cell.b.sheet))
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
