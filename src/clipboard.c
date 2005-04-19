/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * clipboard.c: A temporary store for content from a worksheet
 *
 * Copyright (C) 2000-2005 Jody Goldberg   (jody@gnome.org)
 *  		 1999      Miguel de Icaza (miguel@gnu.org)
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
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
 * USA
 */
#include <gnumeric-config.h>
#include "gnumeric.h"
#include "clipboard.h"

#include "sheet.h"
#include "sheet-style.h"
#include "sheet-merge.h"
#include "dependent.h"
#include "selection.h"
#include "application.h"
#include "command-context.h"
#include "workbook-control.h"
#include "workbook.h"
#include "ranges.h"
#include "expr.h"
#include "expr-impl.h"
#include "commands.h"
#include "value.h"
#include "stf-parse.h"
#include "sheet-object-cell-comment.h"

#include <gtk/gtkmain.h>
#include <glib/gi18n.h>
#include <locale.h>
#include <string.h>
#include <goffice/utils/go-glib-extras.h>

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
	return (cell_is_empty (cell) ||
		(cell != NULL && cell_is_number (cell)) ||
		(cell != NULL && cell_has_expr (cell)));
}

static GnmExpr const *
contents_as_expr (GnmExpr const *expr, GnmValue const *val)
{
	if (NULL != expr) {
		gnm_expr_ref (expr);
		return expr;
	}
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
			   GnmExprRewriteInfo const *rwinfo,
			   GnmCellCopy const *src,
			   int paste_flags)
{
	GnmCell *dst;
	GnmExprOp op;

	if (src->expr == NULL && !VALUE_IS_EMPTY (src->val) && !VALUE_IS_NUMBER (src->val))
		return;

	dst = sheet_cell_fetch (dst_sheet, target_col, target_row);
	if (!cell_has_expr_or_number_or_blank (dst))
		return;

	op = paste_op_to_expr_op (paste_flags);
	/* FIXME : This does not handle arrays, linked cells, ranges, etc. */
	if ((paste_flags & PASTE_CONTENT) &&
	    (NULL != src->expr || cell_has_expr (dst))) {
		GnmExpr const *old_expr    = contents_as_expr (dst->base.expression, dst->value);
		GnmExpr const *copied_expr = contents_as_expr (src->expr, src->val);
		GnmExpr const *res = gnm_expr_new_binary (old_expr, op, copied_expr);
		cell_set_expr (dst, res);
		cell_relocate (dst, rwinfo);
		gnm_expr_unref (res);
	} else {
		GnmEvalPos pos;
		GnmExpr	   expr, arg_a, arg_b;
		arg_a.constant.oper = GNM_EXPR_OP_CONSTANT;
		arg_a.constant.value = dst->value;
		arg_b.constant.oper = GNM_EXPR_OP_CONSTANT;
		arg_b.constant.value = src->val;
		expr.binary.oper = op;
		expr.binary.value_a = &arg_a;
		expr.binary.value_b = &arg_b;

		eval_pos_init_cell (&pos, dst);
		pos.dep = NULL; /* no dynamic deps */
		cell_set_value (dst,
			gnm_expr_eval (&expr, &pos, GNM_EXPR_EVAL_SCALAR_NON_EMPTY));
	}
}

/* NOTE : Make sure to set up any merged regions in the target range BEFORE
 * this is called.
 */
static void
paste_link (GnmPasteTarget const *pt, int top, int left,
	    GnmCellRegion const *content)
{
	GnmCell *cell;
	GnmCellPos pos;
	GnmExpr const *expr;
	GnmCellRef source_cell_ref;
	int x, y;

	/* Not possible to link to arbitrary (non gnumeric) sources yet. */
	/* TODO : eventually support interprocess gnumeric links */
	if (content->origin_sheet == NULL)
		return;

	/* TODO : support relative links ? */
	source_cell_ref.col_relative = 0;
	source_cell_ref.row_relative = 0;
	source_cell_ref.sheet = (content->origin_sheet != pt->sheet)
		? content->origin_sheet : NULL;
	pos.col = left;
	for (x = 0 ; x < content->cols ; x++, pos.col++) {
		source_cell_ref.col = content->base.col + x;
		pos.row = top;
		for (y = 0 ; y < content->rows ; y++, pos.row++) {
			cell = sheet_cell_fetch (pt->sheet, pos.col, pos.row);

			/* This could easily be made smarter */
			if (!cell_is_merged (cell) &&
			    sheet_merge_contains_pos (pt->sheet, &pos))
					continue;
			source_cell_ref.row = content->base.row + y;
			expr = gnm_expr_new_cellref (&source_cell_ref);
			cell_set_expr (cell, expr);
			gnm_expr_unref (expr);
		}
	}
}

