/* vim: set sw=8: */

/*
 * xml-sax-read.c : a test harness for the sax based xml parse routines.
 *
 * Copyright (C) 2000 Jody Goldberg (jgoldberg@home.com)
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
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
 * USA
 */

#include "config.h"
#include <gnome.h>
#include "gnumeric.h"
#include "io-context.h"
#include "plugin.h"
#include "plugin-util.h"
#include "module-plugin-defs.h"
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
#include "value.h"
#include "selection.h"
#include "command-context.h"
#include "workbook-view.h"
#include "workbook.h"
#include "error-info.h"

#include <stdlib.h>
#include <string.h>
#include <gnome-xml/tree.h>
#include <gnome-xml/parser.h>
#include <gnome-xml/parserInternals.h>

GNUMERIC_MODULE_PLUGIN_INFO_DECL;

gboolean xml_sax_file_probe (GnumFileOpener const *fo, const gchar *file_name,
                             FileProbeLevel pl);
void     xml_sax_file_open (GnumFileOpener const *fo, IOContext *io_context,
			    WorkbookView *wb_view, char const *filename);

/*****************************************************************************/

static int
xml_sax_attr_double (CHAR const * const *attrs, char const *name, double * res)
{
	char *end;
	double tmp;

	g_return_val_if_fail (attrs != NULL, FALSE);
	g_return_val_if_fail (attrs[0] != NULL, FALSE);
	g_return_val_if_fail (attrs[1] != NULL, FALSE);

	if (strcmp (attrs[0], name))
		return FALSE;

	tmp = g_strtod (attrs[1], &end);
	if (*end) {
		g_warning ("Invalid attribute '%s', expected double, received '%s'",
			   name, attrs[1]);
		return FALSE;
	}
	*res = tmp;
	return TRUE;
}
static gboolean
xml_sax_double (CHAR const *chars, double *res)
{
	char *end;
	*res = g_strtod (chars, &end);
	return *end == '\0';
}

static int
xml_sax_attr_bool (CHAR const * const *attrs, char const *name, int *res)
{
	g_return_val_if_fail (attrs != NULL, FALSE);
	g_return_val_if_fail (attrs[0] != NULL, FALSE);
	g_return_val_if_fail (attrs[1] != NULL, FALSE);

	if (strcmp (attrs[0], name))
		return FALSE;

	*res = g_strcasecmp (attrs[1], "false") && strcmp (attrs[1], "0");

	return TRUE;
}

static int
xml_sax_attr_int (CHAR const * const *attrs, char const *name, int *res)
{
	char *end;
	int tmp;

	g_return_val_if_fail (attrs != NULL, FALSE);
	g_return_val_if_fail (attrs[0] != NULL, FALSE);
	g_return_val_if_fail (attrs[1] != NULL, FALSE);

	if (strcmp (attrs[0], name))
		return FALSE;

	tmp = strtol (attrs[1], &end, 10);
	if (*end) {
		g_warning ("Invalid attribute '%s', expected integer, received '%s'",
			   name, attrs[1]);
		return FALSE;
	}
	*res = tmp;
	return TRUE;
}

static gboolean
xml_sax_int (CHAR const *chars, int *res)
{
	char *end;
	*res = strtol (chars, &end, 10);
	return *end == '\0';
}

static int
xml_sax_color (CHAR const * const *attrs, char const *name, StyleColor **res)
{
	int red, green, blue;

	g_return_val_if_fail (attrs != NULL, FALSE);
	g_return_val_if_fail (attrs[0] != NULL, FALSE);
	g_return_val_if_fail (attrs[1] != NULL, FALSE);

	if (strcmp (attrs[0], name))
		return FALSE;

	if (sscanf (attrs[1], "%X:%X:%X", &red, &green, &blue) != 3){
		g_warning ("Invalid attribute '%s', expected colour, received '%s'",
			   name, attrs[1]);
		return FALSE;
	}
	*res = style_color_new (red, green, blue);
	return TRUE;
}

static gboolean
xml_sax_range (CHAR const * const *attrs, Range *res)
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

#if 0
/*
 * Save a Workbook in an XML file
 */
void
xmlSax_write (Workbook *wb, char const *filename, ErrorInfo **ret_error)
{
	g_return_val_if_fail (wb != NULL, -1);
	g_return_val_if_fail (filename != NULL, -1);

	*ret_error = NULL;
}
#endif

/*****************************************************************************/

typedef enum {
STATE_START,

STATE_WB,
	STATE_WB_ATTRIBUTES,
		STATE_WB_ATTRIBUTES_ELEM,
			STATE_WB_ATTRIBUTES_ELEM_NAME,
			STATE_WB_ATTRIBUTES_ELEM_TYPE,
			STATE_WB_ATTRIBUTES_ELEM_VALUE,
	STATE_WB_SUMMARY,
		STATE_WB_SUMMARY_ITEM,
			STATE_WB_SUMMARY_ITEM_NAME,
			STATE_WB_SUMMARY_ITEM_VALUE_STR,
			STATE_WB_SUMMARY_ITEM_VALUE_INT,
	STATE_WB_SHEETNAME_INDEX,
		STATE_WB_SHEETNAME,
        STATE_NAMES,
                STATE_NAMES_NAME,
                        STATE_NAMES_NAME_NAME,
                        STATE_NAMES_NAME_VALUE,
	STATE_WB_GEOMETRY,
	STATE_WB_SHEETS,
		STATE_SHEET,
			STATE_SHEET_NAME,	/* convert to attr */
			STATE_SHEET_MAXCOL,	/* convert to attr */
			STATE_SHEET_MAXROW,	/* convert to attr */
			STATE_SHEET_ZOOM,	/* convert to attr */
			STATE_SHEET_NAMES,
				STATE_SHEET_NAMES_NAME,
					STATE_SHEET_NAMES_NAME_NAME,
					STATE_SHEET_NAMES_NAME_VALUE,
			STATE_SHEET_PRINTINFO,
                                STATE_PRINT_MARGINS,
					STATE_PRINT_MARGIN_TOP,
					STATE_PRINT_MARGIN_BOTTOM,
					STATE_PRINT_MARGIN_LEFT,
					STATE_PRINT_MARGIN_RIGHT,
					STATE_PRINT_MARGIN_HEADER,
					STATE_PRINT_MARGIN_FOOTER,
				STATE_PRINT_VCENTER,
				STATE_PRINT_HCENTER,
				STATE_PRINT_GRID,
				STATE_PRINT_MONO,
				STATE_PRINT_DRAFTS,
				STATE_PRINT_TITLES,
				STATE_PRINT_REPEAT_TOP,
				STATE_PRINT_REPEAT_LEFT,
				STATE_PRINT_ORDER,
				STATE_PRINT_ORIENT,
				STATE_PRINT_HEADER,
				STATE_PRINT_FOOTER,
				STATE_PRINT_PAPER,
				STATE_PRINT_EVEN_ONLY_STYLE,
			STATE_SHEET_STYLES,
				STATE_STYLE_REGION,
					STATE_STYLE_STYLE,
						STATE_STYLE_FONT,
						STATE_STYLE_BORDER,
							STATE_BORDER_TOP,
							STATE_BORDER_BOTTOM,
							STATE_BORDER_LEFT,
							STATE_BORDER_RIGHT,
							STATE_BORDER_DIAG,
							STATE_BORDER_REV_DIAG,
			STATE_SHEET_COLS,
				STATE_COL,
			STATE_SHEET_ROWS,
				STATE_ROW,
			STATE_SHEET_SELECTIONS,
				STATE_SELECTION,
			STATE_SHEET_CELLS,
				STATE_CELL,
					STATE_CELL_CONTENT,
			STATE_SHEET_MERGED_REGION,
				STATE_SHEET_MERGE,
			STATE_SHEET_SOLVER,
		STATE_SHEET_OBJECTS,
			STATE_OBJECT_POINTS,
			STATE_OBJECT_RECTANGLE,
			STATE_OBJECT_ELLIPSE,
			STATE_OBJECT_ARROW,
			STATE_OBJECT_LINE,

	STATE_WB_VIEW,

	STATE_UNKNOWN
} xmlSaxState;

