/**
 * ms-chart.c: MS Excel chart support for Gnumeric
 *
 * Author:
 *    Jody Goldberg (jody@gnome.org)
 *
 * (C) 1999-2007 Jody Goldberg
 **/

#include <gnumeric-config.h>
#include <gnumeric.h>
#include "boot.h"
#include "excel.h"
#include "ms-chart.h"
#include "ms-formula-read.h"
#include "ms-excel-read.h"
#include "ms-excel-write.h"
#include "ms-escher.h"
#include "ms-formula-write.h"
#include <compilation.h>

#include <parse-util.h>
#include <gnm-format.h>
#include <expr.h>
#include <value.h>
#include <gutils.h>
#include <graph.h>
#include <style-color.h>
#include <sheet-object-graph.h>
#include <workbook-view.h>

#include <goffice/goffice.h>

#include <gsf/gsf-utils.h>
#include <math.h>
#include <string.h>

/* #define NO_DEBUG_EXCEL */
#ifndef NO_DEBUG_EXCEL
#define d(level, code)	do { if (ms_excel_chart_debug > level) { code } } while (0)
#else
#define d(level, code)
#endif

#define reg_show_R2 err_teetop
#define reg_order err_num
#define reg_intercept err_val
#define reg_parent err_parent

typedef struct {
	struct {
		int num_elements;
		GOData *data;
		GnmValueArray *value;
	} data [GOG_MS_DIM_TYPES];
	GogSeries *series;
	int err_type;
	int reg_type;
	int err_num; /* also used for order in reg curves */
	int err_src;
	int err_parent; /* also used for parent in reg curves */
	double err_val; /* also used for intercept in reg curves */
	double reg_backcast, reg_forecast;
	double reg_min, reg_max;
	GOData *reg_dims[2];
	gboolean err_teetop; /* also used for show R-squared in reg curves */
	gboolean reg_show_eq;
	gboolean reg_skip_invalid;
	GogMSDimType extra_dim;
	int chart_group;
	gboolean  has_legend;
	GOStyle *style;
	GHashTable *singletons;
	GOLineInterpolation interpolation;
} XLChartSeries;

typedef struct {
	MSContainer	 container;

	gboolean        error;
	GArray		*stack;
	MsBiffVersion	 ver;
	guint32		 prev_opcode;

	SheetObject	*sog;
	GogGraph	*graph;
	GogChart	*chart;
	GogObject	*legend;
	GogPlot		*plot;
	GogObject	*label;
	GOStyle		*default_plot_style;
	GogObject	*axis, *xaxis;
	guint8		axislineflags;
	GOStyle		*style;
	GOStyle		*chartline_style[3];
	GOStyle		*dropbar_style;

	int		 style_element;
	int		 cur_role;
	gboolean	 hilo;
	gboolean	 dropbar;
	guint16		 dropbar_width;

	gboolean	 frame_for_grid;
	gboolean	 has_a_grid;
	gboolean	 is_surface;
	gboolean	 is_contour;
	gboolean	 has_extra_dataformat;
	gboolean	axis_cross_at_max;
	double		axis_cross_value;
	int		 plot_counter;
	XLChartSeries	*currentSeries;
	GPtrArray	*series;
	char		*text;
	guint16		parent_index;
	GOLineInterpolation interpolation;
} XLChartReadState;

typedef struct _XLChartHandler	XLChartHandler;
typedef gboolean (*XLChartReader) (XLChartHandler const *handle,
				   XLChartReadState *, BiffQuery *q);
struct _XLChartHandler {
	guint16 const opcode;
	guint16 const min_size; /* Minimum across all versions.  */
	char const *const name;
	XLChartReader const read_fn;
};

#define BC(n)	xl_chart_ ## n
#define BC_R(n)	BC(read_ ## n)

static gboolean
check_style (GOStyle *style, char const *object)
{
	if (style == NULL) {
		g_warning ("File is most likely corrupted.\n"
			   "(%s has no associated style.)",
			   object);
		return FALSE;
	}
	return TRUE;
}

static inline MsBiffVersion
BC_R (ver) (XLChartReadState const *s)
{
	return s->container.importer->ver;
}

static XLChartSeries *
excel_chart_series_new (void)
{
	XLChartSeries *series;

	series = g_new0 (XLChartSeries, 1);
	series->chart_group = -1;
	series->has_legend = TRUE;

	return series;
}

static void
excel_chart_series_delete (XLChartSeries *series)
{
	int i;

	for (i = GOG_MS_DIM_TYPES; i-- > 0 ; ) {
		if (series->data [i].data != NULL)
			g_object_unref (series->data[i].data);
		if (series->data [i].value != NULL)
			value_release ((GnmValue *)(series->data[i].value));
	}
	if (series->style != NULL)
		g_object_unref (series->style);
	if (series->singletons != NULL)
		g_hash_table_destroy (series->singletons);
	g_free (series);
}

static void
BC_R(get_style) (XLChartReadState *s)
{
	if (s->style == NULL)
		s->style = (GOStyle *) gog_style_new ();
}

static int
BC_R(top_state) (XLChartReadState *s, unsigned n)
{
	g_return_val_if_fail (s != NULL, 0);
	XL_CHECK_CONDITION_VAL (s->stack->len >= n+1, 0);
	return g_array_index (s->stack, int, s->stack->len-1-n);
}

static GOColor
BC_R(color) (guint8 const *data, char const *type)
{
	guint32 const bgr = GSF_LE_GET_GUINT32 (data);
	guint16 const r = (bgr >>  0) & 0xff;
	guint16 const g = (bgr >>  8) & 0xff;
	guint16 const b = (bgr >> 16) & 0xff;

	d (1, g_printerr ("%s %02x:%02x:%02x;\n", type, r, g, b););
	return GO_COLOR_FROM_RGB (r, g, b);
}

/****************************************************************************/

static gboolean
BC_R(3dbarshape)(XLChartHandler const *handle,
		 XLChartReadState *s, BiffQuery *q)
{
	XL_CHECK_CONDITION_VAL (q->length >= 2, TRUE);
	d (0, {
		guint16 const type = GSF_LE_GET_GUINT16 (q->data);
		switch (type) {
		case 0 : g_printerr ("box"); break;
		case 1 : g_printerr ("cylinder"); break;
		case 256 : g_printerr ("pyramid"); break;
		case 257 : g_printerr ("cone"); break;
		default :
			   g_printerr ("unknown 3dshape %d\n", type);
		}
	});

	return FALSE;
}
/****************************************************************************/

static gboolean
BC_R(3d)(XLChartHandler const *handle,
	 XLChartReadState *s, BiffQuery *q)
{
	guint16 rotation, elevation, distance, height, depth, gap;
	guint8 flags, zero;
	gboolean use_perspective, cluster, auto_scale, walls_2d;

	XL_CHECK_CONDITION_VAL (q->length >= 14, TRUE);
	rotation = GSF_LE_GET_GUINT16 (q->data);	/* 0-360 */
	elevation = GSF_LE_GET_GUINT16 (q->data+2);	/* -90 - 90 */
	distance = GSF_LE_GET_GUINT16 (q->data+4);	/* 0 - 100 */
	height = GSF_LE_GET_GUINT16 (q->data+6);
	depth = GSF_LE_GET_GUINT16 (q->data+8);
	gap = GSF_LE_GET_GUINT16 (q->data+10);
	flags = GSF_LE_GET_GUINT8 (q->data+12);
	zero = GSF_LE_GET_GUINT8 (q->data+13);

	use_perspective = (flags&0x01) ? TRUE :FALSE;
	cluster = (flags&0x02) ? TRUE :FALSE;
	auto_scale = (flags&0x04) ? TRUE :FALSE;
	walls_2d = (flags&0x20) ? TRUE :FALSE;

	g_return_val_if_fail (zero == 0, FALSE); /* just warn for now */

	if (s->plot == NULL && s->is_surface) {
		s->is_contour = elevation == 90 && distance == 0;
		if (s->chart != NULL && !s->is_contour) {
			GogObject *box = gog_object_get_child_by_name (GOG_OBJECT (s->chart), "3D-Box");
			if (!box)
				box = gog_object_add_by_name (GOG_OBJECT (s->chart), "3D-Box", NULL);
			g_object_set (G_OBJECT (box), "theta", ((elevation > 0)? elevation: -elevation), NULL);
			/* FIXME: use other parameters */
		}
	}
	/* at this point, we don't know if data can be converted to a
	gnumeric matrix, so we cannot create the plot here. */


	d (1, {
		g_printerr ("Rot = %hu\n", rotation);
		g_printerr ("Elev = %hu\n", elevation);
		g_printerr ("Dist = %hu\n", distance);
		g_printerr ("Height = %hu\n", height);
		g_printerr ("Depth = %hu\n", depth);
		g_printerr ("Gap = %hu\n", gap);

		if (use_perspective)
			g_printerr ("Use perspective;\n");
		if (cluster)
			g_printerr ("Cluster;\n");
		if (auto_scale)
			g_printerr ("Auto Scale;\n");
		if (walls_2d)
			g_printerr ("2D Walls;\n");
	});

	return FALSE;
}

/****************************************************************************/

static gboolean
BC_R(ai)(XLChartHandler const *handle,
	 XLChartReadState *s, BiffQuery *q)
{
	guint8 purpose, ref_type, flags, length;
	int top_state;

	XL_CHECK_CONDITION_VAL (q->length >= 8, TRUE);

	purpose = GSF_LE_GET_GUINT8 (q->data);
	ref_type = GSF_LE_GET_GUINT8 (q->data + 1);
	flags = GSF_LE_GET_GUINT16 (q->data + 2);
	length = GSF_LE_GET_GUINT16 (q->data + 6);

	top_state = BC_R(top_state) (s, 0);

	XL_CHECK_CONDITION_VAL (q->length - 8 >= length, TRUE);

	/* ignore these for now */
	if (top_state == BIFF_CHART_text) {
		GnmExprTop const *texpr;
		g_return_val_if_fail (s->label == NULL, FALSE);
		s->label = g_object_new (GOG_TYPE_LABEL, NULL);
		texpr = ms_container_parse_expr (&s->container,
						 q->data+8, length);
		if (texpr != NULL) {
			Sheet *sheet = ms_container_sheet (s->container.parent);
			GOData *data = gnm_go_data_scalar_new_expr (sheet, texpr);

			XL_CHECK_CONDITION_VAL (sheet &&
						s->label,
						(gnm_expr_top_unref (texpr), TRUE));
			gog_dataset_set_dim (GOG_DATASET (s->label), 0, data, NULL);
		}
		return FALSE;
	}
	else if (top_state == BIFF_CHART_trendlimits) {
	}

	/* Rest are 0 */
	if (flags&0x01) {
		GOFormat *fmt = ms_container_get_fmt (&s->container,
			GSF_LE_GET_GUINT16 (q->data + 4));
		d (2, g_printerr ("Has Custom number format;\n"););
		if (fmt != NULL) {
			const char *desc = go_format_as_XL (fmt);
			d (2, g_printerr ("Format = '%s';\n", desc););

			go_format_unref (fmt);
		}
	} else {
		d (2, g_printerr ("Uses number format from data source;\n"););
	}

	g_return_val_if_fail (purpose < GOG_MS_DIM_TYPES, TRUE);
	d (0, {
	switch (purpose) {
	case GOG_MS_DIM_LABELS :     g_printerr ("Labels;\n"); break;
	case GOG_MS_DIM_VALUES :     g_printerr ("Values;\n"); break;
	case GOG_MS_DIM_CATEGORIES : g_printerr ("Categories;\n"); break;
	case GOG_MS_DIM_BUBBLES :    g_printerr ("Bubbles;\n"); break;
	default :
		g_printerr ("UKNOWN : purpose (%x)\n", purpose);
	}
	switch (ref_type) {
	case 0 : g_printerr ("Use default categories;\n"); break;
	case 1 : g_printerr ("Text/Value entered directly;\n"); g_printerr ("data length = %d\n",length); break;
	case 2 : g_printerr ("Linked to Container;\n"); break;
	case 4 : g_printerr ("'Error reported' what the heck is this ??;\n"); break;
	default :
		 g_printerr ("UKNOWN : reference type (%x)\n", ref_type);
	}
	});

	/* (2) == linked to container */
	if (ref_type == 2) {
		GnmExprTop const *texpr =
			ms_container_parse_expr (&s->container,
						 q->data+8, length);
		if (texpr != NULL) {
			Sheet *sheet = ms_container_sheet (s->container.parent);

			if (sheet && s->currentSeries)
				s->currentSeries->data [purpose].data = (purpose == GOG_MS_DIM_LABELS)
					? gnm_go_data_scalar_new_expr (sheet, texpr)
					: gnm_go_data_vector_new_expr (sheet, texpr);
			else {
				gnm_expr_top_unref (texpr);
				g_return_val_if_fail (sheet != NULL, FALSE);
				g_return_val_if_fail (s->currentSeries != NULL, TRUE);
			}
		}
	} else if (ref_type == 1 && purpose != GOG_MS_DIM_LABELS &&
		   s->currentSeries &&
		   s->currentSeries->data [purpose].num_elements > 0) {
		if (s->currentSeries->data [purpose].value)
			g_warning ("Leak?");

		s->currentSeries->data[purpose].value = (GnmValueArray *)
			value_new_array (1, s->currentSeries->data [purpose].num_elements);
	} else {
		g_return_val_if_fail (length == 0, TRUE);
	}

	return FALSE;
}

/****************************************************************************/

static gboolean
BC_R(alruns)(XLChartHandler const *handle,
	     XLChartReadState *s, BiffQuery *q)
{
	XL_CHECK_CONDITION_VAL (q->length >= 4, TRUE);
#if 0
	int length = GSF_LE_GET_GUINT16 (q->data);
	guint8 const *in = (q->data + 2);
	char *conans = g_new (char, length + 2);
	char *out = ans;

	for (; --length >= 0 ; in+=4, ++out) {
		/*
		 * FIXME FIXME FIXME :
		 *        - don't toss font info
		 *        - Merge streams of the same font together.
		 *        - Combine with RTF support once merged
		 */
		guint32 const elem = GSF_LE_GET_GUINT32 (in);
		*out = (char)((elem >> 16) & 0xff);
	}
	*out = '\0';

	/*g_printerr (ans);*/
#endif
	return FALSE;
}

/****************************************************************************/

static gboolean
BC_R(area)(XLChartHandler const *handle,
	   XLChartReadState *s, BiffQuery *q)
{
	guint16 flags;
	char const *type = "normal";
	gboolean in_3d;

	XL_CHECK_CONDITION_VAL (q->length >= 2, TRUE);

	flags = GSF_LE_GET_GUINT16 (q->data);
	in_3d = (BC_R(ver)(s) >= MS_BIFF_V8 && (flags & 0x04));

	g_return_val_if_fail (s->plot == NULL, TRUE);
	s->plot = (GogPlot*) gog_plot_new_by_name ("GogAreaPlot");
	g_return_val_if_fail (s->plot != NULL, TRUE);

	if (flags & 0x02)
		type = "as_percentage";
	else if (flags & 0x01)
		type = "stacked";

	g_object_set (G_OBJECT (s->plot),
		"type",			type,
		"in-3d",		in_3d,
		NULL);

	d(1, g_printerr ("%s area;", type););
	return FALSE;
}

/****************************************************************************/

static gboolean
BC_R(areaformat)(XLChartHandler const *handle,
		 XLChartReadState *s, BiffQuery *q)
{
	guint16 pattern, flags;
	gboolean auto_format, invert_if_negative;

	XL_CHECK_CONDITION_VAL (q->length >= 12, TRUE);

	pattern = GSF_LE_GET_GUINT16 (q->data+8);
	flags = GSF_LE_GET_GUINT16 (q->data+10);
	auto_format = (flags & 0x01) ? TRUE : FALSE;
	invert_if_negative = flags & 0x02;

	d (0, {
		g_printerr ("pattern = %d;\n", pattern);
		if (auto_format)
			g_printerr ("Use auto format;\n");
		if (invert_if_negative)
			g_printerr ("Swap fore and back colours when displaying negatives;\n");
	});

#if 0
	/* 18 */ "5%"
#endif
	BC_R(get_style) (s);
	if (pattern > 0) {
		s->style->fill.type = GO_STYLE_FILL_PATTERN;
		s->style->fill.invert_if_negative = invert_if_negative;
		s->style->fill.pattern.pattern = pattern - 1;
		s->style->fill.pattern.fore = BC_R(color) (q->data+0, "AreaFore");
		s->style->fill.pattern.back = BC_R(color) (q->data+4, "AreaBack");
		if (s->style->fill.pattern.pattern == 0) {
			GOColor tmp = s->style->fill.pattern.fore;
			s->style->fill.pattern.fore = s->style->fill.pattern.back;
			s->style->fill.pattern.back = tmp;
			s->style->fill.auto_fore = auto_format;
			s->style->fill.auto_back = FALSE;
		} else {
			s->style->fill.auto_fore = FALSE;
			s->style->fill.auto_back = auto_format;
		}
	} else if (auto_format) {
		s->style->fill.type = GO_STYLE_FILL_PATTERN;
		s->style->fill.auto_back = TRUE;
		s->style->fill.invert_if_negative = invert_if_negative;
		s->style->fill.pattern.pattern = 0;
		s->style->fill.pattern.fore =
		s->style->fill.pattern.back = 0;
	} else {
		s->style->fill.type = GO_STYLE_FILL_NONE;
		s->style->fill.auto_type = FALSE;
	}

	return FALSE;
}

/****************************************************************************/

static gboolean
BC_R(attachedlabel)(XLChartHandler const *handle,
		    XLChartReadState *s, BiffQuery *q)
{
	XL_CHECK_CONDITION_VAL (q->length >= 2, TRUE);
	d (3,{
	guint16 const flags = GSF_LE_GET_GUINT16 (q->data);
	gboolean const show_value = (flags&0x01) ? TRUE : FALSE;
	gboolean const show_percent = (flags&0x02) ? TRUE : FALSE;
	gboolean const show_label_percent = (flags&0x04) ? TRUE : FALSE;
	gboolean const smooth_line = (flags&0x08) ? TRUE : FALSE;
	gboolean const show_label = (flags&0x10) ? TRUE : FALSE;

	if (show_value)
		g_printerr ("Show Value;\n");
	if (show_percent)
		g_printerr ("Show as Percentage;\n");
	if (show_label_percent)
		g_printerr ("Show as Label Percentage;\n");
	if (smooth_line)
		g_printerr ("Smooth line;\n");
	if (show_label)
		g_printerr ("Show the label;\n");

	if (BC_R(ver)(s) >= MS_BIFF_V8) {
		gboolean const show_bubble_size = (flags&0x20) ? TRUE : FALSE;
		if (show_bubble_size)
			g_printerr ("Show bubble size;\n");
	}
	});
	return FALSE;
}

/****************************************************************************/

static gboolean
BC_R(axesused)(XLChartHandler const *handle,
	       XLChartReadState *s, BiffQuery *q)
{
	guint16 num_axis;
	XL_CHECK_CONDITION_VAL (q->length >= 2, TRUE);
	num_axis = GSF_LE_GET_GUINT16 (q->data);
	XL_CHECK_CONDITION_VAL(1 <= num_axis && num_axis <= 2, TRUE);
	d (0, g_printerr ("There are %hu axis.\n", num_axis););
	return FALSE;
}

/****************************************************************************/

static gboolean
BC_R(axis)(XLChartHandler const *handle,
	   XLChartReadState *s, BiffQuery *q)
{
	/* only true _most_ of the time.
	 * This is 'value axis' and 'catagory axis'
	 * which are logical names for X and Y that transpose for bar plots */
	static char const *const ms_axis[] = {
		"X-Axis", "Y-Axis", "Z-Axis"
	};

	guint16 axis_type;

	XL_CHECK_CONDITION_VAL (q->length >= 2, TRUE);

	axis_type = GSF_LE_GET_GUINT16 (q->data);

	g_return_val_if_fail (axis_type < G_N_ELEMENTS (ms_axis), TRUE);
	g_return_val_if_fail (s->axis == NULL, TRUE);

	s->axis = gog_object_add_by_name (GOG_OBJECT (s->chart),
					  ms_axis [axis_type], NULL);

	if (axis_type == 0)
		s->xaxis = s->axis;
	else if (axis_type == 1) {
		if (s->axis_cross_at_max) {
			g_object_set (s->axis,
				      "pos-str", "high",
				      "cross-axis-id", gog_object_get_id (GOG_OBJECT (s->xaxis)),
				      NULL);
			s->axis_cross_at_max = FALSE;
		} else if (!isnan (s->axis_cross_value)) {
			GnmValue *value = value_new_float (s->axis_cross_value);
			GnmExprTop const *texpr = gnm_expr_top_new_constant (value);
			g_object_set (s->axis,
				      "pos-str", "cross",
				      "cross-axis-id", gog_object_get_id (GOG_OBJECT (s->xaxis)),
				      NULL);
			gog_dataset_set_dim (GOG_DATASET (s->axis), GOG_AXIS_ELEM_CROSS_POINT,
				gnm_go_data_scalar_new_expr (ms_container_sheet (s->container.parent), texpr), NULL);
			s->axis_cross_value = go_nan;
		}
	}

	d (0, g_printerr ("This is a %s .\n", ms_axis[axis_type]););
	return FALSE;
}

/****************************************************************************/

static gboolean
BC_R(axcext)(XLChartHandler const *handle,
	     XLChartReadState *s, BiffQuery *q)
{
	return FALSE;
}

/****************************************************************************/

static gboolean
BC_R(lineformat)(XLChartHandler const *handle,
		 XLChartReadState *s, BiffQuery *q);
static gboolean
BC_R(axislineformat)(XLChartHandler const *handle,
		     XLChartReadState *s, BiffQuery *q)
{
	guint16 opcode;
	guint16 type;

	XL_CHECK_CONDITION_VAL (q->length >= 2, FALSE);
	type = GSF_LE_GET_GUINT16 (q->data);

	d (0, {
	g_printerr ("Axisline is ");
	switch (type)
	{
	case 0 : g_printerr ("the axis line.\n"); break;
	case 1 : g_printerr ("a major grid along the axis.\n"); break;
	case 2 : g_printerr ("a minor grid along the axis.\n"); break;

	/* TODO TODO : floor vs wall */
	case 3 : g_printerr ("a floor/wall along the axis.\n"); break;
	default : g_printerr ("an ERROR.  unknown type (%x).\n", type);
	}
	});

	if (!ms_biff_query_peek_next (q, &opcode) || opcode != BIFF_CHART_lineformat) {
		g_warning ("I had hoped that a lineformat would always follow an axislineformat");
		return FALSE;
	}
	ms_biff_query_next (q);
	if (BC_R(lineformat)(handle, s, q))
		return TRUE;

	if (s->axis != NULL)
		switch (type) {
		case 0:
			g_object_set (G_OBJECT (s->axis),
				"style", s->style,
				NULL);
			if (s->axislineflags == 8)
				g_object_set (s->axis, "invisible", TRUE, NULL);
			else if (q->length >= 10) {
				/* deleted axis sets flag here, rather than in TICK */
				if (0 == (0x4 & GSF_LE_GET_GUINT16 (q->data+8)))
					g_object_set (G_OBJECT (s->axis),
						"major-tick-labeled",	FALSE,
						NULL);
			}
			break;
		case 1: {
			GogObject *GridLine = GOG_OBJECT (g_object_new (GOG_TYPE_GRID_LINE,
							NULL));
			gog_object_add_by_name (GOG_OBJECT (s->axis), "MajorGrid", GridLine);
			if (check_style (s->style, "axis major grid"))
				go_styled_object_set_style (GO_STYLED_OBJECT (GridLine), s->style);
			break;
		}
		case 2: {
			GogObject *GridLine = GOG_OBJECT (g_object_new (GOG_TYPE_GRID_LINE,
							NULL));
			gog_object_add_by_name (GOG_OBJECT (s->axis), "MinorGrid", GridLine);
			if (check_style (s->style, "axis minor grid"))
				go_styled_object_set_style (GO_STYLED_OBJECT (GridLine), s->style);
			break;
		}
		case 3: {
			/* in that case, we have an areaformat too */
			ms_biff_query_next (q);
			if (BC_R(areaformat)(handle, s, q))
				return TRUE;
			break;
		}
		default:
			break;
		}
	if (s->style) {
		g_object_unref (s->style);
		s->style = NULL;
	}

	return FALSE;
}

/****************************************************************************/

static gboolean
BC_R(axisparent)(XLChartHandler const *handle,
		 XLChartReadState *s, BiffQuery *q)
{
	XL_CHECK_CONDITION_VAL (q->length == 18, TRUE);
	/* hmm, what we do here is not conform to the documentation, anyway, we don't do anything useful */
	d (1, {
	guint16 const index = GSF_LE_GET_GUINT16 (q->data);	/* 1 or 2 */
	/* Measured in 1/4000ths of the chart width */
	guint32 const x = GSF_LE_GET_GUINT32 (q->data+2);
	guint32 const y = GSF_LE_GET_GUINT32 (q->data+6);
	guint32 const width = GSF_LE_GET_GUINT32 (q->data+10);
	guint32 const height = GSF_LE_GET_GUINT32 (q->data+14);

	g_printerr ("Axis # %hu @ %f,%f, X=%f, Y=%f\n",
		index, x/4000., y/4000., width/4000., height/4000.);
	});
	return FALSE;
}

/****************************************************************************/

static gboolean
BC_R(bar)(XLChartHandler const *handle,
	  XLChartReadState *s, BiffQuery *q)
{
	char const *type = "normal";
	int overlap_percentage, gap_percentage;
	guint16 flags;
	gboolean horizontal, in_3d;

	XL_CHECK_CONDITION_VAL (q->length >= 6, TRUE);

	overlap_percentage = -GSF_LE_GET_GINT16 (q->data); /* dipsticks */
	gap_percentage = GSF_LE_GET_GINT16 (q->data+2);
	flags = GSF_LE_GET_GUINT16 (q->data+4);
	horizontal = (flags & 0x01) != 0;
	in_3d = (BC_R(ver)(s) >= MS_BIFF_V8 && (flags & 0x08));

	g_return_val_if_fail (s->plot == NULL, TRUE);
	s->plot = (GogPlot*) gog_plot_new_by_name ("GogBarColPlot");
	g_return_val_if_fail (s->plot != NULL, TRUE);

	if (flags & 0x04)
		type = "as_percentage";
	else if (flags & 0x02)
		type = "stacked";

	g_object_set (G_OBJECT (s->plot),
		"horizontal",		horizontal,
		"type",			type,
		"in-3d",		in_3d,
		"overlap-percentage",	overlap_percentage,
		"gap-percentage",	gap_percentage,
		NULL);
	d(1, g_printerr ("%s bar with gap = %d, overlap = %d;",
		      type, gap_percentage, overlap_percentage););
	return FALSE;
}

/****************************************************************************/

static gboolean
BC_R(begin)(XLChartHandler const *handle,
	    XLChartReadState *s, BiffQuery *q)
{
	d(0, g_printerr ("{\n"););
	s->stack = g_array_append_val (s->stack, s->prev_opcode);
	return FALSE;
}

/****************************************************************************/

static gboolean
BC_R(boppop)(XLChartHandler const *handle,
	     XLChartReadState *s, BiffQuery *q)
{
	XL_CHECK_CONDITION_VAL (q->length >= 18, TRUE);

#if 0
	guint8 const type = GSF_LE_GET_GUINT8 (q->data); /* 0-2 */
	gboolean const use_default_split = (GSF_LE_GET_GUINT8 (q->data+1) == 1);
	guint16 const split_type = GSF_LE_GET_GUINT8 (q->data+2); /* 0-3 */
#endif

	/* KLUDGE : call it a pie for now */
	if (NULL == s->plot) {
		gboolean const in_3d = (GSF_LE_GET_GUINT16 (q->data+16) == 1);
		s->plot = (GogPlot*) gog_plot_new_by_name ("GogPiePlot");
		g_return_val_if_fail (s->plot != NULL, TRUE);
		g_object_set (G_OBJECT (s->plot), "in-3d", in_3d, NULL);
	}

	return FALSE;
}
/****************************************************************************/

static gboolean
BC_R(boppopcustom)(XLChartHandler const *handle,
		   XLChartReadState *s, BiffQuery *q)
{
#if 0
	gint16 const count = GSF_LE_GET_GUINT16 (q->data);
	/* TODO TODO : figure out the bitfield array */
#endif
	return FALSE;
}

/***************************************************************************/

static gboolean
BC_R(catserrange)(XLChartHandler const *handle,
		  XLChartReadState *s, BiffQuery *q)
{
	guint flags;
	XL_CHECK_CONDITION_VAL (q->length >= 8, TRUE);
	flags = GSF_LE_GET_GUINT16 (q->data + 6);
	if (((flags & 2) != 0) ^ ((flags & 4) != 0)) {
		if (gog_axis_get_atype (GOG_AXIS (s->axis)) == GOG_AXIS_X)
			s->axis_cross_at_max = TRUE;
		else if (gog_axis_get_atype (GOG_AXIS (s->axis)) == GOG_AXIS_Y && s->xaxis)
			g_object_set (s->xaxis, "pos-str", "high", NULL);
		d (1, g_printerr ("Cross over at max value;\n"););
	}
	return FALSE;
}

/****************************************************************************/

static gboolean
BC_R(chart)(XLChartHandler const *handle,
	    XLChartReadState *s, BiffQuery *q)
{
	XL_CHECK_CONDITION_VAL (q->length >= 16, TRUE);

	d (1, {
	/* Fixed point 2 bytes fraction 2 bytes integer */
	guint32 const x_pos_fixed = GSF_LE_GET_GUINT32 (q->data + 0);
	guint32 const y_pos_fixed = GSF_LE_GET_GUINT32 (q->data + 4);
	guint32 const x_size_fixed = GSF_LE_GET_GUINT32 (q->data + 8);
	guint32 const y_size_fixed = GSF_LE_GET_GUINT32 (q->data + 12);

	/* Measured in points (1/72 of an inch) */
	double const x_pos = x_pos_fixed / (65535. * 72.);
	double const y_pos = y_pos_fixed / (65535. * 72.);
	double const x_size = x_size_fixed / (65535. * 72.);
	double const y_size = y_size_fixed / (65535. * 72.);
	g_printerr ("Chart @ %g, %g is %g\" x %g\"\n", x_pos, y_pos, x_size, y_size);
	});

	return FALSE;
}

