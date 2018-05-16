/**
 * xls-write-pivot.c: MS Excel XLS export of pivot tables (tm)
 *
 * (C) 2008 Jody Goldberg
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
#include "ms-excel-write.h"
#include <gnm-datetime.h>
#include <workbook.h>
#include <go-val.h>
#include <go-data-cache.h>
#include <go-data-cache-field.h>

#include <gsf/gsf-outfile.h>
#include <gsf/gsf-output.h>
#include <gsf/gsf-utils.h>

#include <string.h>

static void
xls_write_SXDB (ExcelWriteState *ewb, GODataCache const *cache, guint16 stream_id)
{
	guint8 buf[18];
	char *refreshedBy = NULL;

	unsigned int	num_items = go_data_cache_num_items  (cache);
	unsigned int	num_fields = go_data_cache_num_fields (cache), num_base_fields = 0, i;
	guint16		source_type = 1; /* default to excel sheet */
#if 0
	GODataCacheSource *src = go_data_cache_get_source (cache);
#endif

	ms_biff_put_var_next (ewb->bp, BIFF_SXDB);
	memset (buf, 0, sizeof (buf));
	GSF_LE_SET_GUINT32 (buf + 0, num_items);
	GSF_LE_SET_GUINT16 (buf + 4, stream_id);
	GSF_LE_SET_GUINT16 (buf + 6, 0x21);	/* 1 = table layout, 20 = refresh enabled */
	GSF_LE_SET_GUINT16 (buf + 8, 0xaaa);	/* OOo uses 0x1FFF, XL2003 0xAAA */
	for (i = 0 ; i < num_fields ; i++)
		if (go_data_cache_field_is_base (go_data_cache_get_field  (cache, i)))
			num_base_fields++;
	GSF_LE_SET_GUINT16 (buf + 10, num_base_fields); /* base fields */
	GSF_LE_SET_GUINT16 (buf + 12, num_fields); /* base + grouped + calced fields */
	GSF_LE_SET_GUINT16 (buf + 14, 0);	/* unused */
	GSF_LE_SET_GUINT16 (buf + 16, source_type);
	ms_biff_put_var_write (ewb->bp, buf, sizeof (buf));

	g_object_get (G_OBJECT (cache), "refreshed-by", &refreshedBy, NULL);
	excel_write_string (ewb->bp, STR_TWO_BYTE_LENGTH, refreshedBy);
	g_free (refreshedBy);
	ms_biff_put_commit (ewb->bp);
}
static void
xls_write_SXDBEX (ExcelWriteState *ewb, GODataCache const *cache)
{
	guint8 *data;
	GOVal *refreshed_on = NULL;
	g_object_get (G_OBJECT (cache), "refreshed-on", &refreshed_on, NULL);
       	data = ms_biff_put_len_next (ewb->bp, BIFF_SXDBEX, 12);
	GSF_LE_SET_DOUBLE (data + 0, go_val_as_float (refreshed_on));
	GSF_LE_SET_GUINT32 (data + 8, 0); /* num formula */
	ms_biff_put_commit (ewb->bp);
	go_val_free (refreshed_on);
}

static void
xls_write_pivot_cache_date_value (ExcelWriteState *ewb, GOVal const *v)
{
	int seconds = datetime_value_to_seconds	(v, workbook_date_conv (ewb->base.wb));
	guint8 tmp, *data = ms_biff_put_len_next (ewb->bp, BIFF_SXDTR, 8);
	GDate  d;

	datetime_value_to_g (&d, v, workbook_date_conv (ewb->base.wb));
	GSF_LE_SET_GUINT16 (data + 0, g_date_get_year (&d));
	GSF_LE_SET_GUINT16 (data + 2, g_date_get_month (&d));
	GSF_LE_SET_GUINT8  (data + 4, g_date_get_day (&d));

	tmp = (seconds >= 0) ? (seconds / 3600) : 0;
	GSF_LE_SET_GUINT8  (data + 5, tmp);
	tmp = (seconds >= 0) ? (seconds / 60 % 60) : 0;
	GSF_LE_SET_GUINT8  (data + 6, tmp);
	tmp = (seconds >= 0) ? (seconds % 60) : 0;
	GSF_LE_SET_GUINT8  (data + 7, tmp);
	ms_biff_put_commit (ewb->bp);
}

