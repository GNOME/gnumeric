/*
 * sheet-merge.c: merged cell support
 *
 * Copyright (C) 2000-2002 Jody Goldberg (jody@gnome.org)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) version 3.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301
 * USA
 */
#include <gnumeric-config.h>
#include <glib/gi18n-lib.h>
#include <gnumeric.h>
#include <sheet-merge.h>

#include <sheet-object.h>
#include <sheet.h>
#include <sheet-view.h>
#include <sheet-private.h>
#include <ranges.h>
#include <cell.h>
#include <cellspan.h>
#include <sheet-style.h>
#include <mstyle.h>
#include <expr.h>
#include <command-context.h>

static gint
range_row_cmp (GnmRange const *a, GnmRange const *b)
{
	int tmp = b->start.row - a->start.row;
	if (tmp == 0)
		tmp = a->start.col - b->start.col; /* YES I DO MEAN a - b */
	return tmp;
}

/**
 * gnm_sheet_merge_add:
 * @sheet: the sheet which will contain the region
 * @r: The region to merge
 * @clear: should the non-corner content of the region be cleared and the
 *          style from the corner applied.
 * @cc: (nullable): the calling context
 *
 * Add a range to the list of merge targets.  Checks for array splitting returns
 * %TRUE if there was an error.  Queues a respan.  Only queues a redraw if @clear
 * is %TRUE.
 */
gboolean
gnm_sheet_merge_add (Sheet *sheet, GnmRange const *r, gboolean clear,
		     GOCmdContext *cc)
{
	GSList *test;
	GnmRange  *r_copy;
	GnmCell   *cell;
	GnmComment *comment;
	GnmRange r2;

	g_return_val_if_fail (IS_SHEET (sheet), TRUE);
	g_return_val_if_fail (range_is_sane (r), TRUE);
	g_return_val_if_fail (r->end.col < gnm_sheet_get_max_cols (sheet), TRUE);
	g_return_val_if_fail (r->end.row < gnm_sheet_get_max_rows (sheet), TRUE);

	r2 = *r;
	range_ensure_sanity (&r2, sheet);

	if (sheet_range_splits_array (sheet, &r2, NULL, cc, _("Merge")))
		return TRUE;

	test = gnm_sheet_merge_get_overlap (sheet, &r2);
	if (test != NULL) {
		if (cc != NULL)
			go_cmd_context_error (cc, g_error_new (go_error_invalid(), 0,
				_("There is already a merged region that intersects\n%s!%s"),
				sheet->name_unquoted, range_as_string (&r2)));
		g_slist_free (test);
		return TRUE;
	}

	if (clear) {
		int i;
		GnmStyle *style;

		sheet_redraw_range (sheet, &r2);

		/* Clear the non-corner content */
		if (r2.start.col != r2.end.col)
			sheet_clear_region (sheet,
					    r2.start.col+1, r2.start.row,
					    r2.end.col, r2.end.row,
					    CLEAR_VALUES | CLEAR_COMMENTS | CLEAR_NOCHECKARRAY | CLEAR_NORESPAN,
					    cc);
		if (r2.start.row != r2.end.row)
			sheet_clear_region (sheet,
					    r2.start.col, r2.start.row+1,
	    /* yes I mean start.col */	    r2.start.col, r2.end.row,
					    CLEAR_VALUES | CLEAR_COMMENTS | CLEAR_NOCHECKARRAY | CLEAR_NORESPAN,
					    cc);

		/* Apply the corner style to the entire region */
		style = gnm_style_dup (sheet_style_get (sheet, r2.start.col,
						      r2.start.row));
		for (i = MSTYLE_BORDER_TOP; i <= MSTYLE_BORDER_DIAGONAL; i++)
			gnm_style_unset_element (style, i);
		sheet_style_apply_range (sheet, &r2, style);
		sheet_region_queue_recalc (sheet, &r2);
	}

	r_copy = gnm_range_dup (&r2);
	g_hash_table_insert (sheet->hash_merged, &r_copy->start, r_copy);

	/* Store in order from bottom to top then LEFT TO RIGHT (by start coord) */
	sheet->list_merged = g_slist_insert_sorted (sheet->list_merged, r_copy,
						    (GCompareFunc)range_row_cmp);

	cell = sheet_cell_get (sheet, r2.start.col, r2.start.row);
	if (cell != NULL) {
		cell->base.flags |= GNM_CELL_IS_MERGED;
		cell_unregister_span (cell);
	}
	sheet_queue_respan (sheet, r2.start.row, r2.end.row);

	/* Ensure that edit pos is not in the center of a region. */
	SHEET_FOREACH_VIEW (sheet, sv, {
		sv->reposition_selection = TRUE;
		if (range_contains (&r2, sv->edit_pos.col, sv->edit_pos.row))
			gnm_sheet_view_set_edit_pos (sv, &r2.start);
	});

	comment = sheet_get_comment (sheet, &r2.start);
	if (comment != NULL)
		sheet_object_update_bounds (GNM_SO (comment), NULL);

	sheet_flag_status_update_range (sheet, &r2);
	if (sheet->cols.max_used < r2.end.col) {
		sheet->cols.max_used = r2.end.col;
		sheet->priv->resize_scrollbar = TRUE;
	}
	if (sheet->rows.max_used < r2.end.row) {
		sheet->rows.max_used = r2.end.row;
		sheet->priv->resize_scrollbar = TRUE;
	}
	return FALSE;
}

