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
two_way_table_new (GHashFunc    hash_func,
		   GCompareFunc key_compare_func,
		   gint   base);

void
two_way_table_free (TwoWayTable *table);

gint
two_way_table_put (const TwoWayTable *table, gpointer key,
		   gboolean unique,  AfterPutFunc apf, gconstpointer closure);

gpointer
two_way_table_replace (const TwoWayTable *table, gint idx, gpointer key);

gint
two_way_table_key_to_idx (const TwoWayTable *table, gconstpointer key);

gpointer
two_way_table_idx_to_key (const TwoWayTable *table, gint idx);

/* Measures base character width for column sizing. Returns width. */
/* A new version based on hard coded tables to match XL */
double
lookup_font_base_char_width_new (char const * const name, double size_pts,
				 gboolean const is_default);


/* a group of iconv_* - like functions, with safe fallbacks if iconv is
 * unavailable. Sorry for stupid prefix - Vlad Harchev <hvv@hippo.ru>
 */
typedef void* excel_iconv_t; /*can't be NULL or (-1) */

/*
 * this returns code of the codepage that should be used when exporting
 * .xls files (it's guessed by looking at language name). Fallback is 1252.
 */
guint excel_iconv_win_codepage (void);

/*these two will figure out which charset names to use*/
excel_iconv_t excel_iconv_open_for_import (guint codepage);
excel_iconv_t excel_iconv_open_for_export (void);
void excel_iconv_close (excel_iconv_t handle);

/* if fails (or if compiled without support for iconv), it will  copy the input
 * string to output and pretend that all worked fine.  If some char is
 * non-convertable, it will replace that char with "?".  It's required that
 * inbytesleft <= outbytesleft (so that fallback will be able to work). As for
 * now, return value is not meaningfull at all - 0 is always returned.
*/
size_t excel_iconv (excel_iconv_t handle, char const **inbuf, size_t *inbytesleft,
		    char **outbuf, size_t *outbytesleft);

/* Same as wcstombs(3), but tries to convert as much characters as possible,
 * replacing the ones it can't convert with '?' or something resembling
 * original character (e.g. "(C)" for copyright sign).
 */
size_t excel_wcstombs(char* outbuf,wchar_t* wc,size_t length);

void destroy_xl_font_widths (void);

#endif