/****************************************************************************/

static gboolean
BC_R(chartformat)(XLChartHandler const *handle,
		  XLChartReadState *s, BiffQuery *q)
{
	guint16 flags, z_order;

	XL_CHECK_CONDITION_VAL (q->length >= 4, TRUE);

	flags = GSF_LE_GET_GUINT16 (q->data+16);
	z_order = GSF_LE_GET_GUINT16 (q->data+18);

	/* always update the counter to keep the index in line with the chart
	 * group specifier for series */
	s->plot_counter = z_order;

	if (s->plot != NULL)
		g_object_set (G_OBJECT (s->plot),
			      "vary-style-by-element", (flags & 0x01) ? TRUE : FALSE,
#if 0
			      "index", s->plot_counter
			      "stacking-position", z_order
#endif
			      NULL);

	d (0, {
		g_printerr ("Z value = %uh\n", z_order);
	});

	return FALSE;
}

/****************************************************************************/

static gboolean
BC_R(chartformatlink)(XLChartHandler const *handle,
		      XLChartReadState *s, BiffQuery *q)
{
	/* ignored */
	return FALSE;
}

/****************************************************************************/

static gboolean
BC_R(chartline)(XLChartHandler const *handle,
		XLChartReadState *s, BiffQuery *q)
{
	guint16 type;

	XL_CHECK_CONDITION_VAL (q->length >= 2, TRUE);
	type = GSF_LE_GET_GUINT16 (q->data);
	XL_CHECK_CONDITION_VAL (type <= 2, FALSE);

	if (type == 1)
		s->hilo = TRUE;
	s->cur_role = type;

	d (0, g_printerr ("Use %s lines\n",
	     (type == 0) ? "drop" : ((type == 1) ? "hi-lo" : "series")););

	return FALSE;
}

/****************************************************************************/

static gboolean
BC_R(clrtclient)(XLChartHandler const *handle,
		 XLChartReadState *s, BiffQuery *q)
{
	g_printerr ("Undocumented BIFF : clrtclient");
	ms_biff_query_dump (q);
	return FALSE;
}
/****************************************************************************/

static gboolean
BC_R(dat)(XLChartHandler const *handle,
	  XLChartReadState *s, BiffQuery *q)
{
	XL_CHECK_CONDITION_VAL (q->length >= 2, TRUE);

#if 0
	gint16 const flags = GSF_LE_GET_GUINT16 (q->data);
	gboolean const horiz_border = (flags&0x01) ? TRUE : FALSE;
	gboolean const vert_border = (flags&0x02) ? TRUE : FALSE;
	gboolean const border = (flags&0x04) ? TRUE : FALSE;
	gboolean const series_keys = (flags&0x08) ? TRUE : FALSE;
#endif
	return FALSE;
}
/****************************************************************************/

static gboolean
BC_R(dataformat)(XLChartHandler const *handle,
		 XLChartReadState *s, BiffQuery *q)
{
	XLChartSeries *series;
	guint16 pt_num, series_index, series_index_for_label;

	XL_CHECK_CONDITION_VAL (q->length >= 8, TRUE);
	pt_num = GSF_LE_GET_GUINT16 (q->data);
	series_index = GSF_LE_GET_GUINT16 (q->data+2);
	series_index_for_label = GSF_LE_GET_GUINT16 (q->data+4);
#if 0
	guint16 const excel4_auto_color = GSF_LE_GET_GUINT16 (q->data+6) & 0x01;
#endif

	if (pt_num == 0 && series_index == 0 && series_index_for_label == 0xfffd)
		s->has_extra_dataformat = TRUE;
	XL_CHECK_CONDITION_VAL (series_index < s->series->len, TRUE);

	series = g_ptr_array_index (s->series, series_index);
	XL_CHECK_CONDITION_VAL (series != NULL, TRUE);

	if (pt_num == 0xffff) {
		s->style_element = -1;
		d (0, g_printerr ("All points"););
	} else {
		s->style_element = pt_num;
		d (0, g_printerr ("Point[%hu]", pt_num););
	}

	d (0, g_printerr (", series=%hu\n", series_index););

	return FALSE;
}

/****************************************************************************/

static gboolean
BC_R(defaulttext)(XLChartHandler const *handle,
		  XLChartReadState *s, BiffQuery *q)
{
	guint16	tmp;

	XL_CHECK_CONDITION_VAL (q->length >= 2, TRUE);
	tmp = GSF_LE_GET_GUINT16 (q->data);

	d (2, g_printerr ("applicability = %hd\n", tmp););

	/*
	 * 0 == 'show labels' label
	 * 1 == Value and percentage data label
	 * 2 == All text in chart
	 * 3 == Undocumented ??
	 */
	XL_CHECK_CONDITION_VAL (tmp <= 3, TRUE);
	return FALSE;
}

/****************************************************************************/

static gboolean
BC_R(dropbar)(XLChartHandler const *handle,
	      XLChartReadState *s, BiffQuery *q)
{
	XL_CHECK_CONDITION_VAL (q->length >= 2, TRUE);

	/* NOTE : The docs lie.  values > 100 seem legal.  My guess based on
	 * the ui is 500.
	 */
	s->dropbar = TRUE;
	s->dropbar_width = GSF_LE_GET_GUINT16 (q->data);
	d (1, g_printerr ("width=%hu\n",s->dropbar_width););
	return FALSE;
}

/****************************************************************************/

static gboolean
BC_R(fbi)(XLChartHandler const *handle,
	  XLChartReadState *s, BiffQuery *q)
{
	guint16 x_basis, y_basis, applied_height, scale_basis, index;

	XL_CHECK_CONDITION_VAL (q->length >= 10, TRUE);

	/*
	 * TODO TODO TODO : Work on appropriate scales.
	 * Is any of this useful other than the index ?
	 */
	x_basis = GSF_LE_GET_GUINT16 (q->data);
	y_basis = GSF_LE_GET_GUINT16 (q->data+2);
	applied_height = GSF_LE_GET_GUINT16 (q->data+4);
	scale_basis = GSF_LE_GET_GUINT16 (q->data+6);
	index = GSF_LE_GET_GUINT16 (q->data+8);

	d (2,
		gsf_mem_dump (q->data, q->length);
		g_printerr ("Font %hu (%hu x %hu) scale=%hu, height=%hu\n",
			index, x_basis, y_basis, scale_basis, applied_height););
	return FALSE;
}
/****************************************************************************/

static gboolean
BC_R(fontx)(XLChartHandler const *handle,
	    XLChartReadState *s, BiffQuery *q)
{
	ExcelFont const *font;
	GOFont const *gfont;
	guint16 fno;

	XL_CHECK_CONDITION_VAL (q->length >= 2, TRUE);
	fno = GSF_LE_GET_GUINT16 (q->data);
	font = excel_font_get (s->container.importer, fno);
	if (!font)
		return FALSE;

	gfont = excel_font_get_gofont (font);
	go_font_ref (gfont);
	BC_R(get_style) (s);
	go_style_set_font (s->style, gfont);
	s->style->font.auto_font = FALSE;
	d (2, g_printerr ("apply font %u %s;", fno, go_font_as_str (gfont)););
	return FALSE;
}

/****************************************************************************/

static gboolean
BC_R(frame)(XLChartHandler const *handle,
	    XLChartReadState *s, BiffQuery *q)
{
#if 0
	guint16 const type = GSF_LE_GET_GUINT16 (q->data);
	guint16 const flags = GSF_LE_GET_GUINT16 (q->data+2);
	gboolean border_shadow, auto_size, auto_pos;

	g_return_val_if_fail (type == 1 || type ==4, TRUE);
	/* FIXME FIXME FIXME : figure out what other values are */
	border_shadow = (type == 4) ? TRUE : FALSE;
	auto_size = (flags&0x01) ? TRUE : FALSE;
	auto_pos = (flags&0x02) ? TRUE : FALSE;
#endif

	s->frame_for_grid = (s->prev_opcode == BIFF_CHART_plotarea);
	s->has_a_grid |= s->frame_for_grid;
	d (0, g_printerr (s->frame_for_grid ? "For grid;\n" : "Not for grid;\n"););

	return FALSE;
}

/****************************************************************************/

static GOColor
ms_chart_map_color (XLChartReadState const *s, guint32 raw, guint32 alpha)
{
	GOColor res;
	if ((~0x7ffffff) & raw) {
		GnmColor *c = excel_palette_get (s->container.importer,
			(0x7ffffff & raw));
		res = c->go_color;
		style_color_unref (c);
	} else {
		guint8 r, g, b;
		r = (raw)       & 0xff;
		g = (raw >> 8)  & 0xff;
		b = (raw >> 16) & 0xff;
		res = GO_COLOR_FROM_RGB (r, g, b);
	}
	return res;
}

static gboolean
BC_R(gelframe) (XLChartHandler const *handle,
		XLChartReadState *s, BiffQuery *q)
{
	MSObjAttrBag *attrs = ms_escher_parse (q, &s->container, TRUE);
	guint32 type = ms_obj_attr_get_uint (attrs,
		MS_OBJ_ATTR_FILL_TYPE, 0);
	guint32 shade_type = ms_obj_attr_get_uint (attrs,
		MS_OBJ_ATTR_FILL_SHADE_TYPE, 0);
	guint32 fill_color = ms_obj_attr_get_uint (attrs,
		MS_OBJ_ATTR_FILL_COLOR, 0);
	guint32 fill_alpha = ms_obj_attr_get_uint (attrs,
		MS_OBJ_ATTR_FILL_ALPHA, 0x10000);
	guint32 fill_back_color = ms_obj_attr_get_uint (attrs,
		MS_OBJ_ATTR_FILL_BACKGROUND, 0);
	guint32 fill_back_alpha = ms_obj_attr_get_uint (attrs,
		MS_OBJ_ATTR_FILL_BACKGROUND_ALPHA, 0x10000);
	guint32 preset = ms_obj_attr_get_uint (attrs,
		MS_OBJ_ATTR_FILL_PRESET, 0);

	d (1, g_printerr ("Frame type = %u\n", type););
	/* plot types we do not support that have gradients */
	if (NULL == s->style || type < 5) {
		ms_obj_attr_bag_destroy (attrs);
		return FALSE;
	}

	s->style->fill.type = GO_STYLE_FILL_GRADIENT;
	s->style->fill.auto_type = FALSE;
	s->style->fill.auto_fore = FALSE;
	s->style->fill.auto_back = FALSE;
	s->style->fill.pattern.fore =
		ms_chart_map_color (s, fill_color, fill_alpha);

	/* FIXME : make presets the same as 2 color for now */
	if (!(shade_type & 8) || preset > 0) {
		s->style->fill.pattern.back = ms_chart_map_color (s,
			fill_back_color, fill_back_alpha);
	} else {
		double brightness;
		unsigned frac = (fill_back_color >> 16) & 0xff;

		/**
		 * 0x10 const
		 * 0x00..0xff fraction
		 * 0x02 == bright 0x01 == dark
		 * 0xf0 == const
		 **/
		switch ((fill_back_color & 0xff00)) {
		default :
			g_warning ("looks like our theory of 1-color gradient brightness is incorrect");
		case 0x0100 : brightness = 0. + frac/512.; break;
		case 0x0200 : brightness = 1. - frac/512.; break;
		}
		go_style_set_fill_brightness (s->style, (1. - brightness) * 100.);
		d (1, g_printerr ("%x : frac = %u, flag = 0x%x ::: %f",
			       fill_back_color, frac, fill_back_color & 0xff00, brightness););
	}

	if (type == 5) { /* Fill from corner */
		/* TODO */
	} else if (type == 6) { /* fill from center */
		/* TODO */
	} else if (type == 7) {
		GOGradientDirection dir = GO_GRADIENT_S_TO_N;
		guint32 angle = ms_obj_attr_get_uint (attrs, MS_OBJ_ATTR_FILL_ANGLE, 0);
		gint32 focus = ms_obj_attr_get_int (attrs, MS_OBJ_ATTR_FILL_FOCUS, 0);

		if (focus < 0)
			focus = ((focus - 25) / 50) % 4 + 4;
		else
			focus = ((focus + 25) / 50) % 4;

		switch (angle) {
		default :
			g_warning ("non standard gradient angle %u, using horizontal", angle);
		case 0 : /* horizontal */
			switch (focus) {
			case 0 : dir = GO_GRADIENT_S_TO_N; break;
			case 1 : dir = GO_GRADIENT_N_TO_S_MIRRORED; break;
			case 2 : dir = GO_GRADIENT_N_TO_S; break;
			case 3 : dir = GO_GRADIENT_S_TO_N_MIRRORED; break;
			}
			break;
		case 0xffd30000 : /* diag down (-45 in 16.16 fixed) */
			switch (focus) {
			case 0 : dir = GO_GRADIENT_SE_TO_NW; break;
			case 1 : dir = GO_GRADIENT_SE_TO_NW_MIRRORED; break;
			case 2 : dir = GO_GRADIENT_NW_TO_SE; break;
			case 3 : dir = GO_GRADIENT_NW_TO_SE_MIRRORED; break;
			}
			break;
		case 0xffa60000 : /* vertical (-90 in 16.16 fixed) */
			switch (focus) {
			case 0 : dir = GO_GRADIENT_E_TO_W; break;
			case 1 : dir = GO_GRADIENT_E_TO_W_MIRRORED; break;
			case 2 : dir = GO_GRADIENT_W_TO_E; break;
			case 3 : dir = GO_GRADIENT_W_TO_E_MIRRORED; break;
			}
			break;
		case 0xff790000 : /* diag up (-135 in 16.16 fixed) */
			switch (focus) {
			case 0 : dir = GO_GRADIENT_SE_TO_NW; break;
			case 1 : dir = GO_GRADIENT_SE_TO_NW_MIRRORED; break;
			case 2 : dir = GO_GRADIENT_NW_TO_SE; break;
			case 3 : dir = GO_GRADIENT_NW_TO_SE_MIRRORED; break;
			}
		}
		s->style->fill.gradient.dir = dir;
	}

	ms_obj_attr_bag_destroy (attrs);

	return FALSE;
}

/****************************************************************************/

static gboolean
BC_R(ifmt)(XLChartHandler const *handle,
	   XLChartReadState *s, BiffQuery *q)
{
	GOFormat *fmt;

	XL_CHECK_CONDITION_VAL (q->length >= 2, TRUE);

	fmt = ms_container_get_fmt (&s->container,
		GSF_LE_GET_GUINT16 (q->data));

	if (fmt != NULL) {
		const char *desc = go_format_as_XL (fmt);

		if (s->axis != NULL)
			g_object_set (G_OBJECT (s->axis),
				"assigned-format-string-XL", desc,
				NULL);
		d (0, g_printerr ("Format = '%s';\n", desc););
		go_format_unref (fmt);
	}

	return FALSE;
}

/****************************************************************************/

static gboolean
BC_R(legend)(XLChartHandler const *handle,
	     XLChartReadState *s, BiffQuery *q)
{
#if 0
	/* Measured in 1/4000ths of the chart width */
	guint32 const x_pos = GSF_LE_GET_GUINT32  (q->data);
	guint32 const y_pos = GSF_LE_GET_GUINT32  (q->data+4);
	guint32 const width = GSF_LE_GET_GUINT32  (q->data+8);
	guint32 const height = GSF_LE_GET_GUINT32 (q->data+12);
	guint8 const spacing = GSF_LE_GET_GUINT8  (q->data+17);
	guint16 const flags = GSF_LE_GET_GUINT16  (q->data+18);
#endif
	guint16 XL_pos;
	GogObjectPosition pos;

	XL_CHECK_CONDITION_VAL (q->length >= 17, TRUE);

	XL_pos = GSF_LE_GET_GUINT8 (q->data+16);

	switch (XL_pos) {
	case 0: pos = GOG_POSITION_S | GOG_POSITION_ALIGN_CENTER; break;
	case 1: pos = GOG_POSITION_N | GOG_POSITION_E; break;
	case 2: pos = GOG_POSITION_N | GOG_POSITION_ALIGN_CENTER; break;
	default :
		g_warning ("Unknown legend position (%d), assuming east.",
			   XL_pos);
	case 3: pos = GOG_POSITION_E | GOG_POSITION_ALIGN_CENTER; break;
	case 4: pos = GOG_POSITION_W | GOG_POSITION_ALIGN_CENTER; break;
	case 7: /* treat floating legends as being on east */
		pos = GOG_POSITION_E | GOG_POSITION_ALIGN_CENTER; break;
	}

	s->legend = gog_object_add_by_name (GOG_OBJECT (s->chart), "Legend", NULL);
	gog_object_set_position_flags (s->legend, pos, GOG_POSITION_COMPASS | GOG_POSITION_ALIGNMENT);

#if 0
	g_printerr ("Legend @ %f,%f, X=%f, Y=%f\n",
		x_pos/4000., y_pos/4000., width/4000., height/4000.);

	/* FIXME : Parse the flags too */
#endif

	return FALSE;
}

/****************************************************************************/

static gboolean
BC_R(legendxn)(XLChartHandler const *handle,
	       XLChartReadState *s, BiffQuery *q)
{
	guint16 flags;

	XL_CHECK_CONDITION_VAL (q->length >= 4, TRUE);

	flags = GSF_LE_GET_GUINT16 (q->data+2);
	if ((flags & 1) && s->currentSeries != NULL)
		s->currentSeries->has_legend = FALSE;
	return FALSE;
}

/****************************************************************************/

static gboolean
BC_R(line)(XLChartHandler const *handle,
	   XLChartReadState *s, BiffQuery *q)
{
	guint16 flags;
	char const *type = "normal";
	gboolean in_3d;

	XL_CHECK_CONDITION_VAL (q->length >= 2, TRUE);

	flags = GSF_LE_GET_GUINT16 (q->data);
	in_3d = (BC_R(ver)(s) >= MS_BIFF_V8 && (flags & 0x04));

	g_return_val_if_fail (s->plot == NULL, TRUE);
	s->plot = (GogPlot*) gog_plot_new_by_name ("GogLinePlot");
	g_return_val_if_fail (s->plot != NULL, TRUE);

	if (flags & 0x02)
		type = "as_percentage";
	else if (flags & 0x01)
		type = "stacked";

	g_object_set (G_OBJECT (s->plot),
		"type",			type,
		"in-3d",		in_3d,
		NULL);

	d(1, g_printerr ("%s line;", type););
	return FALSE;
}

/****************************************************************************/

static char const *const ms_line_pattern[] = {
	"solid",
	"dashed",
	"dotted",
	"dash dotted",
	"dash dot dotted",
	"invisible",
	"dark gray",
	"medium gray",
	"light gray"
};

static gboolean
BC_R(lineformat)(XLChartHandler const *handle,
		 XLChartReadState *s, BiffQuery *q)
{
	guint16 flags;
	guint16 pattern;

	XL_CHECK_CONDITION_VAL (q->length >= (BC_R(ver)(s) >= MS_BIFF_V8 ? 12 : 10), TRUE);

	flags = GSF_LE_GET_GUINT16 (q->data + 8);
	pattern = GSF_LE_GET_GUINT16 (q->data + 4);

	BC_R(get_style) (s);
	switch (GSF_LE_GET_GINT16 (q->data + 6)) {
	default :
	case -1 : s->style->line.width = 0; /* hairline */
		break;
	case  0 : s->style->line.width = 1; /* 'normal' */
		break;
	case  1 : s->style->line.width = 2; /* 'medium' */
		break;
	case  2 : s->style->line.width = 3; /* 'wide' */
		break;
	}
	s->style->line.color      = BC_R(color) (q->data, "LineColor");
	s->style->line.auto_color = s->style->line.auto_dash = (flags & 0x01) ? TRUE : FALSE;

	d (0, g_printerr ("flags == %hd.\n", flags););
	d (0, g_printerr ("Lines are %f pts wide.\n", s->style->line.width););
	d (0, g_printerr ("Lines have a %s pattern.\n",
		       ms_line_pattern [pattern]););

	switch (pattern) {
	default:
	case 0:
		s->style->line.dash_type = GO_LINE_SOLID;
		break;
	case 1:
		s->style->line.dash_type = GO_LINE_DASH;
		break;
	case 2:
		s->style->line.dash_type = GO_LINE_DOT;
		break;
	case 3:
		s->style->line.dash_type = GO_LINE_DASH_DOT;
		break;
	case 4:
		s->style->line.dash_type = GO_LINE_DASH_DOT_DOT;
		break;
	case 5:
		s->style->line.dash_type = GO_LINE_NONE;
		break;
/* we don't really support the other styles, although GOStyle would allow that now */
#if 0
	case 6:
		s->style->line.dash_type = GO_LINE_SOLID;
		s->style->line.pattern = GO_PATTERN_GREY25; /* or 75? */
		s->style->line.fore = GO_COLOR_WHITE;
		break;
	case 7:
		s->style->line.dash_type = GO_LINE_SOLID;
		s->style->line.pattern = GO_PATTERN_GREY50;
		s->style->line.fore = GO_COLOR_WHITE;
		break;
	case 8:
		s->style->line.dash_type = GO_LINE_SOLID;
		s->style->line.pattern = GO_PATTERN_GREY75; /* or 25? */
		s->style->line.fore = GO_COLOR_WHITE;
		break;
#endif
	}

	if (BC_R(ver)(s) >= MS_BIFF_V8 && s->currentSeries != NULL) {
		guint16 const fore = GSF_LE_GET_GUINT16 (q->data + 10);
		d (0, g_printerr ("color index == %hd.\n", fore););
		/* Excel assumes that the color is automatic if it is the same
		as the automatic one whatever the auto flag value is */
		s->style->line.auto_color = (fore == 31 + s->series->len);
	}

	if (s->prev_opcode == BIFF_CHART_chartline) {
		/* we only support hi-lo lines at the moment */
		if (s->cur_role == 1)
			s->chartline_style[s->cur_role] = s->style;
		else
			g_object_unref (s->style);
		s->style = NULL;
	} else if (s->axis != NULL)
		s->axislineflags = flags;

	return FALSE;
}

/****************************************************************************/

static gboolean
BC_R(markerformat)(XLChartHandler const *handle,
		   XLChartReadState *s, BiffQuery *q)
{
	static char const *const ms_chart_marker[] = {
		"none", "square", "diamond", "triangle", "x", "star",
		"dow", "std", "circle", "plus"
	};
	static GOMarkerShape const shape_map[] = {
		GO_MARKER_NONE,
		GO_MARKER_SQUARE,
		GO_MARKER_DIAMOND,
		GO_MARKER_TRIANGLE_UP,
		GO_MARKER_X,
		GO_MARKER_ASTERISK,
		GO_MARKER_HALF_BAR,
		GO_MARKER_BAR,
		GO_MARKER_CIRCLE,
		GO_MARKER_CROSS
	};
	GOMarker *marker;
	guint16 shape;
	guint16 flags;
	gboolean auto_marker;

	XL_CHECK_CONDITION_VAL (q->length >= (BC_R(ver)(s) >= MS_BIFF_V8 ? 20 : 8), TRUE);

	shape = GSF_LE_GET_GUINT16 (q->data+8);
	flags = GSF_LE_GET_GUINT16 (q->data+10);
	auto_marker = (flags & 0x01) ? TRUE : FALSE;

	BC_R(get_style) (s);
	marker = go_marker_new ();

	d (0, g_printerr ("Marker = %s\n", ms_chart_marker [shape]););
	if (shape >= G_N_ELEMENTS (shape_map))
		shape = 1; /* square */
	go_marker_set_shape (marker, shape_map [shape]);

	go_marker_set_outline_color (marker,
		(flags & 0x20) ? 0 : BC_R(color) (q->data + 0, "MarkerFore"));
	go_marker_set_fill_color (marker,
		(flags & 0x10) ? 0 : BC_R(color) (q->data + 4, "MarkerBack"));

	s->style->marker.auto_shape = auto_marker;

	if (BC_R(ver)(s) >= MS_BIFF_V8) {
		guint16 const fore = GSF_LE_GET_GUINT16 (q->data+12);
		guint16 const back = GSF_LE_GET_GUINT16 (q->data+14);
		guint32 const marker_size = GSF_LE_GET_GUINT32 (q->data+16);
		go_marker_set_size (marker, marker_size / 20.);
		d (1, g_printerr ("Marker size : is %f pts\n", marker_size / 20.););
		/* Excel assumes that the color is automatic if it is the same
		as the automatic one whatever the auto flag value is */
		s->style->marker.auto_outline_color = (fore == 31 + s->series->len);
		s->style->marker.auto_fill_color = (back == 31 + s->series->len);
	} else
		s->style->marker.auto_outline_color =
			s->style->marker.auto_fill_color = auto_marker;

	go_style_set_marker (s->style, marker);

	return FALSE;
}

/****************************************************************************/

static gboolean
BC_R(objectlink)(XLChartHandler const *handle,
		 XLChartReadState *s, BiffQuery *q)
{
	guint16 purpose;
	GogObject *label = NULL;

	XL_CHECK_CONDITION_VAL (q->length >= 6, TRUE);

	purpose = GSF_LE_GET_GUINT16 (q->data);
	if (purpose != 4 && s->text == NULL && s->label == NULL)
		return FALSE;

	switch (purpose) {
	case 1:
		g_return_val_if_fail (s->chart != NULL, FALSE);
		label = gog_object_add_by_name (GOG_OBJECT (s->chart), "Title", s->label);
		break;
	case 2:
	case 3:
	case 7: {
		GSList *axes;
		GogAxisType t;

		g_return_val_if_fail (s->chart != NULL, FALSE);

		switch (purpose) {
		case 2: t = GOG_AXIS_Y; break;
		case 3: t = GOG_AXIS_X; break;
		case 7: t = GOG_AXIS_Z; break;
		default :
			g_warning ("Unknown axis type %d", purpose);
			return FALSE;
		}
		axes = gog_chart_get_axes (s->chart, t);

		g_return_val_if_fail (axes != NULL, FALSE);

		label = gog_object_add_by_name (GOG_OBJECT (axes->data), "Label", s->label);
		g_slist_free (axes);
		break;
	}
	case 4:
		break;
	}

	if (label != NULL) {
		Sheet *sheet = ms_container_sheet (s->container.parent);
		if (sheet != NULL && s->text != NULL) {
			GnmValue *value = value_new_string_nocopy (s->text);
			GnmExprTop const *texpr = gnm_expr_top_new_constant (value);
			gog_dataset_set_dim (GOG_DATASET (label), 0,
					     gnm_go_data_scalar_new_expr (sheet, texpr), NULL);
		}
		s->text = NULL;
		s->label = NULL;
	} else if (s->label) {
		d (2, g_printerr ("We have non imported data for a text field;\n"););
		g_object_unref (s->label);
		s->label = NULL;
	}

	d (2, {
	guint16 const series_num = GSF_LE_GET_GUINT16 (q->data+2);
	guint16 const pt_num = GSF_LE_GET_GUINT16 (q->data+4);

	switch (purpose) {
	case 1 : g_printerr ("TEXT is chart title\n"); break;
	case 2 : g_printerr ("TEXT is Y axis title\n"); break;
	case 3 : g_printerr ("TEXT is X axis title\n"); break;
	case 4 : g_printerr ("TEXT is data label for pt %hd in series %hd\n",
			 pt_num, series_num); break;
	case 7 : g_printerr ("TEXT is Z axis title\n"); break;
	default :
		 g_printerr ("ERROR : TEXT is linked to undocumented object\n");
	}});
	if (NULL != label && NULL != s->style)
		go_styled_object_set_style (GO_STYLED_OBJECT (label), s->style);
	return FALSE;
}

/****************************************************************************/

static gboolean
BC_R(picf)(XLChartHandler const *handle,
	   XLChartReadState *s, BiffQuery *q)
{
	return FALSE;
}

/****************************************************************************/

static gboolean
BC_R(pie)(XLChartHandler const *handle,
	  XLChartReadState *s, BiffQuery *q)
{
	double initial_angle, center_size;
	guint16 flags;
	gboolean in_3d;

	XL_CHECK_CONDITION_VAL (q->length >= 6, TRUE);

	initial_angle = GSF_LE_GET_GUINT16 (q->data);
	center_size = GSF_LE_GET_GUINT16 (q->data+2); /* 0-100 */
	flags = GSF_LE_GET_GUINT16 (q->data+4);
	in_3d = (BC_R(ver)(s) >= MS_BIFF_V8 && (flags & 0x01));

	g_return_val_if_fail (s->plot == NULL, TRUE);
	s->plot = (GogPlot*) gog_plot_new_by_name ((center_size == 0) ? "GogPiePlot" : "GogRingPlot");
	g_return_val_if_fail (s->plot != NULL, TRUE);

	g_object_set (G_OBJECT (s->plot),
		"in-3d",		in_3d,
		"initial-angle",	initial_angle,
		NULL);
	if (center_size != 0)
		g_object_set (G_OBJECT (s->plot),
			"center-size",	((double)center_size) / 100.,
			NULL);

#if 0
	gboolean leader_lines = (BC_R(ver)(s) >= MS_BIFF_V8 && (flags & 0x02));
#endif

	return FALSE;
}

/****************************************************************************/

static gboolean
BC_R(pieformat)(XLChartHandler const *handle,
		XLChartReadState *s, BiffQuery *q)
{
	unsigned separation;

	XL_CHECK_CONDITION_VAL (q->length >= 2, TRUE);

	separation = GSF_LE_GET_GUINT16 (q->data); /* 0-500 */

	/* we only support the default right now.  Also, XL sets this for _all_ types
	 * rather than just pies. */
	if (s->style_element >= 0 && s->style != NULL &&
	    !s->has_extra_dataformat)	/* this is cheesy, figure out what the 0xfffd in dataformat means */
		g_object_set_data (G_OBJECT (s->style),
			"pie-separation", GUINT_TO_POINTER (separation));
	else if (s->plot != NULL &&
		 g_object_class_find_property (G_OBJECT_GET_CLASS (s->plot), "default-separation"))
		g_object_set (G_OBJECT (s->plot),
			"default-separation",	((double) separation) / 100.,
			NULL);
	d (2, g_printerr ("Pie slice(s) are %u %% of diam from center\n",
		       separation););
	return FALSE;
}

