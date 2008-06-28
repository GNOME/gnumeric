/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * clipboard.c: A temporary store for contents from a worksheet
 *
 * Copyright (C) 2000-2008 Jody Goldberg   (jody@gnome.org)
 *		 1999      Miguel de Icaza (miguel@gnu.org)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
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
#include "gnumeric.h"
#include "clipboard.h"

#include "sheet.h"
#include "cell.h"
#include "sheet-style.h"
#include "sheet-merge.h"
#include "dependent.h"
#include "selection.h"
#include "command-context.h"
#include "workbook-control.h"
#include "workbook.h"
#include "ranges.h"
#include "colrow.h"
#include "expr.h"
#include "value.h"
#include "mstyle.h"
#include "stf-parse.h"
#include "gnm-format.h"
#include "sheet-object-cell-comment.h"

#include <glib/gi18n-lib.h>
#include <locale.h>
#include <string.h>
#include <goffice/utils/go-glib-extras.h>
#include <goffice/utils/datetime.h>

#ifndef USE_CELL_COPY_POOLS
#define USE_CELL_COPY_POOLS 1
#endif

#if USE_CELL_COPY_POOLS
/* Memory pool for GnmCellCopy.  */
static GOMemChunk *cell_copy_pool;
#define CHUNK_ALLOC(T,p) ((T*)go_mem_chunk_alloc (p))
#define CHUNK_FREE(p,v) go_mem_chunk_free ((p), (v))
#else
#define CHUNK_ALLOC(T,c) g_new (T,1)
#define CHUNK_FREE(p,v) g_free ((v))
#endif

static gboolean
cell_has_expr_or_number_or_blank (GnmCell const * cell)
{
	return (gnm_cell_is_empty (cell) ||
		(cell != NULL && gnm_cell_is_number (cell)) ||
		(cell != NULL && gnm_cell_has_expr (cell)));
}

static GnmExpr const *
contents_as_expr (GnmExprTop const *texpr, GnmValue const *val)
{
	if (texpr)
		return gnm_expr_copy (texpr->expr);
	if (VALUE_IS_EMPTY (val))
		return gnm_expr_new_constant (value_new_float (0.0));
	if (VALUE_IS_NUMBER (val))
		return gnm_expr_new_constant (value_dup (val));
	return NULL;
}

static GnmExprOp
paste_op_to_expr_op (int paste_flags)
{
	g_return_val_if_fail (paste_flags & PASTE_OPER_MASK, 0);

	if (paste_flags & PASTE_OPER_ADD)
		return GNM_EXPR_OP_ADD;
	else if (paste_flags & PASTE_OPER_SUB)
		return GNM_EXPR_OP_SUB;
	else if (paste_flags & PASTE_OPER_MULT)
		return GNM_EXPR_OP_MULT;
	else if (paste_flags & PASTE_OPER_DIV)
		return GNM_EXPR_OP_DIV;

	return 0;
}

static void
paste_cell_with_operation (Sheet *dst_sheet,
			   int target_col, int target_row,
			   GnmExprRelocateInfo const *rinfo,
			   GnmCellCopy const *src,
			   int paste_flags)
{
	GnmCell *dst;
	GnmExprOp op;

	if (src->texpr == NULL &&
	    !VALUE_IS_EMPTY (src->val) &&
	    !VALUE_IS_NUMBER (src->val))
		return;

	dst = sheet_cell_fetch (dst_sheet, target_col, target_row);
	if (!cell_has_expr_or_number_or_blank (dst))
		return;

	op = paste_op_to_expr_op (paste_flags);
	/* FIXME : This does not handle arrays, linked cells, ranges, etc. */
	if ((paste_flags & PASTE_CONTENTS) &&
	    (NULL != src->texpr || gnm_cell_has_expr (dst))) {
		GnmExpr const *old_expr    = contents_as_expr (dst->base.texpr, dst->value);
		GnmExpr const *copied_expr = contents_as_expr (src->texpr, src->val);
		GnmExprTop const *res = gnm_expr_top_new (gnm_expr_new_binary (old_expr, op, copied_expr));
		GnmExprTop const *relo = gnm_expr_top_relocate (res, rinfo, FALSE);
		if (relo) {
			gnm_cell_set_expr (dst, relo);
			gnm_expr_top_unref (relo);
		} else
			gnm_cell_set_expr (dst, res);
		gnm_expr_top_unref (res);
	} else {
		GnmValue  *value;
		GnmEvalPos pos;
		GnmExpr const *expr = gnm_expr_new_binary (
			gnm_expr_new_constant (value_dup (dst->value)),
			op,
			gnm_expr_new_constant (value_dup (src->val)));

		eval_pos_init_cell (&pos, dst);
		pos.dep = NULL; /* no dynamic deps */
		value = gnm_expr_eval (expr, &pos,
				       GNM_EXPR_EVAL_SCALAR_NON_EMPTY);
		gnm_expr_free (expr);
		gnm_cell_set_value (dst, value);
	}
}

