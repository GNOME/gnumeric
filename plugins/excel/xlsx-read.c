/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * xlsx-read.c : Read MS Excel 2007 Office Open xml
 *
 * Copyright (C) 2006-2007 Jody Goldberg (jody@gnome.org)
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
#include "xlsx-utils.h"

#include "sheet-view.h"
#include "sheet-style.h"
#include "sheet-merge.h"
#include "sheet.h"
#include "ranges.h"
#include "style.h"
#include "style-border.h"
#include "style-color.h"
#include "style-conditions.h"
#include "gnm-format.h"
#include "cell.h"
#include "position.h"
#include "expr.h"
#include "expr-name.h"
#include "print-info.h"
#include "validation.h"
#include "input-msg.h"
#include "value.h"
#include "sheet-filter.h"
#include "hlink.h"
#include "selection.h"
#include "command-context.h"
#include "workbook-view.h"
#include "workbook.h"
#include "gutils.h"
#include "graph.h"
#include "sheet-object-graph.h"

#include <goffice/app/error-info.h>
#include <goffice/app/io-context.h>
#include <goffice/app/go-plugin.h>
#include <goffice/utils/datetime.h>
#include <goffice/utils/go-units.h>
#include <goffice/utils/go-marker.h>
#include <goffice/data/go-data-simple.h>

#include <goffice/graph/gog-object.h>
#include <goffice/graph/gog-chart.h>
#include <goffice/graph/gog-plot.h>
#include <goffice/graph/gog-series.h>
#include <goffice/graph/gog-style.h>
#include <goffice/graph/gog-data-set.h>

#include <gsf/gsf-libxml.h>
#include <gsf/gsf-input.h>
#include <gsf/gsf-infile.h>
#include <gsf/gsf-infile-zip.h>
#include <gsf/gsf-open-pkg-utils.h>

#include <glib/gi18n-lib.h>
#include <gmodule.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

/*****************************************************************************/

typedef enum {
	XLXS_TYPE_NUM,
	XLXS_TYPE_SST_STR,	/* 0 based index into sst */
	XLXS_TYPE_BOOL,
	XLXS_TYPE_ERR,
	XLXS_TYPE_INLINE_STR,	/* inline string */
	/* How is this different from inlineStr ?? */
	XLXS_TYPE_STR2
} XLSXValueType;
typedef enum {
	XLSX_PANE_TOP_LEFT	= 0,
	XLSX_PANE_TOP_RIGHT	= 1,
	XLSX_PANE_BOTTOM_LEFT	= 2,
	XLSX_PANE_BOTTOM_RIGHT	= 3
} XLSXPanePos;

typedef enum {
	XLSX_AXIS_UNKNOWN,
	XLSX_AXIS_CAT,
	XLSX_AXIS_VAL
} XLSXAxisType;
typedef struct {
	char	*id;
	GogAxis	*axis;
	GSList	*plots;
	XLSXAxisType type;
	GogObjectPosition compass;
	GogAxisPosition	  cross;

	gboolean deleted;	/* TODO how to use this */
} XLSXAxisInfo;

typedef struct {
	GsfInfile	*zip;

	IOContext	*context;	/* The IOcontext managing things */
	WorkbookView	*wb_view;	/* View for the new workbook */
	Workbook	*wb;		/* The new workbook */

	Sheet		 *sheet;	/* current sheet */
	GnmCellPos	  pos;		/* current cell */
	XLSXValueType	  pos_type;
	GnmValue	 *val;
	GnmExprTop const *texpr;
	GnmRange	  array;
	char		 *shared_id;
	GHashTable	 *shared_exprs;
	GnmConventions   *convs;

	SheetView	*sv;		/* current sheetview */

	GArray		*sst;
	PangoAttrList	*rich_attrs;

	GHashTable	*num_fmts;
	GHashTable	*cell_styles;
	GPtrArray	*fonts;
	GPtrArray	*fills;
	GPtrArray	*borders;
	GPtrArray	*xfs;
	GPtrArray	*style_xfs;
	GPtrArray	*dxfs;
	GPtrArray	*table_styles;
	GnmStyle	*style_accum;
	gboolean	 style_accum_partial;
	GnmStyleBorderType  border_style;
	GnmColor	*border_color;

	GPtrArray	*collection;	/* utility for the shared collection handlers */
	unsigned	 count;
	XLSXPanePos	 pane_pos;

	GnmStyleConditions *conditions;
	GSList		   *cond_regions;
	GnmStyleCond	    cond;

	GnmFilter	   *filter;
	int		    filter_cur_field;
	GSList		   *filter_items; /* an accumulator */

	GSList		   *validation_regions;
	GnmValidation	   *validation;
	GnmInputMsg	   *input_msg;

	GnmPageBreaks	   *page_breaks;

	/* Drawing state */
	SheetObject	   *so;
	gint64		    drawing_pos[8];
	int		    drawing_pos_flags;

	/* Charting state */
	GogGraph	 *graph;
	GogChart	 *chart;
	GogPlot		 *plot;
	GogSeries	 *series;
	int		  dim_type;
	GogObject	 *series_pt;
	gboolean	  series_pt_has_index;
	GogStyle	 *cur_style;
	GOColor		  gocolor;
	GOMarker	 *marker;
	GOMarkerShape	  marker_symbol;
	GogObject	 *cur_obj;
	GSList		 *obj_stack;
	unsigned int	  sp_type;
	char		 *chart_tx;

	struct {
		GogAxis *obj;
		int	 type;
		GHashTable *by_id;
		GHashTable *by_obj;
		XLSXAxisInfo *info;
	} axis;

	GHashTable *theme_colors;
} XLSXReadState;
typedef struct {
	GnmString	*str;
	GOFormat	*markup;
} XLSXStr;

enum {
	XL_NS_SS,
	XL_NS_SS_DRAW,
	XL_NS_CHART,
	XL_NS_DRAW,
	XL_NS_DOC_REL,
	XL_NS_PKG_REL
};

static GsfXMLInNS const xlsx_ns[] = {
	GSF_XML_IN_NS (XL_NS_SS,	"http://schemas.openxmlformats.org/spreadsheetml/2006/main"),		  /* Office 12 */
	GSF_XML_IN_NS (XL_NS_SS,	"http://schemas.openxmlformats.org/spreadsheetml/2006/7/main"),		  /* Office 12 BETA-2 Technical Refresh */
	GSF_XML_IN_NS (XL_NS_SS,	"http://schemas.openxmlformats.org/spreadsheetml/2006/5/main"),		  /* Office 12 BETA-2 */
	GSF_XML_IN_NS (XL_NS_SS,	"http://schemas.microsoft.com/office/excel/2006/2"),			  /* Office 12 BETA-1 Technical Refresh */
	GSF_XML_IN_NS (XL_NS_SS_DRAW,	"http://schemas.openxmlformats.org/drawingml/2006/spreadsheetDrawing"),	  /* Office 12 BETA-2 */
	GSF_XML_IN_NS (XL_NS_SS_DRAW,	"http://schemas.openxmlformats.org/drawingml/2006/3/spreadsheetDrawing"), /* Office 12 BETA-2 Technical Refresh */
	GSF_XML_IN_NS (XL_NS_CHART,	"http://schemas.openxmlformats.org/drawingml/2006/3/chart"),		  /* Office 12 BETA-2 */
	GSF_XML_IN_NS (XL_NS_CHART,	"http://schemas.openxmlformats.org/drawingml/2006/chart"),		  /* Office 12 BETA-2 Technical Refresh */
	GSF_XML_IN_NS (XL_NS_DRAW,	"http://schemas.openxmlformats.org/drawingml/2006/3/main"),		  /* Office 12 BETA-2 */
	GSF_XML_IN_NS (XL_NS_DRAW,	"http://schemas.openxmlformats.org/drawingml/2006/main"),		  /* Office 12 BETA-2 Technical Refresh */
	GSF_XML_IN_NS (XL_NS_DOC_REL,	"http://schemas.openxmlformats.org/officeDocument/2006/relationships"),
	GSF_XML_IN_NS (XL_NS_PKG_REL,	"http://schemas.openxmlformats.org/package/2006/relationships"),
	{ NULL }
};

static gboolean
xlsx_parse_stream (XLSXReadState *state, GsfInput *in, GsfXMLInNode const *dtd)
{
	gboolean  success = FALSE;

	if (NULL != in) {
		GsfXMLInDoc *doc = gsf_xml_in_doc_new (dtd, xlsx_ns);

		success = gsf_xml_in_doc_parse (doc, in, state);

		if (!success)
			gnm_io_warning (state->context,
				_("'%s' is corrupt!"),
				gsf_input_name (in));

		gsf_xml_in_doc_free (doc);
		g_object_unref (G_OBJECT (in));
	}
	return success;
}

static void
xlsx_parse_rel_by_id (GsfXMLIn *xin, char const *part_id,
		      GsfXMLInNode const *dtd,
		      GsfXMLInNS const *ns)
{
	GError *err = gsf_open_pkg_parse_rel_by_id (xin, part_id, dtd, ns);
	if (NULL != err) {
		XLSXReadState *state = (XLSXReadState *)xin->user_state;
		gnm_io_warning (state->context, "%s", err->message);
		g_error_free (err);
	}
}

/****************************************************************************/

static gboolean xlsx_warning (GsfXMLIn *xin, char const *fmt, ...)
	G_GNUC_PRINTF (2, 3);

static gboolean
xlsx_warning (GsfXMLIn *xin, char const *fmt, ...)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
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
	g_printerr ("%s\n", msg);
	g_free (msg);

	return FALSE; /* convenience */
}

typedef struct {
	char const * const name;
	int val;
} EnumVal;

static gboolean
attr_enum (GsfXMLIn *xin, xmlChar const **attrs,
	   char const *target, EnumVal const *enums,
	   int *res)
{
	g_return_val_if_fail (attrs != NULL, FALSE);
	g_return_val_if_fail (attrs[0] != NULL, FALSE);
	g_return_val_if_fail (attrs[1] != NULL, FALSE);

	if (strcmp (attrs[0], target))
		return FALSE;

	for (; enums->name != NULL ; enums++)
		if (!strcmp (enums->name, attrs[1])) {
			*res = enums->val;
			return TRUE;
		}
	return xlsx_warning (xin,
		_("Unknown enum value '%s' for attribute %s"),
		attrs[1], target);
}

/**
 * Take an _int_ as a result to allow the caller to use -1 as an undefined state.
 **/
static gboolean
attr_bool (GsfXMLIn *xin, xmlChar const **attrs,
	   char const *target,
	   int *res)
{
	g_return_val_if_fail (attrs != NULL, FALSE);
	g_return_val_if_fail (attrs[0] != NULL, FALSE);
	g_return_val_if_fail (attrs[1] != NULL, FALSE);

	if (strcmp (attrs[0], target))
		return FALSE;

	*res = 0 == strcmp (attrs[1], "1");

	return TRUE;
}

static gboolean
attr_int (GsfXMLIn *xin, xmlChar const **attrs,
	  char const *target,
	  int *res)
{
	char *end;
	long tmp;

	g_return_val_if_fail (attrs != NULL, FALSE);
	g_return_val_if_fail (attrs[0] != NULL, FALSE);
	g_return_val_if_fail (attrs[1] != NULL, FALSE);

	if (strcmp (attrs[0], target))
		return FALSE;

	errno = 0;
	tmp = strtol (attrs[1], &end, 10);
	if (errno == ERANGE)
		return xlsx_warning (xin,
			_("Integer '%s' is out of range, for attribute %s"),
			attrs[1], target);
	if (*end)
		return xlsx_warning (xin,
			_("Invalid integer '%s' for attribute %s"),
			attrs[1], target);

	*res = tmp;
	return TRUE;
}
static gboolean
attr_int64 (GsfXMLIn *xin, xmlChar const **attrs,
	    char const *target,
	    gint64 *res)
{
	char *end;
	gint64 tmp;

	g_return_val_if_fail (attrs != NULL, FALSE);
	g_return_val_if_fail (attrs[0] != NULL, FALSE);
	g_return_val_if_fail (attrs[1] != NULL, FALSE);

	if (strcmp (attrs[0], target))
		return FALSE;

	errno = 0;
	tmp = g_ascii_strtoll (attrs[1], &end, 10);
	if (errno == ERANGE)
		return xlsx_warning (xin,
			_("Integer '%s' is out of range, for attribute %s"),
			attrs[1], target);
	if (*end)
		return xlsx_warning (xin,
			_("Invalid integer '%s' for attribute %s"),
			attrs[1], target);

	*res = tmp;
	return TRUE;
}

static gboolean
attr_gocolor (GsfXMLIn *xin, xmlChar const **attrs,
	      char const *target,
	      GOColor *res)
{
	char *end;
	unsigned long rgb;

	g_return_val_if_fail (attrs != NULL, FALSE);
	g_return_val_if_fail (attrs[0] != NULL, FALSE);
	g_return_val_if_fail (attrs[1] != NULL, FALSE);

	if (strcmp (attrs[0], target))
		return FALSE;

	errno = 0;
	rgb = strtoul (attrs[1], &end, 16);
	if (errno == ERANGE || *end)
		return xlsx_warning (xin,
			_("Invalid RRGGBB color '%s' for attribute %s"),
			attrs[1], target);

	{
		guint8 const r = (rgb >> 16) & 0xff;
		guint8 const g = (rgb >>  8) & 0xff;
		guint8 const b = (rgb >>  0) & 0xff;
		*res = RGBA_TO_UINT(r, g, b, 0xff);
	}

	return TRUE;
}

static gboolean
attr_float (GsfXMLIn *xin, xmlChar const **attrs,
	    char const *target,
	    gnm_float *res)
{
	char *end;
	double tmp;

	g_return_val_if_fail (attrs != NULL, FALSE);
	g_return_val_if_fail (attrs[0] != NULL, FALSE);
	g_return_val_if_fail (attrs[1] != NULL, FALSE);

	if (strcmp (attrs[0], target))
		return FALSE;

	tmp = gnm_strto (attrs[1], &end);
	if (*end)
		return xlsx_warning (xin,
			_("Invalid number '%s' for attribute %s"),
			attrs[1], target);
	*res = tmp;
	return TRUE;
}

static gboolean
attr_pos (GsfXMLIn *xin, xmlChar const **attrs,
	  char const *target,
	  GnmCellPos *res)
{
	char const *end;
	GnmCellPos tmp;

	g_return_val_if_fail (attrs != NULL, FALSE);
	g_return_val_if_fail (attrs[0] != NULL, FALSE);
	g_return_val_if_fail (attrs[1] != NULL, FALSE);

	if (strcmp (attrs[0], target))
		return FALSE;

	end = cellpos_parse (attrs[1], &tmp, TRUE);
	if (NULL == end || *end != '\0')
		return xlsx_warning (xin,
			_("Invalid cell position '%s' for attribute %s"),
			attrs[1], target);
	*res = tmp;
	return TRUE;
}

static gboolean
attr_range (GsfXMLIn *xin, xmlChar const **attrs,
	    char const *target,
	    GnmRange *res)
{
	g_return_val_if_fail (attrs != NULL, FALSE);
	g_return_val_if_fail (attrs[0] != NULL, FALSE);
	g_return_val_if_fail (attrs[1] != NULL, FALSE);

	if (strcmp (attrs[0], target))
		return FALSE;

	if (!range_parse (res, attrs[1]))
		xlsx_warning (xin, _("Invalid range '%s' for attribute %s"),
			attrs[1], target);
	return TRUE;
}

/***********************************************************************/

static gboolean
simple_bool (GsfXMLIn *xin, xmlChar const **attrs, int *res)
{
	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (attr_bool (xin, attrs, "val", res))
			return TRUE;
	return FALSE;
}
static gboolean
simple_int (GsfXMLIn *xin, xmlChar const **attrs, int *res)
{
	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (attr_int (xin, attrs, "val", res))
			return TRUE;
	return FALSE;
}
static gboolean
simple_float (GsfXMLIn *xin, xmlChar const **attrs, gnm_float *res)
{
	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (attr_float (xin, attrs, "val", res))
			return TRUE;
	return FALSE;
}

static gboolean
simple_enum (GsfXMLIn *xin, xmlChar const **attrs, EnumVal const *enums, int *res)
{
	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (attr_enum (xin, attrs, "val", enums, res))
			return TRUE;
	return FALSE;
}

/***********************************************************************
 * These indexes look like the values in xls.  Dup some code from there.
 * TODO : Can we merge the code ?
 *	  Will the 'indexedColors' look like a palette ?
 */
static struct {
	guint8 r, g, b;
} excel_default_palette_v8 [] = {
	{  0,  0,  0}, {255,255,255}, {255,  0,  0}, {  0,255,  0},
	{  0,  0,255}, {255,255,  0}, {255,  0,255}, {  0,255,255},

	{128,  0,  0}, {  0,128,  0}, {  0,  0,128}, {128,128,  0},
	{128,  0,128}, {  0,128,128}, {192,192,192}, {128,128,128},

	{153,153,255}, {153, 51,102}, {255,255,204}, {204,255,255},
	{102,  0,102}, {255,128,128}, {  0,102,204}, {204,204,255},

	{  0,  0,128}, {255,  0,255}, {255,255,  0}, {  0,255,255},
	{128,  0,128}, {128,  0,  0}, {  0,128,128}, {  0,  0,255},

	{  0,204,255}, {204,255,255}, {204,255,204}, {255,255,153},
	{153,204,255}, {255,153,204}, {204,153,255}, {255,204,153},

	{ 51,102,255}, { 51,204,204}, {153,204,  0}, {255,204,  0},
	{255,153,  0}, {255,102,  0}, {102,102,153}, {150,150,150},

	{  0, 51,102}, { 51,153,102}, {  0, 51,  0}, { 51, 51,  0},
	{153, 51,  0}, {153, 51,102}, { 51, 51,153}, { 51, 51, 51}
};

static GnmColor *
indexed_color (gint idx)
{
	/* NOTE: not documented but seems close
	 * If you find a normative reference please forward it.
	 *
	 * The color index field seems to use
	 *	8-63 = Palette index 0-55
	 *	64  = auto pattern, auto border
	 *      65  = auto background
	 *      127 = auto font
	 *
	 *      65 is always white, and 127 always black. 64 is black
	 *      if the fDefaultHdr flag in WINDOW2 is unset, otherwise it's
	 *      the grid color from WINDOW2.
	 */

	if (idx == 1 || idx == 65)
		return style_color_white ();
	switch (idx) {
	case 0:   /* black */
	case 64 : /* system text ? */
	case 81 : /* tooltip text */
	case 0x7fff : /* system text ? */
		return style_color_black ();
	case 1 :  /* white */
	case 65 : /* system back ? */
		return style_color_white ();

	case 80 : /* tooltip background */
		return style_color_new_gdk (&gs_yellow);

	case 2 : return style_color_new_i8 (0xff,    0,    0); /* red */
	case 3 : return style_color_new_i8 (   0, 0xff,    0); /* green */
	case 4 : return style_color_new_i8 (   0,    0, 0xff); /* blue */
	case 5 : return style_color_new_i8 (0xff, 0xff,    0); /* yellow */
	case 6 : return style_color_new_i8 (0xff,    0, 0xff); /* magenta */
	case 7 : return style_color_new_i8 (   0, 0xff, 0xff); /* cyan */
	default :
		 break;
	}

	idx -= 8;
	if (idx < 0 || (int) G_N_ELEMENTS (excel_default_palette_v8) <= idx) {
		g_warning ("EXCEL: color index (%d) is out of range (8..%d). Defaulting to black",
			   idx + 8, (int)G_N_ELEMENTS (excel_default_palette_v8) + 8);
		return style_color_black ();
	}

	/* TODO cache and ref */
	return style_color_new_i8 (excel_default_palette_v8[idx].r,
				   excel_default_palette_v8[idx].g,
				   excel_default_palette_v8[idx].b);
}

static GOFormat *
xlsx_get_num_fmt (GsfXMLIn *xin, char const *id)
{
	static char const * const std_builtins[] = {
		/* 0 */	 "General",
		/* 1 */	 "0",
		/* 2 */	 "0.00",
		/* 3 */	 "#,##0",
		/* 4 */	 "#,##0.00",
		/* 5 */	 NULL,
		/* 6 */	 NULL,
		/* 7 */	 NULL,
		/* 8 */	 NULL,
		/* 9 */  "0%",
		/* 10 */ "0.00%",
		/* 11 */ "0.00E+00",
		/* 12 */ "# ?/?",
		/* 13 */ "# ?""?/?""?",	/* silly trick to avoid using a trigraph */
		/* 14 */ "mm-dd-yy",
		/* 15 */ "d-mmm-yy",
		/* 16 */ "d-mmm",
		/* 17 */ "mmm-yy",
		/* 18 */ "h:mm AM/PM",
		/* 19 */ "h:mm:ss AM/PM",
		/* 20 */ "h:mm",
		/* 21 */ "h:mm:ss",
		/* 22 */ "m/d/yy h:mm",
		/* 23 */ NULL,
		/* 24 */ NULL,
		/* 25 */ NULL,
		/* 26 */ NULL,
		/* 27 */ NULL,
		/* 28 */ NULL,
		/* 29 */ NULL,
		/* 30 */ NULL,
		/* 31 */ NULL,
		/* 32 */ NULL,
		/* 33 */ NULL,
		/* 34 */ NULL,
		/* 35 */ NULL,
		/* 36 */ NULL,
		/* 37 */ "#,##0 ;(#,##0)",
		/* 38 */ "#,##0 ;[Red](#,##0)",
		/* 39 */ "#,##0.00;(#,##0.00)",
		/* 40 */ "#,##0.00;[Red](#,##0.00)",
		/* 41 */ NULL,
		/* 42 */ NULL,
		/* 43 */ NULL,
		/* 44 */ NULL,
		/* 45 */ "mm:ss",
		/* 46 */ "[h]:mm:ss",
		/* 47 */ "mmss.0",
		/* 48 */ "##0.0E+0",
		/* 49 */ "@"
	};

#if 0
	CHT						CHS
27 [$-404]e/m/d					yyyy"5E74"m"6708"
28 [$-404]e"5E74"m"6708"d"65E5"			m"6708"d"65E5"
29 [$-404]e"5E74"m"6708"d"65E5"			m"6708"d"65E5"
30 m/d/yy					m-d-yy
31 yyyy"5E74"m"6708"d"65E5"			yyyy"5E74"m"6708"d"65E5"
32 hh"6642"mm"5206"				h"65F6"mm"5206"
33 hh"6642"mm"5206"ss"79D2"			h"65F6"mm"5206"ss"79D2"
34 4E0A5348/4E0B5348hh"6642"mm"5206"		4E0A5348/4E0B5348h"65F6"mm"5206"
35 4E0A5348/4E0B5348hh"6642"mm"5206"ss"79D2"	4E0A5348/4E0B5348h"65F6"mm"5206"ss"79D2"
36 [$-404]e/m/d					yyyy"5E74"m"6708"
50 [$-404]e/m/d					yyyy"5E74"m"6708"
51 [$-404]e"5E74"m"6708"d"65E5"			m"6708"d"65E5"
52 4E0A5348/4E0B5348hh"6642"mm"5206"		yyyy"5E74"m"6708"
53 4E0A5348/4E0B5348hh"6642"mm"5206"ss"79D2"	m"6708"d"65E5"
54 [$-404]e"5E74"m"6708"d"65E5"			m"6708"d"65E5"
55 4E0A5348/4E0B5348hh"6642"mm"5206"		4E0A5348/4E0B5348h"65F6"mm"5206"
56 4E0A5348/4E0B5348hh"6642"mm"5206"ss"79D2"	4E0A5348/4E0B5348h"65F6"mm"5206"ss"79D2"
57 [$-404]e/m/d					yyyy"5E74"m"6708"
58 [$-404]e"5E74"m"6708"d"65E5"			m"6708"d"65E5"

	JPN						KOR
27 [$-411]ge.m.d				yyyy"5E74" mm"6708" dd"65E5"
28 [$-411]ggge"5E74"m"6708"d"65E5"		mm-dd
29 [$-411]ggge"5E74"m"6708"d"65E5"		mm-dd
30 m/d/yy					mm-dd-yy
31 yyyy"5E74"m"6708"d"65E5"			yyyy"B144" mm"C6D4" dd"C77C"
32 h"6642"mm"5206"				h"C2DC" mm"BD84"
33 h"6642"mm"5206"ss"79D2"			h"C2DC" mm"BD84" ss"CD08"
34 yyyy"5E74"m"6708"				yyyy-mm-dd
35 m"6708"d"65E5"				yyyy-mm-dd
36 [$-411]ge.m.d				yyyy"5E74" mm"6708" dd"65E5"
50 [$-411]ge.m.d				yyyy"5E74" mm"6708" dd"65E5"
51 [$-411]ggge"5E74"m"6708"d"65E5"		mm-dd
52 yyyy"5E74"m"6708"				yyyy-mm-dd
53 m"6708"d"65E5"				yyyy-mm-dd
54 [$-411]ggge"5E74"m"6708"d"65E5"		mm-dd
55 yyyy"5E74"m"6708"				yyyy-mm-dd
56 m"6708"d"65E5"				yyyy-mm-dd
57 [$-411]ge.m.d				yyyy"5E74" mm"6708" dd"65E5"
58 [$-411]ggge"5E74"m"6708"d"65E5"		mm-dd

	THA
59 "t0"
60 "t0.00"
61 "t#,##0"
62 "t#,##0.00"
67 "t0%"
68 "t0.00%"
69 "t# ?/?"
70 "t# ?""?/?""?" /* silly trick to avoid using a trigraph */
71 0E27/0E14/0E1B0E1B0E1B0E1B
72 0E27-0E140E140E14-0E1B0E1B
73 0E27-0E140E140E14
74 0E140E140E14-0E1B0E1B
75 0E0A:0E190E19
76 0E0A:0E190E19:0E170E17
77 0E27/0E14/0E1B0E1B0E1B0E1B 0E0A:0E190E19
78 0E190E19:0E170E17
79 [0E0A]:0E190E19:0E170E17
80 0E190E19:0E170E17.0
81 d/m/bb
#endif

	XLSXReadState	*state = (XLSXReadState *)xin->user_state;
	GOFormat *res = g_hash_table_lookup (state->num_fmts, id);
	char *end;
	long i;

	if (NULL != res)
		return res;

	/* builtins */
	i = strtol (id, &end, 10);
	if (end != id && *end == '\0' &&
	    i >= 0 && i < (int) G_N_ELEMENTS (std_builtins) &&
	    std_builtins[i] != NULL) {
		res = go_format_new_from_XL (std_builtins[i]);
		g_hash_table_replace (state->num_fmts, g_strdup (id), res);
	} else
		xlsx_warning (xin, _("Undefined number format id '%s'"), id);
	return res;
}

static GnmExprTop const *
xlsx_parse_expr (GsfXMLIn *xin, xmlChar const *expr_str,
		 GnmParsePos const *pp)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	GnmParseError err;
	GnmExprTop const *texpr;

	/* Odd, some time IF and CHOOSE show up with leading spaces ??
	 * = IF(....
	 * = CHOOSE(...
	 * I wonder if it is related to some of the funky old
	 * optimizations in * xls ? */
	while (' ' == *expr_str)
		expr_str++;

	texpr = gnm_expr_parse_str (expr_str, pp,
		GNM_EXPR_PARSE_DEFAULT, state->convs,
		parse_error_init (&err));
	if (NULL == texpr)
		xlsx_warning (xin, "'%s' %s", expr_str, err.err->message);
	parse_error_free (&err);

	return texpr;
}

/* Returns: a GSList of GnmRange in _reverse_ order
 * caller frees the list and the content */
static GSList *
xlsx_parse_sqref (GsfXMLIn *xin, xmlChar const *refs)
{
	GnmRange  r;
	xmlChar const *tmp;
	GSList	 *res = NULL;

	while (NULL != refs && *refs) {
		if (NULL == (tmp = cellpos_parse (refs, &r.start, FALSE))) {
			xlsx_warning (xin, "unable to parse reference list '%s'", refs);
			return res;
		}

		refs = tmp;
		if (*refs == '\0' || *refs == ' ')
			r.end = r.start;
		else if (*refs != ':' ||
			 NULL == (tmp = cellpos_parse (refs + 1, &r.end, FALSE))) {
			xlsx_warning (xin, "unable to parse reference list '%s'", refs);
			return res;
		}

		range_normalize (&r); /* be anal */
		res = g_slist_prepend (res, range_dup (&r));

		for (refs = tmp ; *refs == ' ' ; refs++ ) ;
	}

	return res;
}

/***********************************************************************/

static void
xlsx_chart_push_obj (XLSXReadState *state, GogObject *obj)
{
	state->obj_stack = g_slist_prepend (state->obj_stack, state->cur_obj);
	state->cur_obj = obj;

#if 0
	g_print ("push %s\n", G_OBJECT_TYPE_NAME (obj));
#endif
}

static void
xlsx_chart_pop_obj (XLSXReadState *state)
{
	GSList *obj_stack = state->obj_stack;
	g_return_if_fail (obj_stack != NULL);

#if 0
	g_print ("push %s\n", G_OBJECT_TYPE_NAME (state->cur_obj));
#endif

	state->cur_obj = obj_stack->data;
	state->obj_stack = g_slist_remove (state->obj_stack, state->cur_obj);
}

static void
xlsx_chart_add_plot (GsfXMLIn *xin, char const *type)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	if (NULL != (state->plot = (GogPlot*) gog_plot_new_by_name (type)))
		/* Add _before_ setting styles so theme does not override */
		gog_object_add_by_name (GOG_OBJECT (state->chart),
			"Plot", GOG_OBJECT (state->plot));
}

/* shared with pie of pie, and bar of pie */
static void
xlsx_vary_colors (GsfXMLIn *xin, xmlChar const **attrs)
{
	XLSXReadState	*state = (XLSXReadState *)xin->user_state;
	int vary;
	if (simple_bool (xin, attrs, &vary))
		g_object_set (G_OBJECT (state->plot),
			"vary-style-by-element", vary, NULL);
}

static void
xlsx_chart_pie_sep (GsfXMLIn *xin, xmlChar const **attrs)
{
	XLSXReadState	*state = (XLSXReadState *)xin->user_state;
	int sep;
	if (simple_int (xin, attrs, &sep))
		g_object_set (G_OBJECT (state->plot),
			"default-separation", (double)(CLAMP (sep, 0, 500))/ 100., NULL);
}

/* shared with pie of pie, and bar of pie */
static void xlsx_chart_pie (GsfXMLIn *xin, xmlChar const **attrs) { xlsx_chart_add_plot (xin, "GogPiePlot"); }
static void xlsx_chart_ring (GsfXMLIn *xin, xmlChar const **attrs) { xlsx_chart_add_plot (xin, "GogRingPlot"); }

/***********************************************************************/

static void
xlsx_chart_bar_dir (GsfXMLIn *xin, xmlChar const **attrs)
{
	static EnumVal const dirs[] = {
		{ "bar",	 TRUE },
		{ "col",	 FALSE },
		{ NULL, 0 }
	};
	XLSXReadState	*state = (XLSXReadState *)xin->user_state;
	int dir;

	g_return_if_fail (state->plot != NULL);

	if (simple_enum (xin, attrs, dirs, &dir))
		g_object_set (G_OBJECT (state->plot), "horizontal", dir, NULL);
}

