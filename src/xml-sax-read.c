/* vim: set sw=8: */

/*
 * xml-sax-read.c : a test harness for the sax based xml parse routines.
 *
 * Copyright (C) 2000 Jody Goldberg (jody@gnome.org)
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
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
 * USA
 */

#include <gnumeric-config.h>
#include <gnumeric-i18n.h>
#include <gnumeric.h>
#include "xml-io-version.h"
#include "io-context.h"
#include "plugin.h"
#include "plugin-util.h"
#include "module-plugin-defs.h"
#include "sheet-view.h"
#include "sheet-style.h"
#include "sheet-merge.h"
#include "sheet.h"
#include "ranges.h"
#include "style.h"
#include "style-border.h"
#include "style-color.h"
#include "format.h"
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
#include "error-info.h"

#include <gsf/gsf-libxml.h>
#include <gsf/gsf-input.h>
#include <libxml/tree.h>
#include <libxml/parser.h>
#include <libxml/parserInternals.h>
#include <stdlib.h>
#include <string.h>

GNUMERIC_MODULE_PLUGIN_INFO_DECL;

gboolean xml_sax_file_probe (GnmFileOpener const *fo, GsfInput *input,
                             FileProbeLevel pl);
void     xml_sax_file_open (GnmFileOpener const *fo, IOContext *io_context,
			    WorkbookView *wb_view, GsfInput *input);

/*****************************************************************************/

static gboolean
xml_sax_attr_double (xmlChar const * const *attrs, char const *name, double * res)
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
xml_sax_attr_cellpos (xmlChar const * const *attrs, char const *name, CellPos *val)
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
xml_sax_attr_color (xmlChar const * const *attrs, char const *name, StyleColor **res)
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
xml_sax_attr_range (xmlChar const * const *attrs, Range *res)
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

	gboolean  style_range_init;
	Range	  style_range;
	MStyle   *style;

	CellPos cell;
	int expr_id, array_rows, array_cols;
	int value_type;
	StyleFormat *value_fmt;

	int display_formulas;
	int hide_zero;
	int hide_grid;
	int hide_col_header;
	int hide_row_header;
	int display_outlines;
	int outline_symbols_below;
	int outline_symbols_right;
	StyleColor *tab_color;

	/* expressions with ref > 1 a map from index -> expr pointer */
	GHashTable *expr_map;
	GList *delayed_names;
} XMLSaxParseState;

/****************************************************************************/

static void
unknown_attr (XMLSaxParseState *state,
	      xmlChar const * const *attrs, char const *name)
{
	g_return_if_fail (attrs != NULL);

	if (state->version == GNUM_XML_LATEST)
		gnm_io_warning (state->context,
			_("Unexpected attribute %s::%s == '%s'."),
			name, attrs[0], attrs[1]);
}

