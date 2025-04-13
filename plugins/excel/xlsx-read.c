/*
 * xlsx-read.c : Read MS Excel 2007 Office Open xml
 *
 * Copyright (C) 2006-2007 Jody Goldberg (jody@gnome.org)
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
#include "xlsx-utils.h"
#include "ms-excel-write.h"

#include <sheet-view.h>
#include <sheet-style.h>
#include <sheet-merge.h>
#include <sheet.h>
#include <ranges.h>
#include <style.h>
#include <style-border.h>
#include <style-color.h>
#include <style-conditions.h>
#include <gnm-format.h>
#include <cell.h>
#include <position.h>
#include <expr.h>
#include <expr-name.h>
#include <print-info.h>
#include <validation.h>
#include <input-msg.h>
#include <value.h>
#include <sheet-filter.h>
#include <hlink.h>
#include <selection.h>
#include <command-context.h>
#include <workbook-view.h>
#include <workbook.h>
#include <gutils.h>
#include <graph.h>
#include <sheet-object-graph.h>
#include <sheet-object-cell-comment.h>
#include <gnm-sheet-slicer.h>
#include <gnm-so-filled.h>
#include <gnm-so-line.h>
#include <sheet-object-image.h>
#include <number-match.h>
#include <func.h>
#include "dead-kittens.h"

#include <goffice/goffice.h>


#include <goffice-data.h>		/* MOVE TO GOFFCE with slicer code */
#include <go-data-slicer-field.h>	/* MOVE TO GOFFCE with slicer code */

#include <gsf/gsf-libxml.h>
#include <gsf/gsf-input.h>
#include <gsf/gsf-infile.h>
#include <gsf/gsf-infile-zip.h>
#include <gsf/gsf-open-pkg-utils.h>
#include <gsf/gsf-meta-names.h>
#include <gsf/gsf-doc-meta-data.h>
#include <gsf/gsf-docprop-vector.h>
#include <gsf/gsf-timestamp.h>

#include <glib/gi18n-lib.h>
#include <gmodule.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

/*****************************************************************************/

#define CXML2C(s) ((char const *)(s))

enum {
	ECMA_376_2006 = 1,
	ECMA_376_2008 = 2
};

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
	XLSX_AXIS_VAL,
	XLSX_AXIS_DATE,
	XLSX_AXIS_SER
} XLSXAxisType;
typedef struct {
	char	*id;
	GogAxis	*axis;
	GSList	*plots;
	XLSXAxisType type;
	GogObjectPosition compass;
	GogAxisPosition	  cross;
	char	*cross_id;
	double cross_value;
	gboolean invert_axis;
	double logbase;

	double axis_elements[GOG_AXIS_ELEM_MAX_ENTRY];
	guint8 axis_element_set[GOG_AXIS_ELEM_MAX_ENTRY];

	gboolean deleted;
} XLSXAxisInfo;

typedef struct {
	GsfInfile	*zip;

	int              version;

	GOIOContext	*context;	/* The IOcontext managing things */
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

	GHashTable	*num_fmts;
	GOFormat	*date_fmt;
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

	GHashTable	*theme_colors_by_name;

	GPtrArray	*collection;	/* utility for the shared collection handlers */
	unsigned	 count;
	XLSXPanePos	 pane_pos;

	GnmStyleConditions *conditions;
	GSList		   *cond_regions;
	GnmStyleCond	   *cond;

	GnmFilter	   *filter;
	int		    filter_cur_field;
	GSList		   *filter_items; /* an accumulator */

	GSList		   *validation_regions;
	GnmValidation	   *validation;
	GnmInputMsg	   *input_msg;

	GnmPageBreaks	   *page_breaks;

	/* Rows/Cols state */
	GnmStyle          *pending_rowcol_style;
	GnmRange           pending_rowcol_range;

	/* Drawing state */
	SheetObject	   *so;
	gint64		    drawing_pos[8];
	int		    drawing_pos_flags;
	GODrawingAnchorDir  so_direction;
	GnmSOAnchorMode so_anchor_mode;
	GnmExprTop const *link_texpr;
	char *object_name;

	/* Legacy drawing state */
	double		  grp_offset[4];
	GSList		 *grp_stack;

	/* Charting state */
	GogGraph	 *graph;
	GogChart	 *chart;
	GogPlot		 *plot;
	GogSeries	 *series;
	int		  dim_type;
	GogObject	 *series_pt;
	gboolean	  series_pt_has_index;
	GOStyle	 *cur_style;
	int               gradient_count;
	guint32           chart_color_state;
	GOColor		  color;
	GOMarker	 *marker;
	GogObject	 *cur_obj;
	GSList		 *obj_stack;
	GSList		 *style_stack;
	unsigned int	  sp_type;
	char		 *chart_tx;
	gboolean          inhibit_text_pop;
	double		  chart_pos[4];  /* x, w, y, h */
	gboolean	  chart_pos_mode[4]; /* false: "factor", true: "edge" */
	gboolean	  chart_pos_target; /* true if "inner" */
	int               radio_value;
	int               zindex;

	struct {
		GogAxis *obj;
		int	 type;
		GHashTable *by_id;
		GHashTable *by_obj;
		XLSXAxisInfo *info;
	} axis;

	char *defined_name;
	Sheet *defined_name_sheet;
	GList *delayed_names;

	GSList *pending_objects;
	GHashTable *zorder;

	/* external refs */
       	Workbook *external_ref;
	Sheet 	 *external_ref_sheet;

	/* Pivot state */
	struct {
		GnmSheetSlicer	  *slicer;
		GODataSlicerField *slicer_field;

		GHashTable	  *cache_by_id;
		GODataCache	  *cache;
		GODataCacheSource *cache_src;
		GODataCacheField  *cache_field;
		GPtrArray	  *cache_field_values;

		unsigned int	   field_count, record_count;
		char		  *cache_record_part_id;
	} pivot;

	/* Comment state */
	GPtrArray	*authors;
	GObject		*comment;

	/* Document Properties */
	GsfDocMetaData   *metadata;
	char *meta_prop_name;

	/* Rich Text handling */
	GString		*r_text;
	PangoAttrList	*rich_attrs;
	PangoAttrList	*run_attrs;
} XLSXReadState;
typedef struct {
	GOString	*str;
	GOFormat	*markup;
} XLSXStr;

static GsfXMLInNS const xlsx_ns[] = {
	GSF_XML_IN_NS (XL_NS_SS,	"http://schemas.openxmlformats.org/spreadsheetml/2006/main"),		  /* Office 12 */
	GSF_XML_IN_NS (XL_NS_SS,	"http://schemas.openxmlformats.org/spreadsheetml/2006/7/main"),		  /* Office 12 BETA-2 Technical Refresh */
	GSF_XML_IN_NS (XL_NS_SS,	"http://schemas.openxmlformats.org/spreadsheetml/2006/5/main"),		  /* Office 12 BETA-2 */
	GSF_XML_IN_NS (XL_NS_SS,	"http://schemas.microsoft.com/office/excel/2006/2"),			  /* Office 12 BETA-1 Technical Refresh */
	GSF_XML_IN_NS (XL_NS_MSSS,	"http://schemas.microsoft.com/office/excel/2006/main"),	                  /* Office 14??? */
	GSF_XML_IN_NS (XL_NS_SS,        "http://schemas.microsoft.com/office/spreadsheetml/2009/9/main"),         /* Office 14??? */
	GSF_XML_IN_NS (XL_NS_SS_DRAW,	"http://schemas.openxmlformats.org/drawingml/2006/spreadsheetDrawing"),	  /* Office 12 BETA-2 */
	GSF_XML_IN_NS (XL_NS_SS_DRAW,	"http://schemas.openxmlformats.org/drawingml/2006/3/spreadsheetDrawing"), /* Office 12 BETA-2 Technical Refresh */
	GSF_XML_IN_NS (XL_NS_CHART,	"http://schemas.openxmlformats.org/drawingml/2006/3/chart"),		  /* Office 12 BETA-2 */
	GSF_XML_IN_NS (XL_NS_CHART,	"http://schemas.openxmlformats.org/drawingml/2006/chart"),		  /* Office 12 BETA-2 Technical Refresh */
	GSF_XML_IN_NS (XL_NS_CHART_DRAW,    "http://schemas.openxmlformats.org/drawingml/2006/chartDrawing"),
	GSF_XML_IN_NS (XL_NS_DRAW,	"http://schemas.openxmlformats.org/drawingml/2006/3/main"),		  /* Office 12 BETA-2 */
	GSF_XML_IN_NS (XL_NS_DRAW,	"http://schemas.openxmlformats.org/drawingml/2006/main"),		  /* Office 12 BETA-2 Technical Refresh */
	GSF_XML_IN_NS (XL_NS_GNM_EXT,   "http://www.gnumeric.org/ext/spreadsheetml"),
	GSF_XML_IN_NS (XL_NS_DOC_REL,	"http://schemas.openxmlformats.org/officeDocument/2006/relationships"),
	GSF_XML_IN_NS (XL_NS_PKG_REL,	"http://schemas.openxmlformats.org/package/2006/relationships"),
	GSF_XML_IN_NS (XL_NS_LEG_OFF,   "urn:schemas-microsoft-com:office:office"),
	GSF_XML_IN_NS (XL_NS_LEG_XL,    "urn:schemas-microsoft-com:office:excel"),
	GSF_XML_IN_NS (XL_NS_LEG_VML,   "urn:schemas-microsoft-com:vml"),
	GSF_XML_IN_NS (XL_NS_PROP_CP,   "http://schemas.openxmlformats.org/package/2006/metadata/core-properties"),
	GSF_XML_IN_NS (XL_NS_PROP_DC,   "http://purl.org/dc/elements/1.1/"),
	GSF_XML_IN_NS (XL_NS_PROP_DCMITYPE, "http://purl.org/dc/dcmitype"),
	GSF_XML_IN_NS (XL_NS_PROP_DCTERMS,  "http://purl.org/dc/terms/"),
	GSF_XML_IN_NS (XL_NS_PROP_XSI,  "http://www.w3.org/2001/XMLSchema-instance"),
	GSF_XML_IN_NS (XL_NS_PROP,      "http://schemas.openxmlformats.org/officeDocument/2006/extended-properties"),
	GSF_XML_IN_NS (XL_NS_PROP_VT,   "http://schemas.openxmlformats.org/officeDocument/2006/docPropsVTypes"),
	GSF_XML_IN_NS (XL_NS_PROP_CUSTOM,   "http://schemas.openxmlformats.org/officeDocument/2006/custom-properties"),
	{ NULL, 0 }
};

typedef struct {
	int code;
	gdouble width;
	gdouble height;
	GtkUnit unit;
	gchar const *name;
} XLSXPaperDefs;


static Sheet *
wrap_sheet_new (Workbook *wb, char const *name, int columns, int rows)
{
	Sheet *sheet = sheet_new_with_type (wb, name, GNM_SHEET_DATA, columns, rows);
	GnmPrintInformation *pi = sheet->print_info;

	// Force a load of defaults here.
	gnm_print_info_load_defaults (pi);

	// We have different defaults for header and footer (namely blank)
	xls_header_footer_import (&pi->header, NULL);
	xls_header_footer_import (&pi->footer, NULL);

	return sheet;
}


static void
maybe_update_progress (GsfXMLIn *xin)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	GsfInput *input = gsf_xml_in_get_input (xin);
	gsf_off_t pos = gsf_input_tell (input);

	go_io_value_progress_update (state->context, pos);
}

static void
start_update_progress (XLSXReadState *state, GsfInput *xin,
		       char const *message, double min, double max)
{
	go_io_progress_range_push (state->context, min, max);
	if (xin) {
		go_io_value_progress_set (state->context,
					  gsf_input_size (xin), 10000);
		go_io_progress_message (state->context, message);
	}
}

static void
end_update_progress (XLSXReadState *state)
{
	go_io_progress_range_pop (state->context);
}

static gboolean
xlsx_parse_stream (XLSXReadState *state, GsfInput *in, GsfXMLInNode const *dtd)
{
	gboolean  success = FALSE;

	if (NULL != in) {
		GsfXMLInDoc *doc = gsf_xml_in_doc_new (dtd, xlsx_ns);

		success = gsf_xml_in_doc_parse (doc, in, state);

		if (!success)
			go_io_warning (state->context,
				_("'%s' is corrupt!"),
				gsf_input_name (in));

		gsf_xml_in_doc_free (doc);
		g_object_unref (in);
	}
	return success;
}

static void
xlsx_parse_rel_by_id (GsfXMLIn *xin, char const *part_id,
		      GsfXMLInNode const *dtd,
		      GsfXMLInNS const *ns)
{
	GError *err;
	gboolean debug = gnm_debug_flag ("xlsx-parsing");

	if (debug)
		g_printerr ("{ /* Parsing  : %s :: %s */\n",
			    gsf_input_name (gsf_xml_in_get_input (xin)), part_id);

	err = gsf_open_pkg_parse_rel_by_id (xin, part_id, dtd, ns);
	if (NULL != err) {
		XLSXReadState *state = (XLSXReadState *)xin->user_state;
		go_io_warning (state->context, "%s", err->message);
		g_error_free (err);
	}

	if (debug)
		g_printerr ("} /* DONE : %s :: %s */\n",
			    gsf_input_name (gsf_xml_in_get_input (xin)), part_id);
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

	go_io_warning (state->context, "%s", msg);
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
attr_bool (G_GNUC_UNUSED GsfXMLIn *xin, xmlChar const **attrs,
	   char const *target,
	   int *res)
{
	g_return_val_if_fail (attrs != NULL, FALSE);
	g_return_val_if_fail (attrs[0] != NULL, FALSE);
	g_return_val_if_fail (attrs[1] != NULL, FALSE);

	if (strcmp (attrs[0], target))
		return FALSE;

	*res = (0 == strcmp (attrs[1], "1") || 0 == strcmp (attrs[1], "true")) ;

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
	if (errno == ERANGE || tmp > G_MAXINT || tmp < G_MININT)
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
attr_uint (GsfXMLIn *xin, xmlChar const **attrs,
	   char const *target, unsigned *res)
{
	char *end;
	unsigned long tmp;

	g_return_val_if_fail (attrs != NULL, FALSE);
	g_return_val_if_fail (attrs[0] != NULL, FALSE);
	g_return_val_if_fail (attrs[1] != NULL, FALSE);

	if (strcmp (attrs[0], target))
		return FALSE;

	errno = 0;
	tmp = strtoul (attrs[1], &end, 10);
	if (errno == ERANGE || tmp != (unsigned)tmp)
		return xlsx_warning (xin,
			_("Unsigned integer '%s' is out of range, for attribute %s"),
			attrs[1], target);
	if (*end)
		return xlsx_warning (xin,
			_("Invalid unsigned integer '%s' for attribute %s"),
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
		*res = GO_COLOR_FROM_RGB (r, g, b);
	}

	return TRUE;
}

static gboolean
attr_float (GsfXMLIn *xin, xmlChar const **attrs,
	    char const *target,
	    gnm_float *res)
{
	char *end;
	gnm_float tmp;

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
attr_double (GsfXMLIn *xin, xmlChar const **attrs,
	     char const *target,
	     double *res)
{
	char *end;
	double tmp;

	g_return_val_if_fail (attrs != NULL, FALSE);
	g_return_val_if_fail (attrs[0] != NULL, FALSE);
	g_return_val_if_fail (attrs[1] != NULL, FALSE);

	if (strcmp (attrs[0], target))
		return FALSE;

	tmp = go_strtod (attrs[1], &end);
	if (*end)
		return xlsx_warning (xin,
			_("Invalid number '%s' for attribute %s"),
			attrs[1], target);
	*res = tmp;
	return TRUE;
}

/*
 * Either an integer scaled so 100000 means 100%, or something like "50%"
 * which we'll return as 50*1000.
 *
 * The first seems off-spec, but is what Excel produces.
 */
static gboolean
attr_percent (GsfXMLIn *xin, xmlChar const **attrs,
	      char const *target, int *res)
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
	if (errno == ERANGE || tmp > G_MAXINT / 1000 || tmp < G_MININT / 1000)
		return xlsx_warning (xin,
			_("Integer '%s' is out of range, for attribute %s"),
			attrs[1], target);
	if (*end == 0)
		*res = tmp;
	else if (strcmp (end, "%") == 0)
		*res = tmp * 1000;
	else
		return xlsx_warning (xin,
			_("Invalid integer '%s' for attribute %s"),
			attrs[1], target);

	return TRUE;
}


static gboolean
attr_pos (GsfXMLIn *xin, xmlChar const **attrs,
	  char const *target,
	  GnmCellPos *res)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	char const *end;
	GnmCellPos tmp;

	g_return_val_if_fail (attrs != NULL, FALSE);
	g_return_val_if_fail (attrs[0] != NULL, FALSE);
	g_return_val_if_fail (attrs[1] != NULL, FALSE);

	if (strcmp (attrs[0], target))
		return FALSE;

	end = cellpos_parse (attrs[1], gnm_sheet_get_size (state->sheet), &tmp, TRUE);
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
	static const GnmSheetSize xlsx_size = {
		XLSX_MaxCol, XLSX_MaxRow
	};

	g_return_val_if_fail (attrs != NULL, FALSE);
	g_return_val_if_fail (attrs[0] != NULL, FALSE);
	g_return_val_if_fail (attrs[1] != NULL, FALSE);

	if (strcmp (attrs[0], target))
		return FALSE;

	if (!range_parse (res, attrs[1], &xlsx_size))
		xlsx_warning (xin, _("Invalid range '%s' for attribute %s"),
			attrs[1], target);
	return TRUE;
}

static GnmValue *
attr_datetime (GsfXMLIn *xin, xmlChar const **attrs,
	       char const *target)
{
	unsigned y, m, d, h, mi, n;
	GnmValue *res = NULL;
	gnm_float s;

	g_return_val_if_fail (attrs != NULL, NULL);
	g_return_val_if_fail (attrs[0] != NULL, NULL);
	g_return_val_if_fail (attrs[1] != NULL, NULL);

	if (strcmp (attrs[0], target))
		return NULL;

	n = gnm_sscanf (attrs[1], "%u-%u-%uT%u:%u:%" GNM_SCANF_g, &y, &m, &d, &h, &mi, &s);
	if (n >= 3) {
		GDate date;
		g_date_set_dmy (&date, d, m, y);
		if (g_date_valid (&date)) {
			XLSXReadState *state = (XLSXReadState *)xin->user_state;
			unsigned d_serial = go_date_g_to_serial (&date,
				workbook_date_conv (state->wb));
			if (n >= 6) {
				gnm_float time_frac = h + (gnm_float)mi / 60 + (gnm_float)s / 3600;
				res = value_new_float (d_serial + time_frac / 24);
				value_set_fmt (res, state->date_fmt);
			} else {
				res = value_new_int (d_serial);
				value_set_fmt (res, go_format_default_date ());
			}
		}
	}

	return res;
}

/* returns pts */
static gboolean
xlsx_parse_distance (GsfXMLIn *xin, xmlChar const *str,
		     char const *name, double *pts)
{
	double num;
	char *end = NULL;

	g_return_val_if_fail (str != NULL, FALSE);

	num = go_strtod (CXML2C (str), &end);
	if (CXML2C (str) != end) {
		if (0 == strncmp (end, "mm", 2)) {
			num = GO_CM_TO_PT (num/10.);
			end += 2;
		} else if (0 == strncmp (end, "cm", 2)) {
			num = GO_CM_TO_PT (num);
			end += 2;
		} else if (0 == strncmp (end, "pt", 2)) {
			end += 2;
		} else if (0 == strncmp (end, "pc", 2)) { /* pica 12pt == 1 pica */
			num /= 12.;
			end += 2;
		} else if (0 == strncmp (end, "pi", 2)) { /* pica 12pt == 1 pica */
			num /= 12.;
			end += 2;
		} else if (0 == strncmp (end, "in", 2)) {
			num = GO_IN_TO_PT (num);
			end += 2;
		} else {
			xlsx_warning (xin, _("Invalid attribute '%s', unknown unit '%s'"),
				    name, str);
			return FALSE;
		}
	} else {
		xlsx_warning (xin, _("Invalid attribute '%s', expected distance, received '%s'"),
			    name, str);
		return FALSE;
	}

	if  (*end) {
		return xlsx_warning (xin,
			_("Invalid attribute '%s', expected distance, received '%s'"),
				     name, str);
		return FALSE;
	}

	*pts = num;
	return TRUE;
}

/* returns pts */
static gboolean
attr_distance (GsfXMLIn *xin, xmlChar const **attrs,
	       char const *target, double *pts)
{
	g_return_val_if_fail (attrs != NULL, FALSE);
	g_return_val_if_fail (attrs[0] != NULL, FALSE);
	g_return_val_if_fail (attrs[1] != NULL, FALSE);

	if (strcmp (attrs[0], target))
		return FALSE;

	return xlsx_parse_distance (xin, attrs[1], target, pts);
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
simple_uint (GsfXMLIn *xin, xmlChar const **attrs, unsigned *res)
{
	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (attr_uint (xin, attrs, "val", res))
			return TRUE;
	return FALSE;
}

static gboolean
simple_double (GsfXMLIn *xin, xmlChar const **attrs, double *res)
{
	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (attr_double (xin, attrs, "val", res))
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

static const char *
simple_string (G_GNUC_UNUSED GsfXMLIn *xin, xmlChar const **attrs)
{
	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (strcmp (attrs[0], "val") == 0)
			return CXML2C (attrs[1]);
	return NULL;
}

/***********************************************************************
 * These indexes look like the values in xls.  Dup some code from there.
 * TODO : Can we merge the code ?
 *	  Will the 'indexedColors' look like a palette ?
 */
static const struct {
	guint8 r, g, b;
} xlsx_default_palette_v8[] = {
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

static GOColor
indexed_color (G_GNUC_UNUSED XLSXReadState *state, gint idx)
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
		return GO_COLOR_WHITE;
	switch (idx) {
	case 0:   /* black */
	case 64 : /* system text ? */
	case 81 : /* tooltip text */
	case 0x7fff : /* system text ? */
		return GO_COLOR_BLACK;

	case 1 :  /* white */
	case 65 : /* system back ? */
		return GO_COLOR_WHITE;

	case 80 : /* tooltip background */
		return GO_COLOR_YELLOW;

	case 2 : return GO_COLOR_RED;
	case 3 : return GO_COLOR_GREEN;
	case 4 : return GO_COLOR_BLUE;
	case 5 : return GO_COLOR_YELLOW;
	case 6 : return GO_COLOR_VIOLET;
	case 7 : return GO_COLOR_CYAN;

	default :
		 break;
	}

	idx -= 8;
	if (idx < 0 || (int) G_N_ELEMENTS (xlsx_default_palette_v8) <= idx) {
		g_warning ("EXCEL: color index (%d) is out of range (8..%d). Defaulting to black",
			   idx + 8, (int)G_N_ELEMENTS (xlsx_default_palette_v8) + 8);
		return GO_COLOR_BLACK;
	}

	/* TODO cache and ref */
	return GO_COLOR_FROM_RGB (xlsx_default_palette_v8[idx].r,
				  xlsx_default_palette_v8[idx].g,
				  xlsx_default_palette_v8[idx].b);
}

static gboolean
themed_color_from_name (XLSXReadState *state, const char *name, GOColor *color)
{
	gpointer val;
	gboolean dark = FALSE; // FIXME: refers to theme; from where?
	static const struct {
		const char *name;
		const char *dark;  // Color name when theme is dark
		const char *light; // Color name when theme is light
	} aliases[] = {
		{ "tx1", "lt1", "dk1" },
		{ "tx2", "lt2", "dk2" },
		{ "bg1", "dk1", "lt1" },
		{ "bg2", "dk2", "lt2" }
	};
	unsigned ui;

	if (g_hash_table_lookup_extended (state->theme_colors_by_name, name, NULL, &val)) {
		*color = GPOINTER_TO_UINT (val);
		return TRUE;
	}

	for (ui = 0; ui < G_N_ELEMENTS (aliases); ui++) {
		if (strcmp (name, aliases[ui].name) == 0) {
			name = dark ? aliases[ui].dark : aliases[ui].light;
			return themed_color_from_name (state, name, color);
		}
	}

	return FALSE;
}


static GOColor
themed_color (GsfXMLIn *xin, gint idx)
{
	static char const * const theme_elements [] = {
		"lt1",	"dk1", "lt2", "dk2",
		"accent1", "accent2", "accent3", "accent4", "accent5", "accent6",
		"hlink", "folHlink"
	};
	XLSXReadState	*state = (XLSXReadState *)xin->user_state;

	/* MAGIC :
	 * looks like the indicies map to hard coded names rather than the
	 * order in the file.  Indeed the order in the file seems wrong
	 * it inverts the first to pairs
	 *	1,0,3,2, 4,5,6.....
	 * see:	http://openxmldeveloper.org/forums/thread/1306.aspx
	 * OOo seems to do something similar
	 *
	 * I'll make the assumption we should work by name rather than
	 * index. */
	if (idx >= 0 && idx < (int) G_N_ELEMENTS (theme_elements)) {
		GOColor color;
		if (themed_color_from_name (state, theme_elements[idx], &color))
			return color;

		xlsx_warning (xin, _("Unknown theme color %d"), idx);
	} else {
		xlsx_warning (xin, "Color index (%d) is out of range (0..%d). Defaulting to black",
			      idx, (int) G_N_ELEMENTS (theme_elements));
	}

	return GO_COLOR_BLACK;
}

static GOFormat *
xlsx_get_num_fmt (GsfXMLIn *xin, char const *id, gboolean quiet)
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
		/* 14 */ NULL, // Locale version of "mm-dd-yy"
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
	if (end == id || *end != 0 || i < 0 || i >= (int) G_N_ELEMENTS (std_builtins))
		i = -1;

	if (i >= 0 && std_builtins[i] != NULL) {
		res = go_format_new_from_XL (std_builtins[i]);
		g_hash_table_replace (state->num_fmts, g_strdup (id), res);
	} else if (i == 14) {
		// Format 14 is locale dependent.  ms-excel-read.c suggests that maybe
		// 15 should be too, but I cannot verify that anywhere.
		res = go_format_new_magic (GO_FORMAT_MAGIC_SHORT_DATE);
		g_hash_table_replace (state->num_fmts, g_strdup (id), res);
	} else {
		if (!quiet)
			xlsx_warning (xin, _("Undefined number format id '%s'"), id);
	}

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
	if (NULL == texpr) {
		xlsx_warning (xin, "At %s: '%s' %s",
			      parsepos_as_string (pp),
			      expr_str, err.err->message);
		texpr = gnm_expr_top_new
			(gnm_expr_new_funcall1
			 (gnm_func_lookup_or_add_placeholder ("ERROR"),
			  gnm_expr_new_constant
			  (value_new_string (expr_str))));
	}
	parse_error_free (&err);

	return texpr;
}

/* Returns: a GSList of GnmRange in _reverse_ order
 * caller frees the list and the content */
static GSList *
xlsx_parse_sqref (GsfXMLIn *xin, xmlChar const *refs)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	GnmRange  r;
	xmlChar const *tmp;
	GSList	 *res = NULL;

	while (NULL != refs && *refs) {
		if (NULL == (tmp = cellpos_parse (refs, gnm_sheet_get_size (state->sheet), &r.start, FALSE))) {
			xlsx_warning (xin, "unable to parse reference list '%s'", refs);
			return res;
		}

		refs = tmp;
		if (*refs == '\0' || *refs == ' ')
			r.end = r.start;
		else if (*refs != ':' ||
			 NULL == (tmp = cellpos_parse (refs + 1, gnm_sheet_get_size (state->sheet), &r.end, FALSE))) {
			xlsx_warning (xin, "unable to parse reference list '%s'", refs);
			return res;
		}

		range_normalize (&r); /* be anal */
		res = g_slist_prepend (res, gnm_range_dup (&r));

		for (refs = tmp ; *refs == ' ' ; refs++ ) ;
	}

	return res;
}

/***********************************************************************/

#include "xlsx-read-pivot.c"

/***********************************************************************/

static void xlsx_ext_begin (GsfXMLIn *xin, xmlChar const **attrs);
static void xlsx_ext_end (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob);

#include "xlsx-read-color.c"
#include "xlsx-read-drawing.c"

/***********************************************************************/

static GnmColor *
elem_color (GsfXMLIn *xin, xmlChar const **attrs, gboolean allow_alpha)
{
	XLSXReadState	*state = (XLSXReadState *)xin->user_state;
	int indx;
	GOColor c = GO_COLOR_BLACK;
	double tint = 0.;
	gboolean has_color = FALSE;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2) {
		if (0 == strcmp (attrs[0], "rgb")) {
			guint a, r, g, b;
			if (4 != sscanf (attrs[1], "%02x%02x%02x%02x", &a, &r, &g, &b)) {
				xlsx_warning (xin,
					_("Invalid color '%s' for attribute rgb"),
					attrs[1]);
				return NULL;
			}
			has_color = TRUE;
			c = GO_COLOR_FROM_RGBA (r,g,b,a);
		} else if (attr_int (xin, attrs, "indexed", &indx)) {
			has_color = TRUE;
			c = indexed_color (state, indx);
		} else if (attr_int (xin, attrs, "theme", &indx)) {
			has_color = TRUE;
			c = themed_color (xin, indx);
		} else if (attr_double (xin, attrs, "tint", &tint))
			; /* Nothing */
	}

	if (!has_color)
		return NULL;
	c = gnm_go_color_apply_tint (c, tint);
	if (!allow_alpha)
		c = GO_COLOR_CHANGE_A (c, 0xFF);
	return gnm_color_new_go (c);
}

