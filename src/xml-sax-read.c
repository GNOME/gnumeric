/* vim: set sw=8: */

/*
 * xml-sax-read.c : a sax based parser.  INCOMPLETE
 *
 * Copyright (C) 2000-2005 Jody Goldberg (jody@gnome.org)
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
#include "xml-sax.h"
#include "xml-io-version.h"
#include "gnm-plugin.h"
#include "sheet-view.h"
#include "sheet-style.h"
#include "sheet-merge.h"
#include "sheet.h"
#include "ranges.h"
#include "style.h"
#include "style-border.h"
#include "style-color.h"
#include "style-conditions.h"
#include "validation.h"
#include "hlink.h"
#include "input-msg.h"
#include "gnm-format.h"
#include "cell.h"
#include "position.h"
#include "expr.h"
#include "expr-name.h"
#include "print-info.h"
#include "value.h"
#include "selection.h"
#include "command-context.h"
#include "workbook-view.h"
#include "workbook.h"
#include "sheet-object-impl.h"
#include "sheet-object-cell-comment.h"
#include "gnm-so-line.h"
#include "gnm-so-filled.h"
#include "sheet-object-graph.h"
#include "application.h"
#include "xml-io.h"

#include <goffice/app/io-context.h>
#include <goffice/app/go-plugin.h>
#include <goffice/app/error-info.h>
#include <goffice/utils/go-glib-extras.h>

#include <gsf/gsf-libxml.h>
#include <gsf/gsf-input.h>
#include <gsf/gsf-input-memory.h>
#include <gsf/gsf-input-gzip.h>
#include <glib/gi18n.h>
#include <libxml/tree.h>
#include <libxml/parser.h>
#include <libxml/parserInternals.h>
#include <stdlib.h>
#include <string.h>
#include <locale.h>

GNM_PLUGIN_MODULE_HEADER;

/*****************************************************************************/

gboolean
gnm_xml_attr_double (xmlChar const * const *attrs, char const *name, double * res)
{
	char *end;
	double tmp;

	g_return_val_if_fail (attrs != NULL, FALSE);
	g_return_val_if_fail (attrs[0] != NULL, FALSE);
	g_return_val_if_fail (attrs[1] != NULL, FALSE);

	if (strcmp (attrs[0], name))
		return FALSE;

	tmp = g_strtod ((gchar *)attrs[1], &end);
	if (*end) {
		g_warning ("Invalid attribute '%s', expected double, received '%s'",
			   name, attrs[1]);
		return FALSE;
	}
	*res = tmp;
	return TRUE;
}
static gboolean
xml_sax_double (xmlChar const *chars, double *res)
{
	char *end;
	*res = g_strtod ((gchar *)chars, &end);
	return *end == '\0';
}

static gboolean
xml_sax_attr_bool (xmlChar const * const *attrs, char const *name, gboolean *res)
{
	g_return_val_if_fail (attrs != NULL, FALSE);
	g_return_val_if_fail (attrs[0] != NULL, FALSE);
	g_return_val_if_fail (attrs[1] != NULL, FALSE);

	if (strcmp (attrs[0], name))
		return FALSE;

	*res = g_ascii_strcasecmp ((gchar *)attrs[1], "false") && strcmp (attrs[1], "0");

	return TRUE;
}

static gboolean
xml_sax_attr_int (xmlChar const * const *attrs, char const *name, int *res)
{
	char *end;
	int tmp;

	g_return_val_if_fail (attrs != NULL, FALSE);
	g_return_val_if_fail (attrs[0] != NULL, FALSE);
	g_return_val_if_fail (attrs[1] != NULL, FALSE);

	if (strcmp (attrs[0], name))
		return FALSE;

	tmp = strtol ((gchar *)attrs[1], &end, 10);
	if (*end) {
		g_warning ("Invalid attribute '%s', expected integer, received '%s'",
			   name, attrs[1]);
		return FALSE;
	}
	*res = tmp;
	return TRUE;
}

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

	if (strcmp (attrs[0], name))
		return FALSE;

	eclass = G_ENUM_CLASS (g_type_class_peek (etype));

	ev = g_enum_get_value_by_name (eclass, attrs[1]);
	if (!ev) ev = g_enum_get_value_by_nick (eclass, attrs[1]);
	if (!ev && xml_sax_attr_int (attrs, name, &i))
		/* Check that the value is valid.  */
		ev = g_enum_get_value (eclass, i);
	if (!ev) return FALSE;

	*val = ev->value;
	return TRUE;
}


static gboolean
xml_sax_attr_cellpos (xmlChar const * const *attrs, char const *name, GnmCellPos *val)
{
	g_return_val_if_fail (attrs != NULL, FALSE);
	g_return_val_if_fail (attrs[0] != NULL, FALSE);
	g_return_val_if_fail (attrs[1] != NULL, FALSE);

	if (strcmp (attrs[0], name))
		return FALSE;

	if (cellpos_parse ((gchar *)attrs[1], val, TRUE) == NULL) {
		g_warning ("Invalid attribute '%s', expected cellpos, received '%s'",
			   name, attrs[1]);
		return FALSE;
	}
	return TRUE;
}

static gboolean
xml_sax_attr_color (xmlChar const * const *attrs, char const *name, GnmColor **res)
{
	int red, green, blue;

	g_return_val_if_fail (attrs != NULL, FALSE);
	g_return_val_if_fail (attrs[0] != NULL, FALSE);
	g_return_val_if_fail (attrs[1] != NULL, FALSE);

	if (strcmp (attrs[0], name))
		return FALSE;

	if (sscanf ((gchar *)attrs[1], "%X:%X:%X", &red, &green, &blue) != 3){
		g_warning ("Invalid attribute '%s', expected colour, received '%s'",
			   name, attrs[1]);
		return FALSE;
	}
	*res = style_color_new (red, green, blue);
	return TRUE;
}

static gboolean
xml_sax_attr_range (xmlChar const * const *attrs, GnmRange *res)
{
	int flags = 0;
	for (; attrs[0] && attrs[1] ; attrs += 2)
		if (xml_sax_attr_int (attrs, "startCol", &res->start.col))
			flags |= 0x1;
		else if (xml_sax_attr_int (attrs, "startRow", &res->start.row))
			flags |= 0x2;
		else if (xml_sax_attr_int (attrs, "endCol", &res->end.col))
			flags |= 0x4;
		else if (xml_sax_attr_int (attrs, "endRow", &res->end.row))
			flags |= 0x8;
		else
			return FALSE;

	return flags == 0xf;
}

/*****************************************************************************/

typedef struct {
	GsfXMLIn base;

	IOContext 	*context;	/* The IOcontext managing things */
	WorkbookView	*wb_view;	/* View for the new workbook */
	Workbook	*wb;		/* The new workbook */
	GnumericXMLVersion version;

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
		GnmExpr	const	*expr [2];
		ValidationStyle  style;
		ValidationType	 type;
		ValidationOp	 op;
		gboolean	 allow_blank;
		gboolean	 use_dropdown;
	} validation;
	GnmStyleCond	cond;

	gboolean  style_range_init;
	GnmRange	  style_range;
	GnmStyle   *style;

	GnmCellPos cell;
	int expr_id, array_rows, array_cols;
	int value_type;
	GOFormat *value_fmt;

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
	GnmSheetVisibility visibility;
	GnmColor *tab_color;

	/* expressions with ref > 1 a map from index -> expr pointer */
	GHashTable *expr_map;
	GList *delayed_names;
	SheetObject *so;
} XMLSaxParseState;

SheetObject *
gnm_xml_in_cur_obj (GsfXMLIn const *xin)
{
	XMLSaxParseState *state = (XMLSaxParseState *)xin->user_state;
	return state->so;
}

/****************************************************************************/

static void
unknown_attr (GsfXMLIn *gsf_state, xmlChar const * const *attrs)
{
	XMLSaxParseState *state = (XMLSaxParseState *)gsf_state->user_state;

	if (state->version == GNM_XML_LATEST)
		gnm_io_warning (state->context,
			_("Unexpected attribute %s::%s == '%s'."),
			(NULL != gsf_state->node &&
			 NULL != gsf_state->node->name) ?
			gsf_state->node->name : "<unknow name>",
			attrs[0], attrs[1]);
}

static void
xml_sax_wb (GsfXMLIn *gsf_state, xmlChar const **attrs)
{
	XMLSaxParseState *state = (XMLSaxParseState *)gsf_state->user_state;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (strcmp (attrs[0], "xmlns:gmr") == 0 ||
		    strcmp (attrs[0], "xmlns:gnm") == 0) {
			static struct {
				char const * const id;
				GnumericXMLVersion const version;
			} const GnumericVersions [] = {
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
				{ NULL }
			};
			int i;
			for (i = 0 ; GnumericVersions [i].id != NULL ; ++i )
				if (strcmp (attrs[1], GnumericVersions [i].id) == 0) {
					if (state->version != GNM_XML_UNKNOWN)
						gnm_io_warning (state->context,
							_("Multiple version specifications.  Assuming %d"),
							state->version);
					else {
						state->version = GnumericVersions [i].version;
						break;
					}
				}
		} else if (!strcmp (attrs[0], "xmlns:xsi")) {
		} else if (!strcmp (attrs[0], "xsi:schemaLocation")) {
		} else
			unknown_attr (gsf_state, attrs);
}

static void
xml_sax_wb_sheetname (GsfXMLIn *gsf_state, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	XMLSaxParseState *state = (XMLSaxParseState *)gsf_state->user_state;
	char const *name = gsf_state->content->str;
	
	g_return_if_fail (name != NULL);

	if (NULL == workbook_sheet_by_name (state->wb, name))
		workbook_sheet_attach (state->wb, sheet_new (state->wb, name));
}

