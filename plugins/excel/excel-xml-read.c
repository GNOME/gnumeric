/*
 * excel-xml-read.c : Read MS Excel 2003 SpreadsheetML
 *
 * Copyright (C) 2003-2008 Jody Goldberg (jody@gnome.org)
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
#include <xml-io-version.h>
#include <sheet-view.h>
#include <sheet-style.h>
#include <sheet-merge.h>
#include <sheet-filter.h>
#include <sheet.h>
#include <ranges.h>
#include <style.h>
#include <style-border.h>
#include <style-color.h>
#include <gnm-format.h>
#include <cell.h>
#include <position.h>
#include <expr.h>
#include <expr-name.h>
#include <validation.h>
#include <value.h>
#include <selection.h>
#include <command-context.h>
#include <workbook-view.h>
#include <workbook.h>
#include <gutils.h>
#include <goffice/goffice.h>

#include <gsf/gsf-libxml.h>
#include <gsf/gsf-input.h>
#include <gsf/gsf-utils.h>
#include <gmodule.h>
#include <glib/gi18n-lib.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

/*****************************************************************************/

/* A fake value of the right type, different from all the real enum values.  */
#define VALUE_FAKE_DATETIME ((GnmValueType)42)

typedef struct {
	GnumericXMLVersion version;
	GOIOContext	*context;	/* The IOcontext managing things */
	WorkbookView	*wb_view;	/* View for the new workbook */
	Workbook	*wb;		/* The new workbook */
	Sheet		*sheet;		/* The current sheet */
	GnmCellPos	 pos;
	int              merge_across;
	GnmValueType	 val_type;
	GnmExprTop const*texpr;
	GnmRange	 array_range;
	char		*style_name;
	GnmStyle	*style;
	GnmStyle	*def_style;
	GHashTable	*style_hash;
	GsfDocMetaData	*metadata;	/* Document Properties */
} ExcelXMLReadState;

enum {
	XL_NS_SS,
	XL_NS_O,
	XL_NS_XL,
	XL_NS_XSI,
	XL_NS_C,
	XL_NS_HTML
};

/****************************************************************************/

static gboolean xl_xml_warning (GsfXMLIn *xin, char const *fmt, ...)
	G_GNUC_PRINTF (2, 3);

static gboolean
xl_xml_warning (GsfXMLIn *xin, char const *fmt, ...)
{
	ExcelXMLReadState *state = (ExcelXMLReadState *)xin->user_state;
	char *msg;
	va_list args;

	va_start (args, fmt);
	msg = g_strdup_vprintf (fmt, args);
	va_end (args);

	if (IS_SHEET (state->sheet)) {
		char *tmp;
		if (state->pos.col >= 0 && state->pos.row >= 0)
			tmp = g_strdup_printf ("%s!%s : %s",
				state->sheet->name_quoted,
				cellpos_as_string (&state->pos), msg);
		else
			tmp = g_strdup_printf ("%s : %s",
				state->sheet->name_quoted, msg);
		g_free (msg);
		msg = tmp;
	}

	go_io_warning (state->context, "%s", msg);
	g_warning ("%s", msg);
	g_free (msg);

	return FALSE; /* convenience */
}
static void
unknown_attr (GsfXMLIn *xin,
	      xmlChar const * const *attrs, char const *name)
{
	ExcelXMLReadState *state = (ExcelXMLReadState *)xin->user_state;

	g_return_if_fail (attrs != NULL);

	if (state->version == GNM_XML_LATEST)
		go_io_warning (state->context,
			_("Unexpected attribute %s::%s == '%s'."),
			name, attrs[0], attrs[1]);
}

typedef struct {
	char const * const name;
	int val;
} EnumVal;

static gboolean
attr_enum (GsfXMLIn *xin, xmlChar const * const *attrs,
	   int ns_id, char const *name, EnumVal const *enums, int *res)
{
	g_return_val_if_fail (attrs != NULL, FALSE);
	g_return_val_if_fail (attrs[0] != NULL, FALSE);
	g_return_val_if_fail (attrs[1] != NULL, FALSE);

	if (!gsf_xml_in_namecmp (xin, attrs[0], ns_id, name))
		return FALSE;

	for (; enums->name != NULL ; enums++)
		if (!strcmp (enums->name, attrs[1])) {
			*res = enums->val;
			return TRUE;
		}
	return xl_xml_warning (xin, "Invalid attribute '%s', unknown enum value '%s'",
			       name, attrs[1]);
}

static gboolean
attr_bool (GsfXMLIn *xin, xmlChar const * const *attrs,
	   int ns_id, char const *name, gboolean *res)
{
	g_return_val_if_fail (attrs != NULL, FALSE);
	g_return_val_if_fail (attrs[0] != NULL, FALSE);
	g_return_val_if_fail (attrs[1] != NULL, FALSE);

	if (!gsf_xml_in_namecmp (xin, attrs[0], ns_id, name))
		return FALSE;
	*res = g_ascii_strcasecmp ((gchar *)attrs[1], "false") && strcmp (attrs[1], "0");

	return TRUE;
}

static gboolean
attr_int (GsfXMLIn *xin, xmlChar const * const *attrs,
	  int ns_id, char const *name, int *res)
{
	char *end;
	int tmp;

	g_return_val_if_fail (attrs != NULL, FALSE);
	g_return_val_if_fail (attrs[0] != NULL, FALSE);
	g_return_val_if_fail (attrs[1] != NULL, FALSE);

	if (!gsf_xml_in_namecmp (xin, attrs[0], ns_id, name))
		return FALSE;

	errno = 0;
	tmp = strtol ((gchar *)attrs[1], &end, 10);
	if (errno == ERANGE)
		return xl_xml_warning (xin, "Invalid attribute '%s', integer '%s' is out of range",
				       name, attrs[1]);
	if (*end)
		return xl_xml_warning (xin, "Invalid attribute '%s', expected integer, received '%s'",
				       name, attrs[1]);

	*res = tmp;
	return TRUE;
}

static gboolean
attr_float (GsfXMLIn *xin, xmlChar const * const *attrs,
	    int ns_id, char const *name, gnm_float *res)
{
	char *end;
	double tmp;

	g_return_val_if_fail (attrs != NULL, FALSE);
	g_return_val_if_fail (attrs[0] != NULL, FALSE);
	g_return_val_if_fail (attrs[1] != NULL, FALSE);

	if (!gsf_xml_in_namecmp (xin, attrs[0], ns_id, name))
		return FALSE;

	tmp = gnm_strto ((gchar *)attrs[1], &end);
	if (*end)
		return xl_xml_warning (xin, "Invalid attribute '%s', expected number, received '%s'",
				       name, attrs[1]);
	*res = tmp;
	return TRUE;
}

static GnmColor *
parse_color (GsfXMLIn *xin, xmlChar const *str, char const *name)
{
	guint r, g, b;

	g_return_val_if_fail (str != NULL, NULL);

	if (3 == sscanf (str, "#%2x%2x%2x", &r, &g, &b))
		return gnm_color_new_rgb8 (r, g, b);

	xl_xml_warning (xin, "Invalid attribute '%s', expected color, received '%s'",
			name, str);
	return NULL;
}

static GnmColor *
attr_color (GsfXMLIn *xin, xmlChar const * const *attrs,
	    int ns_id, char const *name)
{
	g_return_val_if_fail (attrs != NULL, NULL);
	g_return_val_if_fail (attrs[0] != NULL, NULL);

	if (!gsf_xml_in_namecmp (xin, attrs[0], ns_id, name))
		return NULL;
	return parse_color (xin, attrs[1], name);
}