/**
 * gnm_sheet_merge_remove:
 * @sheet: the sheet which will contain the region
 * @r: The region
 *
 * Remove a merged range.
 * Queues a redraw.
 *
 * Returns: %TRUE if there was an error.
 **/
gboolean
gnm_sheet_merge_remove (Sheet *sheet, GnmRange const *r)
{
	GnmRange   *r_copy;
	GnmCell    *cell;
	GnmComment *comment;

	g_return_val_if_fail (IS_SHEET (sheet), TRUE);
	g_return_val_if_fail (r != NULL, TRUE);

	r_copy = g_hash_table_lookup (sheet->hash_merged, &r->start);

	g_return_val_if_fail (r_copy != NULL, TRUE);
	g_return_val_if_fail (range_equal (r, r_copy), TRUE);

	g_hash_table_remove (sheet->hash_merged, &r_copy->start);
	sheet->list_merged = g_slist_remove (sheet->list_merged, r_copy);

	cell = sheet_cell_get (sheet, r->start.col, r->start.row);
	if (cell != NULL)
		cell->base.flags &= ~GNM_CELL_IS_MERGED;

	comment = sheet_get_comment (sheet, &r->start);
	if (comment != NULL)
		sheet_object_update_bounds (GNM_SO (comment), NULL);

	sheet_redraw_range (sheet, r);
	sheet_flag_status_update_range (sheet, r);
	SHEET_FOREACH_VIEW (sheet, sv, sv->reposition_selection = TRUE;);
	g_free (r_copy);
	return FALSE;
}

/**
 * gnm_sheet_merge_get_overlap:
 *
 * Returns: (element-type GnmRange) (transfer container): a list of the merged
 * regions that overlap the target region.
 * The list is ordered from top to bottom and RIGHT TO LEFT (by start coord).
 */
GSList *
gnm_sheet_merge_get_overlap (Sheet const *sheet, GnmRange const *range)
{
	GSList *ptr, *res = NULL;

	g_return_val_if_fail (IS_SHEET (sheet), NULL);
	g_return_val_if_fail (range != NULL, NULL);

	for (ptr = sheet->list_merged ; ptr != NULL ; ptr = ptr->next) {
		GnmRange * const test = ptr->data;

		if (range_overlap (range, test))
			res = g_slist_prepend (res, test);
	}

	return res;
}

/**
 * gnm_sheet_merge_contains_pos:
 * @sheet: #Sheet to query
 * @pos: Position to look for a merged range.
 *
 * Returns: (transfer none) (nullable): the merged range covering @pos, or
 * %NULL if @pos is not within a merged region.
 */
