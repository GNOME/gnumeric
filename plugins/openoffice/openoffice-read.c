/* vm: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/*
 * openoffice-read.c : import open/star calc files
 *
 * Copyright (C) 2002-2007 Jody Goldberg (jody@gnome.org)
 * Copyright (C) 2006 Luciano Miguel Wolf (luciano.wolf@indt.org.br)
 * Copyright (C) 2007 Morten Welinder (terra@gnome.org)
 * Copyright (C) 2006-2010 Andreas J. Guelzow (aguelzow@pyrshep.ca)
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

#include <gnm-plugin.h>
#include <workbook-view.h>
#include <workbook.h>
#include <sheet.h>
#include <sheet-merge.h>
#include <sheet-filter.h>
#include <ranges.h>
#include <cell.h>
#include <value.h>
#include <expr.h>
#include <expr-impl.h>
#include <expr-name.h>
#include <func.h>
#include <parse-util.h>
#include <style-color.h>
#include <sheet-style.h>
#include <mstyle.h>
#include <style-border.h>
#include <gnm-format.h>
#include <print-info.h>
#include <command-context.h>
#include <gutils.h>
#include <xml-sax.h>
#include <sheet-object-cell-comment.h>
#include "sheet-object-widget.h"
#include <style-conditions.h>
#include <gnumeric-gconf.h>
#include <mathfunc.h>
#include <sheet-object-graph.h>
#include <sheet-object-image.h>
#include <graph.h>
#include <gnm-so-filled.h>
#include <gnm-so-line.h>


#include <goffice/goffice.h>

#include <gsf/gsf-libxml.h>
#include <gsf/gsf-input.h>
#include <gsf/gsf-infile.h>
#include <gsf/gsf-infile-zip.h>
#include <gsf/gsf-opendoc-utils.h>
#include <gsf/gsf-doc-meta-data.h>
#include <gsf/gsf-output-stdio.h>
#include <gsf/gsf-utils.h>
#include <glib/gi18n-lib.h>

#include <string.h>
#include <errno.h>

GNM_PLUGIN_MODULE_HEADER;

#undef OO_DEBUG_OBJS

#define CXML2C(s) ((char const *)(s))
#define CC2XML(s) ((xmlChar const *)(s))

static inline gboolean
attr_eq (xmlChar const *a, char const *s)
{
	return !strcmp (CXML2C (a), s);
}

enum {
	OO_SYMBOL_TYPE_AUTO = 1,
	OO_SYMBOL_TYPE_NONE = 2,
	OO_SYMBOL_TYPE_NAMED = 3
};

enum {
	OO_CHART_STYLE_PLOTAREA = 0,
	OO_CHART_STYLE_SERIES = 1,
	OO_CHART_STYLE_INHERITANCE = 2
};

enum {
	OO_FILL_TYPE_UNKNOWN = 0,
	OO_FILL_TYPE_SOLID,
	OO_FILL_TYPE_HATCH,
	OO_FILL_TYPE_GRADIENT,
	OO_FILL_TYPE_BITMAP,
	OO_FILL_TYPE_NONE
};

/* Filter Type */
typedef enum {
	OOO_VER_UNKNOWN	= -1,
	OOO_VER_1	=  0,
	OOO_VER_OPENDOC	=  1
} OOVer;
static struct {
	char const * const mime_type;
	int version;
} const OOVersions[] = {
	{ "application/vnd.sun.xml.calc",  OOO_VER_1 },
	{ "application/vnd.oasis.opendocument.spreadsheet",		OOO_VER_OPENDOC },
	{ "application/vnd.oasis.opendocument.spreadsheet-template",	OOO_VER_OPENDOC }
};

/* Formula Type */
typedef enum {
	FORMULA_OPENFORMULA = 0,
	FORMULA_OLD_OPENOFFICE,
	FORMULA_MICROSOFT,
	NUM_FORMULAE_SUPPORTED,
	FORMULA_NOT_SUPPORTED
} OOFormula;

#define OD_BORDER_THIN		1
#define OD_BORDER_MEDIUM	2.5
#define OD_BORDER_THICK		5



typedef enum {
	OO_PLOT_AREA,
	OO_PLOT_BAR,
	OO_PLOT_CIRCLE,
	OO_PLOT_LINE,
	OO_PLOT_RADAR,
	OO_PLOT_RADARAREA,
	OO_PLOT_RING,
	OO_PLOT_SCATTER,
	OO_PLOT_STOCK,
	OO_PLOT_CONTOUR,
	OO_PLOT_BUBBLE,
	OO_PLOT_GANTT,
	OO_PLOT_POLAR,
	OO_PLOT_SCATTER_COLOUR,
	OO_PLOT_XYZ_SURFACE,
	OO_PLOT_SURFACE,
	OO_PLOT_XL_SURFACE,
	OO_PLOT_BOX,
	OO_PLOT_UNKNOWN
} OOPlotType;

typedef enum {
	OO_STYLE_UNKNOWN,
	OO_STYLE_CELL,
	OO_STYLE_COL,
	OO_STYLE_ROW,
	OO_STYLE_SHEET,
	OO_STYLE_GRAPHICS,
	OO_STYLE_CHART,
	OO_STYLE_PARAGRAPH,
	OO_STYLE_TEXT
} OOStyleType;

typedef struct {
	GValue value;
	gchar const *name;
} OOProp;

typedef struct {
	GType t;
	gboolean horizontal;
	int min;
	int max;
	int step;
	int page_step;
	char *value;
	char *value_type;
	char *linked_cell;
	char *label;
	char *implementation;
	char *source_cell_range;
	gboolean as_index;
} OOControl;

typedef struct {
	char *view_box;
	char * d;
	GOArrow *arrow;
} OOMarker;

typedef struct {
	gboolean grid;		/* graph has grid? */
	gboolean src_in_rows;	/* orientation of graph data: rows or columns */
	GSList	*axis_props;	/* axis properties */
	GSList	*plot_props;	/* plot properties */
	GSList	*style_props;	/* any other properties */
	GSList	*other_props;	/* any other properties */
} OOChartStyle;

typedef struct {
	GogGraph	*graph;
	GogChart	*chart;
	SheetObject     *so;
	GSList          *list; /* used by Stock plot and textbox*/

	/* set in plot-area */
	GogPlot		*plot;
	Sheet		*src_sheet;
	GnmRange	 src_range;
	gboolean	 src_in_rows;
	int		 src_n_vectors;
	GnmRange	 src_abscissa;
	gboolean         src_abscissa_set;
	GnmRange	 src_label;
	gboolean         src_label_set;

	GogSeries	*series;
	unsigned	 series_count;	/* reset for each plotarea */
	unsigned	 domain_count;	/* reset for each series */
	unsigned	 data_pt_count;	/* reset for each series */

	GogObject	*axis;
	xmlChar         *cat_expr;
	GogObject	*regression;
	GogObject	*legend;

	GnmExprTop const        *title_expr;
	gchar                   *title_style;

	OOChartStyle		*cur_graph_style; /* for reading of styles */

	GSList		        *saved_graph_styles;
	GSList		        *saved_hatches;
	GSList		        *saved_dash_styles;
	GSList		        *saved_fill_image_styles;
	GSList		        *saved_gradient_styles;

	GHashTable		*graph_styles;
	GHashTable              *hatches;
	GHashTable              *dash_styles;
	GHashTable              *fill_image_styles;
	GHashTable              *gradient_styles;
	GHashTable              *arrow_markers;

	OOChartStyle            *i_plot_styles[OO_CHART_STYLE_INHERITANCE];
	                                          /* currently active styles at plot-area, */
	                                                /* series level*/
	OOPlotType		 plot_type;
	SheetObjectAnchor	 anchor;	/* anchor to draw the frame (images or graphs) */
} OOChartInfo;

typedef enum {
	OO_PAGE_BREAK_NONE,
	OO_PAGE_BREAK_AUTO,
	OO_PAGE_BREAK_MANUAL
} OOPageBreakType;
typedef struct {
	gnm_float	 size_pts;
	int	 count;
	gboolean manual;
	OOPageBreakType break_before, break_after;
} OOColRowStyle;
typedef struct {
	GnmSheetVisibility visibility;
	gboolean is_rtl;
	gboolean tab_color_set;
	GOColor tab_color;
	gboolean tab_text_color_set;
	GOColor tab_text_color;
} OOSheetStyle;

typedef struct {
	GHashTable *settings;
	GSList *stack;
	GType type;
	char *config_item_name;
} OOSettings;

typedef enum {
	ODF_ELAPSED_SET_SECONDS = 1 << 0,
	ODF_ELAPSED_SET_MINUTES = 1 << 1,
	ODF_ELAPSED_SET_HOURS   = 1 << 2
} odf_elapsed_set_t;

typedef struct {
	GOIOContext	*context;	/* The IOcontext managing things */
	WorkbookView	*wb_view;	/* View for the new workbook */
	OOVer		 ver;		/* Its an OOo v1.0 or v2.0? */
	gnm_float	 ver_odf;	/* specific ODF version */
	GsfInfile	*zip;		/* Reference to the open file, to load graphs and images*/
	OOChartInfo	 chart;
	GnmParsePos	 pos;
	GnmCellPos	 extent_data;
	GnmCellPos	 extent_style;
	GnmComment      *cell_comment;

	int		 col_inc, row_inc;
	gboolean	 content_is_simple;
	gboolean	 content_is_error;

	GHashTable	*formats;
	GHashTable	*controls;
	GHashTable	*validations;

	struct {
		GHashTable	*cell;
		GHashTable	*cell_datetime;
		GHashTable	*cell_date;
		GHashTable	*cell_time;
		GHashTable	*col;
		GHashTable	*row;
		GHashTable	*sheet;
	} styles;
	struct {
		GnmStyle	*cells;
		OOColRowStyle	*col_rows;
		OOSheetStyle	*sheets;
		gboolean         requires_disposal;
		OOStyleType      type;
	} cur_style;

	gboolean	 h_align_is_valid, repeat_content;
	int              text_align, gnm_halign;

	struct {
		GnmStyle	*cells;
		OOColRowStyle	*rows;
		OOColRowStyle	*columns;
	} default_style;
	GSList		*sheet_order;
	int		 richtext_len;
	struct {
		GString	*accum;
		char	*name;
		int      magic;
		gboolean truncate_hour_on_overflow;
		int      elapsed_set; /* using a sum of odf_elapsed_set_t */
		guint      pos_seconds;
		guint      pos_minutes;
		gboolean percentage;
		gboolean percent_sign_seen;
	} cur_format;
	GSList          *conditions;
	GSList          *cond_formats;
	GnmFilter	*filter;

	GnmConventions  *convs[NUM_FORMULAE_SUPPORTED];
	struct {
		GnmPageBreaks *h, *v;
	} page_breaks;

	char const *object_name;
	OOControl *cur_control;

	OOSettings settings;

	gsf_off_t last_progress_update;
	char *last_error;
	gboolean  debug;
} OOParseState;

typedef struct {
	GnmConventions base;
	OOParseState *state;
	GsfXMLIn *xin;
} ODFConventions;

typedef struct {
	GOColor from;
	GOColor to;
	gnm_float brightness;
	unsigned int dir;
} gradient_info_t;


/* Some  prototypes */
static GsfXMLInNode const * get_dtd (void);
static GsfXMLInNode const * get_styles_dtd (void);
static void oo_chart_style_free (OOChartStyle *pointer);
static OOFormula odf_get_formula_type (GsfXMLIn *xin, char const **str);
static char const *odf_strunescape (char const *string, GString *target,
				    G_GNUC_UNUSED GnmConventions const *convs);


/* Implementations */
static void
odf_go_string_append_c_n (GString *target, char c, int n)
{
	if (n > 0)
		go_string_append_c_n (target, c, (gsize) n);
}

static void
maybe_update_progress (GsfXMLIn *xin)
{
	OOParseState *state = (OOParseState *)xin->user_state;
	GsfInput *input = gsf_xml_in_get_input (xin);
	gsf_off_t pos = gsf_input_tell (input);

	if (pos >= state->last_progress_update + 10000) {
		go_io_value_progress_update (state->context, pos);
		state->last_progress_update = pos;
	}
}

static GOErrorInfo *oo_go_error_info_new_vprintf (GOSeverity severity,
					  char const *msg_format, ...)
	G_GNUC_PRINTF (2, 3);

static GOErrorInfo *
oo_go_error_info_new_vprintf (GOSeverity severity,
			      char const *msg_format, ...)
{
	va_list args;
	GOErrorInfo *ei;

	va_start (args, msg_format);
	ei = go_error_info_new_vprintf (severity, msg_format, args);
	va_end (args);

	return ei;
}

static gboolean oo_warning (GsfXMLIn *xin, char const *fmt, ...)
	G_GNUC_PRINTF (2, 3);

static gboolean
oo_warning (GsfXMLIn *xin, char const *fmt, ...)
{
	OOParseState *state = (OOParseState *)xin->user_state;
	char *msg;
	char *detail;
	va_list args;

	va_start (args, fmt);
	detail = g_strdup_vprintf (fmt, args);
	va_end (args);

	if (IS_SHEET (state->pos.sheet)) {
		if (state->pos.eval.col >= 0 && state->pos.eval.row >= 0)
			msg = g_strdup_printf ("%s!%s",
					       state->pos.sheet->name_quoted,
					       cellpos_as_string (&state->pos.eval));
		else
			msg = g_strdup(state->pos.sheet->name_quoted);
	} else
		msg = g_strdup (_("General ODF error"));

	if (0 != go_str_compare (msg, state->last_error)) {
		GOErrorInfo *ei = oo_go_error_info_new_vprintf
			(GO_WARNING, "%s", msg);

		go_io_error_info_set (state->context, ei);
		g_free (state->last_error);
		state->last_error = msg;
	} else
		g_free (msg);

	go_error_info_add_details
		(state->context->info->data,
		 oo_go_error_info_new_vprintf (GO_WARNING, "%s", detail));

	g_free (detail);

	return FALSE; /* convenience */
}

static gboolean
oo_attr_bool (GsfXMLIn *xin, xmlChar const * const *attrs,
	      int ns_id, char const *name, gboolean *res)
{
	g_return_val_if_fail (attrs != NULL, FALSE);
	g_return_val_if_fail (attrs[0] != NULL, FALSE);
	g_return_val_if_fail (attrs[1] != NULL, FALSE);

	if (!gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), ns_id, name))
		return FALSE;
	*res = (g_ascii_strcasecmp (CXML2C (attrs[1]), "false") &&
		strcmp (CXML2C (attrs[1]), "0"));

	return TRUE;
}

static gboolean
oo_attr_int (GsfXMLIn *xin, xmlChar const * const *attrs,
	     int ns_id, char const *name, int *res)
{
	char *end;
	long tmp;

	g_return_val_if_fail (attrs != NULL, FALSE);
	g_return_val_if_fail (attrs[0] != NULL, FALSE);
	g_return_val_if_fail (attrs[1] != NULL, FALSE);

	if (!gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), ns_id, name))
		return FALSE;

	errno = 0; /* strtol sets errno, but does not clear it.  */
	tmp = strtol (CXML2C (attrs[1]), &end, 10);
	if (*end || errno != 0 || tmp < INT_MIN || tmp > INT_MAX)
		return oo_warning (xin, _("Invalid integer '%s', for '%s'"),
				   attrs[1], name);

	*res = tmp;
	return TRUE;
}

static gboolean
oo_attr_int_range (GsfXMLIn *xin, xmlChar const * const *attrs,
		     int ns_id, char const *name, int *res, int min, int max)
{
	int tmp;
	if (!oo_attr_int (xin, attrs, ns_id, name, &tmp))
		return FALSE;
	if (tmp < min || tmp > max) {
		oo_warning (xin, _("Possible corrupted integer '%s' for '%s'"),
				   attrs[1], name);
		*res = (tmp < min) ? min :  max;
		return TRUE;
	}
	*res = tmp;
	return TRUE;
}


static gboolean
oo_attr_font_weight (GsfXMLIn *xin, xmlChar const * const *attrs,
		     int *res)
{
	if (!gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_FO, "font-weight"))
		return FALSE;
	if (attr_eq (attrs[1], "bold")) {
		*res = PANGO_WEIGHT_BOLD;
		return TRUE;
	} else if (attr_eq (attrs[1], "normal")) {
		*res = PANGO_WEIGHT_NORMAL;
		return TRUE;
	}
	return oo_attr_int_range (xin, attrs, OO_NS_FO, "font-weight",
				    res, 0, 1000);
}


static gboolean
oo_attr_float (GsfXMLIn *xin, xmlChar const * const *attrs,
	       int ns_id, char const *name, gnm_float *res)
{
	char *end;
	double tmp;

	g_return_val_if_fail (attrs != NULL, FALSE);
	g_return_val_if_fail (attrs[0] != NULL, FALSE);
	g_return_val_if_fail (attrs[1] != NULL, FALSE);

	if (!gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), ns_id, name))
		return FALSE;

	tmp = gnm_strto (CXML2C (attrs[1]), &end);
	if (*end)
		return oo_warning (xin, _("Invalid attribute '%s', expected number, received '%s'"),
				   name, attrs[1]);
	*res = tmp;
	return TRUE;
}

static gboolean
oo_attr_percent (GsfXMLIn *xin, xmlChar const * const *attrs,
	       int ns_id, char const *name, gnm_float *res)
{
	char *end;
	double tmp;

	g_return_val_if_fail (attrs != NULL, FALSE);
	g_return_val_if_fail (attrs[0] != NULL, FALSE);
	g_return_val_if_fail (attrs[1] != NULL, FALSE);

	if (!gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), ns_id, name))
		return FALSE;

	tmp = gnm_strto (CXML2C (attrs[1]), &end);
	if (*end != '%' || *(end + 1))
		return oo_warning (xin,
				   _("Invalid attribute '%s', expected percentage,"
				     " received '%s'"),
				   name, attrs[1]);
	*res = tmp/100.;
	return TRUE;
}
static GnmColor *magic_transparent;

static GnmColor *
oo_parse_color (GsfXMLIn *xin, xmlChar const *str, char const *name)
{
	guint r, g, b;

	g_return_val_if_fail (str != NULL, NULL);

	if (3 == sscanf (CXML2C (str), "#%2x%2x%2x", &r, &g, &b))
		return style_color_new_i8 (r, g, b);

	if (0 == strcmp (CXML2C (str), "transparent"))
		return style_color_ref (magic_transparent);

	oo_warning (xin, _("Invalid attribute '%s', expected color, received '%s'"),
		    name, str);
	return NULL;
}
static GnmColor *
oo_attr_color (GsfXMLIn *xin, xmlChar const * const *attrs,
	       int ns_id, char const *name)
{
	g_return_val_if_fail (attrs != NULL, NULL);
	g_return_val_if_fail (attrs[0] != NULL, NULL);

	if (!gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), ns_id, name))
		return NULL;
	return oo_parse_color (xin, attrs[1], name);
}

static GOLineDashType
odf_match_dash_type (OOParseState *state, gchar const *dash_style)
{
	GOLineDashType t = go_line_dash_from_str (dash_style);
	if (t == GO_LINE_NONE) {
		gpointer res = g_hash_table_lookup
			(state->chart.dash_styles, dash_style);
		if (res != NULL)
			t = GPOINTER_TO_UINT(res);
	}
	return ((t == GO_LINE_NONE)? GO_LINE_DOT : t );
}

static void
odf_apply_style_props (GsfXMLIn *xin, GSList *props, GOStyle *style)
{
	OOParseState *state = (OOParseState *)xin->user_state;
	PangoFontDescription *desc;
	GSList *l;
	gboolean desc_changed = FALSE;
	char const *hatch_name = NULL;
	char const *gradient_name = NULL;
	char const *fill_image_name = NULL;
	unsigned int gnm_hatch = 0;
	int symbol_type = -1, symbol_name = GO_MARKER_DIAMOND;
	GOMarker *m;
	gboolean line_is_not_dash = FALSE;
	unsigned int fill_type = OO_FILL_TYPE_UNKNOWN;
	gboolean stroke_colour_set = FALSE;

	desc = pango_font_description_copy (style->font.font->desc);
	for (l = props; l != NULL; l = l->next) {
		OOProp *prop = l->data;
		if (0 == strcmp (prop->name, "fill")) {
			char const *val_string = g_value_get_string (&prop->value);
			if (0 == strcmp (val_string, "solid")) {
				style->fill.type = GO_STYLE_FILL_PATTERN;
				style->fill.auto_type = FALSE;
				style->fill.pattern.pattern = GO_PATTERN_SOLID;
				fill_type = OO_FILL_TYPE_SOLID;
			} else if (0 == strcmp (val_string, "hatch")) {
				style->fill.type = GO_STYLE_FILL_PATTERN;
				style->fill.auto_type = FALSE;
				fill_type = OO_FILL_TYPE_HATCH;
			} else if (0 == strcmp (val_string, "gradient")) {
				style->fill.type = GO_STYLE_FILL_GRADIENT;
				style->fill.auto_type = FALSE;
				fill_type = OO_FILL_TYPE_GRADIENT;
			} else if (0 == strcmp (val_string, "bitmap")) {
				style->fill.type = GO_STYLE_FILL_IMAGE;
				style->fill.auto_type = FALSE;
				fill_type = OO_FILL_TYPE_BITMAP;
			} else { /* "none" */
				style->fill.type = GO_STYLE_FILL_NONE;
				style->fill.auto_type = FALSE;
				fill_type = OO_FILL_TYPE_NONE;
			}
		} else if (0 == strcmp (prop->name, "fill-color")) {
			GdkColor gdk_color;
			gchar const *color = g_value_get_string (&prop->value);
			if (gdk_color_parse (color, &gdk_color)) {
				style->fill.pattern.back = GO_COLOR_FROM_GDK (gdk_color);
				style->fill.auto_back = FALSE;
			}
		} else if (0 == strcmp (prop->name, "stroke-color")) {
			GdkColor gdk_color;
			gchar const *color = g_value_get_string (&prop->value);
			if (gdk_color_parse (color, &gdk_color)) {
				style->line.color = GO_COLOR_FROM_GDK (gdk_color);
				style->line.fore = GO_COLOR_FROM_GDK (gdk_color);
				style->line.auto_color = FALSE;
				style->line.auto_fore = FALSE;
				style->line.pattern = GO_PATTERN_SOLID;
				stroke_colour_set = TRUE;
			}
		} else if (0 == strcmp (prop->name, "lines") && !stroke_colour_set) {
			style->line.auto_color = g_value_get_boolean (&prop->value);
 		} else if (0 == strcmp (prop->name, "fill-gradient-name"))
			gradient_name = g_value_get_string (&prop->value);
		else if (0 == strcmp (prop->name, "fill-hatch-name"))
			hatch_name = g_value_get_string (&prop->value);
		else if (0 == strcmp (prop->name, "fill-image-name"))
			fill_image_name = g_value_get_string (&prop->value);
		else if (0 == strcmp (prop->name, "gnm-pattern"))
			gnm_hatch = g_value_get_int (&prop->value);
		else if (0 == strcmp (prop->name, "text-rotation-angle")) {
			int angle = g_value_get_int (&prop->value);
			go_style_set_text_angle (style, angle);
		} else if (0 == strcmp (prop->name, "font-size")) {
			pango_font_description_set_size
				(desc, PANGO_SCALE * g_value_get_double
				 (&prop->value));
			desc_changed = TRUE;
		} else if (0 == strcmp (prop->name, "font-weight")) {
			pango_font_description_set_weight
				(desc, g_value_get_int (&prop->value));
			desc_changed = TRUE;
		} else if (0 == strcmp (prop->name, "font-variant")) {
			pango_font_description_set_variant
				(desc, g_value_get_int (&prop->value));
			desc_changed = TRUE;
		} else if (0 == strcmp (prop->name, "font-style")) {
			pango_font_description_set_style
				(desc, g_value_get_int (&prop->value));
			desc_changed = TRUE;
		} else if (0 == strcmp (prop->name, "font-stretch-pango")) {
			pango_font_description_set_stretch
				(desc, g_value_get_int (&prop->value));
			desc_changed = TRUE;
		} else if (0 == strcmp (prop->name, "font-gravity-pango")) {
			pango_font_description_set_gravity
				(desc, g_value_get_int (&prop->value));
			desc_changed = TRUE;
		} else if (0 == strcmp (prop->name, "font-family")) {
			pango_font_description_set_family
				(desc, g_value_get_string (&prop->value));
			desc_changed = TRUE;
		} else if (0 == strcmp (prop->name, "stroke")) {
			if (0 == strcmp (g_value_get_string (&prop->value), "solid")) {
				style->line.dash_type = GO_LINE_SOLID;
				style->line.auto_dash = FALSE;
				line_is_not_dash = TRUE;
			} else if (0 == strcmp (g_value_get_string (&prop->value), "dash")) {
				style->line.auto_dash = FALSE;
				line_is_not_dash = FALSE;
			} else {
				style->line.dash_type = GO_LINE_NONE;
				style->line.auto_dash = FALSE;
				line_is_not_dash = TRUE;
			}
		} else if (0 == strcmp (prop->name, "stroke-dash") && !line_is_not_dash) {
			style->line.dash_type = odf_match_dash_type
				(state, g_value_get_string (&prop->value));
		} else if (0 == strcmp (prop->name, "symbol-type"))
			symbol_type = g_value_get_int (&prop->value);
		else if (0 == strcmp (prop->name, "symbol-name"))
			symbol_name = g_value_get_int (&prop->value);
		else if (0 == strcmp (prop->name, "stroke-width"))
		        style->line.width = g_value_get_double (&prop->value);
		else if (0 == strcmp (prop->name, "repeat"))
			style->fill.image.type = g_value_get_int (&prop->value);

	}
	if (desc_changed)
		go_style_set_font_desc	(style, desc);
	else
		pango_font_description_free (desc);


	switch (fill_type) {
	case OO_FILL_TYPE_HATCH:
		if (hatch_name != NULL) {
			GOPattern *pat = g_hash_table_lookup
				(state->chart.hatches, hatch_name);
			if (pat == NULL)
				oo_warning (xin, _("Unknown hatch name \'%s\'"
						   " encountered!"), hatch_name);
			else {
				style->fill.pattern.fore = pat->fore;
				style->fill.auto_fore = FALSE;
				style->fill.pattern.pattern =  (gnm_hatch > 0) ?
					gnm_hatch : pat->pattern;
			}
		} else oo_warning (xin, _("Hatch fill without hatch name "
					  "encountered!"));
		break;
	case OO_FILL_TYPE_GRADIENT:
		if (gradient_name != NULL) {
			gradient_info_t *info =  g_hash_table_lookup
				(state->chart.gradient_styles, gradient_name);
			if (info == NULL)
				oo_warning (xin, _("Unknown gradient name \'%s\'"
						   " encountered!"), gradient_name);
			else {
				style->fill.auto_fore = FALSE;
				style->fill.auto_back = FALSE;
				style->fill.pattern.back = info->from;
				style->fill.pattern.fore = info->to;
				style->fill.gradient.dir = info->dir;
				style->fill.gradient.brightness = -1.0;
				if (info->brightness >= 0)
					go_style_set_fill_brightness
						(style, info->brightness);
			}
		} else oo_warning (xin, _("Gradient fill without gradient "
					  "name encountered!"));
		break;
	case OO_FILL_TYPE_BITMAP:
		if (fill_image_name != NULL) {
			char const *href = g_hash_table_lookup
				(state->chart.fill_image_styles, fill_image_name);
			if (href == NULL)
				oo_warning (xin, _("Unknown image fill name \'%s\'"
						   " encountered!"), fill_image_name);
			else {
				GsfInput *input;
				char *href_complete;
				char **path;

				if (strncmp (href, "./", 2) == 0)
					href += 2;
				if (strncmp (href, "/", 1) == 0) {
					oo_warning (xin, _("Invalid absolute file "
							   "specification \'%s\' "
							   "encountered."), href);
					break;
				}

				href_complete = g_strconcat (state->object_name,
							     "/", href, NULL);
				path = g_strsplit (href_complete, "/", -1);
				input = gsf_infile_child_by_aname
					(state->zip, (const char **) path);
				g_strfreev (path);
				if (input == NULL)
					oo_warning (xin, _("Unable to open \'%s\'."),
						    href_complete);
				else {
					gsf_off_t len = gsf_input_size (input);
					guint8 const *data = gsf_input_read
						(input, len, NULL);
					GdkPixbufLoader *loader
						= gdk_pixbuf_loader_new ();
					GdkPixbuf *pixbuf = NULL;

					if (gdk_pixbuf_loader_write (loader,
								     (guchar *)data,
								     (gsize)len,
								     NULL)) {
						gdk_pixbuf_loader_close (loader, NULL);
						pixbuf = gdk_pixbuf_loader_get_pixbuf (loader);
						g_object_ref (G_OBJECT (pixbuf));
						if (style->fill.image.image != NULL)
							g_object_set
								(G_OBJECT (style->fill.image.image),
								 "pixbuf", pixbuf, NULL);
						else
							style->fill.image.image =
								go_image_new_from_pixbuf (pixbuf);
						go_image_set_name (style->fill.image.image,
								   fill_image_name);
						g_object_unref (G_OBJECT (loader));
					} else {
						oo_warning (xin, _("Unable to load "
								   "the file \'%s\'."),
							    href_complete);
					}
					g_object_unref (input);
				}
				g_free (href_complete);
			}
		} else oo_warning (xin, _("Image fill without image "
					  "name encountered!"));
		break;
	default:
		break;
	}

	switch (symbol_type) {
	case OO_SYMBOL_TYPE_AUTO:
		style->marker.auto_shape = TRUE;
		break;
	case OO_SYMBOL_TYPE_NONE:
		style->marker.auto_shape = FALSE;
		m = go_marker_new ();
		go_marker_set_shape (m, GO_MARKER_NONE);
		go_style_set_marker (style, m);
		break;
	case OO_SYMBOL_TYPE_NAMED:
		style->marker.auto_shape = FALSE;
		m = go_marker_new ();
		go_marker_set_shape (m, symbol_name);
		go_style_set_marker (style, m);
		break;
	default:
		break;
	}
}


/* returns pts */
static char const *
oo_parse_distance (GsfXMLIn *xin, xmlChar const *str,
		  char const *name, gnm_float *pts)
{
	double num;
	char *end = NULL;

	g_return_val_if_fail (str != NULL, NULL);

	if (0 == strncmp (CXML2C (str), "none", 4)) {
		*pts = 0;
		return CXML2C (str) + 4;
	}

	num = go_strtod (CXML2C (str), &end);
	if (CXML2C (str) != end) {
		if (0 == strncmp (end, "mm", 2)) {
			num = GO_CM_TO_PT (num/10.);
			end += 2;
		} else if (0 == strncmp (end, "m", 1)) {
			num = GO_CM_TO_PT (num*100.);
			end ++;
		} else if (0 == strncmp (end, "km", 2)) {
			num = GO_CM_TO_PT (num*100000.);
			end += 2;
		} else if (0 == strncmp (end, "cm", 2)) {
			num = GO_CM_TO_PT (num);
			end += 2;
		} else if (0 == strncmp (end, "pt", 2)) {
			end += 2;
		} else if (0 == strncmp (end, "pc", 2)) { /* pica 12pt == 1 pica */
			num /= 12.;
			end += 2;
		} else if (0 == strncmp (end, "ft", 2)) {
			num = GO_IN_TO_PT (num*12.);
			end += 2;
		} else if (0 == strncmp (end, "mi", 2)) {
			num = GO_IN_TO_PT (num*63360.);
			end += 2;
		} else if (0 == strncmp (end, "inch", 4)) {
			num = GO_IN_TO_PT (num);
			end += 4;
		} else if (0 == strncmp (end, "in", 2)) {
			num = GO_IN_TO_PT (num);
			end += 2;
		} else {
			oo_warning (xin, _("Invalid attribute '%s', unknown unit '%s'"),
				    name, str);
			return NULL;
		}
	} else {
		oo_warning (xin, _("Invalid attribute '%s', expected distance, received '%s'"),
			    name, str);
		return NULL;
	}

	*pts = num;
	return end;
}

/* returns pts */
static char const *
oo_attr_distance (GsfXMLIn *xin, xmlChar const * const *attrs,
		  int ns_id, char const *name, gnm_float *pts)
{
	g_return_val_if_fail (attrs != NULL, NULL);
	g_return_val_if_fail (attrs[0] != NULL, NULL);
	g_return_val_if_fail (attrs[1] != NULL, NULL);

	if (!gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), ns_id, name))
		return NULL;
	return oo_parse_distance (xin, attrs[1], name, pts);
}

/* returns pts */
static char const *
oo_parse_angle (GsfXMLIn *xin, xmlChar const *str,
		  char const *name, int *angle)
{
	double num;
	char *end = NULL;

	g_return_val_if_fail (str != NULL, NULL);

	num = go_strtod (CXML2C (str), &end);
	if (CXML2C (str) != end) {
		if (*end != '\0') {
			if (0 == strncmp (end, "deg", 3)) {
				end += 3;
			} else if (0 == strncmp (end, "grad", 4)) {
				num = num / 9. * 10.;
				end += 4;
			} else if (0 == strncmp (end, "rad", 2)) {
				num = num * 180. / M_PIgnum;
				end += 3;
			} else {
				oo_warning (xin, _("Invalid attribute '%s', unknown unit '%s'"),
					    name, str);
				return NULL;
			}
		}
	} else {
		oo_warning (xin, _("Invalid attribute '%s', expected angle, received '%s'"),
			    name, str);
		return NULL;
	}

	*angle = ((int) num) % 360;
	return end;
}

/* returns degree */
static char const *
oo_attr_angle (GsfXMLIn *xin, xmlChar const * const *attrs,
		  int ns_id, char const *name, int *deg)
{
	g_return_val_if_fail (attrs != NULL, NULL);
	g_return_val_if_fail (attrs[0] != NULL, NULL);
	g_return_val_if_fail (attrs[1] != NULL, NULL);

	if (!gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), ns_id, name))
		return NULL;
	return oo_parse_angle (xin, attrs[1], name, deg);
}

typedef struct {
	char const * const name;
	int val;
} OOEnum;

static gboolean
oo_attr_enum (GsfXMLIn *xin, xmlChar const * const *attrs,
	      int ns_id, char const *name, OOEnum const *enums, int *res)
{
	g_return_val_if_fail (attrs != NULL, FALSE);
	g_return_val_if_fail (attrs[0] != NULL, FALSE);
	g_return_val_if_fail (attrs[1] != NULL, FALSE);

	if (!gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), ns_id, name))
		return FALSE;

	for (; enums->name != NULL ; enums++)
		if (!strcmp (enums->name, CXML2C (attrs[1]))) {
			*res = enums->val;
			return TRUE;
		}
	return oo_warning (xin, _("Invalid attribute '%s', unknown enum value '%s'"),
			   name, attrs[1]);
}

static gboolean
oo_cellref_check_for_err (GnmCellRef *ref, char const **start)
{
	if (g_str_has_prefix (*start, "$#REF!")) {
		ref->sheet = invalid_sheet;
		*start += 6;
		return TRUE;
	}
	if (g_str_has_prefix (*start, "#REF!")) {
		ref->sheet = invalid_sheet;
		*start += 5;
		return TRUE;
	}
	return FALSE;
}

static char const *
oo_cellref_parse (GnmCellRef *ref, char const *start, GnmParsePos const *pp,
		  gchar **foreign_sheet)
{
	char const *tmp, *ptr = start;
	GnmSheetSize const *ss;
	GnmSheetSize ss_max = { GNM_MAX_COLS, GNM_MAX_ROWS};
	Sheet *sheet;
	char *new_sheet_name = NULL;

	if (*ptr != '.') {
		char *name, *accum;

		/* ignore abs vs rel for sheets */
		if (*ptr == '$')
			ptr++;

		/* From the spec :
		 *	SheetName   ::= [^\. ']+ | "'" ([^'] | "''")+ "'" */
		if ('\'' == *ptr) {
			tmp = ++ptr;
two_quotes :
			/* missing close paren */
			if (NULL == (tmp = strchr (tmp, '\'')))
				return start;

			/* two in a row is the escape for a single */
			if (tmp[1] == '\'') {
				tmp += 2;
				goto two_quotes;
			}

			/* If a name is quoted the entire named must be quoted */
			if (tmp[1] != '.')
				return start;

			accum = name = g_alloca (tmp-ptr+1);
			while (ptr != tmp)
				if ('\'' == (*accum++ = *ptr++))
					ptr++;
			*accum = '\0';
			ptr += 2;
		} else {
			if (NULL == (tmp = strchr (ptr, '.')))
				return start;
			name = g_alloca (tmp-ptr+1);
			strncpy (name, ptr, tmp-ptr);
			name[tmp-ptr] = '\0';
			ptr = tmp + 1;
		}

		if (name[0] == 0)
			return start;

		if (foreign_sheet != NULL) {
			/* This is a reference to a foreign workbook */
			*foreign_sheet = g_strdup (name);
			ref->sheet = NULL;
		} else {
			/* OpenCalc does not pre-declare its sheets, but it does have a
			 * nice unambiguous format.  So if we find a name that has not
			 * been added yet add it.  Reorder below. */
			ref->sheet = workbook_sheet_by_name (pp->wb, name);
			if (ref->sheet == NULL) {
				if (strcmp (name, "#REF!") == 0) {
					ref->sheet = invalid_sheet;
				} else {
					/* We can't add it yet since this whole ref */
					/* may be invalid */
					new_sheet_name = g_strdup (name);
					ref->sheet = NULL;
				}
			}
		}
	} else {
		ptr++; /* local ref */
		ref->sheet = NULL;
	}

	tmp = col_parse (ptr, &ss_max, &ref->col, &ref->col_relative);
	if (!tmp && !oo_cellref_check_for_err (ref, &ptr))
		return start;
	if (tmp) 
		ptr = tmp;
	tmp = row_parse (ptr, &ss_max, &ref->row, &ref->row_relative);
	if (!tmp && !oo_cellref_check_for_err (ref, &ptr))
		return start;
	if (tmp)
		ptr = tmp;

	if (ref->sheet == invalid_sheet) {
		g_free (new_sheet_name);
		return ptr;
	}
	
	if (new_sheet_name != NULL) {
		Sheet *old_sheet = workbook_sheet_by_index (pp->wb, 0);
		ref->sheet = sheet_new (pp->wb, new_sheet_name,
					gnm_sheet_get_max_cols (old_sheet),
					gnm_sheet_get_max_rows (old_sheet));
		workbook_sheet_attach (pp->wb, ref->sheet);
		g_free (new_sheet_name);
	}

	sheet = eval_sheet (ref->sheet, pp->sheet);
	ss = gnm_sheet_get_size (sheet);

	if (foreign_sheet == NULL && (ss->max_cols <= ref->col || ss->max_rows <= ref->row)) {
		int new_cols = ref->col + 1, new_rows = ref->row + 1;
		GOUndo   * goundo;
		gboolean err;

		gnm_sheet_suggest_size (&new_cols, &new_rows);
		goundo = gnm_sheet_resize (sheet, new_cols, new_rows, NULL, &err);
		if (goundo) g_object_unref (goundo);

		ss = gnm_sheet_get_size (sheet);
		if (ss->max_cols <= ref->col || ss->max_rows <= ref->row)
			return start;
	}

	if (ref->col_relative)
		ref->col -= pp->eval.col;
	if (ref->row_relative)
		ref->row -= pp->eval.row;

	return ptr;
}

static char const *
odf_parse_external (char const *start, gchar **external,
		    GnmConventions const *convs)
{
	char const *ptr;
	GString *str;

	/* Source ::= "'" IRI "'" "#" */
	if (*start != '\'')
		return start;
	str = g_string_new (NULL);
	ptr = odf_strunescape (start, str, convs);

	if (ptr == NULL || *ptr != '#') {
		g_string_free (str, TRUE);
		return start;
	}

	*external = g_string_free (str, FALSE);
	return (ptr + 1);
}

static char const *
oo_rangeref_parse (GnmRangeRef *ref, char const *start, GnmParsePos const *pp,
		   GnmConventions const *convs)
{
	char const *ptr;
	char *external = NULL;
	char *external_sheet_1 = NULL;
	char *external_sheet_2 = NULL;
	ODFConventions *oconv = (ODFConventions *)convs;

	ptr = odf_parse_external (start, &external, convs);

	ptr = oo_cellref_parse (&ref->a, ptr, pp, 
				external == NULL ? NULL : &external_sheet_1);
	if (*ptr == ':')
		ptr = oo_cellref_parse (&ref->b, ptr+1, pp, 
				external == NULL ? NULL : &external_sheet_2);
	else
		ref->b = ref->a;
	if (ref->b.sheet == invalid_sheet)
		ref->a.sheet = invalid_sheet;
	if (external != NULL) {
		Workbook *wb = pp->wb, *ext_wb;
		Workbook *ref_wb = wb ? wb : pp->sheet->workbook;

		ext_wb = (*convs->input.external_wb) (convs, ref_wb, external);
		if (ext_wb == NULL) {
			if (oconv != NULL)
				oo_warning (oconv->xin, 
					    _("Ignoring reference to unknown "
					      "external workbook '%s'"), 
					    external);
			ref->a.sheet = invalid_sheet;
		} else {
			if (external_sheet_1 != NULL)
				ref->a.sheet = workbook_sheet_by_name 
					(ext_wb, external_sheet_1);
			else 
				ref->a.sheet = workbook_sheet_by_index
					(ext_wb, 0);
			if (external_sheet_2 != NULL)
				ref->b.sheet = workbook_sheet_by_name 
					(ext_wb, external_sheet_1);
			else 
				ref->b.sheet = NULL;
		}
		g_free (external);
		g_free (external_sheet_1);
		g_free (external_sheet_2);
	}
	return ptr;
}

static char const *
oo_expr_rangeref_parse (GnmRangeRef *ref, char const *start, GnmParsePos const *pp,
			GnmConventions const *convs)
{
	char const *ptr;
	if (*start == '[') {
		if (strncmp (start, "[#REF!]", 7) == 0) {
			ref->a.sheet = invalid_sheet;
			return start + 7;
		}
		ptr = oo_rangeref_parse (ref, start+1, pp, convs);
		if (*ptr == ']')
			return ptr + 1;
	}
	return start;
}

static char const *
odf_strunescape (char const *string, GString *target,
		   G_GNUC_UNUSED GnmConventions const *convs)
{
	/* Constant strings are surrounded by double-quote characters */
	/* (QUOTATION MARK, U+0022); a literal double-quote character '"'*/
	/* (QUOTATION MARK, U+0022) as */
	/* string content is escaped by duplicating it. */

	char quote = *string++;
	size_t oldlen = target->len;

	/* This should be UTF-8 safe as long as quote is ASCII.  */
	do {
		while (*string != quote) {
			if (*string == '\0')
				goto error;
			g_string_append_c (target, *string);
			string++;
		}
		string++;
		if (*string == quote)
			g_string_append_c (target, quote);
	} while (*string++ == quote);
	return --string;

 error:
	g_string_truncate (target, oldlen);
	return NULL;	
}

typedef struct {
	GHashTable *orig2fixed;
	GHashTable *fixed2orig;
	OOParseState *state;
} odf_fix_expr_names_t;

static odf_fix_expr_names_t *
odf_fix_expr_names_t_new (OOParseState *state)
{
	odf_fix_expr_names_t *fen = g_new (odf_fix_expr_names_t, 1);
	
	fen->fixed2orig = g_hash_table_new (g_str_hash, g_str_equal);
	fen->orig2fixed = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
	fen->state = state;

	return fen;
}

static void
odf_fix_expr_names_t_free (odf_fix_expr_names_t *fen)
{
	g_hash_table_unref (fen->fixed2orig);
	g_hash_table_unref (fen->orig2fixed);
	g_free (fen);
}

static void
odf_fix_expr_names_t_add (odf_fix_expr_names_t *fen, char const *orig, char *fixed)
{
	char *orig_c = g_strdup (orig);
	g_hash_table_insert (fen->orig2fixed, orig_c, fixed);
	g_hash_table_insert (fen->fixed2orig, fixed, orig_c);
}

static gboolean
odf_fix_en_validate (char const *name, odf_fix_expr_names_t *fen)
{
	if (!expr_name_validate (name))
		return FALSE;
	if (NULL != g_hash_table_lookup (fen->fixed2orig, name))
		return FALSE;

	WORKBOOK_FOREACH_SHEET
		(fen->state->pos.wb, sheet,
		 {
			 GnmParsePos pp;
			 parse_pos_init_sheet (&pp, sheet);
			 if (expr_name_lookup (&pp, name))
				 return FALSE;
		 });

	return TRUE;
}

static void 
odf_fix_en_collect (gchar const *key, GnmNamedExpr *nexpr, odf_fix_expr_names_t *fen)
{
	GString *str;
	gchar *here;

	if (expr_name_validate (key))
		return;
	if (NULL != g_hash_table_lookup (fen->orig2fixed, key))
		return;
	str = g_string_new (key);
	while ((here = strchr (str->str, '.')) != NULL)
		*here = '_';
	while (!odf_fix_en_validate (str->str, fen))
		g_string_append_c (str, '_');
	odf_fix_expr_names_t_add (fen, key, g_string_free (str, FALSE));
}

static void 
odf_fix_en_apply (const char *orig, GnmNamedExpr *nexpr, GHashTable *orig2fixed)
{
	const char *fixed = g_hash_table_lookup (orig2fixed, orig);
	if (fixed)
		expr_name_set_name (nexpr, fixed);
}

/**
 * When we initialy validate names we have to accept every ODF name
 * in odf_fix_expr_names we fix them.
 *
 *
 */

static void
odf_fix_expr_names (OOParseState *state)
{
	odf_fix_expr_names_t *fen = odf_fix_expr_names_t_new (state);

	workbook_foreach_name (state->pos.wb, FALSE,
			       (GHFunc)odf_fix_en_collect, fen);
	workbook_foreach_name (state->pos.wb, FALSE,
			       (GHFunc)odf_fix_en_apply, fen->orig2fixed);

	odf_fix_expr_names_t_free (fen);
}

/**
 * odf_expr_name_validate:
 * @name: tentative name
 *
 * returns TRUE if the given name is valid, FALSE otherwise.
 * 
 * We are accepting names here that contain periods or look like addresses. 
 * They need to be replaced when we have finished parsing the file since 
 * they are not allowed inside Gnumeric.
 */
static gboolean
odf_expr_name_validate (const char *name)
{
	const char *p;
	GnmValue *v;

	g_return_val_if_fail (name != NULL, FALSE);

	if (name[0] == 0)
		return FALSE;

	v = value_new_from_string (VALUE_BOOLEAN, name, NULL, TRUE);
	if (!v)
		v = value_new_from_string (VALUE_BOOLEAN, name, NULL, FALSE);
	if (v) {
		value_release (v);
		return FALSE;
	}

	/* Hmm...   Now what?  */
	if (!g_unichar_isalpha (g_utf8_get_char (name)) &&
	    name[0] != '_')
		return FALSE;

	for (p = name; *p; p = g_utf8_next_char (p)) {
		if (!g_unichar_isalnum (g_utf8_get_char (p)) &&
		    p[0] != '_' && p[0] != '.')
			return FALSE;
	}

	return TRUE;
}


static GnmExpr const *
oo_func_map_in (GnmConventions const *convs, Workbook *scope,
		char const *name, GnmExprList *args);

static GnmConventions *
oo_conventions_new (OOParseState *state, GsfXMLIn *xin)
{
	GnmConventions *conv = gnm_conventions_new_full 
		(sizeof (ODFConventions));
	ODFConventions *oconv = (ODFConventions *)conv;
	conv->decode_ampersands	= TRUE;
	conv->exp_is_left_associative = TRUE;

	conv->intersection_char	= '!';
	conv->decimal_sep_dot	= TRUE;
	conv->range_sep_colon	= TRUE;
	conv->arg_sep		= ';';
	conv->array_col_sep	= ';';
	conv->array_row_sep	= '|';
	conv->input.string	= odf_strunescape;
	conv->input.func	= oo_func_map_in;
	conv->input.range_ref	= oo_expr_rangeref_parse;
	conv->input.name_validate    = odf_expr_name_validate;
	conv->sheet_name_sep	= '.';
	oconv->state            = state;
	oconv->xin              = xin;

	return conv;
}

static void
oo_load_convention (OOParseState *state, GsfXMLIn *xin, OOFormula type)
{
	GnmConventions *convs;

	g_return_if_fail (state->convs[type] == NULL);

	switch (type) {
	case FORMULA_MICROSOFT:
		convs = gnm_xml_io_conventions ();
		convs->exp_is_left_associative = TRUE;
		break;
	case FORMULA_OLD_OPENOFFICE:
		convs = oo_conventions_new (state, xin);
		convs->sheet_name_sep	= '!'; /* Note that we are using this also as a marker*/
		                               /* in the function handlers */
		break;
	case FORMULA_OPENFORMULA:
	default:
		convs = oo_conventions_new (state, xin);
		break;
	}

	state->convs[type] = convs;
}

static GnmExprTop const *
oo_expr_parse_str_try (GsfXMLIn *xin, char const *str,
		       GnmParsePos const *pp, GnmExprParseFlags flags,
		       OOFormula type, GnmParseError  *perr)
{
	OOParseState *state = (OOParseState *)xin->user_state;

	if (state->convs[type] == NULL)
		oo_load_convention (state, xin, type);
	return gnm_expr_parse_str (str, pp, flags,
				    state->convs[type], perr);
}

static GnmExprTop const *
oo_expr_parse_str (GsfXMLIn *xin, char const *str,
		   GnmParsePos const *pp, GnmExprParseFlags flags,
		   OOFormula type)
{
	GnmExprTop const *texpr;
	GnmParseError  perr;

	parse_error_init (&perr);

	texpr = oo_expr_parse_str_try (xin, str, pp, flags, type, &perr);
	if (texpr == NULL) {
		if (*str != '[') {
			/* There are faulty expressions in the wild that */
			/* are references w/o [] */
			char *test = g_strdup_printf ("[%s]", str);
			texpr = oo_expr_parse_str_try (xin, test, pp, 
						       flags, type, NULL);
			g_free (test);
		}
		if (texpr == NULL)
			oo_warning (xin, _("Unable to parse '%s' ('%s')"),
				    str, perr.err->message);
	}
	parse_error_free (&perr);
	return texpr;
}

/****************************************************************************/

static void
oo_date_convention (GsfXMLIn *xin, xmlChar const **attrs)
{
	/* <table:null-date table:date-value="1904-01-01"/> */
	OOParseState *state = (OOParseState *)xin->user_state;
	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_TABLE, "date-value")) {
			if (!strncmp (CXML2C (attrs[1]), "1904", 4))
				workbook_set_1904 (state->pos.wb, TRUE);
		}
}
static void
oo_iteration (GsfXMLIn *xin, xmlChar const **attrs)
{
	/* <table:iteration table:status="enable"/> */
	OOParseState *state = (OOParseState *)xin->user_state;
	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_TABLE, "status"))
			workbook_iteration_enabled (state->pos.wb,
				strcmp (CXML2C (attrs[1]), "enable") == 0);
}

static void
oo_table_start (GsfXMLIn *xin, xmlChar const **attrs)
{
	/* <table:table table:name="Result" table:style-name="ta1"> */
	OOParseState *state = (OOParseState *)xin->user_state;
	gchar *style_name = NULL;
	gchar *table_name = NULL;

	state->pos.eval.col = 0;
	state->pos.eval.row = 0;
	state->extent_data.col = state->extent_style.col = 0;
	state->extent_data.row = state->extent_style.row = 0;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_TABLE, "name")) {
			table_name = g_strdup (CXML2C (attrs[1]));
		} else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_TABLE, "style-name"))  {
			style_name = g_strdup (CXML2C (attrs[1]));
		}

	if (table_name != NULL) {
		state->pos.sheet = workbook_sheet_by_name (state->pos.wb, table_name);
		if (NULL == state->pos.sheet) {
			state->pos.sheet = sheet_new (state->pos.wb, table_name, 256, 65536);
			workbook_sheet_attach (state->pos.wb, state->pos.sheet);
		} else {
			/* We either have a corrupted file with a duplicate */
			/* sheet name or the sheet was created implicitly.  */
			if (NULL != g_slist_find (state->sheet_order, state->pos.sheet)) {
				/* corrupted file! */
				char *new_name, *base;

				base = g_strdup_printf (_("%s_IN_CORRUPTED_FILE"), table_name);
				new_name =  workbook_sheet_get_free_name (state->pos.wb,
							   base, FALSE, FALSE);
				g_free (base);

				oo_warning (xin, _("This file is corrupted with a "
						   "duplicate sheet name \"%s\", "
						   "now renamed to \"%s\"."),
					    table_name, new_name);
				state->pos.sheet = sheet_new (state->pos.wb, new_name,
							      gnm_conf_get_core_workbook_n_cols (),
							      gnm_conf_get_core_workbook_n_rows ());
				workbook_sheet_attach (state->pos.wb, state->pos.sheet);
				g_free (new_name);
			}
		}
	} else {
		table_name = workbook_sheet_get_free_name (state->pos.wb,
							   _("SHEET_IN_CORRUPTED_FILE"),
							   TRUE, FALSE);
		state->pos.sheet = sheet_new (state->pos.wb, table_name,
					      gnm_conf_get_core_workbook_n_cols (),
					      gnm_conf_get_core_workbook_n_rows ());
		workbook_sheet_attach (state->pos.wb, state->pos.sheet);

		/* We are missing the table name. This is bad! */
		oo_warning (xin, _("This file is corrupted with an "
				   "unnamed sheet "
				   "now named \"%s\"."),
			    table_name);
	}

	g_free (table_name);

	/* Store sheets in correct order in case we implicitly
	 * created one out of order */
	state->sheet_order = g_slist_prepend
		(state->sheet_order, state->pos.sheet);

	if (style_name != NULL) {
		OOSheetStyle const *style = g_hash_table_lookup (state->styles.sheet, style_name);
		if (style) {
			g_object_set (state->pos.sheet,
				      "visibility", style->visibility,
				      "text-is-rtl", style->is_rtl,
				      NULL);
			if (style->tab_color_set) {
				GnmColor *color
					= style_color_new_go (style->tab_color);
				g_object_set
					(state->pos.sheet,
					 "tab-background",
					 color,
					 NULL);
				style_color_unref (color);
			}
			if (style->tab_text_color_set){
				GnmColor *color
					= style_color_new_go
					(style->tab_text_color);
				g_object_set
					(state->pos.sheet,
					 "tab-foreground",
					 color,
					 NULL);
				style_color_unref (color);
			}
		}
		g_free (style_name);
	}
	if (state->default_style.rows != NULL)
		sheet_row_set_default_size_pts (state->pos.sheet,
							state->default_style.rows->size_pts);
	if (state->default_style.columns != NULL)
		sheet_col_set_default_size_pts (state->pos.sheet,
						state->default_style.columns->size_pts);
}

/* odf_validation <table:name> <val1> */
/* odf_validation <table:condition> <of:cell-content-is-in-list("1";"2";"3")> */
/* odf_validation <table:display-list> <unsorted> */
/* odf_validation <table:base-cell-address> <Tabelle1.A1> */

typedef struct {
	char *condition;
	char *base_cell_address;
	gboolean allow_blank;
	gboolean use_dropdown;
	OOFormula f_type;
} odf_validation_t;

static GnmValidation *
odf_validation_new_list (GsfXMLIn *xin, odf_validation_t *val, guint offset)
{
	OOParseState *state = (OOParseState *)xin->user_state;
	GnmValidation *validation = NULL;
	char *start = NULL, *end = NULL;
	GString *str;
	GnmExprTop const *texpr = NULL;
	GnmParsePos   pp;


	start = strchr (val->condition + offset, '(');
	if (start != NULL)
		end = strrchr (start, ')');
	if (end == NULL)
		return NULL;

	pp = state->pos;
	if (val->base_cell_address != NULL) {
		char *tmp = g_strconcat ("[", val->base_cell_address, "]", NULL);
		texpr = oo_expr_parse_str
			(xin, tmp, &pp,
			 GNM_EXPR_PARSE_FORCE_EXPLICIT_SHEET_REFERENCES,
			 FORMULA_OPENFORMULA);
		g_free (tmp);
		if (texpr != NULL) {
			if (GNM_EXPR_GET_OPER (texpr->expr) ==
			    GNM_EXPR_OP_CELLREF) {
				GnmCellRef const *ref = &texpr->expr->cellref.ref;
				parse_pos_init (&pp, state->pos.wb, ref->sheet,
						ref->col, ref->row);
			}
			gnm_expr_top_unref (texpr);
		}
	}

	if (*(start + 1) == '\"') {
		str = g_string_new ("{");
		g_string_append_len (str, start + 1, end - start - 1);
		g_string_append_c (str, '}');
	} else {
		str = g_string_new (NULL);
		g_string_append_len (str, start + 1, end - start - 1);
	}

	texpr = oo_expr_parse_str (xin, str->str, &pp,
				   GNM_EXPR_PARSE_DEFAULT,
				   val->f_type);

	if (texpr != NULL)
		validation = validation_new (VALIDATION_STYLE_WARNING,
					     VALIDATION_TYPE_IN_LIST,
					     VALIDATION_OP_NONE,
					     NULL, NULL,
					     texpr,
					     NULL,
					     val->allow_blank,
					     val->use_dropdown);

	g_string_free (str, TRUE);

	return validation;
}

static GnmValidation *
odf_validation_new_single_expr (GsfXMLIn *xin, odf_validation_t *val, 
				    char const *start, ValidationType val_type, 
				    ValidationOp val_op)
{
	OOParseState *state = (OOParseState *)xin->user_state;
	GnmExprTop const *texpr = NULL;
	GnmParsePos   pp;

	pp = state->pos;
	if (val->base_cell_address != NULL) {
		char *tmp = g_strconcat ("[", val->base_cell_address, "]", NULL);
		texpr = oo_expr_parse_str
			(xin, tmp, &pp,
			 GNM_EXPR_PARSE_FORCE_EXPLICIT_SHEET_REFERENCES,
			 FORMULA_OPENFORMULA);
		g_free (tmp);
		if (texpr != NULL) {
			if (GNM_EXPR_GET_OPER (texpr->expr) ==
			    GNM_EXPR_OP_CELLREF) {
				GnmCellRef const *ref = &texpr->expr->cellref.ref;
				parse_pos_init (&pp, state->pos.wb, ref->sheet,
						ref->col, ref->row);
			}
			gnm_expr_top_unref (texpr);
		}
	}

	texpr = oo_expr_parse_str (xin, start, &pp,
				   GNM_EXPR_PARSE_FORCE_EXPLICIT_SHEET_REFERENCES,
				   val->f_type);

	if (texpr != NULL)
		return validation_new (VALIDATION_STYLE_WARNING,
				       val_type,
				       val_op,
				       NULL, NULL,
				       texpr,
				       NULL,
				       val->allow_blank,
				       val->use_dropdown);
	return NULL;
}

static GnmValidation *
odf_validation_new_pair_expr (GsfXMLIn *xin, odf_validation_t *val, 
			      char const *start, ValidationType val_type, 
			      ValidationOp val_op)
{
	OOParseState *state = (OOParseState *)xin->user_state;
	GnmExprTop const *texpr = NULL;
	GnmParsePos   pp;
	GnmExprTop const *texpr_a = NULL, *texpr_b = NULL;
	char *pair = NULL;
	guint len = strlen (start);

	if (*start != '(' || *(start + len - 1) != ')')
		return NULL;
	start++;
	len -= 2;
	pair = g_strndup (start, len);

	pp = state->pos;
	if (val->base_cell_address != NULL) {
		char *tmp = g_strconcat ("[", val->base_cell_address, "]", NULL);
		texpr = oo_expr_parse_str
			(xin, tmp, &pp,
			 GNM_EXPR_PARSE_FORCE_EXPLICIT_SHEET_REFERENCES,
			 FORMULA_OPENFORMULA);
		g_free (tmp);
		if (texpr != NULL) {
			if (GNM_EXPR_GET_OPER (texpr->expr) ==
			    GNM_EXPR_OP_CELLREF) {
				GnmCellRef const *ref = &texpr->expr->cellref.ref;
				parse_pos_init (&pp, state->pos.wb, ref->sheet,
						ref->col, ref->row);
			}
			gnm_expr_top_unref (texpr);
		}
	}

	while (1) {
		gchar * try = g_strrstr_len (pair, len, ",");
		GnmExprTop const *texpr;
		
		if (try == NULL || try == pair) 
			goto pair_error;
		
		texpr = oo_expr_parse_str
			(xin, try + 1, &pp,
			 GNM_EXPR_PARSE_FORCE_EXPLICIT_SHEET_REFERENCES,
			 val->f_type);
		if (texpr != NULL) {
			texpr_b = texpr;
			*try = '\0';
			break;
		}
		len = try - pair - 1;
	}
	texpr_a = oo_expr_parse_str
		(xin, pair, &pp,
		 GNM_EXPR_PARSE_FORCE_EXPLICIT_SHEET_REFERENCES,
		 val->f_type);

	if (texpr_b != NULL)
		return validation_new (VALIDATION_STYLE_WARNING,
				       val_type,
				       val_op,
				       NULL, NULL,
				       texpr_a,
				       texpr_b,
				       val->allow_blank,
				       val->use_dropdown);
 pair_error:
	g_free (pair);
	return NULL;
}

static GnmValidation *
odf_validation_new_between (GsfXMLIn *xin, odf_validation_t *val, guint offset, ValidationType vtype,
			    gboolean no_not)
{
	char *start = val->condition + offset;

	while (*start == ' ')
		start++;

	return odf_validation_new_pair_expr
		(xin, val, start, vtype, no_not ? VALIDATION_OP_BETWEEN : VALIDATION_OP_NOT_BETWEEN);
}

static GnmValidation *
odf_validation_new_op (GsfXMLIn *xin, odf_validation_t *val, guint offset, ValidationType vtype)
{
	char *start = val->condition + offset;
	ValidationOp val_op = VALIDATION_OP_NONE;

	while (*start == ' ')
		start++;

	if (g_str_has_prefix (start, ">=")) {
		val_op = VALIDATION_OP_GTE;
		start += 2;
	} else if (g_str_has_prefix (start, "<=")) {
		val_op = VALIDATION_OP_LTE;
		start += 2;
	} else if (g_str_has_prefix (start, "!=")) {
		val_op = VALIDATION_OP_NOT_EQUAL;
		start += 2;
	} else if (g_str_has_prefix (start, "=")) {
		val_op = VALIDATION_OP_EQUAL;
		start += 1;
	} else if (g_str_has_prefix (start, ">")) {
		val_op = VALIDATION_OP_GT;
		start += 1;
	} else if (g_str_has_prefix (start, "<")) {
		val_op = VALIDATION_OP_LT;
		start += 1;
	} 
	
	if (val_op == VALIDATION_OP_NONE)
		return NULL;

	while (*start == ' ')
		start++;

	return odf_validation_new_single_expr
		(xin, val, start, vtype, val_op);
}

static GnmValidation *
odf_validations_analyze (GsfXMLIn *xin, odf_validation_t *val, guint offset, ValidationType vtype)
{
	char const *str = val->condition + offset;

	while (*str == ' ')
		str++;

	if (g_str_has_prefix (str, "cell-content-is-in-list"))
		return odf_validation_new_list 
			(xin, val, str - val->condition + strlen ("cell-content-is-in-list"));
	else if (g_str_has_prefix (str, "cell-content-text-length()"))
		return odf_validation_new_op
			(xin, val, str - val->condition + strlen ("cell-content-text-length()"), 
			 VALIDATION_TYPE_TEXT_LENGTH);
	else if (g_str_has_prefix (str, "cell-content-text-length-is-between"))
		return odf_validation_new_between
			(xin, val, str - val->condition + strlen ("cell-content-text-length-is-between"), 
			 VALIDATION_TYPE_TEXT_LENGTH, TRUE);
	else if (g_str_has_prefix (str, "cell-content-text-length-is-not-between"))
		return odf_validation_new_between
			(xin, val, str - val->condition + strlen ("cell-content-text-length-is-not-between"), 
			 VALIDATION_TYPE_TEXT_LENGTH, FALSE);
	else if (g_str_has_prefix (str, "cell-content-is-decimal-number() and"))
		return odf_validations_analyze 
			(xin, val, str - val->condition + strlen ("cell-content-is-decimal-number() and"), 
			 VALIDATION_TYPE_AS_NUMBER);
	else if (g_str_has_prefix (str, "cell-content-is-whole-number() and"))
		return odf_validations_analyze 
			(xin, val, str - val->condition + strlen ("cell-content-is-whole-number() and"), 
			 VALIDATION_TYPE_AS_INT);
	else if (g_str_has_prefix (str, "cell-content-is-date() and"))
		return odf_validations_analyze 
			(xin, val, str - val->condition + strlen ("cell-content-is-date() and"), 
			 VALIDATION_TYPE_AS_DATE);		
	else if (g_str_has_prefix (str, "cell-content-is-time() and"))
		return odf_validations_analyze 
			(xin, val, str - val->condition + strlen ("cell-content-is-time() and"), 
			 VALIDATION_TYPE_AS_TIME);		
	else if (g_str_has_prefix (str, "is-true-formula")) {
		if (vtype != VALIDATION_TYPE_ANY) {
			oo_warning
			(xin, _("Validation condition '%s' is not supported. "
				"It has been changed to '%s'."),
			 val->condition, str);
		}
		return odf_validation_new_single_expr
			(xin, val, str + strlen ("is-true-formula"), VALIDATION_TYPE_CUSTOM, 
			 VALIDATION_OP_NONE);
	} else if (g_str_has_prefix (str, "cell-content()"))
		return odf_validation_new_op
			(xin, val, str - val->condition + strlen ("cell-content()"), 
			 vtype);
	else if (g_str_has_prefix (str, "cell-content-is-between"))
		return odf_validation_new_between
			(xin, val, str - val->condition + strlen ("cell-content-is-between"), 
			 vtype, TRUE);
	else if (g_str_has_prefix (str, "cell-content-is-not-between"))
		return odf_validation_new_between
			(xin, val, str - val->condition + strlen ("cell-content-is-not-between"), 
			 vtype, FALSE);

	return NULL;
}


static GnmValidation *
odf_validations_translate (GsfXMLIn *xin, char const *name)
{
	OOParseState *state = (OOParseState *)xin->user_state;
	odf_validation_t *val = g_hash_table_lookup (state->validations, name);

	if (val == NULL) {
		oo_warning
			(xin, _("Undefined validation style encountered: %s"),
			 name);
		return NULL;
	}

	if (val->condition != NULL && val->f_type != FORMULA_NOT_SUPPORTED) {
		GnmValidation *validation = odf_validations_analyze 
			(xin, val, 0, VALIDATION_TYPE_ANY);
		if (validation != NULL) {
			GError   *err;
			if (NULL == (err = validation_is_ok (validation)))
				return validation;
			else {
				oo_warning (xin,
					    _("Ignoring invalid data "
					      "validation because : %s"),
					    _(err->message));
				validation_unref (validation);
				return NULL;
			}
		}
	}

	oo_warning (xin, _("Unsupported validation condition "
			   "encountered: \"%s\" with base address: \"%s\""),
		    val->condition, val->base_cell_address);

	return NULL;
}

static void
odf_validation_free (odf_validation_t *val)
{
	g_free (val->condition);
	g_free (val->base_cell_address);
}

static odf_validation_t *
odf_validation_new (void)
{
	odf_validation_t *val = g_new0 (odf_validation_t, 1);
	val->use_dropdown = TRUE;
	val->allow_blank = TRUE;
	val->f_type = FORMULA_NOT_SUPPORTED;
	return val;
}

static void
odf_validation (GsfXMLIn *xin, xmlChar const **attrs)
{
	static OOEnum const dropdown_types [] = {
		{ "none",	  0 },
		{ "sort-ascending",	  1 },
		{ "unsorted", 1 },
		{ NULL,	0 },
	};

	OOParseState *state = (OOParseState *)xin->user_state;
	char const *name = NULL;
	int tmp;
	odf_validation_t *validation = odf_validation_new ();

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2){
		if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]),
					 OO_NS_TABLE, "name" )) {
				name = CXML2C (attrs[1]);
		} else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]),
					       OO_NS_TABLE, "condition")) {
			char const *cond = CXML2C (attrs[1]);
			validation->f_type = odf_get_formula_type (xin, &cond);
			validation->condition = g_strdup (cond);
		} else if (oo_attr_bool (xin, attrs,
					 OO_NS_TABLE, "allow-empty-cell",
					 &validation->allow_blank)) {
		} else if (oo_attr_enum (xin, attrs, OO_NS_TABLE, "display-list", dropdown_types, &tmp)) {
			validation->use_dropdown = (tmp == 1);
		} else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]),
					       OO_NS_TABLE, "base-cell-address")) {
			validation->base_cell_address = g_strdup (CXML2C (attrs[1]));
		}
	}
	if (name != NULL)
		g_hash_table_insert (state->validations, g_strdup (name), validation);
	else
		odf_validation_free (validation);
}

static void
oo_table_end (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	OOParseState *state = (OOParseState *)xin->user_state;
	GnmRange r;
	int rows, cols;
	int max_cols, max_rows;

	maybe_update_progress (xin);

	if (NULL != state->page_breaks.h) {
		print_info_set_breaks (state->pos.sheet->print_info,
			state->page_breaks.h);
		state->page_breaks.h = NULL;
	}
	if (NULL != state->page_breaks.v) {
		print_info_set_breaks (state->pos.sheet->print_info,
			state->page_breaks.v);
		state->page_breaks.v = NULL;
	}

	max_cols = gnm_sheet_get_max_cols (state->pos.sheet);
	max_rows = gnm_sheet_get_max_rows (state->pos.sheet);

	/* default cell styles are applied only to cells that are specified
	 * which is a performance nightmare.  Instead we apply the styles to
	 * the entire column or row and clear the area beyond the extent here. */

	rows = state->extent_style.row;
	if (state->extent_data.row > rows)
		rows = state->extent_data.row;
	cols = state->extent_style.col;
	if (state->extent_data.col > cols)
		cols = state->extent_data.col;
	cols++; rows++;
	if (cols < max_cols) {
		range_init (&r, cols, 0,
			    max_cols - 1, max_rows - 1);
		sheet_style_apply_range (state->pos.sheet, &r,
				       sheet_style_default (state->pos.sheet));
	}
	if (rows < max_rows) {
		range_init (&r, 0, rows,
			    max_cols - 1, max_rows - 1);
		sheet_style_apply_range (state->pos.sheet, &r,
				       sheet_style_default (state->pos.sheet));
	}

	state->pos.eval.col = state->pos.eval.row = 0;
	state->pos.sheet = NULL;
}

static void
oo_append_page_break (OOParseState *state, int pos, gboolean is_vert, gboolean is_manual)
{
	GnmPageBreaks *breaks;

	if (is_vert) {
		if (NULL == (breaks = state->page_breaks.v))
			breaks = state->page_breaks.v = gnm_page_breaks_new (TRUE);
	} else {
		if (NULL == (breaks = state->page_breaks.h))
			breaks = state->page_breaks.h = gnm_page_breaks_new (FALSE);
	}

	gnm_page_breaks_append_break (breaks, pos,
				      is_manual ? GNM_PAGE_BREAK_MANUAL : GNM_PAGE_BREAK_NONE);
}

static void
oo_set_page_break (OOParseState *state, int pos, gboolean is_vert, gboolean is_manual)
{
	GnmPageBreaks *breaks = (is_vert) ? state->page_breaks.v : state->page_breaks.h;

	switch (gnm_page_breaks_get_break (breaks, pos)) {
	case GNM_PAGE_BREAK_NONE:
		oo_append_page_break (state, pos, is_vert, is_manual);
		return;
	case GNM_PAGE_BREAK_MANUAL:
		return;
	case GNM_PAGE_BREAK_AUTO:
	default:
		if (is_manual)
			gnm_page_breaks_set_break (breaks, pos, GNM_PAGE_BREAK_MANUAL);
		break;
	}
}

static void
oo_col_row_style_apply_breaks (OOParseState *state, OOColRowStyle *cr_style,
			       int pos, gboolean is_vert)
{

	if (cr_style->break_before != OO_PAGE_BREAK_NONE)
		oo_set_page_break (state, pos, is_vert,
				      cr_style->break_before == OO_PAGE_BREAK_MANUAL);
	if (cr_style->break_after  != OO_PAGE_BREAK_NONE)
		oo_append_page_break (state, pos+1, is_vert,
				      cr_style->break_after  == OO_PAGE_BREAK_MANUAL);
}

static void
oo_update_data_extent (OOParseState *state, int cols, int rows)
{
	if (state->extent_data.col < (state->pos.eval.col + cols - 1))
		state->extent_data.col = state->pos.eval.col + cols - 1;
	if (state->extent_data.row < (state->pos.eval.row + rows - 1))
		state->extent_data.row = state->pos.eval.row + rows - 1;
}
static void
oo_update_style_extent (OOParseState *state, int cols, int rows)
{
	if (cols > 0 && state->extent_style.col < (state->pos.eval.col + cols - 1))
		state->extent_style.col = state->pos.eval.col + cols - 1;
	if (rows > 0 && state->extent_style.row < (state->pos.eval.row + rows - 1))
		state->extent_style.row = state->pos.eval.row + rows - 1;
}

static int
oo_extent_sheet_cols (Sheet *sheet, int cols)
{
	GOUndo   * goundo;
	int new_cols, new_rows;
	gboolean err;

	new_cols = cols;
	new_rows = gnm_sheet_get_max_rows (sheet);
	gnm_sheet_suggest_size (&new_cols, &new_rows);

	goundo = gnm_sheet_resize (sheet, new_cols, new_rows, NULL, &err);
	if (goundo) g_object_unref (goundo);

	return gnm_sheet_get_max_cols (sheet);
}


static void
oo_col_start (GsfXMLIn *xin, xmlChar const **attrs)
{
	OOParseState *state = (OOParseState *)xin->user_state;
	OOColRowStyle *col_info = NULL;
	GnmStyle *style = NULL;
	int	  i, repeat_count = 1;
	gboolean  hidden = FALSE;
	int max_cols = gnm_sheet_get_max_cols (state->pos.sheet);

	maybe_update_progress (xin);

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_TABLE, "default-cell-style-name"))
			style = g_hash_table_lookup (state->styles.cell, attrs[1]);
		else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_TABLE, "style-name"))
			col_info = g_hash_table_lookup (state->styles.col, attrs[1]);
		else if (oo_attr_int_range (xin, attrs, OO_NS_TABLE, "number-columns-repeated", &repeat_count, 0, INT_MAX))
			;
		else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_TABLE, "visibility"))
			hidden = !attr_eq (attrs[1], "visible");

	if (state->pos.eval.col + repeat_count > max_cols) {
		max_cols = oo_extent_sheet_cols (state->pos.sheet, state->pos.eval.col
						 + repeat_count);
		if (state->pos.eval.col + repeat_count > max_cols) {
			oo_warning (xin, _("Ignoring column information beyond"
					   " column %i"), max_cols);
			repeat_count = max_cols - state->pos.eval.col - 1;
		}
	}

	if (hidden)
		colrow_set_visibility (state->pos.sheet, TRUE, FALSE, state->pos.eval.col,
			state->pos.eval.col + repeat_count - 1);

	/* see oo_table_end for details */
	if (NULL != style) {
		GnmRange r;
		r.start.col = state->pos.eval.col;
		r.end.col   = state->pos.eval.col + repeat_count - 1;
		r.start.row = 0;
		r.end.row  = gnm_sheet_get_last_row (state->pos.sheet);
		gnm_style_ref (style);
		sheet_style_apply_range (state->pos.sheet, &r, style);
		oo_update_style_extent (state, repeat_count, -1);
	}
	if (col_info != NULL) {
		if (state->default_style.columns == NULL && repeat_count > max_cols/2) {
			int const last = state->pos.eval.col + repeat_count;
			state->default_style.columns = g_memdup (col_info, sizeof (*col_info));
			state->default_style.columns->count = repeat_count;
			sheet_col_set_default_size_pts (state->pos.sheet,
							state->default_style.columns->size_pts);
			if (col_info->break_before != OO_PAGE_BREAK_NONE)
				for (i = state->pos.eval.row ; i < last; i++ )
					oo_set_page_break (state, i, TRUE,
							   col_info->break_before
							   == OO_PAGE_BREAK_MANUAL);
			if (col_info->break_after!= OO_PAGE_BREAK_NONE)
				for (i = state->pos.eval.col ; i < last; i++ )
					oo_append_page_break (state, i+1, FALSE,
							      col_info->break_after
							      == OO_PAGE_BREAK_MANUAL);
		} else {
			int last = state->pos.eval.col + repeat_count;
			for (i = state->pos.eval.col ; i < last; i++ ) {
				/* I can not find a listing for the default but will
				 * assume it is TRUE to keep the files rational */
				if (col_info->size_pts > 0.)
					sheet_col_set_size_pts (state->pos.sheet, i,
								col_info->size_pts, col_info->manual);
				oo_col_row_style_apply_breaks (state, col_info, i, TRUE);
			}
			col_info->count += repeat_count;
		}
	}

	state->pos.eval.col += repeat_count;
}

static int
oo_extent_sheet_rows (Sheet *sheet, int rows)
{
	GOUndo * goundo;
	int new_cols, new_rows;
	gboolean err;

	new_cols = gnm_sheet_get_max_cols (sheet);
	new_rows = rows;
	gnm_sheet_suggest_size (&new_cols, &new_rows);

	goundo = gnm_sheet_resize (sheet, new_cols, new_rows, NULL, &err);
	if (goundo) g_object_unref (goundo);

	return gnm_sheet_get_max_rows (sheet);
}

static void
oo_row_start (GsfXMLIn *xin, xmlChar const **attrs)
{
	OOParseState *state = (OOParseState *)xin->user_state;
	OOColRowStyle *row_info = NULL;
	GnmStyle *style = NULL;
	int	  i, repeat_count = 1;
	gboolean  hidden = FALSE;
	int max_rows = gnm_sheet_get_max_rows (state->pos.sheet);

	maybe_update_progress (xin);

	state->pos.eval.col = 0;

	if (state->pos.eval.row >= max_rows) {
		max_rows = oo_extent_sheet_rows (state->pos.sheet, state->pos.eval.row + 1);
		if (state->pos.eval.row >= max_rows) {
			oo_warning (xin, _("Content past the maximum number of rows (%i) supported."), max_rows);
			state->row_inc = 0;
			return;
		}
	}

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2) {
		if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_TABLE, "default-cell-style-name"))
			style = g_hash_table_lookup (state->styles.cell, attrs[1]);
		else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_TABLE, "style-name"))
			row_info = g_hash_table_lookup (state->styles.row, attrs[1]);
		else if (oo_attr_int_range (xin, attrs, OO_NS_TABLE, "number-rows-repeated", &repeat_count, 0, INT_MAX))
			;
		else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_TABLE, "visibility"))
			hidden = !attr_eq (attrs[1], "visible");
	}

	if (state->pos.eval.row + repeat_count > max_rows) {
		max_rows = oo_extent_sheet_rows
			(state->pos.sheet,
			 state->pos.eval.row + repeat_count);
		if (state->pos.eval.row + repeat_count >= max_rows)
        	/* There are probably lots of empty lines at the end. */
			repeat_count = max_rows - state->pos.eval.row - 1;
	}

	if (hidden)
		colrow_set_visibility (state->pos.sheet, FALSE, FALSE, state->pos.eval.row,
			state->pos.eval.row+repeat_count - 1);

	/* see oo_table_end for details */
	if (NULL != style) {
		GnmRange r;
		r.start.row = state->pos.eval.row;
		r.end.row   = state->pos.eval.row + repeat_count - 1;
		r.start.col = 0;
		r.end.col  = gnm_sheet_get_last_col (state->pos.sheet);
		gnm_style_ref (style);
		sheet_style_apply_range (state->pos.sheet, &r, style);
		oo_update_style_extent (state, -1, repeat_count);
	}

	if (row_info != NULL) {
		if (state->default_style.rows == NULL && repeat_count > max_rows/2) {
			int const last = state->pos.eval.row + repeat_count;
			state->default_style.rows = g_memdup (row_info, sizeof (*row_info));
			state->default_style.rows->count = repeat_count;
			sheet_row_set_default_size_pts (state->pos.sheet,
							state->default_style.rows->size_pts);
			if (row_info->break_before != OO_PAGE_BREAK_NONE)
				for (i = state->pos.eval.row ; i < last; i++ )
					oo_set_page_break (state, i, FALSE,
							   row_info->break_before
							   == OO_PAGE_BREAK_MANUAL);
			if (row_info->break_after!= OO_PAGE_BREAK_NONE)
				for (i = state->pos.eval.row ; i < last; i++ )
					oo_append_page_break (state, i+1, FALSE,
							      row_info->break_after
							      == OO_PAGE_BREAK_MANUAL);
		} else {
			int const last = state->pos.eval.row + repeat_count;
			for (i = state->pos.eval.row ; i < last; i++ ) {
				if (row_info->size_pts > 0.)
					sheet_row_set_size_pts (state->pos.sheet, i,
								row_info->size_pts, row_info->manual);
				oo_col_row_style_apply_breaks (state, row_info, i, FALSE);
			}
			row_info->count += repeat_count;
		}
	}

	state->row_inc = repeat_count;
}

static void
oo_row_end (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	OOParseState *state = (OOParseState *)xin->user_state;
	state->pos.eval.row += state->row_inc;
}

static OOFormula
odf_get_formula_type (GsfXMLIn *xin, char const **str)
{
	OOParseState *state = (OOParseState *)xin->user_state;
	OOFormula f_type = FORMULA_NOT_SUPPORTED;
	if (state->ver == OOO_VER_OPENDOC) {
		if (strncmp (*str, "msoxl:", 6) == 0) {
			*str += 6;
			f_type = FORMULA_MICROSOFT;
		} else if (strncmp (*str, "oooc:", 5) == 0) {
			*str += 5;
			f_type = FORMULA_OLD_OPENOFFICE;
		} else if (strncmp (*str, "of:", 3) == 0) {
			*str += 3;
			f_type = FORMULA_OPENFORMULA;
		} else {
			/* They really should include a namespace */
			/* We assume that it is an OpenFormula expression */
			*str += 0;
			f_type = FORMULA_OPENFORMULA;
		}
	} else if (state->ver == OOO_VER_1)
		f_type = FORMULA_OLD_OPENOFFICE;

	return f_type;
}

static void
oo_cell_start (GsfXMLIn *xin, xmlChar const **attrs)
{
	OOParseState *state = (OOParseState *)xin->user_state;
	GnmExprTop const *texpr = NULL;
	GnmValue	*val = NULL;
	gboolean  has_date = FALSE, has_datetime = FALSE, has_time = FALSE;
	gboolean	 bool_val;
	gnm_float	 float_val = 0;
	int array_cols = -1, array_rows = -1;
	int merge_cols = 1, merge_rows = 1;
	GnmStyle *style = NULL;
	char const *style_name = NULL;
	char const *validation_name = NULL;
	char const *expr_string;
	GnmRange tmp;
	int max_cols = gnm_sheet_get_max_cols (state->pos.sheet);
	int max_rows = gnm_sheet_get_max_rows (state->pos.sheet);
	GnmValidation *validation = NULL;

	maybe_update_progress (xin);

	state->col_inc = 1;
	state->content_is_error = FALSE;
	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2) {
		if (oo_attr_int_range (xin, attrs, OO_NS_TABLE, "number-columns-repeated", 
				       &state->col_inc, 0, INT_MAX))
			;
		else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_TABLE, "formula")) {
			OOFormula f_type;

			if (attrs[1] == NULL) {
				oo_warning (xin, _("Missing expression"));
				continue;
			}

			expr_string = CXML2C (attrs[1]);
			f_type = odf_get_formula_type (xin, &expr_string);
			if (f_type == FORMULA_NOT_SUPPORTED)
				continue;

			expr_string = gnm_expr_char_start_p (expr_string);
			if (expr_string == NULL)
				oo_warning (xin, _("Expression '%s' does not start "
						   "with a recognized character"), attrs[1]);
			else if (*expr_string == '\0')
				/* Ick.  They seem to store error cells as
				 * having value date with expr : '=' and the
				 * message in the content.
				 */
				state->content_is_error = TRUE;
			else
				texpr = oo_expr_parse_str 
					(xin, expr_string,
					 &state->pos, GNM_EXPR_PARSE_DEFAULT, f_type);
		} else if (oo_attr_bool (xin, attrs,
					 (state->ver == OOO_VER_OPENDOC) ? 
					 OO_NS_OFFICE : OO_NS_TABLE,
					 "boolean-value", &bool_val))
			val = value_new_bool (bool_val);
		else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]),
			(state->ver == OOO_VER_OPENDOC) ? OO_NS_OFFICE : OO_NS_TABLE,
			"date-value")) {
			unsigned y, m, d, h, mi;
			gnm_float s;
			unsigned n = sscanf (CXML2C (attrs[1]), "%u-%u-%uT%u:%u:%" GNM_SCANF_g,
					     &y, &m, &d, &h, &mi, &s);

			if (n >= 3) {
				GDate date;
				g_date_set_dmy (&date, d, m, y);
				if (g_date_valid (&date)) {
					unsigned d_serial = go_date_g_to_serial (&date,
						workbook_date_conv (state->pos.wb));
					if (n >= 6) {
						double time_frac 
							= h + ((double)mi / 60.) + 
							((double)s / 3600.);
						val = value_new_float (d_serial + time_frac / 24.);
						has_datetime = TRUE;
					} else {
						val = value_new_int (d_serial);
						has_date = TRUE;
					}
				}
			}
		} else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]),
					       (state->ver == OOO_VER_OPENDOC) ? 
					       OO_NS_OFFICE : OO_NS_TABLE,
					       "time-value")) {
			unsigned h, m, s;
			if (3 == sscanf (CXML2C (attrs[1]), "PT%uH%uM%uS", &h, &m, &s)) {
				unsigned secs = h * 3600 + m * 60 + s;
				val = value_new_float (secs / (gnm_float)86400);
				has_time = TRUE;
			}
		} else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]),
					       (state->ver == OOO_VER_OPENDOC) ? 
					       OO_NS_OFFICE : OO_NS_TABLE,
					       "string-value"))
			val = value_new_string (CXML2C (attrs[1]));
		else if (oo_attr_float (xin, attrs,
			(state->ver == OOO_VER_OPENDOC) ? OO_NS_OFFICE : OO_NS_TABLE,
			"value", &float_val))
			val = value_new_float (float_val);
		else if (oo_attr_int_range (xin, attrs, OO_NS_TABLE, 
					    "number-matrix-columns-spanned", 
					    &array_cols, 0, INT_MAX))
			;
		else if (oo_attr_int_range (xin, attrs, OO_NS_TABLE, 
					    "number-matrix-rows-spanned", 
					    &array_rows, 0, INT_MAX))
			;
		else if (oo_attr_int_range (xin, attrs, OO_NS_TABLE, 
					    "number-columns-spanned", &merge_cols, 0, INT_MAX))
			;
		else if (oo_attr_int_range (xin, attrs, OO_NS_TABLE, 
					    "number-rows-spanned", &merge_rows, 0, INT_MAX))
			;
		else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_TABLE, "style-name"))
			style_name = attrs[1];
		else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_TABLE, 
					     "content-validation-name"))
			validation_name = attrs[1];
	}

	if (state->pos.eval.col >= max_cols ||
	    state->pos.eval.row >= max_rows) {
		if (texpr)
			gnm_expr_top_unref (texpr);
		value_release (val);
		return;
	}



	merge_cols = MIN (merge_cols, max_cols - state->pos.eval.col);
	merge_rows = MIN (merge_rows, max_rows - state->pos.eval.row);

	if (style_name != NULL) {
		if (has_datetime)
			style = g_hash_table_lookup (state->styles.cell_datetime, style_name);
		else if (has_date)
			style = g_hash_table_lookup (state->styles.cell_date, style_name);
		else if (has_time)
			style = g_hash_table_lookup (state->styles.cell_time, style_name);

		if (style == NULL) {
			style = g_hash_table_lookup (state->styles.cell, style_name);

			if (((style != NULL) || (state->ver == OOO_VER_1))
			    && (has_datetime || has_date || has_time)) {
				if ((style == NULL) ||
				    ((!gnm_style_is_element_set (style, MSTYLE_FORMAT))
				     || go_format_is_general (gnm_style_get_format (style)))) {
					GOFormat *format;
					style = (style == NULL) ? gnm_style_new () : 
						gnm_style_dup (style);
					gnm_style_ref (style);
					/* Now we have 2 references for style */
					if (has_datetime) {
						format = go_format_default_date_time ();
						g_hash_table_replace (state->styles.cell_datetime,
								      g_strdup (style_name), style);
					} else if (has_date) {
						format = go_format_default_date ();
						g_hash_table_replace (state->styles.cell_date,
							      g_strdup (style_name), style);
					} else {
						format = go_format_default_time ();
						g_hash_table_replace (state->styles.cell_time,
								      g_strdup (style_name), style);
					} 
					gnm_style_set_format (style, format);
					/* Since (has_datetime || has_date || has_time) we now */
					/* have 1 references for style */
				} else 
					gnm_style_ref (style);
				/* 1 reference for style*/
			} else if (style != NULL)
				gnm_style_ref (style);
			/* 1 reference for style*/
		} else 
			gnm_style_ref (style);
		/* 1 reference for style*/
	}

	if ((validation_name != NULL) &&
	    (NULL != (validation = odf_validations_translate (xin, validation_name)))) {
		if (style == NULL)
			style = gnm_style_new ();
		else {
			GnmStyle *ostyle = style;
			style = gnm_style_dup (ostyle);
			gnm_style_unref (ostyle);
		}
		/* 1 reference for style*/
		gnm_style_set_validation (style, validation);
	}

	if (style != NULL) {
		if (state->col_inc > 1 || state->row_inc > 1) {
			range_init_cellpos_size (&tmp, &state->pos.eval,
				state->col_inc, state->row_inc);
			sheet_style_apply_range (state->pos.sheet, &tmp, style);
			oo_update_style_extent (state, state->col_inc, state->row_inc);
		} else if (merge_cols > 1 || merge_rows > 1) {
			range_init_cellpos_size (&tmp, &state->pos.eval,
						 merge_cols, merge_rows);
			sheet_style_apply_range (state->pos.sheet, &tmp, style);
			oo_update_style_extent (state, merge_cols, merge_rows);
		} else {
			sheet_style_apply_pos (state->pos.sheet,
					       state->pos.eval.col, state->pos.eval.row,
					       style);
			oo_update_style_extent (state, 1, 1);
		}
	}

	state->content_is_simple = FALSE;
	if (texpr != NULL) {
		GnmCell *cell = sheet_cell_fetch (state->pos.sheet,
						  state->pos.eval.col,
						  state->pos.eval.row);

		if (array_cols > 0 || array_rows > 0) {
			GnmRange r;
			Sheet *sheet = state->pos.sheet;

			if (array_cols <= 0) {
				array_cols = 1;
				oo_warning (xin, _("Invalid array expression does not specify number of columns."));
			} else if (array_rows <= 0) {
				array_rows = 1;
				oo_warning (xin, _("Invalid array expression does not specify number of rows."));
			}

			r.start = state->pos.eval;
			r.end = r.start;
			r.end.col += array_cols - 1;
			r.end.row += array_rows - 1;

			if (r.end.col > gnm_sheet_get_last_col (sheet))
				oo_extent_sheet_cols (sheet, r.end.col + 1);
			if (r.end.row > gnm_sheet_get_last_row (sheet))
				oo_extent_sheet_rows (sheet, r.end.row + 1);

			if (r.end.col > gnm_sheet_get_last_col (sheet)) {
				oo_warning
					(xin,
					 _("Content past the maximum number "
					   "of columns (%i) supported."),
					 gnm_sheet_get_max_cols (sheet));
				r.end.col = gnm_sheet_get_last_col (sheet);
			}
			if (r.end.row > gnm_sheet_get_last_row (sheet)) {
				oo_warning
					(xin,
					 _("Content past the maximum number "
					   "of rows (%i) supported."),
					 gnm_sheet_get_max_rows (sheet));
				r.end.row = gnm_sheet_get_last_row (sheet);
			}

			gnm_cell_set_array (sheet, &r, texpr);
			gnm_expr_top_unref (texpr);
			if (val != NULL)
				gnm_cell_assign_value (cell, val);
			oo_update_data_extent
				(state,
				 r.end.col - r.start.col + 1,
				 r.end.row - r.start.row + 1);
		} else {
			if (val != NULL)
				gnm_cell_set_expr_and_value (cell, texpr, val,
							     TRUE);
			else
				gnm_cell_set_expr (cell, texpr);
			gnm_expr_top_unref (texpr);
			oo_update_data_extent (state, 1, 1);
		}
	} else if (val != NULL) {
		GnmCell *cell = sheet_cell_fetch (state->pos.sheet,
			state->pos.eval.col, state->pos.eval.row);

		/* has cell previously been initialized as part of an array */
		if (gnm_cell_is_nonsingleton_array (cell))
			gnm_cell_assign_value (cell, val);
		else
			gnm_cell_set_value (cell, val);
		oo_update_data_extent (state, 1, 1);
	} else if (!state->content_is_error)
		/* store the content as a string */
		state->content_is_simple = TRUE;

	if (merge_cols > 1 || merge_rows > 1) {
		range_init_cellpos_size (&tmp, &state->pos.eval,
					 merge_cols, merge_rows);
		gnm_sheet_merge_add (state->pos.sheet, &tmp, FALSE, NULL);
	}
}

static void
oo_cell_end (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	OOParseState *state = (OOParseState *)xin->user_state;

	if (state->col_inc > 1 || state->row_inc > 1) {
		GnmCell *cell = sheet_cell_get (state->pos.sheet,
			state->pos.eval.col, state->pos.eval.row);

		if (!gnm_cell_is_empty (cell)) {
			int i, j;
			GnmCell *next;
			for (j = 0; j < state->row_inc ; j++)
				for (i = 0; i < state->col_inc ; i++)
					if (j > 0 || i > 0) {
						next = sheet_cell_fetch (state->pos.sheet,
							state->pos.eval.col + i, state->pos.eval.row + j);
						if (gnm_cell_is_nonsingleton_array (next))
							gnm_cell_assign_value (next, value_dup (cell->value));
						else
							gnm_cell_set_value (next, value_dup (cell->value));
					}
			oo_update_data_extent (state, state->col_inc, state->row_inc);
		}
	}
	state->pos.eval.col += state->col_inc;
}

static void
oo_cell_content_end (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	OOParseState *state = (OOParseState *)xin->user_state;

	if (state->content_is_simple || state->content_is_error) {
		int max_cols = gnm_sheet_get_max_cols (state->pos.sheet);
		int max_rows = gnm_sheet_get_max_rows (state->pos.sheet);
		GnmValue *v;
		GnmCell *cell;

		if (state->pos.eval.col >= max_cols ||
		    state->pos.eval.row >= max_rows)
			return;

		cell = sheet_cell_fetch (state->pos.sheet,
					 state->pos.eval.col,
					 state->pos.eval.row);

		if (state->content_is_simple)
			/* embedded newlines stored as a series of <p> */
			if (VALUE_IS_STRING (cell->value))
				v = value_new_string_str (go_string_new_nocopy (
					g_strconcat (cell->value->v_str.val->str, "\n",
						     xin->content->str, NULL)));
			else
				v = value_new_string (xin->content->str);
		else
			v = value_new_error (NULL, xin->content->str);

		/* Note that we could be looking at the result of an array calculation */
		gnm_cell_assign_value (cell, v);
		oo_update_data_extent (state, 1, 1);
	}
}

static void
oo_covered_cell_start (GsfXMLIn *xin, xmlChar const **attrs)
{
	OOParseState *state = (OOParseState *)xin->user_state;

	state->col_inc = 1;
	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (oo_attr_int_range (xin, attrs, OO_NS_TABLE, "number-columns-repeated", &state->col_inc, 0, INT_MAX))
			;
#if 0
		/* why bother it is covered ? */
		else if (!strcmp (CXML2C (attrs[0]), OO_NS_TABLE, "style-name"))
			style = g_hash_table_lookup (state->styles.cell, attrs[1]);

	if (style != NULL) {
		gnm_style_ref (style);
		sheet_style_apply_pos (state->pos.sheet,
		     state->pos.eval.col, state->pos.eval.row,
		     style);
	}
#endif
}

static void
oo_covered_cell_end (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	OOParseState *state = (OOParseState *)xin->user_state;
	state->pos.eval.col += state->col_inc;
}



static void
oo_dash (GsfXMLIn *xin, xmlChar const **attrs)
{
	OOParseState *state = (OOParseState *)xin->user_state;
	GOLineDashType t = GO_LINE_DOT;
	char const *name = NULL;
	gnm_float distance = 0., len_dot1 = 0., len_dot2 = 0.;
	int n_dots1 = 0, n_dots2 = 2;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_DRAW, "name"))
			name = CXML2C (attrs[1]);
		else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_DRAW, "style"))
			/* rect or round, ignored */;
		else if (NULL != oo_attr_distance (xin, attrs, OO_NS_DRAW, "distance",
						   &distance))
			/* FIXME: this could be a percentage in 1.2 */;
		else if (NULL != oo_attr_distance (xin, attrs, OO_NS_DRAW, "dots1-length",
						   &len_dot1))
			/* FIXME: this could be a percentage in 1.2 */;
		else if (NULL != oo_attr_distance (xin, attrs, OO_NS_DRAW, "dots2-length",
						   &len_dot2))
			/* FIXME: this could be a percentage in 1.2 */;
		else if (oo_attr_int_range (xin, attrs, OO_NS_DRAW,
					    "dots1", &n_dots1, 0, 10));
		else if (oo_attr_int_range (xin, attrs, OO_NS_DRAW,
					    "dots2", &n_dots2, 0, 10));

	/* We need to figure out the best matching dot style */

	if (n_dots2 == 0) {
		/* only one type of dots */
		if (len_dot1 <  1.5)
			t = GO_LINE_S_DOT;
		else if (len_dot1 <  4.5)
			t = GO_LINE_DOT;
		else if (len_dot1 <  9)
			t = GO_LINE_S_DASH;
		else if (len_dot1 <  15)
			t = GO_LINE_DASH;
		else
			t = GO_LINE_LONG_DASH;
	} else if (n_dots2 > 1 && n_dots1 > 1 )
		t = GO_LINE_DASH_DOT_DOT_DOT; /* no matching dashing available */
	else if ( n_dots2 == 1 && n_dots2 == 1) {
		gnm_float max = (len_dot1 < len_dot2) ? len_dot2 : len_dot1;
		if (max > 7.5)
			t = GO_LINE_DASH_DOT;
		else
			t = GO_LINE_S_DASH_DOT;
	} else {
		gnm_float max = (len_dot1 < len_dot2) ? len_dot2 : len_dot1;
		int max_dots = (n_dots1 < n_dots2) ? n_dots2 : n_dots1;

		if (max_dots > 2)
			t = GO_LINE_DASH_DOT_DOT_DOT;
		else if (max > 7.5)
			t = GO_LINE_DASH_DOT_DOT;
		else
			t = GO_LINE_S_DASH_DOT_DOT;
	}

	if (name != NULL)
		g_hash_table_replace (state->chart.dash_styles,
				      g_strdup (name), GUINT_TO_POINTER (t));
	else
		oo_warning (xin, _("Unnamed dash style encountered."));
}


static void
oo_fill_image (GsfXMLIn *xin, xmlChar const **attrs)
{
	OOParseState *state = (OOParseState *)xin->user_state;
	char const *name = NULL;
	char const *href = NULL;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_DRAW, "name"))
			name = CXML2C (attrs[1]);
		else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]),
					     OO_NS_XLINK, "href"))
			href = CXML2C (attrs[1]);
	if (name == NULL)
		oo_warning (xin, _("Unnamed image fill style encountered."));
	else if (href == NULL)
		oo_warning (xin, _("Image fill style \'%s\' has no attached image."),
			    name);
	else {
		g_hash_table_replace (state->chart.fill_image_styles,
				      g_strdup (name), g_strdup (href));
	}
}

static void
oo_gradient (GsfXMLIn *xin, xmlChar const **attrs)
{
	OOParseState *state = (OOParseState *)xin->user_state;
	gradient_info_t *info = g_new0 (gradient_info_t, 1);
	char const *name = NULL;
	int angle = 0;
	char const *style = NULL;
	unsigned int axial_types[] =
		{GO_GRADIENT_S_TO_N_MIRRORED, GO_GRADIENT_SE_TO_NW_MIRRORED,
		 GO_GRADIENT_E_TO_W_MIRRORED, GO_GRADIENT_NE_TO_SW_MIRRORED,
		 GO_GRADIENT_N_TO_S_MIRRORED, GO_GRADIENT_NW_TO_SE_MIRRORED,
		 GO_GRADIENT_W_TO_E_MIRRORED, GO_GRADIENT_SW_TO_NE_MIRRORED};
	unsigned int linear_types[] =
		{GO_GRADIENT_S_TO_N, GO_GRADIENT_SE_TO_NW,
		 GO_GRADIENT_E_TO_W, GO_GRADIENT_NE_TO_SW,
		 GO_GRADIENT_N_TO_S, GO_GRADIENT_NW_TO_SE,
		 GO_GRADIENT_W_TO_E, GO_GRADIENT_SW_TO_NE};

	info->brightness = -1.;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_DRAW, "name"))
			name = CXML2C (attrs[1]);
		else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_DRAW, "start-color")) {
			GdkColor gdk_color;
			if (gdk_color_parse (CXML2C (attrs[1]), &gdk_color))
				info->from = GO_COLOR_FROM_GDK (gdk_color);
			else
				oo_warning (xin, _("Unable to parse gradient color: %s"), CXML2C (attrs[1]));
		} else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_DRAW, "end-color")) {
			GdkColor gdk_color;
			if (gdk_color_parse (CXML2C (attrs[1]), &gdk_color))
				info->to = GO_COLOR_FROM_GDK (gdk_color);
			else
				oo_warning (xin, _("Unable to parse gradient color: %s"), CXML2C (attrs[1]));
		} else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_DRAW, "style"))
			style = CXML2C (attrs[1]);
		else if (oo_attr_float (xin, attrs, OO_GNUM_NS_EXT,
					"brightness", &info->brightness));
		else if (NULL != oo_attr_angle (xin, attrs, OO_NS_DRAW, "angle", &angle));

	if (name != NULL) {
		if (angle < 0)
			angle += 360;
		angle = ((angle + 22)/45) % 8; /* angle is now 0,1,2,...,7*/

		if (style != NULL && 0 == strcmp (style, "axial"))
			info->dir = axial_types[angle];
		else /* linear */
			info->dir = linear_types[angle];

		g_hash_table_replace (state->chart.gradient_styles,
				      g_strdup (name), info);
	} else {
		oo_warning (xin, _("Unnamed gradient style encountered."));
		g_free (info);
	}
}

static void
oo_hatch (GsfXMLIn *xin, xmlChar const **attrs)
{
	OOParseState *state = (OOParseState *)xin->user_state;
	GOPattern *hatch = g_new (GOPattern, 1);
	char const *hatch_name = NULL;
	gnm_float distance = -1.0;
	int angle = 0;
	char const *style = NULL;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_DRAW, "color")) {
			GdkColor gdk_color;
			if (gdk_color_parse (CXML2C (attrs[1]), &gdk_color))
				hatch->fore = GO_COLOR_FROM_GDK (gdk_color);
			else
				oo_warning (xin, _("Unable to parse hatch color: %s"), CXML2C (attrs[1]));
		} else if (NULL != oo_attr_distance (xin, attrs, OO_NS_DRAW, "distance", &distance))
			;
		else if (NULL != oo_attr_angle (xin, attrs, OO_NS_DRAW, "rotation", &angle))
			;
		else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_DRAW, "name"))
			hatch_name = CXML2C (attrs[1]);
		else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_DRAW, "style"))
			style = CXML2C (attrs[1]);

	if (style == NULL)
		hatch->pattern = GO_PATTERN_THATCH;
	else if (0 == strcmp (style, "single")) {
		while (angle < 0)
			angle = angle + 180;
		angle = (angle + 22) / 45;
		switch (angle) {
		case 0:
			hatch->pattern = (distance < 2.5) ? GO_PATTERN_HORIZ : GO_PATTERN_THIN_HORIZ;
			break;
		case 1:
			hatch->pattern = (distance < 2.5) ? GO_PATTERN_DIAG : GO_PATTERN_THIN_DIAG;
			break;
		case 2:
			hatch->pattern = (distance < 2.5) ? GO_PATTERN_VERT : GO_PATTERN_THIN_VERT;
			break;
		default:
			hatch->pattern = (distance < 2.5) ? GO_PATTERN_REV_DIAG : GO_PATTERN_THIN_REV_DIAG;
			break;
		}
	} else  if (0 == strcmp (style, "double")) {
		if (angle < 0)
			angle = - angle;
		angle = (angle + 22) / 45;
		angle = angle & 2;
		switch ((int)(distance + 0.5)) {
		case 0:
		case 1:
			hatch->pattern = (angle == 0) ? GO_PATTERN_GREY75 : GO_PATTERN_THICK_DIAG_CROSS;
			break;
		case 2:
			hatch->pattern = (angle == 0) ? GO_PATTERN_GREY50 : GO_PATTERN_DIAG_CROSS;
			break;
		case 3:
			hatch->pattern = (angle == 0) ? GO_PATTERN_THIN_HORIZ_CROSS : GO_PATTERN_THIN_DIAG_CROSS;
			break;
		case 4:
			hatch->pattern = GO_PATTERN_GREY125;
			break;
		default:
			hatch->pattern = GO_PATTERN_GREY625;
			break;
		}
		hatch->pattern = GO_PATTERN_THATCH;
	} else  if (0 == strcmp (style, "triple")) {
		while (angle < 0)
			angle += 180;
		angle = angle % 180;
		angle = (angle + 22)/45;
		switch (angle) {
		case 0:
			hatch->pattern = (distance < 2.5) ? GO_PATTERN_SMALL_CIRCLES : GO_PATTERN_LARGE_CIRCLES;
			break;
		case 1:
			hatch->pattern = (distance < 2.5) ? GO_PATTERN_SEMI_CIRCLES : GO_PATTERN_BRICKS;
			break;
		default:
			hatch->pattern = GO_PATTERN_THATCH;
			break;
		}
	}

	if (hatch_name == NULL) {
		g_free (hatch);
		oo_warning (xin, _("Unnamed hatch encountered!"));
	} else
		g_hash_table_replace (state->chart.hatches,
				      g_strdup (hatch_name), hatch);

}

static void
oo_style (GsfXMLIn *xin, xmlChar const **attrs)
{
	static OOEnum const style_types [] = {
		{ "table-cell",	  OO_STYLE_CELL },
		{ "table-row",	  OO_STYLE_ROW },
		{ "table-column", OO_STYLE_COL },
		{ "table",	  OO_STYLE_SHEET },
		{ "graphics",	  OO_STYLE_GRAPHICS },
		{ "paragraph",	  OO_STYLE_PARAGRAPH },
		{ "text",	  OO_STYLE_TEXT },
		{ "chart",	  OO_STYLE_CHART },
		{ "graphic",	  OO_STYLE_GRAPHICS },
		{ NULL,	0 },
	};

	OOParseState *state = (OOParseState *)xin->user_state;
	char const *name = NULL;
	char const *parent_name = NULL;
	GnmStyle *style;
	GOFormat *fmt = NULL;
	int tmp;
	OOChartStyle *cur_style;

	g_return_if_fail (state->cur_style.type == OO_STYLE_UNKNOWN);

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (oo_attr_enum (xin, attrs, OO_NS_STYLE, "family", style_types, &tmp))
			state->cur_style.type = tmp;
		else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_STYLE, "name"))
			name = CXML2C (attrs[1]);
		else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_STYLE, "parent-style-name"))
			parent_name = CXML2C (attrs[1]);
		else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_STYLE, "data-style-name")) {
			GOFormat *tmp = g_hash_table_lookup (state->formats, attrs[1]);
			if (tmp != NULL)
				fmt = tmp;
		}

	switch (state->cur_style.type) {
	case OO_STYLE_CELL:
		style = (parent_name != NULL)
			? g_hash_table_lookup (state->styles.cell, parent_name)
			: NULL;
		state->cur_style.cells = (style != NULL)
			? gnm_style_dup (style) : gnm_style_new ();
		gnm_style_ref (state->cur_style.cells); /* We now have 2 references */
		state->h_align_is_valid = state->repeat_content = FALSE;
		state->text_align = -2;
		state->gnm_halign = -2;

		if (fmt != NULL)
			gnm_style_set_format (state->cur_style.cells, fmt);

		if (name != NULL) {
			g_hash_table_replace (state->styles.cell,
				g_strdup (name), state->cur_style.cells);
			/* one reference left for state->cur_style.cells */
		} else if (0 == strcmp (xin->node->id, "DEFAULT_STYLE")) {
			 if (state->default_style.cells)
				 gnm_style_unref (state->default_style.cells);
			 state->default_style.cells = state->cur_style.cells;
			 /* one reference left for state->cur_style.cells */
		} else {
			gnm_style_unref (state->cur_style.cells);
			/* one reference left for state->cur_style.cells */
		}
		
		break;

	case OO_STYLE_COL:
		state->cur_style.col_rows = g_new0 (OOColRowStyle, 1);
		state->cur_style.col_rows->size_pts = -1.;
		if (name)
			g_hash_table_replace (state->styles.col,
				g_strdup (name), state->cur_style.col_rows);
		else if (0 == strcmp (xin->node->id, "DEFAULT_STYLE")) {
			if (state->default_style.columns) {
				oo_warning (xin, _("Duplicate default column style encountered."));
				g_free (state->default_style.columns);
			}
			state->default_style.columns = state->cur_style.col_rows;
		} else
			state->cur_style.requires_disposal = TRUE;
		break;

	case OO_STYLE_ROW:
		state->cur_style.col_rows = g_new0 (OOColRowStyle, 1);
		state->cur_style.col_rows->size_pts = -1.;
		if (name)
			g_hash_table_replace (state->styles.row,
				g_strdup (name), state->cur_style.col_rows);
		else if (0 == strcmp (xin->node->id, "DEFAULT_STYLE")) {
			if (state->default_style.rows) {
				oo_warning (xin, _("Duplicate default row style encountered."));
				g_free (state->default_style.rows);
			}
			state->default_style.rows = state->cur_style.col_rows;
		} else
			state->cur_style.requires_disposal = TRUE;
		break;

	case OO_STYLE_SHEET:
		state->cur_style.sheets = g_new0 (OOSheetStyle, 1);
		if (name)
			g_hash_table_replace (state->styles.sheet,
				g_strdup (name), state->cur_style.sheets);
		else
			state->cur_style.requires_disposal = TRUE;
		break;

	case OO_STYLE_CHART:
	case OO_STYLE_GRAPHICS:
		state->chart.plot_type = OO_PLOT_UNKNOWN;
		if (name != NULL){
			cur_style = g_new0(OOChartStyle, 1);
			cur_style->axis_props = NULL;
			cur_style->plot_props = NULL;
			cur_style->style_props = NULL;
			cur_style->other_props = NULL;
			state->chart.cur_graph_style = cur_style;
			g_hash_table_replace (state->chart.graph_styles,
					      g_strdup (name),
					      state->chart.cur_graph_style);
		} else {
			state->chart.cur_graph_style = NULL;
		}
		break;
	default:
		break;
	}
}

static void
oo_style_end (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	OOParseState *state = (OOParseState *)xin->user_state;

	switch (state->cur_style.type) {
	case OO_STYLE_CELL :
		if (state->cur_style.cells != NULL) {
			gnm_style_unref (state->cur_style.cells);
			state->cur_style.cells = NULL;
		}
		break;
	case OO_STYLE_COL :
	case OO_STYLE_ROW :
		if (state->cur_style.requires_disposal)
			g_free (state->cur_style.col_rows);
		state->cur_style.col_rows = NULL;
		break;
	case OO_STYLE_SHEET :
		if (state->cur_style.requires_disposal)
			g_free (state->cur_style.sheets);
		state->cur_style.sheets = NULL;
		break;
	case OO_STYLE_CHART :
	case OO_STYLE_GRAPHICS :
		state->chart.cur_graph_style = NULL;
		break;

	default :
		break;
	}
	state->cur_style.type = OO_STYLE_UNKNOWN;
	state->cur_style.requires_disposal = FALSE;
}

static void
oo_date_day (GsfXMLIn *xin, xmlChar const **attrs)
{
	OOParseState *state = (OOParseState *)xin->user_state;
	gboolean is_short = TRUE;

	if (state->cur_format.accum == NULL)
		return;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_NUMBER, "style"))
			is_short = (attr_eq (attrs[1], "short"));

	g_string_append (state->cur_format.accum, is_short ? "d" : "dd");
}

static void
oo_date_month (GsfXMLIn *xin, xmlChar const **attrs)
{
	OOParseState *state = (OOParseState *)xin->user_state;
	gboolean as_text = FALSE;
	gboolean is_short = TRUE;

	if (state->cur_format.accum == NULL)
		return;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_NUMBER, "style"))
			is_short = attr_eq (attrs[1], "short");
		else if (oo_attr_bool (xin, attrs, OO_NS_NUMBER, "textual", &as_text))
			;
	g_string_append (state->cur_format.accum, as_text
			 ? (is_short ? "mmm" : "mmmm")
			 : (is_short ? "m" : "mm"));
}
static void
oo_date_year (GsfXMLIn *xin, xmlChar const **attrs)
{
	OOParseState *state = (OOParseState *)xin->user_state;
	gboolean is_short = TRUE;

	if (state->cur_format.accum == NULL)
		return;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_NUMBER, "style"))
			is_short = attr_eq (attrs[1], "short");
	g_string_append (state->cur_format.accum, is_short ? "yy" : "yyyy");
}
static void
oo_date_era (GsfXMLIn *xin, xmlChar const **attrs)
{
}
static void
oo_date_day_of_week (GsfXMLIn *xin, xmlChar const **attrs)
{
	OOParseState *state = (OOParseState *)xin->user_state;
	gboolean is_short = TRUE;

	if (state->cur_format.accum == NULL)
		return;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_NUMBER, "style"))
			is_short = attr_eq (attrs[1], "short");
	g_string_append (state->cur_format.accum, is_short ? "ddd" : "dddd");
}
static void
oo_date_week_of_year (GsfXMLIn *xin, xmlChar const **attrs)
{
}
static void
oo_date_quarter (GsfXMLIn *xin, xmlChar const **attrs)
{
}
static void
oo_date_hours (GsfXMLIn *xin, xmlChar const **attrs)
{
	OOParseState *state = (OOParseState *)xin->user_state;
	gboolean is_short = TRUE;
	gboolean truncate_hour_on_overflow = TRUE;
	gboolean truncate_hour_on_overflow_set = FALSE;

	if (state->cur_format.accum == NULL)
		return;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_NUMBER, "style"))
			is_short = attr_eq (attrs[1], "short");
		else if (oo_attr_bool (xin, attrs, OO_GNUM_NS_EXT,
				       "truncate-on-overflow",
				       &truncate_hour_on_overflow))
			truncate_hour_on_overflow_set = TRUE;

	if (truncate_hour_on_overflow_set) {
		if (truncate_hour_on_overflow)
			g_string_append (state->cur_format.accum, is_short ? "h" : "hh");
		else {
			g_string_append (state->cur_format.accum, is_short ? "[h]" : "[hh]");
			state->cur_format.elapsed_set |= ODF_ELAPSED_SET_HOURS;
		}
	} else {
		if (state->cur_format.truncate_hour_on_overflow)
			g_string_append (state->cur_format.accum, is_short ? "h" : "hh");
		else {
			g_string_append (state->cur_format.accum, is_short ? "[h]" : "[hh]");
			state->cur_format.elapsed_set |= ODF_ELAPSED_SET_HOURS;
		}
	}
}

static void
oo_date_minutes (GsfXMLIn *xin, xmlChar const **attrs)
{
	OOParseState *state = (OOParseState *)xin->user_state;
	gboolean is_short = TRUE;
	gboolean truncate_hour_on_overflow = TRUE;
	gboolean truncate_hour_on_overflow_set = FALSE;

	if (state->cur_format.accum == NULL)
		return;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_NUMBER, "style"))
			is_short = attr_eq (attrs[1], "short");
		else if (oo_attr_bool (xin, attrs, OO_GNUM_NS_EXT,
				       "truncate-on-overflow",
				       &truncate_hour_on_overflow))
			truncate_hour_on_overflow_set = TRUE;
	state->cur_format.pos_minutes = state->cur_format.accum->len;

	if (truncate_hour_on_overflow_set) {
		if (truncate_hour_on_overflow)
			g_string_append (state->cur_format.accum, is_short ? "m" : "mm");
		else {
			g_string_append (state->cur_format.accum, is_short ? "[m]" : "[mm]");
			state->cur_format.elapsed_set |= ODF_ELAPSED_SET_MINUTES;
		}
	} else {
		if (state->cur_format.truncate_hour_on_overflow ||
		    0 != (state->cur_format.elapsed_set & ODF_ELAPSED_SET_HOURS))
			g_string_append (state->cur_format.accum, is_short ? "m" : "mm");
		else {
			g_string_append (state->cur_format.accum, is_short ? "[m]" : "[mm]");
			state->cur_format.elapsed_set |= ODF_ELAPSED_SET_MINUTES;
		}
	}
}

#define OO_DATE_SECONDS_PRINT_SECONDS	{				\
		g_string_append (state->cur_format.accum,		\
				 is_short ? "s" : "ss");		\
		if (digits > 0) {					\
			g_string_append_c (state->cur_format.accum,	\
					   '.');			\
			odf_go_string_append_c_n			\
				(state->cur_format.accum, '0', digits);	\
		}							\
	}


static void
oo_date_seconds (GsfXMLIn *xin, xmlChar const **attrs)
{
	OOParseState *state = (OOParseState *)xin->user_state;
	gboolean is_short = TRUE;
	int digits = 0;
	gboolean truncate_hour_on_overflow = TRUE;
	gboolean truncate_hour_on_overflow_set = FALSE;

	if (state->cur_format.accum == NULL)
		return;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_NUMBER, "style"))
			is_short = attr_eq (attrs[1], "short");
		else if (oo_attr_int_range (xin, attrs, OO_NS_NUMBER,
					      "decimal-places", &digits, 0, 9))
			;
		else if (oo_attr_bool (xin, attrs, OO_GNUM_NS_EXT,
				       "truncate-on-overflow",
				       &truncate_hour_on_overflow))
			truncate_hour_on_overflow_set = TRUE;

	state->cur_format.pos_seconds = state->cur_format.accum->len;

	if (truncate_hour_on_overflow_set) {
		if (truncate_hour_on_overflow) {
			OO_DATE_SECONDS_PRINT_SECONDS;
		} else {
			g_string_append_c (state->cur_format.accum, '[');
			OO_DATE_SECONDS_PRINT_SECONDS;
			g_string_append_c (state->cur_format.accum, ']');
			state->cur_format.elapsed_set |= ODF_ELAPSED_SET_SECONDS;
		}
	} else {
		if (state->cur_format.truncate_hour_on_overflow ||
		    0 != (state->cur_format.elapsed_set &
			  (ODF_ELAPSED_SET_HOURS | ODF_ELAPSED_SET_MINUTES))) {
			OO_DATE_SECONDS_PRINT_SECONDS;
		} else {
			g_string_append_c (state->cur_format.accum, '[');
			OO_DATE_SECONDS_PRINT_SECONDS;
			g_string_append_c (state->cur_format.accum, ']');
			state->cur_format.elapsed_set |= ODF_ELAPSED_SET_SECONDS;
		}
	}
}

#undef OO_DATE_SECONDS_PRINT_SECONDS

static void
oo_date_am_pm (GsfXMLIn *xin, xmlChar const **attrs)
{
	OOParseState *state = (OOParseState *)xin->user_state;
	if (state->cur_format.accum != NULL)
		g_string_append (state->cur_format.accum, "AM/PM");

}

static void
oo_date_text_end_append (GString *accum, char const *text, int n) {
	g_string_append_c (accum, '"');
	g_string_append_len (accum, text, n);
	g_string_append_c (accum, '"');
}

/* date_text_end is also used for non-date formats */
static void
oo_date_text_end (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	OOParseState *state = (OOParseState *)xin->user_state;

	if (state->cur_format.accum == NULL)
		return;

	if (xin->content->len == 1) {
		if (NULL != strchr (" /-(),",*xin->content->str)) {
			g_string_append_c (state->cur_format.accum, *xin->content->str);
			return;
		}
		if (state->cur_format.percentage && *xin->content->str == '%') {
			g_string_append_c (state->cur_format.accum, '%');
			state->cur_format.percent_sign_seen = TRUE;
			return;
		}
	}
	if (xin->content->len > 0) {
		if (state->cur_format.percentage) {
			int len = xin->content->len;
			char const *text = xin->content->str;
			char const *percent_sign;
			while ((percent_sign = strchr (xin->content->str, '%')) != NULL) {
				if (percent_sign > text) {
					oo_date_text_end_append
						(state->cur_format.accum, text,
						 percent_sign - text);
					len -= (percent_sign - text);
				}
				text = percent_sign + 1;
				len--;
				g_string_append_c (state->cur_format.accum, '%');
			}
			if (len > 0)
				oo_date_text_end_append	(state->cur_format.accum, text, len);
		} else
			oo_date_text_end_append	(state->cur_format.accum,
						 xin->content->str, xin->content->len);
	}
}

static void
oo_date_style (GsfXMLIn *xin, xmlChar const **attrs)
{
	OOParseState *state = (OOParseState *)xin->user_state;
	char const *name = NULL;
	int magic = GO_FORMAT_MAGIC_NONE;
	gboolean format_source_is_language = FALSE;
	gboolean truncate_hour_on_overflow = TRUE;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_STYLE, "name"))
			name = CXML2C (attrs[1]);
		else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_STYLE, "family") &&
			 !attr_eq (attrs[1], "data-style"))
			return;
		else if (oo_attr_int (xin, attrs, OO_GNUM_NS_EXT,
				      "format-magic", &magic))
			;
		else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_NUMBER, "format-source"))
			format_source_is_language = attr_eq (attrs[1], "language");
		else if (oo_attr_bool (xin, attrs, OO_NS_NUMBER,
				       "truncate-on-overflow", &truncate_hour_on_overflow));

	g_return_if_fail (state->cur_format.accum == NULL);

	/* We always save a magic number with source language, so if that is gone somebody may have changed formats */
	state->cur_format.magic = format_source_is_language ? magic : GO_FORMAT_MAGIC_NONE;
	state->cur_format.accum = (state->cur_format.magic == GO_FORMAT_MAGIC_NONE) ?  g_string_new (NULL) : NULL;
	state->cur_format.name = g_strdup (name);
	state->cur_format.percentage = FALSE;
	state->cur_format.truncate_hour_on_overflow = truncate_hour_on_overflow;
	state->cur_format.elapsed_set = 0;
	state->cur_format.pos_seconds = 0;
	state->cur_format.pos_minutes = 0;
}

static void
oo_date_style_end_rm_elapsed (GString *str, guint pos)
{
	guint end;
	g_return_if_fail (str->len > pos && str->str[pos] == '[');

	g_string_erase (str, pos, 1);
	end = strcspn (str->str + pos, "]");
	g_string_erase (str, pos + end, 1);
}

static void
oo_date_style_end (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	OOParseState *state = (OOParseState *)xin->user_state;
	int elapsed = state->cur_format.elapsed_set;

	if (state->cur_format.name == NULL) {
		if (state->cur_format.accum) {
			g_string_free (state->cur_format.accum, TRUE);
			state->cur_format.accum = NULL;
		}
		oo_warning (xin, _("Unnamed date style ignored."));
	} else {
		if (state->cur_format.magic != GO_FORMAT_MAGIC_NONE)
			g_hash_table_insert (state->formats, state->cur_format.name,
					     go_format_new_magic (state->cur_format.magic));
		else {
			g_return_if_fail (state->cur_format.accum != NULL);

			while (elapsed != 0 && elapsed != ODF_ELAPSED_SET_SECONDS
			       && elapsed != ODF_ELAPSED_SET_MINUTES
			       && elapsed != ODF_ELAPSED_SET_HOURS) {
				/*We need to fix the format string since several times are set as "elapsed". */
				if (0 != (elapsed & ODF_ELAPSED_SET_SECONDS)) {
					oo_date_style_end_rm_elapsed (state->cur_format.accum,
								      state->cur_format.pos_seconds);
					if (state->cur_format.pos_seconds < state->cur_format.pos_minutes)
						state->cur_format.pos_minutes -= 2;
					elapsed -= ODF_ELAPSED_SET_SECONDS;
				} else {
					oo_date_style_end_rm_elapsed (state->cur_format.accum,
								      state->cur_format.pos_minutes);
					elapsed -= ODF_ELAPSED_SET_MINUTES;
					break;
				}
			}

			g_hash_table_insert (state->formats, state->cur_format.name,
					     go_format_new_from_XL (state->cur_format.accum->str));
			g_string_free (state->cur_format.accum, TRUE);
		}
	}
	state->cur_format.accum = NULL;
	state->cur_format.name = NULL;
}

/*****************************************************************************************************/

static void
odf_fraction (GsfXMLIn *xin, xmlChar const **attrs)
{
	OOParseState *state = (OOParseState *)xin->user_state;
	gboolean grouping = FALSE;
	gboolean no_int_part = FALSE;
	gboolean denominator_fixed = FALSE;
	int denominator = 0;
	int min_d_digits = 0;
	int max_d_digits = 3;
	int min_i_digits = -1;
	int min_n_digits = 0;


	if (state->cur_format.accum == NULL)
		return;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (oo_attr_bool (xin, attrs, OO_NS_NUMBER, "grouping", &grouping)) {}
		else if (oo_attr_int (xin, attrs, OO_NS_NUMBER, "denominator-value", &denominator))
			denominator_fixed = TRUE;
		else if (oo_attr_int_range (xin, attrs, OO_NS_NUMBER,
					      "min-denominator-digits", &min_d_digits, 0, 30))
			;
		else if (oo_attr_int_range (xin, attrs, OO_GNUM_NS_EXT,
					      "max-denominator-digits", &max_d_digits, 0, 30))
			;
		else if (oo_attr_int_range (xin, attrs, OO_NS_NUMBER,
					      "min-integer-digits", &min_i_digits, 0, 30))
			;
		else if  (oo_attr_bool (xin, attrs, OO_GNUM_NS_EXT, "no-integer-part", &no_int_part)) {}
		else if  (oo_attr_int_range (xin, attrs, OO_NS_NUMBER,
					       "min-numerator-digits", &min_n_digits, 0, 30))
			;

	if (!no_int_part && (state->ver_odf < 1.2 || min_i_digits >= 0)) {
		g_string_append_c (state->cur_format.accum, '#');
		odf_go_string_append_c_n (state->cur_format.accum, '0',
					  min_i_digits > 0 ? min_i_digits : 0);
		g_string_append_c (state->cur_format.accum, ' ');
	}
	g_string_append_c (state->cur_format.accum, '?');
	odf_go_string_append_c_n (state->cur_format.accum, '0', min_n_digits);
	g_string_append_c (state->cur_format.accum, '/');
	if (denominator_fixed) {
		int denom = denominator;
		int count = 0;
		while (denom > 0) {
			denom /= 10;
			count ++;
		}
		min_d_digits -= count;
		odf_go_string_append_c_n (state->cur_format.accum, '0',
					  min_d_digits);
		g_string_append_printf (state->cur_format.accum, "%i", denominator);
	} else {
		max_d_digits -= min_d_digits;
		odf_go_string_append_c_n (state->cur_format.accum, '?',
					  max_d_digits);
		odf_go_string_append_c_n (state->cur_format.accum, '0',
					  min_d_digits);
	}
}

static void
odf_text_content (GsfXMLIn *xin, xmlChar const **attrs)
{
	OOParseState *state = (OOParseState *)xin->user_state;
	g_string_append_c (state->cur_format.accum, '@');
}

static void
odf_number (GsfXMLIn *xin, xmlChar const **attrs)
{
	OOParseState *state = (OOParseState *)xin->user_state;
	gboolean grouping = FALSE;
	int decimal_places = 0;
	gboolean decimal_places_specified = FALSE;
/* 	gnm_float display_factor = 1.; */
	int min_i_digits = 1;

	if (state->cur_format.accum == NULL)
		return;

	/* We are ignoring number:decimal-replacement */

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (oo_attr_bool (xin, attrs, OO_NS_NUMBER, "grouping", &grouping)) {}
		else if (oo_attr_int_range (xin, attrs, OO_NS_NUMBER, "decimal-places", &decimal_places, 0, 30)) {
			decimal_places_specified = TRUE;
		} /* else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_NUMBER,  */
/* 					       "display-factor")) */
/* 			display_factor = gnm_strto (CXML2C (attrs[1]), NULL); */
		else if (oo_attr_int_range (xin, attrs, OO_NS_NUMBER,
					      "min-integer-digits", &min_i_digits, 0, 30))
			;

	if (decimal_places_specified)
		go_format_generate_number_str (state->cur_format.accum,  min_i_digits, decimal_places,
					       grouping, FALSE, FALSE, NULL, NULL);
	else
		g_string_append (state->cur_format.accum, go_format_as_XL (go_format_general ()));
}

static void
odf_scientific (GsfXMLIn *xin, xmlChar const **attrs)
{
	OOParseState *state = (OOParseState *)xin->user_state;
	GOFormatDetails *details;
	gboolean engineering = FALSE;
/* 	int min_exp_digits = 1; */

	if (state->cur_format.accum == NULL)
		return;

	details = go_format_details_new (GO_FORMAT_SCIENTIFIC);

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (oo_attr_bool (xin, attrs, OO_NS_NUMBER, "grouping", &details->thousands_sep)) {}
		else if (oo_attr_int_range (xin, attrs, OO_NS_NUMBER, "decimal-places",
					      &details->num_decimals, 0, 30))
		        ;
		else if (oo_attr_int_range (xin, attrs, OO_NS_NUMBER, "min-integer-digits",
					      &details->min_digits, 0, 30))
			;
/* 		else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_NUMBER,  */
/* 					     "min-exponent-digits")) */
/* 			min_exp_digits = atoi (CXML2C (attrs[1])); */
		else if (oo_attr_bool (xin, attrs, OO_GNUM_NS_EXT, "engineering",
				       &engineering));
	if (engineering)
		details->exponent_step = 3;
	go_format_generate_str (state->cur_format.accum, details);

	go_format_details_free (details);
}

static void
odf_currency_symbol_end (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	OOParseState *state = (OOParseState *)xin->user_state;

	if (state->cur_format.accum == NULL)
		return;
	if (0 == strcmp (xin->content->str, "$")) {
		g_string_append_c (state->cur_format.accum, '$');
		return;
	}
	g_string_append (state->cur_format.accum, "[$");
	go_string_append_gstring (state->cur_format.accum, xin->content);
	g_string_append_c (state->cur_format.accum, ']');
}


static void
odf_map (GsfXMLIn *xin, xmlChar const **attrs)
{
	OOParseState *state = (OOParseState *)xin->user_state;
	char const *condition = NULL;
	char const *style_name = NULL;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_STYLE, "condition"))
			condition = attrs[1];
		else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_STYLE, "apply-style-name"))
			style_name = attrs[1];

	if (condition != NULL && style_name != NULL && g_str_has_prefix (condition, "value()")) {
		condition += 7;
		while (*condition == ' ') condition++;
		if (*condition == '>' || *condition == '<' || *condition == '=') {
			state->conditions = g_slist_prepend (state->conditions, g_strdup (condition));
			state->cond_formats = g_slist_prepend (state->cond_formats,
							       g_strdup (style_name));
			return;
		}
	}
}

static inline gboolean
attr_eq_ncase (xmlChar const *a, char const *s, int n)
{
	return !g_ascii_strncasecmp (CXML2C (a), s, n);
}

static void
odf_number_color (GsfXMLIn *xin, xmlChar const **attrs)
{
	OOParseState *state = (OOParseState *)xin->user_state;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_FO, "color")){
			char const *color = NULL;
			if (attr_eq_ncase (attrs[1], "#ff0000", 7))
				color = "[Red]";
			else if (attr_eq_ncase (attrs[1], "#000000", 7))
				color = "[Black]";
			else if (attr_eq_ncase (attrs[1], "#0000ff", 7))
				color = "[Blue]";
			else if (attr_eq_ncase (attrs[1], "#00ffff", 7))
				color = "[Cyan]";
			else if (attr_eq_ncase (attrs[1], "#00ff00", 7))
				color = "[Green]";
			else if (attr_eq_ncase (attrs[1], "#ff00ff", 7))
				color = "[Magenta]";
			else if (attr_eq_ncase (attrs[1], "#ffffff", 7))
				color = "[White]";
			else if (attr_eq_ncase (attrs[1], "#ffff00", 7))
				color = "[Yellow]";
			if (color != NULL)
				g_string_append (state->cur_format.accum, color);
		}
}

static void
odf_number_style (GsfXMLIn *xin, xmlChar const **attrs)
{
	OOParseState *state = (OOParseState *)xin->user_state;
	char const *name = NULL;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_STYLE, "name"))
			name = CXML2C (attrs[1]);

	g_return_if_fail (state->cur_format.accum == NULL);

	state->cur_format.accum = g_string_new (NULL);
	state->cur_format.name = g_strdup (name);
	state->cur_format.percentage = FALSE;
	state->cur_format.percent_sign_seen = FALSE;
	state->conditions = NULL;
	state->cond_formats = NULL;
}


static void
odf_number_percentage_style (GsfXMLIn *xin, xmlChar const **attrs)
{
	OOParseState *state = (OOParseState *)xin->user_state;

	odf_number_style (xin, attrs);
	state->cur_format.percentage = TRUE;
}

static void
odf_number_style_end (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	OOParseState *state = (OOParseState *)xin->user_state;

	g_return_if_fail (state->cur_format.accum != NULL);

	if (state->cur_format.percentage && !state->cur_format.percent_sign_seen)
		g_string_append_c (state->cur_format.accum, '%');
	state->cur_format.percentage = FALSE;

	if (state->cur_format.name == NULL) {
		g_string_free (state->cur_format.accum, TRUE);
		state->cur_format.accum = NULL;
		oo_warning (xin, _("Corrupted file: unnamed number style ignored."));
		return;
	}

	if (state->conditions != NULL) {
		/* We have conditional formats */
		GSList *lc, *lf;
		char *accum;
		int parts = 0;

		accum = g_string_free (state->cur_format.accum, FALSE);
		if (strlen (accum) == 0) {
			g_free (accum);
			accum = NULL;
		}
		state->cur_format.accum = g_string_new (NULL);

		lc = state->conditions;
		lf = state->cond_formats;
		while (lc && lf) {
			char *cond = lc->data;
			if (cond != NULL && *cond == '>') {
				GOFormat const *fmt;
				char *val = cond + strcspn (cond, "0123456789.");
				if ((*(cond+1) != '=') || (strtod (val, NULL) != 0.))
					g_string_append_printf
						(state->cur_format.accum,
						 (*(cond+1) == '=') ? "[>=%s]" : "[>%s]", val);
				fmt = g_hash_table_lookup (state->formats, lf->data);
				if (fmt != NULL)
					g_string_append (state->cur_format.accum, go_format_as_XL (fmt));
				else {
					g_string_append (state->cur_format.accum, "\"\"");
					oo_warning (xin, _("This file appears corrupted, required "
							   "formats are missing."));
				}
				parts++;
				g_free (lc->data);
				lc->data = NULL;
				break;
			}
			lc = lc->next;
			lf = lf->next;
		}

		if (parts == 0) {
			lc = state->conditions;
			lf = state->cond_formats;
			while (lc && lf) {
				char *cond = lc->data;
				if (cond != NULL && *cond == '=') {
					GOFormat const *fmt;
					char *val = cond + strcspn (cond, "0123456789.");
					g_string_append_printf (state->cur_format.accum, "[=%s]", val);
					fmt = g_hash_table_lookup (state->formats, lf->data);
					if (fmt != NULL)
						g_string_append (state->cur_format.accum,
								 go_format_as_XL (fmt));
					else {
						g_string_append (state->cur_format.accum, "\"\"");
						oo_warning (xin, _("This file appears corrupted, required "
								   "formats are missing."));
					}
					parts++;
					g_free (lc->data);
					lc->data = NULL;
					break;
				}
				lc = lc->next;
				lf = lf->next;
			}
		}

		if (parts == 0) {
			lc = state->conditions;
			lf = state->cond_formats;
			while (lc && lf) {
				char *cond = lc->data;
				if (cond != NULL && *cond == '<' && *(cond + 1) == '>') {
					GOFormat const *fmt;
					char *val = cond + strcspn (cond, "0123456789.");
					g_string_append_printf (state->cur_format.accum, "[<>%s]", val);
					fmt = g_hash_table_lookup (state->formats, lf->data);
					if (fmt != NULL)
						g_string_append (state->cur_format.accum,
								 go_format_as_XL (fmt));
					else {
						g_string_append (state->cur_format.accum, "\"\"");
						oo_warning (xin, _("This file appears corrupted, required "
								   "formats are missing."));
					}
					parts++;
					g_free (lc->data);
					lc->data = NULL;
					break;
				}
				lc = lc->next;
				lf = lf->next;
			}
		}

		if ((parts == 0) && (accum != NULL)) {
			g_string_append (state->cur_format.accum, accum);
			parts++;
		}

		lc = state->conditions;
		lf = state->cond_formats;
		while (lc && lf) {
			char *cond = lc->data;
			if (cond != NULL && *cond == '<' && *(cond + 1) != '>') {
				GOFormat const *fmt;
				char *val = cond + strcspn (cond, "0123456789.");
				if (parts > 0)
					g_string_append_c (state->cur_format.accum, ';');
				if ((*(cond+1) != '=') || (strtod (val, NULL) != 0.))
					g_string_append_printf
						(state->cur_format.accum,
						 (*(cond+1) == '=') ? "[<=%s]" : "[<%s]", val);
				fmt = g_hash_table_lookup (state->formats, lf->data);
				if (fmt != NULL)
					g_string_append (state->cur_format.accum,
							 go_format_as_XL (fmt));
				else {
					g_string_append (state->cur_format.accum, "\"\"");
					oo_warning (xin, _("This file appears corrupted, required "
							   "formats are missing."));
				}
				parts++;
			}
			lc = lc->next;
			lf = lf->next;
		}

		if (parts < 2) {
			lc = state->conditions;
			lf = state->cond_formats;
			while (lc && lf) {
				char *cond = lc->data;
				if (cond != NULL && *cond == '=') {
					GOFormat const *fmt;
					char *val = cond + strcspn (cond, "0123456789.");
					if (parts > 0)
						g_string_append_c (state->cur_format.accum, ';');
					g_string_append_printf (state->cur_format.accum, "[=%s]", val);
					fmt = g_hash_table_lookup (state->formats, lf->data);
					if (fmt != NULL)
						g_string_append (state->cur_format.accum,
								 go_format_as_XL (fmt));
					else {
						g_string_append (state->cur_format.accum, "\"\"");
						oo_warning (xin, _("This file appears corrupted, required "
								   "formats are missing."));
					}
					parts++;
					break;
				}
				lc = lc->next;
				lf = lf->next;
			}
		}

		if (parts < 2) {
			lc = state->conditions;
			lf = state->cond_formats;
			while (lc && lf) {
				GOFormat const *fmt;
				char *cond = lc->data;
				if (cond != NULL && *cond == '<' && *(cond + 1) == '>') {
					char *val = cond + strcspn (cond, "0123456789.");
					if (parts > 0)
						g_string_append_c (state->cur_format.accum, ';');
					g_string_append_printf (state->cur_format.accum, "[<>%s]", val);
					fmt = g_hash_table_lookup (state->formats, lf->data);
					if (fmt != NULL)
						g_string_append (state->cur_format.accum,
								 go_format_as_XL (fmt));
					else {
						g_string_append (state->cur_format.accum, "\"\"");
						oo_warning (xin, _("This file appears corrupted, required "
								   "formats are missing."));
					}
					parts++;
					break;
				}
				lc = lc->next;
				lf = lf->next;
			}
		}
		if (accum != NULL) {
			if (parts > 0)
				g_string_append_c (state->cur_format.accum, ';');
			g_string_append (state->cur_format.accum, accum);
			g_free (accum);
		}
	}

	g_hash_table_insert (state->formats, state->cur_format.name,
			     go_format_new_from_XL (state->cur_format.accum->str));
	g_string_free (state->cur_format.accum, TRUE);
	state->cur_format.accum = NULL;
	state->cur_format.name = NULL;
	go_slist_free_custom (state->conditions, g_free);
	state->conditions = NULL;
	go_slist_free_custom (state->cond_formats, g_free);
	state->cond_formats = NULL;
}

/*****************************************************************************************************/

static void
oo_set_gnm_border  (GsfXMLIn *xin, GnmStyle *style,
		    xmlChar const *str, GnmStyleElement location)
{
	GnmStyleBorderType border_style;
	GnmBorder   *old_border, *new_border;
	GnmStyleBorderLocation const loc =
		GNM_STYLE_BORDER_TOP + (int)(location - MSTYLE_BORDER_TOP);

	if (!strcmp ((char const *)str, "hair"))
		border_style = GNM_STYLE_BORDER_HAIR;
	else if (!strcmp ((char const *)str, "medium-dash"))
		border_style = GNM_STYLE_BORDER_MEDIUM_DASH;
	else if (!strcmp ((char const *)str, "dash-dot"))
		border_style = GNM_STYLE_BORDER_DASH_DOT;
	else if (!strcmp ((char const *)str, "medium-dash-dot"))
		border_style = GNM_STYLE_BORDER_MEDIUM_DASH_DOT;
	else if (!strcmp ((char const *)str, "dash-dot-dot"))
		border_style = GNM_STYLE_BORDER_DASH_DOT_DOT;
	else if (!strcmp ((char const *)str, "medium-dash-dot-dot"))
		border_style = GNM_STYLE_BORDER_MEDIUM_DASH_DOT_DOT;
	else if (!strcmp ((char const *)str, "slanted-dash-dot"))
		border_style = GNM_STYLE_BORDER_SLANTED_DASH_DOT;
	else return;

	old_border = gnm_style_get_border (style, location);
	new_border = gnm_style_border_fetch (border_style,
					     style_color_ref(old_border->color),
					     gnm_style_border_get_orientation (loc));
	gnm_style_set_border (style, location, new_border);
}

static void
oo_parse_border (GsfXMLIn *xin, GnmStyle *style,
		 xmlChar const *str, GnmStyleElement location)
{
	double pts;
	char const *end = oo_parse_distance (xin, str, "border", &pts);
	GnmBorder *border = NULL;
	GnmColor *color = NULL;
	const char *border_color = NULL;
	GnmStyleBorderType border_style;
	GnmStyleBorderLocation const loc =
		GNM_STYLE_BORDER_TOP + (int)(location - MSTYLE_BORDER_TOP);

	if (end == NULL || end == CXML2C (str))
		return;
	while (*end == ' ')
		end++;
	/* "0.035cm solid #000000" */
	border_color = strchr (end, '#');
	if (border_color) {
		char *border_type = g_strndup (end, border_color - end);
		color = oo_parse_color (xin, CC2XML (border_color), "color");

		if (g_str_has_prefix (border_type, "none")||
		    g_str_has_prefix (border_type, "hidden"))
			border_style = GNM_STYLE_BORDER_NONE;
		else if (g_str_has_prefix (border_type, "solid") ||
			 g_str_has_prefix (border_type, "groove") ||
			 g_str_has_prefix (border_type, "ridge") ||
			 g_str_has_prefix (border_type, "inset") ||
			 g_str_has_prefix (border_type, "outset")) {
			if (pts <= OD_BORDER_THIN)
				border_style = GNM_STYLE_BORDER_THIN;
			else if (pts <= OD_BORDER_MEDIUM)
				border_style = GNM_STYLE_BORDER_MEDIUM;
			else
				border_style = GNM_STYLE_BORDER_THICK;
		} else if (g_str_has_prefix (border_type, "dashed"))
			border_style = GNM_STYLE_BORDER_DASHED;
		else if (g_str_has_prefix (border_type, "dotted"))
			border_style = GNM_STYLE_BORDER_DOTTED;
		else
			border_style = GNM_STYLE_BORDER_DOUBLE;

		border = gnm_style_border_fetch (border_style, color,
						 gnm_style_border_get_orientation (loc));
		border->width = pts;
		gnm_style_set_border (style, location, border);
		g_free (border_type);
	}
}

static void
odf_style_set_align_h (GnmStyle *style, gboolean h_align_is_valid, gboolean repeat_content,
		       int text_align, int gnm_halign)
{
	int alignment = HALIGN_GENERAL;
	if (h_align_is_valid)
		alignment = repeat_content ? HALIGN_FILL
			: ((text_align < 0) ? ((gnm_halign > -1) ? gnm_halign : HALIGN_LEFT)
			   : text_align);

	gnm_style_set_align_h (style, alignment);
}

static void
oo_style_prop_cell (GsfXMLIn *xin, xmlChar const **attrs)
{
	static OOEnum const text_line_through_styles [] = {
		{ "none",	0 },
		{ "dash",	1 },
		{ "dot-dash",	1 },
		{ "dot-dot-dash", 1 },
		{ "dotted",	1 },
		{ "long-dash",	1 },
		{ "solid",	1 },
		{ "wave",	1 },
		{ NULL,	0 },
	};
	static OOEnum const text_line_through_types [] = {
		{ "none",	0 },
		{ "single",	1 },
		{ "double",	1 },
		{ NULL,	0 },
	};
	static OOEnum const h_alignments [] = {
		{ "start",	-1 },            /* see below, we may have a gnm:GnmHAlign attribute */
		{ "left",	HALIGN_LEFT },
		{ "center",	HALIGN_CENTER },
		{ "end",	HALIGN_RIGHT },   /* This really depends on the text direction */
		{ "right",	HALIGN_RIGHT },
		{ "justify",	HALIGN_JUSTIFY },
		{ "automatic",	HALIGN_GENERAL },
		{ NULL,	0 },
	};
	static OOEnum const v_alignments [] = {
		{ "bottom",	VALIGN_BOTTOM },
		{ "top",	VALIGN_TOP },
		{ "middle",	VALIGN_CENTER },
		{ "automatic",	-1 },            /* see below, we may have a gnm:GnmVAlign attribute */
		{ NULL,	0 },
	};
	static OOEnum const protections [] = {
		{ "none",			0 },
		{ "hidden-and-protected",	1 | 2 },
		{ "protected",			    2 },
		{ "formula-hidden",		1 },
		{ "protected formula-hidden",	1 | 2 },
		{ "formula-hidden protected",	1 | 2 },
		{ NULL,	0 },
	};
	OOParseState *state = (OOParseState *)xin->user_state;
	GnmColor *color;
	GnmStyle *style = state->cur_style.cells;
	gboolean  btmp;
	int	  tmp;
	gnm_float tmp_f;
	gboolean  v_alignment_is_fixed = FALSE;
	int  strike_through_type = 0, strike_through_style = 0;

	g_return_if_fail (style != NULL);

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if ((color = oo_attr_color (xin, attrs, OO_NS_FO, "background-color"))) {
			gnm_style_set_back_color (style, color);
			if (color == magic_transparent)
				gnm_style_set_pattern (style, 0);
			else
				gnm_style_set_pattern (style, 1);
		} else if ((color = oo_attr_color (xin, attrs, OO_NS_FO, "color")))
			gnm_style_set_font_color (style, color);
		else if (oo_attr_enum (xin, attrs, OO_NS_STYLE, "cell-protect", protections, &tmp)) {
			gnm_style_set_contents_locked (style, (tmp & 2) != 0);
			gnm_style_set_contents_hidden (style, (tmp & 1) != 0);
		} else if (oo_attr_enum (xin, attrs,
				       (state->ver >= OOO_VER_OPENDOC) ? OO_NS_FO : OO_NS_STYLE,
					 "text-align", h_alignments, &(state->text_align)))
			/* Note that style:text-align-source, style:text_align, style:repeat-content  */
			/* and gnm:GnmHAlign interact but can appear in any order and arrive from different */
			/* elements, so we can't use local variables                                  */
			odf_style_set_align_h (style, state->h_align_is_valid, state->repeat_content,
					       state->text_align, state->gnm_halign);
		else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_STYLE, "text-align-source")) {
			state->h_align_is_valid = attr_eq (attrs[1], "fix");
			odf_style_set_align_h (style, state->h_align_is_valid, state->repeat_content,
					       state->text_align, state->gnm_halign);
		} else if (oo_attr_bool (xin, attrs, OO_NS_STYLE, "repeat-content", &(state->repeat_content)))
			odf_style_set_align_h (style, state->h_align_is_valid, state->repeat_content,
					       state->text_align, state->gnm_halign);
		else if (oo_attr_int (xin,attrs, OO_GNUM_NS_EXT, "GnmHAlign", &(state->gnm_halign)))
			odf_style_set_align_h (style, state->h_align_is_valid, state->repeat_content,
					       state->text_align, state->gnm_halign);
		else if (oo_attr_enum (xin, attrs,
				       (state->ver >= OOO_VER_OPENDOC) ? OO_NS_STYLE : OO_NS_FO,
				       "vertical-align", v_alignments, &tmp)) {
			if (tmp != -1) {
				gnm_style_set_align_v (style, tmp);
				v_alignment_is_fixed = TRUE;
			} else if (!v_alignment_is_fixed)
                                /* This should depend on the rotation */
				gnm_style_set_align_v (style, VALIGN_BOTTOM);
		} else if (oo_attr_int (xin,attrs, OO_GNUM_NS_EXT, "GnmVAlign", &tmp)) {
			if (!v_alignment_is_fixed) {
				gnm_style_set_align_v (style, tmp);
				v_alignment_is_fixed = TRUE;
			}
		} else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_FO, "wrap-option"))
			gnm_style_set_wrap_text (style, attr_eq (attrs[1], "wrap"));
		else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_FO, "border-bottom"))
			oo_parse_border (xin, style, attrs[1], MSTYLE_BORDER_BOTTOM);
		else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_FO, "border-left"))
			oo_parse_border (xin, style, attrs[1], MSTYLE_BORDER_LEFT);
		else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_FO, "border-right"))
			oo_parse_border (xin, style, attrs[1], MSTYLE_BORDER_RIGHT);
		else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_FO, "border-top"))
			oo_parse_border (xin, style, attrs[1], MSTYLE_BORDER_TOP);
		else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_FO, "border")) {
			oo_parse_border (xin, style, attrs[1], MSTYLE_BORDER_BOTTOM);
			oo_parse_border (xin, style, attrs[1], MSTYLE_BORDER_LEFT);
			oo_parse_border (xin, style, attrs[1], MSTYLE_BORDER_RIGHT);
			oo_parse_border (xin, style, attrs[1], MSTYLE_BORDER_TOP);
		} else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_STYLE, "diagonal-bl-tr"))
			oo_parse_border (xin, style, attrs[1], MSTYLE_BORDER_DIAGONAL);
		else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_STYLE, "diagonal-tl-br"))
			oo_parse_border (xin, style, attrs[1], MSTYLE_BORDER_REV_DIAGONAL);
		else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_GNUM_NS_EXT, "border-line-style-bottom"))
			oo_set_gnm_border (xin, style, attrs[1], MSTYLE_BORDER_BOTTOM);
		else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_GNUM_NS_EXT, "border-line-style-top"))
			oo_set_gnm_border (xin, style, attrs[1], MSTYLE_BORDER_TOP);
		else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_GNUM_NS_EXT, "border-line-style-left"))
			oo_set_gnm_border (xin, style, attrs[1], MSTYLE_BORDER_LEFT);
		else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_GNUM_NS_EXT, "border-line-style-right"))
			oo_set_gnm_border (xin, style, attrs[1], MSTYLE_BORDER_RIGHT);
		else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_GNUM_NS_EXT, "diagonal-bl-tr-line-style"))
			oo_set_gnm_border (xin, style, attrs[1], MSTYLE_BORDER_DIAGONAL);
		else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_GNUM_NS_EXT, "diagonal-tl-br-line-style"))
			oo_set_gnm_border (xin, style, attrs[1], MSTYLE_BORDER_REV_DIAGONAL);
		else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_STYLE, "font-name"))
			/* According to the ODF standards, this name is just a reference to a */
			/* <style:font-face> element. So this may not be an acceptable font name! */
			gnm_style_set_font_name (style, CXML2C (attrs[1]));
		else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_FO, "font-family"))
			gnm_style_set_font_name (style, CXML2C (attrs[1]));
		else if (oo_attr_distance (xin, attrs, OO_NS_FO, "font-size", &tmp_f))
			gnm_style_set_font_size (style, tmp_f);
		else if (oo_attr_bool (xin, attrs, OO_NS_STYLE, "shrink-to-fit", &btmp))
			gnm_style_set_shrink_to_fit (style, btmp);
		else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_FO, "direction"))
			gnm_style_set_text_dir (style, attr_eq (attrs[1], "rtl") ? GNM_TEXT_DIR_RTL : GNM_TEXT_DIR_LTR);
		else if (oo_attr_int (xin, attrs, OO_NS_STYLE, "rotation-angle", &tmp)) {
			tmp = tmp % 360;
			gnm_style_set_rotation	(style, tmp);
		} else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_STYLE, "text-underline-type") ||
			   gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_STYLE, "text-underline")) {
			if (attr_eq (attrs[1], "none"))
				gnm_style_set_font_uline (style, UNDERLINE_NONE);
			else if (attr_eq (attrs[1], "single"))
				gnm_style_set_font_uline (style, UNDERLINE_SINGLE);
			else if (attr_eq (attrs[1], "double"))
				gnm_style_set_font_uline (style, UNDERLINE_DOUBLE);
		} else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_FO, "font-style"))
			gnm_style_set_font_italic (style, attr_eq (attrs[1], "italic"));
		else if (oo_attr_font_weight (xin, attrs, &tmp))
			gnm_style_set_font_bold (style, tmp >= PANGO_WEIGHT_SEMIBOLD);
		else if (oo_attr_enum (xin, attrs, OO_NS_STYLE, "text-line-through-style",
				       text_line_through_styles, &strike_through_type));
		else if (oo_attr_enum (xin, attrs, OO_NS_STYLE, "text-line-through-type",
				       text_line_through_types, &strike_through_style));

#if 0
		else if (!strcmp (attrs[0], OO_NS_FO, "font-weight")) {
				gnm_style_set_font_bold (style, TRUE);
				gnm_style_set_font_uline (style, TRUE);
			="normal"
		} else if (!strcmp (attrs[0], OO_NS_STYLE, "text-underline" )) {
			="italic"
				gnm_style_set_font_italic (style, TRUE);
		}
#endif

	gnm_style_set_font_strike (style, strike_through_type == 1 && strike_through_style == 1);

}

static OOPageBreakType
oo_page_break_type (GsfXMLIn *xin, xmlChar const *attr)
{
	/* Note that truly automatic of soft page breaks are stored */
	/* via text:soft-page-break tags                            */
	if (!strcmp (attr, "page"))
		return OO_PAGE_BREAK_MANUAL;
	if (!strcmp (attr, "column"))
		return OO_PAGE_BREAK_MANUAL;
	if (!strcmp (attr, "auto"))
		return OO_PAGE_BREAK_NONE;
	oo_warning (xin,
		_("Unknown break type '%s' defaulting to NONE"), attr);
	return OO_PAGE_BREAK_NONE;
}

static void
oo_style_prop_col_row (GsfXMLIn *xin, xmlChar const **attrs)
{
	OOParseState *state = (OOParseState *)xin->user_state;
	char const * const size_tag = (state->cur_style.type == OO_STYLE_COL)
		? "column-width" :  "row-height";
	char const * const use_optimal = (state->cur_style.type == OO_STYLE_COL)
		? "use-optimal-column-width" : "use-optimal-row-height";
	gnm_float pts;
	gboolean auto_size;

	g_return_if_fail (state->cur_style.col_rows != NULL);

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (NULL != oo_attr_distance (xin, attrs, OO_NS_STYLE, size_tag, &pts))
			state->cur_style.col_rows->size_pts = pts;
		else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_FO, "break-before"))
			state->cur_style.col_rows->break_before =
				oo_page_break_type (xin, attrs[1]);
		else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_FO, "break-after"))
			state->cur_style.col_rows->break_after =
				oo_page_break_type (xin, attrs[1]);
		else if (oo_attr_bool (xin, attrs, OO_NS_STYLE, use_optimal, &auto_size))
			state->cur_style.col_rows->manual = !auto_size;
}

static void
oo_style_prop_table (GsfXMLIn *xin, xmlChar const **attrs)
{
	static OOEnum const modes [] = {
		{ "lr-tb",	0 },
		{ "rl-tb",	1 },
		{ "tb-rl",	1 },
		{ "tb-lr",	0 },
		{ "lr",		0 },
		{ "rl",		1 },
		{ "tb",		0 },	/* what do tb and page imply in this context ? */
		{ "page",	0 },
		{ NULL,	0 },
	};
	OOParseState *state = (OOParseState *)xin->user_state;
	OOSheetStyle *style = state->cur_style.sheets;
	gboolean tmp_i;
	int tmp_b;

	g_return_if_fail (style != NULL);

	style->visibility = GNM_SHEET_VISIBILITY_VISIBLE;
	style->is_rtl  = FALSE;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (oo_attr_bool (xin, attrs, OO_NS_TABLE, "display", &tmp_b)) {
			if (!tmp_b)
				style->visibility = GNM_SHEET_VISIBILITY_HIDDEN;
		} else if (oo_attr_enum (xin, attrs, OO_NS_STYLE, "writing-mode", modes, &tmp_i))
			style->is_rtl = tmp_i;
		else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]),
					       OO_GNUM_NS_EXT, "tab-color")) {
			GdkColor gdk_color;
			if (gdk_color_parse (CXML2C (attrs[1]), &gdk_color)) {
				style->tab_color
					= GO_COLOR_FROM_GDK (gdk_color);
				style->tab_color_set = TRUE;
			} else
				oo_warning (xin, _("Unable to parse "
						   "tab color \'%s\'"),
					    CXML2C (attrs[1]));
		} else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]),
					       OO_GNUM_NS_EXT,
					       "tab-text-color")) {
			GdkColor gdk_color;
			if (gdk_color_parse (CXML2C (attrs[1]), &gdk_color)) {
				style->tab_text_color
					= GO_COLOR_FROM_GDK (gdk_color);
				style->tab_text_color_set = TRUE;
			} else
				oo_warning (xin, _("Unable to parse tab "
						   "text color \'%s\'"),
					    CXML2C (attrs[1]));
		}

}

static gboolean
odf_style_map_load_two_values (GsfXMLIn *xin, char *condition, GnmStyleCond *cond)
{
	OOParseState *state = (OOParseState *)xin->user_state;

	condition = g_strstrip (condition);
	if (*(condition++) == '(') {
		guint len = strlen (condition);
		char *end = condition + len - 1;
		if (*end == ')') {
			GnmParsePos   pp;

			parse_pos_init (&pp, state->pos.wb, NULL, 0, 0);
			len -= 1;
			*end = '\0';
			while (1) {
				gchar * try = g_strrstr_len (condition, len, ",");
				GnmExprTop const *texpr;

				if (try == NULL || try == condition) return FALSE;

				texpr = oo_expr_parse_str
					(xin, try + 1, &pp,
					 GNM_EXPR_PARSE_FORCE_EXPLICIT_SHEET_REFERENCES,
					 FORMULA_OPENFORMULA);
				if (texpr != NULL) {
					cond->texpr[1] = texpr;
					*try = '\0';
					break;
				}
				len = try - condition - 1;
			}
			cond->texpr[0] = oo_expr_parse_str
				(xin, condition, &pp,
				 GNM_EXPR_PARSE_FORCE_EXPLICIT_SHEET_REFERENCES,
				 FORMULA_OPENFORMULA);
			return ((cond->texpr[0] != NULL) && (cond->texpr[1] != NULL));
		}
	}
	return FALSE;
}

static gboolean
odf_style_map_load_one_value (GsfXMLIn *xin, char *condition, GnmStyleCond *cond)
{
	OOParseState *state = (OOParseState *)xin->user_state;
	GnmParsePos   pp;

	parse_pos_init (&pp, state->pos.wb, NULL, 0, 0);
	cond->texpr[0] = oo_expr_parse_str
		(xin, condition, &pp,
		 GNM_EXPR_PARSE_FORCE_EXPLICIT_SHEET_REFERENCES,
		 FORMULA_OPENFORMULA);
	return (cond->texpr[0] != NULL);
}


static void
oo_style_map (GsfXMLIn *xin, xmlChar const **attrs)
{
	OOParseState *state = (OOParseState *)xin->user_state;
	char const *style_name = NULL;
	char const *condition = NULL, *full_condition;
	GnmStyle *style = NULL;
	GnmStyleCond cond;
	GnmStyleConditions *sc;
	gboolean success = FALSE;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_STYLE, "condition"))
			condition = attrs[1];
		else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_STYLE, "apply-style-name"))
			style_name = attrs[1];
		else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_STYLE, "base-cell-address"))
			;
	if (style_name == NULL || condition == NULL)
		return;

	style = g_hash_table_lookup (state->styles.cell, style_name);

	g_return_if_fail (style != NULL);
	g_return_if_fail (state->cur_style.cells != NULL);

	full_condition = condition;
	cond.texpr[0] = NULL;
	cond.texpr[1] = NULL;

	if (g_str_has_prefix (condition, "cell-content()")) {
		condition += strlen ("cell-content()") - 1;
		while (*(++condition) == ' ');
		switch (*(condition++)) {
		case '<':
			if (*condition == '=') {
				condition++;
				cond.op = GNM_STYLE_COND_LTE;
			} else
				cond.op = GNM_STYLE_COND_LT;
			success = TRUE;
			break;
		case '>':
			if (*condition == '=') {
				condition++;
				cond.op = GNM_STYLE_COND_GTE;
			} else
				cond.op = GNM_STYLE_COND_GT;
			success = TRUE;
			break;
			break;
		case '=':
			cond.op = GNM_STYLE_COND_EQUAL;
			success = TRUE;
			break;
		case '!':
			if (*condition == '=') {
				condition++;
				cond.op = GNM_STYLE_COND_NOT_EQUAL;
				success = TRUE;
			}
			break;
		default:
			break;
		}
		if (success) {
			char *text = g_strdup (condition);
			success = odf_style_map_load_one_value (xin, text, &cond);
			g_free (text);
		}

	} else if (g_str_has_prefix (condition, "cell-content-is-between")) {
		char *text;
		cond.op = GNM_STYLE_COND_BETWEEN;
		condition += strlen ("cell-content-is-between");
		text = g_strdup (condition);
		success = odf_style_map_load_two_values (xin, text, &cond);
		g_free (text);
	} else if (g_str_has_prefix (condition, "cell-content-is-not-between")) {
		char *text;
		cond.op = GNM_STYLE_COND_NOT_BETWEEN;
		condition += strlen ("cell-content-is-not-between");
		text = g_strdup (condition);
		success = odf_style_map_load_two_values (xin, text, &cond);
		g_free (text);
	} else if (g_str_has_prefix (condition, "is-true-formula")) {
		char *text;
		cond.op = GNM_STYLE_COND_CUSTOM;
		condition += strlen ("is-true-formula");
		text = g_strdup (condition);
		success = odf_style_map_load_one_value (xin, text, &cond);
		g_free (text);
	}

	if (!success)
	{
		if (cond.texpr[0] != NULL)
			gnm_expr_top_unref (cond.texpr[0]);
		if (cond.texpr[1] != NULL)
			gnm_expr_top_unref (cond.texpr[1]);
		oo_warning (xin,
			    _("Unknown condition '%s' encountered, ignoring."),
			    full_condition);
		return;
	}

	cond.overlay = style;
	gnm_style_ref (style);

	if (gnm_style_is_element_set (state->cur_style.cells, MSTYLE_CONDITIONS) &&
	    (sc = gnm_style_get_conditions (state->cur_style.cells)) != NULL)
		gnm_style_conditions_insert (sc, &cond, -1);
	else {
		sc = gnm_style_conditions_new ();
		gnm_style_conditions_insert (sc, &cond, -1);
		gnm_style_set_conditions (state->cur_style.cells, sc);
	}

}

static OOProp *
oo_prop_new_double (char const *name, gnm_float val)
{
	OOProp *res = g_new0 (OOProp, 1);
	res->name = name;
	g_value_init (&res->value, G_TYPE_DOUBLE);
	g_value_set_double (&res->value, val);
	return res;
}
static OOProp *
oo_prop_new_bool (char const *name, gboolean val)
{
	OOProp *res = g_new0 (OOProp, 1);
	res->name = name;
	g_value_init (&res->value, G_TYPE_BOOLEAN);
	g_value_set_boolean (&res->value, val);
	return res;
}
static OOProp *
oo_prop_new_int (char const *name, int val)
{
	OOProp *res = g_new0 (OOProp, 1);
	res->name = name;
	g_value_init (&res->value, G_TYPE_INT);
	g_value_set_int (&res->value, val);
	return res;
}
static OOProp *
oo_prop_new_string (char const *name, char const *val)
{
	OOProp *res = g_new0 (OOProp, 1);
	res->name = name;
	g_value_init (&res->value, G_TYPE_STRING);
	g_value_set_string (&res->value, val);
	return res;
}
static void
oo_prop_free (OOProp *prop)
{
	g_value_unset (&prop->value);
	g_free (prop);
}

static void
oo_prop_list_free (GSList *props)
{
	go_slist_free_custom (props, (GFreeFunc) oo_prop_free);
}

static void
oo_prop_list_apply (GSList *props, GObject *obj)
{
	GSList *ptr;
	OOProp *prop;
	GObjectClass *klass;

	if (NULL == obj)
		return;
	klass = G_OBJECT_GET_CLASS (obj);

	for (ptr = props; ptr; ptr = ptr->next) {
		prop = ptr->data;
		if (NULL != g_object_class_find_property (klass, prop->name))
			g_object_set_property (obj, prop->name, &prop->value);
	}
}

static void
oo_prop_list_apply_to_axis (GSList *props, GObject *obj)
{
	GSList *ptr;
	OOProp *prop;

	double minimum = go_ninf, maximum = go_pinf;

	oo_prop_list_apply (props, obj);

	for (ptr = props; ptr; ptr = ptr->next) {
		prop = ptr->data;
		if (0 == strcmp ("minimum", prop->name))
			minimum = g_value_get_double (&prop->value);
		else if (0 == strcmp ("maximum", prop->name))
			maximum = g_value_get_double (&prop->value);
	}

	gog_axis_set_bounds (GOG_AXIS (obj), minimum, maximum);
}

static void
oo_chart_style_to_series (GsfXMLIn *xin, OOChartStyle *oostyle, GObject *obj)
{
	GOStyle *style = NULL;

	if (oostyle == NULL)
		return;

	oo_prop_list_apply (oostyle->plot_props, obj);

	g_object_get (obj, "style", &style, NULL);
	if (style != NULL) {
		odf_apply_style_props (xin, oostyle->style_props, style);
		g_object_unref (G_OBJECT (style));
	}
}

static void
oo_prop_list_has (GSList *props, gboolean *threed, char const *tag)
{
	GSList *ptr;
	for (ptr = props; ptr; ptr = ptr->next) {
		OOProp *prop = ptr->data;
		if (0 == strcmp (prop->name, tag) &&
		    g_value_get_boolean (&prop->value))
			*threed = TRUE;
	}
}

static gboolean
oo_style_have_three_dimensional (OOChartStyle **style)
{
	int i;
	gboolean is_three_dimensional = FALSE;
	for (i = 0; i < OO_CHART_STYLE_INHERITANCE; i++)
		if (style[i] != NULL)
			oo_prop_list_has (style[i]->other_props,
					  &is_three_dimensional,
					  "three-dimensional");
	return is_three_dimensional;
}

static gboolean
oo_style_have_multi_series (OOChartStyle **style)
{
	int i;
	gboolean is_multi_series = FALSE;
	for (i = 0; i < OO_CHART_STYLE_INHERITANCE; i++)
		if (style[i] != NULL)
			oo_prop_list_has (style[i]->other_props,
					  &is_multi_series,
					  "multi-series");
	return is_multi_series;
}

static void
od_style_prop_chart (GsfXMLIn *xin, xmlChar const **attrs)
{
	static OOEnum const symbol_type [] = {
		{"automatic"   , OO_SYMBOL_TYPE_AUTO},
		{"none"        , OO_SYMBOL_TYPE_NONE},
		{"named-symbol", OO_SYMBOL_TYPE_NAMED},
		{NULL          , 0},
	};
	static OOEnum const named_symbols [] = {
		{ "square", GO_MARKER_SQUARE},
		{ "diamond", GO_MARKER_DIAMOND},
		{ "arrow-down", GO_MARKER_TRIANGLE_DOWN},
		{ "arrow-up", GO_MARKER_TRIANGLE_UP},
		{ "arrow-right", GO_MARKER_TRIANGLE_RIGHT},
		{ "arrow-left", GO_MARKER_TRIANGLE_LEFT},
		{ "circle", GO_MARKER_CIRCLE},
		{ "x", GO_MARKER_X},
		{ "plus", GO_MARKER_CROSS},
		{ "asterisk", GO_MARKER_ASTERISK},
		{ "horizontal-bar", GO_MARKER_BAR},
		{ "bow-tie", GO_MARKER_BUTTERFLY},
		{ "hourglass", GO_MARKER_HOURGLASS},
		{ "star", GO_MARKER_LEFT_HALF_BAR},
		{ "vertical-bar", GO_MARKER_HALF_BAR},
		{ NULL, 0},
	};

	static  OOEnum const font_variants [] = {
		{"normal", PANGO_VARIANT_NORMAL},
		{"small-caps", PANGO_VARIANT_SMALL_CAPS},
		{ NULL, 0},
	};

	static  OOEnum const font_styles [] = {
		{ "normal", PANGO_STYLE_NORMAL},
		{ "oblique", PANGO_STYLE_OBLIQUE},
		{  "italic", PANGO_STYLE_ITALIC},
		{ NULL, 0},
	};

	static OOEnum const image_fill_types [] = {
		{"stretch", GO_IMAGE_STRETCHED },
		{"repeat", GO_IMAGE_WALLPAPER },
		{"no-repeat", GO_IMAGE_CENTERED },
		{ NULL,	0 },
	};

	OOParseState *state = (OOParseState *)xin->user_state;
	OOChartStyle *style = state->chart.cur_graph_style;
	gboolean btmp;
	int	  tmp;
	gnm_float ftmp;
	gboolean default_style_has_lines_set = FALSE;
	gboolean draw_stroke_set = FALSE;
	gboolean draw_stroke;

	g_return_if_fail (style != NULL ||
			  state->default_style.cells != NULL);

	if (style == NULL && state->default_style.cells != NULL) {
		style = g_new0 (OOChartStyle, 1);
	}


	style->grid = FALSE;
	style->src_in_rows = FALSE;
	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2) {
		if (oo_attr_bool (xin, attrs, OO_NS_CHART, "logarithmic", &btmp)) {
			if (btmp)
				style->axis_props = g_slist_prepend (style->axis_props,
					oo_prop_new_string ("map-name", "Log"));
		} else if (oo_attr_bool (xin, attrs, OO_NS_CHART, "vertical", &btmp)) {
			/* This is backwards from my intuition */
			style->plot_props = g_slist_prepend (style->plot_props,
				oo_prop_new_bool ("horizontal", btmp));
			/* This is for BoxPlots */
			style->plot_props = g_slist_prepend (style->plot_props,
				oo_prop_new_bool ("vertical", btmp));
		} else if (oo_attr_bool (xin, attrs, OO_GNUM_NS_EXT, "outliers", &btmp)) {
			style->plot_props = g_slist_prepend (style->plot_props,
				oo_prop_new_bool ("outliers", btmp));
		} else if (oo_attr_bool (xin, attrs, OO_GNUM_NS_EXT, "reverse-direction", &btmp)) {
			style->axis_props = g_slist_prepend (style->axis_props,
				oo_prop_new_bool ("invert-axis", btmp));
		} else if (oo_attr_bool (xin, attrs, OO_NS_CHART,
					 "reverse-direction", &btmp)) {
			style->axis_props = g_slist_prepend (style->axis_props,
				oo_prop_new_bool ("invert-axis", btmp));
		} else if (oo_attr_bool (xin, attrs, OO_GNUM_NS_EXT,
					 "vary-style-by-element", &btmp)) {
			style->plot_props = g_slist_prepend (style->plot_props,
				oo_prop_new_bool ("vary-style-by-element",
						  btmp));
		} else if (oo_attr_bool (xin, attrs, OO_GNUM_NS_EXT,
					 "show-negatives", &btmp)) {
			style->plot_props = g_slist_prepend (style->plot_props,
				oo_prop_new_bool ("show-negatives", btmp));
		} else if (oo_attr_float (xin, attrs, OO_NS_CHART,
					  "minimum", &ftmp)) {
			style->axis_props = g_slist_prepend (style->axis_props,
				oo_prop_new_double ("minimum", ftmp));
		} else if (oo_attr_float (xin, attrs, OO_NS_CHART,
					  "maximum", &ftmp)) {
			style->axis_props = g_slist_prepend (style->axis_props,
				oo_prop_new_double ("maximum", ftmp));
		} else if (oo_attr_float (xin, attrs, OO_GNUM_NS_EXT,
					  "radius-ratio", &ftmp)) {
			style->plot_props = g_slist_prepend (style->plot_props,
				oo_prop_new_double ("radius-ratio", ftmp));
		} else if (oo_attr_percent (xin, attrs, OO_GNUM_NS_EXT,
					    "default-separation", &ftmp)) {
			style->plot_props = g_slist_prepend (style->plot_props,
				oo_prop_new_double ("default-separation", ftmp));
		} else if (oo_attr_int_range (xin, attrs, OO_NS_CHART,
					      "pie-offset", &tmp, 0, 500)) {
			style->plot_props = g_slist_prepend (style->plot_props,
				oo_prop_new_double ("default-separation",
						   tmp/100.));
			style->plot_props = g_slist_prepend (style->plot_props,
				oo_prop_new_double ("separation",
						   tmp/100.));
		} else if (oo_attr_percent (xin, attrs, OO_NS_CHART,
					    "hole-size", &ftmp)) {
			style->plot_props = g_slist_prepend (style->plot_props,
				oo_prop_new_double ("center-size", ftmp));
		} else if (oo_attr_bool (xin, attrs, OO_NS_CHART,
					 "reverse-direction", &btmp)) {
			style->axis_props = g_slist_prepend (style->axis_props,
				oo_prop_new_bool ("invert-axis", btmp));
		} else if (oo_attr_bool (xin, attrs, OO_NS_CHART, "stacked",
					 &btmp)) {
			if (btmp)
				style->plot_props = g_slist_prepend
					(style->plot_props,
					 oo_prop_new_string ("type",
							     "stacked"));
		} else if (oo_attr_bool (xin, attrs, OO_NS_CHART, "percentage",
					 &btmp)) {
			if (btmp)
				style->plot_props = g_slist_prepend
					(style->plot_props,
					oo_prop_new_string ("type",
							    "as_percentage"));
		} else if (oo_attr_int_range (xin, attrs, OO_NS_CHART,
					      "overlap", &tmp, -150, 150)) {
			style->plot_props = g_slist_prepend (style->plot_props,
				oo_prop_new_int ("overlap-percentage", tmp));
		} else if (oo_attr_int_range (xin, attrs, OO_NS_CHART,
						"gap-width", &tmp, 0, 500))
			style->plot_props = g_slist_prepend (style->plot_props,
				oo_prop_new_int ("gap-percentage", tmp));
		else if (oo_attr_enum (xin, attrs, OO_NS_CHART, "symbol-type",
				       symbol_type, &tmp)) {
			style->plot_props = g_slist_prepend
				(style->plot_props,
				 oo_prop_new_bool ("default-style-has-markers",
						   tmp != OO_SYMBOL_TYPE_NONE));
			style->style_props = g_slist_prepend
				(style->style_props,
				 oo_prop_new_int ("symbol-type", tmp));
		} else if (oo_attr_enum (xin, attrs, OO_NS_CHART,
					 "symbol-name",
					 named_symbols, &tmp)) {
			style->style_props = g_slist_prepend
				(style->style_props,
				 oo_prop_new_int ("symbol-name", tmp));
		} else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]),
					       OO_NS_CHART, "interpolation")) {
			char const *interpolation = NULL;

			if (attr_eq (attrs[1], "none"))
				interpolation = "linear";
			else if (attr_eq (attrs[1], "b-spline"))
				interpolation = "spline";
			else if (attr_eq (attrs[1], "cubic-spline"))
				interpolation = "cspline";
			else if (g_str_has_prefix (CXML2C(attrs[1]), "gnm:"))
				interpolation = CXML2C(attrs[1]) + 4;
			else oo_warning
				     (xin, _("Unknown interpolation type "
					     "encountered: %s"),
				      CXML2C(attrs[1]));
			if (interpolation != NULL)
				style->plot_props = g_slist_prepend
					(style->plot_props,
					 oo_prop_new_string
					 ("interpolation", interpolation));
		} else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]),
					       OO_NS_DRAW, "stroke")) {
			draw_stroke = !attr_eq (attrs[1], "none");
			draw_stroke_set = TRUE;
			style->style_props = g_slist_prepend
				(style->style_props,
				 oo_prop_new_string ("stroke",
						     CXML2C(attrs[1])));
		} else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]),
					       OO_NS_DRAW, "stroke-dash")) {
			style->style_props = g_slist_prepend
				(style->style_props,
				 oo_prop_new_string ("stroke-dash",
						     CXML2C(attrs[1])));
		} else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]),
					       OO_NS_SVG, "stroke-color")) {
			style->style_props = g_slist_prepend
				(style->style_props,
				 oo_prop_new_string ("stroke-color",
						     CXML2C(attrs[1])));
		} else if (NULL != oo_attr_distance (xin, attrs, OO_NS_SVG,
						     "stroke-width", &ftmp))
			style->style_props = g_slist_prepend
				(style->style_props,
				 oo_prop_new_double ("stroke-width",
						    ftmp));
		else if (oo_attr_bool (xin, attrs, OO_NS_CHART, "lines", &btmp)) {
			style->style_props = g_slist_prepend
				(style->style_props,
				 oo_prop_new_bool ("lines", btmp));
			style->plot_props = g_slist_prepend
				(style->plot_props,
				 oo_prop_new_bool ("default-style-has-lines", btmp));
			default_style_has_lines_set = TRUE;
		} else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_CHART, "series-source"))
			style->src_in_rows = attr_eq (attrs[1], "rows");
		else if (oo_attr_bool (xin, attrs, OO_NS_CHART, "three-dimensional", &btmp))
			style->other_props = g_slist_prepend (style->other_props,
				oo_prop_new_bool ("three-dimensional", btmp));
		else if (oo_attr_bool (xin, attrs, OO_GNUM_NS_EXT, "multi-series", &btmp))
			style->other_props = g_slist_prepend (style->other_props,
				oo_prop_new_bool ("multi-series", btmp));
		else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_DRAW, "fill"))
			style->style_props = g_slist_prepend
				(style->style_props,
				 oo_prop_new_string ("fill",
						     CXML2C(attrs[1])));
		else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_DRAW, "fill-color"))
			style->style_props = g_slist_prepend
				(style->style_props,
				 oo_prop_new_string ("fill-color",
						     CXML2C(attrs[1])));
		else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_DRAW, "fill-hatch-name"))
			style->style_props = g_slist_prepend
				(style->style_props,
				 oo_prop_new_string ("fill-hatch-name",
						     CXML2C(attrs[1])));
		else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_DRAW, "fill-image-name"))
			style->style_props = g_slist_prepend
				(style->style_props,
				 oo_prop_new_string ("fill-image-name",
						     CXML2C(attrs[1])));
		else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_DRAW, "fill-gradient-name"))
			style->style_props = g_slist_prepend
				(style->style_props,
				 oo_prop_new_string ("fill-gradient-name",
						     CXML2C(attrs[1])));
		else if (oo_attr_bool (xin, attrs, OO_NS_CHART, "fill-hatch-solid", &btmp))
			style->other_props = g_slist_prepend (style->other_props,
				oo_prop_new_bool ("fill-hatch-solid", btmp));
		else if (oo_attr_int_range (xin, attrs, OO_GNUM_NS_EXT,
					      "pattern", &tmp,
					      GO_PATTERN_GREY75, GO_PATTERN_MAX - 1))
			style->style_props = g_slist_prepend
				(style->style_props,
				 oo_prop_new_int ("gnm-pattern", tmp));
		else if (oo_attr_angle (xin, attrs, OO_NS_STYLE,
					"text-rotation-angle", &tmp)) {
			style->style_props = g_slist_prepend
				(style->style_props,
				 oo_prop_new_int ("text-rotation-angle", tmp));
		} else if (oo_attr_angle (xin, attrs, OO_NS_STYLE,
					  "rotation-angle", &tmp)) {
			style->style_props = g_slist_prepend
				(style->style_props,
				 oo_prop_new_int ("text-rotation-angle", tmp));
			style->plot_props = g_slist_prepend
				(style->plot_props,
				 oo_prop_new_int ("rotation-angle", tmp));
		} else if (NULL != oo_attr_distance (xin, attrs, OO_NS_FO, "font-size", &ftmp))
			style->style_props = g_slist_prepend
				(style->style_props,
				 oo_prop_new_double ("font-size", ftmp));
		else if (oo_attr_font_weight (xin, attrs, &tmp))
			style->style_props = g_slist_prepend
				(style->style_props,
				 oo_prop_new_int ("font-weight", tmp));
		else if (oo_attr_enum (xin, attrs, OO_NS_FO, "font-variant",
					 font_variants, &tmp))
			style->style_props = g_slist_prepend
				(style->style_props,
				 oo_prop_new_int ("font-variant", tmp));
		else if (oo_attr_enum (xin, attrs, OO_NS_FO, "font-style",
					 font_styles, &tmp))
			style->style_props = g_slist_prepend
				(style->style_props,
				 oo_prop_new_int ("font-style", tmp));
		else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_FO, "font-family"))
			style->style_props = g_slist_prepend
				(style->style_props,
				 oo_prop_new_string ("font-family",
						     CXML2C(attrs[1])));
		else if (oo_attr_int_range (xin, attrs, OO_GNUM_NS_EXT,
					      "font-stretch-pango", &tmp,
					      0, PANGO_STRETCH_ULTRA_EXPANDED))
			style->style_props = g_slist_prepend
				(style->style_props,
				 oo_prop_new_int ("font-stretch-pango", tmp));
		else if (oo_attr_int_range (xin, attrs, OO_GNUM_NS_EXT,
					      "font-gravity-pango", &tmp,
					      0, PANGO_GRAVITY_WEST))
			style->style_props = g_slist_prepend
				(style->style_props,
				 oo_prop_new_int ("font-gravity-pango", tmp));
		else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_CHART,
					     "regression-type"))
			style->other_props = g_slist_prepend
				(style->other_props,
				 oo_prop_new_string ("regression-type",
						     CXML2C(attrs[1])));
		else if (oo_attr_int_range (xin, attrs, OO_GNUM_NS_EXT,
					      "regression-polynomial-dims", &tmp,
					      1, 100))
			style->other_props = g_slist_prepend
				(style->other_props,
				 oo_prop_new_int ("dims", tmp));
		else if (oo_attr_bool (xin, attrs, OO_GNUM_NS_EXT, "regression-affine",
				       &btmp))
			style->other_props = g_slist_prepend (style->other_props,
				oo_prop_new_bool ("regression-affine", btmp));
		else if (oo_attr_bool (xin, attrs, OO_GNUM_NS_EXT,
				       "is-position-manual",
				       &btmp))
			style->plot_props = g_slist_prepend
					(style->plot_props,
					 oo_prop_new_bool
					 ("is-position-manual", btmp));
		else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]),
					     OO_GNUM_NS_EXT,
					     "position"))
			style->plot_props = g_slist_prepend
					(style->plot_props,
					 oo_prop_new_string
					 ("position", CXML2C(attrs[1])));
		else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]),
					     OO_GNUM_NS_EXT,
					     "anchor"))
			style->plot_props = g_slist_prepend
					(style->plot_props,
					 oo_prop_new_string
					 ("anchor", CXML2C(attrs[1])));
		else if (oo_attr_enum (xin, attrs, OO_NS_STYLE, "repeat",
					 image_fill_types, &tmp))
			style->style_props = g_slist_prepend
				(style->style_props,
				 oo_prop_new_int ("repeat", tmp));
		else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]),
					     OO_NS_DRAW,
					     "marker-start"))
			style->other_props = g_slist_prepend
				(style->other_props,
				 oo_prop_new_string
				 ("marker-start", CXML2C(attrs[1])));
		else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]),
					     OO_NS_DRAW,
					     "marker-end"))
			style->other_props = g_slist_prepend
				(style->other_props,
				 oo_prop_new_string
				 ("marker-end", CXML2C(attrs[1])));

	}

	if (draw_stroke_set && !default_style_has_lines_set)
		style->plot_props = g_slist_prepend
			(style->plot_props,
			 oo_prop_new_bool ("default-style-has-lines", draw_stroke));

	if (state->chart.cur_graph_style == NULL && state->default_style.cells != NULL) {
		/* odf_apply_style_props (xin, style->style_props, state->default_style.cells);*/
		/* We should apply the styles to this GnmStyle */
		oo_chart_style_free (style);
	}
}

static void
oo_style_prop (GsfXMLIn *xin, xmlChar const **attrs)
{
	OOParseState *state = (OOParseState *)xin->user_state;
	switch (state->cur_style.type) {
	case OO_STYLE_CELL  : oo_style_prop_cell (xin, attrs); break;
	case OO_STYLE_COL   :
	case OO_STYLE_ROW   : oo_style_prop_col_row (xin, attrs); break;
	case OO_STYLE_SHEET : oo_style_prop_table (xin, attrs); break;
	case OO_STYLE_CHART :
	case OO_STYLE_GRAPHICS :
		od_style_prop_chart (xin, attrs); break;

	default :
		break;
	}
}

static void
oo_named_expr (GsfXMLIn *xin, xmlChar const **attrs)
{
	OOParseState *state = (OOParseState *)xin->user_state;
	char const *name      = NULL;
	char const *base_str  = NULL;
	char const *expr_str  = NULL;
	char const *scope  = NULL;
	char *range_str = NULL;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_TABLE, "name"))
			name = CXML2C (attrs[1]);
		else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_TABLE, "base-cell-address"))
			base_str = CXML2C (attrs[1]);
		else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_TABLE, "expression"))
			expr_str = CXML2C (attrs[1]);
		else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_TABLE, "cell-range-address"))
			expr_str = range_str = g_strconcat ("[", CXML2C (attrs[1]), "]", NULL);
		else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_GNUM_NS_EXT, "scope"))
			scope = CXML2C (attrs[1]);


	if (name != NULL && expr_str != NULL) {
		GnmParsePos   pp;
		GnmExprTop const *texpr;
		OOFormula f_type;

		parse_pos_init (&pp, state->pos.wb, NULL, 0, 0);

		/* Note that base_str is not required */
		if (base_str != NULL) {
			char *tmp = g_strconcat ("[", base_str, "]", NULL);

			texpr = oo_expr_parse_str
				(xin, tmp, &pp,
				 GNM_EXPR_PARSE_FORCE_EXPLICIT_SHEET_REFERENCES,
				 FORMULA_OPENFORMULA);
			g_free (tmp);

			if (texpr == NULL ||
			    GNM_EXPR_GET_OPER (texpr->expr)
			    != GNM_EXPR_OP_CELLREF) {
				oo_warning (xin, _("expression '%s' @ '%s' "
						   "is not a cellref"),
					    name, base_str);
			} else {
				GnmCellRef const *ref =
					&texpr->expr->cellref.ref;
				parse_pos_init (&pp, state->pos.wb, ref->sheet,
						ref->col, ref->row);
			}
			if (texpr != NULL)
				gnm_expr_top_unref (texpr);

		}

		f_type = odf_get_formula_type (xin, &expr_str);
		if (f_type == FORMULA_NOT_SUPPORTED) {
			oo_warning
				(xin, _("Expression '%s' has "
					"unknown namespace"),
				 expr_str);
		} else {

			/* Note that  an = sign is only required if a  */
			/* name space is given. */
			if (*expr_str == '=')
				expr_str++;

			texpr = oo_expr_parse_str (xin, expr_str,
						   &pp, GNM_EXPR_PARSE_DEFAULT,
						   f_type);
			if (texpr != NULL) {
				pp.sheet = state->pos.sheet;
				if (pp.sheet == NULL && scope != NULL)
					pp.sheet = workbook_sheet_by_name (pp.wb, scope);
				expr_name_add (&pp, name, texpr, NULL,
					       TRUE, NULL);
			}
		}
	}

	g_free (range_str);
}

static void
oo_db_range_start (GsfXMLIn *xin, xmlChar const **attrs)
{
	OOParseState *state = (OOParseState *)xin->user_state;
	gboolean buttons = TRUE;
	GnmRangeRef ref;
	GnmRange r;
	char const *name = NULL;
	GnmExpr const *expr = NULL;
	GnmParsePos   pp;

	g_return_if_fail (state->filter == NULL);

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_TABLE, "target-range-address")) {
			char const *ptr = oo_cellref_parse 
				(&ref.a, CXML2C (attrs[1]), &state->pos, NULL);
			if (ref.a.sheet != invalid_sheet &&
			    ':' == *ptr &&
			    '\0' == *oo_cellref_parse (&ref.b, ptr+1, &state->pos, NULL) &&
			    ref.b.sheet != invalid_sheet) {
				state->filter = gnm_filter_new 
					(ref.a.sheet, range_init_rangeref (&r, &ref));
				expr = gnm_expr_new_constant 
					(value_new_cellrange_r (ref.a.sheet, &r));
			} else
				oo_warning (xin, _("Invalid DB range '%s'"), attrs[1]);
		} else if (oo_attr_bool (xin, attrs, OO_NS_TABLE, "display-filter-buttons", &buttons))
			/* ignore this */;
		else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_TABLE, "name"))
			name = CXML2C (attrs[1]);

	/* It appears that OOo likes to use the names it assigned to filters as named-ranges */
	/* This really violates ODF/OpenFormula. So we make sure that there isn't already a named */
	/* expression or range with that name. */
	if (expr != NULL) {
		GnmNamedExpr *nexpr = NULL;
		if (name != NULL &&
		    (NULL == (nexpr = expr_name_lookup
			      (parse_pos_init (&pp, state->pos.wb, NULL, 0, 0), name)) ||
		     expr_name_is_placeholder (nexpr))) {
			GnmExprTop const *texpr = gnm_expr_top_new (expr);
			expr_name_add (&pp, name, texpr, NULL, TRUE, NULL);
		} else
			gnm_expr_free (expr);
	}
}

static void
oo_db_range_end (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	OOParseState *state = (OOParseState *)xin->user_state;

	if (state->filter != NULL) {
		gnm_filter_reapply (state->filter);
		state->filter = NULL;
	}
}

static void
oo_filter_cond (GsfXMLIn *xin, xmlChar const **attrs)
{
	static OOEnum const datatypes [] = {
		{ "text",	  VALUE_STRING },
		{ "number",	  VALUE_FLOAT },
		{ NULL,	0 },
	};
	static OOEnum const operators [] = {
		{ "=",			GNM_FILTER_OP_EQUAL },
		{ "!=",			GNM_FILTER_OP_NOT_EQUAL },
		{ "<",			GNM_FILTER_OP_LT },
		{ "<=",			GNM_FILTER_OP_LTE },
		{ ">",			GNM_FILTER_OP_GT },
		{ ">=",			GNM_FILTER_OP_GTE },

		{ "match",		GNM_FILTER_OP_MATCH },
		{ "!match",		GNM_FILTER_OP_NO_MATCH },
		{ "empty",		GNM_FILTER_OP_BLANKS },
		{ "!empty",		GNM_FILTER_OP_NON_BLANKS },
		{ "bottom percent",	GNM_FILTER_OP_BOTTOM_N_PERCENT },
		{ "bottom values",	GNM_FILTER_OP_BOTTOM_N },
		{ "top percent",	GNM_FILTER_OP_TOP_N_PERCENT },
		{ "top values",		GNM_FILTER_OP_TOP_N },

		{ NULL,	0 },
	};
	OOParseState *state = (OOParseState *)xin->user_state;
	int field_num = 0, type = -1, op = -1;
	char const *val_str = NULL;

	if (NULL == state->filter)
		return;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (oo_attr_int_range (xin, attrs, OO_NS_TABLE, "field-number", &field_num, 0, INT_MAX)) ;
		else if (oo_attr_enum (xin, attrs, OO_NS_TABLE, "data-type", datatypes, &type)) ;
		else if (oo_attr_enum (xin, attrs, OO_NS_TABLE, "operator", operators, &op)) ;
		else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_TABLE, "value"))
			val_str = CXML2C (attrs[1]);

	if (field_num >= 0 && op >= 0) {
		GnmFilterCondition *cond = NULL;
		GnmValue *v = NULL;

		if (type >= 0 && val_str != NULL)
			v = value_new_from_string (type, val_str, NULL, FALSE);

		switch (op) {
		case GNM_FILTER_OP_EQUAL:
		case GNM_FILTER_OP_NOT_EQUAL:
		case GNM_FILTER_OP_LT:
		case GNM_FILTER_OP_LTE:
		case GNM_FILTER_OP_GT:
		case GNM_FILTER_OP_GTE:
		case GNM_FILTER_OP_MATCH:
		case GNM_FILTER_OP_NO_MATCH:
			if (NULL != v) {
				cond = gnm_filter_condition_new_single (op, v);
				v = NULL;
			}
			break;

		case GNM_FILTER_OP_BLANKS:
			cond = gnm_filter_condition_new_single (
				GNM_FILTER_OP_BLANKS, NULL);
			break;
		case GNM_FILTER_OP_NON_BLANKS:
			cond = gnm_filter_condition_new_single (
				GNM_FILTER_OP_NON_BLANKS, NULL);
			break;

		case GNM_FILTER_OP_BOTTOM_N_PERCENT:
		case GNM_FILTER_OP_BOTTOM_N:
		case GNM_FILTER_OP_TOP_N_PERCENT:
		case GNM_FILTER_OP_TOP_N:
			if (v && VALUE_IS_NUMBER(v))
				cond = gnm_filter_condition_new_bucket (
					0 == (op & GNM_FILTER_OP_BOTTOM_MASK),
					0 == (op & GNM_FILTER_OP_PERCENT_MASK),
					value_get_as_float (v));
			break;
		}
		value_release (v);
		if (NULL != cond)
			gnm_filter_set_condition  (state->filter, field_num, cond, FALSE);
	}
}

static void
od_draw_frame_start (GsfXMLIn *xin, xmlChar const **attrs)
{
	/* Note that in ODF spreadsheet files svg:height and svg:width are */
	/* ignored. We only consider */
	/* table:end-x and table:end-y together with table:end-cell-address */

	OOParseState *state = (OOParseState *)xin->user_state;
	GnmRange cell_base;
	double frame_offset[4];
	gchar const *aux = NULL;
	gdouble height = 0., width = 0., x = 0., y = 0., end_x = 0., end_y = 0.;
	ColRowInfo const *col, *row;
	GnmExprTop const *texpr = NULL;

	height = width = x = y = 0.;
	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2){
		if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_SVG, "width"))
			aux = oo_parse_distance (xin, attrs[1], "width", &width);
		else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_SVG, "height"))
			aux = oo_parse_distance (xin, attrs[1], "height", &height);
		else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_SVG, "x"))
			aux = oo_parse_distance (xin, attrs[1], "x", &x);
		else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_SVG, "y"))
			aux = oo_parse_distance (xin, attrs[1], "y", &y);
		else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_TABLE, "end-x"))
			aux = oo_parse_distance (xin, attrs[1], "end-x", &end_x);
		else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_TABLE, "end-y"))
			aux = oo_parse_distance (xin, attrs[1], "end-y", &end_y);
		else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_TABLE, "end-cell-address")) {
			GnmParsePos   pp;
			char *end_str = g_strconcat ("[", CXML2C (attrs[1]), "]", NULL);
			parse_pos_init (&pp, state->pos.wb, NULL, 0, 0);
			texpr = oo_expr_parse_str (xin, end_str, &pp,
						   GNM_EXPR_PARSE_FORCE_EXPLICIT_SHEET_REFERENCES,
						   FORMULA_OPENFORMULA);
			g_free (end_str);
		}
	}

	cell_base.start.col = cell_base.end.col = state->pos.eval.col;
	cell_base.start.row = cell_base.end.row = state->pos.eval.row;

	col = sheet_col_get_info (state->pos.sheet, state->pos.eval.col);
	row = sheet_row_get_info (state->pos.sheet, state->pos.eval.row);

	frame_offset[0] = x;
	frame_offset[1] = y;

	if (texpr == NULL || (GNM_EXPR_GET_OPER (texpr->expr) != GNM_EXPR_OP_CELLREF)) {
		frame_offset[2] = x+width;
		frame_offset[3] = y+height;
	} else {
		GnmCellRef const *ref = &texpr->expr->cellref.ref;
		cell_base.end.col = ref->col;
		cell_base.end.row = ref->row;
		frame_offset[2] = end_x;
		frame_offset[3] = end_y ;
	}

	frame_offset[0] /= col->size_pts;
	frame_offset[1] /= row->size_pts;
	frame_offset[2] /= col->size_pts;
	frame_offset[3] /= row->size_pts;

	if (texpr)
		gnm_expr_top_unref (texpr);
	sheet_object_anchor_init (&state->chart.anchor, &cell_base, frame_offset,
				  GOD_ANCHOR_DIR_DOWN_RIGHT);
	state->chart.so = NULL;
}

static void
od_draw_frame_end (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	OOParseState *state = (OOParseState *)xin->user_state;

	if (state->chart.so != NULL) {
		sheet_object_set_anchor (state->chart.so, &state->chart.anchor);
		sheet_object_set_sheet (state->chart.so, state->pos.sheet);
		g_object_unref (state->chart.so);
		state->chart.so = NULL;
	}
}

static void
od_draw_control_start (GsfXMLIn *xin, xmlChar const **attrs)
{
	OOParseState *state = (OOParseState *)xin->user_state;
	char const *name = NULL;

	od_draw_frame_start (xin, attrs);

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_DRAW, "control"))
			name = CXML2C (attrs[1]);

	if (name != NULL) {
		OOControl *oc = g_hash_table_lookup (state->controls, name);
		GnmExprTop const *result_texpr = NULL;
		if (oc != NULL) {
			SheetObject *so = NULL;
			if (oc->t == sheet_widget_scrollbar_get_type () ||
			    oc->t == sheet_widget_spinbutton_get_type () ||
			    oc->t == sheet_widget_slider_get_type ()) {
				GtkAdjustment *adj;
				int min_real = (oc->min < oc->max) ? oc->min : oc->max;
				int max_real = (oc->min < oc->max) ? oc->max : oc->min;
				gnm_float value_real;

				if (oc->value != NULL) {
					char *end;
					value_real = gnm_strto (oc->value, &end);
					if (*end) {
						oo_warning (xin, _("Invalid attribute 'form:value', "
							   "expected number, received '%s'"), oc->value);
						value_real = 0.;
					}
					if (oc->value_type != NULL && 0 != strcmp (oc->value_type, "float"))
						oo_warning (xin, _("Invalid value-type '%s' advertised for "
								   "'form:value' attribute in 'form:value-range' "
								   "element."),
							    oc->value_type);
				} else value_real = 0.;

				if (value_real < (gnm_float)min_real)
					value_real = min_real;
				if (value_real > (gnm_float)max_real)
					value_real = max_real;

				so = state->chart.so = g_object_new
					(oc->t, "horizontal", oc->horizontal, NULL);
				adj = sheet_widget_adjustment_get_adjustment (so);

				gtk_adjustment_configure (adj,
							  value_real,
							  min_real,
							  max_real,
							  oc->step,
							  oc->page_step,
							  0);
			} else if (oc->t == sheet_widget_radio_button_get_type ()) {
				so = state->chart.so = g_object_new
					(oc->t, "text", oc->label, NULL);
				if (oc->value != NULL) {
					GnmValue *val = NULL;
					if (oc->value_type == NULL ||
					    0 == strcmp (oc->value_type, "string"))
						val = value_new_string (oc->value);
					else if (0 == strcmp (oc->value_type, "float")) {
						char *end;
						gnm_float value_real = gnm_strto (oc->value, &end);
						if (*end) {
							oo_warning (xin, _("Invalid attribute 'form:value', "
									   "expected number, received '%s'"), oc->value);
							val = value_new_string (oc->value);
						} else
							val = value_new_float (value_real);
					} else if (0 == strcmp (oc->value_type, "boolean")) {
						gboolean b = (g_ascii_strcasecmp (oc->value, "false") &&
							      strcmp (oc->value, "0"));
						val = value_new_bool (b);
					} else
						val = value_new_string (oc->value);
					sheet_widget_radio_button_set_value (so, val);
					value_release (val);
				}
			} else if (oc->t == sheet_widget_checkbox_get_type ()) {
				so = state->chart.so = g_object_new
					(oc->t, "text", oc->label, NULL);
			} else if (oc->t == sheet_widget_list_get_type () ||
				   oc->t == sheet_widget_combo_get_type ()) {
				so = state->chart.so = g_object_new
					(oc->t, NULL);
			} else if (oc->t == sheet_widget_button_get_type ()) {
				so = state->chart.so = g_object_new
					(oc->t, "text", oc->label, NULL);
			} else if (oc->t == sheet_widget_frame_get_type ()) {
				so = state->chart.so = g_object_new
					(oc->t, "text", oc->label, NULL);
			}

			od_draw_frame_end (xin, NULL);


			if (oc->linked_cell) {
				GnmParsePos pp;
				GnmRangeRef ref;
				char const *ptr = oo_rangeref_parse
					(&ref, oc->linked_cell,
					 parse_pos_init_sheet (&pp, state->pos.sheet),
					 NULL);
				if (ptr != oc->linked_cell 
				    && ref.a.sheet != invalid_sheet) {
					GnmValue *v = value_new_cellrange
						(&ref.a, &ref.a, 0, 0);
					GnmExprTop const *texpr
						= gnm_expr_top_new_constant (v);
					if (texpr != NULL) {
						if (oc->t == sheet_widget_scrollbar_get_type () ||
						    oc->t == sheet_widget_spinbutton_get_type () ||
						    oc->t == sheet_widget_slider_get_type ())
							sheet_widget_adjustment_set_link
								(so, texpr);
						else if (oc->t == sheet_widget_checkbox_get_type ())
							sheet_widget_checkbox_set_link
								(so, texpr);
						else if (oc->t == sheet_widget_radio_button_get_type ())
							sheet_widget_radio_button_set_link
								(so, texpr);
						else if (oc->t == sheet_widget_button_get_type ())
							sheet_widget_button_set_link
								(so, texpr);
						else if (oc->t == sheet_widget_list_get_type () ||
							 oc->t == sheet_widget_combo_get_type ()) {
							gnm_expr_top_ref ((result_texpr = texpr));
							sheet_widget_list_base_set_links (so, texpr, NULL);
						}
						gnm_expr_top_unref (texpr);
					}
				}
			}
			if (oc->t == sheet_widget_list_get_type () ||
			    oc->t == sheet_widget_combo_get_type ()) {
				if (oc->source_cell_range) {
					GnmParsePos pp;
					GnmRangeRef ref;
					char const *ptr = oo_rangeref_parse
						(&ref, oc->source_cell_range,
						 parse_pos_init_sheet (&pp, state->pos.sheet),
						 NULL);
					if (ptr != oc->source_cell_range && 
					    ref.a.sheet != invalid_sheet) {
						GnmValue *v = value_new_cellrange
							(&ref.a, &ref.b, 0, 0);
						GnmExprTop const *texpr
							= gnm_expr_top_new_constant (v);
						if (texpr != NULL) {
							sheet_widget_list_base_set_links
								(so,
								 result_texpr, texpr);
							gnm_expr_top_unref (texpr);
						}
					}
				}
				if (result_texpr != NULL)
					gnm_expr_top_unref (result_texpr);
				sheet_widget_list_base_set_result_type (so, oc->as_index);
			}
		}
	} else
		od_draw_frame_end (xin, NULL);
}

static void
pop_hash (GSList **list, GHashTable **hash)
{
	g_hash_table_destroy (*hash);
	if (*list == NULL)
		*hash = NULL;
	else {
		*hash = (*list)->data;
		*list = g_slist_delete_link (*list, *list);
	}
}

static void
odf_clear_conventions (OOParseState *state)
{
	gint i;
	for (i = 0; i < NUM_FORMULAE_SUPPORTED; i++)
		if (state->convs[i] != NULL) {
			gnm_conventions_unref (state->convs[i]);
			state->convs[i] = NULL;
		}
}

static void
od_draw_object (GsfXMLIn *xin, xmlChar const **attrs)
{
	OOParseState *state = (OOParseState *)xin->user_state;
	gchar const *name_start = NULL;
	gchar * name;
	gint name_len;
	GsfInput	*content = NULL;

	if (state->chart.so != NULL) {
		if (IS_SHEET_OBJECT_GRAPH (state->chart.so))
			/* Only one object per frame! */
			return;
		/* We prefer objects over images etc. */
		/* We probably should figure out though whetehr */
		/* we in fact understand this object. */
		g_object_unref (state->chart.so);
		state->chart.so = NULL;
	}

	state->chart.so    = sheet_object_graph_new (NULL);
	state->chart.graph = sheet_object_graph_get_gog (state->chart.so);

	state->chart.saved_graph_styles
		= g_slist_prepend (state->chart.saved_graph_styles,
				   state->chart.graph_styles);
	state->chart.saved_hatches
		= g_slist_prepend (state->chart.saved_hatches,
				   state->chart.hatches);
	state->chart.saved_dash_styles
		= g_slist_prepend (state->chart.saved_dash_styles,
				   state->chart.dash_styles);
	state->chart.saved_fill_image_styles
		= g_slist_prepend (state->chart.saved_fill_image_styles,
				   state->chart.fill_image_styles);
	state->chart.saved_gradient_styles
		= g_slist_prepend (state->chart.saved_gradient_styles,
				   state->chart.gradient_styles);

	state->chart.graph_styles = g_hash_table_new_full
		(g_str_hash, g_str_equal,
		 (GDestroyNotify) g_free,
		 (GDestroyNotify) oo_chart_style_free);
	state->chart.hatches = g_hash_table_new_full
		(g_str_hash, g_str_equal,
		(GDestroyNotify) g_free,
		(GDestroyNotify) g_free);
	state->chart.dash_styles = g_hash_table_new_full
		(g_str_hash, g_str_equal,
		 (GDestroyNotify) g_free,
		 NULL);
	state->chart.fill_image_styles = g_hash_table_new_full
		(g_str_hash, g_str_equal,
		(GDestroyNotify) g_free,
		(GDestroyNotify) g_free);
	state->chart.gradient_styles = g_hash_table_new_full
		(g_str_hash, g_str_equal,
		 (GDestroyNotify) g_free,
		 (GDestroyNotify) g_free);

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_XLINK, "href")) {
			name_start = CXML2C (attrs[1]);
			if (strncmp (CXML2C (attrs[1]), "./", 2) == 0)
				name_start += 2;
			if (strncmp (CXML2C (attrs[1]), "/", 1) == 0)
				name_start = NULL;
			break;
		}

	if (!name_start)
		return;
	name_len = strlen (name_start);
	if (*(name_start + name_len - 1) == '/') /* OOo does not append a / */
		name_len--;
	name = g_strndup (name_start, name_len);
	state->object_name = name;

	if (state->debug)
		g_print ("START %s\n", name);

	/* We should be saving/protecting some info to avoid it being overwritten. */

	content = gsf_infile_child_by_vname (state->zip, name, "styles.xml", NULL);
	if (content != NULL) {
		GsfXMLInDoc *doc =
			gsf_xml_in_doc_new (get_styles_dtd (), gsf_ooo_ns);
		odf_clear_conventions (state); /* contain references to xin */
		gsf_xml_in_doc_parse (doc, content, state);
		gsf_xml_in_doc_free (doc);
		odf_clear_conventions (state); /* contain references to xin */
		g_object_unref (content);
	}

	content = gsf_infile_child_by_vname (state->zip, name, "content.xml", NULL);
	if (content != NULL) {
		GsfXMLInDoc *doc =
			gsf_xml_in_doc_new (get_dtd (), gsf_ooo_ns);
		odf_clear_conventions (state); /* contain references to xin */
		gsf_xml_in_doc_parse (doc, content, state);
		gsf_xml_in_doc_free (doc);
		odf_clear_conventions (state); /* contain references to xin */
		g_object_unref (content);
	}
	if (state->debug)
		g_print ("END %s\n", name);
	state->object_name = NULL;
	g_free (name);

	if (state->cur_style.type == OO_STYLE_CHART)
		state->cur_style.type = OO_STYLE_UNKNOWN;
	state->chart.cur_graph_style = NULL;

	pop_hash (&state->chart.saved_graph_styles, &state->chart.graph_styles);
	pop_hash (&state->chart.saved_hatches, &state->chart.hatches);
	pop_hash (&state->chart.saved_dash_styles, &state->chart.dash_styles);
	pop_hash (&state->chart.saved_fill_image_styles,
		  &state->chart.fill_image_styles);
	pop_hash (&state->chart.saved_gradient_styles,
		  &state->chart.gradient_styles);
}

static void
od_draw_image (GsfXMLIn *xin, xmlChar const **attrs)
{
	GsfInput *input;

	OOParseState *state = (OOParseState *)xin->user_state;
	gchar const *file = NULL;
	char **path;

	if (state->chart.so != NULL)
		/* We only use images if there is no object available. */
		return;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]),
					OO_NS_XLINK, "href")) {
			file = CXML2C (attrs[1]);
			break;
		}

	if (!file)
		return;

	path = g_strsplit (file, "/", -1);
	input = gsf_infile_child_by_aname (state->zip, (const char **) path);
	g_strfreev (path);

	if (input != NULL) {
		SheetObjectImage *soi;
		gsf_off_t len = gsf_input_size (input);
		guint8 const *data = gsf_input_read (input, len, NULL);
		soi = g_object_new (SHEET_OBJECT_IMAGE_TYPE, NULL);
		sheet_object_image_set_image (soi, "", (void *)data, len, TRUE);

		state->chart.so = SHEET_OBJECT (soi);
		g_object_unref (input);
	} else
		oo_warning (xin, _("Unable to load "
				   "the file \'%s\'."),
			    file);

}

static void
od_draw_text_box (GsfXMLIn *xin, xmlChar const **attrs)
{
	OOParseState *state = (OOParseState *)xin->user_state;
	GOStyle  *style;

	if (state->chart.so != NULL)
		/* We have already created frame content */
		return;

	style = go_style_new ();

	style->line.width = 0;
	style->line.dash_type = GO_LINE_NONE;
	style->line.auto_dash = FALSE;
	style->fill.type = GO_STYLE_FILL_NONE;
	style->fill.auto_type = FALSE;

	state->chart.so = g_object_new (GNM_SO_FILLED_TYPE, "is-oval", FALSE, "style", style, NULL);
	g_object_unref (style);
}

static void
od_draw_text_box_p_end (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	OOParseState *state = (OOParseState *)xin->user_state;
	gchar *text_old, *text_new;

	if (!IS_GNM_SO_FILLED (state->chart.so))
		/* We are intentionally ignoring this frame content */
		return;

	g_object_get (state->chart.so, "text", &text_old, NULL);

	if (text_old == NULL) {
		g_object_set (state->chart.so, "text", xin->content->str, NULL);
	} else {
		text_new = g_strconcat (text_old, "\n", xin->content->str, NULL);
		g_free (text_old);
		g_object_set (state->chart.so, "text", text_new, NULL);
		g_free (text_new);
	}
}

/* oo_chart_title is used both for chart titles and legend titles */
static void
oo_chart_title (GsfXMLIn *xin, xmlChar const **attrs)
{
	OOParseState *state = (OOParseState *)xin->user_state;
	state->chart.title_expr = NULL;
	state->chart.title_style = NULL;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2){
		if ((gsf_xml_in_namecmp (xin, CXML2C (attrs[0]),
					 OO_NS_TABLE, "cell-address" ) ||
		     gsf_xml_in_namecmp (xin, CXML2C (attrs[0]),
					 OO_NS_TABLE, "cell-range" ))
		    && state->chart.title_expr == NULL) {
			GnmParsePos   pp;
			char *end_str = g_strconcat ("[", CXML2C (attrs[1]), "]", NULL);

			parse_pos_init (&pp, state->pos.wb, NULL, 0, 0);
			state->chart.title_expr
				= oo_expr_parse_str
				(xin, end_str, &pp,
				 GNM_EXPR_PARSE_FORCE_EXPLICIT_SHEET_REFERENCES,
				 FORMULA_OPENFORMULA);
			g_free (end_str);
		} else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]),
					       OO_GNUM_NS_EXT, "expression")) {
			GnmParsePos   pp;

			if (state->chart.title_expr != NULL)
				gnm_expr_top_unref (state->chart.title_expr);

			parse_pos_init (&pp, state->pos.wb, NULL, 0, 0);
			state->chart.title_expr
				= oo_expr_parse_str
				(xin, CXML2C (attrs[1]), &pp,
				 GNM_EXPR_PARSE_FORCE_EXPLICIT_SHEET_REFERENCES,
				 FORMULA_OPENFORMULA);
		} else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]),
					       OO_NS_CHART, "style-name")) {
			state->chart.title_style = g_strdup (CXML2C (attrs[1]));
		}
	}
}

static void
oo_chart_title_end (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	OOParseState *state = (OOParseState *)xin->user_state;
	if (state->chart.title_expr) {
		GOData *data = gnm_go_data_scalar_new_expr
			(state->chart.src_sheet, state->chart.title_expr);
		GogObject *label;
		GogObject *obj;
		gchar const *tag;

		if (state->chart.axis != NULL) {
			obj = (GogObject *)state->chart.axis;
			tag = "Label";
		} else if (state->chart.legend != NULL) {
			obj = (GogObject *)state->chart.legend;
			tag = "Title";
		} else {
			obj = (GogObject *)state->chart.chart;
			tag = "Title";
		}

		label = gog_object_add_by_name (obj, tag, NULL);
		gog_dataset_set_dim (GOG_DATASET (label), 0, data, NULL);
		state->chart.title_expr = NULL;
		if (state->chart.title_style != NULL) {
			OOChartStyle *oostyle = g_hash_table_lookup
				(state->chart.graph_styles, state->chart.title_style);
			if (oostyle != NULL) {
				GOStyle *style;
				g_object_get (G_OBJECT (label), "style", &style, NULL);

				if (style != NULL) {
					odf_apply_style_props (xin, oostyle->style_props, style);
					g_object_unref (style);
				}
			}
			g_free (state->chart.title_style);
			state->chart.title_style = NULL;
		}
	}

}

static void
oo_chart_title_text (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	OOParseState *state = (OOParseState *)xin->user_state;

	if (state->chart.title_expr == NULL)
		state->chart.title_expr =
			gnm_expr_top_new_constant
			(value_new_string (xin->content->str));
}

static void
od_chart_axis_categories (GsfXMLIn *xin, xmlChar const **attrs)
{
	OOParseState *state = (OOParseState *)xin->user_state;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]),
					OO_NS_TABLE, "cell-range-address")) {
			if (state->chart.cat_expr == NULL)
				state->chart.cat_expr
					= g_strdup (CXML2C (attrs[1]));
		}
}


static void
oo_chart_axis (GsfXMLIn *xin, xmlChar const **attrs)
{
	static OOEnum const types[] = {
		{ "x",	GOG_AXIS_X },
		{ "y",	GOG_AXIS_Y },
		{ "z",	GOG_AXIS_Z },
		{ NULL,	0 },
	};
	static OOEnum const types_radar[] = {
		{ "x",	GOG_AXIS_CIRCULAR },
		{ "y",	GOG_AXIS_RADIAL },
		{ NULL,	0 },
	};
	GSList	*axes;

	OOParseState *state = (OOParseState *)xin->user_state;
	OOChartStyle *style = NULL;
	gchar const *style_name = NULL;
	GogAxisType  axis_type;
	int tmp;

	axis_type = GOG_AXIS_UNKNOWN;
	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_CHART, "style-name"))
			style_name = CXML2C (attrs[1]);
		else if (oo_attr_enum (xin, attrs, OO_NS_CHART, "dimension",
				       (state->chart.plot_type == OO_PLOT_RADAR ||
					state->chart.plot_type == OO_PLOT_RADARAREA ||
					state->chart.plot_type == OO_PLOT_POLAR)? types_radar :  types, &tmp))
			axis_type = tmp;

	axes = gog_chart_get_axes (state->chart.chart, axis_type);
	if (NULL != axes) {
		state->chart.axis = axes->data;
		g_slist_free (axes);
	}

	if (NULL != style_name &&
	    NULL != (style = g_hash_table_lookup (state->chart.graph_styles, style_name))) {
		if (NULL != state->chart.axis) {
			GOStyle *gostyle;
			g_object_get (G_OBJECT (state->chart.axis), "style", &gostyle, NULL);

			oo_prop_list_apply_to_axis (style->axis_props,
						    G_OBJECT (state->chart.axis));
			odf_apply_style_props (xin, style->style_props, gostyle);
			g_object_unref (gostyle);
		}

		if (NULL != state->chart.plot && (state->ver == OOO_VER_1))
			oo_prop_list_apply (style->plot_props, G_OBJECT (state->chart.plot));
	}
}

static void
oo_chart_axis_end (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	OOParseState *state = (OOParseState *)xin->user_state;
	state->chart.axis = NULL;
}

static int
gog_series_map_dim (GogSeries const *series, GogMSDimType ms_type)
{
	GogSeriesDesc const *desc = &series->plot->desc.series;
	unsigned i = desc->num_dim;

	if (ms_type == GOG_MS_DIM_LABELS)
		return -1;
	while (i-- > 0)
		if (desc->dim[i].ms_type == ms_type)
			return i;
	return -2;
}

static int
gog_series_map_dim_by_name (GogSeries const *series, char const *dim_name)
{
	GogSeriesDesc const *desc = &series->plot->desc.series;
	unsigned i = desc->num_dim;

	while (i-- > 0)
		if (desc->dim[i].name != NULL && strcmp (desc->dim[i].name, dim_name) == 0)
			return i;
	return -2;
}

/* If range == %NULL use an implicit range */
static void
oo_plot_assign_dim (GsfXMLIn *xin, xmlChar const *range, int dim_type, char const *dim_name)
{
	OOParseState *state = (OOParseState *)xin->user_state;

	/* force relative to A1, not the containing cell */
	GnmExprTop const *texpr;
	GnmParsePos pp;
	GnmValue *v;
	int dim;
	gboolean set_default_labels = FALSE;
	gboolean set_default_series_name = FALSE;

	if (NULL == state->chart.series)
		return;
	if (dim_type < 0)
		dim = - (1 + dim_type);
	else if (dim_name == NULL)
		dim = gog_series_map_dim (state->chart.series, dim_type);
	else
		dim = gog_series_map_dim_by_name (state->chart.series, dim_name);
	if (dim < -1)
		return;

	if (NULL != range) {
		GnmRangeRef ref;
		char const *ptr = oo_rangeref_parse 
			(&ref, CXML2C (range),
			 parse_pos_init_sheet (&pp, state->pos.sheet),
			 NULL);
		if (ptr == CXML2C (range) || ref.a.sheet == invalid_sheet)
			return;
		v = value_new_cellrange (&ref.a, &ref.b, 0, 0);
		if (state->debug)
			g_print ("%d = rangeref (%s)\n", dim, range);
	} else if (NULL != gog_dataset_get_dim (GOG_DATASET (state->chart.series), dim))
		return;	/* implicit does not overwrite existing */
	else if (state->chart.src_n_vectors <= 0) {
		oo_warning (xin,
			    _("Not enough data in the supplied range (%s) for all the requests"), CXML2C (range));
		return;
	} else {
		v = value_new_cellrange_r (
			   state->chart.src_sheet,
			   &state->chart.src_range);

		if (state->debug)
			g_print ("%d = implicit (%s)\n", dim,
				 range_as_string (&state->chart.src_range));

		state->chart.src_n_vectors--;
		if (state->chart.src_in_rows)
			state->chart.src_range.end.row = ++state->chart.src_range.start.row;
		else
			state->chart.src_range.end.col = ++state->chart.src_range.start.col;

		set_default_labels = state->chart.src_abscissa_set;
		set_default_series_name = state->chart.src_label_set;
	}

	texpr = gnm_expr_top_new_constant (v);
	if (NULL != texpr)
		gog_series_set_dim (state->chart.series, dim,
			(dim_type != GOG_MS_DIM_LABELS)
			? gnm_go_data_vector_new_expr (state->pos.sheet, texpr)
			: gnm_go_data_scalar_new_expr (state->pos.sheet, texpr),
			NULL);
	if (set_default_labels) {
		v = value_new_cellrange_r (state->chart.src_sheet,
					   &state->chart.src_abscissa);
		texpr = gnm_expr_top_new_constant (v);
		if (NULL != texpr)
			gog_series_set_dim (state->chart.series, GOG_DIM_LABEL,
					    gnm_go_data_vector_new_expr
					    (state->pos.sheet, texpr),
					    NULL);
	}
	if (set_default_series_name) {
		v = value_new_cellrange_r (state->chart.src_sheet,
					   &state->chart.src_label);
		texpr = gnm_expr_top_new_constant (v);
		if (NULL != texpr)
			gog_series_set_name (state->chart.series,
					     GO_DATA_SCALAR (gnm_go_data_scalar_new_expr
							     (state->pos.sheet, texpr)),
					    NULL);
		if (state->chart.src_in_rows)
			state->chart.src_label.end.row = ++state->chart.src_label.start.row;
		else
			state->chart.src_label.end.col = ++state->chart.src_label.start.col;
	}
}

static void
odf_gog_check_position (GsfXMLIn *xin, xmlChar const **attrs, GSList **list)
{
	gboolean b;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (oo_attr_bool (xin, attrs, OO_GNUM_NS_EXT, "is-position-manual", &b))
			*list = g_slist_prepend (*list, oo_prop_new_bool("is-position-manual",
									 b));
		else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_GNUM_NS_EXT, "position"))
			*list = g_slist_prepend (*list, oo_prop_new_string ("position",
									    CXML2C(attrs[1])));
		else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_GNUM_NS_EXT, "anchor"))
			*list = g_slist_prepend (*list, oo_prop_new_string ("anchor",
									    CXML2C(attrs[1])));
}

static void
odf_gog_plot_area_check_position (GsfXMLIn *xin, xmlChar const **attrs, GSList **list)
{
	gboolean b;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (oo_attr_bool (xin, attrs, OO_GNUM_NS_EXT, "is-position-manual", &b))
			*list = g_slist_prepend (*list, oo_prop_new_bool("is-plot-area-manual",
									 b));
		else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_GNUM_NS_EXT, "position"))
			*list = g_slist_prepend (*list, oo_prop_new_string ("plot-area",
									    CXML2C(attrs[1])));
}

static void
oo_plot_area (GsfXMLIn *xin, xmlChar const **attrs)
{
	static OOEnum const labels[] = {
		{ "both",		2 | 1 },
		{ "column",		2 },
		{ "row",		    1 },
		{ "none",		    0 },
		{ NULL,	0 },
	};

	OOParseState *state = (OOParseState *)xin->user_state;
	gchar const *type = NULL;
	xmlChar const   *source_range_str = NULL;
	int label_flags = 0;
	GSList *prop_list = NULL;

	odf_gog_plot_area_check_position (xin, attrs, &prop_list);

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]),
					OO_NS_CHART, "style-name"))
			state->chart.i_plot_styles[OO_CHART_STYLE_PLOTAREA] = g_hash_table_lookup
				(state->chart.graph_styles, CXML2C (attrs[1]));
		else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_TABLE, "cell-range-address"))
			source_range_str = attrs[1];
		else if (oo_attr_enum (xin, attrs, OO_NS_CHART, "data-source-has-labels", labels, &label_flags))
			;

	state->chart.src_n_vectors = -1;
	state->chart.src_in_rows = TRUE;
	state->chart.src_abscissa_set = FALSE;
	state->chart.src_label_set = FALSE;
	state->chart.series = NULL;
	state->chart.series_count = 0;
	state->chart.list = NULL;
	if (NULL != source_range_str) {
		GnmParsePos pp;
		GnmEvalPos  ep;
		GnmRangeRef ref;
		Sheet	   *dummy;
		char const *ptr = oo_rangeref_parse 
			(&ref, CXML2C (source_range_str),
			 parse_pos_init_sheet (&pp, state->pos.sheet), NULL);
		if (ptr != CXML2C (source_range_str) 
		    && ref.a.sheet != invalid_sheet) {
			gnm_rangeref_normalize (&ref,
				eval_pos_init_sheet (&ep, state->pos.sheet),
				&state->chart.src_sheet, &dummy,
				&state->chart.src_range);

			if (label_flags & 1)
				state->chart.src_range.start.row++;
			if (label_flags & 2)
				state->chart.src_range.start.col++;

			if (state->chart.i_plot_styles[OO_CHART_STYLE_PLOTAREA] != NULL)
				state->chart.src_in_rows = state->chart.i_plot_styles
					[OO_CHART_STYLE_PLOTAREA]->src_in_rows;

			if (state->chart.src_in_rows) {
				state->chart.src_n_vectors = range_height (&state->chart.src_range);
				state->chart.src_range.end.row  = state->chart.src_range.start.row;
				if (label_flags & 1) {
					state->chart.src_abscissa = state->chart.src_range;
					state->chart.src_abscissa.end.row = --state->chart.src_abscissa.start.row;
					state->chart.src_abscissa_set = TRUE;
				}
				if (label_flags & 2) {
					state->chart.src_label = state->chart.src_range;
					state->chart.src_label.end.col = --state->chart.src_label.start.col;
					state->chart.src_label.end.row = state->chart.src_label.start.row;
					state->chart.src_label_set = TRUE;
				}
			} else {
				state->chart.src_n_vectors = range_width (&state->chart.src_range);
				state->chart.src_range.end.col  = state->chart.src_range.start.col;
				if (label_flags & 2) {
					state->chart.src_abscissa = state->chart.src_range;
					state->chart.src_abscissa.end.col = --state->chart.src_abscissa.start.col;
					state->chart.src_abscissa_set = TRUE;
				}
				if (label_flags & 1) {
					state->chart.src_label = state->chart.src_range;
					state->chart.src_label.end.row = --state->chart.src_label.start.row;
					state->chart.src_label.end.col = state->chart.src_label.start.col;
					state->chart.src_label_set = TRUE;
				}
			}
		}
	}

	switch (state->chart.plot_type) {
	case OO_PLOT_AREA:	type = "GogAreaPlot";	break;
	case OO_PLOT_BAR:	type = "GogBarColPlot";	break;
	case OO_PLOT_CIRCLE:	type = "GogPiePlot";	break;
	case OO_PLOT_LINE:	type = "GogLinePlot";	break;
	case OO_PLOT_RADAR:	type = "GogRadarPlot";	break;
	case OO_PLOT_RADARAREA: type = "GogRadarAreaPlot";break;
	case OO_PLOT_RING:	type = "GogRingPlot";	break;
	case OO_PLOT_SCATTER:	type = "GogXYPlot";	break;
	case OO_PLOT_STOCK:	type = "GogMinMaxPlot";	break;  /* This is not quite right! */
	case OO_PLOT_CONTOUR:
		if (oo_style_have_multi_series (state->chart.i_plot_styles)) {
			type = "XLSurfacePlot";
			state->chart.plot_type = OO_PLOT_XL_SURFACE;
		} else if (oo_style_have_three_dimensional (state->chart.i_plot_styles)) {
			type = "GogSurfacePlot";
			state->chart.plot_type = OO_PLOT_SURFACE;
		} else
			type = "GogContourPlot";
		break;
	case OO_PLOT_BUBBLE:	type = "GogBubblePlot"; break;
	case OO_PLOT_GANTT:	type = "GogDropBarPlot"; break;
	case OO_PLOT_POLAR:	type = "GogPolarPlot"; break;
	case OO_PLOT_XYZ_SURFACE:
		if (oo_style_have_three_dimensional (state->chart.i_plot_styles))
			type = "GogXYZSurfacePlot";
		else
			type = "GogXYZContourPlot";
		break;
	case OO_PLOT_SURFACE: type = "GogSurfacePlot"; break;
	case OO_PLOT_SCATTER_COLOUR: type = "GogXYColorPlot";	break;
	case OO_PLOT_XL_SURFACE: type = "XLSurfacePlot";	break;
	case OO_PLOT_BOX: type = "GogBoxPlot";	break;
	case OO_PLOT_UNKNOWN: type = "GogLinePlot";
		/* It is simpler to create a plot than to check that we don't have one */
		 break;
	default: return;
	}

	state->chart.plot = gog_plot_new_by_name (type);
	gog_object_add_by_name (GOG_OBJECT (state->chart.chart),
		"Plot", GOG_OBJECT (state->chart.plot));

	oo_prop_list_apply (prop_list, G_OBJECT (state->chart.chart));
	oo_prop_list_free (prop_list);

	if (state->chart.i_plot_styles[OO_CHART_STYLE_PLOTAREA] != NULL)
		oo_prop_list_apply (state->chart.i_plot_styles[OO_CHART_STYLE_PLOTAREA]->
				    plot_props, G_OBJECT (state->chart.plot));

	if (state->chart.plot_type == OO_PLOT_GANTT) {
		GogObject *yaxis = gog_object_get_child_by_name (GOG_OBJECT (state->chart.chart),
								 "Y-Axis");
		if (yaxis != NULL) {
			GValue *val = g_value_init (g_new0 (GValue, 1), G_TYPE_BOOLEAN);
			g_value_set_boolean (val, TRUE);
			g_object_set_property (G_OBJECT (yaxis), "invert-axis", val);
			g_value_unset (val);
			g_free (val);
		}
	}
}

static void
odf_create_stock_plot (GsfXMLIn *xin)
{
	OOParseState *state = (OOParseState *)xin->user_state;
	GSList *series_addresses = state->chart.list;
	int len = g_slist_length (series_addresses);

	if (len > 3) {
		series_addresses = series_addresses->next;
		len--;
	}

	if (len-- > 0) {
		state->chart.series = gog_plot_new_series (state->chart.plot);
		oo_plot_assign_dim (xin, series_addresses->data, GOG_MS_DIM_LOW, NULL);
	}
	if (len-- > 0) {
		series_addresses = series_addresses->next;
		oo_plot_assign_dim (xin, series_addresses->data, GOG_MS_DIM_HIGH, NULL);
	}
}

static void
oo_plot_area_end (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	OOParseState *state = (OOParseState *)xin->user_state;
	if (state->chart.plot_type == OO_PLOT_STOCK) {
		odf_create_stock_plot (xin);
		go_slist_free_custom (state->chart.list, g_free);
		state->chart.list = NULL;
	} else {
		if (state->chart.series_count == 0 && state->chart.series == NULL)
			state->chart.series = gog_plot_new_series (state->chart.plot);
		if (state->chart.series != NULL) {
			oo_plot_assign_dim (xin, NULL, GOG_MS_DIM_VALUES, NULL);
			state->chart.series = NULL;
		}
	}
	state->chart.plot = NULL;
	state->chart.i_plot_styles[OO_CHART_STYLE_PLOTAREA] = NULL;
}


static void
oo_plot_series (GsfXMLIn *xin, xmlChar const **attrs)
{
	OOParseState *state = (OOParseState *)xin->user_state;
	xmlChar const *label = NULL;

	if (state->debug)
		g_print ("<<<<< Start\n");

	state->chart.series_count++;
	state->chart.domain_count = 0;
	state->chart.data_pt_count = 0;


	/* Create the series */
	switch (state->chart.plot_type) {
	case OO_PLOT_STOCK: /* We need to construct the series later. */
		break;
	case OO_PLOT_SURFACE:
	case OO_PLOT_CONTOUR:
		if (state->chart.series == NULL)
			state->chart.series = gog_plot_new_series (state->chart.plot);
		break;
	default:
		if (state->chart.series == NULL) {
			state->chart.series = gog_plot_new_series (state->chart.plot);
			if (state->chart.cat_expr != NULL) {
				oo_plot_assign_dim
					(xin, state->chart.cat_expr,
					 GOG_MS_DIM_CATEGORIES, NULL);
			}
		}
	}

	/* Now check the attributes */
	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2) {
		if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_CHART, "values-cell-range-address")) {
			switch (state->chart.plot_type) {
			case OO_PLOT_STOCK:
				state->chart.list = g_slist_append (state->chart.list,
								    g_strdup (attrs[1]));
				break;
			case OO_PLOT_SURFACE:
			case OO_PLOT_CONTOUR:
				{
					GnmRangeRef ref;
					GnmValue *v;
					GnmExprTop const *texpr;
					GnmParsePos pp;
					char const *ptr = oo_rangeref_parse 
						(&ref, CXML2C (attrs[1]),
						 parse_pos_init_sheet 
						 (&pp, state->pos.sheet),
						 NULL);
					if (ptr == CXML2C (attrs[1]) || 
					    ref.a.sheet == invalid_sheet)
						return;
					v = value_new_cellrange (&ref.a, &ref.b, 0, 0);
					texpr = gnm_expr_top_new_constant (v);
					if (NULL != texpr)
						gog_series_set_dim (state->chart.series, 2,
								    gnm_go_data_matrix_new_expr
								    (state->pos.sheet, texpr), NULL);
				}
				break;
			case OO_PLOT_GANTT:
				oo_plot_assign_dim (xin, attrs[1],
						    (state->chart.series_count % 2 == 1) ? GOG_MS_DIM_START : GOG_MS_DIM_END,
						    NULL);
				break;
			case OO_PLOT_BUBBLE:
				oo_plot_assign_dim (xin, attrs[1], GOG_MS_DIM_BUBBLES, NULL);
				break;
			case OO_PLOT_SCATTER_COLOUR:
				oo_plot_assign_dim (xin, attrs[1], GOG_MS_DIM_EXTRA1, NULL);
				break;
			default:
				oo_plot_assign_dim (xin, attrs[1], GOG_MS_DIM_VALUES, NULL);
				break;
			}
		} else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_CHART, "label-cell-address")) {
			if (label == NULL)
				label = attrs[1];
		} else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_GNUM_NS_EXT, "label-cell-expression"))
			label = attrs[1];
		else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]),
					     OO_NS_CHART, "style-name"))
			state->chart.i_plot_styles[OO_CHART_STYLE_SERIES] = g_hash_table_lookup
				(state->chart.graph_styles, CXML2C (attrs[1]));
	}

	if (label != NULL) {
		GnmExprTop const *texpr;
		OOFormula f_type = odf_get_formula_type (xin, (char const **)&label);

		if (f_type != FORMULA_NOT_SUPPORTED) {
			GnmParsePos pp;
			GnmRangeRef ref;
			char const *ptr = oo_rangeref_parse
				(&ref, CXML2C (label),
				 parse_pos_init_sheet (&pp, state->pos.sheet),
				 NULL);
			if (ptr == CXML2C (label) 
			    || ref.a.sheet == invalid_sheet)
				texpr = oo_expr_parse_str (xin, label,
							   &state->pos,
							   GNM_EXPR_PARSE_DEFAULT,
							   f_type);
			else {
				GnmValue *v = value_new_cellrange (&ref.a, &ref.b, 0, 0);
				texpr = gnm_expr_top_new_constant (v);
			}
			if (texpr != NULL)
				gog_series_set_name (state->chart.series,
						     GO_DATA_SCALAR (gnm_go_data_scalar_new_expr
								     (state->pos.sheet, texpr)),
						     NULL);
		}
	}
	oo_chart_style_to_series (xin, state->chart.i_plot_styles[OO_CHART_STYLE_PLOTAREA],
				  G_OBJECT (state->chart.series));
	oo_chart_style_to_series (xin, state->chart.i_plot_styles[OO_CHART_STYLE_SERIES],
				  G_OBJECT (state->chart.series));
}

static void
oo_plot_series_end (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	OOParseState *state = (OOParseState *)xin->user_state;

	switch (state->chart.plot_type) {
	case OO_PLOT_STOCK:
	case OO_PLOT_CONTOUR:
		break;
	case OO_PLOT_GANTT:
		if ((state->chart.series_count % 2) != 0)
			break;
		/* else no break */
	default:
		oo_plot_assign_dim (xin, NULL, GOG_MS_DIM_VALUES, NULL);
		state->chart.series = NULL;
		break;
	}
	state->chart.i_plot_styles[OO_CHART_STYLE_SERIES] = NULL;
	if (state->debug)
		g_print (">>>>> end\n");
}

static void
oo_series_domain (GsfXMLIn *xin, xmlChar const **attrs)
{
	OOParseState *state = (OOParseState *)xin->user_state;
	xmlChar const *src = NULL;
	int dim = GOG_MS_DIM_VALUES;
	char const *name = NULL;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_TABLE, "cell-range-address"))
			src = attrs[1];
	switch (state->chart.plot_type) {
	case OO_PLOT_BUBBLE:
	case OO_PLOT_SCATTER_COLOUR:
		dim = (state->chart.domain_count == 0) ? GOG_MS_DIM_VALUES : GOG_MS_DIM_CATEGORIES;
		break;
	case OO_PLOT_XYZ_SURFACE:
	case OO_PLOT_SURFACE:
		name = (state->chart.domain_count == 0) ? "Y" : "X";
		break;
	case OO_PLOT_CONTOUR:
		dim = (state->chart.domain_count == 0) ? -1 : GOG_MS_DIM_CATEGORIES;
		break;
	default:
		dim = GOG_MS_DIM_CATEGORIES;
		break;
	}
	oo_plot_assign_dim (xin, src, dim, name);
	state->chart.domain_count++;
}

static void
oo_series_pt (GsfXMLIn *xin, xmlChar const **attrs)
{
	OOParseState *state = (OOParseState *)xin->user_state;
	char const *style_name = NULL;
	guint repeat_count = 1;
	OOChartStyle *style = NULL;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (oo_attr_int_range (xin, attrs, OO_NS_CHART, "repeated", &repeat_count, 0, INT_MAX))
			;
		else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_CHART, "style-name")) {
			style_name = attrs[1];
		}
	if (repeat_count == 0)
		return; /* Why does ODF allow repeat counts of 0 ??*/

	if (style_name != NULL &&
	    NULL != (style = g_hash_table_lookup (state->chart.graph_styles, style_name))) {
		guint index = state->chart.data_pt_count;
		state->chart.data_pt_count += repeat_count;
		for (; index < state->chart.data_pt_count; index++) {
			GogObject *element = gog_object_add_by_name (GOG_OBJECT (state->chart.series), "Point", NULL);
			if (element != NULL) {
				GOStyle *gostyle;
				g_object_set (G_OBJECT (element), "index", index, NULL);
				oo_prop_list_apply (style->plot_props, G_OBJECT (element));
				g_object_get (G_OBJECT (element), "style", &gostyle, NULL);
				if (gostyle != NULL) {
					OOChartStyle *astyle = state->chart.i_plot_styles[OO_CHART_STYLE_PLOTAREA];
					if (astyle != NULL)
						odf_apply_style_props
							(xin, astyle->style_props, gostyle);
					astyle = state->chart.i_plot_styles[OO_CHART_STYLE_SERIES];
					if (astyle != NULL)
						odf_apply_style_props
							(xin, astyle->style_props, gostyle);
					odf_apply_style_props (xin, style->style_props, gostyle);
					g_object_unref (gostyle);
				}
			}
		}
	} else
		state->chart.data_pt_count += repeat_count;
}

static void
od_series_reg_equation (GsfXMLIn *xin, xmlChar const **attrs)
{
	OOParseState *state = (OOParseState *)xin->user_state;
	char const *style_name = NULL;
	GogObject *equation;
	gboolean automatic_content = TRUE;
	gboolean dispay_equation = TRUE;
	gboolean display_r_square = TRUE;
	GSList *prop_list = NULL;

	odf_gog_check_position (xin, attrs, &prop_list);

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_CHART, "style-name"))
			style_name = CXML2C (attrs[1]);
		else if (oo_attr_bool (xin, attrs, OO_NS_CHART, "automatic-content",
				       &automatic_content));
		else if (oo_attr_bool (xin, attrs, OO_GNUM_NS_EXT, "automatic-content",
				       &automatic_content));
		else if (oo_attr_bool (xin, attrs, OO_NS_CHART, "display-equation",
				       &dispay_equation));
		else if (oo_attr_bool (xin, attrs, OO_GNUM_NS_EXT, "display-equation",
				       &dispay_equation));
		else if (oo_attr_bool (xin, attrs, OO_NS_CHART, "display-r-square",
				       &display_r_square));


	equation = gog_object_add_by_name (GOG_OBJECT (state->chart.regression),
						     "Equation", NULL);

	g_object_set (G_OBJECT (equation),
		      "show-eq", dispay_equation,
		      "show-r2", display_r_square,
		      NULL);

	oo_prop_list_apply (prop_list, G_OBJECT (equation));
	oo_prop_list_free (prop_list);

	if (!automatic_content)
		oo_warning (xin, _("Gnumeric does not support non-automatic"
				   " regression equations. Using automatic"
				   " equation instead."));

	if (style_name != NULL) {
		OOChartStyle *chart_style = g_hash_table_lookup
			(state->chart.graph_styles, style_name);
		GOStyle *style = NULL;
		g_object_get (G_OBJECT (equation), "style", &style, NULL);
		if (style != NULL) {
			odf_apply_style_props (xin, chart_style->style_props, style);
			g_object_unref (style);
		}
		/* In the moment we don't need this. */
/* 		oo_prop_list_apply (chart_style->plot_props, G_OBJECT (equation)); */
	}
}

static void
odf_store_data (OOParseState *state, gchar const *str, GogObject *obj, int dim)
{
	if (str != NULL) {
		GnmParsePos pp;
		GnmRangeRef ref;
		char const *ptr = oo_rangeref_parse
			(&ref, CXML2C (str),
			 parse_pos_init (&pp, state->pos.wb, NULL, 0, 0),
			 NULL);
		if (ptr != CXML2C (str) && ref.a.sheet != invalid_sheet) {
			GnmValue *v = value_new_cellrange (&ref.a, &ref.b, 0, 0);
			GnmExprTop const *texpr = gnm_expr_top_new_constant (v);
			if (NULL != texpr) {
				gog_dataset_set_dim (GOG_DATASET (obj), dim,
						     gnm_go_data_scalar_new_expr
						     (state->pos.sheet, texpr),
						     NULL);
			}
		}
	}
}

static void
od_series_regression (GsfXMLIn *xin, xmlChar const **attrs)
{
	OOParseState *state = (OOParseState *)xin->user_state;
	char const *style_name = NULL;
	gchar const *lower_bd = NULL;
	gchar const *upper_bd = NULL;

	state->chart.regression = NULL;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_CHART, "style-name"))
			style_name = CXML2C (attrs[1]);
		else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_GNUM_NS_EXT, "lower-bound"))
			lower_bd = CXML2C (attrs[1]);
		else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_GNUM_NS_EXT, "upper-bound"))
			upper_bd = CXML2C (attrs[1]);

	if (style_name != NULL) {
		GSList *l;
		OOChartStyle *chart_style = g_hash_table_lookup
			(state->chart.graph_styles, style_name);
		GOStyle *style = NULL;
		GogObject *regression;
		gchar const *type_name = "GogLinRegCurve";

		for (l = chart_style->other_props; l != NULL; l = l->next) {
			OOProp *prop = l->data;
			if (0 == strcmp ("regression-type", prop->name)) {
				char const *reg_type = g_value_get_string (&prop->value);
				if (0 == strcmp (reg_type, "linear"))
					type_name = "GogLinRegCurve";
				else if (0 == strcmp (reg_type, "power"))
					type_name = "GogPowerRegCurve";
				else if (0 == strcmp (reg_type, "exponential"))
					type_name = "GogExpRegCurve";
				else if (0 == strcmp (reg_type, "logarithmic"))
					type_name = "GogLogRegCurve";
				else if (0 == strcmp
					 (reg_type, "gnm:exponential-smoothed"))
					type_name = "GogExpSmooth";
				else if (0 == strcmp
					 (reg_type, "gnm:logfit"))
					type_name = "GogLogFitCurve";
				else if (0 == strcmp
					 (reg_type, "gnm:polynomial"))
					type_name = "GogPolynomRegCurve";
				else if (0 == strcmp
					 (reg_type, "gnm:moving-average"))
					type_name = "GogMovingAvg";
			}
		}

		state->chart.regression = regression =
			GOG_OBJECT (gog_trend_line_new_by_name (type_name));
		regression = gog_object_add_by_name (GOG_OBJECT (state->chart.series),
						     "Trend line", regression);
		oo_prop_list_apply (chart_style->other_props, G_OBJECT (regression));

		g_object_get (G_OBJECT (regression), "style", &style, NULL);
		if (style != NULL) {
			odf_apply_style_props (xin, chart_style->style_props, style);
			g_object_unref (style);
		}

		odf_store_data (state, lower_bd, regression , 0);
		odf_store_data (state, upper_bd, regression , 1);
	}
}


static void
oo_series_droplines (GsfXMLIn *xin, xmlChar const **attrs)
{
	OOParseState *state = (OOParseState *)xin->user_state;
	char const *style_name = NULL;
	gboolean vertical = TRUE;
	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_CHART, "style-name"))
			style_name = CXML2C (attrs[1]);
	if (style_name != NULL) {
		OOChartStyle *chart_style = g_hash_table_lookup
			(state->chart.graph_styles, style_name);
		GOStyle *style = NULL;
		GSList *l;
		char const *role_name = NULL;
		GogObject const *lines;

		for (l = chart_style->plot_props; l != NULL; l = l->next) {
			OOProp *prop = l->data;
			if (0 == strcmp ("vertical", prop->name))
				vertical = g_value_get_boolean (&prop->value);
		}

		switch (state->chart.plot_type) {
		case OO_PLOT_LINE:
			role_name = "Drop lines";
			break;
		case OO_PLOT_SCATTER:
			role_name = vertical ? "Vertical drop lines" : "Horizontal drop lines";
			break;
		default:
			oo_warning (xin , _("Encountered drop lines in a plot not supporting them."));
			return;
		}

		lines = gog_object_add_by_name (GOG_OBJECT (state->chart.series), role_name, NULL);

		g_object_get (G_OBJECT (lines), "style", &style, NULL);
		if (style != NULL) {
			odf_apply_style_props (xin, chart_style->style_props, style);
			g_object_unref (style);
		}
	}
}

static void
oo_chart_end (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	OOParseState *state = (OOParseState *)xin->user_state;

	g_free (state->chart.cat_expr);
	state->chart.cat_expr = NULL;
}

static void
oo_chart (GsfXMLIn *xin, xmlChar const **attrs)
{
	static OOEnum const types[] = {
		{ "chart:area",		OO_PLOT_AREA },
		{ "chart:bar",		OO_PLOT_BAR },
		{ "chart:circle",	OO_PLOT_CIRCLE },
		{ "chart:line",		OO_PLOT_LINE },
		{ "chart:radar",	OO_PLOT_RADAR },
		{ "chart:filled-radar",	OO_PLOT_RADARAREA },
		{ "chart:ring",		OO_PLOT_RING },
		{ "chart:scatter",	OO_PLOT_SCATTER },
		{ "chart:stock",	OO_PLOT_STOCK },
		{ "chart:bubble",	OO_PLOT_BUBBLE },
		{ "chart:gantt",	OO_PLOT_GANTT },
		{ "chart:surface",	OO_PLOT_CONTOUR },
		{ "gnm:polar",  	OO_PLOT_POLAR },
		{ "gnm:xyz-surface", 	OO_PLOT_XYZ_SURFACE },
		{ "gnm:scatter-color", 	OO_PLOT_SCATTER_COLOUR },
		{ "gnm:box", 	        OO_PLOT_BOX },
		{ "gnm:none", 	        OO_PLOT_UNKNOWN },
		{ NULL,	0 },
	};
	OOParseState *state = (OOParseState *)xin->user_state;
	int tmp;
	OOPlotType type = OO_PLOT_UNKNOWN;
	OOChartStyle	*style = NULL;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (oo_attr_enum (xin, attrs, OO_NS_CHART, "class", types, &tmp))
			type = tmp;
		else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]),
					     OO_NS_CHART, "style-name"))
			style = g_hash_table_lookup
				(state->chart.graph_styles, CXML2C (attrs[1]));
	state->chart.plot_type = type;
	state->chart.chart = GOG_CHART (gog_object_add_by_name (
		GOG_OBJECT (state->chart.graph), "Chart", NULL));
	state->chart.plot = NULL;
	state->chart.series = NULL;
	state->chart.axis = NULL;
	state->chart.legend = NULL;
	state->chart.cat_expr = NULL;
	if (NULL != style)
		state->chart.src_in_rows = style->src_in_rows;

	if (type == OO_PLOT_UNKNOWN)
		oo_warning (xin , _("Encountered an unknown chart type, "
				    "trying to create a line plot."));
}

static void
oo_legend (GsfXMLIn *xin, xmlChar const **attrs)
{
	static OOEnum const positions [] = {
		{ "top",	  GOG_POSITION_N },
		{ "bottom",	  GOG_POSITION_S },
		{ "start",	  GOG_POSITION_W },
		{ "end",	  GOG_POSITION_E },
		{ "top-start",	  GOG_POSITION_N | GOG_POSITION_W },
		{ "bottom-start", GOG_POSITION_S | GOG_POSITION_W },
		{ "top-end",	  GOG_POSITION_N | GOG_POSITION_E },
		{ "bottom-end",   GOG_POSITION_S | GOG_POSITION_E },
		{ NULL,	0 },
	};
	static OOEnum const alignments [] = {
		{ "start",	  GOG_POSITION_ALIGN_START },
		{ "center",	  GOG_POSITION_ALIGN_CENTER },
		{ "end",	  GOG_POSITION_ALIGN_END },
		{ NULL,	0 },
	};
	OOParseState *state = (OOParseState *)xin->user_state;
	GogObjectPosition pos = GOG_POSITION_W | GOG_POSITION_ALIGN_CENTER;
	GogObjectPosition align = GOG_POSITION_ALIGN_CENTER;
	GogObject *legend;
	int tmp;
	char const *style_name = NULL;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (oo_attr_enum (xin, attrs, OO_NS_CHART, "legend-position", positions, &tmp))
			pos = tmp;
		else if (oo_attr_enum (xin, attrs, OO_NS_CHART, "legend-align", alignments, &tmp))
			align = tmp;
		else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_CHART, "style-name"))
			style_name = g_strdup (CXML2C (attrs[1]));

	legend = gog_object_add_by_name ((GogObject *)state->chart.chart, "Legend", NULL);
	state->chart.legend = legend;
	if (legend != NULL) {
		gog_object_set_position_flags (legend, pos | align,
					       GOG_POSITION_COMPASS | GOG_POSITION_ALIGNMENT);
		if (style_name) {
			GOStyle *style = NULL;
			g_object_get (G_OBJECT (legend), "style", &style, NULL);
			if (style != NULL) {
				OOChartStyle *chart_style = g_hash_table_lookup
					(state->chart.graph_styles, style_name);
				odf_apply_style_props (xin, chart_style->style_props, style);
				g_object_unref (style);
			}
		}
	}

}

static void
oo_legend_end (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	OOParseState *state = (OOParseState *)xin->user_state;
	state->chart.legend = NULL;
}


static void
oo_chart_grid (GsfXMLIn *xin, xmlChar const **attrs)
{
	OOParseState *state = (OOParseState *)xin->user_state;

	if (state->chart.axis == NULL)
		return;
	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_CHART, "class")) {
			if (attr_eq (attrs[1], "major"))
				gog_object_add_by_name (state->chart.axis, "MajorGrid", NULL);
			else if (attr_eq (attrs[1], "minor"))
				gog_object_add_by_name (state->chart.axis, "MinorGrid", NULL);
		}
}

static void
oo_chart_wall (GsfXMLIn *xin, xmlChar const **attrs)
{
	OOParseState *state = (OOParseState *)xin->user_state;
	GogObject *backplane;
	gchar *style_name = NULL;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_CHART, "style-name"))
			style_name = g_strdup (CXML2C (attrs[1]));

	backplane = gog_object_add_by_name (GOG_OBJECT (state->chart.chart), "Backplane", NULL);

	if (style_name != NULL && backplane != NULL) {
		GOStyle *style = NULL;
		g_object_get (G_OBJECT (backplane), "style", &style, NULL);

		if (style != NULL) {
			OOChartStyle *chart_style = g_hash_table_lookup
				(state->chart.graph_styles, style_name);
			odf_apply_style_props (xin, chart_style->style_props, style);
			g_object_unref (style);
		}
	}
}

static void
oo_chart_style_free (OOChartStyle *cstyle)
{
	oo_prop_list_free (cstyle->axis_props);
	oo_prop_list_free (cstyle->style_props);
	oo_prop_list_free (cstyle->plot_props);
	oo_prop_list_free (cstyle->other_props);
	g_free (cstyle);
}

static void
oo_control_free (OOControl *ctrl)
{
	g_free (ctrl->value);
	g_free (ctrl->value_type);
	g_free (ctrl->label);
	g_free (ctrl->linked_cell);
	g_free (ctrl->implementation);
	g_free (ctrl->source_cell_range);
	g_free (ctrl);
}

static void
oo_marker_free (OOMarker *m)
{
	g_free (m->view_box);
	g_free (m->d);
	g_free (m->arrow);
	g_free (m);
}

static void
odf_annotation_start (GsfXMLIn *xin, xmlChar const **attrs)
{
	OOParseState *state = (OOParseState *)xin->user_state;

	state->cell_comment = cell_set_comment (state->pos.sheet, &state->pos.eval,
						NULL, NULL, NULL);
}

static void
odf_annotation_content_end (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	OOParseState *state = (OOParseState *)xin->user_state;
	char const *old = cell_comment_text_get (state->cell_comment);
	char *new;

	if (old != NULL && strlen (old) > 0)
		new = g_strconcat (old, "\n", xin->content->str, NULL);
	else
		new = g_strdup (xin->content->str);
	cell_comment_text_set (state->cell_comment, new);
	g_free (new);
}

static void
odf_annotation_author_end (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	OOParseState *state = (OOParseState *)xin->user_state;

	cell_comment_author_set (state->cell_comment, xin->content->str);
}

static void
odf_annotation_end (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	OOParseState *state = (OOParseState *)xin->user_state;

	state->cell_comment = NULL;
}

/****************************************************************************/
/******************************** graphic sheet objects *********************/

static void
odf_so_filled (GsfXMLIn *xin, xmlChar const **attrs, gboolean is_oval)
{
	OOParseState *state = (OOParseState *)xin->user_state;
	char const *style_name = NULL;

	od_draw_frame_start (xin, attrs);
	state->chart.so = g_object_new (GNM_SO_FILLED_TYPE,
					"is-oval", is_oval, NULL);

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]),
					OO_NS_DRAW, "style-name"))
			style_name = CXML2C (attrs[1]);

	if (style_name != NULL) {
		OOChartStyle *oostyle = g_hash_table_lookup
			(state->chart.graph_styles, style_name);
		if (oostyle != NULL) {
			GOStyle *style;
			g_object_get (G_OBJECT (state->chart.so),
				      "style", &style, NULL);

			if (style != NULL) {
				odf_apply_style_props (xin, oostyle->style_props,
						       style);
				g_object_unref (style);
			}
		}
	}
}

static void
odf_rect (GsfXMLIn *xin, xmlChar const **attrs)
{
	odf_so_filled (xin, attrs, FALSE);
}

static void
odf_ellipse (GsfXMLIn *xin, xmlChar const **attrs)
{
	odf_so_filled (xin, attrs, TRUE);
}

static GOArrow *
odf_get_arrow_marker (OOParseState *state, char const *name)
{
	OOMarker *m = g_hash_table_lookup (state->chart.arrow_markers, name);

	if (m != NULL) {
		if (m->arrow == NULL) {
			m->arrow = g_new0 (GOArrow, 1);
			go_arrow_init_kite (m->arrow, 8, 10, 3);
		}
		return go_arrow_dup (m->arrow);
	} else {
		GOArrow *arrow = g_new0 (GOArrow, 1);
		go_arrow_init_kite (arrow, 8, 10, 3);
		return arrow;
	}
}

static void
odf_line (GsfXMLIn *xin, xmlChar const **attrs)
{
	OOParseState *state = (OOParseState *)xin->user_state;
	gnm_float x1 = 0., x2 = 0., y1 = 0., y2 = 0.;
	ColRowInfo const *col, *row;
	GODrawingAnchorDir direction;
	GnmRange cell_base;
	double frame_offset[4];
	char const *style_name = NULL;

	cell_base.start.col = cell_base.end.col = state->pos.eval.col;
	cell_base.start.row = cell_base.end.row = state->pos.eval.row;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]),
					OO_NS_DRAW, "style-name"))
			style_name = CXML2C (attrs[1]);
		else if (NULL != oo_attr_distance (xin, attrs,
					      OO_NS_SVG, "x1",
					      &x1));
		else if (NULL != oo_attr_distance (xin, attrs,
					      OO_NS_SVG, "x2",
					      &x2));
		else if (NULL != oo_attr_distance (xin, attrs,
					      OO_NS_SVG, "y1",
					      &y1));
		else if (NULL != oo_attr_distance (xin, attrs,
					      OO_NS_SVG, "y2",
					      &y2));
		else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]),
					     OO_NS_TABLE, "end-cell-address")) {
			GnmParsePos pp;
			GnmRangeRef ref;
			char const *ptr = oo_rangeref_parse
				(&ref, CXML2C (attrs[1]),
				 parse_pos_init_sheet (&pp, state->pos.sheet),
				 NULL);
			if (ptr != CXML2C (attrs[1]) 
			    && ref.a.sheet != invalid_sheet) {
				cell_base.end.col = ref.a.col;
				cell_base.end.row = ref.a.row;
			}
		}

	if (x1 < x2) {
		if (y1 < y2)
			direction = GOD_ANCHOR_DIR_DOWN_RIGHT;
		else
			direction = GOD_ANCHOR_DIR_UP_RIGHT;
		frame_offset[0] = x1;
		frame_offset[2] = x2;
	} else {
		if (y1 < y2)
			direction = GOD_ANCHOR_DIR_DOWN_LEFT;
		else
			direction = GOD_ANCHOR_DIR_UP_LEFT;
		frame_offset[0] = x2;
		frame_offset[2] = x1;
	}
	if (y1 < y2) {
		frame_offset[1] = y1;
		frame_offset[3] = y2;
	} else {
		frame_offset[1] = y2;
		frame_offset[3] = y1;
	}

	frame_offset[0] -= sheet_col_get_distance_pts (state->pos.sheet, 0,
						       cell_base.start.col);
	frame_offset[1] -= sheet_row_get_distance_pts (state->pos.sheet, 0,
						       cell_base.start.row);
	frame_offset[2] -= sheet_col_get_distance_pts (state->pos.sheet, 0,
						       cell_base.end.col);
	frame_offset[3] -= sheet_row_get_distance_pts (state->pos.sheet, 0,
						       cell_base.end.row);

	col = sheet_col_get_info (state->pos.sheet, cell_base.start.col);
	row = sheet_row_get_info (state->pos.sheet, cell_base.start.row);
	frame_offset[0] /= col->size_pts;
	frame_offset[1] /= row->size_pts;

	col = sheet_col_get_info (state->pos.sheet, cell_base.end.col);
	row = sheet_row_get_info (state->pos.sheet, cell_base.end.row);
	frame_offset[2] /= col->size_pts;
	frame_offset[3] /= row->size_pts;


	sheet_object_anchor_init (&state->chart.anchor, &cell_base,
				  frame_offset,
				  direction);
	state->chart.so = g_object_new (GNM_SO_LINE_TYPE, NULL);

	if (style_name != NULL) {
		OOChartStyle *oostyle = g_hash_table_lookup
			(state->chart.graph_styles, style_name);
		if (oostyle != NULL) {
			GOStyle *style;
			char const *start_marker = NULL;
			char const *end_marker = NULL;
			GSList *l;

			g_object_get (G_OBJECT (state->chart.so),
				      "style", &style, NULL);

			if (style != NULL) {
				odf_apply_style_props (xin, oostyle->style_props,
						       style);
				g_object_unref (style);
			}

			for (l = oostyle->other_props; l != NULL; l = l->next) {
				OOProp *prop = l->data;
				if (0 == strcmp ("marker-start", prop->name))
					start_marker = g_value_get_string (&prop->value);
				else if (0 == strcmp ("marker-end", prop->name))
					end_marker = g_value_get_string (&prop->value);
			}

			if (start_marker != NULL) {
				GOArrow *arrow = odf_get_arrow_marker (state, start_marker);

				if (arrow != NULL) {
					g_object_set (G_OBJECT (state->chart.so),
						      "start-arrow", arrow, NULL);
					g_free (arrow);
				}
			}
			if (end_marker != NULL) {
				GOArrow *arrow = odf_get_arrow_marker (state, end_marker);

				if (arrow != NULL) {
					g_object_set (G_OBJECT (state->chart.so),
						      "end-arrow", arrow, NULL);
					g_free (arrow);
				}
			}
		}
	}

}

/****************************************************************************/
/******************************** controls     ******************************/

static void
odf_form_control (GsfXMLIn *xin, xmlChar const **attrs, GType t)
{
	OOControl *oc = g_new0 (OOControl, 1);
	OOParseState *state = (OOParseState *)xin->user_state;
	char *name = NULL;
	static OOEnum const orientations [] = {
		{ "vertical",	0},
		{ "horizontal",	1},
		{ NULL,	0 },
	};
	static OOEnum const list_linkages [] = {
		{ "selection",	0},
		{ "selection-indexes",	1},
		{ "selection-indices",	1},
		{ NULL,	0 },
	};
	int tmp;

	state->cur_control = NULL;
	oc->step = oc->page_step = 1;
	oc->as_index = TRUE;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		/* ODF does not declare an xml: namespace but uses this attribute */
		if (0 == strcmp (CXML2C (attrs[0]), "xml:id")) {
			g_free (name);
			name = g_strdup (CXML2C (attrs[1]));
		} else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]),
					       OO_NS_FORM, "id")) {
			if (name == NULL)
				name = g_strdup (CXML2C (attrs[1]));
		} else if (oo_attr_enum (xin, attrs, OO_NS_FORM, "orientation", orientations,
					 &tmp))
					       oc->horizontal = (tmp != 0);
		else if (oo_attr_int (xin, attrs, OO_NS_FORM, "min-value",
				      &(oc->min)));
		else if (oo_attr_int (xin, attrs, OO_NS_FORM, "max-value",
				      &(oc->max)));
		else if (oo_attr_int_range (xin, attrs, OO_NS_FORM, "step-size",
					    &(oc->step), 0, INT_MAX));
		else if (oo_attr_int_range (xin, attrs, OO_NS_FORM, "page-step-size",
					    &(oc->page_step), 0, INT_MAX));
		else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]),
						OO_NS_FORM, "value")) {
			g_free (oc->value);
			oc->value = g_strdup (CXML2C (attrs[1]));
		} else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]),
						OO_GNUM_NS_EXT, "value-type")) {
			g_free (oc->value_type);
			oc->value_type = g_strdup (CXML2C (attrs[1]));
		} else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]),
					     OO_NS_FORM, "linked-cell")) {
			g_free (oc->linked_cell);
			oc->linked_cell =  g_strdup (CXML2C (attrs[1]));
		} else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]),
					     OO_GNUM_NS_EXT, "linked-cell")) {
			g_free (oc->linked_cell);
			oc->linked_cell =  g_strdup (CXML2C (attrs[1]));
		} else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]),
					     OO_NS_FORM, "label")) {
			g_free (oc->label);
			oc->label =  g_strdup (CXML2C (attrs[1]));
		} else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]),
					     OO_NS_FORM, "control-implementation")) {
			g_free (oc->implementation);
			oc->implementation =  g_strdup (CXML2C (attrs[1]));
		} else if (oo_attr_enum (xin, attrs, OO_NS_FORM, "list-linkage-type", list_linkages,
					 &tmp))
			oc->as_index = (tmp != 0);
		else if (oo_attr_enum (xin, attrs, OO_GNUM_NS_EXT, "list-linkage-type", list_linkages,
				       &tmp))
			oc->as_index = (tmp != 0);
		else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]),
					     OO_NS_FORM, "source-cell-range")) {
			g_free (oc->source_cell_range);
			oc->source_cell_range =  g_strdup (CXML2C (attrs[1]));
		} else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]),
					     OO_GNUM_NS_EXT, "source-cell-range")) {
			if (oc->source_cell_range == NULL)
				oc->source_cell_range =  g_strdup (CXML2C (attrs[1]));
		} else if (oo_attr_int (xin, attrs, OO_NS_FORM, "bound-column",
					&tmp)) {
			if (tmp != 1)
				oo_warning (xin, _("Attribute '%s' has "
						   "the unsupported value '%s'."),
					    "form:bound-column", CXML2C (attrs[1]));
		}

	if (name != NULL) {
		if (oc->implementation != NULL &&
		    t == sheet_widget_slider_get_type ()) {
			if (0 == strcmp (oc->implementation, "gnm:scrollbar"))
				oc->t = sheet_widget_scrollbar_get_type ();
			else if (0 == strcmp (oc->implementation,
					      "gnm:spinbutton"))
				oc->t = sheet_widget_spinbutton_get_type ();
			else if (0 == strcmp (oc->implementation,
					      "gnm:slider"))
				oc->t = sheet_widget_slider_get_type ();
			else if (0 == strcmp (oc->implementation,
					      "ooo:com.sun.star.form."
					      "component.ScrollBar"))
				oc->t = sheet_widget_scrollbar_get_type ();
		} else if (t == sheet_widget_frame_get_type ()) {
			if (oc->implementation == NULL ||
			    0 != strcmp (oc->implementation, "gnm:frame")) {
				oo_control_free (oc);
				return;
			} else
				oc->t = t;
		} else
			oc->t = t;
		g_hash_table_replace (state->controls, name, oc);
	} else {
		oo_control_free (oc);
		return;
	}

	if (t == sheet_widget_button_get_type () ||
	    t == sheet_widget_frame_get_type ())
		state->cur_control = oc;
}


static void
odf_form_value_range (GsfXMLIn *xin, xmlChar const **attrs)
{

	odf_form_control (xin, attrs, sheet_widget_slider_get_type ());
}

static void
odf_form_checkbox (GsfXMLIn *xin, xmlChar const **attrs)
{
	odf_form_control (xin, attrs, sheet_widget_checkbox_get_type ());
}

static void
odf_form_radio (GsfXMLIn *xin, xmlChar const **attrs)
{
	odf_form_control (xin, attrs, sheet_widget_radio_button_get_type ());
}

static void
odf_form_generic (GsfXMLIn *xin, xmlChar const **attrs)
{
	odf_form_control (xin, attrs, sheet_widget_frame_get_type ());
}

static void
odf_form_listbox (GsfXMLIn *xin, xmlChar const **attrs)
{
	odf_form_control (xin, attrs, sheet_widget_list_get_type ());
}

static void
odf_form_combobox (GsfXMLIn *xin, xmlChar const **attrs)
{
	odf_form_control (xin, attrs, sheet_widget_combo_get_type ());
}

static void
odf_form_button (GsfXMLIn *xin, xmlChar const **attrs)
{
	odf_form_control (xin, attrs, sheet_widget_button_get_type ());
}

static void
odf_form_control_end (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	OOParseState *state = (OOParseState *)xin->user_state;
	state->cur_control = NULL;
}

static void
odf_button_event_listener (GsfXMLIn *xin, xmlChar const **attrs)
{
	OOParseState *state = (OOParseState *)xin->user_state;
	char const *event_name = NULL;
	char const *language = NULL;
	char const *macro_name = NULL;

	if (state->cur_control == NULL)
		return;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]),
					OO_NS_SCRIPT, "event-name"))
			event_name = CXML2C (attrs[1]);
		else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]),
					OO_NS_SCRIPT, "language"))
			language = CXML2C (attrs[1]);
		else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]),
					OO_NS_SCRIPT, "macro-name"))
			macro_name = CXML2C (attrs[1]);

	if (event_name && (0 == strcmp (event_name, "dom:mousedown")) &&
	    language && (0 == strcmp (language, "gnm:short-macro")) &&
	    g_str_has_prefix (macro_name, "set-to-TRUE:"))
		state->cur_control->linked_cell = g_strdup (macro_name + 12);
}

static void
odf_control_property (GsfXMLIn *xin, xmlChar const **attrs)
{
	OOParseState *state = (OOParseState *)xin->user_state;
	char const *property_name = NULL;
	char const *value = NULL;

	if (state->cur_control == NULL)
		return;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]),
					OO_NS_FORM, "property-name"))
			property_name = CXML2C (attrs[1]);
		else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]),
					OO_NS_OFFICE, "string-value"))
			value = CXML2C (attrs[1]);

	if ((property_name != NULL) &&
	    (0 == strcmp (property_name, "gnm:label")) &&
	    (NULL != value))
		state->cur_control->label = g_strdup (value);
}


/****************************************************************************/
/******************************** settings.xml ******************************/

static void
unset_gvalue (gpointer data, G_GNUC_UNUSED gpointer user_data)
{
	g_value_unset (data);
}

static void
destroy_gvalue (gpointer data)
{
	g_value_unset (data);
	g_free (data);
}

static void
odf_config_item_end (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	OOParseState *state = (OOParseState *)xin->user_state;
	GHashTable *parent_hash;

	if (state->settings.stack == NULL)
		parent_hash = state->settings.settings;
	else
		parent_hash = state->settings.stack->data;

	if (parent_hash != NULL && state->settings.config_item_name != NULL) {
		GValue *val = NULL;
		switch (state->settings.type) {
		case G_TYPE_BOOLEAN: {
			gboolean b = (g_ascii_strcasecmp (xin->content->str, "false") &&
				      strcmp (xin->content->str, "0"));
			val = g_value_init (g_new0 (GValue, 1), G_TYPE_BOOLEAN);
			g_value_set_boolean (val, b);
			break;
		}
		case G_TYPE_INT: {
			long n;
			char *end;

			errno = 0; /* strtol sets errno, but does not clear it.  */
			n = strtol (xin->content->str, &end, 10);
			if (!(*end || errno != 0 || n < INT_MIN || n > INT_MAX)) {
				val = g_value_init (g_new0 (GValue, 1), G_TYPE_INT);
				g_value_set_int (val, (int)n);
			}
			break;
		}
		case G_TYPE_LONG: {
			long n;
			char *end;

			errno = 0; /* strtol sets errno, but does not clear it.  */
			n = strtol (xin->content->str, &end, 10);
			if (!(*end || errno != 0)) {
				val = g_value_init (g_new0 (GValue, 1), G_TYPE_LONG);
				g_value_set_long (val, n);
			}
			break;
		}
		default:
			break;
		}
		if (val != NULL)
			g_hash_table_replace
				(parent_hash, g_strdup (state->settings.config_item_name),
				 val);
	}

	g_free (state->settings.config_item_name);
	state->settings.config_item_name = NULL;
}


static void
odf_config_item (GsfXMLIn *xin, xmlChar const **attrs)
{
	static OOEnum const config_types [] = {
		{"base64Binary", G_TYPE_INVALID},
		{"boolean", G_TYPE_BOOLEAN},
		{"datetime", G_TYPE_INVALID},
		{"double", G_TYPE_INVALID},
		{"int", G_TYPE_INT},
		{"long", G_TYPE_LONG},
		{"short", G_TYPE_INVALID},
		{"string", G_TYPE_INVALID},
		{ NULL,	0},
	};
	OOParseState *state = (OOParseState *)xin->user_state;

	state->settings.config_item_name = NULL;
	state->settings.type = G_TYPE_INVALID;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2) {
		int i;

		if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_CONFIG, "name"))
			state->settings.config_item_name = g_strdup (CXML2C (attrs[1]));
		else if (oo_attr_enum (xin, attrs, OO_NS_CONFIG, "type", config_types, &i))
			state->settings.type = i;
	}
}

static void
odf_config_stack_pop (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	OOParseState *state = (OOParseState *)xin->user_state;

	g_return_if_fail (state->settings.stack != NULL);

	g_hash_table_unref (state->settings.stack->data);
	state->settings.stack = g_slist_delete_link
		(state->settings.stack, state->settings.stack);
}

static void
odf_config_item_set (GsfXMLIn *xin, xmlChar const **attrs)
{
	OOParseState *state = (OOParseState *)xin->user_state;
	GHashTable *set = g_hash_table_new_full (g_str_hash, g_str_equal,
						 (GDestroyNotify) g_free,
						 (GDestroyNotify) destroy_gvalue);
	GHashTable *parent_hash;
	gchar *name = NULL;
	GValue *val;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_CONFIG, "name"))
			name = g_strdup (CXML2C (attrs[1]));

	if (state->settings.stack == NULL)
		parent_hash = state->settings.settings;
	else
		parent_hash = state->settings.stack->data;

	if (name == NULL) {
		int i = 0;
		do {
			g_free (name);
			name = g_strdup_printf ("Unnamed_Config_Set-%i", i++);
		} while (NULL != g_hash_table_lookup (parent_hash, name));
	}

	state->settings.stack = g_slist_prepend (state->settings.stack, set);

	val = g_value_init (g_new0 (GValue, 1), G_TYPE_HASH_TABLE);
	g_value_set_boxed (val, set);

	g_hash_table_replace (parent_hash, name, val);
}

static void
dump_settings_hash (char const *key, GValue *val, char const *prefix)
{
	gchar *content = g_strdup_value_contents (val);
	g_print ("%s Settings \'%s\' has \'%s\'\n", prefix, key, content);
	g_free (content);

	if (G_VALUE_HOLDS(val,G_TYPE_HASH_TABLE)) {
		char *pre = g_strconcat (prefix, ">>", NULL);
		GHashTable *hash = g_value_get_boxed (val);
		g_hash_table_foreach (hash, (GHFunc)dump_settings_hash, pre);
		g_free (pre);
	}
}

static gboolean
odf_created_by_gnumeric (OOParseState *state)
{
	GsfDocMetaData *meta_data = go_doc_get_meta_data (GO_DOC (state->pos.wb));
	GsfDocProp *prop = gsf_doc_meta_data_lookup  (meta_data,
						      "meta:generator");
	char const *str;

	return (prop != NULL &&
		(NULL != (str = g_value_get_string
			  (gsf_doc_prop_get_val (prop)))) &&
		g_str_has_prefix (str, "gnumeric"));
}

static gboolean
odf_has_gnm_foreign (OOParseState *state)
{
	GValue *val;
	if ((state->settings.settings != NULL) &&
	    NULL != (val = g_hash_table_lookup (state->settings.settings, "gnm:settings")) &&
	    G_VALUE_HOLDS(val,G_TYPE_HASH_TABLE)) {
		GHashTable *hash =  g_value_get_boxed (val);
		val = g_hash_table_lookup (hash, "gnm:has_foreign");
		if (val != NULL && G_VALUE_HOLDS(val, G_TYPE_BOOLEAN))
			return g_value_get_boolean (val);
	}
	return FALSE;
}

static void
odf_apply_ooo_table_config (char const *key, GValue *val, OOParseState *state)
{
	if (G_VALUE_HOLDS(val,G_TYPE_HASH_TABLE)) {
		GHashTable *hash = g_value_get_boxed (val);
		Sheet *sheet = workbook_sheet_by_name (state->pos.wb, key);
		if (hash != NULL && sheet != NULL) {
			GValue *tab = g_hash_table_lookup (hash, "TabColor");
			if (tab != NULL && G_VALUE_HOLDS(tab, G_TYPE_INT)) {
				GOColor color = g_value_get_int (tab);
				color = color << 8;
				sheet->tab_color = style_color_new_go (color);
			}
		}
	}
}


static void
odf_apply_ooo_config (OOParseState *state)
{
	GValue *val;
	GHashTable *hash;

	if ((state->settings.settings == NULL) ||
	    NULL == (val = g_hash_table_lookup (state->settings.settings, "ooo:view-settings")) ||
	    !G_VALUE_HOLDS(val,G_TYPE_HASH_TABLE))
		return;
	hash =  g_value_get_boxed (val);

	if ((hash == NULL) ||
	    NULL == (val = g_hash_table_lookup (hash, "Views")) ||
	    !G_VALUE_HOLDS(val,G_TYPE_HASH_TABLE))
		return;
	hash =  g_value_get_boxed (val);

	if ((hash == NULL) ||
	    NULL == (val = g_hash_table_lookup (hash, "Unnamed_Config_Set-0")) ||
	    !G_VALUE_HOLDS(val,G_TYPE_HASH_TABLE))
		return;
	hash =  g_value_get_boxed (val);

	if ((hash == NULL) ||
	    NULL == (val = g_hash_table_lookup (hash, "Tables")) ||
	    !G_VALUE_HOLDS(val,G_TYPE_HASH_TABLE))
		return;
	hash =  g_value_get_boxed (val);

	if (hash == NULL)
		return;

	g_hash_table_foreach (hash, (GHFunc) odf_apply_ooo_table_config, state);
}

/**************************************************************************/

static void
oo_marker (GsfXMLIn *xin, xmlChar const **attrs)
{
	OOParseState *state = (OOParseState *)xin->user_state;
	OOMarker *marker = g_new0 (OOMarker, 1);
        int type = GO_ARROW_NONE;
	double a = 0., b = 0., c = 0.;
	char const *name = NULL;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]),
					OO_NS_DRAW, "name"))
			name = CXML2C (attrs[1]);
		else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]),
					     OO_NS_SVG, "viewBox"))
			marker->view_box = g_strdup (CXML2C (attrs[1]));
		else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]),
					     OO_NS_SVG, "d"))
			marker->d = g_strdup (CXML2C (attrs[1]));
		else if (oo_attr_int_range (xin, attrs, OO_GNUM_NS_EXT, "arrow-type", &type,
					    GO_ARROW_KITE, GO_ARROW_OVAL));
		else if (oo_attr_float (xin, attrs, OO_GNUM_NS_EXT,
					"arrow-a", &a));
		else if (oo_attr_float (xin, attrs, OO_GNUM_NS_EXT,
					"arrow-b", &b));
		else if (oo_attr_float (xin, attrs, OO_GNUM_NS_EXT,
					"arrow-c", &c));
	if (type != GO_ARROW_NONE) {
		marker->arrow = g_new0 (GOArrow, 1);
		go_arrow_init (marker->arrow, type, a, b, c);
	}
	if (name != NULL) {
		g_hash_table_replace (state->chart.arrow_markers,
				      g_strdup (name), marker);
	} else
		oo_marker_free (marker);

}

/**************************************************************************/

static void
odf_find_version (GsfXMLIn *xin, xmlChar const **attrs)
{
	OOParseState *state = (OOParseState *)xin->user_state;
	
	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (oo_attr_float (xin, attrs, OO_NS_OFFICE,
					"version", &state->ver_odf));
}

/**************************************************************************/

static GsfXMLInNode const styles_dtd[] = {
GSF_XML_IN_NODE_FULL (START, START, -1, NULL, GSF_XML_NO_CONTENT, FALSE, TRUE, NULL, NULL, 0),

/* ooo-1.x */
GSF_XML_IN_NODE (START, OFFICE_FONTS_OOO1, OO_NS_OFFICE, "font-decls", GSF_XML_NO_CONTENT, NULL, NULL),
  GSF_XML_IN_NODE (OFFICE_FONTS_OOO1, FONT_DECL_OOO1, OO_NS_STYLE, "font-decl", GSF_XML_NO_CONTENT, NULL, NULL),

/* ooo-2.x, ooo-3.x */
GSF_XML_IN_NODE (START, OFFICE_DOC_STYLES, OO_NS_OFFICE, "document-styles", GSF_XML_NO_CONTENT, &odf_find_version, NULL),
GSF_XML_IN_NODE (OFFICE_DOC_STYLES, OFFICE_FONTS, OO_NS_OFFICE, "font-face-decls", GSF_XML_NO_CONTENT, NULL, NULL),
  GSF_XML_IN_NODE (OFFICE_FONTS, FONT_DECL, OO_NS_STYLE, "font-face", GSF_XML_NO_CONTENT, NULL, NULL), /* 2nd */

GSF_XML_IN_NODE (OFFICE_DOC_STYLES, OFFICE_STYLES, OO_NS_OFFICE, "styles", GSF_XML_NO_CONTENT, NULL, NULL),
  GSF_XML_IN_NODE (OFFICE_STYLES, FILLIMAGE, OO_NS_DRAW, "fill-image", GSF_XML_NO_CONTENT, &oo_fill_image, NULL),
  GSF_XML_IN_NODE (OFFICE_STYLES, DASH, OO_NS_DRAW, "stroke-dash", GSF_XML_NO_CONTENT, &oo_dash, NULL),
  GSF_XML_IN_NODE (OFFICE_STYLES, HATCH, OO_NS_DRAW, "hatch", GSF_XML_NO_CONTENT, &oo_hatch, NULL),
  GSF_XML_IN_NODE (OFFICE_STYLES, GRADIENT, OO_NS_DRAW, "gradient", GSF_XML_NO_CONTENT, &oo_gradient, NULL),
  GSF_XML_IN_NODE (OFFICE_STYLES, MARKER, OO_NS_DRAW, "marker", GSF_XML_NO_CONTENT, &oo_marker, NULL),
  GSF_XML_IN_NODE (OFFICE_STYLES, STYLE, OO_NS_STYLE, "style", GSF_XML_NO_CONTENT, &oo_style, &oo_style_end),
    GSF_XML_IN_NODE (STYLE, TABLE_CELL_PROPS, OO_NS_STYLE,	"table-cell-properties", GSF_XML_NO_CONTENT, &oo_style_prop, NULL),
    GSF_XML_IN_NODE (STYLE, TEXT_PROP, OO_NS_STYLE,		"text-properties", GSF_XML_NO_CONTENT, &oo_style_prop, NULL),
    GSF_XML_IN_NODE (STYLE, PARAGRAPH_PROPS, OO_NS_STYLE,	"paragraph-properties", GSF_XML_NO_CONTENT, &oo_style_prop, NULL),
      GSF_XML_IN_NODE (PARAGRAPH_PROPS, PARA_TABS, OO_NS_STYLE,  "tab-stops", GSF_XML_NO_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE (STYLE, STYLE_PROP, OO_NS_STYLE,		"properties", GSF_XML_NO_CONTENT, &oo_style_prop, NULL),
      GSF_XML_IN_NODE (STYLE_PROP, STYLE_TAB_STOPS, OO_NS_STYLE, "tab-stops", GSF_XML_NO_CONTENT, NULL, NULL),

  GSF_XML_IN_NODE (OFFICE_STYLES, DEFAULT_STYLE, OO_NS_STYLE, "default-style", GSF_XML_NO_CONTENT, &oo_style, &oo_style_end),
    GSF_XML_IN_NODE (DEFAULT_STYLE, DEFAULT_TABLE_CELL_PROPS, OO_NS_STYLE, "table-cell-properties", GSF_XML_NO_CONTENT, &oo_style_prop, NULL),
    GSF_XML_IN_NODE (DEFAULT_STYLE, DEFAULT_TEXT_PROP, OO_NS_STYLE,	   "text-properties", GSF_XML_NO_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE (DEFAULT_STYLE, DEFAULT_GRAPHIC_PROPS, OO_NS_STYLE,	   "graphic-properties", GSF_XML_NO_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE (DEFAULT_STYLE, DEFAULT_PARAGRAPH_PROPS, OO_NS_STYLE,  "paragraph-properties", GSF_XML_NO_CONTENT, &oo_style_prop, NULL),
      GSF_XML_IN_NODE (DEFAULT_PARAGRAPH_PROPS, DEFAULT_PARA_TABS, OO_NS_STYLE,  "tab-stops", GSF_XML_NO_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE (DEFAULT_STYLE, DEFAULT_STYLE_PROP, OO_NS_STYLE,	   "properties", GSF_XML_NO_CONTENT, &oo_style_prop, NULL),
      GSF_XML_IN_NODE (DEFAULT_STYLE_PROP, STYLE_TAB_STOPS, OO_NS_STYLE, "tab-stops", GSF_XML_NO_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE (DEFAULT_STYLE, DEFAULT_TABLE_COL_PROPS, OO_NS_STYLE, "table-column-properties", GSF_XML_NO_CONTENT, &oo_style_prop, NULL),
    GSF_XML_IN_NODE (DEFAULT_STYLE, DEFAULT_TABLE_ROW_PROPS, OO_NS_STYLE, "table-row-properties", GSF_XML_NO_CONTENT, &oo_style_prop, NULL),

  GSF_XML_IN_NODE (OFFICE_STYLES, NUMBER_STYLE, OO_NS_NUMBER, "number-style", GSF_XML_NO_CONTENT, &odf_number_style, &odf_number_style_end),
    GSF_XML_IN_NODE (NUMBER_STYLE, NUMBER_STYLE_NUMBER, OO_NS_NUMBER,	"number", GSF_XML_NO_CONTENT, &odf_number, NULL),
       GSF_XML_IN_NODE (NUMBER_STYLE_NUMBER, NUMBER_EMBEDDED_TEXT, OO_NS_NUMBER, "embedded-text", GSF_XML_NO_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE (NUMBER_STYLE, NUMBER_STYLE_TEXT, OO_NS_NUMBER,	"text", GSF_XML_CONTENT, NULL, &oo_date_text_end),
    GSF_XML_IN_NODE (NUMBER_STYLE, NUMBER_STYLE_FRACTION, OO_NS_NUMBER, "fraction", GSF_XML_NO_CONTENT, &odf_fraction, NULL),
    GSF_XML_IN_NODE (NUMBER_STYLE, NUMBER_SCI_STYLE_PROP, OO_NS_NUMBER, "scientific-number", GSF_XML_NO_CONTENT, &odf_scientific, NULL),
    GSF_XML_IN_NODE (NUMBER_STYLE, NUMBER_STYLE_PROP, OO_NS_STYLE,	"properties", GSF_XML_NO_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE (NUMBER_STYLE, NUMBER_MAP, OO_NS_STYLE,		"map", GSF_XML_NO_CONTENT, &odf_map, NULL),
    GSF_XML_IN_NODE (NUMBER_STYLE, NUMBER_TEXT_PROP, OO_NS_STYLE,	"text-properties", GSF_XML_NO_CONTENT, &odf_number_color, NULL),

  GSF_XML_IN_NODE (OFFICE_STYLES, DATE_STYLE, OO_NS_NUMBER, "date-style", GSF_XML_NO_CONTENT, &oo_date_style, &oo_date_style_end),
    GSF_XML_IN_NODE (DATE_STYLE, DATE_DAY, OO_NS_NUMBER,		"day", GSF_XML_NO_CONTENT,	&oo_date_day, NULL),
    GSF_XML_IN_NODE (DATE_STYLE, DATE_MONTH, OO_NS_NUMBER,		"month", GSF_XML_NO_CONTENT,	&oo_date_month, NULL),
    GSF_XML_IN_NODE (DATE_STYLE, DATE_YEAR, OO_NS_NUMBER,		"year", GSF_XML_NO_CONTENT,	&oo_date_year, NULL),
    GSF_XML_IN_NODE (DATE_STYLE, DATE_ERA, OO_NS_NUMBER,		"era", GSF_XML_NO_CONTENT,	&oo_date_era, NULL),
    GSF_XML_IN_NODE (DATE_STYLE, DATE_DAY_OF_WEEK, OO_NS_NUMBER,	"day-of-week", GSF_XML_NO_CONTENT, &oo_date_day_of_week, NULL),
    GSF_XML_IN_NODE (DATE_STYLE, DATE_WEEK_OF_YEAR, OO_NS_NUMBER,	"week-of-year", GSF_XML_NO_CONTENT, &oo_date_week_of_year, NULL),
    GSF_XML_IN_NODE (DATE_STYLE, DATE_QUARTER, OO_NS_NUMBER,		"quarter", GSF_XML_NO_CONTENT, &oo_date_quarter, NULL),
    GSF_XML_IN_NODE (DATE_STYLE, DATE_HOURS, OO_NS_NUMBER,		"hours", GSF_XML_NO_CONTENT,	&oo_date_hours, NULL),
    GSF_XML_IN_NODE (DATE_STYLE, DATE_MINUTES, OO_NS_NUMBER,		"minutes", GSF_XML_NO_CONTENT, &oo_date_minutes, NULL),
    GSF_XML_IN_NODE (DATE_STYLE, DATE_SECONDS, OO_NS_NUMBER,		"seconds", GSF_XML_NO_CONTENT, &oo_date_seconds, NULL),
    GSF_XML_IN_NODE (DATE_STYLE, DATE_AM_PM, OO_NS_NUMBER,		"am-pm", GSF_XML_NO_CONTENT,	&oo_date_am_pm, NULL),
    GSF_XML_IN_NODE (DATE_STYLE, DATE_TEXT, OO_NS_NUMBER,		"text", GSF_XML_CONTENT,	NULL, &oo_date_text_end),
    GSF_XML_IN_NODE (DATE_STYLE, DATE_TEXT_PROP, OO_NS_STYLE,		"text-properties", GSF_XML_NO_CONTENT, &odf_number_color, NULL),
    GSF_XML_IN_NODE (DATE_STYLE, DATE_MAP, OO_NS_STYLE,			"map", GSF_XML_NO_CONTENT, NULL, NULL),

  GSF_XML_IN_NODE (OFFICE_STYLES, TIME_STYLE, OO_NS_NUMBER, "time-style", GSF_XML_NO_CONTENT, &oo_date_style, &oo_date_style_end),
    GSF_XML_IN_NODE (TIME_STYLE, TIME_HOURS, OO_NS_NUMBER,		"hours", GSF_XML_NO_CONTENT,	&oo_date_hours, NULL),
    GSF_XML_IN_NODE (TIME_STYLE, TIME_MINUTES, OO_NS_NUMBER,		"minutes", GSF_XML_NO_CONTENT, &oo_date_minutes, NULL),
    GSF_XML_IN_NODE (TIME_STYLE, TIME_SECONDS, OO_NS_NUMBER,		"seconds", GSF_XML_NO_CONTENT, &oo_date_seconds, NULL),
    GSF_XML_IN_NODE (TIME_STYLE, TIME_AM_PM, OO_NS_NUMBER,		"am-pm", GSF_XML_NO_CONTENT,	&oo_date_am_pm, NULL),
    GSF_XML_IN_NODE (TIME_STYLE, TIME_TEXT, OO_NS_NUMBER,		"text", GSF_XML_CONTENT,	NULL, &oo_date_text_end),
    GSF_XML_IN_NODE (TIME_STYLE, TIME_TEXT_PROP, OO_NS_STYLE,		"text-properties", GSF_XML_NO_CONTENT, &odf_number_color, NULL),
    GSF_XML_IN_NODE (TIME_STYLE, TIME_MAP, OO_NS_STYLE,			"map", GSF_XML_NO_CONTENT, NULL, NULL),

  GSF_XML_IN_NODE (OFFICE_STYLES, STYLE_BOOL, OO_NS_NUMBER, "boolean-style", GSF_XML_NO_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE (STYLE_BOOL, BOOL_PROP, OO_NS_NUMBER, "boolean", GSF_XML_NO_CONTENT, NULL, NULL),

  GSF_XML_IN_NODE (OFFICE_STYLES, STYLE_CURRENCY, OO_NS_NUMBER,		"currency-style", GSF_XML_NO_CONTENT, &odf_number_style, &odf_number_style_end),
    GSF_XML_IN_NODE (STYLE_CURRENCY, CURRENCY_STYLE, OO_NS_NUMBER,	"number", GSF_XML_NO_CONTENT, &odf_number, NULL),
    GSF_XML_IN_NODE (STYLE_CURRENCY, CURRENCY_STYLE_PROP, OO_NS_STYLE,	"properties", GSF_XML_NO_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE (STYLE_CURRENCY, CURRENCY_MAP, OO_NS_STYLE,		"map", GSF_XML_NO_CONTENT, &odf_map, NULL),
    GSF_XML_IN_NODE (STYLE_CURRENCY, CURRENCY_SYMBOL, OO_NS_NUMBER,	"currency-symbol", GSF_XML_CONTENT, NULL, &odf_currency_symbol_end),
    GSF_XML_IN_NODE (STYLE_CURRENCY, CURRENCY_TEXT, OO_NS_NUMBER,	"text", GSF_XML_CONTENT, NULL, &oo_date_text_end),
    GSF_XML_IN_NODE (STYLE_CURRENCY, CURRENCY_TEXT_PROP, OO_NS_STYLE,	"text-properties", GSF_XML_NO_CONTENT, &odf_number_color, NULL),

  GSF_XML_IN_NODE (OFFICE_STYLES, STYLE_PERCENTAGE, OO_NS_NUMBER, "percentage-style", GSF_XML_NO_CONTENT, &odf_number_percentage_style, &odf_number_style_end),
    GSF_XML_IN_NODE (STYLE_PERCENTAGE, PERCENTAGE_STYLE_PROP, OO_NS_NUMBER,	"number", GSF_XML_NO_CONTENT, &odf_number, NULL),
    GSF_XML_IN_NODE (STYLE_PERCENTAGE, PERCENTAGE_TEXT, OO_NS_NUMBER,		"text", GSF_XML_CONTENT, NULL, &oo_date_text_end),
    GSF_XML_IN_NODE (STYLE_PERCENTAGE, PERCENTAGE_MAP, OO_NS_STYLE,		"map", GSF_XML_NO_CONTENT, &odf_map, NULL),
    GSF_XML_IN_NODE (STYLE_PERCENTAGE, PERCENTAGE_TEXT_PROP, OO_NS_STYLE,	"text-properties", GSF_XML_NO_CONTENT, &odf_number_color, NULL),

  GSF_XML_IN_NODE (OFFICE_STYLES, STYLE_TEXT, OO_NS_NUMBER, "text-style", GSF_XML_NO_CONTENT, &odf_number_style, &odf_number_style_end),
    GSF_XML_IN_NODE (STYLE_TEXT, STYLE_TEXT_CONTENT, OO_NS_NUMBER,	"text-content", GSF_XML_NO_CONTENT,  &odf_text_content, NULL),
    GSF_XML_IN_NODE (STYLE_TEXT, STYLE_TEXT_PROP, OO_NS_NUMBER,		"text", GSF_XML_CONTENT, NULL, &oo_date_text_end),
    GSF_XML_IN_NODE (STYLE_TEXT, STYLE_TEXT_MAP, OO_NS_STYLE,		"map", GSF_XML_NO_CONTENT, &odf_map, NULL),
    GSF_XML_IN_NODE (STYLE_TEXT, STYLE_TEXT_TEXT_PROP, OO_NS_STYLE,	"text-properties", GSF_XML_NO_CONTENT, &odf_number_color, NULL),

GSF_XML_IN_NODE (OFFICE_DOC_STYLES, AUTOMATIC_STYLES, OO_NS_OFFICE, "automatic-styles", GSF_XML_NO_CONTENT, NULL, NULL),
  GSF_XML_IN_NODE (AUTOMATIC_STYLES, STYLE, OO_NS_STYLE, "style", GSF_XML_NO_CONTENT, NULL, NULL), /* 2nd */
  GSF_XML_IN_NODE (AUTOMATIC_STYLES, PAGE_LAYOUT, OO_NS_STYLE, "page-layout", GSF_XML_NO_CONTENT, NULL, NULL),
  GSF_XML_IN_NODE (AUTOMATIC_STYLES, NUMBER_STYLE, OO_NS_NUMBER, "number-style", GSF_XML_NO_CONTENT, NULL, NULL), /* 2nd */
  GSF_XML_IN_NODE (AUTOMATIC_STYLES, DATE_STYLE, OO_NS_NUMBER, "date-style", GSF_XML_NO_CONTENT, NULL, NULL), /* 2nd */
  GSF_XML_IN_NODE (AUTOMATIC_STYLES, TIME_STYLE, OO_NS_NUMBER, "time-style", GSF_XML_NO_CONTENT, NULL, NULL), /* 2nd */
  GSF_XML_IN_NODE (AUTOMATIC_STYLES, STYLE_BOOL, OO_NS_NUMBER, "boolean-style", GSF_XML_NO_CONTENT, NULL, NULL), /* 2nd */
  GSF_XML_IN_NODE (AUTOMATIC_STYLES, STYLE_CURRENCY, OO_NS_NUMBER,   "currency-style", GSF_XML_NO_CONTENT, NULL, NULL), /* 2nd */
  GSF_XML_IN_NODE (AUTOMATIC_STYLES, STYLE_PERCENTAGE, OO_NS_NUMBER, "percentage-style", GSF_XML_NO_CONTENT, NULL, NULL), /* 2nd */
  GSF_XML_IN_NODE (AUTOMATIC_STYLES, STYLE_TEXT, OO_NS_NUMBER, "text-style", GSF_XML_NO_CONTENT, NULL, NULL), /* 2nd */

GSF_XML_IN_NODE (OFFICE_DOC_STYLES, MASTER_STYLES, OO_NS_OFFICE, "master-styles", GSF_XML_NO_CONTENT, NULL, NULL),
  GSF_XML_IN_NODE (MASTER_STYLES, MASTER_PAGE, OO_NS_STYLE, "master-page", GSF_XML_NO_CONTENT, NULL, NULL),
  GSF_XML_IN_NODE (MASTER_PAGE, MASTER_PAGE_HEADER_LEFT, OO_NS_STYLE, "header-left", GSF_XML_NO_CONTENT, NULL, NULL),
  GSF_XML_IN_NODE (MASTER_PAGE, MASTER_PAGE_FOOTER_LEFT, OO_NS_STYLE, "footer-left", GSF_XML_NO_CONTENT, NULL, NULL),
  GSF_XML_IN_NODE (MASTER_PAGE, MASTER_PAGE_FOOTER, OO_NS_STYLE, "footer", GSF_XML_NO_CONTENT, NULL, NULL),
GSF_XML_IN_NODE_END
};

static GsfXMLInNode const ooo1_content_dtd [] = {
GSF_XML_IN_NODE_FULL (START, START, -1, NULL, GSF_XML_NO_CONTENT, FALSE, TRUE, NULL, NULL, 0),
GSF_XML_IN_NODE (START, OFFICE, OO_NS_OFFICE, "document-content", GSF_XML_NO_CONTENT, NULL, NULL),
  GSF_XML_IN_NODE (OFFICE, SCRIPT, OO_NS_OFFICE, "script", GSF_XML_NO_CONTENT, NULL, NULL),
  GSF_XML_IN_NODE (OFFICE, OFFICE_FONTS, OO_NS_OFFICE, "font-decls", GSF_XML_NO_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE (OFFICE_FONTS, FONT_DECL, OO_NS_STYLE, "font-decl", GSF_XML_NO_CONTENT, NULL, NULL),
  GSF_XML_IN_NODE (OFFICE, OFFICE_STYLES, OO_NS_OFFICE, "automatic-styles", GSF_XML_NO_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE (OFFICE_STYLES, STYLE, OO_NS_STYLE, "style", GSF_XML_NO_CONTENT, &oo_style, &oo_style_end),
      GSF_XML_IN_NODE (STYLE, STYLE_PROP, OO_NS_STYLE, "properties", GSF_XML_NO_CONTENT, &oo_style_prop, NULL),
        GSF_XML_IN_NODE (STYLE_PROP, STYLE_TAB_STOPS, OO_NS_STYLE, "tab-stops", GSF_XML_NO_CONTENT, NULL, NULL),

    GSF_XML_IN_NODE (OFFICE_STYLES, NUMBER_STYLE, OO_NS_NUMBER, "number-style", GSF_XML_NO_CONTENT, NULL, NULL),
      GSF_XML_IN_NODE (NUMBER_STYLE, NUMBER_STYLE_NUMBER, OO_NS_NUMBER,	  "number", GSF_XML_NO_CONTENT, NULL, NULL),
      GSF_XML_IN_NODE (NUMBER_STYLE, NUMBER_STYLE_TEXT, OO_NS_NUMBER,	  "text", GSF_XML_NO_CONTENT, NULL, NULL),
      GSF_XML_IN_NODE (NUMBER_STYLE, NUMBER_STYLE_FRACTION, OO_NS_NUMBER, "fraction", GSF_XML_NO_CONTENT, NULL, NULL),
      GSF_XML_IN_NODE (NUMBER_STYLE, NUMBER_SCI_STYLE_PROP, OO_NS_NUMBER, "scientific-number", GSF_XML_NO_CONTENT, NULL, NULL),
      GSF_XML_IN_NODE (NUMBER_STYLE, NUMBER_STYLE_PROP, OO_NS_STYLE,	  "properties", GSF_XML_NO_CONTENT, NULL, NULL),
      GSF_XML_IN_NODE (NUMBER_STYLE, NUMBER_MAP, OO_NS_STYLE,		  "map", GSF_XML_NO_CONTENT, NULL, NULL),

    GSF_XML_IN_NODE (OFFICE_STYLES, DATE_STYLE, OO_NS_NUMBER, "date-style", GSF_XML_NO_CONTENT, &oo_date_style, &oo_date_style_end),
      GSF_XML_IN_NODE (DATE_STYLE, DATE_DAY, OO_NS_NUMBER,		"day", GSF_XML_NO_CONTENT,	&oo_date_day, NULL),
      GSF_XML_IN_NODE (DATE_STYLE, DATE_MONTH, OO_NS_NUMBER,		"month", GSF_XML_NO_CONTENT,	&oo_date_month, NULL),
      GSF_XML_IN_NODE (DATE_STYLE, DATE_YEAR, OO_NS_NUMBER,		"year", GSF_XML_NO_CONTENT,	&oo_date_year, NULL),
      GSF_XML_IN_NODE (DATE_STYLE, DATE_ERA, OO_NS_NUMBER,		"era", GSF_XML_NO_CONTENT,	&oo_date_era, NULL),
      GSF_XML_IN_NODE (DATE_STYLE, DATE_DAY_OF_WEEK, OO_NS_NUMBER,	"day-of-week", GSF_XML_NO_CONTENT, &oo_date_day_of_week, NULL),
      GSF_XML_IN_NODE (DATE_STYLE, DATE_WEEK_OF_YEAR, OO_NS_NUMBER,	"week-of-year", GSF_XML_NO_CONTENT, &oo_date_week_of_year, NULL),
      GSF_XML_IN_NODE (DATE_STYLE, DATE_QUARTER, OO_NS_NUMBER,		"quarter", GSF_XML_NO_CONTENT, &oo_date_quarter, NULL),
      GSF_XML_IN_NODE (DATE_STYLE, DATE_HOURS, OO_NS_NUMBER,		"hours", GSF_XML_NO_CONTENT,	&oo_date_hours, NULL),
      GSF_XML_IN_NODE (DATE_STYLE, DATE_MINUTES, OO_NS_NUMBER,		"minutes", GSF_XML_NO_CONTENT, &oo_date_minutes, NULL),
      GSF_XML_IN_NODE (DATE_STYLE, DATE_SECONDS, OO_NS_NUMBER,		"seconds", GSF_XML_NO_CONTENT, &oo_date_seconds, NULL),
      GSF_XML_IN_NODE (DATE_STYLE, DATE_AM_PM, OO_NS_NUMBER,		"am-pm", GSF_XML_NO_CONTENT,	&oo_date_am_pm, NULL),
      GSF_XML_IN_NODE (DATE_STYLE, DATE_TEXT, OO_NS_NUMBER,		"text", GSF_XML_CONTENT,	NULL, &oo_date_text_end),
      GSF_XML_IN_NODE (DATE_STYLE, DATE_TEXT_PROP, OO_NS_STYLE,		"text-properties", GSF_XML_NO_CONTENT, NULL, NULL),
      GSF_XML_IN_NODE (DATE_STYLE, DATE_MAP, OO_NS_STYLE,		"map", GSF_XML_NO_CONTENT, NULL, NULL),

    GSF_XML_IN_NODE (OFFICE_STYLES, TIME_STYLE, OO_NS_NUMBER, "time-style", GSF_XML_NO_CONTENT, &oo_date_style, &oo_date_style_end),
      GSF_XML_IN_NODE (TIME_STYLE, TIME_HOURS, OO_NS_NUMBER,		"hours", GSF_XML_NO_CONTENT,	&oo_date_hours, NULL),
      GSF_XML_IN_NODE (TIME_STYLE, TIME_MINUTES, OO_NS_NUMBER,		"minutes", GSF_XML_NO_CONTENT, &oo_date_minutes, NULL),
      GSF_XML_IN_NODE (TIME_STYLE, TIME_SECONDS, OO_NS_NUMBER,		"seconds", GSF_XML_NO_CONTENT, &oo_date_seconds, NULL),
      GSF_XML_IN_NODE (TIME_STYLE, TIME_AM_PM, OO_NS_NUMBER,		"am-pm", GSF_XML_NO_CONTENT,	&oo_date_am_pm, NULL),
      GSF_XML_IN_NODE (TIME_STYLE, TIME_TEXT, OO_NS_NUMBER,		"text", GSF_XML_CONTENT,	NULL, &oo_date_text_end),
      GSF_XML_IN_NODE (TIME_STYLE, TIME_TEXT_PROP, OO_NS_STYLE,		"text-properties", GSF_XML_NO_CONTENT, NULL, NULL),
      GSF_XML_IN_NODE (TIME_STYLE, TIME_MAP, OO_NS_STYLE,		"map", GSF_XML_NO_CONTENT, NULL, NULL),

    GSF_XML_IN_NODE (OFFICE_STYLES, STYLE_BOOL, OO_NS_NUMBER, "boolean-style", GSF_XML_NO_CONTENT, NULL, NULL),
      GSF_XML_IN_NODE (STYLE_BOOL, BOOL_PROP, OO_NS_NUMBER, "boolean", GSF_XML_NO_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE (OFFICE_STYLES, STYLE_CURRENCY, OO_NS_NUMBER, "currency-style", GSF_XML_NO_CONTENT, NULL, NULL),
      GSF_XML_IN_NODE (STYLE_CURRENCY, CURRENCY_STYLE, OO_NS_NUMBER, "number", GSF_XML_NO_CONTENT, NULL, NULL),
      GSF_XML_IN_NODE (STYLE_CURRENCY, CURRENCY_STYLE_PROP, OO_NS_STYLE, "properties", GSF_XML_NO_CONTENT, NULL, NULL),
      GSF_XML_IN_NODE (STYLE_CURRENCY, CURRENCY_MAP, OO_NS_STYLE, "map", GSF_XML_NO_CONTENT, NULL, NULL),
      GSF_XML_IN_NODE (STYLE_CURRENCY, CURRENCY_SYMBOL, OO_NS_NUMBER, "currency-symbol", GSF_XML_NO_CONTENT, NULL, NULL),
      GSF_XML_IN_NODE (STYLE_CURRENCY, CURRENCY_TEXT, OO_NS_NUMBER, "text", GSF_XML_NO_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE (OFFICE_STYLES, STYLE_PERCENTAGE, OO_NS_NUMBER, "percentage-style", GSF_XML_NO_CONTENT, NULL, NULL),
      GSF_XML_IN_NODE (STYLE_PERCENTAGE, PERCENTAGE_STYLE_PROP, OO_NS_NUMBER, "number", GSF_XML_NO_CONTENT, NULL, NULL),
      GSF_XML_IN_NODE (STYLE_PERCENTAGE, PERCENTAGE_TEXT, OO_NS_NUMBER, "text", GSF_XML_NO_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE (OFFICE_STYLES, STYLE_TEXT, OO_NS_NUMBER, "text-style", GSF_XML_NO_CONTENT, NULL, NULL),
      GSF_XML_IN_NODE (STYLE_TEXT, STYLE_TEXT_CONTENT, OO_NS_NUMBER,	"text-content", GSF_XML_NO_CONTENT, NULL, NULL),
      GSF_XML_IN_NODE (STYLE_TEXT, STYLE_TEXT_PROP, OO_NS_NUMBER,		"text", GSF_XML_NO_CONTENT, NULL, NULL),
      GSF_XML_IN_NODE (STYLE_TEXT, STYLE_TEXT_MAP, OO_NS_STYLE,		"map", GSF_XML_NO_CONTENT, NULL, NULL),

  GSF_XML_IN_NODE (OFFICE, OFFICE_BODY, OO_NS_OFFICE, "body", GSF_XML_NO_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE (OFFICE_BODY, TABLE_CALC_SETTINGS, OO_NS_TABLE, "calculation-settings", GSF_XML_NO_CONTENT, NULL, NULL),
      GSF_XML_IN_NODE (TABLE_CALC_SETTINGS, DATE_CONVENTION, OO_NS_TABLE, "null-date", GSF_XML_NO_CONTENT, oo_date_convention, NULL),
      GSF_XML_IN_NODE (TABLE_CALC_SETTINGS, ITERATION, OO_NS_TABLE, "iteration", GSF_XML_NO_CONTENT, oo_iteration, NULL),
    GSF_XML_IN_NODE (OFFICE_BODY, VALIDATIONS, OO_NS_TABLE, "content-validations", GSF_XML_NO_CONTENT, NULL, NULL),
      GSF_XML_IN_NODE (VALIDATIONS, VALIDATION, OO_NS_TABLE, "content-validation", GSF_XML_NO_CONTENT, NULL, NULL),
        GSF_XML_IN_NODE (VALIDATION, VALIDATION_MSG, OO_NS_TABLE, "error-message", GSF_XML_NO_CONTENT, NULL, NULL),

    GSF_XML_IN_NODE (OFFICE_BODY, TABLE, OO_NS_TABLE, "table", GSF_XML_NO_CONTENT, &oo_table_start, &oo_table_end),
      GSF_XML_IN_NODE (TABLE, FORMS,	 OO_NS_OFFICE, "forms", GSF_XML_NO_CONTENT, NULL, NULL),
      GSF_XML_IN_NODE (TABLE, TABLE_COL, OO_NS_TABLE, "table-column", GSF_XML_NO_CONTENT, &oo_col_start, NULL),
      GSF_XML_IN_NODE (TABLE, TABLE_ROW, OO_NS_TABLE, "table-row", GSF_XML_NO_CONTENT, &oo_row_start, &oo_row_end),
	GSF_XML_IN_NODE (TABLE_ROW, TABLE_CELL, OO_NS_TABLE, "table-cell", GSF_XML_NO_CONTENT, &oo_cell_start, &oo_cell_end),
	  GSF_XML_IN_NODE (TABLE_CELL, CELL_CONTROL, OO_NS_DRAW, "control", GSF_XML_NO_CONTENT, NULL, NULL),
	  GSF_XML_IN_NODE (TABLE_CELL, CELL_TEXT, OO_NS_TEXT, "p", GSF_XML_CONTENT, NULL, &oo_cell_content_end),
	    GSF_XML_IN_NODE (CELL_TEXT, CELL_TEXT_S,    OO_NS_TEXT, "s", GSF_XML_NO_CONTENT, NULL, NULL),
	    GSF_XML_IN_NODE (CELL_TEXT, CELL_TEXT_SPAN, OO_NS_TEXT, "span", GSF_XML_SHARED_CONTENT, NULL, NULL),
	  GSF_XML_IN_NODE (TABLE_CELL, CELL_OBJECT, OO_NS_DRAW, "object", GSF_XML_NO_CONTENT, NULL, NULL),		/* ignore for now */
	  GSF_XML_IN_NODE (TABLE_CELL, CELL_GRAPHIC, OO_NS_DRAW, "g", GSF_XML_NO_CONTENT, NULL, NULL),		/* ignore for now */
	    GSF_XML_IN_NODE (CELL_GRAPHIC, CELL_GRAPHIC, OO_NS_DRAW, "g", GSF_XML_NO_CONTENT, NULL, NULL),		/* 2nd def */
	    GSF_XML_IN_NODE (CELL_GRAPHIC, DRAW_POLYLINE, OO_NS_DRAW, "polyline", GSF_XML_NO_CONTENT, NULL, NULL),	/* 2nd def */
	GSF_XML_IN_NODE (TABLE_ROW, TABLE_COVERED_CELL, OO_NS_TABLE, "covered-table-cell", GSF_XML_NO_CONTENT, &oo_covered_cell_start, &oo_covered_cell_end),
      GSF_XML_IN_NODE (TABLE, TABLE_COL_GROUP, OO_NS_TABLE, "table-column-group", GSF_XML_NO_CONTENT, NULL, NULL),
        GSF_XML_IN_NODE (TABLE_COL_GROUP, TABLE_COL_GROUP, OO_NS_TABLE, "table-column-group", GSF_XML_NO_CONTENT, NULL, NULL),
        GSF_XML_IN_NODE (TABLE_COL_GROUP, TABLE_COL, OO_NS_TABLE, "table-column", GSF_XML_NO_CONTENT, NULL, NULL), /* 2nd def */
      GSF_XML_IN_NODE (TABLE, TABLE_ROW_GROUP,	      OO_NS_TABLE, "table-row-group", GSF_XML_NO_CONTENT, NULL, NULL),
        GSF_XML_IN_NODE (TABLE_ROW_GROUP, TABLE_ROW_GROUP, OO_NS_TABLE, "table-row-group", GSF_XML_NO_CONTENT, NULL, NULL),
        GSF_XML_IN_NODE (TABLE_ROW_GROUP, TABLE_ROW,	    OO_NS_TABLE, "table-row", GSF_XML_NO_CONTENT, NULL, NULL), /* 2nd def */
    GSF_XML_IN_NODE (OFFICE_BODY, NAMED_EXPRS, OO_NS_TABLE, "named-expressions", GSF_XML_NO_CONTENT, NULL, NULL),
      GSF_XML_IN_NODE (NAMED_EXPRS, NAMED_EXPR, OO_NS_TABLE, "named-expression", GSF_XML_NO_CONTENT, &oo_named_expr, NULL),
      GSF_XML_IN_NODE (NAMED_EXPRS, NAMED_RANGE, OO_NS_TABLE, "named-range", GSF_XML_NO_CONTENT, &oo_named_expr, NULL),
    GSF_XML_IN_NODE (OFFICE_BODY, DB_RANGES, OO_NS_TABLE, "database-ranges", GSF_XML_NO_CONTENT, NULL, NULL),
      GSF_XML_IN_NODE (DB_RANGES, DB_RANGE, OO_NS_TABLE, "database-range", GSF_XML_NO_CONTENT, NULL, NULL),
        GSF_XML_IN_NODE (DB_RANGE, TABLE_SORT, OO_NS_TABLE, "sort", GSF_XML_NO_CONTENT, NULL, NULL),
          GSF_XML_IN_NODE (TABLE_SORT, SORT_BY, OO_NS_TABLE, "sort-by", GSF_XML_NO_CONTENT, NULL, NULL),

GSF_XML_IN_NODE_END
};

/****************************************************************************/

static GsfXMLInNode const opendoc_settings_dtd [] = {
GSF_XML_IN_NODE_FULL (START, START, -1, NULL, GSF_XML_NO_CONTENT, FALSE, TRUE, NULL, NULL, 0),
GSF_XML_IN_NODE (START, OFFICE, OO_NS_OFFICE, "document-settings", GSF_XML_NO_CONTENT, NULL, NULL),
  GSF_XML_IN_NODE (OFFICE, SETTINGS, OO_NS_OFFICE, "settings", GSF_XML_NO_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE (SETTINGS, CONFIG_ITEM_SET, OO_NS_CONFIG, "config-item-set", GSF_XML_NO_CONTENT, &odf_config_item_set, &odf_config_stack_pop),
      GSF_XML_IN_NODE (CONFIG_ITEM_SET, CONFIG_ITEM_SET, OO_NS_CONFIG, "config-item-set", GSF_XML_NO_CONTENT, NULL, NULL), /* 2nd */
      GSF_XML_IN_NODE (CONFIG_ITEM_SET, CONFIG_ITEM, OO_NS_CONFIG, "config-item", GSF_XML_CONTENT, &odf_config_item, &odf_config_item_end),
      GSF_XML_IN_NODE (CONFIG_ITEM_SET, CONFIG_ITEM_MAP_INDEXED, OO_NS_CONFIG, "config-item-map-indexed", GSF_XML_NO_CONTENT, &odf_config_item_set, &odf_config_stack_pop),
        GSF_XML_IN_NODE (CONFIG_ITEM_MAP_INDEXED, CONFIG_ITEM_MAP_ENTRY, OO_NS_CONFIG, "config-item-map-entry",	GSF_XML_NO_CONTENT, &odf_config_item_set, &odf_config_stack_pop),
          GSF_XML_IN_NODE (CONFIG_ITEM_MAP_ENTRY, CONFIG_ITEM_MAP_INDEXED, OO_NS_CONFIG, "config-item-map-indexed", GSF_XML_NO_CONTENT, NULL, NULL),/* 2nd */
          GSF_XML_IN_NODE (CONFIG_ITEM_MAP_ENTRY, CONFIG_ITEM, OO_NS_CONFIG, "config-item", GSF_XML_NO_CONTENT, NULL, NULL),/* 2nd */
          GSF_XML_IN_NODE (CONFIG_ITEM_MAP_ENTRY, CONFIG_ITEM_MAP_NAMED, OO_NS_CONFIG, "config-item-map-named", GSF_XML_NO_CONTENT, &odf_config_item_set, &odf_config_stack_pop),
          GSF_XML_IN_NODE (CONFIG_ITEM_MAP_ENTRY, CONFIG_ITEM_SET, OO_NS_CONFIG, "config-item-set", GSF_XML_NO_CONTENT, NULL, NULL), /* 2nd */
      GSF_XML_IN_NODE (CONFIG_ITEM_SET, CONFIG_ITEM_MAP_NAMED, OO_NS_CONFIG, "config-item-map-named", GSF_XML_NO_CONTENT,  NULL, NULL),/* 2nd */
        GSF_XML_IN_NODE (CONFIG_ITEM_MAP_NAMED, CONFIG_ITEM_MAP_ENTRY, OO_NS_CONFIG, "config-item-map-entry", GSF_XML_NO_CONTENT,  NULL, NULL),/* 2nd */

GSF_XML_IN_NODE_END
};

/****************************************************************************/
/* Generated based on:
 * http://www.oasis-open.org/committees/download.php/12572/OpenDocument-v1.0-os.pdf */
static GsfXMLInNode const opendoc_content_dtd [] =
{
	GSF_XML_IN_NODE_FULL (START, START, -1, NULL, GSF_XML_NO_CONTENT, FALSE, TRUE, NULL, NULL, 0),
	GSF_XML_IN_NODE (START, OFFICE, OO_NS_OFFICE, "document-content", GSF_XML_NO_CONTENT, &odf_find_version, NULL),
	  GSF_XML_IN_NODE (OFFICE, SCRIPT, OO_NS_OFFICE, "scripts", GSF_XML_NO_CONTENT, NULL, NULL),
	  GSF_XML_IN_NODE (OFFICE, OFFICE_FONTS, OO_NS_OFFICE, "font-face-decls", GSF_XML_NO_CONTENT, NULL, NULL),
	    GSF_XML_IN_NODE (OFFICE_FONTS, FONT_FACE, OO_NS_STYLE, "font-face", GSF_XML_NO_CONTENT, NULL, NULL),
	  GSF_XML_IN_NODE (OFFICE, OFFICE_STYLES, OO_NS_OFFICE, "automatic-styles", GSF_XML_NO_CONTENT, NULL, NULL),
	    GSF_XML_IN_NODE (OFFICE_STYLES, STYLE, OO_NS_STYLE, "style", GSF_XML_NO_CONTENT, &oo_style, &oo_style_end),
	      GSF_XML_IN_NODE (STYLE, TABLE_CELL_PROPS, OO_NS_STYLE, "table-cell-properties", GSF_XML_NO_CONTENT, &oo_style_prop, NULL),
	      GSF_XML_IN_NODE (STYLE, TABLE_COL_PROPS, OO_NS_STYLE, "table-column-properties", GSF_XML_NO_CONTENT, &oo_style_prop, NULL),
	      GSF_XML_IN_NODE (STYLE, TABLE_ROW_PROPS, OO_NS_STYLE, "table-row-properties", GSF_XML_NO_CONTENT, &oo_style_prop, NULL),
	      GSF_XML_IN_NODE (STYLE, CHART_PROPS, OO_NS_STYLE, "chart-properties", GSF_XML_NO_CONTENT, &oo_style_prop, NULL),
	      GSF_XML_IN_NODE (STYLE, TEXT_PROPS, OO_NS_STYLE, "text-properties", GSF_XML_NO_CONTENT, &oo_style_prop, NULL),
	      GSF_XML_IN_NODE (STYLE, TABLE_PROPS, OO_NS_STYLE, "table-properties", GSF_XML_NO_CONTENT, &oo_style_prop, NULL),
	      GSF_XML_IN_NODE (STYLE, PARAGRAPH_PROPS, OO_NS_STYLE, "paragraph-properties", GSF_XML_NO_CONTENT, &oo_style_prop, NULL),
	        GSF_XML_IN_NODE (PARAGRAPH_PROPS, PARA_TABS, OO_NS_STYLE,  "tab-stops", GSF_XML_NO_CONTENT, NULL, NULL),
	      GSF_XML_IN_NODE (STYLE, GRAPHIC_PROPS, OO_NS_STYLE, "graphic-properties", GSF_XML_NO_CONTENT, &oo_style_prop, NULL),
	      GSF_XML_IN_NODE (STYLE, STYLE_MAP, OO_NS_STYLE, "map", GSF_XML_NO_CONTENT, &oo_style_map, NULL),
	    GSF_XML_IN_NODE (OFFICE_STYLES, NUMBER_STYLE, OO_NS_NUMBER, "number-style", GSF_XML_NO_CONTENT, &odf_number_style, &odf_number_style_end),
	      GSF_XML_IN_NODE (NUMBER_STYLE, NUMBER_STYLE_NUMBER, OO_NS_NUMBER,	  "number", GSF_XML_NO_CONTENT, &odf_number, NULL),
                 GSF_XML_IN_NODE (NUMBER_STYLE_NUMBER, NUMBER_EMBEDDED_TEXT, OO_NS_NUMBER, "embedded-text", GSF_XML_NO_CONTENT, NULL, NULL),
	      GSF_XML_IN_NODE (NUMBER_STYLE, NUMBER_STYLE_TEXT, OO_NS_NUMBER,	  "text", GSF_XML_CONTENT, NULL, &oo_date_text_end),
	      GSF_XML_IN_NODE (NUMBER_STYLE, NUMBER_STYLE_FRACTION, OO_NS_NUMBER, "fraction", GSF_XML_NO_CONTENT,  &odf_fraction, NULL),
	      GSF_XML_IN_NODE (NUMBER_STYLE, NUMBER_SCI_STYLE_PROP, OO_NS_NUMBER, "scientific-number", GSF_XML_NO_CONTENT, &odf_scientific, NULL),
	      GSF_XML_IN_NODE (NUMBER_STYLE, NUMBER_MAP, OO_NS_STYLE,		  "map", GSF_XML_NO_CONTENT, &odf_map, NULL),
              GSF_XML_IN_NODE (NUMBER_STYLE, NUMBER_TEXT_PROP, OO_NS_STYLE,	"text-properties", GSF_XML_NO_CONTENT, &odf_number_color, NULL),
	    GSF_XML_IN_NODE (OFFICE_STYLES, DATE_STYLE, OO_NS_NUMBER, "date-style", GSF_XML_NO_CONTENT, &oo_date_style, &oo_date_style_end),
	      GSF_XML_IN_NODE (DATE_STYLE, DATE_DAY, OO_NS_NUMBER,		"day", GSF_XML_NO_CONTENT,	&oo_date_day, NULL),
	      GSF_XML_IN_NODE (DATE_STYLE, DATE_MONTH, OO_NS_NUMBER,		"month", GSF_XML_NO_CONTENT,	&oo_date_month, NULL),
	      GSF_XML_IN_NODE (DATE_STYLE, DATE_YEAR, OO_NS_NUMBER,		"year", GSF_XML_NO_CONTENT,	&oo_date_year, NULL),
	      GSF_XML_IN_NODE (DATE_STYLE, DATE_ERA, OO_NS_NUMBER,		"era", GSF_XML_NO_CONTENT,	&oo_date_era, NULL),
	      GSF_XML_IN_NODE (DATE_STYLE, DATE_DAY_OF_WEEK, OO_NS_NUMBER,	"day-of-week", GSF_XML_NO_CONTENT, &oo_date_day_of_week, NULL),
	      GSF_XML_IN_NODE (DATE_STYLE, DATE_WEEK_OF_YEAR, OO_NS_NUMBER,	"week-of-year", GSF_XML_NO_CONTENT, &oo_date_week_of_year, NULL),
	      GSF_XML_IN_NODE (DATE_STYLE, DATE_QUARTER, OO_NS_NUMBER,		"quarter", GSF_XML_NO_CONTENT, &oo_date_quarter, NULL),
	      GSF_XML_IN_NODE (DATE_STYLE, DATE_HOURS, OO_NS_NUMBER,		"hours", GSF_XML_NO_CONTENT,	&oo_date_hours, NULL),
	      GSF_XML_IN_NODE (DATE_STYLE, DATE_MINUTES, OO_NS_NUMBER,		"minutes", GSF_XML_NO_CONTENT, &oo_date_minutes, NULL),
	      GSF_XML_IN_NODE (DATE_STYLE, DATE_SECONDS, OO_NS_NUMBER,		"seconds", GSF_XML_NO_CONTENT, &oo_date_seconds, NULL),
	      GSF_XML_IN_NODE (DATE_STYLE, DATE_AM_PM, OO_NS_NUMBER,		"am-pm", GSF_XML_NO_CONTENT,	&oo_date_am_pm, NULL),
	      GSF_XML_IN_NODE (DATE_STYLE, DATE_TEXT, OO_NS_NUMBER,		"text", GSF_XML_CONTENT,	NULL, &oo_date_text_end),
	      GSF_XML_IN_NODE (DATE_STYLE, DATE_TEXT_PROP, OO_NS_STYLE,		"text-properties", GSF_XML_NO_CONTENT, &odf_number_color, NULL),
	      GSF_XML_IN_NODE (DATE_STYLE, DATE_MAP, OO_NS_STYLE,		"map", GSF_XML_NO_CONTENT, NULL, NULL),
	    GSF_XML_IN_NODE (OFFICE_STYLES, TIME_STYLE, OO_NS_NUMBER,	"time-style", GSF_XML_NO_CONTENT, &oo_date_style, &oo_date_style_end),
	      GSF_XML_IN_NODE (TIME_STYLE, TIME_HOURS, OO_NS_NUMBER,	"hours", GSF_XML_NO_CONTENT,	&oo_date_hours, NULL),
	      GSF_XML_IN_NODE (TIME_STYLE, TIME_MINUTES, OO_NS_NUMBER,	"minutes", GSF_XML_NO_CONTENT, &oo_date_minutes, NULL),
	      GSF_XML_IN_NODE (TIME_STYLE, TIME_SECONDS, OO_NS_NUMBER,	"seconds", GSF_XML_NO_CONTENT, &oo_date_seconds, NULL),
	      GSF_XML_IN_NODE (TIME_STYLE, TIME_AM_PM, OO_NS_NUMBER,	"am-pm", GSF_XML_NO_CONTENT,	&oo_date_am_pm, NULL),
	      GSF_XML_IN_NODE (TIME_STYLE, TIME_TEXT, OO_NS_NUMBER,	"text", GSF_XML_CONTENT,	NULL, &oo_date_text_end),
	      GSF_XML_IN_NODE (TIME_STYLE, TIME_TEXT_PROP, OO_NS_STYLE,	"text-properties", GSF_XML_NO_CONTENT, &odf_number_color, NULL),
	      GSF_XML_IN_NODE (TIME_STYLE, TIME_MAP, OO_NS_STYLE,	"map", GSF_XML_NO_CONTENT, NULL, NULL),
	    GSF_XML_IN_NODE (OFFICE_STYLES, STYLE_BOOL, OO_NS_NUMBER,	"boolean-style", GSF_XML_NO_CONTENT, NULL, NULL),
	      GSF_XML_IN_NODE (STYLE_BOOL, BOOL_PROP, OO_NS_NUMBER,	"boolean", GSF_XML_NO_CONTENT, NULL, NULL),
	    GSF_XML_IN_NODE (OFFICE_STYLES, STYLE_CURRENCY, OO_NS_NUMBER,      	"currency-style", GSF_XML_NO_CONTENT, &odf_number_style, &odf_number_style_end),
	      GSF_XML_IN_NODE (STYLE_CURRENCY, CURRENCY_STYLE, OO_NS_NUMBER,	"number", GSF_XML_NO_CONTENT, &odf_number, NULL),
	      GSF_XML_IN_NODE (STYLE_CURRENCY, CURRENCY_STYLE_PROP, OO_NS_STYLE,"properties", GSF_XML_NO_CONTENT, NULL, NULL),
	      GSF_XML_IN_NODE (STYLE_CURRENCY, CURRENCY_MAP, OO_NS_STYLE,	"map", GSF_XML_NO_CONTENT, &odf_map, NULL),
	      GSF_XML_IN_NODE (STYLE_CURRENCY, CURRENCY_SYMBOL, OO_NS_NUMBER,	"currency-symbol", GSF_XML_CONTENT, NULL, &odf_currency_symbol_end),
	      GSF_XML_IN_NODE (STYLE_CURRENCY, CURRENCY_TEXT, OO_NS_NUMBER,	"text", GSF_XML_CONTENT, NULL, &oo_date_text_end),
	      GSF_XML_IN_NODE (STYLE_CURRENCY, CURRENCY_TEXT_PROP, OO_NS_STYLE,	"text-properties", GSF_XML_NO_CONTENT, &odf_number_color, NULL),
	    GSF_XML_IN_NODE (OFFICE_STYLES, STYLE_PERCENTAGE, OO_NS_NUMBER, "percentage-style", GSF_XML_NO_CONTENT, &odf_number_percentage_style, &odf_number_style_end),
	      GSF_XML_IN_NODE (STYLE_PERCENTAGE, PERCENTAGE_STYLE_PROP, OO_NS_NUMBER,	"number", GSF_XML_NO_CONTENT, &odf_number, NULL),
	      GSF_XML_IN_NODE (STYLE_PERCENTAGE, PERCENTAGE_TEXT, OO_NS_NUMBER,		"text", GSF_XML_CONTENT, NULL, &oo_date_text_end),
	      GSF_XML_IN_NODE (STYLE_PERCENTAGE, PERCENTAGE_MAP, OO_NS_STYLE,		"map", GSF_XML_NO_CONTENT, &odf_map, NULL),
	      GSF_XML_IN_NODE (STYLE_PERCENTAGE, PERCENTAGE_TEXT_PROP, OO_NS_STYLE,	"text-properties", GSF_XML_NO_CONTENT, &odf_number_color, NULL),
	    GSF_XML_IN_NODE (OFFICE_STYLES, STYLE_TEXT, OO_NS_NUMBER,		"text-style", GSF_XML_NO_CONTENT, NULL, NULL),
	      GSF_XML_IN_NODE (STYLE_TEXT, STYLE_TEXT_CONTENT, OO_NS_NUMBER,	"text-content", GSF_XML_NO_CONTENT, NULL, NULL),
	      GSF_XML_IN_NODE (STYLE_TEXT, STYLE_TEXT_PROP, OO_NS_NUMBER,	"text", GSF_XML_NO_CONTENT, NULL, NULL),
	      GSF_XML_IN_NODE (STYLE_TEXT, STYLE_TEXT_MAP, OO_NS_STYLE,		"map", GSF_XML_NO_CONTENT, NULL, NULL),

	GSF_XML_IN_NODE (OFFICE, OFFICE_BODY, OO_NS_OFFICE, "body", GSF_XML_NO_CONTENT, NULL, NULL),
	  GSF_XML_IN_NODE (OFFICE_BODY, SPREADSHEET, OO_NS_OFFICE, "spreadsheet", GSF_XML_NO_CONTENT, NULL, NULL),
	    GSF_XML_IN_NODE (SPREADSHEET, DATA_PILOT_TABLES, OO_NS_TABLE, "data-pilot-tables", GSF_XML_NO_CONTENT, NULL, NULL),
	      GSF_XML_IN_NODE (DATA_PILOT_TABLES, DATA_PILOT_TABLE, OO_NS_TABLE, "data-pilot-table", GSF_XML_NO_CONTENT, NULL, NULL),
	        GSF_XML_IN_NODE (DATA_PILOT_TABLE, DPT_SOURCE_CELL_RANGE, OO_NS_TABLE, "source-cell-range", GSF_XML_NO_CONTENT, NULL, NULL),
	        GSF_XML_IN_NODE (DATA_PILOT_TABLE, DATA_PILOT_FIELD, OO_NS_TABLE, "data-pilot-field", GSF_XML_NO_CONTENT, NULL, NULL),
	          GSF_XML_IN_NODE (DATA_PILOT_FIELD, DATA_PILOT_LEVEL, OO_NS_TABLE, "data-pilot-level", GSF_XML_NO_CONTENT, NULL, NULL),
	            GSF_XML_IN_NODE (DATA_PILOT_LEVEL, DATA_PILOT_LAYOUT_INFO, OO_NS_TABLE, "data-pilot-layout-info", GSF_XML_NO_CONTENT, NULL, NULL),
	            GSF_XML_IN_NODE (DATA_PILOT_LEVEL, DATA_PILOT_SORT_INFO, OO_NS_TABLE, "data-pilot-sort-info", GSF_XML_NO_CONTENT, NULL, NULL),
	            GSF_XML_IN_NODE (DATA_PILOT_LEVEL, DATA_PILOT_DISPLAY_INFO, OO_NS_TABLE, "data-pilot-display-info", GSF_XML_NO_CONTENT, NULL, NULL),
	            GSF_XML_IN_NODE (DATA_PILOT_LEVEL, DATA_PILOT_MEMBERS, OO_NS_TABLE, "data-pilot-members", GSF_XML_NO_CONTENT, NULL, NULL),
	              GSF_XML_IN_NODE (DATA_PILOT_MEMBERS, DATA_PILOT_MEMBER, OO_NS_TABLE, "data-pilot-member", GSF_XML_NO_CONTENT, NULL, NULL),
	            GSF_XML_IN_NODE (DATA_PILOT_LEVEL, DATA_PILOT_SUBTOTALS, OO_NS_TABLE, "data-pilot-subtotals", GSF_XML_NO_CONTENT, NULL, NULL),
	            GSF_XML_IN_NODE (DATA_PILOT_SUBTOTALS, DATA_PILOT_SUBTOTAL, OO_NS_TABLE, "data-pilot-subtotal", GSF_XML_NO_CONTENT, NULL, NULL),
	          GSF_XML_IN_NODE (DATA_PILOT_FIELD, DATA_PILOT_GROUPS, OO_NS_TABLE, "data-pilot-groups", GSF_XML_NO_CONTENT, NULL, NULL),
	    GSF_XML_IN_NODE (SPREADSHEET, CONTENT_VALIDATIONS, OO_NS_TABLE, "content-validations", GSF_XML_NO_CONTENT, NULL, NULL),
 	      GSF_XML_IN_NODE (CONTENT_VALIDATIONS, CONTENT_VALIDATION, OO_NS_TABLE, "content-validation", GSF_XML_NO_CONTENT, &odf_validation, NULL),
 	        GSF_XML_IN_NODE (CONTENT_VALIDATION, ERROR_MESSAGE, OO_NS_TABLE, "error-message", GSF_XML_NO_CONTENT, NULL, NULL),
	          GSF_XML_IN_NODE (ERROR_MESSAGE, ERROR_MESSAGE_P, OO_NS_TEXT, "p", GSF_XML_NO_CONTENT, NULL, NULL),
		    GSF_XML_IN_NODE (ERROR_MESSAGE_P, ERROR_MESSAGE_P_S, OO_NS_TEXT, "s", GSF_XML_NO_CONTENT, NULL, NULL),
	    GSF_XML_IN_NODE (SPREADSHEET, CALC_SETTINGS, OO_NS_TABLE, "calculation-settings", GSF_XML_NO_CONTENT, NULL, NULL),
	      GSF_XML_IN_NODE (CALC_SETTINGS, ITERATION, OO_NS_TABLE, "iteration", GSF_XML_NO_CONTENT, oo_iteration, NULL),
	      GSF_XML_IN_NODE (CALC_SETTINGS, DATE_CONVENTION, OO_NS_TABLE, "null-date", GSF_XML_NO_CONTENT, oo_date_convention, NULL),
	    GSF_XML_IN_NODE (SPREADSHEET, CHART, OO_NS_CHART, "chart", GSF_XML_NO_CONTENT, NULL, NULL),
	  GSF_XML_IN_NODE (OFFICE_BODY, OFFICE_CHART, OO_NS_OFFICE, "chart", GSF_XML_NO_CONTENT, NULL, NULL),
	    GSF_XML_IN_NODE (OFFICE_CHART, CHART_CHART, OO_NS_CHART, "chart", GSF_XML_NO_CONTENT, &oo_chart, &oo_chart_end),
	      GSF_XML_IN_NODE (CHART_CHART, CHART_TABLE, OO_NS_TABLE, "table", GSF_XML_NO_CONTENT, NULL, NULL),
	        GSF_XML_IN_NODE (CHART_TABLE, CHART_TABLE_ROWS, OO_NS_TABLE, "table-rows", GSF_XML_NO_CONTENT, NULL, NULL),
	          GSF_XML_IN_NODE (CHART_TABLE_ROWS, CHART_TABLE_ROW, OO_NS_TABLE, "table-row", GSF_XML_NO_CONTENT, NULL, NULL),
	            GSF_XML_IN_NODE (CHART_TABLE_ROW, CHART_TABLE_CELL, OO_NS_TABLE, "table-cell", GSF_XML_NO_CONTENT, NULL, NULL),
	              GSF_XML_IN_NODE (CHART_TABLE_CELL, CHART_CELL_P, OO_NS_TEXT, "p", GSF_XML_NO_CONTENT, NULL, NULL),
	        GSF_XML_IN_NODE (CHART_TABLE, CHART_TABLE_COLS, OO_NS_TABLE, "table-columns", GSF_XML_NO_CONTENT, NULL, NULL),
	          GSF_XML_IN_NODE (CHART_TABLE_COLS, CHART_TABLE_COL, OO_NS_TABLE, "table-column", GSF_XML_NO_CONTENT, NULL, NULL),
	        GSF_XML_IN_NODE (CHART_TABLE, CHART_TABLE_HROWS, OO_NS_TABLE, "table-header-rows", GSF_XML_NO_CONTENT, NULL, NULL),
	          GSF_XML_IN_NODE (CHART_TABLE_HROWS, CHART_TABLE_HROW, OO_NS_TABLE, "table-header-row", GSF_XML_NO_CONTENT, NULL, NULL),
	          GSF_XML_IN_NODE (CHART_TABLE_HROWS, CHART_TABLE_ROW, OO_NS_TABLE, "table-row", GSF_XML_NO_CONTENT, NULL, NULL),		/* 2nd Def */
	        GSF_XML_IN_NODE (CHART_TABLE, CHART_TABLE_HCOLS, OO_NS_TABLE, "table-header-columns", GSF_XML_NO_CONTENT, NULL, NULL),
	          GSF_XML_IN_NODE (CHART_TABLE_HCOLS, CHART_TABLE_HCOL, OO_NS_TABLE, "table-header-column", GSF_XML_NO_CONTENT, NULL, NULL),
	          GSF_XML_IN_NODE (CHART_TABLE_HCOLS, CHART_TABLE_COL, OO_NS_TABLE, "table-column", GSF_XML_NO_CONTENT, NULL, NULL),		/* 2nd Def */

	      GSF_XML_IN_NODE (CHART_CHART, CHART_TITLE, OO_NS_CHART, "title", GSF_XML_NO_CONTENT,  &oo_chart_title, &oo_chart_title_end),
		GSF_XML_IN_NODE (CHART_TITLE, TITLE_TEXT, OO_NS_TEXT, "p", GSF_XML_CONTENT, NULL, &oo_chart_title_text),
	      GSF_XML_IN_NODE (CHART_CHART, CHART_SUBTITLE, OO_NS_CHART, "subtitle", GSF_XML_NO_CONTENT, &oo_chart_title, &oo_chart_title_end),
	        GSF_XML_IN_NODE (CHART_SUBTITLE, TITLE_TEXT, OO_NS_TEXT, "p", GSF_XML_NO_CONTENT, NULL, NULL),                                     /* 2nd Def */
	      GSF_XML_IN_NODE (CHART_CHART, CHART_LEGEND, OO_NS_CHART, "legend", GSF_XML_NO_CONTENT, &oo_legend, &oo_legend_end),
	        GSF_XML_IN_NODE (CHART_LEGEND, CHART_LEGEND_TITLE, OO_GNUM_NS_EXT, "title", GSF_XML_NO_CONTENT,  &oo_chart_title, &oo_chart_title_end),
		  GSF_XML_IN_NODE (CHART_LEGEND_TITLE, TITLE_TEXT, OO_NS_TEXT, "p", GSF_XML_NO_CONTENT, NULL, NULL), /* 2nd Def */
	      GSF_XML_IN_NODE (CHART_CHART, CHART_PLOT_AREA, OO_NS_CHART, "plot-area", GSF_XML_NO_CONTENT, &oo_plot_area, &oo_plot_area_end),
		GSF_XML_IN_NODE (CHART_PLOT_AREA, CHART_SERIES, OO_NS_CHART, "series", GSF_XML_NO_CONTENT, &oo_plot_series, &oo_plot_series_end),
		  GSF_XML_IN_NODE (CHART_SERIES, SERIES_DOMAIN, OO_NS_CHART, "domain", GSF_XML_NO_CONTENT, &oo_series_domain, NULL),
		  GSF_XML_IN_NODE (CHART_SERIES, SERIES_DATA_PT, OO_NS_CHART, "data-point", GSF_XML_NO_CONTENT, &oo_series_pt, NULL),
		  GSF_XML_IN_NODE (CHART_SERIES, SERIES_DATA_ERR, OO_NS_CHART, "error-indicator", GSF_XML_NO_CONTENT, NULL, NULL),
		  GSF_XML_IN_NODE (CHART_SERIES, SERIES_REGRESSION, OO_NS_CHART, "regression-curve", GSF_XML_NO_CONTENT,  &od_series_regression, NULL),
	            GSF_XML_IN_NODE (SERIES_REGRESSION, SERIES_REG_EQ, OO_NS_CHART, "equation", GSF_XML_NO_CONTENT,  &od_series_reg_equation, NULL),
	            GSF_XML_IN_NODE (SERIES_REGRESSION, SERIES_REG_EQ_GNM, OO_GNUM_NS_EXT, "equation", GSF_XML_NO_CONTENT,  &od_series_reg_equation, NULL),
		  GSF_XML_IN_NODE (CHART_SERIES, SERIES_REGRESSION_MULTIPLE, OO_GNUM_NS_EXT, "regression-curve", GSF_XML_NO_CONTENT,  &od_series_regression, NULL),
	            GSF_XML_IN_NODE (SERIES_REGRESSION_MULTIPLE, SERIES_REG_EQ, OO_NS_CHART, "equation", GSF_XML_NO_CONTENT,  NULL, NULL),/* 2nd Def */
	            GSF_XML_IN_NODE (SERIES_REGRESSION_MULTIPLE, SERIES_REG_EQ_GNM, OO_GNUM_NS_EXT, "equation", GSF_XML_NO_CONTENT,  NULL, NULL), /* 2nd Def */
		  GSF_XML_IN_NODE (CHART_SERIES, SERIES_DROPLINES, OO_GNUM_NS_EXT, "droplines", GSF_XML_NO_CONTENT, &oo_series_droplines, NULL),
		GSF_XML_IN_NODE (CHART_PLOT_AREA, CHART_WALL, OO_NS_CHART, "wall", GSF_XML_NO_CONTENT, &oo_chart_wall, NULL),
		GSF_XML_IN_NODE (CHART_PLOT_AREA, CHART_FLOOR, OO_NS_CHART, "floor", GSF_XML_NO_CONTENT, NULL, NULL),
		GSF_XML_IN_NODE (CHART_PLOT_AREA, CHART_AXIS, OO_NS_CHART, "axis", GSF_XML_NO_CONTENT, &oo_chart_axis, &oo_chart_axis_end),
		  GSF_XML_IN_NODE (CHART_AXIS, CHART_GRID, OO_NS_CHART, "grid", GSF_XML_NO_CONTENT, &oo_chart_grid, NULL),
		  GSF_XML_IN_NODE (CHART_AXIS, CHART_AXIS_CAT,   OO_NS_CHART, "categories", GSF_XML_NO_CONTENT, &od_chart_axis_categories, NULL),
		  GSF_XML_IN_NODE (CHART_AXIS, CHART_TITLE, OO_NS_CHART, "title", GSF_XML_NO_CONTENT, NULL, NULL),				/* 2nd Def */
#ifdef HAVE_OO_NS_CHART_OOO
	        GSF_XML_IN_NODE (CHART_PLOT_AREA, CHART_OOO_COORDINATE_REGION, OO_NS_CHART_OOO, "coordinate-region", GSF_XML_NO_CONTENT, NULL, NULL),
#endif
	    GSF_XML_IN_NODE (SPREADSHEET, TABLE, OO_NS_TABLE, "table", GSF_XML_NO_CONTENT, &oo_table_start, &oo_table_end),
	      GSF_XML_IN_NODE (TABLE, TABLE_SOURCE, OO_NS_TABLE, "table-source", GSF_XML_NO_CONTENT, NULL, NULL),
	      GSF_XML_IN_NODE (TABLE, FORMS, OO_NS_OFFICE, "forms", GSF_XML_NO_CONTENT, NULL, NULL),
	        GSF_XML_IN_NODE (FORMS, FORM, OO_NS_FORM, "form", GSF_XML_NO_CONTENT, NULL, NULL),
	          GSF_XML_IN_NODE (FORM, FORM_PROPERTIES, OO_NS_FORM, "properties", GSF_XML_NO_CONTENT, NULL, NULL),
	            GSF_XML_IN_NODE (FORM_PROPERTIES, FORM_PROPERTY, OO_NS_FORM, "property", GSF_XML_NO_CONTENT, &odf_control_property, NULL),
	              GSF_XML_IN_NODE (FORM_PROPERTIES, FORM_LIST_PROPERTY, OO_NS_FORM, "list-property", GSF_XML_NO_CONTENT, NULL, NULL),
	          GSF_XML_IN_NODE (FORM, FORM_BUTTON, OO_NS_FORM, "button", GSF_XML_NO_CONTENT, &odf_form_button, &odf_form_control_end),
	            GSF_XML_IN_NODE (FORM_BUTTON, FORM_PROPERTIES, OO_NS_FORM, "properties", GSF_XML_NO_CONTENT, NULL, NULL),			/* 2nd Def */
	            GSF_XML_IN_NODE (FORM_BUTTON, BUTTON_OFFICE_EVENT_LISTENERS, OO_NS_OFFICE, "event-listeners", GSF_XML_NO_CONTENT, NULL, NULL),
	              GSF_XML_IN_NODE (BUTTON_OFFICE_EVENT_LISTENERS, BUTTON_EVENT_LISTENER, OO_NS_SCRIPT, "event-listener", GSF_XML_NO_CONTENT, &odf_button_event_listener, NULL),
	          GSF_XML_IN_NODE (FORM, FORM_VALUE_RANGE, OO_NS_FORM, "value-range", GSF_XML_NO_CONTENT, &odf_form_value_range, NULL),
	            GSF_XML_IN_NODE (FORM_VALUE_RANGE, FORM_PROPERTIES, OO_NS_FORM, "properties", GSF_XML_NO_CONTENT, NULL, NULL),			/* 2nd Def */
	          GSF_XML_IN_NODE (FORM, FORM_CHECKBOX, OO_NS_FORM, "checkbox", GSF_XML_NO_CONTENT, &odf_form_checkbox, NULL),
	            GSF_XML_IN_NODE (FORM_CHECKBOX, FORM_PROPERTIES, OO_NS_FORM, "properties", GSF_XML_NO_CONTENT, NULL, NULL),			/* 2nd Def */
	          GSF_XML_IN_NODE (FORM, FORM_RADIO, OO_NS_FORM, "radio", GSF_XML_NO_CONTENT, &odf_form_radio, NULL),
	            GSF_XML_IN_NODE (FORM_RADIO, FORM_PROPERTIES, OO_NS_FORM, "properties", GSF_XML_NO_CONTENT, NULL, NULL),			/* 2nd Def */
	          GSF_XML_IN_NODE (FORM, FORM_LISTBOX, OO_NS_FORM, "listbox", GSF_XML_NO_CONTENT, &odf_form_listbox, NULL),
	            GSF_XML_IN_NODE (FORM_LISTBOX, FORM_PROPERTIES, OO_NS_FORM, "properties", GSF_XML_NO_CONTENT, NULL, NULL),			/* 2nd Def */
	          GSF_XML_IN_NODE (FORM, FORM_COMBOBOX, OO_NS_FORM, "combobox", GSF_XML_NO_CONTENT, &odf_form_combobox, NULL),
	            GSF_XML_IN_NODE (FORM_COMBOBOX, FORM_PROPERTIES, OO_NS_FORM, "properties", GSF_XML_NO_CONTENT, NULL, NULL),			/* 2nd Def */
	          GSF_XML_IN_NODE (FORM, FORM_GENERIC, OO_NS_FORM, "generic-control", GSF_XML_NO_CONTENT, &odf_form_generic, &odf_form_control_end),
	            GSF_XML_IN_NODE (FORM_GENERIC, FORM_PROPERTIES, OO_NS_FORM, "properties", GSF_XML_NO_CONTENT, NULL, NULL),			/* 2nd Def */
	      GSF_XML_IN_NODE (TABLE, TABLE_ROWS, OO_NS_TABLE, "table-rows", GSF_XML_NO_CONTENT, NULL, NULL),
	      GSF_XML_IN_NODE (TABLE, TABLE_COL, OO_NS_TABLE, "table-column", GSF_XML_NO_CONTENT, &oo_col_start, NULL),
	      GSF_XML_IN_NODE (TABLE, TABLE_ROW, OO_NS_TABLE, "table-row", GSF_XML_NO_CONTENT, &oo_row_start, &oo_row_end),
	      GSF_XML_IN_NODE (TABLE, SOFTPAGEBREAK, OO_NS_TEXT, "soft-page-break", GSF_XML_NO_CONTENT, NULL, NULL),
	      GSF_XML_IN_NODE (TABLE_ROWS, TABLE_ROW, OO_NS_TABLE, "table-row", GSF_XML_NO_CONTENT, NULL, NULL), /* 2nd def */
	      GSF_XML_IN_NODE (TABLE_ROWS, SOFTPAGEBREAK, OO_NS_TEXT, "soft-page-break", GSF_XML_NO_CONTENT, NULL, NULL), /* 2nd def */

		GSF_XML_IN_NODE (TABLE_ROW, TABLE_CELL, OO_NS_TABLE, "table-cell", GSF_XML_NO_CONTENT, &oo_cell_start, &oo_cell_end),
		  GSF_XML_IN_NODE (TABLE_CELL, CELL_TEXT, OO_NS_TEXT, "p", GSF_XML_CONTENT, NULL, &oo_cell_content_end),
		    GSF_XML_IN_NODE (CELL_TEXT, CELL_TEXT_S,    OO_NS_TEXT, "s", GSF_XML_NO_CONTENT, NULL, NULL),
		    GSF_XML_IN_NODE (CELL_TEXT, CELL_TEXT_ADDR, OO_NS_TEXT, "a", GSF_XML_SHARED_CONTENT, NULL, NULL),
		    GSF_XML_IN_NODE (CELL_TEXT, CELL_TEXT_LINE_BREAK,    OO_NS_TEXT, "line-break", GSF_XML_NO_CONTENT, NULL, NULL),
	            GSF_XML_IN_NODE (CELL_TEXT, CELL_TEXT_TAB, OO_NS_TEXT, "tab", GSF_XML_SHARED_CONTENT, NULL, NULL),
		    GSF_XML_IN_NODE (CELL_TEXT, CELL_TEXT_SPAN, OO_NS_TEXT, "span", GSF_XML_SHARED_CONTENT, NULL, NULL),
		      GSF_XML_IN_NODE (CELL_TEXT_SPAN, CELL_TEXT_SPAN, OO_NS_TEXT, "span", GSF_XML_NO_CONTENT, NULL, NULL),/* 2nd def */
		      GSF_XML_IN_NODE (CELL_TEXT_SPAN, CELL_TEXT_S,    OO_NS_TEXT, "s", GSF_XML_NO_CONTENT, NULL, NULL),/* 2nd def */
		      GSF_XML_IN_NODE (CELL_TEXT_SPAN, CELL_TEXT_LINE_BREAK,    OO_NS_TEXT, "line-break", GSF_XML_NO_CONTENT, NULL, NULL),/* 2nd def */
		      GSF_XML_IN_NODE (CELL_TEXT_SPAN, CELL_TEXT_SPAN_ADDR, OO_NS_TEXT, "a", GSF_XML_SHARED_CONTENT, NULL, NULL),
		      GSF_XML_IN_NODE (CELL_TEXT_SPAN, CELL_TEXT_TAB, OO_NS_TEXT, "tab", GSF_XML_NO_CONTENT, NULL, NULL),/* 2nd def */
		  GSF_XML_IN_NODE (TABLE_CELL, CELL_OBJECT, OO_NS_DRAW, "object", GSF_XML_NO_CONTENT, NULL, NULL),		/* ignore for now */
		  GSF_XML_IN_NODE (TABLE_CELL, CELL_GRAPHIC, OO_NS_DRAW, "g", GSF_XML_NO_CONTENT, NULL, NULL),			/* ignore for now */
		    GSF_XML_IN_NODE (CELL_GRAPHIC, CELL_GRAPHIC, OO_NS_DRAW, "g", GSF_XML_NO_CONTENT, NULL, NULL),		/* 2nd def */
		    GSF_XML_IN_NODE (CELL_GRAPHIC, DRAW_POLYLINE, OO_NS_DRAW, "polyline", GSF_XML_NO_CONTENT, NULL, NULL),	/* 2nd def */
	          GSF_XML_IN_NODE (TABLE_CELL, DRAW_CONTROL, OO_NS_DRAW, "control", GSF_XML_NO_CONTENT, &od_draw_control_start, NULL),
	          GSF_XML_IN_NODE (TABLE_CELL, DRAW_RECT, OO_NS_DRAW, "rect", GSF_XML_NO_CONTENT, &odf_rect, &od_draw_frame_end),
	            GSF_XML_IN_NODE (DRAW_RECT, DRAW_TEXT_BOX_TEXT, OO_NS_TEXT, "p", GSF_XML_CONTENT, NULL, &od_draw_text_box_p_end),
	          GSF_XML_IN_NODE (TABLE_CELL, DRAW_LINE, OO_NS_DRAW, "line", GSF_XML_NO_CONTENT, &odf_line, &od_draw_frame_end),
	            GSF_XML_IN_NODE (DRAW_LINE, DRAW_LINE_TEXT, OO_NS_TEXT, "p", GSF_XML_NO_CONTENT, NULL, NULL),
	          GSF_XML_IN_NODE (TABLE_CELL, DRAW_ELLIPSE, OO_NS_DRAW, "ellipse", GSF_XML_NO_CONTENT, &odf_ellipse, &od_draw_frame_end),
	            GSF_XML_IN_NODE (DRAW_ELLIPSE, DRAW_TEXT_BOX_TEXT, OO_NS_TEXT, "p", GSF_XML_NO_CONTENT, NULL, NULL), /* 2nd def */
		  GSF_XML_IN_NODE (TABLE_CELL, DRAW_FRAME, OO_NS_DRAW, "frame", GSF_XML_NO_CONTENT, &od_draw_frame_start, &od_draw_frame_end),
		    GSF_XML_IN_NODE (DRAW_FRAME, DRAW_OBJECT, OO_NS_DRAW, "object", GSF_XML_NO_CONTENT, &od_draw_object, NULL),
	            GSF_XML_IN_NODE (DRAW_OBJECT, DRAW_OBJECT_TEXT, OO_NS_TEXT, "p", GSF_XML_CONTENT, NULL, NULL),

		    GSF_XML_IN_NODE (DRAW_FRAME, DRAW_IMAGE, OO_NS_DRAW, "image", GSF_XML_NO_CONTENT, &od_draw_image, NULL),
	              GSF_XML_IN_NODE (DRAW_IMAGE, DRAW_IMAGE_TEXT,OO_NS_TEXT, "p", GSF_XML_NO_CONTENT, NULL, NULL),
		    GSF_XML_IN_NODE (DRAW_FRAME, SVG_DESC, OO_NS_SVG, "desc", GSF_XML_NO_CONTENT, NULL, NULL),
		    GSF_XML_IN_NODE (DRAW_FRAME, DRAW_TEXT_BOX, OO_NS_DRAW, "text-box", GSF_XML_NO_CONTENT, &od_draw_text_box, NULL),
		    GSF_XML_IN_NODE (DRAW_TEXT_BOX, DRAW_TEXT_BOX_TEXT, OO_NS_TEXT, "p", GSF_XML_NO_CONTENT, NULL, NULL),
	          GSF_XML_IN_NODE (TABLE_CELL, CELL_ANNOTATION, OO_NS_OFFICE, "annotation", GSF_XML_NO_CONTENT, &odf_annotation_start, &odf_annotation_end),
	            GSF_XML_IN_NODE (CELL_ANNOTATION, CELL_ANNOTATION_TEXT, OO_NS_TEXT, "p", GSF_XML_CONTENT, NULL, &odf_annotation_content_end),
  		      GSF_XML_IN_NODE (CELL_ANNOTATION_TEXT, CELL_ANNOTATION_TEXT_S,    OO_NS_TEXT, "s", GSF_XML_NO_CONTENT, NULL, NULL),
		      GSF_XML_IN_NODE (CELL_ANNOTATION_TEXT, CELL_ANNOTATION_TEXT_LINE_BREAK, OO_NS_TEXT, "line-break", GSF_XML_NO_CONTENT, NULL, NULL),
  		      GSF_XML_IN_NODE (CELL_ANNOTATION_TEXT, CELL_ANNOTATION_TEXT_TAB,  OO_NS_TEXT, "tab", GSF_XML_NO_CONTENT, NULL, NULL),
		      GSF_XML_IN_NODE (CELL_ANNOTATION_TEXT, CELL_ANNOTATION_TEXT_SPAN, OO_NS_TEXT, "span", GSF_XML_SHARED_CONTENT, NULL, NULL),
		        GSF_XML_IN_NODE (CELL_ANNOTATION_TEXT_SPAN, CELL_ANNOTATION_TEXT_SPAN, OO_NS_TEXT, "span", GSF_XML_NO_CONTENT, NULL, NULL),/* 2nd def */
		        GSF_XML_IN_NODE (CELL_ANNOTATION_TEXT_SPAN, CELL_ANNOTATION_TEXT_S,    OO_NS_TEXT, "s", GSF_XML_NO_CONTENT, NULL, NULL),/* 2nd def */
		        GSF_XML_IN_NODE (CELL_ANNOTATION_TEXT_SPAN, CELL_ANNOTATION_TEXT_TAB,  OO_NS_TEXT, "tab", GSF_XML_NO_CONTENT, NULL, NULL),/* 2nd def */
	            GSF_XML_IN_NODE (CELL_ANNOTATION, CELL_ANNOTATION_AUTHOR, OO_NS_DC, "creator", GSF_XML_CONTENT, NULL, &odf_annotation_author_end),
	            GSF_XML_IN_NODE (CELL_ANNOTATION, CELL_ANNOTATION_DATE, OO_NS_DC, "date", GSF_XML_NO_CONTENT, NULL, NULL),

		GSF_XML_IN_NODE (TABLE_ROW, TABLE_COVERED_CELL, OO_NS_TABLE, "covered-table-cell", GSF_XML_NO_CONTENT, &oo_covered_cell_start, &oo_covered_cell_end),
		  GSF_XML_IN_NODE (TABLE_COVERED_CELL, COVERED_CELL_TEXT, OO_NS_TEXT, "p", GSF_XML_NO_CONTENT, NULL, NULL),
		    GSF_XML_IN_NODE (COVERED_CELL_TEXT, COVERED_CELL_TEXT_S,    OO_NS_TEXT, "s", GSF_XML_NO_CONTENT, NULL, NULL),
	          GSF_XML_IN_NODE (TABLE_COVERED_CELL, DRAW_CONTROL, OO_NS_DRAW, "control", GSF_XML_NO_CONTENT, NULL, NULL),

	      GSF_XML_IN_NODE (TABLE, TABLE_COL_GROUP, OO_NS_TABLE, "table-column-group", GSF_XML_NO_CONTENT, NULL, NULL),
		GSF_XML_IN_NODE (TABLE_COL_GROUP, TABLE_COL_GROUP, OO_NS_TABLE, "table-column-group", GSF_XML_NO_CONTENT, NULL, NULL),
		GSF_XML_IN_NODE (TABLE_COL_GROUP, TABLE_COL, OO_NS_TABLE, "table-column", GSF_XML_NO_CONTENT, NULL, NULL), /* 2nd def */
	      GSF_XML_IN_NODE (TABLE_ROW_GROUP, TABLE_ROW_GROUP, OO_NS_TABLE, "table-row-group", GSF_XML_NO_CONTENT, NULL, NULL),
	    GSF_XML_IN_NODE (TABLE, TABLE_ROW_GROUP,	      OO_NS_TABLE, "table-row-group", GSF_XML_NO_CONTENT, NULL, NULL),
	      GSF_XML_IN_NODE (TABLE_ROW_GROUP, TABLE_ROW,	    OO_NS_TABLE, "table-row", GSF_XML_NO_CONTENT, NULL, NULL), /* 2nd def */
	  GSF_XML_IN_NODE (TABLE, NAMED_EXPRS, OO_NS_TABLE, "named-expressions", GSF_XML_NO_CONTENT, NULL, NULL),
	  GSF_XML_IN_NODE (SPREADSHEET, NAMED_EXPRS, OO_NS_TABLE, "named-expressions", GSF_XML_NO_CONTENT, NULL, NULL), /* 2nd def */
	    GSF_XML_IN_NODE (NAMED_EXPRS, NAMED_EXPR, OO_NS_TABLE, "named-expression", GSF_XML_NO_CONTENT, &oo_named_expr, NULL),
	    GSF_XML_IN_NODE (NAMED_EXPRS, NAMED_RANGE, OO_NS_TABLE, "named-range", GSF_XML_NO_CONTENT, &oo_named_expr, NULL),

	  GSF_XML_IN_NODE (SPREADSHEET, DB_RANGES, OO_NS_TABLE, "database-ranges", GSF_XML_NO_CONTENT, NULL, NULL),
	    GSF_XML_IN_NODE (DB_RANGES, DB_RANGE, OO_NS_TABLE, "database-range", GSF_XML_NO_CONTENT, &oo_db_range_start, &oo_db_range_end),
	      GSF_XML_IN_NODE (DB_RANGE, FILTER, OO_NS_TABLE, "filter", GSF_XML_NO_CONTENT, NULL, NULL),
		GSF_XML_IN_NODE (FILTER, FILTER_COND, OO_NS_TABLE, "filter-condition", GSF_XML_NO_CONTENT, &oo_filter_cond, NULL),
	    GSF_XML_IN_NODE (DB_RANGE, TABLE_SORT, OO_NS_TABLE, "sort", GSF_XML_NO_CONTENT, NULL, NULL),
	      GSF_XML_IN_NODE (TABLE_SORT, SORT_BY, OO_NS_TABLE, "sort-by", GSF_XML_NO_CONTENT, NULL, NULL),

GSF_XML_IN_NODE_END
};

static GsfXMLInNode const *get_dtd () { return opendoc_content_dtd; }
static GsfXMLInNode const *get_styles_dtd () { return styles_dtd; }

/****************************************************************************/

static GnmExpr const *
odf_func_address_handler (GnmConventions const *convs, Workbook *scope, GnmExprList *args)
{
	guint argc = gnm_expr_list_length (args);

	if (argc == 4 && convs->sheet_name_sep == '!') {
		/* Openoffice was missing the A1 parameter */
		GnmExprList *new_args;
		GnmFunc  *f = gnm_func_lookup_or_add_placeholder ("ADDRESS", scope, FALSE);

		new_args = g_slist_insert ((GSList *) args,
					   (gpointer) gnm_expr_new_constant (value_new_int (1)),
					   3);
		return gnm_expr_new_funcall (f, new_args);
	}
	return NULL;
}

static GnmExpr const *
odf_func_phi_handler (GnmConventions const *convs, Workbook *scope, GnmExprList *args)
{
	GnmFunc  *f = gnm_func_lookup_or_add_placeholder ("NORMDIST", scope, FALSE);

	args = g_slist_append (args,
			       (gpointer) gnm_expr_new_constant (value_new_int (0)));
	args = g_slist_append (args,
			       (gpointer) gnm_expr_new_constant (value_new_int (1)));

	args = g_slist_append (args,
			       (gpointer) gnm_expr_new_funcall
			       (gnm_func_lookup_or_add_placeholder ("FALSE", scope, FALSE), NULL));

	return gnm_expr_new_funcall (f, args);
}

static GnmExpr const *
odf_func_gauss_handler (GnmConventions const *convs, Workbook *scope, GnmExprList *args)
{
	guint argc = gnm_expr_list_length (args);
	GnmFunc  *f = gnm_func_lookup_or_add_placeholder ("ERF", scope, FALSE);
	GnmFunc  *fs = gnm_func_lookup_or_add_placeholder ("SQRT", scope, FALSE);
	GnmExpr const * expr;

	if (argc != 1)
		return NULL;

	expr = gnm_expr_new_binary (gnm_expr_new_funcall1
				    (f, gnm_expr_new_binary ((gnm_expr_copy ((GnmExpr const *)(args->data))),
							     GNM_EXPR_OP_DIV,
									     gnm_expr_new_funcall1 (fs,
												    gnm_expr_new_constant
												    (value_new_int (2))))),
				    GNM_EXPR_OP_DIV,
				    gnm_expr_new_constant (value_new_int (2)));
	gnm_expr_list_unref (args);
	return expr;
}

static GnmExpr const *
odf_func_floor_handler (GnmConventions const *convs, Workbook *scope, GnmExprList *args)
{
	guint argc = gnm_expr_list_length (args);
	GnmExpr const *expr_x;
	GnmExpr const *expr_sig;
	GnmExpr const *expr_mode;
	GnmExpr const *expr_mode_zero;
	GnmExpr const *expr_mode_one;
	GnmExpr const *expr_if;
	GnmFunc  *fd_ceiling;
	GnmFunc  *fd_floor;
	GnmFunc  *fd_if;

	if (argc == 0 || argc > 3)
		return NULL;

	fd_ceiling = gnm_func_lookup_or_add_placeholder ("CEILING", scope, FALSE);
	fd_floor = gnm_func_lookup_or_add_placeholder ("FLOOR", scope, FALSE);
	fd_if = gnm_func_lookup_or_add_placeholder ("IF", scope, FALSE);

	expr_x = g_slist_nth_data ((GSList *) args, 0);
	if (argc > 1)
		expr_sig = gnm_expr_copy (g_slist_nth_data ((GSList *) args, 1));
	else {
		GnmFunc  *fd_sign = gnm_func_lookup_or_add_placeholder ("SIGN", scope, FALSE);
		expr_sig = gnm_expr_new_funcall1 (fd_sign, gnm_expr_copy (expr_x));
	}

	expr_mode_zero = gnm_expr_new_funcall3
		(fd_if,
		 gnm_expr_new_binary
		 (gnm_expr_copy (expr_x),
		  GNM_EXPR_OP_LT,
		  gnm_expr_new_constant (value_new_int (0))),
		 gnm_expr_new_funcall2
		 (fd_ceiling,
		  gnm_expr_copy (expr_x),
		  gnm_expr_copy (expr_sig)),
		 gnm_expr_new_funcall2
		 (fd_floor,
		  gnm_expr_copy (expr_x),
		  gnm_expr_copy (expr_sig)));
	if (argc < 3) {
		gnm_expr_free (expr_sig);
		gnm_expr_list_unref (args);
		return expr_mode_zero;
	}

	expr_mode_one =
		gnm_expr_new_funcall2
		(fd_floor,
		 gnm_expr_copy (expr_x),
		 gnm_expr_copy (expr_sig));

	expr_mode = g_slist_nth_data ((GSList *) args, 2);
	if (GNM_EXPR_GET_OPER (expr_mode) == GNM_EXPR_OP_CONSTANT) {
		GnmValue const * val = expr_mode->constant.value;
		if (VALUE_IS_NUMBER (val)) {
			gnm_float value = value_get_as_float (val);
			if (value == 0.) {
				gnm_expr_free (expr_mode_one);
				gnm_expr_list_unref (args);
				gnm_expr_free (expr_sig);
				return expr_mode_zero;
			} else {
				gnm_expr_free (expr_mode_zero);
				gnm_expr_list_unref (args);
				gnm_expr_free (expr_sig);
				return expr_mode_one;
			}
		}
	}
	expr_if = gnm_expr_new_funcall3
		(fd_if,
		 gnm_expr_new_binary
		 (gnm_expr_new_constant (value_new_int (0)),
		  GNM_EXPR_OP_EQUAL,
		  gnm_expr_copy (expr_mode)),
		 expr_mode_zero,
		 expr_mode_one);

	gnm_expr_free (expr_sig);
	gnm_expr_list_unref (args);
	return expr_if;
}

static GnmExpr const *
odf_func_ceiling_handler (GnmConventions const *convs, Workbook *scope, GnmExprList *args)
{
	guint argc = gnm_expr_list_length (args);
	switch (argc) {
	case 1: {
		GnmFunc  *f = gnm_func_lookup_or_add_placeholder ("CEIL", scope, FALSE);
		return gnm_expr_new_funcall (f, args);
	}
	case 2: case 3: {
		GnmExpr const *expr_mode_zero;
		GnmExpr const *expr_mode_one;
		GnmExpr const *expr_if;
		GnmExpr const *expr_mode;
		GnmExpr const *expr_x = g_slist_nth_data ((GSList *) args, 0);
		GnmExpr const *expr_sig = g_slist_nth_data ((GSList *) args, 1);

		GnmFunc  *fd_ceiling = gnm_func_lookup_or_add_placeholder ("CEILING", scope, FALSE);
		GnmFunc  *fd_floor = gnm_func_lookup_or_add_placeholder ("FLOOR", scope, FALSE);
		GnmFunc  *fd_if = gnm_func_lookup_or_add_placeholder ("IF", scope, FALSE);

		expr_mode_zero = gnm_expr_new_funcall3
			(fd_if,
			 gnm_expr_new_binary
			 (gnm_expr_copy (expr_x),
			  GNM_EXPR_OP_LT,
			  gnm_expr_new_constant (value_new_int (0))),
			 gnm_expr_new_funcall2
			 (fd_floor,
			  gnm_expr_copy (expr_x),
			  gnm_expr_copy (expr_sig)),
			 gnm_expr_new_funcall2
			 (fd_ceiling,
			  gnm_expr_copy (expr_x),
			  gnm_expr_copy (expr_sig)));
		if (argc == 2) {
			gnm_expr_list_unref (args);
			return expr_mode_zero;
		}

		expr_mode_one =
			gnm_expr_new_funcall2
			(fd_ceiling,
			 gnm_expr_copy (expr_x),
			 gnm_expr_copy (expr_sig));

		expr_mode = g_slist_nth_data ((GSList *) args, 2);
		if (GNM_EXPR_GET_OPER (expr_mode) == GNM_EXPR_OP_CONSTANT) {
			GnmValue const * val = expr_mode->constant.value;
			if (VALUE_IS_NUMBER (val)) {
				gnm_float value = value_get_as_float (val);
				if (value == 0.) {
					gnm_expr_free (expr_mode_one);
					gnm_expr_list_unref (args);
					return expr_mode_zero;
				} else {
					gnm_expr_free (expr_mode_zero);
					gnm_expr_list_unref (args);
					return expr_mode_one;
				}
			}
		}
		expr_if = gnm_expr_new_funcall3
			(fd_if,
			 gnm_expr_new_binary
			 (gnm_expr_new_constant (value_new_int (0)),
			  GNM_EXPR_OP_EQUAL,
			  gnm_expr_copy (expr_mode)),
			 expr_mode_zero,
			 expr_mode_one);
		gnm_expr_list_unref (args);
		return expr_if;
	}
	default:
		break;
	}
	return NULL;
}

static GnmExpr const *
odf_func_chisqdist_handler (GnmConventions const *convs, Workbook *scope, GnmExprList *args)
{
	switch (gnm_expr_list_length (args)) {
	case 2: {
		GnmFunc  *f = gnm_func_lookup_or_add_placeholder ("R.PCHISQ", scope, FALSE);
		return gnm_expr_new_funcall (f, args);
	}
	case 3: {
		GSList * link = g_slist_nth ((GSList *) args, 2);
		GnmExpr const *expr = link->data;
		GnmFunc  *fd_if;
		GnmFunc  *fd_pchisq;
		GnmFunc  *fd_dchisq;
		GnmExpr  const *expr_pchisq;
		GnmExpr  const *expr_dchisq;

		args = (GnmExprList *) g_slist_remove_link ((GSList *) args, link);
		g_slist_free (link);

		if (GNM_EXPR_GET_OPER (expr) == GNM_EXPR_OP_FUNCALL) {
			if (go_ascii_strcase_equal (expr->func.func->name, "TRUE")) {
				fd_pchisq = gnm_func_lookup_or_add_placeholder ("R.PCHISQ", scope, FALSE);
				gnm_expr_free (expr);
				return gnm_expr_new_funcall (fd_pchisq, args);
			}
			if (go_ascii_strcase_equal (expr->func.func->name, "FALSE")) {
				fd_dchisq = gnm_func_lookup_or_add_placeholder ("R.DCHISQ", scope, FALSE);
				gnm_expr_free (expr);
				return gnm_expr_new_funcall (fd_dchisq, args);
			}
		}
		fd_if = gnm_func_lookup_or_add_placeholder ("IF", scope, FALSE);
		fd_pchisq = gnm_func_lookup_or_add_placeholder ("R.PCHISQ", scope, FALSE);
		fd_dchisq = gnm_func_lookup_or_add_placeholder ("R.DCHISQ", scope, FALSE);
		expr_pchisq = gnm_expr_new_funcall2
			(fd_pchisq, gnm_expr_copy (g_slist_nth_data ((GSList *) args, 0)),
			 gnm_expr_copy (g_slist_nth_data ((GSList *) args, 1)));
		expr_dchisq = gnm_expr_new_funcall (fd_dchisq, args);
		return gnm_expr_new_funcall3 (fd_if, expr, expr_pchisq, expr_dchisq);
	}
	default:
		break;
	}
	return NULL;
}


static GnmExpr const *
oo_func_map_in (GnmConventions const *convs, Workbook *scope,
		 char const *name, GnmExprList *args)
{
	static struct {
		char const *gnm_name;
		gpointer handler;
	} const sc_func_handlers[] = {
		{"CHISQDIST", odf_func_chisqdist_handler},
		{"CEILING", odf_func_ceiling_handler},
		{"FLOOR", odf_func_floor_handler},
		{"ADDRESS", odf_func_address_handler},
		{"PHI", odf_func_phi_handler},
		{"GAUSS", odf_func_gauss_handler},
		{NULL, NULL}
	};

	static struct {
		char const *oo_name;
		char const *gnm_name;
	} const sc_func_renames[] = {
/* The next functions are or were used by OpenOffice but are not in ODF OpenFormula draft 20090508 */

		{ "INDIRECT_XL",	"INDIRECT" },
		{ "ADDRESS_XL",		"ADDRESS" },
		{ "ERRORTYPE",		"ERROR.TYPE" },
		{ "EASTERSUNDAY",	"EASTERSUNDAY" }, /* OOo stores this without prefix! */
		{ "ORG.OPENOFFICE.EASTERSUNDAY",	"EASTERSUNDAY" },

/* The following is a list of the functions defined in ODF OpenFormula draft 20090508 */
/* where we do not have a function with the same name                                 */

		{ "AVERAGEIFS","ODF.AVERAGEIFS" },
		{ "COUNTIFS","ODF.COUNTIFS" },
		{ "DDE","ODF.DDE" },
		{ "MULTIPLE.OPERATIONS","ODF.MULTIPLE.OPERATIONS" },
		{ "SUMIFS","ODF.SUMIFS" },

/* The following is a complete list of the functions defined in ODF OpenFormula draft 20090508 */
/* We should determine whether any mapping is needed. */

		{ "B","BINOM.DIST.RANGE" },
		{ "CEILING","ODF.CEILING" },          /* see handler */
		{ "CHISQINV","R.QCHISQ" },
		{ "CHISQDIST","ODF.CHISQDIST" },      /* see handler */
		{ "FLOOR","ODF.FLOOR" },              /* see handler */
		{ "FORMULA","GET.FORMULA" },
		{ "GAUSS","ODF.GAUSS" },              /* see handler */
		{ "LEGACY.CHIDIST","CHIDIST" },
		{ "LEGACY.CHIINV","CHIINV" },
		{ "LEGACY.CHITEST","CHITEST" },
		{ "LEGACY.FDIST","FDIST" },
		{ "LEGACY.FINV","FINV" },
		{ "LEGACY.NORMSDIST","NORMSDIST" },
		{ "LEGACY.NORMSINV","NORMSINV" },
		{ "PDURATION","G_DURATION" },
		{ "PHI","NORMDIST" },              /* see handler */
		{ "USDOLLAR","DOLLAR" },

/* { "ADDRESS","ADDRESS" },       also  see handler */
/* { "ABS","ABS" }, */
/* { "ACCRINT","ACCRINT" }, */
/* { "ACCRINTM","ACCRINTM" }, */
/* { "ACOS","ACOS" }, */
/* { "ACOSH","ACOSH" }, */
/* { "ACOT","ACOT" }, */
/* { "ACOTH","ACOTH" }, */
/* { "AMORDEGRC","AMORDEGRC" }, */
/* { "AMORLINC","AMORLINC" }, */
/* { "AND","AND" }, */
/* { "ARABIC","ARABIC" }, */
/* { "AREAS","AREAS" }, */
/* { "ASC","ASC" }, */
/* { "ASIN","ASIN" }, */
/* { "ASINH","ASINH" }, */
/* { "ATAN","ATAN" }, */
/* { "ATAN2","ATAN2" }, */
/* { "ATANH","ATANH" }, */
/* { "AVEDEV","AVEDEV" }, */
/* { "AVERAGE","AVERAGE" }, */
/* { "AVERAGEA","AVERAGEA" }, */
/* { "AVERAGEIF","AVERAGEIF" }, */
/* { "AVERAGEIFS","AVERAGEIFS" }, */
/* { "BASE","BASE" }, */
/* { "BESSELI","BESSELI" }, */
/* { "BESSELJ","BESSELJ" }, */
/* { "BESSELK","BESSELK" }, */
/* { "BESSELY","BESSELY" }, */
/* { "BETADIST","BETADIST" }, */
/* { "BETAINV","BETAINV" }, */
/* { "BIN2DEC","BIN2DEC" }, */
/* { "BIN2HEX","BIN2HEX" }, */
/* { "BIN2OCT","BIN2OCT" }, */
/* { "BINOMDIST","BINOMDIST" }, */
/* { "BITAND","BITAND" }, */
/* { "BITLSHIFT","BITLSHIFT" }, */
/* { "BITOR","BITOR" }, */
/* { "BITRSHIFT","BITRSHIFT" }, */
/* { "BITXOR","BITXOR" }, */
/* { "CHAR","CHAR" }, */
/* { "CHOOSE","CHOOSE" }, */
/* { "CLEAN","CLEAN" }, */
/* { "CODE","CODE" }, */
/* { "COLUMN","COLUMN" }, */
/* { "COLUMNS","COLUMNS" }, */
/* { "COMBIN","COMBIN" }, */
/* { "COMBINA","COMBINA" }, */
/* { "COMPLEX","COMPLEX" }, */
/* { "CONCATENATE","CONCATENATE" }, */
/* { "CONFIDENCE","CONFIDENCE" }, */
/* { "CONVERT","CONVERT" }, */
/* { "CORREL","CORREL" }, */
/* { "COS","COS" }, */
/* { "COSH","COSH" }, */
/* { "COT","COT" }, */
/* { "COTH","COTH" }, */
/* { "COUNT","COUNT" }, */
/* { "COUNTA","COUNTA" }, */
/* { "COUNTBLANK","COUNTBLANK" }, */
/* { "COUNTIF","COUNTIF" }, */
/* { "COUNTIFS","COUNTIFS" }, */
/* { "COUPDAYBS","COUPDAYBS" }, */
/* { "COUPDAYS","COUPDAYS" }, */
/* { "COUPDAYSNC","COUPDAYSNC" }, */
/* { "COUPNCD","COUPNCD" }, */
/* { "COUPNUM","COUPNUM" }, */
/* { "COUPPCD","COUPPCD" }, */
/* { "COVAR","COVAR" }, */
/* { "CRITBINOM","CRITBINOM" }, */
/* { "CSC","CSC" }, */
/* { "CSCH","CSCH" }, */
/* { "CUMIPMT","CUMIPMT" }, */
/* { "CUMPRINC","CUMPRINC" }, */
/* { "DATE","DATE" }, */
/* { "DATEDIF","DATEDIF" }, */
/* { "DATEVALUE","DATEVALUE" }, */
/* { "DAVERAGE","DAVERAGE" }, */
/* { "DAY","DAY" }, */
/* { "DAYS","DAYS" }, */
/* { "DAYS360","DAYS360" }, */
/* { "DB","DB" }, */
/* { "DCOUNT","DCOUNT" }, */
/* { "DCOUNTA","DCOUNTA" }, */
/* { "DDB","DDB" }, */
/* { "DDE","DDE" }, */
/* { "DEC2BIN","DEC2BIN" }, */
/* { "DEC2HEX","DEC2HEX" }, */
/* { "DEC2OCT","DEC2OCT" }, */
/* { "DECIMAL","DECIMAL" }, */
/* { "DEGREES","DEGREES" }, */
/* { "DELTA","DELTA" }, */
/* { "DEVSQ","DEVSQ" }, */
/* { "DGET","DGET" }, */
/* { "DISC","DISC" }, */
/* { "DMAX","DMAX" }, */
/* { "DMIN","DMIN" }, */
/* { "DOLLARDE","DOLLARDE" }, */
/* { "DOLLARFR","DOLLARFR" }, */
/* { "DPRODUCT","DPRODUCT" }, */
/* { "DSTDEV","DSTDEV" }, */
/* { "DSTDEVP","DSTDEVP" }, */
/* { "DSUM","DSUM" }, */
/* { "DURATION","DURATION" }, */
/* { "DVAR","DVAR" }, */
/* { "DVARP","DVARP" }, */
/* { "EDATE","EDATE" }, */
/* { "EFFECT","EFFECT" }, */
/* { "EOMONTH","EOMONTH" }, */
/* { "ERF","ERF" }, */
/* { "ERFC","ERFC" }, */
/* { "ERROR.TYPE","ERROR.TYPE" }, */
/* { "EUROCONVERT","EUROCONVERT" }, */
/* { "EVEN","EVEN" }, */
/* { "EXACT","EXACT" }, */
/* { "EXP","EXP" }, */
/* { "EXPONDIST","EXPONDIST" }, */
/* { "FACT","FACT" }, */
/* { "FACTDOUBLE","FACTDOUBLE" }, */
/* { "FALSE","FALSE" }, */
/* { "FDIST","FDIST" }, */
/* { "FIND","FIND" }, */
/* { "FINDB","FINDB" }, */
/* { "FINV","FINV" }, */
/* { "FISHER","FISHER" }, */
/* { "FISHERINV","FISHERINV" }, */
/* { "FIXED","FIXED" }, */
/* { "FORECAST","FORECAST" }, */
/* { "FREQUENCY","FREQUENCY" }, */
/* { "FTEST","FTEST" }, */
/* { "FV","FV" }, */
/* { "FVSCHEDULE","FVSCHEDULE" }, */
/* { "GAMMA","GAMMA" }, */
/* { "GAMMADIST","GAMMADIST" }, */
/* { "GAMMAINV","GAMMAINV" }, */
/* { "GAMMALN","GAMMALN" }, */
/* { "GCD","GCD" }, */
/* { "GEOMEAN","GEOMEAN" }, */
/* { "GESTEP","GESTEP" }, */
/* { "GETPIVOTDATA","GETPIVOTDATA" }, */
/* { "GROWTH","GROWTH" }, */
/* { "HARMEAN","HARMEAN" }, */
/* { "HEX2BIN","HEX2BIN" }, */
/* { "HEX2DEC","HEX2DEC" }, */
/* { "HEX2OCT","HEX2OCT" }, */
/* { "HLOOKUP","HLOOKUP" }, */
/* { "HOUR","HOUR" }, */
/* { "HYPERLINK","HYPERLINK" }, */
/* { "HYPGEOMDIST","HYPGEOMDIST" }, */
/* { "IF","IF" }, */
/* { "IFERROR","IFERROR" }, */
/* { "IFNA","IFNA" }, */
/* { "IMABS","IMABS" }, */
/* { "IMAGINARY","IMAGINARY" }, */
/* { "IMARGUMENT","IMARGUMENT" }, */
/* { "IMCONJUGATE","IMCONJUGATE" }, */
/* { "IMCOS","IMCOS" }, */
/* { "IMCOT","IMCOT" }, */
/* { "IMCSC","IMCSC" }, */
/* { "IMCSCH","IMCSCH" }, */
/* { "IMDIV","IMDIV" }, */
/* { "IMEXP","IMEXP" }, */
/* { "IMLN","IMLN" }, */
/* { "IMLOG10","IMLOG10" }, */
/* { "IMLOG2","IMLOG2" }, */
/* { "IMPOWER","IMPOWER" }, */
/* { "IMPRODUCT","IMPRODUCT" }, */
/* { "IMREAL","IMREAL" }, */
/* { "IMSEC","IMSEC" }, */
/* { "IMSECH","IMSECH" }, */
/* { "IMSIN","IMSIN" }, */
/* { "IMSQRT","IMSQRT" }, */
/* { "IMSUB","IMSUB" }, */
/* { "IMSUM","IMSUM" }, */
/* { "IMTAN","IMTAN" }, */
/* { "INDEX","INDEX" }, */
/* { "INDIRECT","INDIRECT" }, */
/* { "INFO","INFO" }, */
/* { "INT","INT" }, */
/* { "INTERCEPT","INTERCEPT" }, */
/* { "INTRATE","INTRATE" }, */
/* { "IPMT","IPMT" }, */
/* { "IRR","IRR" }, */
/* { "ISBLANK","ISBLANK" }, */
/* { "ISERR","ISERR" }, */
/* { "ISERROR","ISERROR" }, */
/* { "ISEVEN","ISEVEN" }, */
/* { "ISFORMULA","ISFORMULA" }, */
/* { "ISLOGICAL","ISLOGICAL" }, */
/* { "ISNA","ISNA" }, */
/* { "ISNONTEXT","ISNONTEXT" }, */
/* { "ISNUMBER","ISNUMBER" }, */
/* { "ISODD","ISODD" }, */
/* { "ISOWEEKNUM","ISOWEEKNUM" }, */
/* { "ISPMT","ISPMT" }, */
/* { "ISREF","ISREF" }, */
/* { "ISTEXT","ISTEXT" }, */
/* { "JIS","JIS" }, */
/* { "KURT","KURT" }, */
/* { "LARGE","LARGE" }, */
/* { "LCM","LCM" }, */
/* { "LEFT","LEFT" }, */
/* { "LEFTB","LEFTB" }, */
/* { "LEN","LEN" }, */
/* { "LENB","LENB" }, */
/* { "LINEST","LINEST" }, */
/* { "LN","LN" }, */
/* { "LOG","LOG" }, */
/* { "LOG10","LOG10" }, */
/* { "LOGEST","LOGEST" }, */
/* { "LOGINV","LOGINV" }, */
/* { "LOGNORMDIST","LOGNORMDIST" }, */
/* { "LOOKUP","LOOKUP" }, */
/* { "LOWER","LOWER" }, */
/* { "MATCH","MATCH" }, */
/* { "MAX","MAX" }, */
/* { "MAXA","MAXA" }, */
/* { "MDETERM","MDETERM" }, */
/* { "MDURATION","MDURATION" }, */
/* { "MEDIAN","MEDIAN" }, */
/* { "MID","MID" }, */
/* { "MIDB","MIDB" }, */
/* { "MIN","MIN" }, */
/* { "MINA","MINA" }, */
/* { "MINUTE","MINUTE" }, */
/* { "MINVERSE","MINVERSE" }, */
/* { "MIRR","MIRR" }, */
/* { "MMULT","MMULT" }, */
/* { "MOD","MOD" }, */
/* { "MODE","MODE" }, */
/* { "MONTH","MONTH" }, */
/* { "MROUND","MROUND" }, */
/* { "MULTINOMIAL","MULTINOMIAL" }, */
/* { "MULTIPLE.OPERATIONS","MULTIPLE.OPERATIONS" }, */
/* { "MUNIT","MUNIT" }, */
/* { "N","N" }, */
/* { "NA","NA" }, */
/* { "NEGBINOMDIST","NEGBINOMDIST" }, */
/* { "NETWORKDAYS","NETWORKDAYS" }, */
/* { "NOMINAL","NOMINAL" }, */
/* { "NORMDIST","NORMDIST" }, */
/* { "NORMINV","NORMINV" }, */
/* { "NOT","NOT" }, */
/* { "NOW","NOW" }, */
/* { "NPER","NPER" }, */
/* { "NPV","NPV" }, */
/* { "NUMBERVALUE","NUMBERVALUE" }, */
/* { "OCT2BIN","OCT2BIN" }, */
/* { "OCT2DEC","OCT2DEC" }, */
/* { "OCT2HEX","OCT2HEX" }, */
/* { "ODD","ODD" }, */
/* { "ODDFPRICE","ODDFPRICE" }, */
/* { "ODDFYIELD","ODDFYIELD" }, */
/* { "ODDLPRICE","ODDLPRICE" }, */
/* { "ODDLYIELD","ODDLYIELD" }, */
/* { "OFFSET","OFFSET" }, */
/* { "OR","OR" }, */
/* { "PEARSON","PEARSON" }, */
/* { "PERCENTILE","PERCENTILE" }, */
/* { "PERCENTRANK","PERCENTRANK" }, */
/* { "PERMUT","PERMUT" }, */
/* { "PERMUTATIONA","PERMUTATIONA" }, */
/* { "PI","PI" }, */
/* { "PMT","PMT" }, */
/* { "POISSON","POISSON" }, */
/* { "POWER","POWER" }, */
/* { "PPMT","PPMT" }, */
/* { "PRICE","PRICE" }, */
/* { "PRICEDISC","PRICEDISC" }, */
/* { "PRICEMAT","PRICEMAT" }, */
/* { "PROB","PROB" }, */
/* { "PRODUCT","PRODUCT" }, */
/* { "PROPER","PROPER" }, */
/* { "PV","PV" }, */
/* { "QUARTILE","QUARTILE" }, */
/* { "QUOTIENT","QUOTIENT" }, */
/* { "RADIANS","RADIANS" }, */
/* { "RAND","RAND" }, */
/* { "RANDBETWEEN","RANDBETWEEN" }, */
/* { "RANK","RANK" }, */
/* { "RATE","RATE" }, */
/* { "RECEIVED","RECEIVED" }, */
/* { "REPLACE","REPLACE" }, */
/* { "REPLACEB","REPLACEB" }, */
/* { "REPT","REPT" }, */
/* { "RIGHT","RIGHT" }, */
/* { "RIGHTB","RIGHTB" }, */
/* { "ROMAN","ROMAN" }, */
/* { "ROUND","ROUND" }, */
/* { "ROUNDDOWN","ROUNDDOWN" }, */
/* { "ROUNDUP","ROUNDUP" }, */
/* { "ROW","ROW" }, */
/* { "ROWS","ROWS" }, */
/* { "RRI","RRI" }, */
/* { "RSQ","RSQ" }, */
/* { "SEARCH","SEARCH" }, */
/* { "SEARCHB","SEARCHB" }, */
/* { "SEC","SEC" }, */
/* { "SECH","SECH" }, */
/* { "SECOND","SECOND" }, */
/* { "SERIESSUM","SERIESSUM" }, */
/* { "SHEET","SHEET" }, */
/* { "SHEETS","SHEETS" }, */
/* { "SIGN","SIGN" }, */
/* { "SIN","SIN" }, */
/* { "SINH","SINH" }, */
/* { "SKEW","SKEW" }, */
/* { "SKEWP","SKEWP" }, */
/* { "SLN","SLN" }, */
/* { "SLOPE","SLOPE" }, */
/* { "SMALL","SMALL" }, */
/* { "SQRT","SQRT" }, */
/* { "SQRTPI","SQRTPI" }, */
/* { "STANDARDIZE","STANDARDIZE" }, */
/* { "STDEV","STDEV" }, */
/* { "STDEVA","STDEVA" }, */
/* { "STDEVP","STDEVP" }, */
/* { "STDEVPA","STDEVPA" }, */
/* { "STEYX","STEYX" }, */
/* { "SUBSTITUTE","SUBSTITUTE" }, */
/* { "SUBTOTAL","SUBTOTAL" }, */
/* { "SUM","SUM" }, */
/* { "SUMIF","SUMIF" }, */
/* { "SUMIFS","SUMIFS" }, */
/* { "SUMPRODUCT","SUMPRODUCT" }, */
/* { "SUMSQ","SUMSQ" }, */
/* { "SUMX2MY2","SUMX2MY2" }, */
/* { "SUMX2PY2","SUMX2PY2" }, */
/* { "SUMXMY2","SUMXMY2" }, */
/* { "SYD","SYD" }, */
/* { "T","T" }, */
/* { "TAN","TAN" }, */
/* { "TANH","TANH" }, */
/* { "TBILLEQ","TBILLEQ" }, */
/* { "TBILLPRICE","TBILLPRICE" }, */
/* { "TBILLYIELD","TBILLYIELD" }, */
/* { "TDIST","TDIST" }, */
/* { "TEXT","TEXT" }, */
/* { "TIME","TIME" }, */
/* { "TIMEVALUE","TIMEVALUE" }, */
/* { "TINV","TINV" }, */
/* { "TODAY","TODAY" }, */
/* { "TRANSPOSE","TRANSPOSE" }, */
/* { "TREND","TREND" }, */
/* { "TRIM","TRIM" }, */
/* { "TRIMMEAN","TRIMMEAN" }, */
/* { "TRUE","TRUE" }, */
/* { "TRUNC","TRUNC" }, */
/* { "TTEST","TTEST" }, */
/* { "TYPE","TYPE" }, */
/* { "UNICHAR","UNICHAR" }, */
/* { "UNICODE","UNICODE" }, */
/* { "UPPER","UPPER" }, */
/* { "VALUE","VALUE" }, */
/* { "VAR","VAR" }, */
/* { "VARA","VARA" }, */
/* { "VARP","VARP" }, */
/* { "VARPA","VARPA" }, */
/* { "VDB","VDB" }, */
/* { "VLOOKUP","VLOOKUP" }, */
/* { "WEEKDAY","WEEKDAY" }, */
/* { "WEEKNUM","WEEKNUM" }, */
/* { "WEIBULL","WEIBULL" }, */
/* { "WORKDAY","WORKDAY" }, */
/* { "XIRR","XIRR" }, */
/* { "XNPV","XNPV" }, */
/* { "XOR","XOR" }, */
/* { "YEAR","YEAR" }, */
/* { "YEARFRAC","YEARFRAC" }, */
/* { "YIELD","YIELD" }, */
/* { "YIELDDISC","YIELDDISC" }, */
/* { "YIELDMAT","YIELDMAT" }, */
/* { "ZTEST","ZTEST" }, */
		{ NULL, NULL }
	};
	static char const OOoAnalysisPrefix[] = "com.sun.star.sheet.addin.Analysis.get";
	static char const GnumericPrefix[] = "ORG.GNUMERIC.";
	static GHashTable *namemap = NULL;
	static GHashTable *handlermap = NULL;

	GnmFunc  *f;
	char const *new_name;
	int i;
	GnmExpr const * (*handler) (GnmConventions const *convs, Workbook *scope, GnmExprList *args);

	if (NULL == namemap) {
		namemap = g_hash_table_new (go_ascii_strcase_hash,
					    go_ascii_strcase_equal);
		for (i = 0; sc_func_renames[i].oo_name; i++)
			g_hash_table_insert (namemap,
				(gchar *) sc_func_renames[i].oo_name,
				(gchar *) sc_func_renames[i].gnm_name);
	}
	if (NULL == handlermap) {
		guint i;
		handlermap = g_hash_table_new (go_ascii_strcase_hash,
					       go_ascii_strcase_equal);
		for (i = 0; sc_func_handlers[i].gnm_name; i++)
			g_hash_table_insert (handlermap,
					     (gchar *) sc_func_handlers[i].gnm_name,
					     sc_func_handlers[i].handler);
	}

	handler = g_hash_table_lookup (handlermap, name);
	if (handler != NULL) {
		GnmExpr const * res = handler (convs, scope, args);
		if (res != NULL)
			return res;
	}

	if (0 == g_ascii_strncasecmp (name, GnumericPrefix, sizeof (GnumericPrefix)-1)) {
		f = gnm_func_lookup_or_add_placeholder (name+sizeof (GnumericPrefix)-1, scope, TRUE);
	} else if (0 != g_ascii_strncasecmp (name, OOoAnalysisPrefix, sizeof (OOoAnalysisPrefix)-1)) {
		if (NULL != namemap &&
		    NULL != (new_name = g_hash_table_lookup (namemap, name)))
			name = new_name;
		f = gnm_func_lookup_or_add_placeholder (name, scope, TRUE);
	} else
		f = gnm_func_lookup_or_add_placeholder (name+sizeof (OOoAnalysisPrefix)-1, scope, TRUE);

	return gnm_expr_new_funcall (f, args);
}

static OOVer
determine_oo_version (GsfInfile *zip, OOVer def)
{
	char const *header;
	size_t size;
	GsfInput *mimetype = gsf_infile_child_by_name (zip, "mimetype");
	if (!mimetype) {
		/* Really old versions had no mimetype.  Allow that, except
		   in the probe.  */
		return def;
	}

	/* pick arbitrary size limit of 2k for the mimetype to avoid
	 * potential of any funny business */
	size = MIN (gsf_input_size (mimetype), 2048);
	header = gsf_input_read (mimetype, size, NULL);

	if (header) {
		unsigned ui;

		for (ui = 0 ; ui < G_N_ELEMENTS (OOVersions) ; ui++)
			if (size == strlen (OOVersions[ui].mime_type) &&
			    !memcmp (OOVersions[ui].mime_type, header, size)) {
				g_object_unref (mimetype);
				return OOVersions[ui].version;
			}
	}

	g_object_unref (mimetype);
	return OOO_VER_UNKNOWN;
}

void
openoffice_file_open (GOFileOpener const *fo, GOIOContext *io_context,
		      WorkbookView *wb_view, GsfInput *input);
G_MODULE_EXPORT void
openoffice_file_open (GOFileOpener const *fo, GOIOContext *io_context,
		      WorkbookView *wb_view, GsfInput *input)
{
	GsfXMLInDoc	*doc;
	GsfInput	*contents = NULL;
	GsfInput	*styles = NULL;
	GsfDocMetaData	*meta_data;
	GsfInfile	*zip;
	GnmLocale	*locale;
	OOParseState	 state;
	GError		*err = NULL;
	int i;

	zip = gsf_infile_zip_new (input, &err);
	if (zip == NULL) {
		g_return_if_fail (err != NULL);
		go_cmd_context_error_import (GO_CMD_CONTEXT (io_context),
			err->message);
		g_error_free (err);
		return;
	}

	state.ver = determine_oo_version (zip, OOO_VER_1);
	if (state.ver == OOO_VER_UNKNOWN) {
		/* TODO : include the unknown type in the message when
		 * we move the error handling into the importer object */
		go_cmd_context_error_import (GO_CMD_CONTEXT (io_context),
					     _("Unknown mimetype for openoffice file."));
		g_object_unref (zip);
		return;
	} else if (state.ver == OOO_VER_OPENDOC)
		state.ver_odf = 1.2; /* Probably most common at this time */
	else  state.ver_odf = 0.;

	contents = gsf_infile_child_by_name (zip, "content.xml");
	if (contents == NULL) {
		go_cmd_context_error_import (GO_CMD_CONTEXT (io_context),
			 _("No stream named content.xml found."));
		g_object_unref (zip);
		return;
	}

	styles = gsf_infile_child_by_name (zip, "styles.xml");
	if (styles == NULL) {
		go_cmd_context_error_import (GO_CMD_CONTEXT (io_context),
			 _("No stream named styles.xml found."));
		g_object_unref (contents);
		g_object_unref (zip);
		return;
	}

	locale = gnm_push_C_locale ();

	/* init */
	state.debug = gnm_debug_flag ("opendocumentimport");
	state.context	= io_context;
	state.wb_view	= wb_view;
	state.pos.wb	= wb_view_get_workbook (wb_view);
	state.zip = zip;
	state.pos.sheet = NULL;
	state.pos.eval.col	= -1;
	state.pos.eval.row	= -1;
	state.cell_comment      = NULL;
	state.chart.i_plot_styles[OO_CHART_STYLE_PLOTAREA] = NULL;
	state.chart.i_plot_styles[OO_CHART_STYLE_SERIES] = NULL;
	state.styles.sheet = g_hash_table_new_full (g_str_hash, g_str_equal,
		(GDestroyNotify) g_free,
		(GDestroyNotify) g_free);
	state.styles.col = g_hash_table_new_full (g_str_hash, g_str_equal,
		(GDestroyNotify) g_free,
		(GDestroyNotify) g_free);
	state.styles.row = g_hash_table_new_full (g_str_hash, g_str_equal,
		(GDestroyNotify) g_free,
		(GDestroyNotify) g_free);
	state.styles.cell = g_hash_table_new_full (g_str_hash, g_str_equal,
		(GDestroyNotify) g_free,
		(GDestroyNotify) gnm_style_unref);
	state.styles.cell_datetime = g_hash_table_new_full (g_str_hash, g_str_equal,
		(GDestroyNotify) g_free,
		(GDestroyNotify) gnm_style_unref);
	state.styles.cell_date = g_hash_table_new_full (g_str_hash, g_str_equal,
		(GDestroyNotify) g_free,
		(GDestroyNotify) gnm_style_unref);
	state.styles.cell_time = g_hash_table_new_full (g_str_hash, g_str_equal,
		(GDestroyNotify) g_free,
		(GDestroyNotify) gnm_style_unref);
	state.formats = g_hash_table_new_full (g_str_hash, g_str_equal,
		(GDestroyNotify) g_free,
		(GDestroyNotify) go_format_unref);
	state.validations = g_hash_table_new_full (g_str_hash, g_str_equal,
		(GDestroyNotify) g_free,
		(GDestroyNotify) odf_validation_free);
	state.chart.saved_graph_styles = NULL;
	state.chart.saved_hatches = NULL;
	state.chart.saved_dash_styles = NULL;
	state.chart.saved_fill_image_styles = NULL;
	state.chart.saved_gradient_styles = NULL;
	state.chart.graph_styles = g_hash_table_new_full
		(g_str_hash, g_str_equal,
		(GDestroyNotify) g_free,
		(GDestroyNotify) oo_chart_style_free);
	state.chart.hatches = g_hash_table_new_full
		(g_str_hash, g_str_equal,
		(GDestroyNotify) g_free,
		(GDestroyNotify) g_free);
	state.chart.dash_styles = g_hash_table_new_full
		(g_str_hash, g_str_equal,
		 (GDestroyNotify) g_free,
		 NULL);
	state.chart.fill_image_styles = g_hash_table_new_full
		(g_str_hash, g_str_equal,
		(GDestroyNotify) g_free,
		(GDestroyNotify) g_free);
	state.chart.gradient_styles = g_hash_table_new_full
		(g_str_hash, g_str_equal,
		 (GDestroyNotify) g_free,
		 (GDestroyNotify) g_free);
	state.controls = g_hash_table_new_full
		(g_str_hash, g_str_equal,
		 (GDestroyNotify) g_free,
		 (GDestroyNotify) oo_control_free);
	state.chart.arrow_markers = g_hash_table_new_full
		(g_str_hash, g_str_equal,
		 (GDestroyNotify) g_free,
		 (GDestroyNotify) oo_marker_free);
	state.cur_style.cells    = NULL;
	state.cur_style.col_rows = NULL;
	state.cur_style.sheets   = NULL;
	state.default_style.cells = NULL;
	state.default_style.rows = NULL;
	state.default_style.columns = NULL;
	state.cur_style.type   = OO_STYLE_UNKNOWN;
	state.cur_style.requires_disposal = FALSE;
	state.sheet_order = NULL;
	for (i = 0; i<NUM_FORMULAE_SUPPORTED; i++)
		state.convs[i] = NULL;
	state.cur_format.accum = NULL;
	state.cur_format.percentage = FALSE;
	state.filter = NULL;
	state.page_breaks.h = state.page_breaks.v = NULL;
	state.last_progress_update = 0;
	state.last_error = NULL;
	state.cur_control = NULL;

	go_io_progress_message (state.context, _("Reading file..."));
	go_io_value_progress_set (state.context, gsf_input_size (contents), 0);

	if (state.ver == OOO_VER_OPENDOC) {
		GsfInput *meta_file = gsf_infile_child_by_name (zip, "meta.xml");
		if (NULL != meta_file) {
			meta_data = gsf_doc_meta_data_new ();
			err = gsf_opendoc_metadata_read (meta_file, meta_data);
			if (NULL != err) {
				go_io_warning (io_context,
					_("Invalid metadata '%s'"), err->message);
				g_error_free (err);
			} else
				go_doc_set_meta_data (GO_DOC (state.pos.wb), meta_data);

			g_object_unref (meta_data);
			g_object_unref (meta_file);
		}
	}

	if (NULL != styles) {
		GsfXMLInDoc *doc = gsf_xml_in_doc_new (styles_dtd, gsf_ooo_ns);
		gsf_xml_in_doc_parse (doc, styles, &state);
		gsf_xml_in_doc_free (doc);
		odf_clear_conventions (&state); /* contain references to xin */
		g_object_unref (styles);
	}

	doc  = gsf_xml_in_doc_new (
		(state.ver == OOO_VER_1) ? ooo1_content_dtd : opendoc_content_dtd,
		gsf_ooo_ns);
	if (gsf_xml_in_doc_parse (doc, contents, &state)) {
		GsfInput *settings;
		char const *filesaver;

		/* get the sheet in the right order (in case something was
		 * created out of order implictly) */
		state.sheet_order = g_slist_reverse (state.sheet_order);

		if (state.debug) {
			GSList *l, *sheets;
			g_printerr ("Order we desire:\n");
			for (l = state.sheet_order; l; l = l->next) {
				Sheet *sheet = l->data;
				g_printerr ("Sheet %s\n", sheet->name_unquoted);
			}
			g_printerr ("Order we have:\n");
			sheets = workbook_sheets (state.pos.wb);
			for (l = sheets; l; l = l->next) {
				Sheet *sheet = l->data;
				g_printerr ("Sheet %s\n", sheet->name_unquoted);
			}
			g_slist_free (sheets);
		}

		workbook_sheet_reorder (state.pos.wb, state.sheet_order);
		g_slist_free (state.sheet_order);

		odf_fix_expr_names (&state);

		/* look for the view settings */
		state.settings.settings
			= g_hash_table_new_full (g_str_hash, g_str_equal,
						 (GDestroyNotify) g_free,
						 (GDestroyNotify) destroy_gvalue);
		state.settings.stack = NULL;
		settings = gsf_infile_child_by_name (zip, "settings.xml");
		if (settings != NULL) {
			GsfXMLInDoc *sdoc = gsf_xml_in_doc_new
				(opendoc_settings_dtd, gsf_ooo_ns);
			gsf_xml_in_doc_parse (sdoc, settings, &state);
			gsf_xml_in_doc_free (sdoc);
			odf_clear_conventions (&state); /* contain references to xin */
			g_object_unref (settings);
		}
		if (state.settings.stack != NULL) {
			go_cmd_context_error_import (GO_CMD_CONTEXT (io_context),
						     _("settings.xml stream is malformed!"));
			g_slist_foreach (state.settings.stack,
					 (GFunc)unset_gvalue,
					 NULL);
			go_slist_free_custom (state.settings.stack, g_free);
			state.settings.stack = NULL;
		}

		/* Use the settings here! */
		if (state.debug)
			g_hash_table_foreach (state.settings.settings,
					      (GHFunc)dump_settings_hash, (char *)"");
		if (!odf_has_gnm_foreign (&state)) {
			odf_apply_ooo_config (&state);
			filesaver = odf_created_by_gnumeric (&state) ?
				"Gnumeric_OpenCalc:openoffice"
				: "Gnumeric_OpenCalc:odf";
		} else
			filesaver = "Gnumeric_OpenCalc:odf";

		workbook_set_saveinfo (state.pos.wb, GO_FILE_FL_AUTO,
				       go_file_saver_for_id
				       (filesaver));

		g_hash_table_destroy (state.settings.settings);
		state.settings.settings = NULL;
	} else
		go_io_error_string (io_context, _("XML document not well formed!"));
	gsf_xml_in_doc_free (doc);
	odf_clear_conventions (&state);

	go_io_progress_unset (state.context);
	g_free (state.last_error);

	if (state.default_style.cells)
		gnm_style_unref (state.default_style.cells);
	g_free (state.default_style.rows);
	g_free (state.default_style.columns);
	g_hash_table_destroy (state.styles.sheet);
	g_hash_table_destroy (state.styles.col);
	g_hash_table_destroy (state.styles.row);
	g_hash_table_destroy (state.styles.cell);
	g_hash_table_destroy (state.styles.cell_datetime);
	g_hash_table_destroy (state.styles.cell_date);
	g_hash_table_destroy (state.styles.cell_time);
	go_slist_free_custom (state.chart.saved_graph_styles,
			      (GFreeFunc) g_hash_table_destroy);
	g_hash_table_destroy (state.chart.graph_styles);
	g_hash_table_destroy (state.chart.hatches);
	g_hash_table_destroy (state.chart.dash_styles);
	g_hash_table_destroy (state.chart.fill_image_styles);
	g_hash_table_destroy (state.chart.gradient_styles);
	g_hash_table_destroy (state.formats);
	g_hash_table_destroy (state.controls);
	g_hash_table_destroy (state.validations);
	g_hash_table_destroy (state.chart.arrow_markers);
	g_object_unref (contents);

	g_object_unref (zip);

	i = workbook_sheet_count (state.pos.wb);
	while (i-- > 0)
		sheet_flag_recompute_spans (workbook_sheet_by_index (state.pos.wb, i));
	workbook_queue_all_recalc (state.pos.wb);

	gnm_pop_C_locale (locale);
}

gboolean
openoffice_file_probe (GOFileOpener const *fo, GsfInput *input, GOFileProbeLevel pl);

gboolean
openoffice_file_probe (GOFileOpener const *fo, GsfInput *input, GOFileProbeLevel pl)
{
	GsfInfile *zip;
	OOVer ver;

	gboolean old_ext_ok = FALSE;
	char const *name = gsf_input_name (input);
	if (name) {
		name = gsf_extension_pointer (name);
		old_ext_ok = (name != NULL &&
			      (g_ascii_strcasecmp (name, "sxc") == 0 ||
			       g_ascii_strcasecmp (name, "stc") == 0));
	}

	zip = gsf_infile_zip_new (input, NULL);
	if (zip == NULL)
		return FALSE;

	ver = determine_oo_version
		(zip, old_ext_ok ? OOO_VER_1 : OOO_VER_UNKNOWN);

	g_object_unref (zip);

	return ver != OOO_VER_UNKNOWN;
}

G_MODULE_EXPORT void
go_plugin_init (GOPlugin *plugin, GOCmdContext *cc)
{
	magic_transparent = style_color_auto_back ();
}

G_MODULE_EXPORT void
go_plugin_shutdown (GOPlugin *plugin, GOCmdContext *cc)
{
	style_color_unref (magic_transparent);
	magic_transparent = NULL;
}