static GnmExprTop const *
xl_xml_parse_expr (GsfXMLIn *xin, xmlChar const *expr_str,
		   GnmParsePos const *pp)
{
	GnmExprTop const *texpr;
	GnmParseError err;
	if (*expr_str != '=') {
		xl_xml_warning (xin, "Invalid formula '%s' does not begin with '='", expr_str);
		return NULL;
	}
	/* Odd, some time IF and CHOOSE show up with leading spaces ??
	 * = IF(....
	 * = CHOOSE(...
	 * I wonder if it is related to some of the funky old
	 * optimizations in * xls ? */
	while (' ' == *(++expr_str))
		;
	texpr = gnm_expr_parse_str (expr_str, pp,
		GNM_EXPR_PARSE_DEFAULT, gnm_conventions_xls_r1c1,
		parse_error_init (&err));
	if (NULL == texpr)
		xl_xml_warning (xin, "'%s' %s", expr_str, err.err->message);
	parse_error_free (&err);

	return texpr;
}

static void
xl_xml_doc_prop_start (GsfXMLIn *xin, G_GNUC_UNUSED xmlChar const **attrs)
{
	ExcelXMLReadState *state = (ExcelXMLReadState *)xin->user_state;
	state->metadata = gsf_doc_meta_data_new ();
}

static void
xl_xml_doc_prop_end (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	ExcelXMLReadState *state = (ExcelXMLReadState *)xin->user_state;
	go_doc_set_meta_data (GO_DOC (state->wb), state->metadata);
	g_object_unref (state->metadata);
	state->metadata = NULL;
}

static void
xl_xml_read_prop_type (GsfXMLIn *xin, GType g_type)
{
	ExcelXMLReadState *state = (ExcelXMLReadState *)xin->user_state;
	GValue *res = g_new0 (GValue, 1);
	if (gsf_xml_gvalue_from_str (res, g_type, xin->content->str))
		gsf_doc_meta_data_insert
			(state->metadata,
			 g_strdup (xin->node->user_data.v_str), res);
	else
		 g_free (res);
}

static void
xl_xml_read_prop (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
        xl_xml_read_prop_type (xin, G_TYPE_STRING);
}

static void
xl_xml_read_prop_dt (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
        xl_xml_read_prop_type (xin, GSF_TIMESTAMP_TYPE);
}

static void
xl_xml_read_keywords (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	ExcelXMLReadState *state = (ExcelXMLReadState *)xin->user_state;
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
}

static void
xl_xml_table_start (GsfXMLIn *xin, G_GNUC_UNUSED xmlChar const **attrs)
{
	ExcelXMLReadState *state = (ExcelXMLReadState *)xin->user_state;
	state->pos.col = 0;
}

static void
xl_xml_col_start (GsfXMLIn *xin, xmlChar const **attrs)
{
	ExcelXMLReadState *state = (ExcelXMLReadState *)xin->user_state;
	int tmp, span = 1;
	gboolean  auto_fit = TRUE, hidden = FALSE;
	gnm_float width = -1;
	GnmStyle *style = NULL;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (attr_int (xin, attrs, XL_NS_SS, "Index", &tmp)) {
			if (tmp > 0)
				state->pos.col = tmp - 1;
		} else if (attr_int (xin, attrs, XL_NS_SS, "Span", &tmp)) {
			/* NOTE : span is odd.  It seems to apply as col+span
			 * rather than col+span-1 that is used for rows which
			 * is the more logical (to me) arrangement) */
			if (tmp > 0)
				span = tmp + 1;
		} else if (attr_bool (xin, attrs, XL_NS_SS, "AutoFitWidth", &auto_fit))
			;
		else if (attr_bool (xin, attrs, XL_NS_SS, "Hidden", &hidden))
			;
		else if (gsf_xml_in_namecmp (xin, attrs[0], XL_NS_SS, "StyleID"))
			style = g_hash_table_lookup (state->style_hash, attrs[1]);
		else if (attr_float (xin, attrs, XL_NS_SS, "Width", &width))
			;
		else
			unknown_attr (xin, attrs, "Column");

	if (NULL != style) {
		GnmRange r;
		r.start.col = state->pos.col;
		r.end.col   = state->pos.col + span - 1;
		r.start.row = 0;
		r.end.row  = gnm_sheet_get_max_rows (state->sheet) - 1;
		gnm_style_ref (style);
		sheet_style_set_range (state->sheet, &r, style);
	}
	if (width > 0)
		for (tmp = 0 ; tmp < span ; tmp++)
			sheet_col_set_size_pts (state->sheet,
				state->pos.col + tmp, width, !auto_fit);
	if (hidden)
		colrow_set_visibility (state->sheet, TRUE, FALSE,
			state->pos.col, state->pos.col+span-1);

	state->pos.col += span;
}

static void
xl_xml_row_start (GsfXMLIn *xin, xmlChar const **attrs)
{
	ExcelXMLReadState *state = (ExcelXMLReadState *)xin->user_state;
	int tmp, span = 1;
	gboolean  auto_fit = TRUE, hidden = FALSE;
	gnm_float height = -1;
	GnmStyle *style = NULL;

	state->pos.col = 0;
	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (attr_int (xin, attrs, XL_NS_SS, "Index", &tmp)) {
			if (tmp > 0)
				state->pos.row = tmp - 1;
		} else if (attr_int (xin, attrs, XL_NS_SS, "Span", &tmp)) {
			if (tmp > 0)
				span = tmp;
		} else if (gsf_xml_in_namecmp (xin, attrs[0], XL_NS_SS, "StyleID"))
			style = g_hash_table_lookup (state->style_hash, attrs[1]);
		else if (attr_bool (xin, attrs, XL_NS_SS, "AutoFitHeight", &auto_fit))
			;
		else if (attr_bool (xin, attrs, XL_NS_SS, "Hidden", &hidden))
			;
		else if (attr_float (xin, attrs, XL_NS_SS, "Height", &height))
			;
		else
			unknown_attr (xin, attrs, "Row");

	if (height >= 0)
		for (tmp = 0 ; tmp < span ; tmp++)
			sheet_row_set_size_pts (state->sheet, state->pos.row+tmp, height, !auto_fit);
	if (hidden)
		colrow_set_visibility (state->sheet, FALSE, FALSE,
			state->pos.row, state->pos.row+span-1);

	if (NULL != style) {
		GnmRange r;
		r.start.row = state->pos.row;
		r.end.row   = state->pos.row + span - 1;
		r.start.col = 0;
		r.end.col  = gnm_sheet_get_max_cols (state->sheet) - 1;
		gnm_style_ref (style);
		sheet_style_set_range (state->sheet, &r, style);
	}
}
static void
xl_xml_row_end (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	((ExcelXMLReadState *)xin->user_state)->pos.row++;
}
static void
xl_xml_cell_start (GsfXMLIn *xin, xmlChar const **attrs)
{
	ExcelXMLReadState *state = (ExcelXMLReadState *)xin->user_state;
	GnmStyle *style = NULL;
	int across = 0, down = 0, tmp;
	GnmParsePos pp;

	parse_pos_init (&pp, NULL, state->sheet,
		state->pos.col, state->pos.row);
	state->array_range.start.col = -1; /* poison it */
	state->val_type = VALUE_STRING;
	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (attr_int (xin, attrs, XL_NS_SS, "Index", &tmp)) {
			if (tmp > 0)
				state->pos.col = tmp -1;
		} else if (gsf_xml_in_namecmp (xin, attrs[0], XL_NS_SS, "Formula")) {
			GnmExprTop const *texpr = xl_xml_parse_expr (xin, attrs[1], &pp);
			if (NULL != texpr) {
				if (NULL != state->texpr)
					gnm_expr_top_unref (state->texpr);
				state->texpr = texpr;
			}
		} else if (gsf_xml_in_namecmp (xin, attrs[0], XL_NS_SS, "ArrayRange")) {
			GnmRangeRef rr;
			xmlChar const *end = rangeref_parse (&rr, attrs[1], &pp, gnm_conventions_xls_r1c1);
			if (end != attrs[1] && *end == '\0')
				range_init_rangeref (&state->array_range, &rr);
		} else if (attr_int (xin, attrs, XL_NS_SS, "MergeAcross", &across))
			   ;
		else if (attr_int (xin, attrs, XL_NS_SS, "MergeDown", &down))
			;
		else if (gsf_xml_in_namecmp (xin, attrs[0], XL_NS_SS, "StyleID"))
			style = g_hash_table_lookup (state->style_hash, attrs[1]);
		else
			unknown_attr (xin, attrs, "Cell");

	if (NULL != style) {
		gnm_style_ref (style);
		if (across > 0 || down > 0) {
			GnmRange r;
			r.start = r.end = state->pos;
			r.end.col += across;
			r.end.row += down;
			gnm_sheet_merge_add (state->sheet, &r, FALSE,
				GO_CMD_CONTEXT (state->context));
			sheet_style_set_range (state->sheet, &r, style);
		} else
			sheet_style_set_pos (state->sheet,
				state->pos.col, state->pos.row, style);
	}
	state->merge_across = across;
}
static void
xl_xml_cell_end (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	ExcelXMLReadState *state = (ExcelXMLReadState *)xin->user_state;
	state->pos.col += (1 + state->merge_across);
}
static void
xl_xml_data_start (GsfXMLIn *xin, xmlChar const **attrs)
{
	static EnumVal const val_types[] = {
		{ "String",	VALUE_STRING },
		{ "Number",	VALUE_FLOAT },
		{ "Boolean",	VALUE_BOOLEAN },
		{ "Error",	VALUE_ERROR },
		{ "DateTime",	VALUE_FAKE_DATETIME },
		{ NULL, 0 }
	};
	ExcelXMLReadState *state = (ExcelXMLReadState *)xin->user_state;
	int type;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (attr_enum (xin, attrs, XL_NS_SS, "Type", val_types, &type))
			state->val_type = type;
		else
			unknown_attr (xin, attrs, "CellData");
}