static void
xls_write_pivot_cache_value (ExcelWriteState *ewb, GOVal const *v)
{
	if (NULL != v) {
		switch (v->v_any.type) {
		case VALUE_CELLRANGE:
		case VALUE_ARRAY:
			g_warning ("REMOVE THIS CODE WHEN WE MOVE TO GOFFICE");
			break;

		case VALUE_EMPTY:
			ms_biff_put_empty (ewb->bp, BIFF_SXNIL);
			break;

		case VALUE_BOOLEAN:
			ms_biff_put_2byte (ewb->bp, BIFF_SXBOOL, value_get_as_int (v));
			break;

		case VALUE_FLOAT: {
			GOFormat const *fmt = go_val_get_fmt (v);
			if (NULL != fmt && go_format_is_date (fmt))
				xls_write_pivot_cache_date_value (ewb, v);
			else {
				guint8 *data = ms_biff_put_len_next (ewb->bp, BIFF_SXNUM, 8);
				double d = value_get_as_float (v);
				GSF_LE_SET_DOUBLE (data, d);
				ms_biff_put_commit (ewb->bp);
			}
			break;
		}

		case VALUE_ERROR :
			ms_biff_put_2byte (ewb->bp, BIFF_SXERR, excel_write_map_errcode (v));
			break;

		case VALUE_STRING :
			ms_biff_put_var_next (ewb->bp, BIFF_SXSTRING);
			excel_write_string (ewb->bp, STR_TWO_BYTE_LENGTH, v->v_str.val->str);
			ms_biff_put_commit (ewb->bp);
			break;
		}
	} else
		ms_biff_put_empty (ewb->bp, BIFF_SXNIL);
}

static void
xls_write_pivot_cache_group (ExcelWriteState *ewb,
			     GOValArray const *grouped_items,
			     GOValBucketer *bucketer)
{
	guint16 flags = 0;
	unsigned int i;

	for (i = 0 ; i < grouped_items->len ; i++)
		xls_write_pivot_cache_value (ewb, go_val_array_index (grouped_items, i));

	switch (bucketer->type) {
	case GO_VAL_BUCKET_SECOND	: flags = 1; break;
	case GO_VAL_BUCKET_MINUTE	: flags = 2; break;
	case GO_VAL_BUCKET_HOUR		: flags = 3; break;
	case GO_VAL_BUCKET_DAY_OF_YEAR	: flags = 4; break;
	case GO_VAL_BUCKET_MONTH	: flags = 5; break;
	case GO_VAL_BUCKET_CALENDAR_QUARTER : flags = 6; break;
	case GO_VAL_BUCKET_YEAR		: flags = 7; break;
	default:
		 /* default to linear */;
	case GO_VAL_BUCKET_SERIES_LINEAR : flags = 8; break;
	}
	flags <<= 2;
	ms_biff_put_2byte (ewb->bp, BIFF_SXNUMGROUP, flags);
	if (bucketer->type == GO_VAL_BUCKET_SERIES_LINEAR) {
		GOVal *tmp = go_val_new_float (0.);
		tmp->v_float.val = bucketer->details.series.minimum;
		xls_write_pivot_cache_value (ewb, tmp);
		tmp->v_float.val = bucketer->details.series.maximum;
		xls_write_pivot_cache_value (ewb, tmp);
		tmp->v_float.val = bucketer->details.series.step;
		xls_write_pivot_cache_value (ewb, tmp);
		go_val_free (tmp);
	} else {
		GOVal *tmp = go_val_new_float (0.);
		tmp->v_float.val = bucketer->details.dates.minimum;
		xls_write_pivot_cache_date_value (ewb, tmp);
		tmp->v_float.val = bucketer->details.dates.maximum;
		xls_write_pivot_cache_date_value (ewb, tmp);
		ms_biff_put_2byte (ewb->bp, BIFF_SXINT, 1);
		go_val_free (tmp);
	}
}

static void
xls_write_cache_field (ExcelWriteState *ewb, GODataCacheField const *field)
{
	GOValBucketer *bucketer = NULL;
	GOValArray const *shared_items = go_data_cache_field_get_vals (field, FALSE);
	GOValArray const *grouped_items = go_data_cache_field_get_vals (field, TRUE);
	guint8 buf[14];
	guint16 flags = 0;
	unsigned int i;
	int parent_group;

	g_object_get (G_OBJECT (field),
		"group-parent", &parent_group,
		"bucketer", &bucketer,
		NULL);
	if (NULL != go_data_cache_field_get_vals (field, TRUE)) flags |= 0x10;

	if (grouped_items || shared_items)
		flags |= 1;
	else
		flags |= 2;
	ms_biff_put_var_next (ewb->bp, BIFF_SXFDB);
	GSF_LE_SET_GUINT16 (buf + 0, flags);
	GSF_LE_SET_GUINT16 (buf + 2, 0); /* child_group */
	GSF_LE_SET_GUINT16 (buf + 4, (parent_group >= 0) ? parent_group : 0);
	GSF_LE_SET_GUINT16 (buf + 6, grouped_items ? grouped_items->len : 0);
	GSF_LE_SET_GUINT16 (buf + 8, grouped_items ? grouped_items->len : 0);
	GSF_LE_SET_GUINT16 (buf + 10, 0);
	GSF_LE_SET_GUINT16 (buf + 12, shared_items ? shared_items->len : 0 );
	ms_biff_put_var_write (ewb->bp, buf, sizeof (buf));

	excel_write_string (ewb->bp, STR_TWO_BYTE_LENGTH, go_data_cache_field_get_name (field)->str);
	ms_biff_put_commit (ewb->bp);

	ms_biff_put_2byte (ewb->bp, BIFF_SXFDBTYPE, 0); /* TODO */

	if (grouped_items && bucketer)
		xls_write_pivot_cache_group (ewb, grouped_items, bucketer);
	if (shared_items)
		for (i = 0 ; i < shared_items->len ; i++)
			xls_write_pivot_cache_value (ewb, go_val_array_index (shared_items, i));
}