static void
xlsx_chart_bar_overlap (GsfXMLIn *xin, xmlChar const **attrs)
{
	XLSXReadState	*state = (XLSXReadState *)xin->user_state;
	int overlap;
	g_return_if_fail (state->plot != NULL);
	if (simple_int (xin, attrs, &overlap))
		g_object_set (G_OBJECT (state->plot),
			"overlap-percentage", CLAMP (overlap, -100, 100), NULL);
}
static void
xlsx_chart_bar_group (GsfXMLIn *xin, xmlChar const **attrs)
{
	XLSXReadState	*state = (XLSXReadState *)xin->user_state;
	char const *type = "normal";

	g_return_if_fail (state->plot != NULL);

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (0 == strcmp (attrs[0], "val")) {
			if (0 == strcmp (attrs[1], "percentStacked"))
				type = "as_percentage";
			else if (0 == strcmp (attrs[1], "stacked"))
				type = "stacked";
			g_object_set (G_OBJECT (state->plot), "type", type, NULL);
		}
}
static void
xlsx_chart_bar_gap (GsfXMLIn *xin, xmlChar const **attrs)
{
	XLSXReadState	*state = (XLSXReadState *)xin->user_state;
	int gap;
	if (simple_int (xin, attrs, &gap))
		g_object_set (G_OBJECT (state->plot),
			"gap-percentage", CLAMP (gap, 0, 500), NULL);
}

static void
xlsx_chart_bar (GsfXMLIn *xin, xmlChar const **attrs)
{
	xlsx_chart_add_plot (xin, "GogBarColPlot");
}

/***********************************************************************/

static void
xlsx_axis_info_free (XLSXAxisInfo *info)
{
	g_free (info->id);
	if (NULL != info->axis)
		g_object_unref (info->axis);
	g_free (info);
}

static void
xlsx_plot_axis_id (GsfXMLIn *xin, xmlChar const **attrs)
{
	XLSXReadState	*state = (XLSXReadState *)xin->user_state;
	if (NULL == state->plot)
		return;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (0 == strcmp (attrs[0], "val")) {
			XLSXAxisInfo *res = g_hash_table_lookup (state->axis.by_id, attrs[1]);
			if (NULL == res) {
				res = g_new0 (XLSXAxisInfo, 1);
				res->id = g_strdup (attrs[1]);
				res->axis	= NULL;
				res->plots	= NULL;
				res->type	= XLSX_AXIS_UNKNOWN;
				res->compass	= GOG_POSITION_AUTO;
				res->cross	= GOG_AXIS_CROSS;
				g_hash_table_replace (state->axis.by_id, res->id, res);
#ifdef DEBUG_AXIS
				g_print ("create %s = %p\n", attrs[1], res);
#endif
			}
#ifdef DEBUG_AXIS
			g_print ("add plot %p to %p\n", state->plot, res);
#endif
			res->plots = g_slist_prepend (res->plots, state->plot);
		}
}

static void
xlsx_axis_start (GsfXMLIn *xin, xmlChar const **attrs)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	state->axis.obj	 = g_object_new (GOG_AXIS_TYPE, NULL);
	state->axis.type = xin->node->user_data.v_int;
	state->axis.info = NULL;
	xlsx_chart_push_obj (state, GOG_OBJECT (state->axis.obj));
}
static void
xlsx_axis_id (GsfXMLIn *xin, xmlChar const **attrs)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (0 == strcmp (attrs[0], "val")) {
			state->axis.info = g_hash_table_lookup (state->axis.by_id, attrs[1]);
			if (NULL != state->axis.info) {
				g_return_if_fail (state->axis.info->axis == NULL);
				state->axis.info->axis = state->axis.obj;
				g_hash_table_replace (state->axis.by_obj,
					state->axis.obj, state->axis.info);
			}
#ifdef DEBUG_AXIS
			g_print ("define %s = %p\n", attrs[1], state->axis.info);
#endif
		}
}

static void
xlsx_axis_delete (GsfXMLIn *xin, xmlChar const **attrs)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	int del;
	if (state->axis.info && simple_bool (xin, attrs, &del))
		state->axis.info->deleted = del;
}
static void
xlsx_axis_orientation (GsfXMLIn *xin, xmlChar const **attrs)
{
	static EnumVal const orients[] = {
		{ "minMax",	 FALSE },
		{ "maxMin",	 TRUE },
		{ NULL, 0 }
	};
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	int orient;
	if (state->axis.info && simple_enum (xin, attrs, orients, &orient))
		g_object_set (G_OBJECT (state->axis.obj),
			"invert-axis", orient, NULL);
}
static void
xlsx_chart_logbase (GsfXMLIn *xin, xmlChar const **attrs)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	int base;
	if (state->axis.info && simple_int (xin, attrs, &base))
		g_object_set (G_OBJECT (state->axis.obj),
			"map-name", "Log", NULL);
}
static void
xlsx_axis_pos (GsfXMLIn *xin, xmlChar const **attrs)
{
	static EnumVal const positions[] = {
		{ "t",	 GOG_POSITION_N },
		{ "b",	 GOG_POSITION_S },
		{ "l",	 GOG_POSITION_W },
		{ "r",	 GOG_POSITION_E },
		{ NULL, 0 }
	};
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	int position;
#ifdef DEBUG_AXIS
	g_print ("SET POS %s for %p\n", attrs[1],  state->axis.info);
#endif
	if (state->axis.info && simple_enum (xin, attrs, positions, &position))
		state->axis.info->compass = position;
}

static void
xlsx_axis_bound (GsfXMLIn *xin, xmlChar const **attrs)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	double val;
	if (state->axis.info && simple_float (xin, attrs, &val))
		gog_dataset_set_dim (GOG_DATASET (state->axis.obj),
			xin->node->user_data.v_int,
			go_data_scalar_val_new (val), NULL);
}

static void
xlsx_axis_crosses (GsfXMLIn *xin, xmlChar const **attrs)
{
	static EnumVal const crosses[] = {
		{ "autoZero",	GOG_AXIS_CROSS },
		{ "max",	GOG_AXIS_AT_HIGH },
		{ "min",	GOG_AXIS_AT_LOW },
		{ NULL, 0 }
	};
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	int cross;

	if (state->axis.info && simple_enum (xin, attrs, crosses, &cross))
		state->axis.info->cross = cross;
}

static void
xlsx_chart_gridlines (GsfXMLIn *xin, xmlChar const **attrs)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	if (NULL != state->axis.obj) {
		GogObject *grid = gog_object_add_by_name (
			GOG_OBJECT (state->axis.obj), "MajorGrid", NULL);
		xlsx_chart_push_obj (state, grid);
	}
}

static void
xlsx_axis_end (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;

	/* Try to guess what type of axis to use */
	if (NULL != state->axis.info) {
		GogPlot *plot = state->axis.info->plots->data; /* just use the first */
		char const *type = G_OBJECT_TYPE_NAME (plot);
		char const *role = NULL;
		GSList *ptr;

		if (0 == strcmp (type, "GogRadarPlot") ||
		    0 == strcmp (type, "GogRadarAreaPlot")) {
			role = (state->axis.type == XLSX_AXIS_CAT) ? "Radial-Axis" : "Circular-Axis";
		} else if (0 == strcmp (type, "GogBubblePlot") ||
			   0 == strcmp (type, "GogXYPlot")) {
			/* both are VAL, use the position to decide */
			if (state->axis.info->compass  == GOG_POSITION_N ||
			    state->axis.info->compass  == GOG_POSITION_S)
				role = "X-Axis";
			else
				role = "Y-Axis";
		} else if (0 == strcmp (type, "GogBarColPlot")) {
			gboolean h;
			/* swap for bar plots */
			g_object_get (G_OBJECT (plot), "horizontal", &h, NULL);
			if (h)
				role = (state->axis.type == XLSX_AXIS_CAT) ? "Y-Axis" : "X-Axis";
		}

		if (NULL == role)
			role = (state->axis.type == XLSX_AXIS_CAT) ? "X-Axis" : "Y-Axis";

		/* absorb a ref, and set the id, and atype */
		gog_object_add_by_name (GOG_OBJECT (state->chart),
			role, GOG_OBJECT (state->axis.obj));
		g_object_ref (G_OBJECT (state->axis.obj));
		for (ptr = state->axis.info->plots; ptr != NULL ; ptr = ptr->next) {
#ifdef DEBUG_AXIS
			g_print ("connect plot %p to %p in role %s\n", ptr->data, state->axis.obj, role);
#endif
			gog_plot_set_axis (ptr->data, state->axis.obj);
		}

		state->axis.obj  = NULL;
		state->axis.info = NULL;
	}

	xlsx_chart_pop_obj (state);
	state->axis.info = NULL;
}

static void xlsx_chart_area (GsfXMLIn *xin, xmlChar const **attrs) { xlsx_chart_add_plot (xin, "GogAreaPlot"); }
static void xlsx_chart_line (GsfXMLIn *xin, xmlChar const **attrs) { xlsx_chart_add_plot (xin, "GogLinePlot"); }
static void xlsx_chart_xy (GsfXMLIn *xin, xmlChar const **attrs) { xlsx_chart_add_plot (xin, "GogXYPlot"); }
static void xlsx_chart_bubble (GsfXMLIn *xin, xmlChar const **attrs) { xlsx_chart_add_plot (xin, "GogBubblePlot"); }
static void xlsx_chart_radar (GsfXMLIn *xin, xmlChar const **attrs) { xlsx_chart_add_plot (xin, "GogRadarPlot"); }
#if 0
	char const *type = "GogRadarPlot";
	gboolean with_markers = FALSE;
	/* Irritants.  They put the sub type into a child record ... */
	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (0 == strcmp (attrs[0], "type")) {
			if (0 == strcmp (attrs[1], "filled"))
				type = "as_percentage";
			else if (0 == strcmp (attrs[1], "marker"))
				type = "stacked";
			g_object_set (G_OBJECT (state->plot), "type", type, NULL);
		}
		if (0 == strcmp (xin, attrs, "cx", state->drawing_pos + (COL | TO | OFFSET)))
			state->drawing_pos_flags |= (1 << (COL | TO | OFFSET));
	g_object_set (G_OBJECT (state->plot), "default-style-has-markers", with_markers, NULL);
#endif

static void
xlsx_plot_end (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	state->plot = NULL;
}

static void
xlsx_chart_ser_start (GsfXMLIn *xin, xmlChar const **attrs)
{
	XLSXReadState	*state = (XLSXReadState *)xin->user_state;
	if (NULL != state->plot) {
		state->series = gog_plot_new_series (state->plot);
		xlsx_chart_push_obj (state, GOG_OBJECT (state->series));
	}
}
static void
xlsx_chart_ser_end (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	XLSXReadState	*state = (XLSXReadState *)xin->user_state;
	if (NULL != state->series) {
		xlsx_chart_pop_obj (state);
		state->series = NULL;
	}
}

#warning shared from ms-chart.c for now, move to GOffice with the enum
extern void XL_gog_series_set_dim (GogSeries *series, GogMSDimType ms_type, GOData *val);
static void
xlsx_chart_ser_f (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	XLSXReadState	*state = (XLSXReadState *)xin->user_state;
	if (NULL != state->series) {
		GnmParsePos pp;
		GnmExprTop const *texpr = xlsx_parse_expr (xin, xin->content->str,
			parse_pos_init_sheet (&pp, state->sheet));

		XL_gog_series_set_dim (state->series, state->dim_type,
			(state->dim_type != GOG_MS_DIM_LABELS)
			? gnm_go_data_vector_new_expr (state->sheet, texpr)
			: gnm_go_data_scalar_new_expr (state->sheet, texpr));
	}
}

static void
xlsx_ser_type_start (GsfXMLIn *xin, xmlChar const **attrs)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	state->dim_type = xin->node->user_data.v_int;
}

static void
xlsx_ser_type_end (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	state->dim_type = GOG_MS_DIM_LABELS;
}

static void
xlsx_chart_legend (GsfXMLIn *xin, xmlChar const **attrs)
{
	XLSXReadState	*state = (XLSXReadState *)xin->user_state;
	gog_object_add_by_name (GOG_OBJECT (state->chart), "Legend", NULL);
}

static void
xlsx_chart_pt_start (GsfXMLIn *xin, xmlChar const **attrs)
{
	XLSXReadState	*state = (XLSXReadState *)xin->user_state;
	if (NULL != state->series) {
		state->series_pt_has_index = FALSE;
		state->series_pt = gog_object_add_by_name (
			GOG_OBJECT (state->series), "Point", NULL);
		xlsx_chart_push_obj (state, state->series_pt);
	}
}

static void
xlsx_chart_pt_end (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	if (NULL != state->series) {
		xlsx_chart_pop_obj (state);
		if (!state->series_pt_has_index) {
			gog_object_clear_parent (state->series_pt);
			g_object_unref (state->series_pt);
		}
		state->series_pt = NULL;
	}
}

static void
xlsx_chart_pt_index (GsfXMLIn *xin, xmlChar const **attrs)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	int tmp;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (attr_int (xin, attrs, "val", &tmp)) {
			state->series_pt_has_index = TRUE;
			g_object_set (state->series_pt, "index", tmp, NULL);
		}
}

static void
xlsx_chart_pt_sep (GsfXMLIn *xin, xmlChar const **attrs)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	int sep;
	if (simple_int (xin, attrs, &sep) &&
	    g_object_class_find_property (G_OBJECT_GET_CLASS (state->series_pt), "separation"))
		g_object_set (state->series_pt, "separation", (double)sep / 100., NULL);
}

static void
xlsx_chart_style_start (GsfXMLIn *xin, xmlChar const **attrs)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	if (NULL != state->cur_obj &&
	    IS_GOG_STYLED_OBJECT (state->cur_obj) &&
	    NULL == state->marker) {
		g_return_if_fail (state->cur_style == NULL);
		state->cur_style = gog_style_dup (
			gog_styled_object_get_style (GOG_STYLED_OBJECT (state->cur_obj)));
	}
}
static void
xlsx_chart_style_end (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	XLSXReadState	*state = (XLSXReadState *)xin->user_state;

	if (NULL != state->cur_style) {
		gog_styled_object_set_style (GOG_STYLED_OBJECT (state->cur_obj),
			state->cur_style);
		g_object_unref (state->cur_style);
		state->cur_style = NULL;
	}
}
static void
xlsx_style_line_start (GsfXMLIn *xin, xmlChar const **attrs)
{
	XLSXReadState	*state = (XLSXReadState *)xin->user_state;
	state->sp_type |= GOG_STYLE_LINE;
}

static void
xlsx_style_line_end (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	XLSXReadState	*state = (XLSXReadState *)xin->user_state;
	state->sp_type &= ~GOG_STYLE_LINE;
}

static void
xlsx_chart_no_fill (GsfXMLIn *xin, xmlChar const **attrs)
{
	XLSXReadState	*state = (XLSXReadState *)xin->user_state;
	if (NULL != state->marker)
		;
	else if (NULL != state->cur_style) {
		if (!(state->sp_type & GOG_STYLE_LINE)) {
			state->cur_style->fill.type = GOG_FILL_STYLE_NONE;
			state->cur_style->fill.auto_type = FALSE;
		}
	}
}
static void
xlsx_chart_solid_fill (GsfXMLIn *xin, xmlChar const **attrs)
{
	XLSXReadState	*state = (XLSXReadState *)xin->user_state;
	if (NULL != state->marker)
		;
	else if (NULL != state->cur_style) {
		if (!(state->sp_type & GOG_STYLE_LINE)) {
			state->cur_style->fill.type = GOG_FILL_STYLE_PATTERN;
			state->cur_style->fill.auto_type = FALSE;
		}
	}
}

static void
xlsx_draw_color_themed (GsfXMLIn *xin, xmlChar const **attrs)
{
	static EnumVal const colors[] = {
		{ "bg1",	 0 }, /* Background Color 1 */
		{ "tx1",	 1 }, /* Text Color 1 */
		{ "bg2",	 2 }, /* Background Color 2 */
		{ "tx2",	 3 }, /* Text Color 2 */
		{ "accent1",	 4 }, /* Accent Color 1 */
		{ "accent2",	 5 }, /* Accent Color 2 */
		{ "accent3",	 6 }, /* Accent Color 3 */
		{ "accent4",	 7 }, /* Accent Color 4 */
		{ "accent5",	 8 }, /* Accent Color 5 */
		{ "accent6",	 9 }, /* Accent Color 6 */
		{ "hlink",	10 }, /* Hyperlink Color */
		{ "folHlink",	11 }, /* Followed Hyperlink Color */
		{ "phClr",	12 }, /* Style Color */
		{ "dk1",	13 }, /* Dark Color 1 */
		{ "lt1",	14 }, /* Light Color 1 */
		{ "dk2",	15 }, /* Dark Color 2 */
		{ "lt2",	16 }, /* Light Color 2 */
		{ NULL, 0 }
	};

	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	gpointer val = NULL;
	if (NULL != state->theme_colors) {
		for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
			if (0 == strcmp (attrs[0], "val")) {
				val = g_hash_table_lookup (state->theme_colors, attrs[1]);
				if (NULL == val)
					xlsx_warning (xin, _("Unknown color '%s'"), attrs[1]);
			}
	} else
		xlsx_warning (xin, _("Missing theme"));

	state->gocolor = GPOINTER_TO_UINT (val);
}

static void
xlsx_draw_color_rgb (GsfXMLIn *xin, xmlChar const **attrs)
{
	XLSXReadState	*state = (XLSXReadState *)xin->user_state;
	if (NULL == state->cur_style)
		return;
	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (attr_gocolor (xin, attrs, "val", &state->gocolor))
			;
}

static void
xlsx_draw_color_alpha (GsfXMLIn *xin, xmlChar const **attrs)
{
	XLSXReadState	*state = (XLSXReadState *)xin->user_state;
	int val;
	if (simple_int (xin, attrs, &val)) {
		int level = 255 * val / 100000;
		state->gocolor = UINT_RGBA_CHANGE_A (state->gocolor, level);
	}
}

static void
xlsx_draw_color_end (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	XLSXReadState	*state = (XLSXReadState *)xin->user_state;
	if (NULL != state->marker)
		go_marker_set_fill_color (state->marker, state->gocolor);
	else if (NULL != state->cur_style) {
		if (state->sp_type & GOG_STYLE_LINE) {
			state->cur_style->line.color = state->gocolor;
			state->cur_style->line.auto_color = FALSE;
		} else {
			state->cur_style->fill.pattern.back = state->gocolor;
			state->cur_style->fill.pattern.fore = state->gocolor;
			state->cur_style->fill.auto_fore = FALSE;
			state->cur_style->fill.auto_back = FALSE;
		}
	}
}

static void
xlsx_draw_line_dash (GsfXMLIn *xin, xmlChar const **attrs)
{
	static EnumVal const dashes[] = {
		{ "solid",		GO_LINE_SOLID },
		{ "dot",		GO_LINE_DOT },
		{ "dash",		GO_LINE_DASH },
		{ "lgDash",		GO_LINE_LONG_DASH },
		{ "dashDot",		GO_LINE_DASH_DOT },
		{ "lgDashDot",		GO_LINE_DASH_DOT_DOT },
		{ "lgDashDotDot",	GO_LINE_DASH_DOT_DOT_DOT },
		{ "sysDash",		GO_LINE_S_DASH },
		{ "sysDot",		GO_LINE_S_DOT },
		{ "sysDashDot",		GO_LINE_S_DASH_DOT },
		{ "sysDashDotDot",	GO_LINE_S_DASH_DOT_DOT },
		{ NULL, 0 }
	};
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	int dash;

	if (!simple_enum (xin, attrs, dashes, &dash))
		return;

	if (NULL != state->marker)
		; /* what goes here ?*/
	else if (NULL != state->cur_style) {
		if (state->sp_type & GOG_STYLE_LINE) {
			state->cur_style->line.auto_dash = FALSE;
			state->cur_style->line.dash_type = dash;
			state->cur_style->outline.auto_dash = FALSE;
			state->cur_style->outline.dash_type = dash;
		} else {
			; /* what goes here ?*/
		}
	}
}

static void
xlsx_chart_marker_start (GsfXMLIn *xin, xmlChar const **attrs)
{
	XLSXReadState	*state = (XLSXReadState *)xin->user_state;
	state->marker = go_marker_new ();
	state->marker_symbol = GO_MARKER_MAX;
}

static void
xlsx_chart_marker_symbol (GsfXMLIn *xin, xmlChar const **attrs)
{
	static EnumVal const symbols[] = {
		{ "circle",	GO_MARKER_CIRCLE },
		{ "dash",	GO_MARKER_BAR },		/* FIXME */
		{ "diamond",	GO_MARKER_DIAMOND },
		{ "dot",	GO_MARKER_HALF_BAR },		/* FIXME */
		{ "none",	GO_MARKER_NONE },
		{ "plus",	GO_MARKER_CROSS },		/* CHECK ME */
		{ "square",	GO_MARKER_SQUARE },
		{ "star",	GO_MARKER_ASTERISK },		/* CHECK ME */
		{ "triangle",	GO_MARKER_TRIANGLE_UP },	/* FIXME */
		{ "x",		GO_MARKER_X },
		{ NULL, 0 }
	};
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	int symbol;
	if (NULL != state->marker && simple_enum (xin, attrs, symbols, &symbol))
		state->marker_symbol = symbol;
}

static void
xlsx_chart_marker_end (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	if (NULL != state->cur_obj && IS_GOG_STYLED_OBJECT (state->cur_obj)) {
		GogStyle *style = gog_styled_object_get_style (
			GOG_STYLED_OBJECT (state->cur_obj));
		if (state->marker_symbol != GO_MARKER_MAX) {
			style->marker.auto_shape = FALSE;
			go_marker_set_shape (state->marker, state->marker_symbol);
		}
		gog_style_set_marker (style, state->marker);
		state->marker = NULL;
	}
}

static void
xlsx_chart_text (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;

	if (NULL == state->series) {
		GogObject *label = gog_object_add_by_name (state->cur_obj,
			(state->cur_obj == (GogObject *)state->chart) ? "Title" : "Label", NULL);
		if (NULL != label) {
			GnmValue *value = value_new_string_nocopy (state->chart_tx);
			GnmExprTop const *texpr = gnm_expr_top_new_constant (value);
			gog_dataset_set_dim (GOG_DATASET (label), 0,
				gnm_go_data_scalar_new_expr (state->sheet, texpr), NULL);
			state->chart_tx = NULL;
		}
	}
	g_free (state->chart_tx);
	state->chart_tx = NULL;
}

static void
xlsx_chart_text_content (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	g_return_if_fail (state->chart_tx == NULL);
	state->chart_tx = g_strdup (xin->content->str);
}

static void
xlsx_plot_area (GsfXMLIn *xin, xmlChar const **attrs)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	GogObject *backplane = gog_object_add_by_name (
		GOG_OBJECT (state->chart), "Backplane", NULL);
	xlsx_chart_push_obj (state, backplane);
}
static void
xlsx_chart_pop (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	xlsx_chart_pop_obj ((XLSXReadState *)xin->user_state);
}