/****************************************************************************/

static gboolean
BC_R(plotarea)(XLChartHandler const *handle,
	       XLChartReadState *s, BiffQuery *q)
{
	/* Does nothing.  Should always have a 'FRAME' record following */
	return FALSE;
}

/****************************************************************************/

static gboolean
BC_R(plotgrowth)(XLChartHandler const *handle,
		 XLChartReadState *s, BiffQuery *q)
{
	XL_CHECK_CONDITION_VAL (q->length >= 8, TRUE);
	d (2, {
	/* Docs say these are longs
	 * But it appears that only 2 lsb are valid ??
	 */
	gint16 const horiz = GSF_LE_GET_GUINT16 (q->data+2);
	gint16 const vert = GSF_LE_GET_GUINT16 (q->data+6);

	g_printerr ("Scale H=");
	if (horiz != -1)
		g_printerr ("%u", horiz);
	else
		g_printerr ("Unscaled");
	g_printerr (", V=");
	if (vert != -1)
		g_printerr ("%u", vert);
	else
		g_printerr ("Unscaled");
	});
	return FALSE;
}
/****************************************************************************/

static gboolean
BC_R(pos)(XLChartHandler const *handle,
	  XLChartReadState *s, BiffQuery *q)
{
	switch (BC_R(top_state) (s, 0)) {
	case BIFF_CHART_text :
		d(2, g_printerr ("text pos;"););
		break;
	default :
		;
	}
	return FALSE;
}

/****************************************************************************/

static void
set_radial_axes (XLChartReadState *s)
{
	GSList *l, *cur, *contrib, *ptr;

	l = gog_chart_get_axes (s->chart, GOG_AXIS_X);
	for (cur = l; cur; cur = cur->next) {
		GogObject *axis = cur->data;

		contrib = g_slist_copy ((GSList *) gog_axis_contributors (GOG_AXIS (axis)));
		gog_axis_clear_contributors (GOG_AXIS (axis));
		if (gog_object_is_deletable (axis))
			gog_object_clear_parent (axis);
		else {
			g_slist_free (contrib);
			continue;
		}

		g_object_set (G_OBJECT (axis), "type",
			      GOG_AXIS_CIRCULAR, NULL);
		gog_object_add_by_name (GOG_OBJECT (s->chart),
					"Circular-Axis", axis);
		for (ptr= contrib; ptr != NULL; ptr = ptr->next)
			gog_plot_set_axis (GOG_PLOT (ptr->data), GOG_AXIS (axis));
		g_slist_free (contrib);
	}
	g_slist_free (l);

	l = gog_chart_get_axes (s->chart, GOG_AXIS_Y);
	for (cur = l; cur; cur = cur->next) {
		GogObject *axis = cur->data;

		contrib = g_slist_copy ((GSList *) gog_axis_contributors (GOG_AXIS (axis)));
		gog_axis_clear_contributors (GOG_AXIS (axis));
		if (gog_object_is_deletable (axis))
			gog_object_clear_parent (axis);
		else {
			g_slist_free (contrib);
			continue;
		}

		g_object_set (G_OBJECT (axis), "type",
			      GOG_AXIS_RADIAL, NULL);
		gog_object_add_by_name (GOG_OBJECT (s->chart),
					"Radial-Axis", axis);
		for (ptr= contrib; ptr != NULL; ptr = ptr->next)
			gog_plot_set_axis (GOG_PLOT (ptr->data), GOG_AXIS (axis));
		g_slist_free (contrib);
	}
	g_slist_free (l);
}

static gboolean
BC_R(radar)(XLChartHandler const *handle,
	    XLChartReadState *s, BiffQuery *q)
{
	g_return_val_if_fail (s->plot == NULL, TRUE);

	s->plot = (GogPlot*) gog_plot_new_by_name ("GogRadarPlot");
	/* XL defaults to having markers and does not emit a style record
	 * to define a marker in the default case. */
	if (s->plot)
		g_object_set (G_OBJECT (s->plot), "default-style-has-markers", TRUE, NULL);

	/* Change axes types */
	set_radial_axes (s);

	return FALSE;
}

/****************************************************************************/

static gboolean
BC_R(radararea)(XLChartHandler const *handle,
		XLChartReadState *s, BiffQuery *q)
{
	g_return_val_if_fail (s->plot == NULL, TRUE);
	s->plot = (GogPlot*) gog_plot_new_by_name ("GogRadarAreaPlot");

	/* Change axes types */
	set_radial_axes (s);

	return FALSE;
}

/****************************************************************************/

static gboolean
BC_R(sbaseref)(XLChartHandler const *handle,
	       XLChartReadState *s, BiffQuery *q)
{
	return FALSE;
}

/****************************************************************************/

static gboolean
BC_R(scatter)(XLChartHandler const *handle,
	      XLChartReadState *s, BiffQuery *q)
{
	g_return_val_if_fail (s->plot == NULL, TRUE);

	if (BC_R(ver)(s) >= MS_BIFF_V8) {
		guint16 flags;

		XL_CHECK_CONDITION_VAL (q->length >= 6, TRUE);

		flags = GSF_LE_GET_GUINT16 (q->data + 4);

		/* Has bubbles */
		if (flags & 0x01) {
			guint16 const size_type = GSF_LE_GET_GUINT16 (q->data+2);
			gboolean in_3d = (flags & 0x04) != 0;
			gboolean show_negatives = (flags & 0x02) != 0;
			gboolean size_as_area = (size_type != 2);
			double scale =  GSF_LE_GET_GUINT16 (q->data) / 100.;
			s->plot = (GogPlot*) gog_plot_new_by_name ("GogBubblePlot");
			g_return_val_if_fail (s->plot != NULL, TRUE);
			g_object_set (G_OBJECT (s->plot),
				"in-3d",		in_3d,
				"show-negatives",	show_negatives,
				"size-as-area",		size_as_area,
				"bubble-scale",	scale,
				NULL);
			d(1, g_printerr ("bubbles;"););
			return FALSE;
		}
	}

	s->plot = (GogPlot*) gog_plot_new_by_name ("GogXYPlot");
	g_return_val_if_fail (s->plot != NULL, TRUE);

	d(1, g_printerr ("scatter;"););
	return FALSE;
}

/****************************************************************************/

static gboolean
BC_R(serauxerrbar)(XLChartHandler const *handle,
		   XLChartReadState *s, BiffQuery *q)
{
	guint8 type, src, teetop, num;
	double val;

	XL_CHECK_CONDITION_VAL (q->length >= 14, TRUE);

	type = GSF_LE_GET_GUINT8  (q->data);
	src = GSF_LE_GET_GUINT8  (q->data+1);
	teetop = GSF_LE_GET_GUINT8  (q->data+2);
	num = GSF_LE_GET_GUINT16  (q->data+12);
	/* next octet must be 1 */

	d (1, {
		switch (type) {
		case 1: g_printerr ("type: x-direction plus\n"); break;
		case 2: g_printerr ("type: x-direction minus\n"); break;
		case 3: g_printerr ("type: y-direction plus\n"); break;
		case 4: g_printerr ("type: y-direction minus\n"); break;
		}
		switch (src) {
		case 1: g_printerr ("source: percentage\n"); break;
		case 2: g_printerr ("source: fixed value\n"); break;
		case 3: g_printerr ("source: standard deviation\n"); break;
		case 4: g_printerr ("source: custom\n"); break;
		case 5: g_printerr ("source: standard error\n"); break;
		}
		g_printerr ("%sT-shaped\n", (teetop)? "": "Not ");
		g_printerr ("num values: %d\n", num);
	});
	g_return_val_if_fail (s->currentSeries != NULL, FALSE);

	s->currentSeries->err_type = type;
	s->currentSeries->err_src = src;
	s->currentSeries->err_teetop = teetop;
	s->currentSeries->err_parent = s->parent_index;
	s->currentSeries->err_num = num;
	if (src > 0 && src < 4) {
		val = GSF_LE_GET_DOUBLE (q->data + 4);
		d(1, g_printerr ("value = %g\n",val););
		s->currentSeries->err_val = val;
	}
	return FALSE;
}

/****************************************************************************/

static gboolean
BC_R(serauxtrend)(XLChartHandler const *handle,
		  XLChartReadState *s, BiffQuery *q)
{
	guint8 type, order;
	double intercept, forecast, backcast;
	gboolean equation, r2;

	XL_CHECK_CONDITION_VAL (q->length >= 28, TRUE);

	type = GSF_LE_GET_GUINT8  (q->data);
	order = GSF_LE_GET_GUINT8  (q->data+1);
	intercept = GSF_LE_GET_DOUBLE (q->data+2);
	equation = GSF_LE_GET_GUINT8  (q->data+10);
	r2 = GSF_LE_GET_GUINT8  (q->data+11);
	forecast = GSF_LE_GET_DOUBLE (q->data+12);
	backcast = GSF_LE_GET_DOUBLE (q->data+20);
	d (1, {
		switch (type) {
		case 0: g_printerr ("type: polynomial\n"); break;
		case 1: g_printerr ("type: exponential\n"); break;
		case 2: g_printerr ("type: logarithmic\n"); break;
		case 3: g_printerr ("type: power\n"); break;
		case 4: g_printerr ("type: moving average\n"); break;
		}
		g_printerr ("order: %d\n", order);
		g_printerr ("intercept: %g\n", intercept);
		g_printerr ("show equation: %s\n", (equation)? "yes": "no");
		g_printerr ("show R-squared: %s\n", (r2)? "yes": "no");
		g_printerr ("forecast: %g\n", forecast);
		g_printerr ("backcast: %g\n", backcast);
	});
	g_return_val_if_fail (s->currentSeries != NULL, FALSE);

	s->currentSeries->reg_type = type;
	s->currentSeries->reg_order = order;
	s->currentSeries->reg_show_eq = equation;
	s->currentSeries->reg_show_R2 = r2;
	s->currentSeries->reg_intercept = intercept;
	s->currentSeries->reg_backcast = backcast;
	s->currentSeries->reg_forecast = forecast;
	s->currentSeries->reg_parent = s->parent_index;
	s->currentSeries->reg_skip_invalid = TRUE; /* excel does that, more or less */
	s->currentSeries->reg_min = s->currentSeries->reg_max = go_nan;
	return FALSE;
}

/****************************************************************************/

static gboolean
BC_R(serfmt)(XLChartHandler const *handle,
	     XLChartReadState *s, BiffQuery *q)
{
	guint8 flags;

	XL_CHECK_CONDITION_VAL (q->length >= 2, TRUE);

	flags = GSF_LE_GET_GUINT8  (q->data);
	if (flags & 1) {
	    if (s->currentSeries)
		s->currentSeries->interpolation = GO_LINE_INTERPOLATION_SPLINE;
	    else
		s->interpolation = GO_LINE_INTERPOLATION_SPLINE;
	}
	d (1, {
		g_printerr ("interpolation: %s\n", (flags & 1)? "spline": "linear");
	});
	return FALSE;
}


/****************************************************************************/

static gboolean
BC_R(trendlimits)(XLChartHandler const *handle,
		  XLChartReadState *s, BiffQuery *q)
{
	double min, max;
	gboolean skip_invalid;

	XL_CHECK_CONDITION_VAL (s->currentSeries, TRUE);
	XL_CHECK_CONDITION_VAL (q->length >= 17, TRUE);
	min = GSF_LE_GET_DOUBLE (q->data);
	max = GSF_LE_GET_DOUBLE (q->data + 8);
	skip_invalid = GSF_LE_GET_GUINT8 (q->data + 16);

	d (1, {
		g_printerr ("skip invalid data: %s\n", (skip_invalid)? "yes": "no");
		g_printerr ("min: %g\n", min);
		g_printerr ("max: %g\n", max);
	});
	s->currentSeries->reg_min = min;
	s->currentSeries->reg_max = max;
	s->currentSeries->reg_skip_invalid = skip_invalid;
	return FALSE;
}

/****************************************************************************/

static void
BC_R(vector_details)(XLChartReadState *s, BiffQuery *q, XLChartSeries *series,
		     GogMSDimType purpose,
		     int type_offset, int count_offset, char const *name)
{
	XL_CHECK_CONDITION (q->length >= 2 + (unsigned)count_offset);
#if 0
	switch (GSF_LE_GET_GUINT16 (q->data + type_offset)) {
	case 0 : /* date */ break;
	case 1 : /* value */ break;
	case 2 : /* sequences */ break;
	case 3 : /* string */ break;
	}
#endif

	series->data [purpose].num_elements = GSF_LE_GET_GUINT16 (q->data+count_offset);
	d (0, g_printerr ("%s has %d elements\n",
		       name, series->data [purpose].num_elements););
}


static gboolean
BC_R(series)(XLChartHandler const *handle,
	     XLChartReadState *s, BiffQuery *q)
{
	XLChartSeries *series;

	XL_CHECK_CONDITION_VAL (s->currentSeries == NULL, TRUE);

	d (2, g_printerr ("SERIES = %d\n", s->series->len););

	series = excel_chart_series_new ();

	/* WARNING : The offsets in the documentation are WRONG.
	 *           Use the sizes instead.
	 */
	BC_R(vector_details) (s, q, series, GOG_MS_DIM_CATEGORIES,
			      0, 4, "Categories");
	BC_R(vector_details) (s, q, series, GOG_MS_DIM_VALUES,
			      2, 6, "Values");
	if (BC_R(ver)(s) >= MS_BIFF_V8)
		BC_R(vector_details) (s, q, series, GOG_MS_DIM_BUBBLES,
				      8, 10, "Bubbles");

	g_ptr_array_add (s->series, series);
	s->currentSeries = series;

	return FALSE;
}

/****************************************************************************/

static gboolean
BC_R(serieslist)(XLChartHandler const *handle,
		 XLChartReadState *s, BiffQuery *q)
{
	return FALSE;
}

/****************************************************************************/

static gboolean
BC_R(seriestext)(XLChartHandler const *handle,
		 XLChartReadState *s, BiffQuery *q)
{
	guint16 id;
	int slen;
	char *str;
	GnmValue *value;

	XL_CHECK_CONDITION_VAL (q->length >= 3, TRUE);
	id = GSF_LE_GET_GUINT16 (q->data);	/* must be 0 */
	slen = GSF_LE_GET_GUINT8 (q->data + 2);
	XL_CHECK_CONDITION_VAL (id == 0, TRUE);

	if (slen == 0)
		return FALSE;

	str = excel_biff_text_1 (s->container.importer, q, 2);
	d (2, g_printerr ("'%s';\n", str););

	if (s->currentSeries != NULL &&
	    s->currentSeries->data [GOG_MS_DIM_LABELS].data == NULL) {
		GnmExprTop const *texpr;
		Sheet *sheet = ms_container_sheet (s->container.parent);
		g_return_val_if_fail (sheet != NULL, FALSE);
		value = value_new_string_nocopy (str);
		texpr = gnm_expr_top_new_constant (value);
		s->currentSeries->data [GOG_MS_DIM_LABELS].data =
			gnm_go_data_scalar_new_expr (sheet, texpr);
	} else if (BC_R(top_state) (s, 0) == BIFF_CHART_text) {
		if (s->text != NULL) {
			g_warning ("multiple seriestext associated with 1 text record ?");
			g_free (str);
		} else
			s->text = str;
	} else
		g_free (str);

	return FALSE;
}

/****************************************************************************/

static gboolean
BC_R(serparent)(XLChartHandler const *handle,
		XLChartReadState *s, BiffQuery *q)
{
	guint16 index;

	XL_CHECK_CONDITION_VAL (q->length >= 2, TRUE);
	index = GSF_LE_GET_GUINT16 (q->data) - 1;
	d (1, g_printerr ("Parent series index is %hd\n", index););
	s->parent_index = index;

	return FALSE;
}

/****************************************************************************/

static gboolean
BC_R(sertocrt)(XLChartHandler const *handle,
	       XLChartReadState *s, BiffQuery *q)
{
	guint16 index;

	XL_CHECK_CONDITION_VAL (q->length >= 2, TRUE);
	XL_CHECK_CONDITION_VAL (s->currentSeries != NULL, TRUE);
	index = GSF_LE_GET_GUINT16 (q->data);

	s->currentSeries->chart_group = index;

	d (1, g_printerr ("Series chart group index is %hd\n", index););
	return FALSE;
}

/****************************************************************************/

typedef enum {
	MS_CHART_BLANK_SKIP		= 0,
	MS_CHART_BLANK_ZERO		= 1,
	MS_CHART_BLANK_INTERPOLATE	= 2,
	MS_CHART_BLANK_MAX		= 3
} MSChartBlank;
static char const *const ms_chart_blank[] = {
	"Skip blanks", "Blanks are zero", "Interpolate blanks"
};

static gboolean
BC_R(shtprops)(XLChartHandler const *handle,
	       XLChartReadState *s, BiffQuery *q)
{
	guint16 flags;
	gboolean manual_format, only_plot_visible_cells, dont_size_with_window,
		has_pos_record;
	gboolean ignore_pos_record = FALSE;
	guint8 tmp;
	MSChartBlank blanks;

	XL_CHECK_CONDITION_VAL (q->length >= 4, TRUE);
	flags = GSF_LE_GET_GUINT16 (q->data);
	manual_format		= (flags&0x01) ? TRUE : FALSE;
	only_plot_visible_cells	= (flags&0x02) ? TRUE : FALSE;
	dont_size_with_window	= (flags&0x04) ? TRUE : FALSE;
	has_pos_record		= (flags&0x08) ? TRUE : FALSE;
	tmp = GSF_LE_GET_GUINT16 (q->data+2);
	g_return_val_if_fail (tmp < MS_CHART_BLANK_MAX, TRUE);
	blanks = tmp;
	d (2, g_printerr ("%s;", ms_chart_blank[blanks]););

	if (BC_R(ver)(s) >= MS_BIFF_V8)
		ignore_pos_record = (flags&0x10) ? TRUE : FALSE;

	d (1, {
			g_printerr ("%sesize chart with window.\n",
				    dont_size_with_window ? "Don't r": "R");

			if (has_pos_record && !ignore_pos_record)
				g_printerr ("There should be a POS record around here soon\n");

			if (manual_format)
				g_printerr ("Manually formated\n");
			if (only_plot_visible_cells)
				g_printerr ("Only plot visible (to whom?) cells\n");
		});
	return FALSE;
}

/****************************************************************************/

static gboolean
BC_R(siindex)(XLChartHandler const *handle,
	      XLChartReadState *s, BiffQuery *q)
{
	XL_CHECK_CONDITION_VAL (q->length >= 2, TRUE);
	/* UNDOCUMENTED : Docs says this is long
	 * Biff record is only length 2 */
	s->cur_role = GSF_LE_GET_GUINT16 (q->data);
	d (1, g_printerr ("Series %d is %d\n", s->series->len, s->cur_role););
	return FALSE;
}
/****************************************************************************/

static gboolean
BC_R(surf)(XLChartHandler const *handle,
	   XLChartReadState *s, BiffQuery *q)
{
	XL_CHECK_CONDITION_VAL (q->length >= 2, TRUE);

	/* TODO : implement wireframe (aka use-color) */
#if 0
	/* NOTE: The record is only 2 bytes, so this code is wrong.  */
	guint16 const flags = GSF_LE_GET_GUINT16 (q->data+4);

	g_return_val_if_fail (s->plot == NULL, TRUE);
	s->plot = gog_plot_new_by_name ("GogContourPlot");
	g_object_set (G_OBJECT (s->plot),
		"use-color",		(flags & 0x01) != 0,
		"in-3d",		(BC_R(ver)(s) >= MS_BIFF_V8 && (flags & 0x02)),
		NULL);
#endif

	/* at this point, we don't know if it is a contour plot or a surface
	so we defer plot creation until later */
	s->is_surface = TRUE;

	return FALSE;
}

/****************************************************************************/

enum {
	XL_POS_LOW, /* left or top */
	XL_POS_CENTER,
	XL_POS_HIGH, /* right or bottom */
	XL_POS_JUSTIFY /*not supported, just use as an equivalent for center */
};

static gboolean
BC_R(text)(XLChartHandler const *handle,
	   XLChartReadState *s, BiffQuery *q)
{
#if 0 /*when we have somewhere to store it */
	static GnmHAlign const haligns[] = { /* typo in docs */
		HALIGN_LEFT, HALIGN_CENTER, HALIGN_RIGHT, HALIGN_JUSTIFY
	};
	static GnmVAlign const valigns[] = {
		VALIGN_TOP, VALIGN_CENTER, VALIGN_BOTTOM, VALIGN_JUSTIFY
	};
	unsigned tmp;
#endif
	XL_CHECK_CONDITION_VAL (q->length >= 8, TRUE);

	BC_R(get_style) (s);

#if 0 /*when we have somewhere to store it */
	tmp = GSF_LE_GET_GINT8 (q->data + 0);
	s->style-> .... = haligns[tmp < G_N_ELEMENTS (haligns) ? tmp : 0];
	tmp = GSF_LE_GET_GINT8 (q->data + 1);
	s->style-> .... = valigns[tmp < G_N_ELEMENTS (valigns) ? tmp : 0];
#endif

	s->style->font.color = BC_R(color) (q->data+4, "Font");
	if (BC_R(ver)(s) >= MS_BIFF_V8 && q->length >= 34) {
		/* this over rides the flags */
		s->style->text_layout.angle = GSF_LE_GET_GINT16 (q->data + 30);
	}

	d (2, {
	if (s->prev_opcode == BIFF_CHART_defaulttext) {
		g_printerr ("Text follows defaulttext;\n");
	} else switch (BC_R(top_state) (s, 0)) {
	case BIFF_CHART_axisparent :
		g_printerr ("Text follows axis;\n");
		break;
	case BIFF_CHART_chart :
		g_printerr ("Text follows chart;\n");
		break;
	case BIFF_CHART_legend :
		g_printerr ("Text follows legend;\n");
		break;
	default :
		g_printerr ("BIFF ERROR : A Text record follows a %x\n",
			s->prev_opcode);
		g_object_unref (s->style);
		s->style = NULL;
	}};);

return FALSE;
}

/****************************************************************************/

static gboolean
BC_R(tick)(XLChartHandler const *handle,
	   XLChartReadState *s, BiffQuery *q)
{
	guint16 major, minor, label, flags;

	XL_CHECK_CONDITION_VAL (q->length >= 26, TRUE);

	major = GSF_LE_GET_GUINT8 (q->data);
	minor = GSF_LE_GET_GUINT8 (q->data+1);
	label = GSF_LE_GET_GUINT8 (q->data+2);
	flags = GSF_LE_GET_GUINT16 (q->data+24);

	if (s->axis != NULL)
		g_object_set (G_OBJECT (s->axis),
			/* cheat until we support different label pos */
			"major-tick-labeled",	(label != 0),
			"major-tick-in",	((major & 1) ? TRUE : FALSE),
			"major-tick-out",	((major >= 2) ? TRUE : FALSE),
			"minor-tick-in",	((minor & 1) ? TRUE : FALSE),
			"minor-tick-out",	((minor >= 2) ? TRUE : FALSE),
			NULL);
	BC_R(get_style) (s);
	if (!(flags & 0x01))
		s->style->font.color = BC_R(color) (q->data+4, "LabelColour");
	s->style->text_layout.auto_angle = flags & 0x20;
	switch (flags & 0x1c) {
	default:
		/* not supported */
	case 0:
		s->style->text_layout.angle = 0.;
		break;
	case 8:
		s->style->text_layout.angle = 90.;
		break;
	case 0x0c:
		s->style->text_layout.angle = -90.;
		break;
	}

	if ((!(flags & 0x20)) && BC_R(ver)(s) >= MS_BIFF_V8) {
		guint16 trot = GSF_LE_GET_GUINT16 (q->data+28);
		if (trot <= 0x5a)
			s->style->text_layout.angle = trot;
		else if (trot <= 0xb4)
			s->style->text_layout.angle = 90 - (int) trot;
		else
			; // FIXME: not supported for now
	}

	d (1, {
	switch (major) {
	case 0: g_printerr ("no major tick;\n"); break;
	case 1: g_printerr ("major tick inside axis;\n"); break;
	case 2: g_printerr ("major tick outside axis;\n"); break;
	case 3: g_printerr ("major tick across axis;\n"); break;
	default : g_printerr ("unknown major tick type;\n");
	}
	switch (minor) {
	case 0: g_printerr ("no minor tick;\n"); break;
	case 1: g_printerr ("minor tick inside axis;\n"); break;
	case 2: g_printerr ("minor tick outside axis;\n"); break;
	case 3: g_printerr ("minor tick across axis;\n"); break;
	default : g_printerr ("unknown minor tick type;\n");
	}
	switch (label) {
	case 0: g_printerr ("no tick label;\n"); break;
	case 1: g_printerr ("tick label at low end (NOTE mapped to near axis);\n"); break;
	case 2: g_printerr ("tick label at high end (NOTE mapped to near axis);\n"); break;
	case 3: g_printerr ("tick label near axis;\n"); break;
	default : g_printerr ("unknown tick label position;\n");
	}

	/*
	if (flags&0x01)
		g_printerr ("Auto tick label colour");
	else
		BC_R(color) (q->data+4, "LabelColour", tick, FALSE);
	*/

	if (flags&0x02)
		g_printerr ("Auto text background mode\n");
	else
		g_printerr ("background mode = %d\n", (unsigned)GSF_LE_GET_GUINT8 (q->data+3));

	switch (flags&0x1c) {
	case 0: g_printerr ("no rotation;\n"); break;
	case 4: g_printerr ("top to bottom letters upright;\n"); break;
	case 8: g_printerr ("rotate 90deg counter-clockwise;\n"); break;
	case 0x0c: g_printerr ("rotate 90deg clockwise;\n"); break;
	default : g_printerr ("unknown rotation;\n");
	}

	if (flags&0x20)
		g_printerr ("Auto rotate;\n");
	});

	return FALSE;
}

/****************************************************************************/

static gboolean
BC_R(units)(XLChartHandler const *handle,
	    XLChartReadState *s, BiffQuery *q)
{
	guint16 type;

	XL_CHECK_CONDITION_VAL (q->length >= 2, TRUE);

	/* Irrelevant */
	type = GSF_LE_GET_GUINT16 (q->data);
	XL_CHECK_CONDITION_VAL (type == 0, TRUE);

	return FALSE;
}

/****************************************************************************/

static void
xl_axis_get_elem (Sheet *sheet, GogObject *axis, unsigned dim, gchar const *name,
		  gboolean flag, guint8 const *data, gboolean log_scale)
{
	GOData *dat;
	if (flag) {
		dat = NULL;
		d (1, g_printerr ("%s = Auto\n", name););
		if (dim == GOG_AXIS_ELEM_CROSS_POINT) {
			GnmValue *value = value_new_float (0.);
			GnmExprTop const *texpr = gnm_expr_top_new_constant (value);
			gog_dataset_set_dim (GOG_DATASET (axis), dim,
			    gnm_go_data_scalar_new_expr (sheet, texpr), NULL);
		    g_object_set (axis, "pos-str", "cross", NULL);
		}
	} else {
		double const val = gsf_le_get_double (data);
		gnm_float real_value = log_scale ? gnm_pow10 (val) : (gnm_float)val;
		GnmValue *value = value_new_float (real_value);
		GnmExprTop const *texpr = gnm_expr_top_new_constant (value);
		gog_dataset_set_dim (GOG_DATASET (axis), dim,
			gnm_go_data_scalar_new_expr (sheet, texpr), NULL);
		d (1, g_printerr ("%s = %f\n", name, (double)real_value););
	}
}

static gboolean
BC_R(valuerange)(XLChartHandler const *handle,
		 XLChartReadState *s, BiffQuery *q)
{
	guint16 flags;
	gboolean log_scale;
	double cross;
	Sheet *sheet = ms_container_sheet (s->container.parent);

	XL_CHECK_CONDITION_VAL (q->length >= 42, TRUE);

	flags = GSF_LE_GET_GUINT16 (q->data+40);
	log_scale =  flags & 0x20;
	if (log_scale) {
		g_object_set (s->axis, "map-name", "Log", NULL);
		d (1, g_printerr ("Log scaled;\n"););
	}

	xl_axis_get_elem (sheet, s->axis, GOG_AXIS_ELEM_MIN,	  "Min Value",		flags&0x01, q->data+ 0, log_scale);
	xl_axis_get_elem (sheet, s->axis, GOG_AXIS_ELEM_MAX,	  "Max Value",		flags&0x02, q->data+ 8, log_scale);
	xl_axis_get_elem (sheet, s->axis, GOG_AXIS_ELEM_MAJOR_TICK,  "Major Increment",	flags&0x04, q->data+16, log_scale);
	xl_axis_get_elem (sheet, s->axis, GOG_AXIS_ELEM_MINOR_TICK,  "Minor Increment",	flags&0x08, q->data+24, log_scale);
	cross = (flags & 0x10)
		? ((log_scale)? 1.: 0.)
		: ((log_scale)
		   ? (go_pow10 (gsf_le_get_double (q->data + 32)))
		   : gsf_le_get_double (q->data + 32));

	if (flags & 0x40) {
		g_object_set (s->axis, "invert-axis", TRUE, NULL);
		d (1, g_printerr ("Values in reverse order;\n"););
	}
	if (((flags & 0x80) != 0) ^ ((flags & 0x40) != 0)) {
		if (gog_axis_get_atype (GOG_AXIS (s->axis)) == GOG_AXIS_X)
			s->axis_cross_at_max = TRUE;
		else if (gog_axis_get_atype (GOG_AXIS (s->axis)) == GOG_AXIS_Y && s->xaxis)
			g_object_set (s->xaxis,
				      "pos-str", "high",
				      "cross-axis-id", gog_object_get_id (GOG_OBJECT (s->axis)),
				      NULL);
		d (1, g_printerr ("Cross over at max value;\n"););
	} else {
		if (gog_axis_get_atype (GOG_AXIS (s->axis)) == GOG_AXIS_X)
			s->axis_cross_value = cross;
		else if (gog_axis_get_atype (GOG_AXIS (s->axis)) == GOG_AXIS_Y && s->xaxis && !(flags & 0x10)) {
			GnmValue *value = value_new_float (cross);
			GnmExprTop const *texpr = gnm_expr_top_new_constant (value);
			g_object_set (s->xaxis,
				      "pos-str", "cross",
				      "cross-axis-id", gog_object_get_id (GOG_OBJECT (s->axis)),
				      NULL);
			gog_dataset_set_dim (GOG_DATASET (s->xaxis), GOG_AXIS_ELEM_CROSS_POINT,
				gnm_go_data_scalar_new_expr (sheet, texpr), NULL);
		}
		d (1, g_printerr ("Cross over point = %f\n", cross););
	}

	return FALSE;
}

