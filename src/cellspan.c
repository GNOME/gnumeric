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
cell_register_span (Cell *cell, int left, int right)
{
	ColRowInfo *ri;
	int col, i;

	g_return_if_fail (cell != NULL);
	g_return_if_fail (left <= right);

	ri = cell->row;
	col = cell->col->pos;

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
span_remove(gpointer key, gpointer value, gpointer user_data)
{
	CellSpanInfo *span = (CellSpanInfo *)value;
	if ((Cell *)user_data == span->cell) {
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
cell_unregister_span (Cell *cell)
{
	g_return_if_fail (cell != NULL);
	g_return_if_fail (cell->row != NULL);

	if (cell->row->spans == NULL)
		return;

	g_hash_table_foreach_remove (cell->row->spans, &span_remove,
				     (gpointer)cell);
}

/*
 * row_cell_get_displayed_at
 * @ri: The ColRowInfo for the row we are looking up
 * @col: the column position
 *
 * Returns the Cell* which happens to display at the column
 */
Cell *
row_cell_get_displayed_at (ColRowInfo const * const ri, int const col)
{
	CellSpanInfo const * spaninfo;

	g_return_val_if_fail (ri != NULL, NULL);

	if (ri->spans == NULL)
		return NULL;
	spaninfo = g_hash_table_lookup (ri->spans, GINT_TO_POINTER(col));
	return spaninfo ? spaninfo->cell : NULL;
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
