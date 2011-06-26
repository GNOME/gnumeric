/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
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
		if (NULL != val)
			gsf_xml_out_add_gvalue (output, NULL, val);
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
	double version;

	gsf_xml_out_start_element (xml, "Properties");
	gsf_xml_out_add_cstr_unchecked (xml, "xmlns", ns_docprops_extended);
	gsf_xml_out_add_cstr_unchecked (xml, "xmlns:vt", ns_docprops_extended_vt);
	gsf_xml_out_start_element (xml, "Application");
	gsf_xml_out_add_cstr_unchecked (xml, NULL, PACKAGE_NAME);
	gsf_xml_out_end_element (xml); /* </Application> */
	gsf_xml_out_start_element (xml, "AppVersion");
	/*1.10.17 is not permitted for AppVersion, so we need to convert it to 1.1017 */
	version = GNM_VERSION_EPOCH + 0.01 * GNM_VERSION_MAJOR + 0.0001 * GNM_VERSION_MINOR;
	gsf_xml_out_add_float (xml, NULL, version, 4);
	gsf_xml_out_end_element (xml); /* </AppVersion> */

	gsf_doc_meta_data_foreach (meta, (GHFunc) xlsx_meta_write_props_extended, xml);	

	gsf_xml_out_end_element (xml); /* </Properties> */
	
	g_object_unref (xml);
	gsf_output_close (part);
	g_object_unref (part);
}

static char const *
xlsx_map_prop_type (char const *name)
{
	/* shared by all instances and never freed */
	static GHashTable *xlsx_prop_type_map = NULL;

	if (NULL == xlsx_prop_type_map) 
	{
		static struct {
			char const *gsf_key;
			char const *xlsx_type;
		} const map [] = {
			/* Note that in ECMA-376 edition 1 these are the only 2 props   */
			/* permitted to have types attached to them and they must be as */
			/* as given here. */
			{ GSF_META_NAME_DATE_CREATED,   "dcterms:W3CDTF" },
			{ GSF_META_NAME_DATE_MODIFIED,	"dcterms:W3CDTF" }/* , */
		};
		int i = G_N_ELEMENTS (map);

		xlsx_prop_type_map = g_hash_table_new (g_str_hash, g_str_equal);
		while (i-- > 0)
			g_hash_table_insert (xlsx_prop_type_map,
				(gpointer)map[i].gsf_key,
				(gpointer)map[i].xlsx_type);
	}

	return g_hash_table_lookup (xlsx_prop_type_map, name);
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

	/* Handle specially */
	if (0 == strcmp (prop_name, GSF_META_NAME_KEYWORDS)) {
		GValueArray *va;
		unsigned i;
		char *str;

		if (G_TYPE_STRING == G_VALUE_TYPE (val)) {
			str = g_value_dup_string (val);
			if (str && *str) {
				gsf_xml_out_start_element (output, "cp:keywords");
				gsf_xml_out_add_cstr (output, NULL, str);
				gsf_xml_out_end_element (output);
			}
			g_free (str);
		} else if (NULL != (va = gsf_value_get_docprop_varray (val))) {
			gsf_xml_out_start_element (output, "cp:keywords");
			for (i = 0 ; i < va->n_values; i++) {
				if (i!=0)
					gsf_xml_out_add_cstr_unchecked (output, NULL, " ");
				str = g_value_dup_string (g_value_array_get_nth	(va, i));
				gsf_xml_out_add_cstr (output, NULL, str);
				/* In Edition 2 we would be allowed to have different */
				/* sets of keywords depending on laguage */
				g_free (str);
			}
			gsf_xml_out_end_element (output);
		}
		return;
	}

	if (NULL != (mapped_name = xlsx_map_prop_name (prop_name))) {
		char const *mapped_type = xlsx_map_prop_type (prop_name);

		gsf_xml_out_start_element (output, mapped_name);
		if (mapped_type != NULL)
			gsf_xml_out_add_cstr_unchecked (output, "xsi:type", mapped_type);
		
		if (NULL != val)
			gsf_xml_out_add_gvalue (output, NULL, val);
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

static void
xlsx_write_docprops (XLSXWriteState *state, GsfOutfile *root_part)
{
	GsfOutfile *docprops_dir    = (GsfOutfile *)gsf_outfile_new_child (root_part, "docProps", TRUE);

	xlsx_write_docprops_app (state, root_part, docprops_dir);
	xlsx_write_docprops_core (state, root_part, docprops_dir);
}