/****************************************************************************/

static void
cb_store_singletons (gpointer indx, GOStyle *style, GogObject *series)
{
	GogObject *singleton = gog_object_add_by_name (series, "Point", NULL);
	if (singleton != NULL) {
		g_object_set (singleton,
			"index", GPOINTER_TO_UINT (indx),
			"style", style,
			NULL);
		if (g_object_class_find_property (G_OBJECT_GET_CLASS (singleton), "separation")) {
			gpointer sep = g_object_get_data (G_OBJECT (style), "pie-separation");
			g_object_set (singleton,
				"separation", (double)GPOINTER_TO_UINT (sep) / 100.,
				NULL);
		}
	}
}

static void
xl_axis_swap_elem (GogAxis *a, GogAxis *b, unsigned dim)
{
	GOData *a_dat = gog_dataset_get_dim (GOG_DATASET (a), dim);
	GOData *b_dat = gog_dataset_get_dim (GOG_DATASET (b), dim);

	if (a_dat != NULL) g_object_ref (a_dat);
	if (b_dat != NULL) g_object_ref (b_dat);
	gog_dataset_set_dim (GOG_DATASET (a), dim, b_dat, NULL);
	gog_dataset_set_dim (GOG_DATASET (b), dim, a_dat, NULL);
}

static void
object_swap_children (GogObject *a, GogObject *b, char const *name)
{
	GogObjectRole const *role;
	GSList *children_a, *children_b, *ptr;
	GogObject *obj;
	GOStyle *style;
	role = gog_object_find_role_by_name (a, name);
	g_return_if_fail (role);
	children_a = gog_object_get_children (a, role);
	children_b = gog_object_get_children (b, role);
	ptr = children_a;
	while (ptr) {
		obj = GOG_OBJECT (ptr->data);
		style = go_style_dup (
			go_styled_object_get_style (GO_STYLED_OBJECT (obj)));
		gog_object_clear_parent (obj);
		gog_object_add_by_role (b, role, obj);
		go_styled_object_set_style (GO_STYLED_OBJECT (obj), style);
		g_object_unref (style);
		ptr = ptr->next;
	}
	g_slist_free (children_a);
	ptr = children_b;
	while (ptr) {
		obj = GOG_OBJECT (ptr->data);
		style = go_style_dup (
			go_styled_object_get_style (GO_STYLED_OBJECT (obj)));
		gog_object_clear_parent (obj);
		gog_object_add_by_role (a, role, obj);
		go_styled_object_set_style (GO_STYLED_OBJECT (obj), style);
		g_object_unref (style);
		ptr = ptr->next;
	}
	g_slist_free (children_b);
}

static gboolean
BC_R(end)(XLChartHandler const *handle,
	  XLChartReadState *s, BiffQuery *q)
{
	int popped_state;

	d (0, g_printerr ("}\n"););

	g_return_val_if_fail (s->stack != NULL, TRUE);
	XL_CHECK_CONDITION_VAL (s->stack->len > 0, TRUE);

	popped_state = BC_R(top_state) (s, 0);
	s->stack = g_array_remove_index_fast (s->stack, s->stack->len-1);

	switch (popped_state) {
	case BIFF_CHART_axisparent :
		break;
	case BIFF_CHART_axis :
		if (s->style) {
			g_object_set (s->axis, "style", s->style, NULL);
			g_object_unref (s->style);
			s->style = NULL;
		}
		s->axis = NULL;
		break;

	case BIFF_CHART_legend :
		if (s->style) {
			g_object_set (s->legend, "style", s->style, NULL);
			g_object_unref (s->style);
			s->style = NULL;
		}
		s->legend = NULL;
		break;

	case BIFF_CHART_frame :
		if (s->style != NULL) {
			int top_state = BC_R(top_state) (s, 0);
			GogObject *obj = NULL;
			if (top_state == BIFF_CHART_legend)
				obj = s->legend;
			else if (top_state == BIFF_CHART_chart)
				obj = GOG_OBJECT (s->chart);
			else if (s->frame_for_grid) {
				GogGrid *tmp = gog_chart_get_grid (s->chart);
				obj = (tmp == NULL)
					? gog_object_add_by_name (GOG_OBJECT (s->chart), "Backplane", NULL)
					: GOG_OBJECT (tmp);
			}
			if (obj != NULL)
				g_object_set (G_OBJECT (obj),
					"style", s->style,
					NULL);
			g_object_unref (s->style);
			s->style = NULL;
		}
		s->xaxis = NULL;
		s->axis_cross_at_max = FALSE;
		break;

	case BIFF_CHART_series :
		g_return_val_if_fail (s->currentSeries != NULL, TRUE);
		s->currentSeries = NULL;
		break;

	case BIFF_CHART_chartformat : {
		unsigned i, j;
		XLChartSeries *eseries;
		GogSeries     *series;
		GOStyle      *style;
		gboolean	   plot_has_lines = FALSE, plot_has_marks = FALSE;
		GnmExprTop const *cat_expr = NULL;

		/* check series now and create 3d plot if necessary */
		if (s->is_surface) {
			gboolean is_matrix = TRUE, has_labels = FALSE;
			GnmValue *value;
			GnmRange vector;
			gboolean as_col = FALSE; /* makes gcc happy */
			GOData *cur;
			int row_start = 0, col_start = 0, row = 0, col = -1, last = 0;
			GSList *axisY, *axisZ, *l, *contributors, *ptr;

			/* exchange axis */
			l = axisY = gog_chart_get_axes (s->chart, GOG_AXIS_Y);
			axisZ = gog_chart_get_axes (s->chart, GOG_AXIS_Z);
			while (l) {
				contributors = g_slist_copy ((GSList*) gog_axis_contributors (GOG_AXIS (l->data)));
				gog_axis_clear_contributors (GOG_AXIS (l->data));
				if (gog_object_is_deletable (GOG_OBJECT (l->data)))
					gog_object_clear_parent (GOG_OBJECT (l->data));
				else {
					g_slist_free (contributors);
					continue;
				}
				g_object_set (G_OBJECT (l->data), "type",
					((s->is_contour)? GOG_AXIS_PSEUDO_3D: GOG_AXIS_Z), NULL);
				gog_object_add_by_name (GOG_OBJECT (s->chart),
					((s->is_contour)? "Pseudo-3D-Axis": "Z-Axis"),
					GOG_OBJECT (l->data));
				for (ptr = contributors; ptr != NULL; ptr = ptr->next)
					gog_plot_set_axis (GOG_PLOT (ptr->data), GOG_AXIS (l->data));
				g_slist_free (contributors);
				l = l->next;
			}
			g_slist_free (axisY);
			l = axisZ;
			while (l) {
				contributors = g_slist_copy ((GSList*) gog_axis_contributors (GOG_AXIS (l->data)));
				gog_axis_clear_contributors (GOG_AXIS (l->data));
				if (gog_object_is_deletable (GOG_OBJECT (l->data)))
					gog_object_clear_parent (GOG_OBJECT (l->data));
				else {
					g_slist_free (contributors);
					continue;
				}
				g_object_set (G_OBJECT (l->data), "type", GOG_AXIS_Y, NULL);
				gog_object_add_by_name (GOG_OBJECT (s->chart), "Y-Axis", GOG_OBJECT (l->data));
				for (ptr = contributors; ptr != NULL; ptr = ptr->next)
					gog_plot_set_axis (GOG_PLOT (ptr->data), GOG_AXIS (l->data));
				g_slist_free (contributors);
				l = l->next;
			}
			g_slist_free (axisZ);

			/* examine the first series to retrieve categories */
			if (s->series->len == 0)
				goto not_a_matrix;
			eseries = g_ptr_array_index (s->series, 0);
			style = eseries->style;
			if (GO_IS_DATA_VECTOR (eseries->data [GOG_MS_DIM_CATEGORIES].data)) {
				cat_expr = gnm_go_data_get_expr (eseries->data [GOG_MS_DIM_CATEGORIES].data);
				if (!gnm_expr_top_is_rangeref (cat_expr))
					goto not_a_matrix;
				if ((value = gnm_expr_top_get_range (cat_expr))) {
					as_col = value->v_range.cell.a.col == value->v_range.cell.b.col;
					row = row_start = value->v_range.cell.a.row;
					col = col_start = value->v_range.cell.a.col;
					if (as_col) {
						col++;
						row_start--;
						last = value->v_range.cell.b.row;
					}
					else {
						if (value->v_range.cell.a.row != value->v_range.cell.b.row) {
							value_release (value);
							goto not_a_matrix;
						}
						row++;
						col_start --;
						last = value->v_range.cell.b.col;
					}
					value_release (value);
				} else
					goto not_a_matrix;
			}
			/* verify that all series are adjacent, have same categories and
			same lengths */
			for (i = 0 ; i < s->series->len; i++ ) {
				GnmExprTop const *texpr;

				eseries = g_ptr_array_index (s->series, i);
				if (eseries->chart_group != s->plot_counter)
					continue;
				cur = eseries->data [GOG_MS_DIM_VALUES].data;
				if (!cur || !GO_IS_DATA_VECTOR (cur)) {
					is_matrix = FALSE;
					break;
				}

				texpr = gnm_go_data_get_expr (cur);
				if (!gnm_expr_top_is_rangeref (texpr))
					goto not_a_matrix;

				value = gnm_expr_top_get_range (texpr);
				if (value) {
					if (col == -1) {
						as_col = value->v_range.cell.a.col == value->v_range.cell.b.col;
						row = row_start = value->v_range.cell.a.row;
						col = col_start = value->v_range.cell.a.col;
						if (as_col) {
							last = value->v_range.cell.b.row;
						}
						else {
							if (value->v_range.cell.a.row != value->v_range.cell.b.row) {
								is_matrix = FALSE;
								value_release (value);
								break;
							}
							last = value->v_range.cell.b.col;
						}
					} else if ((as_col && (value->v_range.cell.a.col != col ||
							value->v_range.cell.b.col != col ||
							value->v_range.cell.a.row != row ||
							value->v_range.cell.b.row != last)) ||
							(! as_col && (value->v_range.cell.a.col != col ||
							value->v_range.cell.b.col != last ||
							value->v_range.cell.a.row != row ||
							value->v_range.cell.b.row != row))) {
						is_matrix = FALSE;
						value_release (value);
						break;
					}
					value_release (value);
				}

				cur = eseries->data [GOG_MS_DIM_LABELS].data;
				if (cur) {
					if(!GO_IS_DATA_SCALAR (cur)) {
						is_matrix = FALSE;
						break;
					}
					texpr = gnm_go_data_get_expr (cur);
					if (!gnm_expr_top_is_rangeref (texpr))
						goto not_a_matrix;
					value = gnm_expr_top_get_range (texpr);
					if (value) {
						if ((as_col && (value->v_range.cell.a.col != col ||
								value->v_range.cell.a.row != row_start)) ||
								(! as_col && (value->v_range.cell.a.col != col_start ||
								value->v_range.cell.a.row != row))) {
							is_matrix = FALSE;
							value_release (value);
							break;
						}
						value_release (value);
						has_labels = TRUE;
					}
				}
				cur = eseries->data [GOG_MS_DIM_CATEGORIES].data;
				if (cur && cat_expr &&
				    !gnm_expr_top_equal (gnm_go_data_get_expr (cur), cat_expr)) {
					is_matrix = FALSE;
					break;
				}
				if (as_col)
					col++;
				else
					row++;
			}
			if (is_matrix) {
				Sheet *sheet = ms_container_sheet (s->container.parent);
				s->plot = (GogPlot*) gog_plot_new_by_name ((s->is_contour)?
							"GogContourPlot": "GogSurfacePlot");

				/* build the series */
				series = gog_plot_new_series (s->plot);
				if (style != NULL)
					g_object_set (G_OBJECT (series),
						"style", style,
						NULL);
				if (as_col) {
					g_object_set (G_OBJECT (s->plot),
						"transposed", TRUE,
						NULL);
					col --;
					if (cat_expr != NULL) {
						vector.start.row = row;
						vector.start.col = vector.end.col = col_start;
						vector.end.row = last;
						gog_series_set_dim (series, 1,
							gnm_go_data_vector_new_expr (sheet,
								gnm_expr_top_new_constant (
									value_new_cellrange_r (sheet, &vector))), NULL);
						col_start++;
					}
					if (has_labels) {
						vector.start.col = col_start;
						vector.start.row = vector.end.row = row_start;
						vector.end.col = col;
						gog_series_set_dim (series, 0,
							gnm_go_data_vector_new_expr (sheet,
								gnm_expr_top_new_constant (
									value_new_cellrange_r (sheet, &vector))), NULL);

						row_start++;
					}
					vector.start.row = row_start;
					vector.end.row = last;
					vector.start.col = col_start;
					vector.end.col = col;
					gog_series_set_dim (series, 2,
						gnm_go_data_matrix_new_expr (sheet,
							gnm_expr_top_new_constant (
								value_new_cellrange_r (sheet, &vector))), NULL);
				} else {
					row--;
					if (cat_expr != NULL) {
						vector.start.col = col;
						vector.start.row = vector.end.row = row_start;
						vector.end.col = last;
						gog_series_set_dim (series, 0,
							gnm_go_data_vector_new_expr (sheet,
								gnm_expr_top_new_constant (
									value_new_cellrange_r (sheet, &vector))), NULL);
						row_start++;
					}
					if (has_labels) {
						vector.start.row = row_start;
						vector.start.col = vector.end.col = col_start;
						vector.end.row = row;
						gog_series_set_dim (series, 1,
							gnm_go_data_vector_new_expr (sheet,
								gnm_expr_top_new_constant (
									value_new_cellrange_r (sheet, &vector))), NULL);
						col_start++;
					}
					vector.start.col = col_start;
					vector.end.col = last;
					vector.start.row = row_start;
					vector.end.row = row;
					gog_series_set_dim (series, 2,
						gnm_go_data_matrix_new_expr (sheet,
							gnm_expr_top_new_constant (
								value_new_cellrange_r (sheet, &vector))), NULL);
				}
			} else {
not_a_matrix:
				s->is_surface = FALSE;
				s->plot = (GogPlot*) gog_plot_new_by_name ((s->is_contour)?
							"XLContourPlot": "XLSurfacePlot");
			}
		}

		XL_CHECK_CONDITION_VAL (s->plot != NULL, TRUE);

		/*
		 * Check whether the chart already contains a plot and, if so,
		 * if axis sets are compatible
		 */
		{
			GogAxisSet axis_set = gog_chart_get_axis_set (s->chart);
			GogAxisSet plot_axis_set = G_TYPE_INSTANCE_GET_CLASS ((s->plot), GOG_TYPE_PLOT, GogPlotClass)->axis_set;

			if (axis_set != GOG_AXIS_SET_UNKNOWN &&
			    (axis_set & GOG_AXIS_SET_FUNDAMENTAL & ~plot_axis_set)) {
				GogAxisType i;
				GogAxisSet j;

				g_object_unref (s->plot);
				s->plot = NULL;
				if (s->style) {
					g_object_unref (s->style);
					s->style = NULL;
				}

				/* Now remove all unwanted axes.  */
				for (i = 0, j = 1;
				     i < GOG_AXIS_VIRTUAL;
				     i++, j <<= 1) {
					if ((j & axis_set) == 0) {
						GSList *l = gog_chart_get_axes (s->chart, i), *cur;
						for (cur = l; cur; cur = cur->next) {
							GogObject *axis = cur->data;
							/* first remove contributors otherwise we'll end
							 * with invalid pointers */
							gog_axis_clear_contributors (GOG_AXIS (axis));
							/* remove only if there are no more contributors */
							if (gog_object_is_deletable (axis)) {
								gog_object_clear_parent (axis);
								g_object_unref (axis);
							}
						}
						g_slist_free (l);
					}

				}
				return TRUE;
			}
		}

		/* Add _before_ setting styles so theme does not override */
		gog_object_add_by_name (GOG_OBJECT (s->chart),
					"Plot", GOG_OBJECT (s->plot));

		if (s->default_plot_style != NULL) {
			char const *type = G_OBJECT_TYPE_NAME (s->plot);
			GOStyle const *style = s->default_plot_style;

			if (type != NULL && style->marker.mark != NULL &&
			    (!strcmp (type, "GogXYPlot") ||
			     !strcmp (type, "GogLinePlot") ||
			     !strcmp (type, "GogRadarPlot"))) {
				plot_has_marks =
					(go_marker_get_shape (style->marker.mark) != GO_MARKER_NONE)
					|| style->marker.auto_shape;
				g_object_set (G_OBJECT (s->plot),
					"default-style-has-markers",
					plot_has_marks,
					NULL);
			}
			if (type != NULL && 0 == strcmp (type, "GogXYPlot")) {
				plot_has_lines = style->line.dash_type != GO_LINE_NONE;
				g_object_set (G_OBJECT (s->plot),
					"default-style-has-lines",
					style->line.width >= 0 &&
					plot_has_lines,
					NULL);
			}

			g_object_unref (s->default_plot_style);
			s->default_plot_style = NULL;
		}

		if (!s->is_surface) {
			unsigned k = 0, l = s->series->len, added_plots = 0, n;
			if (l > 0 && s->dropbar) {
				GogObject *plot = GOG_OBJECT (gog_plot_new_by_name ("GogDropBarPlot"));
				if (s->has_extra_dataformat){
					g_object_set (G_OBJECT (plot), "plot-group", "GogStockPlot", NULL);
				}
				l--;
				while (eseries = g_ptr_array_index (s->series, l),
							eseries->chart_group != s->plot_counter)
					if (l != 0)
						l--;
					else {
						eseries = NULL;
							break;
					}
				gog_object_add_by_name (GOG_OBJECT (s->chart), "Plot", plot);
				gog_object_reorder (plot, TRUE, FALSE);
				added_plots++;
				series = gog_plot_new_series (GOG_PLOT (plot));
				g_object_set (plot, "gap_percentage", s->dropbar_width, NULL);
				if (eseries) {
					if (eseries->data [GOG_MS_DIM_CATEGORIES].data != NULL) {
						gog_series_set_XL_dim (series, GOG_MS_DIM_CATEGORIES,
							eseries->data [GOG_MS_DIM_CATEGORIES].data, NULL);
						eseries->data [GOG_MS_DIM_CATEGORIES].data = NULL;
					}
					if (eseries->data [GOG_MS_DIM_VALUES].data != NULL) {
						gog_series_set_XL_dim (series, GOG_MS_DIM_END,
							eseries->data [GOG_MS_DIM_VALUES].data, NULL);
						eseries->data [GOG_MS_DIM_VALUES].data = NULL;
					} else
						eseries->extra_dim = GOG_MS_DIM_END;
				}
				eseries = NULL;
				while (k < l && (eseries = g_ptr_array_index (s->series, k++),
							eseries && eseries->chart_group != s->plot_counter))
					if (k == l) {
						eseries = NULL;
						break;
					}
				if (eseries) {
					if (eseries->data [GOG_MS_DIM_VALUES].data != NULL) {
						gog_series_set_XL_dim (series, GOG_MS_DIM_START,
							eseries->data [GOG_MS_DIM_VALUES].data, NULL);
						eseries->data [GOG_MS_DIM_VALUES].data = NULL;
					} else
						eseries->extra_dim = GOG_MS_DIM_START;
				}
				g_object_set (G_OBJECT (series),
					"style", s->dropbar_style,
					NULL);
				g_object_unref (s->dropbar_style);
				s->dropbar_style = NULL;
			}
			if (l > 0 && s->hilo) {
				GogObject *plot = GOG_OBJECT (gog_plot_new_by_name ("GogMinMaxPlot"));
				if (s->has_extra_dataformat) {
					g_object_set (G_OBJECT (plot), "plot-group", "GogStockPlot", NULL);
					g_object_set (G_OBJECT (s->plot), "plot-group", "GogStockPlot", NULL);
				}
				gog_object_add_by_name (GOG_OBJECT (s->chart), "Plot", plot);
				for (n = 0; n <= added_plots; n++)
					gog_object_reorder (plot, TRUE, FALSE);
				series = gog_plot_new_series (GOG_PLOT (plot));
				eseries = NULL;
				while (k < l && (eseries = g_ptr_array_index (s->series, k++),
							eseries && eseries->chart_group != s->plot_counter))
					if (k == l) {
						eseries = NULL;
						break;
					}
				if (eseries != NULL) {
					if (eseries->data [GOG_MS_DIM_CATEGORIES].data != NULL) {
						gog_series_set_XL_dim (series, GOG_MS_DIM_CATEGORIES,
							eseries->data [GOG_MS_DIM_CATEGORIES].data, NULL);
						eseries->data [GOG_MS_DIM_CATEGORIES].data = NULL;
					}
					if (eseries->data [GOG_MS_DIM_VALUES].data != NULL) {
						gog_series_set_XL_dim (series, GOG_MS_DIM_HIGH,
							eseries->data [GOG_MS_DIM_VALUES].data, NULL);
						eseries->data [GOG_MS_DIM_VALUES].data = NULL;
					} else
						eseries->extra_dim = GOG_MS_DIM_HIGH;
				}
				if (k == s->series->len)
					eseries = NULL;
				else while (eseries = g_ptr_array_index (s->series, k++),
							eseries && eseries->chart_group != s->plot_counter) {
					if (k == s->series->len) {
						eseries = NULL;
						break;
					}
				}
				if (eseries != NULL) {
					if (eseries->data [GOG_MS_DIM_VALUES].data != NULL) {
						gog_series_set_XL_dim (series, GOG_MS_DIM_LOW,
							eseries->data [GOG_MS_DIM_VALUES].data, NULL);
						eseries->data [GOG_MS_DIM_VALUES].data = NULL;
					} else
						eseries->extra_dim = GOG_MS_DIM_LOW;
					if (s->chartline_style[1]) {
						g_object_set (G_OBJECT (series),
								  "style", s->chartline_style[1],
								  NULL);
						g_object_unref (s->chartline_style[1]);
						s->chartline_style[1] = NULL;
					}
				}
			}
			for (i = k ; i < l; i++ ) {
				eseries = g_ptr_array_index (s->series, i);
				if (eseries->chart_group != s->plot_counter)
					continue;
				series = gog_plot_new_series (s->plot);
				for (j = 0 ; j < GOG_MS_DIM_TYPES; j++ )
					if (eseries->data [j].data != NULL) {
						gog_series_set_XL_dim (series, j,
							eseries->data [j].data, NULL);
						eseries->data [j].data = NULL;
					}
				eseries->series = series;
				style = eseries->style;
				if (style != NULL) {
					if (s->hilo || s->dropbar) {
						/* This might be a stock plot, so don't use auto
						styles for lines */
						style->line.auto_dash = FALSE;
						style->marker.auto_shape = FALSE;
						/* These avoid warnings when exporting, do we really care? */
						style->fill.auto_fore = FALSE;
						style->fill.auto_back = FALSE;
					} else {
						if (!plot_has_marks &&
							go_marker_get_shape (style->marker.mark) != GO_MARKER_NONE)
							style->marker.auto_shape = FALSE;
						if (!plot_has_lines && style->line.dash_type != GO_LINE_NONE)
							style->line.auto_dash = FALSE;
					}
					g_object_set (G_OBJECT (series),
						"style", style,
						NULL);
				}
				if (!eseries->has_legend)
					g_object_set (G_OBJECT (series),
						"has-legend", FALSE,
						NULL);
				if (eseries->singletons != NULL)
					g_hash_table_foreach (eseries->singletons,
						(GHFunc) cb_store_singletons, series);
				g_object_set (G_OBJECT (series),
					      "interpolation", go_line_interpolation_as_str (eseries->interpolation),
					      NULL);
			}
		}
		g_object_set (G_OBJECT (s->plot),
			      "interpolation", go_line_interpolation_as_str (s->interpolation),
			      NULL);
		s->interpolation = GO_LINE_INTERPOLATION_LINEAR;
		s->hilo = FALSE;
		s->dropbar = FALSE;

		/* Vile cheesy hack.
		 * XL stores axis as 'value' and 'category' whereas we use X and Y.
		 * When importing bar plots things are transposed, but we do
		 * not know it until we import the plot.  Swap the contents of the axes */
		if (0 == strcmp (G_OBJECT_TYPE_NAME (s->plot), "GogBarColPlot")) {
			gboolean horizontal;
			g_object_get (s->plot, "horizontal", &horizontal, NULL);
			if (horizontal) {
				GogAxis	*x = gog_plot_get_axis (s->plot, GOG_AXIS_X);
				GogAxis	*y = gog_plot_get_axis (s->plot, GOG_AXIS_Y);
				GOStyle *x_style, *y_style;
				int i;
				if (x != NULL && y!= NULL) {
					/* we only execute that code if both axes really exist, see #702126 */
					for (i = 0 ; i < GOG_AXIS_ELEM_MAX_ENTRY ; i++)
						xl_axis_swap_elem (x, y, i);
					g_object_get (G_OBJECT (x), "style", &x_style, NULL);
					g_object_get (G_OBJECT (y), "style", &y_style, NULL);
					g_object_set (G_OBJECT (y), "style", x_style, NULL);
					g_object_set (G_OBJECT (x), "style", y_style, NULL);
					g_object_unref (x_style);
					g_object_unref (y_style);
					/* we must also exchange children */
					object_swap_children (GOG_OBJECT (x), GOG_OBJECT (y), "Label");
					object_swap_children (GOG_OBJECT (x), GOG_OBJECT (y), "MajorGrid");
					object_swap_children (GOG_OBJECT (x), GOG_OBJECT (y), "MinorGrid");
				}
			}
		}
		if (g_slist_length (s->plot->series) == 0) {
			gog_object_clear_parent (GOG_OBJECT (s->plot));
			g_object_unref (s->plot);
		}		s->plot = NULL;
		break;
	}

	case BIFF_CHART_dataformat :
		if (s->style == NULL)
			break;
		if (s->currentSeries != NULL) {
			if (s->style_element < 0) {
				g_return_val_if_fail (s->currentSeries->style == NULL, TRUE);
				s->currentSeries->style = s->style;
			} else {
				if (s->currentSeries->singletons == NULL)
					s->currentSeries->singletons = g_hash_table_new_full (
						g_direct_hash, g_direct_equal, NULL, g_object_unref);
				g_hash_table_insert (s->currentSeries->singletons,
					GUINT_TO_POINTER (s->style_element), s->style);
				d (0, g_printerr ("/* STORE singleton style */\n"););
			}
		} else if (s->plot != NULL) {
			g_return_val_if_fail (s->default_plot_style == NULL, TRUE);
			s->default_plot_style = s->style;
		} else
			g_object_unref (s->style);
		s->style = NULL;
		break;

	case BIFF_CHART_text : {
		gboolean clear_style = TRUE;
		switch (BC_R(top_state) (s, 0)) {
		case BIFF_CHART_legend:
			/* do not destroy the style if it belongs to the
			parent object, applies only to legend at the moment,
			what should be done for DEFAULTEXT? */
			clear_style = FALSE;
			break;
		default:
			break;
		}
		if (s->label) {
			g_object_unref (s->label);
			s->label = NULL;
		}

		if (clear_style && s->style != NULL) {
			g_object_unref (s->style);
			s->style = NULL;
		}
		g_free (s->text);
		s->text = NULL;
		break;
	}

	case BIFF_CHART_dropbar :
		if (s->style != NULL) {
			if (s->dropbar_style == NULL) {
				/* automatic styles are very different in that case between
				excel and gnumeric, so set appropriate auto* to FALSE */
				s->style->fill.auto_back = FALSE;
				s->style->fill.auto_fore = FALSE;
				s->dropbar_style = s->style;
			} else
				g_object_unref (s->style);
			s->style = NULL;
		}
		break;

	case BIFF_CHART_chart : {
		GogPlot *plot = GOG_PLOT (gog_object_get_child_by_name (GOG_OBJECT (s->chart), "Plot"));
 		/* check if the chart has an epty title and the plot only one series,
		 * in that case Excel uses the series label as title */
		if (plot && g_slist_length (plot->series) == 1) {
			GogObject *title = gog_object_get_child_by_name (GOG_OBJECT (s->chart), "Title");
			if (title) {
				GOData *dat = gog_dataset_get_dim (GOG_DATASET (title), 0);
				GError *err = NULL;
				if (dat) {
					char *str = go_data_get_scalar_string (dat);
					if (str && *str) {
						g_free (str);
						break;
					}
					g_free (str);
				}
				dat = gog_dataset_get_dim (GOG_DATASET (plot->series->data), -1);
				if (!dat)
					break;
				gog_dataset_set_dim (GOG_DATASET (title), 0, GO_DATA (g_object_ref (dat)), &err);
				if (err)
					g_error_free (err);
			}
		}
	}
	default :
		break;
	}
	return FALSE;
}

/****************************************************************************/

static XLChartHandler const *chart_biff_handler[256];

