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
#include "border.h"

#define STYLE_DEBUG (style_debugging > 2)

static guint32 stamp = 0;

struct _SheetStyleData {
	GList      *style_list; /* of StyleRegions */
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
	return g_list_copy (STYLE_LIST (sheet));
}

static guint
evalpos_hash (gconstpointer key)
{
	const EvalPosition *ep = (const EvalPosition *) key;

	/* FIXME: what about sheet ? */
	return (ep->eval.col << 8) ^ ep->eval.row;
}

static gint
evalpos_compare (EvalPosition const * a, EvalPosition const * b)
{
	return (a->sheet == b->sheet &&
		a->eval.row == b->eval.row &&
		a->eval.col == b->eval.col);
}

static MStyle *
sheet_style_cache_lookup (const Sheet *sheet, int col, int row)
{
	EvalPosition ep;

	ep.sheet    = (Sheet *)sheet;
	ep.eval.col = col;
	ep.eval.row = row;

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
	ep->eval.col = col;
	ep->eval.row = row;
	mstyle_ref (mstyle);

	g_hash_table_insert (STYLE_CACHE (sheet), ep, mstyle);
}

/**
 * do_list_check_sorted:
 * @list: the list of StyleRegions.
 * @as_per_sheet: which direction the stamp order should be.
 * 
 *   This function checks each StyleRegion's range, and its stamp
 * for correctness.
 * 
 * Return value: FALSE if a sort error occurs.
 **/
static gboolean
do_list_check_sorted (const GList *list, gboolean as_per_sheet)
{
	const GList *l = list;

	if (as_per_sheet) {
		guint32 stamp = -1; /* max guint32 */

		while (l) {
			StyleRegion *sr = l->data;
			if (!range_valid (l->data))
				g_warning ("Invalid styleregion range");
			if (sr->stamp > stamp)
				return FALSE;
			stamp = sr->stamp;
			l = g_list_next (l);
		}
	} else {
		guint32 stamp = 0;

		while (l) {
			StyleRegion *sr = l->data;
			if (!range_valid (l->data))
				g_warning ("Invalid styleregion range");
			if (sr->stamp < stamp)
				return FALSE;
			stamp = sr->stamp;
			l = g_list_next (l);
		}
	}
	return TRUE;
}

static inline gboolean
list_check_sorted (const GList *list, gboolean as_per_sheet)
{
	if (style_debugging > 0)
		return do_list_check_sorted (list, as_per_sheet);
	return TRUE;
}

static inline StyleRegion *
style_region_new (const Range *range, MStyle *mstyle)
{
	StyleRegion *sr;

	sr = g_new (StyleRegion, 1);
	sr->stamp = stamp++;
	sr->range = *range;
	sr->style = mstyle;
       
	return sr;
}

static inline StyleRegion *
style_region_copy (const StyleRegion *sra)
{
	StyleRegion *sr;

	sr = g_new (StyleRegion, 1);
	sr->stamp = sra->stamp;
	sr->range = sra->range;
	sr->style = sra->style;
	mstyle_ref (sra->style);
       
	return sr;
}

