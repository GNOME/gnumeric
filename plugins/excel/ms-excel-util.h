/**
 * ms-excel-util.h: Utility functions for MS Excel export
 *
 * Author:
 *    Jon K Hellan (hellan@acm.org)
 *
 * (C) 1999 Jon K Hellan
 **/
#ifndef GNUMERIC_MS_EXCEL_UTIL_H
#define GNUMERIC_MS_EXCEL_UTIL_H

#include <glib.h>

typedef struct _TwoWayTable   TwoWayTable;

struct _TwoWayTable {
	GHashTable *key_to_idx;
	GPtrArray  *idx_to_key;
	gint       base;	/* Indices assigned consecutively from base */
};

typedef void (*AfterPutFunc) (gconstpointer key, 
			      gboolean      was_added, 
			      gint          index, 
			      gpointer      closure);

TwoWayTable *
two_way_table_new (GHashFunc    hash_func,
		   GCompareFunc key_compare_func, 
		   gint   base);

void
two_way_table_free (TwoWayTable *table);

gint
two_way_table_put (const TwoWayTable *table, gpointer key, 
		   gboolean unique,  AfterPutFunc apf, gpointer closure);

gpointer
two_way_table_replace (const TwoWayTable *table, gint idx, gpointer key);

gint
two_way_table_key_to_idx (const TwoWayTable *table, gconstpointer key);

gpointer
two_way_table_idx_to_key (const TwoWayTable *table, gint idx);

#endif

