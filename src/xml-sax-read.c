/*
 * xml-sax-read.c : a sax based parser.
 *
 * Copyright (C) 2000-2007 Jody Goldberg (jody@gnome.org)
 * Copyright (C) 2007-2024 Morten Welinder (terra@gnome.org)
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

#include <gnumeric-config.h>
#include <gnumeric.h>
#include <xml-sax.h>
#include <xml-io-version.h>
#include <gnm-plugin.h>
#include <sheet-view.h>
#include <sheet-style.h>
#include <sheet-merge.h>
#include <sheet-filter.h>
#include <sheet.h>
#include <ranges.h>
#include <tools/gnm-solver.h>
#include <tools/scenarios.h>
#include <style.h>
#include <style-border.h>
#include <style-color.h>
#include <style-conditions.h>
#include <validation.h>
#include <hlink.h>
#include <input-msg.h>
#include <cell.h>
#include <position.h>
#include <expr.h>
#include <expr-name.h>
#include <print-info.h>
#include <value.h>
#include <selection.h>
#include <command-context.h>
#include <workbook-view.h>
#include <workbook-control.h>
#include <workbook.h>
#include <sheet-object-impl.h>
#include <sheet-object-cell-comment.h>
#include <gnm-so-line.h>
#include <gnm-so-filled.h>
#include <gnm-so-path.h>
#include <gnm-format.h>
#include <sheet-object-graph.h>
#include <sheet-object-component.h>
#include <application.h>
#include <gutils.h>
#include <clipboard.h>
#include <number-match.h>

#include <goffice/goffice.h>

#include <gsf/gsf-libxml.h>
#include <gsf/gsf-input.h>
#include <gsf/gsf-input-memory.h>
#include <gsf/gsf-input-gzip.h>
#include <gsf/gsf-opendoc-utils.h>
#include <gsf/gsf-utils.h>
#include <glib/gi18n-lib.h>
#include <libxml/tree.h>
#include <libxml/parser.h>
#include <libxml/parserInternals.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

/* libgsf defines OO_NS_OFFICE to be 0, so we need to take something different for GNM */
#define GNM		100

static void
xml_sax_barf (const char *locus, const char *reason)
{
	g_warning ("File is most likely corrupted.\n"
		   "The problem was detected in %s.\n"
		   "The failed check was: %s",
		   locus, reason);
}

#define XML_CHECK3(_cond_,_code_,_reason_)		\
  do {							\
	  if (G_UNLIKELY(!(_cond_))) {			\
		  xml_sax_barf (G_STRFUNC, _reason_);	\
		  _code_;				\
		  return;				\
	  }						\
  } while (0)

#define XML_CHECK(_cond_) XML_CHECK3(_cond_,{},#_cond_)
#define XML_CHECK2(_cond_,_code_) XML_CHECK3(_cond_,_code_,#_cond_)


#define CXML2C(s) ((char const *)(s))

static inline gboolean
attr_eq (const xmlChar *a, const char *s)
{
	return !strcmp (CXML2C (a), s);
}

static GOFormat *
make_format (const char *str)
{
	GOFormat *res =
		gnm_format_import (str,
				   GNM_FORMAT_IMPORT_NULL_INVALID |
				   GNM_FORMAT_IMPORT_PATCHUP_INCOMPLETE);
	if (!res) {
		g_warning ("Ignoring invalid format [%s]", str);
		return NULL;
	}

	return res;
}

/*****************************************************************************/

gboolean
gnm_xml_attr_double (xmlChar const * const *attrs, char const *name, double * res)
{
	char *end;
	double tmp;

	g_return_val_if_fail (attrs != NULL, FALSE);
	g_return_val_if_fail (attrs[0] != NULL, FALSE);
	g_return_val_if_fail (attrs[1] != NULL, FALSE);

	if (!attr_eq (attrs[0], name))
		return FALSE;

	tmp = go_strtod (CXML2C (attrs[1]), &end);
	if (*end) {
		g_warning ("Invalid attribute '%s', expected double, received '%s'",
			   name, CXML2C (attrs[1]));
		return FALSE;
	}
	*res = tmp;
	return TRUE;
}

static gboolean
xml_sax_double (xmlChar const *chars, double *res)
{
	char *end;
	*res = go_strtod (CXML2C (chars), &end);
	return *end == '\0';
}

static gboolean
gnm_xml_attr_float (xmlChar const * const *attrs, char const *name, gnm_float* res)
{
	char *end;
	gnm_float tmp;

	g_return_val_if_fail (attrs != NULL, FALSE);
	g_return_val_if_fail (attrs[0] != NULL, FALSE);
	g_return_val_if_fail (attrs[1] != NULL, FALSE);

	if (!attr_eq (attrs[0], name))
		return FALSE;

	tmp = gnm_strto (CXML2C (attrs[1]), &end);
	if (*end) {
		g_warning ("Invalid attribute '%s', expected double, received '%s'",
			   name, CXML2C (attrs[1]));
		return FALSE;
	}
	*res = tmp;
	return TRUE;
}

gboolean
gnm_xml_attr_bool (xmlChar const * const *attrs, char const *name, gboolean *res)
{
	g_return_val_if_fail (attrs != NULL, FALSE);
	g_return_val_if_fail (attrs[0] != NULL, FALSE);
	g_return_val_if_fail (attrs[1] != NULL, FALSE);

	if (!attr_eq (attrs[0], name))
		return FALSE;

	*res = g_ascii_strcasecmp (CXML2C (attrs[1]), "false") && strcmp (CXML2C (attrs[1]), "0");

	return TRUE;
}

gboolean
gnm_xml_attr_int (xmlChar const * const *attrs, char const *name, int *res)
{
	char *end;
	long tmp;

	g_return_val_if_fail (attrs != NULL, FALSE);
	g_return_val_if_fail (attrs[0] != NULL, FALSE);
	g_return_val_if_fail (attrs[1] != NULL, FALSE);

	if (!attr_eq (attrs[0], name))
		return FALSE;

	errno = 0;
	tmp = strtol (CXML2C (attrs[1]), &end, 10);
	if (*end || errno) {
		g_warning ("Invalid attribute '%s', expected integer, received '%s'",
			   name, attrs[1]);
		return FALSE;
	}
	*res = tmp;
	return TRUE;
}

/* NOT SUITABLE FOR HIGH VOLUME VALUES
 * Checking both name and nick gets expensive */
static gboolean
xml_sax_attr_enum (xmlChar const * const *attrs,
		   char const *name,
		   GType etype,
		   gint *val)
{
	GEnumClass *eclass;
	GEnumValue *ev;
	int i;

	g_return_val_if_fail (attrs != NULL, FALSE);
	g_return_val_if_fail (attrs[0] != NULL, FALSE);
	g_return_val_if_fail (attrs[1] != NULL, FALSE);

	if (!attr_eq (attrs[0], name))
		return FALSE;

	eclass = G_ENUM_CLASS (g_type_class_ref (etype));
	ev = g_enum_get_value_by_name (eclass, CXML2C (attrs[1]));
	if (!ev) ev = g_enum_get_value_by_nick (eclass, CXML2C (attrs[1]));
	g_type_class_unref (eclass);

	if (!ev && gnm_xml_attr_int (attrs, name, &i))
		/* Check that the value is valid.  */
		ev = g_enum_get_value (eclass, i);
	if (!ev) return FALSE;

	*val = ev->value;
	return TRUE;
}


static gboolean
xml_sax_attr_cellpos (xmlChar const * const *attrs, char const *name, GnmCellPos *val, Sheet const *sheet)
{
	g_return_val_if_fail (attrs != NULL, FALSE);
	g_return_val_if_fail (attrs[0] != NULL, FALSE);
	g_return_val_if_fail (attrs[1] != NULL, FALSE);

	if (!attr_eq (attrs[0], name))
		return FALSE;

	if (cellpos_parse (CXML2C (attrs[1]), gnm_sheet_get_size (sheet), val, TRUE) == NULL) {
		g_warning ("Invalid attribute '%s', expected cellpos, received '%s'",
			   name, attrs[1]);
		return FALSE;
	}
	return TRUE;
}

static gboolean
xml_sax_attr_color (xmlChar const * const *attrs, char const *name, GnmColor **res)
{
	unsigned int red, green, blue, alpha = 0xffff;

	g_return_val_if_fail (attrs != NULL, FALSE);
	g_return_val_if_fail (attrs[0] != NULL, FALSE);
	g_return_val_if_fail (attrs[1] != NULL, FALSE);

	if (!attr_eq (attrs[0], name))
		return FALSE;

	if (sscanf (CXML2C (attrs[1]), "%X:%X:%X:%X", &red, &green, &blue, &alpha) < 3){
		g_warning ("Invalid attribute '%s', expected colour, received '%s'",
			   name, attrs[1]);
		return FALSE;
	}
	*res = gnm_color_new_rgba16 (red, green, blue, alpha);
	return TRUE;
}

static gboolean
xml_sax_attr_range (xmlChar const * const *attrs, GnmRange *res)
{
	int flags = 0;

	g_return_val_if_fail (attrs != NULL, FALSE);

	for (; attrs[0] && attrs[1] ; attrs += 2)
		if (gnm_xml_attr_int (attrs, "startCol", &res->start.col))
			flags |= 0x1;
		else if (gnm_xml_attr_int (attrs, "startRow", &res->start.row))
			flags |= 0x2;
		else if (gnm_xml_attr_int (attrs, "endCol", &res->end.col))
			flags |= 0x4;
		else if (gnm_xml_attr_int (attrs, "endRow", &res->end.row))
			flags |= 0x8;
		else
			return FALSE;

	return flags == 0xf;
}

/*****************************************************************************/

typedef enum {
	READ_FULL_FILE,
	READ_CLIPBOARD,
	READ_STYLE
} ReadFileWhat;


typedef struct {
	GsfXMLIn base;

	GOIOContext	*context;	/* The IOcontext managing things */
	WorkbookView	*wb_view;	/* View for the new workbook */
	Workbook	*wb;		/* The new workbook */
	GnumericXMLVersion version;
	gsf_off_t last_progress_update;
	GnmConventions *convs;
	gboolean do_progress;

	Sheet *sheet;
	double sheet_zoom;

	/* Only valid while parsing attributes */
	struct {
		char *name;
		char *value;
	} attribute;

	/* Only valid when parsing wb or sheet names */
	struct {
		char *name;
		char *value;
		char *position;
	} name;

	struct {
		char            *title;
		char            *msg;
		GnmExprTop const *texpr[2];
		ValidationStyle  style;
		ValidationType	 type;
		ValidationOp	 op;
		gboolean	 allow_blank;
		gboolean	 use_dropdown;
	} validation;

	GnmStyleCond *cond;
	GnmStyle *cond_save_style;

	gboolean  style_range_init;
	GnmRange	  style_range;
	GnmStyle   *style;

	GnmCellPos cell;
	gboolean seen_cell_contents;
	int expr_id, array_rows, array_cols;
	int value_type;
	GOFormat *value_fmt;
	char *value_result;

	GnmScenario *scenario;
	GnmValue *scenario_range;

	GnmFilter *filter;

	int display_formulas;
	int hide_zero;
	int hide_grid;
	int hide_col_header;
	int hide_row_header;
	int display_outlines;
	int outline_symbols_below;
	int outline_symbols_right;
	int text_is_rtl;
	int is_protected;
	char *expr_conv_name;
	GnmSheetVisibility visibility;
	GnmColor *tab_color;
	GnmColor *tab_text_color;
	GnmColor *grid_color;

	/* expressions with ref > 1 a map from index -> expr pointer */
	GHashTable *expr_map;
	GList *delayed_names;
	SheetObject *so;

	int sheet_rows, sheet_cols;
	GnmSheetType sheet_type;

	GnmPageBreaks *page_breaks;

	GnmCellRegion *clipboard;

	GnmXmlStyleHandler style_handler;
	gpointer style_handler_user;
	GsfXMLInDoc *style_handler_doc;
} XMLSaxParseState;

static void
maybe_update_progress (GsfXMLIn *xin)
{
	XMLSaxParseState *state = (XMLSaxParseState *)xin->user_state;
	GsfInput *input = gsf_xml_in_get_input (xin);
	gsf_off_t pos = gsf_input_tell (input);

	if (state->do_progress && pos >= state->last_progress_update + 10000) {
		go_io_value_progress_update (state->context, pos);
		state->last_progress_update = pos;
	}
}

/**
 * gnm_xml_in_cur_obj:
 * @xin: #GsfXMLIn
 *
 * Returns: (transfer none): the current sheet object.
 **/
SheetObject *
gnm_xml_in_cur_obj (GsfXMLIn const *xin)
{
	XMLSaxParseState *state = (XMLSaxParseState *)xin->user_state;
	return state->so;
}

/**
 * gnm_xml_in_cur_sheet:
 * @xin: #GsfXMLIn
 *
 * Returns: (transfer none): the current sheet.
 **/
Sheet *
gnm_xml_in_cur_sheet (GsfXMLIn const *xin)
{
	XMLSaxParseState *state = (XMLSaxParseState *)xin->user_state;
	return state->sheet;
}

static void
gnm_xml_finish_obj (GsfXMLIn *xin, XMLSaxParseState *state)
{
	GnmCellRegion *cr = state->clipboard;

	if (cr) {
		cr->objects = g_slist_prepend (cr->objects, state->so);
	} else {
		sheet_object_set_sheet (state->so, state->sheet);
		g_object_unref (state->so);
	}

	state->so = NULL;
}

/****************************************************************************/

static void
unknown_attr (GsfXMLIn *xin, xmlChar const * const *attrs)
{
	XMLSaxParseState *state = (XMLSaxParseState *)xin->user_state;

	if (state->version == GNM_XML_LATEST)
		go_io_warning (state->context,
			_("Unexpected attribute %s::%s == '%s'."),
			(NULL != xin->node &&
			 NULL != xin->node->name) ?
			xin->node->name : "<unknown name>",
			attrs[0], attrs[1]);
}

static void
xml_sax_wb (GsfXMLIn *xin, xmlChar const **attrs)
{
	XMLSaxParseState *state = (XMLSaxParseState *)xin->user_state;

	/*
	 * NOTE: If we read a file with a dtd that is newer, i.e., from the
	 * future, then we will not get here!  For that reason we also muck
	 * with ->version in xml_sax_version.
	 */
	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2) {
		if (strcmp (CXML2C (attrs[0]), "xmlns:gmr") == 0 ||
		    strcmp (CXML2C (attrs[0]), "xmlns:gnm") == 0) {
			static struct {
				char const * const id;
				GnumericXMLVersion const version;
			} const GnumericVersions [] = {
				{ "http://www.gnumeric.org/v14.dtd", GNM_XML_V14 },	/* 1.12.21 */
				{ "http://www.gnumeric.org/v13.dtd", GNM_XML_V13 },	/* 1.7.7 */
				{ "http://www.gnumeric.org/v12.dtd", GNM_XML_V12 },	/* 1.7.3 */
				{ "http://www.gnumeric.org/v11.dtd", GNM_XML_V11 },	/* 1.7.0 */
				{ "http://www.gnumeric.org/v10.dtd", GNM_XML_V10 },	/* 1.0.3 */
				{ "http://www.gnumeric.org/v9.dtd", GNM_XML_V9 },	/* 0.73 */
				{ "http://www.gnumeric.org/v8.dtd", GNM_XML_V8 },	/* 0.71 */
				{ "http://www.gnome.org/gnumeric/v7", GNM_XML_V7 },	/* 0.66 */
				{ "http://www.gnome.org/gnumeric/v6", GNM_XML_V6 },	/* 0.62 */
				{ "http://www.gnome.org/gnumeric/v5", GNM_XML_V5 },
				{ "http://www.gnome.org/gnumeric/v4", GNM_XML_V4 },
				{ "http://www.gnome.org/gnumeric/v3", GNM_XML_V3 },
				{ "http://www.gnome.org/gnumeric/v2", GNM_XML_V2 },
				{ "http://www.gnome.org/gnumeric/", GNM_XML_V1 },
				{ NULL, 0}
			};
			int i;
			for (i = 0 ; GnumericVersions [i].id != NULL ; ++i )
				if (strcmp (CXML2C (attrs[1]), GnumericVersions [i].id) == 0) {
					if (state->version != GNM_XML_UNKNOWN)
						go_io_warning (state->context,
							_("Multiple version specifications.  Assuming %d"),
							state->version);
					else {
						state->version = GnumericVersions [i].version;
						break;
					}
				}
		} else if (attr_eq (attrs[0], "xmlns:xsi")) {
		} else if (attr_eq (attrs[0], "xsi:schemaLocation")) {
		} else
			unknown_attr (xin, attrs);
	}
}

static void
xml_sax_document_meta (GsfXMLIn *xin, G_GNUC_UNUSED xmlChar const **attrs)
{
	XMLSaxParseState *state = (XMLSaxParseState *)xin->user_state;

	gsf_doc_meta_data_odf_subtree (go_doc_get_meta_data (GO_DOC (state->wb)), xin);
}



static void
xml_sax_version (GsfXMLIn *xin, xmlChar const **attrs)
{
	XMLSaxParseState *state = (XMLSaxParseState *)xin->user_state;
	int epoch = -1;
	int major = -1;
	int minor = -1;
	int version;

	state->version = GNM_XML_V11;
	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2) {
		if (gnm_xml_attr_int (attrs, "Epoch", &epoch))
			/* Nothing */ ;
		else if (gnm_xml_attr_int (attrs, "Major", &major))
			/* Nothing */ ;
		else if (gnm_xml_attr_int (attrs, "Minor", &minor))
			/* Nothing */ ;
	}

	version = (epoch * 100 + major) * 100 + minor;
	if (state->version == GNM_XML_UNKNOWN && version >= 10700) {
		if (version >= 11221)
			state->version = GNM_XML_V14;
		else if (version >= 10707)
			state->version = GNM_XML_V13;
		else if (version >= 10705)
			state->version = GNM_XML_V12;
		else if (version >= 10700)
			state->version = GNM_XML_V11;
	}
}

static void
xml_sax_wb_sheetsize (GsfXMLIn *xin, xmlChar const **attrs)
{
	XMLSaxParseState *state = (XMLSaxParseState *)xin->user_state;

	/* Defaults for legacy files.  */
	state->sheet_cols = 256;
	state->sheet_rows = 65536;
	state->sheet_type = GNM_SHEET_DATA;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2) {
		if (gnm_xml_attr_int (attrs, "gnm:Cols", &state->sheet_cols))
			; /* Nothing more */
		else if (gnm_xml_attr_int (attrs, "gnm:Rows", &state->sheet_rows))
			; /* Nothing more */
		else if (!strcmp (CXML2C (attrs[0]), "gnm:SheetType") &&
			 !strcmp (CXML2C (attrs[1]), "object"))
			state->sheet_type = GNM_SHEET_OBJECT;
		else
			unknown_attr (xin, attrs);
	}
}

