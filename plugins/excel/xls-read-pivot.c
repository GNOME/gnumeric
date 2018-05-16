/**
 * xlsx-read-pivot.c: MS Excel XLS import for pivot tables (tm)
 *
 * (C) 2005-2008 Jody Goldberg
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301
 * USA
 **/

#include <gnumeric-config.h>
#include <gnumeric.h>
#include "ms-biff.h"
#include "biff-types.h"
#include "boot.h"
#include "ms-excel-read.h"
#include "ms-excel-util.h"

#include <workbook.h>
#include <sheet.h>
#include <ranges.h>
#include <gnm-data-cache-source.h>
#include <gnm-sheet-slicer.h>

#include <go-data-cache.h>
#include <go-data-cache-field.h>
#include <go-data-slicer-field.h>
#include <go-data-slicer.h>

#include <goffice/goffice.h>

#include <gsf/gsf-infile.h>
#include <gsf/gsf-input.h>
#include <gsf/gsf-utils.h>

#include <math.h>
#include <string.h>

#undef NO_DEBUG_EXCEL
#ifndef NO_DEBUG_EXCEL
#define d(level, code)	do { if (ms_excel_pivot_debug > level) { code } } while (0)
#else
#define d(level, code)
#endif

typedef struct {
	GnmXLImporter *imp;

	GODataCache *cache;
	GArray *indexed, *inlined;
} XLSReadPivot;

static gboolean
check_next (BiffQuery *q, unsigned int len)
{
	ms_biff_query_next (q);
	if (q->length == len)
		return TRUE;
	if (len < 10)
		g_warning ("%x : expected len %d not %d", q->opcode, len, q->length);
	else
		g_warning ("%x : expected len %d (0x%x) not %d (0x%x)", q->opcode, len, len, q->length, q->length);
	return FALSE;
}
static gboolean
check_next_min (BiffQuery *q, unsigned int len)
{
	ms_biff_query_next (q);
	if (q->length >= len)
		return TRUE;
	if (len < 10)
		g_warning ("%x : expected >= len %d not %d", q->opcode, len, q->length);
	else
		g_warning ("%x : expected >= len %d (0x%x) not %d (0x%x)", q->opcode, len, len, q->length, q->length);
	return FALSE;
}

/**************************************************************************
 * Pivot Cache
 */
static GnmValue *
xls_read_pivot_cache_value (XLSReadPivot *s, BiffQuery *q)
{
	guint16 opcode;

	if (ms_biff_query_peek_next (q, &opcode)) {
		switch (opcode) {
		case BIFF_SXNUM: if (check_next (q, 8)) {
			double v = GSF_LE_GET_DOUBLE  (q->data + 0);
			d (1, g_printerr ("%g (num);\n", v););
			return go_val_new_float (v);
		}
		break;

		case BIFF_SXBOOL: if (check_next (q, 2)) {
			gboolean v = 0 != GSF_LE_GET_GINT16  (q->data + 0);
			d (1, g_printerr ("%s (bool);\n", v ? "true" : "false"););
			return go_val_new_bool (v);
		}
		break;

		case BIFF_SXERR: if (check_next (q, 2)) {
			gint16 v = GSF_LE_GET_GINT16  (q->data + 0);
			d (1, g_printerr ("%hx (err);\n", v););
			return xls_value_new_err (NULL, v);
		}
		break;

		case BIFF_SXINT: if (check_next (q, 2)) {
			gint16 v = GSF_LE_GET_GINT16  (q->data + 0);
			d (1, g_printerr ("%hx (short);\n", v););
			return go_val_new_float (v);
		}
		break;

		case BIFF_SXSTRING: if (check_next_min (q, 2)) {
			char *v = excel_biff_text_2 (s->imp, q, 0);
			d (1, g_printerr ("'%s' (string);\n", v););
			return go_val_new_str_nocopy (v);
		}
		break;

		case BIFF_SXDTR: if (check_next (q, 8)) { /* date time */
			guint16 y  = GSF_LE_GET_GUINT16 (q->data + 0);
			guint16 m  = GSF_LE_GET_GUINT16 (q->data + 2);
			guint8  d  = GSF_LE_GET_GUINT8  (q->data + 4);
			guint8  h  = GSF_LE_GET_GUINT8  (q->data + 5);
			guint8  mi = GSF_LE_GET_GUINT8  (q->data + 6);
			guint8  se = GSF_LE_GET_GUINT8  (q->data + 7);
			GDate date;

			d (1, g_printerr ("%hu-%hu-%hhuT%hhu:%hhu:%hhu (data);\n",
				       y, m, d, h, mi, se););

			g_date_set_dmy (&date, d, m, y);
			if (g_date_valid (&date)) {
				unsigned d_serial = go_date_g_to_serial (&date,
					workbook_date_conv (s->imp->wb));
				double time_frac = h + ((double)mi / 60.) + ((double)se / 3600.);
				GnmValue *res = value_new_float (d_serial + time_frac / 24.);
				value_set_fmt (res, go_format_default_date_time ());
				return res;
			}
			g_warning ("Invalid date in pivot cache.");
		}
		break;

		case BIFF_SXNIL: if (check_next (q, 0)) {
			d (1, g_printerr ("(empty);\n"););
			return go_val_new_empty ();
		}

		default :
			d (0, g_printerr ("UNEXPECTED RECORD %hx;\n", opcode););
			break;
		}
	}

	d (0, g_printerr ("missing value;\n"););
	return NULL;
}