/* NOTE : Make sure to set up any merged regions in the target range BEFORE
 * this is called.
 */
static void
paste_link (GnmPasteTarget const *pt, int top, int left,
	    GnmCellRegion const *cr)
{
	GnmCellPos pos;
	GnmCellRef source_cell_ref;
	int x, y;

	/* Not possible to link to arbitrary (non gnumeric) sources yet. */
	/* TODO : eventually support interprocess gnumeric links */
	if (cr->origin_sheet == NULL)
		return;

	/* TODO : support relative links ? */
	source_cell_ref.col_relative = 0;
	source_cell_ref.row_relative = 0;
	source_cell_ref.sheet = (cr->origin_sheet != pt->sheet)
		? cr->origin_sheet : NULL;
	pos.col = left;
	for (x = 0 ; x < cr->cols ; x++, pos.col++) {
		source_cell_ref.col = cr->base.col + x;
		pos.row = top;
		for (y = 0 ; y < cr->rows ; y++, pos.row++) {
			GnmExprTop const *texpr;
			GnmCell *cell =
				sheet_cell_fetch (pt->sheet, pos.col, pos.row);

			/* This could easily be made smarter */
			if (!gnm_cell_is_merged (cell) &&
			    gnm_sheet_merge_contains_pos (pt->sheet, &pos))
					continue;
			source_cell_ref.row = cr->base.row + y;
			texpr = gnm_expr_top_new (gnm_expr_new_cellref (&source_cell_ref));
			gnm_cell_set_expr (cell, texpr);
			gnm_expr_top_unref (texpr);
		}
	}
}

struct paste_cell_data {
	GnmPasteTarget const *pt;
	GnmCellRegion const  *cr;
	GnmCellPos	top_left;
	GnmExprRelocateInfo rinfo;
	gboolean translate_dates;
};

/**
 * paste_cell: Pastes a cell in the spreadsheet
 * @dst_sheet:   The sheet where the pasting will be done
 * @target_col:  Column to put the cell into
 * @target_row:  Row to put the cell into.
 * @rinfo:	 A #GnmExprRelocateInfo on how to relocate content
 * @src:         A #GnmCelCopy with the content to paste
 * @paste_flags: Bit mask that describes the paste options.
 */
static void
paste_cell (int target_col, int target_row,
	    GnmCellCopy const *src,
	    const struct paste_cell_data *dat)
{
	Sheet *dst_sheet = dat->pt->sheet;
	int paste_flags = dat->pt->paste_flags;

	if (paste_flags & PASTE_OPER_MASK)
		paste_cell_with_operation (dst_sheet, target_col, target_row,
					   &dat->rinfo, src, paste_flags);
	else {
		GnmCell *dst = sheet_cell_fetch (dst_sheet, target_col, target_row);
		if (NULL != src->texpr && (paste_flags & PASTE_CONTENTS)) {
			GnmExprTop const *relo = gnm_expr_top_relocate (
				src->texpr, &dat->rinfo, FALSE);
			if (paste_flags & PASTE_TRANSPOSE) {
				GnmExprTop const *trelo =
					gnm_expr_top_transpose (relo ? relo : src->texpr);
				if (trelo) {
					if (relo)
						gnm_expr_top_unref (relo);
					relo = trelo;
				}
			} else if (!relo && gnm_expr_top_is_array_corner (src->texpr)) {
				/* We must not share array expressions.  */
				relo = gnm_expr_top_new (gnm_expr_copy (src->texpr->expr));
			}
			gnm_cell_set_expr_and_value (dst, relo ? relo : src->texpr,
						 value_dup (src->val), TRUE);
			if (NULL != relo)
				gnm_expr_top_unref (relo);
		} else {
			GnmValue *newval = NULL;
			GnmValue const *oldval = src->val;

			if (dat->translate_dates && oldval && VALUE_IS_FLOAT (oldval)) {
				GOFormat const *fmt = VALUE_FMT (oldval)
					? VALUE_FMT (oldval)
					: gnm_style_get_format (gnm_cell_get_style (dst));
				if (go_format_is_date (fmt) == +1) {
					gnm_float fnew = go_date_conv_translate
						(value_get_as_float (oldval),
						 dat->cr->date_conv,
						 workbook_date_conv (dst_sheet->workbook));
					newval = value_new_float (fnew);
					value_set_fmt (newval, VALUE_FMT (oldval));
				}
			}

			if (!newval)
				newval = value_dup (src->val);
			gnm_cell_set_value (dst, newval);
		}
	}
}