GnmRange const *
gnm_sheet_merge_contains_pos (Sheet const *sheet, GnmCellPos const *pos)
{
	GSList *ptr;

	g_return_val_if_fail (IS_SHEET (sheet), NULL);
	g_return_val_if_fail (pos != NULL, NULL);

	for (ptr = sheet->list_merged ; ptr != NULL ; ptr = ptr->next) {
		GnmRange const * const range = ptr->data;
		if (range_contains (range, pos->col, pos->row))
			return range;
	}
	return NULL;
}

/**
 * gnm_sheet_merge_get_adjacent:
 * @sheet: The sheet to look in.
 * @pos: the cell to test for adjacent regions.
 * @left: (out): the return for a region on the left
 * @right: (out): the return for a region on the right
 *
 * Returns the nearest regions to either side of @pos.
 */
void
gnm_sheet_merge_get_adjacent (Sheet const *sheet, GnmCellPos const *pos,
			      GnmRange const **left, GnmRange const **right)
{
	GSList *ptr;

	g_return_if_fail (IS_SHEET (sheet));
	g_return_if_fail (pos != NULL);

	*left = *right = NULL;
	for (ptr = sheet->list_merged ; ptr != NULL ; ptr = ptr->next) {
		GnmRange const * const test = ptr->data;
		if (test->start.row <= pos->row && pos->row <= test->end.row) {
			int const diff = test->end.col - pos->col;

			g_return_if_fail (diff != 0);

			if (diff < 0) {
				if (*left == NULL || (*left)->end.col < test->end.col)
					*left = test;
			} else {
				if (*right == NULL || (*right)->start.col > test->start.col)
					*right = test;
			}
		}
	}
}

/**
 * gnm_sheet_merge_is_corner:
 * @sheet: #Sheet to query
 * @pos: cellpos if top left corner
 *
 * Returns: (transfer none): a merged #GnmRange covering @pos if it is the
 * top-left corner of a merged region.
 */
GnmRange const *
gnm_sheet_merge_is_corner (Sheet const *sheet, GnmCellPos const *pos)
{
	g_return_val_if_fail (IS_SHEET (sheet), NULL);
	g_return_val_if_fail (pos != NULL, NULL);

	return g_hash_table_lookup (sheet->hash_merged, pos);
}

static void
cb_restore_merge (Sheet *sheet, GSList *restore)
{
	GSList *l;
	for (l = restore; l; l = l->next) {
		GnmRange const *r = l->data;
		GnmRange const *r2 = g_hash_table_lookup (sheet->hash_merged,
							  &r->start);
		if (r2 && range_equal (r, r2))
			continue;

		// The only reason for r2 to be different from r is that we
		// clipped.  Moving the clipped region back didn't restore
		// the old state, so we'll have to remove the merge and
		// create a new.
		if (r2)
			gnm_sheet_merge_remove (sheet, r2);

		gnm_sheet_merge_add (sheet, r, FALSE, NULL);
	}
}

static void
cb_restore_list_free (GSList *restore)
{
	g_slist_free_full (restore, g_free);
}

/**
 * gnm_sheet_merge_relocate:
 * @ri: Descriptor of what is moving.
 * @pundo: (out) (optional) (transfer full): Undo information.
 *
 * Shifts merged regions that need to move.
 */