static void
xml_sax_wb_view (GsfXMLIn *gsf_state, xmlChar const **attrs)
{
	XMLSaxParseState *state = (XMLSaxParseState *)gsf_state->user_state;

	int sheet_index;
	int width = -1, height = -1;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (xml_sax_attr_int (attrs, "SelectedTab", &sheet_index))
			wb_view_sheet_focus (state->wb_view,
				workbook_sheet_by_index (state->wb, sheet_index));
		else if (xml_sax_attr_int (attrs, "Width", &width)) ;
		else if (xml_sax_attr_int (attrs, "Height", &height)) ;
		else
			unknown_attr (gsf_state, attrs);

	if (width > 0 && height > 0)
		wb_view_preferred_size (state->wb_view, width, height);
}
static void
xml_sax_calculation (GsfXMLIn *gsf_state, xmlChar const **attrs)
{
	XMLSaxParseState *state = (XMLSaxParseState *)gsf_state->user_state;
	gboolean b;
	int 	 i;
	double	 d;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (xml_sax_attr_bool (attrs, "ManualRecalc", &b))
			workbook_autorecalc_enable (state->wb, !b);
		else if (xml_sax_attr_bool (attrs, "EnableIteration", &b))
			workbook_iteration_enabled (state->wb, b);
		else if (xml_sax_attr_int  (attrs, "MaxIterations", &i))
			workbook_iteration_max_number (state->wb, i);
		else if (gnm_xml_attr_double (attrs, "IterationTolerance", &d))
			workbook_iteration_tolerance (state->wb, d);
		else
			unknown_attr (gsf_state, attrs);
}

static void
xml_sax_finish_parse_wb_attr (GsfXMLIn *gsf_state, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	XMLSaxParseState *state = (XMLSaxParseState *)gsf_state->user_state;

	g_return_if_fail (state->attribute.name != NULL);
	g_return_if_fail (state->attribute.value != NULL);

	wb_view_set_attribute (state->wb_view,
		state->attribute.name, state->attribute.value);

	g_free (state->attribute.value);	state->attribute.value = NULL;
	g_free (state->attribute.name);		state->attribute.name = NULL;
}

static void
xml_sax_attr_elem (GsfXMLIn *gsf_state, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	XMLSaxParseState *state = (XMLSaxParseState *)gsf_state->user_state;

	char const *content = gsf_state->content->str;
	int const len = gsf_state->content->len;

	switch (gsf_state->node->user_data.v_int) {
	case 0 :
		g_return_if_fail (state->attribute.name == NULL);
		state->attribute.name = g_strndup (content, len);
		break;

	case 1 :
		g_return_if_fail (state->attribute.value == NULL);
		state->attribute.value = g_strndup (content, len);
		break;

	default :
		g_assert_not_reached ();
	};
}

static void
xml_sax_sheet_start (GsfXMLIn *gsf_state, xmlChar const **attrs)
{
	XMLSaxParseState *state = (XMLSaxParseState *)gsf_state->user_state;

	gboolean tmp;
	gint tmpi;
	GnmColor *color = NULL;

	state->hide_col_header = state->hide_row_header =
	state->display_formulas = state->hide_zero =
	state->hide_grid = state->display_outlines =
	state->outline_symbols_below = state->outline_symbols_right =
	state->text_is_rtl = state->is_protected = -1;
	state->visibility = GNM_SHEET_VISIBILITY_VISIBLE;
	state->tab_color = NULL;
	state->sheet_zoom = 1.; /* default */

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (xml_sax_attr_bool (attrs, "DisplayFormulas", &tmp))
			state->display_formulas = tmp;
		else if (xml_sax_attr_bool (attrs, "HideZero", &tmp))
			state->hide_zero = tmp;
		else if (xml_sax_attr_bool (attrs, "HideGrid", &tmp))
			state->hide_grid = tmp;
		else if (xml_sax_attr_bool (attrs, "HideColHeader", &tmp))
			state->hide_col_header = tmp;
		else if (xml_sax_attr_bool (attrs, "HideRowHeader", &tmp))
			state->hide_row_header = tmp;
		else if (xml_sax_attr_bool (attrs, "DisplayOutlines", &tmp))
			state->display_outlines = tmp;
		else if (xml_sax_attr_bool (attrs, "OutlineSymbolsBelow", &tmp))
			state->outline_symbols_below = tmp;
		else if (xml_sax_attr_bool (attrs, "OutlineSymbolsRight", &tmp))
			state->outline_symbols_right = tmp;
		else if (xml_sax_attr_enum (attrs, "Visibility", GNM_SHEET_VISIBILITY_TYPE, &tmpi))
			state->visibility = tmpi;
		else if (xml_sax_attr_bool (attrs, "RTL_Layout", &tmp))
			state->text_is_rtl = tmp;
		else if (xml_sax_attr_bool (attrs, "Protected", &tmp))
			state->is_protected = tmp;
		else if (xml_sax_attr_color (attrs, "TabColor", &color))
			state->tab_color = color;
		else
			unknown_attr (gsf_state, attrs);
}

static void
xml_sax_sheet_end (GsfXMLIn *gsf_state, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	XMLSaxParseState *state = (XMLSaxParseState *)gsf_state->user_state;

	g_return_if_fail (state->sheet != NULL);

	/* Init ColRowInfo's size_pixels and force a full respan */
	g_object_set (state->sheet, "zoom-factor", state->sheet_zoom, NULL);
	sheet_flag_recompute_spans (state->sheet);
	state->sheet = NULL;
}

static void
xml_sax_sheet_name (GsfXMLIn *gsf_state, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	XMLSaxParseState *state = (XMLSaxParseState *)gsf_state->user_state;

	char const * content = gsf_state->content->str;
	g_return_if_fail (state->sheet == NULL);

	/* * FIXME: Pull this out at some point, so we don't
	 * have to support < GNM_XML_V7 anymore
	 */
	if (state->version >= GNM_XML_V7) {
		state->sheet = workbook_sheet_by_name (state->wb, content);

		if (!state->sheet)
			gnumeric_io_error_string (state->context,
				_("File has inconsistent SheetNameIndex element."));
	} else {
		state->sheet = sheet_new (state->wb, content);
		workbook_sheet_attach (state->wb, state->sheet);
	}

	if (state->display_formulas >= 0)
		g_object_set (state->sheet, "display-formulas", state->display_formulas, NULL);
	if (state->hide_zero >= 0)
		g_object_set (state->sheet, "display-zeros", !state->hide_zero, NULL);
	if (state->hide_grid >= 0)
		g_object_set (state->sheet, "display-grid", !state->hide_grid, NULL);
	if (state->hide_col_header >= 0)
		g_object_set (state->sheet, "display-column-header", !state->hide_col_header, NULL);
	if (state->hide_row_header >= 0)
		g_object_set (state->sheet, "display-row-header", !state->hide_row_header, NULL);
	if (state->display_outlines >= 0)
		g_object_set (state->sheet, "display-outlines", state->display_outlines, NULL);
	if (state->outline_symbols_below >= 0)
		g_object_set (state->sheet, "display-outlines-below", state->outline_symbols_below, NULL);
	if (state->outline_symbols_right >= 0)
		g_object_set (state->sheet, "display-outlines-right", state->outline_symbols_right, NULL);
	if (state->text_is_rtl >= 0)
		g_object_set (state->sheet, "text-is-rtl", state->text_is_rtl, NULL);
	g_object_set (state->sheet, "visibility", state->visibility, NULL);
	state->sheet->tab_color = state->tab_color;
}

static void
xml_sax_sheet_zoom (GsfXMLIn *gsf_state, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	XMLSaxParseState *state = (XMLSaxParseState *)gsf_state->user_state;

	char const * content = gsf_state->content->str;
	double zoom;

	g_return_if_fail (state->sheet != NULL);

	if (xml_sax_double ((xmlChar *)content, &zoom))
		state->sheet_zoom = zoom;
}

static double
xml_sax_print_margins_get_double (GsfXMLIn *gsf_state, xmlChar const **attrs)
{
	double points;
	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2) {
		if (gnm_xml_attr_double (attrs, "Points", &points))
			return points;
		else if (strcmp (attrs[0], "PrefUnit"))
			unknown_attr (gsf_state, attrs);
	}
	return 0.0;
}

static void
xml_sax_print_margins_unit (GsfXMLIn *gsf_state, xmlChar const **attrs, PrintUnit *pu)
{
	double points;
	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2) {
		if (gnm_xml_attr_double (attrs, "Points", &points))
			pu->points = points;
		else if (!strcmp (attrs[0], "PrefUnit")) {
			pu->desired_display = unit_name_to_unit (attrs[1]);
		} else
			unknown_attr (gsf_state, attrs);
	}
}

static void
xml_sax_print_margins (GsfXMLIn *gsf_state, xmlChar const **attrs)
{
	XMLSaxParseState *state = (XMLSaxParseState *)gsf_state->user_state;

	PrintInformation *pi;

	g_return_if_fail (state->sheet != NULL);
	g_return_if_fail (state->sheet->print_info != NULL);

	pi = state->sheet->print_info;
	switch (gsf_state->node->user_data.v_int) {
	case 0: xml_sax_print_margins_unit (gsf_state, attrs,
			&pi->margin.top);
		break;
	case 1: xml_sax_print_margins_unit (gsf_state, attrs,
			&pi->margin.bottom);
		break;
	case 2: print_info_set_margin_left (pi,
			xml_sax_print_margins_get_double (gsf_state, attrs));
		break;
	case 3: print_info_set_margin_right (pi,
			xml_sax_print_margins_get_double (gsf_state, attrs));
		break;
	case 4: print_info_set_margin_header (pi,
			xml_sax_print_margins_get_double (gsf_state, attrs));
		break;
	case 5: print_info_set_margin_footer (pi,
			xml_sax_print_margins_get_double (gsf_state, attrs));
		break;
	default:
		return;
	}
}