static void
paste_object (GnmPasteTarget const *pt, SheetObject const *src, int left, int top)
{
	SheetObject *dst;
	SheetObjectAnchor tmp;

	sheet_object_anchor_assign (&tmp, sheet_object_get_anchor (src));
	if (G_OBJECT_TYPE (src) == CELL_COMMENT_TYPE) {
		if ((pt->paste_flags & PASTE_COMMENTS) &&
		    (pt->paste_flags & PASTE_IGNORE_COMMENTS_AT_ORIGIN &&
		     tmp.cell_bound.start.col == 0  &&
		     tmp.cell_bound.start.row == 0))
			return;
	} else if (!(pt->paste_flags & PASTE_OBJECTS))
		return;

	if (NULL == (dst = sheet_object_dup (src)))
		return;

	if (pt->paste_flags & PASTE_TRANSPOSE) {
		GnmCellPos origin;
		origin.col = 0;
		origin.row = 0;
		range_transpose (&tmp.cell_bound, &origin);
	}
	range_translate (&tmp.cell_bound, left, top);
	sheet_object_set_anchor (dst, &tmp);
	sheet_object_set_sheet (dst, pt->sheet);
	g_object_unref (dst);
}

static void
cb_paste_cell (GnmCellCopy const *src, gconstpointer ignore,
	       struct paste_cell_data *dat)
{
	int target_col = dat->top_left.col;
	int target_row = dat->top_left.row;

	if (dat->pt->paste_flags & PASTE_TRANSPOSE) {
		target_col += src->offset.row;
		target_row += src->offset.col;
	} else {
		target_col += src->offset.col;
		target_row += src->offset.row;
	}

	dat->rinfo.pos.sheet = dat->pt->sheet;
	if (dat->pt->paste_flags & PASTE_EXPR_LOCAL_RELOCATE) {
		dat->rinfo.pos.eval.col = dat->cr->base.col + src->offset.col;
		dat->rinfo.pos.eval.row = dat->cr->base.row + src->offset.row;
	} else {
		dat->rinfo.pos.eval.col = target_col;
		dat->rinfo.pos.eval.row = target_row;
	}

	paste_cell (target_col, target_row, src, dat);
}


/**
 * clipboard_paste_region:
 * @cr : The GnmCellRegion to paste.
 * @pt : Where to paste the values.
 * @cc : The context for error handling.
 *
 * Pastes the supplied GnmCellRegion (@cr) into the supplied
 * GnmPasteTarget (@pt).  This operation is not undoable.  It does not auto grow
 * the destination if the target is a singleton.  This is a simple interface to
 * paste a region.
 *
 * returns : TRUE if there was a problem.
 **/