static void
xml_sax_wb_sheetname (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	XMLSaxParseState *state = (XMLSaxParseState *)xin->user_state;
	char const *name = xin->content->str;
	Workbook *wb = state->wb;

	g_return_if_fail (name != NULL);

	if (NULL == workbook_sheet_by_name (wb, name)) {
		Sheet *sheet;

		if (!gnm_sheet_valid_size (state->sheet_cols,
					   state->sheet_rows)) {
			gnm_sheet_suggest_size (&state->sheet_cols,
						&state->sheet_rows);
		}

		sheet = sheet_new_with_type (wb, name,
				   state->sheet_type,
				   state->sheet_cols,
				   state->sheet_rows);
		workbook_sheet_attach (wb, sheet);
	}
}

static void
xml_sax_wb_view (GsfXMLIn *xin, xmlChar const **attrs)
{
	XMLSaxParseState *state = (XMLSaxParseState *)xin->user_state;

	int sheet_index;
	int width = -1, height = -1;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (gnm_xml_attr_int (attrs, "SelectedTab", &sheet_index)) {
			Sheet *sheet = workbook_sheet_by_index (state->wb,
								sheet_index);
			if (sheet)
				wb_view_sheet_focus (state->wb_view, sheet);
		}
		else if (gnm_xml_attr_int (attrs, "Width", &width)) ;
		else if (gnm_xml_attr_int (attrs, "Height", &height)) ;
		else
			unknown_attr (xin, attrs);

	if (width > 0 && height > 0)
		wb_view_preferred_size (state->wb_view, width, height);
}
static void
xml_sax_calculation (GsfXMLIn *xin, xmlChar const **attrs)
{
	XMLSaxParseState *state = (XMLSaxParseState *)xin->user_state;
	gboolean b;
	int	 i;
	gnm_float tol;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (gnm_xml_attr_bool (attrs, "ManualRecalc", &b))
			workbook_set_recalcmode (state->wb, !b);
		else if (gnm_xml_attr_bool (attrs, "EnableIteration", &b))
			workbook_iteration_enabled (state->wb, b);
		else if (gnm_xml_attr_int  (attrs, "MaxIterations", &i))
			workbook_iteration_max_number (state->wb, i);
		else if (gnm_xml_attr_float (attrs, "IterationTolerance", &tol))
			workbook_iteration_tolerance (state->wb, tol);
		else if (strcmp (CXML2C (attrs[0]), "DateConvention") == 0) {
			GODateConventions const *date_conv =
				go_date_conv_from_str (CXML2C (attrs[1]));
			if (date_conv)
				workbook_set_date_conv (state->wb, date_conv);
			else
				g_printerr ("Ignoring invalid date conventions.\n");
		} else
			unknown_attr (xin, attrs);
}

static void
xml_sax_old_dateconvention (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	XMLSaxParseState *state = (XMLSaxParseState *)xin->user_state;
	workbook_set_1904 (state->wb, strcmp (xin->content->str, "1904") == 0);
}

static void
xml_sax_finish_parse_wb_attr (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	XMLSaxParseState *state = (XMLSaxParseState *)xin->user_state;

	if (state->attribute.name && state->attribute.value) {
		wb_view_set_attribute (state->wb_view,
				       state->attribute.name,
				       state->attribute.value);
	} else {
		xml_sax_barf (G_STRFUNC, _("workbook view attribute is incomplete"));
	}

	g_free (state->attribute.value);	state->attribute.value = NULL;
	g_free (state->attribute.name);		state->attribute.name = NULL;
}

static void
xml_sax_attr_elem (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	XMLSaxParseState *state = (XMLSaxParseState *)xin->user_state;

	char const *content = xin->content->str;
	int const len = xin->content->len;

	switch (xin->node->user_data.v_int) {
	case 0:
		g_return_if_fail (state->attribute.name == NULL);
		state->attribute.name = g_strndup (content, len);
		break;

	case 1:
		g_return_if_fail (state->attribute.value == NULL);
		state->attribute.value = g_strndup (content, len);
		break;

	default:
		g_assert_not_reached ();
	}
}

static void
xml_sax_sheet_start (GsfXMLIn *xin, xmlChar const **attrs)
{
	XMLSaxParseState *state = (XMLSaxParseState *)xin->user_state;

	gboolean tmp;
	gint tmpi;
	GnmColor *color = NULL;

	state->hide_col_header = state->hide_row_header =
		state->display_formulas = state->hide_zero =
		state->hide_grid = state->display_outlines =
		state->outline_symbols_below = state->outline_symbols_right =
		state->text_is_rtl = state->is_protected = -1;
	state->expr_conv_name = NULL;
	state->visibility = GNM_SHEET_VISIBILITY_VISIBLE;
	state->tab_color = NULL;
	state->tab_text_color = NULL;
	state->grid_color = NULL;
	state->sheet_zoom = 1.; /* default */

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (gnm_xml_attr_bool (attrs, "DisplayFormulas", &tmp))
			state->display_formulas = tmp;
		else if (gnm_xml_attr_bool (attrs, "HideZero", &tmp))
			state->hide_zero = tmp;
		else if (gnm_xml_attr_bool (attrs, "HideGrid", &tmp))
			state->hide_grid = tmp;
		else if (gnm_xml_attr_bool (attrs, "HideColHeader", &tmp))
			state->hide_col_header = tmp;
		else if (gnm_xml_attr_bool (attrs, "HideRowHeader", &tmp))
			state->hide_row_header = tmp;
		else if (gnm_xml_attr_bool (attrs, "DisplayOutlines", &tmp))
			state->display_outlines = tmp;
		else if (gnm_xml_attr_bool (attrs, "OutlineSymbolsBelow", &tmp))
			state->outline_symbols_below = tmp;
		else if (gnm_xml_attr_bool (attrs, "OutlineSymbolsRight", &tmp))
			state->outline_symbols_right = tmp;
		else if (xml_sax_attr_enum (attrs, "Visibility", GNM_SHEET_VISIBILITY_TYPE, &tmpi))
			state->visibility = tmpi;
		else if (gnm_xml_attr_bool (attrs, "RTL_Layout", &tmp))
			state->text_is_rtl = tmp;
		else if (gnm_xml_attr_bool (attrs, "Protected", &tmp))
			state->is_protected = tmp;
		else if (strcmp (CXML2C (attrs[0]), "ExprConvention") == 0)
			state->expr_conv_name = g_strdup (attrs[1]);
		else if (xml_sax_attr_color (attrs, "TabColor", &color))
			state->tab_color = color;
		else if (xml_sax_attr_color (attrs, "TabTextColor", &color))
			state->tab_text_color = color;
		else if (xml_sax_attr_color (attrs, "GridColor", &color))
			state->grid_color = color;
		else
			unknown_attr (xin, attrs);
}

static Sheet *
xml_sax_must_have_sheet (XMLSaxParseState *state)
{
	if (!state->sheet) {
		int columns = 256;
		int rows = 65536;

		xml_sax_barf (G_STRFUNC, "sheet should have been named");

		state->sheet = workbook_sheet_add (state->wb, -1,
						   columns, rows);
	}

	return state->sheet;
}

static GnmStyle *
xml_sax_must_have_style (XMLSaxParseState *state)
{
	if (!state->style) {
		xml_sax_barf (G_STRFUNC, "style should have been started");
		state->style = (state->version >= GNM_XML_V6 ||
				state->version <= GNM_XML_V2)
			? gnm_style_new_default ()
			: gnm_style_new ();
	}

	return state->style;
}


static void
xml_sax_sheet_end (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	XMLSaxParseState *state = (XMLSaxParseState *)xin->user_state;

	xml_sax_must_have_sheet (state);

	/* Init ColRowInfo's size_pixels and force a full respan */
	g_object_set (state->sheet, "zoom-factor", state->sheet_zoom, NULL);
	sheet_flag_recompute_spans (state->sheet);
	state->sheet = NULL;
}

static void
xml_sax_sheet_name (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	XMLSaxParseState *state = (XMLSaxParseState *)xin->user_state;
	Sheet *sheet;
	int columns = 256;
	int rows = 65536;

	char const * content = xin->content->str;
	g_return_if_fail (state->sheet == NULL);

	/* * FIXME: Pull this out at some point, so we don't
	 * have to support < GNM_XML_V7 anymore
	 */
	if (state->version >= GNM_XML_V7) {
		sheet = workbook_sheet_by_name (state->wb, content);
		if (!sheet) {
			go_io_error_string (state->context,
				_("File has inconsistent SheetNameIndex element."));
			sheet = sheet_new (state->wb, content,
					   columns, rows);
			workbook_sheet_attach (state->wb, sheet);
		}
	} else {
		sheet = sheet_new (state->wb, content, columns, rows);
		workbook_sheet_attach (state->wb, sheet);
	}
	state->sheet = sheet;

	if (state->display_formulas >= 0)
		g_object_set (sheet, "display-formulas", state->display_formulas, NULL);
	if (state->hide_zero >= 0)
		g_object_set (sheet, "display-zeros", !state->hide_zero, NULL);
	if (state->hide_grid >= 0)
		g_object_set (sheet, "display-grid", !state->hide_grid, NULL);
	if (state->hide_col_header >= 0)
		g_object_set (sheet, "display-column-header", !state->hide_col_header, NULL);
	if (state->hide_row_header >= 0)
		g_object_set (sheet, "display-row-header", !state->hide_row_header, NULL);
	if (state->display_outlines >= 0)
		g_object_set (sheet, "display-outlines", state->display_outlines, NULL);
	if (state->outline_symbols_below >= 0)
		g_object_set (sheet, "display-outlines-below", state->outline_symbols_below, NULL);
	if (state->outline_symbols_right >= 0)
		g_object_set (sheet, "display-outlines-right", state->outline_symbols_right, NULL);
	if (state->text_is_rtl >= 0)
		g_object_set (sheet, "text-is-rtl", state->text_is_rtl, NULL);
	if (state->is_protected >= 0)
		g_object_set (sheet, "protected", state->is_protected, NULL);
	if (state->expr_conv_name != NULL) {
		GnmConventions const *convs = gnm_conventions_default;
		if (0 == strcmp (state->expr_conv_name, "gnumeric:R1C1"))
			convs = gnm_conventions_xls_r1c1;
		g_object_set (sheet, "conventions", convs, NULL);

		g_free (state->expr_conv_name);
		state->expr_conv_name = NULL;
	}
	g_object_set (sheet, "visibility", state->visibility, NULL);
	sheet->tab_color = state->tab_color;
	sheet->tab_text_color = state->tab_text_color;
	if (state->grid_color)
		sheet_style_set_auto_pattern_color (sheet, state->grid_color);
}

static void
xml_sax_sheet_zoom (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	XMLSaxParseState *state = (XMLSaxParseState *)xin->user_state;

	char const * content = xin->content->str;
	double zoom;

	xml_sax_must_have_sheet (state);

	if (xml_sax_double ((xmlChar *)content, &zoom))
		state->sheet_zoom = zoom;
}

static void
xml_sax_print_margins_unit (GsfXMLIn *xin, xmlChar const **attrs,
			    double *points, GtkUnit *desired_display)
{
	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2) {
		double pts;
		if (gnm_xml_attr_double (attrs, "Points", &pts)) {
			*points = pts;
		} else if (attr_eq (attrs[0], "PrefUnit")) {
			*desired_display = unit_name_to_unit (CXML2C (attrs[1]));
		} else
			unknown_attr (xin, attrs);
	}
}

static void
xml_sax_print_margins (GsfXMLIn *xin, xmlChar const **attrs)
{
	XMLSaxParseState *state = (XMLSaxParseState *)xin->user_state;

	GnmPrintInformation *pi;
	double points = -1.;

	xml_sax_must_have_sheet (state);

	pi = state->sheet->print_info;
	switch (xin->node->user_data.v_int) {
	case 0:
		xml_sax_print_margins_unit (xin, attrs,
					    &points,
					    &pi->desired_display.header);
		if (points >= 0.)
			print_info_set_edge_to_below_header (pi, points);
		break;
	case 1:
		xml_sax_print_margins_unit (xin, attrs,
					    &points,
					    &pi->desired_display.footer);
		if (points >= 0.)
			print_info_set_edge_to_above_footer (pi, points);
		break;
	case 2:
		xml_sax_print_margins_unit (xin, attrs,
					    &points, &pi->desired_display.left);
		if (points >= 0.)
			print_info_set_margin_left (pi, points);
		break;
	case 3:
		xml_sax_print_margins_unit (xin, attrs,
					    &points, &pi->desired_display.right);
		if (points >= 0.)
			print_info_set_margin_right (pi, points);
		break;
	case 4:
		xml_sax_print_margins_unit (xin, attrs,
					    &points, &pi->desired_display.top);
		if (points >= 0.)
			print_info_set_margin_header (pi, points);
		break;
	case 5:
		xml_sax_print_margins_unit (xin, attrs,
					    &points, &pi->desired_display.bottom);
		if (points >= 0.)
			print_info_set_margin_footer (pi, points);
		break;
	default:
		return;
	}
}


static void
xml_sax_page_break (GsfXMLIn *xin, xmlChar const **attrs)
{
	XMLSaxParseState *state = (XMLSaxParseState *)xin->user_state;
	GnmPageBreakType  type = GNM_PAGE_BREAK_NONE;
	int pos = -1;

	if (NULL == state->page_breaks)
		return;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (gnm_xml_attr_int (attrs, "pos", &pos)) ;
		else if (!strcmp (CXML2C (attrs[0]), "type"))
			type = gnm_page_break_type_from_str (CXML2C (attrs[1]));

	/* drops invalid positions */
	gnm_page_breaks_append_break (state->page_breaks, pos, type);
}

static void
xml_sax_page_breaks_begin (GsfXMLIn *xin, G_GNUC_UNUSED xmlChar const **attrs)
{
	XMLSaxParseState *state = (XMLSaxParseState *)xin->user_state;
	xml_sax_must_have_sheet (state);
	g_return_if_fail (state->page_breaks == NULL);
	state->page_breaks = gnm_page_breaks_new (xin->node->user_data.v_int);
}

static void
xml_sax_page_breaks_end (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	XMLSaxParseState *state = (XMLSaxParseState *)xin->user_state;

	if (NULL != state->page_breaks) {
		print_info_set_breaks (state->sheet->print_info,
			state->page_breaks);
		state->page_breaks = NULL;
	}
}

static void
xml_sax_print_scale (GsfXMLIn *xin, xmlChar const **attrs)
{
	XMLSaxParseState *state = (XMLSaxParseState *)xin->user_state;

	GnmPrintInformation *pi;
	double percentage;
	int cols, rows;

	xml_sax_must_have_sheet (state);

	pi = state->sheet->print_info;
	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2) {
		if (attr_eq (attrs[0], "type"))
			pi->scaling.type = !strcmp (CXML2C (attrs[1]), "percentage")
				? PRINT_SCALE_PERCENTAGE : PRINT_SCALE_FIT_PAGES;
		else if (gnm_xml_attr_double (attrs, "percentage", &percentage))
			pi->scaling.percentage.x = pi->scaling.percentage.y = percentage;
		else if (gnm_xml_attr_int (attrs, "cols", &cols))
			pi->scaling.dim.cols = cols;
		else if (gnm_xml_attr_int (attrs, "rows", &rows))
			pi->scaling.dim.rows = rows;
	}
}

static void
xml_sax_print_vcenter (GsfXMLIn *xin, xmlChar const **attrs)
{
	XMLSaxParseState *state = (XMLSaxParseState *)xin->user_state;
	GnmPrintInformation *pi;
	int val;

	xml_sax_must_have_sheet (state);

	pi = state->sheet->print_info;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (gnm_xml_attr_int (attrs, "value", &val))
			pi->center_vertically = val;
}

static void
xml_sax_print_hcenter (GsfXMLIn *xin, xmlChar const **attrs)
{
	XMLSaxParseState *state = (XMLSaxParseState *)xin->user_state;
	GnmPrintInformation *pi;
	int val;

	xml_sax_must_have_sheet (state);

	pi = state->sheet->print_info;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (gnm_xml_attr_int (attrs, "value", &val))
			pi->center_horizontally = val;
}

static void
xml_sax_print_grid (GsfXMLIn *xin, xmlChar const **attrs)
{
	XMLSaxParseState *state = (XMLSaxParseState *)xin->user_state;
	GnmPrintInformation *pi;
	int val;

	xml_sax_must_have_sheet (state);

	pi = state->sheet->print_info;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (gnm_xml_attr_int (attrs, "value", &val))
			pi->print_grid_lines = val;
}

static void
xml_sax_print_do_not_print (GsfXMLIn *xin, xmlChar const **attrs)
{
        XMLSaxParseState *state = (XMLSaxParseState *)xin->user_state;
        GnmPrintInformation *pi;
        int val;

	xml_sax_must_have_sheet (state);

	pi = state->sheet->print_info;

        for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
                if (gnm_xml_attr_int (attrs, "value", &val))
                        pi->do_not_print = val;
}

static void
xml_sax_print_print_range (GsfXMLIn *xin, xmlChar const **attrs)
{
        XMLSaxParseState *state = (XMLSaxParseState *)xin->user_state;
        GnmPrintInformation *pi;
        int val;

	xml_sax_must_have_sheet (state);

	pi = state->sheet->print_info;

        for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (xml_sax_attr_enum (attrs, "value", GNM_PRINT_RANGE_TYPE,
				       &val))
                        print_info_set_printrange (pi, val);
}



static void
xml_sax_monochrome (GsfXMLIn *xin, xmlChar const **attrs)
{
	XMLSaxParseState *state = (XMLSaxParseState *)xin->user_state;
	GnmPrintInformation *pi;
	int val;

	xml_sax_must_have_sheet (state);

	pi = state->sheet->print_info;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (gnm_xml_attr_int (attrs, "value", &val))
			pi->print_black_and_white = val;
}

static void
xml_sax_print_titles (GsfXMLIn *xin, xmlChar const **attrs)
{
	XMLSaxParseState *state = (XMLSaxParseState *)xin->user_state;
	GnmPrintInformation *pi;
	int val;

	xml_sax_must_have_sheet (state);

	pi = state->sheet->print_info;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (gnm_xml_attr_int (attrs, "value", &val))
			pi->print_titles = val;
}

static void
xml_sax_repeat_top (GsfXMLIn *xin, xmlChar const **attrs)
{
	XMLSaxParseState *state = (XMLSaxParseState *)xin->user_state;
	GnmPrintInformation *pi;

	xml_sax_must_have_sheet (state);

	pi = state->sheet->print_info;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (!strcmp (CXML2C (attrs[0]), "value")) {
			g_free (pi->repeat_top);
			pi->repeat_top = g_strdup (CXML2C (attrs[1]));
			break;
		}
}

static void
xml_sax_repeat_left (GsfXMLIn *xin, xmlChar const **attrs)
{
	XMLSaxParseState *state = (XMLSaxParseState *)xin->user_state;
	GnmPrintInformation *pi;

	xml_sax_must_have_sheet (state);

	pi = state->sheet->print_info;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (!strcmp (CXML2C (attrs[0]), "value")) {
			g_free (pi->repeat_left);
			pi->repeat_left = g_strdup (CXML2C (attrs[1]));
			break;
		}
}