static GnmStyle *
xlsx_get_style_xf (GsfXMLIn *xin, int xf)
{
	XLSXReadState	*state = (XLSXReadState *)xin->user_state;
	if (0 <= xf && NULL != state->style_xfs && xf < (int)state->style_xfs->len)
		return g_ptr_array_index (state->style_xfs, xf);
	xlsx_warning (xin, _("Undefined style record '%d'"), xf);
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

	if (*xin->content->str == 0) {
		// See #517
		state->val = value_new_empty ();
		return;
	}

	switch (state->pos_type) {
	case XLXS_TYPE_NUM :
		state->val = value_new_float (gnm_strto (xin->content->str, &end));
		break;
	case XLXS_TYPE_SST_STR :
		i = xlsx_relaxed_strtol (xin->content->str, &end, 10);
		if (end != xin->content->str && *end == '\0' &&
		    0 <= i  && i < (int)state->sst->len) {
			entry = &g_array_index (state->sst, XLSXStr, i);
			go_string_ref (entry->str);
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
xlsx_cell_inline_text_end (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	XLSXReadState	*state = (XLSXReadState *)xin->user_state;
	go_string_append_gstring (state->r_text, xin->content);
}


static void
xlsx_cell_inline_begin (GsfXMLIn *xin, xmlChar const **attrs)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;

	state->r_text = g_string_new ("");
}

static void
xlsx_cell_inline_end (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;

	state->val = value_new_string_nocopy (g_string_free (state->r_text, FALSE));
	state->r_text = NULL;

	if (state->rich_attrs) {
		GOFormat *fmt = go_format_new_markup (state->rich_attrs, FALSE);
		state->rich_attrs = NULL;
		value_set_fmt (state->val, fmt);
		go_format_unref (fmt);
	}
}


static void
xlsx_cell_expr_begin (GsfXMLIn *xin, xmlChar const **attrs)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	gboolean has_range = FALSE, is_array = FALSE, is_shared = FALSE;
	GnmRange range;
	xmlChar const *shared_id = NULL;

	/* See https://bugzilla.gnome.org/show_bug.cgi?id=642850 */
	/* for some of the issues surrounding shared formulas.   */

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (0 == strcmp (attrs[0], "t")) {
			if (0 == strcmp (attrs[1], "array"))
				is_array = TRUE;
			else if (0 == strcmp (attrs[1], "shared"))
				is_shared = TRUE;
		} else if (0 == strcmp (attrs[0], "si"))
			shared_id = attrs[1];
		else if (attr_range (xin, attrs, "ref", &range))
			has_range = TRUE;

	state->shared_id = NULL;
	if (is_shared && NULL != shared_id) {
		if (!has_range)
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

	// The implied position is the next cell to the right
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
		/* There may already be a row style set!*/
		sheet_style_apply_pos (state->sheet,
			state->pos.col, state->pos.row, style);
	}
}

static void
xlsx_cell_end (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	GnmCell *cell;

	if (state->texpr == NULL && state->val == NULL) {
		/* A cell with only style.  */
		goto done;
	}

	cell = sheet_cell_fetch (state->sheet, state->pos.col, state->pos.row);

	if (NULL == cell) {
		xlsx_warning (xin, _("Invalid cell %s"),
			cellpos_as_string (&state->pos));
		value_release (state->val);
		if (NULL != state->texpr)
			gnm_expr_top_unref (state->texpr);
	} else if (NULL != state->texpr) {
		if (state->array.start.col >= 0) {
			gnm_cell_set_array (state->sheet,
					    &state->array,
					    state->texpr);
			gnm_expr_top_unref (state->texpr);
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
	} else if (NULL != state->val)
		gnm_cell_assign_value (cell, state->val);

	// We use an empty value as an indicator for "no value"
	if (VALUE_IS_EMPTY (state->val)) {
		cell_queue_recalc (cell);
	}

	state->texpr = NULL;
	state->val = NULL;

done:
	// The next implies position is one cell to the right
	state->pos.col++;
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

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2) {
		if (attr_int (xin, attrs, "r", &row))
			;
		else if (attr_double (xin, attrs, "ht", &h))
			;
		else if (attr_bool (xin, attrs, "customFormat", &cust_fmt))
			;
		else if (attr_bool (xin, attrs, "customHeight", &cust_height))
			;
		else if (attr_int (xin, attrs, "s", &xf_index))
			style = xlsx_get_xf (xin, xf_index);
		else if (attr_int (xin, attrs, "outlineLevel", &outline))
			;
		else if (attr_bool (xin, attrs, "hidden", &hidden))
			;
		else if (attr_bool (xin, attrs, "collapsed", &collapsed))
			;
	}

	if (row == -1)
		row = state->pos.row;
	else
		row--;
	state->pos.col = 0;

	if (row >= 0) {
		if (h >= 0)
			sheet_row_set_size_pts (state->sheet, row, h, cust_height);
		if (hidden > 0)
			colrow_set_visibility (state->sheet, FALSE, FALSE, row, row);
		if (outline >= 0)
			colrow_info_set_outline (sheet_row_fetch (state->sheet, row),
				outline, collapsed);

		if (NULL != style && cust_fmt) {
			GnmRange r;
			r.start.row = r.end.row = row;
			r.start.col = 0;
			r.end.col  = gnm_sheet_get_max_cols (state->sheet) - 1;
			gnm_style_ref (style);
			sheet_style_set_range (state->sheet, &r, style);
		}
	}

	maybe_update_progress (xin);
}

static void
xlsx_CT_Row_end (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	// The next implied row is the one below
	state->pos.row++;
}

static void
xlsx_CT_RowsCols_end (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;

	if (!state->pending_rowcol_style)
		return;

	sheet_style_set_range (state->sheet,
			       &state->pending_rowcol_range,
			       state->pending_rowcol_style);

	state->pending_rowcol_style = NULL;

	maybe_update_progress (xin);
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

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2) {
		if (attr_int (xin, attrs, "min", &first))
			;
		else if (attr_int (xin, attrs, "max", &last))
			;
		else if (attr_double (xin, attrs, "width", &width))
			/* FIXME FIXME FIXME arbitrary map from 130 pixels to
			 * the value stored for a column with 130 pixel width*/
			width *= (130. / 18.5703125) * (72./96.);
		else if (attr_bool (xin, attrs, "customWidth", &cust_width))
			;
		else if (attr_bool (xin, attrs, "bestFit", &best_fit))
			;
		else if (attr_int (xin, attrs, "style", &xf_index))
			style = xlsx_get_xf (xin, xf_index);
		else if (attr_int (xin, attrs, "outlineLevel", &outline))
			;
		else if (attr_bool (xin, attrs, "hidden", &hidden))
			;
		else if (attr_bool (xin, attrs, "collapsed", &collapsed))
			;
	}

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

	first = CLAMP (first, 0, gnm_sheet_get_last_col (state->sheet));
	last = CLAMP (last, 0, gnm_sheet_get_last_col (state->sheet));

	for (i = first; i <= last; i++) {
		if (width > 4)
			sheet_col_set_size_pts (state->sheet, i, width,
				cust_width && !best_fit);
		if (outline > 0)
			colrow_info_set_outline (sheet_col_fetch (state->sheet, i),
				outline, collapsed);
	}
	if (NULL != style) {
		GnmRange r;
		range_init_cols (&r, state->sheet, first, last);

		/*
		 * Sometimes we see a lot of columns with the same style.
		 * We delay applying the style because applying column
		 * by column leads to style fragmentation.  #622365
		 */

		if (style != state->pending_rowcol_style ||
		    state->pending_rowcol_range.start.row != r.start.row ||
		    state->pending_rowcol_range.end.row != r.end.row ||
		    state->pending_rowcol_range.end.col + 1 != r.start.col)
			xlsx_CT_RowsCols_end (xin, NULL);

		if (state->pending_rowcol_style)
			state->pending_rowcol_range.end.col = r.end.col;
		else {
			gnm_style_ref (style);
			state->pending_rowcol_style = style;
			state->pending_rowcol_range = r;
		}
	}
	if (hidden > 0)
		colrow_set_visibility (state->sheet, TRUE, FALSE, first, last);
}

static void
xlsx_CT_SheetPr (G_GNUC_UNUSED GsfXMLIn *xin, G_GNUC_UNUSED xmlChar const **attrs)
{
#if 0
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
    <xsd:attribute name="syncHorizontal" type="xsd:boolean" use="optional" default="false">
    <xsd:attribute name="syncVertical" type="xsd:boolean" use="optional" default="false">
    <xsd:attribute name="syncRef" type="ST_Ref" use="optional">
    <xsd:attribute name="transitionEvaluation" type="xsd:boolean" use="optional" default="false">
    <xsd:attribute name="transitionEntry" type="xsd:boolean" use="optional" default="false">
    <xsd:attribute name="published" type="xsd:boolean" use="optional" default="true">
    <xsd:attribute name="codeName" type="xsd:string" use="optional">
    <xsd:attribute name="filterMode" type="xsd:boolean" use="optional" default="false">
    <xsd:attribute name="enableFormatConditionsCalculation" type="xsd:boolean" use="optional" default="true">
#endif
}

static void
xlsx_sheet_tab_fgbg (GsfXMLIn *xin, xmlChar const **attrs, gboolean fg)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	GnmColor *color = elem_color (xin, attrs, TRUE);
	if (color) {
		g_object_set (state->sheet,
			      (fg ? "tab-foreground" : "tab-background"), color,
			      NULL);
		style_color_unref (color);
	}
}

static void
xlsx_sheet_tabcolor (GsfXMLIn *xin, xmlChar const **attrs)
{
	xlsx_sheet_tab_fgbg (xin, attrs, FALSE);
}

static void
xlsx_ext_tabtextcolor (GsfXMLIn *xin, xmlChar const **attrs)
{
	xlsx_sheet_tab_fgbg (xin, attrs, TRUE);
}

static void
xlsx_sheet_page_setup (G_GNUC_UNUSED GsfXMLIn *xin, G_GNUC_UNUSED xmlChar const **attrs)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	GnmPrintInformation *pi = state->sheet->print_info;
	gboolean tmp;

	if (pi->page_setup == NULL)
		gnm_print_info_load_defaults (pi);

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2) {
		if (attr_bool (xin, attrs, "fitToPage", &tmp))
			pi->scaling.type = tmp ? PRINT_SCALE_FIT_PAGES : PRINT_SCALE_PERCENTAGE;
	}
}

static void
xlsx_CT_SheetFormatPr (GsfXMLIn *xin, xmlChar const **attrs)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	double h, w;
	int i;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2) {
		if (attr_double (xin, attrs, "defaultColWidth", &w))
			sheet_col_set_default_size_pts (state->sheet, w);
		else if (attr_double (xin, attrs, "defaultRowHeight", &h))
			sheet_row_set_default_size_pts (state->sheet, h);
		else if (attr_int (xin, attrs, "outlineLevelRow", &i)) {
			if (i > 0)
				sheet_colrow_gutter (state->sheet, FALSE, i);
		} else if (attr_int (xin, attrs, "outlineLevelCol", &i)) {
			if (i > 0)
				sheet_colrow_gutter (state->sheet, TRUE, i);
		}
	}
}

static GtkPaperSize *
xlsx_paper_size (gdouble width, gdouble height, GtkUnit unit, int code)
{
	GtkPaperSize *size;
	gchar *name;
	gchar *display_name;

	if (code == 0) {
		name = g_strdup_printf ("xlsx_%ix%i", (int)width, (int)height);
		display_name = g_strdup_printf (_("Paper from XLSX file: %ipt\xE2\xA8\x89%ipt"),
						(int)width, (int)height);
	} else {
		name = g_strdup_printf ("xlsx_%i", code);
		display_name = g_strdup_printf (_("Paper from XLSX file, #%i"), code);
	}
	size = gtk_paper_size_new_custom (name, display_name, width, height, unit);
	g_free (name);
	g_free (display_name);
	return size;
}

static gboolean
xlsx_set_paper_from_code (GnmPrintInformation *pi, int code)
{
	XLSXPaperDefs paper[] =
		{{ 0 , 0 , 0 , GTK_UNIT_MM , NULL },
		 { 1 , 8.5 , 11 , GTK_UNIT_INCH , GTK_PAPER_NAME_LETTER },
		 { 2 , 8.5 , 11 , GTK_UNIT_INCH , GTK_PAPER_NAME_LETTER },
		 { 3 , 11 , 17 , GTK_UNIT_INCH , "na_ledger_11x17in" },
		 { 4 , 17 , 11 , GTK_UNIT_INCH , NULL },
		 { 5 , 8.5 , 14 , GTK_UNIT_INCH , GTK_PAPER_NAME_LEGAL },
		 { 6 , 5.5 , 8.5 , GTK_UNIT_INCH , "na_invoice_5.5x8.5in" },
		 { 7 , 7.25 , 10.5 , GTK_UNIT_INCH , GTK_PAPER_NAME_EXECUTIVE },
		 { 8 , 297 , 420 , GTK_UNIT_MM , GTK_PAPER_NAME_A3 },
		 { 9 , 210 , 297 , GTK_UNIT_MM , GTK_PAPER_NAME_A4 },
		 { 10 , 210 , 297 , GTK_UNIT_MM , GTK_PAPER_NAME_A4 },
		 { 11 , 148 , 210 , GTK_UNIT_MM , GTK_PAPER_NAME_A5 },
		 { 12 , 250 , 353 , GTK_UNIT_MM , "iso_b4_250x353mm" },
		 { 13 , 176 , 250 , GTK_UNIT_MM , GTK_PAPER_NAME_B5 },
		 { 14 , 8.5 , 13 , GTK_UNIT_INCH , "na_foolscap_8.5x13in" },
		 { 15 , 215 , 275 , GTK_UNIT_MM , NULL },
		 { 16 , 10 , 14 , GTK_UNIT_INCH , "na_10x14_10x14in" },
		 { 17 , 11 , 17 , GTK_UNIT_INCH , "na_ledger_11x17in" },
		 { 18 , 8.5 , 11 , GTK_UNIT_INCH , GTK_PAPER_NAME_LETTER },
		 { 19 , 3.875 , 8.875 , GTK_UNIT_INCH , "na_number-9_3.875x8.875in" },
		 { 20 , 4.125 , 9.5 , GTK_UNIT_INCH , "na_number-10_4.125x9.5in" },
		 { 21 , 4.5 , 10.375 , GTK_UNIT_INCH , "na_number-11_4.5x10.375in" },
		 { 22 , 4.75 , 11 , GTK_UNIT_INCH , "na_number-12_4.75x11in" },
		 { 23 , 5 , 11.5 , GTK_UNIT_INCH , "na_number-14_5x11.5in" },
		 { 24 , 17 , 22 , GTK_UNIT_INCH , "na_c_17x22in" },
		 { 25 , 22 , 34 , GTK_UNIT_INCH , "na_d_22x34in" },
		 { 26 , 34 , 44 , GTK_UNIT_INCH , "na_e_34x44in" },
		 { 27 , 110 , 220 , GTK_UNIT_MM , "iso_dl_110x220mm" },
		 { 28 , 162 , 229 , GTK_UNIT_MM , "iso_c5_162x229mm" },
		 { 29 , 324 , 458 , GTK_UNIT_MM , "iso_c3_324x458mm" },
		 { 30 , 229 , 324 , GTK_UNIT_MM , "iso_c4_229x324mm" },
		 { 31 , 114 , 162 , GTK_UNIT_MM , "iso_c6_114x162mm" },
		 { 32 , 114 , 229 , GTK_UNIT_MM , "iso_c6c5_114x229mm" },
		 { 33 , 250 , 353 , GTK_UNIT_MM , "iso_b4_250x353mm" },
		 { 34 , 176 , 250 , GTK_UNIT_MM , GTK_PAPER_NAME_B5 },
		 { 35 , 176 , 125 , GTK_UNIT_MM , NULL },
		 { 36 , 110 , 230 , GTK_UNIT_MM , "om_italian_110x230mm" },
		 { 37 , 3.875 , 7.5 , GTK_UNIT_INCH , "na_monarch_3.875x7.5in" },
		 { 38 , 3.625 , 6.5 , GTK_UNIT_INCH , "na_personal_3.625x6.5in" },
		 { 39 , 14.875 , 11 , GTK_UNIT_INCH , NULL },
		 { 40 , 8.5 , 12 , GTK_UNIT_INCH , "na_fanfold-eur_8.5x12in" },
		 { 41 , 8.5 , 13 , GTK_UNIT_INCH , "na_foolscap_8.5x13in" },
		 { 42 , 250 , 353 , GTK_UNIT_MM , "iso_b4_250x353mm" },
		 { 43 , 200 , 148 , GTK_UNIT_MM , NULL },
		 { 44 , 9 , 11 , GTK_UNIT_INCH , "na_9x11_9x11in" },
		 { 45 , 10 , 11 , GTK_UNIT_INCH , "na_10x11_10x11in" },
		 { 46 , 15 , 11 , GTK_UNIT_INCH , NULL },
		 { 47 , 220 , 220 , GTK_UNIT_MM , "om_invite_220x220mm" },
		 { 48 , 0 , 0 , GTK_UNIT_MM , NULL },
		 { 49 , 0 , 0 , GTK_UNIT_MM , NULL },
		 { 50 , 9.275 , 12 , GTK_UNIT_INCH , NULL },
		 { 51 , 9.275 , 15 , GTK_UNIT_INCH , NULL },
		 { 52 , 11.69 , 18 , GTK_UNIT_INCH , NULL },
		 { 53 , 236 , 322 , GTK_UNIT_MM , "iso_a4-extra_235.5x322.3" },
		 { 54 , 8.275 , 11 , GTK_UNIT_INCH , NULL },
		 { 55 , 210 , 297 , GTK_UNIT_MM , GTK_PAPER_NAME_A4 },
		 { 56 , 9.275 , 12 , GTK_UNIT_INCH , NULL },
		 { 57 , 227 , 356 , GTK_UNIT_MM , NULL },
		 { 58 , 305 , 487 , GTK_UNIT_MM , NULL },
		 { 59 , 8.5 , 12.69 , GTK_UNIT_INCH , "na_letter-plus_8.5x12.69in" },
		 { 60 , 210 , 330 , GTK_UNIT_MM , "om_folio_210x330mm" },
		 { 61 , 148 , 210 , GTK_UNIT_MM , GTK_PAPER_NAME_A5 },
		 { 62 , 182 , 257 , GTK_UNIT_MM , "jis_b5_182x257mm" },
		 { 63 , 322 , 445 , GTK_UNIT_MM , "iso_a3-extra_322x445mm" },
		 { 64 , 174 , 235 , GTK_UNIT_MM , "iso_a5-extra_174x235mm" },
		 { 65 , 201 , 276 , GTK_UNIT_MM , "iso_b5-extra_201x276mm" },
		 { 66 , 420 , 594 , GTK_UNIT_MM , "iso_a2_420x594mm" },
		 { 67 , 297 , 420 , GTK_UNIT_MM , GTK_PAPER_NAME_A3 },
		 { 68 , 322 , 445 , GTK_UNIT_MM , "iso_a3-extra_322x445mm" },
		 { 69 , 200 , 148 , GTK_UNIT_MM , NULL },
		 { 70 , 105 , 148 , GTK_UNIT_MM , "iso_a6_105x148mm" },
		 { 71 , 240 , 332 , GTK_UNIT_MM , "jpn_kaku2_240x332mm" },
		 { 72 , 216 , 277 , GTK_UNIT_MM , NULL },
		 { 73 , 120 , 235 , GTK_UNIT_MM , "jpn_chou3_120x235mm" },
		 { 74 , 90 , 205 , GTK_UNIT_MM , "jpn_chou4_90x205mm" },
		 { 75 , 11 , 8.5 , GTK_UNIT_INCH , NULL },
		 { 76 , 420 , 297 , GTK_UNIT_MM , NULL },
		 { 77 , 297 , 210 , GTK_UNIT_MM , NULL },
		 { 78 , 210 , 148 , GTK_UNIT_MM , NULL },
		 { 79 , 364 , 257 , GTK_UNIT_MM , NULL },
		 { 80 , 257 , 182 , GTK_UNIT_MM , NULL },
		 { 81 , 148 , 100 , GTK_UNIT_MM , NULL },
		 { 82 , 148 , 200 , GTK_UNIT_MM , "jpn_oufuku_148x200mm" },
		 { 83 , 148 , 105 , GTK_UNIT_MM , NULL },
		 { 84 , 332 , 240 , GTK_UNIT_MM , NULL },
		 { 85 , 277 , 216 , GTK_UNIT_MM , NULL },
		 { 86 , 235 , 120 , GTK_UNIT_MM , NULL },
		 { 87 , 205 , 90 , GTK_UNIT_MM , NULL },
		 { 88 , 128 , 182 , GTK_UNIT_MM , "jis_b6_128x182mm" },
		 { 89 , 182 , 128 , GTK_UNIT_MM , NULL },
		 { 90 , 12 , 11 , GTK_UNIT_INCH , NULL },
		 { 91 , 235 , 105 , GTK_UNIT_MM , NULL },
		 { 92 , 105 , 235 , GTK_UNIT_MM , "jpn_you4_105x235mm" },
		 { 93 , 146 , 215 , GTK_UNIT_MM , "prc_16k_146x215mm" },
		 { 94 , 97 , 151 , GTK_UNIT_MM , "prc_32k_97x151mm" },
		 { 95 , 97 , 151 , GTK_UNIT_MM , "prc_32k_97x151mm" },
		 { 96 , 102 , 165 , GTK_UNIT_MM , "prc_1_102x165mm" },
		 { 97 , 102 , 176 , GTK_UNIT_MM , "prc_2_102x176mm" },
		 { 98 , 125 , 176 , GTK_UNIT_MM , "prc_3_125x176mm" },
		 { 99 , 110 , 208 , GTK_UNIT_MM , "prc_4_110x208mm" },
		 { 100 , 110 , 220 , GTK_UNIT_MM , "prc_5_110x220mm" },
		 { 101 , 120 , 230 , GTK_UNIT_MM , "prc_6_120x230mm" },
		 { 102 , 160 , 230 , GTK_UNIT_MM , "prc_7_160x230mm" },
		 { 103 , 120 , 309 , GTK_UNIT_MM , "prc_8_120x309mm" },
		 { 104 , 229 , 324 , GTK_UNIT_MM , "iso_c4_229x324mm" },
		 { 105 , 324 , 458 , GTK_UNIT_MM , "prc_10_324x458mm" },
		 { 106 , 215 , 146 , GTK_UNIT_MM , NULL },
		 { 107 , 151 , 97 , GTK_UNIT_MM , NULL },
		 { 108 , 151 , 97 , GTK_UNIT_MM , NULL },
		 { 109 , 165 , 102 , GTK_UNIT_MM , NULL },
		 { 110 , 176 , 102 , GTK_UNIT_MM , NULL },
		 { 111 , 176 , 125 , GTK_UNIT_MM , NULL },
		 { 112 , 208 , 110 , GTK_UNIT_MM , NULL },
		 { 113 , 220 , 110 , GTK_UNIT_MM , NULL },
		 { 114 , 230 , 120 , GTK_UNIT_MM , NULL },
		 { 115 , 230 , 160 , GTK_UNIT_MM , NULL },
		 { 116 , 309 , 120 , GTK_UNIT_MM , NULL },
		 { 117 , 324 , 229 , GTK_UNIT_MM , NULL },
		 { 118 , 458 , 324 , GTK_UNIT_MM , NULL }};

	if (code < 1 || ((guint) code) >= G_N_ELEMENTS (paper) || paper[code].code == 0)
		return FALSE;
	g_return_val_if_fail (paper[code].code == code, FALSE);

	if (paper[code].name != NULL) {
		GtkPaperSize *ps = gtk_paper_size_new (paper[code].name);
		if (ps != NULL) {
			gtk_page_setup_set_paper_size (pi->page_setup, ps);
			gtk_paper_size_free (ps);
			return TRUE;
		}
	}
	if (paper[code].width > 0.0 && paper[code].height > 0.0) {
		GtkPaperSize *ps = xlsx_paper_size (paper[code].width, paper[code].height, paper[code].unit, code);
		if (ps != NULL) {
			gtk_page_setup_set_paper_size (pi->page_setup, ps);
			gtk_paper_size_free (ps);
			return TRUE;
		}
	}

	return FALSE;
}

