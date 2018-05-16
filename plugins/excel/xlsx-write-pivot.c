/*
 * xlsx-write-pivot.c : export pivot tables (tm) to MS Office Open xlsx files.
 *
 * Copyright (C) 2008 Jody Goldberg (jody@gnome.org)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) version 3.
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
 */

#include <go-data-cache-impl.h>
#include <go-data-cache-field.h>
#include <gnm-data-cache-source.h>

/*
 *
 * DO * NOT * COMPILE * DIRECTLY *
 * DO * NOT * COMPILE * DIRECTLY *
 * DO * NOT * COMPILE * DIRECTLY *
 *
 * included via xlsx-write.c
 **/
static void
xlsx_write_pivot_val (XLSXWriteState *state, GsfXMLOut *xml,
		      GOVal const *v)
{
	g_return_if_fail (v != NULL);
	switch (v->v_any.type) {
	case VALUE_CELLRANGE:
	case VALUE_ARRAY:
		g_warning ("REMOVE THIS CODE WHEN WE MOVE TO GOFFICE");
		break;

	case VALUE_EMPTY:
		gsf_xml_out_simple_element (xml, "m", NULL);
		break;

	case VALUE_BOOLEAN:
		gsf_xml_out_start_element (xml, "b");
		xlsx_add_bool (xml, "v", value_get_as_int (v));
		gsf_xml_out_end_element (xml);
		break;

	case VALUE_FLOAT: {
		GOFormat const *fmt = go_val_get_fmt (v);
		if (NULL != fmt && go_format_is_date (fmt)) {
			char *d = format_value (state->date_fmt, v, -1, workbook_date_conv (state->base.wb));
			gsf_xml_out_start_element (xml, "d");
			gsf_xml_out_add_cstr_unchecked (xml, "v", d);
			gsf_xml_out_end_element (xml);
		} else {
			gsf_xml_out_start_element (xml, "n");
			go_xml_out_add_double (xml, "v", v->v_float.val);
			gsf_xml_out_end_element (xml);
		}
		break;
	}

	case VALUE_ERROR :
		gsf_xml_out_start_element (xml, "e");
		gsf_xml_out_add_cstr (xml, "v", v->v_err.mesg->str);
		gsf_xml_out_end_element (xml);
		break;

	case VALUE_STRING :
		gsf_xml_out_start_element (xml, "s");
		gsf_xml_out_add_cstr (xml, "v", v->v_str.val->str);
		gsf_xml_out_end_element (xml);
		break;
	}
}

static char const *
xlsx_write_pivot_cache_records (XLSXWriteState *state, GODataCache const *cache,
				GsfOutput *cache_def_part, unsigned int cache_records_num)
{
	unsigned int i, j;
	GsfXMLOut *xml;
	char *name = g_strdup_printf ("pivotCacheRecords%u.xml", cache_records_num);
	GsfOutput *record_part = gsf_outfile_new_child_full (xlsx_dir_get (&state->pivotCache_dir), name, FALSE,
		"content-type", "application/vnd.openxmlformats-officedocument.spreadsheetml.pivotCacheRecords+xml",
		NULL);
	char const *record_id = gsf_outfile_open_pkg_relate (GSF_OUTFILE_OPEN_PKG (record_part),
		GSF_OUTFILE_OPEN_PKG (cache_def_part),
		"http://schemas.openxmlformats.org/officeDocument/2006/relationships/pivotCacheRecords");

	xml = gsf_xml_out_new (record_part);

	gsf_xml_out_start_element (xml, "pivotCacheRecords");
	gsf_xml_out_add_cstr_unchecked (xml, "xmlns", ns_ss);
	gsf_xml_out_add_cstr_unchecked (xml, "xmlns:r", ns_rel);

	gsf_xml_out_add_int (xml, "count", go_data_cache_num_items (cache));
	for (j = 0 ; j < go_data_cache_num_items (cache); j++) {
		gsf_xml_out_start_element (xml, "r");
		for (i = 0 ; i < go_data_cache_num_fields (cache); i++) {
			GODataCacheField *field = go_data_cache_get_field (cache, i);
			switch (go_data_cache_field_ref_type (field)) {
			case GO_DATA_CACHE_FIELD_TYPE_INDEXED_I8 :
			case GO_DATA_CACHE_FIELD_TYPE_INDEXED_I16 :	/* fallthrough */
			case GO_DATA_CACHE_FIELD_TYPE_INDEXED_I32 :	/* fallthrough */
				gsf_xml_out_start_element (xml, "x");
				gsf_xml_out_add_int (xml, "v",
					go_data_cache_get_index (cache, field, j));
				gsf_xml_out_end_element (xml);
				break;

			case GO_DATA_CACHE_FIELD_TYPE_INLINE : {
				GOVal const *v = go_data_cache_field_get_val (field, j);
				if (v != NULL)
					xlsx_write_pivot_val (state, xml, v);
				break;
			}
			case GO_DATA_CACHE_FIELD_TYPE_NONE :
				continue;
			}
		}
		gsf_xml_out_end_element (xml); /* </r> */
	}
	gsf_xml_out_end_element (xml); /* </pivotCacheRecords> */

	g_object_unref (xml);
	gsf_output_close (record_part);
	g_object_unref (record_part);
	g_free (name);

	return record_id;
}