static GsfXMLInNode const xlsx_chart_dtd[] =
{
GSF_XML_IN_NODE_FULL (START, START, -1, NULL, GSF_XML_NO_CONTENT, FALSE, TRUE, NULL, NULL, 0),
GSF_XML_IN_NODE_FULL (START, CHART_SPACE, XL_NS_CHART, "chartSpace", GSF_XML_NO_CONTENT, FALSE, TRUE, NULL, NULL, 0),
  GSF_XML_IN_NODE (CHART_SPACE, SHAPE_PR, XL_NS_CHART, "spPr", GSF_XML_NO_CONTENT, &xlsx_chart_style_start, &xlsx_chart_style_end),
    GSF_XML_IN_NODE (SHAPE_PR, FILL_NONE,	XL_NS_DRAW, "noFill", GSF_XML_NO_CONTENT, &xlsx_chart_no_fill, NULL),
    GSF_XML_IN_NODE (SHAPE_PR, FILL_SOLID,	XL_NS_DRAW, "solidFill", GSF_XML_NO_CONTENT, &xlsx_chart_solid_fill, NULL),
      GSF_XML_IN_NODE (FILL_SOLID, COLOR_THEMED, XL_NS_DRAW, "schemeClr", GSF_XML_NO_CONTENT, &xlsx_draw_color_themed, &xlsx_draw_color_end),
        GSF_XML_IN_NODE (COLOR_THEMED, COLOR_LUM, XL_NS_DRAW, "lumMod", GSF_XML_NO_CONTENT, NULL, NULL),
      GSF_XML_IN_NODE (FILL_SOLID, COLOR_RGB,	 XL_NS_DRAW, "srgbClr", GSF_XML_NO_CONTENT, &xlsx_draw_color_rgb, &xlsx_draw_color_end),
        GSF_XML_IN_NODE (COLOR_RGB, RGB_ALPHA,	   XL_NS_DRAW, "alpha", GSF_XML_NO_CONTENT, &xlsx_draw_color_alpha, NULL),
        GSF_XML_IN_NODE (COLOR_RGB, RGB_GAMMA,	   XL_NS_DRAW, "gamma", GSF_XML_NO_CONTENT, NULL, NULL),
        GSF_XML_IN_NODE (COLOR_RGB, RGB_INV_GAMMA, XL_NS_DRAW, "invGamma", GSF_XML_NO_CONTENT, NULL, NULL),
        GSF_XML_IN_NODE (COLOR_RGB, RGB_SHADE,	   XL_NS_DRAW, "shade", GSF_XML_NO_CONTENT, NULL, NULL),
        GSF_XML_IN_NODE (COLOR_RGB, RGB_TINT,	   XL_NS_DRAW, "tint", GSF_XML_NO_CONTENT, NULL, NULL),
        GSF_XML_IN_NODE (COLOR_RGB, LN_DASH,	   XL_NS_DRAW, "prstDash", GSF_XML_NO_CONTENT, &xlsx_draw_line_dash, NULL),

    GSF_XML_IN_NODE (SHAPE_PR, FILL_BLIP,	XL_NS_DRAW, "blipFill", GSF_XML_NO_CONTENT, NULL, NULL),
      GSF_XML_IN_NODE (FILL_BLIP, FILL_BLIP_BLIP,	XL_NS_DRAW, "blip", GSF_XML_NO_CONTENT, NULL, NULL),
      GSF_XML_IN_NODE (FILL_BLIP, FILL_BLIP_SRC,	XL_NS_DRAW, "srcRect", GSF_XML_NO_CONTENT, NULL, NULL),
      GSF_XML_IN_NODE (FILL_BLIP, FILL_BLIP_TILE,	XL_NS_DRAW, "tile", GSF_XML_NO_CONTENT, NULL, NULL),

    GSF_XML_IN_NODE (SHAPE_PR, FILL_GRAD,	XL_NS_DRAW, "gradFill", GSF_XML_NO_CONTENT, NULL, NULL),
      GSF_XML_IN_NODE (FILL_GRAD, GRAD_LIST,	XL_NS_DRAW, "gsLst", GSF_XML_NO_CONTENT, NULL, NULL),
       GSF_XML_IN_NODE (GRAD_LIST, GRAD_LIST_ITEM, XL_NS_DRAW, "gs", GSF_XML_NO_CONTENT, NULL, NULL),
         GSF_XML_IN_NODE (GRAD_LIST_ITEM, COLOR_RGB, XL_NS_DRAW, "srgbClr", GSF_XML_NO_CONTENT, NULL, NULL),	/* 2nd Def */
      GSF_XML_IN_NODE (FILL_GRAD, GRAD_LINE,	XL_NS_DRAW, "lin", GSF_XML_NO_CONTENT, NULL, NULL),

    GSF_XML_IN_NODE (SHAPE_PR, FILL_PATT,	XL_NS_DRAW, "pattFill", GSF_XML_NO_CONTENT, &xlsx_chart_solid_fill, NULL),
      GSF_XML_IN_NODE (FILL_PATT, FILL_PATT_BG,	XL_NS_DRAW, "bgClr", GSF_XML_NO_CONTENT, NULL, NULL),
        GSF_XML_IN_NODE (FILL_PATT_BG, COLOR_RGB, XL_NS_DRAW, "srgbClr", GSF_XML_NO_CONTENT, NULL, NULL),	/* 2nd Def */
      GSF_XML_IN_NODE (FILL_PATT, FILL_PATT_FG,	XL_NS_DRAW, "fgClr", GSF_XML_NO_CONTENT, NULL, NULL),
        GSF_XML_IN_NODE (FILL_PATT_FG, COLOR_RGB, XL_NS_DRAW, "srgbClr", GSF_XML_NO_CONTENT, NULL, NULL),	/* 2nd Def */

    GSF_XML_IN_NODE (SHAPE_PR, SHAPE_PR_LN, XL_NS_DRAW, "ln", GSF_XML_NO_CONTENT, &xlsx_style_line_start, &xlsx_style_line_end),
      GSF_XML_IN_NODE (SHAPE_PR_LN, LN_NOFILL, XL_NS_DRAW, "noFill", GSF_XML_NO_CONTENT, NULL, NULL),
      GSF_XML_IN_NODE (SHAPE_PR_LN, LN_DASH, XL_NS_DRAW, "prstDash", GSF_XML_NO_CONTENT, NULL, NULL),		/* 2nd Def */
      GSF_XML_IN_NODE (SHAPE_PR_LN, FILL_SOLID, XL_NS_DRAW, "solidFill", GSF_XML_NO_CONTENT, NULL, NULL),	/* 2nd Def */
      GSF_XML_IN_NODE (SHAPE_PR_LN, FILL_PATT,	XL_NS_DRAW, "pattFill", GSF_XML_NO_CONTENT, NULL, NULL),	/* 2nd Def */
    GSF_XML_IN_NODE (SHAPE_PR, TEXT_PR, XL_NS_CHART, "txPr", GSF_XML_NO_CONTENT, NULL, NULL),
      GSF_XML_IN_NODE (TEXT_PR, TEXT_PR_BODY,	XL_NS_DRAW, "bodyPr", GSF_XML_NO_CONTENT, NULL, NULL),
      GSF_XML_IN_NODE (TEXT_PR, TEXT_PR_STYLE,	XL_NS_DRAW, "lstStyle", GSF_XML_NO_CONTENT, NULL, NULL),
      GSF_XML_IN_NODE (TEXT_PR, TEXT_PR_P,	XL_NS_DRAW, "p", GSF_XML_NO_CONTENT, NULL, NULL),
        GSF_XML_IN_NODE (TEXT_PR_P, PR_P_PR,	XL_NS_DRAW, "pPr", GSF_XML_NO_CONTENT, NULL, NULL),
          GSF_XML_IN_NODE (PR_P_PR, PR_P_PR_DEF, XL_NS_DRAW, "defRPr", GSF_XML_NO_CONTENT, NULL, NULL),
            GSF_XML_IN_NODE (PR_P_PR_DEF, PR_P_PR_DEF_CS, XL_NS_DRAW, "cs", GSF_XML_NO_CONTENT, NULL, NULL),
            GSF_XML_IN_NODE (PR_P_PR_DEF, PR_P_PR_DEF_EA, XL_NS_DRAW, "ea", GSF_XML_NO_CONTENT, NULL, NULL),
            GSF_XML_IN_NODE (PR_P_PR_DEF, PR_P_PR_DEF_LATIN, XL_NS_DRAW, "latin", GSF_XML_NO_CONTENT, NULL, NULL),
	    GSF_XML_IN_NODE (PR_P_PR_DEF, FILL_SOLID, XL_NS_DRAW, "solidFill", GSF_XML_NO_CONTENT, NULL, NULL),	/* 2nd Def */
            GSF_XML_IN_NODE (PR_P_PR_DEF, PR_P_PR_DEF_UFILLTX, XL_NS_DRAW, "uFillTx", GSF_XML_NO_CONTENT, NULL, NULL),
            GSF_XML_IN_NODE (PR_P_PR_DEF, PR_P_PR_DEF_ULNTX, XL_NS_DRAW, "uLnTx", GSF_XML_NO_CONTENT, NULL, NULL),
        GSF_XML_IN_NODE (TEXT_PR_P, PR_P_PR_END,XL_NS_DRAW, "endParaRPr", GSF_XML_NO_CONTENT, NULL, NULL),

  GSF_XML_IN_NODE (CHART_SPACE, TEXT_PR, XL_NS_CHART, "txPr", GSF_XML_NO_CONTENT, NULL, NULL),		/* 2nd Def */

  GSF_XML_IN_NODE (CHART_SPACE, CHART, XL_NS_CHART, "chart", GSF_XML_NO_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE (CHART, PLOTAREA, XL_NS_CHART, "plotArea", GSF_XML_NO_CONTENT, &xlsx_plot_area, &xlsx_chart_pop),
      GSF_XML_IN_NODE (PLOTAREA, SHAPE_PR, XL_NS_CHART, "spPr", GSF_XML_NO_CONTENT, NULL, NULL),	/* 2nd Def */
      GSF_XML_IN_NODE_FULL (PLOTAREA, CAT_AXIS, XL_NS_CHART, "catAx", GSF_XML_NO_CONTENT, FALSE, TRUE,
			    &xlsx_axis_start, &xlsx_axis_end, XLSX_AXIS_CAT),
        GSF_XML_IN_NODE (CAT_AXIS, AXIS_AXID, XL_NS_CHART, "axId", GSF_XML_NO_CONTENT, &xlsx_axis_id, NULL),
        GSF_XML_IN_NODE (CAT_AXIS, AXIS_DELETE, XL_NS_CHART, "delete", GSF_XML_NO_CONTENT, &xlsx_axis_delete, NULL),
        GSF_XML_IN_NODE (CAT_AXIS, SHAPE_PR, XL_NS_CHART, "spPr", GSF_XML_NO_CONTENT, NULL, NULL),			/* 2nd Def */
        GSF_XML_IN_NODE (CAT_AXIS, AXIS_NUMFMT, XL_NS_CHART, "numFmt", GSF_XML_NO_CONTENT, NULL, NULL),
        GSF_XML_IN_NODE (CAT_AXIS, AXIS_DELETE, XL_NS_CHART, "delete", GSF_XML_NO_CONTENT, NULL, NULL),
        GSF_XML_IN_NODE (CAT_AXIS, AXIS_MAJORTICKMARK, XL_NS_CHART, "majorTickMark", GSF_XML_NO_CONTENT, NULL, NULL),
        GSF_XML_IN_NODE (CAT_AXIS, AXIS_MINORTICKMARK, XL_NS_CHART, "minorTickMark", GSF_XML_NO_CONTENT, NULL, NULL),
        GSF_XML_IN_NODE_FULL (CAT_AXIS, AXIS_MAJORTICK_UNIT, XL_NS_CHART, "majorUnit", GSF_XML_NO_CONTENT, FALSE, TRUE,
			      &xlsx_axis_bound, NULL, GOG_AXIS_ELEM_MAJOR_TICK),
        GSF_XML_IN_NODE_FULL (CAT_AXIS, AXIS_MINORTICK_UNIT, XL_NS_CHART, "minorUnit", GSF_XML_NO_CONTENT, FALSE, TRUE,
			      &xlsx_axis_bound, NULL, GOG_AXIS_ELEM_MINOR_TICK),
        GSF_XML_IN_NODE (CAT_AXIS, AXIS_TICK_LBL_SKIP, XL_NS_CHART, "tickLblSkip", GSF_XML_NO_CONTENT, NULL, NULL),
        GSF_XML_IN_NODE (CAT_AXIS, AXIS_TICK_MARK_SKIP, XL_NS_CHART, "tickMarkSkip", GSF_XML_NO_CONTENT, NULL, NULL),
        GSF_XML_IN_NODE (CAT_AXIS, TEXT_PR, XL_NS_CHART, "txPr", GSF_XML_NO_CONTENT, NULL, NULL),		/* 2nd Def */
        GSF_XML_IN_NODE (CAT_AXIS, AXIS_SCALING, XL_NS_CHART, "scaling", GSF_XML_NO_CONTENT, NULL, NULL),
          GSF_XML_IN_NODE_FULL (AXIS_SCALING, AX_MIN, XL_NS_CHART, "min", GSF_XML_NO_CONTENT, FALSE, TRUE,
				&xlsx_axis_bound, NULL, GOG_AXIS_ELEM_MIN),
          GSF_XML_IN_NODE_FULL (AXIS_SCALING, AX_MAX, XL_NS_CHART, "max", GSF_XML_NO_CONTENT, FALSE, TRUE,
				&xlsx_axis_bound, NULL, GOG_AXIS_ELEM_MAX),
          GSF_XML_IN_NODE (AXIS_SCALING, AX_LOG, XL_NS_CHART, "logBase", GSF_XML_NO_CONTENT, &xlsx_chart_logbase, NULL),
          GSF_XML_IN_NODE (AXIS_SCALING, AX_ORIENTATION, XL_NS_CHART, "orientation", GSF_XML_NO_CONTENT, &xlsx_axis_orientation, NULL),
        GSF_XML_IN_NODE (CAT_AXIS, MAJOR_GRID, XL_NS_CHART, "majorGridlines", GSF_XML_NO_CONTENT,
			 &xlsx_chart_gridlines, &xlsx_chart_pop),
          GSF_XML_IN_NODE (MAJOR_GRID, SHAPE_PR, XL_NS_CHART, "spPr", GSF_XML_NO_CONTENT, NULL, NULL),		/* 2nd Def */
        GSF_XML_IN_NODE (CAT_AXIS, AXIS_POS, XL_NS_CHART, "axPos", GSF_XML_NO_CONTENT, &xlsx_axis_pos, NULL),
        GSF_XML_IN_NODE (CAT_AXIS, CAT_AXIS_TICKLBLPOS, XL_NS_CHART, "tickLblPos", GSF_XML_NO_CONTENT, NULL, NULL),
        GSF_XML_IN_NODE (CAT_AXIS, CAT_AXIS_AUTO, XL_NS_CHART, "auto", GSF_XML_NO_CONTENT, NULL, NULL),
        GSF_XML_IN_NODE (CAT_AXIS, AXIS_CROSSAX, XL_NS_CHART, "crossAx", GSF_XML_NO_CONTENT, NULL, NULL),
        GSF_XML_IN_NODE (CAT_AXIS, AXIS_CROSSES, XL_NS_CHART, "crosses", GSF_XML_NO_CONTENT, &xlsx_axis_crosses, NULL),

        GSF_XML_IN_NODE (CAT_AXIS, CAT_AXIS_LBLALGN, XL_NS_CHART, "lblAlgn", GSF_XML_NO_CONTENT, NULL, NULL),
        GSF_XML_IN_NODE (CAT_AXIS, CAT_AXIS_LBLOFFSET, XL_NS_CHART, "lblOffset", GSF_XML_NO_CONTENT, NULL, NULL),
        GSF_XML_IN_NODE (CAT_AXIS, TEXT_PR, XL_NS_CHART, "txPr", GSF_XML_NO_CONTENT, NULL, NULL),		/* 2nd Def */
        GSF_XML_IN_NODE (CAT_AXIS, TITLE, XL_NS_CHART, "title", GSF_XML_NO_CONTENT, NULL, NULL),		/* ID is used */
          GSF_XML_IN_NODE (TITLE, LAYOUT, XL_NS_CHART, "layout", GSF_XML_NO_CONTENT, NULL, NULL),
	    GSF_XML_IN_NODE (LAYOUT, LAST_LAYOUT,	    XL_NS_CHART, "lastLayout", GSF_XML_NO_CONTENT, NULL, NULL),
	      GSF_XML_IN_NODE (LAST_LAYOUT, LAYOUT_X, XL_NS_CHART, "x", GSF_XML_NO_CONTENT, NULL, NULL),
	      GSF_XML_IN_NODE (LAST_LAYOUT, LAYOUT_Y, XL_NS_CHART, "y", GSF_XML_NO_CONTENT, NULL, NULL),
	      GSF_XML_IN_NODE (LAST_LAYOUT, LAYOUT_W, XL_NS_CHART, "w", GSF_XML_NO_CONTENT, NULL, NULL),
	      GSF_XML_IN_NODE (LAST_LAYOUT, LAYOUT_H, XL_NS_CHART, "h", GSF_XML_NO_CONTENT, NULL, NULL),
	    GSF_XML_IN_NODE (LAYOUT, LAST_LAYOUT_OUTER, XL_NS_CHART, "lastLayoutOuter", GSF_XML_NO_CONTENT, NULL, NULL),
	      GSF_XML_IN_NODE (LAST_LAYOUT_OUTER, LAYOUT_X, XL_NS_CHART, "x", GSF_XML_NO_CONTENT, NULL, NULL),
	      GSF_XML_IN_NODE (LAST_LAYOUT_OUTER, LAYOUT_Y, XL_NS_CHART, "y", GSF_XML_NO_CONTENT, NULL, NULL),
	      GSF_XML_IN_NODE (LAST_LAYOUT_OUTER, LAYOUT_W, XL_NS_CHART, "w", GSF_XML_NO_CONTENT, NULL, NULL),
	      GSF_XML_IN_NODE (LAST_LAYOUT_OUTER, LAYOUT_H, XL_NS_CHART, "h", GSF_XML_NO_CONTENT, NULL, NULL),
	    GSF_XML_IN_NODE (LAYOUT, MAN_LAYOUT, XL_NS_CHART, "manualLayout", GSF_XML_NO_CONTENT, NULL, NULL),
	      GSF_XML_IN_NODE (MAN_LAYOUT, MAN_LAYOUT_TARGET, XL_NS_CHART, "layoutTarget", GSF_XML_NO_CONTENT, NULL, NULL),
	      GSF_XML_IN_NODE (MAN_LAYOUT, MAN_LAYOUT_H, XL_NS_CHART, "h", GSF_XML_NO_CONTENT, NULL, NULL),
	      GSF_XML_IN_NODE (MAN_LAYOUT, MAN_LAYOUT_W, XL_NS_CHART, "w", GSF_XML_NO_CONTENT, NULL, NULL),
	      GSF_XML_IN_NODE (MAN_LAYOUT, MAN_LAYOUT_X, XL_NS_CHART, "x", GSF_XML_NO_CONTENT, NULL, NULL),
	      GSF_XML_IN_NODE (MAN_LAYOUT, MAN_LAYOUT_XMODE, XL_NS_CHART, "xMode", GSF_XML_NO_CONTENT, NULL, NULL),
	      GSF_XML_IN_NODE (MAN_LAYOUT, MAN_LAYOUT_Y, XL_NS_CHART, "y", GSF_XML_NO_CONTENT, NULL, NULL),
	      GSF_XML_IN_NODE (MAN_LAYOUT, MAN_LAYOUT_YMODE, XL_NS_CHART, "yMode", GSF_XML_NO_CONTENT, NULL, NULL),
          GSF_XML_IN_NODE (TITLE, SHAPE_PR, XL_NS_CHART, "spPr", GSF_XML_NO_CONTENT, NULL, NULL),		/* 2nd Def */
          GSF_XML_IN_NODE (TITLE, TEXT, XL_NS_CHART, "tx", GSF_XML_NO_CONTENT, NULL, &xlsx_chart_text),
            GSF_XML_IN_NODE (TEXT, TX_RICH, XL_NS_CHART, "rich", GSF_XML_NO_CONTENT, NULL, NULL),
              GSF_XML_IN_NODE (TX_RICH, TX_RICH_BODY, XL_NS_CHART, "bodyP", GSF_XML_NO_CONTENT, NULL, NULL),
              GSF_XML_IN_NODE (TX_RICH, TX_RICH_BODY_PR, XL_NS_DRAW, "bodyPr", GSF_XML_NO_CONTENT, NULL, NULL),
              GSF_XML_IN_NODE (TX_RICH, TX_RICH_STYLES, XL_NS_DRAW, "lstStyle", GSF_XML_NO_CONTENT, NULL, NULL),
              GSF_XML_IN_NODE (TX_RICH, TX_RICH_P, XL_NS_DRAW, "p", GSF_XML_NO_CONTENT, NULL, NULL),
                GSF_XML_IN_NODE (TX_RICH_P, PR_P_PR, XL_NS_DRAW, "pPr", GSF_XML_NO_CONTENT, NULL, NULL),	/* 2nd Def */
                GSF_XML_IN_NODE (TX_RICH_P, TX_RICH_R, XL_NS_DRAW, "r", GSF_XML_NO_CONTENT, NULL, NULL),
                  GSF_XML_IN_NODE (TX_RICH_R, TX_RICH_R_PR, XL_NS_DRAW, "rPr", GSF_XML_NO_CONTENT, NULL, NULL),
                  GSF_XML_IN_NODE (TX_RICH_R, TX_RICH_R_T, XL_NS_DRAW,  "t", GSF_XML_CONTENT, NULL, &xlsx_chart_text_content),
            GSF_XML_IN_NODE (TEXT, STR_REF, XL_NS_CHART, "strRef", GSF_XML_NO_CONTENT, NULL, NULL),
              GSF_XML_IN_NODE (STR_REF, FUNC,	XL_NS_CHART,	"f",	GSF_XML_CONTENT, NULL, &xlsx_chart_ser_f),
              GSF_XML_IN_NODE (STR_REF, STR_CACHE, XL_NS_CHART,	"strCache", GSF_XML_NO_CONTENT, NULL, NULL),
                GSF_XML_IN_NODE (STR_CACHE, STR_CACHE_COUNT, XL_NS_CHART,"ptCount", GSF_XML_NO_CONTENT, NULL, NULL),
                GSF_XML_IN_NODE (STR_CACHE, STR_PT, XL_NS_CHART,"pt", GSF_XML_NO_CONTENT, NULL, NULL),
                  GSF_XML_IN_NODE (STR_PT, STR_VAL, XL_NS_CHART,"v", GSF_XML_NO_CONTENT, NULL, NULL),
          GSF_XML_IN_NODE (TITLE, TEXT_PR, XL_NS_CHART, "txPr", GSF_XML_NO_CONTENT, NULL, NULL),		/* 2nd Def */

      GSF_XML_IN_NODE_FULL (PLOTAREA, VAL_AXIS, XL_NS_CHART, "valAx", GSF_XML_NO_CONTENT, FALSE, TRUE,
			    &xlsx_axis_start, &xlsx_axis_end, XLSX_AXIS_VAL),
        GSF_XML_IN_NODE (VAL_AXIS, AXIS_AXID, XL_NS_CHART, "axId", GSF_XML_NO_CONTENT, NULL, NULL),			/* 2nd Def */
        GSF_XML_IN_NODE (VAL_AXIS, AXIS_DELETE, XL_NS_CHART, "delete", GSF_XML_NO_CONTENT, NULL, NULL),			/* 2nd Def */
        GSF_XML_IN_NODE (VAL_AXIS, TITLE, XL_NS_CHART, "title", GSF_XML_NO_CONTENT, NULL, NULL),			/* 2nd Def */
        GSF_XML_IN_NODE (VAL_AXIS, AXIS_SCALING, XL_NS_CHART, "scaling", GSF_XML_NO_CONTENT, NULL, NULL),		/* 2nd Def */
        GSF_XML_IN_NODE (VAL_AXIS, AXIS_POS, XL_NS_CHART, "axPos", GSF_XML_NO_CONTENT, NULL, NULL),			/* 2nd Def */
        GSF_XML_IN_NODE (VAL_AXIS, MAJOR_GRID, XL_NS_CHART, "majorGridlines", GSF_XML_NO_CONTENT, NULL, NULL),		/* 2nd Def */
        GSF_XML_IN_NODE (VAL_AXIS, AXIS_NUMFMT, XL_NS_CHART, "numFmt", GSF_XML_NO_CONTENT, NULL, NULL),			/* 2nd Def */
        GSF_XML_IN_NODE (VAL_AXIS, AXIS_MAJORTICKMARK, XL_NS_CHART, "majorTickMark", GSF_XML_NO_CONTENT, NULL, NULL),	/* 2nd Def */
        GSF_XML_IN_NODE (VAL_AXIS, AXIS_MINORTICKMARK, XL_NS_CHART, "minorTickMark", GSF_XML_NO_CONTENT, NULL, NULL),
        GSF_XML_IN_NODE (VAL_AXIS, AXIS_MAJORTICK_UNIT, XL_NS_CHART, "majorUnit", GSF_XML_NO_CONTENT, NULL, NULL),
        GSF_XML_IN_NODE (VAL_AXIS, AXIS_MINORTICK_UNIT, XL_NS_CHART, "minorUnit", GSF_XML_NO_CONTENT, NULL, NULL),
        GSF_XML_IN_NODE (VAL_AXIS, VAL_AXIS_TICKLBLPOS, XL_NS_CHART, "tickLblPos", GSF_XML_NO_CONTENT, NULL, NULL),	/* 2nd Def */
        GSF_XML_IN_NODE (VAL_AXIS, AXIS_CROSSAX, XL_NS_CHART, "crossAx", GSF_XML_NO_CONTENT, NULL, NULL),		/* 2nd Def */
        GSF_XML_IN_NODE (VAL_AXIS, AXIS_CROSSES, XL_NS_CHART, "crosses", GSF_XML_NO_CONTENT, NULL, NULL),		/* 2nd Def */
        GSF_XML_IN_NODE (VAL_AXIS, VAL_AXIS_CROSSBETWEEN, XL_NS_CHART, "crossBetween", GSF_XML_NO_CONTENT, NULL, NULL),
        GSF_XML_IN_NODE (VAL_AXIS, SHAPE_PR, XL_NS_CHART, "spPr", GSF_XML_NO_CONTENT, NULL, NULL),		/* 2nd Def */
        GSF_XML_IN_NODE (VAL_AXIS, TEXT_PR, XL_NS_CHART, "txPr", GSF_XML_NO_CONTENT, NULL, NULL),		/* 2nd Def */

      GSF_XML_IN_NODE (PLOTAREA, LAYOUT, XL_NS_CHART, "layout", GSF_XML_NO_CONTENT, NULL, NULL),
        GSF_XML_IN_NODE (LAYOUT, LAST_LAYOUT,	    XL_NS_CHART, "lastLayout", GSF_XML_NO_CONTENT, NULL, NULL),
          GSF_XML_IN_NODE (LAST_LAYOUT, LAYOUT_X, XL_NS_CHART, "x", GSF_XML_NO_CONTENT, NULL, NULL),
          GSF_XML_IN_NODE (LAST_LAYOUT, LAYOUT_Y, XL_NS_CHART, "y", GSF_XML_NO_CONTENT, NULL, NULL),
          GSF_XML_IN_NODE (LAST_LAYOUT, LAYOUT_W, XL_NS_CHART, "w", GSF_XML_NO_CONTENT, NULL, NULL),
          GSF_XML_IN_NODE (LAST_LAYOUT, LAYOUT_H, XL_NS_CHART, "h", GSF_XML_NO_CONTENT, NULL, NULL),
        GSF_XML_IN_NODE (LAYOUT, LAST_LAYOUT_OUTER, XL_NS_CHART, "lastLayoutOuter", GSF_XML_NO_CONTENT, NULL, NULL),
          GSF_XML_IN_NODE (LAST_LAYOUT_OUTER, LAYOUT_X, XL_NS_CHART, "x", GSF_XML_NO_CONTENT, NULL, NULL),
          GSF_XML_IN_NODE (LAST_LAYOUT_OUTER, LAYOUT_Y, XL_NS_CHART, "y", GSF_XML_NO_CONTENT, NULL, NULL),
          GSF_XML_IN_NODE (LAST_LAYOUT_OUTER, LAYOUT_W, XL_NS_CHART, "w", GSF_XML_NO_CONTENT, NULL, NULL),
          GSF_XML_IN_NODE (LAST_LAYOUT_OUTER, LAYOUT_H, XL_NS_CHART, "h", GSF_XML_NO_CONTENT, NULL, NULL),
        GSF_XML_IN_NODE (LAYOUT, MAN_LAYOUT, XL_NS_CHART, "manualLayout", GSF_XML_NO_CONTENT, NULL, NULL),
          GSF_XML_IN_NODE (MAN_LAYOUT, MAN_LAYOUT_H, XL_NS_CHART, "h", GSF_XML_NO_CONTENT, NULL, NULL),
          GSF_XML_IN_NODE (MAN_LAYOUT, MAN_LAYOUT_W, XL_NS_CHART, "w", GSF_XML_NO_CONTENT, NULL, NULL),
          GSF_XML_IN_NODE (MAN_LAYOUT, MAN_LAYOUT_X, XL_NS_CHART, "x", GSF_XML_NO_CONTENT, NULL, NULL),
          GSF_XML_IN_NODE (MAN_LAYOUT, MAN_LAYOUT_XMODE, XL_NS_CHART, "xMode", GSF_XML_NO_CONTENT, NULL, NULL),
          GSF_XML_IN_NODE (MAN_LAYOUT, MAN_LAYOUT_Y, XL_NS_CHART, "y", GSF_XML_NO_CONTENT, NULL, NULL),
          GSF_XML_IN_NODE (MAN_LAYOUT, MAN_LAYOUT_YMODE, XL_NS_CHART, "yMode", GSF_XML_NO_CONTENT, NULL, NULL),

      GSF_XML_IN_NODE (PLOTAREA, SCATTER, XL_NS_CHART,	"scatterChart", GSF_XML_NO_CONTENT, xlsx_chart_xy, &xlsx_plot_end),
        GSF_XML_IN_NODE (SCATTER, SCATTER_STYLE, XL_NS_CHART,	"scatterStyle", GSF_XML_NO_CONTENT, NULL, NULL),
        GSF_XML_IN_NODE (SCATTER, PLOT_AXIS_ID, XL_NS_CHART,		"axId", GSF_XML_NO_CONTENT, &xlsx_plot_axis_id, NULL),

        GSF_XML_IN_NODE (SCATTER, SERIES, XL_NS_CHART,	"ser", GSF_XML_NO_CONTENT, &xlsx_chart_ser_start, &xlsx_chart_ser_end),
          GSF_XML_IN_NODE (SERIES, SERIES_IDX, XL_NS_CHART,	"idx", GSF_XML_NO_CONTENT, NULL, NULL),
          GSF_XML_IN_NODE (SERIES, SERIES_ORDER, XL_NS_CHART,	"order", GSF_XML_NO_CONTENT, NULL, NULL),

          GSF_XML_IN_NODE_FULL (SERIES, SERIES_CAT, XL_NS_CHART,"cat", GSF_XML_NO_CONTENT, FALSE, TRUE,
			   &xlsx_ser_type_start, &xlsx_ser_type_end, GOG_MS_DIM_CATEGORIES),
            GSF_XML_IN_NODE (SERIES_CAT, STR_REF, XL_NS_CHART,	"strRef", GSF_XML_NO_CONTENT, NULL, NULL),		/* 2nd Def */
            GSF_XML_IN_NODE (SERIES_CAT, NUM_LIT, XL_NS_CHART,  "numLit", GSF_XML_NO_CONTENT, NULL, NULL),
              GSF_XML_IN_NODE (NUM_LIT, NUM_LIT_FMT, XL_NS_CHART,   "formatCode", GSF_XML_NO_CONTENT, NULL, NULL),
              GSF_XML_IN_NODE (NUM_LIT, NUM_LIT_COUNT, XL_NS_CHART, "ptCount", GSF_XML_NO_CONTENT, NULL, NULL),
              GSF_XML_IN_NODE (NUM_LIT, NUM_LIT_PT, XL_NS_CHART,     "pt", GSF_XML_NO_CONTENT, NULL, NULL),
                GSF_XML_IN_NODE (NUM_LIT_PT, NUM_LIT_PT_VAL, XL_NS_CHART,     "v", GSF_XML_NO_CONTENT, NULL, NULL),
            GSF_XML_IN_NODE (SERIES_CAT, NUM_REF, XL_NS_CHART,	"numRef", GSF_XML_NO_CONTENT, NULL, NULL),
              GSF_XML_IN_NODE (NUM_REF, FUNC, XL_NS_CHART,	"f", GSF_XML_NO_CONTENT, NULL, NULL),			/* 2nd Def */
              GSF_XML_IN_NODE (NUM_REF, NUM_CACHE, XL_NS_CHART,	"numCache", GSF_XML_NO_CONTENT, NULL, NULL),
                GSF_XML_IN_NODE (NUM_CACHE, NUM_CACHE_FMT, XL_NS_CHART,	 "formatCode", GSF_XML_NO_CONTENT, NULL, NULL),
                GSF_XML_IN_NODE (NUM_CACHE, NUM_CACHE_COUNT, XL_NS_CHART,"ptCount", GSF_XML_NO_CONTENT, NULL, NULL),
                GSF_XML_IN_NODE (NUM_CACHE, NUM_PT, XL_NS_CHART,"pt", GSF_XML_NO_CONTENT, NULL, NULL),
                  GSF_XML_IN_NODE (NUM_PT, NUM_VAL, XL_NS_CHART,"v", GSF_XML_NO_CONTENT, NULL, NULL),

          GSF_XML_IN_NODE_FULL (SERIES, SERIES_VAL, XL_NS_CHART,	"val", GSF_XML_NO_CONTENT, FALSE, TRUE,
			   &xlsx_ser_type_start, &xlsx_ser_type_end, GOG_MS_DIM_VALUES),
            GSF_XML_IN_NODE (SERIES_VAL, NUM_REF, XL_NS_CHART,	"numRef", GSF_XML_NO_CONTENT, NULL, NULL),		/* 2nd Def */

          GSF_XML_IN_NODE_FULL (SERIES, SERIES_X_VAL, XL_NS_CHART,	"xVal", GSF_XML_NO_CONTENT, FALSE, TRUE,
			   &xlsx_ser_type_start, &xlsx_ser_type_end, GOG_MS_DIM_CATEGORIES),
            GSF_XML_IN_NODE (SERIES_X_VAL, NUM_REF, XL_NS_CHART,	"numRef", GSF_XML_NO_CONTENT, NULL, NULL),	/* 2nd Def */
            GSF_XML_IN_NODE (SERIES_X_VAL, NUM_LIT, XL_NS_CHART,	"numLit", GSF_XML_NO_CONTENT, NULL, NULL),	/* 2nd Def */

          GSF_XML_IN_NODE_FULL (SERIES, SERIES_Y_VAL, XL_NS_CHART,	"yVal", GSF_XML_NO_CONTENT, FALSE, TRUE,
			   &xlsx_ser_type_start, &xlsx_ser_type_end, GOG_MS_DIM_VALUES),
            GSF_XML_IN_NODE (SERIES_Y_VAL, NUM_REF, XL_NS_CHART,	"numRef", GSF_XML_NO_CONTENT, NULL, NULL),	/* 2nd Def */
            GSF_XML_IN_NODE (SERIES_Y_VAL, NUM_LIT, XL_NS_CHART,	"numLit", GSF_XML_NO_CONTENT, NULL, NULL),	/* 2nd Def */

          GSF_XML_IN_NODE_FULL (SERIES, SERIES_BUBBLES, XL_NS_CHART,	"bubbleSize", GSF_XML_NO_CONTENT, FALSE, TRUE,
			   &xlsx_ser_type_start, &xlsx_ser_type_end, GOG_MS_DIM_BUBBLES),
            GSF_XML_IN_NODE (SERIES_BUBBLES, NUM_REF, XL_NS_CHART,	"numRef", GSF_XML_NO_CONTENT, NULL, NULL),	/* 2nd Def */
            GSF_XML_IN_NODE (SERIES_BUBBLES, NUM_LIT, XL_NS_CHART,	"numLit", GSF_XML_NO_CONTENT, NULL, NULL),	/* 2nd Def */

          GSF_XML_IN_NODE (SERIES, TEXT, XL_NS_CHART,	"tx", GSF_XML_NO_CONTENT, NULL, NULL),				/* 2nd Def */

          GSF_XML_IN_NODE (SERIES, SERIES_BUBBLES_3D, XL_NS_CHART,	"bubble3D", GSF_XML_NO_CONTENT, NULL, NULL),
          GSF_XML_IN_NODE (SERIES, SHAPE_PR, XL_NS_CHART, "spPr", GSF_XML_NO_CONTENT, NULL, NULL),			/* 2nd Def */
          GSF_XML_IN_NODE (SERIES, SERIES_SMOOTH, XL_NS_CHART, "smooth", GSF_XML_NO_CONTENT, NULL, NULL),
          GSF_XML_IN_NODE (SERIES, SERIES_IDX, XL_NS_CHART,	"idx", GSF_XML_NO_CONTENT, NULL, NULL),
          GSF_XML_IN_NODE (SERIES, SERIES_D_LBLS, XL_NS_CHART,	"dLbls", GSF_XML_NO_CONTENT, NULL, NULL),
	    GSF_XML_IN_NODE (SERIES_D_LBLS, SHAPE_PR, XL_NS_CHART, "spPr", GSF_XML_NO_CONTENT, NULL, NULL),		/* 2nd Def */
	    GSF_XML_IN_NODE (SERIES_D_LBLS, TEXT_PR, XL_NS_CHART, "txPr", GSF_XML_NO_CONTENT, NULL, NULL),		/* 2nd Def */
	    GSF_XML_IN_NODE (SERIES_D_LBLS, SHOW_VAL, XL_NS_CHART, "showVal", GSF_XML_NO_CONTENT, NULL, NULL),
	    GSF_XML_IN_NODE (SERIES_D_LBLS, NUM_FMT, XL_NS_CHART, "numFmt", GSF_XML_NO_CONTENT, NULL, NULL),
	    GSF_XML_IN_NODE (SERIES_D_LBLS, SHOW_BUBBLE, XL_NS_CHART, "showBubbleSize", GSF_XML_NO_CONTENT, NULL, NULL),
	    GSF_XML_IN_NODE (SERIES_D_LBLS, SHOW_CAT_NAME, XL_NS_CHART, "showCatName", GSF_XML_NO_CONTENT, NULL, NULL),
	    GSF_XML_IN_NODE (SERIES_D_LBLS, SHOW_LEADERS, XL_NS_CHART, "showLeaderLines", GSF_XML_NO_CONTENT, NULL, NULL),
	    GSF_XML_IN_NODE (SERIES_D_LBLS, SHOW_PERCENT, XL_NS_CHART, "showPercent", GSF_XML_NO_CONTENT, NULL, NULL),

          GSF_XML_IN_NODE (SERIES, SERIES_PT, XL_NS_CHART,	"dPt", GSF_XML_NO_CONTENT, &xlsx_chart_pt_start, &xlsx_chart_pt_end),
            GSF_XML_IN_NODE (SERIES_PT, PT_IDX, XL_NS_CHART,	"idx", GSF_XML_NO_CONTENT, &xlsx_chart_pt_index, NULL),
            GSF_XML_IN_NODE (SERIES_PT, SHAPE_PR, XL_NS_CHART,	"spPr", GSF_XML_NO_CONTENT, NULL, NULL),
            GSF_XML_IN_NODE (SERIES_PT, PT_SEP, XL_NS_CHART,	"explosion", GSF_XML_NO_CONTENT, &xlsx_chart_pt_sep, NULL),
            GSF_XML_IN_NODE (SERIES_PT, MARKER, XL_NS_CHART,	"marker", GSF_XML_NO_CONTENT, &xlsx_chart_marker_start, &xlsx_chart_marker_end),
              GSF_XML_IN_NODE (MARKER, SHAPE_PR, XL_NS_CHART, "spPr", GSF_XML_NO_CONTENT, NULL, NULL),			/* 2nd Def */
              GSF_XML_IN_NODE (MARKER, MARKER_SYMBOL, XL_NS_CHART, "symbol", GSF_XML_NO_CONTENT, &xlsx_chart_marker_symbol, NULL),
              GSF_XML_IN_NODE (MARKER, MARKER_SIZE, XL_NS_CHART, "size", GSF_XML_NO_CONTENT, NULL, NULL),

          GSF_XML_IN_NODE (SERIES, SERIES_ERR_BARS, XL_NS_CHART,"errBars", GSF_XML_NO_CONTENT, NULL, NULL),
	    GSF_XML_IN_NODE (SERIES_ERR_BARS, SERIES_ERR_BARS_ERRBARTYPE, XL_NS_CHART, "errBarType",  GSF_XML_NO_CONTENT, NULL, NULL),
	    GSF_XML_IN_NODE (SERIES_ERR_BARS, SERIES_ERR_BARS_ERRDIR, XL_NS_CHART, "errDir", GSF_XML_NO_CONTENT, NULL, NULL),
	    GSF_XML_IN_NODE (SERIES_ERR_BARS, SERIES_ERR_BARS_ERRVALTYPE, XL_NS_CHART, "errValType", GSF_XML_NO_CONTENT, NULL, NULL),
	    GSF_XML_IN_NODE (SERIES_ERR_BARS, SERIES_ERR_BARS_MINUS, XL_NS_CHART, "minus", GSF_XML_NO_CONTENT, NULL, NULL),
	      GSF_XML_IN_NODE (SERIES_ERR_BARS_MINUS, NUM_REF, XL_NS_CHART, "numRef", GSF_XML_NO_CONTENT, NULL, NULL),	/* 2nd Def */
              GSF_XML_IN_NODE (SERIES_ERR_BARS_MINUS, NUM_LIT, XL_NS_CHART, "numLit", GSF_XML_NO_CONTENT, NULL, NULL),	/* 2nd Def */
	    GSF_XML_IN_NODE (SERIES_ERR_BARS, SERIES_ERR_BARS_PLUS, XL_NS_CHART, "plus", GSF_XML_NO_CONTENT, NULL, NULL),
	      GSF_XML_IN_NODE (SERIES_ERR_BARS_PLUS, NUM_REF, XL_NS_CHART, "numRef", GSF_XML_NO_CONTENT, NULL, NULL),	/* 2nd Def */
              GSF_XML_IN_NODE (SERIES_ERR_BARS_PLUS, NUM_LIT, XL_NS_CHART, "numLit", GSF_XML_NO_CONTENT, NULL, NULL),	/* 2nd Def */
	    GSF_XML_IN_NODE (SERIES_ERR_BARS, SHAPE_PR, XL_NS_CHART, "spPr", GSF_XML_NO_CONTENT, NULL, NULL),		/* 2nd Def */

      GSF_XML_IN_NODE (PLOTAREA, BUBBLE, XL_NS_CHART,	"bubbleChart", GSF_XML_NO_CONTENT, &xlsx_chart_bubble, &xlsx_plot_end),
        GSF_XML_IN_NODE (BUBBLE, PLOT_AXIS_ID, XL_NS_CHART,	"axId", GSF_XML_NO_CONTENT, NULL, NULL),		/* 2nd Def */
        GSF_XML_IN_NODE (BUBBLE, SERIES, XL_NS_CHART,		"ser", GSF_XML_NO_CONTENT, NULL, NULL),
        GSF_XML_IN_NODE (BUBBLE, BUBBLE_SCALE, XL_NS_CHART,	"bubbleScale", GSF_XML_NO_CONTENT, NULL, NULL),
        GSF_XML_IN_NODE (BUBBLE, BUBBLE_NEGATIVES, XL_NS_CHART,	"showNegBubbles", GSF_XML_NO_CONTENT, NULL, NULL),
        GSF_XML_IN_NODE (BUBBLE, BUBBLE_SIZE_REP, XL_NS_CHART,	"sizeRepresents", GSF_XML_NO_CONTENT, NULL, NULL),
        GSF_XML_IN_NODE (BUBBLE, VARY_COLORS, XL_NS_CHART,	"varyColors", GSF_XML_NO_CONTENT, &xlsx_vary_colors, NULL),

      GSF_XML_IN_NODE (PLOTAREA, BARCOL, XL_NS_CHART,	"barChart", GSF_XML_NO_CONTENT, &xlsx_chart_bar, &xlsx_plot_end),
        GSF_XML_IN_NODE (BARCOL, PLOT_AXIS_ID,	XL_NS_CHART, "axId", GSF_XML_NO_CONTENT, NULL, NULL),			/* 2nd Def */
        GSF_XML_IN_NODE (BARCOL, SERIES,	XL_NS_CHART, "ser", GSF_XML_NO_CONTENT, NULL, NULL),			/* 2nd Def */
        GSF_XML_IN_NODE (BARCOL, BARCOL_DIR,	XL_NS_CHART, "barDir", GSF_XML_NO_CONTENT, &xlsx_chart_bar_dir, NULL),
        GSF_XML_IN_NODE (BARCOL, BARCOL_OVERLAP, XL_NS_CHART,"overlap", GSF_XML_NO_CONTENT, &xlsx_chart_bar_overlap, NULL),
        GSF_XML_IN_NODE (BARCOL, GROUPING,	XL_NS_CHART, "grouping", GSF_XML_NO_CONTENT, &xlsx_chart_bar_group, NULL),
        GSF_XML_IN_NODE (BARCOL, GAP_WIDTH,	XL_NS_CHART, "gapWidth", GSF_XML_NO_CONTENT, &xlsx_chart_bar_gap, NULL),

      GSF_XML_IN_NODE (PLOTAREA, LINE, XL_NS_CHART,	"lineChart", GSF_XML_NO_CONTENT, &xlsx_chart_line, &xlsx_plot_end),
        GSF_XML_IN_NODE (LINE, PLOT_AXIS_ID, XL_NS_CHART,"axId", GSF_XML_NO_CONTENT, NULL, NULL),				/* 2nd Def */
        GSF_XML_IN_NODE (LINE, SERIES, XL_NS_CHART,	"ser", GSF_XML_NO_CONTENT, NULL, NULL),					/* 2nd Def */
          GSF_XML_IN_NODE (SERIES, MARKER, XL_NS_CHART,	"marker", GSF_XML_NO_CONTENT, NULL, NULL),
            GSF_XML_IN_NODE (MARKER, SHAPE_PR, XL_NS_CHART, "spPr", GSF_XML_NO_CONTENT, NULL, NULL),				/* 2nd Def */
            GSF_XML_IN_NODE (MARKER, MARKER_SYMBOL, XL_NS_CHART, "symbol", GSF_XML_NO_CONTENT, NULL, NULL),
            GSF_XML_IN_NODE (MARKER, MARKER_SIZE, XL_NS_CHART, "size", GSF_XML_NO_CONTENT, NULL, NULL),
        GSF_XML_IN_NODE (LINE, PLOT_AXIS_ID, XL_NS_CHART,"axId", GSF_XML_NO_CONTENT, NULL, NULL),				/* 2nd Def */
        GSF_XML_IN_NODE (LINE, GROUPING, XL_NS_CHART,	"grouping", GSF_XML_NO_CONTENT, NULL, NULL),				/* 2nd Def */
        GSF_XML_IN_NODE (LINE, MARKER, XL_NS_CHART,	"marker", GSF_XML_NO_CONTENT, NULL, NULL),				/* 2nd Def */

      GSF_XML_IN_NODE (PLOTAREA, AREA, XL_NS_CHART,	"areaChart", GSF_XML_NO_CONTENT, &xlsx_chart_area, &xlsx_plot_end),
        GSF_XML_IN_NODE (AREA, PLOT_AXIS_ID, XL_NS_CHART,"axId", GSF_XML_NO_CONTENT, NULL, NULL),				/* 2nd Def */
        GSF_XML_IN_NODE (AREA, SERIES, XL_NS_CHART,	"ser", GSF_XML_NO_CONTENT, NULL, NULL),					/* 2nd Def */
        GSF_XML_IN_NODE (AREA, GROUPING, XL_NS_CHART,	"grouping", GSF_XML_NO_CONTENT, NULL, NULL),				/* 2nd Def */

      GSF_XML_IN_NODE (PLOTAREA, RADAR, XL_NS_CHART,	"radarChart", GSF_XML_NO_CONTENT, &xlsx_chart_radar, &xlsx_plot_end),
        GSF_XML_IN_NODE (RADAR, PLOT_AXIS_ID, XL_NS_CHART,  "axId", GSF_XML_NO_CONTENT, NULL, NULL),				/* 2nd Def */
        GSF_XML_IN_NODE (RADAR, SERIES, XL_NS_CHART,	  "ser", GSF_XML_NO_CONTENT, NULL, NULL),				/* 2nd Def */
        GSF_XML_IN_NODE (RADAR, RADAR_STYLE, XL_NS_CHART, "radarStyle", GSF_XML_NO_CONTENT, NULL, NULL),
        GSF_XML_IN_NODE (RADAR, VARY_COLORS, XL_NS_CHART, "varyColors", GSF_XML_NO_CONTENT, NULL, NULL),			/* 2nd Def */

      GSF_XML_IN_NODE (PLOTAREA, PIE, XL_NS_CHART,	"pieChart", GSF_XML_NO_CONTENT, &xlsx_chart_pie, &xlsx_plot_end),
        GSF_XML_IN_NODE (PIE, SERIES, XL_NS_CHART,	"ser", GSF_XML_NO_CONTENT, NULL, NULL),					/* 2nd Def */
          GSF_XML_IN_NODE (SERIES, PIE_SER_SEP, XL_NS_CHART,	"explosion", GSF_XML_NO_CONTENT, &xlsx_chart_pie_sep, NULL),
        GSF_XML_IN_NODE (PIE, VARY_COLORS, XL_NS_CHART,	"varyColors", GSF_XML_NO_CONTENT, NULL, NULL),				/* 2nd Def */
        GSF_XML_IN_NODE (PIE, PIE_FIRST_SLICE, XL_NS_CHART,	"firstSliceAng", GSF_XML_NO_CONTENT, NULL, NULL),

      GSF_XML_IN_NODE (PLOTAREA, OF_PIE, XL_NS_CHART,	"ofPieChart", GSF_XML_NO_CONTENT, &xlsx_chart_pie, &xlsx_plot_end),
        GSF_XML_IN_NODE (OF_PIE, OF_PIE_TYPE,	XL_NS_CHART, "ofPieType", GSF_XML_NO_CONTENT, NULL, NULL),
        GSF_XML_IN_NODE (OF_PIE, SERIES,	XL_NS_CHART, "ser", GSF_XML_NO_CONTENT, NULL, NULL),				/* 2nd Def */
        GSF_XML_IN_NODE (OF_PIE, SERIES_LINES,	XL_NS_CHART, "serLines", GSF_XML_NO_CONTENT, NULL, NULL),
          GSF_XML_IN_NODE (SERIES_LINES, SHAPE_PR, XL_NS_CHART, "spPr", GSF_XML_NO_CONTENT, NULL, NULL),			/* 2nd Def */
        GSF_XML_IN_NODE (OF_PIE, PIE_GAP_WIDTH,	XL_NS_CHART, "gapWidth", GSF_XML_NO_CONTENT, NULL, NULL),
        GSF_XML_IN_NODE (OF_PIE, VARY_COLORS,	XL_NS_CHART, "varyColors", GSF_XML_NO_CONTENT, NULL, NULL),			/* 2nd Def */
        GSF_XML_IN_NODE (OF_PIE, OF_2ND_PIE,	XL_NS_CHART, "secondPieSize", GSF_XML_NO_CONTENT, NULL, NULL),

      GSF_XML_IN_NODE (PLOTAREA, DOUGHNUT, XL_NS_CHART,	"doughnutChart", GSF_XML_NO_CONTENT, &xlsx_chart_ring, &xlsx_plot_end),
        GSF_XML_IN_NODE (DOUGHNUT, SERIES, XL_NS_CHART,	"ser", GSF_XML_NO_CONTENT, NULL, NULL),					/* 2nd Def */
        GSF_XML_IN_NODE (DOUGHNUT, VARY_COLORS, XL_NS_CHART,	"varyColors", GSF_XML_NO_CONTENT, NULL, NULL),			/* 2nd Def */
        GSF_XML_IN_NODE (DOUGHNUT, PIE_FIRST_SLICE, XL_NS_CHART,	"firstSliceAng", GSF_XML_NO_CONTENT, NULL, NULL),	/* 2nd Def */
        GSF_XML_IN_NODE (DOUGHNUT, HOLE_SIZE, XL_NS_CHART,		"holeSize", GSF_XML_NO_CONTENT, NULL, NULL),

      GSF_XML_IN_NODE (PLOTAREA, DATA_TABLE, XL_NS_CHART, "dTable", GSF_XML_NO_CONTENT, NULL, NULL),
        GSF_XML_IN_NODE (DATA_TABLE, SHAPE_PR, XL_NS_CHART, "spPr", GSF_XML_NO_CONTENT, NULL, NULL),		/* 2nd Def */
        GSF_XML_IN_NODE (DATA_TABLE, TEXT_PR, XL_NS_CHART,  "txPr", GSF_XML_NO_CONTENT, NULL, NULL),		/* 2nd Def */
        GSF_XML_IN_NODE (DATA_TABLE, DT_SHOW_H_BORDER, XL_NS_CHART, "showHorzBorder", GSF_XML_NO_CONTENT, NULL, NULL),
        GSF_XML_IN_NODE (DATA_TABLE, DT_SHOW_V_BORDER, XL_NS_CHART, "showVertBorder", GSF_XML_NO_CONTENT, NULL, NULL),
        GSF_XML_IN_NODE (DATA_TABLE, DT_SHOW_KEYS, XL_NS_CHART, "showKeys", GSF_XML_NO_CONTENT, NULL, NULL),
        GSF_XML_IN_NODE (DATA_TABLE, DT_SHOW_OUTLINE, XL_NS_CHART, "showOutline", GSF_XML_NO_CONTENT, NULL, NULL),

    GSF_XML_IN_NODE (CHART, TITLE, XL_NS_CHART, "title", GSF_XML_NO_CONTENT, NULL, NULL),			/* 2nd Def */

    GSF_XML_IN_NODE (CHART, LEGEND, XL_NS_CHART, "legend", GSF_XML_NO_CONTENT, &xlsx_chart_legend, NULL),
      GSF_XML_IN_NODE (LEGEND, SHAPE_PR, XL_NS_CHART, "spPr", GSF_XML_NO_CONTENT, NULL, NULL),		/* 2nd Def */
      GSF_XML_IN_NODE (LEGEND, TEXT_PR, XL_NS_CHART, "txPr", GSF_XML_NO_CONTENT, NULL, NULL),		/* 2nd Def */
      GSF_XML_IN_NODE (LEGEND, LAYOUT, XL_NS_CHART, "layout", GSF_XML_NO_CONTENT, NULL, NULL),		/* 2nd Def */
      GSF_XML_IN_NODE (LEGEND, LEGEND_POS, XL_NS_CHART, "legendPos", GSF_XML_NO_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE (CHART, CHART_HIDDEN, XL_NS_CHART, "plotVisOnly", GSF_XML_NO_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE (CHART, CHART_BLANKS, XL_NS_CHART, "dispBlanksAs", GSF_XML_NO_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE (CHART, AUTO_TITLE_DEL, XL_NS_CHART, "autoTitleDeleted", GSF_XML_NO_CONTENT, NULL, NULL),

  GSF_XML_IN_NODE (CHART_SPACE, STYLE, XL_NS_CHART, "style", GSF_XML_NO_CONTENT, NULL, NULL),
  GSF_XML_IN_NODE (CHART_SPACE, PRINT_SETTINGS, XL_NS_CHART, "printSettings", GSF_XML_NO_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE (PRINT_SETTINGS, PAGE_SETUP, XL_NS_CHART, "pageSetup", GSF_XML_NO_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE (PRINT_SETTINGS, PAGE_MARGINS, XL_NS_CHART, "pageMargins", GSF_XML_NO_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE (PRINT_SETTINGS, HEADER_FOOTER, XL_NS_CHART, "headerFooter", GSF_XML_NO_CONTENT, NULL, NULL),
      GSF_XML_IN_NODE (HEADER_FOOTER, ODD_HEADER, XL_NS_SS, "oddHeader", GSF_XML_NO_CONTENT, NULL, NULL),
      GSF_XML_IN_NODE (HEADER_FOOTER, ODD_FOOTER, XL_NS_SS, "oddFooter", GSF_XML_NO_CONTENT, NULL, NULL),
  GSF_XML_IN_NODE (CHART_SPACE, LANG, XL_NS_CHART, "lang", GSF_XML_NO_CONTENT, NULL, NULL),
GSF_XML_IN_NODE_END
};

/***********************************************************************/

static void
cb_axis_set_position (GObject *axis, XLSXAxisInfo *info,
		      G_GNUC_UNUSED gpointer accum)
{
	g_object_set (axis, "pos", info->cross, NULL);
}

static void
xlsx_axis_cleanup (XLSXReadState *state)
{
	GSList *list, *ptr;

	/* clean out axis that were auto created */
	list = gog_object_get_children (GOG_OBJECT (state->chart), NULL);
	for (ptr = list; ptr != NULL ; ptr = ptr->next)
		if (IS_GOG_AXIS (ptr->data) &&
		    NULL == g_hash_table_lookup (state->axis.by_obj, ptr->data)) {
			if (gog_object_is_deletable (GOG_OBJECT (ptr->data))) {
				gog_object_clear_parent	(GOG_OBJECT (ptr->data));
				g_object_unref (G_OBJECT (ptr->data));
			}
		}
	g_slist_free (list);

	g_hash_table_foreach (state->axis.by_obj,
		(GHFunc)cb_axis_set_position, NULL);
	g_hash_table_destroy (state->axis.by_obj);
	g_hash_table_destroy (state->axis.by_id);
	state->axis.by_obj = state->axis.by_id = NULL;
}

static void
xlsx_read_chart (GsfXMLIn *xin, xmlChar const **attrs)
{
	XLSXReadState	*state = (XLSXReadState *)xin->user_state;
	xmlChar const *part_id = NULL;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (gsf_xml_in_namecmp (xin, attrs[0], XL_NS_DOC_REL, "id"))
			part_id = attrs[1];
	if (NULL != part_id) {
		/* leave it in 'state' for the frame to insert */
		state->so = sheet_object_graph_new (NULL);

		state->graph	 = sheet_object_graph_get_gog (state->so);
		state->cur_obj   = gog_object_add_by_name (GOG_OBJECT (state->graph), "Chart", NULL);
		state->chart	 = GOG_CHART (state->cur_obj);
		state->cur_style = NULL;
		state->obj_stack = NULL;
		state->dim_type  = GOG_MS_DIM_LABELS;
		state->axis.by_id  = g_hash_table_new_full (g_str_hash, g_str_equal,
			NULL, (GDestroyNotify) xlsx_axis_info_free);
		state->axis.by_obj = g_hash_table_new (g_direct_hash, g_direct_equal);
		xlsx_parse_rel_by_id (xin, part_id, xlsx_chart_dtd, xlsx_ns);

		if (NULL != state->obj_stack) {
			g_warning ("left over content on chart object stack");
			g_slist_free (state->obj_stack);
			state->obj_stack = NULL;
		}

		xlsx_axis_cleanup (state);
		if (NULL != state->cur_style) {
			g_warning ("left over style");
			g_object_unref (state->cur_style);
			state->cur_style = NULL;
		}
		state->cur_obj   = NULL;
		state->chart = NULL;
		state->graph = NULL;
	}
}

/**************************************************************************/
#define CELL	0
#define OFFSET	1
#define FROM	0
#define TO	4
#define COL	0
#define ROW	2

static void
xlsx_draw_anchor_start (GsfXMLIn *xin, xmlChar const **attrs)
{
	XLSXReadState	*state = (XLSXReadState *)xin->user_state;

	g_return_if_fail (state->so == NULL);

	memset ((gpointer)state->drawing_pos, 0, sizeof (state->drawing_pos));
	state->drawing_pos_flags = 0;
}

static void
xlsx_drawing_twoCellAnchor_end (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	XLSXReadState	*state = (XLSXReadState *)xin->user_state;

	if (NULL == state->so) {
		xlsx_warning (xin,
			_("Dropping missing object"));
	} else {
		if ((state->drawing_pos_flags & 0xFF) == 0xFF) {
			SheetObjectAnchor anchor;
			GnmRange r;

			range_init (&r,
				state->drawing_pos[COL | FROM],
				state->drawing_pos[ROW | FROM],
				state->drawing_pos[COL | TO],
				state->drawing_pos[ROW | TO]);

#warning implement absolute offsets
				sheet_object_anchor_init (&anchor, &r, NULL, GOD_ANCHOR_DIR_DOWN_RIGHT);
				sheet_object_set_anchor (state->so, &anchor);
				sheet_object_set_sheet (state->so, state->sheet);
		} else
			xlsx_warning (xin,
				_("Dropping object with incomplete anchor %2x"), state->drawing_pos_flags);

		g_object_unref (state->so);
		state->so = NULL;
	}
}

static void
xlsx_drawing_oneCellAnchor_end (GsfXMLIn *xin, GsfXMLBlob *blob)
{
	XLSXReadState	*state = (XLSXReadState *)xin->user_state;

	state->drawing_pos[COL | TO] = state->drawing_pos[COL | FROM] + 5;
	state->drawing_pos[ROW | TO] = state->drawing_pos[ROW | FROM] + 5;
	state->drawing_pos_flags |= ((1 << (COL | TO)) | (1 << (ROW | TO)));
	xlsx_drawing_twoCellAnchor_end (xin, blob);
}

static void
xlsx_drawing_ext (GsfXMLIn *xin, xmlChar const **attrs)
{
	XLSXReadState	*state = (XLSXReadState *)xin->user_state;
	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (attr_int64 (xin, attrs, "cx", state->drawing_pos + (COL | TO | OFFSET)))
			state->drawing_pos_flags |= (1 << (COL | TO | OFFSET));
		else if (attr_int64 (xin, attrs, "cy", state->drawing_pos + (ROW | TO | OFFSET)))
			state->drawing_pos_flags |= (1 << (ROW | TO | OFFSET));
}

static void
xlsx_drawing_pos (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	XLSXReadState	*state = (XLSXReadState *)xin->user_state;
	gint64 val;
	char  *end;

	errno = 0;
	val = g_ascii_strtoll (xin->content->str, &end, 10);
	if (errno == ERANGE || end == xin->content->str || *end != '\0')
		return;

	state->drawing_pos[xin->node->user_data.v_int] = val;
	state->drawing_pos_flags |= 1 << xin->node->user_data.v_int;
#if 0
	fprintf (stderr, "%s %s %s = %" G_GINT64_FORMAT "\n",
		 (xin->node->user_data.v_int & TO) ? "To" : "From",
		 (xin->node->user_data.v_int & ROW) ? "Row" : "Col",
		 (xin->node->user_data.v_int & OFFSET) ? "Offset" : "",
		 val);
#endif
}

static GsfXMLInNode const xlsx_drawing_dtd[] = {
GSF_XML_IN_NODE_FULL (START, START, -1, NULL, GSF_XML_NO_CONTENT, FALSE, TRUE, NULL, NULL, 0),
GSF_XML_IN_NODE_FULL (START, DRAWING, XL_NS_SS_DRAW, "wsDr", GSF_XML_NO_CONTENT, FALSE, TRUE, NULL, NULL, 0),

  GSF_XML_IN_NODE (DRAWING, TWO_CELL, XL_NS_SS_DRAW, "twoCellAnchor", GSF_XML_NO_CONTENT,
		   &xlsx_draw_anchor_start, &xlsx_drawing_twoCellAnchor_end),
    GSF_XML_IN_NODE (TWO_CELL, ANCHOR_FROM, XL_NS_SS_DRAW, "from", GSF_XML_NO_CONTENT, NULL, NULL),
      GSF_XML_IN_NODE_FULL (ANCHOR_FROM, ANCHOR_FROM_COL,	XL_NS_SS_DRAW, "col",	 GSF_XML_CONTENT, FALSE, TRUE, NULL, &xlsx_drawing_pos, FROM | COL | CELL),
      GSF_XML_IN_NODE_FULL (ANCHOR_FROM, ANCHOR_FROM_COL_OFF,	XL_NS_SS_DRAW, "colOff", GSF_XML_CONTENT, FALSE, TRUE, NULL, &xlsx_drawing_pos, FROM | COL | OFFSET),
      GSF_XML_IN_NODE_FULL (ANCHOR_FROM, ANCHOR_FROM_ROW,	XL_NS_SS_DRAW, "row",    GSF_XML_CONTENT, FALSE, TRUE, NULL, &xlsx_drawing_pos, FROM | ROW | CELL),
      GSF_XML_IN_NODE_FULL (ANCHOR_FROM, ANCHOR_FROM_ROW_OFF,	XL_NS_SS_DRAW, "rowOff", GSF_XML_CONTENT, FALSE, TRUE, NULL, &xlsx_drawing_pos, FROM | ROW | OFFSET),
    GSF_XML_IN_NODE (TWO_CELL, TWO_CELL_TO, XL_NS_SS_DRAW, "to", GSF_XML_NO_CONTENT, NULL, NULL),
      GSF_XML_IN_NODE_FULL (TWO_CELL_TO, TWO_CELL_TO_COL,	XL_NS_SS_DRAW, "col",    GSF_XML_CONTENT, FALSE, TRUE, NULL, &xlsx_drawing_pos, TO | COL | CELL),
      GSF_XML_IN_NODE_FULL (TWO_CELL_TO, TWO_CELL_TO_COL_OFF,	XL_NS_SS_DRAW, "colOff", GSF_XML_CONTENT, FALSE, TRUE, NULL, &xlsx_drawing_pos, TO | COL | OFFSET),
      GSF_XML_IN_NODE_FULL (TWO_CELL_TO, TWO_CELL_TO_ROW,	XL_NS_SS_DRAW, "row",	 GSF_XML_CONTENT, FALSE, TRUE, NULL, &xlsx_drawing_pos, TO | ROW | CELL),
      GSF_XML_IN_NODE_FULL (TWO_CELL_TO, TWO_CELL_TO_ROW_OFF,	XL_NS_SS_DRAW, "rowOff", GSF_XML_CONTENT, FALSE, TRUE, NULL, &xlsx_drawing_pos, TO | ROW | OFFSET),
#undef FROM
#undef TO
#undef COL
#undef ROW
#undef CELL
#undef OFFSET

    GSF_XML_IN_NODE (TWO_CELL, GRAPHIC_FRAME, XL_NS_SS_DRAW, "graphicFrame", GSF_XML_NO_CONTENT, NULL, NULL),
      GSF_XML_IN_NODE (GRAPHIC_FRAME, GRAPHIC_PR, XL_NS_SS_DRAW, "nvGraphicFramePr", GSF_XML_NO_CONTENT, NULL, NULL),
        GSF_XML_IN_NODE (GRAPHIC_PR, CNVPR, XL_NS_SS_DRAW, "cNvPr", GSF_XML_NO_CONTENT, NULL, NULL),
        GSF_XML_IN_NODE (GRAPHIC_PR, GRAPHIC_PR_CHILD, XL_NS_SS_DRAW, "cNvGraphicFramePr", GSF_XML_NO_CONTENT, NULL, NULL),
          GSF_XML_IN_NODE (GRAPHIC_PR_CHILD, GRAPHIC_LOCKS, XL_NS_DRAW, "graphicFrameLocks", GSF_XML_NO_CONTENT, NULL, NULL),
      GSF_XML_IN_NODE (GRAPHIC_FRAME, GRAPHIC, XL_NS_DRAW, "graphic", GSF_XML_NO_CONTENT, NULL, NULL),
        GSF_XML_IN_NODE_FULL (GRAPHIC, GRAPHIC_DATA, XL_NS_DRAW, "graphicData",
			      GSF_XML_NO_CONTENT, FALSE, TRUE, NULL, NULL, 0),
          GSF_XML_IN_NODE (GRAPHIC_DATA, CHART, XL_NS_CHART, "chart", GSF_XML_NO_CONTENT, &xlsx_read_chart, NULL),
          GSF_XML_IN_NODE (GRAPHIC_DATA, GRAPHIC_PR_CHILD, XL_NS_SS_DRAW, "cNvGraphicFramePr", GSF_XML_NO_CONTENT, NULL, NULL),	/* 2nd Def */
      GSF_XML_IN_NODE (GRAPHIC_FRAME, TWO_CELL_XFRM, XL_NS_SS_DRAW, "xfrm", GSF_XML_NO_CONTENT, NULL, NULL),
        GSF_XML_IN_NODE (TWO_CELL_XFRM, XFRM_OFF, XL_NS_DRAW, "off", GSF_XML_NO_CONTENT, NULL, NULL),
        GSF_XML_IN_NODE (TWO_CELL_XFRM, XFRM_EXT, XL_NS_DRAW, "ext", GSF_XML_NO_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE (TWO_CELL, CLIENT_DATA, XL_NS_SS_DRAW, "clientData", GSF_XML_NO_CONTENT, NULL, NULL),

  GSF_XML_IN_NODE (DRAWING, ONE_CELL, XL_NS_SS_DRAW, "oneCellAnchor", GSF_XML_NO_CONTENT,
		   &xlsx_draw_anchor_start, &xlsx_drawing_oneCellAnchor_end),
    GSF_XML_IN_NODE (ONE_CELL, ANCHOR_FROM, XL_NS_SS_DRAW, "from", GSF_XML_NO_CONTENT, NULL, NULL),		/* 2nd Def */
    GSF_XML_IN_NODE (ONE_CELL, ONE_CELL_EXT, XL_NS_SS_DRAW, "ext", GSF_XML_NO_CONTENT, &xlsx_drawing_ext, NULL),
    GSF_XML_IN_NODE (ONE_CELL, CLIENT_DATA, XL_NS_SS_DRAW, "clientData", GSF_XML_NO_CONTENT, NULL, NULL),	/* 2nd Def */
    GSF_XML_IN_NODE (ONE_CELL, GRAPHIC_FRAME, XL_NS_SS_DRAW, "graphicFrame", GSF_XML_NO_CONTENT, NULL, NULL),	/* 2nd Def */
GSF_XML_IN_NODE_END
};

/***********************************************************************/

static GnmColor *
elem_color (GsfXMLIn *xin, xmlChar const **attrs)
{
	int indx;
	guint a, r, g, b;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2) {
		if (0 == strcmp (attrs[0], "rgb")) {
			if (4 != sscanf (attrs[1], "%02x%02x%02x%02x", &a, &r, &g, &b)) {
				xlsx_warning (xin,
					_("Invalid color '%s' for attribute rgb"),
					attrs[1]);
				return NULL;
			}

			return style_color_new_i8 (r, g, b);
		} else if (attr_int (xin, attrs, "indexed", &indx))
			return indexed_color (indx);
#if 0
	"type"	opt rgb {auto, icv, rgb, theme }
	"val"	opt ??
	"tint"	opt 0.
#endif
	}
	return NULL;
}

static GnmStyle *
xlsx_get_xf (GsfXMLIn *xin, int xf)
{
	XLSXReadState	*state = (XLSXReadState *)xin->user_state;
	if (0 <= xf && NULL != state->xfs && xf < (int)state->xfs->len)
		return g_ptr_array_index (state->xfs, xf);
	xlsx_warning (xin, _("Undefined style record '%d'"), xf);
	return NULL;
}
static GnmStyle *
xlsx_get_dxf (GsfXMLIn *xin, int dxf)
{
	XLSXReadState	*state = (XLSXReadState *)xin->user_state;
	if (0 <= dxf && NULL != state->dxfs && dxf < (int)state->dxfs->len)
		return g_ptr_array_index (state->dxfs, dxf);
	xlsx_warning (xin, _("Undefined partial style record '%d'"), dxf);
	return NULL;
}

/****************************************************************************/

static void
xlsx_cell_val_end (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	XLSXReadState	*state = (XLSXReadState *)xin->user_state;
	XLSXStr const	*entry;
	char		*end;
	long		 i;

	switch (state->pos_type) {
	case XLXS_TYPE_NUM :
		if (*xin->content->str)
			state->val = value_new_float (gnm_strto (xin->content->str, &end));
		break;
	case XLXS_TYPE_SST_STR :
		i = strtol (xin->content->str, &end, 10);
		if (end != xin->content->str && *end == '\0' &&
		    0 <= i  && i < (int)state->sst->len) {
			entry = &g_array_index (state->sst, XLSXStr, i);
			gnm_string_ref (entry->str);
			state->val = value_new_string_str (entry->str);
			if (NULL != entry->markup)
				value_set_fmt (state->val, entry->markup);
		} else {
			xlsx_warning (xin, _("Invalid sst ref '%s'"), xin->content->str);
		}
		break;
	case XLXS_TYPE_BOOL :
		if (*xin->content->str)
			state->val = value_new_bool (*xin->content->str != '0');
		break;
	case XLXS_TYPE_ERR :
		if (*xin->content->str)
			state->val = value_new_error (NULL, xin->content->str);
		break;

	case XLXS_TYPE_STR2 : /* What is this ? */
	case XLXS_TYPE_INLINE_STR :
		state->val = value_new_string (xin->content->str);
		break;
	default :
		g_warning ("Unknown val type %d", state->pos_type);
	}
}

static void
xlsx_cell_expr_begin (GsfXMLIn *xin, xmlChar const **attrs)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	gboolean has_range = FALSE, is_array = FALSE;
	GnmRange range;
	xmlChar const *shared_id = NULL;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (0 == strcmp (attrs[0], "t")) {
			if (0 == strcmp (attrs[1], "array"))
				is_array = TRUE;
		} else if (0 == strcmp (attrs[0], "si"))
			shared_id = attrs[1];
		else if (attr_range (xin, attrs, "ref", &range))
			has_range = TRUE;

	state->shared_id = NULL;
	if (NULL != shared_id) {
		state->texpr = g_hash_table_lookup (state->shared_exprs, shared_id);
		if (NULL != state->texpr)
			gnm_expr_top_ref (state->texpr);
		else
			state->shared_id = g_strdup (shared_id);
	} else
		state->texpr = NULL;

	/* if the shared expr is already parsed expression do not even collect content */
	((GsfXMLInNode *)(xin->node))->has_content =
		(NULL != state->texpr) ? GSF_XML_NO_CONTENT : GSF_XML_CONTENT;

	if (is_array && has_range)
		state->array = range;
}

static void
xlsx_cell_expr_end (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	GnmParsePos pp;

	if (NULL == state->texpr) {
		parse_pos_init (&pp, NULL, state->sheet,
			state->pos.col, state->pos.row);
		state->texpr = xlsx_parse_expr (xin, xin->content->str, &pp);
		if (NULL != state->texpr &&
		    NULL != state->shared_id) {
			gnm_expr_top_ref (state->texpr);
			g_hash_table_replace (state->shared_exprs,
				state->shared_id, (gpointer)state->texpr);
			state->shared_id = NULL;
		}
	}
	g_free (state->shared_id);
	state->shared_id = NULL;
}

static void
xlsx_cell_begin (GsfXMLIn *xin, xmlChar const **attrs)
{
	static EnumVal const types[] = {
		{ "n",		XLXS_TYPE_NUM },
		{ "s",		XLXS_TYPE_SST_STR },
		{ "str",	XLXS_TYPE_STR2 },
		{ "b",		XLXS_TYPE_BOOL },
		{ "inlineStr",	XLXS_TYPE_INLINE_STR },
		{ "e",		XLXS_TYPE_ERR },
		{ NULL, 0 }
	};
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	int tmp;
	GnmStyle *style = NULL;

	state->pos.col = state->pos.row = -1;
	state->pos_type = XLXS_TYPE_NUM; /* the default */
	state->val = NULL;
	state->texpr = NULL;
	range_init (&state->array, -1, -1, -1, -1); /* invalid */

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (attr_pos (xin, attrs, "r", &state->pos)) ;
		else if (attr_enum (xin, attrs, "t", types, &tmp))
			state->pos_type = tmp;
		else if (attr_int (xin, attrs, "s", &tmp))
			style = xlsx_get_xf (xin, tmp);

	if (NULL != style) {
		gnm_style_ref (style);
		sheet_style_set_pos (state->sheet,
			state->pos.col, state->pos.row, style);
	}
}
static void
xlsx_cell_end (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	GnmCell *cell = sheet_cell_fetch (state->sheet,
		state->pos.col, state->pos.row);

	if (NULL == cell) {
		xlsx_warning (xin, _("Invalid cell %s"),
			cellpos_as_string (&state->pos));
		if (NULL != state->val)
			value_release (state->val);
		if (NULL != state->texpr)
			gnm_expr_top_unref (state->texpr);
	} else if (NULL != state->texpr) {
		if (state->array.start.col >= 0) {
			gnm_cell_set_array_formula (state->sheet,
				state->array.start.col,
				state->array.start.row,
				state->array.end.col,
				state->array.end.row,
				state->texpr);
			if (NULL != state->val)
				gnm_cell_assign_value (cell, state->val);
		} else if (NULL != state->val) {
			gnm_cell_set_expr_and_value	(cell,
				state->texpr, state->val, TRUE);
			gnm_expr_top_unref (state->texpr);
		} else {
			gnm_cell_set_expr (cell, state->texpr);
			gnm_expr_top_unref (state->texpr);
		}
		state->texpr = NULL;
	} else if (NULL != state->val)
		gnm_cell_assign_value (cell, state->val);
	state->val = NULL;
}

static void
xlsx_CT_Row (GsfXMLIn *xin, xmlChar const **attrs)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	int row = -1, xf_index;
	double h = -1.;
	int cust_fmt = FALSE, cust_height = FALSE, collapsed = FALSE;
	int hidden = -1;
	int outline = -1;
	GnmStyle *style = NULL;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (attr_int (xin, attrs, "r", &row)) ;
		else if (attr_float (xin, attrs, "ht", &h)) ;
		else if (attr_bool (xin, attrs, "customFormat", &cust_fmt)) ;
		else if (attr_bool (xin, attrs, "customHeight", &cust_height)) ;
		else if (attr_int (xin, attrs, "s", &xf_index))
			style = xlsx_get_xf (xin, xf_index);
		else if (attr_int (xin, attrs, "outlineLevel", &outline)) ;
		else if (attr_bool (xin, attrs, "hidden", &hidden)) ;
		else if (attr_bool (xin, attrs, "collapsed", &collapsed)) ;

	if (row > 0) {
		row--;
		if (h >= 0.)
			sheet_row_set_size_pts (state->sheet, row, h, cust_height);
		if (hidden > 0)
			colrow_set_visibility (state->sheet, FALSE, FALSE, row, row);
		if (outline >= 0)
			colrow_set_outline (sheet_row_fetch (state->sheet, row),
				outline, collapsed);

		if (NULL != style) {
			GnmRange r;
			r.start.row = r.end.row = row;
			r.start.col = 0;
			r.end.col  = gnm_sheet_get_max_cols (state->sheet) - 1;
			gnm_style_ref (style);
			sheet_style_set_range (state->sheet, &r, style);
		}
	}
}

static void
xlsx_CT_Col (GsfXMLIn *xin, xmlChar const **attrs)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	int first = -1, last = -1, xf_index;
	double width = -1.;
	gboolean cust_width = FALSE, best_fit = FALSE, collapsed = FALSE;
	int i, hidden = -1;
	int outline = -1;
	GnmStyle *style = NULL;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (attr_int (xin, attrs, "min", &first)) ;
		else if (attr_int (xin, attrs, "max", &last)) ;
		else if (attr_float (xin, attrs, "width", &width))
			/* FIXME FIXME FIXME arbitrary map from 130 pixels to
			 * the value stored for a column with 130 pixel width*/
			width *= (130. / 18.5703125) * (72./96.);
		else if (attr_bool (xin, attrs, "customWidth", &cust_width)) ;
		else if (attr_bool (xin, attrs, "bestFit", &best_fit)) ;
		else if (attr_int (xin, attrs, "style", &xf_index))
			style = xlsx_get_xf (xin, xf_index);
		else if (attr_int (xin, attrs, "outlineLevel", &outline)) ;
		else if (attr_bool (xin, attrs, "hidden", &hidden)) ;
		else if (attr_bool (xin, attrs, "collapsed", &collapsed)) ;

	if (first < 0) {
		if (last < 0) {
			xlsx_warning (xin, _("Ignoring column information that does not specify first or last."));
			return;
		}
		first = --last;
	} else if (last < 0)
		last = --first;
	else {
		first--;
		last--;
	}


	if (last >= gnm_sheet_get_max_cols (state->sheet))
		last = gnm_sheet_get_max_cols (state->sheet) - 1;
	for (i = first; i <= last; i++) {
		if (width > 4)
			sheet_col_set_size_pts (state->sheet, i, width,
				cust_width && !best_fit);
		if (outline > 0)
			colrow_set_outline (sheet_col_fetch (state->sheet, i),
				outline, collapsed);
	}
	if (NULL != style) {
		GnmRange r;
		r.start.col = first;
		r.end.col   = last;
		r.start.row = 0;
		r.end.row  = gnm_sheet_get_max_rows (state->sheet) - 1;
		gnm_style_ref (style);
		sheet_style_set_range (state->sheet, &r, style);
	}
	if (hidden > 0)
		colrow_set_visibility (state->sheet, TRUE, FALSE, first, last);
}

static void
xlsx_sheet_tabcolor (GsfXMLIn *xin, xmlChar const **attrs)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	GnmColor *text_color, *color = elem_color (xin, attrs);
	if (NULL != color) {
		int contrast = color->gdk_color.red +
			color->gdk_color.green +
			color->gdk_color.blue;
		if (contrast >= 0x18000)
			text_color = style_color_black ();
		else
			text_color = style_color_white ();
		g_object_set (state->sheet,
			      "tab-foreground", text_color,
			      "tab-background", color,
			      NULL);
		style_color_unref (text_color);
		style_color_unref (color);
	}
}

static void
xlsx_sheet_page_setup (GsfXMLIn *xin, xmlChar const **attrs)
{
	/* XLSXReadState *state = (XLSXReadState *)xin->user_state; */
}

static void
xlsx_CT_SheetFormatPr (GsfXMLIn *xin, xmlChar const **attrs)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	double h;
	int i;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (attr_float (xin, attrs, "defaultRowHeight", &h))
			sheet_row_set_default_size_pts (state->sheet, h);
		else if (attr_int (xin, attrs, "outlineLevelRow", &i)) {
			if (i > 0)
				sheet_colrow_gutter (state->sheet, FALSE, i);
		} else if (attr_int (xin, attrs, "outlineLevelCol", &i)) {
			if (i > 0)
				sheet_colrow_gutter (state->sheet, TRUE, i);
		}
}

static void
xlsx_CT_PageMargins (GsfXMLIn *xin, xmlChar const **attrs)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	double margin;
	PrintInformation *pi = state->sheet->print_info;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (attr_float (xin, attrs, "left", &margin))
			print_info_set_margin_left (pi, GO_IN_TO_PT (margin));
		else if (attr_float (xin, attrs, "right", &margin))
			print_info_set_margin_right (pi, GO_IN_TO_PT (margin));
		else if (attr_float (xin, attrs, "top", &margin))
			print_info_set_edge_to_below_header (pi, GO_IN_TO_PT (margin));
		else if (attr_float (xin, attrs, "bottom", &margin))
			print_info_set_edge_to_above_footer (pi, GO_IN_TO_PT (margin));
		else if (attr_float (xin, attrs, "header", &margin))
			print_info_set_margin_header (pi, GO_IN_TO_PT (margin));
		else if (attr_float (xin, attrs, "footer", &margin))
			print_info_set_margin_footer (pi, GO_IN_TO_PT (margin));
}

static void
xlsx_CT_PageBreak (GsfXMLIn *xin, xmlChar const **attrs)
{
	XLSXReadState    *state = (XLSXReadState *)xin->user_state;
	GnmPageBreakType  type = GNM_PAGE_BREAK_AUTO;
	gboolean tmp;
	int	 pos;

	if (NULL == state->page_breaks)
		return;

	pos = 0;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (attr_int  (xin, attrs, "id", &pos)) ;
		else if (attr_bool (xin, attrs, "man", &tmp)) { if (tmp) type = GNM_PAGE_BREAK_MANUAL; }
		else if (attr_bool (xin, attrs, "pt", &tmp))  { if (tmp) type = GNM_PAGE_BREAK_DATA_SLICE; }
#if 0 /* Ignored */
		else if (attr_int  (xin, attrs, "min", &first)) ;
		else if (attr_int  (xin, attrs, "max", &last)) ;
#endif

	gnm_page_breaks_append_break (state->page_breaks, pos, type);
}

static void
xlsx_CT_PageBreaks_begin (GsfXMLIn *xin, xmlChar const **attrs)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	int count = 0;

	g_return_if_fail (state->page_breaks == NULL);

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (attr_int  (xin, attrs, "count", &count)) ;
#if 0 /* Ignored */
		else if (attr_int  (xin, attrs, "manualBreakCount", &manual_count)) ;
#endif

	state->page_breaks = gnm_page_breaks_new (count,
		xin->node->user_data.v_int);
}