static void
xlsx_CT_PageSetup (GsfXMLIn *xin, xmlChar const **attrs)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	GnmPrintInformation *pi = state->sheet->print_info;
	int orient, paper_code = 0, scale, tmp_int;
	unsigned first_page = pi->start_page;
	gboolean orient_set = FALSE, use_first_page_number = TRUE, tmp_bool;
	double width = 0., height = 0.;
	static EnumVal const orientation_types[] = {
		{ "default",	GTK_PAGE_ORIENTATION_PORTRAIT },
		{ "portrait",	GTK_PAGE_ORIENTATION_PORTRAIT },
		{ "landscape",	GTK_PAGE_ORIENTATION_LANDSCAPE },
		{ NULL, 0 }
	};
	static EnumVal const comment_types[] = {
		{ "asDisplayed", GNM_PRINT_COMMENTS_IN_PLACE },
		{ "atEnd",	 GNM_PRINT_COMMENTS_AT_END },
		{ "none",        GNM_PRINT_COMMENTS_NONE },
		{ NULL, 0 }
	};
	static EnumVal const error_types[] = {
		{ "blank",     GNM_PRINT_ERRORS_AS_BLANK },
		{ "dash",      GNM_PRINT_ERRORS_AS_DASHES },
		{ "NA",        GNM_PRINT_ERRORS_AS_NA },
		{ "displayed", GNM_PRINT_ERRORS_AS_DISPLAYED },
		{ NULL, 0 }
	};
	static EnumVal const page_order_types[] = {
		{ "overThenDown", 1 },
		{ "downThenOver", 0  },
		{ NULL, 0 }
	};

	if (pi->page_setup == NULL)
		gnm_print_info_load_defaults (pi);
	/* In xlsx the default is 1 not 0 */
	pi->scaling.dim.rows = 1;
	pi->scaling.dim.cols = 1;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2) {
		if (attr_enum (xin, attrs, "orientation", orientation_types, &orient))
			orient_set = TRUE;
		else if (attr_enum (xin, attrs, "cellComments", comment_types, &tmp_int))
			pi->comment_placement = tmp_int;
		else if (attr_enum (xin, attrs, "errors", error_types, &tmp_int))
			pi->error_display = tmp_int;
		else if (attr_enum (xin, attrs, "pageOrder", page_order_types, &tmp_int))
			pi->print_across_then_down = (tmp_int != 0);
		else if (attr_int (xin, attrs, "paperSize", &paper_code))
			;
		else if (attr_distance (xin, attrs, "paperWidth", &width))
			;
		else if (attr_distance (xin, attrs, "paperHeight", &height))
			;
		else if (attr_bool (xin, attrs, "blackAndWhite", &tmp_bool))
			pi->print_black_and_white = tmp_bool;
		else if (attr_int (xin, attrs, "copies", &(pi->n_copies)))
			;
		else if (attr_bool (xin, attrs, "draft", &tmp_bool))
			pi->print_as_draft = tmp_bool;
		else if (strcmp (attrs[0], "firstPageNumber") == 0 &&
			 attrs[1][0] == '-') {
			// Supposed to be unsigned but we see a negative.
			// Parse and then see -1 (which will map to don't-use
			// a few steps down the line).
			int p = -1;
			(void)attr_int (xin, attrs, "firstPageNumber", &p);
			first_page = (unsigned)-1;
		} else if (attr_uint (xin, attrs, "firstPageNumber", &first_page))
			;
		else if (attr_int (xin, attrs, "fitToHeight", &(pi->scaling.dim.rows)))
			;
		else if (attr_int (xin, attrs, "fitToWidth", &(pi->scaling.dim.cols)))
			;
		else if (attr_int (xin, attrs, "scale", &scale)) {
			pi->scaling.percentage.x = scale;
			pi->scaling.percentage.y = scale;
		} else if (attr_bool (xin, attrs, "useFirstPageNumber", &use_first_page_number))
			;
	}

	// "OnlyOffice" has been seen producing files with UINT_MAX as the first page number.
	// Clearly they meant not to use it, so we cut off a INT_MAX.
	pi->start_page = use_first_page_number && first_page < INT_MAX
		? (int)first_page
		: -1;

	if (!xlsx_set_paper_from_code (pi, paper_code) && width > 0 && height > 0) {
		GtkPaperSize *ps = xlsx_paper_size (width, height, GTK_UNIT_POINTS, 0);
		gtk_page_setup_set_paper_size (pi->page_setup, ps);
		gtk_paper_size_free (ps);
	}
	if (orient_set)
		print_info_set_paper_orientation (pi, orient);
}

static void
xlsx_CT_PageMargins (GsfXMLIn *xin, xmlChar const **attrs)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	double margin;
	GnmPrintInformation *pi = state->sheet->print_info;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2) {
		if (attr_double (xin, attrs, "left", &margin))
			print_info_set_margin_left (pi, GO_IN_TO_PT (margin));
		else if (attr_double (xin, attrs, "right", &margin))
			print_info_set_margin_right (pi, GO_IN_TO_PT (margin));
		else if (attr_double (xin, attrs, "top", &margin))
			print_info_set_edge_to_below_header (pi, GO_IN_TO_PT (margin));
		else if (attr_double (xin, attrs, "bottom", &margin))
			print_info_set_edge_to_above_footer (pi, GO_IN_TO_PT (margin));
		else if (attr_double (xin, attrs, "header", &margin))
			print_info_set_margin_header (pi, GO_IN_TO_PT (margin));
		else if (attr_double (xin, attrs, "footer", &margin))
			print_info_set_margin_footer (pi, GO_IN_TO_PT (margin));
	}
}


static void
xlsx_CT_oddheader_end (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	GnmPrintInformation *pi = state->sheet->print_info;
	xls_header_footer_import (&pi->header, xin->content->str);
}

static void
xlsx_CT_oddfooter_end (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	GnmPrintInformation *pi = state->sheet->print_info;
	xls_header_footer_import (&pi->footer, xin->content->str);
}

static void
xlsx_CT_PageBreak (GsfXMLIn *xin, xmlChar const **attrs)
{
	XLSXReadState    *state = (XLSXReadState *)xin->user_state;
	GnmPageBreakType  type = GNM_PAGE_BREAK_AUTO;
	gboolean tmp;
	int	 pos;
	int first, last;

	if (NULL == state->page_breaks)
		return;

	pos = 0;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2) {
		if (attr_int  (xin, attrs, "id", &pos))
			;
		else if (attr_bool (xin, attrs, "man", &tmp)) {
			if (tmp) type = GNM_PAGE_BREAK_MANUAL;
		} else if (attr_bool (xin, attrs, "pt", &tmp)) {
			if (tmp) type = GNM_PAGE_BREAK_DATA_SLICE;
		} else if (attr_int  (xin, attrs, "min", &first))
			;
		else if (attr_int  (xin, attrs, "max", &last))
			;
	}

	gnm_page_breaks_append_break (state->page_breaks, pos, type);
}

static void
xlsx_CT_PageBreaks_begin (GsfXMLIn *xin, xmlChar const **attrs)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	int count = 0;
	int manual_count;

	g_return_if_fail (state->page_breaks == NULL);

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2) {
		if (attr_int  (xin, attrs, "count", &count))
			;
		else if (attr_int  (xin, attrs, "manualBreakCount", &manual_count))
			;
	}

	state->page_breaks = gnm_page_breaks_new (xin->node->user_data.v_int);
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
		{ "stop",	 GNM_VALIDATION_STYLE_STOP },
		{ "warning",	 GNM_VALIDATION_STYLE_WARNING },
		{ "information", GNM_VALIDATION_STYLE_INFO },
		{ NULL, 0 }
	};
	static EnumVal const val_types[] = {
		{ "none",	GNM_VALIDATION_TYPE_ANY },
		{ "whole",	GNM_VALIDATION_TYPE_AS_INT },
		{ "decimal",	GNM_VALIDATION_TYPE_AS_NUMBER },
		{ "list",	GNM_VALIDATION_TYPE_IN_LIST },
		{ "date",	GNM_VALIDATION_TYPE_AS_DATE },
		{ "time",	GNM_VALIDATION_TYPE_AS_TIME },
		{ "textLength",	GNM_VALIDATION_TYPE_TEXT_LENGTH },
		{ "custom",	GNM_VALIDATION_TYPE_CUSTOM },
		{ NULL, 0 }
	};
	static EnumVal const val_ops[] = {
		{ "between",	GNM_VALIDATION_OP_BETWEEN },
		{ "notBetween",	GNM_VALIDATION_OP_NOT_BETWEEN },
		{ "equal",	GNM_VALIDATION_OP_EQUAL },
		{ "notEqual",	GNM_VALIDATION_OP_NOT_EQUAL },
		{ "lessThan",		GNM_VALIDATION_OP_LT },
		{ "lessThanOrEqual",	GNM_VALIDATION_OP_LTE },
		{ "greaterThan",	GNM_VALIDATION_OP_GT },
		{ "greaterThanOrEqual",	GNM_VALIDATION_OP_GTE },
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
	ValidationStyle	val_style = GNM_VALIDATION_STYLE_STOP;
	ValidationType	val_type  = GNM_VALIDATION_TYPE_ANY;
	ValidationOp	val_op	  = GNM_VALIDATION_OP_BETWEEN;
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

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2) {
		if (0 == strcmp (attrs[0], "sqref"))
			refs = attrs[1];
		else if (attr_enum (xin, attrs, "errorStyle", val_styles, &tmp))
			val_style = tmp;
		else if (attr_enum (xin, attrs, "type", val_types, &tmp))
			val_type = tmp;
		else if (attr_enum (xin, attrs, "operator", val_ops, &tmp))
			val_op = tmp;

		else if (attr_bool (xin, attrs, "allowBlank", &allowBlank))
			;
		else if (attr_bool (xin, attrs, "showDropDown", &showDropDown))
			;
		else if (attr_bool (xin, attrs, "showInputMessage", &showInputMessage))
			;
		else if (attr_bool (xin, attrs, "showErrorMessage", &showErrorMessage))
			;
		else if (0 == strcmp (attrs[0], "errorTitle"))
			errorTitle = attrs[1];
		else if (0 == strcmp (attrs[0], "error"))
			error = attrs[1];
		else if (0 == strcmp (attrs[0], "promptTitle"))
			promptTitle = attrs[1];
		else if (0 == strcmp (attrs[0], "prompt"))
			prompt = attrs[1];
	}

	/* order matters, we need the 1st item */
	state->validation_regions = g_slist_reverse (
		xlsx_parse_sqref (xin, refs));

	if (state->validation_regions) {
		GnmRange const *r = state->validation_regions->data;
		state->pos = r->start;
	} else {
		state->pos.col = state->pos.row = 0;
	}

	if (showErrorMessage) {
		state->validation = gnm_validation_new
			(val_style, val_type, val_op,
			 state->sheet,
			 errorTitle, error,
			 NULL, NULL, allowBlank, !showDropDown);
		// Note: inverted sense of showDropDown
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
	    NULL != (err = gnm_validation_is_ok (state->validation))) {
		xlsx_warning (xin, _("Ignoring invalid data validation because : %s"),
			      _(err->message));
		gnm_validation_unref (state->validation);
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
xlsx_CT_DataValidation_sqref_end (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	const char *s = xin->content->str;

	state->validation_regions =
		g_slist_concat (
			g_slist_reverse (xlsx_parse_sqref (xin, s)),
			state->validation_regions);
}

static void
xlsx_validation_expr (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	GnmParsePos pp;
	GnmExprTop const *texpr;
	int i = xin->node->user_data.v_int;

	if (state->validation == NULL)
		return;

	/*  Sneaky buggers, parse relative to the 1st sqRef */
	parse_pos_init (&pp, NULL, state->sheet,
		state->pos.col, state->pos.row);
	texpr = xlsx_parse_expr (xin, xin->content->str, &pp);
	if (NULL != texpr) {
		gnm_validation_set_expr (state->validation, texpr, i);
		gnm_expr_top_unref (texpr);
	}
}

static void
xlsx_CT_AutoFilter_begin (GsfXMLIn *xin, xmlChar const **attrs)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	GnmRange r;

	g_return_if_fail (state->filter == NULL);

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2) {
		if (attr_range (xin, attrs, "ref", &r))
			state->filter = gnm_filter_new (state->sheet, &r, TRUE);
	}
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

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2) {
		if (attr_int  (xin, attrs, "colId", &id))
			;
		else if (attr_bool (xin, attrs, "hiddenButton", &hidden))
			;
		else if (attr_bool (xin, attrs, "showButton", &show))
			;
	}

	state->filter_cur_field = id;
}

static void
xlsx_CT_Filters_begin (GsfXMLIn *xin, xmlChar const **attrs)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2) {
		if (0 == strcmp (attrs[0], "val")) {
		}
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
xlsx_CT_Filter (G_GNUC_UNUSED GsfXMLIn *xin, G_GNUC_UNUSED xmlChar const **attrs)
{
#if 0
	XLSXReadState *state = (XLSXReadState *)xin->user_state;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (0 == strcmp (attrs[0], "val")) {
		}
#endif
}

static void
xlsx_CT_CustomFilters_begin (GsfXMLIn *xin, G_GNUC_UNUSED xmlChar const **attrs)
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
xlsx_CT_CustomFilter (G_GNUC_UNUSED GsfXMLIn *xin, G_GNUC_UNUSED xmlChar const **attrs)
{
	static EnumVal const ops[] = {
		{ "lessThan",		GNM_FILTER_OP_LT },
		{ "lessThanOrEqual",	GNM_FILTER_OP_LTE },
		{ "equal",		GNM_FILTER_OP_EQUAL },
		{ "notEqual",		GNM_FILTER_OP_NOT_EQUAL },
		{ "greaterThanOrEqual",	GNM_FILTER_OP_GTE },
		{ "greaterThan",	GNM_FILTER_OP_GT },
		{ NULL, 0 }
	};
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	int tmp;
	GnmFilterOp op = GNM_FILTER_OP_EQUAL;
	GnmValue *v = NULL;
	GnmFilterCondition *cond;
	GODateConventions const *date_conv = workbook_date_conv (state->wb);

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2) {
		if (0 == strcmp (attrs[0], "val")) {
			const char *txt = CXML2C (attrs[1]);
			value_release (v);
			v = format_match (txt, NULL, date_conv);
			if (!v)
				v = value_new_string (txt);
		} else if (attr_enum (xin, attrs, "operator", ops, &tmp)) {
			op = tmp;
		}
	}

	cond = gnm_filter_condition_new_single (op, v);
	if (cond)
		gnm_filter_set_condition (state->filter, state->filter_cur_field,
					  cond, FALSE);
}

static void
xlsx_CT_Top10 (GsfXMLIn *xin, xmlChar const **attrs)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	gboolean top = TRUE;
	gboolean percent = FALSE;
	gnm_float val = -1.;
	GnmFilterCondition *cond;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2) {
		if (attr_float (xin, attrs, "val", &val))
			;
		else if (attr_bool (xin, attrs, "top", &top))
			;
		else if (attr_bool (xin, attrs, "percent", &percent))
			;
	}

	if (NULL != (cond = gnm_filter_condition_new_bucket (top, !percent, FALSE, val)))
		gnm_filter_set_condition (state->filter, state->filter_cur_field,
			cond, FALSE);
}

static void
xlsx_CT_DynamicFilter (G_GNUC_UNUSED GsfXMLIn *xin, G_GNUC_UNUSED xmlChar const **attrs)
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

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2) {
		if (attr_enum (xin, attrs, "type", types, &type))
			;
	}
#endif
}

static void
xlsx_CT_MergeCell (GsfXMLIn *xin, xmlChar const **attrs)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	GnmRange r;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2) {
		if (attr_range (xin, attrs, "ref", &r))
			gnm_sheet_merge_add (state->sheet, &r, FALSE,
				GO_CMD_CONTEXT (state->context));
	}
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

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2) {
		if (attr_bool (xin, attrs, "sheet", &sheet))
			;
		else if (attr_bool (xin, attrs, "objects", &objects))
			;
		else if (attr_bool (xin, attrs, "scenarios", &scenarios))
			;
		else if (attr_bool (xin, attrs, "formatCells", &formatCells))
			;
		else if (attr_bool (xin, attrs, "formatColumns", &formatColumns))
			;
		else if (attr_bool (xin, attrs, "formatRows", &formatRows))
			;
		else if (attr_bool (xin, attrs, "insertColumns", &insertColumns))
			;
		else if (attr_bool (xin, attrs, "insertRows", &insertRows))
			;
		else if (attr_bool (xin, attrs, "insertHyperlinks", &insertHyperlinks))
			;
		else if (attr_bool (xin, attrs, "deleteColumns", &deleteColumns))
			;
		else if (attr_bool (xin, attrs, "deleteRows", &deleteRows))
			;
		else if (attr_bool (xin, attrs, "selectLockedCells", &selectLockedCells))
			;
		else if (attr_bool (xin, attrs, "sort", &sort))
			;
		else if (attr_bool (xin, attrs, "autoFilter", &autoFilter))
			;
		else if (attr_bool (xin, attrs, "pivotTables", &pivotTables))
			;
		else if (attr_bool (xin, attrs, "selectUnlockedCells", &selectUnlockedCells))
			;
	}

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
xlsx_cond_fmt_begin (GsfXMLIn *xin, xmlChar const **attrs)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	char const *refs = NULL;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2) {
		if (0 == strcmp (attrs[0], "sqref"))
			refs = attrs[1];
	}

	state->cond_regions = xlsx_parse_sqref (xin, refs);

	/* create in first call xlsx_cond_rule to avoid creating condition with
	 * no rules */
	state->conditions = NULL;
}