static void
xlsx_write_pivot_cache_source (XLSXWriteState *state, GsfXMLOut *xml, GODataCache const *cache)
{
	GODataCacheSource const *src = go_data_cache_get_source (cache);

	if (NULL == src)
		return;

	if (GNM_IS_DATA_CACHE_SOURCE (src)) {
		GnmDataCacheSource const *ssrc = GNM_DATA_CACHE_SOURCE (src);
		Sheet const *src_sheet	= gnm_data_cache_source_get_sheet (ssrc);
		GnmRange const	*r	= gnm_data_cache_source_get_range (ssrc);
		char const	*name	= gnm_data_cache_source_get_name  (ssrc);
		gsf_xml_out_start_element (xml, "cacheSource");
		gsf_xml_out_add_cstr_unchecked (xml, "type", "worksheet");
		gsf_xml_out_start_element (xml, "worksheetSource");
		if (NULL != r)		xlsx_add_range (xml, "ref", r);
		if (NULL != src_sheet)	gsf_xml_out_add_cstr (xml, "sheet", src_sheet->name_unquoted);
		if (NULL != name)	gsf_xml_out_add_cstr (xml, "name", name);
		/* "id" == sheetId : do we need this ? */
		gsf_xml_out_end_element (xml); /* </worksheetSource> */
		gsf_xml_out_end_element (xml); /* </cacheSource> */
	} else {
		g_warning ("UNSUPPORTED  GODataCacheSource of type %s", G_OBJECT_TYPE_NAME(src));
	}
}

static void
xlsx_write_pivot_val_array (XLSXWriteState *state, GsfXMLOut *xml,
			    GOValArray const *vals, char const *name)
{
	unsigned int i;
	GOVal const *v;

	gsf_xml_out_start_element (xml, name);
	gsf_xml_out_add_uint (xml, "count", vals->len);
	for (i = 0 ; i < vals->len ; i++)
		if (NULL != (v = g_ptr_array_index (vals, i)))
			xlsx_write_pivot_val (state, xml, v);
	gsf_xml_out_end_element (xml);
}

static void
xlsx_write_date (XLSXWriteState *state, GsfXMLOut *xml,
		 char const *id, gnm_float v)
{
	GOVal *tmp = go_val_new_float (v);
	char *d = format_value (state->date_fmt, tmp, -1, workbook_date_conv (state->base.wb));
	gsf_xml_out_add_cstr_unchecked (xml, id, d);
	g_free (d);
	go_val_free (tmp);
}

static void
xlsx_write_pivot_cache_field (XLSXWriteState *state, GsfXMLOut *xml,
			      GODataCacheField const *field)
{
	GOValArray const *vals;

	gsf_xml_out_start_element (xml, "cacheField");
	gsf_xml_out_add_cstr (xml, "name", go_data_cache_field_get_name (field)->str);
	gsf_xml_out_add_int (xml, "numFmtId", 0);	/* TODO */

	if (NULL != (vals = go_data_cache_field_get_vals (field, FALSE)))
		xlsx_write_pivot_val_array (state, xml, vals, "sharedItems");

	if (NULL != (vals = go_data_cache_field_get_vals (field, TRUE))) {
		int		parent_group;
		GOValBucketer  *bucketer = NULL;
		const char *group_by = NULL;

		g_object_get (G_OBJECT (field),
			      "group-parent", &parent_group,
			      "bucketer", &bucketer,
			      NULL);
		gsf_xml_out_start_element (xml, "fieldGroup");
		if (parent_group >= 0)
			gsf_xml_out_add_int (xml, "base", parent_group);

		gsf_xml_out_start_element (xml, "rangePr");
		switch (bucketer->type) {
		case GO_VAL_BUCKET_SECOND		: group_by = "seconds"; break;
		case GO_VAL_BUCKET_MINUTE		: group_by = "minutes"; break;
		case GO_VAL_BUCKET_HOUR			: group_by = "hours"; break;
		case GO_VAL_BUCKET_DAY_OF_YEAR		: group_by = "days"; break;
		case GO_VAL_BUCKET_MONTH		: group_by = "months"; break;
		case GO_VAL_BUCKET_CALENDAR_QUARTER	: group_by = "quarters"; break;
		case GO_VAL_BUCKET_YEAR			: group_by = "years"; break;
		default:
							  /* default to linear */;
		case GO_VAL_BUCKET_SERIES_LINEAR 	:break;
		}
		if (group_by) gsf_xml_out_add_cstr_unchecked (xml, "groupBy", group_by);
		if (bucketer->type == GO_VAL_BUCKET_SERIES_LINEAR) {
			go_xml_out_add_double (xml, "startNum",		bucketer->details.series.minimum);
			go_xml_out_add_double (xml, "endNum",		bucketer->details.series.maximum);
			go_xml_out_add_double (xml, "groupInterval",	bucketer->details.series.step);
		} else {
			xlsx_write_date (state, xml, "startDate", bucketer->details.dates.minimum);
			xlsx_write_date (state, xml, "endDate",   bucketer->details.dates.maximum);
		}
		gsf_xml_out_end_element (xml); /* </rangePr> */

		xlsx_write_pivot_val_array (state, xml, vals, "groupItems");
		gsf_xml_out_end_element (xml); /* </fieldGroup> */
	}

	gsf_xml_out_end_element (xml); /* </cacheField> */
}