static GOValArray *
xls_read_pivot_cache_values (XLSReadPivot *s, BiffQuery *q, unsigned int n, const char *type)
{
	/* TODO : go_val_array_sized_new */
	GPtrArray *res = g_ptr_array_sized_new (n);
	unsigned int i;

	d (1, g_printerr ("/* %u %s items */ ;\n", n, type););
	for (i = 0 ; i < n ; i++) {
		GnmValue *v = xls_read_pivot_cache_value (s, q);
		if (!v) {
			/* TODO : go_val_array_set_size */
			g_ptr_array_set_size (res, n);
			return res;
		}
		/* TODO : go_val_array_add */
		g_ptr_array_add (res, v);
	}
	return res;
}

static void
xls_read_pivot_cache_group (XLSReadPivot *s, BiffQuery *q, GODataCacheField *field)
{
	guint16 opcode;

	if (ms_biff_query_peek_next (q, &opcode) &&
	    opcode == BIFF_SXNUMGROUP &&
	    check_next (q, 2)) {
		GOValBucketer	bucketer;
		GOValArray	*bucket_bounds;
		GError		*valid;
		guint16 flags = GSF_LE_GET_GUINT16  (q->data + 0);
#if 0
		gboolean const autoMinima = 0 != (flags & 1);
		gboolean const autoMaxima = 0 != (flags & 2);
#endif
		unsigned const type = (flags >> 2) & 0xf;

		go_val_bucketer_init (&bucketer);
		bucketer.details.series.step	= 1.;
		d (0, g_printerr ("group with 0x%hx flag type = %d;\n", flags, type););
		switch (type) {
		case 1 : bucketer.type = GO_VAL_BUCKET_SECOND; break;
		case 2 : bucketer.type = GO_VAL_BUCKET_MINUTE; break;
		case 3 : bucketer.type = GO_VAL_BUCKET_HOUR; break;
		case 4 : bucketer.type = GO_VAL_BUCKET_DAY_OF_YEAR; break;
		case 5 : bucketer.type = GO_VAL_BUCKET_MONTH; break;
		case 6 : bucketer.type = GO_VAL_BUCKET_CALENDAR_QUARTER; break;
		case 7 : bucketer.type = GO_VAL_BUCKET_YEAR; break;
		default:
			 /* default to linear */;
		case 8 : bucketer.type = GO_VAL_BUCKET_SERIES_LINEAR; break;
		}

		bucket_bounds = xls_read_pivot_cache_values (s, q, 3, "group bounds");
		if (bucketer.type == GO_VAL_BUCKET_SERIES_LINEAR) {
			bucketer.details.series.minimum = go_val_as_float (go_val_array_index (bucket_bounds, 0));
			bucketer.details.series.maximum = go_val_as_float (go_val_array_index (bucket_bounds, 1));
			bucketer.details.series.step	= go_val_as_float (go_val_array_index (bucket_bounds, 2));
		} else {
			bucketer.details.dates.minimum = go_val_as_float (go_val_array_index (bucket_bounds, 0));
			bucketer.details.dates.maximum = go_val_as_float (go_val_array_index (bucket_bounds, 1));
		}

		/* TODO : min/max/step : TODO */
		if (NULL == (valid = go_val_bucketer_validate (&bucketer)))
			g_object_set (G_OBJECT (field), "bucketer", &bucketer, NULL);
		else {
			g_warning ("Skipping invalid pivot field group for field '%s' because : %s",
				      go_data_cache_field_get_name (field)->str,
				      valid->message);
			g_error_free (valid);
		}
		go_val_array_free (bucket_bounds);
	}
}