static void
xml_sax_print_hf (GsfXMLIn *xin, xmlChar const **attrs)
{
	XMLSaxParseState *state = (XMLSaxParseState *)xin->user_state;
	GnmPrintInformation *pi;
	GnmPrintHF *hf;

	xml_sax_must_have_sheet (state);

	pi = state->sheet->print_info;

	switch (xin->node->user_data.v_int) {
	case 0:
		hf = pi->footer;
		break;
	case 1:
		hf = pi->header;
		break;
	default:
		return;
	}

	g_return_if_fail (hf != NULL);

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2) {
		if ( attr_eq (attrs[0], "Left")) {
			g_free (hf->left_format);
			hf->left_format = g_strdup (CXML2C (attrs[1]));
		} else if (attr_eq (attrs[0], "Middle")) {
			g_free (hf->middle_format);
			hf->middle_format = g_strdup (CXML2C (attrs[1]));
		} else if (attr_eq (attrs[0], "Right")) {
			g_free (hf->right_format);
			hf->right_format = g_strdup (CXML2C (attrs[1]));
		} else
			unknown_attr (xin, attrs);
	}
}


static void
xml_sax_even_if_only_styles (GsfXMLIn *xin, xmlChar const **attrs)
{
	XMLSaxParseState *state = (XMLSaxParseState *)xin->user_state;
	GnmPrintInformation *pi;
	int val;

	xml_sax_must_have_sheet (state);

	pi = state->sheet->print_info;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (gnm_xml_attr_int (attrs, "value", &val))
			pi->print_even_if_only_styles = val;
}




static void
xml_sax_selection_range (GsfXMLIn *xin, xmlChar const **attrs)
{
	XMLSaxParseState *state = (XMLSaxParseState *)xin->user_state;
	GnmRange r;

	xml_sax_must_have_sheet (state);
	if (xml_sax_attr_range (attrs, &r))
		sv_selection_add_range (
			sheet_get_view (state->sheet, state->wb_view), &r);
}

static void
xml_sax_selection (GsfXMLIn *xin, xmlChar const **attrs)
{
	XMLSaxParseState *state = (XMLSaxParseState *)xin->user_state;
	Sheet *sheet = xml_sax_must_have_sheet (state);
	int col = -1, row = -1;

	sv_selection_reset (sheet_get_view (sheet, state->wb_view));

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (gnm_xml_attr_int (attrs, "CursorCol", &col)) ;
		else if (gnm_xml_attr_int (attrs, "CursorRow", &row)) ;
		else
			unknown_attr (xin, attrs);

	XML_CHECK (state->cell.col < 0);
	XML_CHECK (state->cell.row < 0);

	/* Default in case of error.  */
	state->cell.col = 0;
	state->cell.row = 0;

	XML_CHECK (col >= 0 && col < gnm_sheet_get_max_cols (sheet));
	XML_CHECK (row >= 0 && row < gnm_sheet_get_max_rows (sheet));

	state->cell.col = col;
	state->cell.row = row;
}

static void
xml_sax_selection_end (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	XMLSaxParseState *state = (XMLSaxParseState *)xin->user_state;

	GnmCellPos const pos = state->cell;
	state->cell.col = state->cell.row = -1;
	gnm_sheet_view_set_edit_pos (sheet_get_view (state->sheet, state->wb_view), &pos);
}

static void
xml_sax_sheet_layout (GsfXMLIn *xin, xmlChar const **attrs)
{
	XMLSaxParseState *state = (XMLSaxParseState *)xin->user_state;
	GnmCellPos tmp;

	xml_sax_must_have_sheet (state);

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (xml_sax_attr_cellpos (attrs, "TopLeft", &tmp, state->sheet))
			gnm_sheet_view_set_initial_top_left (
				sheet_get_view (state->sheet, state->wb_view),
				tmp.col, tmp.row);
		else
			unknown_attr (xin, attrs);
}

static void
xml_sax_sheet_freezepanes (GsfXMLIn *xin, xmlChar const **attrs)
{
	XMLSaxParseState *state = (XMLSaxParseState *)xin->user_state;
	GnmCellPos frozen_tl, unfrozen_tl;
	int flags = 0;

	xml_sax_must_have_sheet (state);

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (xml_sax_attr_cellpos (attrs, "FrozenTopLeft", &frozen_tl, state->sheet))
			flags |= 1;
		else if (xml_sax_attr_cellpos (attrs, "UnfrozenTopLeft", &unfrozen_tl, state->sheet))
			flags |= 2;
		else
			unknown_attr (xin, attrs);

	if (flags == 3)
		gnm_sheet_view_freeze_panes (sheet_get_view (state->sheet, state->wb_view),
			&frozen_tl, &unfrozen_tl);
}

static void
xml_sax_cols_rows (GsfXMLIn *xin, xmlChar const **attrs)
{
	XMLSaxParseState *state = (XMLSaxParseState *)xin->user_state;
	double def_size;
	gboolean const is_col = xin->node->user_data.v_bool;

	xml_sax_must_have_sheet (state);

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (gnm_xml_attr_double (attrs, "DefaultSizePts", &def_size)) {
			if (is_col)
				sheet_col_set_default_size_pts (state->sheet, def_size);
			else
				sheet_row_set_default_size_pts (state->sheet, def_size);
		}
}

static void
xml_sax_colrow (GsfXMLIn *xin, xmlChar const **attrs)
{
	XMLSaxParseState *state = (XMLSaxParseState *)xin->user_state;

	ColRowInfo *cri = NULL;
	double size = -1.;
	int pos = -1, val;
	int hidden = 0, hard_size = 0, is_collapsed = 0, outline_level = 0;
	int count = 1;
	gboolean const is_col = xin->node->user_data.v_bool;
	Sheet *sheet = xml_sax_must_have_sheet (state);

	maybe_update_progress (xin);

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2) {
		if (gnm_xml_attr_int (attrs, "No", &pos)) ;
		else if (gnm_xml_attr_double (attrs, "Unit", &size)) ;
		else if (gnm_xml_attr_int (attrs, "Count", &count)) ;
		else if (gnm_xml_attr_int (attrs, "HardSize", &hard_size)) ;
		else if (gnm_xml_attr_int (attrs, "Hidden", &hidden)) ;
		else if (gnm_xml_attr_int (attrs, "Collapsed", &is_collapsed)) ;
		else if (gnm_xml_attr_int (attrs, "OutlineLevel", &outline_level)) ;
		else if (gnm_xml_attr_int (attrs, "MarginA", &val))
			; /* deprecated in 1.7.1 */
		else if (gnm_xml_attr_int (attrs, "MarginB", &val))
			; /* deprecated in 1.7.1 */
		else
			unknown_attr (xin, attrs);
	}

	XML_CHECK (size > -1);
	XML_CHECK (pos >= 0 && pos < colrow_max (is_col, sheet));
	XML_CHECK (count >= 1);
	XML_CHECK (count <= colrow_max (is_col, sheet) - pos);

	cri = is_col
		? sheet_col_fetch (state->sheet, pos)
		: sheet_row_fetch (state->sheet, pos);
	cri->hard_size = hard_size;
	cri->visible = !hidden;
	cri->is_collapsed = is_collapsed;
	cri->outline_level = outline_level;

	if (is_col) {
		sheet_col_set_size_pts (state->sheet, pos, size, cri->hard_size);
		if (state->sheet->cols.max_outline_level < cri->outline_level)
			state->sheet->cols.max_outline_level = cri->outline_level;
	} else {
		sheet_row_set_size_pts (state->sheet, pos, size, cri->hard_size);
		if (state->sheet->rows.max_outline_level < cri->outline_level)
			state->sheet->rows.max_outline_level = cri->outline_level;
	}

	// resize flags are already set only need to copy the sizes
	while (--count > 0)
		sheet_colrow_copy_info (state->sheet, ++pos, is_col, cri);
}

static void
xml_sax_style_region_start (GsfXMLIn *xin, xmlChar const **attrs)
{
	XMLSaxParseState *state = (XMLSaxParseState *)xin->user_state;

	g_return_if_fail (state->style_range_init == FALSE);
	g_return_if_fail (state->style == NULL);

	if (attrs == NULL) {
		g_warning ("Invalid tag: gnm:StyleRegion start tag without attributes");
		return;
	}

	state->style = (state->version >= GNM_XML_V6 ||
			state->version <= GNM_XML_V2)
		? gnm_style_new_default ()
		: gnm_style_new ();

	state->style_range_init =
		xml_sax_attr_range (attrs, &state->style_range);
}

static void
xml_sax_style_region_end (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	XMLSaxParseState *state = (XMLSaxParseState *)xin->user_state;

	if (!state->style_range_init) {
		xml_sax_barf (G_STRFUNC, "style region must have range");
		range_init (&state->style_range, 0, 0, 0, 0);
		state->style_range_init = TRUE;
	}

	xml_sax_must_have_style (state);
	xml_sax_must_have_sheet (state);

	if (state->clipboard) {
		GnmCellRegion *cr = state->clipboard;
		GnmStyleRegion *sr = g_new (GnmStyleRegion, 1);

		sr->range = state->style_range;
		sr->style = state->style;

		cr->styles = g_slist_prepend (cr->styles, sr);
	} else if (state->version >= GNM_XML_V6 || state->version <= GNM_XML_V2)
		sheet_style_set_range (state->sheet, &state->style_range,
				       state->style);
	else
		sheet_style_apply_range (state->sheet, &state->style_range,
					 state->style);

	state->style_range_init = FALSE;
	state->style = NULL;

	maybe_update_progress (xin);
}

static void
xml_sax_style_start (GsfXMLIn *xin, xmlChar const **attrs)
{
	XMLSaxParseState *state = (XMLSaxParseState *)xin->user_state;

	int val;
	GnmColor *colour;

	xml_sax_must_have_style (state);

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2) {
		if (xml_sax_attr_enum (attrs, "HAlign", GNM_ALIGN_H_TYPE, &val))
			gnm_style_set_align_h (state->style, val);
		else if (xml_sax_attr_enum (attrs, "VAlign", GNM_ALIGN_V_TYPE, &val))
			gnm_style_set_align_v (state->style, val);

		/* Pre version V6 */
		else if (gnm_xml_attr_int (attrs, "Fit", &val))
			gnm_style_set_wrap_text (state->style, val);

		else if (gnm_xml_attr_int (attrs, "WrapText", &val))
			gnm_style_set_wrap_text (state->style, val);
		else if (gnm_xml_attr_bool (attrs, "ShrinkToFit", &val))
			gnm_style_set_shrink_to_fit (state->style, val);
		else if (gnm_xml_attr_int (attrs, "Rotation", &val)) {
			/* Work around a bug pre 1.5.1 that would allow
			 * negative rotations.  -1 == vertical, map everything
			 * else back onto 0..359 */
			if (val < -1)
				val += 360;
			gnm_style_set_rotation (state->style, val);
		} else if (gnm_xml_attr_int (attrs, "Shade", &val))
			gnm_style_set_pattern (state->style, val);
		else if (gnm_xml_attr_int (attrs, "Indent", &val))
			gnm_style_set_indent (state->style, val);
		else if (xml_sax_attr_color (attrs, "Fore", &colour))
			gnm_style_set_font_color (state->style, colour);
		else if (xml_sax_attr_color (attrs, "Back", &colour))
			gnm_style_set_back_color (state->style, colour);
		else if (xml_sax_attr_color (attrs, "PatternColor", &colour))
			gnm_style_set_pattern_color (state->style, colour);
		else if (attr_eq (attrs[0], "Format")) {
			GOFormat *fmt = make_format (CXML2C (attrs[1]));
			if (fmt) {
				gnm_style_set_format (state->style, fmt);
				go_format_unref (fmt);
			}
		}
		else if (gnm_xml_attr_int (attrs, "Hidden", &val))
			gnm_style_set_contents_hidden (state->style, val);
		else if (gnm_xml_attr_int (attrs, "Locked", &val))
			gnm_style_set_contents_locked (state->style, val);
		else if (gnm_xml_attr_int (attrs, "Orient", &val))
			; /* ignore old useless attribute */
		else
			unknown_attr (xin, attrs);
	}
}

static void
xml_sax_style_font (GsfXMLIn *xin, xmlChar const **attrs)
{
	XMLSaxParseState *state = (XMLSaxParseState *)xin->user_state;

	double size_pts = 10.;
	int val;

	xml_sax_must_have_style (state);

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2) {
		if (gnm_xml_attr_double (attrs, "Unit", &size_pts)) {
			if (!(size_pts >= 1.0))
				xml_sax_barf (G_STRFUNC, "size_pts >= 1");
			else
				gnm_style_set_font_size (state->style, size_pts);
		} else if (gnm_xml_attr_int (attrs, "Bold", &val))
			gnm_style_set_font_bold (state->style, val);
		else if (gnm_xml_attr_int (attrs, "Italic", &val))
			gnm_style_set_font_italic (state->style, val);
		else if (gnm_xml_attr_int (attrs, "Underline", &val))
			gnm_style_set_font_uline (state->style, (GnmUnderline)val);
		else if (gnm_xml_attr_int (attrs, "StrikeThrough", &val))
			gnm_style_set_font_strike (state->style, val);
		else if (gnm_xml_attr_int (attrs, "Script", &val)) {
			if (val == 0)
				gnm_style_set_font_script (state->style, GO_FONT_SCRIPT_STANDARD);
			else if (val < 0)
				gnm_style_set_font_script (state->style, GO_FONT_SCRIPT_SUB);
			else
				gnm_style_set_font_script (state->style, GO_FONT_SCRIPT_SUPER);
		} else
			unknown_attr (xin, attrs);
	}
}

static char const *
font_component (char const *fontname, int idx)
{
	int i = 0;
	char const *p = fontname;

	for (; *p && i < idx; p++) {
		if (*p == '-')
			i++;
	}
	if (*p == '-')
		p++;

	return p;
}

/**
 * style_font_read_from_x11:
 * @mstyle: the style to setup to this font.
 * @fontname: an X11-like font name.
 *
 * Tries to guess the fontname, the weight and italization parameters
 * and setup mstyle
 *
 * Returns: A valid style font.
 */
static void
style_font_read_from_x11 (GnmStyle *mstyle, char const *fontname)
{
	char const *c;

	/* FIXME: we should do something about the typeface instead
	 * of hardcoding it to helvetica.
	 */
	c = font_component (fontname, 2);
	if (strncmp (c, "bold", 4) == 0)
		gnm_style_set_font_bold (mstyle, TRUE);

	c = font_component (fontname, 3);
	if (strncmp (c, "o", 1) == 0)
		gnm_style_set_font_italic (mstyle, TRUE);

	if (strncmp (c, "i", 1) == 0)
		gnm_style_set_font_italic (mstyle, TRUE);
}

static void
xml_sax_style_font_end (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	XMLSaxParseState *state = (XMLSaxParseState *)xin->user_state;

	xml_sax_must_have_style (state);

	if (xin->content->len > 0) {
		char const * content = xin->content->str;
		if (*content == '-')
			style_font_read_from_x11 (state->style, content);
		else
			gnm_style_set_font_name (state->style, content);
	}
}

static void
xml_sax_validation (GsfXMLIn *xin, xmlChar const **attrs)
{
	XMLSaxParseState *state = (XMLSaxParseState *)xin->user_state;

	int dummy;
	gboolean b_dummy;

	g_return_if_fail (state->validation.title == NULL);
	g_return_if_fail (state->validation.msg == NULL);
	g_return_if_fail (state->validation.texpr[0] == NULL);
	g_return_if_fail (state->validation.texpr[1] == NULL);

	state->validation.style = GNM_VALIDATION_STYLE_NONE;
	state->validation.type = GNM_VALIDATION_TYPE_ANY;
	state->validation.op = GNM_VALIDATION_OP_NONE;
	state->validation.allow_blank = TRUE;
	state->validation.use_dropdown = FALSE;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2) {
		if (xml_sax_attr_enum (attrs, "Style",
				       GNM_VALIDATION_STYLE_TYPE,
				       &dummy)) {
			state->validation.style = dummy;
		} else if (xml_sax_attr_enum (attrs, "Type",
					      GNM_VALIDATION_TYPE_TYPE,
					      &dummy)) {
			state->validation.type = dummy;
		} else if (xml_sax_attr_enum (attrs, "Operator",
					      GNM_VALIDATION_OP_TYPE,
					      &dummy)) {
			state->validation.op = dummy;
		} else if (attr_eq (attrs[0], "Title")) {
			state->validation.title = g_strdup (CXML2C (attrs[1]));
		} else if (attr_eq (attrs[0], "Message")) {
			state->validation.msg = g_strdup (CXML2C (attrs[1]));
		} else if (gnm_xml_attr_bool (attrs, "AllowBlank", &b_dummy)) {
			state->validation.allow_blank = b_dummy;
		} else if (gnm_xml_attr_bool (attrs, "UseDropdown", &b_dummy)) {
			state->validation.use_dropdown = b_dummy;
		} else
			unknown_attr (xin, attrs);
	}
}

static void
xml_sax_validation_end (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	XMLSaxParseState *state = (XMLSaxParseState *)xin->user_state;

	xml_sax_must_have_style (state);

	gnm_style_set_validation (state->style,
		gnm_validation_new (state->validation.style,
				state->validation.type,
				state->validation.op,
				state->sheet,
				state->validation.title,
				state->validation.msg,
				state->validation.texpr[0],
				state->validation.texpr[1],
				state->validation.allow_blank,
				state->validation.use_dropdown));

	g_free (state->validation.title);
	state->validation.title = NULL;
	g_free (state->validation.msg);
	state->validation.msg = NULL;
	state->validation.texpr[0] = state->validation.texpr[1] = NULL;
}

static void
xml_sax_validation_expr_end (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	XMLSaxParseState *state = (XMLSaxParseState *)xin->user_state;

	int const i = xin->node->user_data.v_int;
	GnmExprTop const *texpr;
	GnmParsePos pos;

	g_return_if_fail (state->validation.texpr[i] == NULL);

	texpr = gnm_expr_parse_str (xin->content->str,
				    parse_pos_init_sheet (&pos, state->sheet),
				    GNM_EXPR_PARSE_DEFAULT,
				    state->convs,
				    NULL);

	g_return_if_fail (texpr != NULL);

	state->validation.texpr[i] = texpr;
}

static void
xml_sax_condition (GsfXMLIn *xin, xmlChar const **attrs)
{
	XMLSaxParseState *state = (XMLSaxParseState *)xin->user_state;
	GnmStyleCondOp op = GNM_STYLE_COND_CUSTOM;

	g_return_if_fail (state->cond == NULL);
	g_return_if_fail (state->cond_save_style == NULL);

	xml_sax_must_have_style (state);

	state->cond_save_style = state->style;
	state->style = gnm_style_new ();

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2) {
		int dummy;

		if (gnm_xml_attr_int (attrs, "Operator", &dummy))
			op = dummy;
		else
			unknown_attr (xin, attrs);
	}

	state->cond = gnm_style_cond_new (op, state->sheet);
}

static void
xml_sax_condition_end (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	XMLSaxParseState *state = (XMLSaxParseState *)xin->user_state;
	GnmStyleConditions *sc;

	xml_sax_must_have_style (state);
	g_return_if_fail (state->cond_save_style != NULL);
	g_return_if_fail (state->cond != NULL);

	gnm_style_cond_set_overlay (state->cond, state->style);
	gnm_style_unref (state->style);
	state->style = state->cond_save_style;
	state->cond_save_style = NULL;

	if (!gnm_style_is_element_set (state->style, MSTYLE_CONDITIONS) ||
	    NULL == (sc = gnm_style_get_conditions (state->style)))
		gnm_style_set_conditions (state->style,
			(sc = gnm_style_conditions_new (state->sheet)));
	gnm_style_conditions_insert (sc, state->cond, -1);

	gnm_style_cond_free (state->cond);
	state->cond = NULL;
}

