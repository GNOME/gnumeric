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
#include "gutils.h"
#include "gnumeric-util.h"
#include "eval.h"
#include "number-match.h"
#include "format.h"
#include "selection.h"
#include "ranges.h"
#include "mstyle.h"
#include "main.h"
#include "border.h"

/*
 *   The performance of the style code is rather affected by these
 * numbers, please do send me results of various combinations using
 * real sheets and timings of --quit --test_styles.
 */
#define GROW_X               20
#define GROW_Y               20
#define STYLE_MAX_CACHE_SIZE 2048

#define STYLE_DEBUG (style_debugging > 2)

static guint32 stamp = 0;

struct _SheetStyleData {
	GList      *style_list; /* of StyleRegions */
	GHashTable *style_cache;

	Range       cached_range;
	GList      *cached_list;
};

int style_cache_hits = 0;
int style_cache_misses = 0;
int style_cache_flushes = 0;
int style_cache_range_hits = 0;

/*
 * For xml-io only.
 */
GList *
sheet_get_style_list (Sheet *sheet)
{
	g_return_val_if_fail (sheet != NULL, NULL);
	g_return_val_if_fail (sheet->style_data != NULL, NULL);

	return g_list_copy (sheet->style_data->style_list);
}

static guint
cellpos_hash (gconstpointer key)
{
	const CellPos *ep = (const CellPos *) key;

	return (ep->col << 8) ^ ep->row;
}

static gint
cellpos_compare (CellPos const * a, CellPos const * b)
{
	return (a->row == b->row &&
		a->col == b->col);
}

static inline MStyle *
sheet_style_cache_lookup (const SheetStyleData *sd, int col, int row)
{
	CellPos cp;

	if (!sd->style_cache)
		return NULL;

	cp.col = col;
	cp.row = row;

	return g_hash_table_lookup (sd->style_cache, &cp);
}

static gboolean
scache_remove (CellPos *key, MStyle *mstyle, SheetStyleData *sd)
{
	g_return_val_if_fail (key    != NULL, FALSE);
	g_return_val_if_fail (mstyle != NULL, FALSE);

	g_free (key);
	mstyle_unref (mstyle);
	return TRUE;
}

static inline void
sheet_style_cache_flush (SheetStyleData *sd, gboolean all)
{
	if (sd->style_cache) {
		if (!all) /* We are a good size, this will wrap but heh. */
			g_hash_table_freeze (sd->style_cache);
		
		g_hash_table_foreach_remove (sd->style_cache,
					     (GHRFunc)scache_remove, sd);
		style_cache_flushes++;
	}

	if (sd->cached_list)
		g_list_free (sd->cached_list);
	sd->cached_list = NULL;
}

static void
sheet_style_cache_add (SheetStyleData *sd, int col, int row,
		       MStyle *mstyle)
{
	CellPos *cp;

	if (sd->style_cache &&
	    g_hash_table_size (sd->style_cache) > STYLE_MAX_CACHE_SIZE)
		sheet_style_cache_flush (sd, FALSE);

	if (!sd->style_cache)
		sd->style_cache = g_hash_table_new ((GHashFunc)cellpos_hash,
						    (GCompareFunc)cellpos_compare);

	cp = g_new (CellPos, 1);
	cp->col = col;
	cp->row = row;
	mstyle_ref (mstyle);

	g_hash_table_insert (sd->style_cache, cp, mstyle);
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
	g_return_if_fail (sr != NULL);

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
sheet_style_region_unlink (SheetStyleData *sd, StyleRegion *region)
{
	sd->style_list = g_list_remove (sd->style_list, region);
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
sheet_style_region_link (SheetStyleData *sd, StyleRegion *region)
{
	g_return_if_fail (sd != NULL);
	g_return_if_fail (region != NULL);

	if (!list_check_sorted (sd->style_list, TRUE))
		g_warning ("order broken before insert");

	sd->style_list = g_list_insert_sorted (sd->style_list, region,
					       sheet_style_stamp_compare);
	if (!list_check_sorted (sd->style_list, TRUE))
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
	SheetStyleData *sd;

	g_return_if_fail (sheet != NULL);
	g_return_if_fail (IS_SHEET (sheet));
	g_return_if_fail (range_valid (&range));

	sd = sheet->style_data;

	if (STYLE_DEBUG)
		g_warning ("Optimize (%d, %d):(%d, %d)",
			   range.start.col, range.start.row,
			   range.end.col,   range.end.row);

	style_list = NULL;
 	/* Look in the styles applied to the sheet */
	for (l = sd->style_list; l && l->next; l = l->next) {
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
					sheet_style_region_unlink (sd, slave);
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
						sheet_style_region_unlink (sd, slave);
					} else if (STYLE_DEBUG)
						printf ("Regions adjacent but not equal\n");
				}
			}
		}
	}

	g_list_free (style_list);

	/* FIXME: shouldn't be neccessary but just in case */
	sheet_style_cache_flush (sd, TRUE);
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
	StyleRegion    *sr;
	SheetStyleData *sd;

	g_return_if_fail (sheet != NULL);
	g_return_if_fail (IS_SHEET (sheet));
	g_return_if_fail (mstyle != NULL);
	g_return_if_fail (range_valid (&range));
	g_return_if_fail (mstyle_verify (mstyle));

	sd = sheet->style_data;

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
	sd->style_list = g_list_prepend (sd->style_list, sr);

	if (STYLE_DEBUG) {
		printf ("Attaching ");
		mstyle_dump (sr->style);
		printf ("to cell ");
		range_dump (&sr->range);
		printf ("\n");
	}
	sheet_style_cache_flush (sd, TRUE);

	sheet_redraw_range (sheet, &range);
}

