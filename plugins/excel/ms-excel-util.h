/**
 * ms-excel-util.h: Utility functions for MS Excel import / export
 *
 * Author:
 *    Jon K Hellan (hellan@acm.org)
 *
 * (C) 1999, 2000 Jon K Hellan
 **/
#ifndef GNUMERIC_MS_EXCEL_UTIL_H
#define GNUMERIC_MS_EXCEL_UTIL_H

#include <glib.h>
#include <stdlib.h>
#include "sheet.h"

typedef struct _TwoWayTable   TwoWayTable;

struct _TwoWayTable {
	GHashTable *all_keys;
	GHashTable *unique_keys;
	GPtrArray  *idx_to_key;
	gint       base;	/* Indices assigned consecutively from base */
};

typedef void (*AfterPutFunc) (gconstpointer key,
			      gboolean      was_added,
			      gint          index,
			      gconstpointer closure);

TwoWayTable *
two_way_table_new (GHashFunc      hash_func,
		   GCompareFunc   key_compare_func,
		   gint           base,
		   GDestroyNotify key_destroy_func);

void
two_way_table_free (TwoWayTable *table);

gint
two_way_table_put (const TwoWayTable *table, gpointer key,
		   gboolean unique,  AfterPutFunc apf, gconstpointer closure);

void
two_way_table_move (const TwoWayTable *table, gint dst_idx, gint src_idx);

gint
two_way_table_key_to_idx (const TwoWayTable *table, gconstpointer key);

gpointer
two_way_table_idx_to_key (const TwoWayTable *table, gint idx);

/* Measures base character width for column sizing. Returns width. */
/* A new version based on hard coded tables to match XL */
double lookup_font_base_char_width (char const *name, double size_pts,
				    gboolean const is_default);
void destroy_xl_font_widths (void);

#endif