gboolean
clipboard_paste_region (GnmCellRegion const *cr,
			GnmPasteTarget const *pt,
			GOCmdContext *cc)
{
	int repeat_horizontal, repeat_vertical, clearFlags;
	int dst_cols, dst_rows, src_cols, src_rows;
	int i, j;
	GSList *ptr;
	GnmRange const *r;
	gboolean has_contents, adjust_merges = TRUE;
	struct paste_cell_data dat;

	g_return_val_if_fail (pt != NULL, TRUE);
	g_return_val_if_fail (cr != NULL, TRUE);

	/* we do not need any of this fancy stuff when pasting a simple object */
	if (cr->cell_content == NULL &&
	    cr->styles == NULL &&
	    cr->merged == NULL &&
	    cr->objects != NULL) {
		if (pt->paste_flags & (PASTE_COMMENTS | PASTE_OBJECTS))
			for (ptr = cr->objects; ptr; ptr = ptr->next)
				paste_object (pt, ptr->data,
					pt->range.start.col, pt->range.start.row);
		return FALSE;
	}

	r = &pt->range;
	dst_cols = range_width (r);
	dst_rows = range_height (r);
	src_cols = cr->cols;
	src_rows = cr->rows;

	/* If the source is a single cell */
	/* Treat a target of a single merge specially, don't split the merge */
	if (src_cols == 1 && src_rows == 1) {
		GnmRange const *merge = gnm_sheet_merge_is_corner (pt->sheet, &r->start);
		if (merge != NULL && range_equal (r, merge)) {
			dst_cols = dst_rows = 1;
			adjust_merges = FALSE;
		}
	/* Apparently links do not supercede merges */
	} else if (pt->paste_flags & PASTE_LINK)
		adjust_merges = FALSE;

	has_contents = pt->paste_flags & (PASTE_CONTENTS|PASTE_AS_VALUES|PASTE_LINK);

	if (pt->paste_flags & PASTE_TRANSPOSE) {
		int tmp = src_cols;
		src_cols = src_rows;
		src_rows = tmp;
	}

	if (cr->not_as_contents && (pt->paste_flags & PASTE_CONTENTS)) {
		go_cmd_context_error_invalid (cc,
					_("Unable to paste"),
					_("Contents can only be pasted by value or by link."));
		return TRUE;
	}

	/* calculate the tiling */
	repeat_horizontal = dst_cols/src_cols;
	if (repeat_horizontal * src_cols != dst_cols) {
		char *msg = g_strdup_printf (
			_("destination does not have an even multiple of source columns (%d vs %d)\n\n"
			  "Try selecting a single cell or an area of the same shape and size."),
			dst_cols, src_cols);
		go_cmd_context_error_invalid (cc, _("Unable to paste"), msg);
		g_free (msg);
		return TRUE;
	}

	repeat_vertical = dst_rows/src_rows;
	if (repeat_vertical * src_rows != dst_rows) {
		char *msg = g_strdup_printf (
			_("destination does not have an even multiple of source rows (%d vs %d)\n\n"
			  "Try selecting a single cell or an area of the same shape and size."),
			dst_rows, src_rows);
		go_cmd_context_error_invalid (cc, _("Unable to paste"), msg);
		g_free (msg);
		return TRUE;
	}

	if ((pt->range.start.col + dst_cols) > gnm_sheet_get_max_cols (pt->sheet) ||
	    (pt->range.start.row + dst_rows) > gnm_sheet_get_max_rows (pt->sheet)) {
		go_cmd_context_error_invalid (cc,
					_("Unable to paste"),
					_("result passes the sheet boundary"));
		return TRUE;
	}

	clearFlags = 0;
	/* clear the region where we will paste */
	if (has_contents)
		clearFlags = CLEAR_VALUES | CLEAR_NORESPAN;

	if (pt->paste_flags & PASTE_COMMENTS)
		clearFlags |= CLEAR_COMMENTS;

	/* No need to clear the formats.  We will paste over top of these. */
	/* if (pt->paste_flags & PASTE_FORMATS) clearFlags |= CLEAR_FORMATS; */

	if (pt->paste_flags & (PASTE_OPER_MASK | PASTE_SKIP_BLANKS))
		clearFlags = 0;

	/* remove merged regions even for operations, or blanks */
	if (has_contents && adjust_merges)
		clearFlags |= CLEAR_MERGES;

	if (clearFlags != 0) {
		int const dst_col = pt->range.start.col;
		int const dst_row = pt->range.start.row;
		sheet_clear_region (pt->sheet,
				    dst_col, dst_row,
				    dst_col + dst_cols - 1,
				    dst_row + dst_rows - 1,
				    clearFlags, cc);
	}

	dat.translate_dates = cr->date_conv &&
		!go_date_conv_equal (cr->date_conv, workbook_date_conv (pt->sheet->workbook));

	for (i = 0; i < repeat_horizontal ; i++)
		for (j = 0; j < repeat_vertical ; j++) {
			int const left = i * src_cols + pt->range.start.col;
			int const top = j * src_rows + pt->range.start.row;

			dat.top_left.col = left;
			dat.top_left.row = top;
			dat.rinfo.reloc_type = GNM_EXPR_RELOCATE_MOVE_RANGE;
			dat.rinfo.origin_sheet = dat.rinfo.target_sheet = pt->sheet;
			if (pt->paste_flags & PASTE_EXPR_LOCAL_RELOCATE) {
				dat.rinfo.origin.start = cr->base;
				dat.rinfo.origin.end.col = cr->base.col + cr->cols - 1;
				dat.rinfo.origin.end.row = cr->base.row + cr->rows - 1;
				dat.rinfo.col_offset = left - cr->base.col;
				dat.rinfo.row_offset = top - cr->base.row;
			} else {
				dat.rinfo.origin = pt->range;
				dat.rinfo.col_offset = 0;
				dat.rinfo.row_offset = 0;
			}

			/* Move the styles on here so we get correct formats before recalc */
			if (pt->paste_flags & PASTE_FORMATS)
				sheet_style_set_list (pt->sheet, &dat.top_left,
					(pt->paste_flags & PASTE_TRANSPOSE),
					cr->styles);

			if (has_contents && !(pt->paste_flags & PASTE_DONT_MERGE)) {
				for (ptr = cr->merged; ptr != NULL ; ptr = ptr->next) {
					GnmRange tmp = *((GnmRange const *)ptr->data);
					if (pt->paste_flags & PASTE_TRANSPOSE) {
						int x;
						x = tmp.start.col; tmp.start.col = tmp.start.row;  tmp.start.row = x;
						x = tmp.end.col; tmp.end.col = tmp.end.row;  tmp.end.row = x;
					}
					if (!range_translate (&tmp, left, top))
						gnm_sheet_merge_add (pt->sheet, &tmp, TRUE, cc);
				}
			}

			if (has_contents && (pt->paste_flags & PASTE_LINK)) {
				paste_link (pt, top, left, cr);
				continue;
			}

			if (has_contents && NULL != cr->cell_content) {
				dat.pt = pt;
				dat.cr = cr;
				g_hash_table_foreach (cr->cell_content,
					(GHFunc)cb_paste_cell, &dat);
			}

			if (pt->paste_flags & (PASTE_COMMENTS | PASTE_OBJECTS))
				for (ptr = cr->objects; ptr; ptr = ptr->next)
					paste_object (pt, ptr->data, left, top);
		}

	if (!(pt->paste_flags & PASTE_NO_RECALC)) {
		if (has_contents) {
			sheet_region_queue_recalc (pt->sheet, r);
			sheet_flag_status_update_range (pt->sheet, r);
		} else
			sheet_flag_style_update_range (pt->sheet, r);

		sheet_range_calc_spans (pt->sheet, r,
					(pt->paste_flags & PASTE_FORMATS) ? GNM_SPANCALC_RE_RENDER : GNM_SPANCALC_RENDER);
		if (pt->paste_flags & PASTE_UPDATE_ROW_HEIGHT)
			rows_height_update (pt->sheet, &pt->range, FALSE);
		sheet_redraw_all (pt->sheet, FALSE);
	}

	return FALSE;
}