static void
xlsx_cond_fmt_end (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;

	if (NULL != state->conditions) {
		GSList   *ptr;
		GnmStyle *style = gnm_style_new ();
		gnm_style_set_conditions (style, state->conditions);
		for (ptr = state->cond_regions ; ptr != NULL ; ptr = ptr->next) {
			GnmRange *r = ptr->data;
			gnm_style_ref (style);
			sheet_style_apply_range	(state->sheet, r, style);
		}
		gnm_style_unref (style);
		state->conditions = NULL;
	}
	g_slist_free_full (state->cond_regions, g_free);
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
	XLSX_CF_TYPE_CONTAINS_STR,
	XLSX_CF_TYPE_NOT_CONTAINS_STR,
	XLSX_CF_TYPE_BEGINS_WITH,
	XLSX_CF_TYPE_ENDS_WITH,
	XLSX_CF_TYPE_CONTAINS_BLANKS,
	XLSX_CF_TYPE_NOT_CONTAINS_BLANKS,
	XLSX_CF_TYPE_CONTAINS_ERRORS,
	XLSX_CF_TYPE_NOT_CONTAINS_ERRORS,
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
		{ "doesNotContainText",	XLSX_CF_TYPE_NOT_CONTAINS_STR },  /* ??? */
		{ "notContainsText",	XLSX_CF_TYPE_NOT_CONTAINS_STR },
		{ "beginsWith",		XLSX_CF_TYPE_BEGINS_WITH },
		{ "endsWith",		XLSX_CF_TYPE_ENDS_WITH },
		{ "containsBlanks",	XLSX_CF_TYPE_CONTAINS_BLANKS },
		{ "containsNoBlanks",	XLSX_CF_TYPE_NOT_CONTAINS_BLANKS },  /* ??? */
		{ "notContainsBlanks",	XLSX_CF_TYPE_NOT_CONTAINS_BLANKS },
		{ "containsErrors",	XLSX_CF_TYPE_CONTAINS_ERRORS },
		{ "containsNoErrors",	XLSX_CF_TYPE_NOT_CONTAINS_ERRORS },  /* ??? */
		{ "notContainsErrors",	XLSX_CF_TYPE_NOT_CONTAINS_ERRORS },
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
	char const	*type_str = "-";
	GnmStyle        *overlay = NULL;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2) {
		if (attr_bool (xin, attrs, "formatRow", &formatRow))
			;
		else if (attr_bool (xin, attrs, "stopIfTrue", &stopIfTrue))
			;
		else if (attr_bool (xin, attrs, "above", &above))
			;
		else if (attr_bool (xin, attrs, "percent", &percent))
			;
		else if (attr_bool (xin, attrs, "bottom", &bottom))
			;
		else if (attr_int  (xin, attrs, "dxfId", &dxf))
			;
		else if (attr_enum (xin, attrs, "operator", ops, &tmp))
			op = tmp;
		else if (attr_enum (xin, attrs, "type", types, &tmp)) {
			type = tmp;
			type_str = attrs[1];
		}
	}
#if 0
/*
	"numFmtId"	="ST_NumFmtId" use="optional">
	"priority"	="xs:int" use="required">
	"text"		="xs:string" use="optional">
	"timePeriod"	="ST_TimePeriod" use="optional">
	"col1"		="xs:unsignedInt" use="optional">
	"col2"		="xs:unsignedInt" use="optional">
*/
#endif

	if (dxf >= 0)
		overlay = xlsx_get_dxf (xin, dxf);

	switch (type) {
	case XLSX_CF_TYPE_CELL_IS :
		/* Nothing */
		break;
	case XLSX_CF_TYPE_CONTAINS_STR:
	case XLSX_CF_TYPE_NOT_CONTAINS_STR:
	case XLSX_CF_TYPE_BEGINS_WITH:
	case XLSX_CF_TYPE_ENDS_WITH:
	case XLSX_CF_TYPE_CONTAINS_BLANKS:
	case XLSX_CF_TYPE_NOT_CONTAINS_BLANKS:
	case XLSX_CF_TYPE_CONTAINS_ERRORS:
	case XLSX_CF_TYPE_NOT_CONTAINS_ERRORS:
		/* The expressions stored for these are the full
		   expressions, not just what we search for. */
		op = GNM_STYLE_COND_CUSTOM;
		break;

	case XLSX_CF_TYPE_EXPRESSION:
		op = GNM_STYLE_COND_CUSTOM;
		break;

	default :
		xlsx_warning (xin, _("Ignoring unhandled conditional format of type '%s'"), type_str);
	}

	state->cond = gnm_style_cond_new (op, state->sheet);
	gnm_style_cond_set_overlay (state->cond, overlay);

	state->count = 0;
}

static void
xlsx_cond_fmt_rule_end (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	if (state->cond) {
		if (gnm_style_cond_is_valid (state->cond)) {
			if (NULL == state->conditions)
				state->conditions = gnm_style_conditions_new (state->sheet);
			/* Reverse the alternate-expression treatment.  */
			gnm_style_cond_canonicalize (state->cond);

			gnm_style_conditions_insert (state->conditions,
						     state->cond, -1);
		}
		gnm_style_cond_free (state->cond);
		state->cond = NULL;
	}
}

static void
xlsx_cond_fmt_formula_end (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	GnmParsePos pp;
	GnmExprTop const *texpr;
	GnmCellPos const *cp;

	if (!state->cond || state->count > 1 || state->cond_regions == NULL)
		return;

	cp = g_slist_last (state->cond_regions)->data;
	parse_pos_init (&pp, state->sheet->workbook, state->sheet, cp->col, cp->row);
	texpr = xlsx_parse_expr (xin, xin->content->str, &pp);
	if (texpr) {
		gnm_style_cond_set_expr (state->cond, texpr, state->count);
		gnm_expr_top_unref (texpr);
	}

	state->count++;
}

static void
xlsx_CT_SheetView_begin (GsfXMLIn *xin, xmlChar const **attrs)
{
	/* static EnumVal const view_types[] = { */
	/* 	{ "normal",		GNM_SHEET_VIEW_NORMAL_MODE }, */
	/* 	{ "pageBreakPreview",	GNM_SHEET_VIEW_PAGE_BREAK_MODE }, */
	/* 	{ "pageLayout",		GNM_SHEET_VIEW_LAYOUT_MODE }, */
	/* 	{ NULL, 0 } */
	/* }; */

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
	/* int tmp; */
	/* GnmSheetViewMode	view_mode = GNM_SHEET_VIEW_NORMAL_MODE; */
	GnmCellPos topLeft = { -1, -1 };

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2) {
		if (attr_pos (xin, attrs, "topLeftCell", &topLeft) ||
		    attr_bool (xin, attrs, "showGridLines", &showGridLines) ||
		    attr_bool (xin, attrs, "showFormulas", &showFormulas) ||
		    attr_bool (xin, attrs, "showRowColHeaders", &showRowColHeaders) ||
		    attr_bool (xin, attrs, "showZeros", &showZeros) ||
		    attr_bool (xin, attrs, "frozen", &frozen) ||
		    attr_bool (xin, attrs, "frozenSplit", &frozenSplit) ||
		    attr_bool (xin, attrs, "rightToLeft", &rightToLeft) ||
		    attr_bool (xin, attrs, "tabSelected", &tabSelected) ||
		    attr_bool (xin, attrs, "active", &active) ||
		    attr_bool (xin, attrs, "showRuler", &showRuler) ||
		    attr_bool (xin, attrs, "showOutlineSymbols", &showOutlineSymbols) ||
		    attr_bool (xin, attrs, "defaultGridColor", &defaultGridColor) ||
		    attr_bool (xin, attrs, "showWhiteSpace", &showWhiteSpace) ||
		    attr_int (xin, attrs, "zoomScale", &scale) ||
		    attr_int (xin, attrs, "colorId", &grid_color_index))
			;
		/* else if (attr_enum (xin, attrs, "view", view_types, &tmp)) */
		/* 	view_mode = tmp; */
#if 0
"zoomScaleNormal"		type="xs:unsignedInt" use="optional" default="0"
"zoomScaleSheetLayoutView"	type="xs:unsignedInt" use="optional" default="0"
"zoomScalePageLayoutView"	type="xs:unsignedInt" use="optional" default="0"
"workbookViewId"		type="xs:unsignedInt" use="required"
#endif
	}

	/* get this from the workbookViewId */
	g_return_if_fail (state->sv == NULL);
	state->sv = sheet_get_view (state->sheet, state->wb_view);
	state->pane_pos = XLSX_PANE_TOP_LEFT;

	/* until we import multiple views unfreeze just in case a previous view
	 * had frozen */
	gnm_sheet_view_freeze_panes (state->sv, NULL, NULL);

	if (topLeft.col >= 0)
		gnm_sheet_view_set_initial_top_left (state->sv, topLeft.col, topLeft.row);
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
			gnm_color_new_go (indexed_color (state, grid_color_index)));
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

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2) {
		if (0 == strcmp (attrs[0], "sqref"))
			refs = attrs[1];
		else if (attr_enum (xin, attrs, "activePane", pane_types, &i))
			pane_pos = i;
		else if (attr_pos (xin, attrs, "activeCell", &edit_pos))
			;
		else if (attr_int (xin, attrs, "activeCellId", &sel_with_edit_pos))
			;
	}

	if (pane_pos != state->pane_pos)
		return;

	for (i = 0 ; NULL != refs && *refs ; i++) {
		if (NULL == (refs = cellpos_parse (refs, gnm_sheet_get_size (state->sheet), &r.start, FALSE)))
			return;

		if (*refs == '\0' || *refs == ' ')
			r.end = r.start;
		else if (*refs != ':' ||
			 NULL == (refs = cellpos_parse (refs + 1, gnm_sheet_get_size (state->sheet), &r.end, FALSE)))
			return;

		if (i == 0)
			sv_selection_reset (state->sv);

		/* gnumeric assumes the edit_pos is in the last selected range.
		 * We need to re-order the selection list. */
		if (i <= sel_with_edit_pos && edit_pos.col >= 0)
			accum = g_slist_prepend (accum, gnm_range_dup (&r));
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
		gnm_sheet_view_set_edit_pos (state->sv, &edit_pos);
		g_slist_free (accum);
	}
}

static void
xlsx_CT_PivotSelection (G_GNUC_UNUSED GsfXMLIn *xin, G_GNUC_UNUSED xmlChar const **attrs)
{
#if 0
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
    <xsd:attribute name="pane" type="ST_Pane" use="optional" default="topLeft">
    <xsd:attribute name="showHeader" type="xsd:boolean" default="false">
    <xsd:attribute name="label" type="xsd:boolean" default="false">
    <xsd:attribute name="data" type="xsd:boolean" default="false">
    <xsd:attribute name="extendable" type="xsd:boolean" default="false">
    <xsd:attribute name="count" type="xsd:unsignedInt" default="0">
    <xsd:attribute name="axis" type="ST_Axis" use="optional">
    <xsd:attribute name="dimension" type="xsd:unsignedInt" default="0">
    <xsd:attribute name="start" type="xsd:unsignedInt" default="0">
    <xsd:attribute name="min" type="xsd:unsignedInt" default="0">
    <xsd:attribute name="max" type="xsd:unsignedInt" default="0">
    <xsd:attribute name="activeRow" type="xsd:unsignedInt" default="0">
    <xsd:attribute name="activeCol" type="xsd:unsignedInt" default="0">
    <xsd:attribute name="previousRow" type="xsd:unsignedInt" default="0">
    <xsd:attribute name="previousCol" type="xsd:unsignedInt" default="0">
    <xsd:attribute name="click" type="xsd:unsignedInt" default="0">
    <xsd:attribute ref="r:id" use="optional">
#endif
}

static void
xlsx_CT_PivotArea (G_GNUC_UNUSED GsfXMLIn *xin, G_GNUC_UNUSED xmlChar const **attrs)
{
#if 0
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
    <xsd:attribute name="field" use="optional" type="xsd:int">
    <xsd:attribute name="type" type="ST_PivotAreaType" default="normal">
    <xsd:attribute name="dataOnly" type="xsd:boolean" default="true">
    <xsd:attribute name="labelOnly" type="xsd:boolean" default="false">
    <xsd:attribute name="grandRow" type="xsd:boolean" default="false">
    <xsd:attribute name="grandCol" type="xsd:boolean" default="false">
    <xsd:attribute name="cacheIndex" type="xsd:boolean" default="false">
    <xsd:attribute name="outline" type="xsd:boolean" default="true">
    <xsd:attribute name="offset" type="ST_Ref">
    <xsd:attribute name="collapsedLevelsAreSubtotals" type="xsd:boolean" default="false">
    <xsd:attribute name="axis" type="ST_Axis" use="optional">
    <xsd:attribute name="fieldPosition" type="xsd:unsignedInt" use="optional">
#endif
}
static void
xlsx_CT_PivotAreaReferences (G_GNUC_UNUSED GsfXMLIn *xin, G_GNUC_UNUSED xmlChar const **attrs)
{
#if 0
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
#endif
}
static void
xlsx_CT_PivotAreaReference (G_GNUC_UNUSED GsfXMLIn *xin, G_GNUC_UNUSED xmlChar const **attrs)
{
#if 0
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
    <xsd:attribute name="field" use="optional" type="xsd:unsignedInt">
    <xsd:attribute name="count" type="xsd:unsignedInt">
    <xsd:attribute name="selected" type="xsd:boolean" default="true">
    <xsd:attribute name="byPosition" type="xsd:boolean" default="false">
    <xsd:attribute name="relative" type="xsd:boolean" default="false">
    <xsd:attribute name="defaultSubtotal" type="xsd:boolean" default="false">
    <xsd:attribute name="sumSubtotal" type="xsd:boolean" default="false">
    <xsd:attribute name="countASubtotal" type="xsd:boolean" default="false">
    <xsd:attribute name="avgSubtotal" type="xsd:boolean" default="false">
    <xsd:attribute name="maxSubtotal" type="xsd:boolean" default="false">
    <xsd:attribute name="minSubtotal" type="xsd:boolean" default="false">
    <xsd:attribute name="productSubtotal" type="xsd:boolean" default="false">
    <xsd:attribute name="countSubtotal" type="xsd:boolean" default="false">
    <xsd:attribute name="stdDevSubtotal" type="xsd:boolean" default="false">
    <xsd:attribute name="stdDevPSubtotal" type="xsd:boolean" default="false">
    <xsd:attribute name="varSubtotal" type="xsd:boolean" default="false">
    <xsd:attribute name="varPSubtotal" type="xsd:boolean" default="false">
#endif
}

static void
xlsx_CT_Pane (GsfXMLIn *xin, xmlChar const **attrs)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	GnmCellPos topLeft = { 0, 0 };
	int tmp;
	double xSplit = -1., ySplit = -1.;
	gboolean frozen = FALSE;

	g_return_if_fail (state->sv != NULL);

	/* <pane xSplit="2" ySplit="3" topLeftCell="J15" activePane="bottomRight" state="frozen"/> */
	state->pane_pos = XLSX_PANE_TOP_LEFT;
	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2) {
		if (0 == strcmp (attrs[0], "state"))
			frozen = (0 == strcmp (attrs[1], "frozen"));
		else if (attr_pos (xin, attrs, "topLeftCell", &topLeft))
			;
		else if (attr_double (xin, attrs, "xSplit", &xSplit))
			;
		else if (attr_double (xin, attrs, "ySplit", &ySplit))
			;
		else if (attr_enum (xin, attrs, "pane", pane_types, &tmp))
			state->pane_pos = tmp;
	}

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
		gnm_sheet_view_freeze_panes (state->sv, &frozen, &unfrozen);
		gnm_sheet_view_set_initial_top_left (state->sv, topLeft.col, topLeft.row);
	}
}

static void
xlsx_ole_object (G_GNUC_UNUSED GsfXMLIn *xin, G_GNUC_UNUSED xmlChar const **attrs)
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
	GnmHLink *lnk = NULL;
	char const *location = NULL;
	char const *tooltip = NULL;
	char const *extern_id = NULL;
	char *target = NULL;

	/* <hyperlink ref="A42" r:id="rId1"/> */
	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2) {
		if (attr_range (xin, attrs, "ref", &r))
			has_ref = TRUE;
		else if (0 == strcmp (attrs[0], "location"))
			location = CXML2C (attrs[1]);
		else if (0 == strcmp (attrs[0], "tooltip"))
			tooltip = CXML2C (attrs[1]);
		else if (gsf_xml_in_namecmp (xin, attrs[0], XL_NS_DOC_REL, "id"))
			extern_id = CXML2C (attrs[1]);
#if 0 /* ignore "display" on import, it always seems to be the cell content */
		else if (0 == strcmp (attrs[0], "display"))
			;
#endif
	}

	if (!has_ref)
		return;

	if (NULL != extern_id) {
		GsfOpenPkgRel const *rel = gsf_open_pkg_lookup_rel_by_id (
			gsf_xml_in_get_input (xin), extern_id);
		if (NULL != rel &&
		    gsf_open_pkg_rel_is_extern (rel) &&
		    0 == strcmp (gsf_open_pkg_rel_get_type (rel),
				 "http://schemas.openxmlformats.org/officeDocument/2006/relationships/hyperlink")) {
			const char *url = gsf_open_pkg_rel_get_target (rel);
			if (url) {
				if (!g_ascii_strncasecmp (url, "mailto:", 7))
					link_type = gnm_hlink_email_get_type ();
				else if (!g_ascii_strncasecmp (url, "http:", 5) ||
					 !g_ascii_strncasecmp (url, "https:", 6))
					link_type = gnm_hlink_url_get_type ();
				else
					link_type = gnm_hlink_external_get_type ();
				target = location
					? g_strconcat (url, "#", location, NULL)
					: g_strdup (url);
			}
		}
	} else if (location) {
		target = g_strdup (location);
		link_type = gnm_hlink_cur_wb_get_type ();
	}

	if (0 == link_type) {
		xlsx_warning (xin, _("Unknown type of hyperlink"));
		g_free (target);
		return;
	}

	lnk = gnm_hlink_new (link_type, state->sheet);
	gnm_hlink_set_target (lnk, target);
	gnm_hlink_set_tip (lnk, tooltip);
	style = gnm_style_new ();
	gnm_style_set_hlink (style, lnk);
	sheet_style_apply_range	(state->sheet, &r, style);
	g_free (target);
}

static void
cb_find_pivots (GsfInput *opkg, GsfOpenPkgRel const *rel, gpointer    user_data)
{
	XLSXReadState *state = user_data;
	GsfInput *part_stream;
	char const *t = gsf_open_pkg_rel_get_type (rel);

	if (NULL != t &&
	    0 == strcmp (t, "http://schemas.openxmlformats.org/officeDocument/2006/relationships/pivotTable") &&
	    NULL != (part_stream = gsf_open_pkg_open_rel (opkg, rel, NULL)))
		xlsx_parse_stream (state, part_stream, xlsx_pivot_table_dtd);
}

static void
xlsx_CT_worksheet (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;

	gsf_open_pkg_foreach_rel (gsf_xml_in_get_input (xin),
				  &cb_find_pivots, state);
	// Reset implied position
	state->pos.col = 0;
	state->pos.row = 0;
}


static void
xlsx_ext_begin (GsfXMLIn *xin, xmlChar const **attrs)
{
	gboolean warned = FALSE;
	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2) {
		if (0 == strcmp (attrs[0], "uri")) {
#if 0
			xlsx_warning (xin,
				      _("Encountered uninterpretable \"ext\" extension in namespace \"%s\""),
				      attrs[1]);
#endif
			warned = TRUE;
		}
	}

	if (!warned)
		xlsx_warning (xin,
			      _("Encountered uninterpretable \"ext\" extension with missing namespace"));
	if (!gnm_debug_flag ("xlsxext"))
		gsf_xml_in_set_silent_unknowns (xin, TRUE);
}

static void
xlsx_ext_end (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	gsf_xml_in_set_silent_unknowns (xin, FALSE);
}



static void
add_attr (XLSXReadState *state, PangoAttribute *attr)
{
	attr->start_index = 0;
	attr->end_index = -1;

	if (state->run_attrs == NULL)
		state->run_attrs = pango_attr_list_new ();

	pango_attr_list_insert (state->run_attrs, attr);
}

static void
xlsx_run_weight (GsfXMLIn *xin, xmlChar const **attrs)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	PangoWeight wt;
	int b = TRUE;
	(void)simple_bool (xin, attrs, &b);
	wt = b ? PANGO_WEIGHT_BOLD : PANGO_WEIGHT_NORMAL;
	add_attr (state, pango_attr_weight_new (wt));
}

static void
xlsx_run_style (GsfXMLIn *xin, xmlChar const **attrs)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	PangoStyle st;
	int it = TRUE;
	(void)simple_bool (xin, attrs, &it);

	st = it ? PANGO_STYLE_ITALIC : PANGO_STYLE_NORMAL;
	add_attr (state, pango_attr_style_new (st));
}

static void
xlsx_run_family (GsfXMLIn *xin, xmlChar const **attrs)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	const char *fam = simple_string (xin, attrs);
	if (fam) {
		PangoAttribute *attr = pango_attr_family_new (fam);
		add_attr (state, attr);
	}
}

static void
xlsx_run_size (GsfXMLIn *xin, xmlChar const **attrs)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	double sz;
	if (simple_double (xin, attrs, &sz)) {
		PangoAttribute *attr = pango_attr_size_new (CLAMP (sz, 0.0, 1000.0) * PANGO_SCALE);
		add_attr (state, attr);
	}
}

static void
xlsx_run_strikethrough (GsfXMLIn *xin, xmlChar const **attrs)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	int st = TRUE;  /* Default */
	(void)simple_bool (xin, attrs, &st);
	add_attr (state, pango_attr_strikethrough_new (st));
}

static void
xlsx_run_underline (GsfXMLIn *xin, xmlChar const **attrs)
{
	static EnumVal const types[] = {
		{ "single", PANGO_UNDERLINE_SINGLE },
		{ "double", PANGO_UNDERLINE_DOUBLE },
		{ "singleAccounting", PANGO_UNDERLINE_LOW },
		{ "doubleAccounting", PANGO_UNDERLINE_LOW },  /* fixme? */
		{ "none", PANGO_UNDERLINE_NONE },
		{ NULL, 0 }
	};
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	int u = PANGO_UNDERLINE_SINGLE;
	(void)simple_enum (xin, attrs, types, &u);
	add_attr (state, pango_attr_underline_new (u));
}

static void
xlsx_run_vertalign (GsfXMLIn *xin, xmlChar const **attrs)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	static EnumVal const types[] = {
		{ "subscript", GO_FONT_SCRIPT_SUB },
		{ "baseline", GO_FONT_SCRIPT_STANDARD },
		{ "superscript", GO_FONT_SCRIPT_SUPER },
		{ NULL, 0 }
	};
	int v = GO_FONT_SCRIPT_STANDARD;
	(void)simple_enum (xin, attrs, types, &v);
	switch (v) {
	case GO_FONT_SCRIPT_SUB:
		add_attr (state, go_pango_attr_subscript_new (TRUE));
		break;
	case GO_FONT_SCRIPT_SUPER:
		add_attr (state, go_pango_attr_superscript_new (TRUE));
		break;
	default:
		break;
	}
}


static void
xlsx_run_color (GsfXMLIn *xin, xmlChar const **attrs)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	GOColor c = GO_COLOR_BLACK;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2) {
		if (strcmp (attrs[0], "rgb") == 0) {
			unsigned r, g, b, a;
			if (4 != sscanf (attrs[1], "%02x%02x%02x%02x", &a, &r, &g, &b)) {
				xlsx_warning (xin,
					      _("Invalid color '%s' for attribute rgb"),
					      attrs[1]);
				continue;
			}

			c = GO_COLOR_FROM_RGBA (r, g, b, a);
		} else if (strcmp (attrs[0], "indexed") == 0) {
			int idx = atoi (CXML2C (attrs[1]));
			c = indexed_color (state, idx);
		}
	}

	add_attr (state, go_color_to_pango (c, TRUE));
}

static gboolean
cb_trunc_attributes (PangoAttribute *a, gpointer plen)
{
	a->end_index = GPOINTER_TO_UINT (plen);
	return FALSE;
}

static void
xlsx_rich_text (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	const char *s = xin->content->str;

	if (state->run_attrs) {
		unsigned len = strlen (s);
		unsigned start = state->r_text->len, end = start + len;
		pango_attr_list_filter (state->run_attrs,
					(PangoAttrFilterFunc) cb_trunc_attributes,
					GUINT_TO_POINTER (len));
		if (state->rich_attrs == NULL)
			state->rich_attrs = pango_attr_list_new ();
		pango_attr_list_splice (state->rich_attrs, state->run_attrs, start, end);
		pango_attr_list_unref (state->run_attrs);
		state->run_attrs = NULL;
	}
	g_string_append (state->r_text, s);
}

/*
 * Rich text is used in multiple dtds.  We don't hanadle that with
 * particular elegance, but we've got macros.
 */
