/* vim: set sw=8: */
/*
 * Clipboard.c: Implements the copy/paste operations
 *
 * Author:
 *  Miguel de Icaza (miguel@gnu.org)
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
#include "rendered-value.h"
#include "command-context.h"
#include "workbook-control.h"
#include "workbook.h"
#include "ranges.h"
#include "expr.h"
#include "expr-impl.h"
#include "commands.h"
#include "value.h"
#include "dialog-stf.h"
#include "stf-parse.h"
#include "sheet-object-cell-comment.h"

#include <locale.h>
#include <string.h>
#include <ctype.h>

static gboolean
cell_has_expr_or_number_or_blank (Cell const * cell)
{
	return (cell_is_blank (cell) ||
		(cell != NULL && cell_is_number (cell)) ||
		(cell != NULL && cell_has_expr (cell)));
}

static GnmExpr const *
cell_get_contents_as_expr (Cell const * cell)
{
	GnmExpr const *expr = NULL;

	g_return_val_if_fail (cell_has_expr_or_number_or_blank (cell), NULL);

	if (cell_is_blank (cell))
		expr = gnm_expr_new_constant (value_new_float (0.0));
	else if (cell_has_expr (cell)) {
		expr = cell->base.expression;
		gnm_expr_ref (expr);
	} else if (cell_is_number (cell))
		expr = gnm_expr_new_constant (value_duplicate (cell->value));
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
static Value *
apply_paste_oper_to_values (Cell const *old_cell, Cell const *copied_cell,
			    Cell const *new_cell, int paste_flags)
{
	EvalPos pos;
	GnmExpr expr, arg_a, arg_b;
	GnmExprOp op = paste_op_to_expr_op (paste_flags);

	arg_a.constant.oper = GNM_EXPR_OP_CONSTANT;
	arg_a.constant.value = old_cell->value;
	arg_b.constant.oper = GNM_EXPR_OP_CONSTANT;
	arg_b.constant.value = copied_cell->value;

	expr.binary.oper = op;
	expr.binary.value_a = &arg_a;
	expr.binary.value_b = &arg_b;

	return gnm_expr_eval (&expr, eval_pos_init_cell (&pos, new_cell), GNM_EXPR_EVAL_STRICT);
}

static void
paste_cell_with_operation (Sheet *dest_sheet,
			   int target_col, int target_row,
			   GnmExprRewriteInfo const *rwinfo,
			   CellCopy *c_copy, int paste_flags)
{
	Cell *new_cell;

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
		Value *new_val = apply_paste_oper_to_values (new_cell, c_copy->u.cell,
							     new_cell, paste_flags);

		cell_set_value (new_cell, new_val);
	}
}

/* NOTE : Make sure to set up any merged regions in the target range BEFORE
 * this is called.
 */
static void
paste_link (PasteTarget const *pt, int top, int left,
	    CellRegion const *content)
{
	Cell *cell;
	CellPos pos;
	GnmExpr const *expr;
	CellRef source_cell_ref;
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
	if ((paste_flags & PASTE_COMMENTS) && c_copy->comment) {
		CellComment   *cell_comment;
		CellPos       pos;
		pos.col = target_col;
		pos.row = target_row;

		cell_comment = cell_has_comment_pos (dest_sheet, &pos);
		if (cell_comment)
			cell_comment_text_set (cell_comment, c_copy->comment);
		else
			cell_set_comment (dest_sheet, &pos, NULL, c_copy->comment);
	}
	if (!(paste_flags & (PASTE_CONTENT | PASTE_AS_VALUES)))
		return;

	if (paste_flags & PASTE_OPER_MASK) {
		paste_cell_with_operation (dest_sheet, target_col, target_row,
					   rwinfo, c_copy, paste_flags);
		return;
	}

	if (c_copy->type == CELL_COPY_TYPE_CELL) {
		Cell *new_cell = sheet_cell_fetch (dest_sheet, target_col, target_row);
		Cell *src_cell = c_copy->u.cell;

		if (!src_cell) {
			g_warning ("Cell copy type set but no cell found (this is bad!)");
			return;
		}

		if (cell_has_expr (src_cell)) {
			cell_set_expr_and_value (new_cell, src_cell->base.expression,
						 value_duplicate (src_cell->value), FALSE);

			if (paste_flags & PASTE_CONTENT)
				cell_relocate (new_cell, rwinfo);
			else
				cell_convert_expr_to_value (new_cell);
		} else
				cell_set_value (new_cell, value_duplicate (src_cell->value));

	} else {
		Cell *new_cell = sheet_cell_new (dest_sheet,
						 target_col, target_row);

		if (c_copy->u.text)
			cell_set_text (new_cell, c_copy->u.text);
	}
}

