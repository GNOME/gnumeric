/*
 * cellspan.c: Keep track of the columns on which a cell
 * displays information.
 *
 * Author:
 *   Miguel de Icaza (miguel@gnu.org)
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
#include <config.h>
#include "cellspan.h"
#include "cell.h"
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

	if (ri->spans == NULL)
		ri->spans = g_hash_table_new (col_hash, col_compare);

	for (i = left; i <= right; i++){
		CellSpanInfo *spaninfo = g_new (CellSpanInfo, 1);

		spaninfo->cell  = cell;
		spaninfo->left  = left;
		spaninfo->right = right;

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

static inline int
cell_contents_fit_inside_column (const Cell *cell)
{
	int const tmp = cell_rendered_width (cell);
	return (tmp <= COL_INTERNAL_WIDTH (cell->col_info));
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
	int align, left;
	int row, pos, margin;
	int cell_width_pixel;
	MStyle *mstyle;

	g_return_if_fail (cell != NULL);

        /*
	 * If the cell is a number, or the text fits inside the column, or the
	 * alignment modes are set to "justify", then we report only one
	 * column is used.
	 */

	if (cell_is_number (cell)) {
		*col1 = *col2 = cell->pos.col;
		return;
	}

	sheet = cell->sheet;
	mstyle = cell_get_mstyle (cell);
	align = value_get_default_halign (cell->value, mstyle);
	row   = cell->pos.row;

	if ((cell_contents_fit_inside_column (cell) &&
	     align != HALIGN_CENTER_ACROSS_SELECTION) ||
	    align == HALIGN_JUSTIFY ||
	    align == HALIGN_FILL ||
	    mstyle_get_fit_in_cell (mstyle) ||
	    mstyle_get_align_v (mstyle) == VALIGN_JUSTIFY) {
		*col1 = *col2 = cell->pos.col;
		mstyle_unref (mstyle);
		return;
	}
	mstyle_unref (mstyle);

	cell_width_pixel = cell_rendered_width (cell);

	switch (align) {
	case HALIGN_LEFT:
		*col1 = *col2 = cell->pos.col;
		pos = cell->pos.col + 1;
		left = cell_width_pixel - COL_INTERNAL_WIDTH (cell->col_info);
		margin = cell->col_info->margin_b;

		for (; left > 0 && pos < SHEET_MAX_COLS-1; pos++){
			ColRowInfo *ci;
			Cell *sibling;

			sibling = sheet_cell_get (sheet, pos, row);

			if (!cell_is_blank (sibling))
				return;

			ci = sheet_col_get_info (sheet, pos);

			/* The space consumed is:
			 *    - The margin_b from the last column
			 *    - The width of the cell
			 */
			left -= COL_INTERNAL_WIDTH (ci) +
				margin + ci->margin_a;
			margin = ci->margin_b;
			(*col2)++;
		}
		return;

	case HALIGN_RIGHT:
		*col1 = *col2 = cell->pos.col;
		pos = cell->pos.col - 1;
		left = cell_width_pixel - COL_INTERNAL_WIDTH (cell->col_info);
		margin = cell->col_info->margin_a;

		for (; left > 0 && pos >= 0; pos--){
			ColRowInfo *ci;
			Cell *sibling;

			sibling = sheet_cell_get (sheet, pos, row);

			if (!cell_is_blank (sibling))
				return;

			ci = sheet_col_get_info (sheet, pos);

			/* The space consumed is:
			 *   - The margin_a from the last column
			 *   - The width of this cell
			 */
			left -= COL_INTERNAL_WIDTH (ci) +
				margin + ci->margin_b;
			margin = ci->margin_a;
			(*col1)--;
		}
		return;

	case HALIGN_CENTER: {
		int left_left, left_right;
		int margin_a, margin_b;

		*col1 = *col2 = cell->pos.col;
		left = cell_width_pixel -  COL_INTERNAL_WIDTH (cell->col_info);

		left_left  = left / 2 + (left % 2);
		left_right = left / 2;
		margin_a = cell->col_info->margin_a;
		margin_b = cell->col_info->margin_b;

		for (; left_left > 0 || left_right > 0;){
			ColRowInfo *ci;
			Cell *left_sibling, *right_sibling;

			if (*col1 - 1 >= 0){
				left_sibling = sheet_cell_get (sheet, *col1 - 1, row);

				if (!cell_is_blank (left_sibling))
					left_left = 0;
				else {
					ci = sheet_col_get_info (sheet, *col1 - 1);

					left_left -= COL_INTERNAL_WIDTH (ci) +
						margin_a + ci->margin_b;
					margin_a = ci->margin_a;
					(*col1)--;
				}
			} else
				left_left = 0;

			if (*col2 + 1 < SHEET_MAX_COLS-1){
				right_sibling = sheet_cell_get (sheet, *col2 + 1, row);

				if (!cell_is_blank (right_sibling))
					left_right = 0;
				else {
					ci = sheet_col_get_info (sheet, *col2 + 1);

					left_right -= COL_INTERNAL_WIDTH (ci) +
						margin_b + ci->margin_a;
					margin_b = ci->margin_b;
					(*col2)++;
				}
			} else
				left_right = 0;

		} /* for */
		break;
	} /* case HALIGN_CENTER */

	case HALIGN_CENTER_ACROSS_SELECTION:
	{
		ColRowInfo const *ri = cell->row_info;
		int const row = ri->pos;
		int left = cell->pos.col, right = left;
		int tmp;

		left_loop :
			tmp = left - 1;
			/* When scanning left make sure not to overrun other spans */
			if (tmp >= 0 &&
			    cell_is_blank (sheet_cell_get (sheet, tmp, row)) &&
			    NULL == row_span_get (ri, tmp)) {
				MStyle * const mstyle =
				    sheet_style_compute (cell->sheet, tmp, row);
				gboolean const res =
				    (mstyle_get_align_h (mstyle) == HALIGN_CENTER_ACROSS_SELECTION);
				mstyle_unref (mstyle);

				if (res) {
					left = tmp;
					goto left_loop;
				}
			}
		right_loop :
			tmp = right + 1;
			if (tmp < SHEET_MAX_COLS &&
			    cell_is_blank (sheet_cell_get (sheet, tmp, row))) {
				MStyle * const mstyle =
				    sheet_style_compute (cell->sheet, tmp, row);
				gboolean const res =
				    (mstyle_get_align_h (mstyle) == HALIGN_CENTER_ACROSS_SELECTION);
				mstyle_unref (mstyle);

				if (res) {
					right = tmp;
					goto right_loop;
				}
			}

		*col1 = left;
		*col2 = right;
		break;
	}

	default:
		g_warning ("Unknown horizontal alignment type %d\n", align);
		*col1 = *col2 = cell->pos.col;
	} /* switch */
}