static void
xlsx_CT_PageBreaks_end (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;

	if (NULL != state->page_breaks) {
		print_info_set_breaks (state->sheet->print_info,
			state->page_breaks);
		state->page_breaks = NULL;
	}
}

static void
xlsx_CT_DataValidation_begin (GsfXMLIn *xin, xmlChar const **attrs)
{
	static EnumVal const val_styles[] = {
		{ "stop",	 VALIDATION_STYLE_STOP },
		{ "warning",	 VALIDATION_STYLE_WARNING },
		{ "information", VALIDATION_STYLE_INFO },
		{ NULL, 0 }
	};
	static EnumVal const val_types[] = {
		{ "none",	VALIDATION_TYPE_ANY },
		{ "whole",	VALIDATION_TYPE_AS_INT },
		{ "decimal",	VALIDATION_TYPE_AS_NUMBER },
		{ "list",	VALIDATION_TYPE_IN_LIST },
		{ "date",	VALIDATION_TYPE_AS_DATE },
		{ "time",	VALIDATION_TYPE_AS_TIME },
		{ "textLength",	VALIDATION_TYPE_TEXT_LENGTH },
		{ "custom",	VALIDATION_TYPE_CUSTOM },
		{ NULL, 0 }
	};
	static EnumVal const val_ops[] = {
		{ "between",	VALIDATION_OP_BETWEEN },
		{ "notBetween",	VALIDATION_OP_NOT_BETWEEN },
		{ "equal",	VALIDATION_OP_EQUAL },
		{ "notEqual",	VALIDATION_OP_NOT_EQUAL },
		{ "lessThan",		VALIDATION_OP_LT },
		{ "lessThanOrEqual",	VALIDATION_OP_LTE },
		{ "greaterThan",	VALIDATION_OP_GT },
		{ "greaterThanOrEqual",	VALIDATION_OP_GTE },
		{ NULL, 0 }
	};
#if 0
	/* Get docs on this */
	"imeMode" default="noControl"
		"noControl"
		"off"
		"on"
		"disabled"
		"hiragana"
		"fullKatakana"
		"halfKatakana"
		"fullAlpha"
		"halfAlpha"
		"fullHangul"
		"halfHangul"
#endif

	XLSXReadState *state = (XLSXReadState *)xin->user_state;

	/* defaults */
	ValidationStyle	val_style = VALIDATION_STYLE_STOP;
	ValidationType	val_type  = VALIDATION_TYPE_ANY;
	ValidationOp	val_op	  = VALIDATION_OP_BETWEEN;
	gboolean allowBlank = FALSE;
	gboolean showDropDown = FALSE;
	gboolean showInputMessage = FALSE;
	gboolean showErrorMessage = FALSE;
	xmlChar const *errorTitle = NULL;
	xmlChar const *error = NULL;
	xmlChar const *promptTitle = NULL;
	xmlChar const *prompt = NULL;
	xmlChar const *refs = NULL;
	int tmp;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (0 == strcmp (attrs[0], "sqref"))
			refs = attrs[1];
		else if (attr_enum (xin, attrs, "errorStyle", val_styles, &tmp))
			val_style = tmp;
		else if (attr_enum (xin, attrs, "type", val_types, &tmp))
			val_type = tmp;
		else if (attr_enum (xin, attrs, "operator", val_ops, &tmp))
			val_op = tmp;

		else if (attr_bool (xin, attrs, "allowBlank", &allowBlank)) ;
		else if (attr_bool (xin, attrs, "showDropDown", &showDropDown)) ;
		else if (attr_bool (xin, attrs, "showInputMessage", &showInputMessage)) ;
		else if (attr_bool (xin, attrs, "showErrorMessage", &showErrorMessage)) ;

		else if (0 == strcmp (attrs[0], "errorTitle"))
			errorTitle = attrs[1];
		else if (0 == strcmp (attrs[0], "error"))
			error = attrs[1];
		else if (0 == strcmp (attrs[0], "promptTitle"))
			promptTitle = attrs[1];
		else if (0 == strcmp (attrs[0], "prompt"))
			prompt = attrs[1];

	/* order matters, we need the 1st item */
	state->validation_regions = g_slist_reverse (
		xlsx_parse_sqref (xin, refs));

	if (NULL == state->validation_regions)
		return;

	if (showErrorMessage) {
		GnmRange const *r = state->validation_regions->data;
		state->pos = r->start;
		state->validation = validation_new (val_style, val_type, val_op,
			errorTitle, error, NULL, NULL, allowBlank, showDropDown);
	}

	if (showInputMessage && (NULL != promptTitle || NULL != prompt))
		state->input_msg = gnm_input_msg_new (prompt, promptTitle);
}