#define RICH_TEXT_NODES							\
	  GSF_XML_IN_NODE (RICH, RICH_TEXT, XL_NS_SS, "t", GSF_XML_CONTENT, NULL, xlsx_rich_text), \
	  GSF_XML_IN_NODE (RICH, RICH_PROPS, XL_NS_SS, "rPr", GSF_XML_NO_CONTENT, NULL, NULL), \
	    /* GSF_XML_IN_NODE (RICH_PROPS, RICH_FONT, XL_NS_SS, "font", GSF_XML_NO_CONTENT, NULL, NULL), */ \
	    /* docs say 'font' xl is generating rFont */		\
	    GSF_XML_IN_NODE (RICH_PROPS, RICH_FONT, XL_NS_SS, "rFont", GSF_XML_NO_CONTENT, NULL, NULL),	\
									\
	    GSF_XML_IN_NODE (RICH_PROPS, RICH_CHARSET, XL_NS_SS, "charset", GSF_XML_NO_CONTENT, NULL, NULL), \
	    GSF_XML_IN_NODE (RICH_PROPS, RICH_FAMILY, XL_NS_SS, "family", GSF_XML_NO_CONTENT, xlsx_run_family, NULL), \
	    GSF_XML_IN_NODE (RICH_PROPS, RICH_BOLD, XL_NS_SS, "b", GSF_XML_NO_CONTENT, xlsx_run_weight, NULL), \
	    GSF_XML_IN_NODE (RICH_PROPS, RICH_ITALIC, XL_NS_SS, "i", GSF_XML_NO_CONTENT, xlsx_run_style, NULL),	\
	    GSF_XML_IN_NODE (RICH_PROPS, RICH_STRIKE, XL_NS_SS, "strike", GSF_XML_NO_CONTENT, xlsx_run_strikethrough, NULL), \
	    GSF_XML_IN_NODE (RICH_PROPS, RICH_OUTLINE, XL_NS_SS, "outline", GSF_XML_NO_CONTENT, NULL, NULL), \
	    GSF_XML_IN_NODE (RICH_PROPS, RICH_SHADOW, XL_NS_SS, "shadow", GSF_XML_NO_CONTENT, NULL, NULL), \
	    GSF_XML_IN_NODE (RICH_PROPS, RICH_CONDENSE, XL_NS_SS, "condense", GSF_XML_NO_CONTENT, NULL, NULL), \
	    GSF_XML_IN_NODE (RICH_PROPS, RICH_EXTEND, XL_NS_SS, "extend", GSF_XML_NO_CONTENT, NULL, NULL), \
	    GSF_XML_IN_NODE (RICH_PROPS, RICH_COLOR, XL_NS_SS, "color", GSF_XML_NO_CONTENT, xlsx_run_color, NULL), \
	    GSF_XML_IN_NODE (RICH_PROPS, RICH_SZ, XL_NS_SS, "sz", GSF_XML_NO_CONTENT, xlsx_run_size, NULL), \
	    GSF_XML_IN_NODE (RICH_PROPS, RICH_ULINE, XL_NS_SS, "u", GSF_XML_NO_CONTENT, xlsx_run_underline, NULL), \
	    GSF_XML_IN_NODE (RICH_PROPS, RICH_VALIGN, XL_NS_SS, "vertAlign", GSF_XML_NO_CONTENT, xlsx_run_vertalign, NULL), \
	    GSF_XML_IN_NODE (RICH_PROPS, RICH_SCHEME, XL_NS_SS, "scheme", GSF_XML_NO_CONTENT, NULL, NULL)




static GsfXMLInNode const xlsx_sheet_dtd[] = {
GSF_XML_IN_NODE_FULL (START, START, -1, NULL, GSF_XML_NO_CONTENT, FALSE, TRUE, NULL, NULL, 0),
GSF_XML_IN_NODE_FULL (START, SHEET, XL_NS_SS, "worksheet", GSF_XML_NO_CONTENT, FALSE, TRUE, NULL, &xlsx_CT_worksheet, 0),
  GSF_XML_IN_NODE_FULL (SHEET, EXTLST, XL_NS_SS, "extLst", GSF_XML_NO_CONTENT, FALSE, TRUE, NULL, NULL, 0),
    GSF_XML_IN_NODE_FULL (EXTLST, EXTITEM, XL_NS_SS, "ext", GSF_XML_NO_CONTENT, FALSE, TRUE, &xlsx_ext_begin, &xlsx_ext_end, 0),
      GSF_XML_IN_NODE (EXTITEM, EXT_TABTEXTCOLOR, XL_NS_GNM_EXT, "tabTextColor", GSF_XML_NO_CONTENT, &xlsx_ext_tabtextcolor, NULL),
      GSF_XML_IN_NODE_FULL (EXTITEM, EXT_DataValidations, XL_NS_SS, "dataValidations", GSF_XML_NO_CONTENT, FALSE, TRUE, NULL, NULL, 0),
	GSF_XML_IN_NODE (EXT_DataValidations, EXT_DataValidation, XL_NS_SS, "dataValidation", GSF_XML_NO_CONTENT,
			 &xlsx_CT_DataValidation_begin, &xlsx_CT_DataValidation_end),
	  GSF_XML_IN_NODE_FULL (EXT_DataValidation, DATAVAL_FORMULA1, XL_NS_SS, "formula1", GSF_XML_NO_CONTENT, FALSE, FALSE, NULL, NULL, 0),
            GSF_XML_IN_NODE_FULL (DATAVAL_FORMULA1, DATAVAL_F1, XL_NS_MSSS, "f", GSF_XML_CONTENT, FALSE, FALSE, NULL, &xlsx_validation_expr, 0),
	  GSF_XML_IN_NODE_FULL (EXT_DataValidation, DATAVAL_FORMULA2, XL_NS_SS, "formula2", GSF_XML_NO_CONTENT, FALSE, FALSE, NULL, NULL, 1),
            GSF_XML_IN_NODE_FULL (DATAVAL_FORMULA2, DATAVAL_F2, XL_NS_MSSS, "f", GSF_XML_CONTENT, FALSE, FALSE, NULL, &xlsx_validation_expr, 1),
	  GSF_XML_IN_NODE (EXT_DataValidation, DATAVAL_SQREF, XL_NS_MSSS, "sqref", GSF_XML_CONTENT, NULL, xlsx_CT_DataValidation_sqref_end),
  GSF_XML_IN_NODE (SHEET, PROPS, XL_NS_SS, "sheetPr", GSF_XML_NO_CONTENT, &xlsx_CT_SheetPr, NULL),
    GSF_XML_IN_NODE (PROPS, OUTLINE_PROPS, XL_NS_SS, "outlinePr", GSF_XML_NO_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE (PROPS, TAB_COLOR, XL_NS_SS, "tabColor", GSF_XML_NO_CONTENT, &xlsx_sheet_tabcolor, NULL),
    GSF_XML_IN_NODE (PROPS, PAGE_SETUP, XL_NS_SS, "pageSetUpPr", GSF_XML_NO_CONTENT, &xlsx_sheet_page_setup, NULL),
  GSF_XML_IN_NODE (SHEET, DIMENSION, XL_NS_SS, "dimension", GSF_XML_NO_CONTENT, NULL, NULL),
  GSF_XML_IN_NODE (SHEET, VIEWS, XL_NS_SS, "sheetViews", GSF_XML_NO_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE (VIEWS, VIEW, XL_NS_SS, "sheetView",  GSF_XML_NO_CONTENT, &xlsx_CT_SheetView_begin, &xlsx_CT_SheetView_end),
      GSF_XML_IN_NODE (VIEW, PANE, XL_NS_SS, "pane",  GSF_XML_NO_CONTENT, &xlsx_CT_Pane, NULL),
      GSF_XML_IN_NODE (VIEW, SELECTION, XL_NS_SS, "selection",  GSF_XML_NO_CONTENT, &xlsx_CT_Selection, NULL),
      GSF_XML_IN_NODE (VIEW, PIV_SELECTION, XL_NS_SS, "pivotSelection",  GSF_XML_NO_CONTENT, &xlsx_CT_PivotSelection, NULL),
        GSF_XML_IN_NODE (PIV_SELECTION, PIV_AREA, XL_NS_SS, "pivotArea",  GSF_XML_NO_CONTENT, &xlsx_CT_PivotArea, NULL),
          GSF_XML_IN_NODE (PIV_AREA, PIV_AREA_REFS, XL_NS_SS, "references",  GSF_XML_NO_CONTENT, &xlsx_CT_PivotAreaReferences, NULL),
            GSF_XML_IN_NODE (PIV_AREA_REFS, PIV_AREA_REF, XL_NS_SS, "reference",  GSF_XML_NO_CONTENT, &xlsx_CT_PivotAreaReference, NULL),

  GSF_XML_IN_NODE (SHEET, DEFAULT_FMT, XL_NS_SS, "sheetFormatPr", GSF_XML_NO_CONTENT, &xlsx_CT_SheetFormatPr, NULL),

  GSF_XML_IN_NODE (SHEET, COLS,	XL_NS_SS, "cols", GSF_XML_NO_CONTENT, NULL, xlsx_CT_RowsCols_end),
    GSF_XML_IN_NODE (COLS, COL,	XL_NS_SS, "col", GSF_XML_NO_CONTENT, &xlsx_CT_Col, NULL),

  GSF_XML_IN_NODE (SHEET, CONTENT, XL_NS_SS, "sheetData", GSF_XML_NO_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE (CONTENT, ROW, XL_NS_SS, "row", GSF_XML_NO_CONTENT, &xlsx_CT_Row, &xlsx_CT_Row_end),
      GSF_XML_IN_NODE (ROW, CELL, XL_NS_SS, "c", GSF_XML_NO_CONTENT, &xlsx_cell_begin, &xlsx_cell_end),
	GSF_XML_IN_NODE (CELL, VALUE, XL_NS_SS, "v", GSF_XML_CONTENT, NULL, &xlsx_cell_val_end),
	GSF_XML_IN_NODE (CELL, FMLA, XL_NS_SS,  "f", GSF_XML_CONTENT, &xlsx_cell_expr_begin, &xlsx_cell_expr_end),
        GSF_XML_IN_NODE (CELL, TEXTINLINE, XL_NS_SS,  "is", GSF_XML_NO_CONTENT, &xlsx_cell_inline_begin, &xlsx_cell_inline_end),
	  GSF_XML_IN_NODE (TEXTINLINE, TEXTRUN, XL_NS_SS,  "t", GSF_XML_CONTENT, NULL, &xlsx_cell_inline_text_end),
        GSF_XML_IN_NODE (TEXTINLINE, RICH, XL_NS_SS, "r", GSF_XML_NO_CONTENT, NULL, NULL),
	RICH_TEXT_NODES,
	GSF_XML_IN_NODE (CELL, EXTLST, XL_NS_SS, "extLst", GSF_XML_2ND, NULL, NULL),

  GSF_XML_IN_NODE (SHEET, CALC_PR, XL_NS_SS, "sheetCalcPr", GSF_XML_NO_CONTENT, NULL, NULL),
  GSF_XML_IN_NODE (SHEET, CT_SortState, XL_NS_SS, "sortState", GSF_XML_NO_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE (CT_SortState, CT_SortCondition, XL_NS_SS, "sortCondition", GSF_XML_NO_CONTENT, NULL, NULL),
  GSF_XML_IN_NODE (SHEET, SCENARIOS, XL_NS_SS, "scenarios", GSF_XML_NO_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE (SCENARIOS, INPUT_CELLS, XL_NS_SS, "inputCells", GSF_XML_NO_CONTENT, NULL, NULL),
  GSF_XML_IN_NODE (SHEET, PROTECTED_RANGES, XL_NS_SS, "protectedRanges", GSF_XML_NO_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE (PROTECTED_RANGES, PROTECTED_RANGE, XL_NS_SS, "protectedRange", GSF_XML_NO_CONTENT, NULL, NULL),

  GSF_XML_IN_NODE (SHEET, CT_AutoFilter, XL_NS_SS, "autoFilter", GSF_XML_NO_CONTENT,
		   &xlsx_CT_AutoFilter_begin, &xlsx_CT_AutoFilter_end),
    GSF_XML_IN_NODE (CT_AutoFilter, EXTLST, XL_NS_SS, "extLst", GSF_XML_2ND, NULL, NULL),
    GSF_XML_IN_NODE (CT_AutoFilter, CT_SortState, XL_NS_SS, "sortState", GSF_XML_2ND, NULL, NULL),
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
    GSF_XML_IN_NODE (COND_FMTS, EXTLST, XL_NS_SS, "extLst", GSF_XML_2ND, NULL, NULL),
    GSF_XML_IN_NODE (COND_FMTS, COND_RULE, XL_NS_SS, "cfRule", GSF_XML_NO_CONTENT,
		   &xlsx_cond_fmt_rule_begin, &xlsx_cond_fmt_rule_end),
      GSF_XML_IN_NODE (COND_RULE, COND_FMLA, XL_NS_SS, "formula", GSF_XML_CONTENT, NULL, &xlsx_cond_fmt_formula_end),
      GSF_XML_IN_NODE (COND_RULE, COND_COLOR_SCALE, XL_NS_SS, "colorScale", GSF_XML_NO_CONTENT, NULL, NULL),
        GSF_XML_IN_NODE (COND_COLOR_SCALE, CFVO, XL_NS_SS, "cfvo", GSF_XML_NO_CONTENT, NULL, NULL),
        GSF_XML_IN_NODE (COND_COLOR_SCALE, COND_COLOR, XL_NS_SS, "color", GSF_XML_NO_CONTENT, NULL, NULL),
      GSF_XML_IN_NODE (COND_RULE, COND_DATA_BAR, XL_NS_SS, "dataBar", GSF_XML_NO_CONTENT, NULL, NULL),
      GSF_XML_IN_NODE (COND_RULE, COND_ICON_SET, XL_NS_SS, "iconSet", GSF_XML_NO_CONTENT, NULL, NULL),
        GSF_XML_IN_NODE (COND_ICON_SET, CFVO, XL_NS_SS, "cfvo", GSF_XML_2ND, NULL, NULL),

  GSF_XML_IN_NODE (SHEET, HYPERLINKS, XL_NS_SS, "hyperlinks", GSF_XML_NO_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE (HYPERLINKS, HYPERLINK, XL_NS_SS, "hyperlink", GSF_XML_NO_CONTENT, &xlsx_CT_HyperLinks, NULL),

  GSF_XML_IN_NODE (SHEET, PRINT_OPTS, XL_NS_SS, "printOptions", GSF_XML_NO_CONTENT, NULL, NULL),
  GSF_XML_IN_NODE (SHEET, PRINT_MARGINS, XL_NS_SS, "pageMargins", GSF_XML_NO_CONTENT, &xlsx_CT_PageMargins, NULL),
  GSF_XML_IN_NODE (SHEET, PRINT_SETUP, XL_NS_SS, "pageSetup", GSF_XML_NO_CONTENT, &xlsx_CT_PageSetup, NULL),
  GSF_XML_IN_NODE (SHEET, PRINT_HEADER_FOOTER, XL_NS_SS, "headerFooter", GSF_XML_NO_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE (PRINT_HEADER_FOOTER, ODD_HEADER, XL_NS_SS, "oddHeader", GSF_XML_CONTENT, NULL, &xlsx_CT_oddheader_end),
    GSF_XML_IN_NODE (PRINT_HEADER_FOOTER, ODD_FOOTER, XL_NS_SS, "oddFooter", GSF_XML_CONTENT, NULL, &xlsx_CT_oddfooter_end),

  GSF_XML_IN_NODE_FULL (SHEET, ROW_BREAKS, XL_NS_SS, "rowBreaks", GSF_XML_NO_CONTENT,
			FALSE, FALSE, &xlsx_CT_PageBreaks_begin, &xlsx_CT_PageBreaks_end, 1),
    GSF_XML_IN_NODE (ROW_BREAKS, CT_PageBreak, XL_NS_SS, "brk", GSF_XML_NO_CONTENT, &xlsx_CT_PageBreak, NULL),
  GSF_XML_IN_NODE_FULL (SHEET, COL_BREAKS, XL_NS_SS, "colBreaks", GSF_XML_NO_CONTENT,
			FALSE, FALSE, &xlsx_CT_PageBreaks_begin, &xlsx_CT_PageBreaks_end, 0),
    GSF_XML_IN_NODE (COL_BREAKS, CT_PageBreak, XL_NS_SS, "brk", GSF_XML_2ND, NULL, NULL),

  GSF_XML_IN_NODE (SHEET, LEGACY_DRAW, XL_NS_SS, "legacyDrawing", GSF_XML_NO_CONTENT, &xlsx_sheet_legacy_drawing, NULL),
  GSF_XML_IN_NODE (SHEET, OLE_OBJECTS, XL_NS_SS, "oleObjects", GSF_XML_NO_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE (OLE_OBJECTS, OLE_OBJECT, XL_NS_SS, "oleObject", GSF_XML_NO_CONTENT, &xlsx_ole_object, NULL),

GSF_XML_IN_NODE_END
};

/****************************************************************************/

static void
xlsx_CT_PivotCache (GsfXMLIn *xin, xmlChar const **attrs)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	xmlChar const *id = NULL;
	xmlChar const *cacheId = NULL;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2) {
		if (gsf_xml_in_namecmp (xin, attrs[0], XL_NS_DOC_REL, "id"))
			id = attrs[1];
		else if (0 == strcmp (attrs[0], "cacheId"))
			cacheId = attrs[1];
	}

	if (NULL != id && NULL != cacheId) {
		g_return_if_fail (NULL == state->pivot.cache);

		xlsx_parse_rel_by_id (xin, id,
			xlsx_pivot_cache_def_dtd, xlsx_ns);

		g_return_if_fail (NULL != state->pivot.cache);

		/* absorb the reference to the cache */
		g_hash_table_replace (state->pivot.cache_by_id,
			g_strdup (cacheId), state->pivot.cache);
		state->pivot.cache = NULL;
	}
}

static void
xlsx_CT_WorkbookPr (GsfXMLIn *xin, xmlChar const **attrs)
{
	static EnumVal const switchModes[] = {
		{ "on",	 TRUE },
		{ "1",	 TRUE },
		{ "true", TRUE },
		{ "off", FALSE },
		{ "0", FALSE },
		{ "false", FALSE },
		{ NULL, 0 }
	};
	int tmp;
	XLSXReadState *state = (XLSXReadState *)xin->user_state;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2) {
		if (attr_enum (xin, attrs, "date1904", switchModes, &tmp))
			workbook_set_1904 (state->wb, tmp);
	}
}

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

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2) {
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
}

static void
xlsx_CT_workbookView (GsfXMLIn *xin, xmlChar const **attrs)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	int active_tab = -1;
	int width = -1, height = -1;
	const int scale = 10;  /* Guess */

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2) {
		if (attr_int (xin, attrs, "activeTab", &active_tab))
			;
		else if (attr_int (xin, attrs, "windowHeight", &height))
			;
		else if (attr_int (xin, attrs, "windowWidth", &width))
			;
	}

	if (width > scale / 2 && height > scale / 2)
		wb_view_preferred_size (state->wb_view,
					(width + scale / 2) / scale,
					(height + scale / 2) / scale);
}

static void
xlsx_sheet_begin (GsfXMLIn *xin, xmlChar const **attrs)
{
	static EnumVal const visibilities[] = {
		{ "visible",	GNM_SHEET_VISIBILITY_VISIBLE },
		{ "hidden",	GNM_SHEET_VISIBILITY_HIDDEN },
		{ "veryHidden",	GNM_SHEET_VISIBILITY_VERY_HIDDEN },
		{ NULL, 0 }
	};
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	char const *name = NULL;
	char const *part_id = NULL;
	Sheet *sheet;
	int viz = (int)GNM_SHEET_VISIBILITY_VISIBLE;

	maybe_update_progress (xin);

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2) {
		if (0 == strcmp (attrs[0], "name"))
			name = attrs[1];
		else if (attr_enum (xin, attrs, "state", visibilities, &viz))
			; /* Nothing */
		else if (gsf_xml_in_namecmp (xin, attrs[0], XL_NS_DOC_REL, "id"))
			part_id = attrs[1];
	}

	if (NULL == name) {
		xlsx_warning (xin, _("Ignoring a sheet without a name"));
		return;
	}

	sheet =  workbook_sheet_by_name (state->wb, name);
	if (NULL == sheet) {
		sheet = wrap_sheet_new (state->wb, name, XLSX_MaxCol, XLSX_MaxRow);
		workbook_sheet_attach (state->wb, sheet);
	}
	g_object_set (sheet, "visibility", viz, NULL);

	g_object_set_data_full (G_OBJECT (sheet), "_XLSX_RelID", g_strdup (part_id),
		(GDestroyNotify) g_free);
}

static void
xlsx_wb_name_begin (GsfXMLIn *xin, xmlChar const **attrs)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	const char *name = NULL;
	int sheet_idx = -1;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2) {
		if (0 == strcmp (attrs[0], "name"))
			name = attrs[1];
		else if (attr_int (xin, attrs, "localSheetId", &sheet_idx))
			; /* Nothing */
	}

	state->defined_name = g_strdup (name);
	state->defined_name_sheet =
		sheet_idx >= 0
		? workbook_sheet_by_index (state->wb, sheet_idx)
		: NULL;
}

static void
xlsx_wb_name_end (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	GnmParsePos pp;
	Sheet *sheet = state->defined_name_sheet;
	GnmNamedExpr *nexpr;
	char *error_msg = NULL;
	const char *thename = state->defined_name;
	const char *thevalue = xin->content->str;
	gboolean bogus = FALSE;

	g_return_if_fail (thename != NULL);

	parse_pos_init (&pp, state->wb, sheet, 0, 0);

	if (g_str_has_prefix (thename, "_xlnm.")) {
		gboolean editable;

		thename += 6;
		editable = g_str_equal (thename, "Sheet_Title");
		bogus = g_str_equal (thename, "Print_Area") &&
			g_str_equal (thevalue, "!#REF!");
		nexpr = bogus
			? NULL
			: expr_name_add (&pp, thename,
					 gnm_expr_top_new_constant (value_new_empty ()),
					 &error_msg, TRUE, NULL);
		if (nexpr) {
			nexpr->is_permanent = TRUE;
			nexpr->is_editable = editable;
		}
	} else
		nexpr = expr_name_add (&pp, thename,
				       gnm_expr_top_new_constant (value_new_empty ()),
				       &error_msg, TRUE, NULL);

	if (bogus) {
		/* Silently ignore */
	} else if (nexpr) {
		state->delayed_names =
			g_list_prepend (state->delayed_names, sheet);
		state->delayed_names =
			g_list_prepend (state->delayed_names,
					g_strdup (thevalue));
		state->delayed_names =
			g_list_prepend (state->delayed_names, nexpr);
	} else {
		xlsx_warning (xin, _("Failed to define name: %s"), error_msg);
		g_free (error_msg);
	}

	g_free (state->defined_name);
	state->defined_name = NULL;
}

static void
handle_delayed_names (GsfXMLIn *xin)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	GList *l;

	for (l = state->delayed_names; l; l = l->next->next->next) {
		GnmNamedExpr *nexpr = l->data;
		char *expr_str = l->next->data;
		Sheet *sheet = l->next->next->data;
		GnmExprTop const *texpr;
		GnmParsePos pp;

		parse_pos_init (&pp, state->wb, sheet, 0, 0);
		if (*expr_str == 0)
			texpr = gnm_expr_top_new_constant (value_new_error_REF (NULL));
		else
			texpr = xlsx_parse_expr (xin, expr_str, &pp);
		if (texpr) {
			expr_name_set_expr (nexpr, texpr);
		}
		g_free (expr_str);
	}

	g_list_free (state->delayed_names);
	state->delayed_names = NULL;
}

static void
xlsx_wb_names_end (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	handle_delayed_names (xin);
}


/**************************************************************************************************/