/**
 * clipboard_paste_region:
 * @wbc : The context for error handling.
 * @pt : Where to paste the values.
 * @content : The CellRegion to paste.
 *
 * Pastes the supplied CellRegion (@content) into the supplied
 * PasteTarget (@pt).  This operation is not undoable.  It does not auto grow
 * the destination if the target is a singleton.  This is a simple interface to
 * paste a region.
 *
 * returns : TRUE if there was a problem.
 */
gboolean
clipboard_paste_region (WorkbookControl *wbc,
			PasteTarget const *pt,
			CellRegion const *content)
{
	int repeat_horizontal, repeat_vertical, clearFlags;
	int dst_cols, dst_rows, src_cols, src_rows, min_col, max_col, tmp;
	Range const *r;
	gboolean has_content, adjust_merges = TRUE;

	g_return_val_if_fail (pt != NULL, TRUE);
	g_return_val_if_fail (content != NULL, TRUE);

	r = &pt->range;
	dst_cols = range_width (r);
	dst_rows = range_height (r);
	src_cols = content->cols;
	src_rows = content->rows;

	/* If the source is a single cell */
	/* Treat a target of a single merge specially, don't split the merge */
	if (src_cols == 1 && src_rows == 1)
	{
		Range const *merge = sheet_merge_is_corner (pt->sheet, &r->start);
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
		gnumeric_error_invalid (COMMAND_CONTEXT (wbc),
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
		gnumeric_error_invalid (COMMAND_CONTEXT (wbc),
					_("Unable to paste"), msg);
		g_free (msg);
		return TRUE;
	}

	repeat_vertical = dst_rows/src_rows;
	if (repeat_vertical * src_rows != dst_rows) {
		char *msg = g_strdup_printf (
			_("destination does not have an even multiple of source rows (%d vs %d)\n\n"
			  "Try selecting a single cell or an area of the same shape and size."),
			dst_rows, src_rows);
		gnumeric_error_invalid (COMMAND_CONTEXT (wbc),
					_("Unable to paste"), msg);
		g_free (msg);
		return TRUE;
	}

	if ((pt->range.start.col + dst_cols) > SHEET_MAX_COLS ||
	    (pt->range.start.row + dst_rows) > SHEET_MAX_ROWS) {
		gnumeric_error_invalid (COMMAND_CONTEXT (wbc),
					_("Unable to paste"),
					_("result passes the sheet boundary"));
		return TRUE;
	}

	clearFlags = 0;
	/* clear the region where we will paste */
	if (has_content) {
		clearFlags = CLEAR_VALUES | CLEAR_NORESPAN;
	}

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
		sheet_clear_region (wbc, pt->sheet,
				    dst_col, dst_row,
				    dst_col + dst_cols - 1,
				    dst_row + dst_rows - 1,
				    clearFlags);
	}

	for (tmp = repeat_vertical; repeat_horizontal-- > 0 ; repeat_vertical = tmp)
		while (repeat_vertical-- > 0) {
			int const left = repeat_horizontal * src_cols + pt->range.start.col;
			int const top = repeat_vertical * src_rows + pt->range.start.row;
			CellCopyList *l;
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
				CellPos pos;
				pos.col = left;
				pos.row = top;
				sheet_style_set_list (pt->sheet, &pos,
						      (pt->paste_flags & PASTE_TRANSPOSE),
						      content->styles);
			}

			if (has_content && !(pt->paste_flags & PASTE_DONT_MERGE)) {
				GSList *ptr;
				for (ptr = content->merged; ptr != NULL ; ptr = ptr->next) {
					Range tmp = *((Range const *)ptr->data);
					if (pt->paste_flags & PASTE_TRANSPOSE) {
						int x;
						x = tmp.start.col; tmp.start.col = tmp.start.row;  tmp.start.row = x;
						x = tmp.end.col; tmp.end.col = tmp.end.row;  tmp.end.row = x;
					}
					if (!range_translate (&tmp, left, top))
						sheet_merge_add (wbc, pt->sheet, &tmp, TRUE);
				}
			}

			if (has_content && (pt->paste_flags & PASTE_LINK)) {
				paste_link (pt, top, left, content);
				continue;
			}


			for (l = content->content; l; l = l->next) {
				CellCopy *c_copy = l->data;
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
		}

        if (has_content) {
		sheet_region_queue_recalc (pt->sheet, r);
		sheet_flag_status_update_range (pt->sheet, r);
	} else
		sheet_flag_format_update_range (pt->sheet, r);

	sheet_regen_adjacent_spans (pt->sheet,
		r->start.col,  r->start.row, r->end.col, r->end.row,
		&min_col, &max_col);
	sheet_range_calc_spans (pt->sheet, &pt->range,
		(pt->paste_flags & PASTE_FORMATS) ? SPANCALC_RE_RENDER : SPANCALC_RENDER);
	sheet_redraw_region (pt->sheet,
		min_col, r->start.row, max_col, r->end.row);

	if (pt->paste_flags & PASTE_UPDATE_ROW_HEIGHT)
		rows_height_update (pt->sheet, &pt->range, FALSE);

	return FALSE;
}