static void
xml_sax_wb (GsfXMLIn *gsf_state, xmlChar const **attrs)
{
	XMLSaxParseState *state = (XMLSaxParseState *)gsf_state;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (strcmp (attrs[0], "xmlns:gmr") == 0) {
			static struct {
				char const * const id;
				GnumericXMLVersion const version;
			} const GnumericVersions [] = {
				{ "http://www.gnumeric.org/v10.dtd", GNUM_XML_V10 },	/* 1.0.3 */
				{ "http://www.gnumeric.org/v9.dtd", GNUM_XML_V9 },	/* 0.73 */
				{ "http://www.gnumeric.org/v8.dtd", GNUM_XML_V8 },	/* 0.71 */
				{ "http://www.gnome.org/gnumeric/v7", GNUM_XML_V7 },	/* 0.66 */
				{ "http://www.gnome.org/gnumeric/v6", GNUM_XML_V6 },	/* 0.62 */
				{ "http://www.gnome.org/gnumeric/v5", GNUM_XML_V5 },
				{ "http://www.gnome.org/gnumeric/v4", GNUM_XML_V4 },
				{ "http://www.gnome.org/gnumeric/v3", GNUM_XML_V3 },
				{ "http://www.gnome.org/gnumeric/v2", GNUM_XML_V2 },
				{ "http://www.gnome.org/gnumeric/", GNUM_XML_V1 },
				{ NULL }
			};
			int i;
			for (i = 0 ; GnumericVersions [i].id != NULL ; ++i )
				if (strcmp (attrs[1], GnumericVersions [i].id) == 0) {
					if (state->version != GNUM_XML_UNKNOWN)
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
			unknown_attr (state, attrs, "Workbook");
}

static void
xml_sax_wb_sheetname (GsfXMLIn *gsf_state, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	XMLSaxParseState *state = (XMLSaxParseState *)gsf_state;

	char const *content = state->base.content->str;
	Sheet *sheet = sheet_new (state->wb, content);
	workbook_sheet_attach (state->wb, sheet, NULL);
}

static void
xml_sax_wb_view (GsfXMLIn *gsf_state, xmlChar const **attrs)
{
	XMLSaxParseState *state = (XMLSaxParseState *)gsf_state;

	int sheet_index;
	int width = -1, height = -1;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (xml_sax_attr_int (attrs, "SelectedTab", &sheet_index))
			wb_view_sheet_focus (state->wb_view,
				workbook_sheet_by_index (state->wb, sheet_index));
		else if (xml_sax_attr_int (attrs, "Width", &width)) ;
		else if (xml_sax_attr_int (attrs, "Height", &height)) ;
		else
			unknown_attr (state, attrs, "WorkbookView");

	if (width > 0 && height > 0)
		wb_view_preferred_size (state->wb_view, width, height);
}

static void
xml_sax_finish_parse_wb_attr (GsfXMLIn *gsf_state, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	XMLSaxParseState *state = (XMLSaxParseState *)gsf_state;

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
	XMLSaxParseState *state = (XMLSaxParseState *)gsf_state;

	char const *content = state->base.content->str;
	int const len = state->base.content->len;

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
	XMLSaxParseState *state = (XMLSaxParseState *)gsf_state;

	gboolean tmp;
	StyleColor *color = NULL;

	state->hide_col_header = state->hide_row_header =
	state->display_formulas = state->hide_zero =
	state->hide_grid = state->display_outlines =
	state->outline_symbols_below = state->outline_symbols_right = -1;
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
		else if (xml_sax_attr_color (attrs, "TabColor", &color))
			state->tab_color = color;
		else
			unknown_attr (state, attrs, "Sheet");
}

static void
xml_sax_sheet_end (GsfXMLIn *gsf_state, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	XMLSaxParseState *state = (XMLSaxParseState *)gsf_state;

	g_return_if_fail (state->sheet != NULL);

	/* Init ColRowInfo's size_pixels and force a full respan */
	sheet_flag_recompute_spans (state->sheet);
	sheet_set_zoom_factor (state->sheet, state->sheet_zoom,
			       FALSE, FALSE);
	state->sheet = NULL;
}

static void
xml_sax_sheet_name (GsfXMLIn *gsf_state, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	XMLSaxParseState *state = (XMLSaxParseState *)gsf_state;

	char const * content = state->base.content->str;
	g_return_if_fail (state->sheet == NULL);

	/* * FIXME: Pull this out at some point, so we don't
	 * have to support < GNUM_XML_V7 anymore
	 */
	if (state->version >= GNUM_XML_V7) {
		state->sheet = workbook_sheet_by_name (state->wb, content);

		if (!state->sheet)
			gnumeric_io_error_string (state->context,
				_("File has inconsistent SheetNameIndex element."));
	} else {
		state->sheet = sheet_new (state->wb, content);
		workbook_sheet_attach (state->wb, state->sheet, NULL);
	}

	if (state->display_formulas >= 0)
		state->sheet->display_formulas = state->display_formulas;
	if (state->hide_zero >= 0)
		state->sheet->hide_zero = state->hide_zero;
	if (state->hide_grid >= 0)
		state->sheet->hide_grid = state->hide_grid;
	if (state->hide_col_header >= 0)
		state->sheet->hide_col_header = state->hide_col_header;
	if (state->hide_row_header >= 0)
		state->sheet->hide_row_header = state->hide_row_header;
	if (state->display_outlines >= 0)
		state->sheet->display_outlines = state->display_outlines;
	if (state->outline_symbols_below >= 0)
		state->sheet->outline_symbols_below = state->outline_symbols_below;
	if (state->outline_symbols_right >= 0)
		state->sheet->outline_symbols_right = state->outline_symbols_right;
	state->sheet->tab_color = state->tab_color;
}

static void
xml_sax_sheet_zoom (GsfXMLIn *gsf_state, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	XMLSaxParseState *state = (XMLSaxParseState *)gsf_state;

	char const * content = state->base.content->str;
	double zoom;

	g_return_if_fail (state->sheet != NULL);

	if (xml_sax_double ((xmlChar *)content, &zoom))
		state->sheet_zoom = zoom;
}

static double
xml_sax_print_margins_get_double (XMLSaxParseState *state, xmlChar const **attrs)
{
	double points;
	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2) {
		if (xml_sax_attr_double (attrs, "Points", &points))
			return points;
		else if (strcmp (attrs[0], "PrefUnit"))
			unknown_attr (state, attrs, "Margin");
	}
	return 0.0;
}

static void
xml_sax_print_margins_unit (XMLSaxParseState *state, xmlChar const **attrs, PrintUnit *pu)
{
	double points;
	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2) {
		if (xml_sax_attr_double (attrs, "Points", &points))
			pu->points = points;
		else if (!strcmp (attrs[0], "PrefUnit")) {
			pu->desired_display = unit_name_to_unit (attrs[1]);
		} else
			unknown_attr (state, attrs, "Margin");
	}
}

