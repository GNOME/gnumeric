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
	for (l = sheet->style_list; l; l = l->next) {
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
					sheet->style_list = g_list_remove (sheet->style_list, srb);
					mstyle_unref (sra->style);
					sra->style = tmp;
					mstyle_unref (srb->style);
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
						sheet->style_list = g_list_remove (sheet->style_list, srb);
						
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
	 */

	sr = g_new (StyleRegion, 1);
	sr->range = range;
	sr->style = style;
	
	sheet->style_list = g_list_prepend (sheet->style_list, sr);

	if (STYLE_DEBUG) {
		printf ("Attaching ");
		mstyle_dump (sr->style);
		printf ("to cell ");
		range_dump (&sr->range);
		printf ("\n");
	}
		
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
 * @non_default: A pointer where we store the attributes
 *               the cell has which are not part of the
 *               default style.
 */
Style *
sheet_style_compute (Sheet const *sheet, int col, int row)
{
	GList *l;
	Style *style;
	GList *style_list;
	g_return_val_if_fail (sheet != NULL, NULL);
	g_return_val_if_fail (IS_SHEET (sheet), NULL);

	style_list = NULL;
	/* Look in the styles applied to the sheet */
	for (l = sheet->style_list; l; l = l->next) {
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

	style = render_merge (style_list);
	g_list_free (style_list);

	return style;
}

Style *
sheet_style_compute_blank (Sheet const *sheet, int col, int row)
{
	GList *l;
	Style *style;
	GList *style_list;
	g_return_val_if_fail (sheet != NULL, NULL);
	g_return_val_if_fail (IS_SHEET (sheet), NULL);

	style_list = NULL;
	/* Look in the styles applied to the sheet */
	for (l = sheet->style_list; l; l = l->next) {
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

	style = render_merge_blank (style_list);
	g_list_free (style_list);

	return style;
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
	for (l = sheet->style_list; l; l = l->next) {
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
		GList *b;

		for (b = overlap_list; b; b = g_list_next (b)) {
			g_warning ("need to merge styles here with"
				   " conflicts into cb->mash");
/*			mstyle_do_merge (cl.style_list, MSTYLE_ELEMENT_MAX,
mash, TRUE);*/
		}
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
	int i;

	g_return_val_if_fail (sheet != NULL, NULL);
	g_return_val_if_fail (IS_SHEET (sheet), NULL);

	/*
	 * FIXME: each style region needs to be fragmented into totaly
	 * overlapping regions. These must then be merged down to MStyleElement
	 * arrays and then these must be compared + conflicts tagged.
	 * use range_fragment + simplify mstyle_do_merge.
	 */

	mash = g_new (MStyleElement, MSTYLE_ELEMENT_MAX);
	for (i = 0; i < MSTYLE_ELEMENT_MAX; i++)
		mash[i].type = MSTYLE_ELEMENT_UNSET;
	cl.style = mash;

	selection_foreach_range (sheet, sheet_uniq_cb, &cl);

	return mash;
}
