/*
 * Clipboard.c: Implements the copy/paste operations
 *
 * Author:
 *  Miguel de Icaza (miguel@gnu.org)
 */
#include <config.h>
#include <gnome.h>
#include <locale.h>
#include <string.h>
#include <ctype.h>
#include "gnumeric.h"
#include "gnumeric-util.h"
#include "clipboard.h"
#include "dependent.h"
#include "sheet.h"
#include "sheet-style.h"
#include "sheet-merge.h"
#include "eval.h"
#include "selection.h"
#include "application.h"
#include "render-ascii.h"
#include "command-context.h"
#include "workbook-control.h"
#include "workbook.h"
#include "ranges.h"
#include "expr.h"
#include "commands.h"

#include "xml-io.h"
#include "value.h"

#include "dialog-stf.h"
#include "stf-parse.h"

static gboolean
cell_has_expr_or_number_or_blank (Cell const * cell)
{
	return (cell_is_blank (cell) ||
		(cell != NULL && cell_is_number (cell)) ||
		(cell != NULL && cell_has_expr (cell)));
}

static ExprTree *
cell_get_contents_as_expr_tree (Cell const * cell)
{
	ExprTree *expr = NULL;

	g_return_val_if_fail (cell_has_expr_or_number_or_blank (cell), NULL);

	if (cell_is_blank (cell))
		expr = expr_tree_new_constant (value_new_float (0.0));
	else if (cell_has_expr (cell)) {
		expr = cell->base.expression;
		expr_tree_ref (expr);
	} else if (cell_is_number (cell))
		expr = expr_tree_new_constant (value_duplicate (cell->value));
	else
		g_assert_not_reached ();

	return expr;
}

static Operation
paste_oper_to_expr_oper (int paste_flags)
{
	g_return_val_if_fail (paste_flags & PASTE_OPER_MASK, 0);

	if (paste_flags & PASTE_OPER_ADD)
		return OPER_ADD;
	else if (paste_flags & PASTE_OPER_SUB)
		return OPER_SUB;
	else if (paste_flags & PASTE_OPER_MULT)
		return OPER_MULT;
	else if (paste_flags & PASTE_OPER_DIV)
		return OPER_DIV;
	else
		g_assert_not_reached ();

	return 0;
}

static Value *
apply_paste_oper_to_values (Cell const *old_cell, Cell const *copied_cell,
			    Cell const *new_cell, int paste_flags)
{
	EvalPos pos;
	ExprTree expr, arg_a, arg_b;
	Operation op;

	g_return_val_if_fail (paste_flags & PASTE_OPER_MASK, NULL);

	if (paste_flags & PASTE_OPER_ADD)
		op = OPER_ADD;
	else if (paste_flags & PASTE_OPER_SUB)
		op = OPER_SUB;
	else if (paste_flags & PASTE_OPER_MULT)
		op = OPER_MULT;
	else if (paste_flags & PASTE_OPER_DIV)
		op = OPER_DIV;
	else {
		op = OPER_ADD;
		g_assert_not_reached ();
	}

	*((Operation *)&(arg_a.constant.oper)) = OPER_CONSTANT;
	arg_a.constant.value = old_cell->value;
	*((Operation *)&(arg_b.constant.oper)) = OPER_CONSTANT;
	arg_b.constant.value = copied_cell->value;

	*((Operation *)&(expr.binary.oper)) = op;
	expr.binary.value_a = &arg_a;
	expr.binary.value_b = &arg_b;

	return eval_expr (eval_pos_init_cell (&pos, new_cell), &expr, EVAL_STRICT);
}

static void
paste_cell_with_operation (Sheet *dest_sheet,
			   int target_col, int target_row,
			   ExprRewriteInfo *rwinfo,
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
		ExprTree *old_expr    = cell_get_contents_as_expr_tree (new_cell);
		ExprTree *copied_expr = cell_get_contents_as_expr_tree (c_copy->u.cell);
		Operation oper	      = paste_oper_to_expr_oper (paste_flags);
		ExprTree *new_expr    = expr_tree_new_binary (old_expr, oper, copied_expr);
		cell_set_expr (new_cell, new_expr, NULL);
		cell_relocate (new_cell, rwinfo);
	} else {
		Value *new_val = apply_paste_oper_to_values (new_cell, c_copy->u.cell,
							     new_cell, paste_flags);

		cell_set_value (new_cell, new_val, c_copy->u.cell->format);
	}
}