static GnmValue *
cb_clipboard_prepend_cell (GnmCellIter const *iter, GnmCellRegion *cr)
{
	GnmRange     a;
	GnmCellCopy *copy = gnm_cell_copy_new (cr,
		iter->pp.eval.col - cr->base.col,
		iter->pp.eval.row - cr->base.row);
	copy->val = value_dup (iter->cell->value);

	if (gnm_cell_has_expr (iter->cell)) {
		gnm_expr_top_ref (copy->texpr = iter->cell->base.texpr);

		/* Check for array division */
		if (!cr->not_as_contents &&
		    gnm_cell_array_bound (iter->cell, &a) &&
		    (a.start.col < cr->base.col ||
		     a.start.row < cr->base.row ||
		     a.end.col >= (cr->base.col + cr->cols) ||
		     a.end.row >= (cr->base.row + cr->rows)))
			cr->not_as_contents = TRUE;
	} else
		copy->texpr = NULL;

	return NULL;
}

static void
cb_dup_objects (SheetObject const *src, GnmCellRegion *cr)
{
	SheetObject *dst = sheet_object_dup (src);
	if (dst != NULL) {
		SheetObjectAnchor tmp;
		sheet_object_anchor_assign (&tmp, sheet_object_get_anchor (src));
		range_translate (&tmp.cell_bound, - cr->base.col, - cr->base.row);
		sheet_object_set_anchor (dst, &tmp);
		cr->objects = g_slist_prepend (cr->objects, dst);
	}
}

/**
 * clipboard_copy_range:
 *
 * Entry point to the clipboard copy code
 */
GnmCellRegion *
clipboard_copy_range (Sheet *sheet, GnmRange const *r)
{
	GnmCellRegion *cr;
	GSList *merged, *ptr;
	GSList *objects;

	g_return_val_if_fail (IS_SHEET (sheet), NULL);
	g_return_val_if_fail (range_is_sane (r), NULL);

	cr = cellregion_new (sheet);
	cr->base = r->start;
	cr->cols = range_width (r);
	cr->rows = range_height (r);
	cr->rows = range_height (r);
	cr->col_state = colrow_get_states (sheet,
		TRUE,  r->start.col, r->end.col);
	cr->row_state = colrow_get_states (sheet,
		FALSE, r->start.row, r->end.row);

	sheet_foreach_cell_in_range ( sheet, CELL_ITER_IGNORE_NONEXISTENT,
		r->start.col, r->start.row,
		r->end.col, r->end.row,
		(CellIterFunc) cb_clipboard_prepend_cell, cr);
	objects = sheet_objects_get (sheet, r, G_TYPE_NONE);
	g_slist_foreach (objects, (GFunc)cb_dup_objects, cr);
	g_slist_free (objects);

	cr->styles = sheet_style_get_list (sheet, r);

	merged = gnm_sheet_merge_get_overlap (sheet, r);
	for (ptr = merged ; ptr != NULL ; ptr = ptr->next) {
		GnmRange *tmp = range_dup (ptr->data);
		range_translate (tmp, -r->start.col, -r->start.row);
		cr->merged = g_slist_prepend (cr->merged, tmp);
	}
	g_slist_free (merged);

	return cr;
}