static void
xml_sax_print_margins (GsfXMLIn *gsf_state, xmlChar const **attrs)
{
	XMLSaxParseState *state = (XMLSaxParseState *)gsf_state;

	PrintInformation *pi;

	g_return_if_fail (state->sheet != NULL);
	g_return_if_fail (state->sheet->print_info != NULL);

	pi = state->sheet->print_info;
	switch (gsf_state->node->user_data.v_int) {
	case 0: xml_sax_print_margins_unit (state, attrs,
			&pi->margins.top);
		break;
	case 1: xml_sax_print_margins_unit (state, attrs,
			&pi->margins.bottom);
		break;
	case 2: print_info_set_margin_left (pi,
			xml_sax_print_margins_get_double (state, attrs));
		break;
	case 3: print_info_set_margin_right (pi,
			xml_sax_print_margins_get_double (state, attrs));
		break;
	case 4: print_info_set_margin_header (pi,
			xml_sax_print_margins_get_double (state, attrs));
		break;
	case 5: print_info_set_margin_footer (pi,
			xml_sax_print_margins_get_double (state, attrs));
		break;
	default:
		return;
	}
}




static void
xml_sax_print_scale (GsfXMLIn *gsf_state, xmlChar const **attrs)
{
	XMLSaxParseState *state = (XMLSaxParseState *)gsf_state;

	PrintInformation *pi;
	double percentage;
	int cols, rows;

	g_return_if_fail (state->sheet != NULL);
	g_return_if_fail (state->sheet->print_info != NULL);

	pi = state->sheet->print_info;
	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2) {
		if (!strcmp (attrs[0], "type"))
			pi->scaling.type = strcmp (attrs[1], "percentage")
				? SIZE_FIT : PERCENTAGE;
		else if (xml_sax_attr_double (attrs, "percentage", &percentage))
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
	XMLSaxParseState *state = (XMLSaxParseState *)gsf_state;

	Range r;
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
	XMLSaxParseState *state = (XMLSaxParseState *)gsf_state;

	int col = -1, row = -1;

	sv_selection_reset (sheet_get_view (state->sheet, state->wb_view));

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (xml_sax_attr_int (attrs, "CursorCol", &col)) ;
		else if (xml_sax_attr_int (attrs, "CursorRow", &row)) ;
		else
			unknown_attr (state, attrs, "Selection");

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
	XMLSaxParseState *state = (XMLSaxParseState *)gsf_state;

	CellPos const pos = state->cell;
	state->cell.col = state->cell.row = -1;
	sv_set_edit_pos (sheet_get_view (state->sheet, state->wb_view), &pos);
}

