/*
 * xlsx-read-docprops.c : import MS Office Open xlsx document properties.
 *
 * Copyright (C) 2011 Andreas J. Guelzow All Rights Reserved
 * (aguelzow@pyrshep.ca)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either version 2 of the
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
 */

/*
 *
 * DO * NOT * COMPILE * DIRECTLY *
 * DO * NOT * COMPILE * DIRECTLY *
 * DO * NOT * COMPILE * DIRECTLY *
 *
 * included via xlsx-read.c
 **/

static void
xlsx_read_core_keys (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	gchar **strs, **orig_strs;
	GsfDocPropVector *keywords;
	GValue v = G_VALUE_INIT;
	int count = 0;

	if (*xin->content->str == 0)
		return;

	orig_strs = strs = g_strsplit (xin->content->str, " ", 0);
	keywords = gsf_docprop_vector_new ();

	while (strs != NULL && *strs != NULL && **strs) {
		g_value_init (&v, G_TYPE_STRING);
		g_value_set_string (&v, *strs);
		gsf_docprop_vector_append (keywords, &v);
		g_value_unset (&v);
		count ++;
		strs++;
	}
	g_strfreev(orig_strs);

	if (count > 0) {
		GValue *val = g_new0 (GValue, 1);
		g_value_init (val, GSF_DOCPROP_VECTOR_TYPE);
		g_value_set_object (val, keywords);
		gsf_doc_meta_data_insert (state->metadata,
					  g_strdup (xin->node->user_data.v_str), val);
	}
	g_object_unref (keywords);

	maybe_update_progress (xin);
}

static void
xlsx_read_prop_type (GsfXMLIn *xin, GType g_type)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	GValue *res = g_new0 (GValue, 1);
	if (gsf_xml_gvalue_from_str (res, g_type, xin->content->str))
		gsf_doc_meta_data_insert
			(state->metadata,
			 g_strdup (xin->node->user_data.v_str), res);
	else
		g_free (res);
}

static void
xlsx_read_prop (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	xlsx_read_prop_type (xin, G_TYPE_STRING);
}
static void
xlsx_read_prop_dt (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	xlsx_read_prop_type (xin, GSF_TIMESTAMP_TYPE);
}

static void
xlsx_read_prop_int (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	xlsx_read_prop_type (xin, G_TYPE_INT);
}

static void
xlsx_read_prop_boolean (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	xlsx_read_prop_type (xin, G_TYPE_BOOLEAN);
}

static void
xlsx_read_property_begin (GsfXMLIn *xin, xmlChar const **attrs)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	xmlChar const *fmt_id = NULL, *pid = NULL, *name = NULL;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (0 == strcmp (attrs[0], "fmtid"))
			fmt_id = attrs[1];
		else if (0 == strcmp (attrs[0], "pid"))
			pid = attrs[1];
		else if (0 == strcmp (attrs[0], "name"))
			name = attrs[1];
	if (name != NULL)
		state->meta_prop_name = g_strdup (name);
	else
		state->meta_prop_name = g_strdup_printf ("%s-%s", fmt_id, pid);
}

static void
xlsx_read_property_end (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	g_free (state->meta_prop_name);
	state->meta_prop_name = NULL;
	maybe_update_progress (xin);
}

static void
xlsx_read_custom_property_type (GsfXMLIn *xin, GType g_type)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	GValue *res;

	if (state->meta_prop_name == NULL) {
		xlsx_warning (xin, _("Corrupt file: Second child element in custom property encountered."));
		return;
	}

	res = g_new0 (GValue, 1);
	if (gsf_xml_gvalue_from_str (res, g_type, xin->content->str)) {
		gsf_doc_meta_data_insert
			(state->metadata,
			 state->meta_prop_name, res);
		state->meta_prop_name = NULL;
	} else
		g_free (res);

	maybe_update_progress (xin);
}

static void
xlsx_read_custom_property (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	xlsx_read_custom_property_type (xin, xin->node->user_data.v_int);
}

static void
xlsx_read_property_date (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	xlsx_read_custom_property_type (xin, GSF_TIMESTAMP_TYPE);
}