static void
xls_read_pivot_cache_field (XLSReadPivot *s, BiffQuery *q, unsigned int field_num)
{
	GODataCacheField *field;
	guint16 opcode, flags = GSF_LE_GET_GUINT16  (q->data + 0);
	unsigned const index_type = flags & 3; /* XL : allAtoms.someUnhashed = 1 or 2 */
#if 0
4 - must be 0
	gboolean const has_child = flags & 8;
8 - fHasChild (1 bit): A bit that specifies whether ifdbParent specifies a reference to a parent grouping cache field. For more information, see Grouping. If the fCalculatedField field is equal to 1, then this field MUST be equal to 0.

10 - fRangeGroup (1 bit): A bit that specifies whether this cache field is grouped using numeric
	grouping or date grouping, as specified by Grouping. If this field is equal to 1, then this record
	MUST be followed by a sequence of SXString records, as specified by the GRPSXOPER rule. The
	quantity of SXString records is specified by csxoper. Also, if this field is equal to 1, then this
	record MUST be followed by a sequence of records that conforms to the SXRANGE rule that
	specifies the grouping properties for the ranges of values.

20 - fNumField (1 bit): A bit that specifies whether the cache items in this
       cache field contain at least one numeric cache item, as specified by
       SXNum. If fDateInField is equal to 1, this field MUST be
       equal to 0.

40 - fTextEtcField (1 bit): A bit that specifies whether the cache items contain text data. If
	fNumField is 1, this field MUST be ignored.

80 - fnumMinMaxValid (1 bit): A bit that specifies whether a valid minimum or maximum value can
	be computed for the cache field. MUST be equal to 1 if fDateInField or fNumField is equal to 1.

100 - fShortIitms (1 bit): A bit that specifies whether the there are more than
	255 cache items in this cache field. If catm is greater than 255, this
	value MUST be equal to 1; otherwise it MUST be 0.

200 - fNonDates (1 bit): A bit that specifies whether the cache items in this cache field contain values that are not time or date values. If this cache field is a grouping cache field, as specified by Grouping, then this field MUST be ignored. Otherwise, if fDateInField is equal to 1, then this field MUST be 0.

400 - fDateInField (1 bit): A bit that specifies whether the cache items in this cache field contain at least one time or date cache item, as specified by SXDtr. If fNonDates is equal to 1, then this field MUST be equal to 0.

800 - fServerBased (1 bit): A bit that specifies whether this cache field is a server-based page field
when the corresponding pivot field is on the page axis of the PivotTable view , as specified in
source data.
This value applies to an ODBC PivotCache only. MUST NOT be equal to 1 if
fCantGetUniqueItems is equal to 1. If fCantGetUniqueItems is equal to 1, then the ODBC
connection cannot provide a list of unique items for the cache field.
MUST be 0 for a cache field in a non-ODBC PivotCache.

1000 - fCantGetUniqueItems (1 bit): A bit that specifies whether a list of unique values for the cache
field was not available while refreshing the source data. This field applies only to a PivotCache that
uses ODBC source data and is intended to be used in conjunction with optimization features. For
example, the application can optimize memory usage when populating PivotCache records if it has
a list of unique values for a cache field before all the records are retrieved from the ODBC
connection. Or, the application can determine the appropriate setting of fServerBased based on
this value.
MUST be 0 for fields in a non-ODBC PivotCache.

2000 - fCalculatedField (1 bit): A bit that specifies whether this field is a calculated field. The formula
of the calculated field is stored in a directly following SXFormula record. If fHasParent is equal to
1, this field MUST be equal to 0.
#endif

	int group_child		= GSF_LE_GET_GUINT16  (q->data + 2);	/* what use is this ? */
	int group_parent	= GSF_LE_GET_GUINT16  (q->data + 4);
	guint16 count		= GSF_LE_GET_GUINT16  (q->data + 6);
	guint16 grouped_items	= GSF_LE_GET_GUINT16  (q->data + 8);
	guint16 base_items	= GSF_LE_GET_GUINT16  (q->data + 10);
	guint16 std_items	= GSF_LE_GET_GUINT16  (q->data + 12);
	GOString *name		= go_string_new_nocopy (excel_biff_text_2 (s->imp, q, 14));

	d(0, g_printerr ("FIELD [%d] '%s' type %d, has %d %d %d %d items, and flags = 0x%hx, parent = %d, child = %d;\n",
		field_num, name ? name->str : "<UNDEFINED>", index_type, count, grouped_items, base_items, std_items, flags,
		group_parent, group_child););

	field = g_object_new (GO_DATA_CACHE_FIELD_TYPE, "name", name, NULL);
	go_string_unref (name);

	/* KLUDGE TODO TODO TODO */
	if (flags == 0x11)
		g_object_set (field, "group-parent", group_parent, NULL);
	d (4, gsf_mem_dump (q->data + 2, MIN (q->length, 0xc)););

	/* SQL type of field (ignored) */
	if (ms_biff_query_peek_next (q, &opcode) && opcode == BIFF_SXFDBTYPE && check_next (q, 2))
		/* Nothing */ ;

	switch (index_type) {
	case 1 :
		if (grouped_items > 0) {
			go_data_cache_field_set_vals (field, TRUE,
				xls_read_pivot_cache_values (s, q, grouped_items, "grouped"));
			xls_read_pivot_cache_group (s, q, field);
		}

		if (std_items > 0) {
			go_data_cache_field_set_vals (field, FALSE,
				xls_read_pivot_cache_values (s, q, std_items, "shared"));
			g_array_append_val (s->indexed, field_num);
		}
		break;

	case 2 : /* items follow the last normal field and are preceded by an index */
		g_array_append_val (s->inlined, field_num);
		break;

	default :
		g_warning ("unknown  index type %d for field '%s' (#%u)",
			   index_type, name ? name->str : "<UNDEFINED>", field_num);
	}

	go_data_cache_add_field (s->cache, field);
}

