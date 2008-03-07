/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/**
 * ms-pivot.c: MS Excel pivot table import/export
 *
 * Author:
 *    Jody Goldberg (jody@gnome.org)
 *
 * (C) 2005 Jody Goldberg
 **/

#include <gnumeric-config.h>
#include <gnumeric.h>
#include "ms-pivot.h"
#include "ms-biff.h"
#include "biff-types.h"
#include "boot.h"
#include "ms-excel-read.h"

#include <gsf/gsf-infile.h>
#include <gsf/gsf-input.h>
#include <gsf/gsf-utils.h>
#include <math.h>
#include <string.h>
#include <stdio.h>

#undef NO_DEBUG_EXCEL
#ifndef NO_DEBUG_EXCEL
#define d(level, code)	do { if (ms_excel_pivot_debug > level) { code } } while (0)
#else
#define d(level, code)
#endif

typedef struct {
	GnmXLImporter *importer;
	int	 cur_field, num_fields;
	int	 cur_item, num_items;
	guint32	 num_records;
	GArray	*indexed_fields;
} XLPivotReadState;

static void
d_item (XLPivotReadState *s)
{
	s->cur_item++;

	if (s->cur_field > s->num_fields) {
		g_warning ("field %d > %d expected;\n", s->cur_field, s->num_fields);
		return;
	}

	if (s->cur_item > s->num_items) {
		g_warning ("item %d > %d expected;\n", s->cur_item, s->num_items);
		return;
	}

	d (2, fprintf (stderr, "[%d of %d][%d of %d] = ",
		 s->cur_field, s->num_fields, s->cur_item, s->num_items););
}

static gboolean
check_len (BiffQuery const *q, unsigned len)
{
	if (q->length == len)
		return TRUE;
	if (len < 10)
		g_warning ("%x : expected len %d not %d", q->opcode, len, q->length);
	else
		g_warning ("%x : expected len %d (0x%x) not %d (0x%x)", q->opcode, len, len, q->length, q->length);
	return FALSE;
}

static gboolean
check_min_len (BiffQuery const *q, unsigned len)
{
	if (q->length >= len)
		return TRUE;
	if (len < 10)
		g_warning ("%x : expected >= len %d not %d", q->opcode, len, q->length);
	else
		g_warning ("%x : expected >= len %d (0x%x) not %d (0x%x)", q->opcode, len, len, q->length, q->length);
	return FALSE;
}