static GsfXMLInNode const xlsx_docprops_custom_dtd[] = {
GSF_XML_IN_NODE_FULL (START, START, -1, NULL, GSF_XML_NO_CONTENT, FALSE, TRUE, NULL, NULL, 0),
GSF_XML_IN_NODE_FULL (START, CUSTOM_PROPS, XL_NS_PROP_CUSTOM, "Properties", GSF_XML_NO_CONTENT, FALSE, TRUE, NULL, NULL, 0),
GSF_XML_IN_NODE (CUSTOM_PROPS, CUSTOM_PROP, XL_NS_PROP_CUSTOM, "property", GSF_XML_NO_CONTENT, &xlsx_read_property_begin, &xlsx_read_property_end),
GSF_XML_IN_NODE_FULL (CUSTOM_PROP, CUSTOM_PROP_LPWSTR, XL_NS_PROP_VT, "lpwstr", GSF_XML_CONTENT, FALSE, FALSE, NULL, &xlsx_read_custom_property, G_TYPE_STRING),
GSF_XML_IN_NODE_FULL (CUSTOM_PROP, CUSTOM_PROP_LPSTR, XL_NS_PROP_VT, "lpstr", GSF_XML_CONTENT,  FALSE, FALSE, NULL, &xlsx_read_custom_property, G_TYPE_STRING),
GSF_XML_IN_NODE_FULL (CUSTOM_PROP, CUSTOM_PROP_I4, XL_NS_PROP_VT, "i4", GSF_XML_CONTENT,  FALSE, FALSE, NULL, &xlsx_read_custom_property, G_TYPE_INT),
GSF_XML_IN_NODE_FULL (CUSTOM_PROP, CUSTOM_PROP_BOOL, XL_NS_PROP_VT, "bool", GSF_XML_CONTENT,  FALSE, FALSE, NULL, &xlsx_read_custom_property, G_TYPE_BOOLEAN),
GSF_XML_IN_NODE (CUSTOM_PROP, CUSTOM_PROP_DATE, XL_NS_PROP_VT, "date", GSF_XML_CONTENT, NULL, &xlsx_read_property_date),
GSF_XML_IN_NODE_END
};

