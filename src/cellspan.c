/*
 * cellspan.c: Keep track of the columns on which a cell
 * displays information.
 *
 * Author:
 *   Miguel de Icaza (miguel@gnu.org)
 *   Jody Goldberg (jody@gnome.org)
 *
 * The information on cell spanning is attached in the row ColRowInfo
 * structures.  The actual representation of this information is
 * opaque to the code that uses it (the idea is: this first
 * implementation is not really awesome).
 *
 * The reason we need this is that the Grid draw code expects to find
 * the "owner" of the cell to be able to repaint its contents.
 *
 */
#include <gnumeric-config.h>
#include "gnumeric.h"
#include "cellspan.h"

#include "cell.h"
#include "sheet.h"
#include "sheet-merge.h"
#include "sheet-style.h"
#include "style.h"
#include "colrow.h"
#include "value.h"
#include "rendered-value.h"

static guint
col_hash (gconstpointer key)
{
	return GPOINTER_TO_INT(key);
}

static gint
col_compare (gconstpointer a, gconstpointer b)
{
	if (GPOINTER_TO_INT(a) == GPOINTER_TO_INT(b))
		return 1;
	return 0;
}

static void
free_hash_value (gpointer key, gpointer value, gpointer user_data)
{
	g_free (value);
}

void
row_destroy_span (ColRowInfo *ri)
{
	if (ri == NULL || ri->spans == NULL)
		return;

	g_hash_table_foreach (ri->spans, free_hash_value, NULL);
	g_hash_table_destroy (ri->spans);
	ri->spans = NULL;
}

/*
 * cell_register_span
 * @cell:  The cell to register the span
 * @left:  the leftmost column used by the cell
 * @right: the rightmost column used by the cell
 *
 * Registers the region
 */
void
cell_register_span (Cell const * const cell, int left, int right)
{
	ColRowInfo *ri;
	int col, i;

	g_return_if_fail (cell != NULL);
	g_return_if_fail (left <= right);

	ri = cell->row_info;
	col = cell->pos.col;

	if (left == right)
		return;

	if (ri->spans == NULL)
		ri->spans = g_hash_table_new (col_hash, col_compare);

	for (i = left; i <= right; i++){
		CellSpanInfo *spaninfo = g_new (CellSpanInfo, 1);

		spaninfo->cell  = cell;
		spaninfo->left  = left;
		spaninfo->right = right;

		g_return_if_fail (row_span_get (ri, i) == NULL);
		g_hash_table_insert (ri->spans, GINT_TO_POINTER(i), spaninfo);
	}
}

static gboolean
span_remove (gpointer key, gpointer value, gpointer user_data)
{
	CellSpanInfo *span = (CellSpanInfo *)value;
	Cell *cell = user_data;

	if (cell == span->cell) {
		g_free (span); /* free the span descriptor */
		return TRUE;
	}
	return FALSE;
}

/*
 * sheet_cell_unregister_span
 * @cell: The cell to remove from the span information
 *
 * Remove all references to this cell in the span hashtable
 */
void
cell_unregister_span (Cell const * const cell)
{
	g_return_if_fail (cell != NULL);
	g_return_if_fail (cell->row_info != NULL);

	if (cell->row_info->spans == NULL)
		return;

	g_hash_table_foreach_remove (cell->row_info->spans, &span_remove,
				     (gpointer)cell);
}

/*
 * row_span_get
 * @ri: The ColRowInfo for the row we are looking up
 * @col: the column position
 *
 * Returns SpanInfo of the spanning cell being display at the
 * column.  Including
 *   - the cell whose contents span.
 *   - The first and last col in the span.
 */
CellSpanInfo const *
row_span_get (ColRowInfo const * const ri, int const col)
{
	g_return_val_if_fail (ri != NULL, NULL);

	if (ri->spans == NULL)
		return NULL;
	return g_hash_table_lookup (ri->spans, GINT_TO_POINTER(col));
}

