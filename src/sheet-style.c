/*
 * Sheet-Style.c:  Implements the sheet's style optimizations.
 *
 * Author:
 *  Michael Meeks <mmeeks@gnu.org>
 *
 */

/*
 * This module make look small, but its going to become pretty clever.
 */

#include <config.h>
#include <ctype.h>
#include <gnome.h>
#include <string.h>
#include "gnumeric.h"
#include "gnumeric-sheet.h"
#include "utils.h"
#include "gnumeric-util.h"
#include "eval.h"
#include "number-match.h"
#include "format.h"
#include "selection.h"
#include "ranges.h"
#include "mstyle.h"
#include "main.h"

#define STYLE_DEBUG (style_debugging > 2)

struct _SheetStyleData {
	GList      *style_list;
	GHashTable *cell_cache;
};

#define STYLE_LIST(s)  (((SheetStyleData *)(s)->style_data)->style_list)
#define STYLE_CACHE(s) (((SheetStyleData *)(s)->style_data)->cell_cache)

/*
 * For xml-io only.
 */
GList *
sheet_get_style_list (Sheet *sheet)
{
	return STYLE_LIST (sheet);
}

static guint
evalpos_hash (gconstpointer key)
{
	const EvalPosition *ep = (const EvalPosition *) key;

	/* FIXME: what about sheet ? */
	return (ep->eval_col << 8) ^ ep->eval_row;
}

static gint
evalpos_compare (EvalPosition const * a, EvalPosition const * b)
{
	return (a->sheet == b->sheet &&
		a->eval_row == b->eval_row &&
		a->eval_col == b->eval_col);
}

static MStyle *
sheet_style_cache_lookup (const Sheet *sheet, int col, int row)
{
	EvalPosition ep;

	ep.sheet    = (Sheet *)sheet;
	ep.eval_col = col;
	ep.eval_row = row;

	if (!STYLE_CACHE (sheet))
		return NULL;

	return g_hash_table_lookup (STYLE_CACHE (sheet), &ep);
}

static gboolean
scache_remove (EvalPosition *key, MStyle *mstyle, gpointer dummy)
{
	g_return_val_if_fail (key    != NULL, FALSE);
	g_return_val_if_fail (mstyle != NULL, FALSE);

	g_free (key);
	mstyle_unref (mstyle);
	return TRUE;
}

static void
sheet_style_cache_flush (Sheet *sheet)
{
	if (STYLE_CACHE (sheet)) {
		g_hash_table_foreach_remove (STYLE_CACHE (sheet),
					     (GHRFunc)scache_remove, NULL);
		g_hash_table_destroy (STYLE_CACHE (sheet));
		STYLE_CACHE (sheet) = NULL;
	}
}

static void
sheet_style_cache_add (Sheet *sheet, int col, int row,
		       MStyle *mstyle)
{
	EvalPosition  *ep;

	if (STYLE_CACHE (sheet) &&
	    g_hash_table_size (STYLE_CACHE (sheet)) > 1024)
		sheet_style_cache_flush (sheet);

	if (!STYLE_CACHE (sheet))
		STYLE_CACHE (sheet) = g_hash_table_new ((GHashFunc)evalpos_hash,
							(GCompareFunc)evalpos_compare);

	ep = g_new (EvalPosition, 1);
	ep->sheet    = (Sheet *)sheet;
	ep->eval_col = col;
	ep->eval_row = row;
	mstyle_ref (mstyle);

	g_hash_table_insert (STYLE_CACHE (sheet), ep, mstyle);
}

/**
 * sheet_style_attach_single:
 * @sheet: the sheet
 * @col: col &
 * @row: reference of cell to optimise round.
 *
 * This optimizes style usage for this cell,
 * if this shows in your profile, you are doing
 * your style setting incorrectly, use ranges
 * instead to set styles instead of hammering
 * each cell individualy.
 **/