static inline MStyle *
sheet_mstyle_compute_from_list (GList *list, int col, int row)
{
	GList  *l;
	GList  *style_list;
	MStyle *mstyle;

	g_return_val_if_fail (list != NULL, mstyle_new_default ());

	if (!list->next) { /* Short circuit */
		StyleRegion *sr = list->data;

		mstyle_ref (sr->style);
		return sr->style;
	}

	style_list = NULL;

	for (l = list; l; l = l->next) {
		StyleRegion *sr = l->data;

		if (range_contains (&sr->range, col, row)) {
/*			if (style_debugging > 5) {
				range_dump (&sr->range);
				mstyle_dump (sr->style);
				}*/
			style_list = g_list_prepend (style_list,
						     sr->style);
		}
	}
	style_list = g_list_reverse (style_list);

	mstyle = mstyle_do_merge (style_list, MSTYLE_ELEMENT_MAX);
	g_list_free (style_list);

	return mstyle;
}

static GList *
sheet_style_list_overlap (GList *list, const Range *range)
{
	GList  *l;
	GList  *style_list = NULL;

	for (l = list; l; l = l->next) {
		StyleRegion *sr = l->data;

		if (range_overlap (&sr->range, range))
			style_list = g_list_prepend (style_list, sr);
	}

	return g_list_reverse (style_list);
}

/*
 *   Calculate a suitable range around this cell
 * that within which we can store the contained styles
 * for future reference.
 */
static void
calc_grown_range (SheetStyleData *sd, int col, int row)
{
	Range *r = &sd->cached_range;

	if (col < GROW_X) {
		r->start.col = 0;
		r->end.col   = GROW_X * 2;
	} else {
		if (col + GROW_X > SHEET_MAX_COLS - 1) {
			r->end.col   = SHEET_MAX_COLS - 1;
			r->start.col = SHEET_MAX_COLS - 1 - GROW_X * 2;
		} else {
			r->start.col = col - GROW_X;
			r->end.col   = col + GROW_X;
		}
	}

	if (row < GROW_Y) {
		r->start.row = 0;
		r->end.row   = GROW_Y * 2;
	} else {
		if (row + GROW_Y > SHEET_MAX_ROWS - 1) {
			r->end.row   = SHEET_MAX_ROWS - 1;
			r->start.row = SHEET_MAX_ROWS - 1 - GROW_Y * 2;
		} else {
			r->start.row = row - GROW_Y;
			r->end.row   = row + GROW_Y;
		}
	}

/*	r->start.col = MAX (col - GROW_X, 0);
	r->end.col   = MIN (col + GROW_X, SHEET_MAX_COLS - 1);
	r->start.row = MAX (row - GROW_Y, 0);
	r->end.row   = MIN (row + GROW_Y, SHEET_MAX_ROWS - 1);*/
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
	MStyle         *mstyle;
	SheetStyleData *sd;

/*	g_return_val_if_fail (sheet != NULL, NULL);
	g_return_val_if_fail (IS_SHEET (sheet), NULL);*/

	sd = sheet->style_data;

	if ((mstyle = sheet_style_cache_lookup (sd, col, row))) {
		mstyle_ref (mstyle);

		style_cache_hits++;

		return mstyle;
	}

	if (!list_check_sorted (sd->style_list, TRUE))
		g_warning ("Styles not sorted");

	/* Look in the styles applied to the sheet */
	if (sd->cached_list &&
	    range_contains (&sd->cached_range, col, row))

		style_cache_range_hits++;
	else {
		calc_grown_range (sd, col, row);

		if (sd->cached_list)
			g_list_free (sd->cached_list);

		sd->cached_list = sheet_style_list_overlap (sd->style_list, &sd->cached_range);

		style_cache_misses++;
	}

	mstyle = sheet_mstyle_compute_from_list (sd->cached_list,
						 col, row);

	sheet_style_cache_add (sd, col, row, mstyle);

	return mstyle;
}