static void
xml_sax_print_scale (GsfXMLIn *gsf_state, xmlChar const **attrs)
{
	XMLSaxParseState *state = (XMLSaxParseState *)gsf_state->user_state;

	PrintInformation *pi;
	double percentage;
	int cols, rows;

	g_return_if_fail (state->sheet != NULL);
	g_return_if_fail (state->sheet->print_info != NULL);

	pi = state->sheet->print_info;
	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2) {
		if (!strcmp (attrs[0], "type"))
			pi->scaling.type = strcmp (attrs[1], "percentage")
				? PRINT_SCALE_FIT_PAGES : PRINT_SCALE_PERCENTAGE;
		else if (gnm_xml_attr_double (attrs, "percentage", &percentage))
			pi->scaling.percentage.x = pi->scaling.percentage.y = percentage;
		else if (xml_sax_attr_int (attrs, "cols", &cols))
			pi->scaling.dim.cols = cols;
		else if (xml_sax_attr_int (attrs, "rows", &rows))
			pi->scaling.dim.rows = rows;
	}
}

static void
xml_sax_selection_range (GsfXMLIn *gsf_state, xmlChar const **attrs)
{
	XMLSaxParseState *state = (XMLSaxParseState *)gsf_state->user_state;

	GnmRange r;
	if (xml_sax_attr_range (attrs, &r))
		sv_selection_add_range (
			sheet_get_view (state->sheet, state->wb_view),
			r.start.col, r.start.row,
			r.start.col, r.start.row,
			r.end.col, r.end.row);
}

static void
xml_sax_selection (GsfXMLIn *gsf_state, xmlChar const **attrs)
{
	XMLSaxParseState *state = (XMLSaxParseState *)gsf_state->user_state;

	int col = -1, row = -1;

	sv_selection_reset (sheet_get_view (state->sheet, state->wb_view));

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (xml_sax_attr_int (attrs, "CursorCol", &col)) ;
		else if (xml_sax_attr_int (attrs, "CursorRow", &row)) ;
		else
			unknown_attr (gsf_state, attrs);

	g_return_if_fail (col >= 0);
	g_return_if_fail (row >= 0);
	g_return_if_fail (state->cell.col < 0);
	g_return_if_fail (state->cell.row < 0);
	state->cell.col = col;
	state->cell.row = row;
}

static void
xml_sax_selection_end (GsfXMLIn *gsf_state, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	XMLSaxParseState *state = (XMLSaxParseState *)gsf_state->user_state;

	GnmCellPos const pos = state->cell;
	state->cell.col = state->cell.row = -1;
	sv_set_edit_pos (sheet_get_view (state->sheet, state->wb_view), &pos);
}

static void
xml_sax_sheet_layout (GsfXMLIn *gsf_state, xmlChar const **attrs)
{
	XMLSaxParseState *state = (XMLSaxParseState *)gsf_state->user_state;

	GnmCellPos tmp;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (xml_sax_attr_cellpos (attrs, "TopLeft", &tmp))
			sv_set_initial_top_left (
				sheet_get_view (state->sheet, state->wb_view),
				tmp.col, tmp.row);
		else
			unknown_attr (gsf_state, attrs);
}

static void
xml_sax_sheet_freezepanes (GsfXMLIn *gsf_state, xmlChar const **attrs)
{
	XMLSaxParseState *state = (XMLSaxParseState *)gsf_state->user_state;

	GnmCellPos frozen_tl, unfrozen_tl;
	int flags = 0;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (xml_sax_attr_cellpos (attrs, "FrozenTopLeft", &frozen_tl))
			flags |= 1;
		else if (xml_sax_attr_cellpos (attrs, "UnfrozenTopLeft", &unfrozen_tl))
			flags |= 2;
		else
			unknown_attr (gsf_state, attrs);

	if (flags == 3)
		sv_freeze_panes (sheet_get_view (state->sheet, state->wb_view),
			&frozen_tl, &unfrozen_tl);
}

static void
xml_sax_cols_rows (GsfXMLIn *gsf_state, xmlChar const **attrs)
{
	XMLSaxParseState *state = (XMLSaxParseState *)gsf_state->user_state;

	double def_size;
	gboolean const is_col = gsf_state->node->user_data.v_bool;

	g_return_if_fail (state->sheet != NULL);

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (gnm_xml_attr_double (attrs, "DefaultSizePts", &def_size)) {
			if (is_col)
				sheet_col_set_default_size_pts (state->sheet, def_size);
			else
				sheet_row_set_default_size_pts (state->sheet, def_size);
		}
}

static void
xml_sax_colrow (GsfXMLIn *gsf_state, xmlChar const **attrs)
{
	XMLSaxParseState *state = (XMLSaxParseState *)gsf_state->user_state;

	ColRowInfo *cri = NULL;
	double size = -1.;
	int dummy;
	int count = 1;
	gboolean const is_col = gsf_state->node->user_data.v_bool;

	g_return_if_fail (state->sheet != NULL);

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2) {
		if (xml_sax_attr_int (attrs, "No", &dummy)) {
			g_return_if_fail (cri == NULL);

			cri = is_col
				? sheet_col_fetch (state->sheet, dummy)
				: sheet_row_fetch (state->sheet, dummy);
		} else {
			g_return_if_fail (cri != NULL);

			if (gnm_xml_attr_double (attrs, "Unit", &size)) ;
			else if (xml_sax_attr_int (attrs, "Count", &count)) ;
			else if (xml_sax_attr_int (attrs, "MarginA", &dummy))
				cri->margin_a = dummy;
			else if (xml_sax_attr_int (attrs, "MarginB", &dummy))
				cri->margin_b = dummy;
			else if (xml_sax_attr_int (attrs, "HardSize", &dummy))
				cri->hard_size = dummy;
			else if (xml_sax_attr_int (attrs, "Hidden", &dummy))
				cri->visible = !dummy;
			else if (xml_sax_attr_int (attrs, "Collapsed", &dummy))
				cri->is_collapsed = dummy;
			else if (xml_sax_attr_int (attrs, "OutlineLevel", &dummy))
				cri->outline_level = dummy;
			else
				unknown_attr (gsf_state, attrs);
		}
	}

	g_return_if_fail (cri != NULL && size > -1.);

	if (is_col) {
		int pos = cri->pos;
		sheet_col_set_size_pts (state->sheet, pos, size, cri->hard_size);
		/* resize flags are already set only need to copy the sizes */
		while (--count > 0)
			colrow_copy (sheet_col_fetch (state->sheet, ++pos), cri);
	} else {
		int pos = cri->pos;
		sheet_row_set_size_pts (state->sheet, cri->pos, size, cri->hard_size);
		/* resize flags are already set only need to copy the sizes */
		while (--count > 0)
			colrow_copy (sheet_row_fetch (state->sheet, ++pos), cri);
	}
}

static void
xml_sax_style_region_start (GsfXMLIn *gsf_state, xmlChar const **attrs)
{
	XMLSaxParseState *state = (XMLSaxParseState *)gsf_state->user_state;

	g_return_if_fail (state->style_range_init == FALSE);
	g_return_if_fail (state->style == NULL);

	state->style = (state->version >= GNM_XML_V6 ||
			state->version <= GNM_XML_V2)
		? gnm_style_new_default ()
		: gnm_style_new ();
	state->style_range_init =
		xml_sax_attr_range (attrs, &state->style_range);
}

static void
xml_sax_style_region_end (GsfXMLIn *gsf_state, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	XMLSaxParseState *state = (XMLSaxParseState *)gsf_state->user_state;

	g_return_if_fail (state->style_range_init);
	g_return_if_fail (state->style != NULL);
	g_return_if_fail (state->sheet != NULL);

	sheet_style_set_range (state->sheet, &state->style_range, state->style);

	state->style_range_init = FALSE;
	state->style = NULL;
}

static void
xml_sax_styleregion_start (GsfXMLIn *gsf_state, xmlChar const **attrs)
{
	XMLSaxParseState *state = (XMLSaxParseState *)gsf_state->user_state;

	int val;
	GnmColor *colour;

	g_return_if_fail (state->style != NULL);

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2) {
		if (xml_sax_attr_int (attrs, "HAlign", &val))
			gnm_style_set_align_h (state->style, val);
		else if (xml_sax_attr_int (attrs, "VAlign", &val))
			gnm_style_set_align_v (state->style, val);

		/* Pre version V6 */
		else if (xml_sax_attr_int (attrs, "Fit", &val))
			gnm_style_set_wrap_text (state->style, val);

		else if (xml_sax_attr_int (attrs, "WrapText", &val))
			gnm_style_set_wrap_text (state->style, val);
		else if (xml_sax_attr_bool (attrs, "ShrinkToFit", &val))
			gnm_style_set_shrink_to_fit (state->style, val);
		else if (xml_sax_attr_int (attrs, "Rotation", &val)) {
			/* Work around a bug pre 1.5.1 that would allow
			 * negative rotations.  -1 == vertical, map everything
			 * else back onto 0..359 */
			if (val < -1)
				val += 360;
			gnm_style_set_rotation (state->style, val);
		} else if (xml_sax_attr_int (attrs, "Shade", &val))
			gnm_style_set_pattern (state->style, val);
		else if (xml_sax_attr_int (attrs, "Indent", &val))
			gnm_style_set_indent (state->style, val);
		else if (xml_sax_attr_color (attrs, "Fore", &colour))
			gnm_style_set_font_color (state->style, colour);
		else if (xml_sax_attr_color (attrs, "Back", &colour))
			gnm_style_set_back_color (state->style, colour);
		else if (xml_sax_attr_color (attrs, "PatternColor", &colour))
			gnm_style_set_pattern_color (state->style, colour);
		else if (!strcmp (attrs[0], "Format"))
			gnm_style_set_format_text (state->style, (char *)attrs[1]);
		else if (xml_sax_attr_int (attrs, "Hidden", &val))
			gnm_style_set_content_hidden (state->style, val);
		else if (xml_sax_attr_int (attrs, "Locked", &val))
			gnm_style_set_content_locked (state->style, val);
		else if (xml_sax_attr_int (attrs, "Locked", &val))
			gnm_style_set_content_locked (state->style, val);
		else if (xml_sax_attr_int (attrs, "Orient", &val))
			; /* ignore old useless attribute */
		else
			unknown_attr (gsf_state, attrs);
	}
}