static void
xlsx_read_external_book (GsfXMLIn *xin, G_GNUC_UNUSED xmlChar const **attrs)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	GsfOpenPkgRel const *rel = gsf_open_pkg_lookup_rel_by_type
		(gsf_xml_in_get_input (xin),
		 "http://schemas.openxmlformats.org/officeDocument/2006/relationships/"
		 "externalLink");
	if (rel == NULL)
		rel = gsf_open_pkg_lookup_rel_by_type
			(gsf_xml_in_get_input (xin),
			 "http://schemas.openxmlformats.org/officeDocument/2006/relationships/"
			 "externalLinkPath");
	if (NULL != rel && gsf_open_pkg_rel_is_extern (rel))
		state->external_ref = xlsx_conventions_add_extern_ref (
			state->convs, gsf_open_pkg_rel_get_target (rel));
	else
		xlsx_warning (xin, _("Unable to resolve external relationship"));
}
static void
xlsx_read_external_book_end (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	state->external_ref = NULL;
}
static void
xlsx_read_external_sheetname (GsfXMLIn *xin, xmlChar const **attrs)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	if (state->external_ref)
		for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2) {
			if (0 == strcmp (attrs[0], "val"))
				workbook_sheet_attach
					(state->external_ref,
					 state->external_ref_sheet =
					 wrap_sheet_new (state->external_ref, attrs[1], 256, 65536));
		}
}
static void
xlsx_read_external_sheetname_end (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	state->external_ref_sheet = NULL;
}

static GsfXMLInNode const xlsx_extern_dtd[] = {
GSF_XML_IN_NODE_FULL (START, START, -1, NULL, GSF_XML_NO_CONTENT, FALSE, TRUE, NULL, NULL, 0),
GSF_XML_IN_NODE_FULL (START, LINK, XL_NS_SS, "externalLink", GSF_XML_NO_CONTENT, TRUE, TRUE, xlsx_read_external_book, xlsx_read_external_book_end, 0),
  GSF_XML_IN_NODE (LINK, BOOK, XL_NS_SS, "externalBook", GSF_XML_NO_CONTENT, NULL, NULL),
  GSF_XML_IN_NODE (BOOK, SHEET_NAMES, XL_NS_SS, "sheetNames", GSF_XML_NO_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE (SHEET_NAMES, SHEET_NAME, XL_NS_SS, "sheetName", GSF_XML_NO_CONTENT, xlsx_read_external_sheetname, xlsx_read_external_sheetname_end),
  GSF_XML_IN_NODE (BOOK, SHEET_DATASET, XL_NS_SS, "sheetDataSet", GSF_XML_NO_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE (SHEET_DATASET, SHEET_DATA, XL_NS_SS, "sheetData", GSF_XML_NO_CONTENT, NULL, NULL),
      GSF_XML_IN_NODE (SHEET_DATA, ROW, XL_NS_SS, "row", GSF_XML_NO_CONTENT, NULL, NULL),
        GSF_XML_IN_NODE (ROW, CELL, XL_NS_SS, "cell", GSF_XML_NO_CONTENT, NULL, NULL),
          GSF_XML_IN_NODE (CELL, VAL, XL_NS_SS, "v", GSF_XML_NO_CONTENT, NULL, NULL),

GSF_XML_IN_NODE_END
};

static void
xlsx_wb_external_ref (GsfXMLIn *xin, xmlChar const **attrs)
{
	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2) {
		if (gsf_xml_in_namecmp (xin, attrs[0], XL_NS_DOC_REL, "id"))
			xlsx_parse_rel_by_id (xin, attrs[1], xlsx_extern_dtd, xlsx_ns);
	}
}

/**************************************************************************************************/

static void
xlsx_comments_start (GsfXMLIn *xin, G_GNUC_UNUSED xmlChar const **attrs)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	state->authors = g_ptr_array_new_with_free_func ((GDestroyNotify) g_free);
}

static void
xlsx_comments_end (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	g_ptr_array_unref (state->authors);
	state->authors = NULL;
}

static void
xlsx_comment_author_end (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	int i = strlen (xin->content->str);
	char *name = xin->content->str;

	/* remove any trailing white space */
	/* not sure this is correct, we might be careful about encoding */
	while (i > 0 && g_ascii_isspace (name[i-1]))
		i--;
	name = g_new (char, i + 1);
	memcpy (name, xin->content->str, i);
	name[i] = 0;
	g_ptr_array_add (state->authors, name);
}

static void
xlsx_comment_start (GsfXMLIn *xin, xmlChar const **attrs)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	SheetObject *so;
	GnmRange anchor_r;

	state->comment = g_object_new (cell_comment_get_type (), NULL);
	so = GNM_SO (state->comment);
	anchor_r = sheet_object_get_anchor (so)->cell_bound;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2) {
		if (strcmp (attrs[0], "ref") == 0)
			range_parse (&anchor_r, attrs[1], gnm_sheet_get_size (state->sheet));
		else if (strcmp (attrs[0], "authorId") == 0) {
			unsigned id = atoi (attrs[1]);
			char const *name;
			if (id < state->authors->len) {
				name = g_ptr_array_index (state->authors, id);
				if (*name) /* do not set an empty name */
					g_object_set (state->comment, "author", name, NULL);
			}
		}
	}

	cell_comment_set_pos (GNM_CELL_COMMENT (so), &anchor_r.start);
	state->r_text = g_string_new ("");
}

static void
xlsx_comment_end (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	char *text = g_string_free (state->r_text, FALSE);
	state->r_text = NULL;
	g_object_set (state->comment, "text", text, NULL);
	g_free (text);
	if (state->rich_attrs) {
		g_object_set (state->comment, "markup", state->rich_attrs, NULL);
		pango_attr_list_unref (state->rich_attrs);
		state->rich_attrs = NULL;
	}
	sheet_object_set_sheet (GNM_SO (state->comment), state->sheet);
	g_object_unref (state->comment);
	state->comment = NULL;

	maybe_update_progress (xin);
}

static void
xlsx_r_text (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	g_string_append (state->r_text, xin->content->str);
}

static GsfXMLInNode const xlsx_comments_dtd[] = {
GSF_XML_IN_NODE_FULL (START, START, -1, NULL, GSF_XML_NO_CONTENT, FALSE, TRUE, NULL, NULL, 0),
GSF_XML_IN_NODE_FULL (START, COMMENTS, XL_NS_SS, "comments", GSF_XML_NO_CONTENT, FALSE, TRUE, xlsx_comments_start, xlsx_comments_end, 0),
  GSF_XML_IN_NODE (COMMENTS, AUTHORS, XL_NS_SS, "authors", GSF_XML_NO_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE (AUTHORS, AUTHOR, XL_NS_SS, "author", GSF_XML_CONTENT, NULL, xlsx_comment_author_end),
  GSF_XML_IN_NODE (COMMENTS, COMMENTLIST, XL_NS_SS, "commentList", GSF_XML_NO_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE (COMMENTLIST, COMMENT, XL_NS_SS, "comment", GSF_XML_NO_CONTENT, xlsx_comment_start, xlsx_comment_end),
      GSF_XML_IN_NODE (COMMENT, TEXTITEM, XL_NS_SS, "text", GSF_XML_NO_CONTENT, NULL, NULL),
        GSF_XML_IN_NODE (TEXTITEM, TEXT, XL_NS_SS, "t", GSF_XML_CONTENT, NULL, xlsx_r_text),
        GSF_XML_IN_NODE (TEXTITEM, RICH, XL_NS_SS, "r", GSF_XML_NO_CONTENT, NULL, NULL),
	  RICH_TEXT_NODES,
        GSF_XML_IN_NODE (TEXTITEM, ITEM_PHONETIC_RUN, XL_NS_SS, "rPh", GSF_XML_NO_CONTENT, NULL, NULL),
          GSF_XML_IN_NODE (ITEM_PHONETIC_RUN, PHONETIC_TEXT, XL_NS_SS, "t", GSF_XML_CONTENT, NULL, NULL),
        GSF_XML_IN_NODE (TEXTITEM, ITEM_PHONETIC, XL_NS_SS, "phoneticPr", GSF_XML_NO_CONTENT, NULL, NULL),

GSF_XML_IN_NODE_END
};

/**************************************************************************************************/

static gint
cb_by_zorder (gconstpointer a, gconstpointer b, gpointer data)
{
	GHashTable *zorder = data;
	int za = GPOINTER_TO_UINT (g_hash_table_lookup (zorder, a));
	int zb = GPOINTER_TO_UINT (g_hash_table_lookup (zorder, b));
	return zb - za; /* descending */
}


static void
xlsx_wb_end (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	int i, n = workbook_sheet_count (state->wb);
	char const *part_id;
	GnmStyle *style;
	GsfInput *sin, *cin;
	GError *err = NULL;

	end_update_progress (state);

	/* Load sheets after setting up the workbooks to give us time to create
	 * all of them and parse names */
	for (i = 0 ; i < n ; i++, state->sheet = NULL) {
		char *message;
		int j, zoffset;
		GSList *l;

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
			range_init_full_sheet (&r, state->sheet);
			sheet_style_set_range (state->sheet, &r, style);
		}

		sin = gsf_open_pkg_open_rel_by_id (gsf_xml_in_get_input (xin), part_id, &err);
		if (NULL != err) {
			XLSXReadState *state = (XLSXReadState *)xin->user_state;
			go_io_warning (state->context, "%s", err->message);
			g_error_free (err);
			err = NULL;
			continue;
		}
		/* load comments */

		cin = gsf_open_pkg_open_rel_by_type (sin,
			"http://schemas.openxmlformats.org/officeDocument/2006/relationships/comments", NULL);
		message = g_strdup_printf (_("Reading sheet '%s'..."), state->sheet->name_unquoted);
		start_update_progress (state, sin, message,
				       0.3 + i*0.6/n, 0.3 + i*0.6/n + 0.5/n);
		g_free (message);
		xlsx_parse_stream (state, sin, xlsx_sheet_dtd);
		end_update_progress (state);

		if (cin != NULL) {
			start_update_progress (state, cin, _("Reading comments..."),
					       0.3 + i*0.6/n + 0.5/n, 0.3 + i*0.6/n + 0.6/n);
			xlsx_parse_stream (state, cin, xlsx_comments_dtd);
			end_update_progress (state);
		}

		zoffset = (g_slist_length (state->pending_objects) -
			   g_hash_table_size (state->zorder));
		for (j = zoffset, l = state->pending_objects; l; l = l->next) {
			SheetObject *so = l->data;
			int z = GPOINTER_TO_UINT (g_hash_table_lookup (state->zorder, so));
			if (z >= 1)
				z += zoffset;
			else
				z = j--;
			g_hash_table_insert (state->zorder, so, GINT_TO_POINTER (z));
		}
		state->pending_objects = g_slist_sort_with_data
			(state->pending_objects, cb_by_zorder, state->zorder);

		while (state->pending_objects) {
			SheetObject *obj = state->pending_objects->data;
			state->pending_objects = g_slist_delete_link (state->pending_objects,
								      state->pending_objects);
			sheet_object_set_sheet (obj, state->sheet);
			g_object_unref (obj);
		}

		/* Flag a respan here in case nothing else does */
		sheet_flag_recompute_spans (state->sheet);
	}
}

static void
xlsx_webpub_begin (GsfXMLIn *xin, xmlChar const **attrs)
{
	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2) {
		if (strcmp (attrs[0], "characterSet") == 0) {
			XLSXReadState *state = (XLSXReadState *)xin->user_state;
			state->version = ECMA_376_2008;
		}
	}
}

static GsfXMLInNode const xlsx_workbook_dtd[] = {
GSF_XML_IN_NODE_FULL (START, START, -1, NULL, GSF_XML_NO_CONTENT, FALSE, TRUE, NULL, NULL, 0),
GSF_XML_IN_NODE_FULL (START, WORKBOOK, XL_NS_SS, "workbook", GSF_XML_NO_CONTENT, FALSE, TRUE, NULL, &xlsx_wb_end, 0),
  GSF_XML_IN_NODE (WORKBOOK, EXTLST, XL_NS_SS, "extLst", GSF_XML_NO_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE (EXTLST, EXTITEM, XL_NS_SS, "ext", GSF_XML_NO_CONTENT, &xlsx_ext_begin, &xlsx_ext_end),
  GSF_XML_IN_NODE (WORKBOOK, VERSION, XL_NS_SS,	   "fileVersion", GSF_XML_NO_CONTENT, NULL, NULL),
  GSF_XML_IN_NODE (WORKBOOK, PROPERTIES, XL_NS_SS, "workbookPr", GSF_XML_NO_CONTENT, &xlsx_CT_WorkbookPr, NULL),
  GSF_XML_IN_NODE (WORKBOOK, CALC_PROPS, XL_NS_SS, "calcPr", GSF_XML_NO_CONTENT, &xlsx_CT_CalcPr, NULL),

  GSF_XML_IN_NODE (WORKBOOK, VIEWS,	 XL_NS_SS, "bookViews",	GSF_XML_NO_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE (VIEWS,  VIEW,	 XL_NS_SS, "workbookView",  GSF_XML_NO_CONTENT, &xlsx_CT_workbookView, NULL),
  GSF_XML_IN_NODE (WORKBOOK, CUSTOMWVIEWS, XL_NS_SS, "customWorkbookViews", GSF_XML_NO_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE (CUSTOMWVIEWS, CUSTOMWVIEW , XL_NS_SS, "customWorkbookView", GSF_XML_NO_CONTENT, NULL, NULL),
GSF_XML_IN_NODE (CUSTOMWVIEW, EXTLST, XL_NS_SS, "extLst", GSF_XML_2ND, NULL, NULL),
  GSF_XML_IN_NODE_FULL (WORKBOOK, SHEETS, XL_NS_SS, "sheets", GSF_XML_NO_CONTENT, FALSE, TRUE, NULL, NULL, 0),
    GSF_XML_IN_NODE (SHEETS, SHEET,	 XL_NS_SS, "sheet", GSF_XML_NO_CONTENT, &xlsx_sheet_begin, NULL),
  GSF_XML_IN_NODE (WORKBOOK, FGROUPS,	 XL_NS_SS, "functionGroups", GSF_XML_NO_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE (FGROUPS, FGROUP,	 XL_NS_SS, "functionGroup", GSF_XML_NO_CONTENT, NULL, NULL),
  GSF_XML_IN_NODE (WORKBOOK, WEB_PUB,	 XL_NS_SS, "webPublishing", GSF_XML_NO_CONTENT, xlsx_webpub_begin, NULL),
  GSF_XML_IN_NODE (WORKBOOK, EXTERNS,	 XL_NS_SS, "externalReferences", GSF_XML_NO_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE (EXTERNS, EXTERN,	 XL_NS_SS, "externalReference", GSF_XML_NO_CONTENT, xlsx_wb_external_ref, NULL),
  GSF_XML_IN_NODE (WORKBOOK, NAMES,	 XL_NS_SS, "definedNames", GSF_XML_NO_CONTENT, NULL, xlsx_wb_names_end),
    GSF_XML_IN_NODE (NAMES, NAME,	 XL_NS_SS, "definedName", GSF_XML_CONTENT, xlsx_wb_name_begin, xlsx_wb_name_end),
  GSF_XML_IN_NODE (WORKBOOK, PIVOTCACHES,      XL_NS_SS, "pivotCaches", GSF_XML_NO_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE (PIVOTCACHES, PIVOTCACHE,      XL_NS_SS, "pivotCache", GSF_XML_NO_CONTENT, &xlsx_CT_PivotCache, NULL),

  GSF_XML_IN_NODE (WORKBOOK, RECOVERY,	 XL_NS_SS, "fileRecoveryPr", GSF_XML_NO_CONTENT, NULL, NULL),
  GSF_XML_IN_NODE (WORKBOOK, OLESIZE,   XL_NS_SS, "oleSize", GSF_XML_NO_CONTENT, NULL, NULL),
  GSF_XML_IN_NODE (WORKBOOK, SMARTTAGPR,         XL_NS_SS, "smartTagPr", GSF_XML_NO_CONTENT, NULL, NULL),
  GSF_XML_IN_NODE (WORKBOOK, SMARTTTYPES,        XL_NS_SS, "smartTagTypes", GSF_XML_NO_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE (SMARTTTYPES, SMARTTTYPE,      XL_NS_SS, "smartTagType", GSF_XML_NO_CONTENT, NULL, NULL),
  GSF_XML_IN_NODE (WORKBOOK, WEB_PUB_OBJS,     XL_NS_SS, "webPublishObjects", GSF_XML_NO_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE (WEB_PUB_OBJS, WEB_PUB_OBJ,  XL_NS_SS, "webPublishObject", GSF_XML_NO_CONTENT, NULL, NULL),

GSF_XML_IN_NODE_END
};

/****************************************************************************/

static void
xlsx_sst_begin (GsfXMLIn *xin, xmlChar const **attrs)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	int count;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2) {
		if (attr_int (xin, attrs, "uniqueCount", &count))
			g_array_set_size (state->sst, count);
	}
	state->count = 0;
}

static void
xlsx_sstitem_start (GsfXMLIn *xin, G_GNUC_UNUSED xmlChar const **attrs)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	state->r_text = g_string_new ("");
}

static void
xlsx_sstitem_end (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	XLSXStr *entry;
	char *text = g_string_free (state->r_text, FALSE);
	state->r_text = NULL;

	if (state->count >= state->sst->len)
		g_array_set_size (state->sst, state->count+1);
	entry = &g_array_index (state->sst, XLSXStr, state->count);
	state->count++;
	entry->str = go_string_new_nocopy (text);

	if (state->rich_attrs) {
		entry->markup = go_format_new_markup (state->rich_attrs, FALSE);
		state->rich_attrs = NULL;
	}
}

static GsfXMLInNode const xlsx_shared_strings_dtd[] = {
GSF_XML_IN_NODE_FULL (START, START, -1, NULL, GSF_XML_NO_CONTENT, FALSE, TRUE, NULL, NULL, 0),
GSF_XML_IN_NODE_FULL (START, SST, XL_NS_SS, "sst", GSF_XML_NO_CONTENT, FALSE, TRUE, &xlsx_sst_begin, NULL, 0),
  GSF_XML_IN_NODE (SST, ITEM, XL_NS_SS, "si", GSF_XML_NO_CONTENT, &xlsx_sstitem_start, &xlsx_sstitem_end),		/* beta2 */
    GSF_XML_IN_NODE (ITEM, TEXT, XL_NS_SS, "t", GSF_XML_CONTENT, NULL, xlsx_r_text),
    GSF_XML_IN_NODE (ITEM, RICH, XL_NS_SS, "r", GSF_XML_NO_CONTENT, NULL, NULL),
      RICH_TEXT_NODES,
    GSF_XML_IN_NODE (ITEM, ITEM_PHONETIC_RUN, XL_NS_SS, "rPh", GSF_XML_NO_CONTENT, NULL, NULL),
      GSF_XML_IN_NODE (ITEM_PHONETIC_RUN, PHONETIC_TEXT, XL_NS_SS, "t", GSF_XML_SHARED_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE (ITEM, ITEM_PHONETIC, XL_NS_SS, "phoneticPr", GSF_XML_NO_CONTENT, NULL, NULL),

GSF_XML_IN_NODE_END
};

/****************************************************************************/

static void
xlsx_numfmt_common (GsfXMLIn *xin, xmlChar const **attrs, gboolean apply)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	xmlChar const *fmt = NULL;
	xmlChar const *id = NULL;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2) {
		if (0 == strcmp (attrs[0], "numFmtId"))
			id = attrs[1];
		else if (0 == strcmp (attrs[0], "formatCode"))
			fmt = attrs[1];
	}

	if (NULL != id && NULL != fmt) {
		GOFormat *gfmt = go_format_new_from_XL (fmt);
		if (apply)
			gnm_style_set_format (state->style_accum, gfmt);
		if (xlsx_get_num_fmt (xin, id, TRUE)) {
			g_printerr ("Ignoring attempt to override number format %s\n", id);
			go_format_unref (gfmt);
		} else
			g_hash_table_replace (state->num_fmts, g_strdup (id), gfmt);
	}
}

static void
xlsx_style_numfmt (GsfXMLIn *xin, xmlChar const **attrs)
{
	xlsx_numfmt_common (xin, attrs, FALSE);
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
	unsigned count = 0;
	GPtrArray **pcollection;

	g_return_if_fail (NULL == state->collection);

	switch (xin->node->user_data.v_int) {
	case XLSX_COLLECT_FONT: pcollection = &state->fonts; break;
	case XLSX_COLLECT_FILLS: pcollection = &state->fills; break;
	case XLSX_COLLECT_BORDERS: pcollection = &state->borders; break;
	case XLSX_COLLECT_XFS: pcollection = &state->xfs; break;
	case XLSX_COLLECT_STYLE_XFS: pcollection = &state->style_xfs; break;
	case XLSX_COLLECT_DXFS: pcollection = &state->dxfs; break;
	case XLSX_COLLECT_TABLE_STYLES: pcollection = &state->table_styles; break;
	default:
		g_assert_not_reached ();
		return;
	}

	state->count = 0;
	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2) {
		if (attr_uint (xin, attrs, "count", &count))
			;
	}

	/* Don't trust huge counts. */
	count = MIN (count, 1000u);

	if (*pcollection == NULL) {
		*pcollection = g_ptr_array_new ();
		g_ptr_array_set_size (*pcollection, count);
	}

	state->collection = *pcollection;
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
xlsx_col_elem_begin (GsfXMLIn *xin, G_GNUC_UNUSED xmlChar const **attrs)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	if (!state->style_accum_partial) {
		g_return_if_fail (NULL == state->style_accum);
		state->style_accum = gnm_style_new ();
	}
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
xlsx_col_border_begin (GsfXMLIn *xin, xmlChar const **attrs)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	gboolean diagonal_down = FALSE, diagonal_up = FALSE;

	xlsx_col_elem_begin (xin, attrs);
	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2) {
		if (attr_bool (xin, attrs, "diagonalDown", &diagonal_down))
			;
		else (attr_bool (xin, attrs, "diagonalUp", &diagonal_up))
			     ;
	}

	if (diagonal_up) {
		GnmBorder *border = gnm_style_border_fetch
			(GNM_STYLE_BORDER_THIN, style_color_black (), GNM_STYLE_BORDER_DIAGONAL);
		gnm_style_set_border (state->style_accum,
				      MSTYLE_BORDER_DIAGONAL,
				      border);
	}
	if (diagonal_down) {
		GnmBorder *border = gnm_style_border_fetch
			(GNM_STYLE_BORDER_HAIR, style_color_black (), GNM_STYLE_BORDER_DIAGONAL);
		gnm_style_set_border (state->style_accum,
				      MSTYLE_BORDER_REV_DIAGONAL,
				      border);
	}
}

static void
xlsx_font_name (GsfXMLIn *xin, xmlChar const **attrs)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	const char *name = simple_string (xin, attrs);
	if (name)
		gnm_style_set_font_name	(state->style_accum, name);
}

static void
xlsx_font_bold (GsfXMLIn *xin, xmlChar const **attrs)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	int val = TRUE;
	(void)simple_bool (xin, attrs, &val);
	gnm_style_set_font_bold (state->style_accum, val);
}

static void
xlsx_font_italic (GsfXMLIn *xin, xmlChar const **attrs)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	int val = TRUE;
	(void)simple_bool (xin, attrs, &val);
	gnm_style_set_font_italic (state->style_accum, val);
}

static void
xlsx_font_strike (GsfXMLIn *xin, xmlChar const **attrs)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	int val = TRUE;
	(void)simple_bool (xin, attrs, &val);
	gnm_style_set_font_strike (state->style_accum, val);
}

