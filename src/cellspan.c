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
#include <gnome.h>
#include "gnumeric.h"
#include "gnumeric-sheet.h"
#include "eval.h"
#include "format.h"

static guint
col_hash (gconstpointer key)
{
	const int *col = key;

	return *col;
}

static gint
col_compare (gconstpointer a, gconstpointer b)
{
	const int *col_a = a;
	const int *col_b = b;

	if (*col_a == *col_b)
		return 1;
	return 0;
}

/*
 * Initializes the hash table in the RowInfo for keeping track
 * of cell spans (ie, which cells on the spreadsheet are displayed
 * by which cell).  
 */
void
row_init_span (ColRowInfo *ri)
{
	g_return_if_fail (ri != NULL);

	ri->data = g_hash_table_new (col_hash, col_compare);
}

static void
free_hash_key (gpointer key, gpointer value, gpointer user_data)
{
	g_free (key);
}

void
row_destroy_span (ColRowInfo *ri)
{
	g_return_if_fail (ri != NULL);

	g_hash_table_foreach (ri->data, free_hash_key, NULL);
	g_hash_table_destroy (ri->data);
}

/*
 * sheet_cell_register_span
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
	
	for (i = left; i <= right; i++){
		int *key;

		/* Do not register our column, as we already keep this on the main hash */
		if (i != col)
			continue;
		
		key = g_new (int, 1);

		*key = i;
		g_hash_table_insert (ri->data, key, cell);
	}
}

typedef struct {
	Cell  *cell;
	GList *list_of_keys;
} unregister_closure_t;

static void
assemble_unregister_span_list (gpointer key, gpointer value, gpointer user_data)
{
	unregister_closure_t *c = user_data;

	if (c->cell == value)
		c->list_of_keys = g_list_prepend (c->list_of_keys, key);
}

/*
 * sheet_cell_unregister_span
 * @cell: The cell to remove from the span information
 *
 * Remove all of the references to this cell on the span hash
 * table
 */
void
cell_unregister_span (Cell *cell)
{
	unregister_closure_t c;
	GList *l;
	
	g_return_if_fail (cell != NULL);

	c.cell = cell;
	c.list_of_keys = NULL;
	g_hash_table_foreach (cell->row->data, assemble_unregister_span_list, &c);

	for (l = c.list_of_keys; l; l = l->next){
		int *key = l->data;
		
		g_hash_table_remove (cell->row->data, key);
		g_free (key);
	}
	g_list_free (c.list_of_keys);
}

/*
 * row_cell_get_displayed_at
 * @ri: The ColRowInfo for the row we are looking up
 * @col: the column position
 *
 * Returns the Cell* which happens to display at the column
 */
Cell *
row_cell_get_displayed_at (ColRowInfo *ri, int col)
{
	Cell *cell;
	
	g_return_val_if_fail (ri != NULL, NULL);
	
	/* Ok, cell was not found, check the registered span regions */
	cell = g_hash_table_lookup (ri->data, &col);
	return cell;
}
