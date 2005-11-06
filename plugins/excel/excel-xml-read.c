/* vim: set sw=8: */

/*
 * excel-xml-read.c : Read MS Excel's xml
 *
 * Copyright (C) 2003-2005 Jody Goldberg (jody@gnome.org)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
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
#include "excel-xml-read.h"
#include "xml-io-version.h"
#include "sheet-view.h"
#include "sheet-style.h"
#include "sheet-merge.h"
#include "sheet.h"
#include "ranges.h"
#include "style.h"
#include "style-border.h"
#include "style-color.h"
#include "gnm-format.h"
#include "cell.h"
#include "position.h"
#include "expr.h"
#include "expr-name.h"
#include "print-info.h"
#include "validation.h"
#include "value.h"
#include "selection.h"
#include "command-context.h"
#include "workbook-view.h"
#include "workbook.h"
#include <goffice/app/error-info.h>
#include <goffice/app/io-context.h>
#include <goffice/app/go-plugin.h>
#include <goffice/utils/datetime.h>

#include <gsf/gsf-libxml.h>
#include <gsf/gsf-input.h>
#include <gmodule.h>
#include <glib/gi18n.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

/*****************************************************************************/

typedef struct {
	GnumericXMLVersion version;
	IOContext 	*context;	/* The IOcontext managing things */
	WorkbookView	*wb_view;	/* View for the new workbook */
	Workbook	*wb;		/* The new workbook */
	Sheet		*sheet;		/* The current sheet */
	GnmCellPos	 pos;
	GnmValueType	 val_type;
	GnmExpr const   *expr;
	GnmRange	 array_range;
	char		*style_name;
	GnmStyle	*style;
	GnmStyle	*def_style;
	GHashTable	*style_hash;
} ExcelXMLReadState;

enum {
	XL_NS_SS,
	XL_NS_O,
	XL_NS_XL,
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

	gnm_io_warning (state->context, "%s", msg);
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
		gnm_io_warning (state->context,
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
		return style_color_new_i8 (r, g, b);

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

static GnmExpr const *
xl_xml_parse_expr (GsfXMLIn *xin, xmlChar const *expr_str,
		   GnmParsePos const *pp)
{
	GnmExpr const *expr;
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
	expr = gnm_expr_parse_str (expr_str, pp,
		GNM_EXPR_PARSE_DEFAULT, gnm_expr_conventions_r1c1,
		parse_error_init (&err));
	if (NULL == expr)
		xl_xml_warning (xin, "'%s' %s", expr_str, err.err->message);
	parse_error_free (&err);
	return expr;
}

static void
xl_xml_table_start (GsfXMLIn *xin, xmlChar const **attrs)
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
		r.end.row  = SHEET_MAX_ROWS - 1;
		gnm_style_ref (style);
		sheet_style_set_range (state->sheet, &r, style);
	}
	if (width >= 0.)
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