static void
xml_sax_sheet_layout (GsfXMLIn *gsf_state, xmlChar const **attrs)
{
	XMLSaxParseState *state = (XMLSaxParseState *)gsf_state;

	CellPos tmp;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (xml_sax_attr_cellpos (attrs, "TopLeft", &tmp))
			sv_set_initial_top_left (
				sheet_get_view (state->sheet, state->wb_view),
				tmp.col, tmp.row);
		else
			unknown_attr (state, attrs, "SheetLayout");
}

static void
xml_sax_sheet_freezepanes (GsfXMLIn *gsf_state, xmlChar const **attrs)
{
	XMLSaxParseState *state = (XMLSaxParseState *)gsf_state;

	CellPos frozen_tl, unfrozen_tl;
	int flags = 0;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (xml_sax_attr_cellpos (attrs, "FrozenTopLeft", &frozen_tl))
			flags |= 1;
		else if (xml_sax_attr_cellpos (attrs, "UnfrozenTopLeft", &unfrozen_tl))
			flags |= 2;
		else
			unknown_attr (state, attrs, "SheetLayout");

	if (flags == 3)
		sv_freeze_panes (sheet_get_view (state->sheet, state->wb_view),
			&frozen_tl, &unfrozen_tl);
}

static void
xml_sax_cols_rows (GsfXMLIn *gsf_state, xmlChar const **attrs)
{
	XMLSaxParseState *state = (XMLSaxParseState *)gsf_state;

	double def_size;
	gboolean const is_col = gsf_state->node->user_data.v_bool;

	g_return_if_fail (state->sheet != NULL);

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (xml_sax_attr_double (attrs, "DefaultSizePts", &def_size)) {
			if (is_col)
				sheet_col_set_default_size_pts (state->sheet, def_size);
			else
				sheet_row_set_default_size_pts (state->sheet, def_size);
		}
}

static void
xml_sax_colrow (GsfXMLIn *gsf_state, xmlChar const **attrs)
{
	XMLSaxParseState *state = (XMLSaxParseState *)gsf_state;

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

			if (xml_sax_attr_double (attrs, "Unit", &size)) ;
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
				unknown_attr (state, attrs, "ColRow");
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
	XMLSaxParseState *state = (XMLSaxParseState *)gsf_state;

	g_return_if_fail (state->style_range_init == FALSE);
	g_return_if_fail (state->style == NULL);

	state->style = (state->version >= GNUM_XML_V6 ||
			state->version <= GNUM_XML_V2)
		? mstyle_new_default ()
		: mstyle_new ();
	state->style_range_init =
		xml_sax_attr_range (attrs, &state->style_range);
}