static void
xlsx_font_color (GsfXMLIn *xin, xmlChar const **attrs)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	/* LibreOffice 3.3.2 sets the alpha to 0, so text becomes invisible */
	/* (Excel drops the alpha too it seems.) */
	GnmColor *color = elem_color (xin, attrs, FALSE);
	if (NULL != color)
		gnm_style_set_font_color (state->style_accum, color);
}
static void
xlsx_CT_FontSize (GsfXMLIn *xin, xmlChar const **attrs)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	double val;
	if (simple_double (xin, attrs, &val))
		gnm_style_set_font_size	(state->style_accum, val);
}
static void
xlsx_CT_vertAlign (GsfXMLIn *xin, xmlChar const **attrs)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	static EnumVal const types[] = {
		{ "subscript", GO_FONT_SCRIPT_SUB },
		{ "baseline", GO_FONT_SCRIPT_STANDARD },
		{ "superscript", GO_FONT_SCRIPT_SUPER },
		{ NULL, 0 }
	};
	int val = GO_FONT_SCRIPT_STANDARD;
	(void)simple_enum (xin, attrs, types, &val);
	gnm_style_set_font_script (state->style_accum, val);
}
static void
xlsx_font_uline (GsfXMLIn *xin, xmlChar const **attrs)
{
	static EnumVal const types[] = {
		{ "single", UNDERLINE_SINGLE },
		{ "double", UNDERLINE_DOUBLE },
		{ "singleAccounting", UNDERLINE_SINGLE_LOW },
		{ "doubleAccounting", UNDERLINE_DOUBLE_LOW },
		{ "none", UNDERLINE_NONE },
		{ NULL, 0 }
	};
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	int val = UNDERLINE_SINGLE;
	(void)simple_enum (xin, attrs, types, &val);
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
	int val = GO_FONT_SCRIPT_STANDARD;
	(void)simple_enum (xin, attrs, types, &val);
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

	/* we are setting a default pattern of solid, see bug #702615 */
	/* Note that this conflicts with ECMA 376: "This element is used
	   to specify cell fill information for pattern and solid color
	   cell fills. For solid cell fills (no pattern), fgColor is used.
	   For cell fills with patterns specified, then the cell fill color
	   is specified by the bgColor element." */
	/* As the above bug report shows Excel 2010 creates: */
	/* <dxf><font><color rgb="FF9C0006"/></font><fill><patternFill>
	   <bgColor rgb="FFFFC7CE"/></patternFill></fill></dxf> */

	gnm_style_set_pattern (state->style_accum, 1);

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2) {
		if (attr_enum (xin, attrs, "patternType", patterns, &val))
			gnm_style_set_pattern (state->style_accum, val);
	}
}
static void
xlsx_pattern_fg_bg (GsfXMLIn *xin, xmlChar const **attrs)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	gboolean solid_pattern = gnm_style_is_element_set (state->style_accum, MSTYLE_PATTERN)
		&& (1 == gnm_style_get_pattern (state->style_accum));
	/* MAGIC :
	 * Looks like pattern background and forground colours are inverted for
	 * dxfs with solid fills for no apparent reason. */
	gboolean const invert = state->style_accum_partial
		&& solid_pattern;
	/* LibreOffice 3.3.2 sets the alpha to 0, so solid fill becomes invisible */
	/* (Excel drops the alpha too it seems.) */
	GnmColor *color = elem_color (xin, attrs, !solid_pattern);
	if (NULL == color)
		return;

	if (xin->node->user_data.v_int ^ invert)
		gnm_style_set_back_color (state->style_accum, color);
	else
		gnm_style_set_pattern_color (state->style_accum, color);
}

static void
xlsx_CT_GradientFill (GsfXMLIn *xin, G_GNUC_UNUSED xmlChar const **attrs)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;

	gnm_style_set_pattern (state->style_accum, 1);

#if 0
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
    <xsd:attribute name="type" type="ST_GradientType" use="optional" default="linear">
    <xsd:attribute name="degree" type="xsd:double" use="optional" default="0">
    <xsd:attribute name="left" type="xsd:double" use="optional" default="0">
    <xsd:attribute name="right" type="xsd:double" use="optional" default="0">
    <xsd:attribute name="top" type="xsd:double" use="optional" default="0">
    <xsd:attribute name="bottom" type="xsd:double" use="optional" default="0">
#endif
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

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2) {
		if (attr_enum (xin, attrs, "style", borders, &border_style))
			;
	}
	state->border_style = border_style;
	state->border_color = NULL;
}

static void
xlsx_border_begin_v2 (GsfXMLIn *xin, xmlChar const **attrs)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	state->version = ECMA_376_2008;
	xlsx_border_begin (xin, attrs);
}

static void
xlsx_border_end (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	GnmBorder *border;
	GnmStyleBorderLocation const loc = xin->node->user_data.v_int;

	if (NULL == state->border_color)
		state->border_color = style_color_black ();
	border = gnm_style_border_fetch (state->border_style,
		state->border_color, gnm_style_border_get_orientation (loc));
	gnm_style_set_border (state->style_accum,
		GNM_STYLE_BORDER_LOCATION_TO_STYLE_ELEMENT (loc),
		border);
	state->border_color = NULL;
}

static void
xlsx_border_diagonal_end (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	GnmBorder *border, *new_border;

	if (NULL == state->border_color)
		state->border_color = style_color_black ();
	new_border = gnm_style_border_fetch
		(state->border_style, state->border_color, GNM_STYLE_BORDER_DIAGONAL);

	border = gnm_style_get_border (state->style_accum, MSTYLE_BORDER_REV_DIAGONAL);
	if (border != NULL && border->line_type != GNM_STYLE_BORDER_NONE) {
		gnm_style_border_ref (new_border);
		gnm_style_set_border (state->style_accum,
				      MSTYLE_BORDER_REV_DIAGONAL,
				      new_border);
	}
	border = gnm_style_get_border (state->style_accum, MSTYLE_BORDER_DIAGONAL);
	if (border != NULL && border->line_type != GNM_STYLE_BORDER_NONE) {
		gnm_style_border_ref (new_border);
		gnm_style_set_border (state->style_accum,
				      MSTYLE_BORDER_DIAGONAL,
				      new_border);
	}
	gnm_style_border_unref (new_border);
	state->border_color = NULL;
}

static void
xlsx_border_color (GsfXMLIn *xin, xmlChar const **attrs)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	GnmColor *color = elem_color (xin, attrs, TRUE);
	style_color_unref (state->border_color);
	state->border_color = color;
}

static void
xlsx_xf_begin (GsfXMLIn *xin, xmlChar const **attrs)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	GnmStyle *accum = gnm_style_new ();
	GnmStyle *parent = NULL;
	GnmStyle *result;
	GPtrArray *elem = NULL;
	int indx;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2) {
		if (0 == strcmp (attrs[0], "numFmtId")) {
			GOFormat *fmt = xlsx_get_num_fmt (xin, attrs[1], FALSE);
			if (NULL != fmt)
				gnm_style_set_format (accum, fmt);
		} else if (attr_int (xin, attrs, "fontId", &indx))
			elem = state->fonts;
		else if (attr_int (xin, attrs, "fillId", &indx))
			elem = state->fills;
		else if (attr_int (xin, attrs, "borderId", &indx))
			elem = state->borders;
		else if (attr_int (xin, attrs, "xfId", &indx))
			parent = xlsx_get_style_xf (xin, indx);

		if (NULL != elem) {
			GnmStyle const *component = NULL;
			if (0 <= indx && indx < (int)elem->len)
				component = g_ptr_array_index (elem, indx);
			if (NULL != component) {
#if 0
				gnm_style_merge (accum, component);
#else
				GnmStyle *merged = gnm_style_new_merged (accum, component);
				gnm_style_unref (accum);
				accum = merged;
#endif
			} else
				xlsx_warning (xin, "Missing record '%d' for %s", indx, attrs[0]);
			elem = NULL;
		}
	}
	if (NULL == parent) {
		result = gnm_style_new_default ();
		gnm_style_merge (result, accum);
	} else
		result = gnm_style_new_merged (parent, accum);
	gnm_style_unref (accum);

	state->style_accum = result;
#if 0
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
		{ "general" , GNM_HALIGN_GENERAL },
		{ "left" , GNM_HALIGN_LEFT },
		{ "center" , GNM_HALIGN_CENTER },
		{ "right" , GNM_HALIGN_RIGHT },
		{ "fill" , GNM_HALIGN_FILL },
		{ "justify" , GNM_HALIGN_JUSTIFY },
		{ "centerContinuous" , GNM_HALIGN_CENTER_ACROSS_SELECTION },
		{ "distributed" , GNM_HALIGN_DISTRIBUTED },
		{ NULL, 0 }
	};

	static EnumVal const valigns[] = {
		{ "top", GNM_VALIGN_TOP },
		{ "center", GNM_VALIGN_CENTER },
		{ "bottom", GNM_VALIGN_BOTTOM },
		{ "justify", GNM_VALIGN_JUSTIFY },
		{ "distributed", GNM_VALIGN_DISTRIBUTED },
		{ NULL, 0 }
	};

	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	int halign = GNM_HALIGN_GENERAL;
	int valign = GNM_VALIGN_BOTTOM;
	int rotation = 0, indent = 0;
	int wrapText = FALSE, justifyLastLine = FALSE, shrinkToFit = FALSE;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2) {
		if (attr_enum (xin, attrs, "horizontal", haligns, &halign) ||
		    attr_enum (xin, attrs, "vertical", valigns, &valign) ||
		    attr_int (xin, attrs, "textRotation", &rotation) ||
		    attr_bool (xin, attrs, "wrapText", &wrapText) ||
		    attr_int (xin, attrs, "indent", &indent) ||
		    attr_bool (xin, attrs, "justifyLastLine", &justifyLastLine) ||
		    attr_bool (xin, attrs, "shrinkToFit", &shrinkToFit)
		/* "mergeCell" type="xs:boolean" use="optional" default="false" */
		/* "readingOrder" type="xs:unsignedInt" use="optional" default="0" */
			)
			;
	}
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

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2) {
		if (attr_bool (xin, attrs, "locked", &locked))
			;
		else if (attr_bool (xin, attrs, "hidden", &hidden))
			;
	}

	gnm_style_set_contents_locked (state->style_accum, locked);
	gnm_style_set_contents_hidden (state->style_accum, hidden);
}

static void
xlsx_cell_style (GsfXMLIn *xin, xmlChar const **attrs)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	/* xmlChar const *name = NULL; */
	xmlChar const *id = NULL;
	GnmStyle *style = NULL;
	int tmp;

	/* cellStyle name="Normal" xfId="0" builtinId="0" */
	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2) {
		if (attr_int (xin, attrs, "xfId", &tmp))
			style = xlsx_get_style_xf (xin, tmp);
		else /* if (0 == strcmp (attrs[0], "name")) */
		/* 	name = attrs[1]; */
		/* else */ if (0 == strcmp (attrs[0], "builtinId"))
			id = attrs[1];
	}

	if (NULL != style && NULL != id) {
		gnm_style_ref (style);
		g_hash_table_replace (state->cell_styles, g_strdup (id), style);
	}
}

static void
xlsx_dxf_begin (GsfXMLIn *xin, G_GNUC_UNUSED xmlChar const **attrs)
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

static void
xlsx_dxf_numfmt (GsfXMLIn *xin, xmlChar const **attrs)
{
	xlsx_numfmt_common (xin, attrs, TRUE);
}


static GsfXMLInNode const xlsx_styles_dtd[] = {
GSF_XML_IN_NODE_FULL (START, START, -1, NULL, GSF_XML_NO_CONTENT, FALSE, TRUE, NULL, NULL, 0),
GSF_XML_IN_NODE_FULL (START, STYLE_INFO, XL_NS_SS, "styleSheet", GSF_XML_NO_CONTENT, FALSE, TRUE, NULL, NULL, 0),
  GSF_XML_IN_NODE (STYLE_INFO, EXTLST, XL_NS_SS, "extLst", GSF_XML_NO_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE (EXTLST, EXTITEM, XL_NS_SS, "ext", GSF_XML_NO_CONTENT, &xlsx_ext_begin, &xlsx_ext_end),
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
      GSF_XML_IN_NODE (FONT, FONT_SCRIPT,    XL_NS_SS, "vertAlign", GSF_XML_NO_CONTENT,	&xlsx_CT_vertAlign, NULL),
      GSF_XML_IN_NODE (FONT, FONT_ULINE,     XL_NS_SS, "u",	    GSF_XML_NO_CONTENT,	&xlsx_font_uline, NULL),
      GSF_XML_IN_NODE (FONT, FONT_VERTALIGN, XL_NS_SS, "vertAlign", GSF_XML_NO_CONTENT, &xlsx_font_valign, NULL),
      GSF_XML_IN_NODE (FONT, FONT_SCHEME,    XL_NS_SS, "scheme",    GSF_XML_NO_CONTENT, NULL, NULL),

  GSF_XML_IN_NODE_FULL (STYLE_INFO, FILLS, XL_NS_SS, "fills", GSF_XML_NO_CONTENT,
			FALSE, FALSE, &xlsx_collection_begin, &xlsx_collection_end, XLSX_COLLECT_FILLS),
    GSF_XML_IN_NODE (FILLS, FILL, XL_NS_SS, "fill", GSF_XML_NO_CONTENT, &xlsx_col_elem_begin, &xlsx_col_elem_end),
      GSF_XML_IN_NODE (FILL, PATTERN_FILL, XL_NS_SS, "patternFill", GSF_XML_NO_CONTENT, &xlsx_pattern, NULL),
	GSF_XML_IN_NODE_FULL (PATTERN_FILL, PATTERN_FILL_FG,  XL_NS_SS, "fgColor", GSF_XML_NO_CONTENT,
			      FALSE, FALSE, &xlsx_pattern_fg_bg, NULL, TRUE),
	GSF_XML_IN_NODE_FULL (PATTERN_FILL, PATTERN_FILL_BG,  XL_NS_SS, "bgColor", GSF_XML_NO_CONTENT,
			      FALSE, FALSE, &xlsx_pattern_fg_bg, NULL, FALSE),
      GSF_XML_IN_NODE (FILL, IMAGE_FILL, XL_NS_SS, "image", GSF_XML_NO_CONTENT, NULL, NULL),
      GSF_XML_IN_NODE (FILL, GRADIENT_FILL, XL_NS_SS, "gradientFill", GSF_XML_NO_CONTENT, &xlsx_CT_GradientFill, NULL),
	GSF_XML_IN_NODE (GRADIENT_FILL, GRADIENT_STOPS, XL_NS_SS, "stop", GSF_XML_NO_CONTENT, NULL, NULL),
	  GSF_XML_IN_NODE_FULL (GRADIENT_STOPS, GRADIENT_COLOR, XL_NS_SS, "color", GSF_XML_NO_CONTENT, FALSE, FALSE, &xlsx_pattern_fg_bg, NULL, TRUE),

  GSF_XML_IN_NODE_FULL (STYLE_INFO, BORDERS, XL_NS_SS, "borders", GSF_XML_NO_CONTENT,
			FALSE, FALSE, &xlsx_collection_begin, &xlsx_collection_end, XLSX_COLLECT_BORDERS),
    GSF_XML_IN_NODE (BORDERS, BORDER, XL_NS_SS, "border", GSF_XML_NO_CONTENT, &xlsx_col_border_begin, &xlsx_col_elem_end),
      GSF_XML_IN_NODE_FULL (BORDER, LEFT_B, XL_NS_SS, "left", GSF_XML_NO_CONTENT, FALSE, FALSE,
			    &xlsx_border_begin, &xlsx_border_end, GNM_STYLE_BORDER_LEFT),
        GSF_XML_IN_NODE (LEFT_B, LEFT_COLOR, XL_NS_SS, "color", GSF_XML_NO_CONTENT, &xlsx_border_color, NULL),
      GSF_XML_IN_NODE_FULL (BORDER, START_B, XL_NS_SS, "start", GSF_XML_NO_CONTENT, FALSE, FALSE,
			    &xlsx_border_begin_v2, &xlsx_border_end, GNM_STYLE_BORDER_LEFT),
        GSF_XML_IN_NODE (START_B, START_COLOR, XL_NS_SS, "color", GSF_XML_NO_CONTENT, &xlsx_border_color, NULL),
      GSF_XML_IN_NODE_FULL (BORDER, RIGHT_B, XL_NS_SS, "right", GSF_XML_NO_CONTENT, FALSE, FALSE,
			    &xlsx_border_begin, &xlsx_border_end, GNM_STYLE_BORDER_RIGHT),
        GSF_XML_IN_NODE (RIGHT_B, RIGHT_COLOR, XL_NS_SS, "color", GSF_XML_NO_CONTENT, &xlsx_border_color, NULL),
      GSF_XML_IN_NODE_FULL (BORDER, END_B, XL_NS_SS, "end", GSF_XML_NO_CONTENT, FALSE, FALSE,
			    &xlsx_border_begin_v2, &xlsx_border_end, GNM_STYLE_BORDER_RIGHT),
        GSF_XML_IN_NODE (END_B, END_COLOR, XL_NS_SS, "color", GSF_XML_NO_CONTENT, &xlsx_border_color, NULL),
       GSF_XML_IN_NODE_FULL (BORDER, TOP_B, XL_NS_SS,	"top", GSF_XML_NO_CONTENT, FALSE, FALSE,
			    &xlsx_border_begin, &xlsx_border_end, GNM_STYLE_BORDER_TOP),
        GSF_XML_IN_NODE (TOP_B, TOP_COLOR, XL_NS_SS, "color", GSF_XML_NO_CONTENT, &xlsx_border_color, NULL),
      GSF_XML_IN_NODE_FULL (BORDER, BOTTOM_B, XL_NS_SS, "bottom", GSF_XML_NO_CONTENT, FALSE, FALSE,
			    &xlsx_border_begin, &xlsx_border_end, GNM_STYLE_BORDER_BOTTOM),
        GSF_XML_IN_NODE (BOTTOM_B, BOTTOM_COLOR, XL_NS_SS, "color", GSF_XML_NO_CONTENT, &xlsx_border_color, NULL),
      GSF_XML_IN_NODE (BORDER, DIAG_B, XL_NS_SS, "diagonal", GSF_XML_NO_CONTENT,
			    &xlsx_border_begin, &xlsx_border_diagonal_end),
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
    GSF_XML_IN_NODE (STYLE_NAME, EXTLST, XL_NS_SS, "extLst", GSF_XML_2ND, NULL, NULL),
  GSF_XML_IN_NODE_FULL (STYLE_INFO, PARTIAL_XFS, XL_NS_SS, "dxfs", GSF_XML_NO_CONTENT,
			FALSE, FALSE, &xlsx_collection_begin, &xlsx_collection_end, XLSX_COLLECT_DXFS),
    GSF_XML_IN_NODE (PARTIAL_XFS, PARTIAL_XF, XL_NS_SS, "dxf", GSF_XML_NO_CONTENT, &xlsx_dxf_begin, &xlsx_dxf_end),
      GSF_XML_IN_NODE (PARTIAL_XF, DXF_NUM_FMT, XL_NS_SS, "numFmt", GSF_XML_NO_CONTENT, &xlsx_dxf_numfmt, NULL),
      GSF_XML_IN_NODE (PARTIAL_XF, FONT,    XL_NS_SS, "font", GSF_XML_2ND, NULL, NULL),
      GSF_XML_IN_NODE (PARTIAL_XF, FILL,    XL_NS_SS, "fill", GSF_XML_2ND, NULL, NULL),
      GSF_XML_IN_NODE (PARTIAL_XF, BORDER,  XL_NS_SS, "border", GSF_XML_2ND, NULL, NULL),
      GSF_XML_IN_NODE (PARTIAL_XF, DXF_ALIGNMENT, XL_NS_SS, "alignment", GSF_XML_NO_CONTENT, &xlsx_xf_align, NULL),
      GSF_XML_IN_NODE (PARTIAL_XF, DXF_PROTECTION, XL_NS_SS, "protection", GSF_XML_NO_CONTENT, &xlsx_xf_protect, NULL),
      GSF_XML_IN_NODE (PARTIAL_XF, EXTLST, XL_NS_SS, "extLst", GSF_XML_2ND, NULL, NULL),

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
	GOColor c = GO_COLOR_BLACK;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2) {
		if (attr_gocolor (xin, attrs, "lastClr", &c)) {
		}
	}

	state->color = c;
}
static void
xlsx_theme_color_rgb (GsfXMLIn *xin, xmlChar const **attrs)
{
	XLSXReadState	*state = (XLSXReadState *)xin->user_state;
	GOColor c = GO_COLOR_BLACK;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2) {
		if (attr_gocolor (xin, attrs, "val", &c)) {
		}
	}

	state->color = c;
}

static void
xlsx_theme_color_end (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	const char *name = ((GsfXMLInNode *)xin->node_stack->data)->name;

	g_hash_table_replace (state->theme_colors_by_name,
			      g_strdup (name),
			      GUINT_TO_POINTER (state->color));
}