static char const *
xlsx_write_pivot_cache_definition (XLSXWriteState *state, GsfOutfile *wb_part,
				   GODataCache const *cache, unsigned int cache_def_num)
{
	GsfXMLOut *xml;
	int i, n;
	char const *record_id;
	char *name = g_strdup_printf ("pivotCacheDefinition%u.xml", cache_def_num);
	GsfOutput *cache_def_part = gsf_outfile_new_child_full (xlsx_dir_get (&state->pivotCache_dir), name, FALSE,
		"content-type", "application/vnd.openxmlformats-officedocument.spreadsheetml.pivotCacheDefinition+xml",
		NULL);
	char const *cache_def_id = gsf_outfile_open_pkg_relate (GSF_OUTFILE_OPEN_PKG (cache_def_part),
		GSF_OUTFILE_OPEN_PKG (wb_part),
		"http://schemas.openxmlformats.org/officeDocument/2006/relationships/pivotCacheDefinition");

	record_id = xlsx_write_pivot_cache_records (state, cache, cache_def_part, cache_def_num);

	xml = gsf_xml_out_new (cache_def_part);

	gsf_xml_out_start_element (xml, "pivotCacheDefinition");
	gsf_xml_out_add_cstr_unchecked (xml, "xmlns", ns_ss);
	gsf_xml_out_add_cstr_unchecked (xml, "xmlns:r", ns_rel);

	gsf_xml_out_add_cstr (xml, "r:id", record_id);
	if (cache->refreshed_by) gsf_xml_out_add_cstr (xml, "refreshedBy", cache->refreshed_by);
	if (cache->refreshed_on) {
		if (state->version == ECMA_376_2006)
			go_xml_out_add_double (xml, "refreshedDate",
					       go_val_as_float (cache->refreshed_on));
		else {
			GOFormat const *format = go_format_new_from_XL ("yyyy-mm-dd\"T\"hh:mm:ss");
			gchar *date = format_value (format, cache->refreshed_on, -1, NULL);
			gsf_xml_out_add_cstr_unchecked (xml, "refreshedDateIso", date);
			g_free (date);
			go_format_unref (format);
		}
	}
	gsf_xml_out_add_int (xml, "createdVersion",	cache->XL_created_ver);
	gsf_xml_out_add_int (xml, "refreshedVersion",	cache->XL_refresh_ver);
	gsf_xml_out_add_uint (xml, "recordCount",	go_data_cache_num_items  (cache));
	xlsx_add_bool (xml, "upgradeOnRefresh", cache->refresh_upgrades);
	xlsx_write_pivot_cache_source (state, xml, cache);

	gsf_xml_out_start_element (xml, "cacheFields");
	n = go_data_cache_num_fields (cache);
	gsf_xml_out_add_uint (xml, "count", n);
	for (i = 0 ; i < n ; i++)
		xlsx_write_pivot_cache_field (state, xml, go_data_cache_get_field (cache, i));
	gsf_xml_out_end_element (xml); /* </cacheFields> */

	gsf_xml_out_end_element (xml); /* </pivotCacheDefinition> */

	g_object_unref (xml);
	gsf_output_close (cache_def_part);
	g_object_unref (cache_def_part);
	g_free (name);

	return cache_def_id;
}

static GSList *
xlsx_write_pivots (XLSXWriteState *state, GsfOutfile *wb_part)
{
	GHashTable *caches = excel_collect_pivot_caches (state->base.wb);
	GHashTableIter iter;
	GSList *refs = NULL;
	gpointer key, value;
	char const *cache_def_id;

	if (caches == NULL)
		return NULL;

	state->date_fmt = xlsx_pivot_date_fmt ();

	g_hash_table_iter_init (&iter, caches);
	while (g_hash_table_iter_next (&iter, &key, &value)) {
		if (NULL != key) {
			cache_def_id = xlsx_write_pivot_cache_definition (state, wb_part, key, GPOINTER_TO_UINT(value));
			refs = g_slist_prepend (refs, (gpointer)cache_def_id);
		}
	}

	g_hash_table_destroy (caches);
	go_format_unref	(state->date_fmt);
	state->date_fmt = NULL;

	return g_slist_reverse (refs);
}
