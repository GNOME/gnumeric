/* vim: set sw=8: */
/*
 * Clipboard.c: Implements the copy/paste operations
 *
 * Author:
 *  Miguel de Icaza (miguel@gnu.org)
 */
#include <gnumeric-config.h>
#include <glib/gi18n.h>
#include "gnumeric.h"
#include "clipboard.h"

#include "sheet.h"
#include "sheet-style.h"
#include "sheet-merge.h"
#include "dependent.h"
#include "selection.h"
#include "application.h"
#include "rendered-value.h"
#include "command-context.h"
#include "workbook-control.h"
#include "workbook.h"
#include "ranges.h"
#include "expr.h"
#include "expr-impl.h"
#include "commands.h"
#include "value.h"
#include <gtk/gtkmain.h>
#include "stf-parse.h"
#include "sheet-object-cell-comment.h"

#include <locale.h>
#include <string.h>

static gboolean
cell_has_expr_or_number_or_blank (GnmCell const * cell)
{
	return (cell_is_empty (cell) ||
		(cell != NULL && cell_is_number (cell)) ||
		(cell != NULL && cell_has_expr (cell)));
}

static GnmExpr const *
cell_get_contents_as_expr (GnmCell const * cell)
{
	GnmExpr const *expr = NULL;

	g_return_val_if_fail (cell_has_expr_or_number_or_blank (cell), NULL);

	if (cell_is_empty (cell))
		expr = gnm_expr_new_constant (value_new_float (0.0));
	else if (cell_has_expr (cell)) {
		expr = cell->base.expression;
		gnm_expr_ref (expr);
	} else if (cell_is_number (cell))
		expr = gnm_expr_new_constant (value_dup (cell->value));
	else
		g_assert_not_reached ();

	return expr;
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
	else
		g_assert_not_reached ();

	return 0;
}

#warning handle formating
static GnmValue *
apply_paste_oper_to_values (GnmCell const *old_cell, GnmCell const *copied_cell,
			    GnmCell const *new_cell, int paste_flags)
{
	GnmEvalPos pos;
	GnmExpr expr, arg_a, arg_b;
	GnmExprOp op = paste_op_to_expr_op (paste_flags);

	arg_a.constant.oper = GNM_EXPR_OP_CONSTANT;
	arg_a.constant.value = old_cell->value;
	arg_b.constant.oper = GNM_EXPR_OP_CONSTANT;
	arg_b.constant.value = copied_cell->value;

	expr.binary.oper = op;
	expr.binary.value_a = &arg_a;
	expr.binary.value_b = &arg_b;

	eval_pos_init_cell (&pos, new_cell);
	pos.dep = NULL; /* no dynamic deps */
	return gnm_expr_eval (&expr, &pos, GNM_EXPR_EVAL_SCALAR_NON_EMPTY);
}