/**
 * sheet_selection_apply_style:
 * @sheet: the sheet in which can be found
 * @range: the range to which should be applied
 * @style: the style
 *
 *   This routine attaches @style to the range, it swallows
 * the style reference.
 *
 */
void
sheet_range_apply_style (Sheet       *sheet,
			 const Range *range,
			 MStyle      *style)
{
	sheet_style_attach   (sheet, *range, style);
	sheet_style_optimize (sheet, *range);
	sheet_cells_update   (sheet, *range,
			      mstyle_is_element_set (style, MSTYLE_FORMAT));
	sheet_redraw_range (sheet, range);
}

void
sheet_create_styles (Sheet *sheet)
{
	SheetStyleData *sd;

	g_return_if_fail (sheet != NULL);

	sd = g_new (SheetStyleData, 1);

	sd->style_list  = NULL;
	sd->style_cache = NULL;
	sd->cached_list = NULL;

	sheet->style_data = sd;
}

void
sheet_destroy_styles (Sheet *sheet)
{
	GList          *l;
	SheetStyleData *sd;

	g_return_if_fail (sheet != NULL);
	g_return_if_fail (IS_SHEET (sheet));

	sd = sheet->style_data;

	g_return_if_fail (sd != NULL);

	sheet_style_cache_flush (sd, TRUE);

	if (sd->style_cache)
		g_hash_table_destroy (sd->style_cache);

	for (l = sd->style_list; l; l = l->next) {
		StyleRegion *sr = l->data;

		mstyle_unref (sr->style);

		sr->style = NULL;

		g_free (sr);
	}
	g_list_free (sd->style_list);
	sd->style_list = NULL;

	g_free (sd);
	sheet->style_data = NULL;
}

