/**
 * xls-write-docprops.c: MS Excel XLSX export of document properties
 *
 * Copyright (C) 2011 Andreas J. Guelzow, All rights reserved
 * aguelzow@pyrshep.ca
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

/*
 *
 * DO * NOT * COMPILE * DIRECTLY *
 * DO * NOT * COMPILE * DIRECTLY *
 * DO * NOT * COMPILE * DIRECTLY *
 *
 * included via xlsx-write.c
 **/

typedef void (*output_function) (GsfXMLOut *output, GValue const *val);

static void
xlsx_map_time_to_int (GsfXMLOut *output, GValue const *val)
{
	switch (G_VALUE_TYPE(val)) {
	case G_TYPE_INT:
		gsf_xml_out_add_gvalue (output, NULL, val);
		break;
	case G_TYPE_STRING: {
		char const *str = g_value_get_string (val);
		int minutes = 0, seconds = 0;
		if ( 1 < sscanf (str, "PT%dM%dS", &minutes, &seconds)) {
			if (seconds > 29)
				minutes++;
			gsf_xml_out_add_int (output, NULL, minutes);
			break;
		}
		/* no break */
	}
	default:
		gsf_xml_out_add_int (output, NULL, 0);
		break;
	}
}

static void
xlsx_map_to_int (GsfXMLOut *output, GValue const *val)
{
	if (G_TYPE_INT == G_VALUE_TYPE (val))
		gsf_xml_out_add_gvalue (output, NULL, val);
	else
		gsf_xml_out_add_int (output, NULL, 0);
}

static void
xlsx_map_to_bool (GsfXMLOut *output, GValue const *val)
{
	switch (G_VALUE_TYPE(val)) {
	case G_TYPE_BOOLEAN:
		xlsx_add_bool (output, NULL, g_value_get_boolean (val));
		break;
	case G_TYPE_INT:
		xlsx_add_bool (output, NULL, g_value_get_int (val) != 0);
		break;
	case G_TYPE_STRING:
		xlsx_add_bool (output, NULL,
			       0 == g_ascii_strcasecmp (g_value_get_string (val), "true")
			       || 0 == g_ascii_strcasecmp (g_value_get_string (val), "yes"));
		break;
	default:
		xlsx_add_bool (output, NULL, FALSE);
		break;
	}
}

static void
xlsx_map_to_date_core (GsfXMLOut *output, GValue const *val)
{
	gsf_xml_out_add_cstr_unchecked (output, "xsi:type", "dcterms:W3CDTF");
	if (VAL_IS_GSF_TIMESTAMP(val))
		gsf_xml_out_add_gvalue (output, NULL, val);
	else if (G_TYPE_INT == G_VALUE_TYPE (val)) {
		GsfTimestamp * ts = gsf_timestamp_new ();
		char *str;
		gsf_timestamp_set_time (ts, g_value_get_int (val));
		str = gsf_timestamp_as_string (ts);
		gsf_xml_out_add_cstr (output, NULL, str);
		g_free (str);
		gsf_timestamp_free (ts);
	} else {
		GsfTimestamp * ts = gsf_timestamp_new ();
		char *str;
		gsf_timestamp_set_time (ts, g_get_real_time () / 1000000);
		str = gsf_timestamp_as_string (ts);
		gsf_xml_out_add_cstr (output, NULL, str);
		g_free (str);
		gsf_timestamp_free (ts);
	}
}

static void
xlsx_map_to_keys (GsfXMLOut *output, GValue const *val)
{
		GValueArray *va;
		unsigned i;

		if (G_TYPE_STRING == G_VALUE_TYPE (val)) {
			char const *str = g_value_get_string (val);
			if (str && *str)
				gsf_xml_out_add_cstr (output, NULL, str);
		} else if (NULL != (va = gsf_value_get_docprop_varray (val))) {
			char *str;
			for (i = 0 ; i < va->n_values; i++) {
				if (i!=0)
					gsf_xml_out_add_cstr_unchecked (output, NULL, " ");
				str = g_value_dup_string (g_value_array_get_nth	(va, i));
				g_strdelimit (str," \t\n\r",'_');
				gsf_xml_out_add_cstr (output, NULL, str);
				/* In Edition 2 we would be allowed to have different */
				/* sets of keywords depending on laguage */
				g_free (str);
			}
		}
}