static gboolean
xls_read_pivot_cache (XLSReadPivot *s, BiffQuery *q)
{
	unsigned int i, record_count;
	unsigned int num_records, num_fields;
	guint16 stream_id, opcode;
	char *refreshedBy;

	s->cache = g_object_new (GO_DATA_CACHE_TYPE, NULL);

	/* SXDB */
	if (!ms_biff_query_peek_next (q, &opcode) || opcode != BIFF_SXDB || !check_next_min (q, 20))
		return FALSE;

	num_records = GSF_LE_GET_GUINT32 (q->data + 0);
	stream_id   = GSF_LE_GET_GUINT16 (q->data + 4);
	num_fields  = GSF_LE_GET_GUINT16 (q->data + 12); /* base + grouped + calced */
	refreshedBy = excel_biff_text_2 (s->imp, q, 18);
	g_object_set (s->cache, "refreshed-by", refreshedBy, NULL);

	s->indexed = g_array_sized_new (FALSE, FALSE, sizeof (unsigned int), num_fields);
	s->inlined = g_array_sized_new (FALSE, FALSE, sizeof (unsigned int), num_fields);
	d (1, {
		guint16 flags		= GSF_LE_GET_GUINT16 (q->data + 6);
		guint16 rec_per_block	= GSF_LE_GET_GUINT16 (q->data + 8);	/* seems constant */
		guint16 base_fields	= GSF_LE_GET_GUINT16 (q->data + 10);	/* base */
		/* guint16 zero */
		guint16 type		= GSF_LE_GET_GUINT16 (q->data + 16);
		g_printerr ("num_rec = %u;\nstream_id = %u;\n"
			 "rec per block = %u;\nbase fields = %hu;\ntotal fields = %u;\n"
			 "last modified by = '%s';type = 0x%x, flags = 0x%x;\n",
			 num_records, stream_id, rec_per_block, base_fields,
			 num_fields, refreshedBy, type, flags);
	});
	g_free (refreshedBy);

	/* SXDBEX (optional SXDB extension) */
	if (ms_biff_query_peek_next (q, &opcode) && opcode == BIFF_SXDBEX && check_next (q, 12)) {
		GOVal *refreshedDate = value_new_float (GSF_LE_GET_DOUBLE (q->data + 0));
		value_set_fmt (refreshedDate, go_format_default_date_time ());
		g_object_set (s->cache, "refreshed-on", refreshedDate, NULL);
		d (0, {
			guint32 num_fmla	= GSF_LE_GET_GUINT32 (q->data + 8);
			g_printerr ("num_fmla %u : last refresh %s\n",
				 num_fmla, value_peek_string (refreshedDate));
		});
		go_val_free (refreshedDate);
	}

	for (i = 0 ; i < num_fields ; i++)
		if (ms_biff_query_peek_next (q, &opcode) && opcode == BIFF_SXFDB && check_next_min (q, 12))
			xls_read_pivot_cache_field (s, q, i);
		else {
			g_printerr ("expected FDB not %hx\n", opcode);
			return FALSE;
		}

	go_data_cache_import_start (s->cache, MIN (num_records, 10000u));
	record_count = 0;
	while (ms_biff_query_peek_next (q, &opcode) && opcode != BIFF_EOF) {
		switch (opcode) {
		case BIFF_SXDDB: if (check_next (q, s->indexed->len)) {
			for (i = 0 ; i < s->indexed->len; i++) {
				go_data_cache_set_index (s->cache,
					g_array_index (s->indexed, unsigned int, i), record_count,
					GSF_LE_GET_GINT8 (q->data + i));
				d (1, g_printerr ("%hhu ", GSF_LE_GET_GINT8 (q->data + i)););
			}
			d (1, g_printerr ("\n"); );

			if (s->inlined->len > 0) {
				GOValArray *vals = xls_read_pivot_cache_values (s, q, s->inlined->len, "inline");
				for (i = 0 ; i < s->inlined->len; i++)
					go_data_cache_set_val (s->cache,
						g_array_index (s->inlined, unsigned int, i), record_count,
						go_val_array_index_steal (vals, i));
				go_val_array_free (vals);
			}
			record_count++;
		}
		break;

		default:
			ms_biff_query_next (q);
			ms_biff_query_dump (q);
		}
	}
	go_data_cache_import_done (s->cache, record_count);
	record_count = 0;

	g_array_free (s->inlined, TRUE);
	g_array_free (s->indexed, TRUE);

	return TRUE;
}