static GsfXMLInNode const xlsx_docprops_extended_dtd[] = {
GSF_XML_IN_NODE_FULL (START, START, -1, NULL, GSF_XML_NO_CONTENT, FALSE, TRUE, NULL, NULL, 0),
GSF_XML_IN_NODE_FULL (START, X_PROPS, XL_NS_PROP, "Properties", GSF_XML_NO_CONTENT, FALSE, TRUE, NULL, NULL, 0),
GSF_XML_IN_NODE_FULL (X_PROPS, X_PROP_TEMPLATE, XL_NS_PROP, "Template", GSF_XML_CONTENT, FALSE, FALSE, NULL, &xlsx_read_prop, .v_str = GSF_META_NAME_TEMPLATE),
GSF_XML_IN_NODE_FULL (X_PROPS, X_PROP_MANAGER, XL_NS_PROP, "Manager", GSF_XML_CONTENT, FALSE, FALSE, NULL, &xlsx_read_prop, .v_str = GSF_META_NAME_MANAGER),
GSF_XML_IN_NODE_FULL (X_PROPS, X_PROP_COMPANY, XL_NS_PROP, "Company", GSF_XML_CONTENT, FALSE, FALSE, NULL, &xlsx_read_prop, .v_str = GSF_META_NAME_COMPANY),
GSF_XML_IN_NODE_FULL (X_PROPS, X_PROP_PAGES, XL_NS_PROP, "Pages", GSF_XML_CONTENT, FALSE, FALSE, NULL, &xlsx_read_prop_int, .v_str = GSF_META_NAME_PAGE_COUNT),
GSF_XML_IN_NODE_FULL (X_PROPS, X_PROP_WORDS, XL_NS_PROP, "Words", GSF_XML_CONTENT, FALSE, FALSE, NULL, &xlsx_read_prop_int, .v_str = GSF_META_NAME_WORD_COUNT),
GSF_XML_IN_NODE_FULL (X_PROPS, X_PROP_CHARACTERS, XL_NS_PROP, "Characters", GSF_XML_CONTENT, FALSE, FALSE, NULL, &xlsx_read_prop_int, .v_str = GSF_META_NAME_CHARACTER_COUNT),
GSF_XML_IN_NODE_FULL (X_PROPS, X_PROP_PRESENTATION_FORMAT, XL_NS_PROP, "PresentationFormat", GSF_XML_CONTENT, FALSE, FALSE, NULL, &xlsx_read_prop, .v_str = GSF_META_NAME_PRESENTATION_FORMAT),
GSF_XML_IN_NODE_FULL (X_PROPS, X_PROP_LINES, XL_NS_PROP, "Lines", GSF_XML_CONTENT, FALSE, FALSE, NULL, &xlsx_read_prop_int, .v_str = GSF_META_NAME_LINE_COUNT),
GSF_XML_IN_NODE_FULL (X_PROPS, X_PROP_PARAGRAPHS, XL_NS_PROP, "Paragraphs", GSF_XML_CONTENT, FALSE, FALSE, NULL, &xlsx_read_prop_int, .v_str = GSF_META_NAME_PARAGRAPH_COUNT),
GSF_XML_IN_NODE_FULL (X_PROPS, X_PROP_SLIDES, XL_NS_PROP, "Slides", GSF_XML_CONTENT, FALSE, FALSE, NULL, &xlsx_read_prop_int, .v_str = GSF_META_NAME_SLIDE_COUNT),
GSF_XML_IN_NODE_FULL (X_PROPS, X_PROP_NOTES, XL_NS_PROP, "Notes", GSF_XML_CONTENT, FALSE, FALSE, NULL, &xlsx_read_prop, .v_str = GSF_META_NAME_NOTE_COUNT),
GSF_XML_IN_NODE_FULL (X_PROPS, X_PROP_TOTAL_TIME, XL_NS_PROP, "TotalTime", GSF_XML_CONTENT, FALSE, FALSE, NULL, &xlsx_read_prop_int, .v_str = GSF_META_NAME_EDITING_DURATION),
GSF_XML_IN_NODE_FULL (X_PROPS, X_PROP_HIDDEN_SLIDES, XL_NS_PROP, "HiddenSlides", GSF_XML_CONTENT, FALSE, FALSE, NULL, &xlsx_read_prop_int, .v_str = GSF_META_NAME_HIDDEN_SLIDE_COUNT),
GSF_XML_IN_NODE_FULL (X_PROPS, X_PROP_MMCLIPS, XL_NS_PROP, "MMClips", GSF_XML_CONTENT, FALSE, FALSE, NULL, &xlsx_read_prop_int, .v_str = GSF_META_NAME_MM_CLIP_COUNT),
GSF_XML_IN_NODE_FULL (X_PROPS, X_PROP_SCALE_CROP, XL_NS_PROP, "ScaleCrop", GSF_XML_CONTENT, FALSE, FALSE, NULL, &xlsx_read_prop_boolean, .v_str = GSF_META_NAME_SCALE),
GSF_XML_IN_NODE_FULL (X_PROPS, X_PROP_LINKS_UP_TO_DATE, XL_NS_PROP, "LinksUpToDate", GSF_XML_CONTENT, FALSE, FALSE, NULL, &xlsx_read_prop_boolean, .v_str = GSF_META_NAME_LINKS_DIRTY),
GSF_XML_IN_NODE_FULL (X_PROPS, X_PROP_CHARACTERS_WITH_SPACES, XL_NS_PROP, "CharactersWithSpaces", GSF_XML_CONTENT, FALSE, FALSE, NULL, &xlsx_read_prop_int, .v_str = GSF_META_NAME_BYTE_COUNT),
GSF_XML_IN_NODE_FULL (X_PROPS, X_PROP_SHARED_DOC, XL_NS_PROP, "SharedDoc", GSF_XML_CONTENT, FALSE, FALSE, NULL, &xlsx_read_prop_boolean, .v_str = "xlsx:SharedDoc"),
GSF_XML_IN_NODE_FULL (X_PROPS, X_PROP_HYPERLINK_BASE, XL_NS_PROP, "HyperlinkBase", GSF_XML_CONTENT, FALSE, FALSE, NULL, &xlsx_read_prop, .v_str = "xlsx:HyperlinkBase"),
GSF_XML_IN_NODE_FULL (X_PROPS, X_PROP_HYPERLINKS_CHANGED, XL_NS_PROP, "HyperlinksChanged", GSF_XML_CONTENT, FALSE, FALSE, NULL, &xlsx_read_prop_boolean, .v_str = "xlsx:HyperlinksChanged"),
GSF_XML_IN_NODE_FULL (X_PROPS, X_PROP_DOC_SECURITY, XL_NS_PROP, "DocSecurity", GSF_XML_CONTENT, FALSE, FALSE, NULL, &xlsx_read_prop_int, .v_str = GSF_META_NAME_SECURITY),
GSF_XML_IN_NODE_FULL (X_PROPS, X_PROP_DIG_SIG, XL_NS_PROP, "DigSig", GSF_XML_CONTENT, FALSE, FALSE, NULL, NULL, .v_str = ""),
GSF_XML_IN_NODE_FULL (X_PROPS, X_PROP_HEADING_PAIRS, XL_NS_PROP, "HeadingPairs", GSF_XML_NO_CONTENT, FALSE, FALSE, NULL, NULL, .v_str = ""),
GSF_XML_IN_NODE (X_PROP_HEADING_PAIRS, X_PROP_SUB_VECTOR, XL_NS_PROP_VT, "vector", GSF_XML_NO_CONTENT, NULL, NULL),
GSF_XML_IN_NODE (X_PROP_SUB_VECTOR, X_PROP_SUB_LPWSTR, XL_NS_PROP_VT, "lpwstr", GSF_XML_NO_CONTENT, NULL, NULL),
GSF_XML_IN_NODE (X_PROP_SUB_VECTOR, X_PROP_SUB_LPSTR, XL_NS_PROP_VT, "lpstr", GSF_XML_NO_CONTENT, NULL, NULL),
GSF_XML_IN_NODE (X_PROP_SUB_VECTOR, X_PROP_SUB_I4, XL_NS_PROP_VT, "i4", GSF_XML_NO_CONTENT, NULL, NULL),
GSF_XML_IN_NODE (X_PROP_SUB_VECTOR, X_PROP_SUB_VARIANT, XL_NS_PROP_VT, "variant", GSF_XML_NO_CONTENT, NULL, NULL),
GSF_XML_IN_NODE (X_PROP_SUB_VARIANT, X_PROP_SUB_LPWSTR, XL_NS_PROP_VT, "lpwstr", GSF_XML_2ND, NULL, NULL),
GSF_XML_IN_NODE (X_PROP_SUB_VARIANT, X_PROP_SUB_LPSTR, XL_NS_PROP_VT, "lpstr", GSF_XML_2ND, NULL, NULL),
GSF_XML_IN_NODE (X_PROP_SUB_VARIANT, X_PROP_SUB_I4, XL_NS_PROP_VT, "i4", GSF_XML_2ND, NULL, NULL),
GSF_XML_IN_NODE_FULL (X_PROPS, X_PROP_HLINKS, XL_NS_PROP, "HLinks", GSF_XML_NO_CONTENT, FALSE, FALSE, NULL, NULL, .v_str = ""),
GSF_XML_IN_NODE (X_PROP_HLINKS, X_PROP_SUB_VECTOR, XL_NS_PROP_VT, "vector", GSF_XML_2ND, NULL, NULL),
GSF_XML_IN_NODE_FULL (X_PROPS, X_PROP_TITLES_OF_PARTS, XL_NS_PROP, "TitlesOfParts", GSF_XML_NO_CONTENT, FALSE, FALSE, NULL, NULL, .v_str = ""),
GSF_XML_IN_NODE (X_PROP_TITLES_OF_PARTS, X_PROP_SUB_VECTOR, XL_NS_PROP_VT, "vector", GSF_XML_2ND, NULL, NULL),
GSF_XML_IN_NODE_FULL (X_PROPS, X_PROP_APPLICATION, XL_NS_PROP, "Application", GSF_XML_CONTENT, FALSE, FALSE, NULL, NULL, .v_str = ""),
GSF_XML_IN_NODE_FULL (X_PROPS, X_PROP_APP_VERSION, XL_NS_PROP, "AppVersion", GSF_XML_CONTENT, FALSE, FALSE, NULL, NULL, .v_str = ""),
GSF_XML_IN_NODE_END
};