/**
 * cellspan_is_empty :
 *
 * Utility to ensure that a cell is completely empty.
 *    - no spans
 *    - no merged regions
 *    - no content
 *
 * No need to check for merged cells here.  We have already bounded the search region
 * using adjacent merged cells.
 *
 * We could probably have done the same thing with the span regions too, but
 * the current representation is not well suited to that type of search
 * returns TRUE if the cell is empty.
 */
static inline gboolean
cellspan_is_empty (int col, ColRowInfo const *ri, Cell const *ok_span_cell)
{
	CellSpanInfo const *span = row_span_get (ri, col);
	Cell const *tmp;

	if (span != NULL && span->cell != ok_span_cell)
		return FALSE;

	tmp = sheet_cell_get (ok_span_cell->base.sheet, col, ri->pos);
	/* FIXME : cannot use cell_is_blank until expressions can span.
	 * because cells with expressions start out with value Empty
	 * existing spans continue to flow through, but never get removed
	 * because we don't respan expression results.
	 */
	return (tmp == NULL || tmp->value == NULL ||
		(tmp->value->type == VALUE_EMPTY && !cell_has_expr(tmp)));
}

/*
 * cell_calc_span:
 * @cell:   The cell we will examine
 * @col1:   return value: the first column used by this cell
 * @col2:   return value: the last column used by this cell
 *
 * This routine returns the column interval used by a Cell.
 */