static gboolean
xl_read_pivot_cache (XLPivotReadState *s, BiffQuery *q)
{
	unsigned i, fields_in_index = 0;

	/* SXDB */
	if (!ms_biff_query_next (q) || q->opcode != BIFF_SXDB || !check_min_len (q, 20))
		return TRUE;

	s->num_records= GSF_LE_GET_GUINT32  (q->data + 0);
	s->num_fields  = GSF_LE_GET_GUINT16 (q->data + 12); /* base + grouped + calced */

	d (0, {
		guint16 stream_id	= GSF_LE_GET_GUINT16 (q->data + 4);
		guint16 flags		= GSF_LE_GET_GUINT16 (q->data + 6);
		guint16 rec_per_block	= GSF_LE_GET_GUINT16 (q->data + 8);	/* seems constant */
		guint16 base_fields	= GSF_LE_GET_GUINT16 (q->data + 10);	/* base */
		/* guint16 zero */
		guint16 type		= GSF_LE_GET_GUINT16 (q->data + 16);
		char *who		= excel_biff_text_2 (s->importer, q, 18);
		fprintf (stderr, "num_rec = %u;\nstream_id = %hu;\n"
			 "rec per block = %hu;\nbase fields = %hu;\ntotal fields = %hu;\n"
			 "last modified by = '%s';type = 0x%x, flags = 0x%x;\n",
			 s->num_records, stream_id, rec_per_block, base_fields,
			 s->num_fields, who, type, flags);
		g_free (who);
	});

	if (!ms_biff_query_next (q))
		return TRUE;

	/* SXDBEX (optional SXDB extension) */
	if (q->opcode == BIFF_SXDBEX && check_len (q, 12)) {
		d (1, {
			double last_refreshed	= GSF_LE_GET_DOUBLE  (q->data + 0);
			guint32 num_fmla	= GSF_LE_GET_GUINT32 (q->data + 8);
		});
		if (!ms_biff_query_next (q))
			return TRUE;
	}

	s->cur_field  = 0;
	s->num_items  = -1;
	s->indexed_fields = g_array_new (FALSE, FALSE, sizeof (int));
	do {
		switch (q->opcode) {
		case BIFF_SXFDB : if (check_min_len (q, 0x10)) { /* field descriptor */
			guint16 flags = GSF_LE_GET_GUINT16  (q->data + 0);
			unsigned const index_type = flags & 3;
			gboolean const is_calculated	= 0 != (flags & 0x04);
			gboolean const has_child	= 0 != (flags & 0x08);
			gboolean const is_numeric_group	= 0 != (flags & 0x10);
			guint16 child_group	= GSF_LE_GET_GUINT16  (q->data + 2);
			guint16 parent_group	= GSF_LE_GET_GUINT16  (q->data + 4);
			guint16 grouped_items	= GSF_LE_GET_GUINT16  (q->data + 8);
			guint16 base_items	= GSF_LE_GET_GUINT16  (q->data + 10);
			guint16 std_items	= GSF_LE_GET_GUINT16  (q->data + 12);
			char *name = excel_biff_text_2 (s->importer, q, 14);
			switch (index_type) {
			case 1 : /* items follow field description with no index */
				s->num_items  = GSF_LE_GET_GUINT16  (q->data + 6);
				fields_in_index++;
				break;
			case 2 : /* items follow the last normal field and are preceded by an index */
				g_array_append_val (s->indexed_fields, s->cur_field);
				s->num_items  = 0;
				break;
			default :
				s->num_items  = 0;
				g_warning ("unknown  index type %d for field %d", index_type, s->cur_field+1);
			}
			s->cur_field++;
			s->cur_item = 0;
			fprintf (stderr, "FIELD [%d] '%s' has %d items, %d grouped items, and flags = 0x%hx;\n",
				s->cur_field, name, s->num_items, child_group, flags);
			g_free (name);

#if 0
			d (4, gsf_mem_dump (q->data + 2, MIN (q->length, 0xc)););

from OOo, but this seems like a crock.  My tests indicate something else
// known data types
const sal_uInt16 EXC_SXFIELD_DATA_NONE      = 0x0000;   /// Special state for groupings.
const sal_uInt16 EXC_SXFIELD_DATA_STR       = 0x0480;   /// Only strings, nothing else.
const sal_uInt16 EXC_SXFIELD_DATA_INT       = 0x0520;   /// Only integers, opt. with doubles.
const sal_uInt16 EXC_SXFIELD_DATA_DBL       = 0x0560;   /// Only doubles, nothing else.
const sal_uInt16 EXC_SXFIELD_DATA_STR_INT   = 0x05A0;   /// Only strings and integers, opt. with doubles.
const sal_uInt16 EXC_SXFIELD_DATA_STR_DBL   = 0x05E0;   /// Only strings and doubles, nothing else.
const sal_uInt16 EXC_SXFIELD_DATA_DATE      = 0x0900;   /// Only dates, nothing else.
const sal_uInt16 EXC_SXFIELD_DATA_DATE_NUM  = 0x0D00;   /// Dates with integers or doubles without strings.
const sal_uInt16 EXC_SXFIELD_DATA_DATE_STR  = 0x0D80;   /// Dates and strings, opt. with integers or doubles.
const sal_uInt16 EXC_SXFIELD_DATA_MASK      = 0x0DE0;

const sal_uInt16 EXC_SXFIELD_INDEX_MIN      = 0;        /// List index for minimum item in groupings.
const sal_uInt16 EXC_SXFIELD_INDEX_MAX      = 1;        /// List index for maximum item in groupings.
const sal_uInt16 EXC_SXFIELD_INDEX_STEP     = 2;        /// List index for step item in groupings.
#endif
		}
		break;

		case BIFF_SXNUM: if (check_len (q, 8)) {
			double val = GSF_LE_GET_DOUBLE  (q->data + 0);
			d_item (s);
			d (2, fprintf (stderr, "%g (num);\n", val););
		}
		break;

		case BIFF_SXBOOL: if (check_len (q, 2)) {
			gboolean val = 0 != GSF_LE_GET_GINT16  (q->data + 0);
			d_item (s);
			d (2, fprintf (stderr, "%s (bool);\n", val ? "true" : "false"););
		}
		break;

		case BIFF_SXERR: if (check_len (q, 2)) {
			gint16 val = GSF_LE_GET_GINT16  (q->data + 0);
			d_item (s);
			d (2, fprintf (stderr, "%hx (err);\n", val););
		}
		break;

		case BIFF_SXINT: if (check_len (q, 2)) {	/* signed short */
			gint16 val = GSF_LE_GET_GINT16  (q->data + 0);
			d_item (s);
			d (2, fprintf (stderr, "%hx (short);\n", val););
		}
		break;

		case BIFF_SXSTRING: if (check_min_len (q, 2)) {
			char *val = excel_biff_text_2 (s->importer, q, 0);
			d_item (s);
			d (2, fprintf (stderr, "'%s' (string);\n", val););
			g_free (val);
		}
		break;

		case BIFF_SXDTR: if (check_len (q, 8)) { /* date time */
			guint16 year   = GSF_LE_GET_GUINT16  (q->data + 0);
			guint16 month  = GSF_LE_GET_GUINT16  (q->data + 2);
			guint8  day    = GSF_LE_GET_GUINT16  (q->data + 4);
			guint8  hour   = GSF_LE_GET_GUINT8  (q->data + 5);
			guint8  minute = GSF_LE_GET_GUINT8  (q->data + 6);
			guint8  second = GSF_LE_GET_GUINT8  (q->data + 7);
			d_item (s);
			d (2, fprintf (stderr, "%hu/%hu/%hhu %hhu:%hhu:%hhu (date);\n",
				       year, month, day, hour, minute, second););
		 }
		break;

		case BIFF_SXNIL: if (check_len (q, 0)) { /* nil field */
			d_item (s);
			d (2, fprintf (stderr, "(empty);\n"););
		}
		break;

		case BIFF_SXDDB: if (check_len (q, fields_in_index)) {
			d (2, {
				for (i = 0 ; i < q->length; i++)
					fprintf (stderr, "%hhu ", GSF_LE_GET_GINT8 (q->data + i));
				fprintf (stderr, "\n");
			});
		}
		break;

		case BIFF_SXNUMGROUP: if (check_len (q, 2)) { /* from OO : numerical grouping in pivot cache field */
			guint16 flags = GSF_LE_GET_GUINT16  (q->data + 0);
			gboolean const autoMinima = 0 != (flags & 1);
			gboolean const autoMaxima = 0 != (flags & 2);
			unsigned const type = (flags >> 2) & 0xf;
			unsigned const remaining = (flags >> 6) & 0x2ff;
#if 0
			case 1:    "seconds"   break;
			case 2:    "minutes"   break;
			case 3:    "hours"     break;
			case 4:    "days"      break;
			case 5:    "months"    break;
			case 6:    "quarters"  break;
			case 7:    "years"     break;
			case 8:    "numeric"   break;
			default:   "unknown"
#endif
			d (2, fprintf (stderr, "group with 0x%hx flag;\n", flags););
		}
		break;

		case BIFF_SXFDBTYPE : if (check_len (q, 2)) {
		}
		break;

		default:
			ms_biff_query_dump (q);
		}
	} while (ms_biff_query_next (q) && q->opcode != BIFF_EOF);

	g_array_free (s->indexed_fields, TRUE);

	return FALSE;
}