/*
 * This is not complete yet.
 * I just pulled it out of a single saved gnumeric file.
 * However, with a bit of cleanup, we should be able to use it as the basis for a dtd.
 */
static char const * const xmlSax_state_names[] =
{
"START",
"gmr:Workbook",
	"gmr:Attributes",
		"gmr:Attribute",
			"gmr:name",
			"gmr:type",
			"gmr:value",
	"gmr:Summary",
		"gmr:Item",
			"gmr:name",
			"gmr:val-string",
			"gmr:val-int",
	"gmr:SheetNameIndex",
		"gmr:SheetName",
        "gmr:Names",
                "gmr:Name",
                        "gmr:name",
                        "gmr:value",
	"gmr:Geometry",
	"gmr:Sheets",
		"gmr:Sheet",
			"gmr:Name",
			"gmr:MaxCol",
			"gmr:MaxRow",
			"gmr:Zoom",
			"gmr:Names",
				"gmr:Name",
					"gmr:name",
					"gmr:value",
			"gmr:PrintInformation",
				"gmr:Margins",
					"gmr:top",
					"gmr:bottom",
					"gmr:left",
					"gmr:right",
					"gmr:header",
					"gmr:footer",
				"gmr:vcenter",
				"gmr:hcenter",
				"gmr:grid",
				"gmr:monochrome",
				"gmr:draft",
				"gmr:titles",
				"gmr:repeat_top",
				"gmr:repeat_left",
				"gmr:order",
				"gmr:orientation",
				"gmr:Footer",
				"gmr:Header",
				"gmr:paper",
				"gmr:even_if_only_styles",
			"gmr:Styles",
				"gmr:StyleRegion",
					"gmr:Style",
						"gmr:Font",
						"gmr:StyleBorder",
							"gmr:Top",
							"gmr:Bottom",
							"gmr:Left",
							"gmr:Right",
							"gmr:Diagonal",
							"gmr:Rev-Diagonal",
			"gmr:Cols",
				"gmr:ColInfo",

			"gmr:Rows",
				"gmr:RowInfo",
			"gmr:Selections",
				"gmr:Selection",
			"gmr:Cells",
				"gmr:Cell",
					"gmr:Content",
			"gmr:MergedRegions",
				"gmr:Merge",
			"gmr:Solver",
			"gmr:Objects",
				"gmr:Points",
				"gmr:Rectangle",
				"gmr:Ellipse",
				"gmr:Arrow",
				"gmr:Line",
	"gmr:UIData",

	"Unknown",
NULL
};

typedef enum
{
    GNUM_XML_UNKNOWN = 0,
    GNUM_XML_V1,
    GNUM_XML_V2,
    GNUM_XML_V3,	/* >= 0.52 */
    GNUM_XML_V4,	/* >= 0.57 */
    GNUM_XML_V5,	/* >= 0.58 */
    GNUM_XML_V6,	/* >= 0.62 */
    GNUM_XML_V7,        /* >= 0.66 */

    /* NOTE : Keep this up to date */
    GNUM_XML_LATEST = GNUM_XML_V7
} GnumericXMLVersion;
typedef struct _XMLSaxParseState
{
	xmlSaxState state;
	gint	  unknown_depth;	/* handle recursive unknown tags */
	GSList	 *state_stack;

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
		int   type;
	} attribute;
	GList *attributes;

	/* Only valid when parsing wb or sheet names */
	struct {
		char *name;
		char *value;
	} name;
	
	gboolean  style_range_init;
	Range	  style_range;
	MStyle   *style;

	CellPos cell;
	int expr_id, array_rows, array_cols;
	int value_type;
	char const *value_fmt;

	GString *content;

	int display_formulas;
	int hide_zero;
	int hide_grid;
	int hide_col_header;
	int hide_row_header;
	int display_outlines;
	int outline_symbols_below;
	int outline_symbols_right;

	/* expressions with ref > 1 a map from index -> expr pointer */
	GHashTable *expr_map;
} XMLSaxParseState;

static void
xml_sax_unknown_attr (XMLSaxParseState *state, CHAR const * const *attrs, char const *name)
{
	g_return_if_fail (attrs != NULL);

	/* FIXME : Use IOContext to get these messages back to the user. */
	if (state->version == GNUM_XML_LATEST)
		g_warning ("Unexpected attribute '%s'='%s' for element of type %s.",
			   name, attrs[0], attrs[1]);
}

static void
xml_sax_warning (XMLSaxParseState *state, const char *msg, ...)
{
	va_list args;

	va_start (args, msg);
	g_logv ("XML", G_LOG_LEVEL_WARNING, msg, args);
	va_end (args);
}

static void
xml_sax_error (XMLSaxParseState *state, const char *msg, ...)
{
	va_list args;

	va_start (args, msg);
	g_logv ("XML", G_LOG_LEVEL_CRITICAL, msg, args);
	va_end (args);
}

static void
xml_sax_fatal_error (XMLSaxParseState *state, const char *msg, ...)
{
	va_list args;

	va_start (args, msg);
	g_logv ("XML", G_LOG_LEVEL_ERROR, msg, args);
	va_end (args);
}