void
cell_calc_span (Cell const * const cell, int * const col1, int * const col2)
{
	Sheet *sheet;
	int align, left, max_col, min_col;
	int row, pos, margin;
	int cell_width_pixel, indented_w;
	MStyle *mstyle;
	ColRowInfo const *ri;
	Range const *merge_left;
	Range const *merge_right;

	g_return_if_fail (cell != NULL);

	sheet = cell->base.sheet;
	ri = cell->row_info;

        /*
	 * Report only one column is used if
	 *	- Cell has an expression
	 *	- Cell is in a hidden col
	 * 	- Cell is a number
	 * 	- Cell is the top left of a merged cell
	 * 	- The text fits inside column (for non center across selection)
	 * 	- The alignment mode are set to "justify"
	 */
	if (sheet != NULL &&
	    (cell_is_merged (cell) ||
	     (!sheet->display_formulas && cell_is_number (cell)))) {
		*col1 = *col2 = cell->pos.col;
		return;
	}

	mstyle = cell_get_mstyle (cell);
	align = style_default_halign (mstyle, cell);
	row   = cell->pos.row;
	indented_w = cell_width_pixel = cell_rendered_width (cell);
	if (align == HALIGN_LEFT || align == HALIGN_RIGHT)
		indented_w += cell_rendered_offset (cell);

	if (cell_has_expr (cell) ||
	    cell_is_blank (cell) ||
	    !cell->col_info->visible ||
	    ((indented_w <= COL_INTERNAL_WIDTH (cell->col_info)) &&
	     align != HALIGN_CENTER_ACROSS_SELECTION) ||
	    align == HALIGN_JUSTIFY ||
	    align == HALIGN_FILL ||
	    mstyle_get_wrap_text (mstyle) ||
	    mstyle_get_align_v (mstyle) == VALIGN_JUSTIFY) {
		*col1 = *col2 = cell->pos.col;
		return;
	}

	sheet_merge_get_adjacent (sheet, &cell->pos, &merge_left, &merge_right);
	min_col = (merge_left != NULL) ? merge_left->end.col : -1;
	max_col = (merge_right != NULL) ? merge_right->start.col : SHEET_MAX_COLS;

	*col1 = *col2 = cell->pos.col;
	switch (align) {
	case HALIGN_LEFT:
		pos = cell->pos.col + 1;
		left = indented_w - COL_INTERNAL_WIDTH (cell->col_info);
		margin = cell->col_info->margin_b;

		for (; left > 0 && pos < max_col; pos++){
			ColRowInfo const *ci = sheet_col_get_info (sheet, pos);

			if (ci->visible) {
				if (!cellspan_is_empty (pos, ri, cell))
					return;

				/* The space consumed is:
				 *   - The margin_b from the last column
				 *   - The width of the cell
				 */
				left -= COL_INTERNAL_WIDTH (ci) +
					margin + ci->margin_a;
				*col2 = pos;
			}
			margin = ci->margin_b;
		}
		return;

	case HALIGN_RIGHT:
		pos = cell->pos.col - 1;
		left = indented_w - COL_INTERNAL_WIDTH (cell->col_info);
		margin = cell->col_info->margin_a;

		for (; left > 0 && pos > min_col; pos--){
			ColRowInfo const *ci = sheet_col_get_info (sheet, pos);

			if (ci->visible) {
				if (!cellspan_is_empty (pos, ri, cell))
					return;

				/* The space consumed is:
				 *   - The margin_a from the last column
				 *   - The width of this cell
				 */
				left -= COL_INTERNAL_WIDTH (ci) +
					margin + ci->margin_b;
				*col1 = pos;
			}
			margin = ci->margin_a;
		}
		return;

	case HALIGN_CENTER: {
		int remain_left, remain_right;
		int margin_a, margin_b, pos_l, pos_r;

		pos_l = pos_r = cell->pos.col;
		left = cell_width_pixel -  COL_INTERNAL_WIDTH (cell->col_info);

		remain_left  = left / 2 + (left % 2);
		remain_right = left / 2;
		margin_a = cell->col_info->margin_a;
		margin_b = cell->col_info->margin_b;

		for (; remain_left > 0 || remain_right > 0;){
			ColRowInfo const *ci;

			if (--pos_l > min_col){
				ci = sheet_col_get_info (sheet, pos_l);

				if (ci->visible) {
					if (cellspan_is_empty (pos_l, ri, cell)) {
						remain_left -= COL_INTERNAL_WIDTH (ci) +
							margin_a + ci->margin_b;
						margin_a = ci->margin_a;
						*col1 = pos_l;
					} else
						remain_left = 0;
				} else
				    pos_l = margin_a = ci->margin_a;
			} else
				remain_left = 0;

			if (++pos_r < max_col){
				ci = sheet_col_get_info (sheet, pos_r);

				if (ci->visible) {
					if (cellspan_is_empty (pos_r, ri, cell)) {
						remain_right -= COL_INTERNAL_WIDTH (ci) +
							margin_b + ci->margin_a;
						margin_b = ci->margin_b;
						*col2 = pos_r;
					} else
						max_col = remain_right = 0;
				} else
					margin_b = ci->margin_b;
			} else
				remain_right = 0;

		} /* for */
		break;
	} /* case HALIGN_CENTER */

	case HALIGN_CENTER_ACROSS_SELECTION:
	{
		int const row = ri->pos;
		int pos_l, pos_r;

		pos_l = pos_r = cell->pos.col;
		while (--pos_l > min_col) {
			ColRowInfo const *ci = sheet_col_get_info (sheet, pos_l);
			if (ci->visible) {
				if (cellspan_is_empty (pos_l, ri, cell)) {
					MStyle * const mstyle =
						sheet_style_get (cell->base.sheet, pos_l, row);

					if (mstyle_get_align_h (mstyle) != HALIGN_CENTER_ACROSS_SELECTION)
						break;
					*col1 = pos_l;
				} else
					break;
			}
		}
		while (++pos_r < max_col) {
			ColRowInfo const *ci = sheet_col_get_info (sheet, pos_r);
			if (ci->visible) {
				if (cellspan_is_empty (pos_r, ri, cell)) {
					MStyle * const mstyle =
						sheet_style_get (cell->base.sheet, pos_r, row);

					if (mstyle_get_align_h (mstyle) != HALIGN_CENTER_ACROSS_SELECTION)
						break;
					*col2 = pos_r;
				} else
					break;
			}
		}
		break;
	}

	default:
		g_warning ("Unknown horizontal alignment type %d\n", align);
	} /* switch */
}