static void
xml_sax_styleregion_font (GsfXMLIn *gsf_state, xmlChar const **attrs)
{
	XMLSaxParseState *state = (XMLSaxParseState *)gsf_state->user_state;

	double size_pts = 10.;
	int val;

	g_return_if_fail (state->style != NULL);

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2) {
		if (gnm_xml_attr_double (attrs, "Unit", &size_pts))
			gnm_style_set_font_size (state->style, size_pts);
		else if (xml_sax_attr_int (attrs, "Bold", &val))
			gnm_style_set_font_bold (state->style, val);
		else if (xml_sax_attr_int (attrs, "Italic", &val))
			gnm_style_set_font_italic (state->style, val);
		else if (xml_sax_attr_int (attrs, "Underline", &val))
			gnm_style_set_font_uline (state->style, (GnmUnderline)val);
		else if (xml_sax_attr_int (attrs, "StrikeThrough", &val))
			gnm_style_set_font_strike (state->style, val ? TRUE : FALSE);
		else if (xml_sax_attr_int (attrs, "Script", &val)) {
			if (val == 0)
				gnm_style_set_font_script (state->style, GO_FONT_SCRIPT_STANDARD);
			else if (val < 0)
				gnm_style_set_font_script (state->style, GO_FONT_SCRIPT_SUB);
			else
				gnm_style_set_font_script (state->style, GO_FONT_SCRIPT_SUPER);
		} else
			unknown_attr (gsf_state, attrs);
	}
}

static const char *
font_component (const char *fontname, int idx)
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
style_font_read_from_x11 (GnmStyle *mstyle, const char *fontname)
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
xml_sax_styleregion_font_end (GsfXMLIn *gsf_state, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	XMLSaxParseState *state = (XMLSaxParseState *)gsf_state->user_state;

	if (gsf_state->content->len > 0) {
		char const * content = gsf_state->content->str;
		if (*content == '-')
			style_font_read_from_x11 (state->style, content);
		else
			gnm_style_set_font_name (state->style, content);
	}
}

static void
xml_sax_validation (GsfXMLIn *gsf_state, xmlChar const **attrs)
{
	XMLSaxParseState *state = (XMLSaxParseState *)gsf_state->user_state;

	int dummy;
	gboolean b_dummy;

	g_return_if_fail (state->validation.title == NULL);
	g_return_if_fail (state->validation.msg == NULL);
	g_return_if_fail (state->validation.expr[0] == NULL);
	g_return_if_fail (state->validation.expr[1] == NULL);

	state->validation.style = VALIDATION_STYLE_NONE;
	state->validation.type = VALIDATION_TYPE_ANY;
	state->validation.op = VALIDATION_OP_NONE;
	state->validation.allow_blank = TRUE;
	state->validation.use_dropdown = FALSE;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2) {
		if (xml_sax_attr_int (attrs, "Style", &dummy)) {
			state->validation.style = dummy;
		} else if (xml_sax_attr_int (attrs, "Type", &dummy)) {
			state->validation.type = dummy;
		} else if (xml_sax_attr_int (attrs, "Operator", &dummy)) {
			state->validation.op = dummy;
		} else if (!strcmp (attrs[0], "Title")) {
			state->validation.title = g_strdup ((gchar *)attrs[1]);
		} else if (!strcmp (attrs[0], "Message")) {
			state->validation.msg = g_strdup ((gchar *)attrs[1]);
		} else if (xml_sax_attr_bool (attrs, "AllowBlank", &b_dummy)) {
			state->validation.allow_blank = b_dummy;
		} else if (xml_sax_attr_bool (attrs, "UseDropdown", &b_dummy)) {
			state->validation.use_dropdown = b_dummy;
		} else
			unknown_attr (gsf_state, attrs);
	}
}

static void
xml_sax_validation_end (GsfXMLIn *gsf_state, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	XMLSaxParseState *state = (XMLSaxParseState *)gsf_state->user_state;

	g_return_if_fail (state->style != NULL);

	gnm_style_set_validation (state->style,
		validation_new (state->validation.style,
				state->validation.type,
				state->validation.op,
				state->validation.title,
				state->validation.msg,
				state->validation.expr[0],
				state->validation.expr[1],
				state->validation.allow_blank,
				state->validation.use_dropdown));

	g_free (state->validation.title);
	state->validation.title = NULL;
	g_free (state->validation.msg);
	state->validation.msg = NULL;
	state->validation.expr[0] = state->validation.expr[1] = NULL;
}

static void
xml_sax_validation_expr_end (GsfXMLIn *gsf_state, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	XMLSaxParseState *state = (XMLSaxParseState *)gsf_state->user_state;

	int const i = gsf_state->node->user_data.v_int;
	GnmExpr const *expr;
	GnmParsePos pos;

	g_return_if_fail (state->validation.expr [i] == NULL);

	expr = gnm_expr_parse_str_simple (gsf_state->content->str,
		parse_pos_init_sheet (&pos, state->sheet));

	g_return_if_fail (expr != NULL);

	state->validation.expr [i] = expr;
}

static void
xml_sax_condition (GsfXMLIn *gsf_state, xmlChar const **attrs)
{
	XMLSaxParseState *state = (XMLSaxParseState *)gsf_state->user_state;

	int dummy;

	g_return_if_fail (state->cond.expr[0] == NULL);
	g_return_if_fail (state->cond.expr[1] == NULL);

	state->cond.op = GNM_STYLE_COND_CUSTOM;
	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (xml_sax_attr_int (attrs, "Operator", &dummy))
			state->cond.op = dummy;
		else
			unknown_attr (gsf_state, attrs);
}

static void
xml_sax_condition_end (GsfXMLIn *gsf_state, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	XMLSaxParseState *state = (XMLSaxParseState *)gsf_state->user_state;
	GnmStyleConditions *sc;

	g_return_if_fail (state->style != NULL);

	if (!gnm_style_is_element_set (state->style, MSTYLE_CONDITIONS) ||
	    NULL == (sc = gnm_style_get_conditions (state->style)))
		gnm_style_set_conditions (state->style,
			(sc = gnm_style_conditions_new ()));
	gnm_style_conditions_insert (sc, &state->cond, -1);

	state->cond.expr[0] = state->cond.expr[1] = NULL;
}

static void
xml_sax_condition_expr_end (GsfXMLIn *gsf_state, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	XMLSaxParseState *state = (XMLSaxParseState *)gsf_state->user_state;

	int const i = gsf_state->node->user_data.v_int;
	GnmExpr const *expr;
	GnmParsePos pos;

	g_return_if_fail (state->cond.expr [i] == NULL);

	expr = gnm_expr_parse_str_simple (gsf_state->content->str,
		parse_pos_init_sheet (&pos, state->sheet));

	g_return_if_fail (expr != NULL);

	state->cond.expr [i] = expr;
}

static void
xml_sax_hlink (GsfXMLIn *gsf_state, xmlChar const **attrs)
{
	XMLSaxParseState *state = (XMLSaxParseState *)gsf_state->user_state;
	char *type = NULL;
	char *target = NULL;
	char *tip = NULL;

	g_return_if_fail (state->style != NULL);

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2) {
		if (!strcmp (attrs[0], "type"))
			type = g_strdup ((gchar *)attrs[1]);
		else if (!strcmp (attrs[0], "target"))
			target = g_strdup ((gchar *)attrs[1]);
		else if (!strcmp (attrs[0], "tip"))
			target = g_strdup ((gchar *)attrs[1]);
		else
			unknown_attr (gsf_state, attrs);
	}

	if (NULL != type && NULL != target) {
		GnmHLink *link = g_object_new (g_type_from_name (type), NULL);
		gnm_hlink_set_target (link, target);
		if (tip != NULL)
			gnm_hlink_set_tip (link, tip);
		gnm_style_set_hlink (state->style, link);
	}

	g_free (type);
	g_free (target);
	g_free (tip);
}

static void
xml_sax_input_msg (GsfXMLIn *gsf_state, xmlChar const **attrs)
{
	XMLSaxParseState *state = (XMLSaxParseState *)gsf_state->user_state;
	char *title = NULL;
	char *msg = NULL;

	g_return_if_fail (state->style != NULL);

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2) {
		if (!strcmp (attrs[0], "Title"))
			title = g_strdup ((gchar *)attrs[1]);
		else if (!strcmp (attrs[0], "Message"))
			msg = g_strdup ((gchar *)attrs[1]);
		else
			unknown_attr (gsf_state, attrs);
	}

	gnm_style_set_input_msg (state->style,
		gnm_input_msg_new (msg, title));
	g_free (title);
	g_free (msg);
}