static output_function
xlsx_map_prop_name_to_output_fun (char const *name)
{
	/* shared by all instances and never freed */
	static GHashTable *xlsx_prop_name_map_output_fun_extended = NULL;

	if (NULL == xlsx_prop_name_map_output_fun_extended)
	{
		static struct {
			char const *gsf_key;
			output_function xlsx_output_fun;
		} const map [] = {
			{ GSF_META_NAME_DATE_CREATED,       xlsx_map_to_date_core},
			{ GSF_META_NAME_DATE_MODIFIED,      xlsx_map_to_date_core},
			{ GSF_META_NAME_EDITING_DURATION,   xlsx_map_time_to_int},
			{ GSF_META_NAME_KEYWORDS,           xlsx_map_to_keys},
			{ GSF_META_NAME_CHARACTER_COUNT,    xlsx_map_to_int},
			{ GSF_META_NAME_BYTE_COUNT,         xlsx_map_to_int},
			{ GSF_META_NAME_SECURITY,           xlsx_map_to_int},
			{ GSF_META_NAME_HIDDEN_SLIDE_COUNT, xlsx_map_to_int},
			{ "xlsx:HyperlinksChanged",         xlsx_map_to_bool},
			{ GSF_META_NAME_LINE_COUNT,         xlsx_map_to_int},
			{ GSF_META_NAME_LINKS_DIRTY,        xlsx_map_to_bool},
			{ GSF_META_NAME_MM_CLIP_COUNT,      xlsx_map_to_int},
			{ GSF_META_NAME_NOTE_COUNT,         xlsx_map_to_int},
			{ GSF_META_NAME_PAGE_COUNT,         xlsx_map_to_int},
			{ GSF_META_NAME_PARAGRAPH_COUNT,    xlsx_map_to_int},
			{ "xlsx:SharedDoc",                 xlsx_map_to_bool},
			{ GSF_META_NAME_SCALE,              xlsx_map_to_bool},
			{ GSF_META_NAME_SLIDE_COUNT,        xlsx_map_to_int},
			{ GSF_META_NAME_WORD_COUNT,         xlsx_map_to_int}
		};

		int i = G_N_ELEMENTS (map);

		xlsx_prop_name_map_output_fun_extended = g_hash_table_new (g_str_hash, g_str_equal);
		while (i-- > 0)
			g_hash_table_insert (xlsx_prop_name_map_output_fun_extended,
				(gpointer)map[i].gsf_key,
				(gpointer)map[i].xlsx_output_fun);
	}

	return g_hash_table_lookup (xlsx_prop_name_map_output_fun_extended, name);
}


static char const *
xlsx_map_prop_name_extended (char const *name)
{
	/* shared by all instances and never freed */
	static GHashTable *xlsx_prop_name_map_extended = NULL;

	if (NULL == xlsx_prop_name_map_extended)
	{
		static struct {
			char const *gsf_key;
			char const *xlsx_key;
		} const map [] = {
			{ GSF_META_NAME_TEMPLATE,            "Template"},
			{ GSF_META_NAME_MANAGER,             "Manager"},
			{ GSF_META_NAME_COMPANY,             "Company"},
			{ GSF_META_NAME_PAGE_COUNT,          "Pages"},
			{ GSF_META_NAME_WORD_COUNT,          "Words"},
			{ GSF_META_NAME_CHARACTER_COUNT,     "Characters"},
			{ GSF_META_NAME_PRESENTATION_FORMAT, "PresentationFormat"},
			{ GSF_META_NAME_LINE_COUNT,          "Lines"},
			{ GSF_META_NAME_PARAGRAPH_COUNT,     "Paragraphs"},
			{ GSF_META_NAME_SLIDE_COUNT,         "Slides"},
			{ GSF_META_NAME_NOTE_COUNT,          "Notes"},
			{ GSF_META_NAME_EDITING_DURATION,    "TotalTime"},
			{ GSF_META_NAME_HIDDEN_SLIDE_COUNT,  "HiddenSlides"},
			{ "xlsx:MMClips",                    "MMClips"},
			{ GSF_META_NAME_SCALE,               "ScaleCrop"},
			/* { GSF_META_NAME_HEADING_PAIRS, "HeadingPairs"}, */
			/*                         type="CT_VectorVariant" */
			/* { , "TitlesOfParts"},    type="CT_VectorLpstr"> */
			{ GSF_META_NAME_LINKS_DIRTY,         "LinksUpToDate"},
			{ GSF_META_NAME_BYTE_COUNT,          "CharactersWithSpaces"},
			{ "xlsx:SharedDoc",                  "SharedDoc"},
			{ "xlsx:HyperlinkBase",              "HyperlinkBase"},
			/* { , "HLinks"},           type="CT_VectorVariant" */
			{ "xlsx:HyperlinksChanged",          "HyperlinksChanged"},
			/* { , "DigSig"},           type="CT_DigSigBlob" */
			{ GSF_META_NAME_SECURITY,            "DocSecurity"}
		};

		/* Not matching ECMA-376 edition 1 core or extended properties: */
		/* GSF_META_NAME_CODEPAGE */
		/* GSF_META_NAME_CASE_SENSITIVE */
		/* GSF_META_NAME_CELL_COUNT */
		/* GSF_META_NAME_DICTIONARY */
		/* GSF_META_NAME_DOCUMENT_PARTS */
		/* GSF_META_NAME_IMAGE_COUNT */
		/* GSF_META_NAME_LAST_SAVED_BY */
		/* GSF_META_NAME_LOCALE_SYSTEM_DEFAULT */
		/* GSF_META_NAME_THUMBNAIL  */
		/* GSF_META_NAME_MM_CLIP_COUNT */
		/* GSF_META_NAME_OBJECT_COUNT */
		/* GSF_META_NAME_SPREADSHEET_COUNT */
		/* GSF_META_NAME_TABLE_COUNT */
		/* GSF_META_NAME_GENERATOR  stored as Application and AppVersion  */
		/* GSF_META_NAME_KEYWORD cmp with GSF_META_NAME_KEYWORDS in core*/
		/* GSF_META_NAME_LAST_PRINTED cmp with GSF_META_NAME_PRINT_DATE in core*/
		/* GSF_META_NAME_PRINTED_BY */

		int i = G_N_ELEMENTS (map);

		xlsx_prop_name_map_extended = g_hash_table_new (g_str_hash, g_str_equal);
		while (i-- > 0)
			g_hash_table_insert (xlsx_prop_name_map_extended,
				(gpointer)map[i].gsf_key,
				(gpointer)map[i].xlsx_key);
	}

	return g_hash_table_lookup (xlsx_prop_name_map_extended, name);
}