void
sheet_style_attach_single (Sheet *sheet, int col, int row,
			   MStyle *mstyle)
{
	Range r;
	r.start.col = col;
	r.start.row = row;
	r.end.col   = col;
	r.end.row   = row;

	sheet_style_attach (sheet, r, mstyle);

	/*
	 * Expand to 3x3 patch.
	 */
	if (r.start.col > 0)
		r.start.col--;
	if (r.start.row > 0)
		r.start.row--;
	if (r.end.col < SHEET_MAX_COLS - 1)
		r.end.col++;
	if (r.end.row < SHEET_MAX_ROWS - 1)
		r.end.row++;

	sheet_style_optimize (sheet, r);
}

/**
 * sheet_style_optimize:
 * @sheet: The sheet
 * @range: The range containing optimizable StyleRegions.
 *
 * This routine merges similar styles in the Range.
 * since this function is not only slow, but memory
 * intensive it should be used wisely & sparingly and
 * over small ranges.
 *
 * FIXME: This routine has space for some serious
 *        optimization.
 *
 **/
void
sheet_style_optimize (Sheet *sheet, Range range)
{
	GList *l, *a;
	GList *style_list;
	int    overlapping = 0, len = 0, i;

	g_return_if_fail (sheet != NULL);
	g_return_if_fail (IS_SHEET (sheet));

	if (STYLE_DEBUG)
		g_warning ("Optimize (%d, %d):(%d, %d)",
			   range.start.col, range.start.row,
			   range.end.col,   range.end.row);

	style_list = NULL;
 	/* Look in the styles applied to the sheet */
	for (l = STYLE_LIST (sheet); l; l = l->next) {
		StyleRegion *sr = l->data;
		if (range_overlap (&sr->range, &range)) {
			style_list = g_list_prepend (style_list, sr);
			overlapping++;
		}
		len++;
	}

	if (STYLE_DEBUG)
		g_warning ("there are %d overlaps out of %d = %g%%",
			   overlapping, len, (double)((1.0 * overlapping) / len));

	/*
	 * Merge any identical Range regions
	 */
	for (a = style_list; a; a = a->next) {

		StyleRegion *sra = a->data;
		GList       *b;

		b = a->next;
		while (b) {
			StyleRegion *srb = b->data;
			GList *next = g_list_next (b);

			if (STYLE_DEBUG) {
				printf ("Compare equal iteration: ");
				range_dump (&sra->range);
				printf (" to ");
				range_dump (&srb->range);
				printf ("\n");
			}

			/*
			 * This inner loop gets called a lot !
			 */
			if (range_equal (&sra->range, &srb->range)) {
				MStyle *tmp;

				if (STYLE_DEBUG)
					printf ("testme: Two equal ranges, merged\n");

				tmp = mstyle_merge (sra->style, srb->style);
				if (tmp) {
					style_list = g_list_remove (style_list, srb);
					STYLE_LIST (sheet) = g_list_remove (STYLE_LIST (sheet), srb);
					mstyle_unref (sra->style);
					sra->style = tmp;
					mstyle_unref (srb->style);
					srb->style = NULL;
					g_free (srb);
				} else
					g_warning ("failed mstyle_merge!");
			}

			b = next;
		}
	}

	/*
	 * Allow to coalesce in X sense, then again in Y sense.
	 */
	for (i = 0; i < 2; i++) {
		/*
		 * Cull adjacent identical Style ranges.
		 */
		for (a = style_list; a; a = a->next) {

			StyleRegion *sra = a->data;
			GList       *b;

			b = a->next;
			while (b) {
				StyleRegion *srb = b->data;
				GList *next = g_list_next (b);

				if (STYLE_DEBUG) {
					printf ("Compare adjacent iteration: ");
					range_dump (&sra->range);
					printf (" to ");
					range_dump (&srb->range);
					printf ("\n");
				}

				if (range_adjacent (&sra->range, &srb->range)) {
					if (mstyle_equal  ( sra->style,  srb->style)) {
						if (STYLE_DEBUG)
							printf ("testme: Merging two ranges\n");

						sra->range = range_merge (&sra->range, &srb->range);
						style_list = g_list_remove (style_list, srb);
						STYLE_LIST (sheet) = g_list_remove (STYLE_LIST (sheet), srb);

						mstyle_unref (srb->style);
						g_free (srb);
					} else if (STYLE_DEBUG)
						printf ("Regions adjacent but not equal\n");
				}

				b = next;
			}
		}
	}

	g_list_free (style_list);

	/* FIXME: shouldn't be neccessary but just in case */
	sheet_style_cache_flush (sheet);
}

