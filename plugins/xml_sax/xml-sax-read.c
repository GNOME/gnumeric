/* vim: set sw=8: */

/*
 * xml2.c : a test harness for the sax based xml parse routines.
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
#include "style.h"
#include "style-border.h"
#include "style-color.h"
#include "format.h"
#include "cell.h"
#include "position.h"
#include "expr.h"
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

gboolean xml2_file_probe (FileOpener const *fo, const gchar *file_name);
void     xml2_file_open (FileOpener const *fo, IOContext *io_context,
                         WorkbookView *wb_view, char const *filename);


typedef struct _XML2ParseState XML2ParseState;

/*****************************************************************************/

static void
xml2UnknownAttr (XML2ParseState *state, CHAR const * const *attrs, char const *name)
{
	g_warning ("Unexpected attribute '%s'='%s' for element of type %s.", name, attrs[0], attrs[1]);
}

static int
xml2ParseAttrDouble (CHAR const * const *attrs, char const *name, double * res)
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
xmlParseDouble (CHAR const *chars, double *res)
{
	char *end;
	*res = g_strtod (chars, &end);
	return *end == '\0';
}

static int
xml2ParseAttrInt (CHAR const * const *attrs, char const *name, int *res)
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
xmlParseInt (CHAR const *chars, int *res)
{
	char *end;
	*res = strtol (chars, &end, 10);
	return *end == '\0';
}

static int
xml2ParseAttrColour (CHAR const * const *attrs, char const *name, StyleColor **res)
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
xml2ParseRange (CHAR const * const *attrs, Range *res)
{
	int flags = 0;
	for (; attrs[0] && attrs[1] ; attrs += 2)
		if (xml2ParseAttrInt (attrs, "startCol", &res->start.col))
			flags |= 0x1;
		else if (xml2ParseAttrInt (attrs, "startRow", &res->start.row))
			flags |= 0x2;
		else if (xml2ParseAttrInt (attrs, "endCol", &res->end.col))
			flags |= 0x4;
		else if (xml2ParseAttrInt (attrs, "endRow", &res->end.row))
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
xml2_write (Workbook *wb, char const *filename, ErrorInfo **ret_error)
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
	STATE_WB_GEOMETRY,
	STATE_WB_SHEETS,
		STATE_SHEET,
			STATE_SHEET_NAME,	/* convert to attr */
			STATE_SHEET_MAXCOL,	/* convert to attr */
			STATE_SHEET_MAXROW,	/* convert to attr */
			STATE_SHEET_ZOOM,	/* convert to attr */
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
			STATE_SHEET_SOLVER,
		STATE_SHEET_OBJECTS,
				STATE_OBJECT_POINTS,
			STATE_OBJECT_RECTANGLE,
			STATE_OBJECT_ELLIPSE,
			STATE_OBJECT_ARROW,
			STATE_OBJECT_LINE,

	STATE_WB_VIEW,

	STATE_UNKNOWN
} xml2State;

/*
 * This is not complete yet.
 * I just pulled it out of a single saved gnumeric file.
 * However, with a bit of cleanup, we should be able to use it as the basis for a dtd.
 */
static char const * const xml2_state_names[] =
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
	"gmr:Geometry",
	"gmr:Sheets",
		"gmr:Sheet",
			"gmr:Name",
			"gmr:MaxCol",
			"gmr:MaxRow",
			"gmr:Zoom",
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

struct _XML2ParseState
{
	xml2State state;
	gint	  unknown_depth;	/* handle recursive unknown tags */
	GSList	 *state_stack;

	IOContext 	*context;	/* The IOcontext managing things */
	WorkbookView	*wb_view;	/* View for the new workbook */
	Workbook	*wb;		/* The new workbook */
	int version;

	Sheet *sheet;
	double sheet_zoom;

	/* Only valid while parsing attributes */
	struct {
		char *name;
		char *value;
		int   type;
	} attribute;
	GList *attributes;

	gboolean  style_range_init;
	Range	  style_range;
	MStyle   *style;

	CellPos cell;
	int expr_id, array_rows, array_cols;
	ValueType value_type;
	char const *value_fmt;

	GString *content;

	/* expressions with ref > 1 a map from index -> expr pointer */
	GHashTable *expr_map;
};

/****************************************************************************/