static void
xls_read_pivot_cache_by_id (XLSReadPivot *s, GsfInfile *container, guint16 n)
{
	BiffQuery *q;
	GsfInput  *cache_stream, *dir;
	char name[5];

	if (NULL == container)
		return;				/* pre-Excel 95 without ole */
	dir = gsf_infile_child_by_name (container, "_SX_DB_CUR");	/* Excel 97 */
	if (NULL == dir)
		dir = gsf_infile_child_by_name (container, "_SX_DB");	/* Excel 95 */
	if (NULL == dir)
		return;

	snprintf (name, sizeof (name), "%04hX", n);
	cache_stream = gsf_infile_child_by_name (GSF_INFILE (dir), name);
	if (NULL != cache_stream) {
		q = ms_biff_query_new (cache_stream);
		d (0, g_printerr ("{ /* PIVOT CACHE [%s] */\n", name););
		if (!xls_read_pivot_cache (s, q)) {
			g_object_unref (s->cache);
			s->cache = NULL;
		} else
			d (2, go_data_cache_dump (s->cache, NULL, NULL););
		d (0, g_printerr ("}; /* PIVOT CACHE [%s] */\n", name););

		ms_biff_query_destroy (q);
		g_object_unref (cache_stream);
	}
	g_object_unref (dir);
}

/**************************************************************************
 * Pivot Source
 */
