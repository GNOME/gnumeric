/*
 * ranges.c: various functions for common operations on cell ranges.
 *
 * Author:
 *   Miguel de Icaza (miguel@gnu.org).
 *   Michael Meeks   (mmeeks@gnu.org).
 *
 */
#include <config.h>
#include <stdlib.h>
#include <stdio.h>
#include <glib.h>
#include <string.h>
#include <string.h>
#include <ctype.h>
#include <gnome.h>
#include "numbers.h"
#include "symbol.h"
#include "str.h"
#include "expr.h"
#include "utils.h"
#include "gnumeric.h"
#include "ranges.h"

/**
 * range_parse:
 * @sheet: the sheet where the cell range is evaluated
 * @range: a range specification (ex: "A1", "A1:C3").
 * @v: a pointer to a Value where the return value is placed.
 *
 * Returns TRUE if the @range was succesfully parsed.  If
 * this is the case, @v will point to a newly allocated
 * Value structure of type VALUE_CELLRANGE
 */
gboolean
range_parse (Sheet *sheet, const char *range, Value **v)
{
	CellRef a, b;
	char *p;
	char *copy;
	char *part_2;
	
	g_return_val_if_fail (range != NULL, FALSE);
	g_return_val_if_fail (v != NULL, FALSE);

	a.col_relative = 0;
	b.col_relative = 0;
	a.row_relative = 0;
	b.row_relative = 0;

	copy = g_strdup (range);
	if ((p = strchr (copy, ':')) != NULL){
		*p = 0;
		part_2 = p+1;
	} else
		part_2 = NULL;
	    
	if (!parse_cell_name (copy, &a.col, &a.row)){
		g_free (copy);
		return FALSE;
	}

	a.sheet = sheet;
	
	if (part_2){
		if (!parse_cell_name (part_2, &b.col, &b.row))
			return FALSE;

		b.sheet = sheet;
	} else
		b = a;

	*v = value_new_cellrange (&a, &b);

	g_free (copy);
	return  TRUE;
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
 * range_list_parse:
 * @sheet: Sheet where the range specification is relatively parsed to
 * @range_spec: a range or list of ranges to parse (ex: "A1", "A1:B1,C2,D2:D4")
 *
 * Parses a list of ranges, relative to the @sheet and returns a list with the
 * results.
 *
 * Returns a GSList containing Values of type VALUE_CELLRANGE, or NULL on failure
 */
GSList *
range_list_parse (Sheet *sheet, const char *range_spec)
{
	
	char *copy, *range_copy, *r;
	GSList *ranges = NULL;
	
	g_return_val_if_fail (sheet != NULL, NULL);
	g_return_val_if_fail (IS_SHEET (sheet), NULL);
	g_return_val_if_fail (range_spec != NULL, NULL);
	
	range_copy = copy = g_strdup (range_spec);

	while ((r = strtok (range_copy, ",")) != NULL){
		Value *v = NULL;
		
		if (!range_parse (sheet, r, &v)){
			range_list_destroy (ranges);
			g_free (copy);
			return NULL;
		}

		ranges = g_slist_prepend (ranges, v);
		range_copy = NULL;
	}
	
	g_free (copy);

	return ranges;
}

/**
 * range_list_foreach_full:
 * 
 * foreach cell in the range, make sure it exists, and invoke the routine
 * @callback on the resulting cell, passing @data to it
 *
 */
void
range_list_foreach_full (GSList *ranges, void (*callback)(Cell *cell, void *data),
			 void *data, gboolean create_empty)
{
	GSList *l;

	{
		static int message_shown;

		if (!message_shown){
			g_warning ("This routine should also iterate"
				   "through the sheets in the ranges");
			message_shown = TRUE;
		}
	}

	for (l = ranges; l; l = l->next){
		Value *value = l->data;
		CellRef a, b;
		int col, row;
		
		g_assert (value->type == VALUE_CELLRANGE);

		a = value->v.cell_range.cell_a;
		b = value->v.cell_range.cell_b;

		for (col = a.col; col <= b.col; col++)
			for (row = a.row; row < b.row; row++){
				Cell *cell;

				if (create_empty)
					cell = sheet_cell_fetch (a.sheet, col, row);
				else
					cell = sheet_cell_get (a.sheet, col, row);
				if (cell)
					(*callback)(cell, data);
			}
	}
}

void
range_list_foreach_all (GSList *ranges,
			void (*callback)(Cell *cell, void *data),
			void *data)
{
	range_list_foreach_full (ranges, callback, data, TRUE);
}

void
range_list_foreach (GSList *ranges, void (*callback)(Cell *cell, void *data),
		    void *data)
{
	range_list_foreach_full (ranges, callback, data, FALSE);
}

/**
 * range_list_foreach_full:
 * @sheet:     The current sheet.
 * @ranges:    The list of ranges.
 * @callback:  The user function
 * @user_data: Passed to callback intact.
 * 
 * Iterates over the ranges calling the callback with the
 * range, sheet and user data set
 **/
void
range_list_foreach_area (Sheet *sheet, GSList *ranges,
			 void (*callback)(Sheet       *sheet,
					  const Range *range,
					  gpointer     user_data),
			 gpointer user_data)
{
	GSList *l;

	g_return_if_fail (sheet != NULL);

	for (l = ranges; l; l = l->next) {
		Value *value = l->data;
		Sheet *s;
		Range   range;
		
		g_assert (value->type == VALUE_CELLRANGE);

		range.start.col = value->v.cell_range.cell_a.col;
		range.start.row = value->v.cell_range.cell_a.row;
		range.end.col   = value->v.cell_range.cell_b.col;
		range.end.row   = value->v.cell_range.cell_b.row;

		s = sheet;
		if (value->v.cell_range.cell_b.sheet)
			s = value->v.cell_range.cell_b.sheet;
		if (value->v.cell_range.cell_a.sheet)
			s = value->v.cell_range.cell_a.sheet;
		callback (s, &range, user_data);
	}
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
	int adx, bdx, ady, bdy;
       
	g_return_val_if_fail (a != NULL, FALSE);
	g_return_val_if_fail (b != NULL, FALSE);
	
	adx = a->end.col - a->start.col;
	bdx = b->end.col - b->start.col;
	ady = a->end.row - a->start.row;
	bdy = b->end.row - b->start.row;

	if ((a->start.col == b->start.col) &&
	    (a->end.col   == b->end.col)) {
		if (a->end.row + 1 == b->start.row ||
		    b->end.row + 1 == a->start.row)
			return TRUE;
		else
			return FALSE;
	} else if ((a->start.row == b->start.row) &&
	    (a->end.row   == b->end.row)) {
		if (a->end.col + 1 == b->start.col ||
		    b->end.col + 1 == a->start.col)
			return TRUE;
		else
			return FALSE;
	}
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

void
range_dump (Range const *src)
{
	/*
	 * keep these as 2 print statements, because
	 * col_name uses a static buffer
	 */
	printf ("%s%d",
		col_name (src->start.col),
		src->start.row + 1);

	if (src->start.col != src->end.col ||
	    src->start.row != src->end.row)
		printf (":%s%d\n",
			col_name (src->end.col),
			src->end.row + 1);
}

/*
 * This is totaly commutative of course; hence the symmetry.
 */
gboolean inline
range_overlap (Range const *a, Range const *b)
{
	if (a->end.row < b->start.row)
		return FALSE;

	if (b->end.row < a->start.row)
		return FALSE;

	if (a->end.col < b->start.col)
		return FALSE;

	if (b->end.col < a->start.col)
		return FALSE;

	return TRUE;
}

/**
 * range_contained:
 * @a: 
 * @b: 
 * 
 * Is a totaly contained by b
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
 * Return value: 
 **/
inline GList *
range_split_ranges (const Range *hard, const Range *soft,
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
	gboolean tl, tr, bl, br; /* Top, Bottom, Left, Right */

	if (copy_fn)
		middle = copy_fn (soft);
	else {
		middle = g_new (Range, 1);
		*middle = *soft;
	}

	tl = range_contains (soft, hard->start.col, hard->start.row);
	tr = range_contains (soft, hard->end.col,   hard->start.row);
	bl = range_contains (soft, hard->start.col, hard->end.row);
	br = range_contains (soft, hard->end.col,   hard->end.row);

	/* Left */
	if (tl || bl) {
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
		} /* else shared edge */
		middle->start.col = hard->start.col;
	} 

	/* Right */
	if (tr || br) {
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
		} /* else shared edge */
		middle->end.col = hard->end.col;
	} 

	/* Top */
	if (tl || tr)
		middle->start.row = hard->start.row;
	if (tl && tr) {
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
		} /* else shared edge */
	} else if (tl) {
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
		} /* else shared edge */
	} else if (tr) {
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
		} /* else shared edge */
	}

	/* Bottom */
	if (bl || br)
		middle->end.row = hard->end.row;
	if (bl && br) {
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
		} /* else shared edge */
	} else if (bl) {
		if (hard->start.row > soft->start.row) {
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
		} /* else shared edge */
	} else if (br) {
		if (hard->start.row > soft->start.row) {
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
		} /* else shared edge */
	}

	return g_list_prepend (split, middle);
}