static void
paste_link (Sheet *dest_sheet,
	    int source_col, int source_row,
	    int target_col, int target_row)
{
	ExprTree *expr;
	Cell *cell;
	CellRef source_cell_ref;

	cell = sheet_cell_fetch (dest_sheet, target_col, target_row);

	/* FIXME : This is broken
	 * 1) should be relative not absolute (add toggle ?)
	 * 2) this is the WRONG SHEET !
	 */
	source_cell_ref.sheet = dest_sheet;
	source_cell_ref.col = source_col;
	source_cell_ref.row = source_row;
	source_cell_ref.col_relative = 0;
	source_cell_ref.row_relative = 0;
	expr = expr_tree_new_var (&source_cell_ref);

	cell_set_expr (cell, expr, NULL);
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
	    ExprRewriteInfo *rwinfo,
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
		Cell *new_cell = sheet_cell_fetch (dest_sheet, target_col, target_row);
		Cell *src_cell = c_copy->u.cell;

		if (!src_cell) {
			g_warning ("Cell copy type set but no cell found (this is bad!)");
			return;
		}
			
		if (cell_has_expr (src_cell)) {
			cell_set_expr_and_value (new_cell, src_cell->base.expression,
						 value_duplicate (src_cell->value), src_cell->format, FALSE);
			
			if (paste_flags & PASTE_CONTENT)
				cell_relocate (new_cell, rwinfo);
			else
				cell_convert_expr_to_value (new_cell);
		} else
				cell_set_value (new_cell, value_duplicate (src_cell->value), src_cell->format);

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
			CellRegion *content)
{
	int tmp;
	int repeat_horizontal, repeat_vertical;
	int dst_cols = pt->range.end.col - pt->range.start.col + 1;
	int dst_rows = pt->range.end.row - pt->range.start.row + 1;
	int src_cols = content->cols;
	int src_rows = content->rows;
	gboolean has_content;

	g_return_val_if_fail (pt != NULL, TRUE);

	has_content = pt->paste_flags & (PASTE_CONTENT|PASTE_AS_VALUES);

	g_return_val_if_fail (has_content != (PASTE_CONTENT|PASTE_AS_VALUES), TRUE);

	if (pt->paste_flags & PASTE_TRANSPOSE) {
		int tmp = src_cols;
		src_cols = src_rows;
		src_rows = tmp;
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

	tmp = 0;
	/* clear the region where we will paste */
	if (has_content)
		tmp = CLEAR_VALUES | CLEAR_COMMENTS;

	/* No need to clear the formats.  We will paste over top of these. */
	/* if (pt->paste_flags & PASTE_FORMATS) tmp |= CLEAR_FORMATS; */

	if (pt->paste_flags & (PASTE_OPER_MASK | PASTE_SKIP_BLANKS))
		tmp = 0;
	if (tmp) {
		int const dst_col = pt->range.start.col;
		int const dst_row = pt->range.start.row;
		sheet_clear_region (wbc, pt->sheet,
				    dst_col, dst_row,
				    dst_col + dst_cols - 1,
				    dst_row + dst_rows - 1,
				    tmp);
	}

	/* remove and merged regions in the target range */
	if (has_content) {
		GSList *merged, *ptr;
		merged = sheet_merge_get_overlap (pt->sheet, &pt->range);
		for (ptr = merged ; ptr != NULL ; ptr = ptr->next)
			sheet_merge_remove (wbc, pt->sheet, ptr->data);
	}

	for (tmp = repeat_vertical; repeat_horizontal-- > 0 ; repeat_vertical = tmp)
		while (repeat_vertical-- > 0) {
			int const left = repeat_horizontal * src_cols + pt->range.start.col;
			int const top = repeat_vertical * src_rows + pt->range.start.row;
			CellCopyList *l;
			ExprRewriteInfo   rwinfo;
			ExprRelocateInfo *rinfo;

			rwinfo.type = EXPR_REWRITE_RELOCATE;
			rinfo = &rwinfo.u.relocate;
			rinfo->origin_sheet = rinfo->target_sheet = pt->sheet;

			if (pt->paste_flags & PASTE_EXPR_RELOCATE) {
				rinfo->origin.start.col = content->base_col;
				rinfo->origin.end.col = content->base_col + content->cols -1;
				rinfo->origin.start.row = content->base_row;
				rinfo->origin.end.row = content->base_row + content->rows -1;
				rinfo->col_offset = left - content->base_col;
				rinfo->row_offset = top - content->base_row;
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
					if (!range_translate (&tmp, left, top))
						sheet_merge_add (wbc, pt->sheet, &tmp, TRUE);
				}
			}

			if (!has_content)
				continue;

			for (l = content->list; l; l = l->next) {
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
				if (pt->paste_flags & PASTE_EXPR_RELOCATE) {
					rinfo->pos.eval.col = content->base_col + c_copy->col_offset;
					rinfo->pos.eval.row = content->base_row + c_copy->row_offset;
				} else {
					rinfo->pos.eval.col = target_col;
					rinfo->pos.eval.row = target_row;
				}

				if (pt->paste_flags & PASTE_LINK) {
					int source_col = content->base_col + c_copy->col_offset;
					int source_row = content->base_row + c_copy->row_offset;

					paste_link (pt->sheet, source_col, source_row,
						    target_col, target_row);
				} else
					paste_cell (pt->sheet, target_col, target_row,
						    &rwinfo, c_copy, pt->paste_flags);
			}
		}

        if (has_content) {
		GList *deps = sheet_region_get_deps (pt->sheet, &pt->range);
		if (deps)
			dependent_queue_recalc_list (deps, TRUE);
		sheet_range_calc_spans (pt->sheet, pt->range, SPANCALC_RENDER);
		sheet_flag_status_update_range (pt->sheet, &pt->range);
	} else
		sheet_flag_format_update_range (pt->sheet, &pt->range);

	if (pt->paste_flags & PASTE_UPDATE_ROW_HEIGHT)
		rows_height_update (pt->sheet, &pt->range);
	
	return FALSE;
}