void
xls_read_SXStreamID (GnmXLImporter *imp, BiffQuery *q, GsfInfile *container)
{
	XLSReadPivot   s;
	guint16 opcode, cache_id;
	GODataCacheSource *cache_src = NULL;

	XL_CHECK_CONDITION (q->length >= 2);
	cache_id = GSF_LE_GET_GUINT16 (q->data);

	s.imp = imp;
	s.cache = NULL;
	xls_read_pivot_cache_by_id (&s, container, cache_id);
	g_ptr_array_add (imp->pivot.cache_by_index, s.cache);

	if (ms_biff_query_peek_next (q, &opcode) &&
	    opcode == BIFF_SXVS &&
	    check_next (q, 2)) {
		guint16 source_type = GSF_LE_GET_GUINT16 (q->data);
		ms_biff_query_peek_next (q, &opcode);
		switch (source_type) {
		case    1 :	/* Sheet */
			switch (opcode) {
			case BIFF_DCONREF: if (check_next_min (q, 8)) {
				GnmRange r;
				guint8 *source_name = excel_biff_text_2 (imp, q, 6);
				xls_read_range8 (&r, q->data);
				if (source_name) {
					if (source_name[0] == 2) {
						Sheet *sheet = workbook_sheet_by_name (imp->wb, source_name+1);
						if (NULL == sheet) {
							sheet = sheet_new (imp->wb, source_name+1, XLS_MaxCol,
								(imp->ver >= MS_BIFF_V8) ?
								XLS_MaxRow_V8 : XLS_MaxRow_V7);
							workbook_sheet_attach (imp->wb, sheet);
						}
						cache_src = gnm_data_cache_source_new (sheet, &r, NULL);
					} else
						; /* TODO : the rest of dcon-file */
				}

#if 0
				g_printerr ("Sheet : ref '%s' ! %s\n", source_name ? (char*)source_name : "<<unknown>",
					 range_as_string (&r));
#endif
				g_free (source_name);
			}
			break;
			case BIFF_DCONNAME: if (check_next_min (q, 2)) {
				char *name = excel_biff_text_2 (imp, q, 0);
				g_object_set_data_full (G_OBJECT (s.cache),
					"src-name", name, (GDestroyNotify) g_free);
				g_print ("Sheet : name '%s'\n", name);
			}
			break;
			case BIFF_DCONBIN:	g_print ("Sheet : binname\n");
			break;
			default:
				g_print ("missing sheet type : %hx\n", source_type);
			break;
			}
			break;

		case    2 :	/* external */
			break;

		case    4 :	/* consolidation */
			break;

		case 0x10 :	/* scenarios : no source data */
			break;

		default :
			g_print ("unknown source type : %hx\n", source_type);
			break;
		}
	}

	if (NULL != cache_src) {
		if (NULL != s.cache)
			go_data_cache_set_source (s.cache, cache_src);
		else
			g_object_unref (cache_src);
	}
}

/*********************************************************/

void
xls_read_SXIVD (BiffQuery *q, ExcelReadSheet *esheet)
{
	GnmXLImporter *imp = esheet->container.importer;
	GODataSlicerFieldType t = imp->pivot.ivd_index ? GDS_FIELD_TYPE_COL : GDS_FIELD_TYPE_ROW;
	unsigned int i;

	g_return_if_fail (imp->pivot.ivd_index < 2);

	imp->pivot.ivd_index++;

	d(3, ms_biff_query_dump (q););

	for (i = 0 ; 2*i < q->length ; i++) {
		guint16 const indx = GSF_LE_GET_GUINT16 (q->data + i*2);
		/* ignore special orientation index. */
		if (0xfffe != indx) {
			go_data_slicer_field_set_field_type_pos (
				go_data_slicer_get_field ((GODataSlicer *)imp->pivot.slicer, indx),
				t, i);
		}
	}
}