static void
xml2ParseWBView (XML2ParseState *state, CHAR const **attrs)
{
	int sheet_index;
	int width = -1, height = -1;

	for (; attrs[0] && attrs[1] ; attrs += 2)
		if (xml2ParseAttrInt (attrs, "SelectedTab", &sheet_index))
			wb_view_sheet_focus (state->wb_view,
				workbook_sheet_by_index (state->wb, sheet_index));
		else if (xml2ParseAttrInt (attrs, "Width", &width)) ;
		else if (xml2ParseAttrInt (attrs, "Height", &height)) ;
		else
			xml2UnknownAttr (state, attrs, "WorkbookView");

	if (width > 0 && height > 0)
		wb_view_preferred_size (state->wb_view, width, height);
}

static void
xml2_arg_set (GtkArg *arg, gchar *string)
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
xml2FinishParseAttr (XML2ParseState *state)
{
	GtkArg *arg;

	g_return_if_fail (state->attribute.name != NULL);
	g_return_if_fail (state->attribute.value != NULL);
	g_return_if_fail (state->attribute.type >= 0);

	arg = gtk_arg_new (state->attribute.type);
	arg->name = state->attribute.name;
	xml2_arg_set (arg, state->attribute.value);
	state->attributes = g_list_prepend (state->attributes, arg);

	state->attribute.type = -1;
	g_free (state->attribute.value);
	state->attribute.value = NULL;
	state->attribute.name = NULL;
}

static void
xml2_free_arg_list (GList *start)
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
xml2_parse_attr_elem (XML2ParseState *state)
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
		if (xmlParseInt (content, &type))
			state->attribute.type = type;
		break;
	}

	default :
		g_assert_not_reached ();
	};

}

static void
xml2ParseSheet (XML2ParseState *state, CHAR const **attrs)
{
	int tmp;

	if (attrs == NULL)
		return;

	for (; attrs[0] && attrs[1] ; attrs += 2)
		if (xml2ParseAttrInt (attrs, "DisplayFormulas", &tmp))
			state->sheet->display_formulas = tmp;
		else if (xml2ParseAttrInt (attrs, "HideZero", &tmp))
			state->sheet->hide_zero = tmp;
		else if (xml2ParseAttrInt (attrs, "HideGrid", &tmp))
			state->sheet->hide_grid = tmp;
		else if (xml2ParseAttrInt (attrs, "HideColHeader", &tmp))
			state->sheet->hide_col_header = tmp;
		else if (xml2ParseAttrInt (attrs, "HideRowHeader", &tmp))
			state->sheet->hide_row_header = tmp;
		else
			xml2UnknownAttr (state, attrs, "Sheet");
}

static void
xml2ParseSheetName (XML2ParseState *state)
{
	char const * content = state->content->str;
	g_return_if_fail (state->sheet == NULL);

	state->sheet = sheet_new (state->wb, content);
	workbook_sheet_attach (state->wb, state->sheet, NULL);
}

static void
xml2ParseSheetZoom (XML2ParseState *state)
{
	char const * content = state->content->str;
	double zoom;

	g_return_if_fail (state->sheet != NULL);

	if (xmlParseDouble (content, &zoom))
		sheet_set_zoom_factor (state->sheet, zoom, FALSE, FALSE);
}

static void
xml2ParseMargin (XML2ParseState *state, CHAR const **attrs)
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

	for (; attrs[0] && attrs[1] ; attrs += 2) {
		if (xml2ParseAttrDouble (attrs, "Points", &points))
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
			xml2UnknownAttr (state, attrs, "Margin");
	}
}

static void
xml2ParseSelectionRange (XML2ParseState *state, CHAR const **attrs)
{
	Range r;
	if (xml2ParseRange (attrs, &r))
		sheet_selection_add_range (state->sheet,
					   r.start.col, r.start.row,
					   r.start.col, r.start.row,
					   r.end.col, r.end.row);
}

static void
xml2ParseSelection (XML2ParseState *state, CHAR const **attrs)
{
	int col = -1, row = -1;

	sheet_selection_reset (state->sheet);

	for (; attrs[0] && attrs[1] ; attrs += 2)
		if (xml2ParseAttrInt (attrs, "CursorCol", &col)) ;
		else if (xml2ParseAttrInt (attrs, "CursorRow", &row)) ;
		else
			xml2UnknownAttr (state, attrs, "Selection");

	g_return_if_fail (col >= 0);
	g_return_if_fail (row >= 0);
	g_return_if_fail (state->cell.col < 0);
	g_return_if_fail (state->cell.row < 0);
	state->cell.col = col;
	state->cell.row = row;
}