static GsfXMLInNode const xlsx_theme_dtd[] = {
GSF_XML_IN_NODE_FULL (START, START, -1, NULL, GSF_XML_NO_CONTENT, FALSE, TRUE, NULL, NULL, 0),
GSF_XML_IN_NODE_FULL (START, THEME, XL_NS_DRAW, "theme", GSF_XML_NO_CONTENT, FALSE, TRUE, NULL, NULL, 0),
  GSF_XML_IN_NODE (THEME, ELEMENTS, XL_NS_DRAW, "themeElements", GSF_XML_NO_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE (ELEMENTS, COLOR_SCHEME, XL_NS_DRAW, "clrScheme", GSF_XML_NO_CONTENT, NULL, NULL),
      GSF_XML_IN_NODE (COLOR_SCHEME, dk1, XL_NS_DRAW, "dk1", GSF_XML_NO_CONTENT, NULL, NULL),
        GSF_XML_IN_NODE (dk1, SYS_COLOR, XL_NS_DRAW, "sysClr", GSF_XML_NO_CONTENT, &xlsx_theme_color_sys, &xlsx_theme_color_end),
          COLOR_MODIFIER_NODES(SYS_COLOR,TRUE),
        GSF_XML_IN_NODE (dk1, RGB_COLOR, XL_NS_DRAW, "srgbClr", GSF_XML_NO_CONTENT, &xlsx_theme_color_rgb, &xlsx_theme_color_end),
          COLOR_MODIFIER_NODES(RGB_COLOR,FALSE),
      GSF_XML_IN_NODE (COLOR_SCHEME, lt1, XL_NS_DRAW, "lt1", GSF_XML_NO_CONTENT, NULL, NULL),
        GSF_XML_IN_NODE (lt1, SYS_COLOR, XL_NS_DRAW, "sysClr", GSF_XML_2ND, NULL, NULL),
        GSF_XML_IN_NODE (lt1, RGB_COLOR, XL_NS_DRAW, "srgbClr", GSF_XML_2ND, NULL, NULL),
      GSF_XML_IN_NODE (COLOR_SCHEME, lt2, XL_NS_DRAW, "lt2", GSF_XML_NO_CONTENT, NULL, NULL),
        GSF_XML_IN_NODE (lt2, SYS_COLOR, XL_NS_DRAW, "sysClr", GSF_XML_2ND, NULL, NULL),
        GSF_XML_IN_NODE (lt2, RGB_COLOR, XL_NS_DRAW, "srgbClr", GSF_XML_2ND, NULL, NULL),
      GSF_XML_IN_NODE (COLOR_SCHEME, dk2, XL_NS_DRAW, "dk2", GSF_XML_NO_CONTENT, NULL, NULL),
        GSF_XML_IN_NODE (dk2, SYS_COLOR, XL_NS_DRAW, "sysClr", GSF_XML_2ND, NULL, NULL),
        GSF_XML_IN_NODE (dk2, RGB_COLOR, XL_NS_DRAW, "srgbClr", GSF_XML_2ND, NULL, NULL),
      GSF_XML_IN_NODE (COLOR_SCHEME, accent1, XL_NS_DRAW, "accent1", GSF_XML_NO_CONTENT, NULL, NULL),
        GSF_XML_IN_NODE (accent1, SYS_COLOR, XL_NS_DRAW, "sysClr", GSF_XML_2ND, NULL, NULL),
        GSF_XML_IN_NODE (accent1, RGB_COLOR, XL_NS_DRAW, "srgbClr", GSF_XML_2ND, NULL, NULL),
      GSF_XML_IN_NODE (COLOR_SCHEME, accent2, XL_NS_DRAW, "accent2", GSF_XML_NO_CONTENT, NULL, NULL),
        GSF_XML_IN_NODE (accent2, SYS_COLOR, XL_NS_DRAW, "sysClr", GSF_XML_2ND, NULL, NULL),
        GSF_XML_IN_NODE (accent2, RGB_COLOR, XL_NS_DRAW, "srgbClr", GSF_XML_2ND, NULL, NULL),
      GSF_XML_IN_NODE (COLOR_SCHEME, accent3, XL_NS_DRAW, "accent3", GSF_XML_NO_CONTENT, NULL, NULL),
        GSF_XML_IN_NODE (accent3, SYS_COLOR, XL_NS_DRAW, "sysClr", GSF_XML_2ND, NULL, NULL),
        GSF_XML_IN_NODE (accent3, RGB_COLOR, XL_NS_DRAW, "srgbClr", GSF_XML_2ND, NULL, NULL),
      GSF_XML_IN_NODE (COLOR_SCHEME, accent4, XL_NS_DRAW, "accent4", GSF_XML_NO_CONTENT, NULL, NULL),
        GSF_XML_IN_NODE (accent4, SYS_COLOR, XL_NS_DRAW, "sysClr", GSF_XML_2ND, NULL, NULL),
        GSF_XML_IN_NODE (accent4, RGB_COLOR, XL_NS_DRAW, "srgbClr", GSF_XML_2ND, NULL, NULL),
      GSF_XML_IN_NODE (COLOR_SCHEME, accent5, XL_NS_DRAW, "accent5", GSF_XML_NO_CONTENT, NULL, NULL),
        GSF_XML_IN_NODE (accent5, SYS_COLOR, XL_NS_DRAW, "sysClr", GSF_XML_2ND, NULL, NULL),
        GSF_XML_IN_NODE (accent5, RGB_COLOR, XL_NS_DRAW, "srgbClr", GSF_XML_2ND, NULL, NULL),
      GSF_XML_IN_NODE (COLOR_SCHEME, accent6, XL_NS_DRAW, "accent6", GSF_XML_NO_CONTENT, NULL, NULL),
        GSF_XML_IN_NODE (accent6, SYS_COLOR, XL_NS_DRAW, "sysClr", GSF_XML_2ND, NULL, NULL),
        GSF_XML_IN_NODE (accent6, RGB_COLOR, XL_NS_DRAW, "srgbClr", GSF_XML_2ND, NULL, NULL),
      GSF_XML_IN_NODE (COLOR_SCHEME, hlink, XL_NS_DRAW, "hlink", GSF_XML_NO_CONTENT, NULL, NULL),
        GSF_XML_IN_NODE (hlink, SYS_COLOR, XL_NS_DRAW, "sysClr", GSF_XML_2ND, NULL, NULL),
        GSF_XML_IN_NODE (hlink, RGB_COLOR, XL_NS_DRAW, "srgbClr", GSF_XML_2ND, NULL, NULL),
      GSF_XML_IN_NODE (COLOR_SCHEME, folHlink, XL_NS_DRAW, "folHlink", GSF_XML_NO_CONTENT, NULL, NULL),
        GSF_XML_IN_NODE (folHlink, SYS_COLOR, XL_NS_DRAW, "sysClr", GSF_XML_2ND, NULL, NULL),
        GSF_XML_IN_NODE (folHlink, RGB_COLOR, XL_NS_DRAW, "srgbClr", GSF_XML_2ND, NULL, NULL),

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
           COLOR_MODIFIER_NODES(SCHEME_COLOR,FALSE),
        GSF_XML_IN_NODE (FILL_STYLE_LIST,  GRAD_FILL, XL_NS_DRAW, "gradFill", GSF_XML_NO_CONTENT, NULL, NULL),
          GSF_XML_IN_NODE (GRAD_FILL, GRAD_PATH, XL_NS_DRAW, "path", GSF_XML_NO_CONTENT, NULL, NULL),
            GSF_XML_IN_NODE (GRAD_PATH, GRAD_PATH_RECT, XL_NS_DRAW, "fillToRect", GSF_XML_NO_CONTENT, NULL, NULL),
	  GSF_XML_IN_NODE (GRAD_FILL, GRAD_LIST, XL_NS_DRAW, "gsLst", GSF_XML_NO_CONTENT, NULL, NULL),
	   GSF_XML_IN_NODE (GRAD_LIST, GRAD_LIST_ITEM, XL_NS_DRAW, "gs", GSF_XML_NO_CONTENT, NULL, NULL),
	     GSF_XML_IN_NODE (GRAD_LIST_ITEM, RGB_COLOR, XL_NS_DRAW, "srgbClr", GSF_XML_2ND, NULL, NULL),
             GSF_XML_IN_NODE (GRAD_LIST_ITEM, SCHEME_COLOR, XL_NS_DRAW, "schemeClr", GSF_XML_2ND, NULL, NULL),
	  GSF_XML_IN_NODE (GRAD_FILL, GRAD_LINE,	XL_NS_DRAW, "lin", GSF_XML_NO_CONTENT, NULL, NULL),

      GSF_XML_IN_NODE (FORMAT_SCHEME, BG_FILL_STYLE_LIST,	XL_NS_DRAW, "bgFillStyleLst", GSF_XML_NO_CONTENT, NULL, NULL),
        GSF_XML_IN_NODE (BG_FILL_STYLE_LIST, GRAD_FILL, XL_NS_DRAW, "gradFill", GSF_XML_2ND, NULL, NULL),
        GSF_XML_IN_NODE (BG_FILL_STYLE_LIST, SOLID_FILL, XL_NS_DRAW, "solidFill", GSF_XML_2ND, NULL, NULL),
      GSF_XML_IN_NODE (FORMAT_SCHEME, LINE_STYLE_LIST,	XL_NS_DRAW, "lnStyleLst", GSF_XML_NO_CONTENT, NULL, NULL),
        GSF_XML_IN_NODE (LINE_STYLE_LIST, LINE_STYLE, XL_NS_DRAW, "ln", GSF_XML_NO_CONTENT, NULL, NULL),
	  GSF_XML_IN_NODE (LINE_STYLE, LN_NOFILL, XL_NS_DRAW, "noFill", GSF_XML_NO_CONTENT, NULL, NULL),
	  GSF_XML_IN_NODE (LINE_STYLE, LN_DASH, XL_NS_DRAW, "prstDash", GSF_XML_NO_CONTENT, NULL, NULL),
	  GSF_XML_IN_NODE (LINE_STYLE, LN_MITER, XL_NS_DRAW, "miter", GSF_XML_NO_CONTENT, NULL, NULL),
	  GSF_XML_IN_NODE (LINE_STYLE, SOLID_FILL, XL_NS_DRAW, "solidFill", GSF_XML_2ND, NULL, NULL),
	  GSF_XML_IN_NODE (LINE_STYLE, FILL_PATT,	XL_NS_DRAW, "pattFill", GSF_XML_NO_CONTENT, NULL, NULL),
	  GSF_XML_IN_NODE (FORMAT_SCHEME, EFFECT_STYLE_LIST,	XL_NS_DRAW, "effectStyleLst", GSF_XML_NO_CONTENT, NULL, NULL),
            GSF_XML_IN_NODE (EFFECT_STYLE_LIST, EFFECT_STYLE,	XL_NS_DRAW, "effectStyle", GSF_XML_NO_CONTENT, NULL, NULL),
	      GSF_XML_IN_NODE (EFFECT_STYLE, EFFECT_PROP, XL_NS_DRAW, "sp3d", GSF_XML_NO_CONTENT, NULL, NULL),
		GSF_XML_IN_NODE (EFFECT_PROP, CONTOUR_CLR, XL_NS_DRAW, "contourClr", GSF_XML_NO_CONTENT, NULL, NULL),
                  GSF_XML_IN_NODE (CONTOUR_CLR, SCHEME_CLR, XL_NS_DRAW, "schemeClr", GSF_XML_NO_CONTENT, NULL, NULL),
		GSF_XML_IN_NODE (EFFECT_PROP, PROP_BEVEL, XL_NS_DRAW, "bevelT", GSF_XML_NO_CONTENT, NULL, NULL),
	      GSF_XML_IN_NODE (EFFECT_STYLE, EFFECT_LIST, XL_NS_DRAW, "effectLst", GSF_XML_NO_CONTENT, NULL, NULL),
		GSF_XML_IN_NODE (EFFECT_LIST, REFLECTION, XL_NS_DRAW, "reflection", GSF_XML_NO_CONTENT, NULL, NULL),
		GSF_XML_IN_NODE (EFFECT_LIST, OUTER_SHADOW, XL_NS_DRAW, "outerShdw", GSF_XML_NO_CONTENT, NULL, NULL),
		  GSF_XML_IN_NODE (OUTER_SHADOW, RGB_COLOR, XL_NS_DRAW, "srgbClr", GSF_XML_2ND, NULL, NULL),
	      GSF_XML_IN_NODE (EFFECT_STYLE, EFFECT_SCENE_3D, XL_NS_DRAW, "scene3d", GSF_XML_NO_CONTENT, NULL, NULL),
		GSF_XML_IN_NODE (EFFECT_SCENE_3D, 3D_CAMERA, XL_NS_DRAW, "camera", GSF_XML_NO_CONTENT, NULL, NULL),
		  GSF_XML_IN_NODE (3D_CAMERA, 3D_ROT, XL_NS_DRAW, "rot", GSF_XML_NO_CONTENT, NULL, NULL),
		GSF_XML_IN_NODE (EFFECT_SCENE_3D, 3D_LIGHT, XL_NS_DRAW, "lightRig", GSF_XML_NO_CONTENT, NULL, NULL),
		  GSF_XML_IN_NODE (3D_LIGHT, 3D_ROT, XL_NS_DRAW, "rot", GSF_XML_NO_CONTENT, NULL, NULL),

  GSF_XML_IN_NODE (THEME, OBJ_DEFAULTS, XL_NS_DRAW, "objectDefaults", GSF_XML_NO_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE (OBJ_DEFAULTS, SP_DEF, XL_NS_DRAW, "spDef", GSF_XML_NO_CONTENT, NULL, NULL),
      GSF_XML_IN_NODE (SP_DEF, SHAPE_PR,  XL_NS_DRAW, "spPr", GSF_XML_NO_CONTENT, NULL, NULL),
        GSF_XML_IN_NODE (SHAPE_PR, XFRM,  XL_NS_DRAW, "xfrm", GSF_XML_NO_CONTENT, NULL, NULL),
        GSF_XML_IN_NODE (SHAPE_PR, CUST_GEOM, XL_NS_DRAW, "custGeom", GSF_XML_NO_CONTENT, NULL, NULL),
        GSF_XML_IN_NODE (SHAPE_PR, EXTLST,  XL_NS_DRAW, "extLst", GSF_XML_NO_CONTENT, NULL, NULL),
          GSF_XML_IN_NODE (EXTLST, EXTITEM, XL_NS_DRAW, "ext", GSF_XML_NO_CONTENT, &xlsx_ext_begin, &xlsx_ext_end),
        GSF_XML_IN_NODE (SHAPE_PR, SOLID_FILL, XL_NS_DRAW, "solidFill", GSF_XML_NO_CONTENT, NULL, NULL),
      GSF_XML_IN_NODE (SP_DEF, BODY_PR,   XL_NS_DRAW, "bodyPr", GSF_XML_NO_CONTENT, NULL, NULL),
      GSF_XML_IN_NODE (SP_DEF, LST_STYLE, XL_NS_DRAW, "lstStyle", GSF_XML_NO_CONTENT, NULL, NULL),
      GSF_XML_IN_NODE (SP_DEF, STYLE, XL_NS_DRAW, "style", GSF_XML_NO_CONTENT, NULL, NULL),
        GSF_XML_IN_NODE (STYLE, EFFECT_REF, XL_NS_DRAW, "effectRef", GSF_XML_NO_CONTENT, NULL, NULL),
          GSF_XML_IN_NODE (EFFECT_REF, HSL_CLR, XL_NS_DRAW, "hslClr", GSF_XML_NO_CONTENT, NULL, NULL),
          GSF_XML_IN_NODE (EFFECT_REF, PRST_CLR, XL_NS_DRAW, "prstClr", GSF_XML_NO_CONTENT, NULL, NULL),
          GSF_XML_IN_NODE (EFFECT_REF, SCHEME_CLR, XL_NS_DRAW, "schemeClr", GSF_XML_NO_CONTENT, NULL, NULL),
          GSF_XML_IN_NODE (EFFECT_REF, SCRGB_CLR, XL_NS_DRAW, "scrgbClr", GSF_XML_NO_CONTENT, NULL, NULL),
          GSF_XML_IN_NODE (EFFECT_REF, SRGB_CLR, XL_NS_DRAW, "srgbClr", GSF_XML_NO_CONTENT, NULL, NULL),
          GSF_XML_IN_NODE (EFFECT_REF, SYS_CLR, XL_NS_DRAW, "sysClr", GSF_XML_NO_CONTENT, NULL, NULL),
        GSF_XML_IN_NODE (STYLE, FILL_REF, XL_NS_DRAW, "fillRef", GSF_XML_NO_CONTENT, NULL, NULL),
          GSF_XML_IN_NODE (FILL_REF, HSL_CLR, XL_NS_DRAW, "hslClr", GSF_XML_2ND, NULL, NULL),
          GSF_XML_IN_NODE (FILL_REF, PRST_CLR, XL_NS_DRAW, "prstClr", GSF_XML_2ND, NULL, NULL),
          GSF_XML_IN_NODE (FILL_REF, SCHEME_CLR, XL_NS_DRAW, "schemeClr", GSF_XML_2ND, NULL, NULL),
          GSF_XML_IN_NODE (FILL_REF, SCRGB_CLR, XL_NS_DRAW, "scrgbClr", GSF_XML_2ND, NULL, NULL),
          GSF_XML_IN_NODE (FILL_REF, SRGB_CLR, XL_NS_DRAW, "srgbClr", GSF_XML_2ND, NULL, NULL),
          GSF_XML_IN_NODE (FILL_REF, SYS_CLR, XL_NS_DRAW, "sysClr", GSF_XML_2ND, NULL, NULL),
        GSF_XML_IN_NODE (STYLE, FONT_REF, XL_NS_DRAW, "fontRef", GSF_XML_NO_CONTENT, NULL, NULL),
          GSF_XML_IN_NODE (FONT_REF, HSL_CLR, XL_NS_DRAW, "hslClr", GSF_XML_2ND, NULL, NULL),
          GSF_XML_IN_NODE (FONT_REF, PRST_CLR, XL_NS_DRAW, "prstClr", GSF_XML_2ND, NULL, NULL),
          GSF_XML_IN_NODE (FONT_REF, SCHEME_CLR, XL_NS_DRAW, "schemeClr", GSF_XML_2ND, NULL, NULL),
          GSF_XML_IN_NODE (FONT_REF, SCRGB_CLR, XL_NS_DRAW, "scrgbClr", GSF_XML_2ND, NULL, NULL),
          GSF_XML_IN_NODE (FONT_REF, SRGB_CLR, XL_NS_DRAW, "srgbClr", GSF_XML_2ND, NULL, NULL),
          GSF_XML_IN_NODE (FONT_REF, SYS_CLR, XL_NS_DRAW, "sysClr", GSF_XML_2ND, NULL, NULL),
        GSF_XML_IN_NODE (STYLE, LN_REF, XL_NS_DRAW, "lnRef", GSF_XML_NO_CONTENT, NULL, NULL),
          GSF_XML_IN_NODE (LN_REF, HSL_CLR, XL_NS_DRAW, "hslClr", GSF_XML_2ND, NULL, NULL),
          GSF_XML_IN_NODE (LN_REF, PRST_CLR, XL_NS_DRAW, "prstClr", GSF_XML_2ND, NULL, NULL),
          GSF_XML_IN_NODE (LN_REF, SCHEME_CLR, XL_NS_DRAW, "schemeClr", GSF_XML_2ND, NULL, NULL),
          GSF_XML_IN_NODE (LN_REF, SCRGB_CLR, XL_NS_DRAW, "scrgbClr", GSF_XML_2ND, NULL, NULL),
          GSF_XML_IN_NODE (LN_REF, SRGB_CLR, XL_NS_DRAW, "srgbClr", GSF_XML_2ND, NULL, NULL),
          GSF_XML_IN_NODE (LN_REF, SYS_CLR, XL_NS_DRAW, "sysClr", GSF_XML_2ND, NULL, NULL),
      GSF_XML_IN_NODE (SP_DEF, EXTLST, XL_NS_SS, "extLst", GSF_XML_2ND, NULL, NULL),
    GSF_XML_IN_NODE (OBJ_DEFAULTS, LN_DEF, XL_NS_DRAW, "lnDef", GSF_XML_NO_CONTENT, NULL, NULL),
      GSF_XML_IN_NODE (LN_DEF, BODY_PR, XL_NS_DRAW, "bodyPr", GSF_XML_NO_CONTENT, NULL, NULL),
      GSF_XML_IN_NODE (LN_DEF, LST_STYLE, XL_NS_DRAW, "lstStyle", GSF_XML_NO_CONTENT, NULL, NULL),
      GSF_XML_IN_NODE (LN_DEF, SP_PR, XL_NS_DRAW, "spPr", GSF_XML_NO_CONTENT, NULL, NULL),
      GSF_XML_IN_NODE (LN_DEF, STYLE, XL_NS_DRAW, "style", GSF_XML_2ND, NULL, NULL),
      GSF_XML_IN_NODE (LN_DEF, EXTLST, XL_NS_SS, "extLst", GSF_XML_2ND, NULL, NULL),
    GSF_XML_IN_NODE (OBJ_DEFAULTS, TX_DEF, XL_NS_DRAW, "txDef", GSF_XML_NO_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE (OBJ_DEFAULTS, EXTLST, XL_NS_SS, "extLst", GSF_XML_2ND, NULL, NULL),
  GSF_XML_IN_NODE (THEME, EXTRA_COLOR_SCHEME, XL_NS_DRAW, "extraClrSchemeLst", GSF_XML_NO_CONTENT, NULL, NULL),

  GSF_XML_IN_NODE (THEME, EXTLST, XL_NS_DRAW, "extLst", GSF_XML_2ND, NULL, NULL),

  GSF_XML_IN_NODE_END
};

/****************************************************************************/

G_MODULE_EXPORT gboolean
xlsx_file_probe (GOFileOpener const *fo, GsfInput *input, GOFileProbeLevel pl);

gboolean
xlsx_file_probe (G_GNUC_UNUSED GOFileOpener const *fo, GsfInput *input, G_GNUC_UNUSED GOFileProbeLevel pl)
{
	GsfInfile *zip;
	GsfInput  *stream;
	gboolean   res = FALSE;

	if (NULL != (zip = gsf_infile_zip_new (input, NULL))) {
		if (NULL != (stream = gsf_infile_child_by_vname (zip, "xl", "workbook.xml", NULL))) {
			g_object_unref (stream);
			res = TRUE;
		}
		g_object_unref (zip);
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

#include "xlsx-read-docprops.c"

G_MODULE_EXPORT void
xlsx_file_open (GOFileOpener const *fo, GOIOContext *context,
		WorkbookView *wb_view, GsfInput *input);

void
xlsx_file_open (G_GNUC_UNUSED GOFileOpener const *fo, GOIOContext *context,
		WorkbookView *wb_view, GsfInput *input)
{
	XLSXReadState	 state;
	GnmLocale       *locale;

	memset (&state, 0, sizeof (XLSXReadState));
	state.version   = ECMA_376_2006;
	state.context	= context;
	state.wb_view	= wb_view;
	state.wb	= wb_view_get_workbook (wb_view);
	state.sheet	= NULL;
	state.run_attrs	= NULL;
	state.rich_attrs = NULL;
	state.sst = g_array_new (FALSE, TRUE, sizeof (XLSXStr));
	state.shared_exprs = g_hash_table_new_full (g_str_hash, g_str_equal,
		(GDestroyNotify)g_free, (GDestroyNotify) gnm_expr_top_unref);
	state.cell_styles = g_hash_table_new_full (g_str_hash, g_str_equal,
		(GDestroyNotify)g_free, (GDestroyNotify) gnm_style_unref);
	state.num_fmts = g_hash_table_new_full (g_str_hash, g_str_equal,
		(GDestroyNotify)g_free, (GDestroyNotify) go_format_unref);
	state.date_fmt = xlsx_pivot_date_fmt ();
	state.convs = xlsx_conventions_new (FALSE);
	state.theme_colors_by_name = g_hash_table_new_full (g_str_hash, g_str_equal,
		(GDestroyNotify)g_free, NULL);
	/* fill in some default colors (when theme is absent */
	g_hash_table_replace (state.theme_colors_by_name, g_strdup ("lt1"), GUINT_TO_POINTER (GO_COLOR_WHITE));
	g_hash_table_replace (state.theme_colors_by_name, g_strdup ("dk1"), GUINT_TO_POINTER (GO_COLOR_BLACK));
	state.pivot.cache_by_id = g_hash_table_new_full (g_str_hash, g_str_equal,
		(GDestroyNotify)g_free, (GDestroyNotify) g_object_unref);
	state.zorder = g_hash_table_new (g_direct_hash, g_direct_equal);

	locale = gnm_push_C_locale ();

	if (NULL != (state.zip = gsf_infile_zip_new (input, NULL))) {
		/* optional */
		GsfInput *wb_part = gsf_open_pkg_open_rel_by_type (GSF_INPUT (state.zip),
			"http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument", NULL);

		if (NULL != wb_part) {
			GsfInput *in;

			in = gsf_open_pkg_open_rel_by_type (wb_part,
				"http://schemas.openxmlformats.org/officeDocument/2006/relationships/sharedStrings", NULL);
			if (in != NULL) {
				start_update_progress (&state, in, _("Reading shared strings..."),
						       0., 0.05);
				xlsx_parse_stream (&state, in, xlsx_shared_strings_dtd);
				end_update_progress (&state);
			}

			in = gsf_open_pkg_open_rel_by_type (wb_part,
				"http://schemas.openxmlformats.org/officeDocument/2006/relationships/theme", NULL);
			if (in) {
				start_update_progress (&state, in, _("Reading theme..."),
						       0.05, 0.1);
				xlsx_parse_stream (&state, in, xlsx_theme_dtd);
				end_update_progress (&state);
			}

			in = gsf_open_pkg_open_rel_by_type (wb_part,
				"http://schemas.openxmlformats.org/officeDocument/2006/relationships/styles", NULL);
			if (in != NULL) {
				start_update_progress (&state, in, _("Reading styles..."), 0.1, 0.2);
				xlsx_parse_stream (&state, in, xlsx_styles_dtd);
				end_update_progress (&state);
			}

			start_update_progress (&state, wb_part, _("Reading workbook..."),
					       0.2, 0.3);
			xlsx_parse_stream (&state, wb_part, xlsx_workbook_dtd);
			/* end_update_progress (&state);  moved into xlsx_wb_end */
			/* MW 20111121: why?  If parsing fails, I don't think that will ever be called.  */

			xlsx_read_docprops (&state);

		} else
			go_cmd_context_error_import (GO_CMD_CONTEXT (context),
				_("No workbook stream found."));

		g_object_unref (state.zip);
	}

	gnm_pop_C_locale (locale);

	if (NULL != state.sst) {
		unsigned i = state.sst->len;
		XLSXStr *entry;
		while (i-- > 0) {
			entry = &g_array_index (state.sst, XLSXStr, i);
			go_string_unref (entry->str);
			go_format_unref (entry->markup);
		}
		g_array_free (state.sst, TRUE);
	}
	if (state.r_text) g_string_free (state.r_text, TRUE);
	if (state.rich_attrs) pango_attr_list_unref (state.rich_attrs);
	if (state.run_attrs) pango_attr_list_unref (state.run_attrs);
	g_hash_table_destroy (state.pivot.cache_by_id);
	xlsx_conventions_free (state.convs);
	go_format_unref (state.date_fmt);
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
	g_hash_table_destroy (state.theme_colors_by_name);
	g_hash_table_destroy (state.zorder);
	value_release (state.val);
	if (state.texpr) gnm_expr_top_unref (state.texpr);
	if (state.comment) g_object_unref (state.comment);
	if (state.cur_style) g_object_unref (state.cur_style);
	if (state.style_accum) gnm_style_unref (state.style_accum);
	if (state.pending_rowcol_style) gnm_style_unref (state.pending_rowcol_style);
	style_color_unref (state.border_color);

	workbook_set_saveinfo (state.wb, GO_FILE_FL_AUTO,
			       go_file_saver_for_id ((state.version == ECMA_376_2006) ?
						     "Gnumeric_Excel:xlsx" :
						     "Gnumeric_Excel:xlsx2"));
}

/* TODO * TODO * TODO
 *
 * IMPROVE
 *	- column widths : Don't use hard coded font size
 *	- share colours
 *	- conditional formats
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