static void
xlsx_meta_write_props_extended (char const *prop_name, GsfDocProp *prop, GsfXMLOut *output)
{
	char const *mapped_name;
	GValue const *val = gsf_doc_prop_get_val (prop);

	if (NULL != (mapped_name = xlsx_map_prop_name_extended (prop_name))) {
		gsf_xml_out_start_element (output, mapped_name);
		if (NULL != val) {
			output_function of = xlsx_map_prop_name_to_output_fun
				(prop_name);
			if (of != NULL)
				of (output, val);
			else
				gsf_xml_out_add_gvalue (output, NULL, val);
		}
		gsf_xml_out_end_element (output);
	}
}

static void
xlsx_write_docprops_app (XLSXWriteState *state, GsfOutfile *root_part, GsfOutfile *docprops_dir)
{
	GsfOutput *part = gsf_outfile_open_pkg_add_rel
		(docprops_dir, "app.xml",
		 "application/vnd.openxmlformats-officedocument.extended-properties+xml",
		 root_part,
		 "http://schemas.openxmlformats.org/officeDocument/2006/relationships/extended-properties");
	GsfXMLOut *xml = gsf_xml_out_new (part);
	GsfDocMetaData *meta = go_doc_get_meta_data (GO_DOC (state->base.wb));
	char *version;

	gsf_xml_out_start_element (xml, "Properties");
	gsf_xml_out_add_cstr_unchecked (xml, "xmlns", ns_docprops_extended);
	gsf_xml_out_add_cstr_unchecked (xml, "xmlns:vt", ns_docprops_extended_vt);
	gsf_xml_out_simple_element (xml, "Application", PACKAGE_NAME);

	/*1.10.17 is not permitted for AppVersion, so we need to convert it to 1.1017 */
	version = g_strdup_printf ("%d.%02d%02d", GNM_VERSION_EPOCH, GNM_VERSION_MAJOR, GNM_VERSION_MINOR);
	gsf_xml_out_simple_element (xml, "AppVersion", version);
	g_free (version);

	gsf_doc_meta_data_foreach (meta, (GHFunc) xlsx_meta_write_props_extended, xml);

	gsf_xml_out_end_element (xml); /* </Properties> */

	g_object_unref (xml);
	gsf_output_close (part);
	g_object_unref (part);
}