static void
xml_sax_style_region_borders (GsfXMLIn *gsf_state, xmlChar const **attrs)
{
	XMLSaxParseState *state = (XMLSaxParseState *)gsf_state->user_state;

	int pattern = -1;
	GnmColor *colour = NULL;

	g_return_if_fail (state->style != NULL);

	/* Colour is optional */
	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2) {
		if (xml_sax_attr_color (attrs, "Color", &colour)) ;
		else if (xml_sax_attr_int (attrs, "Style", &pattern)) ;
		else
			unknown_attr (gsf_state, attrs);
	}

	if (pattern >= STYLE_BORDER_NONE) {
		GnmStyleElement const type = gsf_state->node->user_data.v_int;
		GnmBorder *border =
			style_border_fetch ((StyleBorderType)pattern, colour,
					    style_border_get_orientation (type));
		gnm_style_set_border (state->style, type, border);
	}
}

static void
xml_sax_cell (GsfXMLIn *gsf_state, xmlChar const **attrs)
{
	XMLSaxParseState *state = (XMLSaxParseState *)gsf_state->user_state;

	int row = -1, col = -1;
	int rows = -1, cols = -1;
	int value_type = -1;
	GOFormat *value_fmt = NULL;
	int expr_id = -1;

	g_return_if_fail (state->cell.row == -1);
	g_return_if_fail (state->cell.col == -1);
	g_return_if_fail (state->array_rows == -1);
	g_return_if_fail (state->array_cols == -1);
	g_return_if_fail (state->expr_id == -1);
	g_return_if_fail (state->value_type == -1);

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2) {
		if (xml_sax_attr_int (attrs, "Col", &col)) ;
		else if (xml_sax_attr_int (attrs, "Row", &row)) ;
		else if (xml_sax_attr_int (attrs, "Cols", &cols)) ;
		else if (xml_sax_attr_int (attrs, "Rows", &rows)) ;
		else if (xml_sax_attr_int (attrs, "ExprID", &expr_id)) ;
		else if (xml_sax_attr_int (attrs, "ValueType", &value_type)) ;
		else if (!strcmp (attrs[0], "ValueFormat"))
			value_fmt = go_format_new_from_XL ((char *)attrs[1], FALSE);
		else
			unknown_attr (gsf_state, attrs);
	}

	g_return_if_fail (col >= 0);
	g_return_if_fail (row >= 0);

	if (cols > 0 || rows > 0) {
		/* Both must be valid */
		g_return_if_fail (cols > 0);
		g_return_if_fail (rows > 0);

		state->array_cols = cols;
		state->array_rows = rows;
	}

	state->cell.row = row;
	state->cell.col = col;
	state->expr_id = expr_id;
	state->value_type = value_type;
	state->value_fmt = value_fmt;
}

/**
 * xml_cell_set_array_expr : Utility routine to parse an expression
 *     and store it as an array.
 *
 * @cell : The upper left hand corner of the array.
 * @text : The text to parse.
 * @rows : The number of rows.
 * @cols : The number of columns.
 */
static void
xml_cell_set_array_expr (GnmCell *cell, char const *text,
			 int const cols, int const rows)
{
	GnmParsePos pp;
	GnmExpr const *expr = gnm_expr_parse_str_simple (text,
		parse_pos_init_cell (&pp, cell));

	g_return_if_fail (expr != NULL);
	cell_set_array_formula (cell->base.sheet,
				cell->pos.col, cell->pos.row,
				cell->pos.col + cols-1, cell->pos.row + rows-1,
				expr);
}

/**
 * xml_not_used_old_array_spec : See if the string corresponds to
 *     a pre-0.53 style array expression.
 *     If it is the upper left corner	 - assign it.
 *     If it is a member of the an array - ignore it the corner will assign it.
 *     If it is not a member of an array return TRUE.
 */
static gboolean
xml_not_used_old_array_spec (GnmCell *cell, char const *content)
{
	int rows, cols, row, col;

#if 0
	/* This is the syntax we are trying to parse */
	g_string_append_printf (str, "{%s}(%d,%d)[%d][%d]", expr_text,
		array.rows, array.cols, array.y, array.x);
#endif
	char *end, *expr_end, *ptr;

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
		xml_cell_set_array_expr (cell, content+2, rows, cols);
	}

	return FALSE;
}

static void
xml_sax_cell_content (GsfXMLIn *gsf_state, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	XMLSaxParseState *state = (XMLSaxParseState *)gsf_state->user_state;

	gboolean is_new_cell, is_post_52_array = FALSE;
	GnmCell *cell;

	int const col = state->cell.col;
	int const row = state->cell.row;
	int const array_cols = state->array_cols;
	int const array_rows = state->array_rows;
	int const expr_id = state->expr_id;
	int const value_type = state->value_type;
	GOFormat *value_fmt = state->value_fmt;
	gpointer const id = GINT_TO_POINTER (expr_id);
	gpointer expr = NULL;

	/* Clean out the state before any error checking */
	state->cell.row = state->cell.col = -1;
	state->array_rows = state->array_cols = -1;
	state->expr_id = -1;
	state->value_type = -1;
	state->value_fmt = NULL;

	g_return_if_fail (col >= 0);
	g_return_if_fail (row >= 0);

	cell = sheet_cell_get (state->sheet, col, row);
	if ((is_new_cell = (cell == NULL)))
		cell = sheet_cell_new (state->sheet, col, row);

	if (cell == NULL)
		return;

	if (expr_id > 0)
		expr = g_hash_table_lookup (state->expr_map, id);

	is_post_52_array = (array_cols > 0) && (array_rows > 0);

	if (gsf_state->content->len > 0) {
		char const * content = gsf_state->content->str;

		if (is_post_52_array) {
			g_return_if_fail (content[0] == '=');

			xml_cell_set_array_expr (cell, content+1,
						 array_cols, array_rows);
		} else if (state->version >= GNM_XML_V3 ||
			   xml_not_used_old_array_spec (cell, content)) {
			if (value_type > 0) {
				GnmValue *v = value_new_from_string (value_type, content, value_fmt, FALSE);
				if (v == NULL) {
					g_warning ("Unable to parse \"%s\" as type %d.",
						   content, value_type);
					cell_set_text (cell, content);
				} else
					cell_set_value (cell, v);
			} else
				cell_set_text (cell, content);
		}

		if (expr_id > 0) {
			gpointer id = GINT_TO_POINTER (expr_id);
			gpointer expr =
				g_hash_table_lookup (state->expr_map, id);
			if (expr == NULL) {
				if (cell_has_expr (cell))
					g_hash_table_insert (state->expr_map, id,
							     (gpointer)cell->base.expression);
				else
					g_warning ("XML-IO : Shared expression with no expession ??");
			} else if (!is_post_52_array)
				g_warning ("XML-IO : Duplicate shared expression");
		}
	} else if (expr_id > 0) {
		gpointer expr = g_hash_table_lookup (state->expr_map,
			GINT_TO_POINTER (expr_id));

		if (expr != NULL)
			cell_set_expr (cell, expr);
		else
			g_warning ("XML-IO : Missing shared expression");
	} else if (is_new_cell)
		/*
		 * Only set to empty if this is a new cell.
		 * If it was created by a previous array
		 * we do not want to erase it.
		 */
		cell_set_value (cell, value_new_empty ());

	if (value_fmt != NULL)
		go_format_unref (value_fmt);
}

static void
xml_sax_merge (GsfXMLIn *gsf_state, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	XMLSaxParseState *state = (XMLSaxParseState *)gsf_state->user_state;

	GnmRange r;
	g_return_if_fail (gsf_state->content->len > 0);

	if (parse_range (gsf_state->content->str, &r))
		sheet_merge_add (state->sheet, &r, FALSE,
			GO_CMD_CONTEXT (state->context));
}

static void
xml_sax_object_start (GsfXMLIn *gsf_state, xmlChar const **attrs)
{
	XMLSaxParseState *state = (XMLSaxParseState *)gsf_state->user_state;
	char const *type_name = gsf_state->node->name;
	int tmp_int, i;
	SheetObject *so;
	SheetObjectClass *klass;

	g_return_if_fail (state->so == NULL);

	/* Old crufty IO */
	if (!strcmp (type_name, "Rectangle"))
		so = g_object_new (GNM_SO_FILLED_TYPE, NULL);
	else if (!strcmp (type_name, "Ellipse"))
		so = g_object_new (GNM_SO_FILLED_TYPE, "is-oval", TRUE, NULL);
	else if (!strcmp (type_name, "Line"))
		so = g_object_new (GNM_SO_LINE_TYPE, "is-arrow", TRUE, NULL);
	else if (!strcmp (type_name, "Arrow"))
		so = g_object_new (GNM_SO_LINE_TYPE, NULL);

	/* Class renamed between 1.0.x and 1.2.x */
	else if (!strcmp (type_name, "GnmGraph"))
		so = sheet_object_graph_new (FALSE);

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

	else {
		GType type = g_type_from_name ((gchar *)type_name);

		if (type == 0) {
			char *str = g_strdup_printf (_("Unsupported object type '%s'"),
						     type_name);
			gnm_io_warning_unsupported_feature (state->context, str);
			g_free (str);
			return;
		}

		so = g_object_new (type, NULL);
		if (so == NULL)
			return;

	}
	state->so = so;

	so->anchor.direction = SO_DIR_UNKNOWN;
	for (i = 0; attrs != NULL && attrs[i] && attrs[i+1] ; i += 2) {
		if (!strcmp (attrs[i], "ObjectBound")) {
			GnmRange r;
			if (parse_range (attrs[i+1], &r))
				so->anchor.cell_bound = r;
		} else if (!strcmp (attrs[i], "ObjectOffset")) {
			sscanf (attrs[i+1], "%g %g %g %g",
				so->anchor.offset +0, so->anchor.offset +1,
				so->anchor.offset +2, so->anchor.offset +3);
		} else if (!strcmp (attrs[i], "ObjectAnchorType")) {
			int n[4], count;
			sscanf (attrs[i+1], "%d %d %d %d", n+0, n+1, n+2, n+3);

			for (count = 4; count-- > 0 ; )
				so->anchor.type[count] = n[count];
		} else if (xml_sax_attr_int (attrs+i, "Direction", &tmp_int))
			so->anchor.direction = tmp_int;
#if 0 /* There may be extra attributes that are handled by the objects */
		else
			unknown_attr (gsf_state, attrs+i);
#endif
	}

	klass = SHEET_OBJECT_CLASS (G_OBJECT_GET_CLASS (so));
	if (klass->prep_sax_parser)
		(klass->prep_sax_parser) (so, gsf_state, attrs);
}