/*
 * We have been saving expressions relative to A1.  This means that when we
 * read, we see a relative reference to a cell above as R[65535]C.  This
 * function patches that to R[-1]C.
 *
 * We ought to fix the format, but then old Gnumerics couldn't read new
 * files.  In fact, if we just added a "Position" attribute then we would
 * get silent corruption.
 */
static GnmExpr const *
cond_patchup (GnmExpr const *expr, GnmExprWalk *data)
{
	XMLSaxParseState *state = data->user;
	GnmCellPos const *pos = &state->style_range.start;
	GnmCellRef const *oref = gnm_expr_get_cellref (expr);
	GnmValue const *ocst = gnm_expr_get_constant (expr);

	if (oref) {
		GnmCellPos tpos;
		GnmCellRef tref = *oref;
		gnm_cellpos_init_cellref (&tpos, oref, pos, state->sheet);
		if (tref.col_relative)
			tref.col = tpos.col - pos->col;
		if (tref.row_relative)
			tref.row = tpos.row - pos->row;
		if (gnm_cellref_equal (&tref, oref))
			return NULL;
		return gnm_expr_new_cellref (&tref);
	}

	if (ocst && VALUE_IS_CELLRANGE (ocst)) {
		GnmRangeRef const *oref = value_get_rangeref (ocst);
		GnmRangeRef tref = *oref;
		GnmRange trange;
		Sheet *start_sheet, *end_sheet;
		GnmEvalPos ep;

		eval_pos_init_pos (&ep, state->sheet, pos);
		gnm_rangeref_normalize (oref, &ep, &start_sheet, &end_sheet,
					&trange);
		if (tref.a.col_relative)
			tref.a.col = trange.start.col - pos->col;
		if (tref.a.row_relative)
			tref.a.row = trange.start.row - pos->row;
		if (tref.b.col_relative)
			tref.b.col = trange.end.col - pos->col;
		if (tref.b.row_relative)
			tref.b.row = trange.end.row - pos->row;
		if (gnm_rangeref_equal (&tref, oref))
			return NULL;
		return gnm_expr_new_constant (value_new_cellrange_unsafe (&tref.a, &tref.b));
	}

	return NULL;
}

static void
xml_sax_condition_expr_end (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	XMLSaxParseState *state = (XMLSaxParseState *)xin->user_state;

	int const i = xin->node->user_data.v_int;
	GnmExprTop const *texpr;
	GnmParsePos pos;
	GnmExpr const *patched_expr;

	g_return_if_fail (gnm_style_cond_get_expr (state->cond, i) == NULL);

	parse_pos_init_sheet (&pos, state->sheet);
	texpr = gnm_expr_parse_str (xin->content->str,
				    &pos,
				    GNM_EXPR_PARSE_DEFAULT,
				    state->convs,
				    NULL);
	g_return_if_fail (texpr != NULL);

	patched_expr = gnm_expr_walk (texpr->expr, cond_patchup, state);
	if (patched_expr) {
		gnm_expr_top_unref (texpr);
		texpr = gnm_expr_top_new (patched_expr);
	}

	gnm_style_cond_set_expr (state->cond, texpr, i);
	gnm_expr_top_unref (texpr);
}

static void
xml_sax_hlink (GsfXMLIn *xin, xmlChar const **attrs)
{
	XMLSaxParseState *state = (XMLSaxParseState *)xin->user_state;
	char *type = NULL;
	char *target = NULL;
	char *tip = NULL;

	xml_sax_must_have_style (state);

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2) {
		if (attr_eq (attrs[0], "type"))
			type = g_strdup (CXML2C (attrs[1]));
		else if (attr_eq (attrs[0], "target"))
			target = g_strdup (CXML2C (attrs[1]));
		else if (attr_eq (attrs[0], "tip"))
			tip = g_strdup (CXML2C (attrs[1]));
		else
			unknown_attr (xin, attrs);
	}

	if (NULL != type && NULL != target) {
		GType typ = g_type_from_name (type);
		GnmHLink *lnk = gnm_hlink_new (typ, state->sheet);
		gnm_hlink_set_target (lnk, target);
		gnm_hlink_set_tip (lnk, tip);
		gnm_style_set_hlink (state->style, lnk);
	}

	g_free (type);
	g_free (target);
	g_free (tip);
}

static void
xml_sax_input_msg (GsfXMLIn *xin, xmlChar const **attrs)
{
	XMLSaxParseState *state = (XMLSaxParseState *)xin->user_state;
	char *title = NULL;
	char *msg = NULL;

	xml_sax_must_have_style (state);

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2) {
		if (attr_eq (attrs[0], "Title"))
			title = g_strdup (CXML2C (attrs[1]));
		else if (attr_eq (attrs[0], "Message"))
			msg = g_strdup (CXML2C (attrs[1]));
		else
			unknown_attr (xin, attrs);
	}

	if (NULL != title || NULL != msg)
		gnm_style_set_input_msg (state->style,
			gnm_input_msg_new (msg, title));
	g_free (title);
	g_free (msg);
}

static void
xml_sax_style_border (GsfXMLIn *xin, xmlChar const **attrs)
{
	XMLSaxParseState *state = (XMLSaxParseState *)xin->user_state;

	int pattern = -1;
	GnmColor *colour = NULL;

	xml_sax_must_have_style (state);

	/* Colour is optional */
	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2) {
		if (xml_sax_attr_color (attrs, "Color", &colour)) ;
		else if (gnm_xml_attr_int (attrs, "Style", &pattern)) ;
		else
			unknown_attr (xin, attrs);
	}

	if (pattern >= GNM_STYLE_BORDER_NONE) {
		GnmStyleElement const type = xin->node->user_data.v_int;
		GnmStyleBorderLocation const loc =
			GNM_STYLE_BORDER_TOP + (int)(type - MSTYLE_BORDER_TOP);
		GnmBorder *border;

		/*
		 * Make sure we have a colour to prevent trouble further
		 * down the line.
		 */
		if (!colour)
			colour = gnm_color_new_go (GO_COLOR_BLACK);

		border = gnm_style_border_fetch
			((GnmStyleBorderType)pattern, colour,
			 gnm_style_border_get_orientation (loc));
		gnm_style_set_border (state->style, type, border);
	}
}

static void
xml_sax_cell (GsfXMLIn *xin, xmlChar const **attrs)
{
	XMLSaxParseState *state = (XMLSaxParseState *)xin->user_state;
	int row = -1, col = -1;
	int rows = -1, cols = -1;
	int value_type = -1;
	GOFormat *value_fmt = NULL;
	int expr_id = -1;
	const char *value_result = NULL;

	g_return_if_fail (state->cell.row == -1);
	g_return_if_fail (state->cell.col == -1);
	g_return_if_fail (state->array_rows == -1);
	g_return_if_fail (state->array_cols == -1);
	g_return_if_fail (state->expr_id == -1);
	g_return_if_fail (state->value_type == -1);
	g_return_if_fail (state->value_result == NULL);

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2) {
		if (gnm_xml_attr_int (attrs, "Col", &col)) ;
		else if (gnm_xml_attr_int (attrs, "Row", &row)) ;
		else if (gnm_xml_attr_int (attrs, "Cols", &cols)) ;
		else if (gnm_xml_attr_int (attrs, "Rows", &rows)) ;
		else if (gnm_xml_attr_int (attrs, "ExprID", &expr_id)) ;
		else if (gnm_xml_attr_int (attrs, "ValueType", &value_type)) ;
		else if (attr_eq (attrs[0], "Value"))
			value_result = CXML2C (attrs[1]);
		else if (attr_eq (attrs[0], "ValueFormat")) {
			go_format_unref (value_fmt);
			value_fmt = make_format (CXML2C (attrs[1]));
		} else
			unknown_attr (xin, attrs);
	}

	// Ignore value_result absent a type
	if (value_type == -1)
		value_result = NULL;

	XML_CHECK2 (col >= 0 && col <= GNM_MAX_COLS - MAX (1, cols),
		    go_format_unref (value_fmt));
	XML_CHECK2 (row >= 0 && row <= GNM_MAX_ROWS - MAX (1, rows),
		    go_format_unref (value_fmt));

	if (cols > 0 || rows > 0) {
		/* Both must be valid */
		XML_CHECK2 (cols > 0 && rows > 0,
			    go_format_unref (value_fmt));

		state->array_cols = cols;
		state->array_rows = rows;
	}

	state->cell.row = row;
	state->cell.col = col;
	state->expr_id = expr_id;
	state->value_type = value_type;
	state->value_fmt = value_fmt;
	state->value_result = g_strdup (value_result);
}

/*
 * xml_cell_set_array_expr : Utility routine to parse an expression
 *     and store it as an array.
 *
 * @cell: The upper left hand corner of the array.
 * @text: The text to parse.
 * @rows: The number of rows.
 * @cols: The number of columns.
 */
static void
xml_cell_set_array_expr (XMLSaxParseState *state,
			 GnmCell *cell, GnmCellRegion *cr, GnmCellCopy *cc_corner,
			 GnmParsePos *pp,
			 char const *text,
			 int const cols, int const rows)
{
	GnmExprTop const *texpr;
	GnmRange r;

	texpr =	gnm_expr_parse_str (text, pp, GNM_EXPR_PARSE_DEFAULT,
				    state->convs,
				    NULL);
	g_return_if_fail (texpr != NULL);

	if (!cell) {
		// Clipboard case
		int col = pp->eval.col;
		int row = pp->eval.row;
		int x = col - cr->base.col;
		int y = row - cr->base.row;

		cc_corner->texpr = gnm_expr_top_new_array_corner
			(cols, rows, gnm_expr_copy (texpr->expr));
		gnm_expr_top_unref (texpr);

		for (int c = 0; c < cols; c++) {
			for (int r = 0; r < rows; r++) {
				GnmCellCopy *cc;

				if (r + c == 0)
					continue;

				cc = gnm_cell_copy_new (cr, x + c, y + r);
				cc->texpr = gnm_expr_top_new_array_elem (c, r);
			}
		}
		return;
	}

	r.start = r.end = cell->pos;
	r.end.col += (cols - 1);
	r.end.row += (rows - 1);

	if (!gnm_cell_set_array (cell->base.sheet, &r, texpr)) {
		xml_sax_barf (G_STRFUNC, "target area empty");
	}

	gnm_expr_top_unref (texpr);
}

/*
 * xml_not_used_old_array_spec : See if the string corresponds to
 *     a pre-0.53 style array expression.
 *     If it is the upper left corner	 - assign it.
 *     If it is a member of an array     - ignore it; the corner will assign it.
 *     If it is not a member of an array return TRUE.
 */
static gboolean
xml_not_used_old_array_spec (XMLSaxParseState *state,
			     GnmCell *cell, GnmCellRegion *cr, GnmCellCopy *cc,
			     GnmParsePos *pp,
			     char const *content)
{
	long rows, cols, row, col;
	char *end, *expr_end, *ptr;

	/* This is the syntax we are trying to parse: "{%s}(%d,%d)[%d][%d]" */

	if (content[0] != '=' || content[1] != '{')
		return TRUE;

	expr_end = strrchr (content, '}');
	if (expr_end == NULL || expr_end[1] != '(')
		return TRUE;

	rows = strtol (ptr = expr_end + 2, &end, 10);
	if (end == ptr || *end != ',')
		return TRUE;
	cols = strtol (ptr = end + 1, &end, 10);
	if (end == ptr || end[0] != ')' || end[1] != '[')
		return TRUE;
	row = strtol (ptr = end + 2, &end, 10);
	if (end == ptr || end[0] != ']' || end[1] != '[')
		return TRUE;
	col = strtol (ptr = end + 2, &end, 10);
	if (end == ptr || end[0] != ']' || end[1] != '\0')
		return TRUE;

	if (row == 0 && col == 0) {
		*expr_end = '\0';
		xml_cell_set_array_expr (state, cell, cr, cc, pp,
					 content + 2, rows, cols);
	}

	return FALSE;
}

static void
xml_sax_cell_content (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	XMLSaxParseState *state = (XMLSaxParseState *)xin->user_state;
	Sheet *sheet = state->sheet;
	gboolean is_new_cell = FALSE, is_post_52_array = FALSE;
	int size_cols, size_rows;
	GnmParsePos pos;
	GnmCell *cell = NULL; /* Regular case */
	GnmCellCopy *cc = NULL; /* Clipboard case */
	GnmCellRegion *cr = state->clipboard;
	GnmExprTop const *texpr = NULL;
	GnmValue *v = NULL;
	char const *content = xin->content->str;
	gboolean has_contents = (xin->content->len > 0);
	const char *expr_start = gnm_expr_char_start_p (content);

	int const col = state->cell.col;
	int const row = state->cell.row;
	int const array_cols = state->array_cols;
	int const array_rows = state->array_rows;
	int expr_id = state->expr_id;
	int const value_type = state->value_type;
	gboolean const seen_contents = state->seen_cell_contents;
	GOFormat *value_fmt = state->value_fmt;
	char *value_result = state->value_result;

	/* Clean out the state before any error checking */
	state->cell.row = state->cell.col = -1;
	state->array_rows = state->array_cols = -1;
	state->expr_id = -1;
	state->value_type = -1;
	state->value_fmt = NULL;
	state->value_result = NULL;
	state->seen_cell_contents = strcmp (xin->node->id, "CELL_CONTENT") == 0;

	if (seen_contents)
		return;

	is_post_52_array = (array_cols > 0) && (array_rows > 0);
	size_cols = is_post_52_array ? array_cols : 1;
	size_rows = is_post_52_array ? array_rows : 1;

	maybe_update_progress (xin);

	if (cr) {
		int x = col - cr->base.col;
		int y = row - cr->base.row;

		XML_CHECK (x >= 0 &&
			   x <= gnm_sheet_get_max_cols (sheet) - size_cols);
		XML_CHECK (y >= 0 &&
			   y <= gnm_sheet_get_max_rows (sheet) - size_rows);
		cc = gnm_cell_copy_new (cr, x, y);
		parse_pos_init (&pos, NULL, sheet, col, row);
	} else {
		XML_CHECK (col >= 0 &&
			   col <= gnm_sheet_get_max_cols (sheet) - size_cols);
		XML_CHECK (row >= 0 &&
			   row <= gnm_sheet_get_max_rows (sheet) - size_rows);

		cell = sheet_cell_get (sheet, col, row);
		is_new_cell = (cell == NULL);
		if (is_new_cell) {
			cell = sheet_cell_create (sheet, col, row);
			if (cell == NULL)
				return;
		}
		parse_pos_init_cell (&pos, cell);
	}

	// ----------------------------------------

	if (is_post_52_array && has_contents) {
		// Array formula
		g_return_if_fail (content[0] == '=');
		xml_cell_set_array_expr (state, cell, cr, cc,
					 &pos, content + 1,
					 array_cols, array_rows);
		texpr = cr ? cc->texpr : cell->base.texpr;
		if (texpr) gnm_expr_top_ref (texpr);
		goto store_shared;
	}

	// ----------------------------------------

	if (has_contents && state->version < GNM_XML_V3 &&
	    !xml_not_used_old_array_spec (state, cell, cr, cc, &pos, content)) {
		// Very old array syntax -- irrelevant
		goto done;
	}

	// ----------------------------------------

	if (!has_contents && expr_id > 0) {
		// Re-use of expression id
		texpr = g_hash_table_lookup (state->expr_map,
					     GINT_TO_POINTER (expr_id));

		if (texpr && gnm_expr_top_is_array_corner (texpr)) {
			g_printerr ("Shared array formula for %s -- how did that happen?\n",
				    cell ? cell_name (cell) : "clipboard");
			texpr = gnm_expr_top_new (gnm_expr_copy (texpr->expr));
			expr_id = -1;
		} else if (texpr) {
			gnm_expr_top_ref (texpr);
			expr_id = -1;
		} else {
			char *msg = g_strdup_printf
				("Looking up shared expression id %d",
				 expr_id);
			char *s = g_strdup_printf ("<shared expression %d>", expr_id);
			xml_sax_barf (G_STRFUNC, msg);
			g_free (msg);

			texpr = gnm_expr_top_new_constant (value_new_string_nocopy (s));
		}
		goto assign_and_done;
	}

	// ----------------------------------------

	if (value_type > 0) {
		// Cell value
		gboolean from_content = (value_result == NULL);
		const char *txt = from_content ? content : value_result;
		v = value_new_from_string (value_type, txt, value_fmt, FALSE);
		if (v == NULL) {
			char *msg = g_strdup_printf
				("Parsing \"%s\" as type 0x%x",
				 txt, value_type);
			xml_sax_barf (G_STRFUNC, msg);
			g_free (msg);
			v = value_new_string (txt);
		}

		// If we consumed the contents as a value, then it's not
		// an expression.
		if (from_content)
			expr_start = NULL;
		else {
			if (value_fmt)
				value_set_fmt (v, value_fmt);
		}
	}

	// ----------------------------------------

	if (expr_start && *expr_start) {
		GnmParseError perr;

		parse_error_init (&perr);
		texpr = gnm_expr_parse_str (expr_start,
					    &pos,
					    GNM_EXPR_PARSE_DEFAULT,
					    state->convs,
					    &perr);
		// Don't warn in the clipboard case.
		// It's probably an unknown sheet ref
		if (!texpr && !cr)
			g_warning ("Unparsable expression for %s: %s (%s)\n",
				   cell ? cell_name (cell) : "-",
				   content,
				   perr.err->message);
		if (!texpr)
			texpr = gnm_expr_top_new_constant (value_new_string (expr_start));
		parse_error_free (&perr);

		if (expr_id > 0) {
			gpointer id = GINT_TO_POINTER (expr_id);
			GnmExprTop const *texpr0 =
				g_hash_table_lookup (state->expr_map, id);
			if (texpr0) {
				if (!is_post_52_array)
					g_warning ("XML-IO: Duplicate shared expression");
				expr_id = -1;
			}
		}
	}

assign_and_done:
	if (!v)
		v = value_new_empty ();
	// When we get here:
	// 1. We own a ref to texpr (or it's NULL)
	// 2. We own v.  After this section we no longer own v.
	if (cell) {
		// Regular case
		if (texpr)
			gnm_cell_set_expr_and_value (cell, texpr, v, TRUE);
		else
			gnm_cell_set_value (cell, v);
	} else {
		// Clipboard case
		cc->texpr = texpr ? gnm_expr_top_ref (texpr) : NULL;
		cc->val = v;
	}

store_shared:
	if (texpr) {
		// We own a ref to texpr at this point.  Store or discard.
		if (expr_id > 0)
			g_hash_table_insert (state->expr_map,
					     GINT_TO_POINTER (expr_id),
					     (gpointer)texpr);
		else
			gnm_expr_top_unref (texpr);
	}

done:
	go_format_unref (value_fmt);
	g_free (value_result);
}