static GsfXMLInNode const xlsx_docprops_core_dtd[] = {
GSF_XML_IN_NODE_FULL (START, START, -1, NULL, GSF_XML_NO_CONTENT, FALSE, TRUE, NULL, NULL, 0),
GSF_XML_IN_NODE_FULL (START, CORE_PROPS, XL_NS_PROP_CP, "coreProperties", GSF_XML_NO_CONTENT, FALSE, TRUE, NULL, NULL, 0),
GSF_XML_IN_NODE_FULL (CORE_PROPS, PROP_CATEGORY, XL_NS_PROP_CP, "category", GSF_XML_CONTENT, FALSE, FALSE, NULL, &xlsx_read_prop, .v_str = GSF_META_NAME_CATEGORY),
GSF_XML_IN_NODE_FULL (CORE_PROPS, PROP_CONTENT_STATUS, XL_NS_PROP_CP, "contentStatus", GSF_XML_CONTENT, FALSE, FALSE, NULL, &xlsx_read_prop, .v_str = "cp:contentStatus"),
GSF_XML_IN_NODE_FULL (CORE_PROPS, PROP_CONTENT_TYPE, XL_NS_PROP_CP, "contentType", GSF_XML_CONTENT, FALSE, FALSE, NULL, &xlsx_read_prop, .v_str = "cp:contentType"),
GSF_XML_IN_NODE_FULL (CORE_PROPS, PROP_KEYWORDS, XL_NS_PROP_CP, "keywords", GSF_XML_CONTENT, FALSE, FALSE, NULL, &xlsx_read_core_keys, .v_str = GSF_META_NAME_KEYWORDS),
GSF_XML_IN_NODE_FULL (CORE_PROPS, PROP_LAST_NODIFIED_BY, XL_NS_PROP_CP, "lastModifiedBy", GSF_XML_CONTENT, FALSE, FALSE, NULL, &xlsx_read_prop, .v_str = GSF_META_NAME_CREATOR),
GSF_XML_IN_NODE_FULL (CORE_PROPS, PROP_LAST_PRINTED, XL_NS_PROP_CP, "lastPrinted", GSF_XML_CONTENT, FALSE, FALSE, NULL, &xlsx_read_prop_dt, .v_str = GSF_META_NAME_PRINT_DATE),
GSF_XML_IN_NODE_FULL (CORE_PROPS, PROP_REVISION, XL_NS_PROP_CP, "revision", GSF_XML_CONTENT, FALSE, FALSE, NULL, &xlsx_read_prop, .v_str = GSF_META_NAME_REVISION_COUNT),
GSF_XML_IN_NODE_FULL (CORE_PROPS, PROP_VERSION, XL_NS_PROP_CP, "version", GSF_XML_CONTENT, FALSE, FALSE, NULL, &xlsx_read_prop, .v_str = "cp:version"),
GSF_XML_IN_NODE_FULL (CORE_PROPS, PROP_CREATOR, XL_NS_PROP_DC, "creator", GSF_XML_CONTENT, FALSE, FALSE, NULL, &xlsx_read_prop, .v_str = GSF_META_NAME_INITIAL_CREATOR),
GSF_XML_IN_NODE_FULL (CORE_PROPS, PROP_DESCRIPTION, XL_NS_PROP_DC, "description", GSF_XML_CONTENT, FALSE, FALSE, NULL, &xlsx_read_prop, .v_str = GSF_META_NAME_DESCRIPTION),
GSF_XML_IN_NODE_FULL (CORE_PROPS, PROP_IDENTIFIER, XL_NS_PROP_DC, "identifier", GSF_XML_CONTENT, FALSE, FALSE, NULL, &xlsx_read_prop, .v_str = "dc:identifier"),
GSF_XML_IN_NODE_FULL (CORE_PROPS, PROP_LANGUAGE, XL_NS_PROP_DC, "language", GSF_XML_CONTENT, FALSE, FALSE, NULL, &xlsx_read_prop, .v_str = GSF_META_NAME_LANGUAGE),
GSF_XML_IN_NODE_FULL (CORE_PROPS, PROP_SUBJECT, XL_NS_PROP_DC, "subject", GSF_XML_CONTENT, FALSE, FALSE, NULL, &xlsx_read_prop, .v_str = GSF_META_NAME_SUBJECT),
GSF_XML_IN_NODE_FULL (CORE_PROPS, PROP_TITLE, XL_NS_PROP_DC, "title", GSF_XML_CONTENT, FALSE, FALSE, NULL, &xlsx_read_prop, .v_str = GSF_META_NAME_TITLE),
GSF_XML_IN_NODE_FULL (CORE_PROPS, PROP_CREATED, XL_NS_PROP_DCTERMS, "created", GSF_XML_CONTENT, FALSE, FALSE, NULL, &xlsx_read_prop_dt, .v_str = GSF_META_NAME_DATE_CREATED),
GSF_XML_IN_NODE_FULL (CORE_PROPS, PROP_MODIFIED, XL_NS_PROP_DCTERMS, "modified", GSF_XML_CONTENT, FALSE, FALSE, NULL, &xlsx_read_prop_dt, .v_str = GSF_META_NAME_DATE_MODIFIED),
GSF_XML_IN_NODE_END
};