Range *
range_copy (const Range *a)
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
range_fragment (const Range *a, const Range *b)
{
	GList *split, *ans = NULL;
	
	split  = range_split_ranges (a, b, NULL);
	ans = g_list_concat (ans, split);
	
	split  = range_split_ranges (b, a, NULL);
	if (split) {
		g_free (split->data);
		split  = g_list_remove (split, split->data);
	}
	ans = g_list_concat (ans, split);

	return ans;
}

/**
 * range_fragment_list:
 * @ra: A list of possibly overlapping ranges.
 * 
 *  Converts the ranges into non-overlapping sub-ranges.
 * 
 * Return value: new list of fully overlapping ranges.
 **/
GList *
range_fragment_list (const GList *ra)
{
	return range_fragment_list_clip (ra, NULL);
}

GList *
range_fragment_list_clip (const GList *ra, const Range *clip)
{
	GList *ranges = NULL;
	GList *a; /* Order n*n: ugly */

	for (a = (GList *)ra; a; a = g_list_next (a)) {
		GList *b;
		gboolean done_split = FALSE;

		b = g_list_next (a);

		while (b) {
			GList *next = g_list_next (b);

			if (! range_equal   (a->data, b->data)
			    & range_overlap (a->data, b->data)) {
				GList *split;

				split = range_fragment (a->data, b->data);
				if (split)
					ranges = g_list_concat (ranges, split);
				done_split = TRUE;
			}
			b = next;
		}
		if (!done_split)
			ranges = g_list_append (ranges, range_copy (a->data));
	}
	if (ranges && clip) {
		GList *l, *next;
		for (l = ranges; l; l = next) {
			next = l->next;
			if (range_overlap (l->data, clip))
				continue;
			g_free (l->data);
			ranges = g_list_remove (ranges, l->data);
		}
	}

	return ranges;
}