static Value *
clipboard_prepend_cell (Sheet *sheet, int col, int row, Cell *cell, void *user_data)
{
	CellRegion *cr = user_data;
	GnmExprArray const *a;
	CellCopy *copy;
	CellComment   *comment;
	
	copy = g_new (CellCopy, 1);
	copy->type       = CELL_COPY_TYPE_CELL;
	copy->u.cell     = cell_copy (cell);
	copy->u.cell->pos.col = copy->col_offset = col - cr->base.col;
	copy->u.cell->pos.row = copy->row_offset = row - cr->base.row;
	comment = cell_has_comment_pos (sheet, &cell->pos);
	if (comment)
		copy->comment = g_strdup (cell_comment_text_get (comment));
	else
		copy->comment = NULL;

	cr->content = g_list_prepend (cr->content, copy);

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
clipboard_prepend_comment (SheetObject const *so, void *user_data)
{
	Range const *r = sheet_object_range_get (so);
	Sheet       *sheet = sheet_object_get_sheet (so);
	Cell        *the_cell = sheet_cell_get (sheet, r->start.col, r->start.row);

	if (the_cell == NULL)
		clipboard_prepend_cell (sheet,  r->start.col, r->start.row, 
					sheet_cell_fetch (sheet, r->start.col, r->start.row),
					user_data);
}

/**
 * clipboard_copy_range:
 *
 * Entry point to the clipboard copy code
 */
CellRegion *
clipboard_copy_range (Sheet *sheet, Range const *r)
{
	CellRegion *cr;
	GSList *merged, *ptr;
	GSList *comments;

	g_return_val_if_fail (IS_SHEET (sheet), NULL);
	g_return_val_if_fail (range_is_sane (r), NULL);

	cr = cellregion_new (sheet);
	cr->base = r->start;
	cr->cols = range_width (r);
	cr->rows = range_height (r);

	sheet_foreach_cell_in_range ( sheet, TRUE,
		r->start.col, r->start.row,
		r->end.col, r->end.row,
		clipboard_prepend_cell, cr);
	comments = sheet_objects_get (sheet, r, cell_comment_get_type ());
	g_slist_foreach (comments, (GFunc)clipboard_prepend_comment, cr);
	g_slist_free(comments);

	cr->styles = sheet_style_get_list (sheet, r);

	merged = sheet_merge_get_overlap (sheet, r);
	for (ptr = merged ; ptr != NULL ; ptr = ptr->next) {
		Range *tmp = range_dup (ptr->data);
		range_translate (tmp, -r->start.col, -r->start.row);
		cr->merged = g_slist_prepend (cr->merged, tmp);
	}
	g_slist_free (merged);

	return cr;
}

PasteTarget*
paste_target_init (PasteTarget *pt, Sheet *sheet, Range const *r, int flags)
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
CellRegion *
cellregion_new (Sheet *origin_sheet)
{
	CellRegion *cr = g_new0 (CellRegion, 1);
	cr->origin_sheet	= origin_sheet;
	cr->cols = cr->rows	= -1;
	cr->not_as_content	= FALSE;
	cr->content		= NULL;
	cr->styles		= NULL;
	cr->merged		= NULL;

	return cr;
}

/**
 * cellregion_free :
 * @content :
 *
 * A convenience routine to free the memory associated with a CellRegion.
 */
void
cellregion_free (CellRegion *cr)
{
	CellCopyList *l;

	g_return_if_fail (cr != NULL);

	for (l = cr->content; l; l = l->next) {
		CellCopy *this_cell = l->data;

		if (this_cell->type == CELL_COPY_TYPE_CELL) {
			/* The cell is not really in the rows or columns */
			this_cell->u.cell->base.sheet = NULL;
			this_cell->u.cell->row_info = NULL;
			this_cell->u.cell->col_info = NULL;
			cell_destroy (this_cell->u.cell);
		} else if (this_cell->u.text)
			g_free (this_cell->u.text);
		if (this_cell->comment)
			g_free (this_cell->comment);
		g_free (this_cell);
	}
	g_list_free (cr->content);
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

	g_free (cr);
}

/**
 * cellregion_to_string
 * @cr :
 *
 * Renders a CellRegion as a sequence of strings.
 */
char *
cellregion_to_string (CellRegion const *cr)
{
	GString *all, *line;
	GList *l;
	char ***data, *return_val;
	int col, row;

	g_return_val_if_fail (cr != NULL, NULL);

	data = g_new0 (char **, cr->rows);

	for (row = 0; row < cr->rows; row++)
		data [row] = g_new0 (char *, cr->cols);

	for (l = cr->content; l; l = l->next) {
		CellCopy *c_copy = l->data;
		char *v;

		if (c_copy->type != CELL_COPY_TYPE_TEXT) {
			MStyle const *mstyle = style_list_get_style (cr->styles,
				&c_copy->u.cell->pos);
			RenderedValue *rv = rendered_value_new (c_copy->u.cell,
				mstyle, FALSE);
			v = rendered_value_get_text (rv);
			rendered_value_destroy (rv);
		} else
			v = g_strdup (c_copy->u.text);

		data [c_copy->row_offset][c_copy->col_offset] = v;
	}

	all = g_string_new (NULL);
	line = g_string_new (NULL);
	for (row = 0; row < cr->rows;) {
		g_string_assign (line, "");

		for (col = 0; col < cr->cols;) {
			if (data [row][col]) {
				g_string_append (line, data [row][col]);
				g_free (data [row][col]);
			}
			if (++col < cr->cols)
				g_string_append_c (line, '\t');
		}
		g_string_append (all, line->str);
		if (++row < cr->rows)
			g_string_append_c (all, '\n');
	}

	return_val = g_strdup (all->str);

	/* Release, everything we used */
	g_string_free (line, TRUE);
	g_string_free (all, TRUE);

	for (row = 0; row < cr->rows; row++)
		g_free (data [row]);
	g_free (data);

	return return_val;
}