static void
paste_cell_with_operation (Sheet *dest_sheet,
			   int target_col, int target_row,
			   GnmExprRewriteInfo const *rwinfo,
			   CellCopy *c_copy, int paste_flags)
{
	GnmCell *new_cell;

	g_return_if_fail (paste_flags & PASTE_OPER_MASK);

	if (!(c_copy->type == CELL_COPY_TYPE_CELL))
		return;

	new_cell = sheet_cell_fetch (dest_sheet, target_col, target_row);

	if ((!cell_has_expr_or_number_or_blank (new_cell)) ||
	    (!cell_has_expr_or_number_or_blank (c_copy->u.cell)))
		return;

	/* FIXME : This does not handle arrays, linked cells, ranges, etc. */
	if ((paste_flags & PASTE_CONTENT) &&
	    ((c_copy->u.cell != NULL && cell_has_expr (c_copy->u.cell)) ||
	           (new_cell != NULL && cell_has_expr (new_cell)))) {
		GnmExpr const *old_expr    = cell_get_contents_as_expr (new_cell);
		GnmExpr const *copied_expr = cell_get_contents_as_expr (c_copy->u.cell);
		GnmExprOp op	           = paste_op_to_expr_op (paste_flags);
		GnmExpr const *new_expr    = gnm_expr_new_binary (old_expr, op, copied_expr);
		cell_set_expr (new_cell, new_expr);
		cell_relocate (new_cell, rwinfo);
		gnm_expr_unref (new_expr);
	} else {
		GnmValue *new_val = apply_paste_oper_to_values (new_cell, c_copy->u.cell,
							     new_cell, paste_flags);

		cell_set_value (new_cell, new_val);
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
 * @dest_sheet:  The sheet where the pasting will be done
 * @target_col:  Column to put the cell into
 * @target_row:  Row to put the cell into.
 * @new_cell:    A new cell (not linked into the sheet, or wb->expr_list)
 * @paste_flags: Bit mask that describes the paste options.
 */
static void
paste_cell (Sheet *dest_sheet,
	    int target_col, int target_row,
	    GnmExprRewriteInfo const *rwinfo,
	    CellCopy *c_copy, int paste_flags)
{
	if (!(paste_flags & (PASTE_CONTENT | PASTE_AS_VALUES)))
		return;

	if (paste_flags & PASTE_OPER_MASK) {
		paste_cell_with_operation (dest_sheet, target_col, target_row,
					   rwinfo, c_copy, paste_flags);
		return;
	}

	if (c_copy->type == CELL_COPY_TYPE_CELL) {
		GnmCell *new_cell = sheet_cell_fetch (dest_sheet, target_col, target_row);
		GnmCell *src_cell = c_copy->u.cell;

		if (!src_cell) {
			g_warning ("Cell copy type set but no cell found (this is bad!)");
			return;
		}

		if (cell_has_expr (src_cell)) {
			cell_set_expr_and_value (new_cell, src_cell->base.expression,
						 value_dup (src_cell->value), FALSE);

			if (paste_flags & PASTE_CONTENT)
				cell_relocate (new_cell, rwinfo);
			else
				cell_convert_expr_to_value (new_cell);
		} else
				cell_set_value (new_cell, value_dup (src_cell->value));

	} else if (c_copy->u.text)
		cell_set_text (sheet_cell_fetch (dest_sheet, target_col, target_row),
			       c_copy->u.text);
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
			CellCopyList *c_ptr;
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


			for (c_ptr = content->content; c_ptr; c_ptr = c_ptr->next) {
				CellCopy *c_copy = c_ptr->data;
				int target_col = left;
				int target_row = top;

				if (pt->paste_flags & PASTE_TRANSPOSE) {
					target_col += c_copy->row_offset;
					target_row += c_copy->col_offset;
				} else {
					target_col += c_copy->col_offset;
					target_row += c_copy->row_offset;
				}

				rinfo->pos.sheet = pt->sheet;
				if (pt->paste_flags & PASTE_EXPR_LOCAL_RELOCATE) {
					rinfo->pos.eval.col = content->base.col + c_copy->col_offset;
					rinfo->pos.eval.row = content->base.row + c_copy->row_offset;
				} else {
					rinfo->pos.eval.col = target_col;
					rinfo->pos.eval.row = target_row;
				}

				paste_cell (pt->sheet, target_col, target_row,
					    &rwinfo, c_copy, pt->paste_flags);
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
			   GnmCell *cell, GnmCellRegion *cr)
{
	GnmExprArray const *a;
	CellCopy *copy;

	copy = g_new (CellCopy, 1);
	copy->type       = CELL_COPY_TYPE_CELL;
	copy->u.cell     = cell_copy (cell);
	copy->u.cell->pos.col = copy->col_offset = col - cr->base.col;
	copy->u.cell->pos.row = copy->row_offset = row - cr->base.row;
	cr->content = g_slist_prepend (cr->content, copy);

	/* Check for array division */
	if (!cr->not_as_content && NULL != (a = cell_is_array (cell))) {
		int base;
		base = copy->col_offset - a->x;
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
	CellCopyList *ptr;

	g_return_if_fail (cr != NULL);
	if (cr->ref_count > 1) {
		cr->ref_count--;
		return;
	}

	for (ptr = cr->content; ptr; ptr = ptr->next) {
		CellCopy *this_cell = ptr->data;

		if (this_cell->type == CELL_COPY_TYPE_CELL) {
			/* The cell is not really in the rows or columns */
			this_cell->u.cell->base.sheet = NULL;
			this_cell->u.cell->row_info = NULL;
			this_cell->u.cell->col_info = NULL;
			cell_destroy (this_cell->u.cell);
		} else if (this_cell->u.text)
			g_free (this_cell->u.text);
		g_free (this_cell);
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

/**
 * cellregion_to_string
 * @cr :
 *
 * Renders a GnmCellRegion as a sequence of strings.
 */
char *
cellregion_to_string (GnmCellRegion const *cr, PangoContext *context)
{
	GString *all, *line;
	CellCopyList *ptr;
	char ***data, *return_val;
	int col, row;

	g_return_val_if_fail (cr != NULL, NULL);
	g_return_val_if_fail (cr->rows >= 0, NULL);
	g_return_val_if_fail (cr->cols >= 0, NULL);

	data = g_new0 (char **, cr->rows);

	for (row = 0; row < cr->rows; row++)
		data[row] = g_new0 (char *, cr->cols);

	for (ptr = cr->content; ptr; ptr = ptr->next) {
		CellCopy *c_copy = ptr->data;
		char *v;

		if (c_copy->type != CELL_COPY_TYPE_TEXT) {
			/* FIXME: feels bogus to use rendered value.  */
			GnmStyle const *mstyle = style_list_get_style (cr->styles,
				&c_copy->u.cell->pos);
			RenderedValue *rv;

			rv = rendered_value_new (c_copy->u.cell,
						 mstyle, FALSE,
						 context,
						 1.0);
			v = g_strdup (rendered_value_get_text (rv));
			rendered_value_destroy (rv);
		} else
			v = g_strdup (c_copy->u.text);

		data[c_copy->row_offset][c_copy->col_offset] = v;
	}

	all = g_string_new (NULL);
	line = g_string_new (NULL);
	for (row = 0; row < cr->rows;) {
		g_string_assign (line, "");

		for (col = 0; col < cr->cols;) {
			if (data[row][col]) {
				g_string_append (line, data[row][col]);
				g_free (data[row][col]);
			}
			if (++col < cr->cols)
				g_string_append_c (line, '\t');
		}
		g_string_append_len (all, line->str, line->len);
		if (++row < cr->rows)
			g_string_append_c (all, '\n');
	}

	/* Release, everything we used */
	g_string_free (line, TRUE);
	return_val = g_string_free (all, FALSE);

	for (row = 0; row < cr->rows; row++)
		g_free (data[row]);
	g_free (data);

	return return_val;
}