/**
 * paste_cell: Pastes a cell in the spreadsheet
 *
 * @dst_sheet:   The sheet where the pasting will be done
 * @target_col:  Column to put the cell into
 * @target_row:  Row to put the cell into.
 * @new_cell:    A new cell (not linked into the sheet, or wb->expr_list)
 * @paste_flags: Bit mask that describes the paste options.
 */
static void
paste_cell (Sheet *dst_sheet,
	    int target_col, int target_row,
	    GnmExprRewriteInfo const *rwinfo,
	    GnmCellCopy const *src, int paste_flags)
{
	if (paste_flags & PASTE_OPER_MASK)
		paste_cell_with_operation (dst_sheet, target_col, target_row,
					   rwinfo, src, paste_flags);
	else if (NULL != src->expr) {
		GnmCell *dst = sheet_cell_fetch (dst_sheet, target_col, target_row);
		cell_set_expr_and_value (dst, src->expr,
			value_dup (src->val), FALSE);
		if (paste_flags & PASTE_CONTENT)
			cell_relocate (dst, rwinfo);
		else
			cell_convert_expr_to_value (dst);
	} else
		cell_set_value (sheet_cell_fetch (dst_sheet, target_col, target_row),
			value_dup (src->val));
}

static void
paste_object (GnmPasteTarget const *pt, SheetObject const *src, int left, int top)
{
	if ((pt->paste_flags & PASTE_OBJECTS) ||
	    G_OBJECT_TYPE (src) == CELL_COMMENT_TYPE) {
		SheetObject *dst = sheet_object_dup (src);
		if (dst != NULL) {
			SheetObjectAnchor tmp;
			sheet_object_anchor_cpy (&tmp, sheet_object_get_anchor (src));
			range_translate (&tmp.cell_bound, left, top);
			sheet_object_set_anchor (dst, &tmp);
			sheet_object_set_sheet (dst, pt->sheet);
			g_object_unref (dst);
		}
	}
}

/**
 * clipboard_paste_region:
 * @content : The GnmCellRegion to paste.
 * @pt : Where to paste the values.
 * @cc : The context for error handling.
 *
 * Pastes the supplied GnmCellRegion (@content) into the supplied
 * GnmPasteTarget (@pt).  This operation is not undoable.  It does not auto grow
 * the destination if the target is a singleton.  This is a simple interface to
 * paste a region.
 *
 * returns : TRUE if there was a problem.
 */