static inline void
style_region_destroy (StyleRegion *sr)
{
	mstyle_unref (sr->style);
	sr->style = NULL;
	g_free (sr);
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

static void
sheet_style_region_unlink (Sheet *sheet, StyleRegion *region)
{
	STYLE_LIST (sheet) = g_list_remove (STYLE_LIST (sheet), region);
	style_region_destroy (region);
}

static gint
sheet_style_stamp_compare (gconstpointer a, gconstpointer b)
{
	if (((StyleRegion *)a)->stamp > ((StyleRegion *)b)->stamp)
		return -1;
	else if (((StyleRegion *)a)->stamp == ((StyleRegion *)b)->stamp)
		return 0;
	else
		return 1;
}

static void
sheet_style_region_link (Sheet *sheet, StyleRegion *region)
{
	g_return_if_fail (sheet != NULL);
	g_return_if_fail (region != NULL);
	g_return_if_fail (IS_SHEET (sheet));

	if (!list_check_sorted (STYLE_LIST (sheet), TRUE))
		g_warning ("order broken before insert");

	STYLE_LIST (sheet) = g_list_insert_sorted (STYLE_LIST (sheet), region,
						   sheet_style_stamp_compare);
	if (!list_check_sorted (STYLE_LIST (sheet), TRUE))
		g_warning ("list_insert_sorted screwed order");
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
	g_return_if_fail (range_valid (&range));

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

	if (!list_check_sorted (style_list, FALSE))
		g_warning ("Styles not sorted");

	if (STYLE_DEBUG)
		g_warning ("there are %d overlaps out of %d = %g%%",
			   overlapping, len, (double)((100.0 * overlapping) / len));

	/*
	 * Merge any identical Range regions
	 */
	for (a = style_list; a; a = a->next) {
		StyleRegion *sra = a->data;
		GList       *b;

		if (!a->data)
			continue;

		for (b = a->next; b && a->data; b = b->next) {
			StyleRegion *srb = b->data;

			if (!b->data)
				continue;

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
				StyleRegion *master, *slave;

				if (STYLE_DEBUG)
					printf ("testme: Two equal ranges, merged\n");

				if (sra->stamp >= srb->stamp) {
					master = sra;
					slave  = srb;
					b->data = NULL;
				} else {
					master = srb;
					slave  = sra;
					a->data = NULL;
				}
				slave->style = mstyle_merge (master->style, slave->style);
				if (mstyle_empty (slave->style))
					sheet_style_region_unlink (sheet, slave);
			}
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

			if (!a->data)
				continue;

			for (b = a->next; b && a->data; b = b->next) {
				StyleRegion *srb = b->data;

				if (!b->data)
					continue;

				if (STYLE_DEBUG) {
					printf ("Compare adjacent iteration: ");
					range_dump (&sra->range);
					printf (" to ");
					range_dump (&srb->range);
					printf ("\n");
				}

				if (range_adjacent (&sra->range, &srb->range)) {
					if (mstyle_equal  ( sra->style,  srb->style)) {
						StyleRegion *master, *slave;

						if (sra->stamp >= srb->stamp) {
							master = sra;
							slave  = srb;
							b->data = NULL;
						} else {
							master = srb;
							slave  = sra;
							a->data = NULL;
						}
						if (STYLE_DEBUG)
							printf ("testme: Merging two ranges\n");

						master->range = range_merge (&master->range, &slave->range);
						sheet_style_region_unlink (sheet, slave);
					} else if (STYLE_DEBUG)
						printf ("Regions adjacent but not equal\n");
				}
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
	g_return_if_fail (range_valid (&range));
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

	sr = style_region_new (&range, mstyle);
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

static inline MStyle *
sheet_mstyle_compute_from_list (GList *list, int col, int row)
{
	GList  *l;
	GList  *style_list = NULL;
	MStyle *mstyle;

	for (l = list; l; l = l->next) {
		StyleRegion *sr = l->data;
		if (range_contains (&sr->range, col, row)) {
			if (style_debugging > 5) {
				range_dump (&sr->range);
				mstyle_dump (sr->style);
			}
			style_list = g_list_prepend (style_list,
						     sr->style);
		}
	}
	style_list = g_list_reverse (style_list);

	mstyle = mstyle_do_merge (style_list, MSTYLE_ELEMENT_MAX);
	g_list_free (style_list);

	return mstyle;
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
	MStyle *mstyle;

	g_return_val_if_fail (sheet != NULL, NULL);
	g_return_val_if_fail (IS_SHEET (sheet), NULL);

	if ((mstyle = sheet_style_cache_lookup (sheet, col, row))) {
		mstyle_ref (mstyle);
		return mstyle;
	}

	if (!list_check_sorted (STYLE_LIST (sheet), TRUE))
		g_warning ("Styles not sorted");

	/* Look in the styles applied to the sheet */

	mstyle = sheet_mstyle_compute_from_list (STYLE_LIST (sheet),
						 col, row);

	sheet_style_cache_add ((Sheet *)sheet, col, row, mstyle);

	return mstyle;
}

static gboolean
sheet_selection_apply_style_cb (Sheet *sheet,
				Range const *range,
				gpointer user_data)
{
	MStyle *mstyle = (MStyle *)user_data;
	mstyle_ref           (mstyle);
	sheet_style_attach   (sheet, *range, user_data);
	sheet_style_optimize (sheet, *range);
	sheet_cells_update   (sheet, *range,
			      mstyle_is_element_set (mstyle, MSTYLE_FORMAT));
	sheet_redraw_cell_region (sheet, range->start.col, range->start.row,
				  range->end.col, range->end.row);
	return TRUE;
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

	g_return_if_fail (sheet->style_data != NULL);

	sheet_style_cache_flush (sheet);
	for (l = STYLE_LIST (sheet); l; l = l->next) {
		StyleRegion *sr = l->data;

		mstyle_unref (sr->style);

		sr->style = NULL;

		g_free (sr);
	}
	g_list_free (STYLE_LIST (sheet));
	STYLE_LIST (sheet) = NULL;

	g_free (sheet->style_data);
	sheet->style_data = NULL;
}

void
sheet_styles_dump (Sheet *sheet)
{
	GList *l;
	int i = 0;

	g_return_if_fail (sheet != NULL);

	for (l = STYLE_LIST (sheet); l; l = l->next) {
		StyleRegion *sr = l->data;
		printf ("Stamp %d Range: ", sr->stamp);
		range_dump (&sr->range);
		printf ("style ");
		mstyle_dump (sr->style);
		printf ("\n");
		i++;
	}
	printf ("There were %d styles\n", i);
}

static Value *
re_dimension_cells_cb (Sheet *sheet, int col, int row, Cell *cell,
		       gpointer re_render)
{
	if (GPOINTER_TO_INT (re_render))
		cell_render_value (cell);

	cell_style_changed (cell);

	return NULL;
}


/**
 * sheet_cells_update:
 * @sheet: The sheet,
 * @r:     the region to update.
 * @render_text: whether to re-render the text in cells
 * 
 * This is used to re-calculate cell dimensions and re-render
 * a cell's text. eg. if a format has changed we need to re-render
 * the cached version of the rendered text in the cell.
 **/
void
sheet_cells_update (Sheet *sheet, Range r,
		    gboolean render_text)
{
	sheet->modified = TRUE;
	sheet_cell_foreach_range (sheet, TRUE,
				  r.start.col, r.start.row,
				  r.end.col, r.end.row,
				  re_dimension_cells_cb,
				  GINT_TO_POINTER (render_text));
}

Range
sheet_get_full_range (void)
{
	Range r;

	r.start.col = 0;
	r.start.row = 0;
	r.end.col = SHEET_MAX_COLS - 1;
	r.end.row = SHEET_MAX_ROWS - 1;

	return r;
}

void
sheet_style_delete_colrow (Sheet *sheet, int pos, int count,
			   gboolean is_col)
{
	Range  del_range;
	GList *l, *next;

	g_return_if_fail (pos >= 0);
	g_return_if_fail (count > 0);
	g_return_if_fail (sheet != NULL);
	g_return_if_fail (IS_SHEET (sheet));

	del_range = sheet_get_full_range ();
	if (is_col) {
		del_range.start.col = pos;
		del_range.end.col   = pos + count - 1;
	} else {
		del_range.start.row = pos;
		del_range.end.row   = pos + count - 1;
	}

	/* Don't touch the last 'global' range */
	for (l = STYLE_LIST (sheet); l && l->next; l = next) {
		StyleRegion *sr = (StyleRegion *)l->data;

		next = g_list_next (l);

		if (is_col) {
			if (sr->range.start.col      >  del_range.end.col)
				sr->range.start.col  -= count;
			else if (sr->range.start.col >= del_range.start.col)
				sr->range.start.col  =  pos + 1;

			if (sr->range.end.col        >  del_range.end.col)
				sr->range.end.col    -= count;
			else if (sr->range.end.col   >= del_range.start.col)
				sr->range.end.col    =  pos - 1;

			if (sr->range.start.col > sr->range.end.col ||
			    sr->range.start.col < 0 ||
			    sr->range.end.col < 0)
				sheet_style_region_unlink (sheet, sr);
		} else { /* s/col/row/ */
			if (sr->range.start.row      >  del_range.end.row)
				sr->range.start.row  -= count;
			else if (sr->range.start.row >= del_range.start.row)
				sr->range.start.row  =  pos + 1;

			if (sr->range.end.row        >  del_range.end.row)
				sr->range.end.row    -= count;
			else if (sr->range.end.row   >= del_range.start.row)
				sr->range.end.row    =  pos - 1;

			if (sr->range.start.row > sr->range.end.row ||
			    sr->range.start.row < 0 ||
			    sr->range.end.row < 0)
				sheet_style_region_unlink (sheet, sr);
		}
	}

	sheet_style_cache_flush (sheet);
}

void
sheet_style_insert_colrow (Sheet *sheet, int pos, int count,
			   gboolean is_col)
{
	Range  move_range, ignore_range;
	GList *l, *next;

	g_return_if_fail (pos >= 0);
	g_return_if_fail (count > 0);
	g_return_if_fail (sheet != NULL);
	g_return_if_fail (IS_SHEET (sheet));

	move_range   = sheet_get_full_range ();
	ignore_range = sheet_get_full_range ();
	if (is_col) {
		move_range.start.col = pos;
		ignore_range.end.col = MAX (pos - 1, 0);
	} else {
		move_range.start.row = pos;
		ignore_range.end.row = MAX (pos - 1, 0);
	}

	/* Don't touch the last 'global' range */
	for (l = STYLE_LIST (sheet); l && l->next; l = next) {
		StyleRegion *sr = (StyleRegion *)l->data;

		next = g_list_next (l);

		/* 1. We can ignore anything to left or above of insert */
		if (pos > 0 && range_contained (&sr->range, &ignore_range))
			continue;

		/* 2. We simply translate the ranges completely to right or bottom */
		if (pos == 0 || range_contained (&sr->range, &move_range)) {
			if (is_col) {
				sr->range.start.col = MIN (sr->range.start.col + count,
							   SHEET_MAX_COLS - 1);
				sr->range.end.col   = MIN (sr->range.end.col + count,
							   SHEET_MAX_COLS - 1);
			} else {
				sr->range.start.row = MIN (sr->range.start.row + count,
							   SHEET_MAX_ROWS - 1);
				sr->range.end.row   = MIN (sr->range.end.row + count,
							   SHEET_MAX_ROWS - 1);
			}
			continue;
		}

		/* 3. An awkward straddle case */
		{
			StyleRegion *frag;

			/* 3.1 Create a new style Region */
			frag = style_region_copy (sr);

			/* 3.2 Split the ranges */
			if (is_col) {
				sr->range.end.col     = pos - 1;
				frag->range.start.col = pos + count;
				frag->range.end.col   = MIN (frag->range.end.col + count,
							     SHEET_MAX_COLS - 1);
			} else {
				sr->range.end.row     = pos - 1;
				frag->range.start.row = pos + count;
				frag->range.end.row   = MIN (frag->range.end.row + count,
							     SHEET_MAX_ROWS - 1);
			}
			
			/* 3.3 Insert in the correct stamp order */
			if (is_col) {
				if (frag->range.start.col <= frag->range.end.col)
					sheet_style_region_link (sheet, frag);
			} else {
				if (frag->range.start.row <= frag->range.end.row)
					sheet_style_region_link (sheet, frag);
			}
		}
	}

	sheet_style_cache_flush (sheet);
}

void
sheet_style_relocate (const struct expr_relocate_info *rinfo)
{
	GList *stored_regions = NULL;
	GList *l, *next;

	g_return_if_fail (rinfo != NULL);
	g_return_if_fail (rinfo->origin_sheet != NULL);
	g_return_if_fail (rinfo->target_sheet != NULL);
	g_return_if_fail (IS_SHEET (rinfo->origin_sheet));
	g_return_if_fail (IS_SHEET (rinfo->target_sheet));

/* 1. Fragment each StyleRegion against the original range */
	for (l = STYLE_LIST (rinfo->origin_sheet); l && l->next; l = next) {
		StyleRegion *sr = (StyleRegion *)l->data;
		GList       *fragments;

		next = l->next;

		if (!range_overlap (&sr->range, &rinfo->origin))
			continue;

		fragments = range_split_ranges (&rinfo->origin, (Range *)sr,
						(RangeCopyFn)style_region_copy);
		stored_regions = g_list_concat (fragments, stored_regions);
		sheet_style_region_unlink (rinfo->origin_sheet, sr);
	}

/* 2 Either fold back or queue Regions */
	for (l = stored_regions; l; l = l->next) {
		StyleRegion *sr = (StyleRegion *)l->data;

/*		printf ("Stored region ");
		range_dump (&sr->range);
		printf ("\n");*/

		if (!range_overlap (&sr->range, &rinfo->origin)) {
/* 2.1 Add back the fragments */
			sheet_style_region_link (rinfo->origin_sheet, sr);
		} else {
/* 2.2 Translate queued regions + re-stamp */
			sr->range.start.col   = MIN (sr->range.start.col + rinfo->col_offset,
						     SHEET_MAX_COLS - 1);
			sr->range.end.col     = MIN (sr->range.end.col   + rinfo->col_offset,
						     SHEET_MAX_COLS - 1);
			sr->range.start.row   = MIN (sr->range.start.row + rinfo->row_offset,
						     SHEET_MAX_ROWS - 1);
			sr->range.end.row     = MIN (sr->range.end.row   + rinfo->row_offset,
						     SHEET_MAX_ROWS - 1);
			sr->stamp = stamp++;
			sheet_style_region_link (rinfo->target_sheet, sr);
		}
	}
	g_list_free (stored_regions);

	sheet_style_cache_flush (rinfo->target_sheet);
	if (rinfo->origin_sheet != rinfo->target_sheet)
		sheet_style_cache_flush (rinfo->origin_sheet);
}

static void
do_apply_border (Sheet *sheet, const Range *r,
		 MStyleElementType t, int idx, MStyleBorder **borders)
{
	MStyle *mstyle;

	if (borders && borders [idx]) {
		style_border_ref (borders [idx]);
		
		mstyle = mstyle_new ();
		mstyle_set_border (mstyle, t, borders [idx]);
		sheet_style_attach (sheet, *r, mstyle);
	}
}

static void
do_blank_border (Sheet *sheet, const Range *r,
		 MStyleElementType t)
{
	MStyle *mstyle;

	mstyle = mstyle_new ();
	mstyle_set_border (mstyle, t,
			   style_border_ref (style_border_none ()));
	sheet_style_attach (sheet, *r, mstyle);
}

/*
 * Apply borders round the edge of a range.
 * ignore special corner cases; these are made by
 * an implicit StyleRegion overlap at present.
 *
 * 'Outer' borders are cleared if they have been set.
 *
 */
static gboolean
sheet_selection_apply_border_cb (Sheet *sheet,
				 Range const *range,
				 gpointer user_data)
{
	Range          r;
	MStyleBorder **borders = user_data;
	MStyle        *mstyle;

	/* 1.1 The top inner */
	r = *range;
	r.end.row = r.start.row;
	do_apply_border (sheet, &r,
			 MSTYLE_BORDER_TOP,
			 STYLE_BORDER_TOP, borders);
	/* 1.2 The top outer */
	if (borders [STYLE_BORDER_TOP]) {
		r.start.row--;
		r.end.row = r.start.row;
		if (r.start.row >= 0)
			do_blank_border (sheet, &r, MSTYLE_BORDER_BOTTOM);
	}

	/* 2.1 The bottom inner */
	r = *range;
	r.start.row = r.end.row;
	do_apply_border (sheet, &r,
			 MSTYLE_BORDER_BOTTOM,
			 STYLE_BORDER_BOTTOM, borders);
	/* 2.2 The bottom outer */
	if (borders [STYLE_BORDER_BOTTOM]) {
		r.start.row++;
		r.end.row = r.start.row;
		if (r.start.row < SHEET_MAX_ROWS)
			do_blank_border (sheet, &r, MSTYLE_BORDER_TOP);
	}

	/* 3.1 The left inner */
	r = *range;
	r.end.col = r.start.col;
	do_apply_border (sheet, &r,
			 MSTYLE_BORDER_LEFT,
			 STYLE_BORDER_LEFT, borders);
	/* 3.2 The left outer */
	if (borders [STYLE_BORDER_LEFT]) {
		r.start.col--;
		r.end.col = r.start.col;
		if (r.start.col >= 0)
			do_blank_border (sheet, &r, MSTYLE_BORDER_RIGHT);
	}

	/* 4.1 The right inner */
	r = *range;
	r.start.col = r.end.col;
	do_apply_border (sheet, &r,
			 MSTYLE_BORDER_RIGHT,
			 STYLE_BORDER_RIGHT, borders);
	/* 4.2 The right outer */
	if (borders [STYLE_BORDER_RIGHT]) {
		r.start.col++;
		r.end.col = r.start.col;
		if (r.start.col < SHEET_MAX_COLS)
			do_blank_border (sheet, &r, MSTYLE_BORDER_LEFT);
	}


	/* 5.1 The horizontal interior top */
	r = *range;
	if (r.start.row != r.end.row) {
		++r.start.row;
		do_apply_border (sheet, &r,
				 MSTYLE_BORDER_TOP,
				 STYLE_BORDER_HORIZ, borders);
	}
	/* 5.2 The horizontal interior bottom */
	r = *range;
	if (r.start.row != r.end.row) {
		--r.end.row;
		do_apply_border (sheet, &r,
				 MSTYLE_BORDER_BOTTOM,
				 STYLE_BORDER_HORIZ, borders);
	}

	/* 6.1 The vertical interior left */
	r = *range;
	if (r.start.col != r.end.col) {
		++r.start.col;
		do_apply_border (sheet, &r,
				 MSTYLE_BORDER_LEFT,
				 STYLE_BORDER_VERT, borders);
	}

	/* 6.2 The vertical interior right */
	r = *range;
	if (r.start.col != r.end.col) {
		--r.end.col;
		do_apply_border (sheet, &r,
				 MSTYLE_BORDER_RIGHT,
				 STYLE_BORDER_VERT, borders);
	}

	/* 7. Diagonals */
	mstyle = mstyle_new ();
	if (borders [STYLE_BORDER_DIAG]) {
		style_border_ref (borders [STYLE_BORDER_DIAG]);
		mstyle_set_border (mstyle, MSTYLE_BORDER_DIAGONAL,
				   borders [STYLE_BORDER_DIAG]);
	}
	if (borders [STYLE_BORDER_REV_DIAG]) {
		style_border_ref (borders [STYLE_BORDER_REV_DIAG]);
		mstyle_set_border (mstyle, MSTYLE_BORDER_REV_DIAGONAL,
				   borders [STYLE_BORDER_REV_DIAG]);
	}
	if (mstyle_empty (mstyle))
		mstyle_unref (mstyle);
	else
		sheet_style_attach (sheet, *range, mstyle);

	sheet_style_optimize (sheet, *range);
	sheet_redraw_cell_region (sheet, range->start.col, range->start.row,
				  range->end.col, range->end.row);

	return TRUE;
}

void
sheet_selection_set_border (Sheet *sheet,
			    MStyleBorder **borders)
{
	MStyle *mstyle;
	int     i;

	g_return_if_fail (sheet != NULL);
	g_return_if_fail (borders != NULL);
	g_return_if_fail (IS_SHEET (sheet));

	mstyle = mstyle_new ();

	selection_foreach_range (sheet,
				 sheet_selection_apply_border_cb,
				 borders);
	sheet_set_dirty (sheet, TRUE);
	for (i = STYLE_BORDER_TOP; i < STYLE_BORDER_EDGE_MAX; i++)
		style_border_unref (borders[i]);
}

/**
 * sheet_get_region_list_for_range:
 * @style_list: 
 * @range: 
 * 
 * Returns a reversed order list of styles that overlap
 * with range.
 * 
 * Return value: 
 **/
static inline GList *
sheet_get_region_list_for_range (GList *style_list, const Range *range)
{
	GList *l, *overlap_list = NULL;

	for (l = style_list; l; l = l->next) {
		StyleRegion *sr = l->data;
		if (range_overlap (&sr->range, range)) {
			if (style_debugging > 0) {
				range_dump (&sr->range);
				mstyle_dump (sr->style);
			}
			overlap_list = g_list_prepend (overlap_list, sr);
		}
	}
	return overlap_list;
}

typedef struct {
	MStyle        *mstyle;
	MStyleBorder **borders;
	gboolean       border_valid[STYLE_BORDER_EDGE_MAX];
} UniqueClosure;

static void
border_invalidate (UniqueClosure *cl, StyleBorderLocation location)
{
	cl->border_valid [location] = FALSE;
	style_border_unref (cl->borders [location]);
	cl->borders [location] = NULL;
}

/**
 * border_mask:
 * @cl: unique data
 * @location: which border to deal with
 * @border: the border to mask against
 * 
 * This masks the border at @cl->borders [@location]
 * it is marked invalid if the border != current value
 * note it doesn't much matter whether this is an inner
 * or an outer one since this should be transparent to the
 * user.
 *
 **/
static void
border_mask (UniqueClosure *cl, StyleBorderLocation location,
	     const MStyleBorder *border)
{
	if (cl->border_valid [location]) {
		if (!cl->borders [location])
			cl->borders [location] = style_border_ref ((MStyleBorder *)border);

		if (cl->borders [location] == style_border_none ()) {
			style_border_unref (cl->borders [location]);
			cl->borders [location] = style_border_ref ((MStyleBorder *)border);
		} else if (border && border != cl->borders [location] &&
			   border != style_border_none ()) {
			border_invalidate (cl, location);
		}
	}
}
	
/*
 * Plenty of room for optimization here
 */
static void
border_check (UniqueClosure *cl, GList *edge_list,
	      const Range *edge_range, const Range *range, const Range *all,
	      StyleBorderLocation location, gboolean do_outer)
{
	GList *frags, *l;

	frags = range_fragment_list_clip (edge_list, edge_range);
	for (l = frags; l; l = g_list_next (l)) {
		Range  *r   = l->data;
		MStyle *inner_style, *outer_style;
		const MStyleBorder *inner_border = NULL, *outer_border = NULL;
		CellPos inner, outer;
		
		inner = r->start;
		outer = r->start;
		switch (location) {
		case STYLE_BORDER_TOP:
			if (inner.col < range->start.col)
				inner.col = outer.col = range->start.col;
			inner.row = range->start.row;
			outer.row = all->start.row;
			break;
		case STYLE_BORDER_BOTTOM:
			if (inner.col < range->start.col)
				inner.col = outer.col = range->start.col;
			inner.row = range->end.row;
			outer.row = all->end.row;
			break;
		case STYLE_BORDER_LEFT:
			if (inner.row < range->start.row)
				inner.row = outer.row = range->start.row;
			inner.col = range->start.col;
			outer.col = all->start.col;
			break;
		case STYLE_BORDER_RIGHT:
			if (inner.row < range->start.row)
				inner.row = outer.row = range->start.row;
			inner.col = range->end.col;
			outer.col = all->end.col;
			break;
		default:
			g_warning ("Serious internal style border error");
			break;
		}
		if (!range_contains (range, inner.col, inner.row))
			continue; /* an outer corner */

		/* Calculate the respective styles */
		inner_style = sheet_mstyle_compute_from_list (edge_list,
							      inner.col,
							      inner.row);

		outer_style = sheet_mstyle_compute_from_list (edge_list,
							      outer.col,
							      outer.row);

		/* Build up the border maps + do internal borders */
		switch (location) {
		case STYLE_BORDER_TOP:
			inner_border = mstyle_get_border (inner_style, MSTYLE_BORDER_TOP);
			outer_border = mstyle_get_border (outer_style, MSTYLE_BORDER_BOTTOM);
			border_mask (cl, STYLE_BORDER_HORIZ,
				     mstyle_get_border (inner_style, MSTYLE_BORDER_BOTTOM));
			break;
		case STYLE_BORDER_BOTTOM:
			inner_border = mstyle_get_border (inner_style, MSTYLE_BORDER_BOTTOM);
			outer_border = mstyle_get_border (outer_style, MSTYLE_BORDER_TOP);
			border_mask (cl, STYLE_BORDER_HORIZ,
				     mstyle_get_border (inner_style, MSTYLE_BORDER_TOP));
			break;
		case STYLE_BORDER_LEFT:
			inner_border = mstyle_get_border (inner_style, MSTYLE_BORDER_LEFT);
			outer_border = mstyle_get_border (outer_style, MSTYLE_BORDER_RIGHT);
			border_mask (cl, STYLE_BORDER_VERT,
				     mstyle_get_border (inner_style, MSTYLE_BORDER_RIGHT));
			break;
		case STYLE_BORDER_RIGHT:
			inner_border = mstyle_get_border (inner_style, MSTYLE_BORDER_RIGHT);
			outer_border = mstyle_get_border (outer_style, MSTYLE_BORDER_LEFT);
			border_mask (cl, STYLE_BORDER_VERT,
				     mstyle_get_border (inner_style, MSTYLE_BORDER_LEFT));
			break;
		default:
			g_warning ("Serious internal style border error");
			break;
		}

		if (do_outer)
			border_mask (cl, location, outer_border);
		border_mask (cl, location, inner_border);
		
		/* If we have gone from nothing to something along an edge or vv. */
		if (cl->border_valid [location] &&
		    (!do_outer || outer_border == style_border_none ()) &&
		    inner_border == style_border_none () &&
		    cl->borders [location] != style_border_none ())
			border_invalidate (cl, location);
		
		/* Normal compare for styles */
		mstyle_compare (cl->mstyle, inner_style);
		mstyle_unref (inner_style);
		mstyle_unref (outer_style);
	}
	range_fragment_free (frags);
}

static gboolean
sheet_unique_cb (Sheet *sheet, Range const *range,
		 gpointer user_data)
{
	UniqueClosure *cl = (UniqueClosure *)user_data;
	GList   *all_list, *middle_list, *frags;
	GList   *edge_list[4], *l;
	Range    edge[4], all, middle;
	gboolean edge_valid[4], middle_valid;
	int      i;

 	/*
	 * 1. Create the super range including outer 
	 *     + inner + ( unwanted corners )
	 */
	all = *range;
	if (all.start.col > 0)
		all.start.col--;
	if (all.end.col < SHEET_MAX_COLS)
		all.end.col++;
 	if (all.start.row > 0)
		all.start.row--;
	if (all.end.row < SHEET_MAX_ROWS)
		all.end.row++;
	all_list = sheet_get_region_list_for_range (STYLE_LIST (sheet), &all);

	/* 2. Create the middle range */
	if (range->end.col > range->start.col + 1 &&
	    range->end.row > range->start.row + 1) {
		middle_valid = TRUE;
		middle = *range;
		middle.start.col++;
		middle.end.col--;
		middle.start.row++;
		middle.end.row--;
	} else
		middle_valid = FALSE;

	/*
	 * 3. Create border ranges around the sides,
	 * each is two cells thick.
	 */
	
	/* 3.1 Top */
	i = STYLE_BORDER_TOP;
	edge [i] = *range;
	edge [i].end.row = edge [i].start.row;
	edge_valid [i] = range_expand (&edge [i], 0, -1, 0, 0);

	/* 3.2 Bottom */
	i = STYLE_BORDER_BOTTOM;
	edge [i] = *range;
	edge [i].start.row = edge [i].end.row;
	edge_valid [i] = range_expand (&edge [i], 0, 0, 0, +1);

	/* 3.3 Left */
	i = STYLE_BORDER_LEFT;
	edge [i] = *range;
	edge [i].end.col = edge [i].start.col;
	edge_valid [i] = range_expand (&edge [i], -1, 0, 0, 0);

	/* 3.4 Right */
	i = STYLE_BORDER_RIGHT;
	edge [i] = *range;
	edge [i].start.col = edge [i].end.col;
	edge_valid [i] = range_expand (&edge [i], 0, 0, +1, 0);

	/* 4.1 Create Region list for edges */
	for (i = STYLE_BORDER_TOP; i <= STYLE_BORDER_RIGHT; i++)
		edge_list [i] = sheet_get_region_list_for_range (all_list,
								 &edge [i]);

	/* 4.2 Create region list for middle */
	if (middle_valid)
		middle_list = sheet_get_region_list_for_range (all_list, &middle);
	else
		middle_list = NULL;

	/* 5.1 Check the middle range */
	frags = range_fragment_list_clip (middle_list, &middle);
	for (l = frags; l; l = g_list_next (l)) {
		Range  *r   = l->data;
		MStyle *tmp = sheet_mstyle_compute_from_list (middle_list,
							      r->start.col,
							      r->start.row);
		mstyle_compare (cl->mstyle, tmp);
		mstyle_unref (tmp);
	}
	range_fragment_free (frags);

	/* 5.2 Move the vert / horiz. data into the array */
	if (middle_valid) {
		/* 5.2.1 Vertical */
		if (mstyle_is_element_conflict (cl->mstyle, MSTYLE_BORDER_LEFT) ||
		    mstyle_is_element_conflict (cl->mstyle, MSTYLE_BORDER_RIGHT))
			border_invalidate (cl, STYLE_BORDER_VERT);
		else {
			border_mask (cl, STYLE_BORDER_VERT,
				     mstyle_get_border (cl->mstyle, MSTYLE_BORDER_LEFT));
			border_mask (cl, STYLE_BORDER_VERT,
				     mstyle_get_border (cl->mstyle, MSTYLE_BORDER_RIGHT));
		}

		/* 5.2.2 Horizontal */
		if (mstyle_is_element_conflict (cl->mstyle, MSTYLE_BORDER_TOP) ||
		    mstyle_is_element_conflict (cl->mstyle, MSTYLE_BORDER_BOTTOM))
			border_invalidate (cl, STYLE_BORDER_HORIZ);
		else {
			border_mask (cl, STYLE_BORDER_HORIZ,
				     mstyle_get_border (cl->mstyle, MSTYLE_BORDER_TOP));
			border_mask (cl, STYLE_BORDER_HORIZ,
				     mstyle_get_border (cl->mstyle, MSTYLE_BORDER_BOTTOM));
		}
	}

	/* 5.3 Check the edges  */
	for (i = STYLE_BORDER_TOP; i <= STYLE_BORDER_RIGHT; i++)
		border_check (cl, edge_list [i], &edge [i],
			      range, &all, i, edge_valid [i]);

	/* 6. Free up resources */
	if (middle_list)
		g_list_free (middle_list);
	g_list_free (all_list);
	for (i = STYLE_BORDER_TOP; i <= STYLE_BORDER_RIGHT; i++) {
		if (edge_valid [i] && edge_list [i])
			g_list_free (edge_list [i]);
		edge_list [i] = NULL;
	}

	return TRUE;
}

/**
 * sheet_selection_get_unique_style:
 * @sheet: the sheet.
 * @borders: An array [STYLE_BORDER_EDGE_MAX]
 *
 * Return a merged list of styles for the selection,
 * if a style is not unique then we get MSTYLE_ELEMENT_CONFLICT.
 * the borders array is used due to the rather intricate nature
 * of border setting. This causes a lot of the complexity in this
 * routine. Essentialy it is neccessary to check adjacent cells for
 * borders.
 *
 * Return value: the merged list; free this.
 **/
MStyle *
sheet_selection_get_unique_style (Sheet *sheet, MStyleBorder **borders)
{
	int           i;
	UniqueClosure cl;

	g_return_val_if_fail (sheet != NULL, NULL);
	g_return_val_if_fail (borders != NULL, NULL);
	g_return_val_if_fail (IS_SHEET (sheet), NULL);

	/*
	 * For each non-overlapping selection the contained style regions
	 * must be fragmented into totaly overlapping regions. These must
	 * then be merged down to MStyleElement arrays and then these must
	 * be compared + conflicts tagged.
	 */

	cl.mstyle  = mstyle_new ();
	cl.borders = borders;
	for (i = STYLE_BORDER_TOP; i < STYLE_BORDER_EDGE_MAX; i++) {
		cl.borders [i] = NULL;
		cl.border_valid [i] = TRUE;
	}

	selection_foreach_range (sheet, sheet_unique_cb, &cl);

	if (!mstyle_is_element_conflict (cl.mstyle, MSTYLE_BORDER_REV_DIAGONAL))
		border_mask (&cl, STYLE_BORDER_REV_DIAG,
			     mstyle_get_border (cl.mstyle, MSTYLE_BORDER_REV_DIAGONAL));
	else
		border_invalidate (&cl, STYLE_BORDER_REV_DIAG);

	if (!mstyle_is_element_conflict (cl.mstyle, MSTYLE_BORDER_DIAGONAL))
		border_mask (&cl, STYLE_BORDER_DIAG,
			     mstyle_get_border (cl.mstyle, MSTYLE_BORDER_DIAGONAL));
	else
		border_invalidate (&cl, STYLE_BORDER_DIAG);
 
	if (style_debugging > 0) {
		printf ("Uniq style is\n");
		mstyle_dump (cl.mstyle);
		printf ("\n");
	}
 
	return cl.mstyle;
}

/**
 * sheet_style_get_extent:
 * @r: input / output range
 * @sheet: for this sheet.
 * 
 *   This enlarges r by a union with each non infinite style
 * range.
 **/
void
sheet_style_get_extent (Range *r, Sheet const *sheet)
{
	GList   *l;

	for (l = STYLE_LIST (sheet); l; l = g_list_next (l)) {
		Range *sr = (Range *)l->data;

		if (!range_is_infinite (sr))
			*r = range_union (r, sr);
	}
}