static void
xls_write_cache_row (ExcelWriteState *ewb, GODataCache const *cache, unsigned int n,
		     GPtrArray *indexed, GPtrArray *inlined)
{
	unsigned int i;

	if (indexed->len > 0) {
		guint8 *data = ms_biff_put_len_next (ewb->bp, BIFF_SXDDB, indexed->len);
		for (i = 0 ; i < indexed->len ; i++)
			data[i] = go_data_cache_get_index (cache, g_ptr_array_index (indexed, i), n);
		ms_biff_put_commit (ewb->bp);
	}

	for (i = 0 ; i < inlined->len ; i++)
		xls_write_pivot_cache_value (ewb,
			go_data_cache_field_get_val (g_ptr_array_index (inlined, i), n));
}

static void
xls_write_pivot_cache (ExcelWriteState *ewb, GODataCache const *cache, guint16 streamid)
{
	unsigned int i;
	GPtrArray *indexed, *inlined;
	GODataCacheField const *field;

	xls_write_SXDB (ewb, cache, streamid);
	xls_write_SXDBEX (ewb, cache);

	indexed = g_ptr_array_new ();
	inlined = g_ptr_array_new ();
	for (i = 0 ; i < go_data_cache_num_fields (cache); i++) {
		field = go_data_cache_get_field (cache, i);
		xls_write_cache_field (ewb, field);
		switch (go_data_cache_field_ref_type (field)) {
		case GO_DATA_CACHE_FIELD_TYPE_INDEXED_I8 :
			g_ptr_array_add (indexed, (gpointer)field);
			break;
		case GO_DATA_CACHE_FIELD_TYPE_INDEXED_I16 :	/* fallthrough */
		case GO_DATA_CACHE_FIELD_TYPE_INDEXED_I32 :	/* fallthrough */
		case GO_DATA_CACHE_FIELD_TYPE_INLINE :
			g_ptr_array_add (inlined, (gpointer)field);
			break;
		case GO_DATA_CACHE_FIELD_TYPE_NONE :
			break;
		}
	}

	for (i = 0 ; i < go_data_cache_num_items (cache); i++)
		xls_write_cache_row (ewb, cache, i, indexed, inlined);
	ms_biff_put_empty (ewb->bp, BIFF_EOF);

	g_ptr_array_free (indexed, TRUE);
	g_ptr_array_free (inlined, TRUE);
}

void
xls_write_pivot_caches (ExcelWriteState *ewb, GsfOutfile *outfile,
			MsBiffVersion version, int codepage)
{
	GsfOutput  *content;
	GsfOutfile *dir;
	GHashTableIter iter;
	gpointer cache, value;
	char name[5];

	if (ewb->base.pivot_caches == NULL)
		return;

	dir = (GsfOutfile *)gsf_outfile_new_child (outfile,
		(version >= MS_BIFF_V8) ? "_SX_DB_CUR" : "_SX_DB", TRUE);
	g_hash_table_iter_init (&iter, ewb->base.pivot_caches);
	while (g_hash_table_iter_next (&iter, &cache, &value)) {
		guint16 id = GPOINTER_TO_UINT (value);
		snprintf (name, sizeof (name), "%04hX", id);
		content = gsf_outfile_new_child (dir, name, FALSE);
		ewb->bp = ms_biff_put_new (content, version, codepage);

		xls_write_pivot_cache (ewb, cache, id);

		ms_biff_put_destroy (ewb->bp);
		ewb->bp = NULL;
	}
	gsf_output_close ((GsfOutput *)dir);
	g_object_unref (dir);

	g_hash_table_destroy (ewb->base.pivot_caches);
	ewb->base.pivot_caches = NULL;
}