void
sheet_styles_dump (Sheet *sheet)
{
	GList          *l;
	int             i = 0;
	SheetStyleData *sd;

	g_return_if_fail (sheet != NULL);
	g_return_if_fail (IS_SHEET (sheet));

	sd = sheet->style_data;

	for (l = sd->style_list; l; l = l->next) {
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

	cell_calc_dimensions (cell, TRUE);

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

	/* Redraw the original region in case the span changes */
	sheet_redraw_cell_region (sheet,
				  r.start.col, r.start.row,
				  r.end.col, r.end.row);

	sheet_cell_foreach_range (sheet, TRUE,
				  r.start.col, r.start.row,
				  r.end.col, r.end.row,
				  re_dimension_cells_cb,
				  GINT_TO_POINTER (render_text));

	/* Redraw the new region in case the span changes */
	sheet_redraw_cell_region (sheet,
				  r.start.col, r.start.row,
				  r.end.col, r.end.row);
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
	SheetStyleData *sd;

	g_return_if_fail (pos >= 0);
	g_return_if_fail (count > 0);
	g_return_if_fail (sheet != NULL);
	g_return_if_fail (IS_SHEET (sheet));

	sd = sheet->style_data;

	del_range = sheet_get_full_range ();
	if (is_col) {
		del_range.start.col = pos;
		del_range.end.col   = pos + count - 1;
	} else {
		del_range.start.row = pos;
		del_range.end.row   = pos + count - 1;
	}

	/* Don't touch the last 'global' range */
	for (l = sd->style_list; l && l->next; l = next) {
		StyleRegion *sr = (StyleRegion *)l->data;

		next = g_list_next (l);

		if (is_col) {
			if (sr->range.start.col      >  del_range.end.col)
				sr->range.start.col  -= count;
			else if (sr->range.start.col >= del_range.start.col)
				sr->range.start.col  =  pos;

			if (sr->range.end.col        >  del_range.end.col)
				sr->range.end.col    -= count;
			else if (sr->range.end.col   >= del_range.start.col)
				sr->range.end.col    =  pos - 1;

			if (sr->range.start.col > sr->range.end.col ||
			    sr->range.start.col < 0 ||
			    sr->range.end.col < 0)
				sheet_style_region_unlink (sheet->style_data, sr);
		} else { /* s/col/row/ */
			if (sr->range.start.row      >  del_range.end.row)
				sr->range.start.row  -= count;
			else if (sr->range.start.row >= del_range.start.row)
				sr->range.start.row  =  pos;

			if (sr->range.end.row        >  del_range.end.row)
				sr->range.end.row    -= count;
			else if (sr->range.end.row   >= del_range.start.row)
				sr->range.end.row    =  pos - 1;

			if (sr->range.start.row > sr->range.end.row ||
			    sr->range.start.row < 0 ||
			    sr->range.end.row < 0)
				sheet_style_region_unlink (sheet->style_data, sr);
		}
	}

	sheet_style_cache_flush (sd, TRUE);
}

static void
stylish_insert_colrow (Sheet *sheet, int pos, int count, gboolean is_col)
{
 	GList *l, *next;
	gint start, end;
	SheetStyleData *sd;

	sd = sheet->style_data;
 
 	/* Don't touch the last 'global' range */
 	for (l = sd->style_list; l && l->next; l = next) {
 		StyleRegion *sr = (StyleRegion *)l->data;
 
 		next = g_list_next (l);
 
		if (is_col) {
			start = sr->range.start.col;
			end = sr->range.end.col;
		} else {
			start = sr->range.start.row;
			end = sr->range.end.row;
		}
 
		/*  We can ignore anything at least 2 cells left or above of insert */
		if (pos >= (end + 2))
 			continue;

		/* End will move for everything else. */
		end += count;

		/* Check if start should move too. */
		if (pos <= start)
			start += count;

		if (is_col) {
 			sr->range.start.col = MIN (start, SHEET_MAX_COLS - 1);
 			sr->range.end.col = MIN (end, SHEET_MAX_COLS - 1);
		} else {
 			sr->range.start.row = MIN (start, SHEET_MAX_ROWS - 1);
 			sr->range.end.row = MIN (end, SHEET_MAX_ROWS - 1);
 		}
			
	}

 	sheet_style_cache_flush (sd, TRUE);
}

static void
styleless_insert_colrow (Sheet *sheet, int pos, int count, gboolean is_col)
{
	Range  move_range, ignore_range;
	GList *l, *next;
	SheetStyleData *sd;

	sd = sheet->style_data;
 
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
	for (l = sd->style_list; l && l->next; l = next) {
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
					sheet_style_region_link (sheet->style_data, frag);
			} else {
				if (frag->range.start.row <= frag->range.end.row)
					sheet_style_region_link (sheet->style_data, frag);
			}
		}
	}

	sheet_style_cache_flush (sd, TRUE);
}

void
sheet_style_insert_colrow (Sheet *sheet, int pos, int count,
			   gboolean is_col)
{
	gboolean stylish, was_default;

	g_return_if_fail (pos >= 0);
	g_return_if_fail (count > 0);
	g_return_if_fail (sheet != NULL);
	g_return_if_fail (IS_SHEET (sheet));

	/* Check if style preserving insert option is specified. */
	gnome_config_push_prefix ("Gnumeric/Options/"); 
	stylish = gnome_config_get_bool_with_default ("StylishInsert=true", 
							&was_default);

	if (was_default) {
		gnome_config_set_bool ("StylishInsert", stylish);
		gnome_config_sync ();
	}
	gnome_config_pop_prefix ();

	if (stylish)
		stylish_insert_colrow (sheet, pos, count, is_col);
	else
		styleless_insert_colrow (sheet, pos, count, is_col);
}

