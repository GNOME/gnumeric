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

#define STYLE_DEBUG (gnumeric_debugging > 0)

typedef struct {
	GList      *style_list;
	GHashTable *cell_cache;
} StyleData;

#define STYLE_LIST(s)  (((StyleData *)(s)->style_data)->style_list)
#define STYLE_CACHE(s) (((StyleData *)(s)->style_data)->cell_cache)

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

static const MStyleElement *
sheet_style_cache_lookup (const EvalPosition * const p)
{
	if (!STYLE_CACHE (p->sheet))
		return NULL;

	return g_hash_table_lookup (STYLE_CACHE (p->sheet), (gpointer)p);
}

static gboolean
scache_remove (EvalPosition *key, MStyleElement *data, gpointer dummy)
{
	g_free (key);
	g_free (data);
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
sheet_style_cache_add (const EvalPosition * const p,
		       const MStyleElement *e)
{
	EvalPosition  *ep;
	MStyleElement *ec;

	if (STYLE_CACHE (p->sheet) &&
	    g_hash_table_size (STYLE_CACHE (p->sheet)) > 1024)
		sheet_style_cache_flush (p->sheet);

	if (!STYLE_CACHE (p->sheet))
		STYLE_CACHE (p->sheet) = g_hash_table_new ((GHashFunc)evalpos_hash,
							   (GCompareFunc)evalpos_compare);

	ep = g_new (EvalPosition, 1);
	*ep = *p;
	ec = g_new (MStyleElement, MSTYLE_ELEMENT_MAX);
	memcpy (ec, e, sizeof (MStyleElement) * MSTYLE_ELEMENT_MAX);

	g_hash_table_insert (STYLE_CACHE (p->sheet), ep, ec);
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
sheet_style_attach (Sheet *sheet, Range range,
		    MStyle *style)
{
	StyleRegion *sr;

	g_return_if_fail (sheet != NULL);
	g_return_if_fail (IS_SHEET (sheet));
	g_return_if_fail (style != NULL);
	g_return_if_fail (range.start.col <= range.end.col);
	g_return_if_fail (range.start.row <= range.end.row);

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
	sr->style = style;
	
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
void
sheet_style_compute (Sheet const *sheet, int col, int row,
		     MStyleElement *style)
{
	GList *l;
	GList *style_list;
	EvalPosition   cache_pos;
	const MStyleElement *cache_elem;

	g_return_if_fail (sheet != NULL);
	g_return_if_fail (IS_SHEET (sheet));

	cache_pos.sheet    = (Sheet *)sheet;
	cache_pos.eval_col = col;
	cache_pos.eval_row = row;
	if ((cache_elem = sheet_style_cache_lookup (&cache_pos))) {
		memcpy (style, cache_elem, sizeof (MStyleElement) * MSTYLE_ELEMENT_MAX);
		return;
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
	mstyle_do_merge (style_list, MSTYLE_ELEMENT_MAX, style);
	g_list_free (style_list);

	sheet_style_cache_add (&cache_pos, style);
}

static void
sheet_selection_apply_style_cb (Sheet *sheet,
				Range const *range,
				gpointer user_data)
{
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
sheet_selection_apply_style (Sheet *sheet, MStyle *style)
{
	selection_foreach_range (sheet, 
				 sheet_selection_apply_style_cb,
				 style);
	sheet_set_dirty (sheet, TRUE);
}

typedef struct {
	MStyleElement *style;
} UniqClosure;

static void
sheet_uniq_cb (Sheet *sheet, Range const *range,
	       gpointer user_data)
{
	UniqClosure *cl = (UniqClosure *)user_data;
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
		MStyleElement mash[MSTYLE_ELEMENT_MAX];

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
		mstyle_do_merge (style_list, MSTYLE_ELEMENT_MAX,
				 mash);
		
		mstyle_elements_compare (cl->style, mash);

		mstyle_elements_destroy (mash);
	}

	range_fragment_free (simple);
	g_list_free (overlap_list);
}

/**
 * sheet_selection_get_uniq_style:
 * @sheet: the sheet.
 * 
 * Return a merged list of styles for the selection,
 * if a style is not uniq then we get MSTYLE_ELEMENT_CONFLICT.
 * 
 * Return value: the merged list; free this.
 **/
MStyleElement *
sheet_selection_get_uniq_style (Sheet *sheet)
{
	UniqClosure cl;
	MStyleElement *mash;

	g_return_val_if_fail (sheet != NULL, NULL);
	g_return_val_if_fail (IS_SHEET (sheet), NULL);

	/*
	 * FIXME: each style region needs to be fragmented into totaly
	 * overlapping regions. These must then be merged down to MStyleElement
	 * arrays and then these must be compared + conflicts tagged.
	 * use range_fragment + simplify mstyle_do_merge.
	 */

	mash = g_new (MStyleElement, MSTYLE_ELEMENT_MAX);
	mstyle_elements_init (mash);
	cl.style = mash;

	selection_foreach_range (sheet, sheet_uniq_cb, &cl);

	return mash;
}

void
sheet_create_styles (Sheet *sheet)
{
	StyleData *sd;

	g_return_if_fail (sheet != NULL);

	sd = g_new (StyleData, 1);
	sd->style_list = NULL;
	sd->cell_cache = NULL;
	sheet->style_data = sd;
}

void
sheet_destroy_styles (Sheet *sheet)
{
	GList *l;

	for (l = STYLE_LIST (sheet); l; l = l->next) {
		StyleRegion *sr = l->data;

		mstyle_unref (sr->style);

		sr->style = NULL;

		g_free (sr);
	}
	g_list_free (STYLE_LIST (sheet));
	STYLE_LIST (sheet) = NULL;
	sheet_style_cache_flush (sheet);	
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