/****************************************************************************/

static void
xml_sax_wb (XMLSaxParseState *state, CHAR const **attrs)
{
	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (strcmp (attrs[0], "xmlns:gmr") == 0) {
			static const struct {
				char const * const id;
				GnumericXMLVersion const version;
			} GnumericVersions [] = {
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
						xml_sax_warning (state, "Multiple version specifications.  Assuming %d",
								state->version);
					else {
						state->version = GnumericVersions [i].version;
						break;
					}
				}
		} else
			xml_sax_unknown_attr (state, attrs, "Workbook");
}

static void
xml_sax_wb_sheetname (XMLSaxParseState *state)
{
	char const * content = state->content->str;
	Sheet *sheet = sheet_new (state->wb, content);
	workbook_sheet_attach (state->wb, sheet, NULL);
}

static void
xml_sax_wb_view (XMLSaxParseState *state, CHAR const **attrs)
{
	int sheet_index;
	int width = -1, height = -1;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (xml_sax_attr_int (attrs, "SelectedTab", &sheet_index))
			wb_view_sheet_focus (state->wb_view,
				workbook_sheet_by_index (state->wb, sheet_index));
		else if (xml_sax_attr_int (attrs, "Width", &width)) ;
		else if (xml_sax_attr_int (attrs, "Height", &height)) ;
		else
			xml_sax_unknown_attr (state, attrs, "WorkbookView");

	if (width > 0 && height > 0)
		wb_view_preferred_size (state->wb_view, width, height);
}

static void
xml_sax_arg_set (GtkArg *arg, gchar *string)
{
	switch (arg->type) {
	case GTK_TYPE_CHAR:
		GTK_VALUE_CHAR (*arg) = string[0];
		break;
	case GTK_TYPE_UCHAR:
		GTK_VALUE_UCHAR (*arg) = string[0];
		break;
	case GTK_TYPE_BOOL:
		if (!strcmp (string, "TRUE"))
			GTK_VALUE_BOOL (*arg) = TRUE;
		else
			GTK_VALUE_BOOL (*arg) = FALSE;
		break;
	case GTK_TYPE_INT:
		GTK_VALUE_INT (*arg) = atoi (string);
		break;
	case GTK_TYPE_UINT:
		GTK_VALUE_UINT (*arg) = atoi (string);
		break;
	case GTK_TYPE_LONG:
		GTK_VALUE_LONG (*arg) = atol (string);
		break;
	case GTK_TYPE_ULONG:
		GTK_VALUE_ULONG (*arg) = atol (string);
		break;
	case GTK_TYPE_FLOAT:
		GTK_VALUE_FLOAT (*arg) = atof (string);
		break;
	case GTK_TYPE_DOUBLE:
		GTK_VALUE_DOUBLE (*arg) = atof (string);
		break;
	case GTK_TYPE_STRING:
		GTK_VALUE_STRING (*arg) = g_strdup (string);
		break;
	}
}

static void
xml_sax_finish_parse_wb_attr (XMLSaxParseState *state)
{
	GtkArg *arg;

	g_return_if_fail (state->attribute.name != NULL);
	g_return_if_fail (state->attribute.value != NULL);
	g_return_if_fail (state->attribute.type >= 0);

	arg = gtk_arg_new (state->attribute.type);
	arg->name = state->attribute.name;
	xml_sax_arg_set (arg, state->attribute.value);
	state->attributes = g_list_prepend (state->attributes, arg);

	state->attribute.type = -1;
	g_free (state->attribute.value);
	state->attribute.value = NULL;
	state->attribute.name = NULL;
}

static void
xml_sax_free_arg_list (GList *start)
{
	GList *list = start;
	while (list) {
		GtkArg *arg = list->data;
		if (arg) {
			g_free (arg->name);
			gtk_arg_free (arg, FALSE);
		}
		list = list->next;
	}
	g_list_free (start);
}

static void
xml_sax_attr_elem (XMLSaxParseState *state)
{
	char const * content = state->content->str;
	int const len = state->content->len;

	switch (state->state) {
	case STATE_WB_ATTRIBUTES_ELEM_NAME :
		g_return_if_fail (state->attribute.name == NULL);
		state->attribute.name = g_strndup (content, len);
		break;

	case STATE_WB_ATTRIBUTES_ELEM_VALUE :
		g_return_if_fail (state->attribute.value == NULL);
		state->attribute.value = g_strndup (content, len);
		break;

	case STATE_WB_ATTRIBUTES_ELEM_TYPE :
	{
		int type;
		if (xml_sax_int (content, &type))
			state->attribute.type = type;
		break;
	}

	default :
		g_assert_not_reached ();
	};
}

static void
xml_sax_sheet_start (XMLSaxParseState *state, CHAR const **attrs)
{
	int tmp;

	state->hide_col_header = state->hide_row_header =
	state->display_formulas = state->hide_zero =
	state->hide_grid = state->display_outlines =
	state->outline_symbols_below = state->outline_symbols_right = -1;
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
		else
			xml_sax_unknown_attr (state, attrs, "Sheet");
}

static void
xml_sax_sheet_end (XMLSaxParseState *state)
{
	g_return_if_fail (state->sheet != NULL);

	/* Init ColRowInfo's size_pixels and force a full respan */
	sheet_flag_recompute_spans (state->sheet);
	sheet_set_zoom_factor (state->sheet, state->sheet_zoom,
			       FALSE, FALSE);
	state->sheet = NULL;
}