static void
cb_clipboard_copy_range_undo (GnmCellRegion *cr, GnmSheetRange *sr,
			      GOCmdContext *cc)
{
	GnmPasteTarget pt;

	clipboard_paste_region
		(cr,
		 paste_target_init (&pt,
				    sr->sheet,
				    &sr->range,
				    PASTE_CONTENTS | PASTE_FORMATS),
		 cc);
}


GOUndo *
clipboard_copy_range_undo (Sheet *sheet, GnmRange const *r)
{
	GnmCellRegion *cr = clipboard_copy_range (sheet, r);
	g_return_val_if_fail (cr != NULL, NULL);
	return go_undo_binary_new (cr, gnm_sheet_range_new (sheet, r),
				   (GOUndoBinaryFunc)cb_clipboard_copy_range_undo,
				   (GFreeFunc)cellregion_unref,
				   (GFreeFunc)g_free);
}


/**
 * clipboard_copy_obj:
 * @sheet : #Sheet
 * @objects : #GSList
 *
 * Returns a cell region with copies of objects in list.  Caller is responsible
 *	for cellregion_unref-ing the result.
 **/
GnmCellRegion *
clipboard_copy_obj (Sheet *sheet, GSList *objects)
{
	SheetObjectAnchor tmp_anchor;
	SheetObjectAnchor const *anchor;
	GnmCellRegion *cr;
	GnmRange *r;
	GSList *ptr;
	SheetObject *so;
	double coords [4];
	guint w, h;

	g_return_val_if_fail (IS_SHEET (sheet), NULL);
	g_return_val_if_fail (objects != NULL, NULL);

	cr = cellregion_new (sheet);
	for (ptr = objects ; ptr != NULL ; ptr = ptr->next)
		if (NULL != (so = sheet_object_dup (ptr->data))) {
			anchor = sheet_object_get_anchor (so);

#warning FIXME : This is only used in gnm_sog_write_image
/* NOTE #1 : It seems necessary to handle pasting an object that has been removed from
 * the sheet after being added to the clipboard.  it seems like we would need
 * this sort of information for anything that implements SheetObjectImageableIface
 **/
			sheet_object_anchor_to_pts (anchor, sheet, coords);
			w = fabs (coords[2] - coords[0]) + 1.5;
			h = fabs (coords[3] - coords[1]) + 1.5;
			g_object_set_data (G_OBJECT (so),  "pt-width-at-copy",
				GUINT_TO_POINTER (w));
			g_object_set_data (G_OBJECT (so),  "pt-height-at-copy",
				GUINT_TO_POINTER (h));

			sheet_object_anchor_assign (&tmp_anchor, anchor);
			r = &tmp_anchor.cell_bound;
			range_translate (r,
				-MIN (r->start.col, r->end.col),
				-MIN (r->start.row, r->end.row));
			sheet_object_set_anchor (so, &tmp_anchor);

			cr->objects = g_slist_prepend (cr->objects, so);
		}

	return cr;
}

GnmPasteTarget*
paste_target_init (GnmPasteTarget *pt, Sheet *sheet, GnmRange const *r, int flags)
{
	pt->sheet = sheet;
	pt->range = *r;
	pt->paste_flags = flags;
	return pt;
}

/**
 * cellregion_new :
 * @origin_sheet : optionally NULL.
 *
 * A convenience routine to create CellRegions and init the flags nicely.
 */
GnmCellRegion *
cellregion_new (Sheet *origin_sheet)
{
	GnmCellRegion *cr = g_new0 (GnmCellRegion, 1);
	cr->origin_sheet	= origin_sheet;
	cr->date_conv           = origin_sheet && origin_sheet->workbook
		? workbook_date_conv (origin_sheet->workbook)
		: NULL;
	cr->cols = cr->rows	= -1;
	cr->not_as_contents	= FALSE;
	cr->cell_content	= NULL;
	cr->col_state		= NULL;
	cr->row_state		= NULL;
	cr->styles		= NULL;
	cr->merged		= NULL;
	cr->objects		= NULL;
	cr->ref_count		= 1;

	return cr;
}

void
cellregion_ref (GnmCellRegion *cr)
{
	g_return_if_fail (cr != NULL);
	cr->ref_count++;
}