void
sheet_style_relocate (const ExprRelocateInfo *rinfo)
{
	GList *stored_regions = NULL;
	GList *l, *next;
	SheetStyleData *sd;
	Range           dest;

	g_return_if_fail (rinfo != NULL);
	g_return_if_fail (rinfo->origin_sheet != NULL);
	g_return_if_fail (rinfo->target_sheet != NULL);
	g_return_if_fail (IS_SHEET (rinfo->origin_sheet));
	g_return_if_fail (IS_SHEET (rinfo->target_sheet));

	sd = rinfo->origin_sheet->style_data;

/* 1. Fragment each StyleRegion against the original range */
	for (l = sd->style_list; l && l->next; l = next) {
		StyleRegion *sr = (StyleRegion *)l->data;
		GList       *fragments;

		next = l->next;

		if (!range_overlap (&sr->range, &rinfo->origin))
			continue;

		fragments = range_split_ranges (&rinfo->origin, (Range *)sr,
						(RangeCopyFn)style_region_copy);
		stored_regions = g_list_concat (fragments, stored_regions);
		sheet_style_region_unlink (rinfo->origin_sheet->style_data, sr);
	}

/* Make sure this doesn't screw up the ranges under our feet later */
	dest = rinfo->origin;
	range_translate (&dest, rinfo->col_offset, rinfo->row_offset);
	sheet_style_attach (rinfo->target_sheet, dest, mstyle_new_default ());

/* 2 Either fold back or queue Regions */
	for (l = stored_regions; l; l = l->next) {
		StyleRegion *sr = (StyleRegion *)l->data;

/*		printf ("Stored region ");
		range_dump (&sr->range);
		printf ("\n");*/

		if (!range_overlap (&sr->range, &rinfo->origin)) {
/* 2.1 Add back the fragments */
			sheet_style_region_link (sd, sr);
		} else {
/* 2.2 Translate queued regions + re-stamp */
			range_translate (&sr->range, rinfo->col_offset, rinfo->row_offset);
			sr->stamp = stamp++;
			sheet_style_region_link (rinfo->target_sheet->style_data, sr);
		}
	}
	g_list_free (stored_regions);

	sheet_style_cache_flush (rinfo->target_sheet->style_data, TRUE);
	if (rinfo->origin_sheet != rinfo->target_sheet)
		sheet_style_cache_flush (rinfo->origin_sheet->style_data, TRUE);
}

GList *
sheet_get_styles_in_range (Sheet *sheet, const Range *r)
{
	GList *ans = NULL;
	GList *l, *next;
	SheetStyleData *sd;

	g_return_val_if_fail (sheet != NULL, NULL);
	g_return_val_if_fail (IS_SHEET (sheet), NULL);

	sd = sheet->style_data;

/* 1. Fragment each StyleRegion against the original range */
	for (l = sd->style_list; l; l = next) {
		StyleRegion *sr = (StyleRegion *)l->data;
		GList       *fragments, *f;

		next = l->next;

		if (!range_overlap (&sr->range, r))
			continue;

		fragments = range_split_ranges (r, (Range *)sr,
						(RangeCopyFn)style_region_copy);
		
/* 2. Iterate over each fragment */
		for (f = fragments; f; f = f->next) {
			StyleRegion *frag = (StyleRegion *)f->data;

/* 2.1 If it is within our range of interest keep it */
			if (range_overlap (&frag->range, r)) {
/* 2.1.1 Translate the range so it it's origin is the TLC of 'r' */
				range_translate (&frag->range,
						 - r->start.col,
						 - r->start.row);
				ans = g_list_prepend (ans, frag);
			} else
/* 2.2 Else send it packing */
				style_region_destroy (frag);
		}
		if (fragments)
			g_list_free (fragments);
	}

	return ans;
}

void
sheet_style_list_destroy (GList *list)
{
	GList *l;

	g_return_if_fail (list != NULL);

	for (l = list; l; l = g_list_next (l))
		style_region_destroy (l->data);

	g_list_free (list);
}

void
sheet_style_attach_list (Sheet *sheet, const GList *list,
			 const CellPos *corner, gboolean transpose)
{
	const GList *l;

	g_return_if_fail (sheet != NULL);
	g_return_if_fail (IS_SHEET (sheet));

	/* Sluggish but simple implementation for now */
	for (l = list; l; l = g_list_next (l)) {
		const StyleRegion *sr = l->data;
		Range              r  = sr->range;

		range_translate (&r, +corner->col, +corner->row);
		if (transpose)
			range_transpose (&r, corner);

		mstyle_ref (sr->style);
		sheet_style_attach (sheet, r, sr->style);
	}
}