static void
xls_read_SXVI (BiffQuery *q, ExcelReadSheet *esheet, unsigned int i)
{
	guint16 const type	  = GSF_LE_GET_GUINT16 (q->data + 0);
	guint16 const flags	  = GSF_LE_GET_GUINT16 (q->data + 2);
	guint16 const cache_index = GSF_LE_GET_GUINT16 (q->data + 4);
	// guint16 const name_len	  = GSF_LE_GET_GUINT16 (q->data + 6);
	GnmXLImporter *imp = esheet->container.importer;
	GODataCacheField *dcf = go_data_slicer_field_get_cache_field (imp->pivot.slicer_field);
	char const *type_str = "unknown";

	XL_CHECK_CONDITION (NULL != dcf);

	d(0, { switch (type) {
	case 0xFE: type_str = "Page"; break;
	case 0xFF: type_str = "Null"; break;
	case 0x00: type_str = "Data"; break;
	case 0x01: type_str = "Default"; break;
	case 0x02: type_str = "SUM"; break;
	case 0x03: type_str = "COUNTA"; break;
	case 0x04: type_str = "COUNT"; break;
	case 0x05: type_str = "AVERAGE"; break;
	case 0x06: type_str = "MAX"; break;
	case 0x07: type_str = "MIN"; break;
	case 0x08: type_str = "PRODUCT"; break;
	case 0x09: type_str = "STDEV"; break;
	case 0x0A: type_str = "STDEVP"; break;
	case 0x0B: type_str = "VAR"; break;
	case 0x0C: type_str = "VARP"; break;
	case 0x0D: type_str = "Grand total"; break;
	default :  type_str = "UNKNOWN"; break;
	}
	g_print ("[%u] %s %s %s %s %s = %hu\n", i, type_str,
		 (flags & 1) ? "hidden " : "",
		 (flags & 2) ? "detailHid " : "",
		 (flags & 4) ? "calc " : "",
		 (flags & 8) ? "missing " : "", cache_index);
	});

	if (type == 0x00 && (flags & 1))
	{
		XL_CHECK_CONDITION (cache_index != 0xffff);
		d(0, {
			g_printerr ("hide : ");
			go_data_cache_dump_value (go_data_cache_field_get_val (dcf, cache_index));
			g_printerr ("\n");
			});
	}
}

void
xls_read_SXVD (BiffQuery *q, ExcelReadSheet *esheet)
{
	static GODataSlicerFieldType const axis_bits[] = {
		GDS_FIELD_TYPE_ROW,	GDS_FIELD_TYPE_COL,
		GDS_FIELD_TYPE_PAGE,	GDS_FIELD_TYPE_DATA
	};
	static GOAggregateBy const aggregation_bits[] = {
		GO_AGGREGATE_AUTO,	 GO_AGGREGATE_BY_SUM,	GO_AGGREGATE_BY_COUNTA,
		GO_AGGREGATE_BY_AVERAGE, GO_AGGREGATE_BY_MAX,	GO_AGGREGATE_BY_MIN,
		GO_AGGREGATE_BY_PRODUCT, GO_AGGREGATE_BY_COUNT, GO_AGGREGATE_BY_STDDEV,
		GO_AGGREGATE_BY_STDDEVP, GO_AGGREGATE_BY_VAR,	GO_AGGREGATE_BY_VARP
	};
	GnmXLImporter *imp = esheet->container.importer;
	unsigned int i, len_name, axis, aggregations, num_items, aggregate_by = 0;
	guint16 opcode;

	XL_CHECK_CONDITION (q->length >= 10);

	axis		= GSF_LE_GET_GUINT16 (q->data + 0);
	/* num_subtotal	= GSF_LE_GET_GUINT16 (q->data + 2); */
	aggregations	= GSF_LE_GET_GUINT16 (q->data + 4);
	num_items	= GSF_LE_GET_GUINT16 (q->data + 6);
	len_name	= GSF_LE_GET_GUINT16 (q->data + 8);

	imp->pivot.slicer_field = g_object_new (GO_DATA_SLICER_FIELD_TYPE,
		"data-cache-field-index", imp->pivot.field_count++,
		NULL);
	go_data_slicer_add_field (GO_DATA_SLICER (imp->pivot.slicer),
		imp->pivot.slicer_field);

	for (i = 0 ; i < G_N_ELEMENTS (axis_bits) ; i++)
		if ((axis & (1 << i)))
			go_data_slicer_field_set_field_type_pos (imp->pivot.slicer_field,
				axis_bits[i], G_MAXINT);

	for (i = 0 ; i < G_N_ELEMENTS (aggregation_bits) ; i++)
		if ((aggregations & (1 << i)))
			aggregate_by |= (1 << aggregation_bits[i]);
	g_object_set (G_OBJECT (imp->pivot.slicer_field),
		"aggregations", aggregate_by, NULL);

	for (i = 0 ; i < num_items ; i++)
		if (ms_biff_query_peek_next (q, &opcode) && BIFF_SXVI == opcode &&
		    check_next_min (q, 8)) {
			xls_read_SXVI (q, esheet, i);
		}
	if (ms_biff_query_peek_next (q, &opcode) && BIFF_SXVDEX == opcode &&
	    check_next_min (q, 12)) {
		/* Ignore */
	}
}