static void
xlsx_CT_DataValidation_end (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	GError   *err;
	GnmStyle *style = NULL;
	GSList   *ptr;

	if (NULL != state->validation &&
	    NULL != (err = validation_is_ok (state->validation))) {
		xlsx_warning (xin, _("Ignoring invalid data validation because : %s"),
			      _(err->message));
		validation_unref (state->validation);
		state->validation = NULL;
	}

	if (NULL != state->validation) {
		style = gnm_style_new ();
		gnm_style_set_validation (style, state->validation);
		state->validation = NULL;
	}

	if (NULL != state->input_msg) {
		if (NULL == style)
			style = gnm_style_new ();
		gnm_style_set_input_msg (style, state->input_msg);
		state->input_msg = NULL;
	}

	for (ptr = state->validation_regions ; ptr != NULL ; ptr = ptr->next) {
		if (NULL != style) {
			gnm_style_ref (style);
			sheet_style_apply_range	(state->sheet, ptr->data, style);
		}
		g_free (ptr->data);
	}
	if (NULL != style)
		gnm_style_unref (style);
	g_slist_free (state->validation_regions);
	state->validation_regions = NULL;
	state->pos.col = state->pos.row = -1;
}

static void
xlsx_validation_expr (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	GnmParsePos pp;
	GnmExprTop const *texpr;

	/*  Sneaky buggers, parse relative to the 1st sqRef */
	parse_pos_init (&pp, NULL, state->sheet,
		state->pos.col, state->pos.row);
	texpr = xlsx_parse_expr (xin, xin->content->str, &pp);
	if (NULL != texpr) {
		validation_set_expr (state->validation, texpr,
			xin->node->user_data.v_int);
		gnm_expr_top_unref (texpr);
	}
}

static void
xlsx_CT_AutoFilter_begin (GsfXMLIn *xin, xmlChar const **attrs)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	GnmRange r;

	g_return_if_fail (state->filter == NULL);

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (attr_range (xin, attrs, "ref", &r))
			state->filter = gnm_filter_new (state->sheet, &r);
}

static void
xlsx_CT_AutoFilter_end (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	g_return_if_fail (state->filter != NULL);
	state->filter = NULL;
}

static void
xlsx_CT_FilterColumn_begin (GsfXMLIn *xin, xmlChar const **attrs)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	int id = -1;
	gboolean hidden = FALSE;
	gboolean show = TRUE;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (attr_int  (xin, attrs, "colId", &id)) ;
		else if (attr_bool (xin, attrs, "hiddenButton", &hidden)) ;
		else if (attr_bool (xin, attrs, "showButton", &show)) ;

	state->filter_cur_field = id;
}

static void
xlsx_CT_Filters_begin (GsfXMLIn *xin, xmlChar const **attrs)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (0 == strcmp (attrs[0], "val")) {
		}
	state->filter_items = NULL;
}
static void
xlsx_CT_Filters_end (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	state->filter_items = NULL;
}
static void
xlsx_CT_Filter (GsfXMLIn *xin, xmlChar const **attrs)
{
#if 0
	XLSXReadState *state = (XLSXReadState *)xin->user_state;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (0 == strcmp (attrs[0], "val")) {
		}
#endif
}

static void
xlsx_CT_CustomFilters_begin (GsfXMLIn *xin, xmlChar const **attrs)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;

#if 0
	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (0 == strcmp (attrs[0], "val")) {
		}
#endif
	state->filter_items = NULL;
}
static void
xlsx_CT_CustomFilters_end (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	state->filter_items = NULL;
}

static void
xlsx_CT_CustomFilter (GsfXMLIn *xin, xmlChar const **attrs)
{
#if 0
	static EnumVal const ops[] = {
		{ "lessThan",		GNM_STYLE_COND_LT },
		{ "lessThanOrEqual",	GNM_STYLE_COND_LTE },
		{ "equal",		GNM_STYLE_COND_EQUAL },
		{ "notEqual",		GNM_STYLE_COND_NOT_EQUAL },
		{ "greaterThanOrEqual",	GNM_STYLE_COND_GTE },
		{ "greaterThan",	GNM_STYLE_COND_GT },
		{ NULL, 0 }
	};
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	int tmp;
	GnmFilterOp op = GNM_STYLE_COND_EQUAL;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (0 == strcmp (attrs[0], "val")) {
		} else if (attr_enum (xin, attrs, "operator", ops, &tmp))
			op = tmp;
#endif
}

static void
xlsx_CT_Top10 (GsfXMLIn *xin, xmlChar const **attrs)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	gboolean top = TRUE;
	gboolean percent = FALSE;
	double val = -1.;
	GnmFilterCondition *cond;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (attr_float (xin, attrs, "val", &val)) ;
		else if (attr_bool (xin, attrs, "top", &top)) ;
		else if (attr_bool (xin, attrs, "percent", &percent)) ;

	if (NULL != (cond = gnm_filter_condition_new_bucket (top, !percent, val)))
		gnm_filter_set_condition (state->filter, state->filter_cur_field,
			cond, FALSE);
}

static void
xlsx_CT_DynamicFilter (GsfXMLIn *xin, xmlChar const **attrs)
{
#if 0
	static EnumVal const types[] = {
		{ "null", 0 },
		{ "aboveAverage", 0 },
		{ "belowAverage", 0 },
		{ "tomorrow", 0 },
		{ "today", 0 },
		{ "yesterday", 0 },
		{ "nextWeek", 0 },
		{ "thisWeek", 0 },
		{ "lastWeek", 0 },
		{ "nextMonth", 0 },
		{ "thisMonth", 0 },
		{ "lastMonth", 0 },
		{ "nextQuarter", 0 },
		{ "thisQuarter", 0 },
		{ "lastQuarter", 0 },
		{ "nextYear", 0 },
		{ "thisYear", 0 },
		{ "lastYear", 0 },
		{ "yearToDate", 0 },
		{ "Q1", 0 },
		{ "Q2", 0 },
		{ "Q3", 0 },
		{ "Q4", 0 },
		{ "M1", 0 },
		{ "M2", 0 },
		{ "M3", 0 },
		{ "M4", 0 },
		{ "M5", 0 },
		{ "M6", 0 },
		{ "M7", 0 },
		{ "M8", 0 },
		{ "M9", 0 },
		{ "M10", 0 },
		{ "M11", 0 },
		{ "M12", 0 },
		{ NULL, 0 }
	};
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	int type = -1;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (attr_enum (xin, attrs, "type", types, &type)) ;
#endif
}

static void
xlsx_CT_MergeCell (GsfXMLIn *xin, xmlChar const **attrs)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	GnmRange r;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (attr_range (xin, attrs, "ref", &r))
			gnm_sheet_merge_add (state->sheet, &r, FALSE,
				GO_CMD_CONTEXT (state->context));
}

static void
xlsx_CT_SheetProtection (GsfXMLIn *xin, xmlChar const **attrs)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	gboolean sheet			= FALSE;
	gboolean objects		= FALSE;
	gboolean scenarios		= FALSE;
	gboolean formatCells		= TRUE;
	gboolean formatColumns		= TRUE;
	gboolean formatRows		= TRUE;
	gboolean insertColumns		= TRUE;
	gboolean insertRows		= TRUE;
	gboolean insertHyperlinks	= TRUE;
	gboolean deleteColumns		= TRUE;
	gboolean deleteRows		= TRUE;
	gboolean selectLockedCells	= FALSE;
	gboolean sort			= TRUE;
	gboolean autoFilter		= TRUE;
	gboolean pivotTables		= TRUE;
	gboolean selectUnlockedCells	= FALSE;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (attr_bool (xin, attrs, "sheet", &sheet)) ;
		else if (attr_bool (xin, attrs, "objects", &objects)) ;
		else if (attr_bool (xin, attrs, "scenarios", &scenarios)) ;
		else if (attr_bool (xin, attrs, "formatCells", &formatCells)) ;
		else if (attr_bool (xin, attrs, "formatColumns", &formatColumns)) ;
		else if (attr_bool (xin, attrs, "formatRows", &formatRows)) ;
		else if (attr_bool (xin, attrs, "insertColumns", &insertColumns)) ;
		else if (attr_bool (xin, attrs, "insertRows", &insertRows)) ;
		else if (attr_bool (xin, attrs, "insertHyperlinks", &insertHyperlinks)) ;
		else if (attr_bool (xin, attrs, "deleteColumns", &deleteColumns)) ;
		else if (attr_bool (xin, attrs, "deleteRows", &deleteRows)) ;
		else if (attr_bool (xin, attrs, "selectLockedCells", &selectLockedCells)) ;
		else if (attr_bool (xin, attrs, "sort", &sort)) ;
		else if (attr_bool (xin, attrs, "autoFilter", &autoFilter)) ;
		else if (attr_bool (xin, attrs, "pivotTables", &pivotTables)) ;
		else if (attr_bool (xin, attrs, "selectUnlockedCells", &selectUnlockedCells)) ;

	g_object_set (state->sheet,
		"protected",				 sheet,
		"protected-allow-edit-objects",		 objects,
		"protected-allow-edit-scenarios",	 scenarios,
		"protected-allow-cell-formatting",	 formatCells,
		"protected-allow-column-formatting",	 formatColumns,
		"protected-allow-row-formatting",	 formatRows,
		"protected-allow-insert-columns",	 insertColumns,
		"protected-allow-insert-rows",		 insertRows,
		"protected-allow-insert-hyperlinks",	 insertHyperlinks,
		"protected-allow-delete-columns",	 deleteColumns,
		"protected-allow-delete-rows",		 deleteRows,
		"protected-allow-select-locked-cells",	 selectLockedCells,
		"protected-allow-sort-ranges",		 sort,
		"protected-allow-edit-auto-filters",	 autoFilter,
		"protected-allow-edit-pivottable",	 pivotTables,
		"protected-allow-select-unlocked-cells", selectUnlockedCells,
		NULL);
}

static void
xlsx_sheet_drawing (GsfXMLIn *xin, xmlChar const **attrs)
{
	xmlChar const *part_id = NULL;
	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (gsf_xml_in_namecmp (xin, attrs[0], XL_NS_DOC_REL, "id"))
			part_id = attrs[1];
	if (NULL != part_id)
		xlsx_parse_rel_by_id (xin, part_id, xlsx_drawing_dtd, xlsx_ns);
}

static void
xlsx_cond_fmt_begin (GsfXMLIn *xin, xmlChar const **attrs)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	char const *refs = NULL;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (0 == strcmp (attrs[0], "sqref"))
			refs = attrs[1];

	state->cond_regions = xlsx_parse_sqref (xin, refs);

	/* create in first call xlsx_cond_rule to avoid creating condition with
	 * no rules */
	state->conditions = NULL;
}

static void
xlsx_cond_fmt_end (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	GnmStyle *style = NULL;
	GSList   *ptr;

	if (NULL != state->conditions) {
		style = gnm_style_new ();
		gnm_style_set_conditions (style, state->conditions);
		for (ptr = state->cond_regions ; ptr != NULL ; ptr = ptr->next) {
			gnm_style_ref (style);
			sheet_style_apply_range	(state->sheet, ptr->data, style);
			g_free (ptr->data);
		}
		gnm_style_unref (style);
	} else for (ptr = state->cond_regions ; ptr != NULL ; ptr = ptr->next)
		g_free (ptr->data);
	g_slist_free (state->cond_regions);
	state->cond_regions = NULL;
}