static char const *
xlsx_map_prop_name (char const *name)
{
	/* shared by all instances and never freed */
	static GHashTable *xlsx_prop_name_map = NULL;

	if (NULL == xlsx_prop_name_map)
	{
		static struct {
			char const *gsf_key;
			char const *xlsx_key;
		} const map [] = {
			{ GSF_META_NAME_CATEGORY,	"cp:category" },
			{ "cp:contentStatus",	        "cp:contentStatus" },
			{ "cp:contentType",	        "cp:contentType" },
			{ GSF_META_NAME_KEYWORDS,	"cp:keywords" },
			{ GSF_META_NAME_CREATOR,	"cp:lastModifiedBy" },
			{ GSF_META_NAME_PRINT_DATE,	"cp:lastPrinted" },
			{ GSF_META_NAME_REVISION_COUNT,	"cp:revision" },
			{ "cp:version",	                "cp:version" },
			{ GSF_META_NAME_INITIAL_CREATOR,"dc:creator" },
			{ GSF_META_NAME_DESCRIPTION,	"dc:description" },
			{ "dc:identifier",	        "dc:identifier" },
			{ GSF_META_NAME_LANGUAGE,	"dc:language" },
			{ GSF_META_NAME_SUBJECT,	"dc:subject" },
			{ GSF_META_NAME_TITLE,		"dc:title" },
			{ GSF_META_NAME_DATE_CREATED,   "dcterms:created" },
			{ GSF_META_NAME_DATE_MODIFIED,	"dcterms:modified" }
		};

		int i = G_N_ELEMENTS (map);

		xlsx_prop_name_map = g_hash_table_new (g_str_hash, g_str_equal);
		while (i-- > 0)
			g_hash_table_insert (xlsx_prop_name_map,
				(gpointer)map[i].gsf_key,
				(gpointer)map[i].xlsx_key);
	}

	return g_hash_table_lookup (xlsx_prop_name_map, name);
}

static void
xlsx_meta_write_props (char const *prop_name, GsfDocProp *prop, GsfXMLOut *output)
{
	char const *mapped_name;
	GValue const *val = gsf_doc_prop_get_val (prop);

	if (NULL != (mapped_name = xlsx_map_prop_name (prop_name))) {
		gsf_xml_out_start_element (output, mapped_name);
		if (NULL != val) {
			output_function of = xlsx_map_prop_name_to_output_fun
				(prop_name);
			if (of != NULL)
				of (output, val);
			else
				gsf_xml_out_add_gvalue (output, NULL, val);
		}
		gsf_xml_out_end_element (output);
	}
}


static void
xlsx_write_docprops_core (XLSXWriteState *state, GsfOutfile *root_part, GsfOutfile *docprops_dir)
{
	GsfOutput *part = gsf_outfile_open_pkg_add_rel
		(docprops_dir, "core.xml",
		 "application/vnd.openxmlformats-package.core-properties+xml",
		 root_part,
/* According to 15.2.12.1 this ought to be  "http://schemas.openxmlformats.org/officedocument/2006/relationships/metadata/core-properties" */
/* but this is what MS Office apparently writes. As a side effect this makes us fail strict validation */
		 "http://schemas.openxmlformats.org/package/2006/relationships/metadata/core-properties");
	GsfXMLOut *xml = gsf_xml_out_new (part);
	GsfDocMetaData *meta = go_doc_get_meta_data (GO_DOC (state->base.wb));

	gsf_xml_out_start_element (xml, "cp:coreProperties");
	gsf_xml_out_add_cstr_unchecked (xml, "xmlns:cp", ns_docprops_core_cp);
	gsf_xml_out_add_cstr_unchecked (xml, "xmlns:dc", ns_docprops_core_dc);
	gsf_xml_out_add_cstr_unchecked (xml, "xmlns:dcmitype", ns_docprops_core_dcmitype);
	gsf_xml_out_add_cstr_unchecked (xml, "xmlns:dcterms", ns_docprops_core_dcterms);
	gsf_xml_out_add_cstr_unchecked (xml, "xmlns:xsi", ns_docprops_core_xsi);

	gsf_doc_meta_data_foreach (meta, (GHFunc) xlsx_meta_write_props, xml);

	gsf_xml_out_end_element (xml); /* </cp:coreProperties> */

	g_object_unref (xml);
	gsf_output_close (part);
	g_object_unref (part);
}

static int
xlsx_map_to_pid (char const *name)
{
	/* shared by all instances and never freed */
	static GHashTable *xlsx_pid_map = NULL;

	if (NULL == xlsx_pid_map)
	{
		static struct {
			char const *name_key;
			int pid_key;
		} const map [] = {
			{ "Editor", 2}
		};

		int i = G_N_ELEMENTS (map);

		xlsx_pid_map = g_hash_table_new (g_str_hash, g_str_equal);
		while (i-- > 0)
			g_hash_table_insert (xlsx_pid_map,
				(gpointer)map[i].name_key,
					     GINT_TO_POINTER (map[i].pid_key));
	}

	return GPOINTER_TO_INT (g_hash_table_lookup (xlsx_pid_map, name));
}

