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
	GList       *l;
	StyleRegion *sr;
	gboolean     add;

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

	add = TRUE;
	for (l = sheet->style_list; l; l = g_list_next (l)) {
		StyleRegion *sr = l->data;
		if (range_equal (&sr->range, &range)) {
			MStyle *tmp;

			g_warning ("Merging overlapping regions");
			tmp = sr->style;
			sr->style = mstyle_merge (tmp, style);
			mstyle_unref (tmp);

			add = FALSE;
		}
	}

	if (add) {
		sr = g_new (StyleRegion, 1);
		sr->range = range;
		sr->style = style;
		
		sheet->style_list = g_list_prepend (sheet->style_list, sr);
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
/*			range_dump (&sr->range);
			mstyle_dump (sr->style);*/
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
			range_dump (&sr->range);
			mstyle_dump (sr->style);
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
	sheet_style_attach (sheet, *range, user_data);
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
	MStyleElement *mash = g_new (MStyleElement, MSTYLE_ELEMENT_MAX);
	GList *l;
	GList *style_list;
	Range const * range = selection_first_range (sheet);

	g_return_val_if_fail (sheet != NULL, NULL);
	g_return_val_if_fail (IS_SHEET (sheet), NULL);

	g_warning ("Unimplemented - worse still; wrong, needs perfect"
		   "redundant area clipping to work");
	/* Or in fact, merge needs to be spatially aware in some clever way:
	   this is a nightmare so, instead get duplicate culling perfect */

	style_list = NULL;
	/* Look in the styles applied to the sheet */
	for (l = sheet->style_list; l; l = l->next) {
		StyleRegion *sr = l->data;
		if (range_overlap (&sr->range, range)) {
			range_dump (&sr->range);
			mstyle_dump (sr->style);
			style_list = g_list_prepend (style_list,
						     sr->style);
		}
	}
	style_list = g_list_reverse (style_list);

	mstyle_do_merge (style_list, MSTYLE_ELEMENT_MAX,
			 mash, TRUE);

	return mash;
}