typedef enum {
	XLSX_CF_TYPE_UNDEFINED,

	XLSX_CF_TYPE_EXPRESSION,
	XLSX_CF_TYPE_CELL_IS,
	XLSX_CF_TYPE_COLOR_SCALE,
	XLSX_CF_TYPE_DATA_BAR,
	XLSX_CF_TYPE_ICON_SET,
	XLSX_CF_TYPE_TOP10,
	XLSX_CF_TYPE_UNIQUE_VALUES,
	XLSX_CF_TYPE_DUPLICATE_VALUES,
	XLSX_CF_TYPE_CONTAINS_STR		= GNM_STYLE_COND_CONTAINS_STR,
	XLSX_CF_TYPE_NOT_CONTAINS_STR		= GNM_STYLE_COND_NOT_CONTAINS_STR,
	XLSX_CF_TYPE_BEGINS_WITH		= GNM_STYLE_COND_BEGINS_WITH_STR,
	XLSX_CF_TYPE_ENDS_WITH			= GNM_STYLE_COND_ENDS_WITH_STR,
	XLSX_CF_TYPE_CONTAINS_BLANKS		= GNM_STYLE_COND_CONTAINS_BLANKS,
	XLSX_CF_TYPE_NOT_CONTAINS_BLANKS	= GNM_STYLE_COND_NOT_CONTAINS_BLANKS,
	XLSX_CF_TYPE_CONTAINS_ERRORS		= GNM_STYLE_COND_CONTAINS_ERR,
	XLSX_CF_TYPE_NOT_CONTAINS_ERRORS	= GNM_STYLE_COND_NOT_CONTAINS_ERR,
	XLSX_CF_TYPE_COMPARE_COLUMNS,
	XLSX_CF_TYPE_TIME_PERIOD,
	XLSX_CF_TYPE_ABOVE_AVERAGE
} XlsxCFTypes;
static void
xlsx_cond_fmt_rule_begin (GsfXMLIn *xin, xmlChar const **attrs)
{
	static EnumVal const ops[] = {
		{ "lessThan",		GNM_STYLE_COND_LT },
		{ "lessThanOrEqual",	GNM_STYLE_COND_LTE },
		{ "equal",		GNM_STYLE_COND_EQUAL },
		{ "notEqual",		GNM_STYLE_COND_NOT_EQUAL },
		{ "greaterThanOrEqual",	GNM_STYLE_COND_GTE },
		{ "greaterThan",	GNM_STYLE_COND_GT },
		{ "between",		GNM_STYLE_COND_BETWEEN },
		{ "notBetween",		GNM_STYLE_COND_NOT_BETWEEN },
		{ "containsText",	GNM_STYLE_COND_CONTAINS_STR },
		{ "notContainsText",	GNM_STYLE_COND_NOT_CONTAINS_STR },
		{ "beginsWith",		GNM_STYLE_COND_BEGINS_WITH_STR },
		{ "endsWith",		GNM_STYLE_COND_ENDS_WITH_STR },
		{ "notContain",		GNM_STYLE_COND_NOT_CONTAINS_STR },
		{ NULL, 0 }
	};

	static EnumVal const types[] = {
		{ "expression",		XLSX_CF_TYPE_EXPRESSION },
		{ "cellIs",		XLSX_CF_TYPE_CELL_IS },
		{ "colorScale",		XLSX_CF_TYPE_COLOR_SCALE },
		{ "dataBar",		XLSX_CF_TYPE_DATA_BAR },
		{ "iconSet",		XLSX_CF_TYPE_ICON_SET },
		{ "top10",		XLSX_CF_TYPE_TOP10 },
		{ "uniqueValues",	XLSX_CF_TYPE_UNIQUE_VALUES },
		{ "duplicateValues",	XLSX_CF_TYPE_DUPLICATE_VALUES },
		{ "containsText",	XLSX_CF_TYPE_CONTAINS_STR },
		{ "doesNotContainText",	XLSX_CF_TYPE_NOT_CONTAINS_STR },
		{ "beginsWith",		XLSX_CF_TYPE_BEGINS_WITH },
		{ "endsWith",		XLSX_CF_TYPE_ENDS_WITH },
		{ "containsBlanks",	XLSX_CF_TYPE_CONTAINS_BLANKS },
		{ "containsNoBlanks",	XLSX_CF_TYPE_NOT_CONTAINS_BLANKS },
		{ "containsErrors",	XLSX_CF_TYPE_CONTAINS_ERRORS },
		{ "containsNoErrors",	XLSX_CF_TYPE_NOT_CONTAINS_ERRORS },
		{ "compareColumns",	XLSX_CF_TYPE_COMPARE_COLUMNS },
		{ "timePeriod",		XLSX_CF_TYPE_TIME_PERIOD },
		{ "aboveAverage",	XLSX_CF_TYPE_ABOVE_AVERAGE },
		{ NULL, 0 }
	};

	XLSXReadState  *state = (XLSXReadState *)xin->user_state;
	gboolean	formatRow = FALSE;
	gboolean	stopIfTrue = FALSE;
	gboolean	above = TRUE;
	gboolean	percent = FALSE;
	gboolean	bottom = FALSE;
	int		tmp, dxf = -1;
	/* use custom invalid flag, it is not in MS enum */
	GnmStyleCondOp	op = GNM_STYLE_COND_CUSTOM;
	XlsxCFTypes	type = XLSX_CF_TYPE_UNDEFINED;
	char const	*type_str = _("Undefined");

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (attr_bool (xin, attrs, "formatRow", &formatRow)) ;
		else if (attr_bool (xin, attrs, "stopIfTrue", &stopIfTrue)) ;
		else if (attr_bool (xin, attrs, "above", &above)) ;
		else if (attr_bool (xin, attrs, "percent", &percent)) ;
		else if (attr_bool (xin, attrs, "bottom", &bottom)) ;
		else if (attr_int  (xin, attrs, "dxfId", &dxf)) ;
		else if (attr_enum (xin, attrs, "operator", ops, &tmp))
			op = tmp;
		else if (attr_enum (xin, attrs, "type", types, &tmp)) {
			type = tmp;
			type_str = attrs[1];
		}
#if 0
	"numFmtId"	="ST_NumFmtId" use="optional">
	"priority"	="xs:int" use="required">
	"text"		="xs:string" use="optional">
	"timePeriod"	="ST_TimePeriod" use="optional">
	"col1"		="xs:unsignedInt" use="optional">
	"col2"		="xs:unsignedInt" use="optional">
#endif

	if (dxf >= 0 && NULL != (state->cond.overlay = xlsx_get_dxf (xin, dxf)))
		gnm_style_ref (state->cond.overlay);

	switch (type) {
	case XLSX_CF_TYPE_CELL_IS :
		state->cond.op = op;
		break;
	case XLSX_CF_TYPE_CONTAINS_STR :
	case XLSX_CF_TYPE_NOT_CONTAINS_STR :
	case XLSX_CF_TYPE_BEGINS_WITH :
	case XLSX_CF_TYPE_ENDS_WITH :
	case XLSX_CF_TYPE_CONTAINS_BLANKS :
	case XLSX_CF_TYPE_NOT_CONTAINS_BLANKS :
	case XLSX_CF_TYPE_CONTAINS_ERRORS :
	case XLSX_CF_TYPE_NOT_CONTAINS_ERRORS :
		state->cond.op = type;
		break;

	default :
		xlsx_warning (xin, _("Ignoring unhandled conditional format of type '%s'"), type_str);
	}
	state->count = 0;
}

static void
xlsx_cond_fmt_rule_end (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	if (gnm_style_cond_is_valid (&state->cond)) {
		if (NULL == state->conditions)
			state->conditions = gnm_style_conditions_new ();
		gnm_style_conditions_insert (state->conditions, &state->cond, -1);
	} else {
		if (NULL != state->cond.texpr[0])
			gnm_expr_top_unref (state->cond.texpr[0]);
		if (NULL != state->cond.texpr[1])
			gnm_expr_top_unref (state->cond.texpr[1]);
		if (NULL != state->cond.overlay)
			gnm_style_unref (state->cond.overlay);
	}
	state->cond.texpr[0] = state->cond.texpr[1] = NULL;
	state->cond.overlay = NULL;
}

static void
xlsx_cond_fmt_formula_end (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	GnmParsePos pp;
	if (state->count > 1)
		return;

	state->cond.texpr[state->count++] = xlsx_parse_expr (xin, xin->content->str,
		parse_pos_init_sheet (&pp, state->sheet));
}

static void
xlsx_CT_SheetView_begin (GsfXMLIn *xin, xmlChar const **attrs)
{
	static EnumVal const view_types[] = {
		{ "normal",		GNM_SHEET_VIEW_NORMAL_MODE },
		{ "pageBreakPreview",	GNM_SHEET_VIEW_PAGE_BREAK_MODE },
		{ "pageLayout",		GNM_SHEET_VIEW_LAYOUT_MODE },
		{ NULL, 0 }
	};

	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	int showGridLines	= TRUE;
	int showFormulas	= FALSE;
	int showRowColHeaders	= TRUE;
	int showZeros		= TRUE;
	int frozen		= FALSE;
	int frozenSplit		= TRUE;
	int rightToLeft		= FALSE;
	int tabSelected		= FALSE;
	int active		= FALSE;
	int showRuler		= TRUE;
	int showOutlineSymbols	= TRUE;
	int defaultGridColor	= TRUE;
	int showWhiteSpace	= TRUE;
	int scale		= 100;
	int grid_color_index	= -1;
	int tmp;
	GnmSheetViewMode	view_mode = GNM_SHEET_VIEW_NORMAL_MODE;
	GnmCellPos topLeft = { -1, -1 };

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (attr_pos (xin, attrs, "topLeftCell", &topLeft)) ;
		else if (attr_bool (xin, attrs, "showGridLines", &showGridLines)) ;
		else if (attr_bool (xin, attrs, "showFormulas", &showFormulas)) ;
		else if (attr_bool (xin, attrs, "showRowColHeaders", &showRowColHeaders)) ;
		else if (attr_bool (xin, attrs, "showZeros", &showZeros)) ;
		else if (attr_bool (xin, attrs, "frozen", &frozen)) ;
		else if (attr_bool (xin, attrs, "frozenSplit", &frozenSplit)) ;
		else if (attr_bool (xin, attrs, "rightToLeft", &rightToLeft)) ;
		else if (attr_bool (xin, attrs, "tabSelected", &tabSelected)) ;
		else if (attr_bool (xin, attrs, "active", &active)) ;
		else if (attr_bool (xin, attrs, "showRuler", &showRuler)) ;
		else if (attr_bool (xin, attrs, "showOutlineSymbols", &showOutlineSymbols)) ;
		else if (attr_bool (xin, attrs, "defaultGridColor", &defaultGridColor)) ;
		else if (attr_bool (xin, attrs, "showWhiteSpace", &showWhiteSpace)) ;
		else if (attr_int (xin, attrs, "zoomScale", &scale)) ;
		else if (attr_int (xin, attrs, "colorId", &grid_color_index)) ;
		else if (attr_enum (xin, attrs, "view", view_types, &tmp))
			view_mode = tmp;
#if 0
"zoomScaleNormal"		type="xs:unsignedInt" use="optional" default="0"
"zoomScaleSheetLayoutView"	type="xs:unsignedInt" use="optional" default="0"
"zoomScalePageLayoutView"	type="xs:unsignedInt" use="optional" default="0"
"workbookViewId"		type="xs:unsignedInt" use="required"
#endif

	/* get this from the workbookViewId */
	g_return_if_fail (state->sv == NULL);
	state->sv = sheet_get_view (state->sheet, state->wb_view);
	state->pane_pos = XLSX_PANE_TOP_LEFT;

	/* until we import multiple views unfreeze just in case a previous view
	 * had frozen */
	sv_freeze_panes (state->sv, NULL, NULL);

	if (topLeft.col >= 0)
		sv_set_initial_top_left (state->sv, topLeft.col, topLeft.row);
	g_object_set (state->sheet,
		"text-is-rtl",		rightToLeft,
		"display-formulas",	showFormulas,
		"display-zeros",	showZeros,
		"display-grid",		showGridLines,
		"display-column-header", showRowColHeaders,
		"display-row-header",	showRowColHeaders,
		"display-outlines",	showOutlineSymbols,
		"zoom-factor",		((double)scale) / 100.,
		NULL);
#if 0
		gboolean active			= FALSE;
		gboolean showRuler		= TRUE;
		gboolean showWhiteSpace		= TRUE;
#endif

#if 0
	g_object_set (state->sv,
		"displayMode",	view_mode,
		NULL);
#endif

	if (!defaultGridColor && grid_color_index >= 0)
		sheet_style_set_auto_pattern_color (state->sheet,
			indexed_color (grid_color_index));
	if (tabSelected)
		wb_view_sheet_focus (state->wb_view, state->sheet);
}
static void
xlsx_CT_SheetView_end (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	g_return_if_fail (state->sv != NULL);
	state->sv = NULL;
}

static EnumVal const pane_types[] = {
	{ "topLeft",     XLSX_PANE_TOP_LEFT },
	{ "topRight",    XLSX_PANE_TOP_RIGHT },
	{ "bottomLeft",  XLSX_PANE_BOTTOM_LEFT },
	{ "bottomRight", XLSX_PANE_BOTTOM_RIGHT },
	{ NULL, 0 }
};
static void
xlsx_CT_Selection (GsfXMLIn *xin, xmlChar const **attrs)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	GnmCellPos edit_pos = { -1, -1 };
	int i, sel_with_edit_pos = 0;
	char const *refs = NULL;
	XLSXPanePos pane_pos = XLSX_PANE_TOP_LEFT;
	GnmRange r;
	GSList *ptr, *accum = NULL;

	g_return_if_fail (state->sv != NULL);

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (0 == strcmp (attrs[0], "sqref"))
			refs = attrs[1];
		else if (attr_enum (xin, attrs, "activePane", pane_types, &i))
			pane_pos = i;
		else if (attr_pos (xin, attrs, "activeCell", &edit_pos)) ;
		else if (attr_int (xin, attrs, "activeCellId", &sel_with_edit_pos))
			;

	if (pane_pos != state->pane_pos)
		return;

	for (i = 0 ; NULL != refs && *refs ; i++) {
		if (NULL == (refs = cellpos_parse (refs, &r.start, FALSE)))
			return;

		if (*refs == '\0' || *refs == ' ')
			r.end = r.start;
		else if (*refs != ':' ||
			 NULL == (refs = cellpos_parse (refs + 1, &r.end, FALSE)))
			return;

		if (i == 0)
			sv_selection_reset (state->sv);

		/* gnumeric assumes the edit_pos is in the last selected range.
		 * We need to re-order the selection list. */
		if (i <= sel_with_edit_pos && edit_pos.col >= 0)
			accum = g_slist_prepend (accum, range_dup (&r));
		else
			sv_selection_add_range (state->sv, &r);
		while (*refs == ' ')
			refs++;
	}

	if (NULL != accum) {
		accum = g_slist_reverse (accum);
		for (ptr = accum ; ptr != NULL ; ptr = ptr->next) {
			sv_selection_add_range (state->sv, ptr->data);
			g_free (ptr->data);
		}
		sv_set_edit_pos (state->sv, &edit_pos);
		g_slist_free (accum);
	}
}
static void
xlsx_CT_Pane (GsfXMLIn *xin, xmlChar const **attrs)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	GnmCellPos topLeft;
	int tmp;
	double xSplit = -1., ySplit = -1.;
	gboolean frozen = FALSE;

	g_return_if_fail (state->sv != NULL);

	/* <pane xSplit="2" ySplit="3" topLeftCell="J15" activePane="bottomRight" state="frozen"/> */
	state->pane_pos = XLSX_PANE_TOP_LEFT;
	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (0 == strcmp (attrs[0], "state"))
			frozen = (0 == strcmp (attrs[1], "frozen"));
		else if (attr_pos (xin, attrs, "topLeftCell", &topLeft)) ;
		else if (attr_float (xin, attrs, "xSplit", &xSplit)) ;
		else if (attr_float (xin, attrs, "ySplit", &ySplit)) ;
		else if (attr_enum (xin, attrs, "pane", pane_types, &tmp))
			state->pane_pos = tmp;

	if (frozen) {
		GnmCellPos frozen, unfrozen;
		frozen = unfrozen = state->sv->initial_top_left;
		if (xSplit > 0)
			unfrozen.col += xSplit;
		else
			topLeft.col = state->sv->initial_top_left.col;
		if (ySplit > 0)
			unfrozen.row += ySplit;
		else
			topLeft.row = state->sv->initial_top_left.row;
		sv_freeze_panes (state->sv, &frozen, &unfrozen);
		sv_set_initial_top_left (state->sv, topLeft.col, topLeft.row);
	}
}

static void
xlsx_ole_object (GsfXMLIn *xin, xmlChar const **attrs)
{
#if 0
	XLSXReadState *state = (XLSXReadState *)xin->user_state;

	/* <oleObject progId="Wordpad.Document.1" shapeId="1032" r:id="rId5"/> */
	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		;
#endif
}

static void
xlsx_CT_HyperLinks (GsfXMLIn *xin, xmlChar const **attrs)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	gboolean has_ref = FALSE;
	GnmStyle *style;
	GnmRange r;
	GType link_type = 0;
	GnmHLink *link = NULL;
	xmlChar const *target = NULL;
	xmlChar const *tooltip = NULL;
	xmlChar const *extern_id = NULL;

	/* <hyperlink ref="A42" r:id="rId1"/> */
	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (attr_range (xin, attrs, "ref", &r))
			has_ref = TRUE;
		else if (0 == strcmp (attrs[0], "location"))
			target = attrs[1];
		else if (0 == strcmp (attrs[0], "tooltip"))
			tooltip = attrs[1];
		else if (gsf_xml_in_namecmp (xin, attrs[0], XL_NS_DOC_REL, "id"))
			extern_id = attrs[1];
#if 0 /* ignore "display" on import, it always seems to be the cell content */
		else if (0 == strcmp (attrs[0], "display"))
#endif
	if (!has_ref)
		return;

	if (NULL != target)
		link_type = gnm_hlink_cur_wb_get_type ();
	else if (NULL != extern_id) {
		GsfOpenPkgRel const *rel = gsf_open_pkg_lookup_rel_by_id (
			gsf_xml_in_get_input (xin), extern_id);
		if (NULL != rel &&
		    gsf_open_pkg_rel_is_extern (rel) &&
		    0 == strcmp (gsf_open_pkg_rel_get_type (rel),
				 "http://schemas.openxmlformats.org/officeDocument/2006/relationships/hyperlink")) {
			target = gsf_open_pkg_rel_get_target (rel);
			if (NULL != target) {
				if (0 == strncmp (target, "mailto:", 7))
					link_type = gnm_hlink_email_get_type ();
				else
					link_type = gnm_hlink_url_get_type ();
			}
		}
	}

	if (0 == link_type) {
		xlsx_warning (xin, _("Unknown type of hyperlink"));
		return;
	}

	link = g_object_new (link_type, NULL);
	if (NULL != target)
		gnm_hlink_set_target (link, target);
	if (NULL != tooltip)
		gnm_hlink_set_tip  (link, tooltip);
	style = gnm_style_new ();
	gnm_style_set_hlink (style, link);
	sheet_style_apply_range	(state->sheet, &r, style);
}

