/*
 * openoffice-read.c : import open/star calc files
 *
 * Copyright (C) 2002-2007 Jody Goldberg (jody@gnome.org)
 * Copyright (C) 2006 Luciano Miguel Wolf (luciano.wolf@indt.org.br)
 * Copyright (C) 2007 Morten Welinder (terra@gnome.org)
 * Copyright (C) 2006-2010 Andreas J. Guelzow (aguelzow@pyrshep.ca)
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

#include <gnm-plugin.h>
#include <workbook-view.h>
#include <workbook.h>
#include <sheet.h>
#include <sheet-view.h>
#include <sheet-merge.h>
#include <sheet-filter.h>
#include <selection.h>
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
#include <input-msg.h>
#include <gnm-format.h>
#include <print-info.h>
#include <command-context.h>
#include <gutils.h>
#include <xml-sax.h>
#include <sheet-object-cell-comment.h>
#include <sheet-object-widget.h>
#include <style-conditions.h>
#include <gnumeric-conf.h>
#include <mathfunc.h>
#include <sheet-object-graph.h>
#include <sheet-object-image.h>
#include <graph.h>
#include <gnm-so-filled.h>
#include <gnm-so-line.h>
#include <gnm-so-path.h>
#include <hlink.h>


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
	OO_PLOT_XL_CONTOUR,
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

typedef enum {
	OO_VALUE_TYPE_VOID,
	OO_VALUE_TYPE_FLOAT,
	OO_VALUE_TYPE_PERCENTAGE,
	OO_VALUE_TYPE_CURRENCY,
	OO_VALUE_TYPE_DATE,
	OO_VALUE_TYPE_TIME,
	OO_VALUE_TYPE_BOOLEAN,
	OO_VALUE_TYPE_STRING
} OOValueType;


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
	char *current_state;
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
	double width;
} OOMarker;

typedef struct {
	gboolean grid;		/* graph has grid? */
	gboolean src_in_rows;	/* orientation of graph data: rows or columns */
	GSList	*axis_props;	/* axis properties */
	GSList	*plot_props;	/* plot properties */
	GSList	*style_props;	/* any other properties */
	GSList	*other_props;	/* any other properties */
	GOFormat *fmt;
} OOChartStyle;

typedef struct {
	int       ref;
	GnmStyle *style;
	GSList   *styles;
	GSList   *conditions;
	GSList   *bases;
} OOCellStyle;

typedef struct {
	GogGraph	*graph;
	GogChart	*chart;
	SheetObject     *so;
	GSList          *list; /* used by Stock plot and textbox*/
	char            *name;
	char            *style_name;

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
	unsigned	 x_axis_count;	/* reset for each plotarea */
	unsigned	 y_axis_count;	/* reset for each plotarea */
	unsigned	 z_axis_count;	/* reset for each plotarea */

	GogObject	*axis;
	xmlChar         *cat_expr;
	GogObject	*regression;
	GogObject	*legend;

	GnmExprTop const        *title_expr;
	gchar                   *title_style;
	gchar                   *title_position;
	gboolean                 title_manual_pos;
	gchar                   *title_anchor;
	double                   title_x;
	double                   title_y;

	OOChartStyle		*cur_graph_style; /* for reading of styles */

	GSList		        *saved_graph_styles;
	GSList		        *saved_hatches;
	GSList		        *saved_dash_styles;
	GSList		        *saved_fill_image_styles;
	GSList		        *saved_gradient_styles;

	GHashTable		*named_axes;
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
	OOPlotType		 plot_type_default;
	SheetObjectAnchor	 anchor;	/* anchor to draw the frame (images or graphs) */
	double                   frame_offset[4]; /* offset as given in the file */
	double                   width;   /* This refers to the ODF frame element */
	double                   height;  /* This refers to the ODF frame element */
	gint                     z_index;

	/* Plot Area */
	double                   plot_area_x;
	double                   plot_area_y;
	double                   plot_area_width;
	double                   plot_area_height;

	/* Legend */
	double                   legend_x;
	double                   legend_y;
	GogObjectPosition        legend_flag;

	/* Custom Shape */
	char                    *cs_type;
	char                    *cs_enhanced_path;
	char                    *cs_modifiers;
	char                    *cs_viewbox;
	GHashTable              *cs_variables;
} OOChartInfo;

typedef enum {
	OO_PAGE_BREAK_NONE,
	OO_PAGE_BREAK_AUTO,
	OO_PAGE_BREAK_MANUAL
} OOPageBreakType;
typedef struct {
	double	 size_pts;
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
	gboolean display_formulas;
	gboolean hide_col_header;
	gboolean hide_row_header;
	char *master_page_name;
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
	SheetObject     *so;
	double           frame_offset[4];
	gboolean         absolute_distance;
	gint             z_index;
	gchar           *control;
} object_offset_t;

typedef struct _OOParseState OOParseState;

typedef struct {
	gboolean         permanent;
	gboolean         p_seen;
	guint            offset;
	GSList          *span_style_stack;
	GSList          *span_style_list;
	gboolean	 content_is_simple;
	GString         *gstr;
	PangoAttrList   *attrs;
} oo_text_p_t;

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
	ValidationStyle style;
	gchar *title;
	gchar *help_title;
	GString *message;
	GString *help_message;
} odf_validation_t;

struct  _OOParseState {
	GOIOContext	*context;	/* The IOcontext managing things */
	WorkbookView	*wb_view;	/* View for the new workbook */
	OOVer		 ver;		/* Is it an OOo v1.0 or v2.0? */
	double		 ver_odf;	/* specific ODF version */
	GsfInfile	*zip;		/* Reference to the open file, to load graphs and images*/
	OOChartInfo	 chart;
	GSList          *chart_list; /* object_offset_t */
	GnmParsePos	 pos;
	int              table_n;
	GnmCellPos	 extent_data;
	GnmComment      *cell_comment;
	GnmCell         *curr_cell;
	GnmExprSharer   *sharer;
	gboolean         preparse;

	int		 col_inc, row_inc;
	gboolean	 content_is_error;
	OOValueType      value_type;

	GSList          *text_p_stack;
	oo_text_p_t      text_p_for_cell;

	GHashTable	*formats;
	GHashTable	*controls;
	GHashTable	*validations;
	GHashTable	*strings;

	odf_validation_t *cur_validation;

	struct {
		GHashTable	*cell;
		GHashTable	*cell_datetime;
		GHashTable	*cell_date;
		GHashTable	*cell_time;
		GHashTable	*col;
		GHashTable	*row;
		GHashTable	*sheet;
		GHashTable	*master_pages;
		GHashTable	*page_layouts;
		GHashTable	*text;
	} styles;
	struct {
		OOCellStyle	*cells;
		PangoAttrList   *text;
		OOColRowStyle	*col_rows;
		OOSheetStyle	*sheets;
		gboolean         requires_disposal;
		OOStyleType      type;
	} cur_style;

	gint     	 h_align_is_valid; /* 0: not set; 1: fix; 2: value-type*/
	gboolean	 repeat_content;
	int              text_align, gnm_halign;

	struct {
		OOCellStyle	*cells;
		OOColRowStyle	*rows;
		OOColRowStyle	*columns;
		OOChartStyle    *graphics;
	} default_style;
	GSList		*sheet_order;
	int		 richtext_len;
	struct {
		GString	*accum;
		guint    offset;
		gboolean string_opened;
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
	GHashTable *openformula_namemap;
	GHashTable *openformula_handlermap;

	struct {
		struct {
			GnmPageBreaks *h, *v;
		} page_breaks;

		GnmPrintInformation *cur_pi;
		GnmPrintHF          *cur_hf;
		char            **cur_hf_format;
		int               rep_rows_from;
		int               rep_rows_to;
		int               rep_cols_from;
		int               rep_cols_to;
	} print;

	char *object_name; /* also used for table during preparsing */
	OOControl *cur_control;

	OOSettings settings;

	gsf_off_t last_progress_update;
	char *last_error;
	gboolean  hd_ft_left_warned;
	gboolean  debug;
};



typedef struct {
	int start;
	int end;
	char *style_name;
} span_style_info_t;

typedef struct {
	GnmConventions base;
	OOParseState *state;
	GsfXMLIn *xin;
} ODFConventions;

typedef struct {
	GOColor from;
	GOColor to;
	double brightness;
	unsigned int dir;
} gradient_info_t;

typedef struct {
	Sheet *sheet;
	int cols;
	int rows;
} sheet_order_t;

typedef struct {
	char const * const name;
	int val;
} OOEnum;

static OOEnum const odf_chart_classes[] = {
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

static OOEnum const odf_value_types[] = {
	{ "float",		OO_VALUE_TYPE_FLOAT },
	{ "percentage",		OO_VALUE_TYPE_PERCENTAGE },
	{ "currency",   	OO_VALUE_TYPE_CURRENCY },
	{ "date",		OO_VALUE_TYPE_DATE },
	{ "time",       	OO_VALUE_TYPE_TIME },
	{ "boolean",	        OO_VALUE_TYPE_BOOLEAN },
	{ "string",		OO_VALUE_TYPE_STRING },
	{ "void",		OO_VALUE_TYPE_VOID },
	{ NULL,	0 },
};


/* Some  prototypes */
static GsfXMLInNode const * get_dtd (void);
static GsfXMLInNode const * get_styles_dtd (void);
static void oo_chart_style_free (OOChartStyle *pointer);
static OOFormula odf_get_formula_type (GsfXMLIn *xin, char const **str);
static char const *odf_strunescape (char const *string, GString *target,
				    G_GNUC_UNUSED GnmConventions const *convs);
static void odf_sheet_suggest_size (GsfXMLIn *xin, int *cols, int *rows);
static void oo_prop_list_has (GSList *props, gboolean *threed, char const *tag);
static void odf_so_set_props (OOParseState *state, OOChartStyle *oostyle);


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
	gnm_float tmp;

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
oo_attr_double (GsfXMLIn *xin, xmlChar const * const *attrs,
		int ns_id, char const *name, double *res)
{
	char *end;
	double tmp;

	g_return_val_if_fail (attrs != NULL, FALSE);
	g_return_val_if_fail (attrs[0] != NULL, FALSE);
	g_return_val_if_fail (attrs[1] != NULL, FALSE);

	if (!gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), ns_id, name))
		return FALSE;

	tmp = go_strtod (CXML2C (attrs[1]), &end);
	if (*end)
		return oo_warning (xin, _("Invalid attribute '%s', expected number, received '%s'"),
				   name, attrs[1]);
	*res = tmp;
	return TRUE;
}

static gboolean
oo_attr_percent (GsfXMLIn *xin, xmlChar const * const *attrs,
		 int ns_id, char const *name, double *res)
{
	char *end;
	double tmp;
	const char *val = CXML2C (attrs[1]);

	g_return_val_if_fail (attrs != NULL, FALSE);
	g_return_val_if_fail (attrs[0] != NULL, FALSE);
	g_return_val_if_fail (attrs[1] != NULL, FALSE);

	if (!gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), ns_id, name))
		return FALSE;

	tmp = go_strtod (val, &end);
	if (end == val || *end != '%' || *(end + 1))
		return oo_warning (xin,
				   _("Invalid attribute '%s', expected percentage,"
				     " received '%s'"),
				   name, val);
	*res = tmp / 100;
	return TRUE;
}

static GnmColor *magic_transparent;

static GnmColor *
oo_parse_color (GsfXMLIn *xin, xmlChar const *str, char const *name)
{
	guint r, g, b;

	g_return_val_if_fail (str != NULL, NULL);

	if (3 == sscanf (CXML2C (str), "#%2x%2x%2x", &r, &g, &b))
		return gnm_color_new_rgb8 (r, g, b);

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

static char const *
odf_string_id (OOParseState *state, char const *string)
{
	char *id = g_strdup_printf ("str%i", g_hash_table_size (state->strings));
	g_hash_table_insert (state->strings, id, g_strdup (string));
	return id;
}

static void
odf_apply_style_props (GsfXMLIn *xin, GSList *props, GOStyle *style, gboolean in_chart_context)
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
	double symbol_height = -1., symbol_width = -1.,
		stroke_width = -1., gnm_stroke_width = -1.;
	GOMarker *m;
	gboolean line_is_not_dash = FALSE;
	unsigned int fill_type = OO_FILL_TYPE_UNKNOWN;
	gboolean stroke_colour_set = FALSE;
	gboolean lines_value = !in_chart_context;
	gboolean gnm_auto_color_value_set = FALSE;
	gboolean gnm_auto_color_value = FALSE;
	gboolean gnm_auto_marker_outline_color_value_set = FALSE;
	gboolean gnm_auto_marker_outline_color_value = FALSE;
	gboolean gnm_auto_marker_fill_color_value_set = FALSE;
	gboolean gnm_auto_marker_fill_color_value = FALSE;
	gboolean gnm_auto_dash_set = FALSE;
	gboolean gnm_auto_width_set = FALSE;
	char const *stroke_dash = NULL;
	char const *marker_outline_colour = NULL;
	double marker_outline_colour_opacity = 1;
	char const *marker_fill_colour = NULL;
	double marker_fill_colour_opacity = 1;
	char const *stroke_color = NULL;
	double stroke_color_opacity = 1;
	gboolean gnm_auto_font_set = FALSE;
	gboolean gnm_auto_font = FALSE;
	gboolean gnm_foreground_solid = FALSE;

	oo_prop_list_has (props, &gnm_foreground_solid, "gnm-foreground-solid");
	style->line.auto_dash = TRUE;

	desc = pango_font_description_copy (style->font.font->desc);
	for (l = props; l != NULL; l = l->next) {
		OOProp *prop = l->data;
		if (0 == strcmp (prop->name, "fill")) {
			char const *val_string = g_value_get_string (&prop->value);
			if (0 == strcmp (val_string, "solid")) {
				style->fill.type = GO_STYLE_FILL_PATTERN;
				style->fill.auto_type = FALSE;
				style->fill.pattern.pattern = (gnm_foreground_solid) ?
					GO_PATTERN_FOREGROUND_SOLID : GO_PATTERN_SOLID;
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
			GdkRGBA rgba;
			gchar const *color = g_value_get_string (&prop->value);
			if (gdk_rgba_parse (&rgba, color)) {
				guint a;
				if (gnm_foreground_solid) {
					a = GO_COLOR_UINT_A (style->fill.pattern.fore);
					go_color_from_gdk_rgba (&rgba, &style->fill.pattern.fore);
					style->fill.auto_fore = FALSE;
					style->fill.pattern.fore = GO_COLOR_CHANGE_A (style->fill.pattern.fore, a);
				} else {
					a = GO_COLOR_UINT_A (style->fill.pattern.back);
					go_color_from_gdk_rgba (&rgba, &style->fill.pattern.back);
					style->fill.auto_back = FALSE;
					style->fill.pattern.back = GO_COLOR_CHANGE_A (style->fill.pattern.back, a);
				}
			}
		}else if (0 == strcmp (prop->name, "opacity")) {
			guint a = 255 * g_value_get_double (&prop->value);
			style->fill.pattern.back = GO_COLOR_CHANGE_A (style->fill.pattern.back, a);
		} else if (0 == strcmp (prop->name, "stroke-color")) {
			stroke_color = g_value_get_string (&prop->value);
		}else if (0 == strcmp (prop->name, "stroke-color-opacity")) {
			stroke_color_opacity = g_value_get_double (&prop->value);
		} else if (0 == strcmp (prop->name, "marker-outline-colour")) {
			marker_outline_colour = g_value_get_string (&prop->value);
		} else if (0 == strcmp (prop->name, "marker-outline-colour-opacity")) {
			marker_outline_colour_opacity = g_value_get_double (&prop->value);
		} else if (0 == strcmp (prop->name, "marker-fill-colour")) {
			marker_fill_colour = g_value_get_string (&prop->value);
		} else if (0 == strcmp (prop->name, "marker-fill-colour-opacity")) {
			marker_fill_colour_opacity = g_value_get_double (&prop->value);
		} else if (0 == strcmp (prop->name, "lines")) {
			lines_value = g_value_get_boolean (&prop->value);
		} else if (0 == strcmp (prop->name, "gnm-auto-color")) {
			gnm_auto_color_value_set = TRUE;
			gnm_auto_color_value = g_value_get_boolean (&prop->value);
		} else if (0 == strcmp (prop->name, "gnm-auto-marker-outline-colour")) {
			gnm_auto_marker_outline_color_value_set = TRUE;
			gnm_auto_marker_outline_color_value = g_value_get_boolean (&prop->value);
		} else if (0 == strcmp (prop->name, "gnm-auto-marker-fill-colour")) {
			gnm_auto_marker_fill_color_value_set = TRUE;
			gnm_auto_marker_fill_color_value = g_value_get_boolean (&prop->value);
		} else if (0 == strcmp (prop->name, "color")) {
			GdkRGBA rgba;
			gchar const *color = g_value_get_string (&prop->value);
			if (gdk_rgba_parse (&rgba, color)) {
				go_color_from_gdk_rgba (&rgba, &style->font.color);
				style->font.auto_color = FALSE;
			}
		} else if (0 == strcmp (prop->name, "gnm-auto-dash")) {
			gnm_auto_dash_set = TRUE;
			style->line.auto_dash = g_value_get_boolean (&prop->value);
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
			if (angle > 180)
				angle -= 360;
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
				if (!gnm_auto_dash_set)
					style->line.auto_dash = FALSE;
				line_is_not_dash = TRUE;
			} else if (0 == strcmp (g_value_get_string (&prop->value), "dash")) {
				if (!gnm_auto_dash_set)
					style->line.auto_dash = FALSE;
				line_is_not_dash = FALSE;
			} else {
				style->line.dash_type = GO_LINE_NONE;
				if (!gnm_auto_dash_set)
					style->line.auto_dash = FALSE;
				line_is_not_dash = TRUE;
			}
		} else if (0 == strcmp (prop->name, "stroke-dash"))
			stroke_dash = g_value_get_string (&prop->value);
		else if (0 == strcmp (prop->name, "symbol-type"))
			symbol_type = g_value_get_int (&prop->value);
		else if (0 == strcmp (prop->name, "symbol-name"))
			symbol_name = g_value_get_int (&prop->value);
		else if (0 == strcmp (prop->name, "symbol-height"))
			symbol_height = g_value_get_double (&prop->value);
		else if (0 == strcmp (prop->name, "symbol-width"))
			symbol_width = g_value_get_double (&prop->value);
		else if (0 == strcmp (prop->name, "stroke-width"))
		        stroke_width = g_value_get_double (&prop->value);
		else if (0 == strcmp (prop->name, "gnm-stroke-width"))
		        gnm_stroke_width = g_value_get_double (&prop->value);
		else if (0 == strcmp (prop->name, "gnm-auto-width")) {
			gnm_auto_width_set = TRUE;
		        style->line.auto_width = g_value_get_boolean (&prop->value);
		} else if (0 == strcmp (prop->name, "repeat"))
			style->fill.image.type = g_value_get_int (&prop->value);
		else if (0 == strcmp (prop->name, "gnm-auto-type"))
			style->fill.auto_type = g_value_get_boolean (&prop->value);
		else if (0 == strcmp (prop->name, "gnm-auto-font")) {
			gnm_auto_font_set = TRUE;
			gnm_auto_font = g_value_get_boolean (&prop->value);
		}
	}

	if (desc_changed)
		go_style_set_font_desc	(style, desc);
	else
		pango_font_description_free (desc);
	style->font.auto_font = gnm_auto_font_set ? gnm_auto_font : !desc_changed;

	/*
	 * Stroke colour is tricky: if we have lines, that is what it
	 * refers to.  Otherwise it refers to markers.
	 */
	if (stroke_color) {
		GdkRGBA rgba;
		if (gdk_rgba_parse (&rgba, stroke_color)) {
			rgba.alpha = stroke_color_opacity;
			style->line.fore = go_color_from_gdk_rgba (&rgba, &style->line.color);
			style->line.auto_color = FALSE;
			style->line.auto_fore = FALSE;
			style->line.pattern = GO_PATTERN_SOLID;
			stroke_colour_set = TRUE;
		}
	}
	if (!gnm_auto_color_value_set)
		gnm_auto_color_value = !stroke_colour_set;

	style->line.auto_color = (lines_value ? gnm_auto_color_value : TRUE);

	if (gnm_stroke_width >= 0)
		style->line.width = gnm_stroke_width;
	else if (stroke_width == 0.) {
		style->line.width = 0.;
		style->line.dash_type = GO_LINE_NONE;
	} else if (stroke_width > 0)
		style->line.width = stroke_width;
	else
		style->line.width = 0;
	if (!gnm_auto_width_set)
		style->line.auto_width = FALSE;

	if (stroke_dash != NULL && !line_is_not_dash)
		style->line.dash_type = odf_match_dash_type (state, stroke_dash);

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
				style->fill.auto_fore = gnm_auto_color_value_set && gnm_auto_color_value;
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
					guint8 const *data = gsf_input_read (input, len, NULL);
					GOImage *image = go_image_new_from_data (NULL, data, len, NULL, NULL);
					if (image) {
						g_clear_object (&style->fill.image.image);
						style->fill.image.image = image;
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
		m = go_marker_new ();
		style->marker.auto_shape = TRUE;
		break;
	case OO_SYMBOL_TYPE_NONE:
		style->marker.auto_shape = FALSE;
		m = go_marker_new ();
		go_marker_set_shape (m, GO_MARKER_NONE);
		break;
	case OO_SYMBOL_TYPE_NAMED:
		style->marker.auto_shape = FALSE;
		m = go_marker_new ();
		go_marker_set_shape (m, symbol_name);
		break;
	default:
		m = NULL;
		break;
	}
	if (m) {
		gboolean dshm;

		if (symbol_type != OO_SYMBOL_TYPE_NONE) {
			/* Inherit line colour.  */
			go_marker_set_fill_color (m, style->line.color);
			if (marker_fill_colour != NULL) {
				GOColor color;
				GdkRGBA rgba;
				if (gdk_rgba_parse (&rgba, marker_fill_colour)) {
					rgba.alpha = marker_fill_colour_opacity;
					go_color_from_gdk_rgba (&rgba, &color);
					go_marker_set_fill_color (m, color);
				}
			}
			go_marker_set_fill_color (m, style->line.color);
			style->marker.auto_fill_color = gnm_auto_marker_fill_color_value_set ?
				gnm_auto_marker_fill_color_value : gnm_auto_color_value;
			if (marker_outline_colour == NULL)
				go_marker_set_outline_color (m, style->line.color);
			else {
				GOColor color;
				GdkRGBA rgba;
				if (gdk_rgba_parse (&rgba, marker_outline_colour)) {
					rgba.alpha = marker_outline_colour_opacity;
					go_color_from_gdk_rgba (&rgba, &color);
					go_marker_set_outline_color (m, color);
				} else
					go_marker_set_outline_color (m, style->line.color);
			}
			style->marker.auto_outline_color = gnm_auto_marker_outline_color_value_set ?
				gnm_auto_marker_outline_color_value : gnm_auto_color_value;
		}

		if (symbol_height >= 0. || symbol_width >= 0.) {
			double size;
			/* If we have only one dimension, use that for the other */
			if (symbol_width < 0) symbol_width = symbol_height;
			if (symbol_height < 0) symbol_height = symbol_width;

			size = (symbol_height + symbol_width + 1) / 2;
			size = MIN (size, G_MAXINT);

			go_marker_set_size (m, (int)size);
		}

		if (gnm_object_has_readable_prop (state->chart.plot,
						  "default-style-has-markers",
						  G_TYPE_BOOLEAN,
						  &dshm) &&
		    !dshm &&
		    go_marker_get_shape (m) == GO_MARKER_NONE) {
			style->marker.auto_shape = TRUE;
		}

		go_style_set_marker (style, m);
	}
}

/* returns pts */
static char const *
oo_parse_spec_distance (char const *str, double *pts)
{
	double num;
	char *end = NULL;

	num = go_strtod (str, &end);
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
		} else
			return GINT_TO_POINTER(1);
	} else return NULL;

	*pts = num;
	return end;

}

/* returns pts */
static char const *
oo_parse_distance (GsfXMLIn *xin, xmlChar const *str,
		  char const *name, double *pts)
{
	char const *end = NULL;

	g_return_val_if_fail (str != NULL, NULL);

	if (0 == strncmp (CXML2C (str), "none", 4)) {
		*pts = 0;
		return CXML2C (str) + 4;
	}

	end = oo_parse_spec_distance (CXML2C (str), pts);

	if (end == GINT_TO_POINTER(1)) {
		oo_warning (xin, _("Invalid attribute '%s', unknown unit '%s'"),
			    name, str);
		return NULL;
	}
	if (end == NULL) {
		oo_warning (xin, _("Invalid attribute '%s', expected distance, received '%s'"),
			    name, str);
		return NULL;
	}
	return end;
}

/* returns pts */
static char const *
oo_attr_distance (GsfXMLIn *xin, xmlChar const * const *attrs,
		  int ns_id, char const *name, double *pts)
{
	g_return_val_if_fail (attrs != NULL, NULL);
	g_return_val_if_fail (attrs[0] != NULL, NULL);
	g_return_val_if_fail (attrs[1] != NULL, NULL);

	if (!gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), ns_id, name))
		return NULL;
	return oo_parse_distance (xin, attrs[1], name, pts);
}

static gboolean
oo_attr_percent_or_distance (GsfXMLIn *xin, xmlChar const * const *attrs,
			     int ns_id, char const *name, double *res,
			     gboolean *found_percent)
{
	char *end;
	double tmp;

	g_return_val_if_fail (attrs != NULL, FALSE);
	g_return_val_if_fail (attrs[0] != NULL, FALSE);
	g_return_val_if_fail (attrs[1] != NULL, FALSE);
	g_return_val_if_fail (res != NULL, FALSE);
	g_return_val_if_fail (found_percent != NULL, FALSE);

	if (!gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), ns_id, name))
		return FALSE;

	tmp = gnm_strto (CXML2C (attrs[1]), &end);
	if (*end != '%' || *(end + 1)) {
		*found_percent = FALSE;
		return (NULL != oo_parse_distance (xin, attrs[1], name, res));
	}
	*res = tmp / 100;
	*found_percent = TRUE;
	return TRUE;
}

static char const *
oo_parse_angle (GsfXMLIn *xin, xmlChar const *str,
		char const *name, int *angle)
{
	double num;
	char *end = NULL;

	g_return_val_if_fail (str != NULL, NULL);

	num = gnm_strto (CXML2C (str), &end);
	if (CXML2C (str) != end) {
		if (*end == '\0') {
			num = gnm_fmod (num, 360);
		} else if (0 == strncmp (end, "deg", 3)) {
			num = gnm_fmod (num, 360);
			end += 3;
		} else if (0 == strncmp (end, "grad", 4)) {
			num = gnm_fmod (num, 400);
			num = num * 10. / 9.;
			end += 4;
		} else if (0 == strncmp (end, "rad", 3)) {
			num = fmod (num, 2 * M_PI);
			num = num * 180 / M_PI;
			end += 3;
		} else {
			oo_warning (xin, _("Invalid attribute '%s', unknown unit '%s'"),
				    name, str);
			return NULL;
		}
	} else {
		oo_warning (xin, _("Invalid attribute '%s', expected angle, received '%s'"),
			    name, str);
		return NULL;
	}

	num = gnm_fake_round (num);
	if (gnm_abs (num) >= 360)
		num = 0;

	*angle = (int)num;

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

static gboolean
odf_attr_range (GsfXMLIn *xin, xmlChar const * const *attrs, Sheet *sheet, GnmRange *res)
{
	int flags = 0;

	g_return_val_if_fail (attrs != NULL, FALSE);

	for (; attrs[0] && attrs[1] ; attrs += 2)
		if (oo_attr_int_range (xin, attrs, OO_GNUM_NS_EXT, "start-col", &res->start.col, 0, gnm_sheet_get_last_col(sheet)))
			flags |= 0x1;
		else if (oo_attr_int_range (xin, attrs, OO_GNUM_NS_EXT, "start-row", &res->start.row, 0, gnm_sheet_get_last_row(sheet)))
			flags |= 0x2;
		else if (oo_attr_int_range (xin, attrs, OO_GNUM_NS_EXT, "end-col", &res->end.col, 0, gnm_sheet_get_last_col(sheet)))
			flags |= 0x4;
		else if (oo_attr_int_range (xin, attrs, OO_GNUM_NS_EXT, "end-row", &res->end.row, 0, gnm_sheet_get_last_row(sheet)))
			flags |= 0x8;
		else
			return FALSE;

	return flags == 0xf;
}

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

// range_part:
// 0: not from range
// 1: start
// 2: end

static char const *
oo_cellref_parse (GnmCellRef *ref, char const *start, GnmParsePos const *pp,
		  gchar **foreign_sheet,
		  int range_part)
{
	char const *tmp, *ptr = start;
	GnmSheetSize const *ss;
	GnmSheetSize ss_max = { GNM_MAX_COLS, GNM_MAX_ROWS};
	Sheet *sheet;
	gboolean have_col, have_row;

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
			/* We have seen instances of ODF files generated by   */
			/* Libreoffice referring internally to table included */
			/* inside charts. See                                 */
			/* https://bugzilla.gnome.org/show_bug.cgi?id=698388  */
			/* Since all true sheets have been created during     */
			/* preparsing, this reference should just be invalid! */

			ref->sheet = workbook_sheet_by_name (pp->wb, name);
			if (ref->sheet == NULL)
					ref->sheet = invalid_sheet;
		}
	} else {
		ptr++; /* local ref */
		ref->sheet = NULL;
	}

	sheet = ref->sheet == invalid_sheet
		? eval_sheet (ref->sheet, pp->sheet)
		: pp->sheet;
	ss = gnm_sheet_get_size2 (sheet, pp->wb);

	tmp = col_parse (ptr, &ss_max, &ref->col, &ref->col_relative);
	have_col = tmp != NULL;
	if (!tmp && !oo_cellref_check_for_err (ref, &ptr) && range_part == 0)
		return start;
	if (tmp)
		ptr = tmp;
	else {
		ref->col = (range_part == 2 ? ss->max_cols - 1 : 0);
	}

	tmp = row_parse (ptr, &ss_max, &ref->row, &ref->row_relative);
	have_row = tmp != NULL;
	if (!tmp && !oo_cellref_check_for_err (ref, &ptr) && range_part == 0)
		return start;
	if (tmp)
		ptr = tmp;
	else
		ref->row = (range_part == 2 ? ss->max_rows - 1 : 0);

	if (ref->sheet == invalid_sheet)
		return ptr;
	if (!have_col && !have_row)
		return start;

	if (foreign_sheet == NULL && (ss->max_cols <= ref->col || ss->max_rows <= ref->row)) {
		int new_cols = ref->col + 1, new_rows = ref->row + 1;
		GOUndo   * goundo;
		gboolean err;

		odf_sheet_suggest_size (NULL, &new_cols, &new_rows);
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
	char const *ptr2;
	char *external = NULL;
	char *external_sheet_1 = NULL;
	char *external_sheet_2 = NULL;
	ODFConventions *oconv = (ODFConventions *)convs;

	ptr = odf_parse_external (start, &external, convs);

	ptr2 = oo_cellref_parse (&ref->a, ptr, pp,
				 external ? &external_sheet_1 : NULL,
				 1);
	if (ptr == ptr2)
		return start;
	ptr = ptr2;

	if (*ptr == ':') {
		ptr2 = oo_cellref_parse (&ref->b, ptr+1, pp,
					 external ? &external_sheet_2 : NULL,
					 2);
		if (ptr2 == ptr + 1)
			ref->b = ref->a;
		else
			ptr = ptr2;
	} else
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
	if (start[0] == '[' && start[1] != ']') {
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

/* Handle formatted text inside text:p */
static void
odf_text_p_add_text (OOParseState *state, char const *str)
{
	oo_text_p_t *ptr;

	g_return_if_fail (state->text_p_stack != NULL);
	ptr = state->text_p_stack->data;

	if (ptr->gstr) {
		g_string_append (ptr->gstr, str);
	} else
		ptr->gstr = g_string_new (str);
}

typedef struct {
	guint start;
	guint end;
	PangoAttrList *attrs;
} odf_text_p_apply_style_t;

static gboolean
odf_text_p_apply_pango_attribute (PangoAttribute *attribute, gpointer ptr)
{
	odf_text_p_apply_style_t *data = ptr;
	PangoAttribute *attr = pango_attribute_copy (attribute);

	attr->start_index = data->start;
	attr->end_index = data->end;

	pango_attr_list_change (data->attrs, attr);

	return FALSE;
}

static void
odf_text_p_apply_style (OOParseState *state,
			PangoAttrList *attrs,
			int start, int end)
{
	oo_text_p_t *ptr;
	odf_text_p_apply_style_t data;

	if (attrs == NULL)
		return;

	g_return_if_fail (state->text_p_stack != NULL);
	ptr = state->text_p_stack->data;

	if (ptr->attrs == NULL)
		ptr->attrs = pango_attr_list_new ();

	data.start = start;
	data.end = end;
	data.attrs = ptr->attrs;

	pango_attr_list_filter (attrs, odf_text_p_apply_pango_attribute, &data);
}

static void
odf_push_text_p (OOParseState *state, gboolean permanent)
{
	oo_text_p_t *ptr;

	if (permanent) {
		ptr = &(state->text_p_for_cell);
		if (ptr->gstr)
			g_string_truncate (ptr->gstr, 0);
		if (ptr->attrs) {
			pango_attr_list_unref (ptr->attrs);
			ptr->attrs = NULL;
		}
	} else {
		ptr = g_new0 (oo_text_p_t, 1);
		ptr->permanent = FALSE;
		ptr->content_is_simple = TRUE;
	}
	ptr->p_seen = FALSE;
	ptr->offset = 0;
	ptr->span_style_stack = NULL;
	ptr->span_style_list = NULL;
	state->text_p_stack = g_slist_prepend (state->text_p_stack, ptr);
}

static void
odf_pop_text_p (OOParseState *state)
{
	oo_text_p_t *ptr;
	GSList *link = state->text_p_stack;

	g_return_if_fail (state->text_p_stack != NULL);

	ptr = link->data;
	g_slist_free (ptr->span_style_stack);
	/* ptr->span_style_list should be NULL. If it isn't something went wrong and we are leaking here! */
	g_slist_free_full (ptr->span_style_list, g_free);
	ptr->span_style_stack = NULL;
	ptr->span_style_list = NULL;
	if (!ptr->permanent) {
		if (ptr->gstr)
			g_string_free (ptr->gstr, TRUE);
		if (ptr->attrs)
			pango_attr_list_unref (ptr->attrs);
		g_free (ptr);
	}

	state->text_p_stack = g_slist_remove_link (state->text_p_stack, link);
	g_slist_free_1 (link);
}

static void
odf_text_content_start (GsfXMLIn *xin, G_GNUC_UNUSED xmlChar const **attrs)
{
       OOParseState *state = (OOParseState *)xin->user_state;
       oo_text_p_t *ptr = state->text_p_stack->data;

       if (ptr->p_seen)
	       odf_text_p_add_text (state, "\n");
       else
	       ptr->p_seen = TRUE;
}

static void
odf_text_content_end (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	OOParseState *state = (OOParseState *)xin->user_state;
	oo_text_p_t *ptr;
	GSList *list = NULL, *l;

	g_return_if_fail ( state->text_p_stack != NULL);
	ptr = state->text_p_stack->data;

	g_return_if_fail (ptr != NULL);
	g_return_if_fail (xin->content != NULL);

	if (strlen (xin->content->str) > ptr->offset)
		odf_text_p_add_text
			(state, xin->content->str + ptr->offset);
	ptr->offset = 0;
	l = list = g_slist_reverse(ptr->span_style_list);
	while (l != NULL) {
		span_style_info_t *ssi = l->data;
		if (ssi != NULL) {
			int start = ssi->start;
			int end = ssi->end;
			char *style_name = ssi->style_name;
			if (style_name != NULL && end > 0 && end > start) {
				PangoAttrList *attrs = g_hash_table_lookup (state->styles.text, style_name);
				if (attrs == NULL)
					oo_warning (xin, _("Unknown text style with name \"%s\" encountered!"), style_name);
				else
					odf_text_p_apply_style (state, attrs, start, end);
			}
			g_free (style_name);
			g_free (ssi);
		}
		l = l->next;
	}
	g_slist_free (list);
	ptr->span_style_list = NULL;
}

static void
odf_text_span_start (GsfXMLIn *xin, xmlChar const **attrs)
{
       OOParseState *state = (OOParseState *)xin->user_state;
       oo_text_p_t *ptr = state->text_p_stack->data;

	if (ptr->content_is_simple) {
		span_style_info_t *ssi = g_new0 (span_style_info_t, 1);

		if (xin->content->str != NULL && *xin->content->str != 0) {
			odf_text_p_add_text (state, xin->content->str + ptr->offset);
			ptr->offset = strlen (xin->content->str);
		}

		ssi->start = ((ptr->gstr) ? ptr->gstr->len : 0);

		for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
			if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_TEXT, "style-name"))
				ssi->style_name = g_strdup (attrs[1]);

		ptr->span_style_stack = g_slist_prepend (ptr->span_style_stack, ssi);
		ptr->span_style_list = g_slist_prepend (ptr->span_style_list, ssi);
	}
}

static void
odf_text_span_end (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	OOParseState *state = (OOParseState *)xin->user_state;
	oo_text_p_t *ptr = state->text_p_stack->data;

	if (ptr->content_is_simple) {
		int end;
		span_style_info_t *ssi = NULL;

		g_return_if_fail (ptr->span_style_stack != NULL);

		if (xin->content->str != NULL && *xin->content->str != 0) {
			odf_text_p_add_text (state, xin->content->str + ptr->offset);
			ptr->offset = strlen (xin->content->str);
		}

		end = ((ptr->gstr) ? ptr->gstr->len : 0);

		ssi = ptr->span_style_stack->data;
		ptr->span_style_stack = g_slist_delete_link (ptr->span_style_stack,
							     ptr->span_style_stack);
		if (ssi != NULL)
			ssi->end = end;
	}
}

static void
odf_text_special (GsfXMLIn *xin, int count, char const *sym)
{
	OOParseState *state = (OOParseState *)xin->user_state;
	oo_text_p_t  *ptr = state->text_p_stack->data;

	if (ptr->content_is_simple) {
		if (xin->content->str != NULL && *xin->content->str != 0) {
			odf_text_p_add_text (state, xin->content->str + ptr->offset);
			ptr->offset = strlen (xin->content->str);
		}

		if (count == 1)
			odf_text_p_add_text (state, sym);
		else if (count > 0) {
			gchar *space = g_strnfill (count, *sym);
			odf_text_p_add_text (state, space);
			g_free (space);
		}
	}
}

static void
odf_text_space (GsfXMLIn *xin, xmlChar const **attrs)
{
	int count = 1;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (oo_attr_int_range (xin, attrs, OO_NS_TEXT, "c", &count, 0, INT_MAX))
		       ;
	odf_text_special (xin, count, " ");
}

static void
odf_text_symbol (GsfXMLIn *xin, G_GNUC_UNUSED xmlChar const **attrs)
{
	odf_text_special (xin, 1, xin->node->user_data.v_str);
}

/* End of handle formatted text inside text:p */


static void
oo_sheet_style_free (OOSheetStyle *style)
{
	if (style) {
		g_free (style->master_page_name);
		g_free (style);
	}
}

typedef struct {
	GHashTable *orig2fixed;
	GHashTable *fixed2orig;
	OOParseState *state;
	GnmNamedExpr *nexpr;
	char const *nexpr_name;
} odf_fix_expr_names_t;

static odf_fix_expr_names_t *
odf_fix_expr_names_t_new (OOParseState *state)
{
	odf_fix_expr_names_t *fen = g_new (odf_fix_expr_names_t, 1);

	fen->fixed2orig = g_hash_table_new (g_str_hash, g_str_equal);
	fen->orig2fixed = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
	fen->state = state;
	fen->nexpr = NULL;
	fen->nexpr_name = NULL;

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
odf_fix_en_collect (G_GNUC_UNUSED gconstpointer key_,
		    GnmNamedExpr *nexpr, odf_fix_expr_names_t *fen)
{
	GString *str;
	gchar *here;
	const char *name = expr_name_name (nexpr);

	if (expr_name_validate (name))
		return;
	if (NULL != g_hash_table_lookup (fen->orig2fixed, name))
		return;
	str = g_string_new (name);

	for (here = str->str; *here; here = g_utf8_next_char (here)) {
		if (!g_unichar_isalnum (g_utf8_get_char (here)) &&
		    here[0] != '_') {
			int i, limit = g_utf8_next_char (here) - here;
			for (i = 0; i<limit;i++)
				here[i] = '_';
		}
	}

	// If the name is inherently invalid ("19" as in #557) then mangle
	// it first.
	if (!expr_name_validate (str->str)) {
		g_string_insert (str, 0, "NAME");
		if (!expr_name_validate (str->str)) {
			char *p;
			for (p = str->str; *p; p++)
				if (!g_ascii_isalnum (*p))
					*p = 'X';
		}
	}

	while (!odf_fix_en_validate (str->str, fen))
		g_string_append_c (str, '_');

	odf_fix_expr_names_t_add (fen, name, g_string_free (str, FALSE));
}

static void
odf_fix_en_find (G_GNUC_UNUSED gconstpointer key,
		 GnmNamedExpr *nexpr, odf_fix_expr_names_t *fen)
{
	if (strcmp (expr_name_name (nexpr), fen->nexpr_name) == 0)
		fen->nexpr = nexpr;
}

static void
odf_fix_en_apply (const char *orig, const char *fixed, odf_fix_expr_names_t *fen)
{
	int i = 0;

	g_return_if_fail (orig != NULL);
	g_return_if_fail (fixed != NULL);
	g_return_if_fail (fen != NULL);

	fen->nexpr_name = orig;

	while (i++ < 1000) {
		fen->nexpr = NULL;
		workbook_foreach_name (fen->state->pos.wb, FALSE,
				       (GHFunc)odf_fix_en_find, fen);

		if (fen->nexpr == NULL)
			return;

		expr_name_set_name (fen->nexpr, fixed);
	}
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
	g_hash_table_foreach (fen->orig2fixed, (GHFunc)odf_fix_en_apply, fen);

	odf_fix_expr_names_t_free (fen);
}

/**
 * odf_expr_name_validate:
 * @name: tentative name
 *
 * Returns: %TRUE if the given name is valid, %FALSE otherwise.
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

/*
 * This is ugly!  And it's ods' fault.
 *
 * On one hand we have function names like ORG.GNUMERIC.LOG2, on the
 * other we have inter-sheet name references Sheet2.localname
 * The former is a name, the latter is a sheet name, a sheetsep, and
 * a name.
 *
 * To resolve that, we look ahead for a '('.
 */
static char const *
odf_name_parser (char const *str, GnmConventions const *convs)
{
	gunichar uc = g_utf8_get_char (str);
	const char *firstdot = NULL;
	int dotcount = 0;

	if (!g_unichar_isalpha (uc) && uc != '_' && uc != '\\')
		return NULL;

	do {
		str = g_utf8_next_char (str);
		uc = g_utf8_get_char (str);

		if (uc == '.') {
			if (dotcount++ == 0)
				firstdot = str;
		}
	} while (g_unichar_isalnum (uc) ||
		 (uc == '_' || uc == '?' || uc == '\\' || uc == '.'));

	if (dotcount == 1 && convs->sheet_name_sep == '.') {
		const char *p = str;

		while (g_unichar_isspace (g_utf8_get_char (p)))
			p = g_utf8_next_char (p);

		if (*p != '(')
			return firstdot;
	}

	return str;
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
	conv->union_char	= '~';
	conv->decimal_sep_dot	= TRUE;
	conv->range_sep_colon	= TRUE;
	conv->arg_sep		= ';';
	conv->array_col_sep	= ';';
	conv->array_row_sep	= '|';
	conv->input.string	= odf_strunescape;
	conv->input.func	= oo_func_map_in;
	conv->input.range_ref	= oo_expr_rangeref_parse;
	conv->input.name        = odf_name_parser;
	conv->input.name_validate = odf_expr_name_validate;
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
	return gnm_expr_parse_str (str, pp, flags | GNM_EXPR_PARSE_UNKNOWN_NAMES_ARE_INVALID,
				    state->convs[type], perr);
}

static GnmExprTop const *
oo_expr_parse_str (GsfXMLIn *xin, char const *str,
		   GnmParsePos const *pp, GnmExprParseFlags flags,
		   OOFormula type)
{
	OOParseState *state = (OOParseState *)xin->user_state;
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

	if (texpr)
		texpr = gnm_expr_sharer_share (state->sharer, texpr);

	return texpr;
}

static GnmExprTop const *
odf_parse_range_address_or_expr (GsfXMLIn *xin, char const *str)
{
	OOParseState *state = (OOParseState *)xin->user_state;
	GnmExprTop const *texpr = NULL;
	OOFormula f_type = odf_get_formula_type (xin, &str);

	if (str != NULL && strlen (str) > 0 && f_type != FORMULA_NOT_SUPPORTED) {
		GnmParsePos pp;
		GnmRangeRef ref;
		char const *ptr;
		gnm_cellref_init (&ref.a, invalid_sheet, 0, 0, TRUE);
		gnm_cellref_init (&ref.b, invalid_sheet, 0, 0, TRUE);
		ptr = oo_rangeref_parse
			(&ref, str,
			 parse_pos_init_sheet (&pp, state->pos.sheet),
			 NULL);
		if (ptr == str
		    || ref.a.sheet == invalid_sheet)
			texpr = oo_expr_parse_str (xin, str,
						   &state->pos,
						   GNM_EXPR_PARSE_DEFAULT,
						   f_type);
		else {
			GnmValue *v = value_new_cellrange (&ref.a, &ref.b, 0, 0);
			texpr = gnm_expr_top_new_constant (v);
		}
	}
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
	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2) {
		if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_TABLE, "status"))
			workbook_iteration_enabled (state->pos.wb,
				strcmp (CXML2C (attrs[1]), "enable") == 0);
		else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_TABLE, "maximum-difference"))
			workbook_iteration_tolerance (state->pos.wb,
						      gnm_strto (CXML2C (attrs[1]), NULL));
	}
}

static void
odf_pi_parse_format_spec (GsfXMLIn *xin, char **fmt, char const *needle, char const *tag)
{
	OOParseState *state = (OOParseState *)xin->user_state;
	GString *str = g_string_new (*fmt);
	gint start = 0;
	gchar *found = NULL;

	while (NULL != (found = g_strstr_len (str->str + start, -1, needle))) {
		char *op_start = found + strlen (needle);
		gchar *p =  op_start;

		while (*p && (*p != ']'))
			p++;
		if (*p == ']') {
			char *id = g_strndup (op_start, p - op_start);
			char const *formula = g_hash_table_lookup (state->strings, id);
			char const *orig_formula = formula;
			OOFormula f_type;
			GnmExprTop const *texpr = NULL;
			char *text, *subs;
			gint start_pos = found - str->str;

			g_free (id);
			g_string_erase (str, start_pos, p - found + 1);

			if (formula == NULL)
				goto stop_parse;

			f_type = odf_get_formula_type (xin, &formula);
			if (f_type == FORMULA_NOT_SUPPORTED) {
				oo_warning (xin, _("Unsupported formula type encountered: %s"),
					    orig_formula);
				goto stop_parse;
			}
			formula = gnm_expr_char_start_p (formula);
			if (formula == NULL) {
				oo_warning (xin, _("Expression '%s' does not start "
						   "with a recognized character"), orig_formula);
				goto stop_parse;
			}
			texpr = oo_expr_parse_str
				(xin, formula, &state->pos, GNM_EXPR_PARSE_DEFAULT, f_type);

			if (texpr != NULL) {
				text = gnm_expr_top_as_string (texpr, &state->pos,
							       gnm_conventions_default);
				gnm_expr_top_unref (texpr);

				if (tag == NULL) {
					subs = text;
				} else {
					subs = g_strdup_printf ("&[%s:%s]", tag, text);
					g_free (text);
				}
				g_string_insert (str, start_pos, subs);
				start = start_pos + strlen (subs);
				g_free (subs);
			}
		} else
			break;
	}

 stop_parse:
	g_free (*fmt);
	*fmt = g_string_free (str, FALSE);
}

static void
odf_pi_parse_format (GsfXMLIn *xin, char **fmt)
{
	if ((*fmt == NULL) ||
	    (NULL == g_strstr_len (*fmt, -1, "&[cell")))
		return;

	odf_pi_parse_format_spec (xin, fmt, "&[cellt:", NULL);
	odf_pi_parse_format_spec (xin, fmt, "&[cell:", _("cell"));
}

static void
odf_pi_parse_hf (GsfXMLIn *xin, GnmPrintHF  *hf)
{
	odf_pi_parse_format (xin, &hf->left_format);
	odf_pi_parse_format (xin, &hf->middle_format);
	odf_pi_parse_format (xin, &hf->right_format);
}

static void
odf_pi_parse_expressions (GsfXMLIn *xin, GnmPrintInformation *pi)
{
	odf_pi_parse_hf (xin, pi->header);
	odf_pi_parse_hf (xin, pi->footer);
}

static void
oo_table_start (GsfXMLIn *xin, xmlChar const **attrs)
{
	OOParseState *state = (OOParseState *)xin->user_state;
	gchar *style_name = NULL;
	gchar *print_range = NULL;
	gboolean do_not_print = FALSE, tmp_b;

	state->pos.eval.col = 0;
	state->pos.eval.row = 0;
	state->print.rep_rows_from = -1;
	state->print.rep_rows_to = -1;
	state->print.rep_cols_from = -1;
	state->print.rep_cols_to = -1;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		/* We need not check for the table name since we did that during pre-parsing */
		/* if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_TABLE, "name")) { */
		if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_TABLE, "style-name"))  {
			style_name = g_strdup (CXML2C (attrs[1]));
		} else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_TABLE, "print-ranges"))  {
			print_range = g_strdup (CXML2C (attrs[1]));
		} else if (oo_attr_bool (xin, attrs, OO_NS_TABLE, "print", &tmp_b))
			do_not_print = !tmp_b;

	++state->table_n;
	state->pos.sheet = ((sheet_order_t *)g_slist_nth_data (state->sheet_order, state->table_n))->sheet;

	if (style_name != NULL) {
		OOSheetStyle const *style = g_hash_table_lookup (state->styles.sheet, style_name);
		if (style) {
			GnmPrintInformation *pi = NULL;
			if (style->master_page_name)
				pi = g_hash_table_lookup (state->styles.master_pages,
							  style->master_page_name);
			if (pi != NULL) {
				gnm_print_info_free (state->pos.sheet->print_info);
				state->pos.sheet->print_info = gnm_print_info_dup (pi);
				odf_pi_parse_expressions (xin, state->pos.sheet->print_info);
			}
			g_object_set (state->pos.sheet,
				      "visibility", style->visibility,
				      "text-is-rtl", style->is_rtl,
				      "display-formulas", style->display_formulas,
				      "display-column-header", !style->hide_col_header,
				      "display-row-header", !style->hide_row_header,
				      NULL);
			if (style->tab_color_set) {
				GnmColor *color
					= gnm_color_new_go (style->tab_color);
				g_object_set
					(state->pos.sheet,
					 "tab-background",
					 color,
					 NULL);
				style_color_unref (color);
			}
			if (style->tab_text_color_set){
				GnmColor *color
					= gnm_color_new_go
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

	state->pos.sheet->print_info->do_not_print = do_not_print;

	if (state->default_style.rows != NULL)
		sheet_row_set_default_size_pts (state->pos.sheet,
						state->default_style.rows->size_pts);
	if (state->default_style.columns != NULL)
		sheet_col_set_default_size_pts (state->pos.sheet,
						state->default_style.columns->size_pts);
	if (print_range != NULL) {
		GnmExprTop const *texpr = odf_parse_range_address_or_expr (xin, print_range);
		if (texpr != NULL) {
			GnmNamedExpr *nexpr = expr_name_lookup (&state->pos, "Print_Area");
			if (nexpr != NULL)
				expr_name_set_expr (nexpr, texpr);
		}
	}
}

static void
odf_shapes (GsfXMLIn *xin, xmlChar const **attrs)
{
	OOParseState *state = (OOParseState *)xin->user_state;
	state->pos.eval.col = -1; /* we use that to know that objects have absolute anchors */
}

static void
odf_shapes_end (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	OOParseState *state = (OOParseState *)xin->user_state;
	state->pos.eval.col = 0;
}

static void
odf_init_pp (GnmParsePos *pp, GsfXMLIn *xin, gchar const *base)
{
	OOParseState *state = (OOParseState *)xin->user_state;

	*pp = state->pos;
	if (base != NULL && *base != 0) {
		GnmExprTop const *texpr = NULL;
		char *tmp = g_strconcat ("[", base, "]", NULL);
		GnmParsePos ppp;
		/* base-cell-addresses are always required to be absolute (and contain a sheet name) */
		parse_pos_init (&ppp, state->pos.wb, state->pos.sheet, 0, 0);
		texpr = oo_expr_parse_str
			(xin, tmp, &ppp,
			 GNM_EXPR_PARSE_FORCE_EXPLICIT_SHEET_REFERENCES,
			 FORMULA_OPENFORMULA);
		g_free (tmp);
		if (texpr != NULL) {
			if (GNM_EXPR_GET_OPER (texpr->expr) ==
			    GNM_EXPR_OP_CELLREF) {
				GnmCellRef const *ref = &texpr->expr->cellref.ref;
				parse_pos_init (pp, state->pos.wb, ref->sheet,
						ref->col, ref->row);
			}
			gnm_expr_top_unref (texpr);
		}
	}
}


static GnmValidation *
odf_validation_new_list (GsfXMLIn *xin, odf_validation_t *val, guint offset)
{
	OOParseState *state = (OOParseState *)xin->user_state;
	GnmValidation *validation = NULL;
	char *start = NULL, *end = NULL;
	GString *str;
	GnmExprTop const *texpr = NULL;
	GnmParsePos pp;

	start = strchr (val->condition + offset, '(');
	if (start != NULL)
		end = strrchr (start, ')');
	if (end == NULL)
		return NULL;

	odf_init_pp (&pp, xin, val->base_cell_address);

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
		validation = gnm_validation_new (val->style,
					     GNM_VALIDATION_TYPE_IN_LIST,
					     GNM_VALIDATION_OP_NONE,
					     state->pos.sheet,
					     val->title,
					     val->message ? val->message->str : NULL,
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
	GnmParsePos pp;
	GnmExprParseFlags flag;

	odf_init_pp (&pp, xin, val->base_cell_address);
	flag = (pp.sheet == NULL || state->pos.sheet == pp.sheet)
		? GNM_EXPR_PARSE_DEFAULT
		: GNM_EXPR_PARSE_FORCE_EXPLICIT_SHEET_REFERENCES;

	texpr = oo_expr_parse_str (xin, start, &pp, flag, val->f_type);

	if (texpr != NULL)
		return gnm_validation_new (val->style,
				       val_type,
				       val_op,
				       state->pos.sheet,
				       val->title,
				       val->message ? val->message->str : NULL,
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
	GnmParsePos pp;
	GnmExprParseFlags flag;
	GnmExprTop const *texpr_a = NULL, *texpr_b = NULL;
	char *pair = NULL;
	guint len = strlen (start);

	if (*start != '(' || *(start + len - 1) != ')')
		return NULL;
	start++;
	len -= 2;
	pair = g_strndup (start, len);

	odf_init_pp (&pp, xin, val->base_cell_address);
	flag = (pp.sheet == NULL || state->pos.sheet == pp.sheet)
		? GNM_EXPR_PARSE_DEFAULT
		: GNM_EXPR_PARSE_FORCE_EXPLICIT_SHEET_REFERENCES;

	while (1) {
		gchar * try = g_strrstr_len (pair, len, ",");
		GnmExprTop const *texpr;

		if (try == NULL || try == pair)
			goto pair_error;

		texpr = oo_expr_parse_str (xin, try + 1, &pp, flag, val->f_type);
		if (texpr != NULL) {
			texpr_b = texpr;
			*try = '\0';
			break;
		}
		len = try - pair - 1;
	}
	texpr_a = oo_expr_parse_str (xin, pair, &pp, flag, val->f_type);

	if (texpr_b != NULL) {
		g_free (pair);
		return gnm_validation_new (val->style,
				       val_type,
				       val_op,
				       state->pos.sheet,
				       val->title,
				       val->message ? val->message->str : NULL,
				       texpr_a,
				       texpr_b,
				       val->allow_blank,
				       val->use_dropdown);
	}
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
		(xin, val, start, vtype, no_not ? GNM_VALIDATION_OP_BETWEEN : GNM_VALIDATION_OP_NOT_BETWEEN);
}

static GnmValidation *
odf_validation_new_op (GsfXMLIn *xin, odf_validation_t *val, guint offset, ValidationType vtype)
{
	char *start = val->condition + offset;
	ValidationOp val_op = GNM_VALIDATION_OP_NONE;

	while (*start == ' ')
		start++;

	if (g_str_has_prefix (start, ">=")) {
		val_op = GNM_VALIDATION_OP_GTE;
		start += 2;
	} else if (g_str_has_prefix (start, "<=")) {
		val_op = GNM_VALIDATION_OP_LTE;
		start += 2;
	} else if (g_str_has_prefix (start, "!=")) {
		val_op = GNM_VALIDATION_OP_NOT_EQUAL;
		start += 2;
	} else if (g_str_has_prefix (start, "=")) {
		val_op = GNM_VALIDATION_OP_EQUAL;
		start += 1;
	} else if (g_str_has_prefix (start, ">")) {
		val_op = GNM_VALIDATION_OP_GT;
		start += 1;
	} else if (g_str_has_prefix (start, "<")) {
		val_op = GNM_VALIDATION_OP_LT;
		start += 1;
	}

	if (val_op == GNM_VALIDATION_OP_NONE)
		return NULL;

	while (*start == ' ')
		start++;

	return odf_validation_new_single_expr
		(xin, val, start, vtype, val_op);
}

static GnmValidation *
odf_validations_analyze (GsfXMLIn *xin, odf_validation_t *val, guint offset,
			 ValidationType vtype, OOFormula f_type)
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
			 GNM_VALIDATION_TYPE_TEXT_LENGTH);
	else if (g_str_has_prefix (str, "cell-content-text-length-is-between"))
		return odf_validation_new_between
			(xin, val, str - val->condition + strlen ("cell-content-text-length-is-between"),
			 GNM_VALIDATION_TYPE_TEXT_LENGTH, TRUE);
	else if (g_str_has_prefix (str, "cell-content-text-length-is-not-between"))
		return odf_validation_new_between
			(xin, val, str - val->condition + strlen ("cell-content-text-length-is-not-between"),
			 GNM_VALIDATION_TYPE_TEXT_LENGTH, FALSE);
	else if (g_str_has_prefix (str, "cell-content-is-decimal-number() and"))
		return odf_validations_analyze
			(xin, val, str - val->condition + strlen ("cell-content-is-decimal-number() and"),
			 GNM_VALIDATION_TYPE_AS_NUMBER, f_type);
	else if (g_str_has_prefix (str, "cell-content-is-whole-number() and"))
		return odf_validations_analyze
			(xin, val, str - val->condition + strlen ("cell-content-is-whole-number() and"),
			 GNM_VALIDATION_TYPE_AS_INT, f_type);
	else if (g_str_has_prefix (str, "cell-content-is-date() and"))
		return odf_validations_analyze
			(xin, val, str - val->condition + strlen ("cell-content-is-date() and"),
			 GNM_VALIDATION_TYPE_AS_DATE, f_type);
	else if (g_str_has_prefix (str, "cell-content-is-time() and"))
		return odf_validations_analyze
			(xin, val, str - val->condition + strlen ("cell-content-is-time() and"),
			 GNM_VALIDATION_TYPE_AS_TIME, f_type);
	else if (g_str_has_prefix (str, "is-true-formula(") && g_str_has_suffix (str, ")")) {
		GString *gstr = g_string_new (str + strlen ("is-true-formula("));
		GnmValidation *validation;
		g_string_truncate (gstr, gstr->len - 1);
		if (vtype != GNM_VALIDATION_TYPE_ANY) {
			oo_warning
			(xin, _("Validation condition '%s' is not supported. "
				"It has been changed to '%s'."),
			 val->condition, str);
		}
		validation = odf_validation_new_single_expr
			(xin, val, gstr->str, GNM_VALIDATION_TYPE_CUSTOM,
			 GNM_VALIDATION_OP_NONE);
		g_string_free (gstr, TRUE);
		return validation;
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

static GnmInputMsg *
odf_validation_get_input_message (GsfXMLIn *xin, char const *name)
{
	OOParseState *state = (OOParseState *)xin->user_state;
	odf_validation_t *val = g_hash_table_lookup (state->validations, name);

	if (val == NULL)
		return NULL;

	if ((val->help_message != NULL && val->help_message->len > 0) ||
	     (val->help_title != NULL && strlen (val->help_title) > 0))
		return gnm_input_msg_new (val->help_message ? val->help_message->str : NULL, val->help_title);
	else
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
		const char *str = val->condition;
		GnmValidation *validation;
		OOFormula f_type = odf_get_formula_type (xin, &str);
		validation = odf_validations_analyze
			(xin, val, str - val->condition, GNM_VALIDATION_TYPE_ANY, f_type);
		if (validation != NULL) {
			GError   *err;
			if (NULL == (err = gnm_validation_is_ok (validation)))
				return validation;
			else {
				oo_warning (xin,
					    _("Ignoring invalid data "
					      "validation because : %s"),
					    _(err->message));
				gnm_validation_unref (validation);
				return NULL;
			}
		}
	}
	if (val->condition != NULL)
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
	g_free (val->title);
	g_free (val->help_title);
	if (val->message)
		g_string_free (val->message, TRUE);
	if (val->help_message)
		g_string_free (val->help_message, TRUE);
	g_free (val);
}

static odf_validation_t *
odf_validation_new (void)
{
	odf_validation_t *val = g_new0 (odf_validation_t, 1);
	val->use_dropdown = TRUE;
	val->allow_blank = TRUE;
	val->f_type = FORMULA_NOT_SUPPORTED;
	val->style = GNM_VALIDATION_STYLE_WARNING;
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
	if (name != NULL) {
		g_hash_table_insert (state->validations, g_strdup (name), validation);
		state->cur_validation = validation;
	} else {
		odf_validation_free (validation);
		state->cur_validation = NULL;
	}
}


static void
odf_validation_error_message (GsfXMLIn *xin, xmlChar const **attrs)
{
	static OOEnum const message_styles [] = {
		{ "information",  GNM_VALIDATION_STYLE_INFO },
		{ "stop",	  GNM_VALIDATION_STYLE_STOP },
		{ "warning",      GNM_VALIDATION_STYLE_WARNING },
		{ NULL,	0 },
	};

	OOParseState *state = (OOParseState *)xin->user_state;
	int tmp;

	if (state->cur_validation)
		for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2){
			if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]),
						OO_NS_TABLE, "title" )) {
				g_free (state->cur_validation->title);
				state->cur_validation->title = g_strdup (CXML2C (attrs[1]));
			} else if (oo_attr_enum (xin, attrs, OO_NS_TABLE, "message-type", message_styles, &tmp))
				state->cur_validation->style = tmp;
			/* ignoring TABLE "display" */
		}

	odf_push_text_p (state, FALSE);
}

static void
odf_validation_error_message_end (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	oo_text_p_t *ptr;
	OOParseState *state = (OOParseState *)xin->user_state;

	g_return_if_fail (state->text_p_stack != NULL);
	ptr = state->text_p_stack->data;
	g_return_if_fail (ptr != NULL);

	if (state->cur_validation) {
		state->cur_validation->message = ptr->gstr;
		ptr->gstr = NULL;
	}
	odf_pop_text_p (state);
}

static void
odf_validation_help_message (GsfXMLIn *xin, xmlChar const **attrs)
{
	OOParseState *state = (OOParseState *)xin->user_state;

	if (state->cur_validation)
		for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2){
			if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]),
						OO_NS_TABLE, "title" )) {
				g_free (state->cur_validation->help_title);
				state->cur_validation->help_title = g_strdup (CXML2C (attrs[1]));
			}
			/* ignoring TABLE "display" */
		}

	odf_push_text_p (state, FALSE);
}

static void
odf_validation_help_message_end (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	oo_text_p_t *ptr;
	OOParseState *state = (OOParseState *)xin->user_state;

	g_return_if_fail (state->text_p_stack != NULL);
	ptr = state->text_p_stack->data;
	g_return_if_fail (ptr != NULL);

	if (state->cur_validation) {
		state->cur_validation->help_message = ptr->gstr;
		ptr->gstr = NULL;
	}
	odf_pop_text_p (state);
}

static void
odf_adjust_offsets_col (OOParseState *state, int *col, double *x, gboolean absolute)
{
	ColRowInfo const *cr = sheet_col_get_info (state->pos.sheet,
						   *col);
	int last =  gnm_sheet_get_last_col (state->pos.sheet);
	if (absolute && *col > 0)
		*x -= sheet_col_get_distance_pts (state->pos.sheet, 0, *col);
	while (cr->size_pts < *x && *col < last) {
		(*col)++;
		(*x) -= cr->size_pts;
		cr = sheet_col_get_info (state->pos.sheet, *col);
	}
	while (*x < 0 && *col > 0) {
		(*col)--;
		cr = sheet_col_get_info (state->pos.sheet, *col);
		(*x) += cr->size_pts;
	}
	*x /= cr->size_pts;
}

static void
odf_adjust_offsets_row (OOParseState *state, int *row, double *y, gboolean absolute)
{
	ColRowInfo const *cr = sheet_row_get_info (state->pos.sheet,
						   *row);
	int last =  gnm_sheet_get_last_row (state->pos.sheet);
	if (absolute && *row > 0)
		*y -= sheet_row_get_distance_pts (state->pos.sheet, 0, *row);
	while (cr->size_pts < *y && *row < last) {
		(*row)++;
		(*y) -= cr->size_pts;
		cr = sheet_row_get_info (state->pos.sheet, *row);
	}
	while (*y < 0 && *row > 0) {
		(*row)--;
		cr = sheet_row_get_info (state->pos.sheet, *row);
		(*y) += cr->size_pts;
	}
	*y /= cr->size_pts;
}

static void
odf_adjust_offsets (OOParseState *state, GnmCellPos *pos, double *x, double *y, gboolean absolute)
{
	odf_adjust_offsets_col (state, &pos->col, x, absolute);
	odf_adjust_offsets_row (state, &pos->row, y, absolute);
}

static gint
odf_z_idx_compare (gconstpointer a, gconstpointer b)
{
	object_offset_t const *za = a, *zb = b;

	/* We are sorting indices in increasing order! */
	return (za->z_index - zb->z_index);
}

static void
odf_destroy_object_offset (gpointer data)
{
	object_offset_t *ob_off = data;

	g_free (ob_off->control);
	g_object_unref (ob_off->so);
	g_free (ob_off);
}

static void
odf_complete_control_setup (OOParseState *state, object_offset_t const *ob_off)
{
	OOControl *oc = g_hash_table_lookup (state->controls, ob_off->control);
	GnmExprTop const *result_texpr = NULL;
	SheetObject *so = ob_off->so;

	if (oc == NULL)
		return;

	if ((oc->t == sheet_widget_checkbox_get_type () ||
	     oc->t == sheet_widget_radio_button_get_type ()) && oc->current_state != NULL)
		g_object_set (G_OBJECT (so), "active",
			      strcmp (oc->current_state, "checked") == 0 ||
			      strcmp (oc->current_state, "true") == 0, NULL);
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

static void
oo_table_end (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	OOParseState *state = (OOParseState *)xin->user_state;
	int max_cols, max_rows;
	GSList *l;
	gint top_z = -1;

	maybe_update_progress (xin);

	if (NULL != state->print.page_breaks.h) {
		print_info_set_breaks (state->pos.sheet->print_info,
			state->print.page_breaks.h);
		state->print.page_breaks.h = NULL;
	}
	if (NULL != state->print.page_breaks.v) {
		print_info_set_breaks (state->pos.sheet->print_info,
			state->print.page_breaks.v);
		state->print.page_breaks.v = NULL;
	}

	max_cols = gnm_sheet_get_max_cols (state->pos.sheet);
	max_rows = gnm_sheet_get_max_rows (state->pos.sheet);

	if (state->print.rep_rows_from >= 0) {
		if (state->print.rep_rows_to < 0)
			state->print.rep_rows_to = max_rows - 1;
		g_free (state->pos.sheet->print_info->repeat_top);
		state->pos.sheet->print_info->repeat_top
			= g_strdup (rows_name (state->print.rep_rows_from,
					       state->print.rep_rows_to));
	}
	if (state->print.rep_cols_from >= 0) {
		if (state->print.rep_cols_to < 0)
			state->print.rep_cols_to = max_cols - 1;
		g_free (state->pos.sheet->print_info->repeat_left);
		state->pos.sheet->print_info->repeat_left
			= g_strdup (cols_name (state->print.rep_cols_from,
					       state->print.rep_cols_to));
	}

	/* We need to fix the anchors of all offsets, ensure that each object has an "odf-z-index", */
	/* and add the objects in the correct order. */
	state->chart_list = g_slist_reverse (state->chart_list);

	for (l = state->chart_list; l != NULL; l = l->next) {
		object_offset_t *ob_off = l->data;
		if (top_z < ob_off->z_index)
			top_z = ob_off->z_index;
	}

	for (l = state->chart_list; l != NULL; l = l->next) {
		object_offset_t *ob_off = l->data;
		if (ob_off->z_index < 0) {
			top_z++;
			ob_off->z_index = top_z;
			if (state->debug)
				g_print ("Object Ordering: Object without z-index encountered.\n");
		}
	}

	state->chart_list = g_slist_sort (state->chart_list,
					  odf_z_idx_compare);


	for (l = state->chart_list; l != NULL; l = l->next) {
		object_offset_t *ob_off = l->data;
		SheetObjectAnchor new;
		SheetObjectAnchor const *old = sheet_object_get_anchor (ob_off->so);
		GnmRange cell_base = *sheet_object_get_range (ob_off->so);

		if (old->mode != GNM_SO_ANCHOR_ABSOLUTE) {
			odf_adjust_offsets (state, &cell_base.start, &ob_off->frame_offset[0],
					    &ob_off->frame_offset[1], ob_off->absolute_distance);
			if (old->mode == GNM_SO_ANCHOR_TWO_CELLS)
				odf_adjust_offsets (state, &cell_base.end, &ob_off->frame_offset[2],
						    &ob_off->frame_offset[3], ob_off->absolute_distance);
		}
		sheet_object_anchor_init (&new, &cell_base, ob_off->frame_offset,
					  old->base.direction,
					  old->mode);
		sheet_object_set_anchor (ob_off->so, &new);

		sheet_object_set_sheet (ob_off->so, state->pos.sheet);
		if (ob_off->control)
			odf_complete_control_setup (state, ob_off);
		odf_destroy_object_offset (ob_off);
		l->data = NULL;
	}

	g_slist_free (state->chart_list);
	state->chart_list = NULL;
	state->pos.eval.col = state->pos.eval.row = 0;
	state->pos.sheet = NULL;
}

static void
oo_append_page_break (OOParseState *state, int pos, gboolean is_vert, gboolean is_manual)
{
	GnmPageBreaks *breaks;

	if (is_vert) {
		if (NULL == (breaks = state->print.page_breaks.v))
			breaks = state->print.page_breaks.v = gnm_page_breaks_new (TRUE);
	} else {
		if (NULL == (breaks = state->print.page_breaks.h))
			breaks = state->print.page_breaks.h = gnm_page_breaks_new (FALSE);
	}

	gnm_page_breaks_append_break (breaks, pos,
				      is_manual ? GNM_PAGE_BREAK_MANUAL : GNM_PAGE_BREAK_NONE);
}

static void
oo_set_page_break (OOParseState *state, int pos, gboolean is_vert, gboolean is_manual)
{
	GnmPageBreaks *breaks = (is_vert) ? state->print.page_breaks.v : state->print.page_breaks.h;

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

static OOCellStyle *
odf_oo_cell_style_new (GnmStyle *style)
{
	OOCellStyle *oostyle = g_new0 (OOCellStyle, 1);
	oostyle->ref = 1;
	if (style)
		oostyle->style = gnm_style_dup (style);
	else
		oostyle->style = gnm_style_new ();
	return oostyle;
}

static void
odf_oo_cell_style_unref (OOCellStyle *oostyle)
{
	if (oostyle != NULL && (--(oostyle->ref)) == 0) {
		gnm_style_unref (oostyle->style);
		g_slist_free_full (oostyle->styles, (GDestroyNotify) odf_oo_cell_style_unref);
		g_slist_free_full (oostyle->conditions, g_free);
		g_slist_free_full (oostyle->bases, g_free);
		g_free (oostyle);
	}
}

static OOCellStyle *
odf_oo_cell_style_ref (OOCellStyle *oostyle)
{
	oostyle->ref++;
	return oostyle;
}

static OOCellStyle *
odf_oo_cell_style_copy (OOCellStyle *oostyle)
{
	OOCellStyle *new = odf_oo_cell_style_new (oostyle->style);
	new->styles = g_slist_copy_deep (oostyle->styles, (GCopyFunc)odf_oo_cell_style_ref, NULL);
	new->conditions = g_slist_copy_deep (oostyle->conditions, (GCopyFunc)g_strdup, NULL);
	new->bases = g_slist_copy_deep (oostyle->bases, (GCopyFunc)g_strdup, NULL);
	return new;
}

static void
odf_oo_cell_style_attach_condition (OOCellStyle *oostyle, OOCellStyle *cstyle,
				    gchar const *condition, gchar const *base)
{
	g_return_if_fail (oostyle != NULL);
	g_return_if_fail (cstyle != NULL);
	g_return_if_fail (condition != NULL);

	if (base == NULL)
		base = "";

	oostyle->styles = g_slist_append (oostyle->styles, odf_oo_cell_style_ref (cstyle));
	oostyle->conditions = g_slist_append (oostyle->conditions, g_strdup (condition));
	oostyle->bases = g_slist_append (oostyle->bases, g_strdup (base));
}

static gboolean
odf_style_load_two_values (GsfXMLIn *xin, char *condition, GnmStyleCond *cond, gchar const *base, OOFormula f_type)
{
	condition = g_strstrip (condition);
	if (*(condition++) == '(') {
		guint len = strlen (condition);
		char *end = condition + len - 1;
		if (*end == ')') {
			GnmParsePos pp;
			GnmExprTop const *texpr;

			odf_init_pp (&pp, xin, base);
			len -= 1;
			*end = '\0';
			while (1) {
				gchar * try = g_strrstr_len (condition, len, ",");
				GnmExprTop const *texpr;

				if (try == NULL || try == condition) return FALSE;

				texpr = oo_expr_parse_str
					(xin, try + 1, &pp,
					 GNM_EXPR_PARSE_DEFAULT,
					 f_type);
				if (texpr != NULL) {
					gnm_style_cond_set_expr (cond, texpr, 1);
					gnm_expr_top_unref (texpr);
					*try = '\0';
					break;
				}
				len = try - condition - 1;
			}
			texpr = oo_expr_parse_str
				(xin, condition, &pp,
				 GNM_EXPR_PARSE_DEFAULT,
				 f_type);
			gnm_style_cond_set_expr (cond, texpr, 0);
			if (texpr) gnm_expr_top_unref (texpr);

			return (gnm_style_cond_get_expr (cond, 0) &&
				gnm_style_cond_get_expr (cond, 1));
		}
	}
	return FALSE;
}

static gboolean
odf_style_load_one_value (GsfXMLIn *xin, char *condition, GnmStyleCond *cond, gchar const *base, OOFormula f_type)
{
	GnmParsePos pp;
	GnmExprTop const *texpr;

	odf_init_pp (&pp, xin, base);
	texpr = oo_expr_parse_str
		(xin, condition, &pp,
		 GNM_EXPR_PARSE_DEFAULT,
		 f_type);
	gnm_style_cond_set_expr (cond, texpr, 0);
	if (texpr) gnm_expr_top_unref (texpr);
	return (gnm_style_cond_get_expr (cond, 0) != NULL);
}

static void
odf_style_add_condition (GsfXMLIn *xin, GnmStyle *style, GnmStyle *cstyle,
			 gchar const *condition, gchar const *base)
{
	OOParseState *state = (OOParseState *)xin->user_state;

	gchar const *full_condition = condition;
	GnmStyleCond *cond = NULL;
	GnmStyleConditions *sc;
	gboolean success = FALSE;
	OOFormula f_type;
	Sheet *sheet = state->pos.sheet;

	g_return_if_fail (style != NULL);
	g_return_if_fail (cstyle != NULL);
	g_return_if_fail (condition != NULL);
	g_return_if_fail (base != NULL);

	f_type = odf_get_formula_type (xin, &condition);

	if (g_str_has_prefix (condition, "cell-content()")) {
		condition += strlen ("cell-content()") - 1;
		while (*(++condition) == ' ');
		switch (*(condition++)) {
		case '<':
			if (*condition == '=') {
				condition++;
				cond = gnm_style_cond_new (GNM_STYLE_COND_LTE, sheet);
			} else
				cond = gnm_style_cond_new (GNM_STYLE_COND_LT, sheet);
			success = TRUE;
			break;
		case '>':
			if (*condition == '=') {
				condition++;
				cond = gnm_style_cond_new (GNM_STYLE_COND_GTE, sheet);
			} else
				cond = gnm_style_cond_new (GNM_STYLE_COND_GT, sheet);
			success = TRUE;
			break;
			break;
		case '=':
			cond = gnm_style_cond_new (GNM_STYLE_COND_EQUAL, sheet);
			success = TRUE;
			break;
		case '!':
			if (*condition == '=') {
				condition++;
				cond = gnm_style_cond_new (GNM_STYLE_COND_NOT_EQUAL, sheet);
				success = TRUE;
			}
			break;
		default:
			break;
		}
		if (success) {
			char *text = g_strdup (condition);
			success = odf_style_load_one_value (xin, text, cond, base, f_type);
			g_free (text);
		}

	} else if (g_str_has_prefix (condition, "cell-content-is-between")) {
		char *text;
		cond = gnm_style_cond_new (GNM_STYLE_COND_BETWEEN, sheet);
		condition += strlen ("cell-content-is-between");
		text = g_strdup (condition);
		success = odf_style_load_two_values (xin, text, cond, base, f_type);
		g_free (text);
	} else if (g_str_has_prefix (condition, "cell-content-is-not-between")) {
		char *text;
		cond = gnm_style_cond_new (GNM_STYLE_COND_NOT_BETWEEN, sheet);
		condition += strlen ("cell-content-is-not-between");
		text = g_strdup (condition);
		success = odf_style_load_two_values (xin, text, cond, base, f_type);
		g_free (text);
	} else if (g_str_has_prefix (condition, "is-true-formula(") && g_str_has_suffix (condition, ")") ) {
		char *text;
		cond = gnm_style_cond_new (GNM_STYLE_COND_CUSTOM, sheet);
		condition += strlen ("is-true-formula(");
		text = g_strdup (condition);
		*(text + strlen (text) - 1) = '\0';
		success = odf_style_load_one_value (xin, text, cond, base, f_type);
		g_free (text);
	}

	if (!success || !cond) {
		if (cond)
			gnm_style_cond_free (cond);
		oo_warning (xin,
			    _("Unknown condition '%s' encountered, ignoring."),
			    full_condition);
		return;
	}

	gnm_style_cond_canonicalize (cond);
	gnm_style_cond_set_overlay (cond, cstyle);

	if (gnm_style_is_element_set (style, MSTYLE_CONDITIONS) &&
	    (sc = gnm_style_get_conditions (style)) != NULL)
		gnm_style_conditions_insert (sc, cond, -1);
	else {
		sc = gnm_style_conditions_new (sheet);
		gnm_style_conditions_insert (sc, cond, -1);
		gnm_style_set_conditions (style, sc);
	}

	gnm_style_cond_free (cond);
}

static GnmStyle *
odf_style_from_oo_cell_style (GsfXMLIn *xin, OOCellStyle *oostyle)
{
	g_return_val_if_fail (oostyle != NULL, NULL);

	if (oostyle->conditions != NULL) {
		/* We need to incorporate the conditional styles */
		GnmStyle *new_style = gnm_style_dup (oostyle->style);
		GSList *styles = oostyle->styles, *conditions = oostyle->conditions, *bases = oostyle->bases;
		while (styles && conditions && bases) {
			GnmStyle *cstyle = odf_style_from_oo_cell_style (xin, styles->data);
			odf_style_add_condition (xin, new_style, cstyle,
						 conditions->data, bases->data);
			gnm_style_unref (cstyle);
			styles = styles->next;
			conditions = conditions->next;
			bases = bases->next;
		}
		gnm_style_unref (oostyle->style);
		oostyle->style = new_style;
		g_slist_free_full (oostyle->styles, (GDestroyNotify) odf_oo_cell_style_unref);
		g_slist_free_full (oostyle->conditions, g_free);
		g_slist_free_full (oostyle->bases, g_free);
		oostyle->styles = NULL;
		oostyle->conditions = NULL;
		oostyle->bases = NULL;
	}
	gnm_style_ref (oostyle->style);
	return oostyle->style;
}

static void
oo_col_start (GsfXMLIn *xin, xmlChar const **attrs)
{
	OOParseState *state = (OOParseState *)xin->user_state;
	OOColRowStyle *col_info = NULL;
	OOCellStyle *oostyle = NULL;
	GnmStyle *style = NULL;
	int	  i, repeat_count = 1;
	gboolean  hidden = FALSE;
	int max_cols = gnm_sheet_get_max_cols (state->pos.sheet);

	maybe_update_progress (xin);

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_TABLE, "default-cell-style-name")) {
			oostyle = g_hash_table_lookup (state->styles.cell, attrs[1]);
			if (oostyle)
				style = odf_style_from_oo_cell_style (xin, oostyle);
			else
				oo_warning (xin, "The cell style with name <%s> is missing", CXML2C (attrs[01]));
		} else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_TABLE, "style-name"))
			col_info = g_hash_table_lookup (state->styles.col, attrs[1]);
		else if (oo_attr_int_range (xin, attrs, OO_NS_TABLE, "number-columns-repeated",
					    &repeat_count, 0, INT_MAX - state->pos.eval.col))
			;
		else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_TABLE, "visibility"))
			hidden = !attr_eq (attrs[1], "visible");

	if (state->pos.eval.col + repeat_count > max_cols) {
		max_cols = gnm_sheet_get_max_cols (state->pos.sheet);
		if (state->pos.eval.col + repeat_count > max_cols) {
			oo_warning (xin, _("Ignoring column information beyond"
					   " column %i"), max_cols);
			repeat_count = max_cols - state->pos.eval.col - 1;
		}
	}

	if (hidden)
		colrow_set_visibility (state->pos.sheet, TRUE, FALSE, state->pos.eval.col,
			state->pos.eval.col + repeat_count - 1);

	if (NULL != style) {
		GnmRange r;
		r.start.col = state->pos.eval.col;
		r.end.col   = state->pos.eval.col + repeat_count - 1;
		r.start.row = 0;
		r.end.row  = ((sheet_order_t *)g_slist_nth_data (state->sheet_order, state->table_n))->rows - 1;
		sheet_style_apply_range (state->pos.sheet, &r, style);
	}
	if (col_info != NULL) {
		if (state->default_style.columns == NULL && repeat_count > max_cols/2) {
			int const last = state->pos.eval.col + repeat_count;
			state->default_style.columns = go_memdup (col_info, sizeof (*col_info));
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
				/* I cannot find a listing for the default but will
				 * assume it is TRUE to keep the files rational */
				if (col_info->size_pts > 0)
					sheet_col_set_size_pts (state->pos.sheet, i,
								col_info->size_pts, col_info->manual);
				oo_col_row_style_apply_breaks (state, col_info, i, TRUE);
			}
			col_info->count += repeat_count;
		}
	}

	state->pos.eval.col += repeat_count;
}

static void
odf_table_header_rows (GsfXMLIn *xin, G_GNUC_UNUSED xmlChar const **attrs)
{
	OOParseState *state = (OOParseState *)xin->user_state;

	if (state->print.rep_rows_from < 0)
		state->print.rep_rows_from = state->pos.eval.row;
	/* otherwise we are continuing an existing range */
}
static void
odf_table_header_rows_end (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	OOParseState *state = (OOParseState *)xin->user_state;

	state->print.rep_rows_to = state->pos.eval.row - 1;
}

static void
odf_table_header_cols (GsfXMLIn *xin, G_GNUC_UNUSED xmlChar const **attrs)
{
	OOParseState *state = (OOParseState *)xin->user_state;

	if (state->print.rep_cols_from < 0)
		state->print.rep_cols_from = state->pos.eval.col;
	/* otherwise we are continuing an existing range */
}
static void
odf_table_header_cols_end (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	OOParseState *state = (OOParseState *)xin->user_state;

	state->print.rep_cols_to = state->pos.eval.col - 1;
}

static void
oo_row_start (GsfXMLIn *xin, xmlChar const **attrs)
{
	OOParseState *state = (OOParseState *)xin->user_state;
	OOColRowStyle *row_info = NULL;
	OOCellStyle *oostyle = NULL;
	GnmStyle *style = NULL;
	int	  i, repeat_count = 1;
	gboolean  hidden = FALSE;
	int max_rows = gnm_sheet_get_max_rows (state->pos.sheet);

	maybe_update_progress (xin);

	state->pos.eval.col = 0;

	if (state->pos.eval.row >= max_rows) {
		max_rows = gnm_sheet_get_max_rows (state->pos.sheet);
		if (state->pos.eval.row >= max_rows) {
			oo_warning (xin, _("Content past the maximum number of rows (%i) supported."), max_rows);
			state->row_inc = 0;
			return;
		}
	}

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2) {
		if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_TABLE, "default-cell-style-name")) {
			oostyle = g_hash_table_lookup (state->styles.cell, attrs[1]);
			if (oostyle)
				style = odf_style_from_oo_cell_style (xin, oostyle);
			else
				oo_warning (xin, "The cell style with name <%s> is missing", CXML2C (attrs[01]));
		} else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_TABLE, "style-name"))
			row_info = g_hash_table_lookup (state->styles.row, attrs[1]);
		else if (oo_attr_int_range (xin, attrs, OO_NS_TABLE, "number-rows-repeated", &repeat_count, 0,
					    INT_MAX - state->pos.eval.row))
			;
		else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_TABLE, "visibility"))
			hidden = !attr_eq (attrs[1], "visible");
	}

	if (state->pos.eval.row + repeat_count > max_rows)
        	/* There are probably lots of empty lines at the end. */
		repeat_count = max_rows - state->pos.eval.row - 1;

	if (hidden)
		colrow_set_visibility (state->pos.sheet, FALSE, FALSE, state->pos.eval.row,
			state->pos.eval.row+repeat_count - 1);

	if (NULL != style) {
		GnmRange r;
		r.start.row = state->pos.eval.row;
		r.end.row   = state->pos.eval.row + repeat_count - 1;
		r.start.col = 0;
		r.end.col  = ((sheet_order_t *)g_slist_nth_data (state->sheet_order, state->table_n))->cols - 1;;
		sheet_style_apply_range (state->pos.sheet, &r, style);
	}

	if (row_info != NULL) {
		if (state->default_style.rows == NULL && repeat_count > max_rows/2) {
			int const last = state->pos.eval.row + repeat_count;
			state->default_style.rows = go_memdup (row_info, sizeof (*row_info));
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
				if (row_info->size_pts > 0)
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
	OOCellStyle *oostyle = NULL;
	GnmStyle *style = NULL;
	char const *style_name = NULL;
	char const *validation_name = NULL;
	char const *expr_string;
	GnmRange tmp;
	int itmp;
	int max_cols = gnm_sheet_get_max_cols (state->pos.sheet);
	int max_rows = gnm_sheet_get_max_rows (state->pos.sheet);
	GnmValidation *validation = NULL;
	gboolean possible_error_constant = FALSE;
	gboolean columns_spanned_fake = FALSE;

	maybe_update_progress (xin);

	state->col_inc = 1;
	state->content_is_error = FALSE;
	state->value_type = OO_VALUE_TYPE_VOID;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2) {
		if (oo_attr_int_range (xin, attrs, OO_NS_TABLE, "number-columns-repeated",
				       &state->col_inc, 0, INT_MAX - state->pos.eval.col))
			;
		else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_GNUM_NS_EXT, "error-value"))
			/* While the value of this attribute contains the true error value   */
			/* it could have been retained by a consumer/producer who did change */
			/* the cell value, so we just remember that we saw this attribute.   */
			possible_error_constant = TRUE;
		else if (oo_attr_enum (xin, attrs, OO_NS_OFFICE, "value-type", odf_value_types, &itmp))
			state->value_type = itmp;
		else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_TABLE, "formula")) {
			OOFormula f_type;

			if (attrs[1] == NULL) {
				oo_warning (xin, _("Missing expression"));
				continue;
			}

			expr_string = CXML2C (attrs[1]);
			f_type = odf_get_formula_type (xin, &expr_string);
			if (f_type == FORMULA_NOT_SUPPORTED) {
				oo_warning (xin,
					    _("Unsupported formula type encountered: %s"),
					    expr_string);
				continue;
			}

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
			else {
				texpr = oo_expr_parse_str
					(xin, expr_string,
					 &state->pos, GNM_EXPR_PARSE_DEFAULT, f_type);
				if (possible_error_constant && texpr != NULL &&
				    GNM_EXPR_GET_OPER (texpr->expr) == GNM_EXPR_OP_CONSTANT) {
					GnmValue const *eval = 	gnm_expr_get_constant (texpr->expr);
					if (VALUE_IS_ERROR (eval)) {
						value_release (val);
						val = value_dup (eval);
						gnm_expr_top_unref (texpr);
						texpr = NULL;
					}
				}
			}
		} else if (oo_attr_bool (xin, attrs,
					 (state->ver == OOO_VER_OPENDOC) ?
					 OO_NS_OFFICE : OO_NS_TABLE,
					 "boolean-value", &bool_val)) {
			if (val == NULL)
				val = value_new_bool (bool_val);
		} else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]),
			(state->ver == OOO_VER_OPENDOC) ? OO_NS_OFFICE : OO_NS_TABLE,
			"date-value")) {
			if (val == NULL) {
				unsigned y, m, d, h, mi;
				gnm_float s;
				int n = gnm_sscanf (CXML2C (attrs[1]),
						    "%u-%u-%uT%u:%u:%" GNM_SCANF_g,
						    &y, &m, &d, &h, &mi, &s);

				if (n >= 3) {
					GDate date;
					g_date_set_dmy (&date, d, m, y);
					if (g_date_valid (&date)) {
						unsigned d_serial = go_date_g_to_serial (&date,
											 workbook_date_conv (state->pos.wb));
						if (n >= 6) {
							gnm_float time_frac
								= h + ((gnm_float)mi / 60) +
								((gnm_float)s / 3600);
							val = value_new_float (d_serial + time_frac / 24);
							has_datetime = TRUE;
						} else {
							val = value_new_int (d_serial);
							has_date = TRUE;
						}
					}
				}
			}
		} else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]),
					       (state->ver == OOO_VER_OPENDOC) ?
					       OO_NS_OFFICE : OO_NS_TABLE,
					       "time-value")) {
			if (val == NULL) {
				unsigned h, m, s;
				if (3 == sscanf (CXML2C (attrs[1]), "PT%uH%uM%uS", &h, &m, &s)) {
					unsigned secs = h * 3600 + m * 60 + s;
					val = value_new_float (secs / (gnm_float)86400);
					has_time = TRUE;
				}
			}
		} else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]),
					       (state->ver == OOO_VER_OPENDOC) ?
					       OO_NS_OFFICE : OO_NS_TABLE,
					       "string-value")) {
			if (val == NULL)
				val = value_new_string (CXML2C (attrs[1]));
		} else if (oo_attr_float (xin, attrs,
					  (state->ver == OOO_VER_OPENDOC) ? OO_NS_OFFICE : OO_NS_TABLE,
					  "value", &float_val)) {
			if (val == NULL)
				val = value_new_float (float_val);
		} else if (oo_attr_int_range (xin, attrs, OO_NS_TABLE,
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
		else if (oo_attr_bool (xin, attrs, OO_GNUM_NS_EXT,
				       "columns-spanned-fake",
				       &columns_spanned_fake))
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

	if (columns_spanned_fake)
		merge_cols = 1;

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
			oostyle = g_hash_table_lookup (state->styles.cell_datetime, style_name);
		else if (has_date)
			oostyle = g_hash_table_lookup (state->styles.cell_date, style_name);
		else if (has_time)
			oostyle = g_hash_table_lookup (state->styles.cell_time, style_name);

		if (oostyle == NULL) {
			oostyle = g_hash_table_lookup (state->styles.cell, style_name);
			if (((oostyle != NULL) || (state->ver == OOO_VER_1))
			    && (has_datetime || has_date || has_time)) {
				if ((oostyle == NULL) ||
				    ((!gnm_style_is_element_set (oostyle->style, MSTYLE_FORMAT))
				     || go_format_is_general (gnm_style_get_format (oostyle->style)))) {
					GOFormat *format;
					oostyle = (oostyle == NULL) ? odf_oo_cell_style_new (NULL) :
						odf_oo_cell_style_copy (oostyle);
					if (has_datetime) {
						format = go_format_default_date_time ();
						g_hash_table_replace (state->styles.cell_datetime,
								      g_strdup (style_name), oostyle);
					} else if (has_date) {
						format = go_format_default_date ();
						g_hash_table_replace (state->styles.cell_date,
							      g_strdup (style_name), oostyle);
					} else {
						format = go_format_default_time ();
						g_hash_table_replace (state->styles.cell_time,
								      g_strdup (style_name), oostyle);
					}
					gnm_style_set_format (oostyle->style, format);
				}
			}
		}
		if (oostyle != NULL)
			style = odf_style_from_oo_cell_style (xin, oostyle);
	}

	if (validation_name != NULL) {
		GnmInputMsg *message;
		if (NULL != (validation = odf_validations_translate (xin, validation_name))) {
			if (style == NULL)
				style = gnm_style_new ();
			/* 1 reference for style*/
			gnm_style_set_validation (style, validation);
		}
		if (NULL != (message = odf_validation_get_input_message (xin, validation_name))) {
			if (style == NULL)
				style = gnm_style_new ();
			/* 1 reference for style*/
			gnm_style_set_input_msg (style, message);
		}
	}

	if (style == NULL && (merge_cols > 1 || merge_rows > 1)) {
		/* We may not have a new style but the current cell may */
		/* have been assigned a style earlier */
		GnmStyle const *old_style
			= sheet_style_get (state->pos.sheet, state->pos.eval.col,
					   state->pos.eval.row);
		if (old_style != NULL)
			style = gnm_style_dup (old_style);
	}

	if (style != NULL) {
		if (state->col_inc > 1 || state->row_inc > 1) {
			range_init_cellpos_size (&tmp, &state->pos.eval,
				state->col_inc, state->row_inc);
			sheet_style_apply_range (state->pos.sheet, &tmp, style);
		} else if (merge_cols > 1 || merge_rows > 1) {
			range_init_cellpos_size (&tmp, &state->pos.eval,
						 merge_cols, merge_rows);
			sheet_style_apply_range (state->pos.sheet, &tmp, style);
		} else {
			sheet_style_apply_pos (state->pos.sheet,
					       state->pos.eval.col, state->pos.eval.row,
					       style);
		}
	}

	state->text_p_for_cell.content_is_simple = FALSE;
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
		} else {
			if (val != NULL)
				gnm_cell_set_expr_and_value (cell, texpr, val,
							     TRUE);
			else
				gnm_cell_set_expr (cell, texpr);
			gnm_expr_top_unref (texpr);
		}
	} else if (val != NULL) {
		GnmCell *cell = sheet_cell_fetch (state->pos.sheet,
			state->pos.eval.col, state->pos.eval.row);

		/* has cell previously been initialized as part of an array */
		if (gnm_cell_is_nonsingleton_array (cell))
			gnm_cell_assign_value (cell, val);
		else
			gnm_cell_set_value (cell, val);
	} else if (!state->content_is_error)
		/* store the content as a string */
		state->text_p_for_cell.content_is_simple = TRUE;

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
		}
	}
	state->pos.eval.col += state->col_inc;
}

static void
oo_add_text_to_cell (OOParseState *state, char const *str, PangoAttrList *attrs)
{
	GnmValue *v = NULL;
	PangoAttrList *old = NULL;
	int start = 0;
	GOFormat *fmt;

	if (state->curr_cell == NULL ||
	    (state->value_type == OO_VALUE_TYPE_VOID && state->ver == OOO_VER_OPENDOC))
		return;

	if ((NULL != state->curr_cell->value) && VALUE_IS_STRING (state->curr_cell->value)) {
		GOFormat *fmt = state->curr_cell->value->v_str.fmt;
		start = strlen (state->curr_cell->value->v_str.val->str);
		if (fmt != NULL)
			go_format_ref (fmt);
		v = value_new_string_str
			(go_string_new_nocopy
			 (g_strconcat (state->curr_cell->value->v_str.val->str,
				       str, NULL)));
		if (fmt != NULL) {
			value_set_fmt (v, fmt);
			go_format_unref (fmt);
		}
	} else
		v = value_new_string (str);
	if (v != NULL)
		gnm_cell_assign_value (state->curr_cell, v);

	if (attrs) {
		if (state->curr_cell->value->v_str.fmt != NULL) {
			old = pango_attr_list_copy
				((PangoAttrList *)go_format_get_markup (state->curr_cell->value->v_str.fmt));
		} else
			old = pango_attr_list_new ();
		pango_attr_list_splice  (old, attrs, start, strlen (str));
		fmt = go_format_new_markup (old, FALSE);
		value_set_fmt (state->curr_cell->value, fmt);
		go_format_unref (fmt);
	}
}

static void
oo_cell_content_start (GsfXMLIn *xin, G_GNUC_UNUSED xmlChar const **attrs)
{
       OOParseState *state = (OOParseState *)xin->user_state;

       odf_push_text_p (state, TRUE);

       if (state->text_p_for_cell.content_is_simple) {
		int max_cols = gnm_sheet_get_max_cols (state->pos.sheet);
		int max_rows = gnm_sheet_get_max_rows (state->pos.sheet);

		if (state->pos.eval.col >= max_cols ||
		    state->pos.eval.row >= max_rows)
			return;

		state->curr_cell = sheet_cell_fetch (state->pos.sheet,
						     state->pos.eval.col,
						     state->pos.eval.row);

		if (VALUE_IS_STRING (state->curr_cell->value)) {
			/* embedded newlines stored as a series of <p> */
			GnmValue *v;
			v = value_new_string_str
				(go_string_new_nocopy
				 (g_strconcat (state->curr_cell->value->v_str.val->str, "\n", NULL)));
			gnm_cell_assign_value (state->curr_cell, v);
		}
       }

}


static void
oo_cell_content_end (GsfXMLIn *xin, GsfXMLBlob *blob)
{
	OOParseState *state = (OOParseState *)xin->user_state;

	if (state->content_is_error) {
		GnmValue *v;
		if (state->curr_cell == NULL) {
			int max_cols = gnm_sheet_get_max_cols (state->pos.sheet);
			int max_rows = gnm_sheet_get_max_rows (state->pos.sheet);

			if (state->pos.eval.col >= max_cols ||
			    state->pos.eval.row >= max_rows)
				return;

			state->curr_cell = sheet_cell_fetch (state->pos.sheet,
						 state->pos.eval.col,
						 state->pos.eval.row);
		}
		v = value_new_error (NULL, xin->content->str);
		gnm_cell_assign_value (state->curr_cell, v);
	} else if (state->text_p_for_cell.content_is_simple) {
		odf_text_content_end (xin, blob);
		if (state->text_p_for_cell.gstr)
			oo_add_text_to_cell (state, state->text_p_for_cell.gstr->str, state->text_p_for_cell.attrs);
	}
	odf_pop_text_p (state);
}



static void
oo_cell_content_link (GsfXMLIn *xin, xmlChar const **attrs)
{
	OOParseState *state = (OOParseState *)xin->user_state;
	char const *href = NULL;
	char const *tip = NULL;
	GnmHLink *hlink = NULL;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_XLINK, "href"))
			href = CXML2C (attrs[1]);
		else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_OFFICE, "title"))
			tip = CXML2C (attrs[1]);

	if (href != NULL) {
		GnmStyle *style;
		GType type;
		char *link_text = NULL;

		if (g_str_has_prefix (href, "http"))
			type = gnm_hlink_url_get_type ();
		else if (g_str_has_prefix (href, "mail"))
			type = gnm_hlink_email_get_type ();
		else if (g_str_has_prefix (href, "file"))
			type = gnm_hlink_external_get_type ();
		else {
			char *dot;
			type = gnm_hlink_cur_wb_get_type ();
			if (href[0] == '#')
				href++;
			link_text = g_strdup (href);

			// Switch to Sheet!A1 format quick'n'dirty.
			dot = strchr (link_text, '.');
			if (dot)
				*dot = '!';
		}

		if (!link_text)
			link_text = g_strdup (href);

		hlink = gnm_hlink_new (type, state->pos.sheet);
		gnm_hlink_set_target (hlink, link_text);
		gnm_hlink_set_tip (hlink, tip);
		style = gnm_style_new ();
		gnm_style_set_hlink (style, hlink);
		gnm_style_set_font_uline (style, UNDERLINE_SINGLE);
		gnm_style_set_font_color (style, gnm_color_new_go (GO_COLOR_BLUE));
		sheet_style_apply_pos (state->pos.sheet,
				       state->pos.eval.col, state->pos.eval.row,
				       style);
		g_free (link_text);
	}
}

static void
oo_covered_cell_start (GsfXMLIn *xin, xmlChar const **attrs)
{
	OOParseState *state = (OOParseState *)xin->user_state;

	state->col_inc = 1;
	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (oo_attr_int_range (xin, attrs, OO_NS_TABLE, "number-columns-repeated",
				       &state->col_inc, 0, INT_MAX - state->pos.eval.col))
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
	double distance = 0., len_dot1 = 0., len_dot2 = 0.;
	int n_dots1 = 0, n_dots2 = 2;
	gboolean found_percent;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_DRAW, "name"))
			name = CXML2C (attrs[1]);
		else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_DRAW, "style"))
			/* rect or round, ignored */;
		else if (oo_attr_percent_or_distance (xin, attrs, OO_NS_DRAW, "distance",
							      &distance, &found_percent));
		else if (oo_attr_percent_or_distance (xin, attrs, OO_NS_DRAW, "dots1-length",
							      &len_dot1, &found_percent));
		else if (oo_attr_percent_or_distance (xin, attrs, OO_NS_DRAW, "dots2-length",
							      &len_dot2, &found_percent));
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
	else if ( n_dots2 == 1 && n_dots1 == 1) {
		double max = (len_dot1 < len_dot2) ? len_dot2 : len_dot1;
		if (max > 7.5)
			t = GO_LINE_DASH_DOT;
		else
			t = GO_LINE_S_DASH_DOT;
	} else {
		double max = (len_dot1 < len_dot2) ? len_dot2 : len_dot1;
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
			GdkRGBA rgba;
			if (gdk_rgba_parse (&rgba, CXML2C (attrs[1])))
				go_color_from_gdk_rgba (&rgba, &info->from);
			else
				oo_warning (xin, _("Unable to parse gradient color: %s"), CXML2C (attrs[1]));
		} else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_DRAW, "end-color")) {
			GdkRGBA rgba;
			if (gdk_rgba_parse (&rgba, CXML2C (attrs[1])))
				go_color_from_gdk_rgba (&rgba, &info->to);
			else
				oo_warning (xin, _("Unable to parse gradient color: %s"), CXML2C (attrs[1]));
		} else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_DRAW, "style"))
			style = CXML2C (attrs[1]);
		else if (oo_attr_double (xin, attrs, OO_GNUM_NS_EXT,
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
	double distance = -1.0;
	int angle = 0;
	char const *style = NULL;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_DRAW, "color")) {
			GdkRGBA rgba;
			if (gdk_rgba_parse (&rgba, CXML2C (attrs[1])))
				go_color_from_gdk_rgba (&rgba, &hatch->fore);
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

static void odf_style_set_align_h (GnmStyle *style, gint h_align_is_valid, gboolean repeat_content,
				   int text_align, int gnm_halign);

static void
odf_free_cur_style (OOParseState *state)
{
	switch (state->cur_style.type) {
	case OO_STYLE_CELL :
		if (state->cur_style.cells != NULL) {
			odf_style_set_align_h (state->cur_style.cells->style,
					       state->h_align_is_valid,
					       state->repeat_content,
					       state->text_align, state->gnm_halign);
			odf_oo_cell_style_unref (state->cur_style.cells);
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
			oo_sheet_style_free (state->cur_style.sheets);
		state->cur_style.sheets = NULL;
		break;
	case OO_STYLE_CHART :
	case OO_STYLE_GRAPHICS :
		if (state->cur_style.requires_disposal)
			oo_chart_style_free (state->chart.cur_graph_style);
		state->chart.cur_graph_style = NULL;
		break;
	case OO_STYLE_TEXT:
		pango_attr_list_unref (state->cur_style.text);
		state->cur_style.text = NULL;
		break;
	default :
		break;
	}
	state->cur_style.type = OO_STYLE_UNKNOWN;
	state->cur_style.requires_disposal = FALSE;
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
	char const *mp_name = NULL;
	char const *parent_name = NULL;
	OOCellStyle *oostyle;
	GOFormat *fmt = NULL;
	int tmp;
	OOChartStyle *cur_style;

	if (state->cur_style.type != OO_STYLE_UNKNOWN)
		odf_free_cur_style (state);

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (oo_attr_enum (xin, attrs, OO_NS_STYLE, "family", style_types, &tmp))
			state->cur_style.type = tmp;
		else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_STYLE, "name"))
			name = CXML2C (attrs[1]);
		else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_STYLE, "master-page-name"))
			mp_name = CXML2C (attrs[1]);
		else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_STYLE, "parent-style-name"))
			parent_name = CXML2C (attrs[1]);
		else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_STYLE, "data-style-name")) {
			GOFormat *tmp = g_hash_table_lookup (state->formats, attrs[1]);
			if (tmp != NULL)
				fmt = tmp;
		}

	switch (state->cur_style.type) {
	case OO_STYLE_TEXT:
		state->cur_style.text = pango_attr_list_new ();
		if (name != NULL)
			g_hash_table_replace (state->styles.text,
					      g_strdup (name), pango_attr_list_ref (state->cur_style.text));

		break;
	case OO_STYLE_CELL:
		oostyle = (parent_name != NULL)
			? g_hash_table_lookup (state->styles.cell, parent_name)
			: NULL;
		if (oostyle)
			state->cur_style.cells = odf_oo_cell_style_copy (oostyle);
		else
			state->cur_style.cells = odf_oo_cell_style_new (NULL);

		state->h_align_is_valid = 0;
		state->repeat_content = FALSE;
		state->text_align = -2;
		state->gnm_halign = -2;

		if (fmt != NULL)
			gnm_style_set_format (state->cur_style.cells->style, fmt);

		if (name != NULL) {
			odf_oo_cell_style_ref (state->cur_style.cells);
			g_hash_table_replace (state->styles.cell,
				g_strdup (name), state->cur_style.cells);
		} else if (0 == strcmp (xin->node->id, "DEFAULT_STYLE")) {
			 if (state->default_style.cells)
				 odf_oo_cell_style_unref (state->default_style.cells);
			 state->default_style.cells = state->cur_style.cells;
			 odf_oo_cell_style_ref (state->cur_style.cells);
		}

		break;

	case OO_STYLE_COL:
		state->cur_style.col_rows = g_new0 (OOColRowStyle, 1);
		state->cur_style.col_rows->size_pts = -1;
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
		state->cur_style.col_rows->size_pts = -1;
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
		state->cur_style.sheets->master_page_name = g_strdup (mp_name);
		if (name)
			g_hash_table_replace (state->styles.sheet,
				g_strdup (name), state->cur_style.sheets);
		else
			state->cur_style.requires_disposal = TRUE;
		break;

	case OO_STYLE_CHART:
	case OO_STYLE_GRAPHICS:
		state->chart.plot_type = OO_PLOT_UNKNOWN;
		cur_style = g_new0(OOChartStyle, 1);
		cur_style->axis_props = NULL;
		cur_style->plot_props = NULL;
		cur_style->style_props = NULL;
		cur_style->other_props = NULL;
		if (fmt != NULL)
			cur_style->fmt = go_format_ref (fmt);
		state->chart.cur_graph_style = cur_style;
		if (name != NULL)
			g_hash_table_replace (state->chart.graph_styles,
					      g_strdup (name),
					      state->chart.cur_graph_style);
		else if (0 == strcmp (xin->node->id, "DEFAULT_STYLE")) {
			if (state->default_style.graphics) {
				oo_warning (xin, _("Duplicate default chart/graphics style encountered."));
				g_free (state->default_style.graphics);
			}
			state->default_style.graphics = state->chart.cur_graph_style;
		} else
			state->cur_style.requires_disposal = TRUE;
		break;
	default:
		break;
	}
}

static void
oo_style_end (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	OOParseState *state = (OOParseState *)xin->user_state;
	odf_free_cur_style (state);
}

static GOFormat *
oo_canonical_format (const char *s)
{
	/*
	 * Quoting certain characters is options and has no functions effect
	 * for the meaning of the format.  However, some formats are recognized
	 * as built-in and others are not.  We therefore apply a simple mapping
	 * to whatever form we prefer.
	 */
	if (g_str_equal (s, "_(* -??_)"))
		s = "_(* \"-\"??_)";

	return go_format_new_from_XL (s);
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
oo_date_era (G_GNUC_UNUSED GsfXMLIn *xin, G_GNUC_UNUSED xmlChar const **attrs)
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
oo_date_week_of_year (G_GNUC_UNUSED GsfXMLIn *xin, G_GNUC_UNUSED xmlChar const **attrs)
{
}
static void
oo_date_quarter (G_GNUC_UNUSED GsfXMLIn *xin, G_GNUC_UNUSED xmlChar const **attrs)
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
oo_date_am_pm (GsfXMLIn *xin, G_GNUC_UNUSED xmlChar const **attrs)
{
	OOParseState *state = (OOParseState *)xin->user_state;
	gchar const *am_suffix = "AM";
	gchar const *pm_suffix = "PM";

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_GNUM_NS_EXT, "am-suffix"))
			am_suffix = CXML2C (attrs[1]);
		else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_GNUM_NS_EXT, "pm-suffix"))
			pm_suffix = CXML2C (attrs[1]);

	if (strlen (am_suffix) > 2 || (*am_suffix != 'a' && *am_suffix != 'A') ||
	    (*(am_suffix + 1) != 'm' && *(am_suffix + 1) != 'M' && *(am_suffix + 1) != 0))
		am_suffix = "AM";
	if (strlen (pm_suffix) > 2 || (*pm_suffix != 'p' && *pm_suffix != 'P') ||
	    (*(pm_suffix + 1) != 'm' && *(pm_suffix + 1) != 'M' && *(pm_suffix + 1) != 0))
		pm_suffix = "PM";
	if (strlen (am_suffix) != strlen (pm_suffix))
		pm_suffix = am_suffix = "AM";

	if (state->cur_format.accum != NULL) {
		g_string_append (state->cur_format.accum, am_suffix);
		g_string_append_c (state->cur_format.accum, '/');
		g_string_append (state->cur_format.accum, pm_suffix);
	}
}

static void
odf_embedded_text_start (GsfXMLIn *xin, xmlChar const **attrs)
{
	OOParseState *state = (OOParseState *)xin->user_state;

	state->cur_format.offset = 0;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (oo_attr_int (xin, attrs, OO_NS_NUMBER,
				 "position", &(state->cur_format.offset)))
			;
}

static void
odf_insert_in_integer (OOParseState *state, const char *str)
{
	gboolean needs_quoting = FALSE;
	const char *p;
	GString *accum = state->cur_format.accum;
	int pos = state->cur_format.offset;

	g_return_if_fail (pos >= 0 && pos < (int)accum->len);

	/*
	 * We want to insert str in front of the state->cur_format.offset's
	 * integer digit.  For the moment we assume that we have just an
	 * integer and str does not contain any quotation marks
	 */

	for (p = str; *p; p++) {
		switch (*p) {
		case '-':
		case ' ':
		case '(':
		case ')':
			break;
		default:
			needs_quoting = TRUE;
			break;
		}
	}

	if (needs_quoting) {
		g_string_insert (accum, accum->len - pos, "\"\"");
		g_string_insert (accum, accum->len - pos - 1, str);
	} else {
		g_string_insert (accum, accum->len - pos, str);
	}
}

static void
odf_embedded_text_end (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	OOParseState *state = (OOParseState *)xin->user_state;

	if (state->cur_format.accum == NULL)
		return;

	odf_insert_in_integer (state, xin->content->str);

	state->cur_format.offset = 0;
}

static void
odf_format_text_start (GsfXMLIn *xin, G_GNUC_UNUSED xmlChar const **attrs)
{
	OOParseState *state = (OOParseState *)xin->user_state;
	state->cur_format.offset = 0;
	state->cur_format.string_opened = FALSE;
}

static void
oo_format_text_append_quoted (OOParseState *state, char const *cnt, int cnt_len)
{
	if (!state->cur_format.string_opened)
		g_string_append_c (state->cur_format.accum, '"');
	state->cur_format.string_opened = TRUE;
	g_string_append_len (state->cur_format.accum, cnt, cnt_len);
}

static void
oo_format_text_append_unquoted (OOParseState *state, char const *cnt, int cnt_len)
{
	if (state->cur_format.string_opened)
		g_string_append_c (state->cur_format.accum, '"');
	state->cur_format.string_opened = FALSE;
	g_string_append_len (state->cur_format.accum, cnt, cnt_len);
}

// 0: must not; 1: must; -1: don't care; -2: escape outside quote
static int
xl_format_quoting_needs (char c, GOFormatFamily fam)
{
	gboolean numeric = (fam != GO_FORMAT_DATE &&
			    fam != GO_FORMAT_TIME &&
			    fam != GO_FORMAT_TEXT);

	switch (c) {
	case '$': case '+': case '(': case ')':
	case ':': case '^': case '\'': case '=':
	case '{': case '}': case '<': case '>':
	case '-': case '!': case '&': case '~':
	case ' ':
		return -1;
	case '"':
		return -2;
	default:
		return 1;

	case '/': case ',':
		return numeric ? 1 : -1;
	case '%':
		return fam == GO_FORMAT_PERCENTAGE ? 0 : 1;
	}
}

static void
oo_format_text_append (OOParseState *state, char const *cnt, int cnt_len,
		       GOFormatFamily fam)
{
	for (; cnt_len > 0; cnt++, cnt_len--) {
		if (fam == GO_FORMAT_PERCENTAGE && *cnt == '%')
			state->cur_format.percent_sign_seen = TRUE;

		switch (xl_format_quoting_needs (*cnt, fam)) {
		case 0:
			oo_format_text_append_unquoted (state, cnt, 1);
			break;
		case 1:
			oo_format_text_append_quoted (state, cnt, 1);
			break;
		case -2:
			oo_format_text_append_unquoted  (state, "\\", 1);
			// Fall through
		case -1:
			g_string_append_c (state->cur_format.accum, *cnt);
			break;
		default:
			g_assert_not_reached ();
		}
	}
}

static void
oo_format_text_end (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	OOParseState *state = (OOParseState *)xin->user_state;
	GOFormatFamily fam = xin->node->user_data.v_int;

	if (state->cur_format.accum == NULL)
		return;

	if (xin->content->len > state->cur_format.offset)
		oo_format_text_append (state, xin->content->str + state->cur_format.offset,
				       xin->content->len - state->cur_format.offset,
				       fam);

	oo_format_text_append_unquoted  (state, "", 0); // Close quoting
	state->cur_format.offset = 0;
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
	state->cur_format.string_opened = FALSE;
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
					     oo_canonical_format (state->cur_format.accum->str));
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
	gboolean pi_scale = FALSE;
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
					     "min-numerator-digits", &min_n_digits, 0, 30)) {}
		else if  (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_GNUM_NS_EXT, "display-factor") &&
			 attr_eq (attrs[1], "pi"))
			pi_scale = TRUE;

	if (!no_int_part && (state->ver_odf < 1.2 || min_i_digits >= 0)) {
		g_string_append_c (state->cur_format.accum, '#');
		odf_go_string_append_c_n (state->cur_format.accum, '0',
					  min_i_digits > 0 ? min_i_digits : 0);
		g_string_append_c (state->cur_format.accum, ' ');
	}
	odf_go_string_append_c_n (state->cur_format.accum, '?', max_d_digits - min_n_digits);
	odf_go_string_append_c_n (state->cur_format.accum, '0', min_n_digits);
	if (pi_scale)
		g_string_append (state->cur_format.accum, " pi");
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
odf_text_content (GsfXMLIn *xin, G_GNUC_UNUSED xmlChar const **attrs)
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
	gboolean decimals_specified = FALSE;
	// double display_factor = 1.;
	int min_i_digits = 1;
	int min_i_chars = 1;

	if (state->cur_format.accum == NULL)
		return;

	/* We are ignoring number:decimal-replacement */

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (oo_attr_bool (xin, attrs, OO_NS_NUMBER, "grouping", &grouping))
			;
		else if (oo_attr_int_range (xin, attrs, OO_NS_NUMBER, "decimal-places", &decimal_places, 0, 30)) {
			decimals_specified = TRUE;
		} /* else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_NUMBER,  */
/* 					       "display-factor")) */
/* 			display_factor = gnm_strto (CXML2C (attrs[1]), NULL); */
		else if (oo_attr_int_range (xin, attrs, OO_NS_NUMBER,
					      "min-integer-digits", &min_i_digits, 0, 30))
			;
		else if (oo_attr_int_range (xin, attrs, OO_GNUM_NS_EXT,
					      "min-integer-chars", &min_i_chars, 0, 30))
			;

	if (decimals_specified || (min_i_digits != 1) || grouping || (min_i_chars > min_i_digits)) {
		if (min_i_chars > min_i_digits) {
			go_format_generate_number_str (state->cur_format.accum, min_i_chars, decimal_places,
						       grouping, FALSE, FALSE, NULL, NULL);
			while (min_i_chars > min_i_digits) {
				/* substitute the left most 0 by ? */
				char *zero = strchr (state->cur_format.accum->str, '0');
				if (zero)
					*zero = '?';
				min_i_chars--;
			}
		} else
			go_format_generate_number_str (state->cur_format.accum, min_i_digits, decimal_places,
						       grouping, FALSE, FALSE, NULL, NULL);

	} else
		g_string_append (state->cur_format.accum, go_format_as_XL (go_format_general ()));
}

static void
odf_scientific (GsfXMLIn *xin, xmlChar const **attrs)
{
	OOParseState *state = (OOParseState *)xin->user_state;
	GOFormatDetails *details;
	gboolean engineering = FALSE;
	gboolean use_literal_E = TRUE;

	if (state->cur_format.accum == NULL)
		return;

	details = go_format_details_new (GO_FORMAT_SCIENTIFIC);
	details->exponent_sign_forced = TRUE;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (oo_attr_bool (xin, attrs, OO_NS_NUMBER, "grouping", &details->thousands_sep)) {}
		else if (oo_attr_int_range (xin, attrs, OO_NS_NUMBER, "decimal-places",
					      &details->num_decimals, 0, 30))
		        ;
		else if (oo_attr_int_range (xin, attrs, OO_NS_NUMBER, "min-integer-digits",
					      &details->min_digits, 0, 30))
			;
		else if (oo_attr_int_range (xin, attrs, OO_NS_NUMBER, "min-exponent-digits",
					      &details->exponent_digits, 0, 30))
			;
		else if (oo_attr_bool (xin, attrs, OO_GNUM_NS_EXT, "forced-exponent-sign",
				       &(details->exponent_sign_forced)))
			;
		else if (oo_attr_bool (xin, attrs, OO_GNUM_NS_EXT, "engineering",
				       &engineering))
			; // engineering format, Gnumeric-style
		else if (oo_attr_int (xin, attrs, OO_NS_LOCALC_EXT, "exponent-interval",
				      &details->exponent_step))
			; // engineering format, Localc-style
		else if (oo_attr_bool (xin, attrs, OO_GNUM_NS_EXT, "literal-E",
				       &use_literal_E));
	if (engineering)
		details->exponent_step = 3;
	details->use_markup = !use_literal_E;
	details->simplify_mantissa = (details->min_digits == 0) && !use_literal_E;
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
		oo_format_text_append_unquoted (state, "$", 1);
		return;
	}
	oo_format_text_append_unquoted (state, "[$", 2);
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
		state->conditions = g_slist_prepend (state->conditions, g_strdup (condition));
		state->cond_formats = g_slist_prepend (state->cond_formats,
						       g_strdup (style_name));
	}
}

static void
odf_format_invisible_text (GsfXMLIn *xin, xmlChar const **attrs)
{
	/* This can only be called inside a fixed text string */
	OOParseState *state = (OOParseState *)xin->user_state;
	char const *cnt = xin->content->str + state->cur_format.offset;
	int cnt_len = xin->content->len - state->cur_format.offset;
	char const *text = NULL;
	GOFormatFamily fam = xin->node->user_data.v_int;

	if (cnt_len == 1) {
		state->cur_format.offset += 1;

	} else if (cnt_len > 1) {
		oo_format_text_append (state, cnt, cnt_len - 1, fam);
		state->cur_format.offset += cnt_len;
	}

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_GNUM_NS_EXT, "char"))
			text = CXML2C (attrs[1]);

	if (text != NULL) {
		oo_format_text_append_unquoted  (state, "_", 1);
		g_string_append (state->cur_format.accum, text);
	}
}

static void
odf_format_repeated_text_end (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	OOParseState *state = (OOParseState *)xin->user_state;

	/* This really only works for a single character. */
	oo_format_text_append_unquoted  (state, "*", 1);
	g_string_append (state->cur_format.accum, xin->content->str);
}

static void
odf_number_color (GsfXMLIn *xin, xmlChar const **attrs)
{
	OOParseState *state = (OOParseState *)xin->user_state;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2) {
		if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_FO, "color")){
			int r, b, g;
			if (3 == sscanf (CXML2C (attrs[1]), "#%2x%2x%2x", &r, &g, &b)) {
				GOColor col = GO_COLOR_FROM_RGB (r, g, b);
				int i = go_format_palette_index_from_color (col);
				char *color = go_format_palette_name_of_index (i);
				g_string_append_c (state->cur_format.accum, '[');
				g_string_append (state->cur_format.accum, color);
				g_string_append_c (state->cur_format.accum, ']');
				g_free (color);
			}
		}
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
	state->cur_format.string_opened = FALSE;
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
odf_cond_to_xl (GsfXMLIn *xin, GString *dst, const char *cond, int part, int parts)
{
	double val;
	const char *oper; /* xl-syntax */
	char *end;
	const char *cond0 = cond;

	while (g_ascii_isspace (*cond))
		cond++;

	if (cond[0] == '>' && cond[1] == '=')
		oper = ">=", cond += 2;
	else if (cond[0] == '>')
		oper = ">", cond++;
	else if (cond[0] == '<' && cond[1] == '=')
		oper = "<=", cond += 2;
	else if (cond[0] == '<' && cond[1] == '>')
		oper = "<>", cond += 2; /* Not standard, see bug 727297 */
	else if (cond[0] == '<')
		oper = "<", cond++;
	else if (cond[0] == '!' && cond[1] == '=')
		oper = "<>", cond += 2; /* surprise! */
	else if (cond[0] == '=')
		oper = "=", cond++;
	else
		goto bad;

	while (g_ascii_isspace (*cond))
		cond++;
	val = go_strtod (cond, &end);
	if (*end != 0 || !go_finite (val))
		goto bad;

	/*
	 * Don't add the default condition.  Note, that on save we cannot store
	 * whether the condition was implicit or not, so just assume it was.
	 */
	if (part <= 2 && val == 0.0) {
		static const char *defaults[3] = { ">", "<", "=" };
		const char *def = (parts == 2 && part == 0)
			? ">="
			: defaults[part];
		if (g_str_equal (oper, def))
			return;
	}

	g_string_append_c (dst, '[');
	g_string_append (dst, oper);
	g_string_append (dst, cond);  /* Copy value in string form */
	g_string_append_c (dst, ']');
	return;

bad:
	oo_warning (xin, _("Corrupted file: invalid number format condition [%s]."), cond0);
}


static void
odf_number_style_end (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	OOParseState *state = (OOParseState *)xin->user_state;

	g_return_if_fail (state->cur_format.accum != NULL);

	if (state->cur_format.percentage && !state->cur_format.percent_sign_seen)
		oo_format_text_append_unquoted (state, "%", 1);
	state->cur_format.percentage = FALSE;

	if (state->cur_format.name == NULL) {
		g_string_free (state->cur_format.accum, TRUE);
		state->cur_format.accum = NULL;
		oo_warning (xin, _("Corrupted file: unnamed number style ignored."));
		return;
	}

	if (state->conditions != NULL) {
		/* We have conditional formats */
		int part = 0, parts = g_slist_length (state->conditions) + 1;
		GSList *lc, *lf;
		GString *accum = g_string_new (NULL);

		/* We added things in opposite order, so reverse now.  */
		lc = state->conditions = g_slist_reverse (state->conditions);
		lf = state->cond_formats = g_slist_reverse (state->cond_formats);

		while (lc && lf) {
			const char *cond = lc->data;
			const char *fmtname = lf->data;
			GOFormat const *fmt = g_hash_table_lookup (state->formats, fmtname);

			odf_cond_to_xl (xin, accum, cond, part, parts);

			if (!fmt) {
				oo_warning (xin, _("This file appears corrupted, required "
						   "formats are missing."));
				fmt = go_format_general ();
			}

			g_string_append (accum, go_format_as_XL (fmt));
			g_string_append_c (accum, ';');
			part++;
			lc = lc->next;
			lf = lf->next;
		}

		if (state->cur_format.accum->len == 0)
			g_string_append (accum, "General");
		else
			g_string_append (accum, state->cur_format.accum->str);

		g_string_free (state->cur_format.accum, TRUE);
		state->cur_format.accum = accum;
	}

	g_hash_table_insert (state->formats, state->cur_format.name,
			     oo_canonical_format (state->cur_format.accum->str));
	g_string_free (state->cur_format.accum, TRUE);
	state->cur_format.accum = NULL;
	state->cur_format.name = NULL;
	g_slist_free_full (state->conditions, g_free);
	state->conditions = NULL;
	g_slist_free_full (state->cond_formats, g_free);
	state->cond_formats = NULL;
}

/*****************************************************************************************************/

static GtkPaperSize *
odf_get_paper_size (double width, double height, gint orient)
{
	GtkPaperSize *size = NULL;
	char *name, *display_name;

	GList *plist = gtk_paper_size_get_paper_sizes (TRUE), *l;

	for (l = plist; l != NULL; l = l->next) {
		GtkPaperSize *n_size = l->data;
		double n_width = gtk_paper_size_get_width (n_size, GTK_UNIT_POINTS);
		double n_height = gtk_paper_size_get_height (n_size, GTK_UNIT_POINTS);
		double w_diff;
		double h_diff;

		if (orient == GTK_PAGE_ORIENTATION_PORTRAIT ||
		    orient == GTK_PAGE_ORIENTATION_REVERSE_PORTRAIT) {
			w_diff = n_width - width;
			h_diff = n_height - height;
		} else {
			w_diff = n_height - width;
			h_diff = n_width - height;
		}

		if (w_diff > -2. && w_diff < 2. && h_diff > -2 && h_diff < 2) {
			size = gtk_paper_size_copy (n_size);
			break;
		}
	}
	g_list_free_full (plist, (GDestroyNotify)gtk_paper_size_free);

	if (size != NULL)
		return size;

	name = g_strdup_printf ("odf_%ix%i", (int)width, (int)height);
	display_name = g_strdup_printf (_("Paper from ODF file: %ipt\xE2\xA8\x89%ipt"), (int)width, (int)height);
	size = gtk_paper_size_new_custom (name, display_name, width, height, GTK_UNIT_POINTS);
	g_free (name);
	g_free (display_name);
	return size;
}

static void
odf_header_properties (GsfXMLIn *xin, xmlChar const **attrs)
{
	OOParseState *state = (OOParseState *)xin->user_state;
	gboolean height_set = FALSE;
	double pts;
	double page_margin;
	GtkPageSetup *gps;

	if (state->print.cur_pi == NULL)
		return;
	gps = gnm_print_info_get_page_setup (state->print.cur_pi);
	page_margin = gtk_page_setup_get_top_margin (gps, GTK_UNIT_POINTS);

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (oo_attr_distance (xin, attrs, OO_NS_SVG, "height", &pts)) {
			print_info_set_edge_to_below_header (state->print.cur_pi, pts + page_margin);
			height_set = TRUE;
		} else if (oo_attr_distance (xin, attrs, OO_NS_FO, "min-height", &pts))
			if (!height_set)
				print_info_set_edge_to_below_header
					(state->print.cur_pi, pts + page_margin);
}

static void
odf_footer_properties (GsfXMLIn *xin, xmlChar const **attrs)
{
	OOParseState *state = (OOParseState *)xin->user_state;
	gboolean height_set = FALSE;
	double pts;
	double page_margin;
	GtkPageSetup *gps;

	if (state->print.cur_pi == NULL)
		return;
	gps = gnm_print_info_get_page_setup (state->print.cur_pi);
	page_margin = gtk_page_setup_get_bottom_margin (gps, GTK_UNIT_POINTS);

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (oo_attr_distance (xin, attrs, OO_NS_SVG, "height", &pts)) {
			print_info_set_edge_to_above_footer (state->print.cur_pi, pts + page_margin);
			height_set = TRUE;
		} else if (oo_attr_distance (xin, attrs, OO_NS_FO, "min-height", &pts))
			if (!height_set)
				print_info_set_edge_to_above_footer
					(state->print.cur_pi, pts + page_margin);

}

static void
odf_page_layout_properties (GsfXMLIn *xin, xmlChar const **attrs)
{
	static OOEnum const centre_type [] = {
		{"none"        , 0},
		{"horizontal"  , 1},
		{"vertical"    , 2},
		{"both"        , 1|2},
		{NULL          , 0},
	};
	static OOEnum const print_order_type [] = {
		{"ltr"  , 0},
		{"ttb"  , 1},
		{NULL   , 0},
	};
	static OOEnum const print_orientation_type [] = {
		{"portrait"  , GTK_PAGE_ORIENTATION_PORTRAIT},
		{"landscape"  , GTK_PAGE_ORIENTATION_LANDSCAPE},
		{NULL   , 0},
	};

	OOParseState *state = (OOParseState *)xin->user_state;
	double pts, height, width;
	gboolean h_set = FALSE, w_set = FALSE;
	GtkPageSetup *gps;
	gint tmp;
	gint orient = GTK_PAGE_ORIENTATION_PORTRAIT;
	gboolean gnm_style_print = FALSE;
	gboolean annotations_at_end = FALSE;
	double scale_to = 1.;
	gint scale_to_x = 0;
	gint scale_to_y = 0;
	GnmPrintInformation *pi = state->print.cur_pi;

	if (pi == NULL)
		return;
	gps = gnm_print_info_get_page_setup (state->print.cur_pi);
	gtk_page_setup_set_orientation (gps, GTK_PAGE_ORIENTATION_PORTRAIT);

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (oo_attr_distance (xin, attrs, OO_NS_FO, "margin-left", &pts))
			gtk_page_setup_set_left_margin (gps, pts, GTK_UNIT_POINTS);
		else if (oo_attr_distance (xin, attrs, OO_NS_FO, "margin-right", &pts))
			gtk_page_setup_set_right_margin (gps, pts, GTK_UNIT_POINTS);
		else if (oo_attr_distance (xin, attrs, OO_NS_FO, "margin-top", &pts))
			gtk_page_setup_set_top_margin (gps, pts, GTK_UNIT_POINTS);
		else if (oo_attr_distance (xin, attrs, OO_NS_FO, "margin-bottom", &pts))
			gtk_page_setup_set_bottom_margin (gps, pts, GTK_UNIT_POINTS);
		else if (oo_attr_distance (xin, attrs, OO_NS_FO, "page-height", &height))
			h_set = TRUE;
		else if (oo_attr_distance (xin, attrs, OO_NS_FO, "page-width", &width))
			w_set = TRUE;
		else if (oo_attr_enum (xin, attrs, OO_NS_STYLE, "table-centering",
				       centre_type, &tmp)) {
			pi->center_horizontally = ((1 & tmp) != 0);
			pi->center_vertically = ((2 & tmp) != 0);
		} else if (oo_attr_enum (xin, attrs, OO_NS_STYLE, "print-page-order",
					 print_order_type, &tmp)) {
			pi->print_across_then_down = (tmp == 0);
		} else if (oo_attr_enum (xin, attrs, OO_NS_STYLE, "print-orientation",
					 print_orientation_type, &orient)) {
			gtk_page_setup_set_orientation (gps, orient);
		} else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]),
					       OO_NS_STYLE, "print")) {
			gchar **items = g_strsplit (CXML2C (attrs[1]), " ", 0);
			gchar **items_c = items;
			pi->print_grid_lines = 0;
			pi->print_titles = 0;
			pi->comment_placement = GNM_PRINT_COMMENTS_NONE;
			for (;items != NULL && *items; items++)
				if (0 == strcmp (*items, "grid"))
					pi->print_grid_lines = 1;
				else if (0 == strcmp (*items, "headers"))
					pi->print_titles = 1;
				else if (0 == strcmp (*items, "annotations"))
					/* ODF does not distinguish AT_END and IN_PLACE */
					pi->comment_placement = GNM_PRINT_COMMENTS_AT_END;
			g_strfreev (items_c);
		} else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]),
					       OO_GNUM_NS_EXT, "style-print")) {
			gchar **items = g_strsplit (CXML2C (attrs[1]), " ", 0);
			gchar **items_c = items;
			gnm_style_print = TRUE;
			pi->print_black_and_white = 0;
			pi->print_as_draft = 0;
			pi->print_even_if_only_styles = 0;
			pi->error_display = GNM_PRINT_ERRORS_AS_DISPLAYED;
			for (;items != NULL && *items; items++)
				if (0 == strcmp (*items, "annotations_at_end"))
					annotations_at_end = TRUE;
				else if (0 == strcmp (*items, "black_n_white"))
					pi->print_black_and_white = 1;
				else if (0 == strcmp (*items, "draft"))
					pi->print_as_draft = 1;
				else if (0 == strcmp (*items, "errors_as_blank"))
					pi->error_display = GNM_PRINT_ERRORS_AS_BLANK;
				else if (0 == strcmp (*items, "errors_as_dashes"))
					pi->error_display = GNM_PRINT_ERRORS_AS_DASHES;
				else if (0 == strcmp (*items, "errors_as_na"))
					pi->error_display = GNM_PRINT_ERRORS_AS_NA;
				else if (0 == strcmp (*items, "print_even_if_only_styles"))
					pi->print_even_if_only_styles = 1;
			g_strfreev (items_c);
		} else if (oo_attr_int_range (xin, attrs, OO_NS_STYLE, "scale-to-pages",
					      &scale_to_x, 1, INT_MAX)) {
			scale_to_y = scale_to_x;
			scale_to = -1.;
		} else if (oo_attr_int_range (xin, attrs, OO_NS_STYLE, "scale-to-X",
					      &scale_to_x, 1, INT_MAX)) {
			scale_to = -1.;
		} else if (oo_attr_int_range (xin, attrs, OO_GNUM_NS_EXT, "scale-to-X",
					      &scale_to_x, 1, INT_MAX)) {
			scale_to = -1.;
		} else if (oo_attr_int_range (xin, attrs, OO_NS_STYLE, "scale-to-Y",
					      &scale_to_y, 1, INT_MAX)) {
			scale_to = -1.;
		} else if (oo_attr_int_range (xin, attrs, OO_GNUM_NS_EXT, "scale-to-Y",
					      &scale_to_y, 1, INT_MAX)) {
			scale_to = -1.;
		} else if (oo_attr_percent (xin, attrs, OO_NS_STYLE, "scale-to", &scale_to))
			;
	if (scale_to < 0) {
		pi->scaling.dim.cols = scale_to_x;
		pi->scaling.dim.rows = scale_to_y;
		pi->scaling.type = PRINT_SCALE_FIT_PAGES;
	} else {
		pi->scaling.type = PRINT_SCALE_PERCENTAGE;
		pi->scaling.percentage.x = pi->scaling.percentage.y = scale_to * 100;
	}

	if (gnm_style_print && pi->comment_placement != GNM_PRINT_COMMENTS_NONE)
		pi->comment_placement = annotations_at_end ? GNM_PRINT_COMMENTS_AT_END :
			GNM_PRINT_COMMENTS_IN_PLACE;

	/* STYLE "writing-mode" is being ignored since we can't store it anywhere atm */

	if (h_set && w_set) {
		GtkPaperSize *size;
		size = odf_get_paper_size (width, height, orient);
		gtk_page_setup_set_paper_size (gps, size);
		gtk_paper_size_free (size);

	}
}

static void
odf_page_layout (GsfXMLIn *xin, xmlChar const **attrs)
{
	OOParseState *state = (OOParseState *)xin->user_state;
	char const *name = NULL;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_STYLE, "name"))
			name = CXML2C (attrs[1]);

	if (name == NULL) {
		oo_warning (xin, _("Missing page layout identifier"));
		name = "Missing page layout identifier";
	}
	state->print.cur_pi = gnm_print_information_new (TRUE);
	g_hash_table_insert (state->styles.page_layouts, g_strdup (name),
			     state->print.cur_pi);
}

static void
odf_page_layout_end (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	OOParseState *state = (OOParseState *)xin->user_state;

	state->print.cur_pi = NULL;
}

static void
odf_header_footer_left (GsfXMLIn *xin, G_GNUC_UNUSED xmlChar const **attrs)
{
	OOParseState *state = (OOParseState *)xin->user_state;
	gboolean display = TRUE;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (oo_attr_bool (xin, attrs, OO_NS_STYLE, "display",
				  &display)) ;

	if (display && !state->hd_ft_left_warned) {
		oo_warning (xin, _("Gnumeric does not support having a different "
				   "style for left pages. This style is ignored."));
		state->hd_ft_left_warned = TRUE;
	}
}

static void
odf_master_page (GsfXMLIn *xin, xmlChar const **attrs)
{
	OOParseState *state = (OOParseState *)xin->user_state;
	char const *name = NULL;
	char const *pl_name = NULL;
	GnmPrintInformation *pi = NULL;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_STYLE, "name"))
			name = CXML2C (attrs[1]);
		else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]),
					     OO_NS_STYLE, "page-layout-name"))
			pl_name = CXML2C (attrs[1]);

	if (pl_name != NULL)
		pi = g_hash_table_lookup (state->styles.page_layouts, pl_name);
	if (pi == NULL) {
		if (state->ver != OOO_VER_1) /* For OOO_VER_1 this may be acceptable */
			oo_warning (xin, _("Master page style without page layout encountered!"));
		state->print.cur_pi = gnm_print_information_new (TRUE);
	} else
		state->print.cur_pi = gnm_print_info_dup (pi);

	if (name == NULL) {
		oo_warning (xin, _("Master page style without name encountered!"));
		name = "Master page style without name encountered!";
	}

	gnm_print_hf_free (state->print.cur_pi->header);
	gnm_print_hf_free (state->print.cur_pi->footer);
	state->print.cur_pi->header = gnm_print_hf_new (NULL, NULL, NULL);
	state->print.cur_pi->footer = gnm_print_hf_new (NULL, NULL, NULL);

	g_hash_table_insert (state->styles.master_pages, g_strdup (name), state->print.cur_pi);
}

static void
odf_master_page_end (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	OOParseState *state = (OOParseState *)xin->user_state;

	state->print.cur_pi = NULL;
	state->print.cur_hf = NULL;
	state->print.cur_hf_format = NULL;
}

static void
odf_header_footer_end (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	OOParseState *state = (OOParseState *)xin->user_state;

	if (state->text_p_stack) {
		oo_text_p_t *ptr = state->text_p_stack->data;
		if (ptr->gstr) {
			g_free (*(state->print.cur_hf_format));
			*(state->print.cur_hf_format) = g_string_free (ptr->gstr, FALSE);
			ptr->gstr = NULL;
		}
	}

	odf_pop_text_p (state);
}

static void
odf_header_footer (GsfXMLIn *xin, xmlChar const **attrs)
{
	OOParseState *state = (OOParseState *)xin->user_state;
	gboolean display = TRUE;
	gdouble margin;
	GtkPageSetup *gps;

	if (state->print.cur_pi == NULL)
		return;
	gps = gnm_print_info_get_page_setup (state->print.cur_pi);

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (oo_attr_bool (xin, attrs, OO_NS_STYLE, "display",
				  &display)) ;
	if (xin->node->user_data.v_int == 0) {
		state->print.cur_hf = state->print.cur_pi->header;
		margin = gtk_page_setup_get_top_margin (gps, GTK_UNIT_POINTS);
		if (display) {
			if (margin >= state->print.cur_pi->edge_to_below_header)
				print_info_set_edge_to_below_header (state->print.cur_pi, margin + 1);
		} else
			print_info_set_edge_to_below_header (state->print.cur_pi, margin);
	} else {
		state->print.cur_hf = state->print.cur_pi->footer;
		margin = gtk_page_setup_get_bottom_margin (gps, GTK_UNIT_POINTS);
		if (display) {
			if (margin >= state->print.cur_pi->edge_to_above_footer)
				print_info_set_edge_to_above_footer (state->print.cur_pi, margin + 1);
		} else
			print_info_set_edge_to_above_footer (state->print.cur_pi, margin);
	}
	state->print.cur_hf_format = &state->print.cur_hf->middle_format;

	odf_push_text_p (state, FALSE);
}

static void
odf_hf_region_end (GsfXMLIn *xin, GsfXMLBlob *blob)
{
	OOParseState *state = (OOParseState *)xin->user_state;

	odf_header_footer_end (xin, blob);
	state->print.cur_hf_format = &state->print.cur_hf->middle_format;
}

static void
odf_hf_region (GsfXMLIn *xin, G_GNUC_UNUSED xmlChar const **attrs)
{
	OOParseState *state = (OOParseState *)xin->user_state;

	if (state->print.cur_hf != NULL)
		switch (xin->node->user_data.v_int) {
		case 0:
			state->print.cur_hf_format = &state->print.cur_hf->left_format;
			break;
		case 1:
			state->print.cur_hf_format = &state->print.cur_hf->middle_format;
			break;
		case 2:
			state->print.cur_hf_format = &state->print.cur_hf->right_format;
			break;
		}
	odf_push_text_p (state, FALSE);
}

static void
odf_hf_item_start (GsfXMLIn *xin)
{
       OOParseState *state = (OOParseState *)xin->user_state;

       if (xin->content->str != NULL && *xin->content->str != 0) {
	       oo_text_p_t *ptr = state->text_p_stack->data;
	       odf_text_p_add_text (state, xin->content->str + ptr->offset);
	       ptr->offset = strlen (xin->content->str);
       }
}

static void
odf_hf_item (GsfXMLIn *xin, char const *item)
{
	OOParseState *state = (OOParseState *)xin->user_state;

	odf_text_p_add_text (state, "&[");
	odf_text_p_add_text (state, item);
	odf_text_p_add_text (state, "]");
}

static void
odf_hf_sheet_name (GsfXMLIn *xin, G_GNUC_UNUSED xmlChar const **attrs)
{
	odf_hf_item_start (xin);
	odf_hf_item (xin, _("TAB"));
}

static void
odf_hf_item_w_data_style (GsfXMLIn *xin, xmlChar const **attrs, char const *item)
{
	OOParseState *state = (OOParseState *)xin->user_state;
	char const *data_style_name = NULL;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_STYLE, "data-style-name"))
			data_style_name = CXML2C (attrs[1]);

	odf_hf_item_start (xin);
	if (data_style_name == NULL)
		odf_hf_item (xin, item);
	else {
		GOFormat const *fmt =
			g_hash_table_lookup (state->formats, data_style_name);
		if (fmt != NULL) {
			char const *fmt_str = go_format_as_XL (fmt);
			char *str = g_strconcat (item, ":", fmt_str, NULL);
			odf_hf_item (xin, str);
			g_free (str);
		}
	}
}

static void
odf_hf_date (GsfXMLIn *xin, xmlChar const **attrs)
{
	odf_hf_item_start (xin);
	odf_hf_item_w_data_style (xin, attrs, _("DATE"));
}

static void
odf_hf_time (GsfXMLIn *xin, xmlChar const **attrs)
{
	odf_hf_item_start (xin);
	odf_hf_item_w_data_style (xin, attrs, _("TIME"));
}

static void
odf_hf_page_number (GsfXMLIn *xin, G_GNUC_UNUSED xmlChar const **attrs)
{
	odf_hf_item_start (xin);
	odf_hf_item (xin, _("PAGE"));
}

static void
odf_hf_page_count (GsfXMLIn *xin, G_GNUC_UNUSED xmlChar const **attrs)
{
	odf_hf_item_start (xin);
	odf_hf_item (xin, _("PAGES"));
}

static void
odf_hf_file (GsfXMLIn *xin, xmlChar const **attrs)
{
	static OOEnum const display_types [] = {
		{ "full",	  0 },
		{ "path",	  1 },
		{ "name", 2 },
		{ "name-and-extension", 2 },
		{ NULL,	0 },
	};
	OOParseState *state = (OOParseState *)xin->user_state;
	int tmp = 2;

	if (state->print.cur_hf_format == NULL)
		return;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (oo_attr_enum (xin, attrs, OO_NS_TEXT, "display", display_types, &tmp)) ;

	odf_hf_item_start (xin);
	switch (tmp) {
	case 0:
		odf_hf_item (xin, _("PATH"));
		odf_text_p_add_text (state, "/");
		odf_hf_item (xin, _("FILE"));
		break;
	case 1:
		odf_hf_item (xin, _("PATH"));
		break;
	default:
	case 2:
		odf_hf_item (xin, _("FILE"));
		break;
	}
}

static void
odf_hf_expression (GsfXMLIn *xin, xmlChar const **attrs)
{
	static OOEnum const display_types [] = {
		{ "none",	  0 },
		{ "formula",	  1 },
		{ "value",        2 },
		{ NULL,	0 },
	};
	OOParseState *state = (OOParseState *)xin->user_state;
	char const *formula = NULL;
	gint tmp = 2;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (oo_attr_enum (xin, attrs, OO_NS_TEXT, "display", display_types, &tmp)) ;
		else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_TEXT, "formula"))
			formula = CXML2C (attrs[1]);

	if (tmp == 0)
		return;

	if (formula == NULL || *formula == '\0') {
		oo_warning (xin, _("Missing expression"));
		return;
	} else {
		/* Since we have no sheets we postpone parsing the expression */
		gchar const *str = odf_string_id (state, formula);
		char *new;
		new = g_strconcat ((tmp == 1) ? "cellt" : "cell", ":", str, NULL);
	        odf_hf_item_start (xin);
		odf_hf_item (xin, new);
		g_free (new);
	}
}

static void
odf_hf_title (GsfXMLIn *xin, G_GNUC_UNUSED xmlChar const **attrs)
{
	        odf_hf_item_start (xin);
	odf_hf_item (xin, _("TITLE"));
}

/*****************************************************************************************************/



static void
oo_set_gnm_border  (G_GNUC_UNUSED GsfXMLIn *xin, GnmStyle *style,
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
	else {
		oo_warning (xin, _("Unknown Gnumeric border style \'%s\' "
				   "encountered."), (char const *)str);
		return;
	}

	old_border = gnm_style_get_border (style, location);
	new_border = gnm_style_border_fetch (border_style,
					     old_border ?
					     style_color_ref(old_border->color)
					     : style_color_black (),
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

		if (color) {
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
		}
		g_free (border_type);
	}
}

static void
odf_style_set_align_h (GnmStyle *style, gint h_align_is_valid, gboolean repeat_content,
		       int text_align, int gnm_halign)
{
	if (repeat_content)
		gnm_style_set_align_h (style, GNM_HALIGN_FILL);
	else switch (h_align_is_valid) {
		case 1:
			if (gnm_halign > -1)
				gnm_style_set_align_h (style, gnm_halign);
			else
				gnm_style_set_align_h (style, (text_align < 0) ? GNM_HALIGN_LEFT : text_align);
			break;
		case 2:
			gnm_style_set_align_h (style, GNM_HALIGN_GENERAL);
			break;
		default:
			break;
		}
}

static void
oo_style_prop_cell (GsfXMLIn *xin, xmlChar const **attrs)
{
	static OOEnum const underline_styles [] = {
		{ "none",	   1 },
		{ "dash",	   2 },
		{ "dot-dash",      2 },
		{ "dot-dot-dash",  2 },
		{ "dotted",        2 },
		{ "long-dash",     2 },
		{ "solid",         3 },
		{ "wave",          4 },
		{ NULL,	0 },
	};
	static OOEnum const underline_types [] = {
		{ "none",	  0 },
		{ "single",	  1 },
		{ "double",       2 },
		{ NULL,	0 },
	};
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
		{ "left",	GNM_HALIGN_LEFT },
		{ "center",	GNM_HALIGN_CENTER },
		{ "end",	GNM_HALIGN_RIGHT },   /* This really depends on the text direction */
		{ "right",	GNM_HALIGN_RIGHT },
		{ "justify",	GNM_HALIGN_JUSTIFY },
		{ "automatic",	GNM_HALIGN_GENERAL },
		{ NULL,	0 },
	};
	static OOEnum const v_alignments [] = {
		{ "bottom",	GNM_VALIGN_BOTTOM },
		{ "top",	GNM_VALIGN_TOP },
		{ "middle",	GNM_VALIGN_CENTER },
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
	GnmColor *color, *gnm_b_color = NULL, *gnm_p_color = NULL;
	int gnm_pattern = 0;
	GnmStyle *style = state->cur_style.cells->style;
	gboolean  btmp;
	int	  tmp;
	double    tmp_f;
	gboolean  v_alignment_is_fixed = FALSE;
	int  strike_through_type = -1, strike_through_style = -1;
	int underline_type = 0;
	int underline_style = 0;
	gboolean underline_bold = FALSE;
	gboolean underline_low = FALSE;

	g_return_if_fail (style != NULL);

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if ((color = oo_attr_color (xin, attrs, OO_NS_FO, "background-color"))) {
			gnm_style_set_back_color (style, color);
			if (color == magic_transparent)
				gnm_style_set_pattern (style, 0);
			else
				gnm_style_set_pattern (style, 1);
		} else if ((color = oo_attr_color (xin, attrs, OO_GNUM_NS_EXT, "background-colour"))) {
			gnm_b_color = color;
		} else if ((color = oo_attr_color (xin, attrs, OO_GNUM_NS_EXT, "pattern-colour"))) {
			gnm_p_color = color;
		} else if (oo_attr_int (xin, attrs, OO_GNUM_NS_EXT, "pattern", &gnm_pattern)) {
		} else if ((color = oo_attr_color (xin, attrs, OO_NS_FO, "color")))
			gnm_style_set_font_color (style, color);
		else if (oo_attr_enum (xin, attrs, OO_NS_STYLE, "cell-protect", protections, &tmp)) {
			gnm_style_set_contents_locked (style, (tmp & 2) != 0);
			gnm_style_set_contents_hidden (style, (tmp & 1) != 0);
		} else if (oo_attr_enum (xin, attrs,
				       (state->ver >= OOO_VER_OPENDOC) ? OO_NS_FO : OO_NS_STYLE,
					 "text-align", h_alignments, &(state->text_align))) {
			/* Note that style:text-align-source, style:text_align, style:repeat-content  */
			/* and gnm:GnmHAlign interact but can appear in any order and arrive from different */
			/* elements, so we can't use local variables                                  */
		} else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_STYLE, "text-align-source")) {
			state->h_align_is_valid = attr_eq (attrs[1], "fix") ? 1 : 2;
		} else if (oo_attr_bool (xin, attrs, OO_NS_STYLE, "repeat-content", &(state->repeat_content))) {
		} else if (oo_attr_int (xin,attrs, OO_GNUM_NS_EXT, "GnmHAlign", &(state->gnm_halign))) {
		}else if (oo_attr_enum (xin, attrs,
				       (state->ver >= OOO_VER_OPENDOC) ? OO_NS_STYLE : OO_NS_FO,
				       "vertical-align", v_alignments, &tmp)) {
			if (tmp != -1) {
				gnm_style_set_align_v (style, tmp);
				v_alignment_is_fixed = TRUE;
			} else if (!v_alignment_is_fixed)
                                /* This should depend on the rotation */
				gnm_style_set_align_v (style, GNM_VALIGN_BOTTOM);
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
		} else if (oo_attr_enum (xin, attrs, OO_NS_STYLE, "text-underline-style",
					 underline_styles, &underline_style)) {
		} else if (oo_attr_enum (xin, attrs, OO_NS_STYLE, "text-underline-type",
					 underline_types, &underline_type)) {
		} else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]),
					       OO_NS_STYLE, "text-underline-width")) {
			underline_bold = attr_eq (attrs[1], "bold");
		} else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]),
					       OO_GNUM_NS_EXT, "text-underline-placement")) {
			underline_low = attr_eq (attrs[1], "low");
		} else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_FO, "font-style"))
			gnm_style_set_font_italic (style, attr_eq (attrs[1], "italic"));
		else if (oo_attr_font_weight (xin, attrs, &tmp))
			gnm_style_set_font_bold (style, tmp >= PANGO_WEIGHT_SEMIBOLD);
		else if (oo_attr_enum (xin, attrs, OO_NS_STYLE, "text-line-through-style",
				       text_line_through_styles, &strike_through_style));
		else if (oo_attr_enum (xin, attrs, OO_NS_STYLE, "text-line-through-type",
				       text_line_through_types, &strike_through_type));
		else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]),
					     OO_NS_STYLE, "text-position")) {
			if (g_str_has_prefix (attrs[1],"super"))
				gnm_style_set_font_script (style, GO_FONT_SCRIPT_SUPER);
			else if (g_str_has_prefix (attrs[1], "sub"))
				gnm_style_set_font_script (style, GO_FONT_SCRIPT_SUB);
			else
				gnm_style_set_font_script (style, GO_FONT_SCRIPT_STANDARD);
		} else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_FO, "margin-left")) {
			tmp_f = 0.;
			oo_parse_distance (xin, attrs[1], "margin-left", &tmp_f);
			gnm_style_set_indent (style, tmp_f);
		}

	if (strike_through_style != -1 || strike_through_type != -1)
		gnm_style_set_font_strike (style, strike_through_style > 0 ||
					   (strike_through_type > 0 &&  strike_through_style == -1));


	if (underline_style > 0) {
		GnmUnderline underline = UNDERLINE_NONE;
		if (underline_style > 1) {
			switch (underline_type) {
			case 0:
				underline = UNDERLINE_NONE;
				break;
			case 2:
				if (underline_low) {
					underline = UNDERLINE_DOUBLE_LOW;
				} else {
					underline = UNDERLINE_DOUBLE;
				}
				break;
			case 1:
			default:
				if (underline_low) {
					underline = underline_bold ? UNDERLINE_DOUBLE_LOW : UNDERLINE_SINGLE_LOW;
				} else {
					underline = underline_bold ? UNDERLINE_DOUBLE : UNDERLINE_SINGLE;
				}
				break;
			}
		}
		gnm_style_set_font_uline (style, underline);
	}

	if (gnm_pattern > 0)
		gnm_style_set_pattern (style, gnm_pattern);
	if (gnm_b_color)
		gnm_style_set_back_color (style, gnm_b_color);
	if (gnm_p_color)
		gnm_style_set_pattern_color (style, gnm_p_color);
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
	double pts;
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
	int tmp_i;
	gboolean tmp_b;

	g_return_if_fail (style != NULL);

	style->visibility = GNM_SHEET_VISIBILITY_VISIBLE;
	style->is_rtl  = FALSE;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (oo_attr_bool (xin, attrs, OO_NS_TABLE, "display", &tmp_b)) {
			if (!tmp_b)
				style->visibility = GNM_SHEET_VISIBILITY_HIDDEN;
		} else if (oo_attr_bool (xin, attrs, OO_GNUM_NS_EXT, "display-formulas",
					 &style->display_formulas)) {
		} else if (oo_attr_bool (xin, attrs, OO_GNUM_NS_EXT, "display-col-header",
					 &tmp_b)) {
			style->hide_col_header = !tmp_b;
		} else if (oo_attr_bool (xin, attrs, OO_GNUM_NS_EXT, "display-row-header",
					 &tmp_b)) {
			style->hide_row_header = !tmp_b;
		} else if (oo_attr_enum (xin, attrs, OO_NS_STYLE, "writing-mode", modes, &tmp_i))
			style->is_rtl = tmp_i;
		else if ((!style->tab_color_set &&
			  /* Gnumeric's version */
			   gsf_xml_in_namecmp (xin, CXML2C (attrs[0]),
					       OO_GNUM_NS_EXT, "tab-color")) ||
			 (!style->tab_color_set &&
			  /* Used by LO 3.3.3 and later */
			  gsf_xml_in_namecmp (xin, CXML2C (attrs[0]),
					      OO_NS_TABLE_OOO, "tab-color")) ||
			 /* For ODF 1.3 etc. */
			(gsf_xml_in_namecmp (xin, CXML2C (attrs[0]),
					     OO_NS_TABLE, "tab-color"))) {
			/* For ODF 1.3 etc. */
			GdkRGBA rgba;
			if (gdk_rgba_parse (&rgba, CXML2C (attrs[1]))) {
				go_color_from_gdk_rgba (&rgba, &style->tab_color);
				style->tab_color_set = TRUE;
			} else
				oo_warning (xin, _("Unable to parse "
						   "tab color \'%s\'"),
					    CXML2C (attrs[1]));
		} else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]),
					       OO_GNUM_NS_EXT,
					       "tab-text-color")) {
			GdkRGBA rgba;
			if (gdk_rgba_parse (&rgba, CXML2C (attrs[1]))) {
				go_color_from_gdk_rgba (&rgba, &style->tab_text_color);
				style->tab_text_color_set = TRUE;
			} else
				oo_warning (xin, _("Unable to parse tab "
						   "text color \'%s\'"),
					    CXML2C (attrs[1]));
		}

}


static void
oo_style_map (GsfXMLIn *xin, xmlChar const **attrs)
{
	OOParseState *state = (OOParseState *)xin->user_state;
	char const *style_name = NULL, *base = NULL;
	char const *condition = NULL;
	OOCellStyle *style = NULL;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_STYLE, "condition"))
			condition = attrs[1];
		else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_STYLE, "apply-style-name"))
			style_name = attrs[1];
		else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_STYLE, "base-cell-address"))
			base = attrs[1];
	if (style_name == NULL || condition == NULL)
		return;

	style = g_hash_table_lookup (state->styles.cell, style_name);
	odf_oo_cell_style_attach_condition(state->cur_style.cells, style, condition, base);
}

static OOProp *
oo_prop_new_double (char const *name, double val)
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
	g_slist_free_full (props, (GDestroyNotify)oo_prop_free);
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
odf_apply_expression (GsfXMLIn *xin, gint dim, GObject *obj, gchar const *expression)
{
	OOParseState *state = (OOParseState *)xin->user_state;
	GnmParsePos pp;
	GOData *data;
	GnmExprTop const *expr;
	parse_pos_init (&pp, state->pos.wb, state->pos.sheet, 0, 0);
	expr = oo_expr_parse_str
		(xin, expression, &pp,
		 GNM_EXPR_PARSE_FORCE_EXPLICIT_SHEET_REFERENCES,
		 FORMULA_OPENFORMULA);
	if (expr != NULL) {
		data = gnm_go_data_scalar_new_expr (state->pos.sheet, expr);
		gog_dataset_set_dim (GOG_DATASET (obj), dim, data, NULL);
	}
}

static void
oo_prop_list_apply_to_axisline (GsfXMLIn *xin, GSList *props, GObject *obj)
{
	GSList *ptr;
	OOProp *prop;
	gchar const *pos_str_expression = NULL;
	gchar const *pos_str_val = NULL;

	oo_prop_list_apply (props, obj);

	for (ptr = props; ptr; ptr = ptr->next) {
		prop = ptr->data;
		if (0 == strcmp ("pos-str-expr", prop->name))
			pos_str_expression = g_value_get_string (&prop->value);
		else if (0 == strcmp ("pos-str-val", prop->name))
			pos_str_val = g_value_get_string (&prop->value);
	}

	if (pos_str_expression)
		odf_apply_expression (xin, 4, obj, pos_str_expression);
	else if (pos_str_val)
		odf_apply_expression (xin, 4, obj, pos_str_val);

}

static void
oo_prop_list_apply_to_axis (GsfXMLIn *xin, GSList *props, GObject *obj)
{
	OOParseState *state = (OOParseState *)xin->user_state;

	GSList *ptr;
	OOProp *prop;
	GOData *data;

	double minimum = go_ninf, maximum = go_pinf;
	double interval_major = 0.;
	double interval_minor_divisor = 0.;
	gchar const *minimum_expression = NULL;
	gchar const *maximum_expression = NULL;
	gboolean logarithmic = FALSE;


	oo_prop_list_apply_to_axisline (xin, props, obj);

	for (ptr = props; ptr; ptr = ptr->next) {
		prop = ptr->data;
		if (0 == strcmp ("minimum", prop->name))
			minimum = g_value_get_double (&prop->value);
		else if (0 == strcmp ("maximum", prop->name))
			maximum = g_value_get_double (&prop->value);
		else if (0 == strcmp ("interval-major", prop->name))
			interval_major = g_value_get_double (&prop->value);
		else if (0 == strcmp ("interval-minor-divisor", prop->name))
			interval_minor_divisor
				= g_value_get_double (&prop->value);
		else if (0 == strcmp ("minimum-expression", prop->name))
			minimum_expression = g_value_get_string (&prop->value);
		else if (0 == strcmp ("maximum-expression", prop->name))
			maximum_expression = g_value_get_string (&prop->value);
		else if (0 == strcmp ("map-name", prop->name))
			logarithmic = (0 == strcmp (g_value_get_string (&prop->value), "Log"));
	}

	gog_axis_set_bounds (GOG_AXIS (obj), minimum, maximum);
	if (minimum_expression)
		odf_apply_expression (xin, 0, obj, minimum_expression);
	if (maximum_expression)
		odf_apply_expression (xin, 1, obj, maximum_expression);

	if (interval_major > 0) {
		data = gnm_go_data_scalar_new_expr
			(state->chart.src_sheet, gnm_expr_top_new_constant
			 (value_new_float(interval_major)));
		gog_dataset_set_dim (GOG_DATASET (obj), 2, data, NULL);
		if (interval_minor_divisor > 1) {
			if (logarithmic)
				data = gnm_go_data_scalar_new_expr
					(state->chart.src_sheet,
					 gnm_expr_top_new_constant
					 (value_new_float (interval_minor_divisor - 1)));
			else
				data = gnm_go_data_scalar_new_expr
					(state->chart.src_sheet,
					 gnm_expr_top_new_constant
					 (value_new_float (interval_major/interval_minor_divisor)));
			gog_dataset_set_dim (GOG_DATASET (obj), 3, data, NULL);
		}
	}
}

static void
oo_chart_style_to_series (GsfXMLIn *xin, OOChartStyle *oostyle, GObject *obj)
{
	GOStyle *style = NULL;

	if (oostyle == NULL)
		return;

	oo_prop_list_apply (oostyle->plot_props, obj);

	style = go_styled_object_get_style (GO_STYLED_OBJECT (obj));
	if (style != NULL) {
		style = go_style_dup (style);
		odf_apply_style_props (xin, oostyle->style_props, style, TRUE);
		go_styled_object_set_style (GO_STYLED_OBJECT (obj), style);
		g_object_unref (style);
	}
}

static void
oo_prop_list_has (GSList *props, gboolean *threed, char const *tag)
{
	GSList *ptr;
	gboolean res;
	for (ptr = props; ptr; ptr = ptr->next) {
		OOProp *prop = ptr->data;
		if (0 == strcmp (prop->name, tag) &&
		    ((res = g_value_get_boolean (&prop->value))))
			*threed = res;
	}
}

static gboolean
oo_style_has_property (OOChartStyle **style, char const *prop, gboolean def)
{
	int i;
	gboolean has_prop = def;
	for (i = 0; i < OO_CHART_STYLE_INHERITANCE; i++)
		if (style[i] != NULL)
			oo_prop_list_has (style[i]->other_props,
					  &has_prop, prop);
	return has_prop;
}

static gboolean
oo_style_has_plot_property (OOChartStyle **style, char const *prop, gboolean def)
{
	int i;
	gboolean has_prop = def;
	for (i = 0; i < OO_CHART_STYLE_INHERITANCE; i++)
		if (style[i] != NULL)
			oo_prop_list_has (style[i]->plot_props,
					  &has_prop, prop);
	return has_prop;
}

static int
odf_scale_initial_angle (int angle)
{
	angle = 90 - angle;
	while (angle < 0)
		angle += 360;

	return (angle % 360);
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
	double ftmp;
	gboolean default_style_has_lines_set = FALSE;
	gboolean draw_stroke_set = FALSE;
	gboolean draw_stroke = FALSE; /* to avoid a warning only */
	gboolean stacked_set = FALSE;
	gboolean stacked_unset = FALSE;
	gboolean overlap_set = FALSE;
	gboolean percentage_set = FALSE;
	gboolean regression_force_intercept_set = FALSE;
	gboolean regression_force_intercept = FALSE;
	double regression_force_intercept_value = 0.;
	char const *interpolation = NULL;
	gboolean local_style = FALSE;


	g_return_if_fail (style != NULL ||
			  state->default_style.cells != NULL);

	if (style == NULL && state->default_style.cells != NULL) {
		local_style = TRUE;
		style = g_new0 (OOChartStyle, 1);
	}


	style->grid = FALSE;
	style->src_in_rows = FALSE;
	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2) {
		if (oo_attr_bool (xin, attrs, OO_NS_CHART, "logarithmic", &btmp)) {
			if (btmp)
				style->axis_props = g_slist_prepend (style->axis_props,
					oo_prop_new_string ("map-name", "Log"));
		} else if (oo_attr_bool (xin, attrs, OO_NS_CHART, "link-data-style-to-source", &btmp)) {
			if (btmp)
				style->other_props = g_slist_prepend
					(style->other_props,
					 oo_prop_new_bool ("ignore-axis-data-style", btmp));
		} else if (oo_attr_bool (xin, attrs, OO_NS_CHART, "vertical", &btmp)) {
			/* This is backwards from my intuition */
			style->plot_props = g_slist_prepend (style->plot_props,
				oo_prop_new_bool ("horizontal", btmp));
			/* This is for BoxPlots */
			style->plot_props = g_slist_prepend (style->plot_props,
				oo_prop_new_bool ("vertical", btmp));
		} else if (oo_attr_bool (xin, attrs, OO_GNUM_NS_EXT, "outliers", &btmp))
			style->plot_props = g_slist_prepend (style->plot_props,
				oo_prop_new_bool ("outliers", btmp));
		else if (oo_attr_bool (xin, attrs, OO_GNUM_NS_EXT, "reverse-direction", &btmp))
			style->axis_props = g_slist_prepend (style->axis_props,
				oo_prop_new_bool ("invert-axis", btmp));
		else if (oo_attr_bool (xin, attrs, OO_NS_CHART,
					 "reverse-direction", &btmp))
			style->axis_props = g_slist_prepend (style->axis_props,
				oo_prop_new_bool ("invert-axis", btmp));
		else if (oo_attr_bool (xin, attrs, OO_GNUM_NS_EXT,
					 "vary-style-by-element", &btmp))
			style->plot_props = g_slist_prepend (style->plot_props,
				oo_prop_new_bool ("vary-style-by-element",
						  btmp));
		else if (oo_attr_bool (xin, attrs, OO_GNUM_NS_EXT,
					 "show-negatives", &btmp))
			style->plot_props = g_slist_prepend (style->plot_props,
				oo_prop_new_bool ("show-negatives", btmp));
		else if (oo_attr_double (xin, attrs, OO_NS_CHART,
					  "minimum", &ftmp))
			style->axis_props = g_slist_prepend (style->axis_props,
				oo_prop_new_double ("minimum", ftmp));
		else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_GNUM_NS_EXT,
					     "chart-minimum-expression"))
			style->axis_props = g_slist_prepend
				(style->axis_props,
				 oo_prop_new_string ("minimum-expression",
						     CXML2C(attrs[1])));
		else if (oo_attr_double (xin, attrs, OO_NS_CHART,
					 "maximum", &ftmp))
			style->axis_props = g_slist_prepend (style->axis_props,
				oo_prop_new_double ("maximum", ftmp));
		else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_GNUM_NS_EXT,
					     "chart-maximum-expression"))
			style->axis_props = g_slist_prepend
				(style->axis_props,
				 oo_prop_new_string ("maximum-expression",
						     CXML2C(attrs[1])));
		else if (oo_attr_double (xin, attrs, OO_NS_CHART,
					 "interval-major", &ftmp))
			style->axis_props = g_slist_prepend (style->axis_props,
				oo_prop_new_double ("interval-major", ftmp));
		else if (oo_attr_double (xin, attrs, OO_NS_CHART,
					 "interval-minor-divisor", &ftmp))
			style->axis_props = g_slist_prepend
				(style->axis_props,
				 oo_prop_new_double ("interval-minor-divisor",
						     ftmp));
		else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_CHART,
					     "axis-position")) {
			if (0 == strcmp (CXML2C(attrs[1]), "start"))
				style->axis_props = g_slist_prepend
					(style->axis_props,
					 oo_prop_new_string ("pos-str",
							     "low"));
			else if (0 == strcmp (CXML2C(attrs[1]), "end"))
				style->axis_props = g_slist_prepend
					(style->axis_props,
					 oo_prop_new_string ("pos-str",
							     "high"));
			else {
				style->axis_props = g_slist_prepend
					(style->axis_props,
					 oo_prop_new_string ("pos-str", "cross"));
				style->axis_props = g_slist_prepend
					(style->axis_props, oo_prop_new_string ("pos-str-val",
										CXML2C(attrs[1])));
			}
		} else if (gsf_xml_in_namecmp  (xin, CXML2C (attrs[0]), OO_GNUM_NS_EXT,
						"axis-position-expression"))
			style->axis_props = g_slist_prepend (style->axis_props,
							     oo_prop_new_string ("pos-str-expr",
										 CXML2C(attrs[1])));
		 else if (oo_attr_double (xin, attrs, OO_GNUM_NS_EXT,
					  "radius-ratio", &ftmp))
			style->plot_props = g_slist_prepend (style->plot_props,
				oo_prop_new_double ("radius-ratio", ftmp));
		else if (oo_attr_percent (xin, attrs, OO_GNUM_NS_EXT,
					    "default-separation", &ftmp))
			style->plot_props = g_slist_prepend (style->plot_props,
				oo_prop_new_double ("default-separation", ftmp));
		else if (oo_attr_int_range (xin, attrs, OO_NS_CHART,
					      "pie-offset", &tmp, 0, 500)) {
			style->plot_props = g_slist_prepend (style->plot_props,
				oo_prop_new_double ("default-separation",
						   tmp/100.));
			style->plot_props = g_slist_prepend (style->plot_props,
				oo_prop_new_double ("separation",
						   tmp/100.));
		} else if (oo_attr_percent (xin, attrs, OO_NS_CHART,
					    "hole-size", &ftmp))
			style->plot_props = g_slist_prepend (style->plot_props,
				oo_prop_new_double ("center-size", ftmp));
		else if (oo_attr_angle (xin, attrs, OO_NS_CHART,
					"angle-offset", &tmp))
			style->plot_props = g_slist_prepend
				(style->plot_props, oo_prop_new_double ("plot-initial-angle",
									(double) odf_scale_initial_angle (tmp)));
		else if (oo_attr_bool (xin, attrs, OO_NS_CHART,
					 "reverse-direction", &btmp))
			style->axis_props = g_slist_prepend (style->axis_props,
				oo_prop_new_bool ("invert-axis", btmp));
		else if (oo_attr_bool (xin, attrs, OO_NS_CHART, "stacked",
					 &btmp)) {
			if (btmp) {
				style->plot_props = g_slist_prepend
					(style->plot_props,
					 oo_prop_new_string ("type",
							     "stacked"));
				stacked_set = TRUE;
			} else
				stacked_unset = TRUE;
		} else if (oo_attr_bool (xin, attrs, OO_NS_CHART, "percentage",
					 &btmp)) {
			if (btmp) {
				style->plot_props = g_slist_prepend
					(style->plot_props,
					oo_prop_new_string ("type",
							    "as_percentage"));
				percentage_set = TRUE;
			}
		} else if (oo_attr_int_range (xin, attrs, OO_NS_CHART,
					      "overlap", &tmp, -150, 150)) {
			style->plot_props = g_slist_prepend (style->plot_props,
				oo_prop_new_int ("overlap-percentage", tmp));
			overlap_set = TRUE;
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
		} else if (oo_attr_distance (xin, attrs, OO_NS_CHART, "symbol-width",
					     &ftmp)) {
			style->style_props = g_slist_prepend
				(style->style_props,
				 oo_prop_new_double ("symbol-width", ftmp));
		} else if (oo_attr_distance (xin, attrs, OO_NS_CHART, "symbol-height",
					     &ftmp)) {
			style->style_props = g_slist_prepend
				(style->style_props,
				 oo_prop_new_double ("symbol-height", ftmp));
		} else if ((interpolation == NULL) &&
			   (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]),
					       OO_NS_CHART, "interpolation") ||
			   gsf_xml_in_namecmp (xin, CXML2C (attrs[0]),
					       OO_GNUM_NS_EXT, "interpolation"))) {
			if (attr_eq (attrs[1], "none"))
				interpolation = "linear";
			else if (attr_eq (attrs[1], "b-spline")) {
				interpolation = "spline";
				oo_warning
					(xin, _("Unknown interpolation type "
						"encountered: \'%s\', using "
						"Bezier cubic spline instead."),
					 CXML2C(attrs[1]));
			} else if (attr_eq (attrs[1], "cubic-spline"))
				interpolation = "odf-spline";
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
		} else if (oo_attr_bool (xin, attrs, OO_GNUM_NS_EXT, "interpolation-skip-invalid", &btmp))
			style->plot_props = g_slist_prepend
				(style->plot_props,
				 oo_prop_new_bool ("interpolation-skip-invalid", btmp));
		else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]),
					       OO_GNUM_NS_EXT, "fill-type"))
			style->plot_props = g_slist_prepend
				(style->plot_props,
				 oo_prop_new_string ("fill-type",
						     CXML2C(attrs[1])));
		else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]),
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
		} else if (oo_attr_percent (xin, attrs, OO_GNUM_NS_EXT,
					    "stroke-color-opacity", &ftmp)) {
			style->style_props = g_slist_prepend
				(style->style_props,
				 oo_prop_new_double ("stroke-color-opacity", ftmp));
		} else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]),
					       OO_GNUM_NS_EXT, "marker-outline-colour")) {
			style->style_props = g_slist_prepend
				(style->style_props,
				 oo_prop_new_string ("marker-outline-colour",
						     CXML2C(attrs[1])));
		} else if (oo_attr_percent (xin, attrs, OO_GNUM_NS_EXT,
					    "marker-outline-colour-opacity", &ftmp)) {
			style->style_props = g_slist_prepend
				(style->style_props,
				 oo_prop_new_double ("marker-outline-colour-opacity", ftmp));
		} else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]),
					       OO_GNUM_NS_EXT, "marker-fill-colour")) {
			style->style_props = g_slist_prepend
				(style->style_props,
				 oo_prop_new_string ("marker-fill-colour",
						     CXML2C(attrs[1])));
		} else if (oo_attr_percent (xin, attrs, OO_GNUM_NS_EXT,
					    "marker-fill-colour-opacity", &ftmp)) {
			style->style_props = g_slist_prepend
				(style->style_props,
				 oo_prop_new_double ("marker-fill-colour-opacity", ftmp));
		} else if (NULL != oo_attr_distance (xin, attrs, OO_NS_SVG,
						     "stroke-width", &ftmp))
			style->style_props = g_slist_prepend
				(style->style_props,
				 oo_prop_new_double ("stroke-width",
						    ftmp));
		else if (NULL != oo_attr_distance (xin, attrs, OO_GNUM_NS_EXT,
						     "stroke-width", &ftmp))
			style->style_props = g_slist_prepend
				(style->style_props,
				 oo_prop_new_double ("gnm-stroke-width",
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
		else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_DRAW, "fill"))
			style->style_props = g_slist_prepend
				(style->style_props,
				 oo_prop_new_string ("fill",
						     CXML2C(attrs[1])));
		else if (oo_attr_bool (xin, attrs, OO_GNUM_NS_EXT, "foreground-solid", &btmp))
			style->style_props = g_slist_prepend
				(style->style_props,
				 oo_prop_new_bool ("gnm-foreground-solid", btmp));
		else if (oo_attr_bool (xin, attrs, OO_GNUM_NS_EXT, "auto-type", &btmp))
			style->style_props = g_slist_prepend
				(style->style_props,
				 oo_prop_new_bool ("gnm-auto-type", btmp));
		else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_DRAW, "fill-color"))
			style->style_props = g_slist_prepend
				(style->style_props,
				 oo_prop_new_string ("fill-color",
						     CXML2C(attrs[1])));
		else if (oo_attr_percent (xin, attrs, OO_NS_DRAW, "opacity", &ftmp))
			style->style_props = g_slist_prepend (style->style_props,
							      oo_prop_new_double ("opacity", ftmp));
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
		else if (oo_attr_bool (xin, attrs, OO_GNUM_NS_EXT, "auto-font",
				       &btmp))
			style->style_props = g_slist_prepend (style->style_props,
				oo_prop_new_bool ("gnm-auto-font", btmp));
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
		else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_FO, "color"))
			style->style_props = g_slist_prepend
				(style->style_props,
				 oo_prop_new_string ("color",
						     CXML2C(attrs[1])));
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
#if HAVE_OO_NS_LOCALC_EXT
		else if (oo_attr_int_range (xin, attrs, OO_NS_LOCALC_EXT,
					      "regression-max-degree", &tmp,
					      1, 100))
			style->other_props = g_slist_prepend
				(style->other_props,
				 oo_prop_new_int ("lo-dims", tmp));
		else if (oo_attr_bool (xin, attrs, OO_NS_LOCALC_EXT, "regression-force-intercept",
				       &regression_force_intercept))
			{
				regression_force_intercept_set = TRUE;
			}
		else if (oo_attr_double (xin, attrs, OO_NS_LOCALC_EXT,
					 "regression-intercept-value", &ftmp))
			regression_force_intercept_value = ftmp;
#endif
		else if (oo_attr_bool (xin, attrs, OO_GNUM_NS_EXT, "regression-affine",
				       &btmp))
			style->other_props = g_slist_prepend (style->other_props,
				oo_prop_new_bool ("affine", btmp));
		else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_GNUM_NS_EXT,
					     "regression-name"))
			style->other_props = g_slist_prepend
				(style->other_props,
				 oo_prop_new_string ("regression-name-expression",
						     CXML2C(attrs[1])));
#if HAVE_OO_NS_LOCALC_EXT
		else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_LOCALC_EXT,
					     "regression-name"))
			style->other_props = g_slist_prepend
				(style->other_props,
				 oo_prop_new_string ("regression-name-constant",
						     CXML2C(attrs[1])));
#endif
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
		else if (oo_attr_distance (xin, attrs, OO_NS_DRAW, "marker-start-width", &ftmp))
			style->other_props = g_slist_prepend
				(style->other_props,
				 oo_prop_new_double ("marker-start-width", ftmp));
		else if (oo_attr_distance (xin, attrs, OO_NS_DRAW, "marker-end-width", &ftmp))
			style->other_props = g_slist_prepend
				(style->other_props,
				 oo_prop_new_double ("marker-end-width", ftmp));
		else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]),
					     OO_NS_FO,
					     "border"))
			style->other_props = g_slist_prepend
				(style->other_props,
				 oo_prop_new_string
				 ("border", CXML2C(attrs[1])));
		else if (oo_attr_bool (xin, attrs, OO_NS_STYLE, "print-content", &btmp))
			style->other_props = g_slist_prepend
				(style->other_props,
				 oo_prop_new_bool ("print-content", btmp));

		else if (oo_attr_bool (xin, attrs, OO_GNUM_NS_EXT, "auto-marker-outline-colour", &btmp))
			style->style_props = g_slist_prepend (style->style_props,
				oo_prop_new_bool ("gnm-auto-marker-outline-colour", btmp));
		else if (oo_attr_bool (xin, attrs, OO_GNUM_NS_EXT, "auto-color", &btmp))
			style->style_props = g_slist_prepend (style->style_props,
				oo_prop_new_bool ("gnm-auto-color", btmp));
		else if (oo_attr_bool (xin, attrs, OO_GNUM_NS_EXT, "auto-marker-fill-colour", &btmp))
			style->style_props = g_slist_prepend (style->style_props,
				oo_prop_new_bool ("gnm-auto-marker-fill-colour", btmp));
		else if (oo_attr_bool (xin, attrs, OO_GNUM_NS_EXT, "auto-dash", &btmp))
			style->style_props = g_slist_prepend (style->style_props,
				oo_prop_new_bool ("gnm-auto-dash", btmp));
		else if (oo_attr_bool (xin, attrs, OO_GNUM_NS_EXT, "auto-width", &btmp))
			style->style_props = g_slist_prepend (style->style_props,
				oo_prop_new_bool ("gnm-auto-width", btmp));
		else if (oo_attr_bool (xin, attrs, OO_NS_CHART, "tick-marks-major-inner", &btmp))
			style->axis_props = g_slist_prepend (style->axis_props,
				oo_prop_new_bool ("major-tick-in", btmp));
		else if (oo_attr_bool (xin, attrs, OO_NS_CHART, "tick-marks-major-outer", &btmp))
			style->axis_props = g_slist_prepend (style->axis_props,
				oo_prop_new_bool ("major-tick-out", btmp));
		else if (oo_attr_bool (xin, attrs, OO_NS_CHART, "tick-marks-minor-inner", &btmp))
			style->axis_props = g_slist_prepend (style->axis_props,
				oo_prop_new_bool ("minor-tick-in", btmp));
		else if (oo_attr_bool (xin, attrs, OO_NS_CHART, "tick-marks-minor-outer", &btmp))
			style->axis_props = g_slist_prepend (style->axis_props,
				oo_prop_new_bool ("minor-tick-out", btmp));
		else if (oo_attr_bool (xin, attrs, OO_NS_CHART, "display-label", &btmp))
			style->axis_props = g_slist_prepend (style->axis_props,
				oo_prop_new_bool ("major-tick-labeled", btmp));
	}

	if (regression_force_intercept_set) {
		btmp = regression_force_intercept && (regression_force_intercept_value == 0);
		style->other_props = g_slist_prepend (style->other_props,
				oo_prop_new_bool ("affine", btmp));
	}

	if ((stacked_set && !overlap_set) ||
	    (percentage_set && !stacked_unset && !overlap_set))
		style->plot_props = g_slist_prepend (style->plot_props,
						     oo_prop_new_int ("overlap-percentage", 100));

	if (!default_style_has_lines_set)
		style->plot_props = g_slist_prepend
			(style->plot_props,
			 oo_prop_new_bool ("default-style-has-lines", draw_stroke_set && draw_stroke));

	if (local_style) {
		/* odf_apply_style_props (xin, style->style_props, state->default_style.cells, TRUE);*/
		/* We should apply the styles to this GnmStyle */
		oo_chart_style_free (style);
	}
}

static void
od_style_prop_text (GsfXMLIn *xin, xmlChar const **attrs)
{
	static OOEnum const style_types [] = {
		{ "normal",	   PANGO_STYLE_NORMAL},
		{ "italic",	   PANGO_STYLE_ITALIC},
		{ "oblique",       PANGO_STYLE_OBLIQUE},
		{ NULL,	0 },
	};
	static OOEnum const underline_styles [] = {
		{ "none",	   1 },
		{ "dash",	   2 },
		{ "dot-dash",      2 },
		{ "dot-dot-dash",  2 },
		{ "dotted",        2 },
		{ "long-dash",     2 },
		{ "solid",         3 },
		{ "wave",          4 },
		{ NULL,	0 },
	};
	static OOEnum const underline_types [] = {
		{ "none",	  0 },
		{ "single",	  1 },
		{ "double",       2 },
		{ NULL,	0 },
	};
	static OOEnum const line_through_styles [] = {
		{ "none",	 0},
		{ "solid",	 1},
		{ "dotted",      2},
		{ "dash",	 3},
		{ "long-dash",   4},
		{ "dot-dash",	 5},
		{ "dot-dot-dash",6},
		{ "wave",        7},
		{ NULL,	0 },
	};

	OOParseState *state = (OOParseState *)xin->user_state;
	PangoAttribute *attr;
	int	  tmp;
	double size = -1.0;
	int underline_type = 0;
	int underline_style = 0;
	gboolean underline_bold = FALSE;
	GnmColor *color;

	g_return_if_fail (state->cur_style.text != NULL);
	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (NULL != oo_attr_distance (xin, attrs, OO_NS_FO, "font-size", &size) && size >= 0.) {
			attr = pango_attr_size_new ((int) gnm_floor (size * PANGO_SCALE + 0.5));
			attr->start_index = 0;
			attr->end_index = 0;
			pango_attr_list_insert (state->cur_style.text, attr);			;
		} else if (oo_attr_font_weight (xin, attrs, &tmp)) {
			attr = pango_attr_weight_new (tmp);
			attr->start_index = 0;
			attr->end_index = 0;
			pango_attr_list_insert (state->cur_style.text, attr);
		} else if (oo_attr_enum (xin, attrs, OO_NS_FO, "font-style", style_types, &tmp)) {
			attr = pango_attr_style_new (tmp);
			attr->start_index = 0;
			attr->end_index = 0;
			pango_attr_list_insert (state->cur_style.text, attr);
		} else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]),
					       OO_NS_STYLE, "text-position")) {
			attr = NULL;
			if (g_str_has_prefix (attrs[1],"super"))
				attr = go_pango_attr_superscript_new (1);
			else if (g_str_has_prefix (attrs[1], "sub"))
				attr = go_pango_attr_subscript_new (1);
			else if (g_str_has_prefix (attrs[1], "0")) {
				attr = go_pango_attr_superscript_new (0);
				attr->start_index = 0;
				attr->end_index = 0;
				pango_attr_list_insert (state->cur_style.text, attr);
				attr = go_pango_attr_subscript_new (0);
			}
			if (attr != NULL) {
				attr->start_index = 0;
				attr->end_index = 0;
				pango_attr_list_insert (state->cur_style.text, attr);
			}
		} else if (oo_attr_enum (xin, attrs, OO_NS_STYLE, "text-underline-style",
					 underline_styles, &underline_style)) {
		} else if (oo_attr_enum (xin, attrs, OO_NS_STYLE, "text-underline-type",
					 underline_types, &underline_type)) {
		} else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]),
					       OO_NS_STYLE, "text-underline-width"))
			underline_bold = attr_eq (attrs[1], "bold");
		else if (oo_attr_enum (xin, attrs, OO_NS_STYLE, "text-line-through-style",
				       line_through_styles, &tmp)) {
			attr = pango_attr_strikethrough_new (tmp > 0);
			attr->start_index = 0;
			attr->end_index = 0;
			pango_attr_list_insert (state->cur_style.text, attr);
		} else if ((color = oo_attr_color (xin, attrs, OO_NS_FO, "color"))) {
			attr = go_color_to_pango (color->go_color, TRUE);
			style_color_unref (color);
			attr->start_index = 0;
			attr->end_index = 0;
			pango_attr_list_insert (state->cur_style.text, attr);
		} else if ((color = oo_attr_color (xin, attrs, OO_NS_FO, "background-color"))) {
			attr = go_color_to_pango (color->go_color, FALSE);
			style_color_unref (color);
			attr->start_index = 0;
			attr->end_index = 0;
			pango_attr_list_insert (state->cur_style.text, attr);
		}

	if (underline_style > 0) {
		PangoUnderline underline;
		if (underline_style == 1)
			underline = PANGO_UNDERLINE_NONE;
		else if (underline_style == 4)
			underline = PANGO_UNDERLINE_ERROR;
		else if (underline_bold)
			underline = PANGO_UNDERLINE_LOW;
		else if (underline_type == 2)
			underline = PANGO_UNDERLINE_DOUBLE;
		else
			underline = PANGO_UNDERLINE_SINGLE;

		attr = 	pango_attr_underline_new (underline);
		attr->start_index = 0;
		attr->end_index = 0;
		pango_attr_list_insert (state->cur_style.text, attr);
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
	case OO_STYLE_TEXT  : od_style_prop_text (xin, attrs); break;
	case OO_STYLE_CHART :
	case OO_STYLE_GRAPHICS :
		od_style_prop_chart (xin, attrs); break;

	default :
		break;
	}
}

static void
oo_named_expr_common (GsfXMLIn *xin, xmlChar const **attrs, gboolean preparse)
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

#if 0
	g_printerr ("%s: %s [sheet=%s] [%s]\n",
		    (preparse ? "preparse" : "parse"),
		    name,
		    state->pos.sheet ? state->pos.sheet->name_unquoted : "-",
		    expr_str);
#endif

	if (preparse) {
		expr_str = "of:=#REF!";
		base_str = NULL;
	}

	if (name && expr_str &&
	    g_str_equal (name, "Print_Area") &&
	    g_str_equal (expr_str, "of:=[.#REF!]")) {
		// Deal with XL nonsense
		expr_str = NULL;
	}

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
			    !gnm_expr_top_get_cellref (texpr)) {
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

			if (*expr_str == 0)
				texpr = gnm_expr_top_new_constant (value_new_error_REF (NULL));
			else
				texpr = oo_expr_parse_str (xin, expr_str,
							   &pp, GNM_EXPR_PARSE_DEFAULT,
							   f_type);
			if (texpr != NULL) {
				pp.sheet = state->pos.sheet;
				if (pp.sheet == NULL && scope != NULL)
					pp.sheet = workbook_sheet_by_name (pp.wb, scope);

				if (preparse) {
					gnm_expr_top_unref (texpr);
					texpr = NULL;
				}

				expr_name_add (&pp, name, texpr, NULL,
					       TRUE, NULL);
			}
		}
	}

	g_free (range_str);
}

static void
oo_named_expr (GsfXMLIn *xin, xmlChar const **attrs)
{
	oo_named_expr_common (xin, attrs, FALSE);
}

static void
oo_db_range_start (GsfXMLIn *xin, xmlChar const **attrs)
{
	OOParseState *state = (OOParseState *)xin->user_state;
	gboolean buttons = FALSE;
	char const *name = NULL;
	GnmExpr const *expr = NULL;
	gchar const *target = NULL;

	g_return_if_fail (state->filter == NULL);

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_TABLE, "target-range-address"))
			target = CXML2C (attrs[1]);
		else if (oo_attr_bool (xin, attrs, OO_NS_TABLE, "display-filter-buttons", &buttons))
			/* ignore this */;
		else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_TABLE, "name"))
			name = CXML2C (attrs[1]);

	if (target) {
		GnmRangeRef ref;
		GnmRange r;
		char const *ptr = oo_cellref_parse
			(&ref.a, target, &state->pos, NULL, 0);
		if (ref.a.sheet != invalid_sheet &&
		    ':' == *ptr &&
		    '\0' == *oo_cellref_parse (&ref.b, ptr+1, &state->pos, NULL, 0) &&
		    ref.b.sheet != invalid_sheet) {
			range_init_rangeref (&r, &ref);
			if (buttons)
				state->filter = gnm_filter_new (ref.a.sheet, &r, TRUE);
			expr = gnm_expr_new_constant
				(value_new_cellrange_r (ref.a.sheet, &r));
		} else
			oo_warning (xin, _("Invalid DB range '%s'"), target);
	}

	/* It appears that OOo likes to use the names it assigned to filters as named-ranges */
	/* This really violates ODF/OpenFormula. So we make sure that there isn't already a named */
	/* expression or range with that name. */
	if (expr != NULL) {
		GnmNamedExpr *nexpr = NULL;
		GnmParsePos   pp;
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
odf_filter_or (GsfXMLIn *xin, G_GNUC_UNUSED xmlChar const **attrs)
{
	oo_warning (xin, _("Gnumeric does not support 'or'-ed autofilter conditions."));
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
		{ "bottom percent",	GNM_FILTER_OP_BOTTOM_N_PERCENT_N },
		{ "bottom values",	GNM_FILTER_OP_BOTTOM_N },
		{ "top percent",	GNM_FILTER_OP_TOP_N_PERCENT_N },
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

		if ((op & GNM_FILTER_OP_TYPE_MASK) == GNM_FILTER_OP_TOP_N) {
			// These have a value, but no data-type
			type = VALUE_FLOAT;
		}

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
		case GNM_FILTER_OP_BOTTOM_N_PERCENT_N:
		case GNM_FILTER_OP_BOTTOM_N:
		case GNM_FILTER_OP_TOP_N_PERCENT:
		case GNM_FILTER_OP_TOP_N_PERCENT_N:
		case GNM_FILTER_OP_TOP_N:
			if (v && VALUE_IS_NUMBER (v))
				cond = gnm_filter_condition_new_bucket (
					0 == (op & GNM_FILTER_OP_BOTTOM_MASK),
					0 == (op & GNM_FILTER_OP_PERCENT_MASK),
					0 == (op & GNM_FILTER_OP_REL_N_MASK),
					value_get_as_float (v));
			break;
		}
		value_release (v);
		if (NULL != cond)
			gnm_filter_set_condition  (state->filter, field_num, cond, FALSE);
	}
}

static void
odf_draw_frame_store_location (OOParseState *state, double *frame_offset, gdouble height, gdouble width)
{
	state->chart.width = width;
	state->chart.height = height;

	state->chart.plot_area_x = 0;
	state->chart.plot_area_y = 0;
	state->chart.plot_area_width = width;
	state->chart.plot_area_height = height;

	/* Column width and row heights are not correct */
	/* yet so we need to save this */
	/* info and adjust later. */
	state->chart.frame_offset[0] = frame_offset[0];
	state->chart.frame_offset[1] = frame_offset[1];
	state->chart.frame_offset[2] = frame_offset[2];
	state->chart.frame_offset[3] = frame_offset[3];
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
	gdouble height = 0., width = 0., x = 0., y = 0., end_x = 0., end_y = 0.;
	GnmExprTop const *texpr = NULL;
	int z = -1;
	GnmSOAnchorMode mode;
	int last_row = gnm_sheet_get_last_row (state->pos.sheet);
	int last_col = gnm_sheet_get_last_col (state->pos.sheet);

	state->chart.name = NULL;
	state->chart.style_name = NULL;

	height = width = x = y = 0.;
	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2){
		if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_SVG, "width"))
			oo_parse_distance (xin, attrs[1], "width", &width);
		else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_SVG, "height"))
			oo_parse_distance (xin, attrs[1], "height", &height);
		else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_SVG, "x"))
			oo_parse_distance (xin, attrs[1], "x", &x);
		else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_SVG, "y"))
			oo_parse_distance (xin, attrs[1], "y", &y);
		else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_TABLE, "end-x"))
			oo_parse_distance (xin, attrs[1], "end-x", &end_x);
		else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_TABLE, "end-y"))
			oo_parse_distance (xin, attrs[1], "end-y", &end_y);
		else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_TABLE, "end-cell-address")) {
			GnmParsePos   pp;
			char *end_str = g_strconcat ("[", CXML2C (attrs[1]), "]", NULL);
			parse_pos_init (&pp, state->pos.wb, NULL, 0, 0);
			texpr = oo_expr_parse_str (xin, end_str, &pp,
						   GNM_EXPR_PARSE_FORCE_EXPLICIT_SHEET_REFERENCES,
						   FORMULA_OPENFORMULA);
			g_free (end_str);
		} else if (oo_attr_int_range (xin,attrs, OO_NS_DRAW, "z-index",
					      &z, 0, G_MAXINT))
			;
		else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_DRAW, "name"))
			state->chart.name = g_strdup (CXML2C (attrs[1]));
		else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_DRAW, "style-name"))
			state->chart.style_name = g_strdup (CXML2C (attrs[1]));
	}

	frame_offset[0] = x;
	frame_offset[1] = y;
	if (state->pos.eval.col >= 0) {
		cell_base.start.col = cell_base.end.col = state->pos.eval.col;
		cell_base.start.row = cell_base.end.row = state->pos.eval.row;

		if (texpr == NULL || (GNM_EXPR_GET_OPER (texpr->expr) != GNM_EXPR_OP_CELLREF)) {
			cell_base.end.col = cell_base.start.col;
			cell_base.end.row = cell_base.start.row;
			frame_offset[2] = width;
			frame_offset[3] = height;
			mode = GNM_SO_ANCHOR_ONE_CELL;

		} else {
			GnmCellRef const *ref = &texpr->expr->cellref.ref;
			cell_base.end.col = ref->col;
			cell_base.end.row = ref->row;
			frame_offset[2] = end_x;
			frame_offset[3] = end_y ;
			mode = GNM_SO_ANCHOR_TWO_CELLS;
		}
		if (texpr)
			gnm_expr_top_unref (texpr);
	} else {
		cell_base.end.col = cell_base.start.col =
			cell_base.end.row = cell_base.start.row = 0; /* actually not needed */
		frame_offset[2] = width;
		frame_offset[3] = height;
		mode = GNM_SO_ANCHOR_ABSOLUTE;
	}

	odf_draw_frame_store_location (state, frame_offset,
				       (height > 0) ? height : go_nan,
				       (width > 0) ? width : go_nan);

	if (cell_base.start.col > last_col || cell_base.start.row > last_row) {
		oo_warning (xin, _("Moving sheet object from column %i and row %i"),
			    cell_base.start.col, cell_base.start.row);
		cell_base.start.col = cell_base.start.row = 0;
		range_ensure_sanity (&cell_base, state->pos.sheet);
	}

	sheet_object_anchor_init (&state->chart.anchor, &cell_base, frame_offset,
				  GOD_ANCHOR_DIR_DOWN_RIGHT, mode);
	state->chart.so = NULL;
	state->chart.z_index = z;
}

static void
od_draw_frame_end_full (GsfXMLIn *xin, gboolean absolute_distance, char const *control_name)
{
	OOParseState *state = (OOParseState *)xin->user_state;

	if (state->chart.so != NULL) {
		/* Column width and row heights are not correct yet, z-index meaningless, */
		/* so we need to save this info and adjust later. */
		object_offset_t *ob_off = g_new (object_offset_t, 1);

		sheet_object_set_anchor (state->chart.so, &state->chart.anchor);
		ob_off->so = state->chart.so;
		ob_off->absolute_distance = absolute_distance;
		ob_off->z_index = state->chart.z_index;
		ob_off->control = g_strdup (control_name);
		ob_off->frame_offset[0] = state->chart.frame_offset[0];
		ob_off->frame_offset[1] = state->chart.frame_offset[1];
		ob_off->frame_offset[2] = state->chart.frame_offset[2];
		ob_off->frame_offset[3] = state->chart.frame_offset[3];
		state->chart_list = g_slist_prepend ( state->chart_list, ob_off);
		if (state->chart.name)
			sheet_object_set_name (state->chart.so, state->chart.name);
		if (state->chart.style_name) {
			OOChartStyle *oostyle = g_hash_table_lookup
				(state->chart.graph_styles, state->chart.style_name);
			if (oostyle != NULL)
				odf_so_set_props (state, oostyle);
		}
		state->chart.so = NULL;
	}
	g_free (state->chart.name);
	state->chart.name = NULL;
	g_free (state->chart.style_name);
	state->chart.style_name = NULL;
}

static void
od_draw_frame_end (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	od_draw_frame_end_full (xin, FALSE, NULL);
}
static void
od_draw_text_frame_end (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	OOParseState *state = (OOParseState *)xin->user_state;
	oo_text_p_t *ptr;

	if (state->text_p_stack != NULL && (NULL != (ptr = state->text_p_stack->data))
	    && ptr->gstr != NULL)
		g_object_set (state->chart.so, "text", ptr->gstr->str, "markup", ptr->attrs, NULL);
	od_draw_frame_end_full (xin, FALSE, NULL);
	odf_pop_text_p (state);
}

static void
odf_line_end (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	OOParseState *state = (OOParseState *)xin->user_state;
	oo_text_p_t *ptr;

	if (state->text_p_stack != NULL && (NULL != (ptr = state->text_p_stack->data))
	    && ptr->gstr != NULL)
		oo_warning (xin, _("Gnumeric's sheet object lines do not support attached text. "
				   "The text \"%s\" has been dropped."), ptr->gstr->str);
	od_draw_frame_end_full (xin, TRUE, NULL);
	odf_pop_text_p (state);
}

static void
od_draw_control_start (GsfXMLIn *xin, xmlChar const **attrs)
{
	OOParseState *state = (OOParseState *)xin->user_state;
	char const *name = NULL;
	char const *style_name = NULL;

	od_draw_frame_start (xin, attrs);

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_DRAW, "control"))
			name = CXML2C (attrs[1]);
		else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_DRAW, "style-name"))
			style_name = CXML2C (attrs[1]);

	if (name != NULL) {
		OOControl *oc = g_hash_table_lookup (state->controls, name);
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
				state->chart.so = g_object_new
					(oc->t, "text", oc->label, NULL);
			} else if (oc->t == sheet_widget_list_get_type () ||
				   oc->t == sheet_widget_combo_get_type ()) {
				state->chart.so = g_object_new
					(oc->t, NULL);
			} else if (oc->t == sheet_widget_button_get_type ()) {
				state->chart.so = g_object_new
					(oc->t, "text", oc->label, NULL);
			} else if (oc->t == sheet_widget_frame_get_type ()) {
				state->chart.so = g_object_new
					(oc->t, "text", oc->label, NULL);
			}
			if (state->chart.so && style_name) {
				OOChartStyle *oostyle = g_hash_table_lookup
					(state->chart.graph_styles, style_name);
				if (oostyle != NULL)
					odf_so_set_props (state, oostyle);
			}
		} else
			oo_warning (xin, "Undefined control '%s' encountered!", name);
	}
	od_draw_frame_end_full (xin, FALSE, name);
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
	int i;

	if (state->chart.so != NULL) {
		if (GNM_IS_SO_GRAPH (state->chart.so))
			/* Only one object per frame! */
			return;
		/* We prefer objects over images etc. */
		/* We probably should figure out though whether */
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

	for (i = 0; i < OO_CHART_STYLE_INHERITANCE; i++)
		state->chart.i_plot_styles[i] = NULL;

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

	odf_free_cur_style (state);

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

	/* We should be saving/protecting some info to avoid it being overwritten. */

	if (state->debug)
		g_print ("START %s\n", name);

	content = gsf_infile_child_by_vname (state->zip, name, "styles.xml", NULL);
	if (content != NULL) {
		GsfXMLInDoc *doc =
			gsf_xml_in_doc_new (get_styles_dtd (),
					    gsf_odf_get_ns ());
		gsf_xml_in_doc_parse (doc, content, state);
		gsf_xml_in_doc_free (doc);
		odf_clear_conventions (state); /* contain references to xin */
		g_object_unref (content);
	}

	content = gsf_infile_child_by_vname (state->zip, name, "content.xml", NULL);
	if (content != NULL) {
		GsfXMLInDoc *doc =
			gsf_xml_in_doc_new (get_dtd (), gsf_odf_get_ns ());
		gsf_xml_in_doc_parse (doc, content, state);
		gsf_xml_in_doc_free (doc);
		odf_clear_conventions (state); /* contain references to xin */
		g_object_unref (content);
	}
	if (state->debug)
		g_print ("END %s\n", name);
	state->object_name = NULL;
	g_free (name);

	odf_free_cur_style (state);

	for (i = 0; i < OO_CHART_STYLE_INHERITANCE; i++)
		state->chart.i_plot_styles[i] = NULL;

	if (go_finite (state->chart.width))
		g_object_set (state->chart.graph, "width-pts", state->chart.width, NULL);
	if (go_finite (state->chart.height))
		g_object_set (state->chart.graph, "height-pts", state->chart.height, NULL);

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
		soi = g_object_new (GNM_SO_IMAGE_TYPE, NULL);
		state->chart.so = GNM_SO (soi);
		sheet_object_image_set_image (soi, "", data, len);
		g_object_unref (input);
		if (state->chart.name != NULL) {
			GOImage *image = NULL;
			g_object_get (G_OBJECT (soi),
				      "image", &image,
				      NULL);
			go_image_set_name (image, state->chart.name);
			g_object_unref (image);
		}
	} else
		oo_warning (xin, _("Unable to load "
				   "the file \'%s\'."),
			    file);

}

static void
od_draw_text_box (GsfXMLIn *xin, G_GNUC_UNUSED xmlChar const **attrs)
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

	odf_push_text_p (state, FALSE);
}

/* oo_chart_title is used both for chart titles and legend titles */
/* 0: title, 1: subtitle, 2:footer, 3: axis */
static void
oo_chart_title (GsfXMLIn *xin, xmlChar const **attrs)
{
	OOParseState *state = (OOParseState *)xin->user_state;

	state->chart.title_expr = NULL;
	state->chart.title_style = NULL;
	state->chart.title_position = NULL;
	state->chart.title_anchor = NULL;
	state->chart.title_manual_pos = TRUE;
	state->chart.title_x = go_nan;
	state->chart.title_y = go_nan;

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
		} else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]),
					       OO_GNUM_NS_EXT, "compass"))
			state->chart.title_position = g_strdup (CXML2C (attrs[1]));
		else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]),
					     OO_GNUM_NS_EXT, "anchor"))
			state->chart.title_anchor = g_strdup (CXML2C (attrs[1]));
		else if (oo_attr_bool (xin, attrs, OO_GNUM_NS_EXT, "is-position-manual",
				       &state->chart.title_manual_pos))
			;
		else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_SVG, "x"))
			oo_parse_distance (xin, attrs[1], "x", &state->chart.title_x);
		else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_SVG, "y"))
			oo_parse_distance (xin, attrs[1], "y", &state->chart.title_y);
	}

	if (!(go_finite (state->chart.title_x) && go_finite (state->chart.title_y)))
		state->chart.title_manual_pos = FALSE;
	if (state->chart.title_position == NULL)
		state->chart.title_position = g_strdup ((xin->node->user_data.v_int == 2) ? "bottom" : "top");

	odf_push_text_p (state, FALSE);
}

static void
oo_chart_title_end (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	OOParseState *state = (OOParseState *)xin->user_state;
	oo_text_p_t *ptr;
	gboolean use_markup = FALSE;

	g_return_if_fail (state->text_p_stack != NULL);
	ptr = state->text_p_stack->data;
	g_return_if_fail (ptr != NULL);

	if (state->chart.title_expr == NULL && ptr->gstr) {
			state->chart.title_expr = gnm_expr_top_new_constant
				(value_new_string_nocopy
				 (go_pango_attrs_to_markup (ptr->attrs, ptr->gstr->str)));
			use_markup = (ptr->attrs != NULL &&
				      !go_pango_attr_list_is_empty (ptr->attrs));
	}

	if (state->chart.title_expr) {
		GOData *data = gnm_go_data_scalar_new_expr
			(state->chart.src_sheet, state->chart.title_expr);
		GogObject *label;
		GogObject *obj;
		gchar const *tag;

		if (state->chart.axis != NULL && xin->node->user_data.v_int == 3) {
			obj = (GogObject *)state->chart.axis;
			tag = "Label";
		} else if (state->chart.legend != NULL) {
			obj = (GogObject *)state->chart.legend;
			tag = "Title";
		} else if (xin->node->user_data.v_int == 0) {
			obj = (GogObject *)state->chart.graph;
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
			GOStyle *style =
				go_styled_object_get_style (GO_STYLED_OBJECT (label));
			if (oostyle && style) {
				style = go_style_dup (style);
				odf_apply_style_props (xin, oostyle->style_props, style, TRUE);
				go_styled_object_set_style (GO_STYLED_OBJECT (label), style);
				g_object_unref (style);
			}
			g_free (state->chart.title_style);
			state->chart.title_style = NULL;
		}
		if (use_markup)
			g_object_set (label, "allow-markup", TRUE, NULL);
		if (xin->node->user_data.v_int != 3) {
			if (state->chart.title_anchor)
				g_object_set (label, "anchor", state->chart.title_anchor, NULL);
			g_object_set (label,
				      "compass", state->chart.title_position,
				      "is-position-manual", state->chart.title_manual_pos,
				      NULL);
		} else
			g_object_set (label,
				      "is-position-manual", state->chart.title_manual_pos,
				      NULL);

		if (state->chart.title_manual_pos) {
			if (go_finite (state->chart.width) && go_finite (state->chart.height)) {
				GogViewAllocation alloc;
				alloc.x = state->chart.title_x / state->chart.width;
				alloc.w = 0;
				alloc.y = state->chart.title_y / state->chart.height;
				alloc.h = 0;

				gog_object_set_position_flags (label, GOG_POSITION_MANUAL, GOG_POSITION_ANY_MANUAL);
				gog_object_set_manual_position (label, &alloc);
			} else {
				g_object_set (label,
					      "is-position-manual", FALSE,
					      NULL);
				oo_warning (xin, _("Unable to determine manual position for a chart component!"));
			}
		}

	}
	g_free (state->chart.title_position);
	state->chart.title_position = NULL;
	g_free (state->chart.title_anchor);
	state->chart.title_anchor = NULL;
	odf_pop_text_p (state);
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
	static OOEnum const types_bar[] = {
		{ "x",	GOG_AXIS_Y },
		{ "y",	GOG_AXIS_X },
		{ "z",	GOG_AXIS_Z },
		{ NULL,	0 },
	};
	static OOEnum const types_contour[] = {
		{ "x",	GOG_AXIS_X },
		{ "y",	GOG_AXIS_Y },
		{ "z",	GOG_AXIS_PSEUDO_3D },
		{ NULL,	0 },
	};
	static OOEnum const types_radar[] = {
		{ "x",	GOG_AXIS_CIRCULAR },
		{ "y",	GOG_AXIS_RADIAL },
		{ NULL,	0 },
	};
	GSList	*axes, *l;

	OOParseState *state = (OOParseState *)xin->user_state;
	OOChartStyle *style = NULL;
	gchar const *style_name = NULL;
	gchar const *chart_name = NULL;
	gchar const *color_map_name = NULL;
	GogAxisType  axis_type;
	int tmp;
	int gnm_id = 0;
	OOEnum const *axes_types;

	switch (state->chart.plot_type) {
	case OO_PLOT_RADAR:
	case OO_PLOT_RADARAREA:
	case OO_PLOT_POLAR:
		axes_types = types_radar;
		break;
	case OO_PLOT_BAR:
		if (oo_style_has_plot_property (state->chart.i_plot_styles, "horizontal", FALSE))
			axes_types = types_bar;
		else
			axes_types = types;
		break;
	case OO_PLOT_CIRCLE:
	case OO_PLOT_RING:
		return;
	case OO_PLOT_XL_CONTOUR:
	case OO_PLOT_CONTOUR:
		if (oo_style_has_property (state->chart.i_plot_styles, "three-dimensional", FALSE))
			axes_types = types;
		else axes_types = types_contour;
		break;
	default:
		axes_types = types;
		break;
	}

	axis_type = GOG_AXIS_UNKNOWN;
	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_CHART, "style-name"))
			style_name = CXML2C (attrs[1]);
		else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_CHART, "name"))
			chart_name = CXML2C (attrs[1]);
		else if (oo_attr_enum (xin, attrs, OO_NS_CHART, "dimension", axes_types, &tmp))
			axis_type = tmp;
		else if (oo_attr_int_range (xin, attrs, OO_GNUM_NS_EXT, "id", &gnm_id, 1, INT_MAX))
		 	;
		else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_GNUM_NS_EXT, "color-map-name"))
			color_map_name = CXML2C (attrs[1]);

	if (gnm_id == 0) {
		switch (axis_type) {
		case GOG_AXIS_X:
			gnm_id = ++(state->chart.x_axis_count);
			break;
		case GOG_AXIS_Y:
			gnm_id = ++(state->chart.y_axis_count);
			break;
		case GOG_AXIS_Z:
			gnm_id = ++(state->chart.z_axis_count);
			break;
		case GOG_AXIS_CIRCULAR:
		case GOG_AXIS_RADIAL:
		case GOG_AXIS_UNKNOWN:
		default:
			gnm_id = 1;
			break;
		}
	}

	axes = gog_chart_get_axes (state->chart.chart, axis_type);
	for (l = axes; NULL != l; l = l->next) {
		if (((unsigned)gnm_id) == gog_object_get_id (GOG_OBJECT (l->data))) {
			state->chart.axis = l->data;
			break;
		}
	}
	g_slist_free (axes);
	if (NULL == state->chart.axis && (axis_type == GOG_AXIS_X || axis_type == GOG_AXIS_Y
					  || axis_type == GOG_AXIS_Z)) {
		GogObject *axis = GOG_OBJECT (g_object_new (GOG_TYPE_AXIS, "type", axis_type, NULL));
		gog_object_add_by_name	 (GOG_OBJECT (state->chart.chart),
					  axis_type == GOG_AXIS_X ? "X-Axis" :
					  (axis_type == GOG_AXIS_Y ? "Y-Axis" : "Z-Axis"), axis);
		axes = gog_chart_get_axes (state->chart.chart, axis_type);
		for (l = axes; NULL != l; l = l->next) {
			if (((unsigned)gnm_id) == gog_object_get_id (GOG_OBJECT (l->data))) {
				state->chart.axis = l->data;
				break;
			}
		}
		g_slist_free (axes);
	}
	if (NULL == state->chart.axis)
		g_print ("Did not find axis with type %i and id %i.\n", axis_type, gnm_id);

	if (NULL != style_name &&
	    NULL != (style = g_hash_table_lookup (state->chart.graph_styles, style_name))) {
		if (NULL != state->chart.axis) {
			GOStyle *gostyle;
			g_object_get (G_OBJECT (state->chart.axis), "style", &gostyle, NULL);

			oo_prop_list_apply_to_axis (xin, style->axis_props,
						    G_OBJECT (state->chart.axis));
			odf_apply_style_props (xin, style->style_props, gostyle, TRUE);
			g_object_unref (gostyle);

			if (style->fmt) {
				gboolean has_prop = FALSE;
				oo_prop_list_has (style->other_props, &has_prop, "ignore-axis-data-style");
				if (!has_prop)
					gog_axis_set_format (GOG_AXIS (state->chart.axis),
							     go_format_ref (style->fmt));
			}
		}

		if (NULL != state->chart.plot && (state->ver == OOO_VER_1))
			oo_prop_list_apply (style->plot_props, G_OBJECT (state->chart.plot));
	}
	if (NULL != chart_name && NULL != state->chart.axis)
		g_hash_table_replace (state->chart.named_axes,
				      g_strdup (chart_name),
				      state->chart.axis);
	if (NULL != color_map_name && NULL != state->chart.axis)
		g_object_set (G_OBJECT(state->chart.axis), "color-map-name", color_map_name, NULL);
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
/* If general_expr, then range contains an expression not necessarily a range. */
static void
oo_plot_assign_dim (GsfXMLIn *xin, xmlChar const *range, int dim_type, char const *dim_name,
		    gboolean general_expr)
{
	OOParseState *state = (OOParseState *)xin->user_state;

	/* force relative to A1, not the containing cell */
	GnmExprTop const *texpr;
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
		if (general_expr) {
			texpr = odf_parse_range_address_or_expr (xin, CXML2C (range));
			if (state->debug)
				g_print ("%d = rangeref (%s) -- general expression\n", dim, range);
		} else {
			char const *range_list = CXML2C (range);
			GnmParsePos pp;
			GnmExprList *args = NULL;
			GnmExpr const *expr;

			parse_pos_init_sheet (&pp, state->pos.sheet);
			while (*range_list != 0) {
				GnmRangeRef ref;
				char const *ptr = oo_rangeref_parse
					(&ref, range_list, &pp, NULL);
				if (ptr == range_list || ref.a.sheet == invalid_sheet) {
					return;
				}
				v = value_new_cellrange (&ref.a, &ref.b, 0, 0);
				expr = gnm_expr_new_constant (v);
				args = gnm_expr_list_append (args, expr);
				range_list = ptr;
				while (*range_list == ' ')
					range_list++;
			}
			if (1 == gnm_expr_list_length (args)) {
				expr = args->data;
				gnm_expr_list_free (args);
			} else
				expr = gnm_expr_new_set (args);
			texpr = gnm_expr_top_new (expr);
			if (state->debug)
				g_print ("%d = rangeref (%s)\n", dim, range);
		}
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
		texpr = gnm_expr_top_new_constant (v);
	}

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
oo_legend_set_position (OOParseState *state)
{
	GogObject *legend = state->chart.legend;

	if (legend == NULL)
		return;

	if (go_finite (state->chart.legend_x) && go_finite (state->chart.legend_y) &&
	    go_finite (state->chart.width) && go_finite (state->chart.height)) {
		GogViewAllocation alloc;
		alloc.x = (state->chart.legend_x - state->chart.plot_area_x) / state->chart.plot_area_width;
		alloc.w = 0;
		alloc.y = (state->chart.legend_y - state->chart.plot_area_y) / state->chart.plot_area_height;
		alloc.h = 0;

		gog_object_set_position_flags (legend, GOG_POSITION_MANUAL, GOG_POSITION_ANY_MANUAL);
		gog_object_set_manual_position (legend, &alloc);
	} else
		gog_object_set_position_flags (legend, state->chart.legend_flag,
					       GOG_POSITION_COMPASS | GOG_POSITION_ALIGNMENT);
}

static gchar const
*odf_find_plot_type (OOParseState *state, OOPlotType *oo_type)
{
	switch (*oo_type) {
	case OO_PLOT_AREA:	return "GogAreaPlot";	break;
	case OO_PLOT_BAR:	return "GogBarColPlot";	break;
	case OO_PLOT_CIRCLE:	return "GogPiePlot";	break;
	case OO_PLOT_LINE:	return "GogLinePlot";	break;
	case OO_PLOT_RADAR:	return "GogRadarPlot";	break;
	case OO_PLOT_RADARAREA: return "GogRadarAreaPlot";break;
	case OO_PLOT_RING:	return "GogRingPlot";	break;
	case OO_PLOT_SCATTER:	return "GogXYPlot";	break;
	case OO_PLOT_STOCK:	return "GogMinMaxPlot";	break;  /* This is not quite right! */
	case OO_PLOT_CONTOUR:
		if (oo_style_has_property (state->chart.i_plot_styles,
						   "three-dimensional", FALSE)) {
			*oo_type = OO_PLOT_SURFACE;
			return "GogSurfacePlot";
		} else
			return "GogContourPlot";
		break;
	case OO_PLOT_BUBBLE:	return "GogBubblePlot"; break;
	case OO_PLOT_GANTT:	return "GogDropBarPlot"; break;
	case OO_PLOT_POLAR:	return "GogPolarPlot"; break;
	case OO_PLOT_XYZ_SURFACE:
		if (oo_style_has_property (state->chart.i_plot_styles,
						   "three-dimensional", FALSE))
			return "GogXYZSurfacePlot";
		else
			return "GogXYZContourPlot";
		break;
	case OO_PLOT_SURFACE: return "GogSurfacePlot"; break;
	case OO_PLOT_SCATTER_COLOUR: return "GogXYColorPlot";	break;
	case OO_PLOT_XL_SURFACE: return "XLSurfacePlot";	break;
	case OO_PLOT_XL_CONTOUR: return "XLContourPlot";	break;
	case OO_PLOT_BOX: return "GogBoxPlot";	break;
	case OO_PLOT_UNKNOWN:
	default:
		return "GogLinePlot";
		/* It is simpler to create a plot than to check that we don't have one */
		 break;
	}
}

static gboolean
oo_prop_list_has_double (GSList *props, double *d, char const *tag)
{
	GSList *ptr;
	for (ptr = props; ptr; ptr = ptr->next) {
		OOProp *prop = ptr->data;
		if (0 == strcmp (prop->name, tag)) {
			*d = g_value_get_double (&prop->value);
			return TRUE;
		}
	}
	return FALSE;
}


static GogPlot *odf_create_plot (OOParseState *state,  OOPlotType *oo_type)
{
	GogPlot *plot;
	gchar const *type;

	type = odf_find_plot_type (state, oo_type);
	plot = gog_plot_new_by_name (type);

	gog_object_add_by_name (GOG_OBJECT (state->chart.chart),
		"Plot", GOG_OBJECT (plot));

	if (state->chart.i_plot_styles[OO_CHART_STYLE_PLOTAREA] != NULL)
		oo_prop_list_apply (state->chart.i_plot_styles[OO_CHART_STYLE_PLOTAREA]->
				    plot_props, G_OBJECT (plot));

	if (0 == strcmp (type, "GogPiePlot") || 0 == strcmp (type, "GogRingPlot")) {
		/* Note we cannot use the oo_prop_list_apply method since series also have a */
		/* initial-angle property */
		double angle = 0.;
		if (!((state->chart.i_plot_styles[OO_CHART_STYLE_PLOTAREA] != NULL) &&
		    oo_prop_list_has_double (state->chart.i_plot_styles[OO_CHART_STYLE_PLOTAREA]->
					     plot_props, &angle, "plot-initial-angle")))
			angle = odf_scale_initial_angle (90);
		g_object_set (plot, "initial-angle", angle, NULL);
	}

	return plot;
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
	xmlChar const   *source_range_str = NULL;
	int label_flags = 0;
	GSList *prop_list = NULL;
	double x = go_nan, y = go_nan, width = go_nan, height = go_nan;

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
		else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_SVG, "x"))
			oo_parse_distance (xin, attrs[1], "x", &x);
		else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_SVG, "y"))
			oo_parse_distance (xin, attrs[1], "y", &y);
		else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_SVG, "width"))
			oo_parse_distance (xin, attrs[1], "width", &width);
		else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_SVG, "height"))
			oo_parse_distance (xin, attrs[1], "height", &height);

	state->chart.src_n_vectors = -1;
	state->chart.src_in_rows = TRUE;
	state->chart.src_abscissa_set = FALSE;
	state->chart.src_label_set = FALSE;
	state->chart.series = NULL;
	state->chart.series_count = 0;
	state->chart.x_axis_count = 0;
	state->chart.y_axis_count = 0;
	state->chart.z_axis_count = 0;
	state->chart.list = NULL;
	state->chart.named_axes = g_hash_table_new_full
		(g_str_hash, g_str_equal,
		(GDestroyNotify) g_free,
		NULL);
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

	state->chart.plot = odf_create_plot (state, &state->chart.plot_type);

	if (go_finite (x) && go_finite (y) &&
	    go_finite (width) && go_finite (height) &&
	    go_finite (state->chart.width) && go_finite (state->chart.height)) {
			GogViewAllocation alloc;
			alloc.x = x / state->chart.width;
			alloc.w = width / state->chart.width;
			alloc.y = y / state->chart.height;
			alloc.h = height / state->chart.height;

			gog_object_set_position_flags (GOG_OBJECT (state->chart.chart),
						       GOG_POSITION_MANUAL, GOG_POSITION_ANY_MANUAL);
			gog_object_set_manual_position (GOG_OBJECT (state->chart.chart), &alloc);
			g_object_set (G_OBJECT (state->chart.chart), "manual-size", "size", NULL);

			state->chart.plot_area_x = x;
			state->chart.plot_area_y = y;
			state->chart.plot_area_width = width;
			state->chart.plot_area_height = height;

			/* Since the plot area has changed we need to fix the legend position */
			oo_legend_set_position (state);
		}

	oo_prop_list_apply (prop_list, G_OBJECT (state->chart.chart));
	oo_prop_list_free (prop_list);

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
		oo_plot_assign_dim (xin, series_addresses->data, GOG_MS_DIM_LOW, NULL, FALSE);
	}
	if (len-- > 0) {
		series_addresses = series_addresses->next;
		oo_plot_assign_dim (xin, series_addresses->data, GOG_MS_DIM_HIGH, NULL, FALSE);
	}
}

static void
oo_plot_area_end (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	OOParseState *state = (OOParseState *)xin->user_state;
	if (state->chart.plot_type == OO_PLOT_STOCK) {
		odf_create_stock_plot (xin);
		g_slist_free_full (state->chart.list, g_free);
		state->chart.list = NULL;
	} else {
		if (state->chart.series_count == 0 && state->chart.series == NULL)
			state->chart.series = gog_plot_new_series (state->chart.plot);
		if (state->chart.series != NULL) {
			oo_plot_assign_dim (xin, NULL, GOG_MS_DIM_VALUES, NULL, FALSE);
			state->chart.series = NULL;
		}
	}
	state->chart.plot = NULL;
	state->chart.i_plot_styles[OO_CHART_STYLE_PLOTAREA] = NULL;
	g_hash_table_destroy (state->chart.named_axes);
	state->chart.named_axes = NULL;
}


static void
oo_plot_series (GsfXMLIn *xin, xmlChar const **attrs)
{
	OOParseState *state = (OOParseState *)xin->user_state;
	xmlChar const *label = NULL;
	OOPlotType plot_type = state->chart.plot_type;
	gboolean ignore_type_change = (state->chart.plot_type == OO_PLOT_SURFACE ||
				       state->chart.plot_type == OO_PLOT_CONTOUR ||
				       state->chart.plot_type == OO_PLOT_XL_SURFACE ||
				       state->chart.plot_type == OO_PLOT_XL_CONTOUR);
	gboolean plot_type_set = FALSE;
	int tmp;
	GogPlot *plot;
	gchar const *cell_range_address = NULL;
	gchar const *cell_range_expression = NULL;
	GogObject *attached_axis = NULL;
	gboolean general_expression;

	if (state->debug)
		g_print ("<<<<< Start\n");

	state->chart.plot_type_default = state->chart.plot_type;
	state->chart.series_count++;
	state->chart.domain_count = 0;
	state->chart.data_pt_count = 0;

	/* Now check the attributes */
	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2) {
		if (oo_attr_enum (xin, attrs, OO_NS_CHART, "class", odf_chart_classes, &tmp)) {
			if (!ignore_type_change) {
				state->chart.plot_type = plot_type = tmp;
				plot_type_set = TRUE;
			}
		} else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_CHART, "values-cell-range-address"))
			cell_range_address = CXML2C (attrs[1]);
		else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_GNUM_NS_EXT, "values-cell-range-expression"))
			cell_range_expression = CXML2C (attrs[1]);
		else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_CHART, "label-cell-address")) {
			if (label == NULL)
				label = attrs[1];
		} else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_GNUM_NS_EXT, "label-cell-expression"))
			label = attrs[1];
		else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]),
					     OO_NS_CHART, "style-name"))
			state->chart.i_plot_styles[OO_CHART_STYLE_SERIES] = g_hash_table_lookup
				(state->chart.graph_styles, CXML2C (attrs[1]));
		else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]),
					     OO_NS_CHART, "attached-axis"))
			attached_axis = g_hash_table_lookup (state->chart.named_axes, CXML2C (attrs[1]));
	}

	if (plot_type_set)
		plot = odf_create_plot (state, &plot_type);
	else
		plot = state->chart.plot;

	general_expression = (NULL != cell_range_expression);

	if (ignore_type_change && !general_expression && state->chart.series_count == 1 && NULL != cell_range_address) {
		/* We need to check whether this is type 2  of the contour/surface plots. */
		GnmExprTop const *texpr;
		texpr = odf_parse_range_address_or_expr (xin, cell_range_address);
		if (NULL != texpr) {
			GnmValue *val = gnm_expr_top_get_range (texpr);
			if (val != NULL) {
				GnmSheetRange r;
				gnm_sheet_range_from_value (&r, val);
				value_release (val);
				if ((range_width (&r.range) == 1) || (range_height (&r.range) == 1)) {
					if (state->chart.plot_type == OO_PLOT_SURFACE)
						plot_type = state->chart.plot_type_default = state->chart.plot_type
							= OO_PLOT_XL_SURFACE;
					else
						plot_type = state->chart.plot_type_default = state->chart.plot_type
							= OO_PLOT_XL_CONTOUR;
					/* We need to get rid of the original state->chart.plot */
					plot = state->chart.plot;
					state->chart.plot= odf_create_plot (state, &state->chart.plot_type);
					gog_object_clear_parent (GOG_OBJECT (plot));
					g_object_unref (G_OBJECT (plot));
					plot = state->chart.plot;
					plot_type_set = TRUE;
				}
			}
			gnm_expr_top_unref (texpr);
		}
	}

	/* Create the series */
	switch (plot_type) {
	case OO_PLOT_STOCK: /* We need to construct the series later. */
		break;
	case OO_PLOT_SURFACE:
	case OO_PLOT_CONTOUR:
		if (state->chart.series == NULL)
			state->chart.series = gog_plot_new_series (plot);
		break;
	case OO_PLOT_XL_SURFACE:
	case OO_PLOT_XL_CONTOUR:
		/*		if (state->chart.series == NULL) */
			state->chart.series = gog_plot_new_series (plot);
		break;
	default:
		if (state->chart.series == NULL) {
			state->chart.series = gog_plot_new_series (plot);
			/* In ODF by default we skip invalid data for interpolation */
			g_object_set (state->chart.series, "interpolation-skip-invalid", TRUE, NULL);
			if (state->chart.cat_expr != NULL) {
				oo_plot_assign_dim
					(xin, state->chart.cat_expr,
					 GOG_MS_DIM_CATEGORIES, NULL, FALSE);
			}
		}
	}

	if (NULL != attached_axis && NULL != plot)
		gog_plot_set_axis (plot, GOG_AXIS (attached_axis));

	if (general_expression)
		cell_range_address = cell_range_expression;

	if (NULL != cell_range_address) {
		switch (plot_type) {
		case OO_PLOT_STOCK:
			state->chart.list = g_slist_append (state->chart.list,
							    g_strdup (cell_range_address));
			break;
		case OO_PLOT_SURFACE:
		case OO_PLOT_CONTOUR:
			{
				GnmExprTop const *texpr;
				texpr = odf_parse_range_address_or_expr (xin, cell_range_address);
				if (NULL != texpr)
					gog_series_set_dim (state->chart.series, 2,
							    gnm_go_data_matrix_new_expr
							    (state->pos.sheet, texpr), NULL);
			}
			break;
		case OO_PLOT_XL_SURFACE:
		case OO_PLOT_XL_CONTOUR:
			{
				GnmExprTop const *texpr;
				texpr = odf_parse_range_address_or_expr (xin, cell_range_address);
				if (NULL != texpr)
					gog_series_set_XL_dim (state->chart.series,
							       GOG_MS_DIM_VALUES,
							       gnm_go_data_vector_new_expr (state->pos.sheet, texpr),
							       NULL);
			}
			break;
		case OO_PLOT_GANTT:
			oo_plot_assign_dim (xin, cell_range_address,
					    (state->chart.series_count % 2 == 1) ? GOG_MS_DIM_START : GOG_MS_DIM_END,
					    NULL, general_expression);
			break;
		case OO_PLOT_BUBBLE:
			oo_plot_assign_dim (xin, cell_range_address, GOG_MS_DIM_BUBBLES, NULL, general_expression);
			break;
		case OO_PLOT_SCATTER_COLOUR:
			oo_plot_assign_dim (xin, cell_range_address, GOG_MS_DIM_EXTRA1, NULL, general_expression);
			break;
		default:
			oo_plot_assign_dim (xin, cell_range_address, GOG_MS_DIM_VALUES, NULL, general_expression);
			break;
		}

	}

	if (label != NULL) {
		GnmExprTop const *texpr = odf_parse_range_address_or_expr (xin, label);
		if (texpr != NULL)
			gog_series_set_name (state->chart.series,
					     GO_DATA_SCALAR (gnm_go_data_scalar_new_expr
							     (state->pos.sheet, texpr)),
					     NULL);
	}
	if (plot_type_set && state->chart.i_plot_styles[OO_CHART_STYLE_SERIES] != NULL)
		oo_prop_list_apply (state->chart.i_plot_styles[OO_CHART_STYLE_SERIES]->
				    plot_props, G_OBJECT (plot));
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
		oo_plot_assign_dim (xin, NULL, GOG_MS_DIM_VALUES, NULL, FALSE);
		state->chart.series = NULL;
		break;
	}
	state->chart.plot_type = state->chart.plot_type_default;
	state->chart.i_plot_styles[OO_CHART_STYLE_SERIES] = NULL;
	if (state->debug)
		g_print (">>>>> end\n");
}

static void
oo_series_domain (GsfXMLIn *xin, xmlChar const **attrs)
{
	OOParseState *state = (OOParseState *)xin->user_state;
	xmlChar const *src = NULL;
	xmlChar const *cell_range_expression = NULL;
	int dim = GOG_MS_DIM_VALUES;
	char const *name = NULL;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_TABLE, "cell-range-address"))
			src = attrs[1];
		else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_GNUM_NS_EXT, "cell-range-expression"))
			cell_range_expression = attrs[1];

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
		/* y-values first, then x-values */
		dim = (state->chart.domain_count == 0) ? GOG_MS_DIM_CATEGORIES : -1;
		break;
	default:
		dim = GOG_MS_DIM_CATEGORIES;
		break;
	}
	oo_plot_assign_dim (xin, (cell_range_expression != NULL) ? cell_range_expression : src, dim, name,
			    cell_range_expression != NULL);
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
					GOStyle *nstyle = go_style_dup (gostyle);
					OOChartStyle *astyle = state->chart.i_plot_styles[OO_CHART_STYLE_PLOTAREA];
					if (astyle != NULL)
						odf_apply_style_props
							(xin, astyle->style_props, nstyle, TRUE);
					astyle = state->chart.i_plot_styles[OO_CHART_STYLE_SERIES];
					if (astyle != NULL)
						odf_apply_style_props
							(xin, astyle->style_props, nstyle, TRUE);
					odf_apply_style_props (xin, style->style_props, nstyle, TRUE);
					g_object_set (element, "style", nstyle, NULL);
					g_object_unref (gostyle);
					g_object_unref (nstyle);
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

	g_return_if_fail (state->chart.regression != NULL);

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
		if (chart_style) {
			GOStyle *style =
				go_styled_object_get_style (GO_STYLED_OBJECT (equation));
			if (style != NULL) {
				style = go_style_dup (style);
				odf_apply_style_props (xin, chart_style->style_props, style, TRUE);
				go_styled_object_set_style (GO_STYLED_OBJECT (equation), style);
				g_object_unref (style);
			}
			/* In the moment we don't need this. */
			/* 		oo_prop_list_apply (chart_style->plot_props, G_OBJECT (equation)); */
		} else
			oo_warning (xin, _("The chart style \"%s\" is not defined!"), style_name);
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
		OOChartStyle *chart_style = g_hash_table_lookup
			(state->chart.graph_styles, style_name);

		if (chart_style) {
			GSList *l;
			GOStyle *style = NULL;
			GogObject *regression;
			gchar const *type_name = "GogLinRegCurve";
			gchar const *regression_name = NULL;
			gchar const *regression_name_c = NULL;
			gboolean write_lo_dims = FALSE;
			GValue *lo_dim = NULL;
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
						 (reg_type, "gnm:polynomial")) {
						type_name = "GogPolynomRegCurve";
						write_lo_dims = TRUE;
					} else if (0 == strcmp
						 (reg_type, "gnm:moving-average"))
						type_name = "GogMovingAvg";
				} else if (0 == strcmp ("regression-name-expression", prop->name)) {
					regression_name = g_value_get_string (&prop->value);
				} else if (0 == strcmp ("regression-name-constant", prop->name)) {
					regression_name_c = g_value_get_string (&prop->value);
				} else if (0 == strcmp ("lo-dims", prop->name)) {
					lo_dim = &prop->value;
				}
			}

			state->chart.regression = regression =
				GOG_OBJECT (gog_trend_line_new_by_name (type_name));
			regression = gog_object_add_by_name (GOG_OBJECT (state->chart.series),
							     "Trend line", regression);
			if (write_lo_dims && lo_dim != NULL)
				g_object_set_property ( G_OBJECT (regression), "dims", lo_dim);
			oo_prop_list_apply (chart_style->other_props, G_OBJECT (regression));

			style = go_styled_object_get_style (GO_STYLED_OBJECT (regression));
			if (style != NULL) {
				style = go_style_dup (style);
				odf_apply_style_props (xin, chart_style->style_props, style, TRUE);
				go_styled_object_set_style (GO_STYLED_OBJECT (regression), style);
				g_object_unref (style);
			}

			if (regression_name != NULL) {
				GnmParsePos pp;
				GOData *data;
				GnmExprTop const *expr;
				parse_pos_init (&pp, state->pos.wb, state->pos.sheet, 0, 0);
				expr = oo_expr_parse_str
					(xin, regression_name, &pp,
					 GNM_EXPR_PARSE_FORCE_EXPLICIT_SHEET_REFERENCES,
					 FORMULA_OPENFORMULA);
				if (expr != NULL) {
					data = gnm_go_data_scalar_new_expr (state->pos.sheet, expr);
					gog_dataset_set_dim (GOG_DATASET (regression), -1, data, NULL);
				}
			} else if (regression_name_c != NULL) {
				GOData *data;
				data = gnm_go_data_scalar_new_expr
					(state->pos.sheet, gnm_expr_top_new_constant
					 (value_new_string (regression_name_c)));
				gog_dataset_set_dim (GOG_DATASET (regression), -1, data, NULL);
			}

			odf_store_data (state, lower_bd, regression, 0);
			odf_store_data (state, upper_bd, regression, 1);
		}
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
		if (chart_style) {
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

			style = go_styled_object_get_style (GO_STYLED_OBJECT (lines));
			if (style != NULL) {
				style = go_style_dup (style);
				odf_apply_style_props (xin, chart_style->style_props, style, TRUE);
				go_styled_object_set_style (GO_STYLED_OBJECT (lines), style);
				g_object_unref (style);
			}
		}
	}
}

static void
oo_series_serieslines (GsfXMLIn *xin, xmlChar const **attrs)
{
	OOParseState *state = (OOParseState *)xin->user_state;
	char const *style_name = NULL;
	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_CHART, "style-name"))
			style_name = CXML2C (attrs[1]);
	if (style_name != NULL) {
		OOChartStyle *chart_style = g_hash_table_lookup (state->chart.graph_styles, style_name);
		GOStyle *style;
		GogObject const *lines;

		lines = gog_object_add_by_name (GOG_OBJECT (state->chart.series), "Series lines", NULL);
		style = go_styled_object_get_style (GO_STYLED_OBJECT (lines));
		if (chart_style && style) {
			style = go_style_dup (style);
			odf_apply_style_props (xin, chart_style->style_props, style, TRUE);
			go_styled_object_set_style (GO_STYLED_OBJECT (lines), style);
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

	state->chart.legend = NULL;
}

static void
odf_chart_set_default_style (GogObject *chart)
{
	GOStyle *style;

	style = go_styled_object_get_style (GO_STYLED_OBJECT (chart));

	style->line.width = -1.0;
	style->line.dash_type = GO_LINE_NONE;

	go_styled_object_style_changed (GO_STYLED_OBJECT (chart));
}

static void
oo_chart (GsfXMLIn *xin, xmlChar const **attrs)
{
	OOParseState *state = (OOParseState *)xin->user_state;
	int tmp;
	OOPlotType type = OO_PLOT_UNKNOWN;
	OOChartStyle	*style = NULL;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (oo_attr_enum (xin, attrs, OO_NS_CHART, "class", odf_chart_classes, &tmp))
			type = tmp;
		else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]),
					     OO_NS_CHART, "style-name"))
			style = g_hash_table_lookup
				(state->chart.graph_styles, CXML2C (attrs[1]));
		else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]),
					     OO_GNUM_NS_EXT, "theme-name")) {
			GValue *val = g_value_init (g_new0 (GValue, 1), G_TYPE_STRING);
			g_value_set_string (val, CXML2C (attrs[0]));
			g_object_set_property (G_OBJECT (state->chart.graph), "theme-name", val);
			g_value_unset (val);
			g_free (val);
		}

	state->chart.plot_type = type;
	state->chart.chart = GOG_CHART (gog_object_add_by_name (
		GOG_OBJECT (state->chart.graph), "Chart", NULL));
	odf_chart_set_default_style (GOG_OBJECT (state->chart.chart));
	state->chart.plot = NULL;
	state->chart.series = NULL;
	state->chart.axis = NULL;
	state->chart.legend = NULL;
	state->chart.cat_expr = NULL;
	if (NULL != style) {
		GSList *ptr;
		state->chart.src_in_rows = style->src_in_rows;
		for (ptr = style->other_props; ptr; ptr = ptr->next) {
			OOProp *prop = ptr->data;
			if (0 == strcmp (prop->name, "border")) {
				double pts = 0.;
				const char *border = g_value_get_string (&prop->value);
				const char *end;

				while (*border == ' ')
					border++;
				end = oo_parse_spec_distance (border, &pts);

				if (end == GINT_TO_POINTER(1) || end == NULL) {
					if (0 == strncmp (border, "thin", 4)) {
						pts = 0.;
						end = border + 4;
					} else if (0 == strncmp (border, "medium", 6)) {
						pts = 1.5;
						end = border + 6;
					} else if (0 == strncmp (border, "thick", 5)) {
						pts = 3.;
						end = border + 5;
					}
				}

				if (end != GINT_TO_POINTER(1) && end != NULL && end > border) {
					/* pts should be valid */
					GOStyle *go_style = go_styled_object_get_style (GO_STYLED_OBJECT (state->chart.chart));
					go_style->line.width = pts;
					go_style->line.dash_type = GO_LINE_SOLID;
					go_styled_object_style_changed (GO_STYLED_OBJECT (state->chart.chart));
				}
			}
		}
	}

	if (type == OO_PLOT_UNKNOWN)
		oo_warning (xin , _("Encountered an unknown chart type, "
				    "trying to create a line plot."));
}

static void
oo_color_scale (GsfXMLIn *xin, G_GNUC_UNUSED xmlChar const **attrs)
{
	OOParseState *state = (OOParseState *)xin->user_state;
	gog_object_add_by_name ((GogObject *)state->chart.chart, "Color-Scale", NULL);
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
	double x = go_nan, y = go_nan;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (oo_attr_enum (xin, attrs, OO_NS_CHART, "legend-position", positions, &tmp))
			pos = tmp;
		else if (oo_attr_enum (xin, attrs, OO_NS_CHART, "legend-align", alignments, &tmp))
			align = tmp;
		else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_CHART, "style-name"))
			style_name = CXML2C (attrs[1]);
		else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_SVG, "x"))
			oo_parse_distance (xin, attrs[1], "x", &x);
		else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_SVG, "y"))
			oo_parse_distance (xin, attrs[1], "y", &y);

	legend = gog_object_add_by_name ((GogObject *)state->chart.chart, "Legend", NULL);
	state->chart.legend = legend;
	if (legend != NULL) {
		GOStyle *style = go_styled_object_get_style (GO_STYLED_OBJECT (legend));
		if (style_name && style) {
			OOChartStyle *chart_style = g_hash_table_lookup
				(state->chart.graph_styles, style_name);
			style = go_style_dup (style);
			if (chart_style)
				odf_apply_style_props (xin, chart_style->style_props, style, TRUE);
			else
				oo_warning (xin, _("Chart style with name '%s' is missing."),
					    style_name);
			go_styled_object_set_style (GO_STYLED_OBJECT (legend), style);
			g_object_unref (style);
		}
		state->chart.legend_x = x;
		state->chart.legend_y = y;
		state->chart.legend_flag = pos | align;

		/* We will need to redo this if we encounter a plot-area specification later */
		oo_legend_set_position (state);
	}
}

static void
oo_chart_grid (GsfXMLIn *xin, xmlChar const **attrs)
{
	OOParseState *state = (OOParseState *)xin->user_state;
	gchar const *style_name = NULL;
	GogObject   *grid = NULL;

	if (state->chart.axis == NULL)
		return;
	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_CHART, "class")) {
			if (attr_eq (attrs[1], "major"))
				grid = gog_object_add_by_name (state->chart.axis, "MajorGrid", NULL);
			else if (attr_eq (attrs[1], "minor"))
				grid = gog_object_add_by_name (state->chart.axis, "MinorGrid", NULL);
		} else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_CHART, "style-name"))
			style_name = CXML2C (attrs[1]);

	if (grid != NULL && style_name != NULL) {
		GOStyle *style = go_styled_object_get_style (GO_STYLED_OBJECT (grid));
		if (style) {
			OOChartStyle *chart_style = g_hash_table_lookup
				(state->chart.graph_styles, style_name);
			style = go_style_dup (style);
			if (chart_style)
				odf_apply_style_props (xin, chart_style->style_props, style, TRUE);
			else
				oo_warning (xin, _("Chart style with name '%s' is missing."),
					    style_name);
			go_styled_object_set_style (GO_STYLED_OBJECT (grid), style);
			g_object_unref (style);
		}
	}
}

static void
oo_chart_wall (GsfXMLIn *xin, xmlChar const **attrs)
{
	OOParseState *state = (OOParseState *)xin->user_state;
	GogObject *backplane;
	gchar const *style_name = NULL;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_CHART, "style-name"))
			style_name = CXML2C (attrs[1]);

	backplane = gog_object_add_by_name (GOG_OBJECT (state->chart.chart), "Backplane", NULL);

	if (style_name != NULL && backplane != NULL) {
		GOStyle *style = go_styled_object_get_style (GO_STYLED_OBJECT (backplane));
		if (style != NULL) {
			OOChartStyle *chart_style = g_hash_table_lookup
				(state->chart.graph_styles, style_name);
			style = go_style_dup (style);
			if (chart_style)
				odf_apply_style_props (xin, chart_style->style_props, style, TRUE);
			else
				oo_warning (xin, _("Chart style with name '%s' is missing."),
					    style_name);
			go_styled_object_set_style (GO_STYLED_OBJECT (backplane), style);
			g_object_unref (style);
		}
	}
}

static void
oo_chart_axisline (GsfXMLIn *xin, xmlChar const **attrs)
{
	OOParseState *state = (OOParseState *)xin->user_state;
	gchar const *style_name = NULL;
	GogObject *axisline;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_CHART, "style-name"))
			style_name = CXML2C (attrs[1]);

	axisline = gog_object_add_by_name (GOG_OBJECT (state->chart.axis), "AxisLine", NULL);

	if (style_name != NULL && axisline != NULL) {
		GOStyle *style = go_styled_object_get_style (GO_STYLED_OBJECT (axisline));
		if (style != NULL) {
			OOChartStyle *chart_style = g_hash_table_lookup
				(state->chart.graph_styles, style_name);
			style = go_style_dup (style);
			if (chart_style) {
				oo_prop_list_apply_to_axisline (xin, chart_style->axis_props,
								G_OBJECT (axisline));
				odf_apply_style_props (xin, chart_style->style_props, style, TRUE);
			} else
				oo_warning (xin, _("Chart style with name '%s' is missing."),
					    style_name);
			go_styled_object_set_style (GO_STYLED_OBJECT (axisline), style);
			g_object_unref (style);
		}
	}
}

static void
oo_chart_style_free (OOChartStyle *cstyle)
{
	if (cstyle == NULL)
		return;
	oo_prop_list_free (cstyle->axis_props);
	oo_prop_list_free (cstyle->style_props);
	oo_prop_list_free (cstyle->plot_props);
	oo_prop_list_free (cstyle->other_props);
	go_format_unref (cstyle->fmt);
	g_free (cstyle);
}

static void
oo_control_free (OOControl *ctrl)
{
	g_free (ctrl->value);
	g_free (ctrl->value_type);
	g_free (ctrl->label);
	g_free (ctrl->current_state);
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
odf_annotation_start (GsfXMLIn *xin, G_GNUC_UNUSED xmlChar const **attrs)
{
	OOParseState *state = (OOParseState *)xin->user_state;

	state->cell_comment = cell_set_comment (state->pos.sheet, &state->pos.eval,
						NULL, NULL, NULL);
	odf_push_text_p (state, FALSE);
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
	oo_text_p_t *ptr;

	if (state->text_p_stack != NULL && (NULL != (ptr = state->text_p_stack->data)))
		g_object_set (G_OBJECT (state->cell_comment),
			      "text", ptr->gstr ? ptr->gstr->str : "",
			      "markup", ptr->attrs, NULL);
	state->cell_comment = NULL;
	odf_pop_text_p (state);
}

/****************************************************************************/
/******************************** graphic sheet objects *********************/

static void
odf_so_set_props (OOParseState *state, OOChartStyle *oostyle)
{
	GSList *l;
	for (l = oostyle->other_props; l != NULL; l = l->next) {
		OOProp *prop = l->data;
		if (0 == strcmp ("print-content", prop->name)) {
			gboolean prop_val;
			prop_val = g_value_get_boolean (&prop->value);
			sheet_object_set_print_flag
				(state->chart.so,
				 &prop_val);
		}
	}
}

static void
odf_so_filled (GsfXMLIn *xin, xmlChar const **attrs, gboolean is_oval)
{
	OOParseState *state = (OOParseState *)xin->user_state;
	char const *style_name = NULL;
	GOStyle *style0;

	od_draw_frame_start (xin, attrs);
	state->chart.so = g_object_new (GNM_SO_FILLED_TYPE,
					"is-oval", is_oval, NULL);
	g_object_get (state->chart.so, "style", &style0, NULL);
	if (style0 != NULL) {
		GOStyle *style = go_style_dup (style0);
		if (state->default_style.graphics)
			odf_apply_style_props
				(xin, state->default_style.graphics->style_props,
				 style, FALSE);

		for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
			if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]),
						OO_NS_DRAW, "style-name"))
				style_name = CXML2C (attrs[1]);
		if (style_name != NULL) {
			OOChartStyle *oostyle = g_hash_table_lookup
				(state->chart.graph_styles, style_name);
			if (oostyle != NULL) {
				odf_apply_style_props (xin, oostyle->style_props,
						       style, FALSE);
				odf_so_set_props (state, oostyle);
			}
		}
		g_object_set (state->chart.so, "style", style, NULL);
		g_object_unref (style);
		g_object_unref (style0);
	}
}

static void
odf_caption (GsfXMLIn *xin, xmlChar const **attrs)
{
	OOParseState *state = (OOParseState *)xin->user_state;

	oo_warning (xin, _("An unsupported caption was encountered and "
			   "converted to a text rectangle."));
	odf_so_filled (xin, attrs, FALSE);
	odf_push_text_p (state, FALSE);
}

static void
odf_rect (GsfXMLIn *xin, xmlChar const **attrs)
{
	OOParseState *state = (OOParseState *)xin->user_state;

	odf_so_filled (xin, attrs, FALSE);
	odf_push_text_p (state, FALSE);
}

static void
odf_ellipse (GsfXMLIn *xin, xmlChar const **attrs)
{
	OOParseState *state = (OOParseState *)xin->user_state;

	odf_so_filled (xin, attrs, TRUE);
	odf_push_text_p (state, FALSE);
}

static void
odf_custom_shape_replace_object (OOParseState *state, SheetObject *so)
{
	GObjectClass *klass = G_OBJECT_GET_CLASS (G_OBJECT (so));

	if (NULL != g_object_class_find_property (klass, "text")) {
		char *text = NULL;
		g_object_get (state->chart.so, "text", &text, NULL);
		g_object_set (so, "text", text, NULL);
		g_free (text);
	}
	if (NULL != g_object_class_find_property (klass, "style")) {
		GOStyle *style = NULL;
		g_object_get (state->chart.so, "style", &style, NULL);
		g_object_set (so, "style", style, NULL);
		g_object_unref (style);
	}
	if (NULL != g_object_class_find_property (klass, "markup")) {
		PangoAttrList   *attrs = NULL;
		g_object_get (state->chart.so, "markup", &attrs, NULL);
		g_object_set (so, "markup", attrs, NULL);
		pango_attr_list_unref (attrs);
	}
	g_object_unref (state->chart.so);
	state->chart.so = so;
}

static double
odf_get_cs_formula_value (GsfXMLIn *xin, char const *key, GHashTable *vals, gint level)
{
	double *x = g_hash_table_lookup (vals, key);
	OOParseState *state = (OOParseState *)xin->user_state;
	gchar *formula, *o_formula;
	GString *gstr;
	GnmConventions const *convs = gnm_conventions_default;
	GnmExprTop const *texpr;
	GnmLocale *oldlocale = NULL;
	double x_ret = level;
	double viewbox_left = 0.;
	double viewbox_top = 0.;
	double viewbox_width = 0.;
	double viewbox_height = 0.;

	if (x)
		return *x;

	o_formula = formula = g_hash_table_lookup (state->chart.cs_variables, key);

	if (level < 0) {
		oo_warning (xin, _("Infinite loop encountered while parsing formula '%s' "
				   "of name '%s'"),
			    o_formula, key);
		return 0;
	}

	g_return_val_if_fail (formula != NULL, level);

	if (state->chart.cs_viewbox) {
		/*
		  Note:
		  In ODF 1.2 part 1 19.570 svg:viewBox we have:
		  "The syntax for using this attribute is the same as the [SVG] syntax.
		  The value of the attribute are four numbers separated by white spaces,
		  which define the left, top, right, and bottom dimensions of the user
		  coordinate system."
		  but [SVG] specifies:
		  "The value of the viewBox attribute is a list of four numbers <min-x>,
		  <min-y>, <width> and <height>"
		  Since so far we have only seen cases with left  == top == 0, We don't know which
		  version is really used. We are implementing the [SVG] version.
		 */
		char *end = NULL;
		viewbox_left = go_strtod (state->chart.cs_viewbox, &end);
		viewbox_top = go_strtod (end, &end);
		viewbox_width = go_strtod (end, &end);
		viewbox_height = go_strtod (end, &end);
	}

	gstr = g_string_new ("");

	while (*formula != 0) {
		gchar *here;
		gchar *name;
		double *val, fval;

		switch (*formula) {
		case ' ':
		case '\t':
			formula++;
			break;
		case '?':
			here = formula + 1;
			/* ODF 1.2 is quite clear that:
			   --------------------------
			   function_reference::= "?" name
			   name::= [^#x20#x9]+
			   --------------------------
			   so we should grab all non-space, non-tab characters
			   as a function_reference name.

			   The problem is that LO creates files in which these
			   function reference are not terminated by space or tab!

			   So if we want to read them correctly we should only use
			   alphanumerics...
			*/

			/* while (*here != ' ' && *here != '\t') */
			/* 	here++; */

			while (g_ascii_isalnum (*here))
				here++;

			name = g_strndup (formula, here - formula);
			formula = here;
			fval = odf_get_cs_formula_value (xin, name, vals, level - 1);
			g_string_append_printf (gstr, "%.12g", fval);
			g_free (name);
			break;
		case '$':
			here = formula + 1;
			while (g_ascii_isdigit (*here))
				here++;
			name = g_strndup (formula, here - formula);
			formula = here;
			val = g_hash_table_lookup (vals, name);
			g_free (name);
			if (val == NULL)
				g_string_append_c (gstr, '0');
			else
				g_string_append_printf (gstr, "%.12g", *val);
			break;
		case 'p':
			if (g_str_has_prefix (formula, "pi")) {
				formula += 2;
				g_string_append (gstr, "pi()");
			} else {
				g_string_append_c (gstr, *formula);
				formula++;
			}
			break;
		case 't':
			if (g_str_has_prefix (formula, "top")) {
				formula += 3;
				g_string_append_printf (gstr, "%.12g", viewbox_top);
			} else {
				g_string_append_c (gstr, *formula);
				formula++;
			}
			break;
		case 'b':
			if (g_str_has_prefix (formula, "bottom")) {
				formula += 6;
				g_string_append_printf (gstr, "%.12g",
							viewbox_top + viewbox_height);
			} else {
				g_string_append_c (gstr, *formula);
				formula++;
			}
			break;
		case 'l':
			if (g_str_has_prefix (formula, "left")) {
				formula += 4;
				g_string_append_printf (gstr, "%.12g", viewbox_left);
			} else {
				g_string_append_c (gstr, *formula);
				formula++;
			}
			break;
		case 'r':
			if (g_str_has_prefix (formula, "right")) {
				formula += 5;
				g_string_append_printf (gstr, "%.12g",
							viewbox_left + viewbox_width);
			} else {
				g_string_append_c (gstr, *formula);
				formula++;
			}
			break;
		case 'h':
			if (g_str_has_prefix (formula, "height")) {
				formula += 6;
				g_string_append_printf (gstr, "%.12g", viewbox_height);
			} else {
				g_string_append_c (gstr, *formula);
				formula++;
			}
			break;
		case 'w':
			if (g_str_has_prefix (formula, "width")) {
				formula += 5;
				g_string_append_printf (gstr, "%.12g", viewbox_width);
			} else {
				g_string_append_c (gstr, *formula);
				formula++;
			}
			break;


			/* The ODF specs says (in ODF 1.2 part 1 item 19.171):
			   "sin(n) returns the trigonometric sine of n, where n is an angle
			   specified in degrees"
			   but LibreOffice clearly uses sin(n) with n in radians
			*/
		/* case 'c': */
		/* 	if (g_str_has_prefix (formula, "cos(")) { */
		/* 		formula += 4; */
		/* 		/\* FIXME: this does not work in general, eg. if the argument *\/ */
		/* 		/\* to cos is a sum *\/ */
		/* 		g_string_append (gstr, "cos(pi()/180*"); */
		/* 	} else { */
		/* 		g_string_append_c (gstr, *formula); */
		/* 		formula++; */
		/* 	} */
		/* 	break; */
		/* case 's': */
		/* 	if (g_str_has_prefix (formula, "sin(")) { */
		/* 		formula += 4; */
		/* 		/\* FIXME: this does not work in general, eg. if the argument *\/ */
		/* 		/\* to sin is a sum *\/ */
		/* 		g_string_append (gstr, "sin(pi()/180*"); */
		/* 	} else { */
		/* 		g_string_append_c (gstr, *formula); */
		/* 		formula++; */
		/* 	} */
		/* 	break; */
		default:
			g_string_append_c (gstr, *formula);
			formula++;
			break;
		}
	}

	oldlocale = gnm_push_C_locale ();
	texpr = gnm_expr_parse_str (gstr->str, &state->pos,
				    GNM_EXPR_PARSE_DEFAULT,
				    convs,
				    NULL);
	gnm_pop_C_locale (oldlocale);

	if (texpr) {
		GnmEvalPos ep;
		GnmValue *val;
		eval_pos_init_sheet (&ep, state->pos.sheet);
		val = gnm_expr_top_eval
			(texpr, &ep, GNM_EXPR_EVAL_PERMIT_NON_SCALAR);
		if (VALUE_IS_NUMBER (val)) {
			x_ret = value_get_as_float (val);
			x = g_new (double, 1);
			*x = x_ret;
			g_hash_table_insert (vals, g_strdup (key), x);
		} else
			oo_warning (xin, _("Unable to evaluate formula '%s' ('%s') of name '%s'"),
				    o_formula, gstr->str, key);
		value_release (val);
		gnm_expr_top_unref (texpr);
	} else
		oo_warning (xin, _("Unable to parse formula '%s' ('%s') of name '%s'"),
			    o_formula, gstr->str, key);
	g_string_free (gstr, TRUE);
	return x_ret;
}

static void
odf_custom_shape_end (GsfXMLIn *xin, GsfXMLBlob *blob)
{
	OOParseState *state = (OOParseState *)xin->user_state;
	GHashTable *vals = NULL;
	char **strs, **cur;
	GPtrArray *paths;

	if (state->chart.cs_variables || state->chart.cs_modifiers) {
		vals = g_hash_table_new_full
			(g_str_hash, g_str_equal,
			 (GDestroyNotify) g_free, (GDestroyNotify) g_free);
		if (state->chart.cs_modifiers) {
			int i = 0;
			char *next = state->chart.cs_modifiers;

			while (*next != 0) {
				char *end  = next;
				double x = go_strtod (next, &end);
				if (end > next) {
					double *xp = g_new (double, 1);
					char *name = g_strdup_printf ("$%i", i);
					*xp = x;
					g_hash_table_insert (vals, name, xp);
					i++;
					while (*end == ' ')
						end++;
					next = end;
				} else break;
			}
		}
		if (state->chart.cs_variables) {
			GList *keys = g_hash_table_get_keys (state->chart.cs_variables);
			GList *l;
			gint level = g_hash_table_size (state->chart.cs_variables);
			for (l = keys; l != NULL; l = l->next)
				odf_get_cs_formula_value (xin, l->data, vals, level);
			g_list_free (keys);
		}
	}

	paths = g_ptr_array_new_with_free_func ((GDestroyNotify) go_path_free);

	if (state->chart.cs_enhanced_path != NULL) {
		strs = g_strsplit (state->chart.cs_enhanced_path, " N", 0);
		for (cur = strs; *cur != NULL; cur++) {
			GOPath *path;
			path = go_path_new_from_odf_enhanced_path (*cur, vals);
			if (path)
				g_ptr_array_add (paths, path);
		}
		g_strfreev (strs);
	}

	if (vals)
		g_hash_table_unref (vals);

	/* Note that we have already created a rectangle */

	if (paths->len == 1) {
		odf_custom_shape_replace_object
			(state, g_object_new (GNM_SO_PATH_TYPE,
					      "path", g_ptr_array_index (paths, 0), NULL));
	} else if (paths->len > 1) {
		odf_custom_shape_replace_object
			(state, g_object_new (GNM_SO_PATH_TYPE,
					      "paths", paths, NULL));
	} else if (state->chart.cs_type) {
		/* ignoring "ellipse" and "rectangle" since they will be handled by the GOPath */
		if (0 == g_ascii_strcasecmp (state->chart.cs_type, "frame") &&
		    g_str_has_prefix (state->chart.cs_enhanced_path, "M ")) {
			odf_custom_shape_replace_object
				(state, g_object_new (GNM_SOW_FRAME_TYPE, NULL));
		} else if (0 == g_ascii_strcasecmp (state->chart.cs_type, "round-rectangle") ||
			   0 == g_ascii_strcasecmp (state->chart.cs_type, "paper") ||
			   0 == g_ascii_strcasecmp (state->chart.cs_type, "parallelogram") ||
			   0 == g_ascii_strcasecmp (state->chart.cs_type, "trapezoid")) {
			/* We have already created the rectangle */
			oo_warning (xin , _("An unsupported custom shape of type '%s' was encountered and "
					    "converted to a rectangle."), state->chart.cs_type);
		} else
			oo_warning (xin , _("An unsupported custom shape of type '%s' was encountered and "
					    "converted to a rectangle."), state->chart.cs_type);
	} else
		oo_warning (xin , _("An unsupported custom shape was encountered and "
				    "converted to a rectangle."));
	g_ptr_array_unref (paths);

	od_draw_text_frame_end (xin, blob);

	g_free (state->chart.cs_enhanced_path);
	g_free (state->chart.cs_modifiers);
	g_free (state->chart.cs_viewbox);
	g_free (state->chart.cs_type);
	state->chart.cs_enhanced_path = NULL;
	state->chart.cs_modifiers = NULL;
	state->chart.cs_viewbox = NULL;
	state->chart.cs_type = NULL;
	if (state->chart.cs_variables)
		g_hash_table_remove_all (state->chart.cs_variables);
}

static void
odf_custom_shape_equation (GsfXMLIn *xin, xmlChar const **attrs)
{
	OOParseState *state = (OOParseState *)xin->user_state;
	gchar const *name = NULL, *meaning = NULL;
	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]),
					OO_NS_DRAW, "name"))
			name = CXML2C (attrs[1]);
		else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]),
					     OO_NS_DRAW, "formula"))
			meaning = CXML2C (attrs[1]);
	if (name && meaning) {
		if (state->chart.cs_variables == NULL)
			state->chart.cs_variables = g_hash_table_new_full
				(g_str_hash, g_str_equal,
				 (GDestroyNotify) g_free, (GDestroyNotify) g_free);
		g_hash_table_insert (state->chart.cs_variables,
				     g_strdup_printf ("?%s", name), g_strdup (meaning));
	}
}

static void
odf_custom_shape (GsfXMLIn *xin, xmlChar const **attrs)
{
	OOParseState *state = (OOParseState *)xin->user_state;

	/* to avoid spill over */
	g_free (state->chart.cs_enhanced_path);
	g_free (state->chart.cs_type);
	state->chart.cs_enhanced_path = NULL;
	state->chart.cs_type = NULL;

	odf_so_filled (xin, attrs, FALSE);
	odf_push_text_p (state, FALSE);
}

static void
odf_custom_shape_enhanced_geometry (GsfXMLIn *xin, xmlChar const **attrs)
{
	OOParseState *state = (OOParseState *)xin->user_state;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]),
					OO_NS_DRAW, "type"))
			state->chart.cs_type = g_strdup (CXML2C (attrs[1]));
		else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]),
					     OO_NS_DRAW, "enhanced-path"))
			state->chart.cs_enhanced_path = g_strdup (CXML2C (attrs[1]));
		else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]),
					     OO_NS_DRAW, "modifiers"))
			state->chart.cs_modifiers = g_strdup (CXML2C (attrs[1]));
		else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]),
					     OO_NS_SVG, "viewBox"))
			state->chart.cs_viewbox = g_strdup (CXML2C (attrs[1]));
}

static GOArrow *
odf_get_arrow_marker (OOParseState *state, char const *name, double width)
{
	OOMarker *m = g_hash_table_lookup (state->chart.arrow_markers, name);

	if (m != NULL) {
		GOArrow *arrow;
		if (m->arrow == NULL) {
			m->arrow = g_new0 (GOArrow, 1);
			go_arrow_init_kite (m->arrow, 8 * width / 6.,
					    10 * width / 6., width / 2.);
			m->width = width;
			arrow = go_arrow_dup (m->arrow);
		} else {
			switch (m->arrow->typ) {
			case GO_ARROW_KITE:
			        if (m->arrow->c == 0 || 2 * m->arrow->c == width) {
					arrow = go_arrow_dup (m->arrow);
				} else {
					double ratio =  width / 2. / m->arrow->c;
					arrow = g_new0 (GOArrow, 1);
					go_arrow_init_kite
						(arrow,
						 m->arrow->a * ratio,
						 m->arrow->b * ratio,
						 width/2);
				}
				break;
			case GO_ARROW_OVAL:
			default:
			        if (m->arrow->a == 0 || 2 * m->arrow->a == width) {
					arrow = go_arrow_dup (m->arrow);
				} else {
					double ratio =  width / 2. / m->arrow->a;
					arrow = g_new0 (GOArrow, 1);
					go_arrow_init_oval
						(arrow,
						 width/2,
						 m->arrow->b * ratio);
				}
				break;
			}
		}
		return arrow;
	} else {
		GOArrow *arrow = g_new0 (GOArrow, 1);
		go_arrow_init_kite (arrow, 8 * width / 6.,
					    10 * width / 6., width / 2.);
		return arrow;
	}
}

static void
odf_line (GsfXMLIn *xin, xmlChar const **attrs)
{
	OOParseState *state = (OOParseState *)xin->user_state;
	double x1 = 0., x2 = 0., y1 = 0., y2 = 0.;
	GODrawingAnchorDir direction;
	GnmRange cell_base;
	double frame_offset[4];
	char const *style_name = NULL;
	char const *name = NULL;
	gdouble height, width;
	int z = -1;
	GnmSOAnchorMode mode;
	cell_base.start.col = state->pos.eval.col;
	cell_base.start.row = state->pos.eval.row;
	cell_base.end.col = cell_base.end.row = -1;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]),
					OO_NS_DRAW, "style-name"))
			style_name = CXML2C (attrs[1]);
		else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_DRAW, "name"))
			name = CXML2C (attrs[1]);
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
		} else if (oo_attr_int_range (xin,attrs, OO_NS_DRAW, "z-index",
					      &z, 0, G_MAXINT))
			;

	if (x1 < x2) {
		if (y1 < y2)
			direction = GOD_ANCHOR_DIR_DOWN_RIGHT;
		else
			direction = GOD_ANCHOR_DIR_UP_RIGHT;
		frame_offset[0] = x1;
		frame_offset[2] = x2;
		width = x2 - x1;
	} else {
		if (y1 < y2)
			direction = GOD_ANCHOR_DIR_DOWN_LEFT;
		else
			direction = GOD_ANCHOR_DIR_UP_LEFT;
		frame_offset[0] = x2;
		frame_offset[2] = x1;
		width = x1 - x2;
	}
	if (y1 < y2) {
		frame_offset[1] = y1;
		frame_offset[3] = y2;
		height = y2 - y1;
	} else {
		frame_offset[1] = y2;
		frame_offset[3] = y1;
		height = y1 - y2;
	}

	if (state->pos.eval.col >= 0) {
		if (cell_base.end.col >= 0) {
			mode = GNM_SO_ANCHOR_TWO_CELLS;
		} else {
			cell_base.end.col = cell_base.start.col;
			cell_base.end.row = cell_base.start.row;
			frame_offset[2] = width;
			frame_offset[3] = height;
			mode = GNM_SO_ANCHOR_ONE_CELL;
		}
	} else {
		cell_base.end.col = cell_base.start.col =
			cell_base.end.row = cell_base.start.row = 0; /* actually not needed */
		frame_offset[2] = width;
		frame_offset[3] = height;
		mode = GNM_SO_ANCHOR_ABSOLUTE;
	}

	odf_draw_frame_store_location (state, frame_offset,
				       height, width);

	sheet_object_anchor_init (&state->chart.anchor, &cell_base,
				  frame_offset,
				  direction,
	                          mode);
	state->chart.so = g_object_new (GNM_SO_LINE_TYPE, NULL);

	if (name)
		sheet_object_set_name (state->chart.so, name);

	if (style_name != NULL) {
		OOChartStyle *oostyle = g_hash_table_lookup
			(state->chart.graph_styles, style_name);
		if (oostyle != NULL) {
			GOStyle *style0;
			char const *start_marker = NULL;
			char const *end_marker = NULL;
			double start_marker_width = 0.;
			double end_marker_width = 0.;
			GSList *l;

			g_object_get (state->chart.so, "style", &style0, NULL);
			if (style0 != NULL) {
				GOStyle *style = go_style_dup (style0);
				odf_apply_style_props (xin, oostyle->style_props,
						       style, FALSE);

				g_object_set (state->chart.so, "style", style, NULL);
				g_object_unref (style);
				g_object_unref (style0);
			}

			for (l = oostyle->other_props; l != NULL; l = l->next) {
				OOProp *prop = l->data;
				if (0 == strcmp ("marker-start", prop->name))
					start_marker = g_value_get_string (&prop->value);
				else if (0 == strcmp ("marker-end", prop->name))
					end_marker = g_value_get_string (&prop->value);
				else if (0 == strcmp ("marker-start-width", prop->name))
					start_marker_width = g_value_get_double (&prop->value);
				else if (0 == strcmp ("marker-end-width", prop->name))
					end_marker_width = g_value_get_double (&prop->value);
				else if (0 == strcmp ("print-content", prop->name)) {
					gboolean prop_val;
					prop_val =g_value_get_boolean (&prop->value);
					sheet_object_set_print_flag
						(state->chart.so,
						 &prop_val);
				}
			}

			if (start_marker != NULL) {
				GOArrow *arrow = odf_get_arrow_marker
					(state, start_marker, start_marker_width);

				if (arrow != NULL) {
					g_object_set (G_OBJECT (state->chart.so),
						      "start-arrow", arrow, NULL);
					g_free (arrow);
				}
			}
			if (end_marker != NULL) {
				GOArrow *arrow = odf_get_arrow_marker
					(state, end_marker, end_marker_width);

				if (arrow != NULL) {
					g_object_set (G_OBJECT (state->chart.so),
						      "end-arrow", arrow, NULL);
					g_free (arrow);
				}
			}
		}
	}
	odf_push_text_p (state, FALSE);
	state->chart.z_index = z;
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
					     OO_NS_FORM, "current-state")) {
			g_free (oc->current_state);
			oc->current_state =  g_strdup (CXML2C (attrs[1]));
		} else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]),
					     OO_NS_FORM, "current-selected")) {
			g_free (oc->current_state);
			oc->current_state =  g_strdup (CXML2C (attrs[1]));
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

static void
odf_selection_range (GsfXMLIn *xin, xmlChar const **attrs)
{
	OOParseState *state = (OOParseState *)xin->user_state;
	GnmRange r;
	if (odf_attr_range (xin, attrs, state->pos.sheet, &r))
		sv_selection_add_range (sheet_get_view (state->pos.sheet, state->wb_view), &r);
}

static void
odf_selection (GsfXMLIn *xin, xmlChar const **attrs)
{
	OOParseState *state = (OOParseState *)xin->user_state;
	Sheet *sheet = state->pos.sheet;
	int col = -1, row = -1;

	sv_selection_reset (sheet_get_view (sheet, state->wb_view));

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (oo_attr_int_range
		    (xin, attrs, OO_GNUM_NS_EXT, "cursor-col", &col,
		     0, gnm_sheet_get_last_col(sheet))) {
		} else if (oo_attr_int_range
			   (xin, attrs, OO_GNUM_NS_EXT, "cursor-row", &row,
			    0, gnm_sheet_get_last_row(sheet))) {};

	state->pos.eval.col = col;
	state->pos.eval.row = row;
}

static void
odf_selection_end (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	OOParseState *state = (OOParseState *)xin->user_state;

	gnm_sheet_view_set_edit_pos (sheet_get_view (state->pos.sheet, state->wb_view), &state->pos.eval);
}



/****************************************************************************/
/******************************** settings.xml ******************************/

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
		case G_TYPE_STRING:
			val = g_value_init (g_new0 (GValue, 1), G_TYPE_STRING);
			g_value_set_string (val, (xin->content->str));
			break;
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
		{"short", G_TYPE_INT},
		{"string", G_TYPE_STRING},
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
odf_apply_gnm_config (OOParseState *state)
{
	GValue *val;
	if ((state->settings.settings != NULL) &&
	    NULL != (val = g_hash_table_lookup (state->settings.settings, "gnm:settings")) &&
	    G_VALUE_HOLDS(val,G_TYPE_HASH_TABLE)) {
		int width = 0, height = 0;
		GHashTable *hash =  g_value_get_boxed (val);
		val = g_hash_table_lookup (hash, "gnm:active-sheet");
		if (val != NULL && G_VALUE_HOLDS(val, G_TYPE_STRING)) {
			const gchar *name = g_value_get_string (val);
			Sheet *sheet = workbook_sheet_by_name (state->pos.wb, name);
			if (sheet != NULL)
				wb_view_sheet_focus (state->wb_view, sheet);
		}
		val = g_hash_table_lookup (hash, "gnm:geometry-width");
		if (val != NULL && G_VALUE_HOLDS(val, G_TYPE_INT)) {
				width = g_value_get_int (val);
		}
		val = g_hash_table_lookup (hash, "gnm:geometry-height");
		if (val != NULL && G_VALUE_HOLDS(val, G_TYPE_INT)) {
				height = g_value_get_int (val);
		}
		if (width > 0 && height > 0)
			wb_view_preferred_size (state->wb_view, width, height);
	}
}

static void
odf_apply_ooo_table_config (char const *key, GValue *val, OOParseState *state)
{
	if (G_VALUE_HOLDS(val,G_TYPE_HASH_TABLE)) {
		GHashTable *hash = g_value_get_boxed (val);
		Sheet *sheet = workbook_sheet_by_name (state->pos.wb, key);

		if (hash != NULL && sheet != NULL) {
			SheetView *sv = sheet_get_view (sheet, state->wb_view);
			GnmCellPos pos;
			GValue *item;
			int pos_left = 0, pos_bottom = 0;
			int vsm = 0, hsm = 0, vsp = -1, hsp = -1;

			if (!odf_has_gnm_foreign (state)) {
				item = g_hash_table_lookup (hash, "TabColor");
				if (item != NULL && G_VALUE_HOLDS(item, G_TYPE_INT)) {
					GOColor color = g_value_get_int (item);
					color = color << 8;
					sheet->tab_color = gnm_color_new_go (color);
				}
				item = g_hash_table_lookup (hash, "CursorPositionX");
				if (item != NULL && G_VALUE_HOLDS(item, G_TYPE_INT)) {
					GValue *itemy = g_hash_table_lookup (hash, "CursorPositionY");
					if (itemy != NULL && G_VALUE_HOLDS(itemy, G_TYPE_INT)) {
						GnmRange r;
						pos.col = g_value_get_int (item);
						pos.row = g_value_get_int (itemy);
						r.start = pos;
						r.end = pos;

						sv_selection_reset (sv);
						sv_selection_add_range (sv, &r);
						gnm_sheet_view_set_edit_pos
							(sheet_get_view (sheet, state->wb_view),
							 &pos);
					}
				}
				item = g_hash_table_lookup (hash, "HasColumnRowHeaders");
				if (item != NULL && G_VALUE_HOLDS(item, G_TYPE_BOOLEAN)) {
					gboolean val = g_value_get_boolean (item);
					g_object_set (sheet, "display-row-header", val, NULL);
					g_object_set (sheet, "display-column-header", val, NULL);
				}
			}

			item = g_hash_table_lookup (hash, "ShowGrid");
			if (item != NULL && G_VALUE_HOLDS(item, G_TYPE_BOOLEAN))
				g_object_set (sheet, "display-grid", g_value_get_boolean (item), NULL);

			item = g_hash_table_lookup (hash, "ShowZeroValues");
			if (item != NULL && G_VALUE_HOLDS(item, G_TYPE_BOOLEAN))
				g_object_set (sheet, "display-zeros", g_value_get_boolean (item), NULL);

			item = g_hash_table_lookup (hash, "ZoomValue");
			if (item != NULL && G_VALUE_HOLDS(item, G_TYPE_INT))
				g_object_set (sheet, "zoom-factor",  g_value_get_int (item)/100., NULL);

			item = g_hash_table_lookup (hash, "HorizontalSplitMode");
			if (item != NULL && G_VALUE_HOLDS(item, G_TYPE_INT))
				vsm = g_value_get_int (item);
			item = g_hash_table_lookup (hash, "VerticalSplitMode");
			if (item != NULL && G_VALUE_HOLDS(item, G_TYPE_INT))
				hsm = g_value_get_int (item);

			/* We are not implementing SplitMode == 1 */
			if (hsm != 2) hsm = 0;
			if (vsm != 2) vsm = 0;

			if (vsm > 0 || hsm > 0)  {
				if (vsm > 0) {
					item = g_hash_table_lookup (hash, "VerticalSplitPosition");
					if (item != NULL && G_VALUE_HOLDS(item, G_TYPE_INT))
						vsp = g_value_get_int (item);
				} else vsp = 0;
				if (hsm > 0) {
					item = g_hash_table_lookup (hash, "HorizontalSplitPosition");
					if (item != NULL && G_VALUE_HOLDS(item, G_TYPE_INT))
						hsp = g_value_get_int (item);
				} else hsp = 0;
				if (vsp > 0 || hsp > 0) {
					GnmCellPos fpos = {0, 0};
					pos.col = hsp;
					pos.row = vsp;
					gnm_sheet_view_freeze_panes (sv, &fpos, &pos);
				}

				item = g_hash_table_lookup (hash, "PositionRight");
			} else {
				item = g_hash_table_lookup (hash, "PositionLeft");
			}
			if (item != NULL && G_VALUE_HOLDS(item, G_TYPE_INT))
				pos_left = g_value_get_int (item);

			item = g_hash_table_lookup (hash, "PositionBottom");
			if (item != NULL && G_VALUE_HOLDS(item, G_TYPE_INT))
				pos_bottom = g_value_get_int (item);

			gnm_sheet_view_set_initial_top_left (sv, pos_left, pos_bottom);
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

	if (hash == NULL)
		return;

	val = g_hash_table_lookup (hash, "ActiveTable");

	if (NULL != val && G_VALUE_HOLDS(val,G_TYPE_STRING)) {
		const gchar *name = g_value_get_string (val);
		Sheet *sheet = workbook_sheet_by_name (state->pos.wb, name);
		if (sheet != NULL)
			wb_view_sheet_focus (state->wb_view, sheet);
 	}

	if (NULL == (val = g_hash_table_lookup (hash, "Tables")) ||
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
	gboolean read_gnum_marker_info = FALSE;

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
					    GO_ARROW_KITE, GO_ARROW_OVAL))
			read_gnum_marker_info = TRUE;
		else if (oo_attr_double (xin, attrs, OO_GNUM_NS_EXT,
					 "arrow-a", &a));
		else if (oo_attr_double (xin, attrs, OO_GNUM_NS_EXT,
					 "arrow-b", &b));
		else if (oo_attr_double (xin, attrs, OO_GNUM_NS_EXT,
					 "arrow-c", &c));
	if (!read_gnum_marker_info && g_str_has_prefix (name, "gnm-arrow-"))
		sscanf (name, "gnm-arrow-%d-%lf-%lf-%lf", &type, &a, &b, &c);

	if (type != GO_ARROW_NONE) {
		marker->arrow = g_new0 (GOArrow, 1);
		go_arrow_init (marker->arrow, type, a, b, c);
		marker->width = 2. * (type == GO_ARROW_KITE ? c : a);
	} else {
		/* At this time we are not implemeting drawing these markers  */
		/* directly from the SVG string. So we are trying to at least */
		/* recognize some common LibreOffice markers. Note that the   */
		/* width will likel be adjusted later.                        */
		if (0 == strcmp (name, "Circle")) {
			marker->arrow = g_new0 (GOArrow, 1);
			go_arrow_init_oval (marker->arrow, 10., 10.);
			marker->width = 20;
		} else if (0 == strcmp (name, "Arrow") &&
			   0 == strcmp (marker->d, "M10 0l-10 30h20z")) {
			marker->arrow = g_new0 (GOArrow, 1);
			go_arrow_init_kite (marker->arrow, 30., 30., 10.);
			marker->width = 20;
		} else if (0 == strcmp (name, "Diamond") &&
			   0 == strcmp (marker->d, "M1500 0l1500 3000-1500 3000-1500-3000z")) {
			marker->arrow = g_new0 (GOArrow, 1);
			go_arrow_init_kite (marker->arrow, 60., 30., 15.);
			marker->width = 30;
		} else if (0 == strcmp (name, "Square_20_45") &&
			   0 == strcmp (marker->d, "M0 564l564 567 567-567-567-564z")) {
			marker->arrow = g_new0 (GOArrow, 1);
			go_arrow_init_kite (marker->arrow, 20., 10., 5.);
			marker->width = 10;
		} else if (0 == strcmp (name, "Arrow_20_concave") &&
			   0 == strcmp (marker->d,
					"M1013 1491l118 89-567-1580-564 1580 114-85 "
					"136-68 148-46 161-17 161 13 153 46z")) {
			marker->arrow = g_new0 (GOArrow, 1);
			go_arrow_init_kite (marker->arrow, 25., 30., 10.);
			marker->width = 20;
		}  else if (0 == strcmp (name, "Symmetric_20_Arrow") &&
			   0 == strcmp (marker->d, "M564 0l-564 902h1131z")) {
			marker->arrow = g_new0 (GOArrow, 1);
			go_arrow_init_kite (marker->arrow, 10., 10., 6.);
			marker->width = 12;
		}
	}
	if (name != NULL) {
		g_hash_table_replace (state->chart.arrow_markers,
				      g_strdup (name), marker);
	} else
		oo_marker_free (marker);

}

/****************** These are the preparse functions ***********************/

static void
odf_sheet_suggest_size (GsfXMLIn *xin, int *cols, int *rows)
{
	int c = GNM_MIN_COLS;
	int r = GNM_MIN_ROWS;

	while (c < *cols && c < GNM_MAX_COLS)
		c *= 2;

	while (r < *rows && r < GNM_MAX_ROWS)
		r *= 2;

	while (!gnm_sheet_valid_size (c, r))
		gnm_sheet_suggest_size (&c, &r);

	if (xin != NULL && (*cols > c || *rows > r))
		oo_warning (xin, _("The sheet size of %i columns and %i rows used in this file "
			      "exceeds Gnumeric's maximum supported sheet size"), *cols, *rows);

	*cols = c;
	*rows = r;
}

static void
odf_preparse_create_sheet (GsfXMLIn *xin)
{
	OOParseState *state = (OOParseState *)xin->user_state;
	int rows, cols;
	char *table_name = state->object_name;
	Sheet *sheet;
	sheet_order_t *sot = g_new(sheet_order_t, 1);

	cols = state->extent_data.col + 1;
	rows = state->extent_data.row + 1;
	sot->cols = cols;
	sot->rows = rows;
	odf_sheet_suggest_size (xin, &cols, &rows);

	if (table_name != NULL) {
		sheet = workbook_sheet_by_name (state->pos.wb, table_name);
		if (NULL == sheet) {
			sheet = sheet_new (state->pos.wb, table_name, cols, rows);
			workbook_sheet_attach (state->pos.wb, sheet);
		} else {
			/* We have a corrupted file with a duplicate sheet name */
			char *new_name, *base;

			base = g_strdup_printf (_("%s_IN_CORRUPTED_FILE"), table_name);
			new_name =  workbook_sheet_get_free_name (state->pos.wb,
								  base, FALSE, FALSE);
			g_free (base);

			oo_warning (xin, _("This file is corrupted with a "
					   "duplicate sheet name \"%s\", "
					   "now renamed to \"%s\"."),
				    table_name, new_name);
			sheet = sheet_new (state->pos.wb, new_name, cols, rows);
			workbook_sheet_attach (state->pos.wb, sheet);
			g_free (new_name);
		}
	} else {
		table_name = workbook_sheet_get_free_name (state->pos.wb,
							   _("SHEET_IN_CORRUPTED_FILE"),
							   TRUE, FALSE);
		sheet = sheet_new (state->pos.wb, table_name, cols, rows);
		workbook_sheet_attach (state->pos.wb, sheet);

		/* We are missing the table name. This is bad! */
		oo_warning (xin, _("This file is corrupted with an "
				   "unnamed sheet "
				   "now named \"%s\"."),
			    table_name);
	}
	g_free (table_name);
	state->object_name = NULL;

	/* Store sheets in correct order in case we implicitly
	 * created one out of order */
	sot->sheet = sheet;
	state->sheet_order = g_slist_prepend
		(state->sheet_order, sot);

	state->pos.sheet = sheet;

#if 0
	g_printerr ("Created sheet %s\n", sheet->name_unquoted);
#endif
}


static void
oo_named_exprs_preparse (GsfXMLIn *xin, xmlChar const **attrs)
{
	OOParseState *state = (OOParseState *)xin->user_state;

	if (state->pos.sheet == NULL && state->object_name != NULL) {
		// Create sheet, but not for global name section
		odf_preparse_create_sheet (xin);
	}
}

static void
oo_named_expr_preparse (GsfXMLIn *xin, xmlChar const **attrs)
{
	oo_named_expr_common (xin, attrs, TRUE);
}

static void
odf_preparse_table_start (GsfXMLIn *xin, xmlChar const **attrs)
{
	OOParseState *state = (OOParseState *)xin->user_state;

	state->pos.eval.col = 0;
	state->pos.eval.row = 0;
	state->pos.sheet = NULL;
	state->extent_data.col = 0;
	state->extent_data.row = 0;
	state->object_name = NULL;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_TABLE, "name"))
			state->object_name = g_strdup (CXML2C (attrs[1]));
}

static void
odf_preparse_spreadsheet_end (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
}

static void
odf_preparse_table_end (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	OOParseState *state = (OOParseState *)xin->user_state;

	if (state->pos.sheet == NULL)
		odf_preparse_create_sheet (xin);

	state->pos.sheet = NULL;
}


static void
odf_preparse_row_start (GsfXMLIn *xin, xmlChar const **attrs)
{
	OOParseState *state = (OOParseState *)xin->user_state;

	state->pos.eval.col = 0;
	state->row_inc = 1;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (oo_attr_int_range (xin, attrs, OO_NS_TABLE, "number-rows-repeated", &state->row_inc,
				       0, INT_MAX - state->pos.eval.row));
}

static void
odf_preparse_row_end (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	OOParseState *state = (OOParseState *)xin->user_state;
	state->pos.eval.row += state->row_inc;
}

static void
odf_preparse_cell_start (GsfXMLIn *xin, xmlChar const **attrs)
{
	OOParseState *state = (OOParseState *)xin->user_state;

	state->col_inc = 1;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (oo_attr_int_range (xin, attrs, OO_NS_TABLE, "number-columns-repeated",
				       &state->col_inc, 0, INT_MAX - state->pos.eval.col));

	oo_update_data_extent (state, state->col_inc, state->row_inc);
	state->pos.eval.col += state->col_inc;
}

static void
odf_preparse_covered_cell_start (GsfXMLIn *xin, xmlChar const **attrs)
{
	OOParseState *state = (OOParseState *)xin->user_state;

	state->col_inc = 1;
	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (oo_attr_int_range (xin, attrs, OO_NS_TABLE, "number-columns-repeated",
				       &state->col_inc, 0, INT_MAX - state->pos.eval.col));
	state->pos.eval.col += state->col_inc;
}



/**************************************************************************/

static void
odf_find_version (GsfXMLIn *xin, xmlChar const **attrs)
{
	OOParseState *state = (OOParseState *)xin->user_state;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (oo_attr_double (xin, attrs, OO_NS_OFFICE,
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
GSF_XML_IN_NODE (OFFICE_DOC_STYLES, OFFICE_FONTS_OOO1, OO_NS_OFFICE, "font-decls", GSF_XML_NO_CONTENT, NULL, NULL),
GSF_XML_IN_NODE (OFFICE_DOC_STYLES, OFFICE_FONTS, OO_NS_OFFICE, "font-face-decls", GSF_XML_NO_CONTENT, NULL, NULL),
  GSF_XML_IN_NODE (OFFICE_FONTS, FONT_DECL, OO_NS_STYLE, "font-face", GSF_XML_NO_CONTENT, NULL, NULL),

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
    GSF_XML_IN_NODE (DEFAULT_STYLE, DEFAULT_GRAPHIC_PROPS, OO_NS_STYLE,	   "graphic-properties", GSF_XML_NO_CONTENT, &oo_style_prop, NULL),
    GSF_XML_IN_NODE (DEFAULT_STYLE, DEFAULT_PARAGRAPH_PROPS, OO_NS_STYLE,  "paragraph-properties", GSF_XML_NO_CONTENT, &oo_style_prop, NULL),
      GSF_XML_IN_NODE (DEFAULT_PARAGRAPH_PROPS, DEFAULT_PARA_TABS, OO_NS_STYLE,  "tab-stops", GSF_XML_NO_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE (DEFAULT_STYLE, DEFAULT_STYLE_PROP, OO_NS_STYLE,	   "properties", GSF_XML_NO_CONTENT, &oo_style_prop, NULL),
      GSF_XML_IN_NODE (DEFAULT_STYLE_PROP, STYLE_TAB_STOPS, OO_NS_STYLE, "tab-stops", GSF_XML_NO_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE (DEFAULT_STYLE, DEFAULT_TABLE_COL_PROPS, OO_NS_STYLE, "table-column-properties", GSF_XML_NO_CONTENT, &oo_style_prop, NULL),
    GSF_XML_IN_NODE (DEFAULT_STYLE, DEFAULT_TABLE_ROW_PROPS, OO_NS_STYLE, "table-row-properties", GSF_XML_NO_CONTENT, &oo_style_prop, NULL),

  GSF_XML_IN_NODE (OFFICE_STYLES, NUMBER_STYLE, OO_NS_NUMBER, "number-style", GSF_XML_NO_CONTENT, &odf_number_style, &odf_number_style_end),
#if HAVE_OO_NS_LOCALC_EXT
    GSF_XML_IN_NODE (NUMBER_STYLE, NUMBER_STYLE_NUMBERFILL_CHARACTER, OO_NS_LOCALC_EXT,	"fill-character", GSF_XML_NO_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE (NUMBER_STYLE, LOEXT_TEXT, OO_NS_LOCALC_EXT, "text", GSF_XML_NO_CONTENT, NULL, NULL),
#endif
    GSF_XML_IN_NODE (NUMBER_STYLE, NUMBER_STYLE_NUMBER, OO_NS_NUMBER,	"number", GSF_XML_NO_CONTENT, &odf_number, NULL),
GSF_XML_IN_NODE (NUMBER_STYLE_NUMBER, NUMBER_EMBEDDED_TEXT, OO_NS_NUMBER, "embedded-text", GSF_XML_CONTENT, &odf_embedded_text_start, &odf_embedded_text_end),
    GSF_XML_IN_NODE_FULL (NUMBER_STYLE, NUMBER_STYLE_TEXT, OO_NS_NUMBER,	"text", GSF_XML_CONTENT, FALSE, FALSE, &odf_format_text_start, &oo_format_text_end, GO_FORMAT_NUMBER),
       GSF_XML_IN_NODE_FULL (NUMBER_STYLE_TEXT, FORMAT_TEXT_INVISIBLE, OO_GNUM_NS_EXT, "invisible", GSF_XML_NO_CONTENT, FALSE, FALSE, &odf_format_invisible_text, NULL, GO_FORMAT_NUMBER),
       GSF_XML_IN_NODE (NUMBER_STYLE_TEXT, FORMAT_TEXT_REPEATED, OO_GNUM_NS_EXT, "repeated", GSF_XML_NO_CONTENT, NULL, &odf_format_repeated_text_end),
    GSF_XML_IN_NODE (NUMBER_STYLE, NUMBER_STYLE_FRACTION, OO_NS_NUMBER, "fraction", GSF_XML_NO_CONTENT, &odf_fraction, NULL),
    GSF_XML_IN_NODE (NUMBER_STYLE, NUMBER_SCI_STYLE_PROP, OO_NS_NUMBER, "scientific-number", GSF_XML_NO_CONTENT, &odf_scientific, NULL),
    GSF_XML_IN_NODE (NUMBER_STYLE, NUMBER_STYLE_PROP, OO_NS_STYLE,	"properties", GSF_XML_NO_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE (NUMBER_STYLE, NUMBER_MAP, OO_NS_STYLE,		"map", GSF_XML_NO_CONTENT, &odf_map, NULL),
    GSF_XML_IN_NODE (NUMBER_STYLE, NUMBER_TEXT_PROP, OO_NS_STYLE,	"text-properties", GSF_XML_NO_CONTENT, &odf_number_color, NULL),
    GSF_XML_IN_NODE (NUMBER_STYLE, FORMAT_TEXT_INVISIBLE, OO_GNUM_NS_EXT, "invisible", GSF_XML_2ND, NULL, NULL),
    GSF_XML_IN_NODE (NUMBER_STYLE, NUMBER_FILL_CHARACTER, OO_NS_NUMBER,	"fill-character", GSF_XML_NO_CONTENT, NULL, NULL),

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
    GSF_XML_IN_NODE_FULL (DATE_STYLE, DATE_TEXT, OO_NS_NUMBER,		"text", GSF_XML_CONTENT, FALSE, FALSE, &odf_format_text_start, &oo_format_text_end, GO_FORMAT_DATE),
    GSF_XML_IN_NODE (DATE_STYLE, DATE_TEXT_PROP, OO_NS_STYLE,		"text-properties", GSF_XML_NO_CONTENT, &odf_number_color, NULL),
    GSF_XML_IN_NODE (DATE_STYLE, DATE_MAP, OO_NS_STYLE,			"map", GSF_XML_NO_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE (DATE_STYLE, DATE_FILL_CHARACTER, OO_NS_NUMBER,	"fill-character", GSF_XML_NO_CONTENT, NULL, NULL),

  GSF_XML_IN_NODE (OFFICE_STYLES, TIME_STYLE, OO_NS_NUMBER, "time-style", GSF_XML_NO_CONTENT, &oo_date_style, &oo_date_style_end),
    GSF_XML_IN_NODE (TIME_STYLE, TIME_HOURS, OO_NS_NUMBER,		"hours", GSF_XML_NO_CONTENT,	&oo_date_hours, NULL),
    GSF_XML_IN_NODE (TIME_STYLE, TIME_MINUTES, OO_NS_NUMBER,		"minutes", GSF_XML_NO_CONTENT, &oo_date_minutes, NULL),
    GSF_XML_IN_NODE (TIME_STYLE, TIME_SECONDS, OO_NS_NUMBER,		"seconds", GSF_XML_NO_CONTENT, &oo_date_seconds, NULL),
    GSF_XML_IN_NODE (TIME_STYLE, TIME_AM_PM, OO_NS_NUMBER,		"am-pm", GSF_XML_NO_CONTENT,	&oo_date_am_pm, NULL),
    GSF_XML_IN_NODE_FULL (TIME_STYLE, TIME_TEXT, OO_NS_NUMBER,		"text", GSF_XML_CONTENT, FALSE, FALSE, &odf_format_text_start, &oo_format_text_end, GO_FORMAT_TIME),
    GSF_XML_IN_NODE (TIME_STYLE, TIME_TEXT_PROP, OO_NS_STYLE,		"text-properties", GSF_XML_NO_CONTENT, &odf_number_color, NULL),
    GSF_XML_IN_NODE (TIME_STYLE, TIME_MAP, OO_NS_STYLE,			"map", GSF_XML_NO_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE (TIME_STYLE, TIME_FILL_CHARACTER, OO_NS_NUMBER,	"fill-character", GSF_XML_NO_CONTENT, NULL, NULL),

  GSF_XML_IN_NODE (OFFICE_STYLES, STYLE_BOOL, OO_NS_NUMBER, "boolean-style", GSF_XML_NO_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE (STYLE_BOOL, BOOL_PROP, OO_NS_NUMBER, "boolean", GSF_XML_NO_CONTENT, NULL, NULL),

  GSF_XML_IN_NODE (OFFICE_STYLES, STYLE_CURRENCY, OO_NS_NUMBER,		"currency-style", GSF_XML_NO_CONTENT, &odf_number_style, &odf_number_style_end),
    GSF_XML_IN_NODE (STYLE_CURRENCY, CURRENCY_STYLE, OO_NS_NUMBER,	"number", GSF_XML_NO_CONTENT, &odf_number, NULL),
    GSF_XML_IN_NODE (STYLE_CURRENCY, CURRENCY_STYLE_PROP, OO_NS_STYLE,	"properties", GSF_XML_NO_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE (STYLE_CURRENCY, CURRENCY_MAP, OO_NS_STYLE,		"map", GSF_XML_NO_CONTENT, &odf_map, NULL),
    GSF_XML_IN_NODE (STYLE_CURRENCY, CURRENCY_SYMBOL, OO_NS_NUMBER,	"currency-symbol", GSF_XML_CONTENT, NULL, &odf_currency_symbol_end),
    GSF_XML_IN_NODE_FULL (STYLE_CURRENCY, CURRENCY_TEXT, OO_NS_NUMBER,	"text", GSF_XML_CONTENT, FALSE, FALSE, &odf_format_text_start, &oo_format_text_end, GO_FORMAT_CURRENCY),
      GSF_XML_IN_NODE (CURRENCY_TEXT, FORMAT_TEXT_INVISIBLE, OO_GNUM_NS_EXT, "invisible", GSF_XML_2ND, NULL, NULL),
      GSF_XML_IN_NODE (CURRENCY_TEXT, FORMAT_TEXT_REPEATED, OO_GNUM_NS_EXT, "repeated", GSF_XML_2ND, NULL, NULL),
    GSF_XML_IN_NODE (STYLE_CURRENCY, CURRENCY_TEXT_PROP, OO_NS_STYLE,	"text-properties", GSF_XML_NO_CONTENT, &odf_number_color, NULL),
    GSF_XML_IN_NODE (STYLE_CURRENCY, CURRENCY_FILL_CHARACTER, OO_NS_NUMBER,	"fill-character", GSF_XML_NO_CONTENT, NULL, NULL),

  GSF_XML_IN_NODE (OFFICE_STYLES, STYLE_PERCENTAGE, OO_NS_NUMBER, "percentage-style", GSF_XML_NO_CONTENT, &odf_number_percentage_style, &odf_number_style_end),
    GSF_XML_IN_NODE (STYLE_PERCENTAGE, PERCENTAGE_STYLE_PROP, OO_NS_NUMBER,	"number", GSF_XML_NO_CONTENT, &odf_number, NULL),
    GSF_XML_IN_NODE_FULL (STYLE_PERCENTAGE, PERCENTAGE_TEXT, OO_NS_NUMBER,		"text", GSF_XML_CONTENT, FALSE, FALSE, &odf_format_text_start, &oo_format_text_end, GO_FORMAT_PERCENTAGE),
    GSF_XML_IN_NODE (STYLE_PERCENTAGE, PERCENTAGE_MAP, OO_NS_STYLE,		"map", GSF_XML_NO_CONTENT, &odf_map, NULL),
    GSF_XML_IN_NODE (STYLE_PERCENTAGE, PERCENTAGE_TEXT_PROP, OO_NS_STYLE,	"text-properties", GSF_XML_NO_CONTENT, &odf_number_color, NULL),
    GSF_XML_IN_NODE (STYLE_PERCENTAGE, PERCENTAGE_FILL_CHARACTER, OO_NS_NUMBER,	"fill-character", GSF_XML_NO_CONTENT, NULL, NULL),

  GSF_XML_IN_NODE (OFFICE_STYLES, STYLE_TEXT, OO_NS_NUMBER, "text-style", GSF_XML_NO_CONTENT, &odf_number_style, &odf_number_style_end),
    GSF_XML_IN_NODE (STYLE_TEXT, STYLE_TEXT_CONTENT, OO_NS_NUMBER,	"text-content", GSF_XML_NO_CONTENT,  &odf_text_content, NULL),
GSF_XML_IN_NODE_FULL (STYLE_TEXT, STYLE_TEXT_PROP, OO_NS_NUMBER,	"text", GSF_XML_CONTENT, FALSE, FALSE, &odf_format_text_start, &oo_format_text_end, GO_FORMAT_TEXT),
      GSF_XML_IN_NODE (STYLE_TEXT_PROP, FORMAT_TEXT_INVISIBLE, OO_GNUM_NS_EXT, "invisible", GSF_XML_2ND, NULL, NULL),
    GSF_XML_IN_NODE (STYLE_TEXT, STYLE_TEXT_MAP, OO_NS_STYLE,		"map", GSF_XML_NO_CONTENT, &odf_map, NULL),
    GSF_XML_IN_NODE (STYLE_TEXT, STYLE_TEXT_TEXT_PROP, OO_NS_STYLE,	"text-properties", GSF_XML_NO_CONTENT, &odf_number_color, NULL),

GSF_XML_IN_NODE (OFFICE_DOC_STYLES, AUTOMATIC_STYLES, OO_NS_OFFICE, "automatic-styles", GSF_XML_NO_CONTENT, NULL, NULL),
GSF_XML_IN_NODE (AUTOMATIC_STYLES, PAGE_MASTER, OO_NS_STYLE, "page-master", GSF_XML_NO_CONTENT, NULL, NULL), /* ooo1 */
GSF_XML_IN_NODE (PAGE_MASTER, PAGE_MASTER_PROPS, OO_NS_STYLE, "properties", GSF_XML_NO_CONTENT, NULL, NULL),
GSF_XML_IN_NODE (PAGE_MASTER_PROPS, PAGE_MASTER_BG_IMAGE, OO_NS_STYLE, "background-image", GSF_XML_NO_CONTENT, NULL, NULL),
GSF_XML_IN_NODE (PAGE_MASTER, PAGE_MASTER_HEADER, OO_NS_STYLE, "header-style", GSF_XML_NO_CONTENT, NULL, NULL),
GSF_XML_IN_NODE (PAGE_MASTER_HEADER, PAGE_MASTER_PROPS, OO_NS_STYLE, "properties", GSF_XML_2ND, NULL, NULL),
GSF_XML_IN_NODE (PAGE_MASTER, PAGE_MASTER_FOOTER, OO_NS_STYLE, "footer-style", GSF_XML_NO_CONTENT, NULL, NULL),
GSF_XML_IN_NODE (PAGE_MASTER_FOOTER, PAGE_MASTER_PROPS, OO_NS_STYLE, "properties", GSF_XML_2ND, NULL, NULL),

  GSF_XML_IN_NODE (AUTOMATIC_STYLES, STYLE, OO_NS_STYLE, "style", GSF_XML_2ND, NULL, NULL),
  GSF_XML_IN_NODE (AUTOMATIC_STYLES, PAGE_LAYOUT, OO_NS_STYLE, "page-layout", GSF_XML_NO_CONTENT,  &odf_page_layout, &odf_page_layout_end),
GSF_XML_IN_NODE (PAGE_LAYOUT, PAGE_LAYOUT_PROPS, OO_NS_STYLE, "page-layout-properties", GSF_XML_NO_CONTENT, &odf_page_layout_properties, NULL),
GSF_XML_IN_NODE (PAGE_LAYOUT_PROPS, BACK_IMAGE, OO_NS_STYLE, "background-image", GSF_XML_NO_CONTENT, NULL, NULL),
GSF_XML_IN_NODE (PAGE_LAYOUT, HEADER_STYLE, OO_NS_STYLE, "header-style", GSF_XML_NO_CONTENT, NULL, NULL),
GSF_XML_IN_NODE (HEADER_STYLE, HEADER_PROPERTIES, OO_NS_STYLE, "header-footer-properties", GSF_XML_NO_CONTENT, odf_header_properties, NULL),
GSF_XML_IN_NODE (HEADER_PROPERTIES, HF_BACK_IMAGE, OO_NS_STYLE, "background-image", GSF_XML_NO_CONTENT, NULL, NULL),
GSF_XML_IN_NODE (PAGE_LAYOUT, FOOTER_STYLE, OO_NS_STYLE, "footer-style", GSF_XML_NO_CONTENT, NULL, NULL),
GSF_XML_IN_NODE (FOOTER_STYLE, FOOTER_PROPERTIES, OO_NS_STYLE, "header-footer-properties", GSF_XML_NO_CONTENT, odf_footer_properties, NULL),
GSF_XML_IN_NODE (FOOTER_PROPERTIES, HF_BACK_IMAGE, OO_NS_STYLE, "background-image", GSF_XML_2ND, NULL, NULL),
  GSF_XML_IN_NODE (AUTOMATIC_STYLES, NUMBER_STYLE, OO_NS_NUMBER, "number-style", GSF_XML_2ND, NULL, NULL),
  GSF_XML_IN_NODE (AUTOMATIC_STYLES, DATE_STYLE, OO_NS_NUMBER, "date-style", GSF_XML_2ND, NULL, NULL),
  GSF_XML_IN_NODE (AUTOMATIC_STYLES, TIME_STYLE, OO_NS_NUMBER, "time-style", GSF_XML_2ND, NULL, NULL),
  GSF_XML_IN_NODE (AUTOMATIC_STYLES, STYLE_BOOL, OO_NS_NUMBER, "boolean-style", GSF_XML_2ND, NULL, NULL),
  GSF_XML_IN_NODE (AUTOMATIC_STYLES, STYLE_CURRENCY, OO_NS_NUMBER,   "currency-style", GSF_XML_2ND, NULL, NULL),
  GSF_XML_IN_NODE (AUTOMATIC_STYLES, STYLE_PERCENTAGE, OO_NS_NUMBER, "percentage-style", GSF_XML_2ND, NULL, NULL),
  GSF_XML_IN_NODE (AUTOMATIC_STYLES, STYLE_TEXT, OO_NS_NUMBER, "text-style", GSF_XML_2ND, NULL, NULL),

GSF_XML_IN_NODE (OFFICE_DOC_STYLES, MASTER_STYLES, OO_NS_OFFICE, "master-styles", GSF_XML_NO_CONTENT, NULL, NULL),
  GSF_XML_IN_NODE (MASTER_STYLES, MASTER_PAGE, OO_NS_STYLE, "master-page", GSF_XML_NO_CONTENT, &odf_master_page, &odf_master_page_end),
  GSF_XML_IN_NODE (MASTER_PAGE, MASTER_PAGE_HEADER_LEFT, OO_NS_STYLE, "header-left", GSF_XML_NO_CONTENT, &odf_header_footer_left, NULL),
    GSF_XML_IN_NODE (MASTER_PAGE_HEADER_LEFT, MASTER_PAGE_HEADER_FOOTER_LEFT_P,  OO_NS_TEXT, "p", GSF_XML_NO_CONTENT, NULL, NULL),
      GSF_XML_IN_NODE (MASTER_PAGE_HEADER_FOOTER_LEFT_P, MASTER_PAGE_HEADER_FOOTER_LEFT_SPAN,  OO_NS_TEXT, "span", GSF_XML_NO_CONTENT, NULL, NULL),
        GSF_XML_IN_NODE (MASTER_PAGE_HEADER_FOOTER_LEFT_SPAN, MASTER_PAGE_HEADER_FOOTER_LEFT_PAGE_NUMBER,  OO_NS_TEXT, "page-number", GSF_XML_NO_CONTENT, NULL, NULL),
        GSF_XML_IN_NODE (MASTER_PAGE_HEADER_FOOTER_LEFT_SPAN, MASTER_PAGE_HEADER_FOOTER_LEFT_SHEET_NAME,  OO_NS_TEXT, "sheet-name", GSF_XML_NO_CONTENT, NULL, NULL),
  GSF_XML_IN_NODE (MASTER_PAGE, MASTER_PAGE_FOOTER_LEFT, OO_NS_STYLE, "footer-left", GSF_XML_NO_CONTENT, &odf_header_footer_left, NULL),
    GSF_XML_IN_NODE (MASTER_PAGE_FOOTER_LEFT, MASTER_PAGE_HEADER_FOOTER_LEFT_P,  OO_NS_TEXT, "p", GSF_XML_2ND, NULL, NULL),
  GSF_XML_IN_NODE_FULL (MASTER_PAGE, MASTER_PAGE_HEADER, OO_NS_STYLE, "header", GSF_XML_NO_CONTENT, FALSE, FALSE, &odf_header_footer, &odf_header_footer_end, 0),
GSF_XML_IN_NODE_FULL (MASTER_PAGE_HEADER, MASTER_PAGE_HF_R_LEFT, OO_NS_STYLE, "region-left", GSF_XML_NO_CONTENT, FALSE, FALSE, &odf_hf_region, &odf_hf_region_end, 0),
GSF_XML_IN_NODE (MASTER_PAGE_HF_R_LEFT, MASTER_PAGE_HF_P, OO_NS_TEXT, "p", GSF_XML_CONTENT, &odf_text_content_start, &odf_text_content_end),
GSF_XML_IN_NODE (MASTER_PAGE_HF_P, TEXT_S,         OO_NS_TEXT, "s", GSF_XML_NO_CONTENT, &odf_text_space, NULL),
GSF_XML_IN_NODE_FULL (MASTER_PAGE_HF_P, TEXT_LINE_BREAK, OO_NS_TEXT, "line-break", GSF_XML_NO_CONTENT, FALSE, FALSE, &odf_text_symbol, NULL, .v_str = "\n"),
GSF_XML_IN_NODE_FULL (MASTER_PAGE_HF_P, TEXT_TAB,  OO_NS_TEXT, "tab", GSF_XML_SHARED_CONTENT, FALSE, FALSE, odf_text_symbol, NULL, .v_str = "\t"),
GSF_XML_IN_NODE (MASTER_PAGE_HF_P, HF_TITLE, OO_NS_TEXT, "title", GSF_XML_NO_CONTENT, &odf_hf_title, NULL),
GSF_XML_IN_NODE (MASTER_PAGE_HF_P, HF_DATE, OO_NS_TEXT, "date", GSF_XML_NO_CONTENT, &odf_hf_date, NULL),
GSF_XML_IN_NODE (MASTER_PAGE_HF_P, HF_TIME, OO_NS_TEXT, "time", GSF_XML_NO_CONTENT, &odf_hf_time, NULL),
GSF_XML_IN_NODE (MASTER_PAGE_HF_P, HF_SHEET_NAME, OO_NS_TEXT, "sheet-name", GSF_XML_NO_CONTENT, &odf_hf_sheet_name, NULL),
GSF_XML_IN_NODE (MASTER_PAGE_HF_P, HF_PAGE_NUMBER, OO_NS_TEXT, "page-number", GSF_XML_NO_CONTENT, &odf_hf_page_number, NULL),
GSF_XML_IN_NODE (MASTER_PAGE_HF_P, HF_PAGE_COUNT, OO_NS_TEXT, "page-count", GSF_XML_NO_CONTENT, &odf_hf_page_count, NULL),
GSF_XML_IN_NODE (MASTER_PAGE_HF_P, HF_FILE_NAME, OO_NS_TEXT, "file-name", GSF_XML_NO_CONTENT, &odf_hf_file, NULL),
GSF_XML_IN_NODE (MASTER_PAGE_HF_P, HF_EXPRESSION, OO_NS_TEXT, "expression", GSF_XML_NO_CONTENT, &odf_hf_expression, NULL),
GSF_XML_IN_NODE (MASTER_PAGE_HF_P, TEXT_SPAN,      OO_NS_TEXT, "span", GSF_XML_SHARED_CONTENT, &odf_text_span_start, &odf_text_span_end),
GSF_XML_IN_NODE (TEXT_SPAN, TEXT_SPAN, OO_NS_TEXT, "span", GSF_XML_2ND, NULL, NULL),
GSF_XML_IN_NODE (TEXT_SPAN, TEXT_S,    OO_NS_TEXT, "s", GSF_XML_2ND, NULL, NULL),
GSF_XML_IN_NODE (TEXT_SPAN, TEXT_LINE_BREAK,    OO_NS_TEXT, "line-break", GSF_XML_2ND, NULL, NULL),
GSF_XML_IN_NODE (TEXT_SPAN, TEXT_TAB, OO_NS_TEXT, "tab", GSF_XML_2ND, NULL, NULL),
GSF_XML_IN_NODE (TEXT_SPAN, HF_SHEET_NAME, OO_NS_TEXT, "sheet-name", GSF_XML_2ND, NULL, NULL),
GSF_XML_IN_NODE (TEXT_SPAN, HF_PAGE_NUMBER, OO_NS_TEXT, "page-number", GSF_XML_2ND, NULL, NULL),
GSF_XML_IN_NODE (TEXT_SPAN, HF_PAGE_COUNT, OO_NS_TEXT, "page-count", GSF_XML_2ND, NULL, NULL),
GSF_XML_IN_NODE (TEXT_SPAN, HF_FILE_NAME, OO_NS_TEXT, "file-name", GSF_XML_2ND, NULL, NULL),
GSF_XML_IN_NODE (TEXT_SPAN, HF_EXPRESSION, OO_NS_TEXT, "expression", GSF_XML_2ND, NULL, NULL),

GSF_XML_IN_NODE_FULL (MASTER_PAGE_HEADER, MASTER_PAGE_HF_R_RIGHT, OO_NS_STYLE, "region-right", GSF_XML_NO_CONTENT, FALSE, FALSE, &odf_hf_region, &odf_hf_region_end, 2),
    GSF_XML_IN_NODE (MASTER_PAGE_HF_R_RIGHT, MASTER_PAGE_HF_P, OO_NS_TEXT, "p", GSF_XML_2ND, NULL, NULL),
GSF_XML_IN_NODE_FULL (MASTER_PAGE_HEADER, MASTER_PAGE_HF_R_CENTER, OO_NS_STYLE, "region-center", GSF_XML_NO_CONTENT, FALSE, FALSE, &odf_hf_region, &odf_hf_region_end, 1),
    GSF_XML_IN_NODE (MASTER_PAGE_HF_R_CENTER, MASTER_PAGE_HF_P, OO_NS_TEXT, "p", GSF_XML_2ND, NULL, NULL),
  GSF_XML_IN_NODE (MASTER_PAGE_HEADER, MASTER_PAGE_HF_P, OO_NS_TEXT, "p", GSF_XML_2ND, NULL, NULL),
  GSF_XML_IN_NODE_FULL (MASTER_PAGE, MASTER_PAGE_FOOTER, OO_NS_STYLE, "footer", GSF_XML_NO_CONTENT, FALSE, FALSE, &odf_header_footer, &odf_header_footer_end, 1),
  GSF_XML_IN_NODE (MASTER_PAGE_FOOTER, MASTER_PAGE_HF_R_LEFT, OO_NS_STYLE, "region-left", GSF_XML_2ND, NULL, NULL),
  GSF_XML_IN_NODE (MASTER_PAGE_FOOTER, MASTER_PAGE_HF_R_RIGHT, OO_NS_STYLE, "region-right", GSF_XML_2ND, NULL, NULL),
  GSF_XML_IN_NODE (MASTER_PAGE_FOOTER, MASTER_PAGE_HF_R_CENTER, OO_NS_STYLE, "region-center", GSF_XML_2ND, NULL, NULL),
  GSF_XML_IN_NODE (MASTER_PAGE_FOOTER, MASTER_PAGE_HF_P, OO_NS_TEXT, "p", GSF_XML_2ND, NULL, NULL),
GSF_XML_IN_NODE_END
};

static GsfXMLInNode const ooo1_content_dtd [] = {
GSF_XML_IN_NODE_FULL (START, START, -1, NULL, GSF_XML_NO_CONTENT, FALSE, TRUE, NULL, NULL, 0),
GSF_XML_IN_NODE (START, OFFICE, OO_NS_OFFICE, "document-content", GSF_XML_NO_CONTENT, NULL, NULL),
  GSF_XML_IN_NODE (OFFICE, SCRIPT, OO_NS_OFFICE, "script", GSF_XML_NO_CONTENT, NULL, NULL),
  GSF_XML_IN_NODE (OFFICE, OFFICE_FONTS, OO_NS_OFFICE, "font-decls", GSF_XML_NO_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE (OFFICE_FONTS, FONT_DECL, OO_NS_STYLE, "font-decl", GSF_XML_NO_CONTENT, NULL, NULL),
  GSF_XML_IN_NODE (OFFICE, OFFICE_STYLES, OO_NS_OFFICE, "automatic-styles", GSF_XML_NO_CONTENT, NULL, NULL),
GSF_XML_IN_NODE (OFFICE_STYLES, PAGE_MASTER, OO_NS_STYLE, "page-master", GSF_XML_NO_CONTENT, NULL, NULL),
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
      GSF_XML_IN_NODE (NUMBER_STYLE, NUMBER_FILL_CHARACTER, OO_NS_NUMBER, "fill-character", GSF_XML_NO_CONTENT, NULL, NULL),

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
      GSF_XML_IN_NODE_FULL (DATE_STYLE, DATE_TEXT, OO_NS_NUMBER,		"text", GSF_XML_CONTENT, FALSE, FALSE, &odf_format_text_start, &oo_format_text_end, GO_FORMAT_DATE),
      GSF_XML_IN_NODE (DATE_STYLE, DATE_TEXT_PROP, OO_NS_STYLE,		"text-properties", GSF_XML_NO_CONTENT, NULL, NULL),
      GSF_XML_IN_NODE (DATE_STYLE, DATE_MAP, OO_NS_STYLE,		"map", GSF_XML_NO_CONTENT, NULL, NULL),
      GSF_XML_IN_NODE (DATE_STYLE, DATE_FILL_CHARACTER, OO_NS_NUMBER, "fill-character", GSF_XML_NO_CONTENT, NULL, NULL),

    GSF_XML_IN_NODE (OFFICE_STYLES, TIME_STYLE, OO_NS_NUMBER, "time-style", GSF_XML_NO_CONTENT, &oo_date_style, &oo_date_style_end),
      GSF_XML_IN_NODE (TIME_STYLE, TIME_HOURS, OO_NS_NUMBER,		"hours", GSF_XML_NO_CONTENT,	&oo_date_hours, NULL),
      GSF_XML_IN_NODE (TIME_STYLE, TIME_MINUTES, OO_NS_NUMBER,		"minutes", GSF_XML_NO_CONTENT, &oo_date_minutes, NULL),
      GSF_XML_IN_NODE (TIME_STYLE, TIME_SECONDS, OO_NS_NUMBER,		"seconds", GSF_XML_NO_CONTENT, &oo_date_seconds, NULL),
      GSF_XML_IN_NODE (TIME_STYLE, TIME_AM_PM, OO_NS_NUMBER,		"am-pm", GSF_XML_NO_CONTENT,	&oo_date_am_pm, NULL),
      GSF_XML_IN_NODE_FULL (TIME_STYLE, TIME_TEXT, OO_NS_NUMBER,		"text", GSF_XML_CONTENT, FALSE, FALSE, &odf_format_text_start, &oo_format_text_end, GO_FORMAT_TIME),
      GSF_XML_IN_NODE (TIME_STYLE, TIME_TEXT_PROP, OO_NS_STYLE,		"text-properties", GSF_XML_NO_CONTENT, NULL, NULL),
      GSF_XML_IN_NODE (TIME_STYLE, TIME_MAP, OO_NS_STYLE,		"map", GSF_XML_NO_CONTENT, NULL, NULL),
      GSF_XML_IN_NODE (TIME_STYLE, TIME_FILL_CHARACTER, OO_NS_NUMBER, "fill-character", GSF_XML_NO_CONTENT, NULL, NULL),

    GSF_XML_IN_NODE (OFFICE_STYLES, STYLE_BOOL, OO_NS_NUMBER, "boolean-style", GSF_XML_NO_CONTENT, NULL, NULL),
      GSF_XML_IN_NODE (STYLE_BOOL, BOOL_PROP, OO_NS_NUMBER, "boolean", GSF_XML_NO_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE (OFFICE_STYLES, STYLE_CURRENCY, OO_NS_NUMBER, "currency-style", GSF_XML_NO_CONTENT, NULL, NULL),
      GSF_XML_IN_NODE (STYLE_CURRENCY, CURRENCY_STYLE, OO_NS_NUMBER, "number", GSF_XML_NO_CONTENT, NULL, NULL),
      GSF_XML_IN_NODE (STYLE_CURRENCY, CURRENCY_STYLE_PROP, OO_NS_STYLE, "properties", GSF_XML_NO_CONTENT, NULL, NULL),
      GSF_XML_IN_NODE (STYLE_CURRENCY, CURRENCY_MAP, OO_NS_STYLE, "map", GSF_XML_NO_CONTENT, NULL, NULL),
      GSF_XML_IN_NODE (STYLE_CURRENCY, CURRENCY_SYMBOL, OO_NS_NUMBER, "currency-symbol", GSF_XML_NO_CONTENT, NULL, NULL),
      GSF_XML_IN_NODE (STYLE_CURRENCY, CURRENCY_TEXT, OO_NS_NUMBER, "text", GSF_XML_NO_CONTENT, NULL, NULL),
      GSF_XML_IN_NODE (STYLE_CURRENCY, CURRENCY_FILL_CHARACTER, OO_NS_NUMBER,	"fill-character", GSF_XML_NO_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE (OFFICE_STYLES, STYLE_PERCENTAGE, OO_NS_NUMBER, "percentage-style", GSF_XML_NO_CONTENT, NULL, NULL),
      GSF_XML_IN_NODE (STYLE_PERCENTAGE, PERCENTAGE_STYLE_PROP, OO_NS_NUMBER, "number", GSF_XML_NO_CONTENT, NULL, NULL),
      GSF_XML_IN_NODE (STYLE_PERCENTAGE, PERCENTAGE_TEXT, OO_NS_NUMBER, "text", GSF_XML_NO_CONTENT, NULL, NULL),
      GSF_XML_IN_NODE (STYLE_PERCENTAGE, PERCENTAGE_FILL_CHARACTER, OO_NS_NUMBER,	"fill-character", GSF_XML_NO_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE (OFFICE_STYLES, STYLE_TEXT, OO_NS_NUMBER, "text-style", GSF_XML_NO_CONTENT, NULL, NULL),
      GSF_XML_IN_NODE (STYLE_TEXT, STYLE_TEXT_CONTENT, OO_NS_NUMBER,	"text-content", GSF_XML_NO_CONTENT, NULL, NULL),
      GSF_XML_IN_NODE (STYLE_TEXT, STYLE_TEXT_PROP, OO_NS_NUMBER,		"text", GSF_XML_NO_CONTENT, NULL, NULL),
      GSF_XML_IN_NODE (STYLE_TEXT, STYLE_TEXT_MAP, OO_NS_STYLE,		"map", GSF_XML_NO_CONTENT, NULL, NULL),

  GSF_XML_IN_NODE (OFFICE, OFFICE_BODY, OO_NS_OFFICE, "body", GSF_XML_NO_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE (OFFICE_BODY, TABLE_CALC_SETTINGS, OO_NS_TABLE, "calculation-settings", GSF_XML_NO_CONTENT, NULL, NULL),
      GSF_XML_IN_NODE (TABLE_CALC_SETTINGS, DATE_CONVENTION, OO_NS_TABLE, "null-date", GSF_XML_NO_CONTENT, &oo_date_convention, NULL),
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
	  GSF_XML_IN_NODE (TABLE_CELL, CELL_TEXT, OO_NS_TEXT, "p", GSF_XML_CONTENT, &oo_cell_content_start, &oo_cell_content_end),
	    GSF_XML_IN_NODE (CELL_TEXT, CELL_TEXT_S,    OO_NS_TEXT, "s", GSF_XML_NO_CONTENT, NULL, NULL),
	    GSF_XML_IN_NODE (CELL_TEXT, CELL_TEXT_SPAN, OO_NS_TEXT, "span", GSF_XML_SHARED_CONTENT, NULL, NULL),
	  GSF_XML_IN_NODE (TABLE_CELL, CELL_OBJECT, OO_NS_DRAW, "object", GSF_XML_NO_CONTENT, NULL, NULL),		/* ignore for now */
	  GSF_XML_IN_NODE (TABLE_CELL, CELL_GRAPHIC, OO_NS_DRAW, "g", GSF_XML_NO_CONTENT, NULL, NULL),		/* ignore for now */
	    GSF_XML_IN_NODE (CELL_GRAPHIC, CELL_GRAPHIC, OO_NS_DRAW, "g", GSF_XML_2ND, NULL, NULL),
	    GSF_XML_IN_NODE (CELL_GRAPHIC, DRAW_POLYLINE, OO_NS_DRAW, "polyline", GSF_XML_NO_CONTENT, NULL, NULL),
            GSF_XML_IN_NODE (CELL_GRAPHIC, CELL_CONTROL, OO_NS_DRAW, "control", GSF_XML_2ND, NULL, NULL),
	  GSF_XML_IN_NODE (TABLE_CELL, DRAW_LINE, OO_NS_DRAW, "line", GSF_XML_NO_CONTENT, &odf_line, &odf_line_end),
            GSF_XML_IN_NODE (DRAW_LINE, IGNORED_TEXT_CONTENT, OO_NS_TEXT, "p", GSF_XML_NO_CONTENT, NULL, NULL), /* ignore for now */
	GSF_XML_IN_NODE (TABLE_ROW, TABLE_COVERED_CELL, OO_NS_TABLE, "covered-table-cell", GSF_XML_NO_CONTENT, &oo_covered_cell_start, &oo_covered_cell_end),
          GSF_XML_IN_NODE (TABLE_COVERED_CELL, DRAW_LINE, OO_NS_DRAW, "line", GSF_XML_2ND, NULL, NULL),
      GSF_XML_IN_NODE (TABLE, TABLE_COL_GROUP, OO_NS_TABLE, "table-column-group", GSF_XML_NO_CONTENT, NULL, NULL),
        GSF_XML_IN_NODE (TABLE_COL_GROUP, TABLE_COL_GROUP, OO_NS_TABLE, "table-column-group", GSF_XML_NO_CONTENT, NULL, NULL),
        GSF_XML_IN_NODE (TABLE_COL_GROUP, TABLE_COL, OO_NS_TABLE, "table-column", GSF_XML_2ND, NULL, NULL),
      GSF_XML_IN_NODE (TABLE, TABLE_ROW_GROUP,	      OO_NS_TABLE, "table-row-group", GSF_XML_NO_CONTENT, NULL, NULL),
        GSF_XML_IN_NODE (TABLE_ROW_GROUP, TABLE_ROW_GROUP, OO_NS_TABLE, "table-row-group", GSF_XML_NO_CONTENT, NULL, NULL),
        GSF_XML_IN_NODE (TABLE_ROW_GROUP, TABLE_ROW,	    OO_NS_TABLE, "table-row", GSF_XML_2ND, NULL, NULL),
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
      GSF_XML_IN_NODE (CONFIG_ITEM_SET, CONFIG_ITEM_SET, OO_NS_CONFIG, "config-item-set", GSF_XML_2ND, NULL, NULL),
      GSF_XML_IN_NODE (CONFIG_ITEM_SET, CONFIG_ITEM, OO_NS_CONFIG, "config-item", GSF_XML_CONTENT, &odf_config_item, &odf_config_item_end),
      GSF_XML_IN_NODE (CONFIG_ITEM_SET, CONFIG_ITEM_MAP_INDEXED, OO_NS_CONFIG, "config-item-map-indexed", GSF_XML_NO_CONTENT, &odf_config_item_set, &odf_config_stack_pop),
        GSF_XML_IN_NODE (CONFIG_ITEM_MAP_INDEXED, CONFIG_ITEM_MAP_ENTRY, OO_NS_CONFIG, "config-item-map-entry",	GSF_XML_NO_CONTENT, &odf_config_item_set, &odf_config_stack_pop),
          GSF_XML_IN_NODE (CONFIG_ITEM_MAP_ENTRY, CONFIG_ITEM_MAP_INDEXED, OO_NS_CONFIG, "config-item-map-indexed", GSF_XML_2ND, NULL, NULL),
          GSF_XML_IN_NODE (CONFIG_ITEM_MAP_ENTRY, CONFIG_ITEM, OO_NS_CONFIG, "config-item", GSF_XML_2ND, NULL, NULL),
          GSF_XML_IN_NODE (CONFIG_ITEM_MAP_ENTRY, CONFIG_ITEM_MAP_NAMED, OO_NS_CONFIG, "config-item-map-named", GSF_XML_NO_CONTENT, &odf_config_item_set, &odf_config_stack_pop),
          GSF_XML_IN_NODE (CONFIG_ITEM_MAP_ENTRY, CONFIG_ITEM_SET, OO_NS_CONFIG, "config-item-set", GSF_XML_2ND, NULL, NULL),
      GSF_XML_IN_NODE (CONFIG_ITEM_SET, CONFIG_ITEM_MAP_NAMED, OO_NS_CONFIG, "config-item-map-named", GSF_XML_2ND,  NULL, NULL),
        GSF_XML_IN_NODE (CONFIG_ITEM_MAP_NAMED, CONFIG_ITEM_MAP_ENTRY, OO_NS_CONFIG, "config-item-map-entry", GSF_XML_2ND,  NULL, NULL),

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
#if HAVE_OO_NS_LOCALC_EXT
              GSF_XML_IN_NODE (NUMBER_STYLE, NUMBER_STYLE_NUMBERFILL_CHARACTER, OO_NS_LOCALC_EXT, "fill-character", GSF_XML_NO_CONTENT, NULL, NULL),
              GSF_XML_IN_NODE (NUMBER_STYLE, LOEXT_TEXT, OO_NS_LOCALC_EXT, "text", GSF_XML_NO_CONTENT, NULL, NULL),
#endif
	      GSF_XML_IN_NODE (NUMBER_STYLE, NUMBER_STYLE_NUMBER, OO_NS_NUMBER,	  "number", GSF_XML_NO_CONTENT, &odf_number, NULL),
                 GSF_XML_IN_NODE (NUMBER_STYLE_NUMBER, NUMBER_EMBEDDED_TEXT, OO_NS_NUMBER, "embedded-text", GSF_XML_NO_CONTENT, NULL, NULL),
	GSF_XML_IN_NODE_FULL (NUMBER_STYLE, NUMBER_STYLE_TEXT, OO_NS_NUMBER,  "text", GSF_XML_CONTENT, FALSE, FALSE, &odf_format_text_start, &oo_format_text_end, GO_FORMAT_NUMBER),
	      GSF_XML_IN_NODE (NUMBER_STYLE, NUMBER_STYLE_FRACTION, OO_NS_NUMBER, "fraction", GSF_XML_NO_CONTENT,  &odf_fraction, NULL),
	      GSF_XML_IN_NODE (NUMBER_STYLE, NUMBER_SCI_STYLE_PROP, OO_NS_NUMBER, "scientific-number", GSF_XML_NO_CONTENT, &odf_scientific, NULL),
	      GSF_XML_IN_NODE (NUMBER_STYLE, NUMBER_MAP, OO_NS_STYLE,		  "map", GSF_XML_NO_CONTENT, &odf_map, NULL),
              GSF_XML_IN_NODE (NUMBER_STYLE, NUMBER_TEXT_PROP, OO_NS_STYLE,	"text-properties", GSF_XML_NO_CONTENT, &odf_number_color, NULL),
              GSF_XML_IN_NODE (NUMBER_STYLE, NUMBER_FILL_CHARACTER, OO_NS_NUMBER, "fill-character", GSF_XML_NO_CONTENT, NULL, NULL),
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
              GSF_XML_IN_NODE_FULL (DATE_STYLE, DATE_TEXT, OO_NS_NUMBER, "text", GSF_XML_CONTENT, FALSE, FALSE, &odf_format_text_start, &oo_format_text_end, GO_FORMAT_DATE),
	      GSF_XML_IN_NODE (DATE_STYLE, DATE_TEXT_PROP, OO_NS_STYLE,		"text-properties", GSF_XML_NO_CONTENT, &odf_number_color, NULL),
	      GSF_XML_IN_NODE (DATE_STYLE, DATE_MAP, OO_NS_STYLE,		"map", GSF_XML_NO_CONTENT, NULL, NULL),
              GSF_XML_IN_NODE (DATE_STYLE, DATE_FILL_CHARACTER, OO_NS_NUMBER, "fill-character", GSF_XML_NO_CONTENT, NULL, NULL),
	    GSF_XML_IN_NODE (OFFICE_STYLES, TIME_STYLE, OO_NS_NUMBER,	"time-style", GSF_XML_NO_CONTENT, &oo_date_style, &oo_date_style_end),
	      GSF_XML_IN_NODE (TIME_STYLE, TIME_HOURS, OO_NS_NUMBER,	"hours", GSF_XML_NO_CONTENT,	&oo_date_hours, NULL),
	      GSF_XML_IN_NODE (TIME_STYLE, TIME_MINUTES, OO_NS_NUMBER,	"minutes", GSF_XML_NO_CONTENT, &oo_date_minutes, NULL),
	      GSF_XML_IN_NODE (TIME_STYLE, TIME_SECONDS, OO_NS_NUMBER,	"seconds", GSF_XML_NO_CONTENT, &oo_date_seconds, NULL),
	      GSF_XML_IN_NODE (TIME_STYLE, TIME_AM_PM, OO_NS_NUMBER,	"am-pm", GSF_XML_NO_CONTENT,	&oo_date_am_pm, NULL),
	      GSF_XML_IN_NODE_FULL (TIME_STYLE, TIME_TEXT, OO_NS_NUMBER, "text", GSF_XML_CONTENT, FALSE, FALSE, &odf_format_text_start, &oo_format_text_end, GO_FORMAT_TIME),
	      GSF_XML_IN_NODE (TIME_STYLE, TIME_TEXT_PROP, OO_NS_STYLE,	"text-properties", GSF_XML_NO_CONTENT, &odf_number_color, NULL),
	      GSF_XML_IN_NODE (TIME_STYLE, TIME_MAP, OO_NS_STYLE,	"map", GSF_XML_NO_CONTENT, NULL, NULL),
              GSF_XML_IN_NODE (TIME_STYLE, TIME_FILL_CHARACTER, OO_NS_NUMBER, "fill-character", GSF_XML_NO_CONTENT, NULL, NULL),
	    GSF_XML_IN_NODE (OFFICE_STYLES, STYLE_BOOL, OO_NS_NUMBER,	"boolean-style", GSF_XML_NO_CONTENT, NULL, NULL),
	      GSF_XML_IN_NODE (STYLE_BOOL, BOOL_PROP, OO_NS_NUMBER,	"boolean", GSF_XML_NO_CONTENT, NULL, NULL),
	    GSF_XML_IN_NODE (OFFICE_STYLES, STYLE_CURRENCY, OO_NS_NUMBER,      	"currency-style", GSF_XML_NO_CONTENT, &odf_number_style, &odf_number_style_end),
	      GSF_XML_IN_NODE (STYLE_CURRENCY, CURRENCY_STYLE, OO_NS_NUMBER,	"number", GSF_XML_NO_CONTENT, &odf_number, NULL),
	      GSF_XML_IN_NODE (STYLE_CURRENCY, CURRENCY_STYLE_PROP, OO_NS_STYLE,"properties", GSF_XML_NO_CONTENT, NULL, NULL),
	      GSF_XML_IN_NODE (STYLE_CURRENCY, CURRENCY_MAP, OO_NS_STYLE,	"map", GSF_XML_NO_CONTENT, &odf_map, NULL),
	      GSF_XML_IN_NODE (STYLE_CURRENCY, CURRENCY_SYMBOL, OO_NS_NUMBER,	"currency-symbol", GSF_XML_CONTENT, NULL, &odf_currency_symbol_end),
	GSF_XML_IN_NODE_FULL (STYLE_CURRENCY, CURRENCY_TEXT, OO_NS_NUMBER,"text", GSF_XML_CONTENT, FALSE, FALSE, &odf_format_text_start, &oo_format_text_end, GO_FORMAT_CURRENCY),
	      GSF_XML_IN_NODE (STYLE_CURRENCY, CURRENCY_TEXT_PROP, OO_NS_STYLE,	"text-properties", GSF_XML_NO_CONTENT, &odf_number_color, NULL),
              GSF_XML_IN_NODE (STYLE_CURRENCY, CURRENCY_FILL_CHARACTER, OO_NS_NUMBER, "fill-character", GSF_XML_NO_CONTENT, NULL, NULL),
	    GSF_XML_IN_NODE (OFFICE_STYLES, STYLE_PERCENTAGE, OO_NS_NUMBER, "percentage-style", GSF_XML_NO_CONTENT, &odf_number_percentage_style, &odf_number_style_end),
	      GSF_XML_IN_NODE (STYLE_PERCENTAGE, PERCENTAGE_STYLE_PROP, OO_NS_NUMBER,	"number", GSF_XML_NO_CONTENT, &odf_number, NULL),
	GSF_XML_IN_NODE_FULL (STYLE_PERCENTAGE, PERCENTAGE_TEXT, OO_NS_NUMBER,	"text", GSF_XML_CONTENT, FALSE, FALSE, &odf_format_text_start, &oo_format_text_end, GO_FORMAT_PERCENTAGE),
	      GSF_XML_IN_NODE (STYLE_PERCENTAGE, PERCENTAGE_MAP, OO_NS_STYLE,		"map", GSF_XML_NO_CONTENT, &odf_map, NULL),
	      GSF_XML_IN_NODE (STYLE_PERCENTAGE, PERCENTAGE_TEXT_PROP, OO_NS_STYLE,	"text-properties", GSF_XML_NO_CONTENT, &odf_number_color, NULL),
              GSF_XML_IN_NODE (STYLE_PERCENTAGE, PERCENTAGE_FILL_CHARACTER, OO_NS_NUMBER, "fill-character", GSF_XML_NO_CONTENT, NULL, NULL),
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
 	        GSF_XML_IN_NODE (CONTENT_VALIDATION, ERROR_MESSAGE, OO_NS_TABLE, "error-message", GSF_XML_NO_CONTENT, &odf_validation_error_message , &odf_validation_error_message_end),
	            GSF_XML_IN_NODE (ERROR_MESSAGE, TEXT_CONTENT, OO_NS_TEXT, "p", GSF_XML_CONTENT, &odf_text_content_start, &odf_text_content_end),
  		    GSF_XML_IN_NODE (TEXT_CONTENT, TEXT_S,    OO_NS_TEXT, "s", GSF_XML_NO_CONTENT,  &odf_text_space, NULL),
	            GSF_XML_IN_NODE_FULL (TEXT_CONTENT, TEXT_LINE_BREAK, OO_NS_TEXT, "line-break", GSF_XML_NO_CONTENT, FALSE, FALSE, &odf_text_symbol, NULL, .v_str = "\n"),
	            GSF_XML_IN_NODE_FULL (TEXT_CONTENT, TEXT_TAB,  OO_NS_TEXT, "tab", GSF_XML_SHARED_CONTENT, FALSE, FALSE, odf_text_symbol, NULL, .v_str = "\t"),
		    GSF_XML_IN_NODE (TEXT_CONTENT, TEXT_SPAN,      OO_NS_TEXT, "span", GSF_XML_SHARED_CONTENT, &odf_text_span_start, &odf_text_span_end),
		      GSF_XML_IN_NODE (TEXT_SPAN, TEXT_SPAN, OO_NS_TEXT, "span", GSF_XML_2ND, NULL, NULL),
		      GSF_XML_IN_NODE (TEXT_SPAN, TEXT_S,    OO_NS_TEXT, "s", GSF_XML_2ND, NULL, NULL),
		      GSF_XML_IN_NODE (TEXT_SPAN, TEXT_LINE_BREAK,    OO_NS_TEXT, "line-break", GSF_XML_2ND, NULL, NULL),
		      GSF_XML_IN_NODE (TEXT_SPAN, TEXT_TAB, OO_NS_TEXT, "tab", GSF_XML_2ND, NULL, NULL),
		      GSF_XML_IN_NODE (TEXT_SPAN, TEXT_ADDR, OO_NS_TEXT, "a", GSF_XML_SHARED_CONTENT, &oo_cell_content_link, NULL),
	                GSF_XML_IN_NODE (TEXT_ADDR, TEXT_S,    OO_NS_TEXT, "s", GSF_XML_2ND, NULL, NULL),
		        GSF_XML_IN_NODE (TEXT_ADDR, TEXT_TAB, OO_NS_TEXT, "tab", GSF_XML_2ND, NULL, NULL),
	                GSF_XML_IN_NODE (TEXT_ADDR, TEXT_SPAN, OO_NS_TEXT, "span", GSF_XML_2ND, NULL, NULL),
 	        GSF_XML_IN_NODE (CONTENT_VALIDATION, HELP_MESSAGE, OO_NS_TABLE, "help-message", GSF_XML_NO_CONTENT, &odf_validation_help_message , &odf_validation_help_message_end),
	            GSF_XML_IN_NODE (HELP_MESSAGE, TEXT_CONTENT, OO_NS_TEXT, "p", GSF_XML_2ND, NULL, NULL),
 	    GSF_XML_IN_NODE (SPREADSHEET, CALC_SETTINGS, OO_NS_TABLE, "calculation-settings", GSF_XML_NO_CONTENT, NULL, NULL),
	      GSF_XML_IN_NODE (CALC_SETTINGS, ITERATION, OO_NS_TABLE, "iteration", GSF_XML_NO_CONTENT, &oo_iteration, NULL),
	      GSF_XML_IN_NODE (CALC_SETTINGS, DATE_CONVENTION, OO_NS_TABLE, "null-date", GSF_XML_NO_CONTENT, &oo_date_convention, NULL),
	    GSF_XML_IN_NODE (SPREADSHEET, CHART, OO_NS_CHART, "chart", GSF_XML_NO_CONTENT, NULL, NULL),
	  GSF_XML_IN_NODE (OFFICE_BODY, OFFICE_CHART, OO_NS_OFFICE, "chart", GSF_XML_NO_CONTENT, NULL, NULL),
	    GSF_XML_IN_NODE (OFFICE_CHART, CHART_CHART, OO_NS_CHART, "chart", GSF_XML_NO_CONTENT, &oo_chart, &oo_chart_end),
	      GSF_XML_IN_NODE (CHART_CHART, CHART_TABLE, OO_NS_TABLE, "table", GSF_XML_NO_CONTENT, NULL, NULL),
	        GSF_XML_IN_NODE (CHART_TABLE, CHART_TABLE_ROWS, OO_NS_TABLE, "table-rows", GSF_XML_NO_CONTENT, NULL, NULL),
	          GSF_XML_IN_NODE (CHART_TABLE_ROWS, CHART_TABLE_ROW, OO_NS_TABLE, "table-row", GSF_XML_NO_CONTENT, NULL, NULL),
	            GSF_XML_IN_NODE (CHART_TABLE_ROW, CHART_TABLE_CELL, OO_NS_TABLE, "table-cell", GSF_XML_NO_CONTENT, NULL, NULL),
	              GSF_XML_IN_NODE (CHART_TABLE_CELL, CHART_CELL_P, OO_NS_TEXT, "p", GSF_XML_NO_CONTENT, NULL, NULL),
	              GSF_XML_IN_NODE (CHART_TABLE_CELL, CHART_CELL_DRAW_G, OO_NS_DRAW, "g", GSF_XML_NO_CONTENT, NULL, NULL),
	                GSF_XML_IN_NODE (CHART_CELL_DRAW_G, CHART_CELL_SVG_DESC, OO_NS_SVG, "desc", GSF_XML_NO_CONTENT, NULL, NULL),

	        GSF_XML_IN_NODE (CHART_TABLE, CHART_TABLE_COLS, OO_NS_TABLE, "table-columns", GSF_XML_NO_CONTENT, NULL, NULL),
	          GSF_XML_IN_NODE (CHART_TABLE_COLS, CHART_TABLE_COL, OO_NS_TABLE, "table-column", GSF_XML_NO_CONTENT, NULL, NULL),
	        GSF_XML_IN_NODE (CHART_TABLE, CHART_TABLE_HROWS, OO_NS_TABLE, "table-header-rows", GSF_XML_NO_CONTENT, NULL, NULL),
	          GSF_XML_IN_NODE (CHART_TABLE_HROWS, CHART_TABLE_HROW, OO_NS_TABLE, "table-header-row", GSF_XML_NO_CONTENT, NULL, NULL),
	          GSF_XML_IN_NODE (CHART_TABLE_HROWS, CHART_TABLE_ROW, OO_NS_TABLE, "table-row", GSF_XML_2ND, NULL, NULL),
	        GSF_XML_IN_NODE (CHART_TABLE, CHART_TABLE_HCOLS, OO_NS_TABLE, "table-header-columns", GSF_XML_NO_CONTENT, NULL, NULL),
	          GSF_XML_IN_NODE (CHART_TABLE_HCOLS, CHART_TABLE_HCOL, OO_NS_TABLE, "table-header-column", GSF_XML_NO_CONTENT, NULL, NULL),
	          GSF_XML_IN_NODE (CHART_TABLE_HCOLS, CHART_TABLE_COL, OO_NS_TABLE, "table-column", GSF_XML_2ND, NULL, NULL),

	      GSF_XML_IN_NODE_FULL (CHART_CHART, CHART_TITLE, OO_NS_CHART, "title", GSF_XML_NO_CONTENT, FALSE, FALSE, &oo_chart_title, &oo_chart_title_end, .v_int = 0),
	        GSF_XML_IN_NODE (CHART_TITLE, TEXT_CONTENT, OO_NS_TEXT, "p", GSF_XML_2ND, NULL, NULL),
	      GSF_XML_IN_NODE_FULL (CHART_CHART, CHART_SUBTITLE, OO_NS_CHART, "subtitle", GSF_XML_NO_CONTENT, FALSE, FALSE, &oo_chart_title, &oo_chart_title_end, .v_int = 1),
	        GSF_XML_IN_NODE (CHART_SUBTITLE, TEXT_CONTENT, OO_NS_TEXT, "p", GSF_XML_2ND, NULL, NULL),
	      GSF_XML_IN_NODE_FULL (CHART_CHART, CHART_FOOTER, OO_NS_CHART, "footer", GSF_XML_NO_CONTENT, FALSE, FALSE, &oo_chart_title, &oo_chart_title_end, .v_int = 2),
	        GSF_XML_IN_NODE (CHART_FOOTER, TEXT_CONTENT, OO_NS_TEXT, "p", GSF_XML_2ND, NULL, NULL),
	      GSF_XML_IN_NODE (CHART_CHART, CHART_LEGEND, OO_NS_CHART, "legend", GSF_XML_NO_CONTENT, &oo_legend, NULL),
	        GSF_XML_IN_NODE (CHART_LEGEND, CHART_LEGEND_TITLE, OO_GNUM_NS_EXT, "title", GSF_XML_NO_CONTENT, &oo_chart_title, &oo_chart_title_end),
		  GSF_XML_IN_NODE (CHART_LEGEND_TITLE, TEXT_CONTENT, OO_NS_TEXT, "p", GSF_XML_2ND, NULL, NULL),
	      GSF_XML_IN_NODE (CHART_CHART, CHART_COLOR_SCALE, OO_GNUM_NS_EXT, "color-scale", GSF_XML_NO_CONTENT, &oo_color_scale, NULL),
	      GSF_XML_IN_NODE (CHART_CHART, CHART_PLOT_AREA, OO_NS_CHART, "plot-area", GSF_XML_NO_CONTENT, &oo_plot_area, &oo_plot_area_end),
		GSF_XML_IN_NODE (CHART_PLOT_AREA, CHART_SERIES, OO_NS_CHART, "series", GSF_XML_NO_CONTENT, &oo_plot_series, &oo_plot_series_end),
		  GSF_XML_IN_NODE (CHART_SERIES, SERIES_DOMAIN, OO_NS_CHART, "domain", GSF_XML_NO_CONTENT, &oo_series_domain, NULL),
		  GSF_XML_IN_NODE (CHART_SERIES, SERIES_DATA_PT, OO_NS_CHART, "data-point", GSF_XML_NO_CONTENT, &oo_series_pt, NULL),
		  GSF_XML_IN_NODE (CHART_SERIES, SERIES_DATA_ERR, OO_NS_CHART, "error-indicator", GSF_XML_NO_CONTENT, NULL, NULL),
		  GSF_XML_IN_NODE (CHART_SERIES, SERIES_REGRESSION, OO_NS_CHART, "regression-curve", GSF_XML_NO_CONTENT,  &od_series_regression, NULL),
	            GSF_XML_IN_NODE (SERIES_REGRESSION, SERIES_REG_EQ, OO_NS_CHART, "equation", GSF_XML_NO_CONTENT,  &od_series_reg_equation, NULL),
	            GSF_XML_IN_NODE (SERIES_REGRESSION, SERIES_REG_EQ_GNM, OO_GNUM_NS_EXT, "equation", GSF_XML_NO_CONTENT,  &od_series_reg_equation, NULL),
		  GSF_XML_IN_NODE (CHART_SERIES, SERIES_REGRESSION_MULTIPLE, OO_GNUM_NS_EXT, "regression-curve", GSF_XML_NO_CONTENT,  &od_series_regression, NULL),
	            GSF_XML_IN_NODE (SERIES_REGRESSION_MULTIPLE, SERIES_REG_EQ, OO_NS_CHART, "equation", GSF_XML_2ND, NULL, NULL),
	            GSF_XML_IN_NODE (SERIES_REGRESSION_MULTIPLE, SERIES_REG_EQ_GNM, OO_GNUM_NS_EXT, "equation", GSF_XML_2ND, NULL, NULL),
		  GSF_XML_IN_NODE (CHART_SERIES, SERIES_DROPLINES, OO_GNUM_NS_EXT, "droplines", GSF_XML_NO_CONTENT, &oo_series_droplines, NULL),
		  GSF_XML_IN_NODE (CHART_SERIES, SERIES_SERIESLINES, OO_GNUM_NS_EXT, "serieslines", GSF_XML_NO_CONTENT, &oo_series_serieslines, NULL),
		GSF_XML_IN_NODE (CHART_PLOT_AREA, CHART_WALL, OO_NS_CHART, "wall", GSF_XML_NO_CONTENT, &oo_chart_wall, NULL),
		GSF_XML_IN_NODE (CHART_PLOT_AREA, CHART_FLOOR, OO_NS_CHART, "floor", GSF_XML_NO_CONTENT, NULL, NULL),
		GSF_XML_IN_NODE (CHART_PLOT_AREA, CHART_AXIS, OO_NS_CHART, "axis", GSF_XML_NO_CONTENT, &oo_chart_axis, &oo_chart_axis_end),
		GSF_XML_IN_NODE (CHART_PLOT_AREA, GNM_CHART_AXIS, OO_GNUM_NS_EXT, "axis", GSF_XML_NO_CONTENT, &oo_chart_axis, &oo_chart_axis_end),
		  GSF_XML_IN_NODE (CHART_AXIS, CHART_AXIS_LINE, OO_GNUM_NS_EXT, "axisline", GSF_XML_NO_CONTENT, &oo_chart_axisline, NULL),
		  GSF_XML_IN_NODE (CHART_AXIS, CHART_GRID, OO_NS_CHART, "grid", GSF_XML_NO_CONTENT, &oo_chart_grid, NULL),
		  GSF_XML_IN_NODE (CHART_AXIS, CHART_AXIS_CAT,   OO_NS_CHART, "categories", GSF_XML_NO_CONTENT, &od_chart_axis_categories, NULL),
	          GSF_XML_IN_NODE_FULL (CHART_AXIS, CHART_AXIS_TITLE, OO_NS_CHART, "title", GSF_XML_NO_CONTENT, FALSE, FALSE, &oo_chart_title, &oo_chart_title_end, .v_int = 3),
		  GSF_XML_IN_NODE (GNM_CHART_AXIS, GNM_CHART_AXIS_LINE, OO_GNUM_NS_EXT, "axisline", GSF_XML_NO_CONTENT, &oo_chart_axisline, NULL),
		  GSF_XML_IN_NODE (GNM_CHART_AXIS, GNM_CHART_GRID, OO_NS_CHART, "grid", GSF_XML_NO_CONTENT, &oo_chart_grid, NULL),
		  GSF_XML_IN_NODE (GNM_CHART_AXIS, GNM_CHART_AXIS_CAT,   OO_NS_CHART, "categories", GSF_XML_NO_CONTENT, &od_chart_axis_categories, NULL),
	          GSF_XML_IN_NODE_FULL (GNM_CHART_AXIS, GNM_CHART_AXIS_TITLE, OO_NS_CHART, "title", GSF_XML_NO_CONTENT, FALSE, FALSE, &oo_chart_title, &oo_chart_title_end, .v_int = 3),
	            GSF_XML_IN_NODE (CHART_AXIS_TITLE, TEXT_CONTENT, OO_NS_TEXT, "p", GSF_XML_2ND, NULL, NULL),
	        GSF_XML_IN_NODE (CHART_PLOT_AREA, CHART_OOO_COORDINATE_REGION, OO_NS_CHART_OOO, "coordinate-region", GSF_XML_NO_CONTENT, NULL, NULL),
	    GSF_XML_IN_NODE (SPREADSHEET, TABLE, OO_NS_TABLE, "table", GSF_XML_NO_CONTENT, &oo_table_start, &oo_table_end),
	      GSF_XML_IN_NODE (TABLE, SHEET_SELECTIONS, OO_GNUM_NS_EXT, "selections", GSF_XML_NO_CONTENT, &odf_selection, &odf_selection_end),
	        GSF_XML_IN_NODE (SHEET_SELECTIONS, SELECTION, OO_GNUM_NS_EXT, "selection", GSF_XML_NO_CONTENT, &odf_selection_range, NULL),
	      GSF_XML_IN_NODE (TABLE, TABLE_SOURCE, OO_NS_TABLE, "table-source", GSF_XML_NO_CONTENT, NULL, NULL),
	      GSF_XML_IN_NODE (TABLE, TABLE_SHAPES, OO_NS_TABLE, "shapes", GSF_XML_NO_CONTENT, &odf_shapes, &odf_shapes_end),
                  GSF_XML_IN_NODE (TABLE_SHAPES, DRAW_CONTROL_SHAPES, OO_NS_DRAW, "control", GSF_XML_NO_CONTENT, NULL, NULL),
	          GSF_XML_IN_NODE (TABLE_SHAPES, DRAW_FRAME, OO_NS_DRAW, "frame", GSF_XML_NO_CONTENT, &od_draw_frame_start, &od_draw_frame_end),
		  GSF_XML_IN_NODE (TABLE_SHAPES, DRAW_CAPTION, OO_NS_DRAW, "caption", GSF_XML_NO_CONTENT, &odf_caption, &od_draw_text_frame_end),
	            GSF_XML_IN_NODE (DRAW_CAPTION, TEXT_CONTENT, OO_NS_TEXT, "p", GSF_XML_2ND, NULL, NULL),
		  GSF_XML_IN_NODE (TABLE_SHAPES, DRAW_CUSTOM_SHAPE, OO_NS_DRAW, "custom-shape", GSF_XML_NO_CONTENT, &odf_custom_shape, &odf_custom_shape_end),
	            GSF_XML_IN_NODE (DRAW_CUSTOM_SHAPE, TEXT_CONTENT, OO_NS_TEXT, "p", GSF_XML_2ND, NULL, NULL),
	            GSF_XML_IN_NODE (DRAW_CUSTOM_SHAPE, DRAW_ENHANCED_GEOMETRY, OO_NS_DRAW, "enhanced-geometry", GSF_XML_NO_CONTENT, &odf_custom_shape_enhanced_geometry, NULL),
	GSF_XML_IN_NODE (DRAW_ENHANCED_GEOMETRY, DRAW_ENHANCED_GEOMETRY_EQUATION, OO_NS_DRAW, "equation", GSF_XML_NO_CONTENT, odf_custom_shape_equation, NULL),
	GSF_XML_IN_NODE (DRAW_ENHANCED_GEOMETRY, DRAW_ENHANCED_GEOMETRY_HANDLE, OO_NS_DRAW, "handle", GSF_XML_NO_CONTENT, NULL, NULL),
	          GSF_XML_IN_NODE (TABLE_SHAPES, DRAW_ELLIPSE, OO_NS_DRAW, "ellipse", GSF_XML_NO_CONTENT, &odf_ellipse, &od_draw_text_frame_end),
	            GSF_XML_IN_NODE (DRAW_ELLIPSE, TEXT_CONTENT, OO_NS_TEXT, "p", GSF_XML_2ND, NULL, NULL),
	          GSF_XML_IN_NODE (TABLE_SHAPES, DRAW_LINE, OO_NS_DRAW, "line", GSF_XML_NO_CONTENT, &odf_line, &odf_line_end),
                    GSF_XML_IN_NODE (DRAW_LINE, TEXT_CONTENT, OO_NS_TEXT, "p", GSF_XML_2ND, NULL, NULL),
	          GSF_XML_IN_NODE (TABLE_SHAPES, DRAW_RECT, OO_NS_DRAW, "rect", GSF_XML_NO_CONTENT, &odf_rect, &od_draw_text_frame_end),
	            GSF_XML_IN_NODE (DRAW_RECT, TEXT_CONTENT, OO_NS_TEXT, "p", GSF_XML_2ND, NULL, NULL),
	      GSF_XML_IN_NODE (TABLE, FORMS, OO_NS_OFFICE, "forms", GSF_XML_NO_CONTENT, NULL, NULL),
	        GSF_XML_IN_NODE (FORMS, FORM, OO_NS_FORM, "form", GSF_XML_NO_CONTENT, NULL, NULL),
	          GSF_XML_IN_NODE (FORM, FORM_PROPERTIES, OO_NS_FORM, "properties", GSF_XML_NO_CONTENT, NULL, NULL),
	            GSF_XML_IN_NODE (FORM_PROPERTIES, FORM_PROPERTY, OO_NS_FORM, "property", GSF_XML_NO_CONTENT, &odf_control_property, NULL),
	              GSF_XML_IN_NODE (FORM_PROPERTIES, FORM_LIST_PROPERTY, OO_NS_FORM, "list-property", GSF_XML_NO_CONTENT, NULL, NULL),
	          GSF_XML_IN_NODE (FORM, FORM_BUTTON, OO_NS_FORM, "button", GSF_XML_NO_CONTENT, &odf_form_button, &odf_form_control_end),
	            GSF_XML_IN_NODE (FORM_BUTTON, FORM_PROPERTIES, OO_NS_FORM, "properties", GSF_XML_2ND, NULL, NULL),
	            GSF_XML_IN_NODE (FORM_BUTTON, BUTTON_OFFICE_EVENT_LISTENERS, OO_NS_OFFICE, "event-listeners", GSF_XML_NO_CONTENT, NULL, NULL),
	              GSF_XML_IN_NODE (BUTTON_OFFICE_EVENT_LISTENERS, BUTTON_EVENT_LISTENER, OO_NS_SCRIPT, "event-listener", GSF_XML_NO_CONTENT, &odf_button_event_listener, NULL),
	          GSF_XML_IN_NODE (FORM, FORM_VALUE_RANGE, OO_NS_FORM, "value-range", GSF_XML_NO_CONTENT, &odf_form_value_range, NULL),
	            GSF_XML_IN_NODE (FORM_VALUE_RANGE, FORM_PROPERTIES, OO_NS_FORM, "properties", GSF_XML_2ND, NULL, NULL),
	          GSF_XML_IN_NODE (FORM, FORM_CHECKBOX, OO_NS_FORM, "checkbox", GSF_XML_NO_CONTENT, &odf_form_checkbox, NULL),
	            GSF_XML_IN_NODE (FORM_CHECKBOX, FORM_PROPERTIES, OO_NS_FORM, "properties", GSF_XML_2ND, NULL, NULL),
	          GSF_XML_IN_NODE (FORM, FORM_RADIO, OO_NS_FORM, "radio", GSF_XML_NO_CONTENT, &odf_form_radio, NULL),
	            GSF_XML_IN_NODE (FORM_RADIO, FORM_PROPERTIES, OO_NS_FORM, "properties", GSF_XML_2ND, NULL, NULL),
	          GSF_XML_IN_NODE (FORM, FORM_LISTBOX, OO_NS_FORM, "listbox", GSF_XML_NO_CONTENT, &odf_form_listbox, NULL),
	            GSF_XML_IN_NODE (FORM_LISTBOX, FORM_PROPERTIES, OO_NS_FORM, "properties", GSF_XML_2ND, NULL, NULL),
	          GSF_XML_IN_NODE (FORM, FORM_COMBOBOX, OO_NS_FORM, "combobox", GSF_XML_NO_CONTENT, &odf_form_combobox, NULL),
	            GSF_XML_IN_NODE (FORM_COMBOBOX, FORM_PROPERTIES, OO_NS_FORM, "properties", GSF_XML_2ND, NULL, NULL),
	          GSF_XML_IN_NODE (FORM, FORM_GENERIC, OO_NS_FORM, "generic-control", GSF_XML_NO_CONTENT, &odf_form_generic, &odf_form_control_end),
	            GSF_XML_IN_NODE (FORM_GENERIC, FORM_PROPERTIES, OO_NS_FORM, "properties", GSF_XML_2ND, NULL, NULL),
	      GSF_XML_IN_NODE (TABLE, TABLE_ROWS, OO_NS_TABLE, "table-rows", GSF_XML_NO_CONTENT, NULL, NULL),
	      GSF_XML_IN_NODE (TABLE, TABLE_H_ROWS, OO_NS_TABLE, "table-header-rows", GSF_XML_NO_CONTENT, &odf_table_header_rows, &odf_table_header_rows_end),
	      GSF_XML_IN_NODE (TABLE, TABLE_COL, OO_NS_TABLE, "table-column", GSF_XML_NO_CONTENT, &oo_col_start, NULL),
	      GSF_XML_IN_NODE (TABLE, TABLE_COLS, OO_NS_TABLE, "table-columns", GSF_XML_NO_CONTENT, NULL, NULL),
	      GSF_XML_IN_NODE (TABLE, TABLE_H_COLS, OO_NS_TABLE, "table-header-columns", GSF_XML_NO_CONTENT, &odf_table_header_cols, &odf_table_header_cols_end),
	      GSF_XML_IN_NODE (TABLE_H_COLS, TABLE_COL, OO_NS_TABLE, "table-column", GSF_XML_2ND, NULL, NULL),
	      GSF_XML_IN_NODE (TABLE_COLS, TABLE_COL, OO_NS_TABLE, "table-column", GSF_XML_2ND, NULL, NULL),
	      GSF_XML_IN_NODE (TABLE, TABLE_ROW, OO_NS_TABLE, "table-row", GSF_XML_NO_CONTENT, &oo_row_start, &oo_row_end),
	      GSF_XML_IN_NODE (TABLE, SOFTPAGEBREAK, OO_NS_TEXT, "soft-page-break", GSF_XML_NO_CONTENT, NULL, NULL),
	      GSF_XML_IN_NODE (TABLE_ROWS, TABLE_ROW, OO_NS_TABLE, "table-row", GSF_XML_2ND, NULL, NULL),
	      GSF_XML_IN_NODE (TABLE_H_ROWS, TABLE_ROW, OO_NS_TABLE, "table-row", GSF_XML_2ND, NULL, NULL),
	      GSF_XML_IN_NODE (TABLE_ROWS, SOFTPAGEBREAK, OO_NS_TEXT, "soft-page-break", GSF_XML_2ND, NULL, NULL),
	      GSF_XML_IN_NODE (TABLE_H_ROWS, SOFTPAGEBREAK, OO_NS_TEXT, "soft-page-break", GSF_XML_2ND, NULL, NULL),
		GSF_XML_IN_NODE (TABLE_ROW, TABLE_CELL, OO_NS_TABLE, "table-cell", GSF_XML_NO_CONTENT, &oo_cell_start, &oo_cell_end),
		  GSF_XML_IN_NODE (TABLE_CELL, DETECTIVE, OO_NS_TABLE, "detective", GSF_XML_NO_CONTENT, NULL, NULL),
	            GSF_XML_IN_NODE (DETECTIVE, DETECTIVE_OPERATION, OO_NS_TABLE, "operation", GSF_XML_NO_CONTENT, NULL, NULL),
		  GSF_XML_IN_NODE (TABLE_CELL, DRAW_CUSTOM_SHAPE, OO_NS_DRAW, "custom-shape", GSF_XML_2ND, NULL, NULL),
		  GSF_XML_IN_NODE (TABLE_CELL, CELL_TEXT, OO_NS_TEXT, "p", GSF_XML_CONTENT, &oo_cell_content_start, &oo_cell_content_end),
		    GSF_XML_IN_NODE (CELL_TEXT, DRAW_CUSTOM_SHAPE, OO_NS_DRAW, "custom-shape", GSF_XML_2ND, NULL, NULL),
		    GSF_XML_IN_NODE (CELL_TEXT, TEXT_S,   OO_NS_TEXT, "s", GSF_XML_2ND, NULL, NULL),
		    GSF_XML_IN_NODE (CELL_TEXT, TEXT_ADDR, OO_NS_TEXT, "a", GSF_XML_2ND, NULL, NULL),
	            GSF_XML_IN_NODE (CELL_TEXT, TEXT_LINE_BREAK, OO_NS_TEXT, "line-break", GSF_XML_2ND, NULL, NULL),
	            GSF_XML_IN_NODE (CELL_TEXT, TEXT_TAB,  OO_NS_TEXT, "tab", GSF_XML_2ND,NULL, NULL ),
		    GSF_XML_IN_NODE (CELL_TEXT, TEXT_SPAN, OO_NS_TEXT, "span", GSF_XML_2ND, NULL, NULL),
		  GSF_XML_IN_NODE (TABLE_CELL, CELL_OBJECT, OO_NS_DRAW, "object", GSF_XML_NO_CONTENT, NULL, NULL),		/* ignore for now */
		  GSF_XML_IN_NODE (TABLE_CELL, CELL_GRAPHIC, OO_NS_DRAW, "g", GSF_XML_NO_CONTENT, NULL, NULL),			/* ignore for now */
		    GSF_XML_IN_NODE (CELL_GRAPHIC, CELL_GRAPHIC, OO_NS_DRAW, "g", GSF_XML_2ND, NULL, NULL),
		    GSF_XML_IN_NODE (CELL_GRAPHIC, DRAW_POLYLINE, OO_NS_DRAW, "polyline", GSF_XML_NO_CONTENT, NULL, NULL),
	            GSF_XML_IN_NODE (CELL_GRAPHIC, DRAW_CONTROL, OO_NS_DRAW, "control", GSF_XML_NO_CONTENT, &od_draw_control_start, NULL),
	          GSF_XML_IN_NODE (TABLE_CELL, DRAW_CONTROL, OO_NS_DRAW, "control", GSF_XML_2ND, NULL, NULL),
	          GSF_XML_IN_NODE (TABLE_CELL, DRAW_RECT, OO_NS_DRAW, "rect", GSF_XML_2ND, NULL, NULL),
	          GSF_XML_IN_NODE (TABLE_CELL, DRAW_LINE, OO_NS_DRAW, "line", GSF_XML_2ND, NULL, NULL),
	          GSF_XML_IN_NODE (TABLE_CELL, DRAW_ELLIPSE, OO_NS_DRAW, "ellipse", GSF_XML_2ND, NULL, NULL),
	          GSF_XML_IN_NODE (TABLE_CELL, DRAW_FRAME, OO_NS_DRAW, "frame", GSF_XML_2ND, NULL, NULL),
		    GSF_XML_IN_NODE (DRAW_FRAME, DRAW_OBJECT, OO_NS_DRAW, "object", GSF_XML_NO_CONTENT, &od_draw_object, NULL),
	            GSF_XML_IN_NODE (DRAW_OBJECT, DRAW_OBJECT_TEXT, OO_NS_TEXT, "p", GSF_XML_CONTENT, NULL, NULL),

		    GSF_XML_IN_NODE (DRAW_FRAME, DRAW_IMAGE, OO_NS_DRAW, "image", GSF_XML_NO_CONTENT, &od_draw_image, NULL),
	              GSF_XML_IN_NODE (DRAW_IMAGE, DRAW_IMAGE_TEXT,OO_NS_TEXT, "p", GSF_XML_NO_CONTENT, NULL, NULL),
		    GSF_XML_IN_NODE (DRAW_FRAME, SVG_DESC, OO_NS_SVG, "desc", GSF_XML_NO_CONTENT, NULL, NULL),
		    GSF_XML_IN_NODE (DRAW_FRAME, SVG_TITLE, OO_NS_SVG, "title", GSF_XML_NO_CONTENT, NULL, NULL),
		    GSF_XML_IN_NODE (DRAW_FRAME, DRAW_TEXT_BOX, OO_NS_DRAW, "text-box", GSF_XML_NO_CONTENT, &od_draw_text_box, od_draw_text_frame_end),
	            GSF_XML_IN_NODE (DRAW_TEXT_BOX, TEXT_CONTENT, OO_NS_TEXT, "p", GSF_XML_2ND, NULL, NULL),
	          GSF_XML_IN_NODE (TABLE_CELL, CELL_ANNOTATION, OO_NS_OFFICE, "annotation", GSF_XML_NO_CONTENT, &odf_annotation_start, &odf_annotation_end),
	            GSF_XML_IN_NODE (CELL_ANNOTATION, TEXT_CONTENT, OO_NS_TEXT, "p", GSF_XML_2ND, NULL, NULL),
	            GSF_XML_IN_NODE (CELL_ANNOTATION, CELL_ANNOTATION_AUTHOR, OO_NS_DC, "creator", GSF_XML_CONTENT, NULL, &odf_annotation_author_end),
	            GSF_XML_IN_NODE (CELL_ANNOTATION, CELL_ANNOTATION_DATE, OO_NS_DC, "date", GSF_XML_NO_CONTENT, NULL, NULL),

		GSF_XML_IN_NODE (TABLE_ROW, TABLE_COVERED_CELL, OO_NS_TABLE, "covered-table-cell", GSF_XML_NO_CONTENT, &oo_covered_cell_start, &oo_covered_cell_end),
		  GSF_XML_IN_NODE (TABLE_COVERED_CELL, COVERED_CELL_TEXT, OO_NS_TEXT, "p", GSF_XML_NO_CONTENT, NULL, NULL),
		    GSF_XML_IN_NODE (COVERED_CELL_TEXT, COVERED_CELL_TEXT_S,    OO_NS_TEXT, "s", GSF_XML_NO_CONTENT, NULL, NULL),
	          GSF_XML_IN_NODE (TABLE_COVERED_CELL, DRAW_CONTROL, OO_NS_DRAW, "control", GSF_XML_NO_CONTENT, NULL, NULL),

	      GSF_XML_IN_NODE (TABLE, TABLE_COL_GROUP, OO_NS_TABLE, "table-column-group", GSF_XML_NO_CONTENT, NULL, NULL),
		GSF_XML_IN_NODE (TABLE_COL_GROUP, TABLE_COL_GROUP, OO_NS_TABLE, "table-column-group", GSF_XML_NO_CONTENT, NULL, NULL),
	      GSF_XML_IN_NODE (TABLE_COL_GROUP, TABLE_H_COLS, OO_NS_TABLE, "table-header-columns", GSF_XML_2ND, NULL, NULL),
		GSF_XML_IN_NODE (TABLE_COL_GROUP, TABLE_COL, OO_NS_TABLE, "table-column", GSF_XML_2ND, NULL, NULL),
	      GSF_XML_IN_NODE (TABLE_ROW_GROUP, TABLE_ROW_GROUP, OO_NS_TABLE, "table-row-group", GSF_XML_NO_CONTENT, NULL, NULL),
	    GSF_XML_IN_NODE (TABLE, TABLE_ROW_GROUP,	      OO_NS_TABLE, "table-row-group", GSF_XML_NO_CONTENT, NULL, NULL),
	      GSF_XML_IN_NODE (TABLE_ROW_GROUP, TABLE_ROW,	    OO_NS_TABLE, "table-row", GSF_XML_2ND, NULL, NULL),
	  GSF_XML_IN_NODE (TABLE, NAMED_EXPRS, OO_NS_TABLE, "named-expressions", GSF_XML_NO_CONTENT, NULL, NULL),
	  GSF_XML_IN_NODE (SPREADSHEET, NAMED_EXPRS, OO_NS_TABLE, "named-expressions", GSF_XML_2ND, NULL, NULL),
	    GSF_XML_IN_NODE (NAMED_EXPRS, NAMED_EXPR, OO_NS_TABLE, "named-expression", GSF_XML_NO_CONTENT, &oo_named_expr, NULL),
	    GSF_XML_IN_NODE (NAMED_EXPRS, NAMED_RANGE, OO_NS_TABLE, "named-range", GSF_XML_NO_CONTENT, &oo_named_expr, NULL),

	  GSF_XML_IN_NODE (SPREADSHEET, DB_RANGES, OO_NS_TABLE, "database-ranges", GSF_XML_NO_CONTENT, NULL, NULL),
	    GSF_XML_IN_NODE (DB_RANGES, DB_RANGE, OO_NS_TABLE, "database-range", GSF_XML_NO_CONTENT, &oo_db_range_start, &oo_db_range_end),
	      GSF_XML_IN_NODE (DB_RANGE, FILTER, OO_NS_TABLE, "filter", GSF_XML_NO_CONTENT, NULL, NULL),
		GSF_XML_IN_NODE (FILTER, FILTER_COND, OO_NS_TABLE, "filter-condition", GSF_XML_NO_CONTENT, &oo_filter_cond, NULL),
		GSF_XML_IN_NODE (FILTER, FILTER_AND, OO_NS_TABLE, "filter-and", GSF_XML_NO_CONTENT, NULL, NULL),
		   GSF_XML_IN_NODE (FILTER_AND, FILTER_OR, OO_NS_TABLE, "filter-or", GSF_XML_NO_CONTENT, &odf_filter_or, NULL),
                       GSF_XML_IN_NODE (FILTER_OR, FILTER_COND_IGNORE, OO_NS_TABLE, "filter-condition", GSF_XML_NO_CONTENT, NULL, NULL),
	               GSF_XML_IN_NODE (FILTER_OR, FILTER_AND_IGNORE, OO_NS_TABLE, "filter-or", GSF_XML_NO_CONTENT, NULL, NULL),
	           GSF_XML_IN_NODE (FILTER_AND, FILTER_COND, OO_NS_TABLE, "filter-condition", GSF_XML_2ND, NULL, NULL),
                GSF_XML_IN_NODE (FILTER, FILTER_OR, OO_NS_TABLE, "filter-or", GSF_XML_2ND, NULL, NULL),
	    GSF_XML_IN_NODE (DB_RANGE, TABLE_SORT, OO_NS_TABLE, "sort", GSF_XML_NO_CONTENT, NULL, NULL),
	      GSF_XML_IN_NODE (TABLE_SORT, SORT_BY, OO_NS_TABLE, "sort-by", GSF_XML_NO_CONTENT, NULL, NULL),

GSF_XML_IN_NODE_END
};

static GsfXMLInNode const opendoc_content_preparse_overrides[] =
{
	GSF_XML_IN_NODE (OFFICE_BODY, SPREADSHEET, OO_NS_OFFICE, "spreadsheet", GSF_XML_NO_CONTENT, NULL, &odf_preparse_spreadsheet_end),
	GSF_XML_IN_NODE (SPREADSHEET, TABLE, OO_NS_TABLE, "table", GSF_XML_NO_CONTENT, &odf_preparse_table_start, &odf_preparse_table_end),
	GSF_XML_IN_NODE (TABLE, TABLE_ROW, OO_NS_TABLE, "table-row", GSF_XML_NO_CONTENT, &odf_preparse_row_start, &odf_preparse_row_end),
	GSF_XML_IN_NODE (TABLE_ROW, TABLE_CELL, OO_NS_TABLE, "table-cell", GSF_XML_NO_CONTENT, &odf_preparse_cell_start, NULL),
	GSF_XML_IN_NODE (TABLE_ROW, TABLE_COVERED_CELL, OO_NS_TABLE, "covered-table-cell", GSF_XML_NO_CONTENT, &odf_preparse_covered_cell_start, NULL),
	GSF_XML_IN_NODE (TABLE, NAMED_EXPRS, OO_NS_TABLE, "named-expressions", GSF_XML_NO_CONTENT, &oo_named_exprs_preparse, NULL),
	GSF_XML_IN_NODE (NAMED_EXPRS, NAMED_EXPR, OO_NS_TABLE, "named-expression", GSF_XML_NO_CONTENT, &oo_named_expr_preparse, NULL),
	GSF_XML_IN_NODE (NAMED_EXPRS, NAMED_RANGE, OO_NS_TABLE, "named-range", GSF_XML_NO_CONTENT, &oo_named_expr_preparse, NULL),
	GSF_XML_IN_NODE_END
};
static GsfXMLInNode const *opendoc_content_preparse_dtd;


static GsfXMLInNode const ooo1_content_preparse_overrides [] =
{
	GSF_XML_IN_NODE (OFFICE_BODY, TABLE, OO_NS_TABLE, "table", GSF_XML_NO_CONTENT, &odf_preparse_table_start, &odf_preparse_table_end),
	GSF_XML_IN_NODE (TABLE, TABLE_ROW, OO_NS_TABLE, "table-row", GSF_XML_NO_CONTENT, &odf_preparse_row_start, &odf_preparse_row_end),
	GSF_XML_IN_NODE (TABLE_ROW, TABLE_CELL, OO_NS_TABLE, "table-cell", GSF_XML_NO_CONTENT, &odf_preparse_cell_start, NULL),
	GSF_XML_IN_NODE (TABLE_ROW, TABLE_COVERED_CELL, OO_NS_TABLE, "covered-table-cell", GSF_XML_NO_CONTENT, &odf_preparse_covered_cell_start, NULL),
	GSF_XML_IN_NODE_END
};
static GsfXMLInNode const *ooo1_content_preparse_dtd;


static GsfXMLInNode const *get_dtd () { return opendoc_content_dtd; }
static GsfXMLInNode const *get_styles_dtd () { return styles_dtd; }

/****************************************************************************/

static GnmExpr const *
odf_func_address_handler (GnmConventions const *convs, G_GNUC_UNUSED Workbook *scope, GnmExprList *args)
{
	guint argc = gnm_expr_list_length (args);

	if (argc == 4 && convs->sheet_name_sep == '!') {
		/* Openoffice was missing the A1 parameter */
		GnmExprList *new_args;
		GnmFunc  *f = gnm_func_lookup_or_add_placeholder ("ADDRESS");

		new_args = g_slist_insert ((GSList *) args,
					   (gpointer) gnm_expr_new_constant (value_new_int (1)),
					   3);
		return gnm_expr_new_funcall (f, new_args);
	}
	return NULL;
}

static GnmExpr const *
odf_func_phi_handler (G_GNUC_UNUSED GnmConventions const *convs, G_GNUC_UNUSED Workbook *scope, GnmExprList *args)
{
	GnmFunc  *f = gnm_func_lookup_or_add_placeholder ("NORMDIST");

	args = g_slist_append (args,
			       (gpointer) gnm_expr_new_constant (value_new_int (0)));
	args = g_slist_append (args,
			       (gpointer) gnm_expr_new_constant (value_new_int (1)));

	args = g_slist_append (args,
			       (gpointer) gnm_expr_new_funcall
			       (gnm_func_lookup_or_add_placeholder ("FALSE"), NULL));

	return gnm_expr_new_funcall (f, args);
}

static GnmExpr const *
odf_func_gauss_handler (G_GNUC_UNUSED GnmConventions const *convs, G_GNUC_UNUSED Workbook *scope, GnmExprList *args)
{
	guint argc = gnm_expr_list_length (args);
	GnmFunc  *f = gnm_func_lookup_or_add_placeholder ("ERF");
	GnmFunc  *fs = gnm_func_lookup_or_add_placeholder ("SQRT");
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
odf_func_floor_handler (G_GNUC_UNUSED GnmConventions const *convs, G_GNUC_UNUSED Workbook *scope, GnmExprList *args)
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

	fd_ceiling = gnm_func_lookup_or_add_placeholder ("CEILING");
	fd_floor = gnm_func_lookup_or_add_placeholder ("FLOOR");
	fd_if = gnm_func_lookup_or_add_placeholder ("IF");

	expr_x = g_slist_nth_data ((GSList *) args, 0);
	if (argc > 1)
		expr_sig = gnm_expr_copy (g_slist_nth_data ((GSList *) args, 1));
	else {
		GnmFunc  *fd_sign = gnm_func_lookup_or_add_placeholder ("SIGN");
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
			if (value == 0) {
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
odf_func_ceiling_handler (G_GNUC_UNUSED GnmConventions const *convs, G_GNUC_UNUSED Workbook *scope, GnmExprList *args)
{
	guint argc = gnm_expr_list_length (args);
	switch (argc) {
	case 1: {
		GnmFunc  *f = gnm_func_lookup_or_add_placeholder ("CEIL");
		return gnm_expr_new_funcall (f, args);
	}
	case 2: case 3: {
		GnmExpr const *expr_mode_zero;
		GnmExpr const *expr_mode_one;
		GnmExpr const *expr_if;
		GnmExpr const *expr_mode;
		GnmExpr const *expr_x = g_slist_nth_data ((GSList *) args, 0);
		GnmExpr const *expr_sig = g_slist_nth_data ((GSList *) args, 1);

		GnmFunc  *fd_ceiling = gnm_func_lookup_or_add_placeholder ("CEILING");
		GnmFunc  *fd_floor = gnm_func_lookup_or_add_placeholder ("FLOOR");
		GnmFunc  *fd_if = gnm_func_lookup_or_add_placeholder ("IF");

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
				if (value == 0) {
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
odf_func_chisqdist_handler (G_GNUC_UNUSED GnmConventions const *convs, G_GNUC_UNUSED Workbook *scope, GnmExprList *args)
{
	switch (gnm_expr_list_length (args)) {
	case 2: {
		GnmFunc  *f = gnm_func_lookup_or_add_placeholder ("R.PCHISQ");
		return gnm_expr_new_funcall (f, args);
	}
	case 3: {
		GnmExpr const *arg0 = args->data;
		GnmExpr const *arg1 = args->next->data;
		GnmExpr const *arg2 = args->next->next->data;
		GnmFunc  *fd_if;
		GnmFunc  *fd_pchisq;
		GnmFunc  *fd_dchisq;
		GnmExpr const *expr_pchisq;
		GnmExpr const *expr_dchisq;
		GnmExpr const *res, *simp;

		fd_if = gnm_func_lookup_or_add_placeholder ("IF");
		fd_pchisq = gnm_func_lookup_or_add_placeholder ("R.PCHISQ");
		fd_dchisq = gnm_func_lookup_or_add_placeholder ("R.DCHISQ");
		expr_pchisq = gnm_expr_new_funcall2
			(fd_pchisq,
			 gnm_expr_copy (arg0),
			 gnm_expr_copy (arg1));
		expr_dchisq = gnm_expr_new_funcall2
			(fd_dchisq,
			 arg0,
			 arg1);
		res = gnm_expr_new_funcall3
			(fd_if,
			 arg2,
			 expr_pchisq,
			 expr_dchisq);

		simp = gnm_expr_simplify_if (res);
		if (simp) {
			gnm_expr_free (res);
			res = simp;
		}

		g_slist_free (args);
		return res;
	}
	default:
		break;
	}
	return NULL;
}

static GnmExpr const *
odf_func_f_dist_handler (G_GNUC_UNUSED GnmConventions const *convs, G_GNUC_UNUSED Workbook *scope, GnmExprList *args)
{
	switch (gnm_expr_list_length (args)) {
	case 4: {
		GnmExpr const *arg0 = args->data;
		GnmExpr const *arg1 = args->next->data;
		GnmExpr const *arg2 = args->next->next->data;
		GnmExpr const *arg3 = args->next->next->next->data;
		GnmFunc  *fd_if;
		GnmFunc  *fd_pf;
		GnmFunc  *fd_df;
		GnmExpr const *expr_pf;
		GnmExpr const *expr_df;
		GnmExpr const *res, *simp;

		fd_if = gnm_func_lookup_or_add_placeholder ("IF");
		fd_pf = gnm_func_lookup_or_add_placeholder ("R.PF");
		fd_df = gnm_func_lookup_or_add_placeholder ("R.DF");
		expr_pf = gnm_expr_new_funcall3
			(fd_pf,
			 gnm_expr_copy (arg0),
			 gnm_expr_copy (arg1),
			 gnm_expr_copy (arg2));
		expr_df = gnm_expr_new_funcall3
			(fd_df,
			 arg0,
			 arg1,
			 arg2);
		res = gnm_expr_new_funcall3
			(fd_if,
			 arg3,
			 expr_pf,
			 expr_df);

		simp = gnm_expr_simplify_if (res);
		if (simp) {
			gnm_expr_free (res);
			res = simp;
		}

		g_slist_free (args);
		return res;
	}
	default:
		break;
	}
	return NULL;
}

static GnmExpr const *
odf_func_dist4_handler (GnmExprList *args, char const *fd_p_name, char const *fd_d_name)
{
	switch (gnm_expr_list_length (args)) {
	case 4: {
		GnmExpr const *arg0 = args->data;
		GnmExpr const *arg1 = args->next->data;
		GnmExpr const *arg2 = args->next->next->data;
		GnmExpr const *arg3 = args->next->next->next->data;
		GnmFunc  *fd_if;
		GnmFunc  *fd_p;
		GnmFunc  *fd_d;
		GnmExpr const *expr_p;
		GnmExpr const *expr_d;
		GnmExpr const *res, *simp;

		fd_if = gnm_func_lookup_or_add_placeholder ("IF");
		fd_p = gnm_func_lookup_or_add_placeholder (fd_p_name);
		fd_d = gnm_func_lookup_or_add_placeholder (fd_d_name);
		expr_p = gnm_expr_new_funcall3
			(fd_p,
			 gnm_expr_copy (arg0),
			 gnm_expr_copy (arg1),
			 gnm_expr_copy (arg2));
		expr_d = gnm_expr_new_funcall3
			(fd_d,
			 arg0,
			 arg1,
			 arg2);
		res = gnm_expr_new_funcall3
			(fd_if,
			 arg3,
			 expr_p,
			 expr_d);

		simp = gnm_expr_simplify_if (res);
		if (simp) {
			gnm_expr_free (res);
			res = simp;
		}

		g_slist_free (args);
		return res;
	}
	default:
		break;
	}
	return NULL;
}

static GnmExpr const *
odf_func_dist3_handler (GnmExprList *args, char const *fd_p_name, char const *fd_d_name)
{
	switch (gnm_expr_list_length (args)) {
	case 3: {
		GnmExpr const *arg0 = args->data;
		GnmExpr const *arg1 = args->next->data;
		GnmExpr const *arg2 = args->next->next->data;
		GnmFunc  *fd_if;
		GnmFunc  *fd_p;
		GnmFunc  *fd_d;
		GnmExpr const *expr_p;
		GnmExpr const *expr_d;
		GnmExpr const *res, *simp;

		fd_if = gnm_func_lookup_or_add_placeholder ("IF");
		fd_p = gnm_func_lookup_or_add_placeholder (fd_p_name);
		fd_d = gnm_func_lookup_or_add_placeholder (fd_d_name);
		expr_p = gnm_expr_new_funcall2
			(fd_p,
			 gnm_expr_copy (arg0),
			 gnm_expr_copy (arg1));
		expr_d = gnm_expr_new_funcall2
			(fd_d,
			 arg0,
			 arg1);
		res = gnm_expr_new_funcall3
			(fd_if,
			 arg2,
			 expr_p,
			 expr_d);

		simp = gnm_expr_simplify_if (res);
		if (simp) {
			gnm_expr_free (res);
			res = simp;
		}

		g_slist_free (args);
		return res;
	}
	default:
		break;
	}
	return NULL;
}

static GnmExpr const *
odf_func_norm_s_dist_handler (G_GNUC_UNUSED GnmConventions const *convs, G_GNUC_UNUSED Workbook *scope, GnmExprList *args)
{
	switch (gnm_expr_list_length (args)) {
	case 2: {
		GnmExpr const *arg0 = args->data;
		GnmExpr const *arg1 = args->next->data;
		GnmFunc  *fd_if;
		GnmFunc  *fd_p;
		GnmFunc  *fd_d;
		GnmExpr const *expr_p;
		GnmExpr const *expr_d;
		GnmExpr const *res, *simp;

		fd_if = gnm_func_lookup_or_add_placeholder ("IF");
		fd_p = gnm_func_lookup_or_add_placeholder ("R.DNORM");
		fd_d = gnm_func_lookup_or_add_placeholder ("NORMSDIST");
		expr_p = gnm_expr_new_funcall3
			(fd_p,
			 gnm_expr_copy (arg0),
			 gnm_expr_new_constant (value_new_int (0)),
			 gnm_expr_new_constant (value_new_int (1)));
		expr_d = gnm_expr_new_funcall1
			(fd_d,
			 arg0);
		res = gnm_expr_new_funcall3
			(fd_if,
			 arg1,
			 expr_p,
			 expr_d);

		simp = gnm_expr_simplify_if (res);
		if (simp) {
			gnm_expr_free (res);
			res = simp;
		}

		g_slist_free (args);
		return res;
	}
	default:
		break;
	}
	return NULL;
}

static void
odf_func_concatenate_handler_cb (gpointer data, gpointer user_data)
{
	GnmExpr const *expr = data;
	gboolean *check =  (gboolean *)(user_data);

	if (gnm_expr_is_rangeref (expr))
		(*check) = (*check) || (GNM_EXPR_GET_OPER (expr) != GNM_EXPR_OP_CELLREF);
}

static GnmExpr const *
odf_func_concatenate_handler (G_GNUC_UNUSED GnmConventions const *convs, G_GNUC_UNUSED Workbook *scope, GnmExprList *args)
{
	gboolean has_range = FALSE;
	GnmFunc  *fd;

	g_slist_foreach ((GSList *) args, odf_func_concatenate_handler_cb, (gpointer) &has_range);

	if (has_range)
		return NULL;

	fd = gnm_func_lookup_or_add_placeholder ("CONCATENATE");

	return gnm_expr_new_funcall (fd, args);
}

static GnmExpr const *
odf_func_t_dist_tail_handler (GnmExprList *args, int tails)
{
	switch (gnm_expr_list_length (args)) {
	case 2: {
		GnmExpr const *arg0 = args->data;
		GnmExpr const *arg1 = args->next->data;
		GnmFunc  *fd;
		GnmExpr const *res;

		fd = gnm_func_lookup_or_add_placeholder ("TDIST");
		res = gnm_expr_new_funcall3
			(fd,
			 arg0,
			 arg1,
			 gnm_expr_new_constant (value_new_int (tails)));

		g_slist_free (args);
		return res;
	}
	default:
		break;
	}
	return NULL;
}

static GnmExpr const *
odf_func_t_dist_handler (G_GNUC_UNUSED GnmConventions const *convs, G_GNUC_UNUSED Workbook *scope, GnmExprList *args)
{
	return odf_func_dist3_handler (args, "R.PT", "R.DT");
}

static GnmExpr const *
odf_func_t_dist_rt_handler (G_GNUC_UNUSED GnmConventions const *convs, G_GNUC_UNUSED Workbook *scope, GnmExprList *args)
{
	return odf_func_t_dist_tail_handler (args, 1);
}

static GnmExpr const *
odf_func_t_dist_2t_handler (G_GNUC_UNUSED GnmConventions const *convs, G_GNUC_UNUSED Workbook *scope, GnmExprList *args)
{
	return odf_func_t_dist_tail_handler (args, 2);
}

static GnmExpr const *
odf_func_lognorm_dist_handler (G_GNUC_UNUSED GnmConventions const *convs, G_GNUC_UNUSED Workbook *scope, GnmExprList *args)
{
	return odf_func_dist4_handler (args, "LOGNORMDIST", "R.DLNORM");
}

static GnmExpr const *
odf_func_negbinom_dist_handler (G_GNUC_UNUSED GnmConventions const *convs, G_GNUC_UNUSED Workbook *scope, GnmExprList *args)
{
	return odf_func_dist4_handler (args, "R.PNBINOM", "NEGBINOMDIST");
}

static GnmExpr const *
odf_func_true_handler (G_GNUC_UNUSED GnmConventions const *convs,
		       G_GNUC_UNUSED Workbook *scope, GnmExprList *args)
{
	return args ? NULL : gnm_expr_new_constant (value_new_bool (TRUE));
}

static GnmExpr const *
odf_func_false_handler (G_GNUC_UNUSED GnmConventions const *convs,
			G_GNUC_UNUSED Workbook *scope, GnmExprList *args)
{
	return args ? NULL : gnm_expr_new_constant (value_new_bool (FALSE));
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
		{"TRUE", odf_func_true_handler},
		{"FALSE", odf_func_false_handler},
		{"CONCATENATE", odf_func_concatenate_handler},
		{"COM.MICROSOFT.F.DIST", odf_func_f_dist_handler},
		{"COM.MICROSOFT.LOGNORM.DIST", odf_func_lognorm_dist_handler},
		{"COM.MICROSOFT.NEGBINOM.DIST", odf_func_negbinom_dist_handler},
		{"COM.MICROSOFT.T.DIST", odf_func_t_dist_handler},
		{"COM.MICROSOFT.T.DIST.RT", odf_func_t_dist_rt_handler},
		{"COM.MICROSOFT.T.DIST.2T", odf_func_t_dist_2t_handler},
		{"COM.MICROSOFT.NORM.S.DIST", odf_func_norm_s_dist_handler},
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

		{ "CONCATENATE","ODF.CONCATENATE" },
		{ "DDE","ODF.DDE" },
		{ "MULTIPLE.OPERATIONS","ODF.MULTIPLE.OPERATIONS" },

/* The following is a complete list of the functions defined in ODF OpenFormula draft 20090508 */
/* We should determine whether any mapping is needed. */

		{ "B","BINOM.DIST.RANGE" },
		{ "CEILING","ODF.CEILING" },          /* see handler */
		{ "CHISQINV","R.QCHISQ" },
		{ "CHISQDIST","ODF.CHISQDIST" },      /* see handler */
		{ "FALSE","FALSE" },                  /* see handler */
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
		{ "LEGACY.TDIST","TDIST" },
		{ "PDURATION","G_DURATION" },
		{ "PHI","NORMDIST" },              /* see handler */
		{ "SUMPRODUCT","ODF.SUMPRODUCT" },
		{ "TIME","ODF.TIME" },
		{ "TRUE","TRUE" },                 /* see handler */
		{ "USDOLLAR","DOLLAR" },

/* The following are known to have appeared (usually with a COM.MICROSOFT prefix. */

		{ "BETA.DIST","BETA.DIST" }, /* We need this mapping to satisfy */
		                             /* the COM.MICROSOFT prefix        */
		{ "BETA.INV","BETAINV" },
		{ "BINOM.DIST","BINOMDIST" },
		{ "BINOM.INV","CRITBINOM" },
		{ "CHISQ.DIST","R.PCHISQ" },
		{ "CHISQ.DIST.RT","CHIDIST" },
		{ "CHISQ.INV","R.QCHISQ" },
		{ "CHISQ.INV.RT","CHIINV" },
		{ "CHISQ.TEST","CHITEST" },
		{ "CONCAT", "CONCAT" },
		{ "CONFIDENCE.NORM","CONFIDENCE" },
		{ "CONFIDENCE.T","CONFIDENCE.T" },
		{ "COVARIANCE.P","COVAR" },
		{ "COVARIANCE.S","COVARIANCE.S" },
		{ "ENCODEURL", "ENCODEURL" },
		{ "EXPON.DIST","EXPONDIST" },
		{ "F.DIST.RT","FDIST" },
		{ "F.INV.RT","FINV" },
		{ "F.TEST","FTEST" },
		{ "GAMMA.DIST","GAMMADIST" },
		{ "GAMMA.INV","GAMMAINV" },
		{ "GAMMALN.PRECISE","GAMMALN" },
		{ "HYPGEOM.DIST","HYPGEOMDIST" },
		{ "IFS","IFS" },
		{ "LOGNORM.INV","LOGINV" },
		{ "MINIFS", "MINIFS" },
		{ "MAXIFS", "MAXIFS" },
		{ "MODE.SNGL","MODE" },
		{ "MODE.MULT","MODE.MULT" },
		{ "NORM.DIST","NORMDIST" },
		{ "NORM.INV","NORMINV" },
		{ "NORM.S.INV","NORMSINV" },
		{ "PERCENTILE.INC","PERCENTILE" },
		{ "PERCENTILE.EXC","PERCENTILE.EXC" },
		{ "PERCENTRANK.INC","PERCENTRANK" },
		{ "PERCENTRANK.EXC","PERCENTRANK.EXC" },
		{ "POISSON.DIST","POISSON" },
		{ "QUARTILE.INC","QUARTILE" },
		{ "QUARTILE.EXC","QUARTILE.EXC" },
		{ "RANK.EQ","RANK" },
		{ "RANK.AVG","RANK.AVG" },
		{ "STDEV.S","STDEV" },
		{ "STDEV.P","STDEVP" },
		{ "SWITCH", "SWITCH" },
		{ "T.INV","R.QT" },
		{ "T.INV.2T","TINV" },
		{ "T.TEST","TTEST" },
		{ "TEXTJOIN", "TEXTJOIN" },
		{ "VAR.S","VAR" },
		{ "VAR.P","VARP" },
		{ "WEIBULL.DIST","WEIBULL" },
		{ "Z.TEST","ZTEST" },

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
/* { "TEXT","TEXT" }, */
/* { "TIME","TIME" }, */
/* { "TIMEVALUE","TIMEVALUE" }, */
/* { "TINV","TINV" }, */
/* { "TODAY","TODAY" }, */
/* { "TRANSPOSE","TRANSPOSE" }, */
/* { "TREND","TREND" }, */
/* { "TRIM","TRIM" }, */
/* { "TRIMMEAN","TRIMMEAN" }, */
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
	static char const MicrosoftPrefix[] = "COM.MICROSOFT.";

	GnmFunc  *f = NULL;
	int i;
	GnmExpr const * (*handler) (GnmConventions const *convs, Workbook *scope, GnmExprList *args);
	ODFConventions *oconv = (ODFConventions *)convs;
	GHashTable *namemap;
	GHashTable *handlermap;

	if (NULL == oconv->state->openformula_namemap) {
		namemap = g_hash_table_new (go_ascii_strcase_hash,
					    go_ascii_strcase_equal);
		for (i = 0; sc_func_renames[i].oo_name; i++)
			g_hash_table_insert (namemap,
				(gchar *) sc_func_renames[i].oo_name,
				(gchar *) sc_func_renames[i].gnm_name);
		oconv->state->openformula_namemap = namemap;
	} else
		namemap = oconv->state->openformula_namemap;

	if (NULL == oconv->state->openformula_handlermap) {
		guint i;
		handlermap = g_hash_table_new (go_ascii_strcase_hash,
					       go_ascii_strcase_equal);
		for (i = 0; sc_func_handlers[i].gnm_name; i++)
			g_hash_table_insert (handlermap,
					     (gchar *) sc_func_handlers[i].gnm_name,
					     sc_func_handlers[i].handler);
		oconv->state->openformula_handlermap = handlermap;
	} else
		handlermap = oconv->state->openformula_handlermap;

	handler = g_hash_table_lookup (handlermap, name);
	if (handler != NULL) {
		GnmExpr const * res = handler (convs, scope, args);
		if (res != NULL)
			return res;
	}

	if (0 == g_ascii_strncasecmp (name, GnumericPrefix, sizeof (GnumericPrefix)-1)) {
		f = gnm_func_lookup_or_add_placeholder (name+sizeof (GnumericPrefix)-1);
	} else if (0 == g_ascii_strncasecmp (name, OOoAnalysisPrefix, sizeof (OOoAnalysisPrefix)-1)) {
		f = gnm_func_lookup_or_add_placeholder (name+sizeof (OOoAnalysisPrefix)-1);
	} else if (0 == g_ascii_strncasecmp (name, MicrosoftPrefix, sizeof (MicrosoftPrefix)-1)) {
		char const *new_name;
		if (NULL != namemap &&
		    NULL != (new_name = g_hash_table_lookup (namemap, name+sizeof (MicrosoftPrefix)-1)))
			f = gnm_func_lookup_or_add_placeholder (new_name);
	}

	if (NULL == f) {
		char const *new_name;
		if (NULL != namemap &&
		    NULL != (new_name = g_hash_table_lookup (namemap, name)))
			name = new_name;
		f = gnm_func_lookup_or_add_placeholder (name);
	}

	return gnm_expr_new_funcall (f, args);
}

static gboolean
identified_google_docs (GsfInfile *zip)
{
	/* As of 2011/10/1 google-docs does not store any generator info so */
	/* we cannot use that for identification */
	gboolean google_docs = FALSE;
	GsfInput *content = gsf_infile_child_by_name
		(zip, "content.xml");
	if (content) {
		/* pick arbitrary size limit of 0.5k for the manifest to avoid
		 * potential of any funny business */
		size_t size = MIN (gsf_input_size (content), 512);
		char const *mf = gsf_input_read (content, size, NULL);
		if (mf)
			google_docs =
				(NULL != g_strstr_len
				 (mf, -1,
				  "urn:oasis:names:tc:opendocument:xmlns:office:1.0"));
		g_object_unref (content);
	}
	return google_docs;
}

static OOVer
determine_oo_version (GsfInfile *zip, OOVer def)
{
	char const *header;
	size_t size;
	GsfInput *mimetype = gsf_infile_child_by_name (zip, "mimetype");
	if (!mimetype) {
		/* Google-docs is the mimetype so we need to check that */
		if (identified_google_docs (zip))
			return OOO_VER_OPENDOC;
		/* Really old versions also had no mimetype.  Allow that,
		   except in the probe.  */
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
openoffice_file_open (G_GNUC_UNUSED GOFileOpener const *fo, GOIOContext *io_context,
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
	gboolean         content_malformed = FALSE;
	GSList          *l;
	int              max_rows = GNM_MIN_ROWS;
	int              max_cols = GNM_MIN_COLS;

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
		/* uncommon but legal */
	}

	locale = gnm_push_C_locale ();

	/* init */
	state.debug = gnm_debug_flag ("opendocumentimport");
	state.hd_ft_left_warned = FALSE;
	state.context	= io_context;
	state.wb_view	= wb_view;
	state.pos.wb	= wb_view_get_workbook (wb_view);
	state.zip = zip;
	state.pos.sheet = NULL;
	state.pos.eval.col	= -1;
	state.pos.eval.row	= -1;
	state.cell_comment      = NULL;
	state.sharer = gnm_expr_sharer_new ();
	state.chart.name = NULL;
	state.chart.style_name = NULL;

	state.chart.cs_enhanced_path = NULL;
	state.chart.cs_modifiers = NULL;
	state.chart.cs_viewbox = NULL;
	state.chart.cs_type = NULL;
	state.chart.cs_variables = NULL;
	for (i = 0; i < OO_CHART_STYLE_INHERITANCE; i++)
		state.chart.i_plot_styles[i] = NULL;
	state.styles.sheet = g_hash_table_new_full (g_str_hash, g_str_equal,
		(GDestroyNotify) g_free,
		(GDestroyNotify) oo_sheet_style_free);
	state.styles.text = g_hash_table_new_full (g_str_hash, g_str_equal,
		(GDestroyNotify) g_free,
		(GDestroyNotify) pango_attr_list_unref);
	state.styles.col = g_hash_table_new_full (g_str_hash, g_str_equal,
		(GDestroyNotify) g_free,
		(GDestroyNotify) g_free);
	state.styles.row = g_hash_table_new_full (g_str_hash, g_str_equal,
		(GDestroyNotify) g_free,
		(GDestroyNotify) g_free);
	state.styles.cell = g_hash_table_new_full (g_str_hash, g_str_equal,
		(GDestroyNotify) g_free,
		(GDestroyNotify) odf_oo_cell_style_unref);
	state.styles.cell_datetime = g_hash_table_new_full (g_str_hash, g_str_equal,
		(GDestroyNotify) g_free,
		(GDestroyNotify) odf_oo_cell_style_unref);
	state.styles.cell_date = g_hash_table_new_full (g_str_hash, g_str_equal,
		(GDestroyNotify) g_free,
		(GDestroyNotify) odf_oo_cell_style_unref);
	state.styles.cell_time = g_hash_table_new_full (g_str_hash, g_str_equal,
		(GDestroyNotify) g_free,
		(GDestroyNotify) odf_oo_cell_style_unref);
	state.styles.master_pages = g_hash_table_new_full (g_str_hash, g_str_equal,
		(GDestroyNotify) g_free,
		(GDestroyNotify) gnm_print_info_free);
	state.styles.page_layouts = g_hash_table_new_full (g_str_hash, g_str_equal,
		(GDestroyNotify) g_free,
		(GDestroyNotify) gnm_print_info_free);
	state.formats = g_hash_table_new_full (g_str_hash, g_str_equal,
		(GDestroyNotify) g_free,
		(GDestroyNotify) go_format_unref);
	state.validations = g_hash_table_new_full (g_str_hash, g_str_equal,
		(GDestroyNotify) g_free,
		(GDestroyNotify) odf_validation_free);
	state.chart.so = NULL;
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
	state.strings = g_hash_table_new_full
		(g_str_hash, g_str_equal,
		 (GDestroyNotify) g_free,
		 (GDestroyNotify) g_free);
	state.cur_style.cells    = NULL;
	state.cur_style.col_rows = NULL;
	state.cur_style.sheets   = NULL;
	state.default_style.cells = NULL;
	state.default_style.rows = NULL;
	state.default_style.columns = NULL;
	state.default_style.graphics = NULL;
	state.cur_style.type   = OO_STYLE_UNKNOWN;
	state.cur_style.requires_disposal = FALSE;
	state.sheet_order = NULL;
	for (i = 0; i<NUM_FORMULAE_SUPPORTED; i++)
		state.convs[i] = NULL;
	state.openformula_namemap = NULL;
	state.openformula_handlermap = NULL;
	state.cur_format.accum = NULL;
	state.cur_format.percentage = FALSE;
	state.filter = NULL;
	state.print.page_breaks.h = state.print.page_breaks.v = NULL;
	state.last_progress_update = 0;
	state.last_error = NULL;
	state.cur_control = NULL;
	state.chart_list = NULL;

	state.text_p_stack = NULL;
	state.text_p_for_cell.permanent = TRUE;
	state.text_p_for_cell.span_style_stack = NULL;
	state.text_p_for_cell.span_style_list = NULL;
	state.text_p_for_cell.gstr = NULL;
	state.text_p_for_cell.attrs = NULL;

	state.table_n = -1;

	go_io_progress_message (state.context, _("Reading file..."));
	go_io_value_progress_set (state.context, gsf_input_size (contents), 0);

	if (state.ver == OOO_VER_OPENDOC) {
		GsfInput *meta_file = gsf_infile_child_by_name (zip, "meta.xml");
		if (NULL != meta_file) {
			meta_data = gsf_doc_meta_data_new ();
			err = gsf_doc_meta_data_read_from_odf (meta_data, meta_file);
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

	state.preparse = TRUE;
	doc  = gsf_xml_in_doc_new ((state.ver == OOO_VER_1)
				   ? ooo1_content_preparse_dtd
				   : opendoc_content_preparse_dtd,
				   gsf_odf_get_ns ());
	content_malformed = !gsf_xml_in_doc_parse (doc, contents, &state);
	gsf_xml_in_doc_free (doc);
	odf_clear_conventions (&state); /* contain references to xin */
	state.sheet_order = g_slist_reverse (state.sheet_order);
	state.preparse = FALSE;

	/* We want to make all sheets the same size (see bug 505 on gitlab) */
	l = state.sheet_order;
	while (l != NULL) {
		sheet_order_t *sot;
		sot = (sheet_order_t *)(l->data);
		if (sot->cols > max_cols)
			max_cols = sot->cols;
		if (sot->rows > max_rows)
			max_rows = sot->rows;
		l = l->next;
	}

	if (!gnm_sheet_valid_size (max_cols, max_rows))
		gnm_sheet_suggest_size (&max_cols, &max_rows);

	l = state.sheet_order;
	while (l != NULL) {
		sheet_order_t *sot;
		gboolean perr = FALSE;
		sot = (sheet_order_t *)(l->data);
		if ((sot->cols < max_cols) || (sot->rows < max_rows)) {
			GOUndo *undo = gnm_sheet_resize (sot->sheet, max_cols, max_rows,
							  NULL, &perr);
			if (undo) g_object_unref (undo);
		}
		l = l->next;
	}

	if (NULL != styles) {
		GsfXMLInDoc *doc = gsf_xml_in_doc_new (styles_dtd,
						       gsf_odf_get_ns ());
		gsf_xml_in_doc_parse (doc, styles, &state);
		gsf_xml_in_doc_free (doc);
		odf_clear_conventions (&state); /* contain references to xin */
		g_object_unref (styles);
	}

	if (!content_malformed) {
		gsf_input_seek (contents, 0, G_SEEK_SET);
		doc  = gsf_xml_in_doc_new ((state.ver == OOO_VER_1)
					   ? ooo1_content_dtd
					   : opendoc_content_dtd,
					   gsf_odf_get_ns ());
		content_malformed = !gsf_xml_in_doc_parse (doc, contents, &state);
		gsf_xml_in_doc_free (doc);
		odf_clear_conventions (&state);

		odf_fix_expr_names (&state);
	}


	if (!content_malformed) {
		GsfInput *settings;
		char const *filesaver;

		/* look for the view settings */
		state.settings.settings
			= g_hash_table_new_full (g_str_hash, g_str_equal,
						 (GDestroyNotify) g_free,
						 (GDestroyNotify) destroy_gvalue);
		state.settings.stack = NULL;
		settings = gsf_infile_child_by_name (zip, "settings.xml");
		if (settings != NULL) {
			GsfXMLInDoc *sdoc = gsf_xml_in_doc_new
				(opendoc_settings_dtd, gsf_odf_get_ns ());
			gsf_xml_in_doc_parse (sdoc, settings, &state);
			gsf_xml_in_doc_free (sdoc);
			odf_clear_conventions (&state); /* contain references to xin */
			g_object_unref (settings);
		}
		if (state.settings.stack != NULL) {
			go_cmd_context_error_import (GO_CMD_CONTEXT (io_context),
						     _("settings.xml stream is malformed!"));
			g_slist_free_full (state.settings.stack,
					   (GDestroyNotify) g_hash_table_unref);
			state.settings.stack = NULL;
		}

		/* Use the settings here! */
		if (state.debug)
			g_hash_table_foreach (state.settings.settings,
					      (GHFunc)dump_settings_hash, (char *)"");
		if (!odf_has_gnm_foreign (&state)) {
			filesaver = odf_created_by_gnumeric (&state) ?
				"Gnumeric_OpenCalc:openoffice"
				: "Gnumeric_OpenCalc:odf";
		} else {
			filesaver = "Gnumeric_OpenCalc:odf";
		}
		odf_apply_ooo_config (&state);
		odf_apply_gnm_config (&state);

		workbook_set_saveinfo (state.pos.wb, GO_FILE_FL_AUTO,
				       go_file_saver_for_id
				       (filesaver));

		g_hash_table_destroy (state.settings.settings);
		state.settings.settings = NULL;
	}


	go_io_progress_unset (state.context);
	g_free (state.last_error);

	/* This should be empty! */
	while (state.text_p_stack)
		odf_pop_text_p (&state);

	if (state.default_style.cells)
		odf_oo_cell_style_unref (state.default_style.cells);
	g_free (state.default_style.rows);
	g_free (state.default_style.columns);
	oo_chart_style_free (state.default_style.graphics);
	odf_free_cur_style (&state);
	g_hash_table_destroy (state.styles.sheet);
	g_hash_table_destroy (state.styles.text);
	g_hash_table_destroy (state.styles.col);
	g_hash_table_destroy (state.styles.row);
	g_hash_table_destroy (state.styles.cell);
	g_hash_table_destroy (state.styles.cell_datetime);
	g_hash_table_destroy (state.styles.cell_date);
	g_hash_table_destroy (state.styles.cell_time);
	g_hash_table_destroy (state.styles.master_pages);
	g_hash_table_destroy (state.styles.page_layouts);
	g_slist_free_full (state.chart.saved_graph_styles,
			   (GDestroyNotify)g_hash_table_destroy);
	g_hash_table_destroy (state.chart.graph_styles);
	g_hash_table_destroy (state.chart.hatches);
	g_hash_table_destroy (state.chart.dash_styles);
	g_hash_table_destroy (state.chart.fill_image_styles);
	g_hash_table_destroy (state.chart.gradient_styles);
	g_hash_table_destroy (state.formats);
	g_hash_table_destroy (state.controls);
	g_hash_table_destroy (state.validations);
	g_hash_table_destroy (state.strings);
	g_hash_table_destroy (state.chart.arrow_markers);
	g_slist_free_full (state.sheet_order, (GDestroyNotify)g_free);
	if (state.openformula_namemap)
		g_hash_table_destroy (state.openformula_namemap);
	if (state.openformula_handlermap)
		g_hash_table_destroy (state.openformula_handlermap);
	g_object_unref (contents);
	gnm_expr_sharer_unref (state.sharer);
	g_free (state.chart.cs_enhanced_path);
	g_free (state.chart.cs_modifiers);
	g_free (state.chart.cs_viewbox);
	g_free (state.chart.cs_type);
	if (state.chart.so)
		g_object_unref (state.chart.so);
	if (state.chart_list)
		g_slist_free_full (state.chart_list, odf_destroy_object_offset);
	if (state.chart.cs_variables)
		g_hash_table_destroy (state.chart.cs_variables);
	g_slist_free (state.text_p_for_cell.span_style_stack);
	g_slist_free_full (state.text_p_for_cell.span_style_list, g_free);
	if (state.text_p_for_cell.gstr)
		g_string_free (state.text_p_for_cell.gstr, TRUE);
	if (state.text_p_for_cell.attrs)
		pango_attr_list_unref (state.text_p_for_cell.attrs);

	g_object_unref (zip);

	if (content_malformed)
		go_io_error_string (io_context, _("XML document not well formed!"));

	i = workbook_sheet_count (state.pos.wb);
	while (i-- > 0)
		sheet_flag_recompute_spans (workbook_sheet_by_index (state.pos.wb, i));
	workbook_queue_all_recalc (state.pos.wb);

	gnm_pop_C_locale (locale);
}

gboolean
openoffice_file_probe (GOFileOpener const *fo, GsfInput *input, GOFileProbeLevel pl);

gboolean
openoffice_file_probe (G_GNUC_UNUSED GOFileOpener const *fo, GsfInput *input, G_GNUC_UNUSED GOFileProbeLevel pl)
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

static char *
make_node_id (GsfXMLInNode const *node)
{
	return g_strconcat (node->id, " -- ", node->parent_id, NULL);
}

static GsfXMLInNode const *
create_preparse_dtd (const GsfXMLInNode *orig, const GsfXMLInNode *overrides)
{
	int i, N;
	GHashTable *loc_hash =
		g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
	GsfXMLInNode *res;

	for (N = 0; orig[N].id != NULL; N++) {
		g_hash_table_replace (loc_hash, make_node_id (orig + N),
				      GINT_TO_POINTER (N));

	}

	res = go_memdup_n (orig, N + 1, sizeof (GsfXMLInNode));
	for (i = 0; i < N; i++) {
		res[i].start = NULL;
		res[i].end = NULL;
		res[i].has_content = GSF_XML_NO_CONTENT;
	}

	for (i = 0; overrides[i].id != NULL; i++) {
		char *id = make_node_id (overrides + i);
		int loc = GPOINTER_TO_INT (g_hash_table_lookup (loc_hash, id));
		if (loc)
			res[loc] = overrides[i];
		g_free (id);
	}

	g_hash_table_destroy (loc_hash);

	return res;
}

G_MODULE_EXPORT void
go_plugin_init (G_GNUC_UNUSED GOPlugin *plugin, G_GNUC_UNUSED GOCmdContext *cc)
{
	magic_transparent = style_color_auto_back ();

	opendoc_content_preparse_dtd =
		create_preparse_dtd (opendoc_content_dtd, opendoc_content_preparse_overrides);

	ooo1_content_preparse_dtd =
		create_preparse_dtd (ooo1_content_dtd, ooo1_content_preparse_overrides);
}

G_MODULE_EXPORT void
go_plugin_shutdown (G_GNUC_UNUSED GOPlugin *plugin, G_GNUC_UNUSED GOCmdContext *cc)
{
	style_color_unref (magic_transparent);
	magic_transparent = NULL;
	g_free ((gpointer)opendoc_content_preparse_dtd);
	opendoc_content_preparse_dtd = NULL;
	g_free ((gpointer)ooo1_content_preparse_dtd);
	ooo1_content_preparse_dtd = NULL;
}