static void
xml_sax_style_region_end (GsfXMLIn *gsf_state, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	XMLSaxParseState *state = (XMLSaxParseState *)gsf_state;

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
	XMLSaxParseState *state = (XMLSaxParseState *)gsf_state;

	int val;
	StyleColor *colour;

	g_return_if_fail (state->style != NULL);

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2) {
		if (xml_sax_attr_int (attrs, "HAlign", &val))
			mstyle_set_align_h (state->style, val);
		else if (xml_sax_attr_int (attrs, "VAlign", &val))
			mstyle_set_align_v (state->style, val);

		/* Pre version V6 */
		else if (xml_sax_attr_int (attrs, "Fit", &val))
			mstyle_set_wrap_text (state->style, val);

		else if (xml_sax_attr_int (attrs, "WrapText", &val))
			mstyle_set_wrap_text (state->style, val);
		else if (xml_sax_attr_int (attrs, "ShrinkToFit", &val))
			mstyle_set_shrink_to_fit (state->style, val);
		else if (xml_sax_attr_int (attrs, "Rotation", &val))
			mstyle_set_rotation (state->style, val);
		else if (xml_sax_attr_int (attrs, "Shade", &val))
			mstyle_set_pattern (state->style, val);
		else if (xml_sax_attr_int (attrs, "Indent", &val))
			mstyle_set_indent (state->style, val);
		else if (xml_sax_attr_color (attrs, "Fore", &colour))
			mstyle_set_color (state->style, MSTYLE_COLOR_FORE, colour);
		else if (xml_sax_attr_color (attrs, "Back", &colour))
			mstyle_set_color (state->style, MSTYLE_COLOR_BACK, colour);
		else if (xml_sax_attr_color (attrs, "PatternColor", &colour))
			mstyle_set_color (state->style, MSTYLE_COLOR_PATTERN, colour);
		else if (!strcmp (attrs[0], "Format"))
			mstyle_set_format_text (state->style, (char *)attrs[1]);
		else if (xml_sax_attr_int (attrs, "Hidden", &val))
			mstyle_set_content_hidden (state->style, val);
		else if (xml_sax_attr_int (attrs, "Locked", &val))
			mstyle_set_content_locked (state->style, val);
		else if (xml_sax_attr_int (attrs, "Locked", &val))
			mstyle_set_content_locked (state->style, val);
		else if (xml_sax_attr_int (attrs, "Orient", &val))
			; /* ignore old useless attribute */
		else
			unknown_attr (state, attrs, "StyleRegion");
	}
}

static void
xml_sax_styleregion_font (GsfXMLIn *gsf_state, xmlChar const **attrs)
{
	XMLSaxParseState *state = (XMLSaxParseState *)gsf_state;

	double size_pts = 10.;
	int val;

	g_return_if_fail (state->style != NULL);

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2) {
		if (xml_sax_attr_double (attrs, "Unit", &size_pts))
			mstyle_set_font_size (state->style, size_pts);
		else if (xml_sax_attr_int (attrs, "Bold", &val))
			mstyle_set_font_bold (state->style, val);
		else if (xml_sax_attr_int (attrs, "Italic", &val))
			mstyle_set_font_italic (state->style, val);
		else if (xml_sax_attr_int (attrs, "Underline", &val))
			mstyle_set_font_uline (state->style, (StyleUnderlineType)val);
		else if (xml_sax_attr_int (attrs, "StrikeThrough", &val))
			mstyle_set_font_strike (state->style, val ? TRUE : FALSE);
		else
			unknown_attr (state, attrs, "StyleFont");
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
style_font_read_from_x11 (MStyle *mstyle, const char *fontname)
{
	char const *c;

	/* FIXME: we should do something about the typeface instead
	 * of hardcoding it to helvetica.
	 */
	c = font_component (fontname, 2);
	if (strncmp (c, "bold", 4) == 0)
		mstyle_set_font_bold (mstyle, TRUE);

	c = font_component (fontname, 3);
	if (strncmp (c, "o", 1) == 0)
		mstyle_set_font_italic (mstyle, TRUE);

	if (strncmp (c, "i", 1) == 0)
		mstyle_set_font_italic (mstyle, TRUE);
}

static void
xml_sax_styleregion_font_end (GsfXMLIn *gsf_state, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	XMLSaxParseState *state = (XMLSaxParseState *)gsf_state;

	if (state->base.content->len > 0) {
		char const * content = state->base.content->str;
		if (*content == '-')
			style_font_read_from_x11 (state->style, content);
		else
			mstyle_set_font_name (state->style, content);
	}
}

static void
xml_sax_validation (GsfXMLIn *gsf_state, xmlChar const **attrs)
{
	XMLSaxParseState *state = (XMLSaxParseState *)gsf_state;

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
			unknown_attr (state, attrs, "Validation");
	}
}