static GsfXMLInNode const xlsx_sheet_dtd[] =
{
GSF_XML_IN_NODE_FULL (START, START, -1, NULL, GSF_XML_NO_CONTENT, FALSE, TRUE, NULL, NULL, 0),
GSF_XML_IN_NODE_FULL (START, SHEET, XL_NS_SS, "worksheet", GSF_XML_NO_CONTENT, FALSE, TRUE, NULL, NULL, 0),
  GSF_XML_IN_NODE (SHEET, PROPS, XL_NS_SS, "sheetPr", GSF_XML_NO_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE (PROPS, OUTLINE_PROPS, XL_NS_SS, "outlinePr", GSF_XML_NO_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE (PROPS, TAB_COLOR, XL_NS_SS, "tabColor", GSF_XML_NO_CONTENT, &xlsx_sheet_tabcolor, NULL),
    GSF_XML_IN_NODE (PROPS, PAGE_SETUP, XL_NS_SS, "pageSetUpPr", GSF_XML_NO_CONTENT, &xlsx_sheet_page_setup, NULL),
  GSF_XML_IN_NODE (SHEET, DIMENSION, XL_NS_SS, "dimension", GSF_XML_NO_CONTENT, NULL, NULL),
  GSF_XML_IN_NODE (SHEET, VIEWS, XL_NS_SS, "sheetViews", GSF_XML_NO_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE (VIEWS, VIEW, XL_NS_SS, "sheetView",  GSF_XML_NO_CONTENT, &xlsx_CT_SheetView_begin, &xlsx_CT_SheetView_end),
      GSF_XML_IN_NODE (VIEW, SELECTION, XL_NS_SS, "selection",  GSF_XML_NO_CONTENT, &xlsx_CT_Selection, NULL),
      GSF_XML_IN_NODE (VIEW, PANE, XL_NS_SS, "pane",  GSF_XML_NO_CONTENT, &xlsx_CT_Pane, NULL),

  GSF_XML_IN_NODE (SHEET, DEFAULT_FMT, XL_NS_SS, "sheetFormatPr", GSF_XML_NO_CONTENT, &xlsx_CT_SheetFormatPr, NULL),

  GSF_XML_IN_NODE (SHEET, COLS,	XL_NS_SS, "cols", GSF_XML_NO_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE (COLS, COL,	XL_NS_SS, "col", GSF_XML_NO_CONTENT, &xlsx_CT_Col, NULL),

  GSF_XML_IN_NODE (SHEET, CONTENT, XL_NS_SS, "sheetData", GSF_XML_NO_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE (CONTENT, ROW, XL_NS_SS, "row", GSF_XML_NO_CONTENT, &xlsx_CT_Row, NULL),
      GSF_XML_IN_NODE (ROW, CELL, XL_NS_SS, "c", GSF_XML_NO_CONTENT, &xlsx_cell_begin, &xlsx_cell_end),
	GSF_XML_IN_NODE (CELL, VALUE, XL_NS_SS, "v", GSF_XML_CONTENT, NULL, &xlsx_cell_val_end),
	GSF_XML_IN_NODE (CELL, FMLA, XL_NS_SS,  "f", GSF_XML_CONTENT, &xlsx_cell_expr_begin, &xlsx_cell_expr_end),

  GSF_XML_IN_NODE (SHEET, CT_SortState, XL_NS_SS, "sortState", GSF_XML_NO_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE (CT_SortState, CT_SortCondition, XL_NS_SS, "sortCondition", GSF_XML_NO_CONTENT, NULL, NULL),

  GSF_XML_IN_NODE (SHEET, CT_AutoFilter, XL_NS_SS, "autoFilter", GSF_XML_NO_CONTENT,
		   &xlsx_CT_AutoFilter_begin, &xlsx_CT_AutoFilter_end),
    GSF_XML_IN_NODE (CT_AutoFilter, CT_SortState, XL_NS_SS, "sortState", GSF_XML_NO_CONTENT, NULL, NULL), /* 2nd Def */
    GSF_XML_IN_NODE (CT_AutoFilter, CT_FilterColumn, XL_NS_SS,    "filterColumn", GSF_XML_NO_CONTENT,
		     &xlsx_CT_FilterColumn_begin, NULL),
      GSF_XML_IN_NODE (CT_FilterColumn, CT_Filters, XL_NS_SS, "filters", GSF_XML_NO_CONTENT,
		       &xlsx_CT_Filters_begin, &xlsx_CT_Filters_end),
        GSF_XML_IN_NODE (CT_Filters, CT_Filter, XL_NS_SS, "filter", GSF_XML_NO_CONTENT, &xlsx_CT_Filter, NULL),
      GSF_XML_IN_NODE (CT_FilterColumn, CT_CustomFilters, XL_NS_SS, "customFilters", GSF_XML_NO_CONTENT,
		       &xlsx_CT_CustomFilters_begin, &xlsx_CT_CustomFilters_end),
        GSF_XML_IN_NODE (CT_CustomFilters, CT_CustomFilter, XL_NS_SS, "customFilter", GSF_XML_NO_CONTENT, &xlsx_CT_CustomFilter, NULL),
      GSF_XML_IN_NODE (CT_FilterColumn, CT_Top10, XL_NS_SS, "top10", GSF_XML_NO_CONTENT, &xlsx_CT_Top10, NULL),
      GSF_XML_IN_NODE (CT_FilterColumn, CT_DynamicFilter, XL_NS_SS, "dynamicFilter", GSF_XML_NO_CONTENT, &xlsx_CT_DynamicFilter, NULL),
      GSF_XML_IN_NODE (CT_FilterColumn, CT_ColorFilter, XL_NS_SS, "colorFilter", GSF_XML_NO_CONTENT, NULL, NULL),
      GSF_XML_IN_NODE (CT_FilterColumn, CT_IconFilter, XL_NS_SS, "iconFilter", GSF_XML_NO_CONTENT, NULL, NULL),

  GSF_XML_IN_NODE (SHEET, CT_DataValidations, XL_NS_SS, "dataValidations", GSF_XML_NO_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE (CT_DataValidations, CT_DataValidation, XL_NS_SS, "dataValidation", GSF_XML_NO_CONTENT,
		     &xlsx_CT_DataValidation_begin, &xlsx_CT_DataValidation_end),
      GSF_XML_IN_NODE_FULL (CT_DataValidation, VAL_FORMULA1, XL_NS_SS, "formula1", GSF_XML_CONTENT, FALSE, FALSE, NULL, &xlsx_validation_expr, 0),
      GSF_XML_IN_NODE_FULL (CT_DataValidation, VAL_FORMULA2, XL_NS_SS, "formula2", GSF_XML_CONTENT, FALSE, FALSE, NULL, &xlsx_validation_expr, 1),

  GSF_XML_IN_NODE (SHEET, MERGES, XL_NS_SS, "mergeCells", GSF_XML_NO_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE (MERGES, MERGE, XL_NS_SS, "mergeCell", GSF_XML_NO_CONTENT, &xlsx_CT_MergeCell, NULL),

  GSF_XML_IN_NODE (SHEET, DRAWING, XL_NS_SS, "drawing", GSF_XML_NO_CONTENT, &xlsx_sheet_drawing, NULL),

  GSF_XML_IN_NODE (SHEET, PROTECTION, XL_NS_SS, "sheetProtection", GSF_XML_NO_CONTENT, &xlsx_CT_SheetProtection, NULL),
  GSF_XML_IN_NODE (SHEET, PHONETIC, XL_NS_SS, "phoneticPr", GSF_XML_NO_CONTENT, NULL, NULL),
  GSF_XML_IN_NODE (SHEET, COND_FMTS, XL_NS_SS, "conditionalFormatting", GSF_XML_NO_CONTENT,
		   &xlsx_cond_fmt_begin, &xlsx_cond_fmt_end),
    GSF_XML_IN_NODE (COND_FMTS, COND_RULE, XL_NS_SS, "cfRule", GSF_XML_NO_CONTENT,
		   &xlsx_cond_fmt_rule_begin, &xlsx_cond_fmt_rule_end),
      GSF_XML_IN_NODE (COND_RULE, COND_FMLA, XL_NS_SS, "formula", GSF_XML_CONTENT, NULL, &xlsx_cond_fmt_formula_end),
      GSF_XML_IN_NODE (COND_RULE, COND_COLOR_SCALE, XL_NS_SS, "colorScale", GSF_XML_NO_CONTENT, NULL, NULL),
        GSF_XML_IN_NODE (COND_COLOR_SCALE, CFVO, XL_NS_SS, "cfvo", GSF_XML_NO_CONTENT, NULL, NULL),
        GSF_XML_IN_NODE (COND_COLOR_SCALE, COND_COLOR, XL_NS_SS, "color", GSF_XML_NO_CONTENT, NULL, NULL),
      GSF_XML_IN_NODE (COND_RULE, COND_DATA_BAR, XL_NS_SS, "dataBar", GSF_XML_NO_CONTENT, NULL, NULL),
      GSF_XML_IN_NODE (COND_RULE, COND_ICON_SET, XL_NS_SS, "iconSet", GSF_XML_NO_CONTENT, NULL, NULL),
        GSF_XML_IN_NODE (COND_ICON_SET, CFVO, XL_NS_SS, "cfvo", GSF_XML_NO_CONTENT, NULL, NULL),	/* 2nd Def */

  GSF_XML_IN_NODE (SHEET, HYPERLINKS, XL_NS_SS, "hyperlinks", GSF_XML_NO_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE (HYPERLINKS, HYPERLINK, XL_NS_SS, "hyperlink", GSF_XML_NO_CONTENT, &xlsx_CT_HyperLinks, NULL),

  GSF_XML_IN_NODE (SHEET, PRINT_OPTS, XL_NS_SS, "printOptions", GSF_XML_NO_CONTENT, NULL, NULL),
  GSF_XML_IN_NODE (SHEET, PRINT_MARGINS, XL_NS_SS, "pageMargins", GSF_XML_NO_CONTENT, &xlsx_CT_PageMargins, NULL),
  GSF_XML_IN_NODE (SHEET, PRINT_SETUP, XL_NS_SS, "pageSetup", GSF_XML_NO_CONTENT, NULL, NULL),
  GSF_XML_IN_NODE (SHEET, PRINT_HEADER_FOOTER, XL_NS_SS, "headerFooter", GSF_XML_NO_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE (PRINT_HEADER_FOOTER, ODD_HEADER, XL_NS_SS, "oddHeader", GSF_XML_NO_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE (PRINT_HEADER_FOOTER, ODD_FOOTER, XL_NS_SS, "oddFooter", GSF_XML_NO_CONTENT, NULL, NULL),

  GSF_XML_IN_NODE_FULL (SHEET, ROW_BREAKS, XL_NS_SS, "rowBreaks", GSF_XML_NO_CONTENT,
			FALSE, FALSE, &xlsx_CT_PageBreaks_begin, &xlsx_CT_PageBreaks_end, 1),
    GSF_XML_IN_NODE (ROW_BREAKS, CT_PageBreak, XL_NS_SS, "brk", GSF_XML_NO_CONTENT, &xlsx_CT_PageBreak, NULL),
  GSF_XML_IN_NODE_FULL (SHEET, COL_BREAKS, XL_NS_SS, "colBreaks", GSF_XML_NO_CONTENT,
			FALSE, FALSE, &xlsx_CT_PageBreaks_begin, &xlsx_CT_PageBreaks_end, 0),
    GSF_XML_IN_NODE (COL_BREAKS, CT_PageBreak, XL_NS_SS, "brk", GSF_XML_NO_CONTENT, NULL, NULL), /* 2nd Def */

  GSF_XML_IN_NODE (SHEET, LEGACY_DRAW, XL_NS_SS, "legacyDrawing", GSF_XML_NO_CONTENT, NULL, NULL),
  GSF_XML_IN_NODE (SHEET, OLE_OBJECTS, XL_NS_SS, "oleObjects", GSF_XML_NO_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE (OLE_OBJECTS, OLE_OBJECT, XL_NS_SS, "oleObject", GSF_XML_NO_CONTENT, &xlsx_ole_object, NULL),

GSF_XML_IN_NODE_END
};

/****************************************************************************/

static void
xlsx_CT_CalcPr (GsfXMLIn *xin, xmlChar const **attrs)
{
	static EnumVal const calcModes[] = {
		{ "manual",	 FALSE },
		{ "auto",	 TRUE },
		{ "autoNoTable", TRUE },
		{ NULL, 0 }
	};
	static EnumVal const refModes[] = {
		{ "A1",		TRUE },
		{ "R1C1",	FALSE },
		{ NULL, 0 }
	};
	int tmp;
	gnm_float delta;
	XLSXReadState *state = (XLSXReadState *)xin->user_state;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (attr_enum (xin, attrs, "calcMode", calcModes, &tmp))
			workbook_set_recalcmode (state->wb, tmp);
		else if (attr_bool (xin, attrs, "fullCalcOnLoad", &tmp))
			;
		else if (attr_enum (xin, attrs, "refMode", refModes, &tmp))
			;
		else if (attr_bool (xin, attrs, "iterate", &tmp))
			workbook_iteration_enabled (state->wb, tmp);
		else if (attr_int (xin, attrs, "iterateCount", &tmp))
			workbook_iteration_max_number (state->wb, tmp);
		else if (attr_float (xin, attrs, "iterateDelta", &delta))
			workbook_iteration_tolerance (state->wb, delta);
		else if (attr_bool (xin, attrs, "fullPrecision", &tmp))
			;
		else if (attr_bool (xin, attrs, "calcCompleted", &tmp))
			;
		else if (attr_bool (xin, attrs, "calcOnSave", &tmp))
			;
		else if (attr_bool (xin, attrs, "conncurrentCalc", &tmp))
			;
		else if (attr_bool (xin, attrs, "forceFullCalc", &tmp))
			;
		else if (attr_int (xin, attrs, "concurrentManualCalc", &tmp))
			;
}

static void
xlsx_sheet_begin (GsfXMLIn *xin, xmlChar const **attrs)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	char const *name = NULL;
	char const *part_id = NULL;
	Sheet *sheet;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (0 == strcmp (attrs[0], "name"))
			name = attrs[1];
		else if (gsf_xml_in_namecmp (xin, attrs[0], XL_NS_DOC_REL, "id"))
			part_id = attrs[1];

	if (NULL == name) {
		xlsx_warning (xin, _("Ignoring a sheet without a name"));
		return;
	}

	sheet =  workbook_sheet_by_name (state->wb, name);
	if (NULL == sheet) {
		sheet = sheet_new (state->wb, name);
		workbook_sheet_attach (state->wb, sheet);
	}

	g_object_set_data_full (G_OBJECT (sheet), "_XLSX_RelID", g_strdup (part_id),
			(GDestroyNotify) g_free);
}

static void
xlsx_wb_end (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	int i, n = workbook_sheet_count (state->wb);
	char const *part_id;
	GnmStyle *style;

	/* Load sheets after setting up the workbooks to give us time to create
	 * all of them and parse names */
	for (i = 0 ; i < n ; i++, state->sheet = NULL) {
		if (NULL == (state->sheet = workbook_sheet_by_index (state->wb, i)))
			continue;
		if (NULL == (part_id = g_object_get_data (G_OBJECT (state->sheet), "_XLSX_RelID"))) {
			xlsx_warning (xin, _("Missing part-id for sheet '%s'"),
				      state->sheet->name_unquoted);
			continue;
		}

		/* Apply the 'Normal' style (aka builtin 0) to the entire sheet */
		if (NULL != (style = g_hash_table_lookup(state->cell_styles, "0"))) {
			GnmRange r;
			gnm_style_ref (style);
			sheet_style_set_range (state->sheet,
				range_init_full_sheet (&r), style);
		}

		xlsx_parse_rel_by_id (xin, part_id, xlsx_sheet_dtd, xlsx_ns);

		/* Flag a respan here in case nothing else does */
		sheet_flag_recompute_spans (state->sheet);
	}
}

static GsfXMLInNode const xlsx_workbook_dtd[] = {
GSF_XML_IN_NODE_FULL (START, START, -1, NULL, GSF_XML_NO_CONTENT, FALSE, TRUE, NULL, NULL, 0),
GSF_XML_IN_NODE_FULL (START, WORKBOOK, XL_NS_SS, "workbook", GSF_XML_NO_CONTENT, FALSE, TRUE, NULL, &xlsx_wb_end, 0),
  GSF_XML_IN_NODE (WORKBOOK, VERSION, XL_NS_SS,	   "fileVersion", GSF_XML_NO_CONTENT, NULL, NULL),
  GSF_XML_IN_NODE (WORKBOOK, PROPERTIES, XL_NS_SS, "workbookPr", GSF_XML_NO_CONTENT, NULL, NULL),
  GSF_XML_IN_NODE (WORKBOOK, CALC_PROPS, XL_NS_SS, "calcPr", GSF_XML_NO_CONTENT, &xlsx_CT_CalcPr, NULL),
  GSF_XML_IN_NODE (WORKBOOK, VIEWS,	 XL_NS_SS, "bookViews",	GSF_XML_NO_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE (VIEWS,  VIEW,	 XL_NS_SS, "workbookView",  GSF_XML_NO_CONTENT, NULL, NULL),
  GSF_XML_IN_NODE (WORKBOOK, SHEETS,	 XL_NS_SS, "sheets", GSF_XML_NO_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE (SHEETS, SHEET,	 XL_NS_SS, "sheet", GSF_XML_NO_CONTENT, &xlsx_sheet_begin, NULL),
  GSF_XML_IN_NODE (WORKBOOK, WEB_PUB,	 XL_NS_SS, "webPublishing", GSF_XML_NO_CONTENT, NULL, NULL),
  GSF_XML_IN_NODE (WORKBOOK, EXTERNS,	 XL_NS_SS, "externalReferences", GSF_XML_NO_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE (EXTERNS, EXTERN,	 XL_NS_SS, "externalReference", GSF_XML_NO_CONTENT, NULL, NULL),
  GSF_XML_IN_NODE (WORKBOOK, NAMES,	 XL_NS_SS, "definedNames", GSF_XML_NO_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE (NAMES, NAME,	 XL_NS_SS, "definedName", GSF_XML_NO_CONTENT, NULL, NULL),
  GSF_XML_IN_NODE (WORKBOOK, RECOVERY,	 XL_NS_SS, "fileRecoveryPr", GSF_XML_NO_CONTENT, NULL, NULL),

GSF_XML_IN_NODE_END
};

/****************************************************************************/

static void
xlsx_sst_begin (GsfXMLIn *xin, xmlChar const **attrs)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	int count;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (attr_int (xin, attrs, "uniqueCount", &count))
			g_array_set_size (state->sst, count);
	state->count = 0;
}

static void
xlsx_sstitem_end (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	XLSXStr *entry;

	if (state->count >= state->sst->len)
		g_array_set_size (state->sst, state->count+1);
	entry = &g_array_index (state->sst, XLSXStr, state->count);
	state->count++;
	entry->str = gnm_string_get (xin->content->str);
	if (state->rich_attrs) {
		entry->markup = go_format_new_markup (state->rich_attrs, FALSE);
		state->rich_attrs = NULL;
	}

	/* sst does not have content so that we can ignore whitespace outside
	 * the <t> elements, but the <t>s do have SHARED content */
	g_string_truncate (xin->content, 0);
}

static GsfXMLInNode const xlsx_shared_strings_dtd[] = {
GSF_XML_IN_NODE_FULL (START, START, -1, NULL, GSF_XML_NO_CONTENT, FALSE, TRUE, NULL, NULL, 0),
GSF_XML_IN_NODE_FULL (START, SST, XL_NS_SS, "sst", GSF_XML_NO_CONTENT, FALSE, TRUE, &xlsx_sst_begin, NULL, 0),
  GSF_XML_IN_NODE (SST, ITEM, XL_NS_SS, "si", GSF_XML_NO_CONTENT, NULL, &xlsx_sstitem_end),		/* beta2 */
    GSF_XML_IN_NODE (ITEM, TEXT, XL_NS_SS, "t", GSF_XML_SHARED_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE (ITEM, RICH, XL_NS_SS, "r", GSF_XML_NO_CONTENT, NULL, NULL),
      GSF_XML_IN_NODE (RICH, RICH_TEXT, XL_NS_SS, "t", GSF_XML_SHARED_CONTENT, NULL, NULL),
      GSF_XML_IN_NODE (RICH, RICH_PROPS, XL_NS_SS, "rPr", GSF_XML_NO_CONTENT, NULL, NULL),
#if 0
	GSF_XML_IN_NODE (RICH_PROPS, RICH_FONT, XL_NS_SS, "font", GSF_XML_NO_CONTENT, NULL, NULL),
	/* docs say 'font' xl is generating rFont */
#endif
	GSF_XML_IN_NODE (RICH_PROPS, RICH_FONT, XL_NS_SS, "rFont", GSF_XML_NO_CONTENT, NULL, NULL),

	GSF_XML_IN_NODE (RICH_PROPS, RICH_CHARSET, XL_NS_SS, "charset", GSF_XML_NO_CONTENT, NULL, NULL),
	GSF_XML_IN_NODE (RICH_PROPS, RICH_FAMILY, XL_NS_SS, "family", GSF_XML_NO_CONTENT, NULL, NULL),
	GSF_XML_IN_NODE (RICH_PROPS, RICH_BOLD, XL_NS_SS, "b", GSF_XML_NO_CONTENT, NULL, NULL),
	GSF_XML_IN_NODE (RICH_PROPS, RICH_ITALIC, XL_NS_SS, "i", GSF_XML_NO_CONTENT, NULL, NULL),
	GSF_XML_IN_NODE (RICH_PROPS, RICH_STRIKE, XL_NS_SS, "strike", GSF_XML_NO_CONTENT, NULL, NULL),
	GSF_XML_IN_NODE (RICH_PROPS, RICH_OUTLINE, XL_NS_SS, "outline", GSF_XML_NO_CONTENT, NULL, NULL),
	GSF_XML_IN_NODE (RICH_PROPS, RICH_SHADOW, XL_NS_SS, "shadow", GSF_XML_NO_CONTENT, NULL, NULL),
	GSF_XML_IN_NODE (RICH_PROPS, RICH_CONDENSE, XL_NS_SS, "condense", GSF_XML_NO_CONTENT, NULL, NULL),
	GSF_XML_IN_NODE (RICH_PROPS, RICH_EXTEND, XL_NS_SS, "extend", GSF_XML_NO_CONTENT, NULL, NULL),
	GSF_XML_IN_NODE (RICH_PROPS, RICH_COLOR, XL_NS_SS, "color", GSF_XML_NO_CONTENT, NULL, NULL),
	GSF_XML_IN_NODE (RICH_PROPS, RICH_SZ, XL_NS_SS, "sz", GSF_XML_NO_CONTENT, NULL, NULL),
	GSF_XML_IN_NODE (RICH_PROPS, RICH_ULINE, XL_NS_SS, "u", GSF_XML_NO_CONTENT, NULL, NULL),
	GSF_XML_IN_NODE (RICH_PROPS, RICH_VALIGN, XL_NS_SS, "vertAlign", GSF_XML_NO_CONTENT, NULL, NULL),
	GSF_XML_IN_NODE (RICH_PROPS, RICH_SCHEME, XL_NS_SS, "scheme", GSF_XML_NO_CONTENT, NULL, NULL),
      GSF_XML_IN_NODE (RICH, RICH_PROPS, XL_NS_SS, "rPr", GSF_XML_NO_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE (ITEM, ITEM_PHONETIC_RUN, XL_NS_SS, "rPh", GSF_XML_NO_CONTENT, NULL, NULL),
      GSF_XML_IN_NODE (ITEM_PHONETIC_RUN, PHONETIC_TEXT, XL_NS_SS, "t", GSF_XML_SHARED_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE (ITEM, ITEM_PHONETIC, XL_NS_SS, "phoneticPr", GSF_XML_NO_CONTENT, NULL, NULL),

GSF_XML_IN_NODE_END
};

/****************************************************************************/

static void
xlsx_style_numfmt (GsfXMLIn *xin, xmlChar const **attrs)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	xmlChar const *fmt = NULL;
	xmlChar const *id = NULL;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (0 == strcmp (attrs[0], "numFmtId"))
			id = attrs[1];
		else if (0 == strcmp (attrs[0], "formatCode"))
			fmt = attrs[1];

	if (NULL != id && NULL != fmt)
		g_hash_table_replace (state->num_fmts, g_strdup (id),
			go_format_new_from_XL (fmt));
}

enum {
	XLSX_COLLECT_FONT,
	XLSX_COLLECT_FILLS,
	XLSX_COLLECT_BORDERS,
	XLSX_COLLECT_XFS,
	XLSX_COLLECT_STYLE_XFS,
	XLSX_COLLECT_DXFS,
	XLSX_COLLECT_TABLE_STYLES
};

static void
xlsx_collection_begin (GsfXMLIn *xin, xmlChar const **attrs)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	int count = 0;

	g_return_if_fail (NULL == state->collection);

	state->count = 0;
	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (attr_int (xin, attrs, "count", &count))
			;
	state->collection = g_ptr_array_new ();
	g_ptr_array_set_size (state->collection, count);

	switch (xin->node->user_data.v_int) {
	case XLSX_COLLECT_FONT :	state->fonts = state->collection;	 break;
	case XLSX_COLLECT_FILLS :	state->fills = state->collection;	 break;
	case XLSX_COLLECT_BORDERS :	state->borders = state->collection;	 break;
	case XLSX_COLLECT_XFS :		state->xfs = state->collection;		 break;
	case XLSX_COLLECT_STYLE_XFS :	state->style_xfs = state->collection;	 break;
	case XLSX_COLLECT_DXFS :	state->dxfs = state->collection;	 break;
	case XLSX_COLLECT_TABLE_STYLES: state->table_styles = state->collection; break;
	}
}

static void
xlsx_collection_end (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;

	/* resize just in case the count hint was wrong */
	g_ptr_array_set_size (state->collection, state->count);
	state->count = 0;
	state->collection = NULL;
}

static void
xlsx_col_elem_end (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;

	if (!state->style_accum_partial) {
		GnmStyle *res = state->style_accum;
		state->style_accum = NULL;
		if (state->count >= state->collection->len)
			g_ptr_array_add (state->collection, res);
		else if (NULL != g_ptr_array_index (state->collection, state->count)) {
			g_warning ("dup @ %d = %p", state->count, res);
			gnm_style_unref (res);
		} else
			g_ptr_array_index (state->collection, state->count) = res;
		state->count++;
	}
}

static void
xlsx_col_elem_begin (GsfXMLIn *xin, xmlChar const **attrs)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	if (!state->style_accum_partial) {
		g_return_if_fail (NULL == state->style_accum);
		state->style_accum = gnm_style_new ();
	}
}

static void
xlsx_font_name (GsfXMLIn *xin, xmlChar const **attrs)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (0 == strcmp (attrs[0], "val"))
			gnm_style_set_font_name	(state->style_accum, attrs[1]);
}
static void
xlsx_font_bold (GsfXMLIn *xin, xmlChar const **attrs)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	int val = TRUE;
	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (attr_bool (xin, attrs, "val", &val)) ;
			;
	gnm_style_set_font_bold (state->style_accum, val);
}
static void
xlsx_font_italic (GsfXMLIn *xin, xmlChar const **attrs)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	int val = TRUE;
	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (attr_bool (xin, attrs, "val", &val)) ;
			;
	gnm_style_set_font_italic (state->style_accum, val);
}
static void
xlsx_font_strike (GsfXMLIn *xin, xmlChar const **attrs)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	int val = TRUE;
	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (attr_bool (xin, attrs, "val", &val))
			;
	gnm_style_set_font_strike (state->style_accum, val);
}
static void
xlsx_font_color (GsfXMLIn *xin, xmlChar const **attrs)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	GnmColor *color = elem_color (xin, attrs);

	if (NULL != color)
		gnm_style_set_font_color (state->style_accum, color);
}
static void
xlsx_CT_FontSize (GsfXMLIn *xin, xmlChar const **attrs)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	double val;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (attr_float (xin, attrs, "val", &val))
			gnm_style_set_font_size	(state->style_accum, val);
}
static void
xlsx_font_uline (GsfXMLIn *xin, xmlChar const **attrs)
{
	static EnumVal const types[] = {
		{ "single", UNDERLINE_SINGLE },
		{ "double", UNDERLINE_DOUBLE },
		{ "singleAccounting", UNDERLINE_SINGLE },
		{ "doubleAccounting", UNDERLINE_DOUBLE },
		{ "none", UNDERLINE_NONE },
		{ NULL, 0 }
	};
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	int val = UNDERLINE_SINGLE;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (attr_enum (xin, attrs, "val", types, &val))
			;
	gnm_style_set_font_uline (state->style_accum, val);
}

static void
xlsx_font_valign (GsfXMLIn *xin, xmlChar const **attrs)
{
	static EnumVal const types[] = {
		{ "baseline",	 GO_FONT_SCRIPT_STANDARD },
		{ "superscript", GO_FONT_SCRIPT_SUPER },
		{ "subscript",   GO_FONT_SCRIPT_SUB },
		{ NULL, 0 }
	};
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	int val = UNDERLINE_SINGLE;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (attr_enum (xin, attrs, "val", types, &val))
			gnm_style_set_font_script (state->style_accum, val);
}

static void
xlsx_pattern (GsfXMLIn *xin, xmlChar const **attrs)
{
	static EnumVal const patterns[] = {
		{ "none",		0 },
		{ "solid",		1 },
		{ "mediumGray",		3 },
		{ "darkGray",		2 },
		{ "lightGray",		4 },
		{ "darkHorizontal",	7 },
		{ "darkVertical",	8 },
		{ "darkDown",		10},
		{ "darkUp",		9 },
		{ "darkGrid",		11 },
		{ "darkTrellis",	12 },
		{ "lightHorizontal",	13 },
		{ "lightVertical",	14 },
		{ "lightDown",		15 },
		{ "lightUp",		16 },
		{ "lightGrid",		17 },
		{ "lightTrellis",	18 },
		{ "gray125",		5 },
		{ "gray0625",		6 },
		{ NULL, 0 }
	};
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	int val = 0; /* none */

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (attr_enum (xin, attrs, "patternType", patterns, &val))
			gnm_style_set_pattern (state->style_accum, val);
}
static void
xlsx_pattern_fg (GsfXMLIn *xin, xmlChar const **attrs)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	GnmColor *color = elem_color (xin, attrs);

	if (NULL != color) {
		if (gnm_style_is_element_set (state->style_accum, MSTYLE_COLOR_PATTERN) &&
		    gnm_style_get_pattern (state->style_accum) == 1)
			gnm_style_set_back_color (state->style_accum, color);
		else
			gnm_style_set_pattern_color (state->style_accum, color);
	}
}
static void
xlsx_pattern_bg (GsfXMLIn *xin, xmlChar const **attrs)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	GnmColor *color = elem_color (xin, attrs);
	if (NULL != color) {
		if (!gnm_style_is_element_set (state->style_accum, MSTYLE_COLOR_PATTERN) ||
		    gnm_style_get_pattern (state->style_accum) != 1)
			gnm_style_set_back_color (state->style_accum, color);
		else
			gnm_style_set_pattern_color (state->style_accum, color);
	}
}

static void
xlsx_border_begin (GsfXMLIn *xin, xmlChar const **attrs)
{
	static EnumVal const borders[] = {
		{ "none",		GNM_STYLE_BORDER_NONE },
		{ "thin",		GNM_STYLE_BORDER_THIN },
		{ "medium",		GNM_STYLE_BORDER_MEDIUM },
		{ "dashed",		GNM_STYLE_BORDER_DASHED },
		{ "dotted",		GNM_STYLE_BORDER_DOTTED },
		{ "thick",		GNM_STYLE_BORDER_THICK },
		{ "double",		GNM_STYLE_BORDER_DOUBLE },
		{ "hair",		GNM_STYLE_BORDER_HAIR },
		{ "mediumDashed",	GNM_STYLE_BORDER_MEDIUM_DASH },
		{ "dashDot",		GNM_STYLE_BORDER_DASH_DOT },
		{ "mediumDashDot",	GNM_STYLE_BORDER_MEDIUM_DASH_DOT },
		{ "dashDotDot",		GNM_STYLE_BORDER_DASH_DOT_DOT },
		{ "mediumDashDotDot",	GNM_STYLE_BORDER_MEDIUM_DASH_DOT_DOT },
		{ "slantDashDot",	GNM_STYLE_BORDER_SLANTED_DASH_DOT },
		{ NULL, 0 }
	};
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	int border_style = GNM_STYLE_BORDER_NONE;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (attr_enum (xin, attrs, "style", borders, &border_style))
			;
	state->border_style = border_style;
	state->border_color = NULL;
}

static void
xlsx_border_end (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	GnmStyleBorderLocation const loc = xin->node->user_data.v_int;
	GnmBorder *border = gnm_style_border_fetch (state->border_style,
		state->border_color, gnm_style_border_get_orientation (loc));
	gnm_style_set_border (state->style_accum,
		GNM_STYLE_BORDER_LOCATION_TO_STYLE_ELEMENT (loc),
		border);
}

static void
xlsx_border_color (GsfXMLIn *xin, xmlChar const **attrs)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	GnmColor *color = elem_color (xin, attrs);
	if (state->border_color)
		style_color_unref (state->border_color);
	state->border_color = color;
}

static void
xlsx_xf_begin (GsfXMLIn *xin, xmlChar const **attrs)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	GnmStyle *style = gnm_style_new_default ();
	GPtrArray *elem = NULL;
	int indx;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2) {
		if (0 == strcmp (attrs[0], "numFmtId")) {
			GOFormat *fmt = xlsx_get_num_fmt (xin, attrs[1]);
			if (NULL != fmt)
				gnm_style_set_format (style, fmt);
		} else if (attr_int (xin, attrs, "fontId", &indx))
			elem = state->fonts;
		else if (attr_int (xin, attrs, "fillId", &indx))
			elem = state->fills;
		else if (attr_int (xin, attrs, "borderId", &indx))
			elem = state->borders;

		if (NULL != elem) {
			GnmStyle *existing = NULL;
			if (0 <= indx && indx < (int)elem->len)
				existing = g_ptr_array_index (elem, indx);
			if (NULL != existing) {
				GnmStyle *merged = gnm_style_new_merged (existing, style);
				gnm_style_unref (style);
				style = merged;
			} else
				xlsx_warning (xin, _("Missing record '%d'"), indx);
			elem = NULL;
		}
	}
	state->style_accum = style;
#if 0
		"xfId"			parent style ??
		"quotePrefix"			??

		"applyNumberFormat"
		"applyFont"
		"applyFill"
		"applyBorder"
		"applyAlignment"
		"applyProtection"
#endif
}
static void
xlsx_xf_end (GsfXMLIn *xin, GsfXMLBlob *blob)
{
	xlsx_col_elem_end (xin, blob);
}

static void
xlsx_xf_align (GsfXMLIn *xin, xmlChar const **attrs)
{
	static EnumVal const haligns[] = {
		{ "general" , HALIGN_GENERAL },
		{ "left" , HALIGN_LEFT },
		{ "center" , HALIGN_CENTER },
		{ "right" , HALIGN_RIGHT },
		{ "fill" , HALIGN_FILL },
		{ "justify" , HALIGN_JUSTIFY },
		{ "centerContinuous" , HALIGN_CENTER_ACROSS_SELECTION },
		{ "distributed" , HALIGN_DISTRIBUTED },
		{ NULL, 0 }
	};

	static EnumVal const valigns[] = {
		{ "top", VALIGN_TOP },
		{ "center", VALIGN_CENTER },
		{ "bottom", VALIGN_BOTTOM },
		{ "justify", VALIGN_JUSTIFY },
		{ "distributed", VALIGN_DISTRIBUTED },
		{ NULL, 0 }
	};

	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	int halign = HALIGN_GENERAL;
	int valign = VALIGN_BOTTOM;
	int rotation = 0, indent = 0;
	int wrapText = FALSE, justifyLastLine = FALSE, shrinkToFit = FALSE;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (attr_enum (xin, attrs, "horizontal", haligns, &halign)) ;
		else if (attr_enum (xin, attrs, "vertical", valigns, &valign)) ;
		else if (attr_int (xin, attrs, "textRotation", &rotation));
		else if (attr_bool (xin, attrs, "wrapText", &wrapText)) ;
		else if (attr_int (xin, attrs, "indent", &indent)) ;
		else if (attr_bool (xin, attrs, "justifyLastLine", &justifyLastLine)) ;
		else if (attr_bool (xin, attrs, "shrinkToFit", &shrinkToFit)) ;
		/* "mergeCell" type="xs:boolean" use="optional" default="false" */
		/* "readingOrder" type="xs:unsignedInt" use="optional" default="0" */

		gnm_style_set_align_h	   (state->style_accum, halign);
		gnm_style_set_align_v	   (state->style_accum, valign);
		gnm_style_set_rotation	   (state->style_accum,
			(rotation == 0xff) ? -1 : ((rotation > 90) ? (360 + 90 - rotation) : rotation));
		gnm_style_set_wrap_text   (state->style_accum, wrapText);
		gnm_style_set_indent	   (state->style_accum, indent);
		gnm_style_set_shrink_to_fit (state->style_accum, shrinkToFit);
}
static void
xlsx_xf_protect (GsfXMLIn *xin, xmlChar const **attrs)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	int locked = TRUE;
	int hidden = TRUE;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (attr_bool (xin, attrs, "locked", &locked)) ;
		else if (attr_bool (xin, attrs, "hidden", &hidden)) ;
	gnm_style_set_contents_locked (state->style_accum, locked);
	gnm_style_set_contents_hidden (state->style_accum, hidden);
}

static void
xlsx_cell_style (GsfXMLIn *xin, xmlChar const **attrs)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	xmlChar const *name = NULL;
	xmlChar const *id = NULL;
	GnmStyle *style = NULL;
	int tmp;

	/* cellStyle name="Normal" xfId="0" builtinId="0" */
	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (attr_int (xin, attrs, "xfId", &tmp))
			style = xlsx_get_xf (xin, tmp);
		else if (0 == strcmp (attrs[0], "name"))
			name = attrs[1];
		else if (0 == strcmp (attrs[0], "builtinId"))
			id = attrs[1];

	if (NULL != style && NULL != id) {
		gnm_style_ref (style);
		g_hash_table_replace (state->cell_styles, g_strdup (id), style);
	}
}

static void
xlsx_dxf_begin (GsfXMLIn *xin, xmlChar const **attrs)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	state->style_accum_partial = TRUE;
	state->style_accum = gnm_style_new ();
}
static void
xlsx_dxf_end (GsfXMLIn *xin, GsfXMLBlob *blob)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	state->style_accum_partial = FALSE;
	xlsx_col_elem_end (xin, blob);
}