static void
xml_sax_merge (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	XMLSaxParseState *state = (XMLSaxParseState *)xin->user_state;
	GnmCellRegion *cr = state->clipboard;
	Sheet *sheet = state->sheet;
	GnmRange r;

	g_return_if_fail (xin->content->len > 0);

	if (range_parse (&r, xin->content->str, gnm_sheet_get_size (sheet))) {
		if (cr) {
			cr->merged = g_slist_prepend (cr->merged,
						      gnm_range_dup (&r));
		} else {
			gnm_sheet_merge_add (sheet, &r, FALSE,
					     GO_CMD_CONTEXT (state->context));
		}
	}
}

static void
xml_sax_filter_operator (XMLSaxParseState *state,
			 GnmFilterOp *op, xmlChar const *str)
{
	static char const *filter_cond_name[] = { "eq", "gt", "lt", "gte", "lte", "ne" };
	int i;

	for (i = G_N_ELEMENTS (filter_cond_name); i-- ; )
		if (0 == g_ascii_strcasecmp (CXML2C (str), filter_cond_name[i])) {
			*op = i;
			return;
		}

	go_io_warning (state->context, _("Unknown filter operator \"%s\""), str);
}

static void
xml_sax_filter_condition (GsfXMLIn *xin, xmlChar const **attrs)
{
	XMLSaxParseState *state = (XMLSaxParseState *)xin->user_state;
	char const *type = NULL;
	char const *val0 = NULL;
	char const *val1 = NULL;
	GnmValueType vtype0 = VALUE_EMPTY, vtype1 = VALUE_EMPTY;
	GnmFilterOp op0 = GNM_FILTER_UNUSED, op1 = GNM_FILTER_UNUSED;
	GnmFilterCondition *cond = NULL;
	gboolean top = TRUE, items = TRUE, rel_range = TRUE, is_and = FALSE;
	int tmp, cond_num = 0;
	double bucket_count = 10.;

	if (NULL == state->filter) return;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2) {
		if (attr_eq (attrs[0], "Type"))   type = CXML2C (attrs[1]);
		else if (gnm_xml_attr_int (attrs, "Index", &cond_num)) ;
		else if (gnm_xml_attr_bool (attrs, "top", &top)) ;
		else if (gnm_xml_attr_bool (attrs, "items", &items)) ;
		else if (gnm_xml_attr_bool (attrs, "rel_range", &rel_range)) ;
		else if (gnm_xml_attr_double (attrs, "count", &bucket_count));
		else if (gnm_xml_attr_bool (attrs, "IsAnd", &is_and)) ;
		else if (attr_eq (attrs[0], "Op0")) xml_sax_filter_operator (state, &op0, attrs[1]);
		else if (attr_eq (attrs[0], "Op1")) xml_sax_filter_operator (state, &op1, attrs[1]);
		/*
		 * WARNING WARNING WARING
		 * Value and ValueType are _reversed_ !!!
		 * An error in the DOM exporter was propogated to the SAX
		 * exporter and fixing this reversal would break all old files.
		 */
		else if (attr_eq (attrs[0], "ValueType0")) val0 = CXML2C (attrs[1]);
		else if (attr_eq (attrs[0], "ValueType1")) val1 = CXML2C (attrs[1]);
		else if (gnm_xml_attr_int (attrs, "Value0", &tmp)) vtype0 = tmp;
		else if (gnm_xml_attr_int (attrs, "Value1", &tmp)) vtype1 = tmp;
	}

	if (NULL == type) {
		go_io_warning (state->context, _("Missing filter type"));
	} else if (0 == g_ascii_strcasecmp (type, "expr")) {
		GnmValue *v0 = NULL, *v1 = NULL;
		if (val0 && vtype0 != VALUE_EMPTY && op0 != GNM_FILTER_UNUSED)
			v0 = value_new_from_string (vtype0, val0, NULL, FALSE);
		if (val1 && vtype1 != VALUE_EMPTY && op1 != GNM_FILTER_UNUSED)
			v1 = value_new_from_string (vtype1, val1, NULL, FALSE);
		if (v0 && v1)
			cond = gnm_filter_condition_new_double (
				op0, v0, is_and, op1, v1);
		else if (v0)
			cond = gnm_filter_condition_new_single (op0, v0);
		else {
			go_io_warning (state->context, _("Malformed sheet filter condition"));
			value_release (v0);
			value_release (v1);
		}
	} else if (0 == g_ascii_strcasecmp (type, "blanks")) {
		cond = gnm_filter_condition_new_single (
			GNM_FILTER_OP_BLANKS, NULL);
	} else if (0 == g_ascii_strcasecmp (type, "noblanks")) {
		cond = gnm_filter_condition_new_single (
			GNM_FILTER_OP_NON_BLANKS, NULL);
	} else if (0 == g_ascii_strcasecmp (type, "bucket")) {
		cond = gnm_filter_condition_new_bucket
			(top, items, rel_range, bucket_count);
	} else {
		go_io_warning (state->context, _("Unknown filter type \"%s\""), type);
	}
	if (cond != NULL)
		gnm_filter_set_condition (state->filter, cond_num, cond, FALSE);
}

static void
xml_sax_filter_start (GsfXMLIn *xin, xmlChar const **attrs)
{
	XMLSaxParseState *state = (XMLSaxParseState *)xin->user_state;
	GnmRange   r;
	int i;

	xml_sax_must_have_sheet (state);
	g_return_if_fail (state->filter == NULL);

	for (i = 0; attrs != NULL && attrs[i] && attrs[i + 1] ; i += 2)
		if (attr_eq (attrs[i], "Area") &&
		    range_parse (&r, CXML2C (attrs[i + 1]), gnm_sheet_get_size (state->sheet)))
			state->filter = gnm_filter_new (state->sheet, &r, TRUE);
	if (NULL == state->filter)
		go_io_warning (state->context, _("Invalid filter, missing Area"));
}

static void
xml_sax_filter_end (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	XMLSaxParseState *state = (XMLSaxParseState *)xin->user_state;
	state->filter = NULL;
}

static void
xml_sax_read_obj (GsfXMLIn *xin, gboolean needs_cleanup,
		  char const *type_name, xmlChar const **attrs)
{
	XMLSaxParseState *state = (XMLSaxParseState *)xin->user_state;
	int tmp_int, i;
	SheetObject *so;
	SheetObjectClass *klass;
	GnmRange		anchor_r;
	GODrawingAnchorDir	anchor_dir;
	GnmSOAnchorMode anchor_mode;
	SheetObjectAnchor	anchor;
	double			f_tmp[4], *anchor_offset = NULL;

	g_return_if_fail (state->so == NULL);

	/* Old crufty IO */
	if (!strcmp (type_name, "Rectangle"))
		so = g_object_new (GNM_SO_FILLED_TYPE, NULL);
	else if (!strcmp (type_name, "Ellipse"))
		so = g_object_new (GNM_SO_FILLED_TYPE, "is-oval", TRUE, NULL);
	else if (!strcmp (type_name, "Line"))
		so = g_object_new (GNM_SO_LINE_TYPE, NULL);
	else if (!strcmp (type_name, "Arrow")) {
		GOArrow arrow;
		go_arrow_init_kite (&arrow, 8., 10., 3.);
		so = g_object_new (GNM_SO_LINE_TYPE,
				   "end-arrow", &arrow,
				   NULL);
	}

	/* Class renamed between 1.0.x and 1.2.x */
	else if (!strcmp (type_name, "GnmGraph"))
		so = sheet_object_graph_new (NULL);

	/* Class renamed in 1.2.2 */
	else if (!strcmp (type_name, "CellComment"))
		so = g_object_new (cell_comment_get_type (), NULL);

	/* Class renamed in 1.3.91 */
	else if (!strcmp (type_name, "SheetObjectGraphic"))
		so = g_object_new (GNM_SO_LINE_TYPE, NULL);
	else if (!strcmp (type_name, "SheetObjectFilled"))
		so = g_object_new (GNM_SO_FILLED_TYPE, NULL);
	else if (!strcmp (type_name, "SheetObjectText"))
		so = g_object_new (GNM_SO_FILLED_TYPE, NULL);
	else if (!strcmp (type_name, "SheetObjectComponent"))
		so = sheet_object_component_new (NULL);
	else if (!strcmp (type_name, "SheetObjectPath"))
		so = g_object_new (GNM_SO_PATH_TYPE, NULL);

	else {
		GType type = g_type_from_name (type_name);

		if (type == 0 || !g_type_is_a (type, GNM_SO_TYPE)) {
			char *str = g_strdup_printf (_("Unsupported object type '%s'"),
						     type_name);
			go_io_warning_unsupported_feature (state->context, str);
			g_free (str);
			return;
		}

		so = g_object_new (type, NULL);
		if (so == NULL)
			return;
	}

	g_return_if_fail (so != NULL);
	klass = GNM_SO_CLASS (G_OBJECT_GET_CLASS (so));
	g_return_if_fail (klass != NULL);

	state->so = so;

	anchor_dir = GOD_ANCHOR_DIR_UNKNOWN;
	anchor_mode = GNM_SO_ANCHOR_TWO_CELLS;
	/* Provide a default.  */
	anchor_r = sheet_object_get_anchor (so)->cell_bound;

	for (i = 0; attrs != NULL && attrs[i] && attrs[i + 1] ; i += 2) {
		if (attr_eq (attrs[i], "Name"))
			sheet_object_set_name (so, CXML2C (attrs[i + 1]));
		else if (xml_sax_attr_enum (attrs + i, "AnchorMode", GNM_SHEET_OBJECT_ANCHOR_MODE_TYPE, &tmp_int))
			anchor_mode = tmp_int;
		else if (attr_eq (attrs[i], "ObjectBound"))
			range_parse (&anchor_r, CXML2C (attrs[i + 1]), gnm_sheet_get_size (state->sheet));
		else if (attr_eq (attrs[i], "ObjectOffset") &&
			4 == sscanf (CXML2C (attrs[i + 1]), "%lg %lg %lg %lg",
				     f_tmp + 0, f_tmp + 1, f_tmp + 2, f_tmp + 3))
			anchor_offset = f_tmp;
		else if (gnm_xml_attr_int (attrs+i, "Direction", &tmp_int))
			anchor_dir = tmp_int;
		else if (gnm_xml_attr_int (attrs+i, "Print", &tmp_int)) {
			gboolean b = (tmp_int != 0);
			sheet_object_set_print_flag (so, &b);
		}
	}

	/* Patch problems introduced in some 1.7.x versions that stored
	 * comments in merged cells with the full rectangle of the merged cell
	 * rather than just the top left corner */
	if (G_OBJECT_TYPE (so) == GNM_CELL_COMMENT_TYPE)
		anchor_r.end = anchor_r.start;

	sheet_object_anchor_init (&anchor, &anchor_r, anchor_offset, anchor_dir, anchor_mode);
	sheet_object_set_anchor (so, &anchor);

	if (NULL != klass->prep_sax_parser)
		(klass->prep_sax_parser) (so, xin, attrs, state->convs);
	if (needs_cleanup) {
		/* Put in something to get gnm_xml_finish_obj called */
		static GsfXMLInNode const dtd[] = {
		  GSF_XML_IN_NODE (STYLE, STYLE, -1, "", GSF_XML_NO_CONTENT, NULL, NULL),
		  GSF_XML_IN_NODE_END
		};
		static GsfXMLInDoc *doc = NULL;
		if (NULL == doc) {
			doc = gsf_xml_in_doc_new (dtd, NULL);
			gnm_xml_in_doc_dispose_on_exit (&doc);
		}
		// We need to pass state there because xin->user_state might
		// have changed, see #751217
		gsf_xml_in_push_state (xin, doc, state,
			(GsfXMLInExtDtor) gnm_xml_finish_obj, attrs);
	}
}

static void
xml_sax_object_start (GsfXMLIn *xin, xmlChar const **attrs)
{
	char const *type_name = xin->node->name;
	maybe_update_progress (xin);
	xml_sax_read_obj (xin, FALSE, type_name, attrs);
}

static void
xml_sax_object_end (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	gnm_xml_finish_obj (xin, xin->user_state);
	/*
	 * WARNING: the object is not completely finished at this
	 * time.  Any handler installed by gog_object_sax_push_parser
	 * has not yet been called.  As a consequence, we cannot
	 * update the GUI here.
	 */
}

static GnmValue *
parse_constraint_side (const char *s, const GnmParsePos *pp)
{
	GODateConventions const *date_conv = sheet_date_conv (pp->sheet);
	GnmValue *v = format_match_number (s, NULL, date_conv);

	if (!v) {
		GnmExprParseFlags flags = GNM_EXPR_PARSE_DEFAULT;
		v = value_new_cellrange_parsepos_str (pp, s, flags);
	}

	return v;
}

static void
xml_sax_solver_constr_start (GsfXMLIn *xin, xmlChar const **attrs)
{
	int type = 0;
	GnmSolverConstraint *c;
	Sheet *sheet = gnm_xml_in_cur_sheet (xin);
	GnmSolverParameters *sp = sheet->solver_parameters;
	int lhs_col = 0, lhs_row = 0, rhs_col = 0, rhs_row = 0;
	int cols = 1, rows = 1;
	gboolean old = FALSE;
	GnmParsePos pp;

	c = gnm_solver_constraint_new (sheet);

	parse_pos_init_sheet (&pp, sheet);

	for (; attrs && attrs[0] && attrs[1] ; attrs += 2) {
		if (gnm_xml_attr_int (attrs, "Lcol", &lhs_col) ||
		    gnm_xml_attr_int (attrs, "Lrow", &lhs_row) ||
		    gnm_xml_attr_int (attrs, "Rcol", &rhs_col) ||
		    gnm_xml_attr_int (attrs, "Rrow", &rhs_row) ||
		    gnm_xml_attr_int (attrs, "Cols", &cols) ||
		    gnm_xml_attr_int (attrs, "Rows", &rows))
			old = TRUE;
		else if (gnm_xml_attr_int (attrs, "Type", &type))
			; /* Nothing */
		else if (attr_eq (attrs[0], "lhs")) {
			GnmValue *v = parse_constraint_side (CXML2C (attrs[1]),
							     &pp);
			gnm_solver_constraint_set_lhs (c, v);
		} else if (attr_eq (attrs[0], "rhs")) {
			GnmValue *v = parse_constraint_side (CXML2C (attrs[1]),
							     &pp);
			gnm_solver_constraint_set_rhs (c, v);
		}
	}

	switch (type) {
	default:
	case 1: c->type = GNM_SOLVER_LE; break;
	case 2: c->type = GNM_SOLVER_GE; break;
	case 4: c->type = GNM_SOLVER_EQ; break;
	case 8: c->type = GNM_SOLVER_INTEGER; break;
	case 16: c->type = GNM_SOLVER_BOOLEAN; break;
	}

	if (old)
		gnm_solver_constraint_set_old (c, c->type,
					       lhs_col, lhs_row,
					       rhs_col, rhs_row,
					       cols, rows);

	sp->constraints = g_slist_append (sp->constraints, c);
}

static void
xml_sax_solver_start (GsfXMLIn *xin, xmlChar const **attrs)
{
	XMLSaxParseState *state = (XMLSaxParseState *)xin->user_state;
	Sheet *sheet;
	GnmSolverParameters *sp;
	int col = -1, row = -1;
	int ptype, mtype;
	GnmParsePos pp;
	gboolean old = FALSE;

	xml_sax_must_have_sheet (state);
	sheet = gnm_xml_in_cur_sheet (xin);
	sp = sheet->solver_parameters;

	parse_pos_init_sheet (&pp, sheet);

	for (; attrs && attrs[0] && attrs[1] ; attrs += 2) {
		if (gnm_xml_attr_int (attrs, "ModelType", &mtype)) {
			sp->options.model_type = (GnmSolverModelType)mtype;
		} else if (gnm_xml_attr_int (attrs, "ProblemType", &ptype)) {
				sp->problem_type = (GnmSolverProblemType)ptype;
		} else if (attr_eq (attrs[0], "Inputs")) {
			GnmValue *v = value_new_cellrange_parsepos_str
				(&pp,
				 CXML2C (attrs[1]),
				 GNM_EXPR_PARSE_DEFAULT);
			gnm_solver_param_set_input (sp, v);
		} else if (gnm_xml_attr_int (attrs, "TargetCol", &col) ||
			   gnm_xml_attr_int (attrs, "TargetRow", &row)) {
			old = TRUE;
		} else if (attr_eq (attrs[0], "Target")) {
			GnmValue *v = value_new_cellrange_parsepos_str
				(&pp,
				 CXML2C (attrs[1]),
				 GNM_EXPR_PARSE_DEFAULT);
			GnmSheetRange sr;
			GnmCellRef cr;
			gboolean  bad;

			bad = (!v ||
			       (gnm_sheet_range_from_value (&sr, v), !range_is_singleton (&sr.range)));
			value_release (v);
			if (bad) {
				continue;
			}

			gnm_cellref_init (&cr, sr.sheet,
					  sr.range.start.col,
					  sr.range.start.row,
					  TRUE);
			gnm_solver_param_set_target (sp, &cr);
		} else if (gnm_xml_attr_int (attrs, "MaxTime", &(sp->options.max_time_sec)) ||
			   gnm_xml_attr_int (attrs, "MaxIter", &(sp->options.max_iter)) ||
			   gnm_xml_attr_bool (attrs, "NonNeg", &(sp->options.assume_non_negative)) ||
			   gnm_xml_attr_bool (attrs, "Discr", &(sp->options.assume_discrete)) ||
			   gnm_xml_attr_bool (attrs, "AutoScale", &(sp->options.automatic_scaling)) ||
			   gnm_xml_attr_bool (attrs, "ProgramR", &(sp->options.program_report)) ||
			   gnm_xml_attr_bool (attrs, "SensitivityR", &(sp->options.sensitivity_report)))
			; /* Nothing */
	}

	if (old &&
	    col >= 0 && col < gnm_sheet_get_max_cols (sheet) &&
	    row >= 0 && row < gnm_sheet_get_max_rows (sheet)) {
		GnmCellRef cr;
		gnm_cellref_init (&cr, NULL, col, row, TRUE);
		gnm_solver_param_set_target (sp, &cr);
	}
}

static void
xml_sax_scenario_start (GsfXMLIn *xin, xmlChar const **attrs)
{
	XMLSaxParseState *state = (XMLSaxParseState *)xin->user_state;
	const char *name = "scenario";
	const char *comment = NULL;

	xml_sax_must_have_sheet (state);

	for (; attrs && attrs[0] && attrs[1] ; attrs += 2) {
		if (attr_eq (attrs[0], "Name")) {
			name = CXML2C (attrs[1]);
		} else if (attr_eq (attrs[0], "Comment")) {
			comment = CXML2C (attrs[1]);
		}
	}

	state->scenario = gnm_sheet_scenario_new (state->sheet, name);
	if (comment)
		gnm_scenario_set_comment (state->scenario, comment);
}

static void
xml_sax_scenario_end (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	XMLSaxParseState *state = (XMLSaxParseState *)xin->user_state;
	GnmScenario *sc = state->scenario;
	sc->items = g_slist_reverse (sc->items);
	gnm_sheet_scenario_add (state->sheet, sc);
	state->scenario = NULL;
}