static void
do_apply_border (Sheet *sheet, const Range *r,
		 MStyleElementType t, int idx, MStyleBorder **borders)
{
	MStyle *mstyle;

	if (borders [idx]) {
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

/**
 * sheet_range_set_border:
 * @sheet:
 * @range: 
 * @borders: an array of border styles to apply
 * 
 * Apply borders round the edge of a range.
 * ignore special corner cases; these are made by
 * an implicit StyleRegion overlap at present.
 *
 * 'Outer' borders are cleared if they have been set.
 *
 * The MStyleBorder allocations are not swallowed
 *
 **/
void
sheet_range_set_border (Sheet         *sheet,
			const Range   *range,
			MStyleBorder **borders)
{
	Range          r;
	MStyle        *mstyle;

	if (borders == NULL)
		return;

	if (borders [STYLE_BORDER_TOP]) {
		/* 1.1 The top inner */
		r = *range;
		r.end.row = r.start.row;
		do_apply_border (sheet, &r,
				 MSTYLE_BORDER_TOP,
				 STYLE_BORDER_TOP, borders);

		/* 1.2 The top outer */
		r.start.row--;
		r.end.row = r.start.row;
		if (r.start.row >= 0)
			do_blank_border (sheet, &r, MSTYLE_BORDER_BOTTOM);
	}

	/* 2   We prefer to paint Top of the row below */ 
	if (borders [STYLE_BORDER_BOTTOM]) {
		r = *range;
		r.start.row = r.end.row;
		if (r.start.row < SHEET_MAX_ROWS-1) {
			/* 2.1 The bottom outer */
			do_blank_border (sheet, &r, MSTYLE_BORDER_BOTTOM);

			/* 2.1 The bottom inner */
			++r.end.row;
			r.start.row = r.end.row;
			do_apply_border (sheet, &r,
					 MSTYLE_BORDER_TOP,
					 STYLE_BORDER_BOTTOM, borders);
		} else
			do_apply_border (sheet, &r,
					 MSTYLE_BORDER_BOTTOM,
					 STYLE_BORDER_BOTTOM, borders);
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
	/* 4   We prefer to paint left of the col to the right */ 
	if (borders [STYLE_BORDER_RIGHT]) {
		r = *range;
		r.start.col = r.end.col;
		if (r.start.col < SHEET_MAX_COLS-1) {
			/* 2.1 The bottom outer */
			do_blank_border (sheet, &r, MSTYLE_BORDER_RIGHT);

			/* 2.1 The bottom inner */
			++r.end.col;
			r.start.col = r.end.col;
			do_apply_border (sheet, &r,
					 MSTYLE_BORDER_LEFT,
					 STYLE_BORDER_RIGHT, borders);
		} else
			do_apply_border (sheet, &r,
					 MSTYLE_BORDER_RIGHT,
					 STYLE_BORDER_RIGHT, borders);
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
		style_border_ref (borders  [STYLE_BORDER_DIAG]);
		mstyle_set_border (mstyle, MSTYLE_BORDER_DIAGONAL,
				   borders [STYLE_BORDER_DIAG]);
	}
	if (borders [STYLE_BORDER_REV_DIAG]) {
		style_border_ref (borders  [STYLE_BORDER_REV_DIAG]);
		mstyle_set_border (mstyle, MSTYLE_BORDER_REV_DIAGONAL,
				   borders [STYLE_BORDER_REV_DIAG]);
	}
	if (mstyle_empty (mstyle))
		mstyle_unref (mstyle);
	else
		sheet_style_attach (sheet, *range, mstyle);

	sheet_style_optimize (sheet, *range);
	sheet_redraw_range (sheet, range);
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
	all_list = sheet_get_region_list_for_range (sheet->style_data->style_list, &all);

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
	if (middle_valid) {
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
	}

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
	g_list_free (middle_list);
	g_list_free (all_list);
	for (i = STYLE_BORDER_TOP; i <= STYLE_BORDER_RIGHT; i++)
		g_list_free (edge_list [i]);

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
	GList          *l;
	SheetStyleData *sd;

	sd = sheet->style_data;

	for (l = sd->style_list; l && l->next; l = g_list_next (l)) {
		Range *sr = (Range *)l->data;

		if (!range_is_infinite (sr))
			*r = range_union (r, sr);
	}
}