static void
xml_sax_validation_end (GsfXMLIn *gsf_state, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	XMLSaxParseState *state = (XMLSaxParseState *)gsf_state;

	g_return_if_fail (state->style != NULL);

	mstyle_set_validation (state->style,
		validation_new (state->validation.style,
				state->validation.type,
				state->validation.op,
				state->validation.title,
				state->validation.msg,
				state->validation.expr[0],
				state->validation.expr[1],
				state->validation.allow_blank,
				state->validation.use_dropdown));

	if (state->validation.title != NULL) {
		g_free (state->validation.title);
		state->validation.title = NULL;
	}
	if (state->validation.msg != NULL) {
		g_free (state->validation.msg);
		state->validation.msg = NULL;
	}
	state->validation.expr[0] = state->validation.expr[1] = NULL;
}

static void
xml_sax_validation_expr_end (GsfXMLIn *gsf_state, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	XMLSaxParseState *state = (XMLSaxParseState *)gsf_state;

	int const i = gsf_state->node->user_data.v_int;
	GnmExpr const *expr;
	ParsePos pos;

	g_return_if_fail (state->validation.expr [i] == NULL);

	expr = gnm_expr_parse_str_simple (state->base.content->str,
		parse_pos_init_sheet (&pos, state->sheet));

	g_return_if_fail (expr != NULL);

	state->validation.expr [i] = expr;
}

static void
xml_sax_style_region_borders (GsfXMLIn *gsf_state, xmlChar const **attrs)
{
	XMLSaxParseState *state = (XMLSaxParseState *)gsf_state;

	int pattern = -1;
	StyleColor *colour = NULL;

	g_return_if_fail (state->style != NULL);

	/* Colour is optional */
	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2) {
		if (xml_sax_attr_color (attrs, "Color", &colour)) ;
		else if (xml_sax_attr_int (attrs, "Style", &pattern)) ;
		else
			unknown_attr (state, attrs, "StyleBorder");
	}

	if (pattern >= STYLE_BORDER_NONE) {
		MStyleElementType const type = gsf_state->node->user_data.v_int;
		StyleBorder *border =
			style_border_fetch ((StyleBorderType)pattern, colour,
					    style_border_get_orientation (type));
		mstyle_set_border (state->style, type, border);
	}
}

