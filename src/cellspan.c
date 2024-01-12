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
 */
#include <gnumeric-config.h>
#include <gnumeric.h>
#include <cellspan.h>

#include <cell.h>
#include <sheet.h>
#include <sheet-merge.h>
#include <sheet-style.h>
#include <style.h>
#include <colrow.h>
#include <value.h>
#include <rendered-value.h>

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
free_hash_value (G_GNUC_UNUSED gpointer key, gpointer value,
		 G_GNUC_UNUSED gpointer user_data)
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
cell_register_span (GnmCell const *cell, int left, int right)
{
	ColRowInfo *ri;
	int row, i;

	g_return_if_fail (cell != NULL);
	g_return_if_fail (left <= right);

	row = cell->pos.row;
	ri = sheet_row_get (cell->base.sheet, row);

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
span_remove (G_GNUC_UNUSED gpointer key, gpointer value,
	     gpointer user_data)
{
	CellSpanInfo *span = (CellSpanInfo *)value;
	GnmCell *cell = user_data;

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
cell_unregister_span (GnmCell const * const cell)
{
	ColRowInfo *ri;

	g_return_if_fail (cell != NULL);

	ri = sheet_row_get (cell->base.sheet, cell->pos.row);

	if (ri->spans == NULL)
		return;

	g_hash_table_foreach_remove (ri->spans,
				     &span_remove, (gpointer)cell);
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

/* making CellSpanInfo a boxed type. As this objects are constant, no need
 * to really copy free them. Right? */
static const CellSpanInfo*
cell_span_info_copy (CellSpanInfo const *sp)
{
	return sp;
}

GType
cell_span_info_get_type (void)
{
	static GType t = 0;

	if (t == 0) {
		t = g_boxed_type_register_static ("CellSpanInfo",
			 (GBoxedCopyFunc)cell_span_info_copy,
			 (GBoxedFreeFunc)cell_span_info_copy);
	}
	return t;
}

/**
 * cellspan_is_empty:
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
 * returns %TRUE if the cell is empty.
 */
static inline gboolean
cellspan_is_empty (int col, GnmCell const *ok_span_cell)
{
	Sheet *sheet = ok_span_cell->base.sheet;
	int row = ok_span_cell->pos.row;
	ColRowInfo *ri = sheet_row_get (sheet, row);
	CellSpanInfo const *span = row_span_get (ri, col);
	GnmCell const *tmp;

	if (span != NULL && span->cell != ok_span_cell)
		return FALSE;

	tmp = sheet_cell_get (sheet, col, row);

	/* FIXME : cannot use gnm_cell_is_empty until expressions can span.
	 * because cells with expressions start out with value Empty
	 * existing spans continue to flow through, but never get removed
	 * because we don't respan expression results.
	 */
	return (tmp == NULL || tmp->value == NULL ||
		(VALUE_IS_EMPTY (tmp->value) && !gnm_cell_has_expr(tmp)));
}

/*
 * cell_calc_span:
 * @cell:   The cell we will examine
 * @col1:   return value: the first column used by this cell
 * @col2:   return value: the last column used by this cell
 *
 * This routine returns the column interval used by a GnmCell.
 */
void
cell_calc_span (GnmCell const *cell, int *col1, int *col2)
{
	Sheet *sheet;
	int h_align, v_align, left, max_col, min_col;
	int pos;
	int cell_width_pixel, indented_w;
	GnmStyle const *style;
	ColRowInfo const *ci;
	GnmRange const *merge_left;
	GnmRange const *merge_right;

	g_return_if_fail (cell != NULL);

	sheet = cell->base.sheet;
	style = gnm_cell_get_effective_style (cell);
	h_align = gnm_style_default_halign (style, cell);

        /*
	 * Report only one column is used if
	 *	- Cell is in a hidden col
	 *	- Cell is a number
	 *	- Cell is the top left of a merged cell
	 *	- The text fits inside column (for non center across selection)
	 *	- The alignment mode are set to "justify"
	 */
	if (sheet != NULL &&
	    h_align != GNM_HALIGN_CENTER_ACROSS_SELECTION &&
	    (gnm_cell_is_merged (cell) ||
	     (!sheet->display_formulas && gnm_cell_is_number (cell)))) {
		*col1 = *col2 = cell->pos.col;
		return;
	}

	v_align = gnm_style_get_align_v (style);
	indented_w = cell_width_pixel = gnm_cell_rendered_width (cell);
	if (h_align == GNM_HALIGN_LEFT || h_align == GNM_HALIGN_RIGHT) {
		GnmRenderedValue *rv = gnm_cell_get_rendered_value (cell);
		char const *text = (rv)? pango_layout_get_text (rv->layout): NULL;
		PangoDirection dir = (text && *text)? pango_find_base_dir (text, -1): PANGO_DIRECTION_LTR;
		if (gnm_style_get_align_h (style) == GNM_HALIGN_GENERAL && dir == PANGO_DIRECTION_RTL)
			h_align = GNM_HALIGN_RIGHT;
		indented_w += gnm_cell_rendered_offset (cell);
		if (sheet->text_is_rtl)
			h_align = (h_align == GNM_HALIGN_LEFT) ? GNM_HALIGN_RIGHT : GNM_HALIGN_LEFT;
	}

	ci = sheet_col_get_info	(sheet, cell->pos.col);
	if (gnm_cell_is_empty (cell) ||
	    !ci->visible ||
	    (h_align != GNM_HALIGN_CENTER_ACROSS_SELECTION &&
		 (gnm_style_get_wrap_text (style) ||
		  indented_w <= COL_INTERNAL_WIDTH (ci))) ||
	    h_align == GNM_HALIGN_JUSTIFY ||
	    h_align == GNM_HALIGN_FILL ||
	    h_align == GNM_HALIGN_DISTRIBUTED ||
	    v_align == GNM_VALIGN_JUSTIFY ||
	    v_align == GNM_VALIGN_DISTRIBUTED) {
		*col1 = *col2 = cell->pos.col;
		return;
	}

	gnm_sheet_merge_get_adjacent (sheet, &cell->pos, &merge_left, &merge_right);
	min_col = (merge_left != NULL) ? merge_left->end.col : -1;
	max_col = (merge_right != NULL) ? merge_right->start.col : gnm_sheet_get_max_cols (sheet);

	*col1 = *col2 = cell->pos.col;
	switch (h_align) {
	case GNM_HALIGN_LEFT:
		pos = cell->pos.col + 1;
		left = indented_w - COL_INTERNAL_WIDTH (ci);

		for (; left > 0 && pos < max_col; pos++){
			ColRowInfo const *ci = sheet_col_get_info (sheet, pos);

			if (ci->visible) {
				if (!cellspan_is_empty (pos, cell))
					return;

				/* The space consumed is:
				 *   - The margin_b from the last column
				 *   - The width of the cell
				 */
				left -= ci->size_pixels - 1;
				*col2 = pos;
			}
		}
		return;

	case GNM_HALIGN_RIGHT:
		pos = cell->pos.col - 1;
		left = indented_w - COL_INTERNAL_WIDTH (ci);

		for (; left > 0 && pos > min_col; pos--){
			ColRowInfo const *ci = sheet_col_get_info (sheet, pos);

			if (ci->visible) {
				if (!cellspan_is_empty (pos, cell))
					return;

				/* The space consumed is:
				 *   - The margin_a from the last column
				 *   - The width of this cell
				 */
				left -= ci->size_pixels - 1;
				*col1 = pos;
			}
		}
		return;

	case GNM_HALIGN_CENTER: {
		int remain_left, remain_right;
		int pos_l, pos_r;

		pos_l = pos_r = cell->pos.col;
		left = cell_width_pixel - COL_INTERNAL_WIDTH (ci);

		remain_left  = left / 2 + (left % 2);
		remain_right = left / 2;

		for (; remain_left > 0;)
			if (--pos_l > min_col){
				ColRowInfo const *ci = sheet_col_get_info (sheet, pos_l);

				if (ci->visible) {
					if (cellspan_is_empty (pos_l, cell)) {
						remain_left -= ci->size_pixels - 1;
						*col1 = pos_l;
					} else
						remain_left = 0;
				}
			} else
				remain_left = 0;

		for (; remain_right > 0;)
			if (++pos_r < max_col){
				ColRowInfo const *ci = sheet_col_get_info (sheet, pos_r);

				if (ci->visible) {
					if (cellspan_is_empty (pos_r, cell)) {
						remain_right -= ci->size_pixels - 1;
						*col2 = pos_r;
					} else
						max_col = remain_right = 0;
				}
			} else
				remain_right = 0;
		break;
	} /* case GNM_HALIGN_CENTER */

	case GNM_HALIGN_CENTER_ACROSS_SELECTION: {
		int const row = cell->pos.row;
		int pos_l, pos_r;

		pos_l = pos_r = cell->pos.col;
		while (--pos_l > min_col) {
			ColRowInfo const *ci = sheet_col_get_info (sheet, pos_l);
			if (ci->visible) {
				if (cellspan_is_empty (pos_l, cell)) {
					GnmStyle const * const style =
						sheet_style_get (cell->base.sheet, pos_l, row);

					if (gnm_style_get_align_h (style) != GNM_HALIGN_CENTER_ACROSS_SELECTION)
						break;
					*col1 = pos_l;
				} else
					break;
			}
		}
		while (++pos_r < max_col) {
			ColRowInfo const *ci = sheet_col_get_info (sheet, pos_r);
			if (ci->visible) {
				if (cellspan_is_empty (pos_r, cell)) {
					GnmStyle const * const style =
						sheet_style_get (cell->base.sheet, pos_r, row);

					if (gnm_style_get_align_h (style) != GNM_HALIGN_CENTER_ACROSS_SELECTION)
						break;
					*col2 = pos_r;
				} else
					break;
			}
		}
		break;
	}

	default:
		g_warning ("Unknown horizontal alignment type %x.", h_align);
	} /* switch */
}

void
row_calc_spans (ColRowInfo *ri, int row, Sheet const *sheet)
{
	int left, right, col;
	GnmRange const *merged;
	GnmCell *cell;
	int const last = sheet->cols.max_used;

	row_destroy_span (ri);
	for (col = 0 ; col <= last ; ) {
		cell = sheet_cell_get (sheet, col, row);
		if (cell == NULL) {
			/* skip segments with no cells */
			if (col == COLROW_SEGMENT_START (col) &&
			    NULL == COLROW_GET_SEGMENT (&(sheet->cols), col))
				col = COLROW_SEGMENT_END (col) + 1;
			else
				col++;
			continue;
		}

		/* render as necessary */
		(void)gnm_cell_fetch_rendered_value (cell, TRUE);

		if (gnm_cell_is_merged (cell)) {
			merged = gnm_sheet_merge_is_corner (sheet, &cell->pos);
			if (NULL != merged) {
				col = merged->end.col + 1;
				continue;
			}
		}

		cell_calc_span (cell, &left, &right);
		if (left != right) {
			cell_register_span (cell, left, right);
			col = right + 1;
		} else
			col++;
	}

	ri->needs_respan = FALSE;
}