/**
 * sheet_style_attach:
 * @sheet: The sheet to attach to
 * @range: the range to attach to
 * @style: the style to attach.
 *
 *  This routine applies a set of style elements in 'style' to
 * a range in a sheet. This function needs some clever optimization
 * the current code is grossly simplistic.
 **/
void
sheet_style_attach (Sheet  *sheet, Range range,
		    MStyle *mstyle)
{
	StyleRegion *sr;

	g_return_if_fail (sheet != NULL);
	g_return_if_fail (IS_SHEET (sheet));
	g_return_if_fail (mstyle != NULL);
	g_return_if_fail (range.start.col <= range.end.col);
	g_return_if_fail (range.start.row <= range.end.row);
	g_return_if_fail (mstyle_verify (mstyle));

	/*
	 * FIXME: We need some clever people here....
	 *
	 *  Compartmentalize the styles via a fast row/col. vector perhaps
	 * to speedup lookups.
	 *
	 *  Optimize the range fragmentation code.
	 *
	 *  Can we afford to merge on the fly ?
	 */

	sr = g_new (StyleRegion, 1);
	sr->range = range;
	sr->style = mstyle;
	mstyle_ref (mstyle);

	STYLE_LIST (sheet) = g_list_prepend (STYLE_LIST (sheet), sr);

	if (STYLE_DEBUG) {
		printf ("Attaching ");
		mstyle_dump (sr->style);
		printf ("to cell ");
		range_dump (&sr->range);
		printf ("\n");
	}
	sheet_style_cache_flush (sheet);

	/* FIXME: Need to clip range against view port */
	sheet_redraw_cell_region (sheet,
				  range.start.col, range.start.row,
				  range.end.col,   range.end.row);
}

/**
 * sheet_style_compute:
 * @sheet:	 Which sheet we are looking up
 * @col:	 column
 * @row:	 row
 * @mash:        an array [MSTYLE_ELEMENT_MAX]
 *
 *   The function merges all the requisite styles together
 * returning the merged result in the 'mash' array. In order
 * to generate a conventional Style use style_mstyle_new.
 *
 */
MStyle *
sheet_style_compute (Sheet const *sheet, int col, int row)
{
	GList  *l;
	GList  *style_list;
	MStyle *mstyle;

	g_return_val_if_fail (sheet != NULL, NULL);
	g_return_val_if_fail (IS_SHEET (sheet), NULL);

	if ((mstyle = sheet_style_cache_lookup (sheet, col, row))) {
		mstyle_ref (mstyle);
		return mstyle;
	}

	style_list = NULL;
	/* Look in the styles applied to the sheet */
	for (l = STYLE_LIST (sheet); l; l = l->next) {
		StyleRegion *sr = l->data;
		if (range_contains (&sr->range, col, row)) {
			if (STYLE_DEBUG) {
				range_dump (&sr->range);
				mstyle_dump (sr->style);
			}
			style_list = g_list_prepend (style_list,
						     sr->style);
		}
	}
	style_list = g_list_reverse (style_list);

	if (!mstyle_list_check_sorted (style_list))
		g_warning ("Styles not sorted");
	mstyle = mstyle_do_merge (style_list, MSTYLE_ELEMENT_MAX);
	g_list_free (style_list);

	sheet_style_cache_add ((Sheet *)sheet, col, row, mstyle);

	return mstyle;
}

static void
sheet_selection_apply_style_cb (Sheet *sheet,
				Range const *range,
				gpointer user_data)
{
	mstyle_ref           (user_data);
	sheet_style_attach   (sheet, *range, user_data);
	sheet_style_optimize (sheet, *range);
}

/**
 * sheet_selection_apply_style:
 * @style: style to be attached
 *
 * This routine attaches @style to the various SheetSelections
 *
 */