static void
xml_sax_object_end (GsfXMLIn *gsf_state, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	XMLSaxParseState *state = (XMLSaxParseState *)gsf_state->user_state;
	sheet_object_set_sheet (state->so, state->sheet);
	g_object_unref (state->so);
	state->so = NULL;
}

static void
xml_sax_named_expr_end (GsfXMLIn *gsf_state, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	XMLSaxParseState *state = (XMLSaxParseState *)gsf_state->user_state;

	GnmParseError  perr;
	GnmParsePos    pos;
	GnmExpr const	*expr;

	g_return_if_fail (state->name.name != NULL);
	g_return_if_fail (state->name.value != NULL);

	parse_pos_init (&pos, state->wb, state->sheet, 0, 0);
	if (state->name.position) {
		GnmCellRef tmp;
		char const *res = cellref_parse (&tmp, state->name.position, &pos.eval);
		if (res != NULL && *res == '\0') {
			pos.eval.col = tmp.col;
			pos.eval.row = tmp.row;
		}
	}

	parse_error_init (&perr);
	expr = gnm_expr_parse_str (state->name.value, &pos,
				   GNM_EXPR_PARSE_DEFAULT,
				   gnm_expr_conventions_default, &perr);
	if (expr != NULL) {
		char *err = NULL;
		expr_name_add (&pos, state->name.name, expr, &err, TRUE, NULL);
		if (err != NULL) {
			gnm_io_warning (state->context, err);
			g_free (err);
		}
	} else
		state->delayed_names = g_list_prepend (state->delayed_names,
			expr_name_add (&pos, state->name.name, 
				gnm_expr_new_constant (value_new_string (state->name.value)),
				NULL, TRUE, NULL));

	parse_error_free (&perr);

	if (state->name.position) {
		g_free (state->name.position);
		state->name.position = NULL;
	}
	g_free (state->name.name);
	g_free (state->name.value);
	state->name.name = NULL;
	state->name.value = NULL;
}

