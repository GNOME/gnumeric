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

	/* FIXME: Some serious work needs to be done here */
	sr = g_new (StyleRegion, 1);
	sr->range = range;
	sr->style = style;

	sheet->style_list = g_list_prepend (sheet->style_list, sr);

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
	Range const * range = selection_first_range (sheet);

	g_warning ("Fixme: applying style to first range only");
	sheet_style_attach (sheet, *range, style);
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
	int col, row;
	Range const * range = selection_first_range (sheet);

	g_warning ("need range_overlap function");
	col = range->start.col;
	row = range->start.row;

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
		if (range_contains (&sr->range, col, row)) {
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