void
sheet_selection_apply_style (Sheet *sheet, MStyle *mstyle)
{
	selection_foreach_range (sheet,
				 sheet_selection_apply_style_cb,
				 mstyle);
	sheet_set_dirty (sheet, TRUE);
	mstyle_unref            (mstyle);
}

typedef struct {
	MStyle *mstyle;
} UniqueClosure;

static void
sheet_unique_cb (Sheet *sheet, Range const *range,
	       gpointer user_data)
{
	UniqueClosure *cl = (UniqueClosure *)user_data;
	GList *l, *simple;
	GList *overlap_list = NULL;

 	/* Look in the styles applied to the sheet */
	for (l = STYLE_LIST (sheet); l; l = l->next) {
		StyleRegion *sr = l->data;
		if (range_overlap (&sr->range, range)) {
			if (STYLE_DEBUG) {
				range_dump (&sr->range);
				mstyle_dump (sr->style);
			}
			overlap_list = g_list_prepend (overlap_list, sr);
		}
	}

	/* Fragment ranges into fully overlapping ones */
	simple = range_fragment (overlap_list);
	for (l = simple; l; l = g_list_next (l)) {
		Range  *r = simple->data;
		GList  *b, *style_list = NULL;
		MStyle *tmp;

		/*
		 * Build a list of styles here.
		 */
		for (b = overlap_list; b; b = g_list_next (b)) {
			StyleRegion *sr = b->data;
			if (range_contains (&sr->range, r->start.col,
					    r->start.row)) {
				if (STYLE_DEBUG) {
					range_dump (&sr->range);
					mstyle_dump (sr->style);
				}
				style_list = g_list_prepend (style_list,
							     sr->style);
			}
		}
		style_list = g_list_reverse (style_list);

		if (!mstyle_list_check_sorted (style_list))
			g_warning ("Styles not sorted");

		tmp = mstyle_do_merge (style_list, MSTYLE_ELEMENT_MAX);

		mstyle_compare (cl->mstyle, tmp);

		mstyle_unref (tmp);
		g_list_free (style_list);
	}

	range_fragment_free (simple);
	g_list_free (overlap_list);
}

/**
 * sheet_selection_get_unique_style:
 * @sheet: the sheet.
 *
 * Return a merged list of styles for the selection,
 * if a style is not unique then we get MSTYLE_ELEMENT_CONFLICT.
 *
 * Return value: the merged list; free this.
 **/
MStyle *
sheet_selection_get_unique_style (Sheet *sheet)
{
	UniqueClosure cl;

	g_return_val_if_fail (sheet != NULL, NULL);
	g_return_val_if_fail (IS_SHEET (sheet), NULL);

	/*
	 * For each non-overlapping selection the contained style regions
	 * must be fragmented into totaly overlapping regions. These must
	 * then be merged down to MStyleElement arrays and then these must
	 * be compared + conflicts tagged.
	 */

	cl.mstyle = mstyle_new ();

	selection_foreach_range (sheet, sheet_unique_cb, &cl);

	return cl.mstyle;
}

void
sheet_create_styles (Sheet *sheet)
{
	SheetStyleData *sd;

	g_return_if_fail (sheet != NULL);

	sd = g_new (SheetStyleData, 1);
	sd->style_list = NULL;
	sd->cell_cache = NULL;
	sheet->style_data = sd;
}

void
sheet_destroy_styles (Sheet *sheet)
{
	GList *l;

	sheet_style_cache_flush (sheet);
	for (l = STYLE_LIST (sheet); l; l = l->next) {
		StyleRegion *sr = l->data;

		mstyle_unref (sr->style);

		sr->style = NULL;

		g_free (sr);
	}
	g_list_free (STYLE_LIST (sheet));
	STYLE_LIST (sheet) = NULL;
}

void
sheet_styles_dump (Sheet *sheet)
{
	GList *l;
	int i = 0;

	g_return_if_fail (sheet != NULL);

	for (l = STYLE_LIST (sheet); l; l = l->next) {
		StyleRegion *sr = l->data;
		printf ("Range: ");
		range_dump (&sr->range);
		printf ("style ");
		mstyle_dump (sr->style);
		printf ("\n");
		i++;
	}
	printf ("There were %d styles\n", i);
}