void
gnm_sheet_merge_relocate (GnmExprRelocateInfo const *ri, GOUndo **pundo)
{
	GSList   *ptr, *copy, *reapply = NULL, *restore = NULL;
	GnmRange	 dest;
	gboolean change_sheets;

	g_return_if_fail (ri != NULL);
	g_return_if_fail (IS_SHEET (ri->origin_sheet));
	g_return_if_fail (IS_SHEET (ri->target_sheet));

	dest = ri->origin;
	range_translate (&dest, ri->target_sheet, ri->col_offset, ri->row_offset);
	change_sheets = (ri->origin_sheet != ri->target_sheet);

	/* Clear the destination range on the target sheet */
	if (change_sheets) {
		copy = g_slist_copy (ri->target_sheet->list_merged);
		for (ptr = copy; ptr != NULL ; ptr = ptr->next) {
			GnmRange const *r = ptr->data;
			if (range_contains (&dest, r->start.col, r->start.row))
				gnm_sheet_merge_remove (ri->target_sheet, r);
		}
		g_slist_free (copy);
	}

	copy = g_slist_copy (ri->origin_sheet->list_merged);
	for (ptr = copy; ptr != NULL ; ptr = ptr->next ) {
		GnmRange const *r = ptr->data;
		GnmRange r0 = *r; // Copy because removal invalidates r
		GnmRange r2 = *r;
		gboolean needs_restore = FALSE;
		gboolean needs_reapply = FALSE;

		if (range_contains (&ri->origin, r->start.col, r->start.row)) {
			range_translate (&r2, ri->target_sheet,
					 ri->col_offset, ri->row_offset);
			range_ensure_sanity (&r2, ri->target_sheet);

			gnm_sheet_merge_remove (ri->origin_sheet, r);
			if (range_is_singleton (&r2))
				needs_restore = TRUE;
			else if (r2.start.col <= r2.end.col &&
				 r2.start.row <= r2.end.row) {
				needs_restore = TRUE;
				needs_reapply = TRUE;
			} else {
				// Completely deleted.
			}
		} else if (range_contains (&ri->origin, r->end.col, r->end.row)) {
			r2.end.col += ri->col_offset;
			r2.end.row += ri->row_offset;
			range_ensure_sanity (&r2, ri->target_sheet);
			gnm_sheet_merge_remove (ri->origin_sheet, r);
			needs_restore = TRUE;
			needs_reapply = !range_is_singleton (&r2);
		} else if (!change_sheets &&
			   range_contains (&dest, r->start.col, r->start.row))
			gnm_sheet_merge_remove (ri->origin_sheet, r);

		if (needs_reapply)
			reapply = g_slist_prepend (reapply,
						   gnm_range_dup (&r2));
		if (needs_restore && pundo)
			restore = g_slist_prepend (restore,
						   gnm_range_dup (&r0));
	}
	g_slist_free (copy);

	// Reapply surviving, changed ranges.
	for (ptr = reapply ; ptr != NULL ; ptr = ptr->next) {
		GnmRange *dest = ptr->data;
		gnm_sheet_merge_add (ri->target_sheet, dest, TRUE, NULL);
		g_free (dest);
	}
	g_slist_free (reapply);

	if (restore) {
		GOUndo *u = go_undo_binary_new
			(ri->origin_sheet, restore,
			 (GOUndoBinaryFunc)cb_restore_merge,
			 NULL,
			 (GFreeFunc)cb_restore_list_free);
		*pundo = go_undo_combine (*pundo, u);
	}
}

/**
 * gnm_sheet_merge_find_bounding_box:
 * @sheet: sheet
 * @r: the range to extend
 *
 * Extends @r such that no merged range is split by its boundary.
 */
void
gnm_sheet_merge_find_bounding_box (Sheet const *sheet, GnmRange *target)
{
	gboolean changed;
	GSList *merged, *ptr;

	/* expand to include any merged regions */
	do {
		changed = FALSE;
		merged = gnm_sheet_merge_get_overlap (sheet, target);
		for (ptr = merged ; ptr != NULL ; ptr = ptr->next) {
			GnmRange const *r = ptr->data;
			if (target->start.col > r->start.col) {
				target->start.col = r->start.col;
				changed = TRUE;
			}
			if (target->start.row > r->start.row) {
				target->start.row = r->start.row;
				changed = TRUE;
			}
			if (target->end.col < r->end.col) {
				target->end.col = r->end.col;
				changed = TRUE;
			}
			if (target->end.row < r->end.row) {
				target->end.row = r->end.row;
				changed = TRUE;
			}
		}
		g_slist_free (merged);
	} while (changed);

}