static void
xlsx_read_docprops_core (XLSXReadState *state)
{
	GsfInput *in;
	/* optional */
	in = gsf_open_pkg_open_rel_by_type
		(GSF_INPUT (state->zip),
		 "http://schemas.openxmlformats.org/package/2006/relationships/metadata/"
		 "core-properties", NULL);

	if (in == NULL) return;

	start_update_progress (state, in, _("Reading core properties..."), 0.9, 0.94);
	xlsx_parse_stream (state, in, xlsx_docprops_core_dtd);
	end_update_progress (state);
}

static void
xlsx_read_docprops_extended (XLSXReadState *state)
{
	GsfInput *in;
	/* optional */
	in = gsf_open_pkg_open_rel_by_type
		(GSF_INPUT (state->zip),
		 "http://schemas.openxmlformats.org/officeDocument/2006/relationships/"
		 "extended-properties", NULL);

	if (in == NULL) return;

	start_update_progress (state, in, _("Reading extended properties..."), 0.94, 0.97);
	xlsx_parse_stream (state, in, xlsx_docprops_extended_dtd);
	end_update_progress (state);
}

static void
xlsx_read_docprops_custom (XLSXReadState *state)
{
	GsfInput *in;
	/* optional */
	in = gsf_open_pkg_open_rel_by_type
		(GSF_INPUT (state->zip),
		 "http://schemas.openxmlformats.org/officeDocument/2006/relationships/"
		 "custom-properties", NULL);

	if (in == NULL) return;

	start_update_progress (state, in, _("Reading custom properties..."), 0.97, 1.);
	xlsx_parse_stream (state, in, xlsx_docprops_custom_dtd);
	end_update_progress (state);
}

static void
xlsx_read_docprops (XLSXReadState *state)
{
	state->metadata = gsf_doc_meta_data_new ();

	xlsx_read_docprops_core (state);
	xlsx_read_docprops_extended (state);
	xlsx_read_docprops_custom (state);

	go_doc_set_meta_data (GO_DOC (state->wb), state->metadata);
	g_object_unref (state->metadata);
	state->metadata = NULL;
}