static void
xml_sax_named_expr_prop (GsfXMLIn *gsf_state, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	XMLSaxParseState *state = (XMLSaxParseState *)gsf_state->user_state;

	char const * content = gsf_state->content->str;
	int const len = gsf_state->content->len;

	switch (gsf_state->node->user_data.v_int) {
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
xml_sax_orientation (GsfXMLIn *gsf_state, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	XMLSaxParseState *state = (XMLSaxParseState *)gsf_state->user_state;
	PrintInformation *pi;

	g_return_if_fail (state->sheet != NULL);
	g_return_if_fail (state->sheet->print_info != NULL);

	pi = state->sheet->print_info;
	pi->portrait_orientation = (strcmp (gsf_state->content->str, "portrait") == 0);
}

static void
xml_sax_paper (GsfXMLIn *gsf_state, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	XMLSaxParseState *state = (XMLSaxParseState *)gsf_state->user_state;

	g_return_if_fail (state->sheet != NULL);
	g_return_if_fail (state->sheet->print_info != NULL);

	print_info_set_paper (state->sheet->print_info, gsf_state->content->str);
}

/****************************************************************************/

#define GNM	0

static GsfXMLInNS content_ns[] = {
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
	{ NULL }
};

static GsfXMLInNode gnumeric_1_0_dtd[] = {
GSF_XML_IN_NODE_FULL (START, START, -1, NULL, FALSE, FALSE, TRUE, NULL, NULL, 0),
GSF_XML_IN_NODE_FULL (START, WB, GNM, "Workbook", FALSE, TRUE, FALSE, &xml_sax_wb, NULL, 0),
  GSF_XML_IN_NODE (WB, WB_VERSION, GNM, "Version", FALSE, NULL, NULL),
  GSF_XML_IN_NODE (WB, WB_ATTRIBUTES, GNM, "Attributes", FALSE, NULL, NULL),
    GSF_XML_IN_NODE (WB_ATTRIBUTES, WB_ATTRIBUTE, GNM, "Attribute", FALSE, NULL, &xml_sax_finish_parse_wb_attr),
      GSF_XML_IN_NODE_FULL (WB_ATTRIBUTE, WB_ATTRIBUTE_NAME, GNM, "name",   TRUE, FALSE, FALSE, NULL, &xml_sax_attr_elem, 0),
      GSF_XML_IN_NODE_FULL (WB_ATTRIBUTE, WB_ATTRIBUTE_VALUE, GNM, "value", TRUE, FALSE, FALSE, NULL, &xml_sax_attr_elem, 1),
      GSF_XML_IN_NODE (WB_ATTRIBUTE, WB_ATTRIBUTE_TYPE, GNM, "type", FALSE, NULL, NULL),

  GSF_XML_IN_NODE (WB, WB_SUMMARY, GNM, "Summary", FALSE, NULL, NULL),
      GSF_XML_IN_NODE (WB_SUMMARY, WB_SUMMARY_ITEM, GNM, "Item", FALSE, NULL, NULL),
	GSF_XML_IN_NODE (WB_SUMMARY_ITEM, WB_SUMMARY_ITEM_NAME, GNM, "name", TRUE, NULL, NULL),
	GSF_XML_IN_NODE (WB_SUMMARY_ITEM, WB_SUMMARY_ITEM_VALUE_STR, GNM, "val-string", TRUE, NULL, NULL),
	GSF_XML_IN_NODE (WB_SUMMARY_ITEM, WB_SUMMARY_ITEM_VALUE_INT, GNM, "val-int", TRUE, NULL, NULL),

  GSF_XML_IN_NODE (WB, WB_SHEETNAME_INDEX, GNM, "SheetNameIndex", FALSE, NULL, NULL),
    GSF_XML_IN_NODE (WB_SHEETNAME_INDEX, WB_SHEETNAME, GNM, "SheetName", TRUE, NULL, &xml_sax_wb_sheetname),

  GSF_XML_IN_NODE (WB, WB_NAMED_EXPRS, GNM, "Names", FALSE, NULL, NULL),
    GSF_XML_IN_NODE (WB_NAMED_EXPRS, WB_NAMED_EXPR, GNM, "Name", FALSE, NULL, &xml_sax_named_expr_end),
      GSF_XML_IN_NODE_FULL (WB_NAMED_EXPR, WB_NAMED_EXPR_NAME,	   GNM, "name",	    TRUE, FALSE, FALSE, NULL, &xml_sax_named_expr_prop, 0),
      GSF_XML_IN_NODE_FULL (WB_NAMED_EXPR, WB_NAMED_EXPR_VALUE,	   GNM, "value",    TRUE, FALSE, FALSE, NULL, &xml_sax_named_expr_prop, 1),
      GSF_XML_IN_NODE_FULL (WB_NAMED_EXPR, WB_NAMED_EXPR_POSITION, GNM, "position", TRUE, FALSE, FALSE, NULL, &xml_sax_named_expr_prop, 2),
      /* sometimes not namespaced */
      GSF_XML_IN_NODE_FULL (WB_NAMED_EXPR, WB_NAMED_EXPR_NAME_NS,     -1, "name",     TRUE, FALSE, FALSE, NULL, &xml_sax_named_expr_prop, 0),
      GSF_XML_IN_NODE_FULL (WB_NAMED_EXPR, WB_NAMED_EXPR_VALUE_NS,    -1, "value",    TRUE, FALSE, FALSE, NULL, &xml_sax_named_expr_prop, 1),
      GSF_XML_IN_NODE_FULL (WB_NAMED_EXPR, WB_NAMED_EXPR_POSITION_NS, -1, "position", TRUE, FALSE, FALSE, NULL, &xml_sax_named_expr_prop, 2),

  GSF_XML_IN_NODE (WB, WB_SHEETS, GNM, "Sheets", FALSE, NULL, NULL),
    GSF_XML_IN_NODE (WB_SHEETS, SHEET, GNM, "Sheet", FALSE, &xml_sax_sheet_start, &xml_sax_sheet_end),
      GSF_XML_IN_NODE (SHEET, SHEET_NAME, GNM, "Name", TRUE, NULL, &xml_sax_sheet_name),
      GSF_XML_IN_NODE (SHEET, SHEET_MAXCOL, GNM, "MaxCol", FALSE, NULL, NULL),
      GSF_XML_IN_NODE (SHEET, SHEET_MAXROW, GNM, "MaxRow", FALSE, NULL, NULL),
      GSF_XML_IN_NODE (SHEET, SHEET_ZOOM, GNM, "Zoom", TRUE, NULL, &xml_sax_sheet_zoom),
      GSF_XML_IN_NODE (SHEET, SHEET_NAMED_EXPRS, GNM, "Names", FALSE, NULL, NULL),
	GSF_XML_IN_NODE (SHEET_NAMED_EXPRS, SHEET_NAMED_EXPR, GNM, "Name", FALSE, NULL, &xml_sax_named_expr_end),
	  GSF_XML_IN_NODE_FULL (SHEET_NAMED_EXPR, SHEET_NAMED_EXPR_NAME,     GNM, "name",     TRUE, FALSE, FALSE, NULL, &xml_sax_named_expr_prop, 0),
	  GSF_XML_IN_NODE_FULL (SHEET_NAMED_EXPR, SHEET_NAMED_EXPR_VALUE,    GNM, "value",    TRUE, FALSE, FALSE, NULL, &xml_sax_named_expr_prop, 1),
	  GSF_XML_IN_NODE_FULL (SHEET_NAMED_EXPR, SHEET_NAMED_EXPR_POSITION, GNM, "position", TRUE, FALSE, FALSE, NULL, &xml_sax_named_expr_prop, 2),

      GSF_XML_IN_NODE (SHEET, SHEET_PRINTINFO, GNM, "PrintInformation", FALSE, NULL, NULL),
	GSF_XML_IN_NODE (SHEET_PRINTINFO, PRINT_MARGINS, GNM, "Margins", FALSE, NULL, NULL),
	  GSF_XML_IN_NODE_FULL (PRINT_MARGINS, PRINT_MARGIN_TOP,    GNM, "top",	  TRUE, FALSE, FALSE, &xml_sax_print_margins, NULL, 0),
	  GSF_XML_IN_NODE_FULL (PRINT_MARGINS, PRINT_MARGIN_BOTTOM, GNM, "bottom",TRUE, FALSE, FALSE, &xml_sax_print_margins, NULL, 1),
	  GSF_XML_IN_NODE_FULL (PRINT_MARGINS, PRINT_MARGIN_LEFT,   GNM, "left",  TRUE, FALSE, FALSE, &xml_sax_print_margins, NULL, 2),
	  GSF_XML_IN_NODE_FULL (PRINT_MARGINS, PRINT_MARGIN_RIGHT,  GNM, "right", TRUE, FALSE, FALSE, &xml_sax_print_margins, NULL, 3),
	  GSF_XML_IN_NODE_FULL (PRINT_MARGINS, PRINT_MARGIN_HEADER, GNM, "header",TRUE, FALSE, FALSE, &xml_sax_print_margins, NULL, 4),
	  GSF_XML_IN_NODE_FULL (PRINT_MARGINS, PRINT_MARGIN_FOOTER, GNM, "footer",TRUE, FALSE, FALSE, &xml_sax_print_margins, NULL, 5),

	GSF_XML_IN_NODE (SHEET_PRINTINFO, PRINT_SCALE,	    GNM, "Scale",	TRUE, &xml_sax_print_scale, NULL),
	GSF_XML_IN_NODE (SHEET_PRINTINFO, PRINT_VCENTER,    GNM, "vcenter",	FALSE, NULL, NULL),
	GSF_XML_IN_NODE (SHEET_PRINTINFO, PRINT_HCENTER,    GNM, "hcenter",	FALSE, NULL, NULL),
	GSF_XML_IN_NODE (SHEET_PRINTINFO, PRINT_GRID,	    GNM, "grid",	FALSE, NULL, NULL),
	GSF_XML_IN_NODE (SHEET_PRINTINFO, PRINT_MONO,	    GNM, "monochrome",	FALSE, NULL, NULL),
	GSF_XML_IN_NODE (SHEET_PRINTINFO, PRINT_AS_DRAFT,   GNM, "draft",	FALSE, NULL, NULL),
	GSF_XML_IN_NODE (SHEET_PRINTINFO, PRINT_COMMENTS,   GNM, "comments",	FALSE, NULL, NULL),
	GSF_XML_IN_NODE (SHEET_PRINTINFO, PRINT_TITLES,	    GNM, "titles",	FALSE, NULL, NULL),
	GSF_XML_IN_NODE (SHEET_PRINTINFO, PRINT_REPEAT_TOP, GNM, "repeat_top", 	FALSE, NULL, NULL),
	GSF_XML_IN_NODE (SHEET_PRINTINFO, PRINT_REPEAT_LEFT,GNM, "repeat_left",	FALSE, NULL, NULL),
	GSF_XML_IN_NODE (SHEET_PRINTINFO, PRINT_HEADER,	    GNM, "Footer",	FALSE, NULL, NULL),
	GSF_XML_IN_NODE (SHEET_PRINTINFO, PRINT_FOOTER,	    GNM, "Header",	FALSE, NULL, NULL),
	GSF_XML_IN_NODE (SHEET_PRINTINFO, PRINT_ORDER,	    GNM, "order",	TRUE,  NULL, NULL),
	GSF_XML_IN_NODE (SHEET_PRINTINFO, PRINT_PAPER,	    GNM, "paper",	TRUE,  NULL, &xml_sax_paper),
	GSF_XML_IN_NODE (SHEET_PRINTINFO, PRINT_ORIENT,	    GNM, "orientation",	TRUE,  NULL, &xml_sax_orientation),
	GSF_XML_IN_NODE (SHEET_PRINTINFO, PRINT_ONLY_STYLE, GNM, "even_if_only_styles", TRUE, NULL, NULL),

      GSF_XML_IN_NODE (SHEET, SHEET_STYLES, GNM, "Styles", FALSE, NULL, NULL),
	GSF_XML_IN_NODE (SHEET_STYLES, STYLE_REGION, GNM, "StyleRegion", FALSE, &xml_sax_style_region_start, &xml_sax_style_region_end),
	  GSF_XML_IN_NODE (STYLE_REGION, STYLE_STYLE, GNM, "Style", FALSE, &xml_sax_styleregion_start, NULL),
	    GSF_XML_IN_NODE (STYLE_STYLE, STYLE_FONT, GNM, "Font", TRUE, &xml_sax_styleregion_font, &xml_sax_styleregion_font_end),
	    GSF_XML_IN_NODE (STYLE_STYLE, STYLE_BORDER, GNM, "StyleBorder", FALSE, NULL, NULL),
	      GSF_XML_IN_NODE_FULL (STYLE_BORDER, BORDER_TOP,     GNM, "Top",
				    FALSE, FALSE, FALSE, &xml_sax_style_region_borders, NULL, MSTYLE_BORDER_TOP),
	      GSF_XML_IN_NODE_FULL (STYLE_BORDER, BORDER_BOTTOM,  GNM, "Bottom",
				    FALSE, FALSE, FALSE, &xml_sax_style_region_borders, NULL, MSTYLE_BORDER_BOTTOM),
	      GSF_XML_IN_NODE_FULL (STYLE_BORDER, BORDER_LEFT,    GNM, "Left",
				    FALSE, FALSE, FALSE, &xml_sax_style_region_borders, NULL, MSTYLE_BORDER_LEFT),
	      GSF_XML_IN_NODE_FULL (STYLE_BORDER, BORDER_RIGHT,   GNM, "Right",
				    FALSE, FALSE, FALSE, &xml_sax_style_region_borders, NULL, MSTYLE_BORDER_RIGHT),
	      GSF_XML_IN_NODE_FULL (STYLE_BORDER, BORDER_DIAG,    GNM, "Diagonal",
				    FALSE, FALSE, FALSE, &xml_sax_style_region_borders, NULL, MSTYLE_BORDER_DIAGONAL),
	      GSF_XML_IN_NODE_FULL (STYLE_BORDER, BORDER_REV_DIAG,GNM, "Rev-Diagonal",
				    FALSE, FALSE, FALSE, &xml_sax_style_region_borders, NULL, MSTYLE_BORDER_REV_DIAGONAL),

	    GSF_XML_IN_NODE (STYLE_STYLE, STYLE_VALIDATION, GNM, "Validation", FALSE, &xml_sax_validation, &xml_sax_validation_end),
	      GSF_XML_IN_NODE_FULL (STYLE_VALIDATION, STYLE_VALIDATION_EXPR0, GNM, "Expression0",
				    TRUE, FALSE, FALSE, NULL, &xml_sax_validation_expr_end, 0),
	      GSF_XML_IN_NODE_FULL (STYLE_VALIDATION, STYLE_VALIDATION_EXPR1, GNM, "Expression1",
				    TRUE, FALSE, FALSE, NULL, &xml_sax_validation_expr_end, 1),
	    GSF_XML_IN_NODE (STYLE_STYLE, STYLE_HYPERLINK, GNM, "HyperLink", FALSE, &xml_sax_hlink, NULL),
	    GSF_XML_IN_NODE (STYLE_STYLE, STYLE_INPUT_MSG, GNM, "InputMessage", FALSE, &xml_sax_input_msg, NULL),
	    GSF_XML_IN_NODE (STYLE_STYLE, STYLE_CONDITION, GNM, "Validation", FALSE, &xml_sax_condition, &xml_sax_condition_end),
	      GSF_XML_IN_NODE_FULL (STYLE_CONDITION, STYLE_CONDITION_EXPR0, GNM, "Expression0",
				    TRUE, FALSE, FALSE, NULL, &xml_sax_condition_expr_end, 0),
	      GSF_XML_IN_NODE_FULL (STYLE_CONDITION, STYLE_CONDITION_EXPR1, GNM, "Expression1",
				    TRUE, FALSE, FALSE, NULL, &xml_sax_condition_expr_end, 1),

      GSF_XML_IN_NODE_FULL (SHEET, SHEET_COLS, GNM, "Cols",
			    FALSE, FALSE, FALSE, &xml_sax_cols_rows, NULL, TRUE),
	GSF_XML_IN_NODE_FULL (SHEET_COLS, COL, GNM, "ColInfo",
			      FALSE, FALSE, FALSE, &xml_sax_colrow, NULL, TRUE),

      GSF_XML_IN_NODE_FULL (SHEET, SHEET_ROWS, GNM, "Rows",
			    FALSE, FALSE, FALSE, &xml_sax_cols_rows, NULL, FALSE),
	GSF_XML_IN_NODE_FULL (SHEET_ROWS, ROW, GNM, "RowInfo",
			      FALSE, FALSE, FALSE, &xml_sax_colrow, NULL, FALSE),

      GSF_XML_IN_NODE (SHEET, SHEET_SELECTIONS, GNM, "Selections", FALSE, &xml_sax_selection, &xml_sax_selection_end),
	GSF_XML_IN_NODE (SHEET_SELECTIONS, SELECTION, GNM, "Selection", FALSE, &xml_sax_selection_range, NULL),

      GSF_XML_IN_NODE (SHEET, SHEET_CELLS, GNM, "Cells", FALSE, NULL, NULL),
	GSF_XML_IN_NODE (SHEET_CELLS, CELL, GNM, "Cell", TRUE, &xml_sax_cell, &xml_sax_cell_content),
	  GSF_XML_IN_NODE (CELL, CELL_CONTENT, GNM, "Content", TRUE, NULL, &xml_sax_cell_content),

      GSF_XML_IN_NODE (SHEET, SHEET_MERGED_REGION, GNM, "MergedRegions", FALSE, NULL, NULL),
	GSF_XML_IN_NODE (SHEET_MERGED_REGION, SHEET_MERGE, GNM, "Merge", TRUE, NULL, &xml_sax_merge),

      GSF_XML_IN_NODE (SHEET, SHEET_LAYOUT, GNM, "SheetLayout", FALSE, &xml_sax_sheet_layout, NULL),
	GSF_XML_IN_NODE (SHEET_LAYOUT, SHEET_FREEZEPANES, GNM, "FreezePanes", FALSE, &xml_sax_sheet_freezepanes, NULL),

      GSF_XML_IN_NODE (SHEET, SHEET_SOLVER, GNM, "Solver", FALSE, NULL, NULL),
      GSF_XML_IN_NODE (SHEET, SHEET_SCENARIOS, GNM, "Scenarios", FALSE, NULL, NULL),
        GSF_XML_IN_NODE (SHEET_SCENARIOS, SHEET_SCENARIO, GNM, "Scenario", FALSE, NULL, NULL),

      GSF_XML_IN_NODE (SHEET, SHEET_OBJECTS, GNM, "Objects", FALSE, NULL, NULL),
        /* Old crufty IO */
	GSF_XML_IN_NODE (SHEET_OBJECTS, OBJECT_ANCIENT_RECTANGLE, GNM, "Rectangle", FALSE, &xml_sax_object_start, &xml_sax_object_end),
	GSF_XML_IN_NODE (SHEET_OBJECTS, OBJECT_ANCIENT_ELLIPSE, GNM, "Ellipse", FALSE,	&xml_sax_object_start, &xml_sax_object_end),
	GSF_XML_IN_NODE (SHEET_OBJECTS, OBJECT_ANCIENT_ARROW, GNM, "Arrow", FALSE,	&xml_sax_object_start, &xml_sax_object_end),
	GSF_XML_IN_NODE (SHEET_OBJECTS, OBJECT_ANCIENT_LINE, GNM, "Line", FALSE,	&xml_sax_object_start, &xml_sax_object_end),
	/* Class renamed between 1.0.x and 1.2.x */
	GSF_XML_IN_NODE (SHEET_OBJECTS, OBJECT_OLD_GRAPH, GNM,	"GnmGraph", FALSE,	&xml_sax_object_start, &xml_sax_object_end),
	/* Class renamed in 1.2.2 */
	GSF_XML_IN_NODE (SHEET_OBJECTS, OBJECT_OLD_COMMENT, GNM, "CellComment", FALSE,	&xml_sax_object_start, &xml_sax_object_end),
	/* Class renamed in 1.3.91 */
	GSF_XML_IN_NODE (SHEET_OBJECTS, OBJECT_OLD_LINE, GNM, "SheetObjectGraphic", FALSE, &xml_sax_object_start, &xml_sax_object_end),
	GSF_XML_IN_NODE (SHEET_OBJECTS, OBJECT_OLD_FILLED, GNM, "SheetObjectFilled", FALSE, &xml_sax_object_start, &xml_sax_object_end),
	GSF_XML_IN_NODE (SHEET_OBJECTS, OBJECT_OLD_TEXT, GNM, "SheetObjectText", FALSE,	&xml_sax_object_start, &xml_sax_object_end),
	GSF_XML_IN_NODE (SHEET_OBJECTS, OBJECT_GRAPH, GNM, "SheetObjectGraph", FALSE,	&xml_sax_object_start, &xml_sax_object_end),
	GSF_XML_IN_NODE (SHEET_OBJECTS, OBJECT_IMAGE, GNM, "SheetObjectImage", FALSE,	&xml_sax_object_start, &xml_sax_object_end),

  GSF_XML_IN_NODE (WB, WB_GEOMETRY, GNM, "Geometry", FALSE, &xml_sax_wb_view, NULL),
  GSF_XML_IN_NODE (WB, WB_VIEW, GNM, "UIData", FALSE, &xml_sax_wb_view, NULL),
  GSF_XML_IN_NODE (WB, WB_CALC, GNM, "Calculation", FALSE, &xml_sax_calculation, NULL),
  { NULL }
};

#if 0
I do not like this interface
- We may want per node unknown handlers although I am not sure the space is worth the time
- We need a link from the new nodes added to the plugin that supplied them

static gboolean
xml_sax_unknown (GsfXMLIn *state, xmlChar const *elem, xmlChar const **attrs)
{
	g_return_val_if_fail (state != NULL, FALSE);
	g_return_val_if_fail (state->doc != NULL, FALSE);
	g_return_val_if_fail (state->node != NULL, FALSE);

	if (GNM == state->node->ns_id &&
	    0 == strcmp (state->node->id, "SHEET_OBJECTS")) {
		g_warning ("unknown : %s", state->node->name);
		return TRUE;
	}
	return FALSE;
}
#endif

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
	guint8 const *buf;
	gsf_off_t input_size;
	GString *buffer;
	guint ui;
	char *converted;
	const char *encoding;
	gboolean ok;

	buf = gsf_input_read (input, strlen (noencheader), NULL);
	if (!buf || strncmp (noencheader, buf, strlen (noencheader)) != 0)
		return input;

	input_size = gsf_input_remaining (input);
	buffer = g_string_sized_new (input_size + strlen (encheader));
	g_string_append (buffer, encheader);
	ok = gsf_input_read (input, input_size, buffer->str + strlen (encheader)) != NULL;
	gsf_input_seek (input, 0, G_SEEK_SET);
	if (!ok) {
		g_string_free (buffer, TRUE);
		return input;
	}
	buffer->len = input_size + strlen (encheader);
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
		}
	}

	encoding = go_guess_encoding (buffer->str, buffer->len, NULL, &converted);
	g_string_free (buffer, TRUE);

	if (encoding) {
		g_object_unref (input);
		if (!quiet)
			g_warning ("Converted xml document with no explicit encoding from transliterated %s to UTF-8.",
				   encoding);
		return gsf_input_memory_new (converted, strlen (converted), TRUE);
	} else {
		if (!quiet)
			g_warning ("Failed to convert xml document with no explicit encoding to UTF-8.");
		return input;
	}
}