static void
xml_sax_sheet_name (XMLSaxParseState *state)
{
	char const * content = state->content->str;
	g_return_if_fail (state->sheet == NULL);

	/*
	 * FIXME: Pull this out at some point, so we don't
	 * have to support < GNUM_XML_V7 anymore
	 */
	if (state->version >= GNUM_XML_V7) {
		state->sheet = workbook_sheet_by_name (state->wb, content);

		if (!state->sheet)
			xml_sax_fatal_error (state, "SheetNameIndex reading failed");
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
}

static void
xml_sax_sheet_zoom (XMLSaxParseState *state)
{
	char const * content = state->content->str;
	double zoom;

	g_return_if_fail (state->sheet != NULL);

	if (xml_sax_double (content, &zoom))
		state->sheet_zoom = zoom;
}

static void
xml_sax_print_margins (XMLSaxParseState *state, CHAR const **attrs)
{
	PrintInformation *pi;
	PrintUnit *pu;
	double points;

	g_return_if_fail (state->sheet != NULL);
	g_return_if_fail (state->sheet->print_info != NULL);

	pi = state->sheet->print_info;
	switch (state->state) {
	case STATE_PRINT_MARGIN_TOP:
		pu = &pi->margins.top;
		break;
	case STATE_PRINT_MARGIN_BOTTOM:
		pu = &pi->margins.bottom;
		break;
	case STATE_PRINT_MARGIN_LEFT:
		pu = &pi->margins.left;
		break;
	case STATE_PRINT_MARGIN_RIGHT:
		pu = &pi->margins.right;
		break;
	case STATE_PRINT_MARGIN_HEADER:
		pu = &pi->margins.header;
		break;
	case STATE_PRINT_MARGIN_FOOTER:
		pu = &pi->margins.footer;
		break;
	default:
		return;
	}

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2) {
		if (xml_sax_attr_double (attrs, "Points", &points))
			pu->points = points;
		else if (!strcmp (attrs[0], "PrefUnit")) {
			if (!strcmp (attrs[1], "points"))
				pu->desired_display = UNIT_POINTS;
			else if (!strcmp (attrs[1], "mm"))
				pu->desired_display = UNIT_MILLIMETER;
			else if (!strcmp (attrs[1], "cm"))
				pu->desired_display = UNIT_CENTIMETER;
			else if (!strcmp (attrs[1], "in"))
				pu->desired_display = UNIT_INCH;
		} else
			xml_sax_unknown_attr (state, attrs, "Margin");
	}
}

static void
xml_sax_selection_range (XMLSaxParseState *state, CHAR const **attrs)
{
	Range r;
	if (xml_sax_range (attrs, &r))
		sheet_selection_add_range (state->sheet,
					   r.start.col, r.start.row,
					   r.start.col, r.start.row,
					   r.end.col, r.end.row);
}

static void
xml_sax_selection (XMLSaxParseState *state, CHAR const **attrs)
{
	int col = -1, row = -1;

	sheet_selection_reset (state->sheet);

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (xml_sax_attr_int (attrs, "CursorCol", &col)) ;
		else if (xml_sax_attr_int (attrs, "CursorRow", &row)) ;
		else
			xml_sax_unknown_attr (state, attrs, "Selection");

	g_return_if_fail (col >= 0);
	g_return_if_fail (row >= 0);
	g_return_if_fail (state->cell.col < 0);
	g_return_if_fail (state->cell.row < 0);
	state->cell.col = col;
	state->cell.row = row;
}

static void
xml_sax_selection_end (XMLSaxParseState *state)
{
	CellPos const pos = state->cell;

	state->cell.col = state->cell.row = -1;

	g_return_if_fail (pos.col >= 0);
	g_return_if_fail (pos.row >= 0);

	sheet_set_edit_pos (state->sheet, pos.col, pos.row);
}

static void
xml_sax_cols_rows (XMLSaxParseState *state, CHAR const **attrs, gboolean is_col)
{
	double def_size;

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
xml_sax_colrow (XMLSaxParseState *state, CHAR const **attrs, gboolean is_col)
{
	ColRowInfo *cri = NULL;
	double size = -1.;
	int dummy;
	int count = 1;

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
				xml_sax_unknown_attr (state, attrs, "ColRow");
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
xml_sax_style_region_start (XMLSaxParseState *state, CHAR const **attrs)
{
	g_return_if_fail (state->style_range_init == FALSE);
	g_return_if_fail (state->style == NULL);

	state->style = mstyle_new ();
	state->style_range_init =
		xml_sax_range (attrs, &state->style_range);
}

static void
xml_sax_style_region_end (XMLSaxParseState *state)
{
	g_return_if_fail (state->style_range_init);
	g_return_if_fail (state->style != NULL);
	g_return_if_fail (state->sheet != NULL);

	sheet_style_set_range (state->sheet, &state->style_range, state->style);

	state->style_range_init = FALSE;
	state->style = NULL;
}

static void
xml_sax_styleregion_start (XMLSaxParseState *state, CHAR const **attrs)
{
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
		else if (xml_sax_attr_int (attrs, "Orient", &val))
			mstyle_set_orientation (state->style, val);
		else if (xml_sax_attr_int (attrs, "Shade", &val))
			mstyle_set_pattern (state->style, val);
		else if (xml_sax_attr_int (attrs, "Indent", &val))
			mstyle_set_indent (state->style, val);
		else if (xml_sax_color (attrs, "Fore", &colour))
			mstyle_set_color (state->style, MSTYLE_COLOR_FORE, colour);
		else if (xml_sax_color (attrs, "Back", &colour))
			mstyle_set_color (state->style, MSTYLE_COLOR_BACK, colour);
		else if (xml_sax_color (attrs, "PatternColor", &colour))
			mstyle_set_color (state->style, MSTYLE_COLOR_PATTERN, colour);
		else if (!strcmp (attrs[0], "Format"))
			mstyle_set_format_text (state->style, attrs[1]);
		else
			xml_sax_unknown_attr (state, attrs, "StyleRegion");
	}
}

static void
xml_sax_styleregion_font (XMLSaxParseState *state, CHAR const **attrs)
{
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
			xml_sax_unknown_attr (state, attrs, "StyleFont");
	}
}

static const char *
font_component (const char *fontname, int idx)
{
	int i = 0;
	const char *p = fontname;

	for (; *p && i < idx; p++){
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
	const char *c;

	/*
	 * FIXME: we should do something about the typeface instead
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
xml_sax_styleregion_font_end (XMLSaxParseState *state)
{
	if (state->content->len > 0) {
		char const * content = state->content->str;
		if (*content == '-')
			style_font_read_from_x11 (state->style, content);
		else
			mstyle_set_font_name (state->style, content);
	}
}

static void
xml_sax_style_region_borders (XMLSaxParseState *state, CHAR const **attrs)
{
	int pattern = -1;
	StyleColor *colour = NULL;

	g_return_if_fail (state->style != NULL);

	/* Colour is optional */
	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2) {
		if (xml_sax_color (attrs, "Color", &colour)) ;
		else if (xml_sax_attr_int (attrs, "Style", &pattern)) ;
		else
			xml_sax_unknown_attr (state, attrs, "StyleBorder");
	}

	if (pattern >= STYLE_BORDER_NONE) {
		MStyleElementType const type = MSTYLE_BORDER_TOP +
			state->state - STATE_BORDER_TOP;
		StyleBorder *border =
			style_border_fetch ((StyleBorderType)pattern, colour,
					    style_border_get_orientation (type));
		mstyle_set_border (state->style, type, border);
	}
}