static void
xml2FinishSelection (XML2ParseState *state)
{
	CellPos const pos = state->cell;

	state->cell.col = state->cell.row = -1;

	g_return_if_fail (pos.col >= 0);
	g_return_if_fail (pos.row >= 0);

	sheet_set_edit_pos (state->sheet, pos.col, pos.row);
}

static void
xml2ParseColRow (XML2ParseState *state, CHAR const **attrs, gboolean is_col)
{
	ColRowInfo *cri = NULL;
	double size = -1.;
	int dummy;
	int count = 1;

	g_return_if_fail (state->sheet != NULL);

	for (; attrs[0] && attrs[1] ; attrs += 2) {
		if (xml2ParseAttrInt (attrs, "No", &dummy)) {
			g_return_if_fail (cri == NULL);

			cri = is_col
				? sheet_col_fetch (state->sheet, dummy)
				: sheet_row_fetch (state->sheet, dummy);
		} else {
			g_return_if_fail (cri != NULL);

			if (xml2ParseAttrDouble (attrs, "Unit", &size)) ;
			else if (xml2ParseAttrInt (attrs, "Count", &count)) ;
			else if (xml2ParseAttrInt (attrs, "MarginA", &dummy))
				cri->margin_a = dummy;
			else if (xml2ParseAttrInt (attrs, "MarginB", &dummy))
				cri->margin_b = dummy;
			else if (xml2ParseAttrInt (attrs, "HardSize", &dummy))
				cri->hard_size = dummy;
			else if (xml2ParseAttrInt (attrs, "Hidden", &dummy))
				cri->visible = !dummy;
			else
				xml2UnknownAttr (state, attrs, "ColRow");
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
xml2ParseStyleRegion (XML2ParseState *state, CHAR const **attrs)
{
	g_return_if_fail (state->style_range_init == FALSE);
	g_return_if_fail (state->style == NULL);

	state->style = mstyle_new ();
	state->style_range_init =
		xml2ParseRange (attrs, &state->style_range);
}

static void
xml2ParseStyleRegionStyle (XML2ParseState *state, CHAR const **attrs)
{
	int val;
	StyleColor *colour;

	g_return_if_fail (state->style != NULL);

	if (attrs == NULL)
		return;

	for (; attrs[0] && attrs[1] ; attrs += 2) {
		if (xml2ParseAttrInt (attrs, "HAlign", &val))
			mstyle_set_align_h (state->style, val);
		else if (xml2ParseAttrInt (attrs, "VAlign", &val))
			mstyle_set_align_v (state->style, val);

		/* Pre version V6 */
		else if (xml2ParseAttrInt (attrs, "Fit", &val))
			mstyle_set_wrap_text (state->style, val);

		else if (xml2ParseAttrInt (attrs, "WrapText", &val))
			mstyle_set_wrap_text (state->style, val);
		else if (xml2ParseAttrInt (attrs, "Orient", &val))
			mstyle_set_orientation (state->style, val);
		else if (xml2ParseAttrInt (attrs, "Shade", &val))
			mstyle_set_pattern (state->style, val);
		else if (xml2ParseAttrInt (attrs, "Indent", &val))
			mstyle_set_indent (state->style, val);
		else if (xml2ParseAttrColour (attrs, "Fore", &colour))
			mstyle_set_color (state->style, MSTYLE_COLOR_FORE, colour);
		else if (xml2ParseAttrColour (attrs, "Back", &colour))
			mstyle_set_color (state->style, MSTYLE_COLOR_BACK, colour);
		else if (xml2ParseAttrColour (attrs, "PatternColor", &colour))
			mstyle_set_color (state->style, MSTYLE_COLOR_PATTERN, colour);
		else if (!strcmp (attrs[0], "Format"))
			mstyle_set_format_text (state->style, attrs[1]);
		else
			xml2UnknownAttr (state, attrs, "StyleRegion");
	}
}

static void
xml2ParseStyleRegionFont (XML2ParseState *state, CHAR const **attrs)
{
	double size_pts = 10.;
	int val;

	g_return_if_fail (state->style != NULL);

	if (attrs == NULL)
		return;

	for (; attrs[0] && attrs[1] ; attrs += 2) {
		if (xml2ParseAttrDouble (attrs, "Unit", &size_pts))
			mstyle_set_font_size (state->style, size_pts);
		else if (xml2ParseAttrInt (attrs, "Bold", &val))
			mstyle_set_font_bold (state->style, val);
		else if (xml2ParseAttrInt (attrs, "Italic", &val))
			mstyle_set_font_italic (state->style, val);
		else if (xml2ParseAttrInt (attrs, "Underline", &val))
			mstyle_set_font_uline (state->style, (StyleUnderlineType)val);
		else if (xml2ParseAttrInt (attrs, "StrikeThrough", &val))
			mstyle_set_font_strike (state->style, val ? TRUE : FALSE);
		else
			xml2UnknownAttr (state, attrs, "StyleFont");
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
xml2FinishStyleRegionFont (XML2ParseState *state)
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
xml2ParseStyleRegionBorders (XML2ParseState *state, CHAR const **attrs)
{
	int pattern = -1;
	StyleColor *colour = NULL;

	g_return_if_fail (state->style != NULL);

	/* Colour is optional */
	for (; attrs[0] && attrs[1] ; attrs += 2) {
		if (xml2ParseAttrColour (attrs, "Color", &colour)) ;
		else if (xml2ParseAttrInt (attrs, "Style", &pattern)) ;
		else
			xml2UnknownAttr (state, attrs, "StyleBorder");
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
xml2ParseCell (XML2ParseState *state, CHAR const **attrs)
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

	for (; attrs[0] && attrs[1] ; attrs += 2) {
		if (xml2ParseAttrInt (attrs, "Col", &col)) ;
		else if (xml2ParseAttrInt (attrs, "Row", &row)) ;
		else if (xml2ParseAttrInt (attrs, "Cols", &cols)) ;
		else if (xml2ParseAttrInt (attrs, "Rows", &rows)) ;
		else if (xml2ParseAttrInt (attrs, "ExprID", &expr_id)) ;
		else if (xml2ParseAttrInt (attrs, "ValueType", &value_type)) ;
		else if (!strcmp (attrs[0], "ValueFormat"))
			value_fmt = attrs[1];
		else
			xml2UnknownAttr (state, attrs, "Cell");
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
	char *error_string = NULL;
	ParsePos pp;
	ExprTree * expr;

	expr = expr_parse_string (text,
				  parse_pos_init_cell (&pp, cell),
				  NULL, &error_string);

	g_return_if_fail (expr != NULL);
	cell_set_array_formula (cell->base.sheet,
				cell->pos.row,
				cell->pos.col,
				cell->pos.row + rows-1,
				cell->pos.col + cols-1,
				expr, TRUE);
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
	cols = strtol (ptr = ++end, &end, 10);
	if (end == ptr || end[0] != ')' || end[1] != '[')
		return TRUE;
	row = strtol (ptr = (end += 2), &end, 10);
	if (end == ptr || end[0] != ']' || end[1] != '[')
		return TRUE;
	col = strtol (ptr = (end += 2), &end, 10);
	if (end == ptr || end[0] != ']' || end[1] != '\0')
		return TRUE;

	if (row == 0 && col == 0) {
		*expr_end = '\0';
		xml_cell_set_array_expr (cell, content+2, rows, cols);
	}

	return FALSE;
}

static void
xml2ParseCellContent (XML2ParseState *state)
{
	gboolean is_new_cell, is_post_52_array = FALSE;
	Cell *cell;

	int const col = state->cell.col;
	int const row = state->cell.row;
	int const array_cols = state->array_cols;
	int const array_rows = state->array_rows;
	int const expr_id = state->expr_id;
	ValueType const value_type = state->value_type;
	char const *value_fmt =state->value_fmt;
	gpointer const id = GINT_TO_POINTER (expr_id);
	gpointer expr = NULL;

	/* Clean out the state before any error checking */
	state->cell.row = state->cell.col = -1;
	state->array_rows = state->array_cols = -1;
	state->expr_id = -1;
	state->value_type = 0;
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
		} else if (xml_not_used_old_array_spec (cell, content)) {
			if (value_type != 0) {
				Value *v = value_new_from_string (value_type, content);
				StyleFormat *sf = (value_fmt != NULL)
					? style_format_new_XL (value_fmt, FALSE)
					: NULL;
				cell_set_value (cell, v, sf);
			} else
				cell_set_text (cell, content);
		}

		if (expr_id > 0) {
			if (expr == NULL) {
				if (cell_has_expr (cell))
					g_hash_table_insert (state->expr_map, id,
							     cell->base.expression);
				else
					g_warning ("XML-IO2 : Shared expression with no expession ??");
			} else if (!is_post_52_array)
				g_warning ("XML-IO : Duplicate shared expression");
		}
	} else if (expr_id > 0) {
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
xml2ParseObject (XML2ParseState *state, CHAR const **attrs)
{
}

/****************************************************************************/

static gboolean
xml2SwitchState (XML2ParseState *state, CHAR const *name, xml2State const newState)
{
	if (strcmp (name, xml2_state_names[newState]))
		    return FALSE;

	state->state_stack = g_slist_prepend (state->state_stack, GINT_TO_POINTER (state->state));
	state->state = newState;
	return TRUE;
}

static void
xml2UnknownState (XML2ParseState *state, CHAR const *name)
{
	if (state->unknown_depth++)
		return;
	g_warning ("Unexpected element '%s' in state %s.", name, xml2_state_names[state->state]);
}

/*
 * We parse and do some limited validation of the XML file, if this
 * passes, then we return TRUE
gboolean
xml2_file_probe (FileOpener const *fo, const gchar *file_name)
{
	return TRUE;
}
 */

static void
xml2StartElement (XML2ParseState *state, CHAR const *name, CHAR const **attrs)
{
	switch (state->state) {
	case STATE_START:
		if (xml2SwitchState (state, name, STATE_WB)) {
		} else
			xml2UnknownState (state, name);
		break;

	case STATE_WB :
		if (xml2SwitchState (state, name, STATE_WB_ATTRIBUTES)) {
		} else if (xml2SwitchState (state, name, STATE_WB_SUMMARY)) {
		} else if (xml2SwitchState (state, name, STATE_WB_GEOMETRY)) {
			xml2ParseWBView (state, attrs);
		} else if (xml2SwitchState (state, name, STATE_WB_SHEETS)) {
		} else if (xml2SwitchState (state, name, STATE_WB_VIEW))
			xml2ParseWBView (state, attrs);
		else
			xml2UnknownState (state, name);
		break;

	case STATE_WB_ATTRIBUTES :
		if (xml2SwitchState (state, name, STATE_WB_ATTRIBUTES_ELEM)) {
		} else
			xml2UnknownState (state, name);
		break;

	case STATE_WB_ATTRIBUTES_ELEM :
		if (xml2SwitchState (state, name, STATE_WB_ATTRIBUTES_ELEM_NAME)) {
		} else if (xml2SwitchState (state, name, STATE_WB_ATTRIBUTES_ELEM_TYPE)) {
		} else if (xml2SwitchState (state, name, STATE_WB_ATTRIBUTES_ELEM_VALUE)) {
		} else
			xml2UnknownState (state, name);
		break;

	case STATE_WB_SUMMARY :
		if (xml2SwitchState (state, name, STATE_WB_SUMMARY_ITEM)) {
		} else
			xml2UnknownState (state, name);
		break;

	case STATE_WB_SUMMARY_ITEM :
		if (xml2SwitchState (state, name, STATE_WB_SUMMARY_ITEM_NAME)) {
		} else if (xml2SwitchState (state, name, STATE_WB_SUMMARY_ITEM_VALUE_STR)) {
		} else if (xml2SwitchState (state, name, STATE_WB_SUMMARY_ITEM_VALUE_INT)) {
		} else
			xml2UnknownState (state, name);
		break;

	case STATE_WB_SHEETS :
		if (xml2SwitchState (state, name, STATE_SHEET)) {
			xml2ParseSheet (state, attrs);
		} else
			xml2UnknownState (state, name);
		break;

	case STATE_SHEET :
		if (xml2SwitchState (state, name, STATE_SHEET_NAME)) {
		} else if (xml2SwitchState (state, name, STATE_SHEET_MAXCOL)) {
		} else if (xml2SwitchState (state, name, STATE_SHEET_MAXROW)) {
		} else if (xml2SwitchState (state, name, STATE_SHEET_ZOOM)) {
		} else if (xml2SwitchState (state, name, STATE_SHEET_PRINTINFO)) {
		} else if (xml2SwitchState (state, name, STATE_SHEET_STYLES)) {
		} else if (xml2SwitchState (state, name, STATE_SHEET_COLS)) {
		} else if (xml2SwitchState (state, name, STATE_SHEET_ROWS)) {
		} else if (xml2SwitchState (state, name, STATE_SHEET_SELECTIONS)) {
			xml2ParseSelection (state, attrs);
		} else if (xml2SwitchState (state, name, STATE_SHEET_CELLS)) {
		} else if (xml2SwitchState (state, name, STATE_SHEET_SOLVER)) {
		} else if (xml2SwitchState (state, name, STATE_SHEET_OBJECTS)) {
		} else
			xml2UnknownState (state, name);
		break;

	case STATE_SHEET_PRINTINFO :
		if (xml2SwitchState (state, name, STATE_PRINT_MARGINS)) {
		} else if (xml2SwitchState (state, name, STATE_PRINT_VCENTER)) {
		} else if (xml2SwitchState (state, name, STATE_PRINT_HCENTER)) {
		} else if (xml2SwitchState (state, name, STATE_PRINT_GRID)) {
		} else if (xml2SwitchState (state, name, STATE_PRINT_MONO)) {
		} else if (xml2SwitchState (state, name, STATE_PRINT_DRAFTS)) {
		} else if (xml2SwitchState (state, name, STATE_PRINT_TITLES)) {
		} else if (xml2SwitchState (state, name, STATE_PRINT_REPEAT_TOP)) {
		} else if (xml2SwitchState (state, name, STATE_PRINT_REPEAT_LEFT)) {
		} else if (xml2SwitchState (state, name, STATE_PRINT_ORDER)) {
		} else if (xml2SwitchState (state, name, STATE_PRINT_ORIENT)) {
		} else if (xml2SwitchState (state, name, STATE_PRINT_HEADER)) {
		} else if (xml2SwitchState (state, name, STATE_PRINT_FOOTER)) {
		} else if (xml2SwitchState (state, name, STATE_PRINT_PAPER)) {
		} else
			xml2UnknownState (state, name);
		break;

	case STATE_PRINT_MARGINS :
		if (xml2SwitchState (state, name, STATE_PRINT_MARGIN_TOP) ||
		    xml2SwitchState (state, name, STATE_PRINT_MARGIN_BOTTOM) ||
		    xml2SwitchState (state, name, STATE_PRINT_MARGIN_LEFT) ||
		    xml2SwitchState (state, name, STATE_PRINT_MARGIN_RIGHT) ||
		    xml2SwitchState (state, name,
				     STATE_PRINT_MARGIN_HEADER) ||
		    xml2SwitchState (state, name, STATE_PRINT_MARGIN_FOOTER)) {
			xml2ParseMargin (state, attrs);
		} else
			xml2UnknownState (state, name);
		break;

	case STATE_SHEET_STYLES :
		if (xml2SwitchState (state, name, STATE_STYLE_REGION))
			xml2ParseStyleRegion (state, attrs);
		else
			xml2UnknownState (state, name);
		break;

	case STATE_STYLE_REGION :
		if (xml2SwitchState (state, name, STATE_STYLE_STYLE))
			xml2ParseStyleRegionStyle (state, attrs);
		else
			xml2UnknownState (state, name);
		break;

	case STATE_STYLE_STYLE :
		if (xml2SwitchState (state, name, STATE_STYLE_FONT))
			xml2ParseStyleRegionFont (state, attrs);
		else if (xml2SwitchState (state, name, STATE_STYLE_BORDER)) {
		} else
			xml2UnknownState (state, name);
		break;

	case STATE_STYLE_BORDER :
		if (xml2SwitchState (state, name, STATE_BORDER_TOP) ||
		    xml2SwitchState (state, name, STATE_BORDER_BOTTOM) ||
		    xml2SwitchState (state, name, STATE_BORDER_LEFT) ||
		    xml2SwitchState (state, name, STATE_BORDER_RIGHT) ||
		    xml2SwitchState (state, name, STATE_BORDER_DIAG) ||
		    xml2SwitchState (state, name, STATE_BORDER_REV_DIAG))
			xml2ParseStyleRegionBorders (state, attrs);
		else
			xml2UnknownState (state, name);
		break;

	case STATE_SHEET_COLS :
		if (xml2SwitchState (state, name, STATE_COL))
			xml2ParseColRow (state, attrs, TRUE);
		else
			xml2UnknownState (state, name);
		break;

	case STATE_SHEET_ROWS :
		if (xml2SwitchState (state, name, STATE_ROW))
			xml2ParseColRow (state, attrs, FALSE);
		else
			xml2UnknownState (state, name);
		break;

	case STATE_SHEET_SELECTIONS :
		if (xml2SwitchState (state, name, STATE_SELECTION))
			xml2ParseSelectionRange (state, attrs);
		else
			xml2UnknownState (state, name);
		break;

	case STATE_SHEET_CELLS :
		if (xml2SwitchState (state, name, STATE_CELL))
			xml2ParseCell (state, attrs);
		else
			xml2UnknownState (state, name);
		break;

	case STATE_CELL :
		if (!xml2SwitchState (state, name, STATE_CELL_CONTENT))
			xml2UnknownState (state, name);
		break;

	case STATE_SHEET_OBJECTS :
		if (xml2SwitchState (state, name, STATE_OBJECT_RECTANGLE) ||
		    xml2SwitchState (state, name, STATE_OBJECT_ELLIPSE) ||
		    xml2SwitchState (state, name, STATE_OBJECT_ARROW) ||
		    xml2SwitchState (state, name, STATE_OBJECT_LINE)) {
			xml2ParseObject (state, attrs);
		} else
			xml2UnknownState (state, name);
		break;

	case STATE_OBJECT_RECTANGLE :
	case STATE_OBJECT_ELLIPSE :
	case STATE_OBJECT_ARROW :
	case STATE_OBJECT_LINE :
		if (xml2SwitchState (state, name, STATE_OBJECT_POINTS)) {
		} else
			xml2UnknownState (state, name);
		break;

	default :
		break;
	};
}

static void
xml2EndElement (XML2ParseState *state, const CHAR *name)
{
	if (state->unknown_depth > 0) {
		state->unknown_depth--;
		return;
	}

	g_return_if_fail (state->state_stack != NULL);
	g_return_if_fail (!strcmp (name, xml2_state_names[state->state]));

	switch (state->state) {
	case STATE_SHEET :
		state->sheet = NULL;
		break;

	case STATE_WB_ATTRIBUTES_ELEM :
		xml2FinishParseAttr (state);
		break;

	case STATE_WB_ATTRIBUTES :
		wb_view_set_attributev (state->wb_view, state->attributes);
		xml2_free_arg_list (state->attributes);
		state->attributes = NULL;
		break;

	case STATE_SHEET_SELECTIONS :
		xml2FinishSelection (state);
		break;

	case STATE_STYLE_REGION :
		g_return_if_fail (state->style_range_init);
		g_return_if_fail (state->style != NULL);
		g_return_if_fail (state->sheet != NULL);

		sheet_style_set_range (state->sheet, &state->style_range, state->style);

		state->style_range_init = FALSE;
		state->style = NULL;
		break;

	case STATE_STYLE_FONT :
		xml2FinishStyleRegionFont (state);
		g_string_truncate(state->content, 0);
		break;

	case STATE_WB_ATTRIBUTES_ELEM_NAME :
	case STATE_WB_ATTRIBUTES_ELEM_TYPE :
	case STATE_WB_ATTRIBUTES_ELEM_VALUE :
		xml2_parse_attr_elem (state);
		g_string_truncate(state->content, 0);
		break;

	case STATE_WB_SUMMARY_ITEM_NAME :
	case STATE_WB_SUMMARY_ITEM_VALUE_STR :
	case STATE_WB_SUMMARY_ITEM_VALUE_INT :
		g_string_truncate(state->content, 0);
		break;

	case STATE_SHEET_NAME :
		xml2ParseSheetName (state);
		g_string_truncate(state->content, 0);
		break;

	case STATE_SHEET_ZOOM :
		xml2ParseSheetZoom (state);
		g_string_truncate(state->content, 0);
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
		g_string_truncate(state->content, 0);
		break;

	case STATE_CELL :
		if (state->cell.row >= 0 || state->cell.col >= 0)
			xml2ParseCellContent (state);
		break;

	case STATE_CELL_CONTENT :
		xml2ParseCellContent (state);
		g_string_truncate(state->content, 0);
		break;

	default :
		break;
	};

	/* pop the state stack */
	state->state = GPOINTER_TO_INT (state->state_stack->data);
	state->state_stack = g_slist_remove (state->state_stack, state->state_stack->data);
}

static void
xml2Characters (XML2ParseState *state, const CHAR *chars, int len)
{
	switch (state->state) {
	case STATE_WB_ATTRIBUTES_ELEM_NAME :
	case STATE_WB_ATTRIBUTES_ELEM_TYPE :
	case STATE_WB_ATTRIBUTES_ELEM_VALUE :
	case STATE_WB_SUMMARY_ITEM_NAME :
	case STATE_WB_SUMMARY_ITEM_VALUE_INT :
	case STATE_WB_SUMMARY_ITEM_VALUE_STR :
	case STATE_SHEET_NAME :
	case STATE_SHEET_ZOOM :
	case STATE_PRINT_MARGIN_TOP :
	case STATE_PRINT_MARGIN_BOTTOM :
	case STATE_PRINT_MARGIN_LEFT :
	case STATE_PRINT_MARGIN_RIGHT :
	case STATE_PRINT_MARGIN_HEADER :
	case STATE_PRINT_MARGIN_FOOTER :
	case STATE_PRINT_ORDER :
	case STATE_PRINT_ORIENT :
	case STATE_PRINT_PAPER :
	case STATE_STYLE_FONT :
	case STATE_CELL_CONTENT :
		while (len-- > 0)
			g_string_append_c (state->content, *chars++);

	default :
		break;
	};
}

static xmlEntityPtr
xml2GetEntity (XML2ParseState *state, const CHAR *name)
{
	return xmlGetPredefinedEntity (name);
}

static void
xml2Warning (XML2ParseState *state, const char *msg, ...)
{
	va_list args;

	va_start (args, msg);
	g_logv ("XML", G_LOG_LEVEL_WARNING, msg, args);
	va_end (args);
}

static void
xml2Error (XML2ParseState *state, const char *msg, ...)
{
	va_list args;

	va_start (args, msg);
	g_logv ("XML", G_LOG_LEVEL_CRITICAL, msg, args);
	va_end (args);
}

static void
xml2FatalError (XML2ParseState *state, const char *msg, ...)
{
	va_list args;

	va_start (args, msg);
	g_logv ("XML", G_LOG_LEVEL_ERROR, msg, args);
	va_end (args);
}

static void
xml2StartDocument (XML2ParseState *state)
{
	state->state = STATE_START;
	state->unknown_depth = 0;
	state->state_stack = NULL;

	state->sheet = NULL;
	state->version = -1;

	state->content = g_string_sized_new (128);

	state->attribute.name = state->attribute.value = NULL;
	state->attribute.type = -1;
	state->attributes = NULL;

	state->style_range_init = FALSE;
	state->style = NULL;

	state->cell.row = state->cell.col = -1;
	state->array_rows = state->array_cols = -1;
	state->expr_id = -1;
	state->value_type = 0;
	state->value_fmt = NULL;

	state->expr_map = g_hash_table_new (g_direct_hash, g_direct_equal);
}

static void
xml2EndDocument (XML2ParseState *state)
{
	g_string_free (state->content, TRUE);
	g_hash_table_destroy (state->expr_map);

	g_return_if_fail (state->state == STATE_START);
	g_return_if_fail (state->unknown_depth == 0);
}

static xmlSAXHandler xml2SAXParser = {
	0, /* internalSubset */
	0, /* isStandalone */
	0, /* hasInternalSubset */
	0, /* hasExternalSubset */
	0, /* resolveEntity */
	(getEntitySAXFunc)xml2GetEntity, /* getEntity */
	0, /* entityDecl */
	0, /* notationDecl */
	0, /* attributeDecl */
	0, /* elementDecl */
	0, /* unparsedEntityDecl */
	0, /* setDocumentLocator */
	(startDocumentSAXFunc)xml2StartDocument, /* startDocument */
	(endDocumentSAXFunc)xml2EndDocument, /* endDocument */
	(startElementSAXFunc)xml2StartElement, /* startElement */
	(endElementSAXFunc)xml2EndElement, /* endElement */
	0, /* reference */
	(charactersSAXFunc)xml2Characters, /* characters */
	0, /* ignorableWhitespace */
	0, /* processingInstruction */
	0, /* comment */
	(warningSAXFunc)xml2Warning, /* warning */
	(errorSAXFunc)xml2Error, /* error */
	(fatalErrorSAXFunc)xml2FatalError, /* fatalError */
};

void
xml2_file_open (FileOpener const *fo, IOContext *io_context,
                WorkbookView *wb_view, char const *filename)
{
	xmlParserCtxtPtr ctxt;
	XML2ParseState state;

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
		                            _("xmlCreateFileParserCtxt() failed.")));
		return;
	}
	ctxt->sax = &xml2SAXParser;
	ctxt->userData = &state;

	xmlParseDocument (ctxt);

	if (ctxt->wellFormed) {
		gnumeric_io_error_info_set (io_context,
		                            error_info_new_str (
		                            _("XML document not well formed!")));
	}
	ctxt->sax = NULL;
	xmlFreeParserCtxt (ctxt);
}