	if (height >= 0.)
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
		r.end.col  = SHEET_MAX_COLS - 1;
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
	GnmExpr const *expr;
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
			expr = xl_xml_parse_expr (xin, attrs[1], &pp);
			if (NULL != expr) {
				if (NULL != state->expr)
					gnm_expr_unref (state->expr);
				state->expr = expr;
			}
		} else if (gsf_xml_in_namecmp (xin, attrs[0], XL_NS_SS, "ArrayRange")) {
			GnmRangeRef rr;
			xmlChar const *end = rangeref_parse (&rr, attrs[1], &pp, gnm_expr_conventions_r1c1);
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
			sheet_merge_add (state->sheet, &r, FALSE,
				GO_CMD_CONTEXT (state->context));
			sheet_style_set_range (state->sheet, &r, style);
		} else
			sheet_style_set_pos (state->sheet,
				state->pos.col, state->pos.row, style);
	}
}
static void
xl_xml_cell_end (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	((ExcelXMLReadState *)xin->user_state)->pos.col++;
}
static void
xl_xml_data_start (GsfXMLIn *xin, xmlChar const **attrs)
{
	static EnumVal const val_types[] = {
		{ "String",	VALUE_STRING },
		{ "Number",	VALUE_FLOAT },
		{ "Boolean",	VALUE_BOOLEAN },
		{ "Error",	VALUE_ERROR },
		{ "DateTime",	0x42 /* some cheesy magic */ },
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
	
	if (state->val_type == 0x42) { /* see cheesy magic above */
		GDate date;
		unsigned y, mo, d, h, mi;
		double s;
		if (6 == sscanf (xin->content->str, "%u-%u-%uT%u:%u:%lg", &y, &mo, &d, &h, &mi, &s)) {
			g_date_clear (&date, 1);
			g_date_set_dmy (&date, d, mo, y);
			if (g_date_valid (&date)) {
				unsigned d_serial = datetime_g_to_serial (&date,
					workbook_date_conv (state->wb));
				v = value_new_float (d_serial + h/24. + mi/(24.*60.) + s/(24.*60.*60.));
			} else
				v = value_new_string (xin->content->str);
		} else
			v = value_new_string (xin->content->str);
	} else
		v = value_new_from_string (state->val_type, xin->content->str, NULL, FALSE);
	if (NULL != state->expr) {
		if (NULL != v)
			cell_set_expr_and_value (cell, state->expr, v, TRUE);
		else
			cell_set_expr (cell, state->expr);
		gnm_expr_unref (state->expr);
		state->expr = NULL;
	} else if (NULL != v)
		cell_set_value (cell, v);
	else
		cell_set_text (cell, xin->content->str);
}

static void
xl_xml_font (GsfXMLIn *xin, xmlChar const **attrs)
{
	static EnumVal const underlines[] = {
		{ "None",		UNDERLINE_NONE },
		{ "Single",		UNDERLINE_SINGLE },
		{ "SingleAccounting",	UNDERLINE_SINGLE },
		{ "Double",		UNDERLINE_DOUBLE },
		{ "DoubleAccounting",	UNDERLINE_DOUBLE },
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
		{ "Bottom", VALIGN_BOTTOM },
		{ "Center", VALIGN_CENTER },
		{ "Distributed", VALIGN_DISTRIBUTED },
		{ "Justify", VALIGN_JUSTIFY },
		{ "Top", VALIGN_TOP },
		{ NULL, 0 }
	};
	static EnumVal const halignments [] = {
		{ "Center", HALIGN_CENTER },
		{ "CenterAcrossSelection", HALIGN_CENTER_ACROSS_SELECTION },
		{ "Distributed", HALIGN_DISTRIBUTED },
		{ "Fill", HALIGN_FILL },
		{ "Justify", HALIGN_JUSTIFY },
		{ "Left", HALIGN_LEFT },
		{ "Right", HALIGN_RIGHT },

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
		{ "Top",		STYLE_BORDER_TOP },
		{ "Bottom",		STYLE_BORDER_BOTTOM },
		{ "Right",		STYLE_BORDER_RIGHT },
		{ "Left",		STYLE_BORDER_LEFT },
		{ "DiagonalLeft",	STYLE_BORDER_REV_DIAG },
		{ "DiagonalRight",	STYLE_BORDER_DIAG },
		{ NULL, 0 }
	};
	static EnumVal const line_styles[] = {
		{ "Continuous",		STYLE_BORDER_HAIR },		/* 1 2 3 */
		{ "Dash",		STYLE_BORDER_DASHED },		/* 1 2 */
		{ "DashDot",		STYLE_BORDER_DASH_DOT },	/* 1 2 */
		{ "DashDotDot",		STYLE_BORDER_DASH_DOT_DOT },	/* 1 2 */
		{ "Dot",		STYLE_BORDER_DOTTED },		/* 1 */
		{ "Double",		STYLE_BORDER_DOUBLE },		/* 3 */
		{ "SlantDashDot",	STYLE_BORDER_SLANTED_DASH_DOT },/* 2 */
		{ NULL, 0 }
	};
	ExcelXMLReadState *state = (ExcelXMLReadState *)xin->user_state;
	StyleBorderLocation location  = STYLE_BORDER_EDGE_MAX;
	StyleBorderType	    line_type = STYLE_BORDER_MAX;
	GnmBorder	   *border;
	GnmColor 	   *color = NULL, *new_color;
	int		   weight = 1, tmp;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (attr_enum (xin, attrs, XL_NS_SS, "Position", sides, &tmp))
			location = tmp;
		else if (attr_enum (xin, attrs, XL_NS_SS, "LineStyle", line_styles, &tmp))
			line_type = tmp;
		else if (attr_int (xin, attrs, XL_NS_SS, "Weight", &weight))
			;
		else if ((new_color = attr_color (xin, attrs, XL_NS_SS, "Color"))) {
			if (color)
				style_color_unref (color);
			color = new_color;
		} else
			unknown_attr (xin, attrs, "Style::Border");

	switch (line_type) {
	default:
		break;
	case STYLE_BORDER_HAIR:
		if (weight == 2)
			line_type = STYLE_BORDER_THIN;
		else if (weight >= 3)
			line_type = STYLE_BORDER_THICK;
		break;
	case STYLE_BORDER_DASHED:
		if (weight >= 2)
			line_type = STYLE_BORDER_MEDIUM_DASH;
		break;
	case STYLE_BORDER_DASH_DOT:
		if (weight >= 2)
			line_type = STYLE_BORDER_MEDIUM_DASH_DOT;
		break;
	case STYLE_BORDER_DASH_DOT_DOT:
		if (weight >= 2)
			line_type = STYLE_BORDER_MEDIUM_DASH_DOT_DOT;
		break;
	}

	if (color != NULL &&
	    location  != STYLE_BORDER_EDGE_MAX &&
	    line_type != STYLE_BORDER_MAX) {
		border = style_border_fetch (line_type,
			color, style_border_get_orientation (location));
		gnm_style_set_border (state->style, MSTYLE_BORDER_TOP + location, border);
	} else if (color)
		    style_color_unref (color);
}

static void
xl_xml_num_interior (GsfXMLIn *xin, xmlChar const **attrs)
{
	static EnumVal const patterns[] = {
		{ "Solid",	1 },
		{ "Gray75",	2 },
		{ "Gray50", 	3 },
		{ "Gray25", 	4 },
		{ "Gray125", 	5 },
		{ "Gray0625", 	6 },
		{ "HorzStripe", 7 },
		{ "VertStripe", 8 },
		{ "ReverseDiagStripe", 9 },
		{ "DiagStripe", 10 },
		{ "DiagCross", 	11 },
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
	ExcelXMLReadState *state = (ExcelXMLReadState *)xin->user_state;
	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (gsf_xml_in_namecmp (xin, attrs[0], XL_NS_SS, "Format")) {
			GOFormat *fmt = NULL;
			if (!strcmp (attrs[1], "Percent"))
				fmt = go_format_default_percentage ();
			else if (!strcmp (attrs[1], "Short Time"))
				fmt = go_format_default_time ();

			if (NULL != fmt) 
				go_format_ref (fmt);
			else if (!strcmp (attrs[1], "Fixed"))
				fmt = go_format_new_from_XL ("0.00", FALSE);
			else
				fmt = go_format_new_from_XL (attrs[1], FALSE);
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
		GnmExpr const *expr = xl_xml_parse_expr (xin, expr_str,
			parse_pos_init (&pp, state->wb, NULL, 0, 0));
		g_warning ("%s = %s", name, expr_str);
		if (NULL != expr)
			expr_name_add (&pp, name, expr, NULL, TRUE, NULL);
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
			state->sheet = sheet_new (state->wb, name);
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
xl_xml_pane (GsfXMLIn *xin, xmlChar const **attrs)
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
		end = rangeref_parse (&rr, ptr, &pp, gnm_expr_conventions_r1c1);
		if (end != ptr) {
			range_init_rangeref (&r, &rr);
			fprintf (stderr, "%s = ", ptr);
			range_dump (&r, "\n");
			sv_selection_add_range (sv,
				state->pos.col, state->pos.row,
				r.start.col, r.start.row,
				r.end.col, r.end.row);

			if (*end != ',')
				break;
			ptr = end + 1;
		} else
			break;
	}
}

/****************************************************************************/

static GsfXMLInNS content_ns[] = {
	GSF_XML_IN_NS (XL_NS_SS,   "urn:schemas-microsoft-com:office:spreadsheet"),
	GSF_XML_IN_NS (XL_NS_O,    "urn:schemas-microsoft-com:office:office"),
	GSF_XML_IN_NS (XL_NS_XL,   "urn:schemas-microsoft-com:office:excel"),
	GSF_XML_IN_NS (XL_NS_HTML, "http://www.w3.org/TR/REC-html40"),
	{ NULL }
};

static GsfXMLInNode const excel_xml_dtd[] = {
GSF_XML_IN_NODE_FULL (START, START, -1, NULL, FALSE, FALSE, TRUE, NULL, NULL, 0),
GSF_XML_IN_NODE_FULL (START, WORKBOOK, XL_NS_SS, "Workbook", FALSE, FALSE, TRUE, NULL, NULL, 0),
  GSF_XML_IN_NODE (WORKBOOK, DOC_PROP, XL_NS_O, "DocumentProperties", FALSE, NULL, NULL),
    GSF_XML_IN_NODE (DOC_PROP, PROP_AUTHOR,	 XL_NS_O, "Author",     TRUE, NULL, NULL),
    GSF_XML_IN_NODE (DOC_PROP, PROP_LAST_AUTHOR, XL_NS_O, "LastAuthor", TRUE, NULL, NULL),
    GSF_XML_IN_NODE (DOC_PROP, PROP_CREATED,	 XL_NS_O, "Created",    TRUE, NULL, NULL),
    GSF_XML_IN_NODE (DOC_PROP, PROP_LAST_SAVED,	 XL_NS_O, "LastSaved",  TRUE, NULL, NULL),
    GSF_XML_IN_NODE (DOC_PROP, PROP_COMPANY,	 XL_NS_O, "Company",    TRUE, NULL, NULL),
    GSF_XML_IN_NODE (DOC_PROP, PROP_VERSION,	 XL_NS_O, "Version",    TRUE, NULL, NULL),
  GSF_XML_IN_NODE (WORKBOOK, DOC_SETTINGS, XL_NS_O, "OfficeDocumentSettings", FALSE, NULL, NULL),
    GSF_XML_IN_NODE (DOC_SETTINGS, DOC_COLORS, XL_NS_O, "Colors", FALSE, NULL, NULL),
    GSF_XML_IN_NODE (DOC_SETTINGS, DOC_COMPONENTS, XL_NS_O, "DownloadComponents", FALSE, NULL, NULL),
    GSF_XML_IN_NODE (DOC_SETTINGS, DOC_COMPONENTS_LOCATION, XL_NS_O, "LocationOfComponents", FALSE, NULL, NULL),
  GSF_XML_IN_NODE (WORKBOOK, WB_VIEW, XL_NS_XL, "ExcelWorkbook", FALSE, NULL, NULL),
    GSF_XML_IN_NODE (WB_VIEW, TAB_RATIO, XL_NS_XL, "TabRatio", FALSE, NULL, NULL),
    GSF_XML_IN_NODE (WB_VIEW, SUPBOOK, XL_NS_XL, "SupBook", FALSE, NULL, NULL),
      GSF_XML_IN_NODE (SUPBOOK, SUP_DLL, XL_NS_XL, "Dll", FALSE, NULL, NULL),
      GSF_XML_IN_NODE (SUPBOOK, SUP_EXTERNNAME, XL_NS_XL, "ExternName", FALSE, NULL, NULL),
        GSF_XML_IN_NODE (SUP_EXTERNNAME, EXTERNNAME_NAME, XL_NS_XL, "Name", FALSE, NULL, NULL),
    GSF_XML_IN_NODE (WB_VIEW, VIEW_HEIGHT, XL_NS_XL, "WindowHeight", TRUE, NULL, NULL),
    GSF_XML_IN_NODE (WB_VIEW, VIEW_WIDTH,  XL_NS_XL, "WindowWidth",  TRUE, NULL, NULL),
    GSF_XML_IN_NODE (WB_VIEW, VIEW_TOP_X,  XL_NS_XL, "WindowTopX",   TRUE, NULL, NULL),
    GSF_XML_IN_NODE (WB_VIEW, VIEW_TOP_Y,  XL_NS_XL, "WindowTopY",   TRUE, NULL, NULL),
    GSF_XML_IN_NODE (WB_VIEW, PROTECT_STRUCTURE, XL_NS_XL, "ProtectStructure",   TRUE, NULL, NULL),
    GSF_XML_IN_NODE (WB_VIEW, PROTECT_WINDOWS,   XL_NS_XL, "ProtectWindows",     TRUE, NULL, NULL),
    GSF_XML_IN_NODE (WB_VIEW, NATURAL_LANGUAGE,  XL_NS_XL, "AcceptLabelsInFormulas", TRUE, NULL, NULL),
  GSF_XML_IN_NODE (WORKBOOK, STYLES, XL_NS_SS, "Styles", FALSE, NULL, NULL),
    GSF_XML_IN_NODE (STYLES, STYLE, XL_NS_SS,  "Style", FALSE, &xl_xml_style_start, &xl_xml_style_end),
      GSF_XML_IN_NODE (STYLE, ALIGNMENT,  XL_NS_SS, "Alignment", FALSE, &xl_xml_alignment, NULL),
      GSF_XML_IN_NODE (STYLE, BORDERS,    XL_NS_SS, "Borders",   FALSE, NULL, NULL),
        GSF_XML_IN_NODE (BORDERS, BORDER, XL_NS_SS, "Border",    FALSE, &xl_xml_border, NULL),
      GSF_XML_IN_NODE (STYLE, FONT,       XL_NS_SS, "Font",      FALSE, &xl_xml_font, NULL),
      GSF_XML_IN_NODE (STYLE, INTERIOR,   XL_NS_SS, "Interior",  FALSE, &xl_xml_num_interior, NULL),
      GSF_XML_IN_NODE (STYLE, NUM_FMT,    XL_NS_SS, "NumberFormat", FALSE, &xl_xml_num_fmt, NULL),
      GSF_XML_IN_NODE (STYLE, PROTECTION, XL_NS_SS, "Protection", FALSE, NULL, NULL),
  GSF_XML_IN_NODE (WORKBOOK, NAMES, XL_NS_SS, "Names", FALSE, NULL, NULL),
    GSF_XML_IN_NODE (NAMES, NAMED_RANGE, XL_NS_SS, "NamedRange", FALSE, &xl_xml_named_range, NULL),

  GSF_XML_IN_NODE_FULL (WORKBOOK, WORKSHEET, XL_NS_SS, "Worksheet", FALSE, FALSE, TRUE, &xl_xml_sheet_start, &xl_xml_sheet_end, 0),
    GSF_XML_IN_NODE (WORKSHEET, TABLE, XL_NS_SS, "Table", FALSE, &xl_xml_table_start, NULL),
      GSF_XML_IN_NODE (TABLE, COLUMN, XL_NS_SS, "Column", FALSE, &xl_xml_col_start, NULL),
      GSF_XML_IN_NODE (TABLE, ROW, XL_NS_SS, "Row", FALSE, &xl_xml_row_start, &xl_xml_row_end),
	GSF_XML_IN_NODE_FULL (ROW, CELL, XL_NS_SS, "Cell", FALSE, FALSE, TRUE, &xl_xml_cell_start, &xl_xml_cell_end, 0),
          GSF_XML_IN_NODE (CELL, NAMED_CELL, XL_NS_SS, "NamedCell", FALSE, NULL, NULL),
          GSF_XML_IN_NODE_FULL (CELL, CELL_DATA, XL_NS_SS, "Data", GSF_XML_CONTENT, FALSE, TRUE, &xl_xml_data_start, &xl_xml_data_end, 0),
	    GSF_XML_IN_NODE_FULL (CELL_DATA, HTML_B,    XL_NS_HTML, "B",    GSF_XML_SHARED_CONTENT, TRUE, FALSE, NULL, NULL, 0),
	    GSF_XML_IN_NODE_FULL (CELL_DATA, HTML_I,    XL_NS_HTML, "I",    GSF_XML_SHARED_CONTENT, TRUE, FALSE, NULL, NULL, 1),
	    GSF_XML_IN_NODE_FULL (CELL_DATA, HTML_U,    XL_NS_HTML, "U",    GSF_XML_SHARED_CONTENT, TRUE, FALSE, NULL, NULL, 2),
	    GSF_XML_IN_NODE_FULL (CELL_DATA, HTML_FONT, XL_NS_HTML, "Font", GSF_XML_SHARED_CONTENT, TRUE, FALSE, NULL, NULL, 3),
	    GSF_XML_IN_NODE_FULL (CELL_DATA, HTML_S,    XL_NS_HTML, "S",    GSF_XML_SHARED_CONTENT, TRUE, FALSE, NULL, NULL, 4),
	    GSF_XML_IN_NODE_FULL (CELL_DATA, HTML_SUP,  XL_NS_HTML, "Sup",  GSF_XML_SHARED_CONTENT, TRUE, FALSE, NULL, NULL, 5),
	    GSF_XML_IN_NODE_FULL (CELL_DATA, HTML_SUB,  XL_NS_HTML, "Sub",  GSF_XML_SHARED_CONTENT, TRUE, FALSE, NULL, NULL, 6),
    GSF_XML_IN_NODE (WORKSHEET, OPTIONS, XL_NS_XL, "WorksheetOptions", FALSE, NULL, NULL),
      GSF_XML_IN_NODE (OPTIONS, TOP_ROW, XL_NS_XL, "TopRowVisible", FALSE, NULL, NULL),
      GSF_XML_IN_NODE (OPTIONS, UNSYNCED, XL_NS_XL, "Unsynced", FALSE, NULL, NULL),	/* ?? */
      GSF_XML_IN_NODE (OPTIONS, SELECTED, XL_NS_XL, "Selected", FALSE, NULL, NULL),	/* ?? */
      GSF_XML_IN_NODE (OPTIONS, PANES, XL_NS_XL, "Panes", FALSE, NULL, NULL),
        GSF_XML_IN_NODE (PANES, PANE, XL_NS_XL,  "Pane", FALSE, &xl_xml_pane, NULL),
          GSF_XML_IN_NODE (PANE, PANE_NUM, XL_NS_XL,  "Number", TRUE, NULL, NULL),
          GSF_XML_IN_NODE (PANE, PANE_ACTIVEROW, XL_NS_XL,  "ActiveRow", TRUE, NULL, &xl_xml_editpos_row),
          GSF_XML_IN_NODE (PANE, PANE_ACTIVECOL, XL_NS_XL,  "ActiveCol", TRUE, NULL, &xl_xml_editpos_col),
          GSF_XML_IN_NODE (PANE, PANE_SELECTION, XL_NS_XL,  "RangeSelection", TRUE, NULL, &xl_xml_selection),
      GSF_XML_IN_NODE (OPTIONS, PAGE_SETUP, XL_NS_XL, "PageSetup", FALSE, NULL, NULL),
	GSF_XML_IN_NODE (PAGE_SETUP, PAGE_HEADER, XL_NS_XL, "Header", FALSE, NULL, NULL),
	GSF_XML_IN_NODE (PAGE_SETUP, PAGE_FOOTER, XL_NS_XL, "Footer", FALSE, NULL, NULL),
      GSF_XML_IN_NODE (OPTIONS, PRINT, XL_NS_XL, "Print", FALSE, NULL, NULL),
	GSF_XML_IN_NODE (PRINT, PRINT_VALID_INFO,  XL_NS_XL, "ValidPrinterInfo", FALSE, NULL, NULL),
	GSF_XML_IN_NODE (PRINT, PRINT_PAPER_SIZE,  XL_NS_XL, "PaperSizeIndex", TRUE, NULL, NULL),
	GSF_XML_IN_NODE (PRINT, PRINT_HRES,	   XL_NS_XL, "HorizontalResolution", TRUE, NULL, NULL),
	GSF_XML_IN_NODE (PRINT, PRINT_VRES,	   XL_NS_XL, "VerticalResolution", TRUE, NULL, NULL),

      GSF_XML_IN_NODE (OPTIONS, PROT_OBJS,	 XL_NS_XL, "ProtectObjects", TRUE, NULL, NULL),
      GSF_XML_IN_NODE (OPTIONS, PROT_SCENARIOS,  XL_NS_XL, "ProtectScenarios", TRUE, NULL, NULL),
      GSF_XML_IN_NODE (OPTIONS, PAGEBREAK_ZOOM,	 XL_NS_XL, "PageBreakZoom", FALSE, NULL, NULL),
    GSF_XML_IN_NODE (WORKSHEET, COND_FMT, XL_NS_XL, "ConditionalFormatting", FALSE, NULL, NULL),
      GSF_XML_IN_NODE (COND_FMT, COND_RANGE,	XL_NS_XL, "Range", FALSE, NULL, NULL),
      GSF_XML_IN_NODE (COND_FMT, COND,		XL_NS_XL, "Condition", FALSE, NULL, NULL),
        GSF_XML_IN_NODE (COND, COND_VALUE1,	XL_NS_XL, "Value1", FALSE, NULL, NULL),
        GSF_XML_IN_NODE (COND, COND_STYLE,	XL_NS_XL, "Format", FALSE, NULL, NULL),
  { NULL }
};

G_MODULE_EXPORT void
excel_xml_file_open (GOFileOpener const *fo, IOContext *context,
		     WorkbookView *wbv, GsfInput *input);

void
excel_xml_file_open (GOFileOpener const *fo, IOContext *io_context,
		     WorkbookView *wb_view, GsfInput *input)
{
	GsfXMLInDoc *doc;
	ExcelXMLReadState state;

	state.context	= io_context;
	state.wb_view	= wb_view;
	state.wb	= wb_view_workbook (wb_view);
	state.sheet	= NULL;
	state.style	= NULL;
	state.def_style	= NULL;
	state.expr	= NULL;
	state.style_hash = g_hash_table_new_full (g_str_hash, g_str_equal,
		(GDestroyNotify)g_free, (GDestroyNotify) gnm_style_unref);

	doc = gsf_xml_in_doc_new (excel_xml_dtd, content_ns);
	if (!gsf_xml_in_doc_parse (doc, input, &state))
		gnumeric_io_error_string (io_context, _("XML document not well formed!"));
	gsf_xml_in_doc_free (doc);

	g_hash_table_destroy (state.style_hash);
}