void
excel_read_pivot_caches (GnmXLImporter *importer,
			 BiffQuery const *content_query,
			 GsfInfile *ole)
{
	int i, n;
	XLPivotReadState   s;
	BiffQuery *q;
	GsfInput  *cache, *dir;

	return;

	if (NULL == ole)
		return;				/* pre-Excel 95 without ole */
	dir = gsf_infile_child_by_name (ole, "_SX_DB_CUR");	/* Excel 97 */
	if (NULL == dir)
		dir = gsf_infile_child_by_name (ole, "_SX_DB");	/* Excel 95 */
	if (NULL == dir)
		return;

	s.importer = importer;
	n = gsf_infile_num_children (GSF_INFILE (dir));
	for (i = 0 ; i < n ; i++) {
		cache = gsf_infile_child_by_index (GSF_INFILE (dir), i);
		if (NULL == cache)
			continue;
		d (0, fprintf (stderr, "{ /* PIVOT CACHE [%d of %d] = %s*/\n", i+1, n, gsf_input_name (cache)););
		q = ms_biff_query_new (cache);
		xl_read_pivot_cache (&s, q);
		ms_biff_query_destroy (q);
		g_object_unref (cache);
		d (0, fprintf (stderr, "}; /* PIVOT CACHE [%d of %d] */\n", i+1, n););
	}
	g_object_unref (dir);
}
