/**
 * ms-excel-util.c: Utility functions for MS Excel export
 *
 * Author:
 *    Jon K Hellan (hellan@acm.org)
 *
 * (C) 1999 Jon K Hellan
 **/

#include "config.h"
#include <glib.h>

#include "ms-excel-util.h"

/*
 * TwoWayTable
 *
 * This is a data structure for a one to one mapping between a key and an 
 * index. You can access the key by index and the index by key. Indices are 
 * assigned to keys in the order they are entered. 
 *
 * Example of use:
 * Build a table of unique fonts in the workbook, assigning each an index.
 * Then write them out ordered by index.
 *
 * Methods:
 * two_way_table_new:        Make a TwoWayTable.
 * two_way_table_free:       Destroy the TwoWayTable
 * two_way_table_put:        Put a key to the TwoWayTable
 * two_way_table_replace:    Replace the key for an index in the TwoWayTable
 * two_way_table_key_to_idx: Find index given a key
 * two_way_table_idx_to_key: Find key given the index
 * 
 * Implementation: 
 * A g_hash_table and a g_ptr_array. The value stored in the hash
 * table is index + 1. This is because hash lookup returns NULL on
 * failure. If 0 could be stored in the hash, we could not distinguish
 * this value from failure.
 */

/**
 * two_way_table_new
 * @hash_func        Hash function
 * @key_compare_func Comparison function
 * @base             Index valuse start from here.
 *
 * Makes a TwoWayTable. Returns the table.
 */
TwoWayTable *
two_way_table_new (GHashFunc    hash_func,
		   GCompareFunc key_compare_func, 
		   gint         base)
{
	TwoWayTable *table = g_new (TwoWayTable, 1);

	g_return_val_if_fail (base >= 0, NULL);
	table->key_to_idx  = g_hash_table_new (hash_func, key_compare_func);
	table->idx_to_key  = g_ptr_array_new ();
	table->base        = base;

	return table;
}

/**
 * two_way_table_free
 * @table Table
 * 
 * Destroys the TwoWayTable.
 */
void
two_way_table_free (TwoWayTable *table)
{
	g_hash_table_destroy (table->key_to_idx);
	g_ptr_array_free (table->idx_to_key, TRUE);
	g_free (table);
}

/**
 * two_way_table_put
 * @table  Table
 * @key    Key to enter
 * @unique True if key is entered also if already in table
 * @apf    Function to call after putting.
 *
 * Puts a key to the TwoWayTable if it is not already there. Returns
 * the index of the key. apf is of type AfterPutFunc, and can be used
 * for logging, freeing resources, etc. It is told if the key was
 * entered or not.  
 */
gint
two_way_table_put (const TwoWayTable *table, gpointer key, 
		   gboolean unique,  AfterPutFunc apf, gpointer closure)
{
	gint index = two_way_table_key_to_idx (table, key);
	gboolean found = (index >= 0);
	gboolean addit = !found || !unique;

	if (addit) {
		index = table->idx_to_key->len + table->base;

		if (!found)
			g_hash_table_insert (table->key_to_idx, key, 
					     GINT_TO_POINTER (index + 1));
		g_ptr_array_add (table->idx_to_key, key);
	}

	if (apf)
		apf (key, addit, index, closure);

	return index;
}

/**
 * two_way_table_replace
 * @table Table
 * @idx   Index to be updated
 * @key   New key to be assigned to the index
 * 
 * Replaces the key bound to an index with a new value. Returns the old key.
 */
gpointer
two_way_table_replace (const TwoWayTable *table, gint idx, gpointer key)
{
	gpointer old_key = two_way_table_idx_to_key (table, idx);

	g_hash_table_remove(table->key_to_idx, old_key);
	g_hash_table_insert(table->key_to_idx, key, GINT_TO_POINTER (idx + 1));
	g_ptr_array_index (table->idx_to_key, idx) = key;
	
	return old_key;
}

/**
 * two_way_table_key_to_idx
 * @table Table
 * @key   Key
 * 
 * Returns index of key, or -1 if key not found.
 */
gint
two_way_table_key_to_idx (const TwoWayTable *table, gconstpointer key)
{
	return GPOINTER_TO_INT (g_hash_table_lookup (table->key_to_idx, key))
		- 1;
}

/**
 * two_way_table_idx_to_key
 * @table Table
 * @idx   Index
 * 
 * Returns key bound to index, or NULL if index is out of range.
 */
gpointer
two_way_table_idx_to_key (const TwoWayTable *table, gint idx)
{
	
	g_return_val_if_fail (idx - table->base >= 0, NULL);
	g_return_val_if_fail (idx - table->base < table->idx_to_key->len, 
			      NULL);

	return g_ptr_array_index (table->idx_to_key, idx - table->base);
}