static void
BC(register_handler)(XLChartHandler const *const handle);
#define BIFF_CHART(name, size) \
{	static XLChartHandler const handle = { \
	BIFF_CHART_ ## name, size, #name, & BC_R(name) }; \
	BC(register_handler)(& handle); \
}

static void
BC(register_handlers)(void)
{
	static gboolean already_initialized = FALSE;
	int i;
	if (already_initialized)
		return;
	already_initialized = TRUE;

	/* Init the handles */
	i = G_N_ELEMENTS (chart_biff_handler);
	while (--i >= 0)
		chart_biff_handler[i] = NULL;

	BIFF_CHART(3dbarshape, 2);	/* Unknown, seems to be 2 */
	BIFF_CHART(3d, 14);
	BIFF_CHART(ai, 8);
	BIFF_CHART(alruns, 2);
	BIFF_CHART(area, 2);
	BIFF_CHART(areaformat, 12);
	BIFF_CHART(attachedlabel, 2);
	BIFF_CHART(axesused, 2);
	BIFF_CHART(axis, 18);
	BIFF_CHART(axcext, 18);
	BIFF_CHART(axislineformat, 2);
	BIFF_CHART(axisparent, 18);
	BIFF_CHART(bar, 6);
	BIFF_CHART(begin, 0);
	BIFF_CHART(boppop, 18);
	BIFF_CHART(boppopcustom, 2);
	BIFF_CHART(catserrange, 8);
	BIFF_CHART(chart, 16);
	BIFF_CHART(chartformat, 20);
	BIFF_CHART(chartformatlink, 0);
	BIFF_CHART(chartline, 2);
	BIFF_CHART(clrtclient, 0);	/* Unknown */
	BIFF_CHART(dat, 2);
	BIFF_CHART(dataformat, 8);
	BIFF_CHART(defaulttext, 2);
	BIFF_CHART(dropbar, 2);
	BIFF_CHART(end, 0);
	BIFF_CHART(fbi, 10);
	BIFF_CHART(fontx, 2);
	BIFF_CHART(frame, 4);
	BIFF_CHART(gelframe, 0);
	BIFF_CHART(ifmt, 2);
	BIFF_CHART(legend, 20);
	BIFF_CHART(legendxn, 4);
	BIFF_CHART(line, 2);
	BIFF_CHART(lineformat, 10);
	BIFF_CHART(markerformat, 12);
	BIFF_CHART(objectlink, 6);
	BIFF_CHART(picf, 14);
	BIFF_CHART(pie, 4);
	BIFF_CHART(pieformat, 2);
	BIFF_CHART(plotarea, 0);
	BIFF_CHART(plotgrowth, 8);
	BIFF_CHART(pos, 20); /* For all states */
	BIFF_CHART(radar, 2);
	BIFF_CHART(radararea, 2);
	BIFF_CHART(sbaseref, 8);
	BIFF_CHART(scatter, 0);
	BIFF_CHART(serauxerrbar, 14);
	BIFF_CHART(serauxtrend, 28);
	BIFF_CHART(serfmt, 2);
	BIFF_CHART(series, 8);
	BIFF_CHART(serieslist, 2);
	BIFF_CHART(seriestext, 3);
	BIFF_CHART(serparent, 2);
	BIFF_CHART(sertocrt, 2);
	BIFF_CHART(shtprops, 3);
	BIFF_CHART(siindex, 2);
	BIFF_CHART(surf, 2);
	BIFF_CHART(text, 26);
	BIFF_CHART(tick, 26);
	BIFF_CHART(units, 2);
	BIFF_CHART(valuerange, 42);
	/* gnumeric specific */
	BIFF_CHART(trendlimits, 17);
}

/*
 *This is a temporary routine.  I wanted each handler in a separate function
 *to avoid massive nesting.  While experimenting with the real (vs MS
 *documentation) structure of a saved chart, this form offers maximum error
 *checking.
 */
static void
BC(register_handler)(XLChartHandler const *const handle)
{
	unsigned const num_handler = G_N_ELEMENTS (chart_biff_handler);
	guint32 num = handle->opcode & 0xff;

	if (num >= num_handler)
		g_printerr ("Invalid BIFF_CHART handler (%x)\n", handle->opcode);
	else if (chart_biff_handler[num])
		g_printerr ("Multiple BIFF_CHART handlers for (%x)\n",
			handle->opcode);
	else
		chart_biff_handler[num] = handle;
}

static gboolean
chart_realize_obj (MSContainer *container, MSObj *obj)
{
	g_warning ("Dropping nested object");
	return FALSE;
}

static SheetObject *
chart_create_obj  (MSContainer *container, MSObj *obj)
{
	return (container &&
		container->parent &&
		container->parent->vtbl->create_obj)
		? container->parent->vtbl->create_obj (container->parent, obj)
		: NULL;
}

static GnmExprTop const *
chart_parse_expr  (MSContainer *container, guint8 const *data, int length)
{
	return excel_parse_formula (container, NULL, 0, 0,
				    data, length, 0 /* FIXME? */,
				    FALSE, NULL);
}

static Sheet *
chart_get_sheet (MSContainer const *container)
{
	return ms_container_sheet (container->parent);
}

static void
xl_chart_import_trend_line (XLChartReadState *state, XLChartSeries *series)
{
	GogTrendLine *rc;
	Sheet *sheet;
	XLChartSeries *parent;

	XL_CHECK_CONDITION ((unsigned) series->reg_parent < state->series->len);
	parent = g_ptr_array_index (state->series, series->reg_parent);

	XL_CHECK_CONDITION (parent != NULL && parent->series != NULL);

	switch (series->reg_type) {
	case 0:
		if (series->reg_order == 1)
			rc = gog_trend_line_new_by_name ("GogLinRegCurve");
		else {
			rc = gog_trend_line_new_by_name ("GogPolynomRegCurve");
			g_object_set (G_OBJECT (rc), "dims", series->reg_order, NULL);
		}
		break;
	case 1:
		rc = gog_trend_line_new_by_name ("GogExpRegCurve");
		break;
	case 2:
		rc = gog_trend_line_new_by_name ("GogLogRegCurve");
		break;
	case 3:
		rc = gog_trend_line_new_by_name ("GogPowerRegCurve");
		break;
	case 4:
		rc = gog_trend_line_new_by_name ("GogMovingAvg");
			g_object_set (G_OBJECT (rc), "span", series->reg_order, "xavg", FALSE, NULL);
		break;
	default:
		g_warning ("Unknown trend line type: %d", series->reg_type);
		rc = NULL;
		break;
	}
	if (rc) {
		if (GOG_IS_REG_CURVE (rc)) {
			sheet = ms_container_sheet (state->container.parent);
			g_object_set (G_OBJECT (rc),
				"affine", series->reg_intercept != 0.,
				"skip-invalid", series->reg_skip_invalid,
				NULL);
			if (sheet) {
				if (series->reg_dims[0]){
					gog_dataset_set_dim (GOG_DATASET (rc), 0, series->reg_dims[0], NULL);
					series->reg_dims[0] = NULL;
				} else if (go_finite (series->reg_min)) {
					GnmValue *value = value_new_float (series->reg_min);
					GnmExprTop const *texpr = gnm_expr_top_new_constant (value);
					GOData *data = gnm_go_data_scalar_new_expr (sheet, texpr);
					gog_dataset_set_dim (GOG_DATASET (rc), 0, data, NULL);
				}
				if (series->reg_dims[1]){
					gog_dataset_set_dim (GOG_DATASET (rc), 1, series->reg_dims[1], NULL);
					series->reg_dims[1] = NULL;
				} else if (go_finite (series->reg_max)) {
					GnmValue *value = value_new_float (series->reg_max);
					GnmExprTop const *texpr = gnm_expr_top_new_constant (value);
					GOData *data = gnm_go_data_scalar_new_expr (sheet, texpr);
					gog_dataset_set_dim (GOG_DATASET (rc), 1, data, NULL);
				}
			}
			if (series->reg_show_eq || series->reg_show_R2) {
				GogObject *obj = gog_object_add_by_name (
					GOG_OBJECT (rc), "Equation", NULL);
				g_object_set (G_OBJECT (obj),
					"show_eq", series->reg_show_eq,
					"show_r2", series->reg_show_R2,
					NULL);
			}
		}
		gog_object_add_by_name (GOG_OBJECT (parent->series),
			"Trend line", GOG_OBJECT (rc));
		if (series->style)
			go_styled_object_set_style (GO_STYLED_OBJECT (rc), series->style);
	}
}

static void
xl_chart_import_error_bar (XLChartReadState *state, XLChartSeries *series)
{
	unsigned p = series->err_parent;
	XLChartSeries *parent;
	Sheet *sheet;
	int orig_dim;
	GogMSDimType msdim;
	char const *prop_name = (series->err_type < 3)
		? "x-errors" : "y-errors";
	GParamSpec *pspec;

	XL_CHECK_CONDITION (p < state->series->len);

	parent = g_ptr_array_index (state->series, p);
	XL_CHECK_CONDITION (parent != NULL && parent->series != NULL);

	pspec = g_object_class_find_property (
		G_OBJECT_GET_CLASS (parent->series),
		prop_name);
	state->plot = parent->series->plot;
	if (pspec == NULL) {
		pspec = g_object_class_find_property (
			G_OBJECT_GET_CLASS (parent->series), "errors");
		prop_name = (pspec)? "errors": NULL;
		msdim = (series->err_type < 3)
			? GOG_MS_DIM_TYPES + series->err_type
			: GOG_MS_DIM_TYPES + series->err_type - 2;
	} else
		msdim = (series->err_type < 3)
			? GOG_MS_DIM_TYPES + series->err_type + 2
			: GOG_MS_DIM_TYPES + series->err_type - 2;

	sheet = ms_container_sheet (state->container.parent);
	if (sheet && parent && prop_name) {
		GogErrorBar   *error_bar;
		GOData	      *data;

		g_object_get (G_OBJECT (parent->series),
			      prop_name, &error_bar,
			      NULL);
		if (!error_bar) {
			error_bar = g_object_new (GOG_TYPE_ERROR_BAR, NULL);
			error_bar->display = GOG_ERROR_BAR_DISPLAY_NONE;
		}
		error_bar->display |= (series->err_type & 1)
			? GOG_ERROR_BAR_DISPLAY_POSITIVE
			: GOG_ERROR_BAR_DISPLAY_NEGATIVE;
		if (!series->err_teetop)
			error_bar->width = 0;
		if (check_style (series->style, "error bar")) {
			g_object_unref (error_bar->style);
			error_bar->style = go_style_dup (series->style);
		}
		switch (series->err_src) {
		case 1: {
			/* percentage */
			GnmExprTop const *texpr =
				gnm_expr_top_new_constant (
					value_new_float (series->err_val));
			error_bar->type = GOG_ERROR_BAR_TYPE_PERCENT;
			data = gnm_go_data_vector_new_expr (sheet, texpr);
			gog_series_set_XL_dim (parent->series, msdim, data, NULL);
			break;
		}
		case 2: {
			/* fixed value */
			GnmExprTop const *texpr =
				gnm_expr_top_new_constant (
					value_new_float (series->err_val));
			error_bar->type = GOG_ERROR_BAR_TYPE_ABSOLUTE;
			data = gnm_go_data_vector_new_expr (sheet, texpr);
			gog_series_set_XL_dim (parent->series, msdim, data, NULL);
			break;
		}
		case 3:
			/* not supported */
			break;
		case 4:
			orig_dim = (series->err_type < 3)
				? GOG_MS_DIM_CATEGORIES
				: GOG_MS_DIM_VALUES;
			error_bar->type = GOG_ERROR_BAR_TYPE_ABSOLUTE;
			if (series->data[orig_dim].data) {
				gog_series_set_XL_dim (parent->series, msdim,
							series->data[orig_dim].data, NULL);
				series->data[orig_dim].data = NULL;
			} else if (series->data[orig_dim].value) {
				GnmExprTop const *texpr =
					gnm_expr_top_new_constant ((GnmValue *)
								   series->data[orig_dim].value);
				series->data[orig_dim].value = NULL;
				data = gnm_go_data_vector_new_expr (sheet, texpr);
				gog_series_set_XL_dim (parent->series, msdim, data, NULL);
			}
			break;
		default:
			/* type 5, standard error is not supported */
			break;
		}
		g_object_set (G_OBJECT (parent->series),
						prop_name, error_bar,
						NULL);
		g_object_unref (error_bar);
	}
}

static void
ms_excel_chart_read_PROTECT (BiffQuery *q, XLChartReadState *state)
{
	gboolean is_protected;

	XL_CHECK_CONDITION (q->length >= 2);
	is_protected = (1 == GSF_LE_GET_GUINT16 (q->data));
	d (4, g_printerr ("Chart is%s protected;\n",
			  is_protected ? "" : " not"););
}

static void
ms_excel_chart_read_NUMBER (BiffQuery *q, XLChartReadState *state, size_t ofs)
{
	unsigned row, sernum;
	double val;
	XLChartSeries *series;

	XL_CHECK_CONDITION (q->length >= ofs + 8);
	row = GSF_LE_GET_GUINT16 (q->data);
	sernum = GSF_LE_GET_GUINT16 (q->data + 2);
	val = gsf_le_get_double (q->data + ofs);

	if (state->series == NULL || state->cur_role < 0)
		return;
	XL_CHECK_CONDITION (state->cur_role < GOG_MS_DIM_TYPES);
	XL_CHECK_CONDITION (sernum < state->series->len);

	series = g_ptr_array_index (state->series, sernum);
	if (series == NULL)
		return;

	if (series->data[state->cur_role].value != NULL) {
		XL_CHECK_CONDITION (row < (guint)series->data[state->cur_role].num_elements);

		value_release (series->data[state->cur_role].value->vals[0][row]);
		series->data[state->cur_role].value->vals[0][row] = value_new_float (val);
	}
	d (10, g_printerr ("series %d, index %d, value %f\n", sernum, row, val););
}

static void
ms_excel_chart_read_LABEL (BiffQuery *q, XLChartReadState *state)
{
	guint16 row, sernum;
	char *label;
	XLChartSeries *series;

	XL_CHECK_CONDITION (q->length >= 6);
	row = GSF_LE_GET_GUINT16 (q->data + 0);
	sernum = GSF_LE_GET_GUINT16 (q->data + 2);
	/* xf  = GSF_LE_GET_GUINT16 (q->data + 4); */

	if (state->series == NULL || state->cur_role < 0)
		return;
	XL_CHECK_CONDITION (state->cur_role < GOG_MS_DIM_TYPES);
	XL_CHECK_CONDITION (sernum < state->series->len);

	series = g_ptr_array_index (state->series, sernum);
	if (series == NULL)
		return;

	label = excel_biff_text_2 (state->container.importer, q, 6);
	if (label != NULL  &&
	    series->data[state->cur_role].value != NULL) {
		XL_CHECK_CONDITION (row < (guint)series->data[state->cur_role].num_elements);

		value_release (series->data[state->cur_role].value->vals[0][row]);
		series->data[state->cur_role].value->vals[0][row] = value_new_string (label);
	}
	d (10, {g_printerr ("'%s' row = %d, series = %d\n", label, row, sernum);});
	g_free (label);
}


gboolean
ms_excel_chart_read (BiffQuery *q, MSContainer *container,
		     SheetObject *sog, Sheet *full_page)
{
	static MSContainerClass const vtbl = {
		chart_realize_obj,
		chart_create_obj,
		chart_parse_expr,
		chart_get_sheet,
		NULL
	};
	int const num_handler = G_N_ELEMENTS (chart_biff_handler);
	int i;
	gboolean done = FALSE;
	XLChartReadState state;

	/* Register the handlers if this is the 1st time through */
	BC(register_handlers)();

	/* FIXME : create an anchor parser for charts */
	ms_container_init (&state.container, &vtbl,
		container, container->importer);

	state.stack	    = g_array_new (FALSE, FALSE, sizeof(int));
	state.prev_opcode   = 0xdeadbeef; /* Invalid */
	state.currentSeries = NULL;
	state.series	    = g_ptr_array_new ();
	state.plot_counter  = -1;
	state.has_a_grid    = FALSE;
	state.label	    = NULL;
	state.text	    = NULL;
	state.is_contour	= FALSE;
	state.is_surface	= FALSE;
	state.cur_role = -1;
	state.parent_index = 0;
	state.hilo = FALSE;
	state.dropbar = FALSE;
	state.has_extra_dataformat = FALSE;
	state.axis_cross_at_max = FALSE;
	state.axis_cross_value = go_nan;
	state.xaxis = NULL;
	state.interpolation = GO_LINE_INTERPOLATION_LINEAR;
	state.error = FALSE;
	state.style_element = -1;

	if (NULL != (state.sog = sog)) {
		state.graph = sheet_object_graph_get_gog (sog);
		state.chart = GOG_CHART (gog_object_add_by_name (GOG_OBJECT (state.graph), "Chart", NULL));

		if (NULL != full_page) {
			GOStyle *style = (GOStyle *) gog_style_new ();
			style->line.width = 0;
			style->line.dash_type = GO_LINE_NONE;
			style->fill.type = GO_STYLE_FILL_NONE;
			g_object_set (G_OBJECT (state.graph), "style", style, NULL);
			g_object_set (G_OBJECT (state.chart), "style", style, NULL);
			g_object_unref (style);
		}
	} else {
		state.graph = NULL;
		state.chart = NULL;
	}
	state.default_plot_style = NULL;
	state.plot  = NULL;
	state.axis  = NULL;
	state.style = NULL;
	state.legend = NULL;
	for (i = 0; i < 3; i++)
		state.chartline_style[i] = NULL;
	state.dropbar_style = NULL;

	d (0, g_printerr ("{ /* CHART */\n"););

	while (!done && ms_biff_query_next (q)) {
		/* Use registered jump table for chart records */
		if ((q->opcode & 0xff00) == 0x1000) {
			int const lsb = q->opcode & 0xff;
			int const begin_end =
				(q->opcode == BIFF_CHART_begin ||
				 q->opcode == BIFF_CHART_end);

			if (lsb >= num_handler ||
			    !chart_biff_handler [lsb] ||
			    chart_biff_handler  [lsb]->opcode != q->opcode) {
				d (0, {	g_printerr ("Unknown BIFF_CHART record\n");
					ms_biff_query_dump (q);});
			} else {
				XLChartHandler const *const h =
					chart_biff_handler [lsb];

				if (state.graph	== NULL) {
					g_warning ("File is most like corrupted.\n"
						   "Ignoring spurious chart record (%s).",
						   h->name);
				} else if (q->length < h->min_size) {
					g_warning ("File is most like corrupted.\n"
						   "Ignoring truncated %s record with length %u < %u",
						   h->name, q->length, h->min_size);
				} else {
					d (0, { if (!begin_end)
							g_printerr ("%s(\n", h->name); });
					(void)(*h->read_fn)(h, &state, q);
					d (0, { if (!begin_end)
							g_printerr (");\n"); });
				}
			}
		} else switch (q->opcode) {
		case BIFF_EOF:
			done = TRUE;
			d (0, g_printerr ("}; /* CHART */\n"););
			if (state.stack->len > 0)
				state.error = TRUE;
			break;

		case BIFF_PROTECT:
			ms_excel_chart_read_PROTECT (q, &state);
			break;

		case BIFF_BLANK_v0:
		case BIFF_BLANK_v2: /* Stores a missing value in the inline value tables */
			break;

		case BIFF_NUMBER_v0:
			ms_excel_chart_read_NUMBER (q, &state, 7);
			break;
		case BIFF_NUMBER_v2:
			ms_excel_chart_read_NUMBER (q, &state, 6);
			break;

		case BIFF_LABEL_v0: break; /* ignore for now */
		case BIFF_LABEL_v2:
			ms_excel_chart_read_LABEL (q, &state);
			break;

		case BIFF_MS_O_DRAWING:
			ms_escher_parse (q, &state.container, FALSE);
			break;

		case BIFF_EXTERNCOUNT: /* ignore */ break;
		case BIFF_EXTERNSHEET: /* These cannot be biff8 */
			excel_read_EXTERNSHEET_v7 (q, &state.container);
			break;

		case BIFF_WINDOW2_v0 :
		case BIFF_WINDOW2_v2 :
			if (full_page != NULL && BC_R(ver)(&state) > MS_BIFF_V2 &&
			    q->length >= 2 &&
			    GSF_LE_GET_GUINT16 (q->data + 0) & 0x0400)
				wb_view_sheet_focus (container->importer->wbv, full_page);
			break;

		case BIFF_SCL :
			if (full_page != NULL)
				excel_read_SCL (q, full_page);
			break;

		case BIFF_PLS:		/* Skip for Now */
		case BIFF_DIMENSIONS_v0 :/* Skip for Now */
		case BIFF_DIMENSIONS_v2 :/* Skip for Now */
		case BIFF_HEADER :	/* Skip for Now */
		case BIFF_FOOTER :	/* Skip for Now */
		case BIFF_HCENTER :	/* Skip for Now */
		case BIFF_VCENTER :	/* Skip for Now */
		case BIFF_CODENAME :
		case BIFF_SETUP :
			d (8, g_printerr ("Handled biff %x in chart;\n",
				     q->opcode););
			break;

		case BIFF_PRINTSIZE: {
#if 0
			/* Undocumented, seems like an enum ?? */
			gint16 const v = GSF_LE_GET_GUINT16 (q->data);
#endif
		}
		break;

		default :
			excel_unexpected_biff (q, "Chart", ms_excel_chart_debug);
		}
		state.prev_opcode = q->opcode;
	}

	/* If there was no grid in the stream remove the automatic grid */
	if (!state.error && state.chart != NULL && !state.has_a_grid) {
		GogGrid *grid = gog_chart_get_grid (state.chart);
		if (grid != NULL) {
			gog_object_clear_parent (GOG_OBJECT (grid));
			g_object_unref (grid);
		}
	}

	for (i = state.series->len; !state.error && i-- > 0 ; ) {
		int j;
		XLChartSeries *series = g_ptr_array_index (state.series, i);

		if (series != NULL) {
			Sheet *sheet = ms_container_sheet (state.container.parent);
			GOData	      *data;

			if (series->chart_group < 0 && BC_R(ver)(&state) >= MS_BIFF_V5) {
				/* might be a error bar series or a regression curve */
				if (series->err_type == 0)
					xl_chart_import_trend_line (&state, series);
				else
					xl_chart_import_error_bar (&state, series);
			}
			for (j = GOG_MS_DIM_VALUES ; j < GOG_MS_DIM_TYPES; j++ )
				if (NULL != series->data [j].value) {
					GnmExprTop const *texpr;

					if (!sheet || !series->series)
						continue;

					texpr = gnm_expr_top_new_constant
						((GnmValue *)(series->data [j].value));
					series->data [j].value = NULL;

					data = gnm_go_data_vector_new_expr (sheet, texpr);
					if (series->extra_dim == 0)
						gog_series_set_XL_dim (series->series, j, data, NULL);
					else if (j == GOG_MS_DIM_VALUES)
						gog_series_set_XL_dim (series->series, series->extra_dim, data, NULL);
				}
		}
	}
	/* Cleanup */
	for (i = state.series->len; i-- > 0 ; ) {
		XLChartSeries *series = g_ptr_array_index (state.series, i);
		excel_chart_series_delete (series);
	}
	g_ptr_array_free (state.series, TRUE);

	g_array_free (state.stack, TRUE);
	ms_container_finalize (&state.container);

	if (state.error) {
		state.graph = NULL;
		state.chart = NULL;
	}

	if (state.chart) {
		/* try to replace hidden axes by visible ones when possible */
		GSList *l, *cur;
		GogAxis *hidden, *visible;
		int i;
		for (i = GOG_AXIS_X; i <= GOG_AXIS_Y; i++) {
			hidden = visible = NULL;
			l = gog_chart_get_axes (state.chart, i);

			for (cur = l; cur; cur = cur->next) {
				gboolean invisible;
				g_object_get (cur->data, "invisible", &invisible, NULL);
				if (invisible)
					hidden = GOG_AXIS (cur->data);
				else
					visible = GOG_AXIS (cur->data);
			}
			g_slist_free (l);
			if (hidden && visible) {
				GSList *l1, *cur1;

				l1 = g_slist_copy ((GSList *) gog_axis_contributors (hidden));
				for (cur1 = l1; cur1; cur1 = cur1->next) {
					if (GOG_IS_PLOT (cur1->data))
						gog_plot_set_axis (GOG_PLOT (cur1->data), visible);
				}
				g_slist_free (l1);
				/* now reparent the children of the hidden axis */
				l1 = gog_object_get_children (GOG_OBJECT (hidden), NULL);
				for (cur1 = l1; cur1; cur1 = cur1->next) {
					GogObject *obj = GOG_OBJECT (cur1->data);
					GogObjectRole const *role = obj->role;
					gog_object_clear_parent (obj);
					gog_object_set_parent (obj, GOG_OBJECT (visible), role, obj->id);
				}
				g_slist_free (l1);

				gog_object_clear_parent (GOG_OBJECT (hidden));
				g_object_unref (hidden);
			}
		}
	}

	if (!state.error && full_page != NULL) {
		static GnmRange const fixed_size = { { 1, 1 }, { 12, 32 } };
		SheetObjectAnchor anchor;
		sheet_object_anchor_init (&anchor, &fixed_size, NULL,
			GOD_ANCHOR_DIR_DOWN_RIGHT, GNM_SO_ANCHOR_TWO_CELLS);
		sheet_object_set_anchor (sog, &anchor);
		sheet_object_set_sheet (sog, full_page);
		g_object_unref (sog);
	}

	if (state.style != NULL)
		g_object_unref (state.style); /* avoids a leak with corrupted files */
	if (state.default_plot_style != NULL)
		g_object_unref (state.default_plot_style);

	return state.error;
}

/* A wrapper which reads and checks the BOF record then calls ms_excel_chart_read */
/**
 * ms_excel_chart_read_BOF :
 * @q: #BiffQuery
 * @container: #MSContainer
 * @sog: #SheetObjectGraph
 **/
gboolean
ms_excel_chart_read_BOF (BiffQuery *q, MSContainer *container, SheetObject *sog)
{
	MsBiffBofData *bof;
	gboolean res = TRUE;

	/* 1st record must be a valid BOF record */
	g_return_val_if_fail (ms_biff_query_next (q), TRUE);
	bof = ms_biff_bof_data_new (q);

	g_return_val_if_fail (bof != NULL, TRUE);
	g_return_val_if_fail (bof->type == MS_BIFF_TYPE_Chart, TRUE);

	/* NOTE : _Ignore_ the verison in the BOF, it lies!
	 * XP saving as 95 will mark the book as biff7
	 * but sheets and charts are marked as biff8, even though they are not
	 * using unicode */
	res = ms_excel_chart_read (q, container, sog, NULL);

	ms_biff_bof_data_destroy (bof);
	return res;
}

/***************************************************************************/


typedef struct {
	GogAxis *axis [GOG_AXIS_TYPES];
	gboolean transpose, center_ticks;
	GSList  *plots;
} XLAxisSet;

typedef struct {
	BiffPut		*bp;
	ExcelWriteState *ewb;
	SheetObject	*so;
	GogGraph const	*graph;
	GogObject const	*chart;
	GogView		*root_view;

	unsigned	 nest_level;
	unsigned cur_series;
	unsigned cur_vis_index;
	unsigned cur_set;
	GSList *pending_series;
	GSList *extra_objects;
	GPtrArray *values[3];

	/* these are for combining dropbar, minmax and line plots on export */
	GogPlot *line_plot;
	XLAxisSet *line_set;
	gboolean has_dropbar;
	gboolean has_hilow;
	gboolean is_stock;
	GOStyle *dp_style, *hl_style;
	guint16 dp_width;
	GogAxis *primary_axis[GOG_AXIS_TYPES];
} XLChartWriteState;

typedef struct {
	unsigned series;
	GnmValue const *value;
} XLValue;

static gint
cb_axis_set_cmp (XLAxisSet const *a, XLAxisSet const *b)
{
	int i;
	if (a->transpose != b->transpose)
		return TRUE;
	for (i = GOG_AXIS_X; i < GOG_AXIS_TYPES; i++)
		if (a->axis[i] != b->axis[i])
			return TRUE;
	return FALSE;
}

static void
chart_write_BEGIN (XLChartWriteState *s)
{
	ms_biff_put_empty (s->bp, BIFF_CHART_begin);
	s->nest_level++;
}

static void
chart_write_END (XLChartWriteState *s)
{
	g_return_if_fail (s->nest_level > 0);
	s->nest_level--;
	ms_biff_put_empty (s->bp, BIFF_CHART_end);
}

/* returns the index of an element in the palette that is close to
 * the selected color */
static unsigned
chart_write_color (XLChartWriteState *s, guint8 *data, GOColor c)
{
	guint32 abgr;
	abgr  = GO_COLOR_UINT_R(c);
	abgr |= GO_COLOR_UINT_G(c) << 8;
	abgr |= GO_COLOR_UINT_B(c) << 16;
	GSF_LE_SET_GUINT32 (data, abgr);

	return palette_get_index (&s->ewb->base, abgr & 0xffffff);
}

static void
chart_write_AREAFORMAT (XLChartWriteState *s, GOStyle const *style, gboolean disable_auto)
{
	guint8 *data = ms_biff_put_len_next (s->bp, BIFF_CHART_areaformat,
		(s->bp->version >= MS_BIFF_V8) ? 16: 12);
	guint16 fore_index, back_index, pat, flags = 0;
	GOColor fore, back;

	if (style != NULL) {
		switch (style->fill.type) {
		default :
			g_warning ("invalid fill type, saving as none");
		case GO_STYLE_FILL_IMAGE:
#warning export images
		case GO_STYLE_FILL_NONE:
			pat = 0;
			fore = GO_COLOR_WHITE;
			back = GO_COLOR_WHITE;
			break;
		case GO_STYLE_FILL_PATTERN:
			if ((style->fill.pattern.pattern == GO_PATTERN_SOLID && style->fill.pattern.back == 0)
			    || (style->fill.pattern.pattern == GO_PATTERN_FOREGROUND_SOLID && style->fill.pattern.fore == 0)
			    || (style->fill.pattern.fore == 0 && style->fill.pattern.back == 0)) {
				pat = 0;
				fore = GO_COLOR_WHITE;
				back = GO_COLOR_WHITE;
			} else {
				pat = style->fill.pattern.pattern + 1;
				if (pat == 1) {
					back = style->fill.pattern.fore;
					fore = style->fill.pattern.back;
				} else {
					fore = style->fill.pattern.fore;
					back = style->fill.pattern.back;
				}
			}
			break;
		case GO_STYLE_FILL_GRADIENT:
			pat = 1;
			fore = back = style->fill.pattern.fore;
#warning export gradients
			break;
		}

		/* As we set only one of auto_fore and auto_back when reading,
		 * we don't need to have both TRUE to set the auto flag here.
		 * See bug #671845 */
		if (style->fill.auto_type && (style->fill.auto_fore || style->fill.auto_back) && !disable_auto)
			flags |= 1;
		if (style->fill.invert_if_negative)
			flags |= 2;
	} else {
		fore = back = 0;
		pat = 0;
		flags = disable_auto ? 0 : 1;
	}
	fore_index = chart_write_color (s, data+0, fore);
	back_index = chart_write_color (s, data+4, back);
	GSF_LE_SET_GUINT16 (data+8,  pat);
	GSF_LE_SET_GUINT16 (data+10, flags);
	if (s->bp->version >= MS_BIFF_V8) {
		GSF_LE_SET_GUINT16 (data+12, fore_index);
		GSF_LE_SET_GUINT16 (data+14, back_index);
	}

	ms_biff_put_commit (s->bp);
}

static void
chart_write_LINEFORMAT (XLChartWriteState *s, GOStyleLine const *lstyle,
			gboolean draw_ticks, gboolean clear_lines_for_null)
{
	guint8 *data = ms_biff_put_len_next (s->bp, BIFF_CHART_lineformat,
		(s->bp->version >= MS_BIFF_V8) ? 12: 10);
	guint16 w, color_index, pat, flags = 0;
	static guint8 const patterns[] = {5, 0, 2, 3, 4, 4, 2, 1, 1, 1, 3, 4};

	if (lstyle != NULL) {
		color_index = chart_write_color (s, data, lstyle->color);
		pat = patterns[lstyle->dash_type];
		if (lstyle->width < 0.) {
			w = 0xffff;
			pat = 5;	/* none */
		} else if (lstyle->width <= .5)
			w = 0xffff;	/* hairline */
		else if (lstyle->width <= 1.5)
			w = 0;	/* normal */
		else if (lstyle->width <= 2.5)
			w = 1;	/* medium */
		else
			w = 2;	/* wide */
		/* seems that excel understand auto as solid, so if pattern is not solid
		   do not set the auto flag, see #605043 */
		flags |= (lstyle->auto_color && pat == 0)? 1: 0;
	} else {
		color_index = chart_write_color (s, data, 0);
		if (clear_lines_for_null) {
			pat = 5;
			flags = 8;	/* docs only mention 1, but there is an 8 in there too */
		} else {
			pat = 0;
			flags = 9;	/* docs only mention 1, but there is an 8 in there too */
		}
		w = 0xffff;
	}
	if (draw_ticks)
		flags |= 4;

	GSF_LE_SET_GUINT16 (data+4, pat);
	GSF_LE_SET_GUINT16 (data+6, w);
	GSF_LE_SET_GUINT16 (data+8, flags);
	if (s->bp->version >= MS_BIFF_V8)
		GSF_LE_SET_GUINT16 (data+10, color_index);
	ms_biff_put_commit (s->bp);
}

static void
chart_write_SERFMT (XLChartWriteState *s, GOLineInterpolation interpolation)
{
    if (interpolation == GO_LINE_INTERPOLATION_SPLINE) {
	guint8 *data = ms_biff_put_len_next (s->bp, BIFF_CHART_serfmt, 2);
	GSF_LE_SET_GUINT8 (data+0, 1);
	ms_biff_put_commit (s->bp);
    }
}

static void
chart_write_MARKERFORMAT (XLChartWriteState *s, GOStyle const *style,
			  gboolean clear_marks_for_null)
{
	guint8 *data = ms_biff_put_len_next (s->bp, BIFF_CHART_markerformat,
		(s->bp->version >= MS_BIFF_V8) ? 20: 12);
	guint16 fore_index, back_index, shape, flags = 0;
	guint32 size;
	GOColor fore, back;
	static int const shape_map[] = {
		0,    /* GO_MARKER_NONE	         */
		1,    /* GO_MARKER_SQUARE	 */
		2,    /* GO_MARKER_DIAMOND	 */
		3,    /* GO_MARKER_TRIANGLE_DOWN */
		3,    /* GO_MARKER_TRIANGLE_UP	 */
		3,    /* GO_MARKER_TRIANGLE_RIGHT*/
		3,    /* GO_MARKER_TRIANGLE_LEFT */
		8,    /* GO_MARKER_CIRCLE	 */
		4,    /* GO_MARKER_X		 */
		9,    /* GO_MARKER_CROSS	 */
		5,    /* GO_MARKER_ASTERISK	 */
		7,    /* GO_MARKER_BAR		 */
		6,    /* GO_MARKER_HALF_BAR	 */
		1,    /* GO_MARKER_BUTTERFLY	 */
		1,    /* GO_MARKER_HOURGLASS	 */
		6     /* GO_MARKER_LEFT_HALF_BAR */
	};

	if (style != NULL) {
		fore = go_marker_get_outline_color (style->marker.mark);
		back = go_marker_get_fill_color	(style->marker.mark);
		shape = shape_map[go_marker_get_shape (style->marker.mark)];
		size = go_marker_get_size (style->marker.mark) * 20;
		if (style->marker.auto_outline_color &&
		    style->marker.auto_fill_color && style->marker.auto_shape &&
			((size == 100) || (s->bp->version < MS_BIFF_V8)))
			flags |= 1;
		if (fore == 0)
			flags |= 0x20;
		if (back == 0)
			flags |= 0x10;
	} else {
		fore = back = 0;
		if (clear_marks_for_null) {
			shape = flags = 0;
		} else {
			shape = 2;
			flags = 1;
		}
		size = 100;
	}

	fore_index = chart_write_color (s, data+0, fore);
	back_index = chart_write_color (s, data+4, back);

	GSF_LE_SET_GUINT16 (data+8,  shape);
	GSF_LE_SET_GUINT16 (data+10, flags);
	if (s->bp->version >= MS_BIFF_V8) {
		/* if s->cur_series is UINT_MAX, we are not saving a series format */
		GSF_LE_SET_GUINT16 (data+12,
			(style && style->marker.auto_outline_color && s->cur_series != UINT_MAX) ?
			(guint16) (32 + s->cur_series): fore_index);
		GSF_LE_SET_GUINT16 (data+14,
			(style && style->marker.auto_outline_color && s->cur_series != UINT_MAX) ?
			32 + s->cur_series: back_index);
		GSF_LE_SET_GUINT32 (data+16, size);
	}

	ms_biff_put_commit (s->bp);
}
static void
chart_write_PIEFORMAT (XLChartWriteState *s, double separation)
{
	gint tmp = separation * 100;
	if (tmp < 0)
		tmp = 0;
	else if (tmp > 500)
		tmp = 500;
	ms_biff_put_2byte (s->bp, BIFF_CHART_pieformat, tmp);
}

static guint32
map_length (XLChartWriteState *s, double l, gboolean is_horiz)
{
	double tmp = l / (is_horiz ? s->root_view->allocation.w : s->root_view->allocation.h);
#warning use _tmp_ here when we get the null view in place
	return (unsigned)(4000. * tmp + .5);
}

static void
chart_write_position (XLChartWriteState *s, GogObject const *obj, guint8 *data, int hpos, int vpos)
{
	GogView *view = gog_view_find_child_view  (s->root_view, obj);
	guint32 tmp;
	double l = 0.; /* just to make gcc happy */

	g_return_if_fail (view != NULL);

	switch (hpos) {
	case XL_POS_LOW:
		l = view->allocation.x;
		break;
	case XL_POS_HIGH:
		l = view->allocation.x + view->allocation.w;
		break;
	case XL_POS_CENTER:
	case XL_POS_JUSTIFY:
		l = view->allocation.x + view->allocation.w / 2;
		break;
	}
	tmp = map_length (s, l, TRUE);
	GSF_LE_SET_GUINT32 (data + 0, tmp);
	switch (vpos) {
	case XL_POS_LOW:
		l = view->allocation.y;
		break;
	case XL_POS_HIGH:
		l = view->allocation.y + view->allocation.h;
		break;
	case XL_POS_CENTER:
	case XL_POS_JUSTIFY:
		l = view->allocation.y + view->allocation.h / 2;
		break;
	}
	tmp = map_length (s, l, FALSE);
	GSF_LE_SET_GUINT32 (data + 4, tmp);
	tmp = map_length (s, view->allocation.w, TRUE);
	GSF_LE_SET_GUINT32 (data + 8, tmp);
	tmp = map_length (s, view->allocation.h, FALSE);
	GSF_LE_SET_GUINT32 (data + 12, tmp);
}

#if 0
static void
chart_write_FBI (XLChartWriteState *s, guint16 i, guint16 n)
{
	/* seems to vary from machine to machine */
	static guint8 const fbi[] = { 0xe4, 0x1b, 0xd0, 0x11, 0xc8, 0 };
	guint8 *data = ms_biff_put_len_next (s->bp, BIFF_CHART_fbi, 10);
	memcpy (data, fbi, sizeof fbi);
	GSF_LE_SET_GUINT16 (data + sizeof fbi + 6, i);
	GSF_LE_SET_GUINT16 (data + sizeof fbi + 8, n);
	ms_biff_put_commit (s->bp);
}
#endif

static void
chart_write_DATAFORMAT (XLChartWriteState *s, guint16 flag, guint16 indx, guint16 visible_indx)
{
	guint8 *data = ms_biff_put_len_next (s->bp, BIFF_CHART_dataformat, 8);
	GSF_LE_SET_GUINT16 (data+0, flag);
	GSF_LE_SET_GUINT16 (data+2, indx);
	GSF_LE_SET_GUINT16 (data+4, visible_indx);
	GSF_LE_SET_GUINT16 (data+6, 0); /* do not use XL 4 autocolor */
	ms_biff_put_commit (s->bp);
}

static void
chart_write_3d (XLChartWriteState *s, guint16 rotation, guint16 elevation,
		guint16 distance, guint16 height, guint16 depth, guint16 gap,
		guint8 flags, guint8 zero)
{
	guint8 *data = ms_biff_put_len_next (s->bp, BIFF_CHART_3d, 14);
	GSF_LE_SET_GUINT16 (data, rotation);
	GSF_LE_SET_GUINT16 (data+2, elevation);
	GSF_LE_SET_GUINT16 (data+4, distance);
	GSF_LE_SET_GUINT16 (data+6, height);
	GSF_LE_SET_GUINT16 (data+8, depth);
	GSF_LE_SET_GUINT16 (data+10, gap);
	GSF_LE_SET_GUINT8 (data+12, flags);
	GSF_LE_SET_GUINT8 (data+13, zero) ;
	ms_biff_put_commit (s->bp);
}

static void
chart_write_AI (XLChartWriteState *s, GOData const *dim, unsigned n,
		guint8 ref_type)
{
	guint8 buf[8], lendat[2];
	unsigned len;
	GnmExprTop const *texpr = NULL;
	GnmValue const *value = NULL;
	gboolean need_release = FALSE;

	if (dim != NULL) {
		if (GNM_IS_GO_DATA_SCALAR (dim) || GNM_IS_GO_DATA_VECTOR (dim)) {
			texpr = gnm_go_data_get_expr (dim);
			if ((value = gnm_expr_top_get_range (texpr)) != NULL) {
				GType const t = G_OBJECT_TYPE (dim);
				value_release ((GnmValue*) value);
				value = NULL;
				/* the following condition should always be true */
				if (t == GNM_GO_DATA_SCALAR_TYPE ||
				    t == GNM_GO_DATA_VECTOR_TYPE)
					ref_type = 2;
			} else if ((value = gnm_expr_top_get_constant (texpr)))
				ref_type = 1;
			else /* might be any expression */
				ref_type = 2;
		} else {
			char *str = go_data_serialize (dim, (gpointer)gnm_conventions_default);
			ref_type = 1;
			value = value_new_string (str);
			g_free (str);
			need_release = TRUE;
		}
	}
	ms_biff_put_var_next (s->bp, BIFF_CHART_ai);
	GSF_LE_SET_GUINT8  (buf+0, n);
	GSF_LE_SET_GUINT8  (buf+1, ref_type);

	/* no custom number format support for a dimension yet */
	GSF_LE_SET_GUINT16 (buf+2, 0);
	GSF_LE_SET_GUINT16 (buf+4, 0);

	GSF_LE_SET_GUINT16 (buf+6, 0); /* placeholder length */
	ms_biff_put_var_write (s->bp, buf, 8);

	if (ref_type == 2 && dim) {
		len = excel_write_formula (s->ewb, texpr,
			gnm_go_data_get_sheet (dim),
			0, 0, EXCEL_CALLED_FROM_NAME);
		ms_biff_put_var_seekto (s->bp, 6);
		GSF_LE_SET_GUINT16 (lendat, len);
		ms_biff_put_var_write (s->bp, lendat, 2);
	} else if (ref_type == 1 && value) {
		if (n) {
			XLValue *xlval = g_new0 (XLValue, 1);
			xlval->series = s->cur_series;
			xlval->value = value;
			g_ptr_array_add (s->values[n - 1], xlval);
		} else {
			guint dat[2];
			char *str = (NULL != value && VALUE_IS_STRING (value))
				? value_get_as_string (value)
				: go_data_serialize (dim, (gpointer)gnm_conventions_default);

			ms_biff_put_commit (s->bp);
			ms_biff_put_var_next (s->bp, BIFF_CHART_seriestext);
			GSF_LE_SET_GUINT16 (dat, 0);
			ms_biff_put_var_write  (s->bp, (guint8*) dat, 2);
			excel_write_string (s->bp, STR_ONE_BYTE_LENGTH, str);
			g_free (str);
		}
		if (need_release)
			value_release ((GnmValue *) value);
	}

	ms_biff_put_commit (s->bp);
}

static void
chart_write_text (XLChartWriteState *s, GOData const *src, GOStyledObject const *obj, int purpose)
{
	static guint8 const default_text[] = {
		2,		/* halign = center */
		2,		/* valign = center */
		1, 0,		/* transparent */
		0, 0, 0, 0,	/* black (as rgb) */

		/* position seems constant ?? */
		0xd6, 0xff, 0xff, 0xff,
		0xbe, 0xff, 0xff, 0xff,
		0, 0, 0, 0,
		0, 0, 0, 0,

		0xb1, 0,	/* flags 1 */
		/* biff8 specific */
		0, 0,		/* index of color */
		0x10, 0x3d,	/* flags 2 */
		0, 0		/* rotation */
	};
	guint8 *data;
	guint16 color_index = 0x4d;
	unsigned const len = (s->bp->version >= MS_BIFF_V8) ? 32: 26;
	GOStyle *style = (obj)? go_styled_object_get_style (GO_STYLED_OBJECT (obj)): NULL;

	/* TEXT */
	data = ms_biff_put_len_next (s->bp, BIFF_CHART_text, len);
	memcpy (data, default_text, len);
	if (obj)
		chart_write_position (s, GOG_OBJECT (obj), data+8, XL_POS_CENTER, XL_POS_CENTER);
	if (style != NULL) {
		color_index = chart_write_color (s, data+4, style->font.color);

	}
	if (s->bp->version >= MS_BIFF_V8) {
		GSF_LE_SET_GUINT16 (data+26, color_index);
	}
	ms_biff_put_commit (s->bp);

	chart_write_BEGIN (s);

	/* BIFF_CHART_pos, optional we use auto positioning */
	if (style && !style->font.auto_font)
		ms_biff_put_2byte (s->bp, BIFF_CHART_fontx,
				   excel_font_from_go_font (&s->ewb->base, style->font.font));

	chart_write_AI (s, src, 0, 1);
	if (obj && purpose) {
		data = ms_biff_put_len_next (s->bp, BIFF_CHART_objectlink, 6);
		GSF_LE_SET_GUINT16 (data, purpose);
		ms_biff_put_commit (s->bp);
	}

	chart_write_END (s);
}

static void
store_dim (GogSeries const *series, GogMSDimType t,
	   guint8 *store_type, guint8 *store_count, guint16 default_count)
{
	int msdim = gog_series_map_XL_dim (series, t);
	GOData *dat = NULL;
	guint16 count, type;

	if (msdim >= -1)
		dat = gog_dataset_get_dim (GOG_DATASET (series), msdim);
	if (dat == NULL) {
		count = default_count;
		type = 1; /* numeric */
	} else if (GO_IS_DATA_SCALAR (dat)) {
		/* cheesy test to see if the content is strings or numbers */
		double tmp = go_data_scalar_get_value (GO_DATA_SCALAR (dat));
		type = gnm_finite (tmp) ? 1 : 3;
		count = 1;
	} else if (GO_IS_DATA_VECTOR (dat)) {
		count = go_data_vector_get_len (GO_DATA_VECTOR (dat));
		if (count > 0) {
			/* cheesy test to see if the content is strings or numbers */
			double tmp = go_data_vector_get_value (GO_DATA_VECTOR (dat), 0);
			type = gnm_finite (tmp) ? 1 : 3;
		} else
			type = 1;
		if (count > 30000) /* XL limit */
			count = 30000;
	} else {
		g_warning ("How did this happen ?");
		count = 0;
		type = 1; /* numeric */
	}
	GSF_LE_SET_GUINT16 (store_type, type);
	GSF_LE_SET_GUINT16 (store_count, count);
}

static gboolean
style_is_completely_auto (GOStyle const *style)
{
	if ((style->interesting_fields & GO_STYLE_FILL)) {
		if (style->fill.type != GO_STYLE_FILL_PATTERN ||
		    !style->fill.auto_back)
			return FALSE;
	}
	if ((style->interesting_fields & (GO_STYLE_OUTLINE | GO_STYLE_LINE)) &&
	    (!style->line.auto_color || !style->line.auto_dash ||
		(style->line.width != 0.)))
		return FALSE;
	if ((style->interesting_fields & GO_STYLE_MARKER)) {
		if (!style->marker.auto_shape ||
		    !style->marker.auto_outline_color ||
		    !style->marker.auto_fill_color)
			return FALSE;
	}
	return TRUE;
}

static void
chart_write_style (XLChartWriteState *s, GOStyle const *style,
		   guint16 indx, unsigned n, unsigned v, double separation,
		   GOLineInterpolation interpolation)
{
	chart_write_DATAFORMAT (s, indx, n, v);
	chart_write_BEGIN (s);
	ms_biff_put_2byte (s->bp, BIFF_CHART_3dbarshape, 0); /* box */
	if (!style_is_completely_auto (style) || interpolation == GO_LINE_INTERPOLATION_SPLINE) {
		chart_write_LINEFORMAT (s, &style->line, FALSE, FALSE);
		if ((style->interesting_fields & GO_STYLE_LINE))
			chart_write_SERFMT (s, interpolation);
		chart_write_AREAFORMAT (s, style, FALSE);
		chart_write_PIEFORMAT (s, separation);
		chart_write_MARKERFORMAT (s, style, FALSE);
	}
	chart_write_END (s);
}

static gboolean
chart_write_error_bar (XLChartWriteState *s, GogErrorBar *bar, unsigned n,
	unsigned parent, unsigned type)
{
	guint8 *data, value;
	gboolean values_as_formula = FALSE;
	GODataVector *vec = GO_DATA_VECTOR ((type & 1)?
		bar->series->values[bar->error_i].data:
		bar->series->values[bar->error_i + 1].data);
	unsigned length,
		series_length = gog_series_num_elements (bar->series);
	int i, imax = (s->bp->version >= MS_BIFF_V8) ?
					GOG_MS_DIM_BUBBLES: GOG_MS_DIM_CATEGORIES;
	double error = 0.;

	if (bar->type == GOG_ERROR_BAR_TYPE_NONE)
		return FALSE;
	if (!GO_IS_DATA (vec))
		vec = GO_DATA_VECTOR (bar->series->values[bar->error_i].data);
	if (!GO_IS_DATA (vec)) /* if still no data, do not save */
		return FALSE;
	length = go_data_vector_get_len (vec);
	if (length == 1)
		/* whatever the data are, we must save as a constant */
		values_as_formula = FALSE;
	else if (bar->type == GOG_ERROR_BAR_TYPE_ABSOLUTE)
		values_as_formula = TRUE;

	s->cur_series = n;
	data = ms_biff_put_len_next (s->bp, BIFF_CHART_series,
		(s->bp->version >= MS_BIFF_V8) ? 12: 8);
	GSF_LE_SET_GUINT16 (data+0, 1);
	GSF_LE_SET_GUINT16 (data+4, series_length);
	GSF_LE_SET_GUINT16 (data+2, 1);
	GSF_LE_SET_GUINT16 (data+6, length);
	if (s->bp->version >= MS_BIFF_V8) {
		GSF_LE_SET_GUINT16 (data+8, 1);
		GSF_LE_SET_GUINT16 (data+10, 0);
	}
	ms_biff_put_commit (s->bp);

	chart_write_BEGIN (s);
	for (i = GOG_MS_DIM_LABELS; i <= imax; i++) {
		if (i == GOG_MS_DIM_VALUES && values_as_formula)
			chart_write_AI (s, GO_DATA (vec), i, 2);
		else {
			data = ms_biff_put_len_next (s->bp, BIFF_CHART_ai, 8);
			GSF_LE_SET_GUINT8  (data+0, i);
			GSF_LE_SET_GUINT8  (data+1, 1);

			GSF_LE_SET_GUINT16 (data+2, 0);
			GSF_LE_SET_GUINT16 (data+4, 0);

			GSF_LE_SET_GUINT16 (data+6, 0);
			ms_biff_put_commit (s->bp);
		}
	}
	chart_write_style (s, bar->style, 0xffff, n, 0, 0., GO_LINE_INTERPOLATION_LINEAR);

	data = ms_biff_put_len_next (s->bp, BIFF_CHART_serparent, 2);
	GSF_LE_SET_GUINT16  (data, parent + 1);
	ms_biff_put_commit (s->bp);

	data = ms_biff_put_len_next (s->bp, BIFF_CHART_serauxerrbar, 14);
	GSF_LE_SET_GUINT8 (data, type);
	switch (bar->type) {
	case GOG_ERROR_BAR_TYPE_ABSOLUTE:
		if (values_as_formula) {
			value = 4;
		} else {
			error = go_data_vector_get_value (vec, 0);
			value = 2;
		}
		break;
	case GOG_ERROR_BAR_TYPE_PERCENT:
		error = go_data_vector_get_value (vec, 0); /* we lose data :-( */
		value = 1;
		break;
	case GOG_ERROR_BAR_TYPE_RELATIVE:
		error = go_data_vector_get_value (vec, 0) * 100.; /* we lose data :-( */
		value = 1;
		break;
	default:
		g_warning ("unknown error bar type"); /* should not occur */
		error = 0.;
		value = 1;
	}
	GSF_LE_SET_GUINT8 (data+1, value);
	GSF_LE_SET_GUINT8 (data+2, ((bar->width > 0.)? TRUE: FALSE));
	GSF_LE_SET_GUINT8 (data+3, 1);
	GSF_LE_SET_DOUBLE (data+4, error);
	GSF_LE_SET_GUINT16 (data+12, length);

	ms_biff_put_commit (s->bp);

	chart_write_END (s);
	return TRUE;
}

/* the data below are invalid, many other invalid code might be used,
but this is the one xl uses */
static unsigned char invalid_data[8] = {0xff, 0xff, 0xff, 0xff, 0, 1, 0xff, 0xff};

static gboolean
chart_write_trend_line (XLChartWriteState *s, GogTrendLine *rc, unsigned n, unsigned parent)
{
	guint8 *data, type;
	unsigned order = 0, nb = 96;
	gboolean affine  = FALSE, skip_invalid, show_eq = FALSE, show_R2 = FALSE;
	int i, imax = (s->bp->version >= MS_BIFF_V8) ?
					GOG_MS_DIM_BUBBLES: GOG_MS_DIM_CATEGORIES;
	GogObject *eqn = NULL;
	double min, max;

	if (0 == strcmp (G_OBJECT_TYPE_NAME (rc), "GogLinRegCurve")) {
		type = 0;
		order = 1;
		nb = 2;
	} else if (0 == strcmp (G_OBJECT_TYPE_NAME (rc), "GogPolynomRegCurve")) {
		type = 0;
		g_object_get (G_OBJECT (rc), "dims", &order, NULL);
	} else if (0 == strcmp (G_OBJECT_TYPE_NAME (rc), "GogExpRegCurve"))
		type = 1;
	else if (0 == strcmp (G_OBJECT_TYPE_NAME (rc), "GogLogRegCurve"))
		type = 2;
	else if (0 == strcmp (G_OBJECT_TYPE_NAME (rc), "GogPowerRegCurve"))
		type = 3;
	else if (0 == strcmp (G_OBJECT_TYPE_NAME (rc), "GogMovingAvg")) {
		type = 4;
		g_object_get (G_OBJECT (rc), "span", &order, NULL);
	} else
		return FALSE;
	s->cur_series = n;
	data = ms_biff_put_len_next (s->bp, BIFF_CHART_series,
		(s->bp->version >= MS_BIFF_V8) ? 12: 8);
	GSF_LE_SET_GUINT16 (data+0, 1);
	GSF_LE_SET_GUINT16 (data+4, nb);
	GSF_LE_SET_GUINT16 (data+2, 1);
	GSF_LE_SET_GUINT16 (data+6, nb);
	if (s->bp->version >= MS_BIFF_V8) {
		GSF_LE_SET_GUINT16 (data+8, 1);
		GSF_LE_SET_GUINT16 (data+10, 0);
	}
	ms_biff_put_commit (s->bp);

	chart_write_BEGIN (s);
	for (i = GOG_MS_DIM_LABELS; i <= imax; i++) {
		data = ms_biff_put_len_next (s->bp, BIFF_CHART_ai, 8);
		GSF_LE_SET_GUINT8  (data+0, i);
		GSF_LE_SET_GUINT8  (data+1, 1);

		GSF_LE_SET_GUINT16 (data+2, 0);
		GSF_LE_SET_GUINT16 (data+4, 0);

		GSF_LE_SET_GUINT16 (data+6, 0);
		ms_biff_put_commit (s->bp);
	}
	chart_write_style (s, GOG_STYLED_OBJECT (rc)->style,
		0xffff, n, 0, 0., GO_LINE_INTERPOLATION_LINEAR);

	data = ms_biff_put_len_next (s->bp, BIFF_CHART_serparent, 2);
	GSF_LE_SET_GUINT16  (data, parent + 1);
	ms_biff_put_commit (s->bp);
	data = ms_biff_put_len_next (s->bp, BIFF_CHART_serauxtrend, 28);
	GSF_LE_SET_GUINT8  (data+0, type);
	GSF_LE_SET_GUINT8  (data+1, (guint8) order);
	if (GOG_IS_REG_CURVE (rc)) {
		g_object_get (G_OBJECT (rc), "affine", &affine,
				"skip-invalid", &skip_invalid, NULL);
		eqn = gog_object_get_child_by_name (GOG_OBJECT (rc), "Equation");
	}
	if (affine)
		memcpy (data+2, invalid_data, 8);
	else
		GSF_LE_SET_DOUBLE (data+2, 0.);
	if (eqn)
		g_object_get (G_OBJECT (eqn), "show-eq", &show_eq, "show-r2", &show_R2, NULL);
	GSF_LE_SET_GUINT8 (data+10, show_eq);
	GSF_LE_SET_GUINT8 (data+11, show_R2);
	GSF_LE_SET_DOUBLE (data+12, 0.);
	GSF_LE_SET_DOUBLE (data+20, 0.);
	ms_biff_put_commit (s->bp);

	/* now write our stuff */
	if (GOG_IS_REG_CURVE (rc)) {
		/*
			data+0 == min, #NA if not set
			data+8 == max, #NA if not set
			data+16 == flags (1 if skip invalid)
		*/
		data = ms_biff_put_len_next (s->bp, BIFF_CHART_trendlimits, 17);
		gog_reg_curve_get_bounds (GOG_REG_CURVE (rc), &min, &max);
		if (min > - DBL_MAX)
			GSF_LE_SET_DOUBLE (data+0, min);
		else
			memcpy (data+0, invalid_data, 8);
		if (max < DBL_MAX)
			GSF_LE_SET_DOUBLE (data+8, max);
		else
			memcpy (data+8, invalid_data, 8);
		GSF_LE_SET_GUINT8 (data+16, skip_invalid);
		ms_biff_put_commit (s->bp);
		{
			GOData *dat0 = gog_dataset_get_dim (GOG_DATASET (rc), 0),
				*dat1 = gog_dataset_get_dim (GOG_DATASET (rc), 1);
			gboolean range0, range1;
			GnmValue *val0 = NULL, *val1 = NULL; /* initialized to make gcc happy */
			if (dat0) {
				GnmExprTop const *texpr = gnm_go_data_get_expr (dat0);
				range0 = ((val0 = gnm_expr_top_get_range (texpr)) != NULL);
			} else
				range0 = FALSE;
			if (dat1) {
				GnmExprTop const *texpr = gnm_go_data_get_expr (dat1);
				range1 = ((val1 = gnm_expr_top_get_range (texpr)) != NULL);
			} else
				range1 = FALSE;
			if (range0 || range1) {
				chart_write_BEGIN (s);
				if (range0) {
					value_release (val0);
					chart_write_AI (s, dat0, 0, 2);
				}
				if  (range1) {
					value_release (val1);
					chart_write_AI (s, dat1, 1, 2);
				}
				chart_write_END (s);
			}
		}
	}

	chart_write_END (s);
	return TRUE;
}

static int
chart_write_series (XLChartWriteState *s, GogSeries const *series, unsigned n)
{
	static guint8 const default_ref_type[] = { 1, 2, 0, 1 };
	int i, msdim, saved = 1;
	guint8 *data;
	GOData *dat;
	unsigned num_elements = gog_series_num_elements (series);
	GList const *ptr;
	char *interpolation;

	/* SERIES */
	s->cur_series = n;
	data = ms_biff_put_len_next (s->bp, BIFF_CHART_series,
		(s->bp->version >= MS_BIFF_V8) ? 12: 8);
	store_dim (series, GOG_MS_DIM_CATEGORIES, data+0, data+4, num_elements);
	store_dim (series, GOG_MS_DIM_VALUES, data+2, data+6, num_elements);
	if (s->bp->version >= MS_BIFF_V8) {
		msdim = gog_series_map_XL_dim (series, GOG_MS_DIM_BUBBLES);
		store_dim (series, GOG_MS_DIM_BUBBLES, data+8, data+10,
			   (msdim >= 0) ? num_elements : 0);
	}
	ms_biff_put_commit (s->bp);

	chart_write_BEGIN (s);
	for (i = GOG_MS_DIM_LABELS; i <= GOG_MS_DIM_BUBBLES; i++) {
		msdim = gog_series_map_XL_dim (series, i);
		if (msdim >= -1)
			dat = gog_dataset_get_dim (GOG_DATASET (series),
				gog_series_map_XL_dim (series, i));
		else
			dat = NULL;
		chart_write_AI (s, dat, i, default_ref_type[i]);
	}

	g_object_get (G_OBJECT (series), "interpolation", &interpolation, NULL);
	chart_write_style (s, GOG_STYLED_OBJECT (series)->style,
		0xffff, s->cur_series, s->cur_vis_index, 0.,
		go_line_interpolation_from_str (interpolation));
	g_free (interpolation);
	for (ptr = gog_series_get_overrides (series); ptr != NULL ; ptr = ptr->next) {
		double sep = 0;
		if (g_object_class_find_property (
			G_OBJECT_GET_CLASS (ptr->data), "separation"))
			g_object_get (G_OBJECT (ptr->data), "separation", &sep, NULL);

		chart_write_style (s, GOG_STYLED_OBJECT (ptr->data)->style,
			GOG_SERIES_ELEMENT (ptr->data)->index, s->cur_series,
			s->cur_vis_index, sep, GO_LINE_INTERPOLATION_LINEAR);
	}
	s->cur_vis_index++;

	ms_biff_put_2byte (s->bp, BIFF_CHART_sertocrt, s->cur_set);
	chart_write_END (s);
	/* now write error bars and regression curves */
	/* Regression curves */
	{
		GSList *cur, *l = gog_object_get_children (GOG_OBJECT (series),
			gog_object_find_role_by_name (GOG_OBJECT (series), "Trend line"));
		cur = l;
		while (cur) {
			if (chart_write_trend_line (s, GOG_TREND_LINE (cur->data),
				n + saved, n))
				saved++;
			cur = cur->next;
		}
		g_slist_free (l);
	}
	/* error bars */
	{
		GogErrorBar *error_bar = NULL;
		GParamSpec *pspec = g_object_class_find_property (
			G_OBJECT_GET_CLASS (series), "errors");
		if (pspec) {
			g_object_get (G_OBJECT (series), "errors", &error_bar, NULL);
			if (error_bar) {
				if ((error_bar->display & GOG_ERROR_BAR_DISPLAY_POSITIVE) &&
					chart_write_error_bar (s, error_bar,
					n + saved, n, 3))
					saved++;
				if ((error_bar->display & GOG_ERROR_BAR_DISPLAY_NEGATIVE) &&
					chart_write_error_bar (s, error_bar,
					n + saved, n, 4))
					saved++;
				g_object_unref (error_bar);
			}
		} else {
			pspec = g_object_class_find_property (
				G_OBJECT_GET_CLASS (series), "x-errors");
			if (pspec) {
				g_object_get (G_OBJECT (series), "x-errors", &error_bar, NULL);
				if (error_bar) {
					if ((error_bar->display & GOG_ERROR_BAR_DISPLAY_POSITIVE) &&
						chart_write_error_bar (s, error_bar,
						n + saved, n, 1))
						saved++;
					if ((error_bar->display & GOG_ERROR_BAR_DISPLAY_NEGATIVE) &&
						chart_write_error_bar (s, error_bar,
						n + saved, n, 2))
						saved++;
					g_object_unref (error_bar);
				}
				/* if we have an x-errors prop, we also have y-errors */
				g_object_get (G_OBJECT (series), "y-errors", &error_bar, NULL);
				if (error_bar) {
					if ((error_bar->display & GOG_ERROR_BAR_DISPLAY_POSITIVE) &&
						chart_write_error_bar (s, error_bar,
						n + saved, n, 3))
						saved++;
					if ((error_bar->display & GOG_ERROR_BAR_DISPLAY_NEGATIVE) &&
						chart_write_error_bar (s, error_bar,
						n + saved, n, 4))
						saved++;
					g_object_unref (error_bar);
				}
			}
		}
	}
	return saved;
}

static void
chart_write_dummy_style (XLChartWriteState *s, double default_separation,
			 gboolean clear_marks, gboolean clear_lines,
			 GOLineInterpolation interpolation)
{
	chart_write_DATAFORMAT (s, 0, 0, 0xfffd);
	chart_write_BEGIN (s);
	ms_biff_put_2byte (s->bp, BIFF_CHART_3dbarshape, 0); /* box */
	chart_write_LINEFORMAT (s, NULL, FALSE, clear_lines);
	chart_write_SERFMT (s, interpolation);
	chart_write_AREAFORMAT (s, NULL, FALSE);
	chart_write_MARKERFORMAT (s, NULL, clear_marks);
	chart_write_PIEFORMAT (s, default_separation);
	chart_write_END (s);
}

static void
chart_write_frame (XLChartWriteState *s, GogObject const *frame,
		   gboolean calc_size, gboolean disable_auto)
{
	GOStyle *style = go_styled_object_get_style (GO_STYLED_OBJECT (frame));
	guint8 *data = ms_biff_put_len_next (s->bp, BIFF_CHART_frame, 4);
	GSF_LE_SET_GUINT16 (data + 0, 0); /* 0 == std/no border, 4 == shadow */
	GSF_LE_SET_GUINT16 (data + 2, (0x2 | (calc_size ? 1 : 0)));
	ms_biff_put_commit (s->bp);

	chart_write_BEGIN (s);
	chart_write_LINEFORMAT (s, &style->line, FALSE, FALSE);
	chart_write_AREAFORMAT (s, style, disable_auto);
	chart_write_END (s);
}

static guint16
xl_axis_set_elem (GogAxis const *axis,
		  unsigned dim, guint16 flag, guint8 *data, gboolean log_scale)
{
	gboolean user_defined = FALSE;
	double val = gog_axis_get_entry (axis, dim, &user_defined);
	if (log_scale)
		val = log10 (val);
	gsf_le_set_double (data, user_defined ? val : 0.);
	return user_defined ? 0 : flag;
}

static void
chart_write_axis (XLChartWriteState *s, GogAxis const *axis,
		unsigned i, gboolean centered, gboolean force_catserrange,
		gboolean cross_at_max, gboolean force_inverted, double cross_at)
{
	gboolean labeled, in, out, inverted = FALSE;
	guint16 tick_color_index, flags = 0;
	guint8 tmp, *data;

	data = ms_biff_put_len_next (s->bp, BIFF_CHART_axis, 18);
	GSF_LE_SET_GUINT32 (data + 0, i);
	memset (data+2, 0, 16);
	ms_biff_put_commit (s->bp);

	chart_write_BEGIN (s);
	if ((axis && gog_axis_is_discrete (axis)) || force_catserrange) {
		data = ms_biff_put_len_next (s->bp, BIFF_CHART_catserrange, 8);

		GSF_LE_SET_GUINT16 (data+0, 1); /* values_axis_crosses_at_cat_index */
		GSF_LE_SET_GUINT16 (data+2, 1); /* frequency_of_label */
		GSF_LE_SET_GUINT16 (data+4, 1); /* frequency_of_tick */
		if (axis)
			g_object_get (G_OBJECT (axis), "invert-axis", &inverted, NULL);
		else
			inverted = force_inverted;
		flags = centered ? 1 : 0; /* bit 0 == cross in middle of cat or between cats
					     bit 1 == enum cross point from max not min */
		if (cross_at_max)
			flags |= 2;
		if (inverted)
			flags |= 0x4; /* cats in reverse order */
		GSF_LE_SET_GUINT16 (data+6, flags);
		ms_biff_put_commit (s->bp);

		data = ms_biff_put_len_next (s->bp, BIFF_CHART_axcext, 18);
		GSF_LE_SET_GUINT16 (data+ 0, 0); /* min cat ignored if auto */
		GSF_LE_SET_GUINT16 (data+ 2, 0); /* max cat ignored if auto */
		GSF_LE_SET_GUINT16 (data+ 4, 1); /* value of major unit */
		GSF_LE_SET_GUINT16 (data+ 6, 0); /* units of major unit */
		GSF_LE_SET_GUINT16 (data+ 8, 1); /* value of minor unit */
		GSF_LE_SET_GUINT16 (data+10, 0); /* units of minor unit */
		GSF_LE_SET_GUINT16 (data+12, 0); /* base unit */
		GSF_LE_SET_GUINT16 (data+14, 0); /* crossing point */
		GSF_LE_SET_GUINT16 (data+16, 0xef); /*  1 == default min
						     *  2 == default max
						     *  4 == default major unit
						     *  8 == default minor unit
						     * 10 == this is a date axis
						     * 20 == default base
						     * 40 == default cross
						     * 80 == default date settings */
		ms_biff_put_commit (s->bp);
	} else {
		char *scale = NULL;
		gboolean log_scale = FALSE;

		if (axis != NULL)
			g_object_get (G_OBJECT (axis),
					  "map-name",		&scale,
					  "invert-axis",		&inverted,
					  NULL);
		else
			inverted = force_inverted;
		if (scale != NULL) {
			log_scale = !strcmp (scale, "Log");
			g_free (scale);
		}

		data = ms_biff_put_len_next (s->bp, BIFF_CHART_valuerange, 42);
		memset (data, 0, 42);

		if (log_scale)
			flags |= 0x20;
		if (inverted)
			flags |= 0x40;
		if (cross_at_max)
			flags |= 0x80; /* partner crosses at max */

		flags |= 0x100; /* UNDOCUMENTED */

		if (axis != NULL) {
			flags |= xl_axis_set_elem (axis, GOG_AXIS_ELEM_MIN,		0x01, data+ 0, log_scale);
			flags |= xl_axis_set_elem (axis, GOG_AXIS_ELEM_MAX,		0x02, data+ 8, log_scale);
			flags |= xl_axis_set_elem (axis, GOG_AXIS_ELEM_MAJOR_TICK,	0x04, data+16, log_scale);
			flags |= xl_axis_set_elem (axis, GOG_AXIS_ELEM_MINOR_TICK,	0x08, data+24, log_scale);
			if (isnan (cross_at) || (log_scale && cross_at == 1.) || (!log_scale && cross_at == 0.))
				/* assume this is the auto case for excel */
				flags |= 0x10;
			else
				gsf_le_set_double (data+32, log_scale ? log10 (cross_at): cross_at);
		} else
			flags |= 0x1f;
		GSF_LE_SET_GUINT16 (data+40, flags);
		ms_biff_put_commit (s->bp);
	}
	if (axis != NULL) {
		GOStyle *style = GOG_STYLED_OBJECT (axis)->style;
		int font;
		GOFormat *fmt = gog_axis_get_format (axis);
		if (fmt) {
			int ifmt = excel_write_add_object_format (s->ewb, fmt);
			data = ms_biff_put_len_next (s->bp, BIFF_CHART_ifmt, 2);
			GSF_LE_SET_GUINT16 (data, ifmt);
			ms_biff_put_commit (s->bp);
		}
		data = ms_biff_put_len_next (s->bp, BIFF_CHART_tick,
			(s->bp->version >= MS_BIFF_V8) ? 30 : 26);
		g_object_get (G_OBJECT (axis),
			"major-tick-labeled",		&labeled,
			"major-tick-in",		&in,
			"major-tick-out",		&out,
			/* "major-tick-size-pts",	(unsupported in XL) */
			/* "minor-tick-size-pts",	(unsupported in XL) */
			NULL);
		tmp = out ? 2 : 0;
		if (in)
			tmp |= 1;
		GSF_LE_SET_GUINT8  (data+0, tmp);

		g_object_get (G_OBJECT (axis),
			"minor-tick-in",	&in,
			"minor-tick-out",	&out,
			NULL);
		tmp = out ? 2 : 0;
		if (in)
			tmp |= 1;
		GSF_LE_SET_GUINT8  (data+1, tmp);

		tmp = labeled ? 3 : 0; /* label : 0 == none
					*	  1 == low	(unsupported in gnumeric)
					*	  2 == high	(unsupported in gnumeric)
					*	  3 == beside axis */
		GSF_LE_SET_GUINT8  (data+2, tmp);
		GSF_LE_SET_GUINT8  (data+3, 1); /* background mode : 1 == transparent
						 *		     2 == opaque */
		tick_color_index = chart_write_color (s, data+4, style->font.color); /* tick label color */
		memset (data+8, 0, 16);
		/* if font is black, set the auto color flag, otherwise, don't set */
		flags = (style->font.color == GO_COLOR_BLACK)? 0x03: 0x02;
		if (style->text_layout.auto_angle)
			flags |= 0x20;
		else if (s->bp->version < MS_BIFF_V8) {
			if (style->text_layout.angle < -45)
				flags |= 0x0C;
			else if (style->text_layout.angle > 45)
				flags |= 0x08;
		}
		GSF_LE_SET_GUINT16 (data+24, flags);
		if (s->bp->version >= MS_BIFF_V8) {
			GSF_LE_SET_GUINT16 (data+26, tick_color_index);
			if (style->text_layout.auto_angle)
				GSF_LE_SET_GUINT16 (data+28, 0);
			else if (style->text_layout.angle >= 0)
				GSF_LE_SET_GUINT16 (data+28, (int) style->text_layout.angle);
			else
				GSF_LE_SET_GUINT16 (data+28, 90 - (int) style->text_layout.angle);
		}
		ms_biff_put_commit (s->bp);
		font = excel_font_from_go_font (&s->ewb->base, style->font.font);
		if (font > 0 && !style->font.auto_font)
		    ms_biff_put_2byte (s->bp, BIFF_CHART_fontx, font);
	}

	ms_biff_put_2byte (s->bp, BIFF_CHART_axislineformat, 0); /* a real axis */
	if (axis != NULL) {
		GogObject *Grid;
		gboolean invisible;
		g_object_get (G_OBJECT (axis), "invisible", &invisible, NULL);
		chart_write_LINEFORMAT (s, (invisible? NULL: &GOG_STYLED_OBJECT (axis)->style->line),
					!invisible, invisible);
		Grid = gog_object_get_child_by_name (GOG_OBJECT (axis), "MajorGrid");
		if (Grid) {
			ms_biff_put_2byte (s->bp, BIFF_CHART_axislineformat, 1);
			chart_write_LINEFORMAT (s, &GOG_STYLED_OBJECT (Grid)->style->line,
						FALSE, FALSE);
		}
		Grid = gog_object_get_child_by_name (GOG_OBJECT (axis), "MinorGrid");
		if (Grid) {
			ms_biff_put_2byte (s->bp, BIFF_CHART_axislineformat, 2);
			chart_write_LINEFORMAT (s, &GOG_STYLED_OBJECT (Grid)->style->line,
						FALSE, FALSE);
		}
	} else {
		GOStyleLine line_style;
		line_style.width = 0.;
		line_style.dash_type = GO_LINE_NONE;
		line_style.auto_dash = FALSE;
		line_style.color = 0;
		line_style.auto_color = FALSE;
		chart_write_LINEFORMAT (s, NULL,
					FALSE, TRUE);
	}
	chart_write_END (s);
}

static guint16
map_1_5d_type (XLChartWriteState *s, GogPlot const *plot,
	       guint16 stacked, guint16 percentage, guint16 flag_3d)
{
	char *type;
	gboolean in_3d = FALSE;
	guint16 res;

	g_object_get (G_OBJECT (plot), "type", &type, "in-3d", &in_3d, NULL);

	res = (s->bp->version >= MS_BIFF_V8 && in_3d) ? flag_3d : 0;

	if (0 == strcmp (type, "stacked"))
		res |= stacked;
	else if (0 == strcmp (type, "as_percentage"))
		res |= (percentage | stacked);

	g_free (type);

	return res;
}

static void
chart_write_plot (XLChartWriteState *s, GogPlot const *plot)
{
	guint16 flags = 0;
	guint8 *data;
	char const *type = G_OBJECT_TYPE_NAME (plot);
	gboolean check_lines = FALSE;
	gboolean check_marks = FALSE;
	GOLineInterpolation interpolation;
	char *interp;

	if (0 == strcmp (type, "GogAreaPlot")) {
		ms_biff_put_2byte (s->bp, BIFF_CHART_area,
			map_1_5d_type (s, plot, 1, 2, 4));
	} else if (0 == strcmp (type, "GogBarColPlot")) {
		gboolean horizontal;
		int overlap_percentage, gap_percentage;

		g_object_get (G_OBJECT (plot),
			      "horizontal",		&horizontal,
			      "overlap-percentage",	&overlap_percentage,
			      "gap-percentage",		&gap_percentage,
			      NULL);
		if (horizontal)
			flags |= 1;
		flags |= map_1_5d_type (s, plot, 2, 4, 8);

		data = ms_biff_put_len_next (s->bp, BIFF_CHART_bar, 6);
		GSF_LE_SET_GINT16 (data, -overlap_percentage); /* dipsticks */
		GSF_LE_SET_GINT16 (data+2, gap_percentage);
		GSF_LE_SET_GUINT16 (data+4, flags);
		ms_biff_put_commit (s->bp);
	} else if (0 == strcmp (type, "GogLinePlot")) {
		ms_biff_put_2byte (s->bp, BIFF_CHART_line,
			map_1_5d_type (s, plot, 1, 2, 4));
		check_marks = TRUE;
	} else if (0 == strcmp (type, "GogPiePlot") ||
		   0 == strcmp (type, "GogRingPlot")) {
		gboolean in_3d = FALSE;
		double initial_angle = 0, center_size = 0, default_separation = 0;
		gint16 center = 0;
		g_object_get (G_OBJECT (plot),
			"in-3d",		&in_3d,
			"initial-angle",	&initial_angle,
			"default-separation",	&default_separation,
			NULL);

		data = ms_biff_put_len_next (s->bp, BIFF_CHART_pie,
			(s->bp->version >= MS_BIFF_V8) ? 6 : 4);
		GSF_LE_SET_GUINT16 (data + 0, (int)initial_angle);

		if (0 == strcmp (type, "GogRingPlot")) {
			g_object_get (G_OBJECT (plot),
				"center-size",		&center_size,
				NULL);
			center = (int)floor (center_size * 100. + .5);
			if (center < 0)
				center = 0;
			else if (center > 100)
				center = 100;
		} else
			center = 0;
		GSF_LE_SET_GUINT16 (data + 2, center);
		if (s->bp->version >= MS_BIFF_V8 && in_3d)
			flags = 1;
		GSF_LE_SET_GUINT16 (data + 4, flags);
		ms_biff_put_commit (s->bp);
		if (fabs (default_separation) > .005)
			chart_write_dummy_style (s, default_separation, FALSE,
						 FALSE, GO_LINE_INTERPOLATION_LINEAR);
	} else if (0 == strcmp (type, "GogRadarPlot")) {
		ms_biff_put_2byte (s->bp, BIFF_CHART_radar, flags);
		check_marks = TRUE;
	} else if (0 == strcmp (type, "GogRadarAreaPlot")) {
		ms_biff_put_2byte (s->bp, BIFF_CHART_radararea, flags);
	} else if (0 == strcmp (type, "GogBubblePlot") ||
		   0 == strcmp (type, "GogXYPlot")) {
		if (s->bp->version >= MS_BIFF_V8) {
			data = ms_biff_put_len_next (s->bp, BIFF_CHART_scatter, 6);
			if (0 == strcmp (type, "GogXYPlot")) {
				GSF_LE_SET_GUINT16 (data + 0, 100);
				GSF_LE_SET_GUINT16 (data + 2, 1);
				GSF_LE_SET_GUINT16 (data + 4, 0);
				check_marks = check_lines = TRUE;
			} else {
				gboolean show_neg = FALSE, in_3d = FALSE, as_area = TRUE;
				double scale;
				g_object_get (G_OBJECT (plot),
					"show-negatives",	&show_neg,
					"in-3d",		&in_3d,
					"size-as-area",		&as_area,
				        "bubble-scale",		&scale,
					NULL);
				scale *= 100.;
				/* TODO : find accurate size */
				GSF_LE_SET_GUINT16 (data + 0, (guint16) scale);
				GSF_LE_SET_GUINT16 (data + 2, as_area ? 1 : 2);

				flags = 1;
				if (show_neg)
					flags |= 2;
				if (in_3d)
					flags |= 4;
				GSF_LE_SET_GUINT16 (data + 4, flags);
			}
			ms_biff_put_commit (s->bp);
		} else
			ms_biff_put_empty (s->bp, BIFF_CHART_scatter);
	} else if (0 == strcmp (type, "GogContourPlot") ||
			0 == strcmp (type, "XLContourPlot")) {
		ms_biff_put_2byte (s->bp, BIFF_CHART_surf, 1); /* we always use color fill at the moment */
		chart_write_3d (s, 0, 90, 0, 100, 100, 150, 0x05, 0); /* these are default xl values */
	} else if (0 == strcmp (type, "GogSurfacePlot") ||
			0 == strcmp (type, "XLSurfacePlot")) {
		guint16 rotation = 0, elevation = 90, distance = 0, height = 100, depth = 100, gap = 150;
		guint8 flags = 0x05, zero = 0;
		int psi, theta, phi, fov;
		GogObject *box = gog_object_get_child_by_name (s->chart, "3D-Box");

		g_object_get (G_OBJECT (box), "psi", &psi, "theta", &theta, "phi", &phi, "fov", &fov, NULL);
		elevation = (guint16) theta; /* FIXME: theta might be as large as 180 */
/* TODO: evaluate the other parameters */
		ms_biff_put_2byte (s->bp, BIFF_CHART_surf, 1); /* we always use color fill at the moment */
		chart_write_3d (s, rotation, elevation, distance, height, depth, gap, flags, zero);
	} else {
		g_warning ("unexpected plot type %s", type);
	}

	/* be careful ! the XL default is to have lines and markers */
	if (check_marks) {
		g_object_get (G_OBJECT (plot),
			"default-style-has-markers",	&check_marks,
			NULL);
		check_marks = !check_marks;
	}
	if (check_lines) {
		g_object_get (G_OBJECT (plot),
			"default-style-has-lines",	&check_lines,
			NULL);
		check_lines = !check_lines;
	}

	g_object_get (G_OBJECT (plot), "interpolation", &interp, NULL);
	interpolation = go_line_interpolation_from_str (interp);
	g_free (interp);
	if (check_marks || check_lines || interpolation)
		chart_write_dummy_style (s, 0., check_marks, check_lines, interpolation);
}

static void
chart_write_CHARTLINE (XLChartWriteState *s, guint16 type, GOStyle *style)
{
	guint8 *data = ms_biff_put_len_next (s->bp, BIFF_CHART_chartline, 2);
	GSF_LE_SET_GUINT16 (data + 0, type);
	ms_biff_put_commit (s->bp);
	chart_write_LINEFORMAT (s, &style->line, FALSE, FALSE);
}

static void
chart_write_DROPBAR (XLChartWriteState *s)
{
	guint8 *data = ms_biff_put_len_next (s->bp, BIFF_CHART_dropbar, 2);
	GSF_LE_SET_GUINT16 (data + 0, s->dp_width);
	ms_biff_put_commit (s->bp);
	chart_write_BEGIN (s);
	chart_write_LINEFORMAT (s, &s->dp_style->line, FALSE, FALSE);
	chart_write_AREAFORMAT (s, s->dp_style, FALSE);
	chart_write_END (s);
	data = ms_biff_put_len_next (s->bp, BIFF_CHART_dropbar, 2);
	GSF_LE_SET_GUINT16 (data + 0, s->dp_width);
	ms_biff_put_commit (s->bp);
	s->dp_style->line.color = 0xffffff00 ^ s->dp_style->line.color;
	s->dp_style->fill.pattern.fore = 0xffffff00 ^ s->dp_style->fill.pattern.fore ;
	s->dp_style->fill.pattern.back = 0xffffff00 ^ s->dp_style->fill.pattern.back ;
	chart_write_BEGIN (s);
	chart_write_LINEFORMAT (s, &s->dp_style->line, FALSE, FALSE);
	chart_write_AREAFORMAT (s, s->dp_style, FALSE);
	chart_write_END (s);
	g_object_unref (s->dp_style);
}

GNM_BEGIN_KILL_SWITCH_WARNING
static void
chart_write_LEGEND (XLChartWriteState *s, GogObject const *legend)
{
	GogObjectPosition pos = gog_object_get_position_flags (legend,
		GOG_POSITION_COMPASS | GOG_POSITION_ALIGNMENT);
	guint16 flags = 0x1f;
	guint8  XL_pos;
	guint8 *data;

	switch (pos) {
	case GOG_POSITION_S | GOG_POSITION_ALIGN_CENTER:	XL_pos = 0; break;
	case GOG_POSITION_N | GOG_POSITION_E:			XL_pos = 1; break;
	case GOG_POSITION_N | GOG_POSITION_ALIGN_CENTER:	XL_pos = 2; break;

	default :
	case GOG_POSITION_E | GOG_POSITION_ALIGN_CENTER:	XL_pos = 3; break;
	case GOG_POSITION_W | GOG_POSITION_ALIGN_CENTER:	XL_pos = 4; break;
	/* On import we map 'floating' to East, XL_pos = 7; break; */
	}

	data = ms_biff_put_len_next (s->bp, BIFF_CHART_legend, 20);
	chart_write_position (s, legend, data, XL_POS_LOW, XL_POS_LOW);
	GSF_LE_SET_GUINT8 (data + 16, XL_pos);
	GSF_LE_SET_GUINT8 (data + 17, 1);
	GSF_LE_SET_GUINT16 (data + 18, flags);

	ms_biff_put_commit (s->bp);

	chart_write_BEGIN (s);
	/* BIFF_CHART_pos, optional we use auto positioning */
	chart_write_text (s, NULL, GO_STYLED_OBJECT (legend), 0);
	chart_write_END (s);
}
GNM_END_KILL_SWITCH_WARNING

static void
chart_write_axis_sets (XLChartWriteState *s, GSList *sets)
{
	guint16 i = 0, j = 0, nser;
	guint8 *data;
	gboolean x_inverted = FALSE, y_inverted = FALSE;
	GSList *sptr, *pptr;
	XLAxisSet *axis_set;
	GogObject const *legend = gog_object_get_child_by_name (s->chart, "Legend");
	GogObject const *label;
	unsigned num = g_slist_length (sets);

	if (num == 0)
		return;
	if (num > 2)
		num = 2; /* excel does not support more that 2. */

	ms_biff_put_2byte (s->bp, BIFF_CHART_axesused, MIN (g_slist_length (sets), 2));
	for (sptr = sets; sptr != NULL ; sptr = sptr->next) {
		data = ms_biff_put_len_next (s->bp, BIFF_CHART_axisparent, 4*4 + 2);
		/* pick arbitrary position, this sort of info is in the view  */
		GSF_LE_SET_GUINT16 (data + 0, i);
		GSF_LE_SET_GUINT32 (data + 2, 400);	/* 10% of 4000th of chart area */
		GSF_LE_SET_GUINT32 (data + 6, 400);
		GSF_LE_SET_GUINT32 (data + 10, 3000);	/* 75% of 4000th of chart area */
		GSF_LE_SET_GUINT32 (data + 14, 3000);
		/* chart_write_position (s, legend, data); */
		ms_biff_put_commit (s->bp);

		chart_write_BEGIN (s);
		axis_set = sptr->data;
		switch (gog_chart_get_axis_set (GOG_CHART (s->chart))) {
		default :
		case GOG_AXIS_SET_UNKNOWN :
		case GOG_AXIS_SET_NONE :
			break;
		case GOG_AXIS_SET_XY : {
			gboolean x_cross_at_max, y_cross_at_max, inverted,
			x_force_catserrange = FALSE, y_force_catserrange = FALSE;
			double xcross = go_nan, ycross = go_nan;
			char *str;
			if (axis_set->axis[GOG_AXIS_X] != NULL) {
				g_object_get (G_OBJECT (axis_set->axis[GOG_AXIS_X]),
					"pos-str", &str, "invert-axis", &inverted, NULL);
				y_cross_at_max = !strcmp (str, "high");
				x_cross_at_max = inverted;
				if (!strcmp (str, "cross"))
					ycross = gog_axis_get_entry (axis_set->axis[GOG_AXIS_X], GOG_AXIS_ELEM_CROSS_POINT, NULL);
				g_free (str);
			} else {
				g_object_get (G_OBJECT (s->primary_axis[GOG_AXIS_X]),
					"invert-axis", &x_inverted, NULL);
				x_cross_at_max = x_inverted;
				y_cross_at_max = FALSE;
				x_force_catserrange = gog_axis_is_discrete (s->primary_axis[GOG_AXIS_X]);
			}
			if (axis_set->axis[GOG_AXIS_Y] != NULL) {
				g_object_get (G_OBJECT (axis_set->axis[GOG_AXIS_Y]),
					"pos-str", &str, "invert-axis", &inverted, NULL);
				x_cross_at_max ^= !strcmp (str, "high");
				y_cross_at_max ^= inverted;
				if (!strcmp (str, "cross"))
					xcross = gog_axis_get_entry (axis_set->axis[GOG_AXIS_Y], GOG_AXIS_ELEM_CROSS_POINT, NULL);
				g_free (str);
			} else {
				g_object_get (G_OBJECT (s->primary_axis[GOG_AXIS_Y]),
					"pos-str", &str, "invert-axis", &y_inverted, NULL);
				y_cross_at_max ^= y_inverted;
				y_force_catserrange = gog_axis_is_discrete (s->primary_axis[GOG_AXIS_Y]);
				/* What did we want str for?  */
				g_free (str);
			}

			/* BIFF_CHART_pos, optional we use auto positioning */
			if (axis_set->transpose) {
				chart_write_axis (s, axis_set->axis[GOG_AXIS_Y],
					0, axis_set->center_ticks, y_force_catserrange, y_cross_at_max, y_inverted, ycross);
				chart_write_axis (s, axis_set->axis[GOG_AXIS_X],
					1, TRUE, x_force_catserrange, x_cross_at_max, x_inverted, xcross);
			} else {
				chart_write_axis (s, axis_set->axis[GOG_AXIS_X],
					0, axis_set->center_ticks, x_force_catserrange, x_cross_at_max, x_inverted, xcross);
				chart_write_axis (s, axis_set->axis[GOG_AXIS_Y],
					1, TRUE, y_force_catserrange, y_cross_at_max, y_inverted, ycross);
			}
			break;
		}
		case GOG_AXIS_SET_XY_pseudo_3d :
				chart_write_axis (s, axis_set->axis[GOG_AXIS_X],
					0, FALSE, TRUE, FALSE, FALSE, go_nan);
				chart_write_axis (s, axis_set->axis[GOG_AXIS_PSEUDO_3D],
					1, FALSE, FALSE, FALSE, FALSE, go_nan);
				chart_write_axis (s, axis_set->axis[GOG_AXIS_Y],
					2, FALSE, TRUE, FALSE, FALSE, go_nan);
			break;
		case GOG_AXIS_SET_RADAR :
				chart_write_axis (s, axis_set->axis[GOG_AXIS_CIRCULAR],
					0, FALSE, TRUE, FALSE, FALSE, go_nan);
				chart_write_axis (s, axis_set->axis[GOG_AXIS_RADIAL],
					1, FALSE, FALSE, FALSE, FALSE, go_nan);
			break;
		}

		if (i == 0) {
			GogObject *grid = gog_object_get_child_by_name (s->chart, "Backplane");
			if (grid != NULL) {
				ms_biff_put_empty (s->bp, BIFF_CHART_plotarea);
				chart_write_frame (s, grid, TRUE, TRUE);
			}
		}

		for (pptr = axis_set->plots ; pptr != NULL ; pptr = pptr->next, i++) {
			gboolean vary;
			guint16 flags = 0;

			g_object_get (G_OBJECT (pptr->data),
				      "vary-style-by-element", &vary,
				      NULL);

			data = ms_biff_put_len_next (s->bp, BIFF_CHART_chartformat, 20);
			memset (data, 0, 16);
			if (vary)
				flags |= 1;
			GSF_LE_SET_GUINT16 (data + 16, flags);
			GSF_LE_SET_GUINT16 (data + 18, i); /* use i as z order for now */
			ms_biff_put_commit (s->bp);

			chart_write_BEGIN (s);
			chart_write_plot (s, pptr->data);

			/* BIFF_CHART_chartformatlink documented as unnecessary */
			if (i == 0 && legend != NULL)
				chart_write_LEGEND (s, legend);
			nser = g_slist_length (GOG_PLOT (pptr->data)->series);
			if (i > 0) {
				/* write serieslist */
				int index = 0;
				data = ms_biff_put_len_next (s->bp, BIFF_CHART_serieslist, 2 + 2 * nser);
				GSF_LE_SET_GUINT16 (data, nser);
				while (index < nser) {
					GSF_LE_SET_GUINT16 (data + 2 + 2 * index, j + index);
					index++;
				}
				ms_biff_put_commit (s->bp);
			}
			j += nser;
			if (pptr->data == s->line_plot) {
				if (s->has_dropbar)
					chart_write_DROPBAR (s);
				if (s->has_hilow) {
					chart_write_CHARTLINE (s, 1, s->hl_style);
					g_object_unref (s->hl_style);
				}
				if (s->is_stock) {
					chart_write_DATAFORMAT (s, 0, 0, -3);
					chart_write_BEGIN (s);
					ms_biff_put_2byte (s->bp, BIFF_CHART_3dbarshape, 0); /* box */
					chart_write_LINEFORMAT (s, NULL, FALSE, TRUE);
					chart_write_AREAFORMAT (s, NULL, FALSE);
					chart_write_PIEFORMAT (s, 0.);
					chart_write_MARKERFORMAT (s, NULL, TRUE);
					chart_write_END (s);
					s->has_dropbar = FALSE;
					s->has_hilow = FALSE;
				}
			}
			chart_write_END (s);
		}
		/* Now write axes labels */
		if (axis_set->axis[GOG_AXIS_X] != NULL) {
			label = gog_object_get_child_by_name (GOG_OBJECT (axis_set->axis[GOG_AXIS_X]), "Label");
			if (label) {
				GOData *text = gog_dataset_get_dim (GOG_DATASET (label), 0);
				if (text != NULL) {
					chart_write_text (s, text,
						GO_STYLED_OBJECT (label), 3);
				}
			}
		}
		if (axis_set->axis[GOG_AXIS_Y] != NULL) {
			label = gog_object_get_child_by_name (GOG_OBJECT (axis_set->axis[GOG_AXIS_Y]), "Label");
			if (label) {
				GOData *text = gog_dataset_get_dim (GOG_DATASET (label), 0);
				if (text != NULL) {
					chart_write_text (s, text,
						GO_STYLED_OBJECT (label), 2);
				}
			}
		}

		chart_write_END (s);

		g_slist_free (axis_set->plots);
		g_free (axis_set);
	}
	g_slist_free (sets);
}

static void
chart_write_siindex (XLChartWriteState *s, guint msdim)
{
	guint8 *data;
	unsigned i, j, jmax;
	gboolean as_col;
	data = ms_biff_put_len_next (s->bp, BIFF_CHART_siindex, 2);
	GSF_LE_SET_GUINT16 (data, msdim);
	ms_biff_put_commit (s->bp);
	msdim--;
	for (i = 0; i < s->values[msdim]->len; i++) {
		XLValue *xlval = s->values[msdim]->pdata[i];
		if (!VALUE_IS_ARRAY (xlval->value))
			continue;
		as_col = xlval->value->v_array.y > xlval->value->v_array.x;
		jmax = as_col
			? xlval->value->v_array.y
			: xlval->value->v_array.x;
		for (j = 0; j < jmax; j++) {
			GnmValue const* value = as_col
				? xlval->value->v_array.vals[0][j]
				: xlval->value->v_array.vals[j][0];
			switch (value->v_any.type) {
			case VALUE_FLOAT:
				data = ms_biff_put_len_next (s->bp, BIFF_NUMBER_v2, 14);
				GSF_LE_SET_DOUBLE (data + 6, value_get_as_float (value));
				break;
			case VALUE_STRING: {
				guint8 dat[6];
				ms_biff_put_var_next (s->bp, BIFF_LABEL_v2);
				GSF_LE_SET_GUINT16 (dat, j);
				GSF_LE_SET_GUINT16 (dat + 2, i);
				GSF_LE_SET_GUINT16 (dat + 4, 0);
				ms_biff_put_var_write  (s->bp, (guint8*) dat, 6);
				excel_write_string (s->bp, STR_TWO_BYTE_LENGTH,
						    value_peek_string (value));
				ms_biff_put_commit (s->bp);
				continue;
			default:
				break;
			}
			}
			GSF_LE_SET_GUINT16 (data, j);
			GSF_LE_SET_GUINT16 (data + 2, i);
			GSF_LE_SET_GUINT16 (data + 4, 0);
			ms_biff_put_commit (s->bp);
		}
	}
}

void
ms_excel_chart_write (ExcelWriteState *ewb, SheetObject *so)
{
	guint8 *data;
	GogRenderer *renderer;
	XLChartWriteState state;
	unsigned i, num_series = 0;
	GSList const *plots, *series;
	GSList *charts, *sets, *ptr;
	XLAxisSet *axis_set = NULL;
	GogPlot *cur_plot;
	GError *error;
	GogLabel *label;
	double pos[4];

	state.bp  = ewb->bp;
	state.ewb = ewb;
	state.so  = so;
	state.graph = sheet_object_graph_get_gog (so);
	for (i = 0; i < 3; i++)
		state.values[i] = g_ptr_array_new ();
	state.pending_series = NULL;
	state.extra_objects = NULL;
	state.line_plot = NULL;
	state.line_set = NULL;
	state.has_dropbar = FALSE;
	state.has_hilow = FALSE;
	state.cur_set = 0;
	state.is_stock = FALSE;
	state.cur_vis_index = 0;

	g_return_if_fail (state.graph != NULL);

	charts = gog_object_get_children (GOG_OBJECT (state.graph),
		gog_object_find_role_by_name (GOG_OBJECT (state.graph), "Chart"));

	g_return_if_fail (charts != NULL);

	/* TODO : handle multiple charts */
	state.chart = charts->data;
	state.nest_level = 0;
	g_slist_free (charts);

	/* TODO : create a null renderer class for use in sizing things */
	sheet_object_position_pts_get (so, pos);
	renderer  = g_object_new (GOG_TYPE_RENDERER,
				  "model", state.graph,
				  NULL);
	gog_renderer_update (renderer, pos[2] - pos[0], pos[3] - pos[1]);
	g_object_get (G_OBJECT (renderer), "view", &state.root_view, NULL);


	excel_write_BOF (state.bp, MS_BIFF_TYPE_Chart);
	ms_biff_put_empty (state.bp, BIFF_HEADER);
	ms_biff_put_empty (state.bp, BIFF_FOOTER);
	ms_biff_put_2byte (state.bp, BIFF_HCENTER, 0);
	ms_biff_put_2byte (state.bp, BIFF_VCENTER, 0);
	/* TODO : maintain this info on import */
	excel_write_SETUP (state.bp, NULL);
	/* undocumented always seems to be 3 */
	ms_biff_put_2byte (state.bp, BIFF_PRINTSIZE, 3);
#if 0 /* do not write these until we know more */
	chart_write_FBI (&state, 0, 0x5);
	chart_write_FBI (&state, 1, 0x6);
#endif

	ms_biff_put_2byte (state.bp, BIFF_PROTECT, 0);
	ms_biff_put_2byte (state.bp, BIFF_CHART_units, 0);

#warning be smart about singletons and titles
	data = ms_biff_put_len_next (state.bp, BIFF_CHART_chart, 4*4);
//	chart_write_position (&state, state.chart, data, XL_POS_LOW, XL_POS_LOW);
	GSF_LE_SET_GUINT32 (data + 0, (int) (state.root_view->allocation.x * 65535.));
	GSF_LE_SET_GUINT32 (data + 4, (int) (state.root_view->allocation.y * 65535.));
	GSF_LE_SET_GUINT32 (data + 8, (int) (state.root_view->allocation.w * 65535.));
	GSF_LE_SET_GUINT32 (data + 12, (int) (state.root_view->allocation.h * 65535.));
	ms_biff_put_commit (state.bp);

	chart_write_BEGIN (&state);
	excel_write_SCL	(state.bp, 1.0, TRUE); /* seems unaffected by zoom */

	if (state.bp->version >= MS_BIFF_V8) {
		/* zoom does not seem to effect it */
		data = ms_biff_put_len_next (state.bp, BIFF_CHART_plotgrowth, 8);
		GSF_LE_SET_GUINT32 (data + 0, 0x10000);
		GSF_LE_SET_GUINT32 (data + 4, 0x10000);
		ms_biff_put_commit (state.bp);
	}
	chart_write_frame (&state, state.chart, FALSE, FALSE);
	/* collect axis sets */
	sets = NULL;
	for (i = GOG_AXIS_X; i < GOG_AXIS_TYPES; i++)
		state.primary_axis[i] = NULL;
	for (plots = gog_chart_get_plots (GOG_CHART (state.chart)) ; plots != NULL ; plots = plots->next) {
		/* XL cannot handle plots with no data */
		cur_plot = GOG_PLOT (plots->data);
		if (gog_plot_get_series (cur_plot) == NULL) {
			g_warning ("MS Excel cannot handle plots with no data, dropping %s",
				gog_object_get_name (plots->data));
			continue;
		}

		axis_set = g_new0 (XLAxisSet, 1);
		for (i = GOG_AXIS_X; i < GOG_AXIS_TYPES; i++) {
			axis_set->axis[i] = gog_plot_get_axis (cur_plot, i);
			if (state.primary_axis[i] == NULL)
				state.primary_axis[i] = axis_set->axis[i];
			else if (axis_set->axis[i] == state.primary_axis[i])
				axis_set->axis[i] = NULL; /* write a dummy axis */
		}

		if (0 == strcmp (G_OBJECT_TYPE_NAME (cur_plot), "GogBarColPlot")) {
			g_object_get (G_OBJECT (plots->data),
				      "horizontal", &axis_set->transpose,
				      NULL);
			axis_set->center_ticks = TRUE;
		} else if (0 == strcmp (G_OBJECT_TYPE_NAME (cur_plot), "GogAreaPlot"))
			axis_set->center_ticks = TRUE;
		else if (0 == strcmp (G_OBJECT_TYPE_NAME (cur_plot), "GogDropBarPlot")) {
			/* The code here is supposed to work on plots imported from excel
			Nothing sure about the ones created in gnumeric */
			/* drop bars concern the first and last series in an xl line plot so
			we must create a new plot and take only one series */
			GogSeries *orig, *first, *last;
			GOStyle *style;
			if (state.has_dropbar) {
				g_free (axis_set);
				continue;
			}
			if (state.line_plot == NULL) {
				state.line_plot = GOG_PLOT (gog_plot_new_by_name ("GogLinePlot"));
				state.line_set = axis_set;
			} else if (cb_axis_set_cmp (axis_set, state.line_set)) {
				/* excel does not support that AFAIK (Jean) */
				g_free (axis_set);
				continue;
			}
			state.has_dropbar = TRUE;
			axis_set->center_ticks = TRUE;
			/* now, take the first series in the plot and split it into two series */
			if (!state.is_stock) {
				char *group_name;
				g_object_get (G_OBJECT (cur_plot), "plot-group", &group_name, NULL);
				if (group_name && !strcmp (group_name, "GogStockPlot"))
					state.is_stock = TRUE;
			}
			orig = GOG_SERIES (gog_plot_get_series (cur_plot)->data);
			state.dp_style = go_style_dup (
					go_styled_object_get_style (GO_STYLED_OBJECT (orig)));
			g_object_get (cur_plot, "gap_percentage", &state.dp_width, NULL);
			first = gog_plot_new_series (state.line_plot);
			gog_series_set_dim (first, -1,
					go_data_dup (gog_dataset_get_dim (GOG_DATASET (orig), -1)),
			&error);
			gog_series_set_dim (first, 0,
					go_data_dup (gog_dataset_get_dim (GOG_DATASET (orig), 0)),
			&error);
			gog_series_set_dim (first, 1,
					go_data_dup (gog_dataset_get_dim (GOG_DATASET (orig), 1)),
			&error);
			if (!state.is_stock) {
				style = go_styled_object_get_style (GO_STYLED_OBJECT (first));
				/* FIXME: change this code when series lines are available ! */
				style->line.auto_dash = FALSE;
				style->line.auto_color = FALSE;
				style->line.dash_type = GO_LINE_NONE;
				style->marker.auto_shape = FALSE;
				go_marker_set_shape (style->marker.mark, GO_MARKER_NONE);
				style->marker.auto_fill_color = FALSE;
			}
			last = gog_plot_new_series (state.line_plot);
			gog_series_set_dim (last, -1,
					go_data_dup (gog_dataset_get_dim (GOG_DATASET (orig), -1)),
					&error);
			gog_series_set_dim (last, 0,
					go_data_dup (gog_dataset_get_dim (GOG_DATASET (orig), 0)),
					&error);
			gog_series_set_dim (last, 1,
					go_data_dup (gog_dataset_get_dim (GOG_DATASET (orig), 2)),
					&error);
			if (!state.is_stock) {
				style = go_styled_object_get_style (GO_STYLED_OBJECT (first));
				/* FIXME: change this code when series lines are available ! */
				style->line.auto_dash = FALSE;
				style->line.auto_color = FALSE;
				style->line.dash_type = GO_LINE_NONE;
				style->marker.auto_shape = FALSE;
				go_marker_set_shape (style->marker.mark, GO_MARKER_NONE);
				style->marker.auto_fill_color = FALSE;
			}
			/* now, bring the two series to the begining */
			if (g_slist_length (state.line_plot->series) > 2) {
				state.line_plot->series = g_slist_remove (state.line_plot->series, first);
				state.line_plot->series = g_slist_remove (state.line_plot->series, last);
				state.line_plot->series = g_slist_prepend (state.line_plot->series, last);
				state.line_plot->series = g_slist_prepend (state.line_plot->series, first);
			}
			cur_plot = (state.has_hilow)? NULL: state.line_plot;
		} else if (0 == strcmp (G_OBJECT_TYPE_NAME (cur_plot), "GogMinMaxPlot")) {
			/* same comment as for dropbars */
			GogSeries *orig, *high, *low;
			GOStyle *style;
			if (state.has_hilow) {
				g_free (axis_set);
				continue;
			}
			if (state.line_plot == NULL) {
				state.line_plot = GOG_PLOT (gog_plot_new_by_name ("GogLinePlot"));
				state.line_set = axis_set;
			} else if (cb_axis_set_cmp (axis_set, state.line_set)) {
				/* excel does not support that AFAIK (Jean) */
				g_free (axis_set);
				continue;
			}
			state.has_hilow = TRUE;
			axis_set->center_ticks = TRUE;
			if (!state.is_stock) {
				char *group_name;
				g_object_get (G_OBJECT (cur_plot), "plot-group", &group_name, NULL);
				if (group_name && !strcmp (group_name, "GogStockPlot"))
					state.is_stock = TRUE;
			}
			orig = GOG_SERIES (gog_plot_get_series (cur_plot)->data);
			state.hl_style = go_style_dup (
					go_styled_object_get_style (GO_STYLED_OBJECT (orig)));
			high = gog_plot_new_series (state.line_plot);
			gog_series_set_dim (high, -1,
					go_data_dup (gog_dataset_get_dim (GOG_DATASET (orig), -1)),
			&error);
			gog_series_set_dim (high, 0,
					go_data_dup (gog_dataset_get_dim (GOG_DATASET (orig), 0)),
			&error);
			gog_series_set_dim (high, 1,
					go_data_dup (gog_dataset_get_dim (GOG_DATASET (orig), 1)),
			&error);
			if (!state.is_stock) {
				style = go_styled_object_get_style (GO_STYLED_OBJECT (high));
				/* FIXME: change this code when series lines are available ! */
				style->line.auto_dash = FALSE;
				style->line.auto_color = FALSE;
				style->line.dash_type = GO_LINE_NONE;
				style->marker.auto_shape = FALSE;
				go_marker_set_shape (style->marker.mark, GO_MARKER_NONE);
				style->marker.auto_fill_color = FALSE;
			}
			low = gog_plot_new_series (state.line_plot);
			gog_series_set_dim (low, -1,
					go_data_dup (gog_dataset_get_dim (GOG_DATASET (orig), -1)),
					&error);
			gog_series_set_dim (low, 0,
					go_data_dup (gog_dataset_get_dim (GOG_DATASET (orig), 0)),
					&error);
			gog_series_set_dim (low, 1,
					go_data_dup (gog_dataset_get_dim (GOG_DATASET (orig), 2)),
					&error);
			if (!state.is_stock) {
				style = go_styled_object_get_style (GO_STYLED_OBJECT (low));
				/* FIXME: change this code when series lines are available ! */
				style->line.auto_dash = FALSE;
				style->line.auto_color = FALSE;
				style->line.dash_type = GO_LINE_NONE;
				style->marker.auto_shape = FALSE;
				go_marker_set_shape (style->marker.mark, GO_MARKER_NONE);
				style->marker.auto_fill_color = FALSE;
			}
			cur_plot = (state.has_dropbar)? NULL: state.line_plot;
		} else if (0 == strcmp (G_OBJECT_TYPE_NAME (cur_plot), "GogLinePlot")) {
			if (state.line_plot != NULL &&
					!cb_axis_set_cmp (axis_set, state.line_set)) {
				GogSeries *orig, *new;
				for (series = gog_plot_get_series (cur_plot); series != NULL;
							series = series->next) {
					orig = GOG_SERIES (series->data);
					new = gog_plot_new_series (state.line_plot);
					gog_series_set_dim (new, -1,
							go_data_dup (gog_dataset_get_dim (GOG_DATASET (orig), -1)),
							&error);
					gog_series_set_dim (new, 0,
							go_data_dup (gog_dataset_get_dim (GOG_DATASET (orig), 0)),
							&error);
					gog_series_set_dim (new, 1,
							go_data_dup (gog_dataset_get_dim (GOG_DATASET (orig), 1)),
							&error);
					go_styled_object_set_style (GO_STYLED_OBJECT (new),
							go_styled_object_get_style (GO_STYLED_OBJECT (orig)));
				}
				cur_plot = NULL;
			}
		}
		if (cur_plot) {
			ptr = g_slist_find_custom (sets, axis_set,
				(GCompareFunc) cb_axis_set_cmp);
			if (ptr != NULL) {
				g_free (axis_set);
				axis_set = ptr->data;
			} else
				sets = g_slist_append (sets, axis_set);
			axis_set->plots = g_slist_append (axis_set->plots, cur_plot);
		}
	}

	/* line plots with drop bars need to have the series in the correct order ! */
	if (state.line_plot && state.has_dropbar) {
		if (g_slist_length (state.line_plot->series) > 2) {
			gpointer data = g_slist_nth (state.line_plot->series, 1)->data;
			state.line_plot->series = g_slist_remove (state.line_plot->series, data);
			state.line_plot->series = g_slist_append (state.line_plot->series, data);
		}
	}

	/* dump the associated series (skip any that we are dropping */
	for (ptr = sets; ptr != NULL ; ptr = ptr->next) {
		for (plots = ((XLAxisSet *)ptr->data)->plots ; plots != NULL ; plots = plots->next) {
			/* first test if the plot uses a matrix or not */
			gboolean has_matrix = FALSE;
			GogPlotDesc const *desc = gog_plot_description (GOG_PLOT (plots->data));
			int n = 0, m;
			if (!desc) /* this should not happen, but ... */
				continue;
			for (m = 0; m < (int) desc->series.num_dim; m++)
				if (desc->series.dim[m].val_type == GOG_DIM_MATRIX) {
					n = m;
					has_matrix = TRUE;
					break; /* hopefully there is only one matrix */
				}
			if (!has_matrix) {
				for (series = gog_plot_get_series (plots->data) ; series != NULL ; series = series->next)
					num_series += chart_write_series (&state, series->data, num_series);
			} else if (n == 2) { /* surfaces and countours have the matrix as third data, other
						plot types that might use matrices will probably not be exportable
						to any of excel formats */
				/* we should have only one series there */
				GogSeries *ser = GOG_SERIES (gog_plot_get_series (plots->data)->data);
				/* create an equivalent XLContourPlot and save its series */
				if (ser != NULL) {
					gboolean as_col, s_as_col = FALSE;
					gboolean s_is_rc = FALSE, mat_is_rc;
					GnmExprTop const *stexpr = NULL;
					GnmExprTop const *mattexpr;
					GnmValue const *sval = NULL, *matval;
					GnmValue *val;
					GogSeries *serbuf;
					GnmRange vector, svec;
					int i, j, sn = 0, cur = 0, scur = 0;
					GOData *s, *c, *mat = ser->values[2].data;
					GODataMatrixSize size = go_data_matrix_get_size (GO_DATA_MATRIX (mat));
					GogPlot *plotbuf = (GogPlot*) gog_plot_new_by_name ((0 == strcmp (G_OBJECT_TYPE_NAME (plots->data), "GogContourPlot"))? "XLContourPlot": "XLSurfacePlot");
					Sheet *sheet = sheet_object_get_sheet (so);
					g_object_get (G_OBJECT (plots->data), "transposed", &as_col, NULL);
					mattexpr = gnm_go_data_get_expr (mat);
					mat_is_rc = gnm_expr_top_is_rangeref (mattexpr);
					if (mat_is_rc) {
						matval = gnm_expr_top_get_range (mattexpr);
					} else {
						matval = gnm_expr_top_get_constant (mattexpr);
					}
					if (as_col) {
						c = ser->values[1].data;
						s = ser->values[0].data;
					} else {
						c = ser->values[0].data;
						s = ser->values[1].data;
					}
					if (s) {
						sn = go_data_vector_get_len (GO_DATA_VECTOR (s));
						stexpr = gnm_go_data_get_expr (s);
						s_is_rc = gnm_expr_top_is_rangeref (stexpr);
					}
					if (mat_is_rc) {
						if (as_col) {
							vector.start.row = matval->v_range.cell.a.row;
							vector.end.row = matval->v_range.cell.b.row;
							cur = matval->v_range.cell.a.col;
						} else {
							vector.start.col = matval->v_range.cell.a.col;
							vector.end.col = matval->v_range.cell.b.col;
							cur = matval->v_range.cell.a.row;
						}
					} else {
					}
					if (s) {
						if (s_is_rc) {
							sval = gnm_expr_top_get_range (stexpr);
							s_as_col = sval->v_range.cell.a.col == sval->v_range.cell.b.col;
							if (s_as_col) {
								svec.start.col = svec.end.col = sval->v_range.cell.a.col;
								scur = sval->v_range.cell.a.row;
							} else {
								svec.start.row = svec.end.row = sval->v_range.cell.a.row;
								scur = sval->v_range.cell.a.col;
							}
						} else {
							sval = gnm_expr_top_get_constant (stexpr);
							s_as_col = sval->v_array.y > sval->v_array.x;
						}
					}
					n = (as_col)? size.columns: size.rows;
					if (!mat_is_rc)
						m = (as_col)? size.rows: size.columns;
					else
						m = 0;
					for (i = 0; i < n; i++) {
						serbuf = gog_plot_new_series (plotbuf);
						if (c) {
							g_object_ref (c);
							gog_series_set_dim (serbuf, 0, c, NULL);
						}
						if (s && (i < sn)) {
							if (s_is_rc) {
								if (s_as_col)
									svec.start.row = svec.end.row = scur++;
								else
									svec.start.col = svec.end.col = scur++;
								gog_series_set_dim (serbuf, -1,
									gnm_go_data_scalar_new_expr (sheet,
										gnm_expr_top_new_constant (
											value_new_cellrange_r (sheet, &svec))), NULL);
							} else {
								val = value_dup ((s_as_col)? sval->v_array.vals[0][i]:
											sval->v_array.vals[i][0]);
								gog_series_set_dim (serbuf, -1,
									gnm_go_data_scalar_new_expr (sheet,
										gnm_expr_top_new_constant ( val)), NULL);
							}
						}
						if (mat_is_rc) {
							if (as_col)
								vector.start.col = vector.end.col = cur++;
							else
								vector.start.row = vector.end.row = cur++;
							gog_series_set_dim (serbuf, 1,
								gnm_go_data_vector_new_expr (sheet,
									gnm_expr_top_new_constant (
										value_new_cellrange_r (sheet, &vector))), NULL);
						} else {
							val = value_new_array (m, 1);
							for (j = 0; j < m; j++) {
								value_array_set (val, j, 0,
									value_dup((as_col)? matval->v_array.vals[i][j]:
										matval->v_array.vals[j][i]));
								}
							gog_series_set_dim (serbuf, 1,
								gnm_go_data_vector_new_expr (sheet,
									gnm_expr_top_new_constant (val)), NULL);
						}
					}
					if (mat_is_rc)
						value_release ((GnmValue*) matval);
					if (s && s_is_rc)
						value_release ((GnmValue*) sval);
					for (series = gog_plot_get_series (plotbuf) ; series != NULL ; series = series->next)
						num_series += chart_write_series (&state, series->data, num_series);
					state.extra_objects = g_slist_append (state.extra_objects, plotbuf);
				}
			}
			state.cur_set++;
		}
	}

	data = ms_biff_put_len_next (state.bp, BIFF_CHART_shtprops, 4);
	GSF_LE_SET_GUINT32 (data + 0, 0xa);
	ms_biff_put_commit (state.bp);

#warning what do these connect to ?
	for (i = 2; i <= 3; i++) {
		ms_biff_put_2byte (state.bp, BIFF_CHART_defaulttext, i);
		chart_write_text (&state, NULL, NULL, 0);
	}

	state.cur_series = UINT_MAX;
	chart_write_axis_sets (&state, sets);

	/* write chart title if any */
	label = GOG_LABEL (gog_object_get_child_by_name (GOG_OBJECT (state.chart), "Title"));
	if (label == NULL)
		/* in that case, try the graph title */
		label = GOG_LABEL (gog_object_get_child_by_name (GOG_OBJECT (state.graph), "Title"));
	if (label != NULL) {
		GOData *text = gog_dataset_get_dim (GOG_DATASET (label), 0);
		if (text != NULL) {
			chart_write_text (&state, text,
				GO_STYLED_OBJECT (label), 1);
		}
	}

	for (i = 0; i < 3; i++) {
		chart_write_siindex (&state, i + 1);
		g_ptr_array_foreach (state.values[i], (GFunc) g_free, NULL);
		g_ptr_array_free (state.values[i], TRUE);
	}
	g_slist_free_full (state.extra_objects, g_object_unref);
	if (state.line_plot)
		g_object_unref (state.line_plot);

	chart_write_END (&state);
#if 0 /* they seem optional */
	BIFF_DIMENSIONS
	BIFF_CHART_siindex x num_series ?
#endif
	ms_biff_put_empty (ewb->bp, BIFF_EOF);

	g_object_unref (state.root_view);
	g_object_unref (renderer);
}