static void
xl_xml_data_end (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	ExcelXMLReadState *state = (ExcelXMLReadState *)xin->user_state;
	GnmCell *cell = sheet_cell_fetch (state->sheet, state->pos.col, state->pos.row);
	GnmValue *v;

	if (state->val_type == VALUE_FAKE_DATETIME) {
		GDate date;
		unsigned y, mo, d, h, mi;
		double s;
		if (6 == sscanf (xin->content->str, "%u-%u-%uT%u:%u:%lg", &y, &mo, &d, &h, &mi, &s)) {
			g_date_clear (&date, 1);
			g_date_set_dmy (&date, d, mo, y);
			if (g_date_valid (&date)) {
				unsigned d_serial = go_date_g_to_serial (&date,
					workbook_date_conv (state->wb));
				v = value_new_float (d_serial + h/24. + mi/(24.*60.) + s/(24.*60.*60.));
			} else
				v = value_new_string (xin->content->str);
		} else
			v = value_new_string (xin->content->str);
	} else if (state->val_type == VALUE_FLOAT) {
			char *end;
			v  = value_new_float (gnm_strto (xin->content->str, &end));
			if (*end)
				xl_xml_warning
					(xin, _("Invalid content of ss:data "
						"element, expected number, "
						"received '%s'"),
					 xin->content->str);
	} else
		v = value_new_from_string (state->val_type, xin->content->str,
					   NULL, FALSE);
	if (NULL != state->texpr) {
		if (NULL != v)
			gnm_cell_set_expr_and_value (cell, state->texpr, v, TRUE);
		else
			gnm_cell_set_expr (cell, state->texpr);
		gnm_expr_top_unref (state->texpr);
		state->texpr = NULL;
	} else if (NULL != v)
		gnm_cell_set_value (cell, v);
	else {
		gnm_cell_set_text (cell, xin->content->str);
		xl_xml_warning
			(xin, _("Invalid content of ss:data "
				"element, received '%s'"),
			 xin->content->str);
	}
}

static void
xl_xml_font (GsfXMLIn *xin, xmlChar const **attrs)
{
	static EnumVal const underlines[] = {
		{ "None",		UNDERLINE_NONE },
		{ "Single",		UNDERLINE_SINGLE },
		{ "SingleAccounting",	UNDERLINE_SINGLE_LOW },
		{ "Double",		UNDERLINE_DOUBLE },
		{ "DoubleAccounting",	UNDERLINE_DOUBLE_LOW },
		{ NULL, 0 }
	};
	static EnumVal const scripts[] = {
		{ "Superscript",	GO_FONT_SCRIPT_SUPER },
		{ "Subscript",		GO_FONT_SCRIPT_SUB },
		{ "None",		GO_FONT_SCRIPT_STANDARD },
		{ NULL, 0 }
	};
	ExcelXMLReadState *state = (ExcelXMLReadState *)xin->user_state;
	GnmColor *color;
	gboolean b_tmp;
	int i_tmp;
	gnm_float tmp;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (gsf_xml_in_namecmp (xin, attrs[0], XL_NS_SS, "Family"))
			;
		else if (gsf_xml_in_namecmp (xin, attrs[0], XL_NS_SS, "FontName"))
			;
		else if (attr_float (xin, attrs, XL_NS_SS, "Size", &tmp))
			gnm_style_set_font_size	(state->style, tmp);
		else if (attr_bool (xin, attrs, XL_NS_SS, "Bold", &b_tmp))
			gnm_style_set_font_bold (state->style, b_tmp);
		else if (attr_bool (xin, attrs, XL_NS_SS, "Italic", &b_tmp))
			gnm_style_set_font_italic (state->style, b_tmp);
		else if (attr_bool (xin, attrs, XL_NS_SS, "StrikeThrough", &b_tmp))
			gnm_style_set_font_strike (state->style, b_tmp);
		else if (attr_enum (xin, attrs, XL_NS_SS, "Underline", underlines, &i_tmp))
			gnm_style_set_font_uline (state->style, i_tmp);
		else if (attr_enum (xin, attrs, XL_NS_SS, "VerticalAlign", scripts, &i_tmp))
			gnm_style_set_font_script (state->style, i_tmp);
		else if ((color = attr_color (xin, attrs, XL_NS_SS, "Color")))
			gnm_style_set_font_color (state->style, color);
		else
			unknown_attr (xin, attrs, "Style::Font");
}