void
cellregion_unref (GnmCellRegion *cr)
{
	g_return_if_fail (cr != NULL);

	if (cr->ref_count > 1) {
		cr->ref_count--;
		return;
	}

	if (NULL != cr->cell_content) {
		g_hash_table_destroy (cr->cell_content);
		cr->cell_content = NULL;
	}

	if (NULL != cr->col_state)
		cr->col_state = colrow_state_list_destroy (cr->col_state);
	if (NULL != cr->row_state)
		cr->row_state = colrow_state_list_destroy (cr->row_state);
	if (cr->styles != NULL) {
		style_list_free (cr->styles);
		cr->styles = NULL;
	}
	if (cr->merged != NULL) {
		GSList *ptr;
		for (ptr = cr->merged; ptr != NULL ; ptr = ptr->next)
			g_free (ptr->data);
		g_slist_free (cr->merged);
		cr->merged = NULL;
	}
	if (cr->objects != NULL) {
		GSList *ptr;
		for (ptr = cr->objects; ptr != NULL ; ptr = ptr->next)
			g_object_unref (ptr->data);
		g_slist_free (cr->objects);
		cr->objects = NULL;
	}

	g_free (cr);
}

static GnmCellCopy *
cellregion_get_content (GnmCellRegion const *cr, int col, int row)
{
	if (cr->cell_content) {
		GnmCellPos pos;
		pos.col = col;
		pos.row = row;
		return g_hash_table_lookup (cr->cell_content, &pos);
	} else
		return NULL;
}

static void
cb_invalidate_cellcopy (GnmCellCopy *cc, gconstpointer ignore,
			GnmExprRelocateInfo *rinfo)
{
	GnmExprTop const *texpr;
	if (NULL != cc->texpr) {
		texpr = gnm_expr_top_relocate (cc->texpr, rinfo, FALSE);
		if (NULL != texpr) {
			gnm_expr_top_unref (cc->texpr);
			cc->texpr = texpr;
		}
	}
}

/**
 * cellregion_invalidate_sheet :
 * @cr    : #GnmCellRegion
 * @sheet : #Sheet
 *
 * Invalidate references from cell content, objects or style to @sheet.
 **/
void
cellregion_invalidate_sheet (GnmCellRegion *cr,
			     Sheet *sheet)
{
	GSList *ptr;
	gboolean save_invalidated;
	GnmExprRelocateInfo rinfo;

	g_return_if_fail (cr != NULL);
	g_return_if_fail (IS_SHEET (sheet));

	save_invalidated = sheet->being_invalidated;
	sheet->being_invalidated = TRUE;

	rinfo.reloc_type = GNM_EXPR_RELOCATE_INVALIDATE_SHEET;
	if (NULL != cr->cell_content)
		g_hash_table_foreach (cr->cell_content,
			(GHFunc)cb_invalidate_cellcopy, &rinfo);
	sheet->being_invalidated = save_invalidated;

	for (ptr = cr->objects; ptr != NULL ; ptr = ptr->next)
		sheet_object_invalidate_sheet (ptr->data, sheet);
}

static void
cb_cellregion_extent (GnmCellCopy *cc, gconstpointer ignore, GnmRange *extent)
{
	if (extent->start.col >= 0) {
		if (extent->start.col > cc->offset.col)
			extent->start.col = cc->offset.col;
		else if (extent->end.col < cc->offset.col)
			extent->end.col = cc->offset.col;

		if (extent->start.row > cc->offset.row)
			extent->start.row = cc->offset.row;
		else if (extent->end.row < cc->offset.row)
			extent->end.row = cc->offset.row;
	} else /* first cell */
		extent->start = extent->end = cc->offset;
}

/**
 * cellregion_extent:
 * @cr : #GnmCellRegion
 * @extent : #GnmRange
 *
 * Find the min and max col/row with cell content
 **/
static void
cellregion_extent (GnmCellRegion const *cr, GnmRange *extent)
{
	if (NULL != cr->cell_content) {
		range_init (extent, -1, -1, -1, -1);
		g_hash_table_foreach (cr->cell_content,
			(GHFunc)cb_cellregion_extent, extent);
	} else
		range_init (extent, 0, 0, 0, 0);
}