void
gnm_xml_file_open (GOFileOpener const *fo, IOContext *io_context,
		   gpointer wb_view, GsfInput *input)
{
	XMLSaxParseState state;
	GsfXMLInDoc     *doc;
	char *old_num_locale, *old_monetary_locale;

	g_return_if_fail (IS_WORKBOOK_VIEW (wb_view));
	g_return_if_fail (GSF_IS_INPUT (input));

	doc = gsf_xml_in_doc_new (gnumeric_1_0_dtd, content_ns);
	if (doc == NULL)
		return;
#if 0
	gsf_xml_in_doc_set_unknown_handler (gnm_sax_in_doc, &xml_sax_unknown);
#endif
	/* init */
	state.context = io_context;
	state.wb_view = wb_view;
	state.wb = wb_view_workbook (wb_view);
	state.sheet = NULL;
	state.version = GNM_XML_UNKNOWN;
	state.attribute.name = state.attribute.value = NULL;
	state.name.name = state.name.value = state.name.position = NULL;
	state.style_range_init = FALSE;
	state.style = NULL;
	state.cell.row = state.cell.col = -1;
	state.array_rows = state.array_cols = -1;
	state.expr_id = -1;
	state.value_type = -1;
	state.value_fmt = NULL;
	state.validation.title = state.validation.msg = NULL;
	state.validation.expr[0] = state.validation.expr[1] = NULL;
	state.expr_map = g_hash_table_new (g_direct_hash, g_direct_equal);
	state.delayed_names = NULL;
	state.so = NULL;

	g_object_ref (input);
	input = maybe_gunzip (input);
	input = maybe_convert (input, FALSE);
	gsf_input_seek (input, 0, G_SEEK_SET);

	old_num_locale = g_strdup (go_setlocale (LC_NUMERIC, NULL));
	go_setlocale (LC_NUMERIC, "C");
	old_monetary_locale = g_strdup (go_setlocale (LC_MONETARY, NULL));
	go_setlocale (LC_MONETARY, "C");
	go_set_untranslated_bools ();

	if (!gsf_xml_in_doc_parse (doc, input, &state))
		gnumeric_io_error_string (io_context, _("XML document not well formed!"));
	else
		workbook_queue_all_recalc (state.wb);

	/* go_setlocale restores bools to locale translation */
	go_setlocale (LC_MONETARY, old_monetary_locale);
	g_free (old_monetary_locale);
	go_setlocale (LC_NUMERIC, old_num_locale);
	g_free (old_num_locale);

	g_object_unref (input);

	/* cleanup */
	g_hash_table_destroy (state.expr_map);

	gsf_xml_in_doc_free (doc);
}