static void
xml_sax_scenario_item_start (GsfXMLIn *xin, xmlChar const **attrs)
{
	XMLSaxParseState *state = (XMLSaxParseState *)xin->user_state;
	const char *rtxt = NULL;
	GnmParsePos pp;

	for (; attrs && attrs[0] && attrs[1] ; attrs += 2) {
		if (attr_eq (attrs[0], "Range")) {
			rtxt = CXML2C (attrs[1]);
		} else if (gnm_xml_attr_int (attrs, "ValueType",
					     &state->value_type))
			; /* Nothing */
		else if (attr_eq (attrs[0], "ValueFormat"))
			state->value_fmt = make_format (CXML2C (attrs[1]));
	}

	parse_pos_init_sheet (&pp, state->sheet);
	state->scenario_range = rtxt
		? value_new_cellrange_parsepos_str (&pp, rtxt, GNM_EXPR_PARSE_DEFAULT)
		: NULL;
}

static void
xml_sax_scenario_item_end (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	XMLSaxParseState *state = (XMLSaxParseState *)xin->user_state;
	char const * content = xin->content->str;
	int const len = xin->content->len;
	GnmScenarioItem *sci = NULL;
	GnmScenario *sc = state->scenario;
	GnmSheetRange sr;

	if (!state->scenario_range)
		goto bad;

	gnm_sheet_range_from_value (&sr, state->scenario_range);
	sci = gnm_scenario_item_new (sc->sheet);
	gnm_scenario_item_set_range (sci, &sr);

	if (len > 0) {
		GnmValue *v = value_new_from_string (state->value_type,
						     content,
						     state->value_fmt,
						     FALSE);
		if (!v)
			goto bad;
		gnm_scenario_item_set_value (sci, v);
		value_release (v);
	}

	sc->items = g_slist_prepend (sc->items, sci);
	goto out;

bad:
	g_warning ("Ignoring invalid scenario item");
	if (sci)
		gnm_scenario_item_free (sci);

out:
	state->value_type = -1;
	go_format_unref (state->value_fmt);
	state->value_fmt = NULL;
	value_release (state->scenario_range);
	state->scenario_range = NULL;
}

static void
xml_sax_named_expr_end (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	XMLSaxParseState *state = (XMLSaxParseState *)xin->user_state;
	GnmParsePos pp;
	GnmNamedExpr *nexpr;

	g_return_if_fail (state->name.name != NULL);
	g_return_if_fail (state->name.value != NULL);

	/*For the next while we have to ignore Print_areas that look like a whole sheet */
	if (0 == strcmp (state->name.name, "Print_Area")
	    && g_str_has_suffix (state->name.value, "$A$1:$IV$65536")) {
		g_free (state->name.value);
		state->name.value = NULL;
		g_free (state->name.position);
		state->name.position = NULL;
	} else {
		parse_pos_init (&pp, state->wb, state->sheet, 0, 0);
		nexpr = expr_name_add (&pp, state->name.name,
				       gnm_expr_top_new_constant (value_new_empty ()),
				       NULL,
				       TRUE,
				       NULL);
		if (nexpr) {
			state->delayed_names = g_list_prepend (state->delayed_names, state->sheet);
			state->delayed_names = g_list_prepend (state->delayed_names, state->name.value);
			state->name.value = NULL;
			state->delayed_names = g_list_prepend (state->delayed_names, state->name.position);
			state->name.position = NULL;
			state->delayed_names = g_list_prepend (state->delayed_names, nexpr);
		} else {
			g_warning ("Strangeness with defined name: %s",
				   state->name.name);
			g_free (state->name.value);
			state->name.value = NULL;
			g_free (state->name.position);
			state->name.position = NULL;
		}
	}

	g_free (state->name.name);
	state->name.name = NULL;
}

static void
xml_sax_named_expr_prop (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	XMLSaxParseState *state = (XMLSaxParseState *)xin->user_state;

	char const * content = xin->content->str;
	int const len = xin->content->len;

	switch (xin->node->user_data.v_int) {
	case 0:
		g_return_if_fail (state->name.name == NULL);
		state->name.name = g_strndup (content, len);
		break;
	case 1:
		g_return_if_fail (state->name.value == NULL);
		state->name.value = g_strndup (content, len);
		break;
	case 2:
		g_return_if_fail (state->name.position == NULL);
		state->name.position = g_strndup (content, len);
		break;
	default:
		return;
	}
}

static void
xml_sax_print_order (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	XMLSaxParseState *state = (XMLSaxParseState *)xin->user_state;

	xml_sax_must_have_sheet (state);

	state->sheet->print_info->print_across_then_down =
		(strcmp (xin->content->str, "r_then_d") == 0);
}

static void
xml_sax_print_comments_start (GsfXMLIn *xin, xmlChar const **attrs)
{
	XMLSaxParseState *state = (XMLSaxParseState *)xin->user_state;
	gint tmpi;

	xml_sax_must_have_sheet (state);

	/* In 1.11.x and later this is saved as an enum value */
	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (xml_sax_attr_enum (attrs, "placement", GNM_PRINT_COMMENT_PLACEMENT_TYPE, &tmpi))
			state->sheet->print_info->comment_placement = tmpi;
}

static void
xml_sax_print_comments_end (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	XMLSaxParseState *state = (XMLSaxParseState *)xin->user_state;

	if (xin->content->str == NULL || *xin->content->str == 0)
		/* 1.11.x or later file */
		return;

	xml_sax_must_have_sheet (state);

	if (strcmp (xin->content->str, "in_place") == 0)
		state->sheet->print_info->comment_placement =
			GNM_PRINT_COMMENTS_IN_PLACE;
	else if (strcmp (xin->content->str, "at_end") == 0)
		state->sheet->print_info->comment_placement =
			GNM_PRINT_COMMENTS_AT_END;
	else
		state->sheet->print_info->comment_placement =
			GNM_PRINT_COMMENTS_NONE;
}

static void
xml_sax_print_errors_start (GsfXMLIn *xin, xmlChar const **attrs)
{
	XMLSaxParseState *state = (XMLSaxParseState *)xin->user_state;
	gint tmpi;

	xml_sax_must_have_sheet (state);

	/* In 1.11.x and later this is saved as an enum value */
	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (xml_sax_attr_enum (attrs, "PrintErrorsAs", GNM_PRINT_ERRORS_TYPE, &tmpi))
			state->sheet->print_info->error_display = tmpi;
}


static void
xml_sax_print_errors_end (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	XMLSaxParseState *state = (XMLSaxParseState *)xin->user_state;

	if (xin->content->str == NULL || *xin->content->str == 0)
		/* 1.11.x or later file */
		return;

	xml_sax_must_have_sheet (state);

	if (strcmp (xin->content->str, "as_blank") == 0)
		state->sheet->print_info->error_display =
			GNM_PRINT_ERRORS_AS_BLANK;
	else if (strcmp (xin->content->str, "as_dashes") == 0)
		state->sheet->print_info->error_display =
			GNM_PRINT_ERRORS_AS_DASHES;
	else if (strcmp (xin->content->str, "as_na") == 0)
		state->sheet->print_info->error_display =
			GNM_PRINT_ERRORS_AS_NA;
	else
		state->sheet->print_info->error_display =
			GNM_PRINT_ERRORS_AS_DISPLAYED;
}


static void
xml_sax_orientation (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	XMLSaxParseState *state = (XMLSaxParseState *)xin->user_state;
	GnmPrintInformation *pi;
	GtkPageOrientation orient = GTK_PAGE_ORIENTATION_PORTRAIT;

	xml_sax_must_have_sheet (state);

	pi = state->sheet->print_info;

#warning TODO: we should also handle inversion
	if (strcmp (xin->content->str, "portrait") == 0)
			orient = GTK_PAGE_ORIENTATION_PORTRAIT;
	else if (strcmp (xin->content->str, "landscape") == 0)
			orient = GTK_PAGE_ORIENTATION_LANDSCAPE;

	print_info_set_paper_orientation (pi, orient);
}

static void
xml_sax_paper (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	XMLSaxParseState *state = (XMLSaxParseState *)xin->user_state;

	xml_sax_must_have_sheet (state);

	print_info_set_paper (state->sheet->print_info, xin->content->str);
}

static void
xml_sax_print_to_uri (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	XMLSaxParseState *state = (XMLSaxParseState *)xin->user_state;

	xml_sax_must_have_sheet (state);

	print_info_set_printtofile_uri (state->sheet->print_info,
					xin->content->str);
}

static void
handle_delayed_names (XMLSaxParseState *state)
{
	GList *l;

	for (l = state->delayed_names; l; l = l->next->next->next->next) {
		GnmNamedExpr *nexpr = l->data;
		char *pos_str = l->next->data;
		char *expr_str = l->next->next->data;
		Sheet *sheet = l->next->next->next->data;
		GnmParseError perr;
		GnmExprTop const *texpr;
		GnmParsePos pp;

		parse_pos_init (&pp, state->wb, sheet, 0, 0);
		if (pos_str) {
			GnmCellRef tmp;
			char const *rest;
			GnmSheetSize const *ss =
				gnm_sheet_get_size2 (sheet, state->wb);
			rest = cellref_parse (&tmp, ss, pos_str, &pp.eval);
			if (rest != NULL && *rest == '\0') {
				pp.eval.col = tmp.col;
				pp.eval.row = tmp.row;
			}
		}

		parse_error_init (&perr);
		texpr = gnm_expr_parse_str (expr_str, &pp,
					    GNM_EXPR_PARSE_DEFAULT,
					    state->convs,
					    &perr);
		if (!texpr) {
			  go_io_warning (state->context, "%s", perr.err->message);
		} else if (expr_name_check_for_loop (expr_name_name (nexpr), texpr)) {
			g_printerr ("Ignoring would-be circular definition of %s\n",
				    expr_name_name (nexpr));
			gnm_expr_top_unref (texpr);
		} else {
			nexpr->pos.eval = pp.eval;
			expr_name_set_expr (nexpr, texpr);
		}

		parse_error_free (&perr);
		g_free (expr_str);
		g_free (pos_str);
	}

	g_list_free (state->delayed_names);
	state->delayed_names = NULL;
}

static void
xml_sax_go_doc (GsfXMLIn *xin, xmlChar const **attrs)
{
	XMLSaxParseState *state = (XMLSaxParseState *)xin->user_state;
	go_doc_read (GO_DOC (state->wb), xin, attrs);
}

/****************************************************************************/

static GsfXMLInNS const content_ns[] = {
	GSF_XML_IN_NS (GNM, "http://www.gnumeric.org/v14.dtd"), /* future */
	GSF_XML_IN_NS (GNM, "http://www.gnumeric.org/v13.dtd"),
	GSF_XML_IN_NS (GNM, "http://www.gnumeric.org/v12.dtd"),
	GSF_XML_IN_NS (GNM, "http://www.gnumeric.org/v11.dtd"),
	GSF_XML_IN_NS (GNM, "http://www.gnumeric.org/v10.dtd"),
	GSF_XML_IN_NS (GNM, "http://www.gnumeric.org/v9.dtd"),
	GSF_XML_IN_NS (GNM, "http://www.gnumeric.org/v8.dtd"),
	GSF_XML_IN_NS (GNM, "http://www.gnome.org/gnumeric/v7"),
	GSF_XML_IN_NS (GNM, "http://www.gnome.org/gnumeric/v6"),
	GSF_XML_IN_NS (GNM, "http://www.gnome.org/gnumeric/v5"),
	GSF_XML_IN_NS (GNM, "http://www.gnome.org/gnumeric/v4"),
	GSF_XML_IN_NS (GNM, "http://www.gnome.org/gnumeric/v3"),
	GSF_XML_IN_NS (GNM, "http://www.gnome.org/gnumeric/v2"),
	GSF_XML_IN_NS (GNM, "http://www.gnome.org/gnumeric/"),
/* The next items are from libgsf, there is no obvious way of adding them automatically */
	GSF_XML_IN_NS (OO_NS_XSI, "http://www.w3.org/2001/XMLSchema-instance"),
	GSF_XML_IN_NS (OO_NS_OFFICE, "urn:oasis:names:tc:opendocument:xmlns:office:1.0"),
	GSF_XML_IN_NS (OO_NS_OOO,	"http://openoffice.org/2004/office"),
	GSF_XML_IN_NS (OO_NS_DC,	"http://purl.org/dc/elements/1.1/"),
	GSF_XML_IN_NS (OO_NS_XLINK,	"http://www.w3.org/1999/xlink"),
	GSF_XML_IN_NS (OO_NS_META,	"urn:oasis:names:tc:opendocument:xmlns:meta:1.0"),
	GSF_XML_IN_NS_END
};