GString *
cellregion_to_string (GnmCellRegion const *cr,
		      gboolean only_visible,
		      GODateConventions const *date_conv)
{
	GString *all, *line;
	GnmCellCopy const *cc;
	int col, row, next_col_check, next_row_check;
	GnmRange extent;
	ColRowStateList	const *col_state = NULL, *row_state = NULL;
	ColRowRLEState const *rle;
	int ncells, i;
	GnmStyle const *style;
	GOFormat const *fmt;

	g_return_val_if_fail (cr != NULL, NULL);
	g_return_val_if_fail (cr->rows >= 0, NULL);
	g_return_val_if_fail (cr->cols >= 0, NULL);

	/* pre-allocate rough approximation of buffer */
	ncells = cr->cell_content ? g_hash_table_size (cr->cell_content) : 0;
	all = g_string_sized_new (20 * ncells + 1);
	line = g_string_new (NULL);

	cellregion_extent (cr, &extent);

	if (only_visible && NULL != (row_state = cr->row_state)) {
		next_row_check = i = 0;
		while ((i += ((ColRowRLEState *)(row_state->data))->length) <= extent.start.row) {
			if (NULL == (row_state = row_state->next)) {
				next_row_check = gnm_sheet_get_max_rows (cr->origin_sheet);
				break;
			}
			next_row_check = i;
		}
	} else
		next_row_check = gnm_sheet_get_max_rows (cr->origin_sheet);

	for (row = extent.start.row; row <= extent.end.row;) {
		if (row >= next_row_check) {
			rle = row_state->data;
			row_state = row_state->next;
			next_row_check += rle->length;
			if (!rle->state.visible) {
				row = next_row_check;
				continue;
			}
		}

		g_string_assign (line, "");

		if (only_visible && NULL != (col_state = cr->col_state)) {
			next_col_check = i = 0;
			while ((i += ((ColRowRLEState *)(col_state->data))->length) <= extent.start.col) {
				if (NULL == (col_state = col_state->next)) {
					next_col_check = gnm_sheet_get_max_cols (cr->origin_sheet);
					break;
				}
				next_col_check = i;
			}
		} else
			next_col_check = gnm_sheet_get_max_cols (cr->origin_sheet);

		for (col = extent.start.col; col <= extent.end.col;) {
			if (col == next_col_check) {
				rle = col_state->data;
				col_state = col_state->next;
				next_col_check += rle->length;
				if (!rle->state.visible) {
					col = next_col_check;
					continue;
				}
			}

			cc = cellregion_get_content (cr, col, row);
			if (cc) {
				style = style_list_get_style (cr->styles, col, row);
				fmt = gnm_style_get_format (style);

				if (go_format_is_general (fmt) &&
				    VALUE_FMT (cc->val))
					fmt = VALUE_FMT (cc->val);

				format_value_gstring (line, fmt, cc->val,
						      NULL, -1, date_conv);
			}
			if (++col <= extent.end.col)
				g_string_append_c (line, '\t');
		}
		g_string_append_len (all, line->str, line->len);
		if (++row <= extent.end.row)
			g_string_append_c (all, '\n');
	}

	g_string_free (line, TRUE);
	return all;
}

int
cellregion_cmd_size (GnmCellRegion const *cr)
{
	int res = 1;

	g_return_val_if_fail (cr != NULL, 1);

	res += g_slist_length (cr->styles);
	if (NULL != cr->cell_content)
		res += g_hash_table_size (cr->cell_content);
	return res;
}

static void
gnm_cell_copy_free (GnmCellCopy *cc)
{
	if (cc->texpr) {
		gnm_expr_top_unref (cc->texpr);
		cc->texpr = NULL;
	}
	if (cc->val) {
		value_release (cc->val);
		cc->val = NULL;
	}
	CHUNK_FREE (cell_copy_pool, cc);
}

GnmCellCopy *
gnm_cell_copy_new (GnmCellRegion *cr, int col_offset, int row_offset)
{
	GnmCellCopy *res = CHUNK_ALLOC (GnmCellCopy, cell_copy_pool);
	((GnmCellPos *)(&res->offset))->col = col_offset;
	((GnmCellPos *)(&res->offset))->row = row_offset;
	res->texpr = NULL;
	res->val = NULL;

	if (NULL == cr->cell_content)
		cr->cell_content = g_hash_table_new_full (
			(GHashFunc)&gnm_cellpos_hash,
			(GCompareFunc)&gnm_cellpos_equal,
			(GDestroyNotify) gnm_cell_copy_free,
			NULL);

	g_hash_table_insert (cr->cell_content, res, res);

	return res;
}

void
clipboard_init (void)
{
#if USE_CELL_COPY_POOLS
	cell_copy_pool =
		go_mem_chunk_new ("cell copy pool",
				  sizeof (GnmCellCopy),
				  4 * 1024 - 128);
#endif
}

#if USE_CELL_COPY_POOLS
static void
cb_cell_copy_pool_leak (gpointer data, G_GNUC_UNUSED gpointer user)
{
	GnmCellCopy const *cc = data;
	g_printerr ("Leaking cell copy at %p.\n", cc);
}
#endif

void
clipboard_shutdown (void)
{
#if USE_CELL_COPY_POOLS
	go_mem_chunk_foreach_leak (cell_copy_pool, cb_cell_copy_pool_leak, NULL);
	go_mem_chunk_destroy (cell_copy_pool, FALSE);
	cell_copy_pool = NULL;
#endif
}