static Value *
clipboard_prepend_cell (Sheet *sheet, int col, int row, Cell *cell, void *user_data)
{
	CellRegion *c = user_data;
	CellCopy *copy;

	copy = g_new (CellCopy, 1);
	copy->type       = CELL_COPY_TYPE_CELL;
	copy->u.cell     = cell_copy (cell);
	copy->u.cell->pos.col = copy->col_offset = col - c->base_col;
	copy->u.cell->pos.row = copy->row_offset = row - c->base_row;

	c->list = g_list_prepend (c->list, copy);

	return NULL;
}

/**
 * clipboard_copy_range:
 *
 * Entry point to the clipboard copy code
 */
CellRegion *
clipboard_copy_range (Sheet *sheet, Range const *r)
{
	CellRegion *c;
	GSList *merged, *ptr;

	g_return_val_if_fail (IS_SHEET (sheet), NULL);
	g_return_val_if_fail (r != NULL, NULL);
	g_return_val_if_fail (r->start.col <= r->end.col, NULL);
	g_return_val_if_fail (r->start.row <= r->end.row, NULL);

	c = g_new0 (CellRegion, 1);

	c->base_col = r->start.col;
	c->base_row = r->start.row;
	c->cols = r->end.col - r->start.col + 1;
	c->rows = r->end.row - r->start.row + 1;

	/*
	 * We assume that the cells are traversed somehow starting at
	 * the upper left corner.  We don't depend on whether it is
	 * row-major or col-major.
	 */
	sheet_foreach_cell_in_range ( sheet, TRUE,
		r->start.col, r->start.row,
		r->end.col, r->end.row,
		clipboard_prepend_cell, c);
	/* reverse the list so that upper left corner is first */
	c->list = g_list_reverse (c->list);

	c->styles = sheet_style_get_list (sheet, r);

	c->merged = NULL;
	merged = sheet_merge_get_overlap (sheet, r);
	for (ptr = merged ; ptr != NULL ; ptr = ptr->next) {
		Range *tmp = range_copy (ptr->data);
		range_translate (tmp, -r->start.col, -r->start.row);
		c->merged = g_slist_prepend (c->merged, tmp);
	}
	g_slist_free (merged);

	return c;
}

/**
 * clipboard_paste :
 * @content:     a Cell Region that contains information to be pasted
 * @time:        Time at which the event happened.
 */
void
clipboard_paste (WorkbookControl *wbc, PasteTarget const *pt, guint32 time)
{
	CellRegion *content;

	g_return_if_fail (pt != NULL);
	g_return_if_fail (IS_SHEET (pt->sheet));

	content = application_clipboard_contents_get ();

	/* If this application has marked a selection use it */
	if (content) {
		cmd_paste_copy (wbc, pt, content);
		return;
	}

	/* See if the control has access to information to paste */
	wb_control_paste_from_selection (wbc, pt, time);
}

/*
 * Destroys the contents of a CellRegion
 */
void
clipboard_release (CellRegion *content)
{
	CellCopyList *l;

	g_return_if_fail (content != NULL);

	for (l = content->list; l; l = l->next) {
		CellCopy *this_cell = l->data;

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
	if (content->styles != NULL) {
		style_list_free (content->styles);
		content->styles = NULL;
	}
	if (content->merged != NULL) {
		GSList *ptr;
		for (ptr = content->merged; ptr != NULL ; ptr = ptr->next)
			g_free (ptr->data);
		g_slist_free (content->merged);
		content->merged = NULL;
	}

	g_list_free (content->list);
	content->list = NULL;

	g_free (content);
}

PasteTarget*
paste_target_init (PasteTarget *pt, Sheet *sheet, Range const *r, int flags)
{
	pt->sheet = sheet;
	pt->range = *r;
	pt->paste_flags = flags;
	return pt;
}