static GsfXMLInNode gnumeric_1_0_dtd[] = {
GSF_XML_IN_NODE_FULL (START, START, -1, NULL, GSF_XML_NO_CONTENT, FALSE, TRUE, NULL, NULL, 0),
GSF_XML_IN_NODE_FULL (START, WB, GNM, "Workbook", GSF_XML_NO_CONTENT, TRUE, TRUE, &xml_sax_wb, NULL, 0),
  GSF_XML_IN_NODE (WB, WB_VERSION, GNM, "Version", GSF_XML_NO_CONTENT, &xml_sax_version, NULL),
  GSF_XML_IN_NODE (WB, WB_ATTRIBUTES, GNM, "Attributes", GSF_XML_NO_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE (WB_ATTRIBUTES, WB_ATTRIBUTE, GNM, "Attribute", GSF_XML_NO_CONTENT, NULL, &xml_sax_finish_parse_wb_attr),
      GSF_XML_IN_NODE_FULL (WB_ATTRIBUTE, WB_ATTRIBUTE_NAME, GNM, "name",   GSF_XML_CONTENT, FALSE, FALSE, NULL, &xml_sax_attr_elem, 0),
      GSF_XML_IN_NODE_FULL (WB_ATTRIBUTE, WB_ATTRIBUTE_VALUE, GNM, "value", GSF_XML_CONTENT, FALSE, FALSE, NULL, &xml_sax_attr_elem, 1),
      GSF_XML_IN_NODE (WB_ATTRIBUTE, WB_ATTRIBUTE_TYPE, GNM, "type", GSF_XML_NO_CONTENT, NULL, NULL),

  /* The old 'SummaryItem' Metadata.  Removed in 1.7.x */
  GSF_XML_IN_NODE (WB, WB_SUMMARY, GNM, "Summary", GSF_XML_NO_CONTENT, NULL, NULL),
      GSF_XML_IN_NODE (WB_SUMMARY, WB_SUMMARY_ITEM, GNM, "Item", GSF_XML_NO_CONTENT, NULL, NULL),
	GSF_XML_IN_NODE (WB_SUMMARY_ITEM, WB_SUMMARY_ITEM_NAME, GNM, "name", GSF_XML_CONTENT, NULL, NULL),
	GSF_XML_IN_NODE (WB_SUMMARY_ITEM, WB_SUMMARY_ITEM_VALUE_STR, GNM, "val-string", GSF_XML_CONTENT, NULL, NULL),
	GSF_XML_IN_NODE (WB_SUMMARY_ITEM, WB_SUMMARY_ITEM_VALUE_INT, GNM, "val-int", GSF_XML_CONTENT, NULL, NULL),

  GSF_XML_IN_NODE (WB, WB_SHEETNAME_INDEX, GNM, "SheetNameIndex", GSF_XML_NO_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE (WB_SHEETNAME_INDEX, WB_SHEETNAME, GNM, "SheetName", GSF_XML_CONTENT, &xml_sax_wb_sheetsize, &xml_sax_wb_sheetname),

  GSF_XML_IN_NODE (WB, WB_NAMED_EXPRS, GNM, "Names", GSF_XML_NO_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE (WB_NAMED_EXPRS, WB_NAMED_EXPR, GNM, "Name", GSF_XML_NO_CONTENT, NULL, &xml_sax_named_expr_end),
      GSF_XML_IN_NODE_FULL (WB_NAMED_EXPR, WB_NAMED_EXPR_NAME,	   GNM, "name",	    GSF_XML_CONTENT, FALSE, FALSE, NULL, &xml_sax_named_expr_prop, 0),
      GSF_XML_IN_NODE_FULL (WB_NAMED_EXPR, WB_NAMED_EXPR_VALUE,	   GNM, "value",    GSF_XML_CONTENT, FALSE, FALSE, NULL, &xml_sax_named_expr_prop, 1),
      GSF_XML_IN_NODE_FULL (WB_NAMED_EXPR, WB_NAMED_EXPR_POSITION, GNM, "position", GSF_XML_CONTENT, FALSE, FALSE, NULL, &xml_sax_named_expr_prop, 2),
      /* sometimes not namespaced */
      GSF_XML_IN_NODE_FULL (WB_NAMED_EXPR, WB_NAMED_EXPR_NAME_NS,     -1, "name",     GSF_XML_CONTENT, FALSE, FALSE, NULL, &xml_sax_named_expr_prop, 0),
      GSF_XML_IN_NODE_FULL (WB_NAMED_EXPR, WB_NAMED_EXPR_VALUE_NS,    -1, "value",    GSF_XML_CONTENT, FALSE, FALSE, NULL, &xml_sax_named_expr_prop, 1),
      GSF_XML_IN_NODE_FULL (WB_NAMED_EXPR, WB_NAMED_EXPR_POSITION_NS, -1, "position", GSF_XML_CONTENT, FALSE, FALSE, NULL, &xml_sax_named_expr_prop, 2),

  GSF_XML_IN_NODE (WB, WB_SHEETS, GNM, "Sheets", GSF_XML_NO_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE (WB_SHEETS, SHEET, GNM, "Sheet", GSF_XML_NO_CONTENT, &xml_sax_sheet_start, &xml_sax_sheet_end),
      GSF_XML_IN_NODE (SHEET, SHEET_NAME, GNM, "Name", GSF_XML_CONTENT, NULL, &xml_sax_sheet_name),
      GSF_XML_IN_NODE (SHEET, SHEET_MAXCOL, GNM, "MaxCol", GSF_XML_NO_CONTENT, NULL, NULL),
      GSF_XML_IN_NODE (SHEET, SHEET_MAXROW, GNM, "MaxRow", GSF_XML_NO_CONTENT, NULL, NULL),
      GSF_XML_IN_NODE (SHEET, SHEET_ZOOM, GNM, "Zoom", GSF_XML_CONTENT, NULL, &xml_sax_sheet_zoom),
      GSF_XML_IN_NODE (SHEET, SHEET_NAMED_EXPRS, GNM, "Names", GSF_XML_NO_CONTENT, NULL, NULL),
	GSF_XML_IN_NODE (SHEET_NAMED_EXPRS, SHEET_NAMED_EXPR, GNM, "Name", GSF_XML_NO_CONTENT, NULL, &xml_sax_named_expr_end),
	  GSF_XML_IN_NODE_FULL (SHEET_NAMED_EXPR, SHEET_NAMED_EXPR_NAME,     GNM, "name",     GSF_XML_CONTENT, FALSE, FALSE, NULL, &xml_sax_named_expr_prop, 0),
	  GSF_XML_IN_NODE_FULL (SHEET_NAMED_EXPR, SHEET_NAMED_EXPR_VALUE,    GNM, "value",    GSF_XML_CONTENT, FALSE, FALSE, NULL, &xml_sax_named_expr_prop, 1),
	  GSF_XML_IN_NODE_FULL (SHEET_NAMED_EXPR, SHEET_NAMED_EXPR_POSITION, GNM, "position", GSF_XML_CONTENT, FALSE, FALSE, NULL, &xml_sax_named_expr_prop, 2),

      GSF_XML_IN_NODE (SHEET, SHEET_PRINTINFO, GNM, "PrintInformation", GSF_XML_NO_CONTENT, NULL, NULL),
        GSF_XML_IN_NODE (SHEET_PRINTINFO, SHEET_PRINTUNIT, GNM, "PrintUnit", GSF_XML_NO_CONTENT, NULL, NULL),	/* ignore ancient field */
	GSF_XML_IN_NODE (SHEET_PRINTINFO, PRINT_MARGINS, GNM, "Margins", GSF_XML_NO_CONTENT, NULL, NULL),
	  GSF_XML_IN_NODE_FULL (PRINT_MARGINS, PRINT_MARGIN_TOP,    GNM, "top",	  GSF_XML_CONTENT, FALSE, FALSE, &xml_sax_print_margins, NULL, 0),
	  GSF_XML_IN_NODE_FULL (PRINT_MARGINS, PRINT_MARGIN_BOTTOM, GNM, "bottom",GSF_XML_CONTENT, FALSE, FALSE, &xml_sax_print_margins, NULL, 1),
	  GSF_XML_IN_NODE_FULL (PRINT_MARGINS, PRINT_MARGIN_LEFT,   GNM, "left",  GSF_XML_CONTENT, FALSE, FALSE, &xml_sax_print_margins, NULL, 2),
	  GSF_XML_IN_NODE_FULL (PRINT_MARGINS, PRINT_MARGIN_RIGHT,  GNM, "right", GSF_XML_CONTENT, FALSE, FALSE, &xml_sax_print_margins, NULL, 3),
	  GSF_XML_IN_NODE_FULL (PRINT_MARGINS, PRINT_MARGIN_HEADER, GNM, "header",GSF_XML_CONTENT, FALSE, FALSE, &xml_sax_print_margins, NULL, 4),
	  GSF_XML_IN_NODE_FULL (PRINT_MARGINS, PRINT_MARGIN_FOOTER, GNM, "footer",GSF_XML_CONTENT, FALSE, FALSE, &xml_sax_print_margins, NULL, 5),
	GSF_XML_IN_NODE_FULL (SHEET_PRINTINFO, V_PAGE_BREAKS, GNM, "vPageBreaks", GSF_XML_NO_CONTENT,
			      FALSE, FALSE, &xml_sax_page_breaks_begin, &xml_sax_page_breaks_end, 1),
	  GSF_XML_IN_NODE (V_PAGE_BREAKS, PAGE_BREAK, GNM, "break", GSF_XML_NO_CONTENT, &xml_sax_page_break, NULL),
	GSF_XML_IN_NODE_FULL (SHEET_PRINTINFO, H_PAGE_BREAKS, GNM, "hPageBreaks", GSF_XML_NO_CONTENT,
			      FALSE, FALSE, &xml_sax_page_breaks_begin, &xml_sax_page_breaks_end, 0),
	  GSF_XML_IN_NODE (H_PAGE_BREAKS, PAGE_BREAK, GNM, "break", GSF_XML_2ND, NULL, NULL),

	GSF_XML_IN_NODE (SHEET_PRINTINFO, PRINT_SCALE,	    GNM, "Scale",	GSF_XML_CONTENT, &xml_sax_print_scale, NULL),
	GSF_XML_IN_NODE (SHEET_PRINTINFO, PRINT_VCENTER,    GNM, "vcenter",	GSF_XML_CONTENT, &xml_sax_print_vcenter, NULL),
	GSF_XML_IN_NODE (SHEET_PRINTINFO, PRINT_HCENTER,    GNM, "hcenter",	GSF_XML_CONTENT, &xml_sax_print_hcenter, NULL),
	GSF_XML_IN_NODE (SHEET_PRINTINFO, PRINT_GRID,	    GNM, "grid",	GSF_XML_NO_CONTENT, &xml_sax_print_grid, NULL),
	GSF_XML_IN_NODE (SHEET_PRINTINFO, PRINT_DO_NOT_PRINT, GNM, "do_not_print",GSF_XML_NO_CONTENT, &xml_sax_print_do_not_print, NULL),
	GSF_XML_IN_NODE (SHEET_PRINTINFO, PRINT_PRINT_RANGE, GNM, "print_range",GSF_XML_NO_CONTENT, &xml_sax_print_print_range, NULL),
	GSF_XML_IN_NODE (SHEET_PRINTINFO, PRINT_MONO,	    GNM, "monochrome",	GSF_XML_NO_CONTENT, &xml_sax_monochrome, NULL),
	GSF_XML_IN_NODE (SHEET_PRINTINFO, PRINT_AS_DRAFT,   GNM, "draft",	GSF_XML_NO_CONTENT, NULL, NULL),
	GSF_XML_IN_NODE (SHEET_PRINTINFO, PRINT_COMMENTS,   GNM, "comments",	GSF_XML_CONTENT, &xml_sax_print_comments_start, &xml_sax_print_comments_end),
	GSF_XML_IN_NODE (SHEET_PRINTINFO, PRINT_ERRORS,   GNM, "errors",	GSF_XML_CONTENT, &xml_sax_print_errors_start, &xml_sax_print_errors_end),
	GSF_XML_IN_NODE (SHEET_PRINTINFO, PRINT_TITLES,	    GNM, "titles",	GSF_XML_NO_CONTENT, &xml_sax_print_titles, NULL),
	GSF_XML_IN_NODE (SHEET_PRINTINFO, PRINT_REPEAT_TOP, GNM, "repeat_top",	GSF_XML_NO_CONTENT, &xml_sax_repeat_top, NULL),
	GSF_XML_IN_NODE (SHEET_PRINTINFO, PRINT_REPEAT_LEFT,GNM, "repeat_left",	GSF_XML_NO_CONTENT, &xml_sax_repeat_left, NULL),
	GSF_XML_IN_NODE_FULL (SHEET_PRINTINFO, PRINT_FOOTER,	    GNM, "Footer",	GSF_XML_NO_CONTENT, FALSE, FALSE, &xml_sax_print_hf, NULL, 0),
	GSF_XML_IN_NODE_FULL (SHEET_PRINTINFO, PRINT_HEADER,	    GNM, "Header",	GSF_XML_NO_CONTENT, FALSE, FALSE, &xml_sax_print_hf, NULL, 1),
	GSF_XML_IN_NODE (SHEET_PRINTINFO, PRINT_ORDER,	    GNM, "order",	GSF_XML_CONTENT,  NULL, &xml_sax_print_order),
	GSF_XML_IN_NODE (SHEET_PRINTINFO, PRINT_PAPER,	    GNM, "paper",	GSF_XML_CONTENT,  NULL, &xml_sax_paper),
	GSF_XML_IN_NODE (SHEET_PRINTINFO, PRINT_TO_URI,	    GNM, "print-to-uri",GSF_XML_CONTENT,  NULL, &xml_sax_print_to_uri),
	GSF_XML_IN_NODE (SHEET_PRINTINFO, PRINT_ORIENT,	    GNM, "orientation",	GSF_XML_CONTENT,  NULL, &xml_sax_orientation),
	GSF_XML_IN_NODE (SHEET_PRINTINFO, PRINT_ONLY_STYLE, GNM, "even_if_only_styles", GSF_XML_CONTENT, &xml_sax_even_if_only_styles, NULL),

      GSF_XML_IN_NODE (SHEET, SHEET_STYLES, GNM, "Styles", GSF_XML_NO_CONTENT, NULL, NULL),
	GSF_XML_IN_NODE (SHEET_STYLES, STYLE_REGION, GNM, "StyleRegion", GSF_XML_NO_CONTENT, &xml_sax_style_region_start, &xml_sax_style_region_end),
	  GSF_XML_IN_NODE (STYLE_REGION, STYLE_STYLE, GNM, "Style", GSF_XML_NO_CONTENT, &xml_sax_style_start, NULL),
	    GSF_XML_IN_NODE (STYLE_STYLE, STYLE_FONT, GNM, "Font", GSF_XML_CONTENT, &xml_sax_style_font, &xml_sax_style_font_end),
	    GSF_XML_IN_NODE (STYLE_STYLE, STYLE_BORDER, GNM, "StyleBorder", GSF_XML_NO_CONTENT, NULL, NULL),
	      GSF_XML_IN_NODE_FULL (STYLE_BORDER, BORDER_TOP,     GNM, "Top",
				    GSF_XML_NO_CONTENT, FALSE, FALSE, &xml_sax_style_border, NULL, MSTYLE_BORDER_TOP),
	      GSF_XML_IN_NODE_FULL (STYLE_BORDER, BORDER_BOTTOM,  GNM, "Bottom",
				    GSF_XML_NO_CONTENT, FALSE, FALSE, &xml_sax_style_border, NULL, MSTYLE_BORDER_BOTTOM),
	      GSF_XML_IN_NODE_FULL (STYLE_BORDER, BORDER_LEFT,    GNM, "Left",
				    GSF_XML_NO_CONTENT, FALSE, FALSE, &xml_sax_style_border, NULL, MSTYLE_BORDER_LEFT),
	      GSF_XML_IN_NODE_FULL (STYLE_BORDER, BORDER_RIGHT,   GNM, "Right",
				    GSF_XML_NO_CONTENT, FALSE, FALSE, &xml_sax_style_border, NULL, MSTYLE_BORDER_RIGHT),
	      GSF_XML_IN_NODE_FULL (STYLE_BORDER, BORDER_DIAG,    GNM, "Diagonal",
				    GSF_XML_NO_CONTENT, FALSE, FALSE, &xml_sax_style_border, NULL, MSTYLE_BORDER_DIAGONAL),
	      GSF_XML_IN_NODE_FULL (STYLE_BORDER, BORDER_REV_DIAG,GNM, "Rev-Diagonal",
				    GSF_XML_NO_CONTENT, FALSE, FALSE, &xml_sax_style_border, NULL, MSTYLE_BORDER_REV_DIAGONAL),

	    GSF_XML_IN_NODE (STYLE_STYLE, STYLE_VALIDATION, GNM, "Validation", GSF_XML_NO_CONTENT, &xml_sax_validation, &xml_sax_validation_end),
	      GSF_XML_IN_NODE_FULL (STYLE_VALIDATION, STYLE_VALIDATION_EXPR0, GNM, "Expression0",
				    GSF_XML_CONTENT, FALSE, FALSE, NULL, &xml_sax_validation_expr_end, 0),
	      GSF_XML_IN_NODE_FULL (STYLE_VALIDATION, STYLE_VALIDATION_EXPR1, GNM, "Expression1",
				    GSF_XML_CONTENT, FALSE, FALSE, NULL, &xml_sax_validation_expr_end, 1),
	    GSF_XML_IN_NODE (STYLE_STYLE, STYLE_HYPERLINK, GNM, "HyperLink", GSF_XML_NO_CONTENT, &xml_sax_hlink, NULL),
	    GSF_XML_IN_NODE (STYLE_STYLE, STYLE_INPUT_MSG, GNM, "InputMessage", GSF_XML_NO_CONTENT, &xml_sax_input_msg, NULL),
	    GSF_XML_IN_NODE (STYLE_STYLE, STYLE_CONDITION, GNM, "Condition", GSF_XML_NO_CONTENT, &xml_sax_condition, &xml_sax_condition_end),
	      GSF_XML_IN_NODE_FULL (STYLE_CONDITION, STYLE_CONDITION_EXPR0, GNM, "Expression0",
				    GSF_XML_CONTENT, FALSE, FALSE, NULL, &xml_sax_condition_expr_end, 0),
	      GSF_XML_IN_NODE_FULL (STYLE_CONDITION, STYLE_CONDITION_EXPR1, GNM, "Expression1",
				    GSF_XML_CONTENT, FALSE, FALSE, NULL, &xml_sax_condition_expr_end, 1),
	      GSF_XML_IN_NODE (STYLE_CONDITION, STYLE_STYLE, GNM, "Style", GSF_XML_NO_CONTENT, NULL, NULL),

      GSF_XML_IN_NODE_FULL (SHEET, SHEET_COLS, GNM, "Cols",
			    GSF_XML_NO_CONTENT, FALSE, FALSE, &xml_sax_cols_rows, NULL, TRUE),
	GSF_XML_IN_NODE_FULL (SHEET_COLS, COL, GNM, "ColInfo",
			      GSF_XML_NO_CONTENT, FALSE, FALSE, &xml_sax_colrow, NULL, TRUE),

      GSF_XML_IN_NODE_FULL (SHEET, SHEET_ROWS, GNM, "Rows",
			    GSF_XML_NO_CONTENT, FALSE, FALSE, &xml_sax_cols_rows, NULL, FALSE),
	GSF_XML_IN_NODE_FULL (SHEET_ROWS, ROW, GNM, "RowInfo",
			      GSF_XML_NO_CONTENT, FALSE, FALSE, &xml_sax_colrow, NULL, FALSE),

      GSF_XML_IN_NODE (SHEET, SHEET_SELECTIONS, GNM, "Selections", GSF_XML_NO_CONTENT, &xml_sax_selection, &xml_sax_selection_end),
	GSF_XML_IN_NODE (SHEET_SELECTIONS, SELECTION, GNM, "Selection", GSF_XML_NO_CONTENT, &xml_sax_selection_range, NULL),

      GSF_XML_IN_NODE (SHEET, SHEET_CELLS, GNM, "Cells", GSF_XML_NO_CONTENT, NULL, NULL),
	GSF_XML_IN_NODE (SHEET_CELLS, CELL, GNM, "Cell", GSF_XML_CONTENT, &xml_sax_cell, &xml_sax_cell_content),
	  GSF_XML_IN_NODE (CELL, CELL_CONTENT, GNM, "Content", GSF_XML_CONTENT, NULL, &xml_sax_cell_content),

      GSF_XML_IN_NODE (SHEET, SHEET_MERGED_REGIONS, GNM, "MergedRegions", GSF_XML_NO_CONTENT, NULL, NULL),
	GSF_XML_IN_NODE (SHEET_MERGED_REGIONS, MERGED_REGION, GNM, "Merge", GSF_XML_CONTENT, NULL, &xml_sax_merge),

      GSF_XML_IN_NODE (SHEET, SHEET_FILTERS, GNM, "Filters", GSF_XML_NO_CONTENT, NULL, NULL),
	GSF_XML_IN_NODE (SHEET_FILTERS, FILTER, GNM, "Filter", GSF_XML_NO_CONTENT, &xml_sax_filter_start, &xml_sax_filter_end),
	  GSF_XML_IN_NODE (FILTER, FILTER_FIELD, GNM, "Field", GSF_XML_NO_CONTENT, &xml_sax_filter_condition, NULL),

      GSF_XML_IN_NODE (SHEET, SHEET_LAYOUT, GNM, "SheetLayout", GSF_XML_NO_CONTENT, &xml_sax_sheet_layout, NULL),
	GSF_XML_IN_NODE (SHEET_LAYOUT, SHEET_FREEZEPANES, GNM, "FreezePanes", GSF_XML_NO_CONTENT, &xml_sax_sheet_freezepanes, NULL),

      GSF_XML_IN_NODE (SHEET, SHEET_SOLVER, GNM, "Solver", GSF_XML_NO_CONTENT, xml_sax_solver_start, NULL),
	GSF_XML_IN_NODE (SHEET_SOLVER, SOLVER_CONSTR, GNM, "Constr", GSF_XML_NO_CONTENT, xml_sax_solver_constr_start, NULL),
      GSF_XML_IN_NODE (SHEET, SHEET_SCENARIOS, GNM, "Scenarios", GSF_XML_NO_CONTENT, NULL, NULL),
        GSF_XML_IN_NODE (SHEET_SCENARIOS, SHEET_SCENARIO, GNM, "Scenario", GSF_XML_NO_CONTENT, xml_sax_scenario_start, xml_sax_scenario_end),
          GSF_XML_IN_NODE (SHEET_SCENARIO, SCENARIO_ITEM, GNM, "Item", GSF_XML_CONTENT, xml_sax_scenario_item_start, xml_sax_scenario_item_end),

      GSF_XML_IN_NODE (SHEET, SHEET_OBJECTS, GNM, "Objects", GSF_XML_NO_CONTENT, NULL, NULL),
        /* Old crufty IO */
	GSF_XML_IN_NODE (SHEET_OBJECTS, OBJECT_ANCIENT_RECT, GNM, "Rectangle", GSF_XML_NO_CONTENT, &xml_sax_object_start, &xml_sax_object_end),
	GSF_XML_IN_NODE (SHEET_OBJECTS, OBJECT_ANCIENT_ELLIPSE, GNM, "Ellipse", GSF_XML_NO_CONTENT,	&xml_sax_object_start, &xml_sax_object_end),
	GSF_XML_IN_NODE (SHEET_OBJECTS, OBJECT_ANCIENT_ARROW, GNM, "Arrow", GSF_XML_NO_CONTENT,	&xml_sax_object_start, &xml_sax_object_end),
	GSF_XML_IN_NODE (SHEET_OBJECTS, OBJECT_ANCIENT_LINE, GNM, "Line", GSF_XML_NO_CONTENT,	&xml_sax_object_start, &xml_sax_object_end),
	/* Class renamed between 1.0.x and 1.2.x */
	GSF_XML_IN_NODE (SHEET_OBJECTS, OBJECT_OLD_GRAPH, GNM,	"GnmGraph", GSF_XML_NO_CONTENT,	&xml_sax_object_start, &xml_sax_object_end),
	/* Class renamed in 1.2.2 */
	GSF_XML_IN_NODE (SHEET_OBJECTS, OBJECT_OLD_COMMENT, GNM, "CellComment", GSF_XML_NO_CONTENT,	&xml_sax_object_start, &xml_sax_object_end),
	/* Class renamed in 1.3.91 */
	GSF_XML_IN_NODE (SHEET_OBJECTS, OBJECT_OLD_LINE, GNM, "SheetObjectGraphic", GSF_XML_NO_CONTENT, &xml_sax_object_start, &xml_sax_object_end),
	GSF_XML_IN_NODE (SHEET_OBJECTS, OBJECT_OLD_FILLED, GNM, "SheetObjectFilled", GSF_XML_NO_CONTENT, &xml_sax_object_start, &xml_sax_object_end),
	GSF_XML_IN_NODE (SHEET_OBJECTS, OBJECT_OLD_TEXT, GNM, "SheetObjectText", GSF_XML_NO_CONTENT,	&xml_sax_object_start, &xml_sax_object_end),
	GSF_XML_IN_NODE (SHEET_OBJECTS, OBJECT_GRAPH, GNM, "SheetObjectGraph", GSF_XML_NO_CONTENT,	&xml_sax_object_start, &xml_sax_object_end),
	GSF_XML_IN_NODE (SHEET_OBJECTS, OBJECT_IMAGE, GNM, "SheetObjectImage", GSF_XML_NO_CONTENT,	&xml_sax_object_start, &xml_sax_object_end),
	GSF_XML_IN_NODE (SHEET_OBJECTS, OBJECT_COMPONENT, GNM, "SheetObjectComponent", GSF_XML_NO_CONTENT, &xml_sax_object_start, &xml_sax_object_end),
	GSF_XML_IN_NODE (SHEET_OBJECTS, OBJECT_PATH, GNM, "SheetObjectPath", GSF_XML_NO_CONTENT, &xml_sax_object_start, &xml_sax_object_end),

  GSF_XML_IN_NODE (WB, WB_GEOMETRY, GNM, "Geometry", GSF_XML_NO_CONTENT, &xml_sax_wb_view, NULL),
  GSF_XML_IN_NODE (WB, WB_VIEW, GNM, "UIData", GSF_XML_NO_CONTENT, &xml_sax_wb_view, NULL),
  GSF_XML_IN_NODE (WB, WB_CALC, GNM, "Calculation", GSF_XML_NO_CONTENT, &xml_sax_calculation, NULL),
  GSF_XML_IN_NODE (WB, WB_DATE, GNM, "DateConvention", GSF_XML_CONTENT, NULL, &xml_sax_old_dateconvention),
  GSF_XML_IN_NODE (WB, GODOC, -1, "GODoc", GSF_XML_NO_CONTENT, &xml_sax_go_doc, NULL),
    GSF_XML_IN_NODE (WB, DOCUMENTMETA, OO_NS_OFFICE, "document-meta", GSF_XML_NO_CONTENT, &xml_sax_document_meta, NULL),
  GSF_XML_IN_NODE_END
};

static void
xml_sax_clipboardrange_start (GsfXMLIn *xin, xmlChar const **attrs)
{
	XMLSaxParseState *state = (XMLSaxParseState *)xin->user_state;
	int cols = -1, rows = -1, base_col = -1, base_row = -1;
	GnmCellRegion *cr;

	cr = state->clipboard = gnm_cell_region_new (state->sheet);

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2) {
		if (gnm_xml_attr_int (attrs, "Cols", &cols) ||
		    gnm_xml_attr_int (attrs, "Rows", &rows) ||
		    gnm_xml_attr_int (attrs, "BaseCol", &base_col) ||
		    gnm_xml_attr_int (attrs, "BaseRow", &base_row) ||
		    gnm_xml_attr_bool (attrs, "NotAsContent", &cr->not_as_contents))
			; /* Nothing */
		else if (attr_eq (attrs[0], "DateConvention")) {
			GODateConventions const *date_conv =
				go_date_conv_from_str (CXML2C (attrs[1]));
			if (date_conv)
				cr->date_conv = date_conv;
			else
				g_printerr ("Ignoring invalid date conventions.\n");
		}
	}

	if (cols <= 0 || rows <= 0 || base_col < 0 || base_row < 0) {
		g_printerr ("Invalid clipboard contents.\n");
	} else {
		cr->cols = cols;
		cr->rows = rows;
		cr->base.col = base_col;
		cr->base.row = base_row;
	}
}

static GsfXMLInNode clipboard_dtd[] = {
	GSF_XML_IN_NODE_FULL (START, START, -1, NULL, GSF_XML_NO_CONTENT, FALSE, TRUE, NULL, NULL, 0),
	GSF_XML_IN_NODE_FULL (START, CLIPBOARDRANGE, GNM, "ClipboardRange", GSF_XML_NO_CONTENT, TRUE, TRUE, xml_sax_clipboardrange_start, NULL, 0),
	  /* We insert "Styles" (etc) */
	GSF_XML_IN_NODE_END
};

static void
gnm_xml_in_doc_add_subset (GsfXMLInDoc *doc, GsfXMLInNode *dtd,
			   const char *id, const char *new_parent)
{
	GHashTable *parents = g_hash_table_new (g_str_hash, g_str_equal);
	GsfXMLInNode end_node = GSF_XML_IN_NODE_END;
	GArray *new_dtd = g_array_new (FALSE, FALSE, sizeof (GsfXMLInNode));

	for (; dtd->id; dtd++) {
		GsfXMLInNode node = *dtd;

		if (g_str_equal (id, dtd->id)) {
			g_hash_table_insert (parents,
					     (gpointer)id,
					     (gpointer)id);
			if (new_parent)
				node.parent_id = new_parent;
		} else if (g_hash_table_lookup (parents, dtd->parent_id))
			g_hash_table_insert (parents,
					     (gpointer)dtd->id,
					     (gpointer)dtd->id);
		else
			continue;

		g_array_append_val (new_dtd, node);
	}

	g_array_append_val (new_dtd, end_node);

	gsf_xml_in_doc_add_nodes (doc, (GsfXMLInNode*)(new_dtd->data));

	g_array_free (new_dtd, TRUE);
	g_hash_table_destroy (parents);
}


static gboolean
xml_sax_unknown (GsfXMLIn *xin, xmlChar const *elem, xmlChar const **attrs)
{
	g_return_val_if_fail (xin != NULL, FALSE);
	g_return_val_if_fail (xin->doc != NULL, FALSE);
	g_return_val_if_fail (xin->node != NULL, FALSE);

	if (GNM == xin->node->ns_id &&
	    0 == strcmp (xin->node->id, "SHEET_OBJECTS")) {
		char const *type_name = gsf_xml_in_check_ns (xin, CXML2C (elem), GNM);
		if (type_name != NULL) {
			XMLSaxParseState *state = (XMLSaxParseState *)xin->user_state;
			/* This may change xin->user_state.  */
			xml_sax_read_obj (xin, TRUE, type_name, attrs);
			/* xin->user_state hasn't been restored yet.  */
			return state->so != NULL;
		}
	}
	return FALSE;
}

static void
read_file_init_state (XMLSaxParseState *state,
		      GOIOContext *io_context,
		      WorkbookView *wb_view, Sheet *sheet)
{
	state->context = io_context;
	state->wb_view = wb_view;
	state->wb = sheet
		? sheet->workbook
		: (wb_view ? wb_view_get_workbook (wb_view) : NULL);
	state->sheet = sheet;
	state->version = GNM_XML_UNKNOWN;
	state->last_progress_update = 0;
	state->convs = gnm_xml_io_conventions ();
	state->attribute.name = state->attribute.value = NULL;
	state->name.name = state->name.value = state->name.position = NULL;
	state->style_range_init = FALSE;
	state->style = NULL;
	state->cell.row = state->cell.col = -1;
	state->seen_cell_contents = FALSE;
	state->array_rows = state->array_cols = -1;
	state->expr_id = -1;
	state->value_type = -1;
	state->value_fmt = NULL;
	state->value_result = NULL;
	state->scenario = NULL;
	state->scenario_range = NULL;
	state->filter = NULL;
	state->validation.title = state->validation.msg = NULL;
	state->validation.texpr[0] = state->validation.texpr[1] = NULL;
	state->cond = NULL;
	state->cond_save_style = NULL;
	state->expr_map = g_hash_table_new_full
		(g_direct_hash, g_direct_equal,
		 NULL, (GFreeFunc)gnm_expr_top_unref);
	state->delayed_names = NULL;
	state->so = NULL;
	state->page_breaks = NULL;
	state->clipboard = NULL;
	state->style_handler = NULL;
	state->style_handler_user = NULL;
	state->style_handler_doc = NULL;
}

static void
read_file_free_state (XMLSaxParseState *state, gboolean self)
{
	g_hash_table_destroy (state->expr_map);
	state->expr_map = NULL;

	gnm_conventions_unref (state->convs);
	state->convs = NULL;

	/*
	 * Malformed documents can cause the parser to exit early.
	 * This cleans up various bits that may be left.
	 */

	if (state->style) {
		gnm_style_unref (state->style);
		state->style = NULL;
	}

	if (state->cond_save_style) {
		gnm_style_unref (state->cond_save_style);
		state->cond_save_style = NULL;
	}

	if (state->cond) {
		gnm_style_cond_free (state->cond);
		state->cond = NULL;
	}

	if (state->style_handler_doc) {
		gsf_xml_in_doc_free (state->style_handler_doc);
		state->style_handler_doc = NULL;
	}

	if (self)
		g_free (state);
}

static gboolean
read_file_common (ReadFileWhat what, XMLSaxParseState *state,
		  GOIOContext *io_context,
		  WorkbookView *wb_view, Sheet *sheet,
		  GsfInput *input)
{
	GsfXMLInDoc     *doc;
	GnmLocale       *locale;
	gboolean         ok;

	g_return_val_if_fail (GNM_IS_WORKBOOK_VIEW (wb_view), FALSE);
	g_return_val_if_fail (GSF_IS_INPUT (input), FALSE);

	read_file_init_state (state, io_context, wb_view, sheet);

	switch (what) {
	case READ_FULL_FILE:
		state->do_progress = TRUE;
		doc = gsf_xml_in_doc_new (gnumeric_1_0_dtd, content_ns);
		break;
	case READ_CLIPBOARD:
		state->do_progress = FALSE;
		doc = gsf_xml_in_doc_new (clipboard_dtd, content_ns);
		if (!doc)
			break;
		gnm_xml_in_doc_add_subset (doc, gnumeric_1_0_dtd,
					   "SHEET_STYLES",
					   "CLIPBOARDRANGE");
		gnm_xml_in_doc_add_subset (doc, gnumeric_1_0_dtd,
					   "SHEET_COLS",
					   "CLIPBOARDRANGE");
		gnm_xml_in_doc_add_subset (doc, gnumeric_1_0_dtd,
					   "SHEET_ROWS",
					   "CLIPBOARDRANGE");
		gnm_xml_in_doc_add_subset (doc, gnumeric_1_0_dtd,
					   "SHEET_CELLS",
					   "CLIPBOARDRANGE");
		gnm_xml_in_doc_add_subset (doc, gnumeric_1_0_dtd,
					   "SHEET_MERGED_REGIONS",
					   "CLIPBOARDRANGE");
		gnm_xml_in_doc_add_subset (doc, gnumeric_1_0_dtd,
					   "SHEET_OBJECTS",
					   "CLIPBOARDRANGE");
		break;
	default:
		g_assert_not_reached ();
		return FALSE;
	}

	if (doc == NULL)
		return FALSE;

	gsf_xml_in_doc_set_unknown_handler (doc, &xml_sax_unknown);

	go_doc_init_read (GO_DOC (state->wb), input);
	gsf_input_seek (input, 0, G_SEEK_SET);

	if (state->do_progress) {
		go_io_progress_message (state->context,
					_("Reading file..."));
		go_io_value_progress_set (state->context,
					  gsf_input_size (input), 0);
	}

	locale = gnm_push_C_locale ();
	ok = gsf_xml_in_doc_parse (doc, input, state);
	handle_delayed_names (state);
	gnm_pop_C_locale (locale);

	go_doc_end_read (GO_DOC (state->wb));

	if (state->do_progress)
		go_io_progress_unset (state->context);

	if (!ok) {
		go_io_error_string (state->context,
				    _("XML document not well formed!"));
	}

	gsf_xml_in_doc_free (doc);

	return ok;
}

/* ------------------------------------------------------------------------- */

static GsfInput *
maybe_gunzip (GsfInput *input)
{
	GsfInput *gzip = gsf_input_gzip_new (input, NULL);
	if (gzip) {
		g_object_unref (input);
		return gzip;
	} else {
		gsf_input_seek (input, 0, G_SEEK_SET);
		return input;
	}
}

static GsfInput *
maybe_convert (GsfInput *input, gboolean quiet)
{
	static char const *noencheader = "<?xml version=\"1.0\"?>";
	static char const *encheader = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>";
	const size_t nelen = strlen (noencheader);
	const size_t elen = strlen (encheader);
	guint8 const *buf;
	gsf_off_t input_size;
	GString the_buffer, *buffer = &the_buffer;
	guint ui;
	GString *converted = NULL;
	char const *encoding;
	gboolean ok;
	gboolean any_numbered = FALSE;

	input_size = gsf_input_remaining (input);

	buf = gsf_input_read (input, nelen, NULL);
	if (!buf ||
	    strncmp (noencheader, (const char *)buf, nelen) != 0 ||
	    input_size >= (gsf_off_t)(G_MAXINT - elen))
		return input;

	input_size -= nelen;

	the_buffer.len = 0;
	the_buffer.allocated_len = input_size + elen + 1;
	the_buffer.str = g_try_malloc (the_buffer.allocated_len);
	if (!the_buffer.str)
		return input;

	g_string_append (buffer, encheader);
	ok = gsf_input_read (input, input_size, (guint8 *)buffer->str + elen) != NULL;
	gsf_input_seek (input, 0, G_SEEK_SET);
	if (!ok) {
		g_free (buffer->str);
		return input;
	}
	buffer->len = input_size + elen;
	buffer->str[buffer->len] = 0;

	for (ui = 0; ui < buffer->len; ui++) {
		if (buffer->str[ui] == '&' &&
		    buffer->str[ui + 1] == '#' &&
		    g_ascii_isdigit (buffer->str[ui + 2])) {
			guint start = ui;
			guint c = 0;
			ui += 2;
			while (g_ascii_isdigit (buffer->str[ui])) {
				c = c * 10 + (buffer->str[ui] - '0');
				ui++;
			}
			if (buffer->str[ui] == ';' && c >= 128 && c <= 255) {
				buffer->str[start] = c;
				g_string_erase (buffer, start + 1, ui - start);
				ui = start;
			}
			any_numbered = TRUE;
		}
	}

	encoding = go_guess_encoding (buffer->str, buffer->len, NULL, &converted, NULL);
	if (encoding && !any_numbered &&
	    converted && buffer->len == converted->len &&
	    strcmp (buffer->str, converted->str) == 0)
		quiet = TRUE;

	g_free (buffer->str);

	if (encoding) {
		gsize len = converted->len;
		g_object_unref (input);
		if (!quiet)
			g_warning ("Converted xml document with no explicit encoding from transliterated %s to UTF-8.",
				   encoding);
		return gsf_input_memory_new ((void *)g_string_free (converted, FALSE), len, TRUE);
	} else {
		if (!quiet)
			g_warning ("Failed to convert xml document with no explicit encoding to UTF-8.");
		return input;
	}
}

static void
gnm_xml_file_open (G_GNUC_UNUSED GOFileOpener const *fo, GOIOContext *io_context,
		   GoView *view, GsfInput *input)
{
	XMLSaxParseState state;
	gboolean ok;

	g_object_ref (input);
	input = maybe_gunzip (input);
	input = maybe_convert (input, FALSE);

	ok = read_file_common (READ_FULL_FILE, &state,
			       io_context, GNM_WORKBOOK_VIEW (view), NULL,
			       input);

	g_object_unref (input);

	if (ok) {
		workbook_queue_all_recalc (state.wb);

		workbook_set_saveinfo
			(state.wb,
			 GO_FILE_FL_AUTO,
			 go_file_saver_for_id ("Gnumeric_XmlIO:sax"));
	}

	read_file_free_state (&state, FALSE);
}

/* ------------------------------------------------------------------------- */

GnmCellRegion *
gnm_xml_cellregion_read (WorkbookControl *wbc, GOIOContext *io_context,
			 Sheet *sheet,
			 const char *buffer, int length)
{
	WorkbookView *wb_view;
	GsfInput *input;
	XMLSaxParseState state;
	GnmCellRegion *result;

	wb_view = wb_control_view (wbc);
	input = gsf_input_memory_new (buffer, length, FALSE);
	read_file_common (READ_CLIPBOARD, &state,
       		       io_context, wb_view, sheet,
       		       input);
	g_object_unref (input);

	result = state.clipboard;
	state.clipboard = NULL;

	read_file_free_state (&state, FALSE);

	return result;
}

/* ------------------------------------------------------------------------- */

static void
style_parser_done (GsfXMLIn *xin, XMLSaxParseState *old_state)
{
	GnmStyle *style = old_state->style;
	old_state->style_handler (xin, style, old_state->style_handler_user);
	read_file_free_state (old_state, TRUE);
}

/**
 * gnm_xml_prep_style_parser:
 * @xin:
 * @attrs:
 * @handler: (scope async):
 * @user: user data.
 *
 **/
void
gnm_xml_prep_style_parser (GsfXMLIn *xin,
			   xmlChar const **attrs,
			   GnmXmlStyleHandler handler,
			   gpointer user)
{
	static GsfXMLInNode dtd[] = {
		GSF_XML_IN_NODE (STYLE_STYLE, STYLE_STYLE, GNM, "Style", GSF_XML_NO_CONTENT, &xml_sax_style_start, NULL),
		/* Nodes added below.  */
		GSF_XML_IN_NODE_END
	};
	GsfXMLInDoc *doc = gsf_xml_in_doc_new (dtd, NULL);
	XMLSaxParseState *state = g_new0 (XMLSaxParseState, 1);

	read_file_init_state (state, NULL, NULL, NULL);
	state->style_handler = handler;
	state->style_handler_user = user;
	state->style_handler_doc = doc;
	state->style = gnm_style_new_default ();

	/* Not a full style, just those parts that do not require a sheet.  */
	gnm_xml_in_doc_add_subset (doc, gnumeric_1_0_dtd,
				   "STYLE_FONT",
				   "STYLE_STYLE");
	gnm_xml_in_doc_add_subset (doc, gnumeric_1_0_dtd,
				   "STYLE_BORDER",
				   "STYLE_STYLE");

	gsf_xml_in_push_state (xin, doc, state,
			       (GsfXMLInExtDtor)style_parser_done, attrs);
}

/* ------------------------------------------------------------------------- */

static gboolean
gnm_xml_probe_element (const xmlChar *name,
		       G_GNUC_UNUSED const xmlChar *prefix,
		       const xmlChar *URI,
		       G_GNUC_UNUSED int nb_namespaces,
		       G_GNUC_UNUSED const xmlChar **namespaces,
		       G_GNUC_UNUSED int nb_attributes,
		       G_GNUC_UNUSED int nb_defaulted,
		       G_GNUC_UNUSED const xmlChar **attributes)
{
	return 0 == strcmp (name, "Workbook") &&
		NULL != URI && NULL != strstr (URI, "gnumeric");
}

static gboolean
xml_probe (G_GNUC_UNUSED GOFileOpener const *fo, GsfInput *input, GOFileProbeLevel pl)
{
	if (pl == GO_FILE_PROBE_FILE_NAME) {
		char const *name = gsf_input_name (input);
		int len;

		if (name == NULL)
			return FALSE;

		len = strlen (name);
		if (len >= 7 && !g_ascii_strcasecmp (name+len-7, ".xml.gz"))
			return TRUE;

		name = gsf_extension_pointer (name);

		return (name != NULL &&
			(g_ascii_strcasecmp (name, "gnumeric") == 0 ||
			 g_ascii_strcasecmp (name, "xml") == 0));
	}
	/* probe by content */
	return gsf_xml_probe (input, &gnm_xml_probe_element);
}

#define XML_SAX_ID "Gnumeric_XmlIO:sax"

void
gnm_xml_sax_read_init (void)
{
	GOFileOpener *opener;
	GSList *suffixes = go_slist_create (g_strdup ("gnumeric"),
					    g_strdup ("xml"),
					    NULL);
	GSList *mimes = go_slist_create (g_strdup ("application/x-gnumeric"),
					 NULL);

	opener = go_file_opener_new
		(XML_SAX_ID,
		 _("Gnumeric XML (*.gnumeric)"),
		 suffixes, mimes,
		 xml_probe, gnm_xml_file_open);
	go_file_opener_register (opener, 50);
	g_object_unref (opener);
}

void
gnm_xml_sax_read_shutdown (void)
{
	go_file_opener_unregister
		(go_file_opener_for_id (XML_SAX_ID));
}
