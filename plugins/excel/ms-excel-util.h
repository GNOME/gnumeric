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
#include <print-info.h>

#define XL_CHECK_CONDITION_FULL(cond,code)					\
  do {									\
	if (!(cond)) {							\
		g_warning ("File is most likely corrupted.\n"		\
			   "(Condition \"%s\" failed in %s.)\n",	\
			   #cond,					\
			   G_STRFUNC);					\
		code							\
	}								\
  } while (0)

/*
 * Check a condition relating to whether the file being read is ok.
 * (Not to be confused with checking a programming error.)
 *
 * If it fails, print a warning and return.
 */
#define XL_CHECK_CONDITION(cond) XL_CHECK_CONDITION_FULL(cond,return;)
#define XL_CHECK_CONDITION_VAL(cond,val) XL_CHECK_CONDITION_FULL(cond,return val;)

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
	double	colinfo_step;
} XL_font_width;

/* Measures base character width for column sizing. Returns width. */
/* A new version based on hard coded tables to match XL */
XL_font_width const *xl_lookup_font_specs   (char const *name);
void		     destroy_xl_font_widths (void);


/*****************************************************************************/

const char *xls_paper_name (unsigned idx, gboolean *rotated);
unsigned xls_paper_size (GtkPaperSize *ps, gboolean rotated);

/*****************************************************************************/

char *xls_header_footer_export (const GnmPrintHF *hf);
void xls_header_footer_import (GnmPrintHF **hf, const char *txt);

/*****************************************************************************/

typedef enum {
	XL_ARROW_NONE = 0,
	XL_ARROW_REGULAR = 1,
	XL_ARROW_STEALTH = 2,
	XL_ARROW_DIAMOND = 3,
	XL_ARROW_OVAL = 4,
	XL_ARROW_OPEN = 5
} XLArrowType;

void xls_arrow_to_xl (GOArrow const *arrow, double width,
		      XLArrowType *ptyp, int *pl, int *pw);
void xls_arrow_from_xl (GOArrow *arrow, double width,
			XLArrowType typ, int l, int w);

/*****************************************************************************/

GHashTable *xls_collect_hlinks (GnmStyleList *sl, int max_col, int max_row);

typedef struct {
	GnmValidation const *v;
	GnmInputMsg *msg;
	GSList	    *ranges;
} XLValInputPair;
GHashTable *xls_collect_validations  (GnmStyleList *ptr,
				      int max_col, int max_row);

/*****************************************************************************/

#endif /* GNM_MS_EXCEL_UTIL_H */