static void
xml_sax_cell (XMLSaxParseState *state, CHAR const **attrs)
{
	int row = -1, col = -1;
	int rows = -1, cols = -1;
	int value_type = -1;
	char const *value_fmt = NULL;
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
			value_fmt = attrs[1];
		else
			xml_sax_unknown_attr (state, attrs, "Cell");
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
	ExprTree *expr;

	expr = expr_parse_string (text,
				  parse_pos_init_cell (&pp, cell),
				  NULL, NULL);

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
	g_string_sprintfa (str, "{%s}(%d,%d)[%d][%d]", expr_text,
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
xml_sax_cell_content (XMLSaxParseState *state)
{
	gboolean is_new_cell, is_post_52_array = FALSE;
	Cell *cell;

	int const col = state->cell.col;
	int const row = state->cell.row;
	int const array_cols = state->array_cols;
	int const array_rows = state->array_rows;
	int const expr_id = state->expr_id;
	int const value_type = state->value_type;
	char const *value_fmt =state->value_fmt;
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

	if (state->content->len > 0) {
		char const * content = state->content->str;

		if (is_post_52_array) {
			g_return_if_fail (content[0] == '=');

			xml_cell_set_array_expr (cell, content+1,
						 array_cols, array_rows);
		} else if (state->version >= GNUM_XML_V3 ||
			   xml_not_used_old_array_spec (cell, content)) {
			if (value_type > 0) {
				Value *v = value_new_from_string (value_type, content);
				StyleFormat *sf = (value_fmt != NULL)
					? style_format_new_XL (value_fmt, FALSE)
					: NULL;
				cell_set_value (cell, v, sf);
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
							     cell->base.expression);
				else
					g_warning ("XML-IO : Shared expression with no expession ??");
			} else if (!is_post_52_array)
				g_warning ("XML-IO : Duplicate shared expression");
		}
	} else if (expr_id > 0) {
		gpointer expr = g_hash_table_lookup (state->expr_map,
			GINT_TO_POINTER (expr_id));

		if (expr != NULL)
			cell_set_expr (cell, expr, NULL);
		else
			g_warning ("XML-IO : Missing shared expression");
	} else if (is_new_cell)
		/*
		 * Only set to empty if this is a new cell.
		 * If it was created by a previous array
		 * we do not want to erase it.
		 */
		cell_set_value (cell, value_new_empty (), NULL);
}

static void
xml_sax_merge (XMLSaxParseState *state)
{
	Range r;
	g_return_if_fail (state->content->len > 0);

	if (parse_range (state->content->str,
			 &r.start.col, &r.start.row,
			 &r.end.col, &r.end.row))
		sheet_merge_add (NULL, state->sheet, &r, FALSE);
}

static void
xml_sax_object (XMLSaxParseState *state, CHAR const **attrs)
{
	
}

static void
xml_sax_finish_parse_wb_names_name (XMLSaxParseState *state)
{
	g_return_if_fail (state->name.name != NULL);
	g_return_if_fail (state->name.value != NULL);
	
	if (state->version >= GNUM_XML_V7) {
		ParseError  perr;
		
		if (!expr_name_create (state->wb, NULL, state->name.name,
				       state->name.value, &perr))
			g_warning (perr.message);
		parse_error_free (&perr);
	} else {
		/*
		 * We can't do this for versions < V7. The problem
		 * is that we really need the SheetNameIndex for this
		 * to function correctly.
		 * FIXME: We should fallback to the xml DOM parser
		 * when this fails.
		 */
		g_warning ("Can't process named expression '%s'. Ignoring!", state->name.name);
	}

	g_free (state->name.name);
	g_free (state->name.value);
	state->name.name = NULL;
	state->name.value = NULL;
}

static void
xml_sax_finish_parse_sheet_names_name (XMLSaxParseState *state)
{
	ParseError  perr;
	
	g_return_if_fail (state->name.name != NULL);
	g_return_if_fail (state->name.value != NULL);

	if (!expr_name_create (NULL, state->sheet, state->name.name,
			       state->name.value, &perr))
		g_warning (perr.message);
	parse_error_free (&perr);
			  
	g_free (state->name.name);
	g_free (state->name.value);
	state->name.name = NULL;
	state->name.value = NULL;
}

static void
xml_sax_name (XMLSaxParseState *state)
{
	char const * content = state->content->str;
	int const len = state->content->len;

	switch (state->state) {
	case STATE_SHEET_NAMES_NAME_NAME:
	case STATE_NAMES_NAME_NAME:
		g_return_if_fail (state->name.name == NULL);
		state->name.name = g_strndup (content, len);
		break;
	case STATE_SHEET_NAMES_NAME_VALUE:
	case STATE_NAMES_NAME_VALUE:
		g_return_if_fail (state->name.value == NULL);
		state->name.value = g_strndup (content, len);
		break;
	default:
		return;
	}
}

/****************************************************************************/

static gboolean
xml_sax_switch_state (XMLSaxParseState *state, CHAR const *name, xmlSaxState const newState)
{
	if (strcmp (name, xmlSax_state_names[newState]))
		    return FALSE;

	state->state_stack = g_slist_prepend (state->state_stack, GINT_TO_POINTER (state->state));
	state->state = newState;
	return TRUE;
}

static void
xml_sax_unknown_state (XMLSaxParseState *state, CHAR const *name)
{
	if (state->unknown_depth++)
		return;
	g_warning ("Unexpected element '%s' in state %s.", name, xmlSax_state_names[state->state]);
}

/*
 * We parse and do some limited validation of the XML file, if this
 * passes, then we return TRUE
gboolean
xml_sax_file_probe (GnumFileOpener const *fo, const gchar *file_name, FileProbeLevel pl)
{
	return TRUE;
}
 */

static void
xml_sax_start_element (XMLSaxParseState *state, CHAR const *name, CHAR const **attrs)
{
	switch (state->state) {
	case STATE_START:
		if (xml_sax_switch_state (state, name, STATE_WB)) {
			xml_sax_wb (state, attrs);
		} else
			xml_sax_unknown_state (state, name);
		break;

	case STATE_WB :
		if (xml_sax_switch_state (state, name, STATE_WB_ATTRIBUTES)) {
		} else if (xml_sax_switch_state (state, name, STATE_WB_SUMMARY)) {
		} else if (xml_sax_switch_state (state, name, STATE_WB_SHEETNAME_INDEX)) {
		} else if (xml_sax_switch_state (state, name, STATE_WB_GEOMETRY)) {
			xml_sax_wb_view (state, attrs);
		} else if (xml_sax_switch_state (state, name, STATE_WB_SHEETS)) {
		} else if (xml_sax_switch_state (state, name, STATE_WB_VIEW)) {
			xml_sax_wb_view (state, attrs);
		} else if (xml_sax_switch_state (state, name, STATE_NAMES)) {
		} else
			xml_sax_unknown_state (state, name);
		break;

	case STATE_WB_ATTRIBUTES :
		if (xml_sax_switch_state (state, name, STATE_WB_ATTRIBUTES_ELEM)) {
		} else
			xml_sax_unknown_state (state, name);
		break;

	case STATE_WB_ATTRIBUTES_ELEM :
		if (xml_sax_switch_state (state, name, STATE_WB_ATTRIBUTES_ELEM_NAME)) {
		} else if (xml_sax_switch_state (state, name, STATE_WB_ATTRIBUTES_ELEM_TYPE)) {
		} else if (xml_sax_switch_state (state, name, STATE_WB_ATTRIBUTES_ELEM_VALUE)) {
		} else
			xml_sax_unknown_state (state, name);
		break;

	case STATE_WB_SUMMARY :
		if (xml_sax_switch_state (state, name, STATE_WB_SUMMARY_ITEM)) {
		} else
			xml_sax_unknown_state (state, name);
		break;

	case STATE_WB_SUMMARY_ITEM :
		if (xml_sax_switch_state (state, name, STATE_WB_SUMMARY_ITEM_NAME)) {
		} else if (xml_sax_switch_state (state, name, STATE_WB_SUMMARY_ITEM_VALUE_STR)) {
		} else if (xml_sax_switch_state (state, name, STATE_WB_SUMMARY_ITEM_VALUE_INT)) {
		} else
			xml_sax_unknown_state (state, name);
		break;

	case STATE_WB_SHEETNAME_INDEX :
		if (xml_sax_switch_state (state, name, STATE_WB_SHEETNAME)) {
		} else
			xml_sax_unknown_state (state, name);
		break;
		
	case STATE_WB_SHEETS :
		if (xml_sax_switch_state (state, name, STATE_SHEET)) {
			xml_sax_sheet_start (state, attrs);
		} else
			xml_sax_unknown_state (state, name);
		break;

	case STATE_SHEET :
		if (xml_sax_switch_state (state, name, STATE_SHEET_NAME)) {
		} else if (xml_sax_switch_state (state, name, STATE_SHEET_MAXCOL)) {
		} else if (xml_sax_switch_state (state, name, STATE_SHEET_MAXROW)) {
		} else if (xml_sax_switch_state (state, name, STATE_SHEET_ZOOM)) {
		} else if (xml_sax_switch_state (state, name, STATE_SHEET_NAMES)) {
		} else if (xml_sax_switch_state (state, name, STATE_SHEET_PRINTINFO)) {
		} else if (xml_sax_switch_state (state, name, STATE_SHEET_STYLES)) {
		} else if (xml_sax_switch_state (state, name, STATE_SHEET_COLS)) {
			xml_sax_cols_rows (state, attrs, TRUE);
		} else if (xml_sax_switch_state (state, name, STATE_SHEET_ROWS)) {
			xml_sax_cols_rows (state, attrs, FALSE);
		} else if (xml_sax_switch_state (state, name, STATE_SHEET_SELECTIONS)) {
			xml_sax_selection (state, attrs);
		} else if (xml_sax_switch_state (state, name, STATE_SHEET_CELLS)) {
		} else if (xml_sax_switch_state (state, name, STATE_SHEET_MERGED_REGION)) {
		} else if (xml_sax_switch_state (state, name, STATE_SHEET_SOLVER)) {
		} else if (xml_sax_switch_state (state, name, STATE_SHEET_OBJECTS)) {
		} else
			xml_sax_unknown_state (state, name);
		break;
		
	case STATE_SHEET_NAMES:
		if (xml_sax_switch_state (state, name, STATE_SHEET_NAMES_NAME)) {
		} else
			xml_sax_unknown_state (state, name);
		break;
	case STATE_SHEET_NAMES_NAME:
		if (xml_sax_switch_state (state, name, STATE_SHEET_NAMES_NAME_NAME)) {
		} else if (xml_sax_switch_state (state, name, STATE_SHEET_NAMES_NAME_VALUE)) {
		} else
			xml_sax_unknown_state (state, name);
		break;
		
	case STATE_SHEET_MERGED_REGION :
		if (!xml_sax_switch_state (state, name, STATE_SHEET_MERGE))
			xml_sax_unknown_state (state, name);
		break;

	case STATE_SHEET_PRINTINFO :
		if (xml_sax_switch_state (state, name, STATE_PRINT_MARGINS)) {
		} else if (xml_sax_switch_state (state, name, STATE_PRINT_VCENTER)) {
		} else if (xml_sax_switch_state (state, name, STATE_PRINT_HCENTER)) {
		} else if (xml_sax_switch_state (state, name, STATE_PRINT_GRID)) {
		} else if (xml_sax_switch_state (state, name, STATE_PRINT_MONO)) {
		} else if (xml_sax_switch_state (state, name, STATE_PRINT_DRAFTS)) {
		} else if (xml_sax_switch_state (state, name, STATE_PRINT_TITLES)) {
		} else if (xml_sax_switch_state (state, name, STATE_PRINT_REPEAT_TOP)) {
		} else if (xml_sax_switch_state (state, name, STATE_PRINT_REPEAT_LEFT)) {
		} else if (xml_sax_switch_state (state, name, STATE_PRINT_ORDER)) {
		} else if (xml_sax_switch_state (state, name, STATE_PRINT_ORIENT)) {
		} else if (xml_sax_switch_state (state, name, STATE_PRINT_HEADER)) {
		} else if (xml_sax_switch_state (state, name, STATE_PRINT_FOOTER)) {
		} else if (xml_sax_switch_state (state, name, STATE_PRINT_PAPER)) {
		} else if (xml_sax_switch_state (state, name, STATE_PRINT_EVEN_ONLY_STYLE)) {
		} else
			xml_sax_unknown_state (state, name);
		break;

	case STATE_PRINT_MARGINS :
		if (xml_sax_switch_state (state, name, STATE_PRINT_MARGIN_TOP) ||
		    xml_sax_switch_state (state, name, STATE_PRINT_MARGIN_BOTTOM) ||
		    xml_sax_switch_state (state, name, STATE_PRINT_MARGIN_LEFT) ||
		    xml_sax_switch_state (state, name, STATE_PRINT_MARGIN_RIGHT) ||
		    xml_sax_switch_state (state, name,
				     STATE_PRINT_MARGIN_HEADER) ||
		    xml_sax_switch_state (state, name, STATE_PRINT_MARGIN_FOOTER)) {
			xml_sax_print_margins (state, attrs);
		} else
			xml_sax_unknown_state (state, name);
		break;

	case STATE_SHEET_STYLES :
		if (xml_sax_switch_state (state, name, STATE_STYLE_REGION))
			xml_sax_style_region_start (state, attrs);
		else
			xml_sax_unknown_state (state, name);
		break;

	case STATE_STYLE_REGION :
		if (xml_sax_switch_state (state, name, STATE_STYLE_STYLE))
			xml_sax_styleregion_start (state, attrs);
		else
			xml_sax_unknown_state (state, name);
		break;

	case STATE_STYLE_STYLE :
		if (xml_sax_switch_state (state, name, STATE_STYLE_FONT))
			xml_sax_styleregion_font (state, attrs);
		else if (xml_sax_switch_state (state, name, STATE_STYLE_BORDER)) {
		} else
			xml_sax_unknown_state (state, name);
		break;

	case STATE_STYLE_BORDER :
		if (xml_sax_switch_state (state, name, STATE_BORDER_TOP) ||
		    xml_sax_switch_state (state, name, STATE_BORDER_BOTTOM) ||
		    xml_sax_switch_state (state, name, STATE_BORDER_LEFT) ||
		    xml_sax_switch_state (state, name, STATE_BORDER_RIGHT) ||
		    xml_sax_switch_state (state, name, STATE_BORDER_DIAG) ||
		    xml_sax_switch_state (state, name, STATE_BORDER_REV_DIAG))
			xml_sax_style_region_borders (state, attrs);
		else
			xml_sax_unknown_state (state, name);
		break;

	case STATE_SHEET_COLS :
		if (xml_sax_switch_state (state, name, STATE_COL))
			xml_sax_colrow (state, attrs, TRUE);
		else
			xml_sax_unknown_state (state, name);
		break;

	case STATE_SHEET_ROWS :
		if (xml_sax_switch_state (state, name, STATE_ROW))
			xml_sax_colrow (state, attrs, FALSE);
		else
			xml_sax_unknown_state (state, name);
		break;

	case STATE_SHEET_SELECTIONS :
		if (xml_sax_switch_state (state, name, STATE_SELECTION))
			xml_sax_selection_range (state, attrs);
		else
			xml_sax_unknown_state (state, name);
		break;

	case STATE_SHEET_CELLS :
		if (xml_sax_switch_state (state, name, STATE_CELL))
			xml_sax_cell (state, attrs);
		else
			xml_sax_unknown_state (state, name);
		break;

	case STATE_CELL :
		if (!xml_sax_switch_state (state, name, STATE_CELL_CONTENT))
			xml_sax_unknown_state (state, name);
		break;

	case STATE_SHEET_OBJECTS :
		if (xml_sax_switch_state (state, name, STATE_OBJECT_RECTANGLE) ||
		    xml_sax_switch_state (state, name, STATE_OBJECT_ELLIPSE) ||
		    xml_sax_switch_state (state, name, STATE_OBJECT_ARROW) ||
		    xml_sax_switch_state (state, name, STATE_OBJECT_LINE)) {
			xml_sax_object (state, attrs);
		} else
			xml_sax_unknown_state (state, name);
		break;

	case STATE_OBJECT_RECTANGLE :
	case STATE_OBJECT_ELLIPSE :
	case STATE_OBJECT_ARROW :
	case STATE_OBJECT_LINE :
		if (xml_sax_switch_state (state, name, STATE_OBJECT_POINTS)) {
		} else
			xml_sax_unknown_state (state, name);
		break;
		
	case STATE_NAMES:
		if (xml_sax_switch_state (state, name, STATE_NAMES_NAME)) {
		} else
			xml_sax_unknown_state (state, name);
		break;
	case STATE_NAMES_NAME:
		if (xml_sax_switch_state (state, name, STATE_NAMES_NAME_NAME)) {
		} else if (xml_sax_switch_state (state, name, STATE_NAMES_NAME_VALUE)) {
		} else
			xml_sax_unknown_state (state, name);
		break;

	default :
		break;
	};
}

static void
xml_sax_end_element (XMLSaxParseState *state, const CHAR *name)
{
	if (state->unknown_depth > 0) {
		state->unknown_depth--;
		return;
	}

	g_return_if_fail (state->state_stack != NULL);
	g_return_if_fail (!strcmp (name, xmlSax_state_names[state->state]));

	switch (state->state) {
	case STATE_SHEET :
		xml_sax_sheet_end (state);
		break;

	case STATE_WB_ATTRIBUTES_ELEM :
		xml_sax_finish_parse_wb_attr (state);
		break;

	case STATE_WB_ATTRIBUTES :
		wb_view_set_attribute_list (state->wb_view, state->attributes);
		xml_sax_free_arg_list (state->attributes);
		state->attributes = NULL;
		break;

	case STATE_SHEET_SELECTIONS :
		xml_sax_selection_end (state);
		break;

	case STATE_STYLE_REGION :
		xml_sax_style_region_end (state);
		break;

	case STATE_STYLE_FONT :
		xml_sax_styleregion_font_end (state);
		g_string_truncate (state->content, 0);
		break;

	case STATE_WB_ATTRIBUTES_ELEM_NAME :
	case STATE_WB_ATTRIBUTES_ELEM_TYPE :
	case STATE_WB_ATTRIBUTES_ELEM_VALUE :
		xml_sax_attr_elem (state);
		g_string_truncate (state->content, 0);
		break;

	case STATE_WB_SUMMARY_ITEM_NAME :
	case STATE_WB_SUMMARY_ITEM_VALUE_STR :
	case STATE_WB_SUMMARY_ITEM_VALUE_INT :
		g_string_truncate (state->content, 0);
		break;

	case STATE_WB_SHEETNAME :
		xml_sax_wb_sheetname (state);
		g_string_truncate (state->content, 0);
		break;
		
	case STATE_SHEET_NAME :
		xml_sax_sheet_name (state);
		g_string_truncate (state->content, 0);
		break;

	case STATE_SHEET_ZOOM :
		xml_sax_sheet_zoom (state);
		g_string_truncate (state->content, 0);
		break;
		
	case STATE_SHEET_NAMES_NAME :
		xml_sax_finish_parse_sheet_names_name (state);
		break;
	case STATE_SHEET_NAMES_NAME_NAME :
	case STATE_SHEET_NAMES_NAME_VALUE :
		xml_sax_name (state);
		g_string_truncate (state->content, 0);
		break;
		
	case STATE_PRINT_MARGIN_TOP :
	case STATE_PRINT_MARGIN_BOTTOM :
	case STATE_PRINT_MARGIN_LEFT :
	case STATE_PRINT_MARGIN_RIGHT :
	case STATE_PRINT_MARGIN_HEADER :
	case STATE_PRINT_MARGIN_FOOTER :
	case STATE_PRINT_ORDER :
	case STATE_PRINT_ORIENT :
	case STATE_PRINT_PAPER :
	case STATE_PRINT_EVEN_ONLY_STYLE :
		g_string_truncate (state->content, 0);
		break;

	case STATE_CELL :
		if (state->cell.row >= 0 || state->cell.col >= 0)
			xml_sax_cell_content (state);
		break;

	case STATE_CELL_CONTENT :
		xml_sax_cell_content (state);
		g_string_truncate (state->content, 0);
		break;

	case STATE_SHEET_MERGE :
		xml_sax_merge (state);
		g_string_truncate (state->content, 0);
		break;
		
	case STATE_NAMES_NAME :
		xml_sax_finish_parse_wb_names_name (state);
		break;
	case STATE_NAMES_NAME_NAME :
	case STATE_NAMES_NAME_VALUE :
		xml_sax_name (state);
		g_string_truncate (state->content, 0);
		break;
	default :
		break;
	};

	/* pop the state stack */
	state->state = GPOINTER_TO_INT (state->state_stack->data);
	state->state_stack = g_slist_remove (state->state_stack, state->state_stack->data);
}

static void
xml_sax_characters (XMLSaxParseState *state, const CHAR *chars, int len)
{
	switch (state->state) {
	case STATE_WB_ATTRIBUTES_ELEM_NAME :
	case STATE_WB_ATTRIBUTES_ELEM_TYPE :
	case STATE_WB_ATTRIBUTES_ELEM_VALUE :
	case STATE_WB_SUMMARY_ITEM_NAME :
	case STATE_WB_SUMMARY_ITEM_VALUE_INT :
	case STATE_WB_SUMMARY_ITEM_VALUE_STR :
	case STATE_WB_SHEETNAME :
	case STATE_SHEET_NAME :
	case STATE_SHEET_ZOOM :
	case STATE_SHEET_NAMES_NAME_NAME :
	case STATE_SHEET_NAMES_NAME_VALUE :
	case STATE_PRINT_MARGIN_TOP :
	case STATE_PRINT_MARGIN_BOTTOM :
	case STATE_PRINT_MARGIN_LEFT :
	case STATE_PRINT_MARGIN_RIGHT :
	case STATE_PRINT_MARGIN_HEADER :
	case STATE_PRINT_MARGIN_FOOTER :
	case STATE_PRINT_ORDER :
	case STATE_PRINT_ORIENT :
	case STATE_PRINT_PAPER :
	case STATE_PRINT_EVEN_ONLY_STYLE :
	case STATE_STYLE_FONT :
	case STATE_CELL_CONTENT :
	case STATE_SHEET_MERGE :
	case STATE_NAMES_NAME_NAME :
	case STATE_NAMES_NAME_VALUE :
		while (len-- > 0)
			g_string_append_c (state->content, *chars++);

	default :
		break;
	};
}

static xmlEntityPtr
xml_sax_get_entity (XMLSaxParseState *state, const CHAR *name)
{
	return xmlGetPredefinedEntity (name);
}

static void
xml_sax_start_document (XMLSaxParseState *state)
{
	state->state = STATE_START;
	state->unknown_depth = 0;
	state->state_stack = NULL;

	state->sheet = NULL;
	state->version = GNUM_XML_UNKNOWN;

	state->content = g_string_sized_new (128);

	state->attribute.name = state->attribute.value = NULL;
	state->attribute.type = -1;
	state->attributes = NULL;

	state->name.name = state->name.value = NULL;

	state->style_range_init = FALSE;
	state->style = NULL;

	state->cell.row = state->cell.col = -1;
	state->array_rows = state->array_cols = -1;
	state->expr_id = -1;
	state->value_type = -1;
	state->value_fmt = NULL;

	state->expr_map = g_hash_table_new (g_direct_hash, g_direct_equal);
}

static void
xml_sax_end_document (XMLSaxParseState *state)
{
	g_string_free (state->content, TRUE);
	g_hash_table_destroy (state->expr_map);

	g_return_if_fail (state->state == STATE_START);
	g_return_if_fail (state->unknown_depth == 0);
}

static xmlSAXHandler xmlSaxSAXParser = {
	0, /* internalSubset */
	0, /* isStandalone */
	0, /* hasInternalSubset */
	0, /* hasExternalSubset */
	0, /* resolveEntity */
	(getEntitySAXFunc)xml_sax_get_entity, /* getEntity */
	0, /* entityDecl */
	0, /* notationDecl */
	0, /* attributeDecl */
	0, /* elementDecl */
	0, /* unparsedEntityDecl */
	0, /* setDocumentLocator */
	(startDocumentSAXFunc)xml_sax_start_document, /* startDocument */
	(endDocumentSAXFunc)xml_sax_end_document, /* endDocument */
	(startElementSAXFunc)xml_sax_start_element, /* startElement */
	(endElementSAXFunc)xml_sax_end_element, /* endElement */
	0, /* reference */
	(charactersSAXFunc)xml_sax_characters, /* characters */
	0, /* ignorableWhitespace */
	0, /* processingInstruction */
	0, /* comment */
	(warningSAXFunc)xml_sax_warning, /* warning */
	(errorSAXFunc)xml_sax_error, /* error */
	(fatalErrorSAXFunc)xml_sax_fatal_error, /* fatalError */
};

void
xml_sax_file_open (GnumFileOpener const *fo, IOContext *io_context,
		   WorkbookView *wb_view, char const *filename)
{
	xmlParserCtxtPtr ctxt;
	XMLSaxParseState state;

	g_return_if_fail (wb_view != NULL);
	g_return_if_fail (filename != NULL);

	state.context = io_context;
	state.wb_view = wb_view;
	state.wb = wb_view_workbook (wb_view);

	/*
	 * TODO : think about pushing the data into the parser
	 * and using vfs.
	 */
	ctxt = xmlCreateFileParserCtxt (filename);
	if (ctxt == NULL) { 
		gnumeric_io_error_info_set (io_context,
		                            error_info_new_str (
		                            _("xmlCreateFileParserCtxt () failed.")));
		return;
	}
	ctxt->sax = &xmlSaxSAXParser;
	ctxt->userData = &state;

	xmlParseDocument (ctxt);

	if (!ctxt->wellFormed)
		gnumeric_io_error_info_set (io_context,
		                            error_info_new_str (
		                            _("XML document not well formed!")));
	else
		workbook_queue_all_recalc (state.wb);

	ctxt->sax = NULL;
	xmlFreeParserCtxt (ctxt);
}