static void
xlsx_meta_write_props_custom_type (char const *prop_name, GValue const *val, GsfXMLOut *xml, char const *type,
				   int *custom_pid)
{
	int pid = xlsx_map_to_pid (prop_name);


	gsf_xml_out_start_element (xml, "property");
	gsf_xml_out_add_cstr_unchecked (xml, "fmtid", "{D5CDD505-2E9C-101B-9397-08002B2CF9AE}");
	if (pid != 0)
		gsf_xml_out_add_int (xml, "pid", pid);
	else {
		gsf_xml_out_add_int (xml, "pid", *custom_pid);
		*custom_pid += 1;
	}
	gsf_xml_out_add_cstr (xml, "name", prop_name);
	gsf_xml_out_start_element (xml, type);
	if (NULL != val) {
		switch (G_VALUE_TYPE (val)) {
		case G_TYPE_BOOLEAN:
			gsf_xml_out_add_cstr (xml, NULL,
					      g_value_get_boolean (val) ? "true" : "false");
			break;
		default:
			gsf_xml_out_add_gvalue (xml, NULL, val);
			break;
		}
	}
	gsf_xml_out_end_element (xml);
	gsf_xml_out_end_element (xml); /* </property> */
}

static void
xlsx_meta_write_props_custom (char const *prop_name, GsfDocProp *prop, XLSXClosure *info)
{
	GsfXMLOut *output = info->xml;
	XLSXWriteState *state = info->state;

	if ((0 != strcmp (GSF_META_NAME_GENERATOR, prop_name)) && (NULL == xlsx_map_prop_name (prop_name))
	    &&  (NULL == xlsx_map_prop_name_extended (prop_name))) {
		GValue const *val = gsf_doc_prop_get_val (prop);
		if (VAL_IS_GSF_TIMESTAMP(val))
			xlsx_meta_write_props_custom_type (prop_name, val, output, "vt:date", &state->custom_prop_id);
		else switch (G_VALUE_TYPE(val)) {
			case G_TYPE_BOOLEAN:
				xlsx_meta_write_props_custom_type (prop_name, val, output, "vt:bool", &state->custom_prop_id);
				break;
			case G_TYPE_INT:
			case G_TYPE_LONG:
				xlsx_meta_write_props_custom_type (prop_name, val, output, "vt:i4", &state->custom_prop_id);
				break;
			case G_TYPE_FLOAT:
			case G_TYPE_DOUBLE:
				xlsx_meta_write_props_custom_type (prop_name, val, output, "vt:decimal", &state->custom_prop_id);
			break;
			case G_TYPE_STRING:
				xlsx_meta_write_props_custom_type (prop_name, val, output, "vt:lpwstr", &state->custom_prop_id);
				break;
			default:
				break;
			}
	}
}

static void
xlsx_write_docprops_custom (XLSXWriteState *state, GsfOutfile *root_part, GsfOutfile *docprops_dir)
{
	GsfOutput *part = gsf_outfile_open_pkg_add_rel
		(docprops_dir, "custom.xml",
		 "application/vnd.openxmlformats-officedocument.custom-properties+xml",
		 root_part,
		 "http://schemas.openxmlformats.org/officeDocument/2006/relationships/custom-properties");
	GsfXMLOut *xml = gsf_xml_out_new (part);
	GsfDocMetaData *meta = go_doc_get_meta_data (GO_DOC (state->base.wb));
	XLSXClosure info = { state, xml };

	gsf_xml_out_start_element (xml, "Properties");
	gsf_xml_out_add_cstr_unchecked (xml, "xmlns", ns_docprops_custom);
	gsf_xml_out_add_cstr_unchecked (xml, "xmlns:vt", ns_docprops_extended_vt);

	gsf_doc_meta_data_foreach (meta, (GHFunc) xlsx_meta_write_props_custom, &info);

	gsf_xml_out_end_element (xml); /* </Properties> */

	g_object_unref (xml);
	gsf_output_close (part);
	g_object_unref (part);
}

static void
xlsx_write_docprops (XLSXWriteState *state, GsfOutfile *root_part)
{
	GsfOutfile *docprops_dir = (GsfOutfile *)gsf_outfile_new_child (root_part, "docProps", TRUE);

	xlsx_write_docprops_app (state, root_part, docprops_dir);
	xlsx_write_docprops_core (state, root_part, docprops_dir);
	xlsx_write_docprops_custom (state, root_part, docprops_dir);

	gsf_output_close (GSF_OUTPUT (docprops_dir));
	g_object_unref (docprops_dir);
}