void
xls_read_SXVIEW (BiffQuery *q, ExcelReadSheet *esheet)
{
	GnmXLImporter *imp = esheet->container.importer;
	unsigned int first_header_row, cache_idx, name_len, data_field_name_len,
		     data_field_axis, data_field_pos, num_fields,
		     num_row_fields, num_column_fields, num_page_fields,
		     num_data_fields, num_data_rows, data_columns, flags, autoformat;
	guint32 len;
	GOString *name = NULL, *data_field_name = NULL;
	GnmCellPos	 first_data;
	GnmRange	 range;
	GODataCache	*cache;

	XL_CHECK_CONDITION (q->length >= 44);
	xls_read_range16 (&range, q->data);
	first_header_row	= GSF_LE_GET_GINT16 (q->data +  8);
	first_data.row		= GSF_LE_GET_GINT16 (q->data + 10);
	first_data.col		= GSF_LE_GET_GINT16 (q->data + 12);
	cache_idx		= GSF_LE_GET_GINT16 (q->data + 14);
	data_field_axis		= GSF_LE_GET_GUINT16 (q->data + 18); /* Default axis for a data field */
	data_field_pos		= GSF_LE_GET_GUINT16 (q->data + 20); /* Default position for a data field */
	num_fields		= GSF_LE_GET_GUINT16 (q->data + 22); /* Number of fields */
	num_row_fields		= GSF_LE_GET_GUINT16 (q->data + 24); /* Number of row fields */
	num_column_fields	= GSF_LE_GET_GUINT16 (q->data + 26); /* Number of column fields */
	num_page_fields		= GSF_LE_GET_GUINT16 (q->data + 28); /* Number of page fields */
	num_data_fields		= GSF_LE_GET_GUINT16 (q->data + 30); /* Number of data fields */
	num_data_rows		= GSF_LE_GET_GUINT16 (q->data + 32); /* Number of data rows */
	data_columns		= GSF_LE_GET_GUINT16 (q->data + 34); /* Number of data columns */
	flags			= GSF_LE_GET_GUINT16 (q->data + 36);
	autoformat		= GSF_LE_GET_GUINT16 (q->data + 38); /* Index to the PivotTable autoformat */
	name_len		= GSF_LE_GET_GINT16 (q->data + 40);
	data_field_name_len	= GSF_LE_GET_GINT16 (q->data + 42);

	cache = (cache_idx < imp->pivot.cache_by_index->len
		 ? g_ptr_array_index (imp->pivot.cache_by_index, cache_idx)
		 : NULL);

	name = go_string_new_nocopy (
		excel_get_text (imp, q->data + 44, name_len,
			       &len, NULL, q->length - 44));
	len = MIN (len, q->length - 44);
	data_field_name = go_string_new_nocopy (
		excel_get_text (imp, q->data + 44 + len, data_field_name_len,
				&len, NULL, q->length - 44 - len));

	d(0, g_printerr ("Slicer in : %s named '%s';\n",
		       range_as_string (&range), name ? name->str : "<UNDEFINED>"););
	if (NULL != imp->pivot.slicer)
		g_object_unref (imp->pivot.slicer);
	imp->pivot.slicer = g_object_new (GNM_SHEET_SLICER_TYPE,
		"name",		name,
		"cache",	cache,
		"range",	&range,
		"sheet",	esheet->sheet,
		"first-header-row",	MAX (first_header_row - range.start.row, 0),
		"first-data-row", 	MAX (first_data.row - range.start.row, 0),
		"first-data-col",	MAX (first_data.col - range.start.col, 0),
#if 0
		"row-page-count",
		"col-page-count",
#endif
		NULL);
	go_string_unref (name);
	go_string_unref (data_field_name);

	imp->pivot.field_count = 0;
	imp->pivot.ivd_index = 0;

	/* Usually followed by num_fields SXVD */
}