static void
xml_sax_cell (GsfXMLIn *gsf_state, xmlChar const **attrs)
{
	XMLSaxParseState *state = (XMLSaxParseState *)gsf_state;

	int row = -1, col = -1;
	int rows = -1, cols = -1;
	int value_type = -1;
	StyleFormat *value_fmt = NULL;
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
			value_fmt = style_format_new_XL ((char *)attrs[1], FALSE);
		else
			unknown_attr (state, attrs, "Cell");
	}

	g_return_if_fail (col >= 0);
	g_return_if_fail (row >= 0);

	if (cols > 0 || rows > 0) {
		/* Both must be valid */
		g_return_if_fail (cols <= 0);
		g_return_if_fail (rows <= 0);

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
xml_cell_set_array_expr (Cell *cell, char const *text,
			 int const cols, int const rows)
{
	ParsePos pp;
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
xml_not_used_old_array_spec (Cell *cell, char const *content)
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
	XMLSaxParseState *state = (XMLSaxParseState *)gsf_state;

	gboolean is_new_cell, is_post_52_array = FALSE;
	Cell *cell;

	int const col = state->cell.col;
	int const row = state->cell.row;
	int const array_cols = state->array_cols;
	int const array_rows = state->array_rows;
	int const expr_id = state->expr_id;
	int const value_type = state->value_type;
	StyleFormat *value_fmt = state->value_fmt;
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

	if (state->base.content->len > 0) {
		char const * content = state->base.content->str;

		if (is_post_52_array) {
			g_return_if_fail (content[0] == '=');

			xml_cell_set_array_expr (cell, content+1,
						 array_cols, array_rows);
		} else if (state->version >= GNUM_XML_V3 ||
			   xml_not_used_old_array_spec (cell, content)) {
			if (value_type > 0) {
				Value *v = value_new_from_string (value_type, content, value_fmt, FALSE);
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
		style_format_unref (value_fmt);
}

static void
xml_sax_merge (GsfXMLIn *gsf_state, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	XMLSaxParseState *state = (XMLSaxParseState *)gsf_state;

	Range r;
	g_return_if_fail (state->base.content->len > 0);

	if (parse_range (state->base.content->str, &r))
		sheet_merge_add (state->sheet, &r, FALSE,
			COMMAND_CONTEXT (state->context));
}

static void
xml_sax_object (GsfXMLIn *gsf_state, xmlChar const **attrs)
{
}

static void
xml_sax_named_expr_end (GsfXMLIn *gsf_state, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	XMLSaxParseState *state = (XMLSaxParseState *)gsf_state;

	ParseError  perr;
	ParsePos    pos;
	GnmExpr const	*expr;

	g_return_if_fail (state->name.name != NULL);
	g_return_if_fail (state->name.value != NULL);

	parse_pos_init_sheet (&pos, state->sheet);
	if (state->name.position) {
		CellRef tmp;
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
		expr_name_add (&pos, state->name.name, expr, &err, TRUE);
		if (err != NULL) {
			gnm_io_warning (state->context, err);
			g_free (err);
		}
	} else
		state->delayed_names = g_list_prepend (state->delayed_names,
			expr_name_add (&pos, state->name.name, 
				gnm_expr_new_constant (value_new_string (state->name.value)), NULL, TRUE));

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
	XMLSaxParseState *state = (XMLSaxParseState *)gsf_state;

	char const * content = state->base.content->str;
	int const len = state->base.content->len;

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
	GSF_XML_IN_NODE (SHEET_PRINTINFO, PRINT_PAPER,	    GNM, "paper",	TRUE,  NULL, NULL),
	GSF_XML_IN_NODE (SHEET_PRINTINFO, PRINT_ORIENT,	    GNM, "orientation",	TRUE,  NULL, NULL),
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
      GSF_XML_IN_NODE (SHEET, SHEET_OBJECTS, GNM, "Objects", FALSE, NULL, NULL),
	GSF_XML_IN_NODE (SHEET_OBJECTS, OBJECT_POINTS, GNM, "Points", FALSE, NULL, NULL),
	GSF_XML_IN_NODE (SHEET_OBJECTS, OBJECT_RECTANGLE, GNM, "Rectangle", FALSE, &xml_sax_object, NULL),
	GSF_XML_IN_NODE (SHEET_OBJECTS, OBJECT_ELLIPSE, GNM, "Ellipse", FALSE, &xml_sax_object, NULL),
	GSF_XML_IN_NODE (SHEET_OBJECTS, OBJECT_ARROW, GNM, "Arrow", FALSE, &xml_sax_object, NULL),
	GSF_XML_IN_NODE (SHEET_OBJECTS, OBJECT_LINE, GNM, "Line", FALSE, &xml_sax_object, NULL),

  GSF_XML_IN_NODE (WB, WB_GEOMETRY, GNM, "Geometry", FALSE, &xml_sax_wb_view, NULL),
  GSF_XML_IN_NODE (WB, WB_VIEW, GNM, "UIData", FALSE, &xml_sax_wb_view, NULL),
  { NULL }
};
static GsfXMLInDoc *doc;

void
xml_sax_file_open (GnmFileOpener const *fo, IOContext *io_context,
		   WorkbookView *wb_view, GsfInput *input)
{
	XMLSaxParseState state;

	g_return_if_fail (IS_WORKBOOK_VIEW (wb_view));
	g_return_if_fail (GSF_IS_INPUT (input));

	/* init */
	state.base.doc = doc;

	state.context = io_context;
	state.wb_view = wb_view;
	state.wb = wb_view_workbook (wb_view);
	state.sheet = NULL;
	state.version = GNUM_XML_UNKNOWN;
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

	if (!gsf_xml_in_parse (&state.base, input))
		gnumeric_io_error_string (io_context, _("XML document not well formed!"));
	else
		workbook_queue_all_recalc (state.wb);

	/* cleanup */
	g_hash_table_destroy (state.expr_map);
}

void
plugin_init (void)
{
	doc = gsf_xml_in_doc_new (gnumeric_1_0_dtd, content_ns);
}
void
plugin_cleanup (void)
{
	gsf_xml_in_doc_free (doc);
}