gboolean
clipboard_paste_region (GnmCellRegion const *content,
			GnmPasteTarget const *pt,
			GOCmdContext *cc)
{
	int repeat_horizontal, repeat_vertical, clearFlags;
	int dst_cols, dst_rows, src_cols, src_rows;
	int i, j;
	GSList *ptr;
	GnmRange const *r;
	gboolean has_content, adjust_merges = TRUE;

	g_return_val_if_fail (pt != NULL, TRUE);
	g_return_val_if_fail (content != NULL, TRUE);

	/* we do not need any of this fancy stuff when pasting a simple object */
	if (content->content == NULL && content->objects != NULL) {
		if (pt->paste_flags & (PASTE_COMMENTS | PASTE_OBJECTS))
			for (ptr = content->objects; ptr; ptr = ptr->next)
				paste_object (pt, ptr->data,
					pt->range.start.col, pt->range.start.row);
		return FALSE;
	}

	r = &pt->range;
	dst_cols = range_width (r);
	dst_rows = range_height (r);
	src_cols = content->cols;
	src_rows = content->rows;

	/* If the source is a single cell */
	/* Treat a target of a single merge specially, don't split the merge */
	if (src_cols == 1 && src_rows == 1) {
		GnmRange const *merge = sheet_merge_is_corner (pt->sheet, &r->start);
		if (merge != NULL && range_equal (r, merge)) {
			dst_cols = dst_rows = 1;
			adjust_merges = FALSE;
		}
	/* Apparently links do not supercede merges */
	} else if (pt->paste_flags & PASTE_LINK)
		adjust_merges = FALSE;

	has_content = pt->paste_flags & (PASTE_CONTENT|PASTE_AS_VALUES|PASTE_LINK);

	if (pt->paste_flags & PASTE_TRANSPOSE) {
		int tmp = src_cols;
		src_cols = src_rows;
		src_rows = tmp;
	}

	if (content->not_as_content && (pt->paste_flags & PASTE_CONTENT)) {
		go_cmd_context_error_invalid (cc,
					_("Unable to paste"),
					_("Content can only be pasted by value or by link."));
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

	if ((pt->range.start.col + dst_cols) > SHEET_MAX_COLS ||
	    (pt->range.start.row + dst_rows) > SHEET_MAX_ROWS) {
		go_cmd_context_error_invalid (cc,
					_("Unable to paste"),
					_("result passes the sheet boundary"));
		return TRUE;
	}

	clearFlags = 0;
	/* clear the region where we will paste */
	if (has_content)
		clearFlags = CLEAR_VALUES | CLEAR_NORESPAN;

	if (pt->paste_flags & PASTE_COMMENTS)
		clearFlags |= CLEAR_COMMENTS;

	/* No need to clear the formats.  We will paste over top of these. */
	/* if (pt->paste_flags & PASTE_FORMATS) clearFlags |= CLEAR_FORMATS; */

	if (pt->paste_flags & (PASTE_OPER_MASK | PASTE_SKIP_BLANKS))
		clearFlags = 0;

	/* remove merged regions even for operations, or blanks */
	if (has_content && adjust_merges)
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

	for (i = 0; i < repeat_horizontal ; i++)
		for (j = 0; j < repeat_vertical ; j++) {
			int const left = i * src_cols + pt->range.start.col;
			int const top = j * src_rows + pt->range.start.row;
			GnmExprRewriteInfo   rwinfo;
			GnmExprRelocateInfo *rinfo;

			rwinfo.type = GNM_EXPR_REWRITE_RELOCATE;
			rinfo = &rwinfo.u.relocate;
			rinfo->origin_sheet = rinfo->target_sheet = pt->sheet;

			if (pt->paste_flags & PASTE_EXPR_LOCAL_RELOCATE) {
				rinfo->origin.start = content->base;
				rinfo->origin.end.col = content->base.col + content->cols - 1;
				rinfo->origin.end.row = content->base.row + content->rows - 1;
				rinfo->col_offset = left - content->base.col;
				rinfo->row_offset = top - content->base.row;
			} else {
				rinfo->origin = pt->range;
				rinfo->col_offset = 0;
				rinfo->row_offset = 0;
			}

			/* Move the styles on here so we get correct formats before recalc */
			if (pt->paste_flags & PASTE_FORMATS) {
				GnmCellPos pos;
				pos.col = left;
				pos.row = top;
				sheet_style_set_list (pt->sheet, &pos,
						      (pt->paste_flags & PASTE_TRANSPOSE),
						      content->styles);
			}

			if (has_content && !(pt->paste_flags & PASTE_DONT_MERGE)) {
				for (ptr = content->merged; ptr != NULL ; ptr = ptr->next) {
					GnmRange tmp = *((GnmRange const *)ptr->data);
					if (pt->paste_flags & PASTE_TRANSPOSE) {
						int x;
						x = tmp.start.col; tmp.start.col = tmp.start.row;  tmp.start.row = x;
						x = tmp.end.col; tmp.end.col = tmp.end.row;  tmp.end.row = x;
					}
					if (!range_translate (&tmp, left, top))
						sheet_merge_add (pt->sheet, &tmp, TRUE, cc);
				}
			}

			if (has_content && (pt->paste_flags & PASTE_LINK)) {
				paste_link (pt, top, left, content);
				continue;
			}

			if (has_content)
				for (ptr = content->content; ptr; ptr = ptr->next) {
					GnmCellCopy const *src = ptr->data;
					int target_col = left;
					int target_row = top;

					if (pt->paste_flags & PASTE_TRANSPOSE) {
						target_col += src->row_offset;
						target_row += src->col_offset;
					} else {
						target_col += src->col_offset;
						target_row += src->row_offset;
					}

					rinfo->pos.sheet = pt->sheet;
					if (pt->paste_flags & PASTE_EXPR_LOCAL_RELOCATE) {
						rinfo->pos.eval.col = content->base.col + src->col_offset;
						rinfo->pos.eval.row = content->base.row + src->row_offset;
					} else {
						rinfo->pos.eval.col = target_col;
						rinfo->pos.eval.row = target_row;
					}

					paste_cell (pt->sheet, target_col, target_row,
						    &rwinfo, src, pt->paste_flags);
				}
			if (pt->paste_flags & (PASTE_COMMENTS | PASTE_OBJECTS))
				for (ptr = content->objects; ptr; ptr = ptr->next)
					paste_object (pt, ptr->data, left, top);
		}

	if (!(pt->paste_flags & PASTE_NO_RECALC)) {
		if (has_content) {
			sheet_region_queue_recalc (pt->sheet, r);
			sheet_flag_status_update_range (pt->sheet, r);
		} else
			sheet_flag_format_update_range (pt->sheet, r);

		sheet_range_calc_spans (pt->sheet, r,
					(pt->paste_flags & PASTE_FORMATS) ? SPANCALC_RE_RENDER : SPANCALC_RENDER);
		if (pt->paste_flags & PASTE_UPDATE_ROW_HEIGHT)
			rows_height_update (pt->sheet, &pt->range, FALSE);
		sheet_redraw_all (pt->sheet, FALSE);
	}

	return FALSE;
}

static GnmValue *
cb_clipboard_prepend_cell (Sheet *sheet, int col, int row,
			   GnmCell const *cell, GnmCellRegion *cr)
{
	GnmExprArray const *a;
	GnmCellCopy *copy = gnm_cell_copy_new (
		col - cr->base.col, row - cr->base.row);
	copy->val = value_dup (cell->value);
	if (cell_has_expr (cell))
		gnm_expr_ref (copy->expr = cell->base.expression);
	else
		copy->expr = NULL;
	cr->content = g_slist_prepend (cr->content, copy);

	/* Check for array division */
	if (!cr->not_as_content && NULL != (a = cell_is_array (cell))) {
		int base = copy->col_offset - a->x;
		if (base < 0 || (base + a->cols) > cr->cols)
			cr->not_as_content = TRUE;
		else {
			base = copy->row_offset - a->y;
			if (base < 0 || (base + a->rows) > cr->rows)
				cr->not_as_content = TRUE;
		}
	}

	return NULL;
}

static void
cb_dup_objects (SheetObject const *src, GnmCellRegion *cr)
{
	SheetObject *dst = sheet_object_dup (src);
	if (dst != NULL) {
		SheetObjectAnchor tmp;
		sheet_object_anchor_cpy (&tmp, sheet_object_get_anchor (src));
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

	sheet_foreach_cell_in_range ( sheet, CELL_ITER_IGNORE_NONEXISTENT,
		r->start.col, r->start.row,
		r->end.col, r->end.row,
		(CellIterFunc) cb_clipboard_prepend_cell, cr);
	objects = sheet_objects_get (sheet, r, G_TYPE_NONE);
	g_slist_foreach (objects, (GFunc)cb_dup_objects, cr);
	g_slist_free (objects);

	cr->styles = sheet_style_get_list (sheet, r);

	merged = sheet_merge_get_overlap (sheet, r);
	for (ptr = merged ; ptr != NULL ; ptr = ptr->next) {
		GnmRange *tmp = range_dup (ptr->data);
		range_translate (tmp, -r->start.col, -r->start.row);
		cr->merged = g_slist_prepend (cr->merged, tmp);
	}
	g_slist_free (merged);

	return cr;
}

/**
 * clipboard_copy_obj:
 *
 * Returns a cell region with copies of objects in list.
 */
GnmCellRegion *
clipboard_copy_obj (Sheet *sheet, GSList *objects)
{
	GnmCellRegion *cr;
	GnmRange *r;
	GSList *ptr;
	SheetObject *so;
	double coords [4];
	guint w, h;

	g_return_val_if_fail (IS_SHEET (sheet), NULL);
	g_return_val_if_fail (objects != NULL, NULL);

	cr = cellregion_new (sheet);
	for (ptr = objects ; ptr != NULL ; ptr = ptr->next) {
		sheet_object_position_pts_get (SHEET_OBJECT (ptr->data),
					       coords);
		w = fabs (coords[2] - coords[0]) + 1;
		h = fabs (coords[3] - coords[1]) + 1.;
		so = sheet_object_dup (ptr->data);
		if (so != NULL) {
			r = (GnmRange *) sheet_object_get_range	(so);
			range_translate (r,
					 -MIN (r->start.col, r->end.col),
					 -MIN (r->start.row, r->end.row));
			g_object_set_data (G_OBJECT (so),  "pt-width-at-copy",
					   GUINT_TO_POINTER (w));
			g_object_set_data (G_OBJECT (so),  "pt-height-at-copy",
					   GUINT_TO_POINTER (h));
			cr->objects = g_slist_prepend (cr->objects, so);
		}
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
	cr->cols = cr->rows	= -1;
	cr->not_as_content	= FALSE;
	cr->content		= NULL;
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
	GSList	*ptr;
	GnmCellCopy *cc;

	g_return_if_fail (cr != NULL);
	if (cr->ref_count > 1) {
		cr->ref_count--;
		return;
	}

	for (ptr = cr->content; ptr; ptr = ptr->next) {
		cc = ptr->data;
		if (NULL != cc->expr) {
			gnm_expr_unref (cc->expr);
			cc->expr = NULL;
		}
		if (cc->val != NULL) {
			value_release (cc->val);
			cc->val = NULL;
		}
		CHUNK_FREE (cell_copy_pool, cc);
	}
	g_slist_free (cr->content);
	cr->content = NULL;

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

GnmCellCopy *
gnm_cell_copy_new (int col_offset, int row_offset)
{
	GnmCellCopy *res = CHUNK_ALLOC (GnmCellCopy, cell_copy_pool);
	res->col_offset = col_offset;
	res->row_offset = row_offset;
	res->expr = NULL;
	res->val = NULL;
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
