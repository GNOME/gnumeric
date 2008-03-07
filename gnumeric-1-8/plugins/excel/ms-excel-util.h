/* vim: set sw=8 ts=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/**
 * ms-excel-util.h: Utility functions for MS Excel import / export
 *
 * Author:
 *    Jon K Hellan (hellan@acm.org)
 *
 * (C) 1999-2005 Jon K Hellan
 **/
#ifndef GNM_MS_EXCEL_UTIL_H
#define GNM_MS_EXCEL_UTIL_H

#include <glib.h>
#include <stdlib.h>

/*
 * Check a condition relating to whether the file being read is ok.
 * (Not to be confused with checking a programming error.)
 *
 * If it fails, print a warning and return.
 */
#define XL_CHECK_CONDITION(cond)					\
  do {									\
	if (!(cond)) {							\
		g_warning ("File is most likely corrupted.\n"		\
			   "(Condition \"%s\" failed in %s.)\n",	\
			   #cond,					\
			   G_STRFUNC);					\
		return;							\
	}								\
  } while (0)

#define XL_CHECK_CONDITION_VAL(cond,val)				\
  do {									\
	if (!(cond)) {							\
		g_warning ("File is most likely corrupted.\n"		\
			   "(Condition \"%s\" failed in %s.)\n",	\
			   #cond,					\
			   G_STRFUNC);					\
		return (val);						\
	}								\
  } while (0)


typedef struct _TwoWayTable   TwoWayTable;

struct _TwoWayTable {
	GHashTable *all_keys;
	GHashTable *unique_keys;
	GPtrArray  *idx_to_key;
	gint       base;	/* Indices assigned consecutively from base */
	GDestroyNotify key_destroy_func;
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
two_way_table_put (TwoWayTable const *table, gpointer key,
		   gboolean unique,  AfterPutFunc apf, gconstpointer closure);

void
two_way_table_move (TwoWayTable const *table, gint dst_idx, gint src_idx);

gint
two_way_table_key_to_idx (TwoWayTable const *table, gconstpointer key);

gpointer
two_way_table_idx_to_key (TwoWayTable const *table, gint idx);

/*****************************************************************************/

typedef struct {
	char const *name;
	int	defcol_unit;
	int	colinfo_baseline;
	float	colinfo_step;
} XL_font_width;

/* Measures base character width for column sizing. Returns width. */
/* A new version based on hard coded tables to match XL */
XL_font_width const *xl_lookup_font_specs   (char const *name);
void		     destroy_xl_font_widths (void);

#endif /* GNM_MS_EXCEL_UTIL_H */