static void
xl_xml_alignment (GsfXMLIn *xin, xmlChar const **attrs)
{
	static EnumVal const valignments [] = {
		{ "Bottom", GNM_VALIGN_BOTTOM },
		{ "Center", GNM_VALIGN_CENTER },
		{ "Distributed", GNM_VALIGN_DISTRIBUTED },
		{ "Justify", GNM_VALIGN_JUSTIFY },
		{ "Top", GNM_VALIGN_TOP },
		{ NULL, 0 }
	};
	static EnumVal const halignments [] = {
		{ "Center", GNM_HALIGN_CENTER },
		{ "CenterAcrossSelection", GNM_HALIGN_CENTER_ACROSS_SELECTION },
		{ "Distributed", GNM_HALIGN_DISTRIBUTED },
		{ "Fill", GNM_HALIGN_FILL },
		{ "Justify", GNM_HALIGN_JUSTIFY },
		{ "Left", GNM_HALIGN_LEFT },
		{ "Right", GNM_HALIGN_RIGHT },

		{ NULL, 0 }
	};
	ExcelXMLReadState *state = (ExcelXMLReadState *)xin->user_state;
	gboolean b_tmp;
	int i_tmp;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (attr_int (xin, attrs, XL_NS_SS, "Rotate", &i_tmp))
			gnm_style_set_rotation (state->style, i_tmp);
		else if (attr_bool (xin, attrs, XL_NS_SS, "WrapText", &b_tmp))
			gnm_style_set_wrap_text (state->style, b_tmp);
		else if (attr_enum (xin, attrs, XL_NS_SS, "Vertical", valignments, &i_tmp))
			gnm_style_set_align_v (state->style, i_tmp);
		else if (attr_enum (xin, attrs, XL_NS_SS, "Horizontal", halignments, &i_tmp))
			gnm_style_set_align_h (state->style, i_tmp);
		else if (attr_int (xin, attrs, XL_NS_SS, "Indent",  &i_tmp))
			gnm_style_set_indent (state->style, i_tmp);
}

static void
xl_xml_border (GsfXMLIn *xin, xmlChar const **attrs)
{
	static EnumVal const sides[] = {
		{ "Top",		GNM_STYLE_BORDER_TOP },
		{ "Bottom",		GNM_STYLE_BORDER_BOTTOM },
		{ "Right",		GNM_STYLE_BORDER_RIGHT },
		{ "Left",		GNM_STYLE_BORDER_LEFT },
		{ "DiagonalLeft",	GNM_STYLE_BORDER_REV_DIAG },
		{ "DiagonalRight",	GNM_STYLE_BORDER_DIAG },
		{ NULL, 0 }
	};
	static EnumVal const line_styles[] = {
		{ "Continuous",		GNM_STYLE_BORDER_HAIR },		/* 1 2 3 */
		{ "Dash",		GNM_STYLE_BORDER_DASHED },		/* 1 2 */
		{ "DashDot",		GNM_STYLE_BORDER_DASH_DOT },	/* 1 2 */
		{ "DashDotDot",		GNM_STYLE_BORDER_DASH_DOT_DOT },	/* 1 2 */
		{ "Dot",		GNM_STYLE_BORDER_DOTTED },		/* 1 */
		{ "Double",		GNM_STYLE_BORDER_DOUBLE },		/* 3 */
		{ "SlantDashDot",	GNM_STYLE_BORDER_SLANTED_DASH_DOT },/* 2 */
		{ NULL, 0 }
	};
	ExcelXMLReadState *state = (ExcelXMLReadState *)xin->user_state;
	GnmStyleBorderLocation location  = GNM_STYLE_BORDER_EDGE_MAX;
	GnmStyleBorderType	    line_type = GNM_STYLE_BORDER_MAX;
	GnmBorder	   *border;
	GnmColor	   *color = NULL, *new_color;
	int		   weight = 1, tmp;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (attr_enum (xin, attrs, XL_NS_SS, "Position", sides, &tmp))
			location = tmp;
		else if (attr_enum (xin, attrs, XL_NS_SS, "LineStyle", line_styles, &tmp))
			line_type = tmp;
		else if (attr_int (xin, attrs, XL_NS_SS, "Weight", &weight))
			;
		else if ((new_color = attr_color (xin, attrs, XL_NS_SS, "Color"))) {
			style_color_unref (color);
			color = new_color;
		} else
			unknown_attr (xin, attrs, "Style::Border");

	switch (line_type) {
	default:
		break;
	case GNM_STYLE_BORDER_HAIR:
		if (weight == 2)
			line_type = GNM_STYLE_BORDER_THIN;
		else if (weight >= 3)
			line_type = GNM_STYLE_BORDER_THICK;
		break;
	case GNM_STYLE_BORDER_DASHED:
		if (weight >= 2)
			line_type = GNM_STYLE_BORDER_MEDIUM_DASH;
		break;
	case GNM_STYLE_BORDER_DASH_DOT:
		if (weight >= 2)
			line_type = GNM_STYLE_BORDER_MEDIUM_DASH_DOT;
		break;
	case GNM_STYLE_BORDER_DASH_DOT_DOT:
		if (weight >= 2)
			line_type = GNM_STYLE_BORDER_MEDIUM_DASH_DOT_DOT;
		break;
	}

	if (color != NULL &&
	    location  != GNM_STYLE_BORDER_EDGE_MAX &&
	    line_type != GNM_STYLE_BORDER_MAX) {
		border = gnm_style_border_fetch (line_type,
			color, gnm_style_border_get_orientation (location));
		gnm_style_set_border (state->style,
				      GNM_STYLE_BORDER_LOCATION_TO_STYLE_ELEMENT (location),
				      border);
	} else if (color)
		    style_color_unref (color);
}

static void
xl_xml_num_interior (GsfXMLIn *xin, xmlChar const **attrs)
{
	static EnumVal const patterns[] = {
		{ "Solid",	1 },
		{ "Gray75",	2 },
		{ "Gray50",	3 },
		{ "Gray25",	4 },
		{ "Gray125",	5 },
		{ "Gray0625",	6 },
		{ "HorzStripe", 7 },
		{ "VertStripe", 8 },
		{ "ReverseDiagStripe", 9 },
		{ "DiagStripe", 10 },
		{ "DiagCross",	11 },
		{ "ThickDiagCross", 12 },
		{ "ThinHorzStripe", 13 },
		{ "ThinVertStripe", 14 },
		{ "ThinReverseDiagStripe", 15 },
		{ "ThinDiagStripe", 16 },
		{ "ThinHorzCross",  17 },
		{ "ThinDiagCross",  18 },
		{ NULL, 0 }
	};
	ExcelXMLReadState *state = (ExcelXMLReadState *)xin->user_state;
	GnmColor *color;
	int tmp;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if ((color = attr_color (xin, attrs, XL_NS_SS, "Color")))
			gnm_style_set_back_color (state->style, color);
		else if (attr_enum (xin, attrs, XL_NS_SS, "Pattern", patterns, &tmp))
			gnm_style_set_pattern (state->style, tmp);
		else if ((color = attr_color (xin, attrs, XL_NS_SS, "PatternColor")))
			gnm_style_set_pattern_color (state->style, color);
		else
			unknown_attr (xin, attrs, "Style::Interior");
}