static GsfXMLInNode const xlsx_styles_dtd[] = {
GSF_XML_IN_NODE_FULL (START, START, -1, NULL, GSF_XML_NO_CONTENT, FALSE, TRUE, NULL, NULL, 0),
GSF_XML_IN_NODE_FULL (START, STYLE_INFO, XL_NS_SS, "styleSheet", GSF_XML_NO_CONTENT, FALSE, TRUE, NULL, NULL, 0),

  GSF_XML_IN_NODE (STYLE_INFO, NUM_FMTS, XL_NS_SS, "numFmts", GSF_XML_NO_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE (NUM_FMTS, NUM_FMT, XL_NS_SS, "numFmt", GSF_XML_NO_CONTENT, &xlsx_style_numfmt, NULL),

  GSF_XML_IN_NODE_FULL (STYLE_INFO, FONTS, XL_NS_SS, "fonts", GSF_XML_NO_CONTENT,
			FALSE, FALSE, &xlsx_collection_begin, &xlsx_collection_end, XLSX_COLLECT_FONT),
    GSF_XML_IN_NODE (FONTS, FONT, XL_NS_SS, "font", GSF_XML_NO_CONTENT, &xlsx_col_elem_begin, &xlsx_col_elem_end),
      GSF_XML_IN_NODE (FONT, FONT_NAME,	     XL_NS_SS, "name",	    GSF_XML_NO_CONTENT, &xlsx_font_name, NULL),
      GSF_XML_IN_NODE (FONT, FONT_CHARSET,   XL_NS_SS, "charset",   GSF_XML_NO_CONTENT, NULL, NULL),
      GSF_XML_IN_NODE (FONT, FONT_FAMILY,    XL_NS_SS, "family",    GSF_XML_NO_CONTENT, NULL, NULL),
      GSF_XML_IN_NODE (FONT, FONT_BOLD,	     XL_NS_SS, "b",	    GSF_XML_NO_CONTENT, &xlsx_font_bold, NULL),
      GSF_XML_IN_NODE (FONT, FONT_ITALIC,    XL_NS_SS, "i",	    GSF_XML_NO_CONTENT, &xlsx_font_italic, NULL),
      GSF_XML_IN_NODE (FONT, FONT_STRIKE,    XL_NS_SS, "strike",    GSF_XML_NO_CONTENT, &xlsx_font_strike, NULL),
      GSF_XML_IN_NODE (FONT, FONT_OUTLINE,   XL_NS_SS, "outline",   GSF_XML_NO_CONTENT, NULL, NULL),
      GSF_XML_IN_NODE (FONT, FONT_SHADOW,    XL_NS_SS, "shadow",    GSF_XML_NO_CONTENT, NULL, NULL),
      GSF_XML_IN_NODE (FONT, FONT_CONDENSE,  XL_NS_SS, "condense",  GSF_XML_NO_CONTENT, NULL, NULL),
      GSF_XML_IN_NODE (FONT, FONT_EXTEND,    XL_NS_SS, "extend",    GSF_XML_NO_CONTENT, NULL, NULL),
      GSF_XML_IN_NODE (FONT, FONT_COLOR,     XL_NS_SS, "color",     GSF_XML_NO_CONTENT, &xlsx_font_color, NULL),
      GSF_XML_IN_NODE (FONT, FONT_SZ,	     XL_NS_SS, "sz",	    GSF_XML_NO_CONTENT,	&xlsx_CT_FontSize, NULL),
      GSF_XML_IN_NODE (FONT, FONT_ULINE,     XL_NS_SS, "u",	    GSF_XML_NO_CONTENT,	&xlsx_font_uline, NULL),
      GSF_XML_IN_NODE (FONT, FONT_VERTALIGN, XL_NS_SS, "vertAlign", GSF_XML_NO_CONTENT, &xlsx_font_valign, NULL),
      GSF_XML_IN_NODE (FONT, FONT_SCHEME,    XL_NS_SS, "scheme",    GSF_XML_NO_CONTENT, NULL, NULL),

  GSF_XML_IN_NODE_FULL (STYLE_INFO, FILLS, XL_NS_SS, "fills", GSF_XML_NO_CONTENT,
			FALSE, FALSE, &xlsx_collection_begin, &xlsx_collection_end, XLSX_COLLECT_FILLS),
    GSF_XML_IN_NODE (FILLS, FILL, XL_NS_SS, "fill", GSF_XML_NO_CONTENT, &xlsx_col_elem_begin, &xlsx_col_elem_end),
      GSF_XML_IN_NODE (FILL, PATTERN_FILL, XL_NS_SS, "patternFill", GSF_XML_NO_CONTENT, &xlsx_pattern, NULL),
	GSF_XML_IN_NODE (PATTERN_FILL, PATTERN_FILL_FG,  XL_NS_SS, "fgColor", GSF_XML_NO_CONTENT, &xlsx_pattern_fg, NULL),
	GSF_XML_IN_NODE (PATTERN_FILL, PATTERN_FILL_BG,  XL_NS_SS, "bgColor", GSF_XML_NO_CONTENT, &xlsx_pattern_bg, NULL),
      GSF_XML_IN_NODE (FILL, IMAGE_FILL, XL_NS_SS, "image", GSF_XML_NO_CONTENT, NULL, NULL),
      GSF_XML_IN_NODE (FILL, GRADIENT_FILL, XL_NS_SS, "gradient", GSF_XML_NO_CONTENT, NULL, NULL),
	GSF_XML_IN_NODE (GRADIENT_FILL, GRADIENT_STOPS, XL_NS_SS, "stop", GSF_XML_NO_CONTENT, NULL, NULL),

  GSF_XML_IN_NODE_FULL (STYLE_INFO, BORDERS, XL_NS_SS, "borders", GSF_XML_NO_CONTENT,
			FALSE, FALSE, &xlsx_collection_begin, &xlsx_collection_end, XLSX_COLLECT_BORDERS),
    GSF_XML_IN_NODE (BORDERS, BORDER, XL_NS_SS, "border", GSF_XML_NO_CONTENT, &xlsx_col_elem_begin, &xlsx_col_elem_end),
      GSF_XML_IN_NODE_FULL (BORDER, LEFT_B, XL_NS_SS, "left", GSF_XML_NO_CONTENT, FALSE, FALSE,
			    &xlsx_border_begin, &xlsx_border_end, GNM_STYLE_BORDER_LEFT),
        GSF_XML_IN_NODE (LEFT_B, LEFT_COLOR, XL_NS_SS, "color", GSF_XML_NO_CONTENT, &xlsx_border_color, NULL),
      GSF_XML_IN_NODE_FULL (BORDER, RIGHT_B, XL_NS_SS, "right", GSF_XML_NO_CONTENT, FALSE, FALSE,
			    &xlsx_border_begin, &xlsx_border_end, GNM_STYLE_BORDER_RIGHT),
        GSF_XML_IN_NODE (RIGHT_B, RIGHT_COLOR, XL_NS_SS, "color", GSF_XML_NO_CONTENT, &xlsx_border_color, NULL),
      GSF_XML_IN_NODE_FULL (BORDER, TOP_B, XL_NS_SS,	"top", GSF_XML_NO_CONTENT, FALSE, FALSE,
			    &xlsx_border_begin, &xlsx_border_end, GNM_STYLE_BORDER_TOP),
        GSF_XML_IN_NODE (TOP_B, TOP_COLOR, XL_NS_SS, "color", GSF_XML_NO_CONTENT, &xlsx_border_color, NULL),
      GSF_XML_IN_NODE_FULL (BORDER, BOTTOM_B, XL_NS_SS, "bottom", GSF_XML_NO_CONTENT, FALSE, FALSE,
			    &xlsx_border_begin, &xlsx_border_end, GNM_STYLE_BORDER_BOTTOM),
        GSF_XML_IN_NODE (BOTTOM_B, BOTTOM_COLOR, XL_NS_SS, "color", GSF_XML_NO_CONTENT, &xlsx_border_color, NULL),
      GSF_XML_IN_NODE_FULL (BORDER, DIAG_B, XL_NS_SS, "diagonal", GSF_XML_NO_CONTENT, FALSE, FALSE,
			    &xlsx_border_begin, &xlsx_border_end, GNM_STYLE_BORDER_DIAG),
        GSF_XML_IN_NODE (DIAG_B, DIAG_COLOR, XL_NS_SS, "color", GSF_XML_NO_CONTENT, &xlsx_border_color, NULL),

      GSF_XML_IN_NODE (BORDER, BORDER_VERT, XL_NS_SS,	"vertical", GSF_XML_NO_CONTENT, NULL, NULL),
        GSF_XML_IN_NODE (BORDER_VERT, VERT_COLOR, XL_NS_SS, "color", GSF_XML_NO_CONTENT, NULL, NULL),
      GSF_XML_IN_NODE (BORDER, BORDER_HORIZ, XL_NS_SS,	"horizontal", GSF_XML_NO_CONTENT, NULL, NULL),
        GSF_XML_IN_NODE (BORDER_HORIZ, HORIZ_COLOR, XL_NS_SS, "color", GSF_XML_NO_CONTENT, NULL, NULL),

  GSF_XML_IN_NODE_FULL (STYLE_INFO, XFS, XL_NS_SS, "cellXfs", GSF_XML_NO_CONTENT,
			FALSE, FALSE, &xlsx_collection_begin, &xlsx_collection_end, XLSX_COLLECT_XFS),
    GSF_XML_IN_NODE (XFS, XF, XL_NS_SS, "xf", GSF_XML_NO_CONTENT, &xlsx_xf_begin, &xlsx_xf_end),
      GSF_XML_IN_NODE (XF, ALIGNMENT, XL_NS_SS, "alignment", GSF_XML_NO_CONTENT, &xlsx_xf_align, NULL),
      GSF_XML_IN_NODE (XF, PROTECTION, XL_NS_SS, "protection", GSF_XML_NO_CONTENT, &xlsx_xf_protect, NULL),

  GSF_XML_IN_NODE_FULL (STYLE_INFO, STYLE_XFS, XL_NS_SS, "cellStyleXfs", GSF_XML_NO_CONTENT,
		   FALSE, FALSE, &xlsx_collection_begin, &xlsx_collection_end, XLSX_COLLECT_STYLE_XFS),
    GSF_XML_IN_NODE (STYLE_XFS, STYLE_XF, XL_NS_SS, "xf", GSF_XML_NO_CONTENT, &xlsx_xf_begin, &xlsx_xf_end),
      GSF_XML_IN_NODE (STYLE_XF, STYLE_ALIGNMENT, XL_NS_SS, "alignment", GSF_XML_NO_CONTENT, &xlsx_xf_align, NULL),
      GSF_XML_IN_NODE (STYLE_XF, STYLE_PROTECTION, XL_NS_SS, "protection", GSF_XML_NO_CONTENT, &xlsx_xf_protect, NULL),

  GSF_XML_IN_NODE (STYLE_INFO, STYLE_NAMES, XL_NS_SS, "cellStyles", GSF_XML_NO_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE (STYLE_NAMES, STYLE_NAME, XL_NS_SS, "cellStyle", GSF_XML_NO_CONTENT, &xlsx_cell_style, NULL),

  GSF_XML_IN_NODE_FULL (STYLE_INFO, PARTIAL_XFS, XL_NS_SS, "dxfs", GSF_XML_NO_CONTENT,
			FALSE, FALSE, &xlsx_collection_begin, &xlsx_collection_end, XLSX_COLLECT_DXFS),
    GSF_XML_IN_NODE (PARTIAL_XFS, PARTIAL_XF, XL_NS_SS, "dxf", GSF_XML_NO_CONTENT, &xlsx_dxf_begin, &xlsx_dxf_end),
      GSF_XML_IN_NODE (PARTIAL_XF, NUM_FMT, XL_NS_SS, "numFmt", GSF_XML_NO_CONTENT, NULL, NULL),		/* 2nd Def */
      GSF_XML_IN_NODE (PARTIAL_XF, FONT,    XL_NS_SS, "font", GSF_XML_NO_CONTENT, NULL, NULL),			/* 2nd Def */
      GSF_XML_IN_NODE (PARTIAL_XF, FILL,    XL_NS_SS, "fill", GSF_XML_NO_CONTENT, NULL, NULL),			/* 2nd Def */
      GSF_XML_IN_NODE (PARTIAL_XF, BORDER,  XL_NS_SS, "border", GSF_XML_NO_CONTENT, NULL, NULL),		/* 2nd Def */
      GSF_XML_IN_NODE (PARTIAL_XF, DXF_ALIGNMENT, XL_NS_SS, "alignment", GSF_XML_NO_CONTENT, NULL, NULL),
      GSF_XML_IN_NODE (PARTIAL_XF, DXF_PROTECTION, XL_NS_SS, "protection", GSF_XML_NO_CONTENT, NULL, NULL),
      GSF_XML_IN_NODE (PARTIAL_XF, DXF_FSB, XL_NS_SS, "extLst", GSF_XML_NO_CONTENT, NULL, NULL),

  GSF_XML_IN_NODE_FULL (STYLE_INFO, TABLE_STYLES, XL_NS_SS, "tableStyles", GSF_XML_NO_CONTENT,
			FALSE, FALSE, &xlsx_collection_begin, &xlsx_collection_end, XLSX_COLLECT_TABLE_STYLES),
    GSF_XML_IN_NODE (TABLE_STYLES, TABLE_STYLE, XL_NS_SS, "tableStyle", GSF_XML_NO_CONTENT, NULL, NULL),

  GSF_XML_IN_NODE (STYLE_INFO, COLORS, XL_NS_SS, "colors", GSF_XML_NO_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE (COLORS, INDEXED_COLORS, XL_NS_SS, "indexedColors", GSF_XML_NO_CONTENT, NULL, NULL),
      GSF_XML_IN_NODE (INDEXED_COLORS, INDEXED_RGB, XL_NS_SS, "rgbColor", GSF_XML_NO_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE (COLORS, THEME_COLORS, XL_NS_SS, "themeColors", GSF_XML_NO_CONTENT, NULL, NULL),
      GSF_XML_IN_NODE (THEME_COLORS, THEMED_RGB, XL_NS_SS, "rgbColor", GSF_XML_NO_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE (COLORS, MRU_COLORS, XL_NS_SS, "mruColors", GSF_XML_NO_CONTENT, NULL, NULL),
      GSF_XML_IN_NODE (MRU_COLORS, MRU_COLOR, XL_NS_SS, "color", GSF_XML_NO_CONTENT, NULL, NULL),

GSF_XML_IN_NODE_END
};

/****************************************************************************/

static void
xlsx_theme_color_sys (GsfXMLIn *xin, xmlChar const **attrs)
{
	XLSXReadState	*state = (XLSXReadState *)xin->user_state;
	GOColor c;
	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (attr_gocolor (xin, attrs, "lastClr", &c)) {
			g_hash_table_replace (state->theme_colors,
				g_strdup (((GsfXMLInNode *)xin->node_stack->data)->name),
				GUINT_TO_POINTER (c));
		}
}
static void
xlsx_theme_color_rgb (GsfXMLIn *xin, xmlChar const **attrs)
{
	XLSXReadState	*state = (XLSXReadState *)xin->user_state;
	GOColor c;
	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (attr_gocolor (xin, attrs, "val", &c)) {
			g_hash_table_replace (state->theme_colors,
				g_strdup (((GsfXMLInNode *)xin->node_stack->data)->name),
				GUINT_TO_POINTER (c));
		}
}
static void
xlsx_theme_start (GsfXMLIn *xin, xmlChar const **attrs)
{
	XLSXReadState	*state = (XLSXReadState *)xin->user_state;
	state->theme_colors = g_hash_table_new_full (g_str_hash, g_str_equal,
		(GDestroyNotify)g_free, NULL);
}

static GsfXMLInNode const xlsx_theme_dtd[] = {
GSF_XML_IN_NODE_FULL (START, START, -1, NULL, GSF_XML_NO_CONTENT, FALSE, TRUE, NULL, NULL, 0),
GSF_XML_IN_NODE_FULL (START, THEME, XL_NS_DRAW, "theme", GSF_XML_NO_CONTENT, FALSE, TRUE, &xlsx_theme_start, NULL, 0),
  GSF_XML_IN_NODE (THEME, ELEMENTS, XL_NS_DRAW, "themeElements", GSF_XML_NO_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE (ELEMENTS, COLOR_SCHEME, XL_NS_DRAW, "clrScheme", GSF_XML_NO_CONTENT, NULL, NULL),
      GSF_XML_IN_NODE (COLOR_SCHEME, dk1, XL_NS_DRAW, "dk1", GSF_XML_NO_CONTENT, NULL, NULL),
        GSF_XML_IN_NODE (dk1, SYS_COLOR, XL_NS_DRAW, "sysClr", GSF_XML_NO_CONTENT, &xlsx_theme_color_sys, NULL),
        GSF_XML_IN_NODE (dk1, RGB_COLOR, XL_NS_DRAW, "srgbClr", GSF_XML_NO_CONTENT, &xlsx_theme_color_rgb, NULL),
          GSF_XML_IN_NODE (RGB_COLOR, COLOR_ALPHA, XL_NS_DRAW, "alpha", GSF_XML_NO_CONTENT, NULL, NULL),
      GSF_XML_IN_NODE (COLOR_SCHEME, lt1, XL_NS_DRAW, "lt1", GSF_XML_NO_CONTENT, NULL, NULL),
        GSF_XML_IN_NODE (lt1, SYS_COLOR, XL_NS_DRAW, "sysClr", GSF_XML_NO_CONTENT, NULL, NULL),	/* 2nd Def */
        GSF_XML_IN_NODE (lt1, RGB_COLOR, XL_NS_DRAW, "srgbClr", GSF_XML_NO_CONTENT, NULL, NULL),	/* 2nd Def */
      GSF_XML_IN_NODE (COLOR_SCHEME, lt2, XL_NS_DRAW, "lt2", GSF_XML_NO_CONTENT, NULL, NULL),
        GSF_XML_IN_NODE (lt2, SYS_COLOR, XL_NS_DRAW, "sysClr", GSF_XML_NO_CONTENT, NULL, NULL),	/* 2nd Def */
        GSF_XML_IN_NODE (lt2, RGB_COLOR, XL_NS_DRAW, "srgbClr", GSF_XML_NO_CONTENT, NULL, NULL),	/* 2nd Def */
      GSF_XML_IN_NODE (COLOR_SCHEME, dk2, XL_NS_DRAW, "dk2", GSF_XML_NO_CONTENT, NULL, NULL),
        GSF_XML_IN_NODE (dk2, SYS_COLOR, XL_NS_DRAW, "sysClr", GSF_XML_NO_CONTENT, NULL, NULL),	/* 2nd Def */
        GSF_XML_IN_NODE (dk2, RGB_COLOR, XL_NS_DRAW, "srgbClr", GSF_XML_NO_CONTENT, NULL, NULL),	/* 2nd Def */
      GSF_XML_IN_NODE (COLOR_SCHEME, accent1, XL_NS_DRAW, "accent1", GSF_XML_NO_CONTENT, NULL, NULL),
        GSF_XML_IN_NODE (accent1, SYS_COLOR, XL_NS_DRAW, "sysClr", GSF_XML_NO_CONTENT, NULL, NULL),	/* 2nd Def */
        GSF_XML_IN_NODE (accent1, RGB_COLOR, XL_NS_DRAW, "srgbClr", GSF_XML_NO_CONTENT, NULL, NULL),	/* 2nd Def */
      GSF_XML_IN_NODE (COLOR_SCHEME, accent2, XL_NS_DRAW, "accent2", GSF_XML_NO_CONTENT, NULL, NULL),
        GSF_XML_IN_NODE (accent2, SYS_COLOR, XL_NS_DRAW, "sysClr", GSF_XML_NO_CONTENT, NULL, NULL),	/* 2nd Def */
        GSF_XML_IN_NODE (accent2, RGB_COLOR, XL_NS_DRAW, "srgbClr", GSF_XML_NO_CONTENT, NULL, NULL),	/* 2nd Def */
      GSF_XML_IN_NODE (COLOR_SCHEME, accent3, XL_NS_DRAW, "accent3", GSF_XML_NO_CONTENT, NULL, NULL),
        GSF_XML_IN_NODE (accent3, SYS_COLOR, XL_NS_DRAW, "sysClr", GSF_XML_NO_CONTENT, NULL, NULL),	/* 2nd Def */
        GSF_XML_IN_NODE (accent3, RGB_COLOR, XL_NS_DRAW, "srgbClr", GSF_XML_NO_CONTENT, NULL, NULL),	/* 2nd Def */
      GSF_XML_IN_NODE (COLOR_SCHEME, accent4, XL_NS_DRAW, "accent4", GSF_XML_NO_CONTENT, NULL, NULL),
        GSF_XML_IN_NODE (accent4, SYS_COLOR, XL_NS_DRAW, "sysClr", GSF_XML_NO_CONTENT, NULL, NULL),	/* 2nd Def */
        GSF_XML_IN_NODE (accent4, RGB_COLOR, XL_NS_DRAW, "srgbClr", GSF_XML_NO_CONTENT, NULL, NULL),	/* 2nd Def */
      GSF_XML_IN_NODE (COLOR_SCHEME, accent5, XL_NS_DRAW, "accent5", GSF_XML_NO_CONTENT, NULL, NULL),
        GSF_XML_IN_NODE (accent5, SYS_COLOR, XL_NS_DRAW, "sysClr", GSF_XML_NO_CONTENT, NULL, NULL),	/* 2nd Def */
        GSF_XML_IN_NODE (accent5, RGB_COLOR, XL_NS_DRAW, "srgbClr", GSF_XML_NO_CONTENT, NULL, NULL),	/* 2nd Def */
      GSF_XML_IN_NODE (COLOR_SCHEME, accent6, XL_NS_DRAW, "accent6", GSF_XML_NO_CONTENT, NULL, NULL),
        GSF_XML_IN_NODE (accent6, SYS_COLOR, XL_NS_DRAW, "sysClr", GSF_XML_NO_CONTENT, NULL, NULL),	/* 2nd Def */
        GSF_XML_IN_NODE (accent6, RGB_COLOR, XL_NS_DRAW, "srgbClr", GSF_XML_NO_CONTENT, NULL, NULL),	/* 2nd Def */
      GSF_XML_IN_NODE (COLOR_SCHEME, hlink, XL_NS_DRAW, "hlink", GSF_XML_NO_CONTENT, NULL, NULL),
        GSF_XML_IN_NODE (hlink, SYS_COLOR, XL_NS_DRAW, "sysClr", GSF_XML_NO_CONTENT, NULL, NULL),	/* 2nd Def */
        GSF_XML_IN_NODE (hlink, RGB_COLOR, XL_NS_DRAW, "srgbClr", GSF_XML_NO_CONTENT, NULL, NULL),	/* 2nd Def */
      GSF_XML_IN_NODE (COLOR_SCHEME, folHlink, XL_NS_DRAW, "folHlink", GSF_XML_NO_CONTENT, NULL, NULL),
        GSF_XML_IN_NODE (folHlink, SYS_COLOR, XL_NS_DRAW, "sysClr", GSF_XML_NO_CONTENT, NULL, NULL),	/* 2nd Def */
        GSF_XML_IN_NODE (folHlink, RGB_COLOR, XL_NS_DRAW, "srgbClr", GSF_XML_NO_CONTENT, NULL, NULL),/* 2nd Def */

    GSF_XML_IN_NODE (ELEMENTS, FONT_SCHEME, XL_NS_DRAW, "fontScheme", GSF_XML_NO_CONTENT, NULL, NULL),
      GSF_XML_IN_NODE (FONT_SCHEME, MAJOR_FONT, XL_NS_DRAW, "majorFont", GSF_XML_NO_CONTENT, NULL, NULL),
        GSF_XML_IN_NODE (MAJOR_FONT, FONT_CS, XL_NS_DRAW, "cs", GSF_XML_NO_CONTENT, NULL, NULL),
        GSF_XML_IN_NODE (MAJOR_FONT, FONT_EA, XL_NS_DRAW, "ea", GSF_XML_NO_CONTENT, NULL, NULL),
        GSF_XML_IN_NODE (MAJOR_FONT, FONT_FONT, XL_NS_DRAW, "font", GSF_XML_NO_CONTENT, NULL, NULL),
        GSF_XML_IN_NODE (MAJOR_FONT, FONT_LATIN, XL_NS_DRAW, "latin", GSF_XML_NO_CONTENT, NULL, NULL),
      GSF_XML_IN_NODE (FONT_SCHEME, MINOR_FONT, XL_NS_DRAW, "minorFont", GSF_XML_NO_CONTENT, NULL, NULL),
        GSF_XML_IN_NODE (MINOR_FONT, FONT_CS, XL_NS_DRAW, "cs", GSF_XML_NO_CONTENT, NULL, NULL),
        GSF_XML_IN_NODE (MINOR_FONT, FONT_EA, XL_NS_DRAW, "ea", GSF_XML_NO_CONTENT, NULL, NULL),
        GSF_XML_IN_NODE (MINOR_FONT, FONT_FONT, XL_NS_DRAW, "font", GSF_XML_NO_CONTENT, NULL, NULL),
        GSF_XML_IN_NODE (MINOR_FONT, FONT_LATIN, XL_NS_DRAW, "latin", GSF_XML_NO_CONTENT, NULL, NULL),

    GSF_XML_IN_NODE (ELEMENTS, FORMAT_SCHEME, XL_NS_DRAW, "fmtScheme", GSF_XML_NO_CONTENT, NULL, NULL),
      GSF_XML_IN_NODE (FORMAT_SCHEME, FILL_STYLE_LIST,	XL_NS_DRAW, "fillStyleLst", GSF_XML_NO_CONTENT, NULL, NULL),
        GSF_XML_IN_NODE (FILL_STYLE_LIST,  SOLID_FILL, XL_NS_DRAW, "solidFill", GSF_XML_NO_CONTENT, NULL, NULL),
          GSF_XML_IN_NODE (SOLID_FILL, SCHEME_COLOR, XL_NS_DRAW, "schemeClr", GSF_XML_NO_CONTENT, NULL, NULL),
           GSF_XML_IN_NODE (SCHEME_COLOR, COLOR_TINT, XL_NS_DRAW, "tint", GSF_XML_NO_CONTENT, NULL, NULL),
           GSF_XML_IN_NODE (SCHEME_COLOR, COLOR_LUM, XL_NS_DRAW, "lumMod", GSF_XML_NO_CONTENT, NULL, NULL),
           GSF_XML_IN_NODE (SCHEME_COLOR, COLOR_SAT, XL_NS_DRAW, "satMod", GSF_XML_NO_CONTENT, NULL, NULL),
           GSF_XML_IN_NODE (SCHEME_COLOR, COLOR_SHADE, XL_NS_DRAW, "shade", GSF_XML_NO_CONTENT, NULL, NULL),
        GSF_XML_IN_NODE (FILL_STYLE_LIST,  GRAD_FILL, XL_NS_DRAW, "gradFill", GSF_XML_NO_CONTENT, NULL, NULL),
          GSF_XML_IN_NODE (GRAD_FILL, GRAD_PATH, XL_NS_DRAW, "path", GSF_XML_NO_CONTENT, NULL, NULL),
            GSF_XML_IN_NODE (GRAD_PATH, GRAD_PATH_RECT, XL_NS_DRAW, "fillToRect", GSF_XML_NO_CONTENT, NULL, NULL),
	  GSF_XML_IN_NODE (GRAD_FILL, GRAD_LIST, XL_NS_DRAW, "gsLst", GSF_XML_NO_CONTENT, NULL, NULL),
	   GSF_XML_IN_NODE (GRAD_LIST, GRAD_LIST_ITEM, XL_NS_DRAW, "gs", GSF_XML_NO_CONTENT, NULL, NULL),
	     GSF_XML_IN_NODE (GRAD_LIST_ITEM, RGB_COLOR, XL_NS_DRAW, "srgbClr", GSF_XML_NO_CONTENT, NULL, NULL),	/* 2nd Def */
             GSF_XML_IN_NODE (GRAD_LIST_ITEM, SCHEME_COLOR, XL_NS_DRAW, "schemeClr", GSF_XML_NO_CONTENT, NULL, NULL),	/* 2nd Def */
	  GSF_XML_IN_NODE (GRAD_FILL, GRAD_LINE,	XL_NS_DRAW, "lin", GSF_XML_NO_CONTENT, NULL, NULL),

      GSF_XML_IN_NODE (FORMAT_SCHEME, BG_FILL_STYLE_LIST,	XL_NS_DRAW, "bgFillStyleLst", GSF_XML_NO_CONTENT, NULL, NULL),
        GSF_XML_IN_NODE (BG_FILL_STYLE_LIST, GRAD_FILL, XL_NS_DRAW, "gradFill", GSF_XML_NO_CONTENT, NULL, NULL),	/* 2nd Def */
        GSF_XML_IN_NODE (BG_FILL_STYLE_LIST, SOLID_FILL, XL_NS_DRAW, "solidFill", GSF_XML_NO_CONTENT, NULL, NULL),	/* 2nd Def */
      GSF_XML_IN_NODE (FORMAT_SCHEME, LINE_STYLE_LIST,	XL_NS_DRAW, "lnStyleLst", GSF_XML_NO_CONTENT, NULL, NULL),
        GSF_XML_IN_NODE (LINE_STYLE_LIST, LINE_STYLE, XL_NS_DRAW, "ln", GSF_XML_NO_CONTENT, NULL, NULL),
	  GSF_XML_IN_NODE (LINE_STYLE, LN_NOFILL, XL_NS_DRAW, "noFill", GSF_XML_NO_CONTENT, NULL, NULL),
	  GSF_XML_IN_NODE (LINE_STYLE, LN_DASH, XL_NS_DRAW, "prstDash", GSF_XML_NO_CONTENT, NULL, NULL),		/* 2nd Def */
	  GSF_XML_IN_NODE (LINE_STYLE, SOLID_FILL, XL_NS_DRAW, "solidFill", GSF_XML_NO_CONTENT, NULL, NULL),		/* 2nd Def */
	  GSF_XML_IN_NODE (LINE_STYLE, FILL_PATT,	XL_NS_DRAW, "pattFill", GSF_XML_NO_CONTENT, NULL, NULL),
	  GSF_XML_IN_NODE (FORMAT_SCHEME, EFFECT_STYLE_LIST,	XL_NS_DRAW, "effectStyleLst", GSF_XML_NO_CONTENT, NULL, NULL),
            GSF_XML_IN_NODE (EFFECT_STYLE_LIST, EFFECT_STYLE,	XL_NS_DRAW, "effectStyle", GSF_XML_NO_CONTENT, NULL, NULL),
	      GSF_XML_IN_NODE (EFFECT_STYLE, EFFECT_PROP, XL_NS_DRAW, "sp3d", GSF_XML_NO_CONTENT, NULL, NULL),
		GSF_XML_IN_NODE (EFFECT_PROP, PROP_BEVEL, XL_NS_DRAW, "bevelT", GSF_XML_NO_CONTENT, NULL, NULL),
	      GSF_XML_IN_NODE (EFFECT_STYLE, EFFECT_LIST, XL_NS_DRAW, "effectLst", GSF_XML_NO_CONTENT, NULL, NULL),
		GSF_XML_IN_NODE (EFFECT_LIST, OUTER_SHADOW, XL_NS_DRAW, "outerShdw", GSF_XML_NO_CONTENT, NULL, NULL),
		  GSF_XML_IN_NODE (OUTER_SHADOW, RGB_COLOR, XL_NS_DRAW, "srgbClr", GSF_XML_NO_CONTENT, NULL, NULL),	/* 2nd Def */
	      GSF_XML_IN_NODE (EFFECT_STYLE, EFFECT_SCENE_3D, XL_NS_DRAW, "scene3d", GSF_XML_NO_CONTENT, NULL, NULL),
		GSF_XML_IN_NODE (EFFECT_SCENE_3D, 3D_CAMERA, XL_NS_DRAW, "camera", GSF_XML_NO_CONTENT, NULL, NULL),
		  GSF_XML_IN_NODE (3D_CAMERA, 3D_ROT, XL_NS_DRAW, "rot", GSF_XML_NO_CONTENT, NULL, NULL),
		GSF_XML_IN_NODE (EFFECT_SCENE_3D, 3D_LIGHT, XL_NS_DRAW, "lightRig", GSF_XML_NO_CONTENT, NULL, NULL),
		  GSF_XML_IN_NODE (3D_LIGHT, 3D_ROT, XL_NS_DRAW, "rot", GSF_XML_NO_CONTENT, NULL, NULL),

  GSF_XML_IN_NODE (THEME, OBJ_DEFAULTS, XL_NS_DRAW, "objectDefaults", GSF_XML_NO_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE (OBJ_DEFAULTS, SP_DEF, XL_NS_DRAW, "spDef", GSF_XML_NO_CONTENT, NULL, NULL),
      GSF_XML_IN_NODE (SP_DEF, SHAPE_PR,  XL_NS_DRAW, "spPr", GSF_XML_NO_CONTENT, NULL, NULL),
      GSF_XML_IN_NODE (SP_DEF, BODY_PR,   XL_NS_DRAW, "bodyPr", GSF_XML_NO_CONTENT, NULL, NULL),
      GSF_XML_IN_NODE (SP_DEF, LST_STYLE, XL_NS_DRAW, "lstStyle", GSF_XML_NO_CONTENT, NULL, NULL),
      GSF_XML_IN_NODE (SP_DEF, LN_DEF,	  XL_NS_DRAW, "lnDef", GSF_XML_NO_CONTENT, NULL, NULL),
  GSF_XML_IN_NODE (THEME, EXTRA_COLOR_SCHEME, XL_NS_DRAW, "extraClrSchemeLst", GSF_XML_NO_CONTENT, NULL, NULL),

  GSF_XML_IN_NODE_END
};

/****************************************************************************/

G_MODULE_EXPORT gboolean
xlsx_file_probe (GOFileOpener const *fo, GsfInput *input, FileProbeLevel pl);

gboolean
xlsx_file_probe (GOFileOpener const *fo, GsfInput *input, FileProbeLevel pl)
{
	GsfInfile *zip;
	GsfInput  *stream;
	gboolean   res = FALSE;

	if (NULL != (zip = gsf_infile_zip_new (input, NULL))) {
		if (NULL != (stream = gsf_infile_child_by_vname (zip, "xl", "workbook.xml", NULL))) {
			g_object_unref (G_OBJECT (stream));
			res = TRUE;
		}
		g_object_unref (G_OBJECT (zip));
	}
	return res;
}

static void
xlsx_style_array_free (GPtrArray *styles)
{
	if (styles != NULL) {
		unsigned i = styles->len;
		GnmStyle *style;
		while (i-- > 0)
			if (NULL != (style = g_ptr_array_index (styles, i)))
				gnm_style_unref (style);

		g_ptr_array_free (styles, TRUE);
	}
}

G_MODULE_EXPORT void
xlsx_file_open (GOFileOpener const *fo, IOContext *context,
		WorkbookView *wb_view, GsfInput *input);

void
xlsx_file_open (GOFileOpener const *fo, IOContext *context,
		WorkbookView *wb_view, GsfInput *input)
{
	XLSXReadState	 state;
	GnmLocale       *locale;

	memset (&state, 0, sizeof (XLSXReadState));
	state.context	= context;
	state.wb_view	= wb_view;
	state.wb	= wb_view_get_workbook (wb_view);
	state.sheet	= NULL;
	state.sst = g_array_new (FALSE, TRUE, sizeof (XLSXStr));
	state.shared_exprs = g_hash_table_new_full (g_str_hash, g_str_equal,
		(GDestroyNotify)g_free, (GDestroyNotify) gnm_expr_top_unref);
	state.cell_styles = g_hash_table_new_full (g_str_hash, g_str_equal,
		(GDestroyNotify)g_free, (GDestroyNotify) gnm_style_unref);
	state.num_fmts = g_hash_table_new_full (g_str_hash, g_str_equal,
		(GDestroyNotify)g_free, (GDestroyNotify) go_format_unref);
	state.convs = xlsx_conventions_new ();
	state.theme_colors = NULL;

	locale = gnm_push_C_locale ();

	if (NULL != (state.zip = gsf_infile_zip_new (input, NULL))) {
		/* optional */
		GsfInput *wb_part = gsf_open_pkg_get_rel_by_type (GSF_INPUT (state.zip),
			"http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument");

		if (NULL != wb_part) {
			GsfInput *in;

			in = gsf_open_pkg_get_rel_by_type (wb_part,
				"http://schemas.openxmlformats.org/officeDocument/2006/relationships/sharedStrings");
			xlsx_parse_stream (&state, in, xlsx_shared_strings_dtd);

			in = gsf_open_pkg_get_rel_by_type (wb_part,
				"http://schemas.openxmlformats.org/officeDocument/2006/relationships/styles");
			xlsx_parse_stream (&state, in, xlsx_styles_dtd);

			in = gsf_open_pkg_get_rel_by_type (wb_part,
				"http://schemas.openxmlformats.org/officeDocument/2006/relationships/theme");
			xlsx_parse_stream (&state, in, xlsx_theme_dtd);

			xlsx_parse_stream (&state, wb_part, xlsx_workbook_dtd);
		} else
			go_cmd_context_error_import (GO_CMD_CONTEXT (context),
				_("No workbook stream found."));
		g_object_unref (G_OBJECT (state.zip));
	}

	gnm_pop_C_locale (locale);

	if (NULL != state.sst) {
		unsigned i = state.sst->len;
		XLSXStr *entry;
		while (i-- > 0) {
			entry = &g_array_index (state.sst, XLSXStr, i);
			gnm_string_unref (entry->str);
			if (NULL != entry->markup)
				go_format_unref (entry->markup);
		}
		g_array_free (state.sst, TRUE);
	}
	xlsx_conventions_free (state.convs);
	g_hash_table_destroy (state.num_fmts);
	g_hash_table_destroy (state.cell_styles);
	g_hash_table_destroy (state.shared_exprs);
	xlsx_style_array_free (state.fonts);
	xlsx_style_array_free (state.fills);
	xlsx_style_array_free (state.borders);
	xlsx_style_array_free (state.xfs);
	xlsx_style_array_free (state.style_xfs);
	xlsx_style_array_free (state.dxfs);
	xlsx_style_array_free (state.table_styles);
	if (state.theme_colors)
		g_hash_table_destroy (state.theme_colors);

	workbook_set_saveinfo (state.wb, FILE_FL_AUTO,
		go_file_saver_for_id ("Gnumeric_Excel:xlsx"));
}

/* TODO * TODO * TODO
 *
 * IMPROVE
 *	- column widths : Don't use hard coded font size
 *	- share colours
 *	- conditional formats
 *		: why do we need to flip fg and bg for solid in xf but not for dxf
 *		: other condition types
 *		: check binary operators
 *
 * ".xlam",	"application/vnd.ms-excel.addin.macroEnabled.12" ,
 * ".xlsb",	"application/vnd.ms-excel.sheet.binary.macroEnabled.12" ,
 * ".xlsm",	"application/vnd.ms-excel.sheet.macroEnabled.12" ,
 * ".xlsx",	"application/vnd.openxmlformats-officedocument.spreadsheetml.sheet" ,
 * ".xltm",	"application/vnd.ms-excel.template.macroEnabled.12" ,
 * ".xltx",	"application/vnd.openxmlformats-officedocument.spreadsheetml.template"
**/