void
range_fragment_free (GList *fragments)
{
	GList *l = fragments;

	for (l = fragments; l; l = g_list_next (l))
		g_free (l->data);

	g_list_free (fragments);
}

void
range_clip (Range *clipped, Range const *master,
	    Range const *slave)
{
	g_return_if_fail (slave != NULL);
	g_return_if_fail (master != NULL);
	g_return_if_fail (clipped != NULL);

	*clipped = *slave;
	g_warning ("Unimplemented");
}

gboolean
range_is_singleton (Range const *r)
{
	return r->start.col == r->end.col && r->start.row == r->end.row;
}

static void
range_style_apply_cb (Sheet *sheet, const Range *range, gpointer user_data)
{
	mstyle_ref ((MStyle *)user_data);
	sheet_style_attach (sheet, *range, (MStyle *)user_data);
}

void
ranges_set_style (Sheet *sheet, GSList *ranges, MStyle *mstyle)
{
	range_list_foreach_area (sheet, ranges,
				 range_style_apply_cb, mstyle);
	mstyle_unref (mstyle);
}

/**
 * range_translate:
 * @range: 
 * @col_offset: 
 * @row_offset: 
 * 
 * Translate the range, returns TRUE if the range is
 * still valid ( on the sheet ), otherwise FALSE.
 * will clip the result range to the sheet dimensions hence
 * does not preserve area.
 * If we return FALSE the Range is undefined.
 * 
 * Return value: range still valid.
 **/
gboolean
range_translate (Range *range, int col_offset,
		 int row_offset)
{
	if (range->end.col   + col_offset < 0 ||
	    range->start.col + col_offset >= SHEET_MAX_COLS)
		return FALSE;

	if (range->end.row   + row_offset < 0 ||
	    range->start.row + row_offset >= SHEET_MAX_ROWS)
		return FALSE;

	range->start.col += MIN (col_offset, SHEET_MAX_COLS - 1);
	range->end.col   += MIN (col_offset, SHEET_MAX_COLS - 1);
	range->start.row += MIN (row_offset, SHEET_MAX_ROWS - 1);
	range->end.row   += MIN (row_offset, SHEET_MAX_ROWS - 1);

	return TRUE;
}

/**
 * range_expand:
 * @range: the range to expand
 * @d_tlx: top left column delta
 * @d_tly: top left row delta
 * @d_brx: bottom right column delta
 * @d_bry: bottom right row delta
 * 
 * Attempts to expand a ranges area
 * 
 * Return value: TRUE if we can expand, FALSE if not enough room.
 **/
gboolean
range_expand (Range *range, int d_tlx, int d_tly, int d_brx, int d_bry)
{
	int t;

	t = range->start.col + d_tlx;
	if (t < 0 ||
	    t >= SHEET_MAX_COLS)
		return FALSE;
	range->start.col = t;

	t = range->start.row + d_tly;
	if (t < 0 ||
	    t >= SHEET_MAX_ROWS)
		return FALSE;
	range->start.row = t;

	t = range->end.col + d_brx;
	if (t < 0 ||
	    t >= SHEET_MAX_COLS)
		return FALSE;
	range->end.col = t;

	t = range->end.row + d_bry;
	if (t < 0 ||
	    t >= SHEET_MAX_ROWS)
		return FALSE;
	range->end.row = t;
	
	return TRUE;
}