static void
xl_xml_num_fmt (GsfXMLIn *xin, xmlChar const **attrs)
{
	static struct {
		char const *name;
		GOFormatMagic id;
	} named_magic_formats [] = {
		{ "General Date",        GO_FORMAT_MAGIC_SHORT_DATETIME },
		{ "Long Date",           GO_FORMAT_MAGIC_LONG_DATE },
		{ "Medium Date",         GO_FORMAT_MAGIC_MEDIUM_DATE },
		{ "Short Date",          GO_FORMAT_MAGIC_SHORT_DATE },
		{ "Long Time",           GO_FORMAT_MAGIC_LONG_TIME },
		{ "Medium Time",         GO_FORMAT_MAGIC_MEDIUM_TIME },
		{ "Short Time",          GO_FORMAT_MAGIC_SHORT_TIME },
		{ NULL, 0 }
	};
	static struct {
		char const *name;
		char const *format;
	} named_formats [] = {
		{ "General Number",     "General" },
		{ "Currency",       	"$#,##0.00_);[Red](#,##0.00)" },
		{ "Euro Currency",     	"[$EUR-2]#,##0.00_);[Red](#,##0.00)" },
		{ "Fixed",              "0.00" },
		{ "Standard",           "#,##0.00" },	/* number, 2dig, +sep */
		{ "Percent",            "0.00%" },	/* std percent */
		{ "Scientific",         "0.00E+00" },	/* std scientific */
		{ "Yes/No",		"\"Yes\";\"Yes\";\"No\"" },
		{ "True/False",		"\"True\";\"True\";\"False\"" },
		{ "On/Off",		"\"On\";\"On\";\"Off\"" },
		{ NULL, NULL }
	};
	ExcelXMLReadState *state = (ExcelXMLReadState *)xin->user_state;
	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (gsf_xml_in_namecmp (xin, attrs[0], XL_NS_SS, "Format")) {
			GOFormat *fmt = NULL;
			int i;

			for (i = 0 ; named_formats[i].name ; i++)
				if (0 == strcmp (attrs[1], named_formats[i].name))
					fmt = go_format_new_from_XL (named_formats[i].format);

			if (NULL == fmt)
				for (i = 0 ; named_magic_formats[i].name ; i++)
					if (0 == strcmp (attrs[1], named_magic_formats[i].name))
						fmt = go_format_new_magic (named_magic_formats[i].id);

			if (NULL == fmt)
				fmt = go_format_new_from_XL (attrs[1]);
			gnm_style_set_format (state->style, fmt);
			go_format_unref (fmt);
		} else
			unknown_attr (xin, attrs, "Style::NumberFormat");
}

static void
xl_xml_style_start (GsfXMLIn *xin, xmlChar const **attrs)
{
	ExcelXMLReadState *state = (ExcelXMLReadState *)xin->user_state;
	char const *id = NULL;
	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (gsf_xml_in_namecmp (xin, attrs[0], XL_NS_SS, "ID"))
			id = attrs[1];
		else if (gsf_xml_in_namecmp (xin, attrs[0], XL_NS_SS, "Name"))
			; /* does anything use this ? */
		else
			unknown_attr (xin, attrs, "Style");

	if (id == NULL)
		return;

	g_return_if_fail (state->style == NULL);

	state->style = (state->def_style != NULL)
		? gnm_style_dup (state->def_style)
		: gnm_style_new_default ();
	if (!strcmp (id, "Default"))
		state->def_style = state->style;
	g_hash_table_replace (state->style_hash, g_strdup (id), state->style);
}

static void
xl_xml_style_end (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	((ExcelXMLReadState *)xin->user_state)->style = NULL;
}

static void
xl_xml_named_range (GsfXMLIn *xin, xmlChar const **attrs)
{
	ExcelXMLReadState *state = (ExcelXMLReadState *)xin->user_state;
	char const *name = NULL;
	char const *expr_str = NULL;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (gsf_xml_in_namecmp (xin, attrs[0], XL_NS_SS, "Name"))
			name = attrs[1];
		else if (gsf_xml_in_namecmp (xin, attrs[0], XL_NS_SS, "RefersTo"))
			expr_str = attrs[1];

	if (NULL != name && NULL != expr_str) {
		GnmParsePos pp;
		GnmExprTop const *texpr = xl_xml_parse_expr (xin, expr_str,
			parse_pos_init (&pp, state->wb, NULL, 0, 0));
		g_warning ("%s = %s", name, expr_str);
		if (NULL != texpr)
			expr_name_add (&pp, name, texpr, NULL, TRUE, NULL);
	}
}

static void
xl_xml_sheet_start (GsfXMLIn *xin, xmlChar const **attrs)
{
	ExcelXMLReadState *state = (ExcelXMLReadState *)xin->user_state;
	char const *name = NULL;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (gsf_xml_in_namecmp (xin, attrs[0], XL_NS_SS, "Name"))
			name = attrs[1];
		else
			unknown_attr (xin, attrs, "Worksheet");

	if (name != NULL){
		g_return_if_fail (state->sheet == NULL);
		state->sheet =  workbook_sheet_by_name (state->wb, name);
		if (state->sheet == NULL) {
		  state->sheet = sheet_new (state->wb, name,
					    16384, 1048576);  /* FIXME */
			workbook_sheet_attach (state->wb, state->sheet);
		}

		/* Flag a respan here in case nothing else does */
		sheet_flag_recompute_spans (state->sheet);
		state->pos.col = state->pos.row = 0;
	}
}

static void
xl_xml_sheet_end (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	ExcelXMLReadState *state = (ExcelXMLReadState *)xin->user_state;

	g_return_if_fail (state->sheet != NULL);

	state->sheet = NULL;
}

static void
xl_xml_pane (GsfXMLIn *xin, G_GNUC_UNUSED xmlChar const **attrs)
{
	ExcelXMLReadState *state = (ExcelXMLReadState *)xin->user_state;
	state->pos.col = 0;
	state->pos.row = 0;
}
static void
xl_xml_editpos_row (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	ExcelXMLReadState *state = (ExcelXMLReadState *)xin->user_state;
	char *end;
	int tmp;
	errno = 0;
	tmp = strtol (xin->content->str, &end, 10);
	if (errno != ERANGE && *end == '\0')
		state->pos.row = tmp;
}
static void
xl_xml_editpos_col (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	ExcelXMLReadState *state = (ExcelXMLReadState *)xin->user_state;
	char *end;
	int tmp;
	errno = 0;
	tmp = strtol (xin->content->str, &end, 10);
	if (errno != ERANGE && *end == '\0')
		state->pos.col = tmp;
}
static void
xl_xml_selection (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	ExcelXMLReadState *state = (ExcelXMLReadState *)xin->user_state;
	SheetView *sv = sheet_get_view (state->sheet, state->wb_view);
	char const *ptr, *end;
	GnmParsePos pp;
	GnmRangeRef rr;
	GnmRange    r;

	g_return_if_fail (sv != NULL);

	parse_pos_init_sheet (&pp, state->sheet);
	sv_selection_reset (sv);
	for (ptr = xin->content->str; ptr && *ptr ; ) {
		end = rangeref_parse (&rr, ptr, &pp, gnm_conventions_xls_r1c1);
		if (end != ptr) {
			range_init_rangeref (&r, &rr);
			sv_selection_add_full
				(sv,
				 state->pos.col, state->pos.row,
				 r.start.col, r.start.row,
				 r.end.col, r.end.row,
				 GNM_SELECTION_MODE_ADD);

			if (*end != ',')
				break;
			ptr = end + 1;
		} else
			break;
	}
}

static void
xl_xml_auto_filter_start (GsfXMLIn *xin, G_GNUC_UNUSED xmlChar const **attrs)
{
	ExcelXMLReadState *state = (ExcelXMLReadState *)xin->user_state;
	GnmFilter *filter;
	GnmParsePos pp;
	GnmRangeRef rr;
	GnmRange r;
	char const *end, *range = NULL;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (gsf_xml_in_namecmp (xin, attrs[0], XL_NS_XL, "Range"))
			range = attrs[1];
		else
			unknown_attr (xin, attrs, "AutoFilter");

	if (range)
	{
		parse_pos_init_sheet (&pp, state->sheet);
		end = rangeref_parse (&rr, range, &pp, gnm_conventions_xls_r1c1);
		if (end != range)
		{
			range_init_rangeref (&r, &rr);
			filter = gnm_filter_new (state->sheet, &r, TRUE);
			gnm_filter_reapply (filter);
		}
	}
}

/****************************************************************************/

static GsfXMLInNS content_ns[] = {
	GSF_XML_IN_NS (XL_NS_SS,   "urn:schemas-microsoft-com:office:spreadsheet"),
	GSF_XML_IN_NS (XL_NS_SS,   "http://schemas.microsoft.com/office/excel/2003/xml"),
	GSF_XML_IN_NS (XL_NS_O,    "urn:schemas-microsoft-com:office:office"),
	GSF_XML_IN_NS (XL_NS_XL,   "urn:schemas-microsoft-com:office:excel"),
	GSF_XML_IN_NS (XL_NS_C,    "urn:schemas-microsoft-com:office:component:spreadsheet"),
	GSF_XML_IN_NS (XL_NS_HTML, "http://www.w3.org/TR/REC-html40"),
	GSF_XML_IN_NS (XL_NS_XSI,  "http://www.w3.org/2001/XMLSchema-instance"),

	{ NULL, 0 }
};

static GsfXMLInNode const excel_xml_dtd[] = {
GSF_XML_IN_NODE_FULL (START, START, -1, NULL, GSF_XML_NO_CONTENT, FALSE, TRUE, NULL, NULL, 0),
GSF_XML_IN_NODE_FULL (START, WORKBOOK, XL_NS_SS, "Workbook", GSF_XML_NO_CONTENT, FALSE, TRUE, NULL, NULL, 0),
  GSF_XML_IN_NODE (WORKBOOK, DOC_PROP, XL_NS_O, "DocumentProperties", GSF_XML_NO_CONTENT, &xl_xml_doc_prop_start, &xl_xml_doc_prop_end),
    GSF_XML_IN_NODE_FULL (DOC_PROP, PROP_AUTHOR,         XL_NS_O, "Author", GSF_XML_CONTENT, FALSE, FALSE, NULL, &xl_xml_read_prop, .v_str = GSF_META_NAME_INITIAL_CREATOR),
    GSF_XML_IN_NODE_FULL (DOC_PROP, PROP_CATEGORY,       XL_NS_O, "Category", GSF_XML_CONTENT, FALSE, FALSE, NULL, &xl_xml_read_prop, .v_str = GSF_META_NAME_CATEGORY),
    GSF_XML_IN_NODE_FULL (DOC_PROP, PROP_CEATED,         XL_NS_O, "Created", GSF_XML_CONTENT, FALSE, FALSE, NULL, &xl_xml_read_prop_dt, .v_str = GSF_META_NAME_DATE_CREATED),
    GSF_XML_IN_NODE_FULL (DOC_PROP, PROP_COMPANY,        XL_NS_O, "Company", GSF_XML_CONTENT, FALSE, FALSE, NULL, &xl_xml_read_prop, .v_str = GSF_META_NAME_COMPANY),
    GSF_XML_IN_NODE_FULL (DOC_PROP, PROP_DESCRIPTION,    XL_NS_O, "Description", GSF_XML_CONTENT, FALSE, FALSE, NULL, &xl_xml_read_prop, .v_str = GSF_META_NAME_DESCRIPTION),
    GSF_XML_IN_NODE_FULL (DOC_PROP, PROP_HYPERLINK_BASE, XL_NS_O, "HyperlinkBase", GSF_XML_CONTENT, FALSE, FALSE, NULL, &xl_xml_read_prop, .v_str = "xlsx:HyperlinkBase"),
    GSF_XML_IN_NODE_FULL (DOC_PROP, PROP_KEYWORDS,       XL_NS_O, "Keywords", GSF_XML_CONTENT, FALSE, FALSE, NULL, &xl_xml_read_keywords, .v_str = GSF_META_NAME_KEYWORDS),

    GSF_XML_IN_NODE_FULL (DOC_PROP, PROP_LAST_AUTHOR,    XL_NS_O, "LastAuthor", GSF_XML_CONTENT, FALSE, FALSE, NULL, &xl_xml_read_prop, .v_str = GSF_META_NAME_CREATOR),
    GSF_XML_IN_NODE_FULL (DOC_PROP, PROP_LAST_PRINTED,   XL_NS_O, "LastPrinted", GSF_XML_CONTENT, FALSE, FALSE, NULL, &xl_xml_read_prop_dt, .v_str = GSF_META_NAME_PRINT_DATE),
    GSF_XML_IN_NODE_FULL (DOC_PROP, PROP_LAST_SAVED,     XL_NS_O, "LastSaved", GSF_XML_CONTENT, FALSE, FALSE, NULL, &xl_xml_read_prop_dt, .v_str = GSF_META_NAME_DATE_MODIFIED),
    GSF_XML_IN_NODE (DOC_PROP, PROP_LINES,               XL_NS_O, "Lines", GSF_XML_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE_FULL (DOC_PROP, PROP_MANAGER,        XL_NS_O, "Manager", GSF_XML_CONTENT, FALSE, FALSE, NULL, &xl_xml_read_prop, .v_str = GSF_META_NAME_MANAGER),
    GSF_XML_IN_NODE (DOC_PROP, PROP_REVISION,            XL_NS_O, "Revision", GSF_XML_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE_FULL (DOC_PROP, PROP_SUBJECT,        XL_NS_O, "Subject", GSF_XML_CONTENT, FALSE, FALSE, NULL, &xl_xml_read_prop, .v_str = GSF_META_NAME_SUBJECT),
    GSF_XML_IN_NODE_FULL (DOC_PROP, PROP_TITLE,          XL_NS_O, "Title", GSF_XML_CONTENT, FALSE, FALSE, NULL, &xl_xml_read_prop, .v_str = GSF_META_NAME_TITLE),
    GSF_XML_IN_NODE (DOC_PROP, PROP_TOTAL_TIME,          XL_NS_O, "TotalTime", GSF_XML_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE (DOC_PROP, PROP_VERSION,             XL_NS_O, "Version", GSF_XML_CONTENT, NULL, NULL),

  GSF_XML_IN_NODE (WORKBOOK, DOC_SETTINGS, XL_NS_O, "OfficeDocumentSettings", GSF_XML_NO_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE (DOC_SETTINGS, DOC_COLORS, XL_NS_O, "Colors", GSF_XML_NO_CONTENT, NULL, NULL),
      GSF_XML_IN_NODE (DOC_COLORS, DOC_COLOR,  XL_NS_O, "Color", GSF_XML_NO_CONTENT, NULL, NULL),
        GSF_XML_IN_NODE (DOC_COLOR,  COLOR_INDEX, XL_NS_O, "Index", GSF_XML_NO_CONTENT, NULL, NULL),
        GSF_XML_IN_NODE (DOC_COLOR,  COLOR_RGB, XL_NS_O, "RGB", GSF_XML_NO_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE (DOC_SETTINGS, DOC_COMPONENTS, XL_NS_O, "DownloadComponents", GSF_XML_NO_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE (DOC_SETTINGS, DOC_COMPONENTS_LOCATION, XL_NS_O, "LocationOfComponents", GSF_XML_NO_CONTENT, NULL, NULL),
  GSF_XML_IN_NODE (WORKBOOK, WB_VIEW, XL_NS_XL, "ExcelWorkbook", GSF_XML_NO_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE (WB_VIEW, TAB_RATIO, XL_NS_XL, "TabRatio", GSF_XML_NO_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE (WB_VIEW, SUPBOOK, XL_NS_XL, "SupBook", GSF_XML_NO_CONTENT, NULL, NULL),
      GSF_XML_IN_NODE (SUPBOOK, SUP_DLL, XL_NS_XL, "Dll", GSF_XML_NO_CONTENT, NULL, NULL),
      GSF_XML_IN_NODE (SUPBOOK, SUP_EXTERNNAME, XL_NS_XL, "ExternName", GSF_XML_NO_CONTENT, NULL, NULL),
        GSF_XML_IN_NODE (SUP_EXTERNNAME, EXTERNNAME_NAME, XL_NS_XL, "Name", GSF_XML_NO_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE (WB_VIEW, VIEW_HEIGHT, XL_NS_XL, "WindowHeight", GSF_XML_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE (WB_VIEW, VIEW_WIDTH,  XL_NS_XL, "WindowWidth",  GSF_XML_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE (WB_VIEW, VIEW_TOP_X,  XL_NS_XL, "WindowTopX",   GSF_XML_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE (WB_VIEW, VIEW_TOP_Y,  XL_NS_XL, "WindowTopY",   GSF_XML_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE (WB_VIEW, PROTECT_STRUCTURE, XL_NS_XL, "ProtectStructure",   GSF_XML_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE (WB_VIEW, PROTECT_WINDOWS,   XL_NS_XL, "ProtectWindows",     GSF_XML_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE (WB_VIEW, NATURAL_LANGUAGE,  XL_NS_XL, "AcceptLabelsInFormulas", GSF_XML_CONTENT, NULL, NULL),
  GSF_XML_IN_NODE (WORKBOOK, STYLES, XL_NS_SS, "Styles", GSF_XML_NO_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE (STYLES, STYLE, XL_NS_SS,  "Style", GSF_XML_NO_CONTENT, &xl_xml_style_start, &xl_xml_style_end),
      GSF_XML_IN_NODE (STYLE, ALIGNMENT,  XL_NS_SS, "Alignment", GSF_XML_NO_CONTENT, &xl_xml_alignment, NULL),
      GSF_XML_IN_NODE (STYLE, BORDERS,    XL_NS_SS, "Borders",   GSF_XML_NO_CONTENT, NULL, NULL),
        GSF_XML_IN_NODE (BORDERS, BORDER, XL_NS_SS, "Border",    GSF_XML_NO_CONTENT, &xl_xml_border, NULL),
      GSF_XML_IN_NODE (STYLE, FONT,       XL_NS_SS, "Font",      GSF_XML_NO_CONTENT, &xl_xml_font, NULL),
      GSF_XML_IN_NODE (STYLE, INTERIOR,   XL_NS_SS, "Interior",  GSF_XML_NO_CONTENT, &xl_xml_num_interior, NULL),
      GSF_XML_IN_NODE (STYLE, NUM_FMT,    XL_NS_SS, "NumberFormat", GSF_XML_NO_CONTENT, &xl_xml_num_fmt, NULL),
      GSF_XML_IN_NODE (STYLE, PROTECTION, XL_NS_SS, "Protection", GSF_XML_NO_CONTENT, NULL, NULL),
  GSF_XML_IN_NODE (WORKBOOK, NAMES, XL_NS_SS, "Names", GSF_XML_NO_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE (NAMES, NAMED_RANGE, XL_NS_SS, "NamedRange", GSF_XML_NO_CONTENT, &xl_xml_named_range, NULL),

  GSF_XML_IN_NODE_FULL (WORKBOOK, WORKSHEET, XL_NS_SS, "Worksheet", GSF_XML_NO_CONTENT, FALSE, TRUE, &xl_xml_sheet_start, &xl_xml_sheet_end, 0),
    GSF_XML_IN_NODE (WORKSHEET, TABLE, XL_NS_SS, "Table", GSF_XML_NO_CONTENT, &xl_xml_table_start, NULL),
      GSF_XML_IN_NODE (TABLE, COLUMN, XL_NS_SS, "Column", GSF_XML_NO_CONTENT, &xl_xml_col_start, NULL),
      GSF_XML_IN_NODE (TABLE, ROW, XL_NS_SS, "Row", GSF_XML_NO_CONTENT, &xl_xml_row_start, &xl_xml_row_end),
	GSF_XML_IN_NODE_FULL (ROW, CELL, XL_NS_SS, "Cell", GSF_XML_NO_CONTENT, FALSE, TRUE, &xl_xml_cell_start, &xl_xml_cell_end, 0),
          GSF_XML_IN_NODE (CELL, NAMED_CELL, XL_NS_SS, "NamedCell", GSF_XML_NO_CONTENT, NULL, NULL),
          GSF_XML_IN_NODE_FULL (CELL, CELL_DATA, XL_NS_SS, "Data", GSF_XML_CONTENT, GSF_XML_NO_CONTENT, TRUE, &xl_xml_data_start, &xl_xml_data_end, 0),
	    GSF_XML_IN_NODE_FULL (CELL_DATA, HTML_B,    XL_NS_HTML, "B",    GSF_XML_SHARED_CONTENT, TRUE, FALSE, NULL, NULL, 0),
	    GSF_XML_IN_NODE_FULL (CELL_DATA, HTML_I,    XL_NS_HTML, "I",    GSF_XML_SHARED_CONTENT, TRUE, FALSE, NULL, NULL, 1),
	    GSF_XML_IN_NODE_FULL (CELL_DATA, HTML_U,    XL_NS_HTML, "U",    GSF_XML_SHARED_CONTENT, TRUE, FALSE, NULL, NULL, 2),
	    GSF_XML_IN_NODE_FULL (CELL_DATA, HTML_FONT, XL_NS_HTML, "Font", GSF_XML_SHARED_CONTENT, TRUE, FALSE, NULL, NULL, 3),
	    GSF_XML_IN_NODE_FULL (CELL_DATA, HTML_S,    XL_NS_HTML, "S",    GSF_XML_SHARED_CONTENT, TRUE, FALSE, NULL, NULL, 4),
	    GSF_XML_IN_NODE_FULL (CELL_DATA, HTML_SUP,  XL_NS_HTML, "Sup",  GSF_XML_SHARED_CONTENT, TRUE, FALSE, NULL, NULL, 5),
	    GSF_XML_IN_NODE_FULL (CELL_DATA, HTML_SUB,  XL_NS_HTML, "Sub",  GSF_XML_SHARED_CONTENT, TRUE, FALSE, NULL, NULL, 6),
    GSF_XML_IN_NODE (WORKSHEET, OPTIONS, XL_NS_XL, "WorksheetOptions", GSF_XML_NO_CONTENT, NULL, NULL),
      GSF_XML_IN_NODE (OPTIONS, TOP_ROW, XL_NS_XL, "TopRowVisible", GSF_XML_NO_CONTENT, NULL, NULL),
      GSF_XML_IN_NODE (OPTIONS, UNSYNCED, XL_NS_XL, "Unsynced", GSF_XML_NO_CONTENT, NULL, NULL),	/* ?? */
      GSF_XML_IN_NODE (OPTIONS, SELECTED, XL_NS_XL, "Selected", GSF_XML_NO_CONTENT, NULL, NULL),	/* ?? */
      GSF_XML_IN_NODE (OPTIONS, PANES, XL_NS_XL, "Panes", GSF_XML_NO_CONTENT, NULL, NULL),
        GSF_XML_IN_NODE (PANES, PANE, XL_NS_XL,  "Pane", GSF_XML_NO_CONTENT, &xl_xml_pane, NULL),
          GSF_XML_IN_NODE (PANE, PANE_NUM, XL_NS_XL,  "Number", GSF_XML_CONTENT, NULL, NULL),
          GSF_XML_IN_NODE (PANE, PANE_ACTIVEROW, XL_NS_XL,  "ActiveRow", GSF_XML_CONTENT, NULL, &xl_xml_editpos_row),
          GSF_XML_IN_NODE (PANE, PANE_ACTIVECOL, XL_NS_XL,  "ActiveCol", GSF_XML_CONTENT, NULL, &xl_xml_editpos_col),
          GSF_XML_IN_NODE (PANE, PANE_SELECTION, XL_NS_XL,  "RangeSelection", GSF_XML_CONTENT, NULL, &xl_xml_selection),
      GSF_XML_IN_NODE (OPTIONS, PAGE_SETUP, XL_NS_XL, "PageSetup", GSF_XML_NO_CONTENT, NULL, NULL),
	GSF_XML_IN_NODE (PAGE_SETUP, PAGE_LAYOUT, XL_NS_XL, "Layout", GSF_XML_NO_CONTENT, NULL, NULL),
	GSF_XML_IN_NODE (PAGE_SETUP, PAGE_HEADER, XL_NS_XL, "Header", GSF_XML_NO_CONTENT, NULL, NULL),
	GSF_XML_IN_NODE (PAGE_SETUP, PAGE_FOOTER, XL_NS_XL, "Footer", GSF_XML_NO_CONTENT, NULL, NULL),
	GSF_XML_IN_NODE (PAGE_SETUP, PAGE_MARGINS, XL_NS_XL, "PageMargins", GSF_XML_NO_CONTENT, NULL, NULL),
      GSF_XML_IN_NODE (OPTIONS, PRINT, XL_NS_XL, "Print", GSF_XML_NO_CONTENT, NULL, NULL),
	GSF_XML_IN_NODE (PRINT, PRINT_NUMBER_COPIES, XL_NS_XL, "NumberofCopies", GSF_XML_NO_CONTENT, NULL, NULL),
	GSF_XML_IN_NODE (PRINT, PRINT_VALID_INFO,  XL_NS_XL, "ValidPrinterInfo", GSF_XML_NO_CONTENT, NULL, NULL),
	GSF_XML_IN_NODE (PRINT, PRINT_PAPER_SIZE,  XL_NS_XL, "PaperSizeIndex", GSF_XML_CONTENT, NULL, NULL),
	GSF_XML_IN_NODE (PRINT, PRINT_HRES,	   XL_NS_XL, "HorizontalResolution", GSF_XML_CONTENT, NULL, NULL),
	GSF_XML_IN_NODE (PRINT, PRINT_VRES,	   XL_NS_XL, "VerticalResolution", GSF_XML_CONTENT, NULL, NULL),

      GSF_XML_IN_NODE (OPTIONS, PROT_OBJS,	 XL_NS_XL, "ProtectObjects", GSF_XML_CONTENT, NULL, NULL),
      GSF_XML_IN_NODE (OPTIONS, PROT_SCENARIOS,  XL_NS_XL, "ProtectScenarios", GSF_XML_CONTENT, NULL, NULL),
      GSF_XML_IN_NODE (OPTIONS, PAGEBREAK_ZOOM,	 XL_NS_XL, "PageBreakZoom", GSF_XML_NO_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE (WORKSHEET, COND_FMT, XL_NS_XL, "ConditionalFormatting", GSF_XML_NO_CONTENT, NULL, NULL),
      GSF_XML_IN_NODE (COND_FMT, COND_RANGE,	XL_NS_XL, "Range", GSF_XML_NO_CONTENT, NULL, NULL),
      GSF_XML_IN_NODE (COND_FMT, COND,		XL_NS_XL, "Condition", GSF_XML_NO_CONTENT, NULL, NULL),
        GSF_XML_IN_NODE (COND, COND_VALUE1,	XL_NS_XL, "Value1", GSF_XML_NO_CONTENT, NULL, NULL),
        GSF_XML_IN_NODE (COND, COND_STYLE,	XL_NS_XL, "Format", GSF_XML_NO_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE (WORKSHEET, AUTO_FILTER,    XL_NS_XL, "AutoFilter", GSF_XML_NO_CONTENT, &xl_xml_auto_filter_start, NULL),
    GSF_XML_IN_NODE (WORKSHEET, WS_NAMES,       XL_NS_SS, "Names", GSF_XML_NO_CONTENT, NULL, NULL),
      GSF_XML_IN_NODE (WS_NAMES, WS_NAMED_RANGE,   XL_NS_SS, "NamedRange", GSF_XML_NO_CONTENT, NULL, NULL),

  GSF_XML_IN_NODE_END
};

G_MODULE_EXPORT void
excel_xml_file_open (GOFileOpener const *fo, GOIOContext *context,
		     WorkbookView *wbv, GsfInput *input);

G_MODULE_EXPORT gboolean
excel_xml_file_probe (GOFileOpener const *fo, GsfInput *input, GOFileProbeLevel pl);

static gboolean
xl_xml_probe_start_element (const xmlChar *name,
			    G_GNUC_UNUSED const xmlChar *prefix,
			    const xmlChar *URI,
			    G_GNUC_UNUSED int nb_namespaces,
			    G_GNUC_UNUSED const xmlChar **namespaces,
			    G_GNUC_UNUSED int nb_attributes,
			    G_GNUC_UNUSED int nb_defaulted,
			    G_GNUC_UNUSED const xmlChar **attributes)
{
	/* starts with <Workbook> in namespace "schemas-microsoft-com:office:spreadsheet" */
	return 0 == strcmp (name, "Workbook") &&
		NULL != URI &&
		NULL != strstr (URI, "schemas-microsoft-com:office:spreadsheet");
}

gboolean
excel_xml_file_probe (G_GNUC_UNUSED GOFileOpener const *fo,
		      GsfInput *input, GOFileProbeLevel pl)
{
	if (pl == GO_FILE_PROBE_FILE_NAME) {
		char const *ext;
		char const *name = gsf_input_name (input);
		return  NULL != name &&
			NULL != (ext = gsf_extension_pointer (name)) &&
			0 == g_ascii_strcasecmp (ext, "xml");
	}

	return gsf_xml_probe (input, &xl_xml_probe_start_element);
}

void
excel_xml_file_open (G_GNUC_UNUSED GOFileOpener const *fo,
		     GOIOContext *io_context,
		     WorkbookView *wb_view, GsfInput *input)
{
	GsfXMLInDoc *doc;
	ExcelXMLReadState state;
	GnmLocale	*locale;

	locale = gnm_push_C_locale ();

	state.context	= io_context;
	state.wb_view	= wb_view;
	state.wb	= wb_view_get_workbook (wb_view);
	state.sheet	= NULL;
	state.style	= NULL;
	state.def_style	= NULL;
	state.texpr	= NULL;
	state.style_hash = g_hash_table_new_full (g_str_hash, g_str_equal,
		(GDestroyNotify)g_free, (GDestroyNotify) gnm_style_unref);

	doc = gsf_xml_in_doc_new (excel_xml_dtd, content_ns);
	if (!gsf_xml_in_doc_parse (doc, input, &state))
		go_io_error_string (io_context, _("XML document not well formed!"));
	gsf_xml_in_doc_free (doc);

	g_hash_table_destroy (state.style_hash);

	gnm_pop_C_locale (locale);
}
