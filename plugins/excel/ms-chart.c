/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/**
 * ms-chart.c: MS Excel chart support for Gnumeric
 *
 * Author:
 *    Jody Goldberg (jody@gnome.org)
 *
 * (C) 1999-2004 Jody Goldberg
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

#include <parse-util.h>
#include <format.h>
#include <expr.h>
#include <value.h>
#include <gutils.h>
#include <graph.h>
#include <style-color.h>
#include <sheet-object-graph.h>
#include <workbook-view.h>

#include <goffice/graph/goffice-graph.h>
#include <goffice/graph/gog-graph.h>
#include <goffice/graph/gog-chart.h>
#include <goffice/graph/gog-axis.h>
#include <goffice/graph/gog-plot-impl.h>
#include <goffice/graph/gog-series-impl.h>
#include <goffice/graph/gog-object.h>
#include <goffice/graph/gog-styled-object.h>
#include <goffice/graph/gog-style.h>
#include <goffice/graph/gog-plot-engine.h>
#include <goffice/graph/gog-renderer.h>
#include <goffice/graph/go-data-simple.h>
#include <goffice/utils/go-color.h>
#include <goffice/utils/go-pattern.h>
#include <goffice/utils/go-marker.h>

#include <gsf/gsf-utils.h>
#include <math.h>
#include <string.h>
#include <stdio.h>

/* #define NO_DEBUG_EXCEL */
#ifndef NO_DEBUG_EXCEL
#define d(level, code)	do { if (ms_excel_chart_debug > level) { code } } while (0)
#else
#define d(level, code)
#endif

typedef struct {
	struct {
		int num_elements;
		GOData *data;
	} data [GOG_MS_DIM_TYPES];
	int chart_group;
	gboolean  has_legend;
	GogStyle *style;
	GHashTable *singletons;
} XLChartSeries;

typedef struct {
	MSContainer	 container;

	GArray		*stack;
	MsBiffVersion	 ver;
	guint32		 prev_opcode;

	SheetObject	*sog;
	GogGraph	*graph;
	GogChart	*chart;
	GogObject	*legend;
	GogPlot		*plot;
	GogStyle	*default_plot_style;
	GogObject	*axis;
	GogStyle	*style;

	int		 style_element;

	gboolean	 frame_for_grid;
	gboolean	 has_a_grid;
	int		 plot_counter;
	XLChartSeries	*currentSeries;
	GPtrArray	*series;
	char		*text;
} XLChartReadState;

typedef struct _XLChartHandler	XLChartHandler;
typedef gboolean (*XLChartReader) (XLChartHandler const *handle,
				   XLChartReadState *, BiffQuery *q);
struct _XLChartHandler {
	guint16 const opcode;
	int const	min_size; /* To be useful this needs to be versioned */
	char const *const name;
	XLChartReader const read_fn;
};

#define BC(n)	biff_chart_ ## n
#define BC_R(n)	BC(read_ ## n)

static XLChartSeries *
excel_chart_series_new (void)
{
	XLChartSeries *series;
	int i;

	series = g_new (XLChartSeries, 1);

	series->chart_group = -1;
	series->has_legend = TRUE;
	series->style = NULL;
	series->singletons = NULL;
	for (i = GOG_MS_DIM_TYPES; i-- > 0 ; ) {
		series->data [i].data = NULL;
		series->data [i].num_elements = 0;
	}

	return series;
}

static void
excel_chart_series_delete (XLChartSeries *series)
{
	int i;

	for (i = GOG_MS_DIM_TYPES; i-- > 0 ; )
		if (series->data [i].data != NULL)
			g_object_unref (series->data[i].data);
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
		s->style = gog_style_new ();
}

static int
BC_R(top_state) (XLChartReadState *s)
{
	g_return_val_if_fail (s != NULL, 0);
	return g_array_index (s->stack, int, s->stack->len-1);
}

static GOColor
BC_R(color) (guint8 const *data, char const *type)
{
	guint32 const bgr = GSF_LE_GET_GUINT32 (data);
	guint16 const r = (bgr >>  0) & 0xff;
	guint16 const g = (bgr >>  8) & 0xff;
	guint16 const b = (bgr >> 16) & 0xff;

	d (1, fprintf(stderr, "%s %02x:%02x:%02x;\n", type, r, g, b););
	return RGBA_TO_UINT (r, g, b, 0xff);
}

/****************************************************************************/

static gboolean
BC_R(3dbarshape)(XLChartHandler const *handle,
		 XLChartReadState *s, BiffQuery *q)
{
	d (0, {
		guint16 const type = GSF_LE_GET_GUINT16 (q->data);
		switch (type) {
		case 0 : fputs ("box", stderr); break;
		case 1 : fputs ("cylinder", stderr); break;
		case 256 : fputs ("pyramid", stderr); break;
		case 257 : fputs ("cone", stderr); break;
		default :
			   fprintf (stderr, "unknown 3dshape %d\n", type);
		};
	});

	return FALSE;
}
/****************************************************************************/

static gboolean
BC_R(3d)(XLChartHandler const *handle,
	 XLChartReadState *s, BiffQuery *q)
{
	d (0, {
		guint16 const rotation = GSF_LE_GET_GUINT16 (q->data);	/* 0-360 */
		guint16 const elevation = GSF_LE_GET_GUINT16 (q->data+2);	/* -90 - 90 */
		guint16 const distance = GSF_LE_GET_GUINT16 (q->data+4);	/* 0 - 100 */
		guint16 const height = GSF_LE_GET_GUINT16 (q->data+6);
		guint16 const depth = GSF_LE_GET_GUINT16 (q->data+8);
		guint16 const gap = GSF_LE_GET_GUINT16 (q->data+10);
		guint8 const flags = GSF_LE_GET_GUINT8 (q->data+12);
		guint8 const zero = GSF_LE_GET_GUINT8 (q->data+13);

		gboolean const use_perspective = (flags&0x01) ? TRUE :FALSE;
		gboolean const cluster = (flags&0x02) ? TRUE :FALSE;
		gboolean const auto_scale = (flags&0x04) ? TRUE :FALSE;
		gboolean const walls_2d = (flags&0x20) ? TRUE :FALSE;

		g_return_val_if_fail (zero == 0, FALSE); /* just warn for now */

		fprintf (stderr, "Rot = %hu\n", rotation);
		fprintf (stderr, "Elev = %hu\n", elevation);
		fprintf (stderr, "Dist = %hu\n", distance);
		fprintf (stderr, "Height = %hu\n", height);
		fprintf (stderr, "Depth = %hu\n", depth);
		fprintf (stderr, "Gap = %hu\n", gap);

		if (use_perspective)
			fputs ("Use perspective;\n", stderr);
		if (cluster)
			fputs ("Cluster;\n", stderr);
		if (auto_scale)
			fputs ("Auto Scale;\n", stderr);
		if (walls_2d)
			fputs ("2D Walls;\n", stderr);
	});

	return FALSE;
}

/****************************************************************************/

static gboolean
BC_R(ai)(XLChartHandler const *handle,
	 XLChartReadState *s, BiffQuery *q)
{
	guint8 const purpose = GSF_LE_GET_GUINT8 (q->data);
	guint8 const ref_type = GSF_LE_GET_GUINT8 (q->data + 1);
	guint16 const flags = GSF_LE_GET_GUINT16 (q->data + 2);
	guint16 const length = GSF_LE_GET_GUINT16 (q->data + 6);

	int popped_state = BC_R(top_state) (s);

	/* ignore these for now */
	if (popped_state == BIFF_CHART_text)
		return FALSE;

	/* Rest are 0 */
	if (flags&0x01) {
		GnmFormat *fmt = ms_container_get_fmt (&s->container,
			GSF_LE_GET_GUINT16 (q->data + 4));
		d (2, fputs ("Has Custom number format;\n", stderr););
		if (fmt != NULL) {
			char *desc = style_format_as_XL (fmt, FALSE);
			d (2, fprintf (stderr, "Format = '%s';\n", desc););
			g_free (desc);

			style_format_unref (fmt);
		}
	} else {
		d (2, fputs ("Uses number format from data source;\n", stderr););
	}

	g_return_val_if_fail (purpose < GOG_MS_DIM_TYPES, TRUE);
	d (0, {
	switch (purpose) {
	case GOG_MS_DIM_LABELS :     fputs ("Labels;\n", stderr); break;
	case GOG_MS_DIM_VALUES :     fputs ("Values;\n", stderr); break;
	case GOG_MS_DIM_CATEGORIES : fputs ("Categories;\n", stderr); break;
	case GOG_MS_DIM_BUBBLES :    fputs ("Bubbles;\n", stderr); break;
	default :
		g_assert_not_reached ();
	};
	switch (ref_type) {
	case 0 : fputs ("Use default categories;\n", stderr); break;
	case 1 : fputs ("Text/Value entered directly;\n", stderr); break;
	case 2 : fputs ("Linked to Container;\n", stderr); break;
	case 4 : fputs ("'Error reported' what the heck is this ??;\n", stderr); break;
	default :
		 fprintf (stderr, "UKNOWN : reference type (%x)\n", ref_type);
	};
	});

	/* (2) == linked to container */
	if (ref_type == 2) {
		GnmExpr const *expr = ms_container_parse_expr (&s->container,
			q->data+8, length);
		if (expr != NULL) {
			Sheet *sheet = ms_container_sheet (s->container.parent);

			g_return_val_if_fail (sheet != NULL, FALSE);
			g_return_val_if_fail (s->currentSeries != NULL, TRUE);

			s->currentSeries->data [purpose].data = (purpose == GOG_MS_DIM_LABELS)
				? gnm_go_data_scalar_new_expr (sheet, expr)
				: gnm_go_data_vector_new_expr (sheet, expr);
		}
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
	gint16 length = GSF_LE_GET_GUINT16 (q->data);
	guint8 const *in = (q->data + 2);
	char *const ans = (char *) g_new (char, length + 2);
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

	/*fputs (ans, stderr);*/
	return FALSE;
}

/****************************************************************************/

static gboolean
BC_R(area)(XLChartHandler const *handle,
	   XLChartReadState *s, BiffQuery *q)
{
	guint16 const flags = GSF_LE_GET_GUINT16 (q->data);
	char const *type = "normal";
	gboolean in_3d = (s->container.ver >= MS_BIFF_V8 && (flags & 0x04));

	g_return_val_if_fail (s->plot == NULL, TRUE);
	s->plot = gog_plot_new_by_name ("GogAreaPlot");
	g_return_val_if_fail (s->plot != NULL, TRUE);

	if (flags & 0x02)
		type = "as_percentage";
	else if (flags & 0x01)
		type = "stacked";

	g_object_set (G_OBJECT (s->plot),
		"type",			type,
		"in_3d",		in_3d,
		NULL);

	d(1, fprintf (stderr, "%s area;", type););
	return FALSE;
}

/****************************************************************************/

static gboolean
BC_R(areaformat)(XLChartHandler const *handle,
		 XLChartReadState *s, BiffQuery *q)
{
	guint16 const pattern = GSF_LE_GET_GUINT16 (q->data+8);
	guint16 const flags = GSF_LE_GET_GUINT16 (q->data+10);
	gboolean const auto_format = (flags & 0x01) ? TRUE : FALSE;
	gboolean const invert_if_negative = flags & 0x02;

	d (0, {
		fprintf (stderr, "pattern = %d;\n", pattern);
		if (auto_format)
			fputs ("Use auto format;\n", stderr);
		if (invert_if_negative)
			fputs ("Swap fore and back colours when displaying negatives;\n", stderr);
	});

#if 0 
	/* 18 */ "5%"
#endif
	BC_R(get_style) (s);
	if (pattern > 0) {
		s->style->fill.type = GOG_FILL_STYLE_PATTERN;
		s->style->fill.auto_back = auto_format;
		s->style->fill.invert_if_negative = invert_if_negative;
		s->style->fill.pattern.pattern = pattern - 1;
		s->style->fill.pattern.fore = BC_R(color) (q->data+0, "AreaFore");
		s->style->fill.pattern.back = BC_R(color) (q->data+4, "AreaBack");
		if (s->style->fill.pattern.pattern == 0) {
			GOColor tmp = s->style->fill.pattern.fore;
			s->style->fill.pattern.fore = s->style->fill.pattern.back;
			s->style->fill.pattern.back = tmp;
		}
	} else if (auto_format) {
		s->style->fill.type = GOG_FILL_STYLE_PATTERN;
		s->style->fill.auto_back = TRUE;
		s->style->fill.invert_if_negative = invert_if_negative;
		s->style->fill.pattern.pattern = 0;
		s->style->fill.pattern.fore =
		s->style->fill.pattern.back = 0;
	} else
		s->style->fill.type = GOG_FILL_STYLE_NONE;

	return FALSE;
}

/****************************************************************************/

static gboolean
BC_R(attachedlabel)(XLChartHandler const *handle,
		    XLChartReadState *s, BiffQuery *q)
{
	d (3,{
	guint16 const flags = GSF_LE_GET_GUINT16 (q->data);
	gboolean const show_value = (flags&0x01) ? TRUE : FALSE;
	gboolean const show_percent = (flags&0x02) ? TRUE : FALSE;
	gboolean const show_label_prercent = (flags&0x04) ? TRUE : FALSE;
	gboolean const smooth_line = (flags&0x08) ? TRUE : FALSE;
	gboolean const show_label = (flags&0x10) ? TRUE : FALSE;

	if (show_value)
		fputs ("Show Value;\n", stderr);
	if (show_percent)
		fputs ("Show as Percentage;\n", stderr);
	if (show_label_prercent)
		fputs ("Show as Label Percentage;\n", stderr);
	if (smooth_line)
		fputs ("Smooth line;\n", stderr);
	if (show_label)
		fputs ("Show the label;\n", stderr);

	if (s->container.ver >= MS_BIFF_V8) {
		gboolean const show_bubble_size = (flags&0x20) ? TRUE : FALSE;
		if (show_bubble_size)
			fputs ("Show bubble size;\n", stderr);
	}
	});
	return FALSE;
}

/****************************************************************************/

static gboolean
BC_R(axesused)(XLChartHandler const *handle,
	       XLChartReadState *s, BiffQuery *q)
{
	guint16 const num_axis = GSF_LE_GET_GUINT16 (q->data);
	g_return_val_if_fail(1 <= num_axis && num_axis <= 2, TRUE);
	d (0, fprintf (stderr, "There are %hu axis.\n", num_axis););
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
		"X-Axis", "Y-Axis", "Series-Axis"
	};

	guint16 const axis_type = GSF_LE_GET_GUINT16 (q->data);

	g_return_val_if_fail (axis_type < G_N_ELEMENTS (ms_axis), TRUE);
	g_return_val_if_fail (s->axis == NULL, TRUE);

	s->axis = gog_object_add_by_name (GOG_OBJECT (s->chart),
					  ms_axis [axis_type], NULL);

	d (0, fprintf (stderr, "This is a %s .\n", ms_axis[axis_type]););
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
	guint16 const type = GSF_LE_GET_GUINT16 (q->data);

	d (0, {
	fprintf (stderr, "Axisline is ");
	switch (type)
	{
	case 0 : fputs ("the axis line.\n", stderr); break;
	case 1 : fputs ("a major grid along the axis.\n", stderr); break;
	case 2 : fputs ("a minor grid along the axis.\n", stderr); break;

	/* TODO TODO : floor vs wall */
	case 3 : fputs ("a floor/wall along the axis.\n", stderr); break;
	default : fprintf (stderr, "an ERROR.  unkown type (%x).\n", type);
	};
	});

	if (!ms_biff_query_peek_next (q, &opcode) || opcode != BIFF_CHART_lineformat) {
		g_warning ("I had hoped that a lineformat would always follow an axislineformat");
		return FALSE;
	}
	ms_biff_query_next (q);
	if (BC_R(lineformat)(handle, s, q))
		return TRUE;

	if (type == 0 && s->axis != NULL) {
		g_object_set (G_OBJECT (s->axis),
			"style", s->style,
			NULL);
		/* deleted axis sets flag here, rather than in TICK */
		if (0 == (0x4 & GSF_LE_GET_GUINT16 (q->data+8)))
			g_object_set (G_OBJECT (s->axis),
				"major-tick-labeled",	FALSE,
				NULL);
	}
	g_object_unref (s->style);
	s->style = NULL;

	return FALSE;
}

/****************************************************************************/

static gboolean
BC_R(axisparent)(XLChartHandler const *handle,
		 XLChartReadState *s, BiffQuery *q)
{
	d (1, {
	guint16 const index = GSF_LE_GET_GUINT16 (q->data);	/* 1 or 2 */
	/* Measured in 1/4000ths of the chart width */
	guint32 const x = GSF_LE_GET_GUINT32 (q->data+2);
	guint32 const y = GSF_LE_GET_GUINT32 (q->data+6);
	guint32 const width = GSF_LE_GET_GUINT32 (q->data+10);
	guint32 const height = GSF_LE_GET_GUINT32 (q->data+14);

	fprintf (stderr, "Axis # %hu @ %f,%f, X=%f, Y=%f\n",
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
	int overlap_percentage = -GSF_LE_GET_GINT16 (q->data); /* dipsticks */
	int gap_percentage = GSF_LE_GET_GINT16 (q->data+2);
	guint16 const flags = GSF_LE_GET_GUINT16 (q->data+4);
	gboolean horizontal = (flags & 0x01) != 0;
	gboolean in_3d = (s->container.ver >= MS_BIFF_V8 && (flags & 0x08));

	g_return_val_if_fail (s->plot == NULL, TRUE);
	s->plot = gog_plot_new_by_name ("GogBarColPlot");
	g_return_val_if_fail (s->plot != NULL, TRUE);

	if (flags & 0x04)
		type = "as_percentage";
	else if (flags & 0x02)
		type = "stacked";

	g_object_set (G_OBJECT (s->plot),
		"horizontal",		horizontal,
		"type",			type,
		"in_3d",		in_3d,
		"overlap_percentage",	overlap_percentage,
		"gap_percentage",	gap_percentage,
		NULL);
	d(1, fprintf (stderr, "%s bar with gap = %d, overlap = %d;",
		      type, gap_percentage, overlap_percentage););
	return FALSE;
}

/****************************************************************************/

static gboolean
BC_R(begin)(XLChartHandler const *handle,
	    XLChartReadState *s, BiffQuery *q)
{
	d(0, fputs ("{\n", stderr););
	s->stack = g_array_append_val (s->stack, s->prev_opcode);
	return FALSE;
}

/****************************************************************************/

static gboolean
BC_R(boppop)(XLChartHandler const *handle,
	     XLChartReadState *s, BiffQuery *q)
{
#if 0
	guint8 const type = GSF_LE_GET_GUINT8 (q->data); /* 0-2 */
	gboolean const use_default_split = (GSF_LE_GET_GUINT8 (q->data+1) == 1);
	guint16 const split_type = GSF_LE_GET_GUINT8 (q->data+2); /* 0-3 */
#endif

	gboolean const is_3d = (GSF_LE_GET_GUINT16 (q->data+16) == 1);
	if (is_3d)
		fputs("in 3D", stderr);

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
	return FALSE;
}

/****************************************************************************/

static gboolean
BC_R(chart)(XLChartHandler const *handle,
	    XLChartReadState *s, BiffQuery *q)
{
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
	fprintf(stderr, "Chart @ %g, %g is %g\" x %g\"\n", x_pos, y_pos, x_size, y_size);
	});

	return FALSE;
}

/****************************************************************************/

static gboolean
BC_R(chartformat)(XLChartHandler const *handle,
		  XLChartReadState *s, BiffQuery *q)
{
	guint16 const flags = GSF_LE_GET_GUINT16 (q->data+16);
	guint16 const z_order = GSF_LE_GET_GUINT16 (q->data+18);

	/* always update the counter to keep the index in line with the chart
	 * group specifier for series */
	s->plot_counter++;

	if (s->plot != NULL)
		g_object_set (G_OBJECT (s->plot),
			"vary_style_by_element", (flags & 0x01) ? TRUE : FALSE,
			NULL);
#if 0
	"index", s->plot_counter
	"stacking_position", z_order
#endif

	d (0, {
		fprintf (stderr, "Z value = %uh\n", z_order);
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
	guint16 const type = GSF_LE_GET_GUINT16 (q->data);

	g_return_val_if_fail (type <= 2, FALSE);

	d (0, fprintf (stderr, "Use %s lines\n",
	     (type == 0) ? "drop" : ((type == 1) ? "hi-lo" : "series")););

	return FALSE;
}

/****************************************************************************/

static gboolean
BC_R(clrtclient)(XLChartHandler const *handle,
		 XLChartReadState *s, BiffQuery *q)
{
	fputs ("Undocumented BIFF : clrtclient", stderr);
	ms_biff_query_dump (q);
	return FALSE;
}
/****************************************************************************/

static gboolean
BC_R(dat)(XLChartHandler const *handle,
	  XLChartReadState *s, BiffQuery *q)
{
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
	guint16 const pt_num = GSF_LE_GET_GUINT16 (q->data);
	guint16 const series_index = GSF_LE_GET_GUINT16 (q->data+2);
#if 0
	guint16 const series_index_for_label = GSF_LE_GET_GUINT16 (q->data+4);
	guint16 const excel4_auto_color = GSF_LE_GET_GUINT16 (q->data+6) & 0x01;
#endif

	g_return_val_if_fail (series_index < s->series->len, TRUE);

	series = g_ptr_array_index (s->series, series_index);

	g_return_val_if_fail (series != NULL, TRUE);

	if (pt_num == 0xffff) {
		s->style_element = -1;
		d (0, fprintf (stderr, "All points"););
	} else {
		s->style_element = pt_num;
		d (0, fprintf (stderr, "Point[%hu]", pt_num););
	}

	d (0, fprintf (stderr, ", series=%hu\n", series_index););

	return FALSE;
}

/****************************************************************************/

static gboolean
BC_R(defaulttext)(XLChartHandler const *handle,
		  XLChartReadState *s, BiffQuery *q)
{
	guint16	const tmp = GSF_LE_GET_GUINT16 (q->data);

	d (2, fprintf (stderr, "applicability = %hd\n", tmp););

	/*
	 * 0 == 'show labels' label
	 * 1 == Value and percentage data label
	 * 2 == All text in chart
	 * 3 == Undocumented ??
	 */
	g_return_val_if_fail (tmp <= 3, TRUE);
	return FALSE;
}

/****************************************************************************/

static gboolean
BC_R(dropbar)(XLChartHandler const *handle,
	      XLChartReadState *s, BiffQuery *q)
{
	/* NOTE : The docs lie.  values > 100 seem legal.  My guess based on
	 * the ui is 500.
	guint16 const width = GSF_LE_GET_GUINT16 (q->data);
	 */
	return FALSE;
}

/****************************************************************************/

static gboolean
BC_R(fbi)(XLChartHandler const *handle,
	  XLChartReadState *s, BiffQuery *q)
{
	/*
	 * TODO TODO TODO : Work on appropriate scales.
	 * Is any of this useful other than the index ?
	 */
	guint16 const x_basis = GSF_LE_GET_GUINT16 (q->data);
	guint16 const y_basis = GSF_LE_GET_GUINT16 (q->data+2);
	guint16 const applied_height = GSF_LE_GET_GUINT16 (q->data+4);
	guint16 const scale_basis = GSF_LE_GET_GUINT16 (q->data+6);
	guint16 const index = GSF_LE_GET_GUINT16 (q->data+8);

	d (2,
		gsf_mem_dump (q->data, q->length);
		fprintf (stderr, "Font %hu (%hu x %hu) scale=%hu, height=%hu\n",
			index, x_basis, y_basis, scale_basis, applied_height););
	return FALSE;
}
/****************************************************************************/

static gboolean
BC_R(fontx)(XLChartHandler const *handle,
	    XLChartReadState *s, BiffQuery *q)
{
#if 0
	/* Child of TEXT, index into FONT table */
	guint16 const font = GSF_LE_GET_GUINT16 (q->data);
#endif
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
	d (0, fputs (s->frame_for_grid ? "For grid;\n" : "Not for grid;\n", stderr););

	return FALSE;
}

/****************************************************************************/

static GOColor
ms_chart_map_color (XLChartReadState const *s, guint32 raw, guint32 alpha)
{
	GOColor res;
	if ((~0x7ffffff) & raw) {
		GnmColor *c= excel_palette_get (s->container.ewb->palette,
			(0x7ffffff & raw));
		res = GDK_TO_UINT (c->color);
		style_color_unref (c);
	} else {
		guint8 r, g, b;
		r = (raw)       & 0xff;
		g = (raw >> 8)  & 0xff;
		b = (raw >> 16) & 0xff;
		res = RGBA_TO_UINT (r, g, b, 0xff);
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

	s->style->fill.type = GOG_FILL_STYLE_GRADIENT;
	s->style->fill.pattern.fore = 
		ms_chart_map_color (s, fill_color, fill_alpha);

	/* FIXME : make presets the same as 2 color for now */
	if (!(shade_type & 8) || preset > 0) {
		s->style->fill.pattern.back = ms_chart_map_color (s,
			fill_back_color, fill_back_alpha);
	} else {
		float brightness;
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
		gog_style_set_fill_brightness (s->style, (1. - brightness) * 100.);
		d (1, fprintf (stderr, "%x : frac = %u, flag = 0x%hx ::: %f",
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
	GnmFormat *fmt = ms_container_get_fmt (&s->container,
		GSF_LE_GET_GUINT16 (q->data));

	if (fmt != NULL) {
		char *desc = style_format_as_XL (fmt, FALSE);

		if (s->axis != NULL)
			g_object_set (G_OBJECT (s->axis),
				"assigned-format-string-XL", desc,
				NULL);
		d (0, fprintf (stderr, "Format = '%s';\n", desc););
		g_free (desc);
		style_format_unref (fmt);
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
	guint16 const XL_pos = GSF_LE_GET_GUINT8 (q->data+16);
	GogObjectPosition pos;

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
	};

	s->legend = gog_object_add_by_name (GOG_OBJECT (s->chart), "Legend", NULL);
	gog_object_set_pos (s->legend, pos);

#if 0
	fprintf (stderr, "Legend @ %f,%f, X=%f, Y=%f\n",
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
	guint16 const flags = GSF_LE_GET_GUINT16 (q->data+2);
	if ((flags & 1) && s->currentSeries != NULL)
		s->currentSeries->has_legend = FALSE;
	return FALSE;
}

/****************************************************************************/

static gboolean
BC_R(line)(XLChartHandler const *handle,
	   XLChartReadState *s, BiffQuery *q)
{
	guint16 const flags = GSF_LE_GET_GUINT16 (q->data);
	char const *type = "normal";
	gboolean in_3d = (s->container.ver >= MS_BIFF_V8 && (flags & 0x04));

	g_return_val_if_fail (s->plot == NULL, TRUE);
	s->plot = gog_plot_new_by_name ("GogLinePlot");
	g_return_val_if_fail (s->plot != NULL, TRUE);

	if (flags & 0x02)
		type = "as_percentage";
	else if (flags & 0x01)
		type = "stacked";

	g_object_set (G_OBJECT (s->plot),
		"type",			type,
		"in_3d",		in_3d,
		NULL);

	d(1, fprintf (stderr, "%s line;", type););
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
	static GOLineDashType const dash_map []= {
		GO_LINE_SOLID,
		GO_LINE_DASH,
		GO_LINE_DOT,
		GO_LINE_DASH_DOT,
		GO_LINE_DASH_DOT_DOT,
		GO_LINE_NONE
	};
	guint16 const flags = GSF_LE_GET_GUINT16 (q->data+8);

	BC_R(get_style) (s);
	switch (GSF_LE_GET_GUINT16 (q->data+6)) {
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
	s->style->line.auto_color = (flags & 0x01) ? TRUE : FALSE;
	s->style->line.pattern    = GSF_LE_GET_GUINT16 (q->data+4);

	d (0, fprintf (stderr, "flags == %hd.\n", flags););
	d (0, fprintf (stderr, "Lines are %f pts wide.\n", s->style->line.width););
	d (0, fprintf (stderr, "Lines have a %s pattern.\n",
		       ms_line_pattern [s->style->line.pattern ]););

	if (s->style->line.pattern <= G_N_ELEMENTS (dash_map))
		s->style->line.dash_type = dash_map [s->style->line.pattern];
	else
		s->style->line.dash_type = GO_LINE_SOLID;

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
	guint16 shape = GSF_LE_GET_GUINT16 (q->data+8);
	guint16 const flags = GSF_LE_GET_GUINT16 (q->data+10);
	gboolean const auto_color = (flags & 0x01) ? TRUE : FALSE;

	BC_R(get_style) (s);
	marker = go_marker_new ();

	d (0, fprintf (stderr, "Marker = %s\n", ms_chart_marker [shape]););
	if (shape >= G_N_ELEMENTS (shape_map))
		shape = 1; /* square */
	go_marker_set_shape (marker, shape_map [shape]);

	go_marker_set_outline_color (marker, 
		(flags & 0x20) ? 0 : BC_R(color) (q->data + 0, "MarkerFore"));
	go_marker_set_fill_color (marker, 
		(flags & 0x10) ? 0 : BC_R(color) (q->data + 4, "MarkerBack"));

	s->style->marker.auto_shape = shape > 0;
	s->style->marker.auto_outline_color =
		s->style->marker.auto_fill_color = auto_color;

	if (s->container.ver >= MS_BIFF_V8) {
		guint32 const marker_size = GSF_LE_GET_GUINT32 (q->data+16);
		go_marker_set_size (marker, marker_size / 20.);
		d (1, fprintf (stderr, "Marker size : is %f pts\n", marker_size / 20.););
	}

	gog_style_set_marker (s->style, marker);

	return FALSE;
}

/****************************************************************************/

static gboolean
BC_R(objectlink)(XLChartHandler const *handle,
		 XLChartReadState *s, BiffQuery *q)
{
	guint16 const purpose = GSF_LE_GET_GUINT16 (q->data);
	GogObject *label = NULL;
	
	if (s->text == NULL)
		return FALSE;

	if (purpose == 1) {
		g_return_val_if_fail (s->chart != NULL, FALSE);
		label = gog_object_add_by_name (GOG_OBJECT (s->chart), "Title", NULL);
	} else if (purpose == 2 || purpose == 3 || purpose == 7) {
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
		axes = gog_chart_get_axis (s->chart, t);

		g_return_val_if_fail (axes != NULL, FALSE);

		label = gog_object_add_by_name (GOG_OBJECT (axes->data), "Label", NULL);
	}

	if (label != NULL) {
		gog_dataset_set_dim (GOG_DATASET (label), 0,
			go_data_scalar_str_new (s->text, TRUE), NULL);
		s->text = NULL;
	}

	d (2, {
	guint16 const series_num = GSF_LE_GET_GUINT16 (q->data+2);
	guint16 const pt_num = GSF_LE_GET_GUINT16 (q->data+2);

	switch (purpose) {
	case 1 : fprintf (stderr, "TEXT is chart title\n"); break;
	case 2 : fprintf (stderr, "TEXT is Y axis title\n"); break;
	case 3 : fprintf (stderr, "TEXT is X axis title\n"); break;
	case 4 : fprintf (stderr, "TEXT is data label for pt %hd in series %hd\n",
			 pt_num, series_num); break;
	case 7 : fprintf (stderr, "TEXT is Z axis title\n"); break;
	default :
		 fprintf (stderr, "ERROR : TEXT is linked to undocumented object\n");
	};});
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
	float initial_angle = GSF_LE_GET_GUINT16 (q->data);
	float center_size = GSF_LE_GET_GUINT16 (q->data+2); /* 0-100 */
	guint16 const flags = GSF_LE_GET_GUINT16 (q->data+4);
	gboolean in_3d = (s->container.ver >= MS_BIFF_V8 && (flags & 0x01));

	g_return_val_if_fail (s->plot == NULL, TRUE);
	s->plot = gog_plot_new_by_name ((center_size == 0) ? "GogPiePlot" : "GogRingPlot");
	g_return_val_if_fail (s->plot != NULL, TRUE);

	g_object_set (G_OBJECT (s->plot),
		"in_3d",		in_3d,
		"initial_angle",	initial_angle,
		NULL);
	if (center_size != 0)
		g_object_set (G_OBJECT (s->plot),
			"center_size",	((double)center_size) / 100.,
			NULL);

#if 0
	gboolean leader_lines = (s->container.ver >= MS_BIFF_V8 && (flags & 0x02));
#endif

	return FALSE;
}

/****************************************************************************/

static gboolean
BC_R(pieformat)(XLChartHandler const *handle,
		XLChartReadState *s, BiffQuery *q)
{
	unsigned separation = GSF_LE_GET_GUINT16 (q->data); /* 0-500 */

	/* we only support the default right now.  Also, XL sets this for _all_ types
	 * rather than just pies. */
	if (s->style_element >= 0 && s->style != NULL)
		g_object_set_data (G_OBJECT (s->style),
			"pie-separation", GUINT_TO_POINTER (separation));
	else if (s->plot != NULL &&
	    g_object_class_find_property (G_OBJECT_GET_CLASS (s->plot), "default_separation"))
		g_object_set (G_OBJECT (s->plot),
			"default_separation",	((double) separation) / 100.,
			NULL);
	d (2, fprintf (stderr, "Pie slice(s) are %u %% of diam from center\n",
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
	d (2, {
	/* Docs say these are longs
	 * But it appears that only 2 lsb are valid ??
	 */
	gint16 const horiz = GSF_LE_GET_GUINT16 (q->data+2);
	gint16 const vert = GSF_LE_GET_GUINT16 (q->data+6);

	fprintf (stderr, "Scale H=");
	if (horiz != -1)
		fprintf (stderr, "%u", horiz);
	else
		fprintf (stderr, "Unscaled");
	fprintf (stderr, ", V=");
	if (vert != -1)
		fprintf (stderr, "%u", vert);
	else
		fprintf (stderr, "Unscaled");
	});
	return FALSE;
}
/****************************************************************************/

static gboolean
BC_R(pos)(XLChartHandler const *handle,
	  XLChartReadState *s, BiffQuery *q)
{
	return FALSE;
}

/****************************************************************************/

static gboolean
BC_R(radar)(XLChartHandler const *handle,
	    XLChartReadState *s, BiffQuery *q)
{
	g_return_val_if_fail (s->plot == NULL, TRUE);
	s->plot = gog_plot_new_by_name ("GogRadarPlot");

	return FALSE;
}

/****************************************************************************/

static gboolean
BC_R(radararea)(XLChartHandler const *handle,
		XLChartReadState *s, BiffQuery *q)
{
	g_return_val_if_fail (s->plot == NULL, TRUE);
	s->plot = gog_plot_new_by_name ("GogRadarPlot");
	g_object_set (G_OBJECT (s->plot),
		"area",		TRUE,
		NULL);

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

	if (s->container.ver >= MS_BIFF_V8) {
		guint16 const flags = GSF_LE_GET_GUINT16 (q->data+4);

		/* Has bubbles */
		if (flags & 0x01) {
			guint16 const size_type = GSF_LE_GET_GUINT16 (q->data+2);
			gboolean in_3d = (flags & 0x04) != 0;
			gboolean show_negatives = (flags & 0x02) != 0;
			gboolean size_as_area = (size_type != 2);
			float scale =  GSF_LE_GET_GUINT16 (q->data) / 100.;
			s->plot = gog_plot_new_by_name ("GogBubblePlot");
			g_return_val_if_fail (s->plot != NULL, TRUE);
			g_object_set (G_OBJECT (s->plot),
				"in_3d",		in_3d, 
				"show_negatives",	show_negatives,
				"size_as_area", 	size_as_area,
				"bubble_scale",	scale,
				NULL);
			d(1, fprintf (stderr, "bubbles;"););
			return FALSE;
		}
	}

	s->plot = gog_plot_new_by_name ("GogXYPlot");
	g_return_val_if_fail (s->plot != NULL, TRUE);

	d(1, fprintf (stderr, "scatter;"););
	return FALSE;
}

/****************************************************************************/

static gboolean
BC_R(serauxerrbar)(XLChartHandler const *handle,
		   XLChartReadState *s, BiffQuery *q)
{
	return FALSE;
}

/****************************************************************************/

static gboolean
BC_R(serfmt)(XLChartHandler const *handle,
	     XLChartReadState *s, BiffQuery *q)
{
	return FALSE;
}

/****************************************************************************/

static void
BC_R(vector_details)(XLChartReadState *s, BiffQuery *q, XLChartSeries *series,
		     GogMSDimType purpose,
		     int type_offset, int count_offset, char const *name)
{
#if 0
	switch (GSF_LE_GET_GUINT16 (q->data + type_offset)) {
	case 0 : /* date */ break;
	case 1 : /* value */ break;
	case 2 : /* sequences */ break;
	case 3 : /* string */ break;
	}
#endif

	series->data [purpose].num_elements = GSF_LE_GET_GUINT16 (q->data+count_offset);
	d (0, fprintf (stderr, "%s has %d elements\n",
		       name, series->data [purpose].num_elements););
}


static gboolean
BC_R(series)(XLChartHandler const *handle,
	     XLChartReadState *s, BiffQuery *q)
{
	XLChartSeries *series;

	g_return_val_if_fail (s->currentSeries == NULL, TRUE);

	d (2, fprintf (stderr, "SERIES = %d\n", s->series->len););

	series = excel_chart_series_new ();

	/* WARNING : The offsets in the documentation are WRONG.
	 *           Use the sizes instead.
	 */
	BC_R(vector_details) (s, q, series, GOG_MS_DIM_CATEGORIES,
			      0, 4, "Categories");
	BC_R(vector_details) (s, q, series, GOG_MS_DIM_VALUES,
			      2, 6, "Values");
	if (s->container.ver >= MS_BIFF_V8)
		BC_R(vector_details) (s, q, series, GOG_MS_DIM_VALUES,
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
	guint16 const id = GSF_LE_GET_GUINT16 (q->data);	/* must be 0 */
	int const slen = GSF_LE_GET_GUINT8 (q->data + 2);
	char *str;

	g_return_val_if_fail (id == 0, FALSE);

	if (slen == 0)
		return FALSE;

	str = biff_get_text (q->data + 3, slen, NULL, s->container.ver);
	d (2, fprintf (stderr, "'%s';\n", str););

	/* A quick heuristic */
	if (s->currentSeries != NULL &&
	    s->currentSeries->data [GOG_MS_DIM_LABELS].data == NULL) {
		s->currentSeries->data [GOG_MS_DIM_LABELS].data =
			go_data_scalar_str_new (str, TRUE);
	} else if (BC_R(top_state) (s) == BIFF_CHART_text) {
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
	return FALSE;
}

/****************************************************************************/

static gboolean
BC_R(sertocrt)(XLChartHandler const *handle,
	       XLChartReadState *s, BiffQuery *q)
{
	guint16 const index = GSF_LE_GET_GUINT16 (q->data);

	g_return_val_if_fail (s->currentSeries != NULL, FALSE);

	s->currentSeries->chart_group = index;

	d (1, fprintf (stderr, "Series chart group index is %hd\n", index););
	return FALSE;
}

/****************************************************************************/

typedef enum {
	MS_CHART_BLANK_SKIP		= 0,
	MS_CHART_BLANK_ZERO		= 1,
	MS_CHART_BLANK_INTERPOLATE	= 2,
	MS_CHART_BLANK_MAX		= 3
} MS_CHART_BLANK;
static char const *const ms_chart_blank[] = {
	"Skip blanks", "Blanks are zero", "Interpolate blanks"
};

static gboolean
BC_R(shtprops)(XLChartHandler const *handle,
	       XLChartReadState *s, BiffQuery *q)
{
	guint16 const flags = GSF_LE_GET_GUINT16 (q->data);
	guint8 const tmp = GSF_LE_GET_GUINT16 (q->data+2);
	gboolean const manual_format		= (flags&0x01) ? TRUE : FALSE;
	gboolean const only_plot_visible_cells	= (flags&0x02) ? TRUE : FALSE;
	gboolean const dont_size_with_window	= (flags&0x04) ? TRUE : FALSE;
	gboolean const has_pos_record		= (flags&0x08) ? TRUE : FALSE;
	gboolean ignore_pos_record = FALSE;
	MS_CHART_BLANK blanks;

	g_return_val_if_fail (tmp < MS_CHART_BLANK_MAX, TRUE);
	blanks = tmp;
	d (2, fprintf (stderr, "%s;", ms_chart_blank[blanks]););

	if (s->container.ver >= MS_BIFF_V8)
		ignore_pos_record = (flags&0x10) ? TRUE : FALSE;

	d (1, {
	fprintf (stderr, "%sesize chart with window.\n",
		dont_size_with_window ? "Don't r": "R");

	if (has_pos_record && !ignore_pos_record)
		fprintf (stderr, "There should be a POS record around here soon\n");

	if (manual_format)
		fprintf (stderr, "Manually formated\n");
	if (only_plot_visible_cells)
		fprintf (stderr, "Only plot visible (to whom?) cells\n");
	});
	return FALSE;
}

/****************************************************************************/

static gboolean
BC_R(siindex)(XLChartHandler const *handle,
	      XLChartReadState *s, BiffQuery *q)
{
	d (1, {
	/* UNDOCUMENTED : Docs says this is long
	 * Biff record is only length 2
	 */
	gint16 const index = GSF_LE_GET_GUINT16 (q->data);
	fprintf (stderr, "Series %d is %hd\n", s->series->len, index);});
	return FALSE;
}
/****************************************************************************/

static gboolean
BC_R(surf)(XLChartHandler const *handle,
	   XLChartReadState *s, BiffQuery *q)
{
	return FALSE;
}

/****************************************************************************/

static gboolean
BC_R(text)(XLChartHandler const *handle,
	   XLChartReadState *s, BiffQuery *q)
{
	if (s->prev_opcode == BIFF_CHART_defaulttext) {
		d (4, fputs ("Text follows defaulttext;\n", stderr););
	} else {
	}
	BC_R(get_style) (s);
	s->style->font.color = BC_R(color) (q->data+0, "Font");

#if 0
case BIFF_CHART_chart :
	fputs ("Text follows chart;\n", stderr);
	break;
case BIFF_CHART_legend :
	fputs ("Text follows legend;\n", stderr);
	break;
default :
	fprintf (stderr, "BIFF ERROR : A Text record follows a %x\n",
		s->prev_opcode);

}
#endif
return FALSE;
}

/****************************************************************************/

static gboolean
BC_R(tick)(XLChartHandler const *handle,
	   XLChartReadState *s, BiffQuery *q)
{
	guint16 const major = GSF_LE_GET_GUINT8 (q->data);
	guint16 const minor = GSF_LE_GET_GUINT8 (q->data+1);
	guint16 const label = GSF_LE_GET_GUINT8 (q->data+2);

	if (s->axis != NULL)
		g_object_set (G_OBJECT (s->axis),
			/* cheat until we support different label pos */
			"major-tick-labeled",	(label != 0),
			"major-tick-in",	((major & 1) ? TRUE : FALSE),
			"major-tick-out",	((major >= 2) ? TRUE : FALSE),
			"minor-tick-in",	((minor & 1) ? TRUE : FALSE),
			"minor-tick-out",	((minor >= 2) ? TRUE : FALSE),
			NULL);
	d (1, {
	guint16 const flags = GSF_LE_GET_GUINT8 (q->data+24);

	switch (major) {
	case 0: fputs ("no major tick;\n", stderr); break;
	case 1: fputs ("major tick inside axis;\n", stderr); break;
	case 2: fputs ("major tick outside axis;\n", stderr); break;
	case 3: fputs ("major tick across axis;\n", stderr); break;
	default : fputs ("unknown major tick type;\n", stderr);
	}
	switch (minor) {
	case 0: fputs ("no minor tick;\n", stderr); break;
	case 1: fputs ("minor tick inside axis;\n", stderr); break;
	case 2: fputs ("minor tick outside axis;\n", stderr); break;
	case 3: fputs ("minor tick across axis;\n", stderr); break;
	default : fputs ("unknown minor tick type;\n", stderr);
	}
	switch (label) {
	case 0: fputs ("no tick label;\n", stderr); break;
	case 1: fputs ("tick label at low end (NOTE mapped to near axis);\n", stderr); break;
	case 2: fputs ("tick label at high end (NOTE mapped to near axis);\n", stderr); break;
	case 3: fputs ("tick label near axis;\n", stderr); break;
	default : fputs ("unknown tick label position;\n", stderr);
	}

	/*
	if (flags&0x01)
		fputs ("Auto tick label colour", stderr);
	else
		BC_R(color) (q->data+4, "LabelColour", tick, FALSE);
	*/

	if (flags&0x02)
		fputs ("Auto text background mode", stderr);
	else
		fprintf (stderr, "background mode = %d\n", (unsigned)GSF_LE_GET_GUINT8 (q->data+3));

	switch (flags&0x1c) {
	case 0: fputs ("no rotation;\n", stderr); break;
	case 1: fputs ("top to bottom letters upright;\n", stderr); break;
	case 2: fputs ("rotate 90deg counter-clockwise;\n", stderr); break;
	case 3: fputs ("rotate 90deg clockwise;\n", stderr); break;
	default : fputs ("unknown rotation;\n", stderr);
	}

	if (flags&0x20)
		fputs ("Auto rotate;\n", stderr);
	});

	return FALSE;
}

/****************************************************************************/

static gboolean
BC_R(units)(XLChartHandler const *handle,
	    XLChartReadState *s, BiffQuery *q)
{
	/* Irrelevant */
	guint16 const type = GSF_LE_GET_GUINT16 (q->data);
	g_return_val_if_fail(type == 0, TRUE);

	return FALSE;
}
/****************************************************************************/

static void
xl_axis_get_elem (GogObject *axis, unsigned dim, gchar const *name,
		  gboolean flag, guint8 const *data)
{
	GOData *dat;
	if (flag) {
		dat = NULL;
		d (1, fprintf (stderr, "%s = Auto\n", name););
	} else {
		double const val = gsf_le_get_double (data);
		gog_dataset_set_dim (GOG_DATASET (axis), dim,
			go_data_scalar_val_new (val), NULL);
		d (1, fprintf (stderr, "%s = %f\n", name, val););
	}
}

static gboolean
BC_R(valuerange)(XLChartHandler const *handle,
		 XLChartReadState *s, BiffQuery *q)
{
	guint16 const flags = GSF_LE_GET_GUINT16 (q->data+40);

	xl_axis_get_elem (s->axis, AXIS_ELEM_MIN,	  "Min Value",		flags&0x01, q->data+ 0);
	xl_axis_get_elem (s->axis, AXIS_ELEM_MAX,	  "Max Value",		flags&0x02, q->data+ 8);
	xl_axis_get_elem (s->axis, AXIS_ELEM_MAJOR_TICK,  "Major Increment",	flags&0x04, q->data+16);
	xl_axis_get_elem (s->axis, AXIS_ELEM_MINOR_TICK,  "Minor Increment",	flags&0x08, q->data+24);
	xl_axis_get_elem (s->axis, AXIS_ELEM_CROSS_POINT, "Cross over point",	flags&0x10, q->data+32);

#if 0
	if (flags & 0x20) {
		g_object_set (s->axis, "log-scale", TRUE, NULL);
		d (1, fputs ("Log scaled;\n", stderr););
	}
#endif
	if (flags & 0x40) {
		g_object_set (s->axis, "invert-axis", TRUE, NULL);
		d (1, fputs ("Values in reverse order;\n", stderr););
	}
	if (flags & 0x80) {
		g_object_set (s->axis, "pos_str", "high", NULL);
		d (1, fputs ("Cross over at max value;\n", stderr););
	}

	return FALSE;
}

/****************************************************************************/

static int
XL_gog_series_map_dim (GogSeries const *series, GogMSDimType ms_type)
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

static void
XL_gog_series_set_dim (GogSeries *series, GogMSDimType ms_type, GOData *val)
{
	int dim = XL_gog_series_map_dim (series, ms_type);
	if (dim >= -1) {
		gog_series_set_dim (series, dim, val, NULL);
		return;
	}
	g_object_unref (val);
}

static void
cb_store_singletons (gpointer indx, GogStyle *style, GogObject *series)
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

static gboolean
BC_R(end)(XLChartHandler const *handle,
	  XLChartReadState *s, BiffQuery *q)
{
	int popped_state;

	d (0, fputs ("}\n", stderr););

	g_return_val_if_fail (s->stack != NULL, TRUE);
	g_return_val_if_fail (s->stack->len > 0, TRUE);

	popped_state = BC_R(top_state) (s);
	s->stack = g_array_remove_index_fast (s->stack, s->stack->len-1);

	switch (popped_state) {
	case BIFF_CHART_axisparent :
		break;
	case BIFF_CHART_axis :
		s->axis = NULL;
		break;

	case BIFF_CHART_frame :
		if (s->style != NULL) {
			int top_state = BC_R(top_state) (s);
			GogObject *obj = NULL;
			if (top_state == BIFF_CHART_legend)
				obj = s->legend;
			else if (top_state == BIFF_CHART_chart)
				obj = GOG_OBJECT (s->chart);
			else if (s->frame_for_grid) {
				GogGrid *tmp = gog_chart_get_grid (s->chart);
				obj = (tmp == NULL)
					? gog_object_add_by_name (GOG_OBJECT (s->chart), "Grid", NULL)
					: GOG_OBJECT (tmp);
			}
			if (obj != NULL)
				g_object_set (G_OBJECT (obj),
					"style", s->style,
					NULL);
			g_object_unref (s->style);
			s->style = NULL;
		}
		break;

	case BIFF_CHART_series :
		g_return_val_if_fail (s->currentSeries != NULL, TRUE);
		s->currentSeries = NULL;
		break;

	case BIFF_CHART_chartformat : {
		unsigned i, j;
		XLChartSeries *eseries;
		GogSeries     *series;
		GogStyle      *style;

		g_return_val_if_fail (s->plot != NULL, TRUE);

		/* Add _before_ setting styles so theme does not override */
		gog_object_add_by_name (GOG_OBJECT (s->chart),
			"Plot", GOG_OBJECT (s->plot));

		if (s->default_plot_style != NULL) {
			char const *type = G_OBJECT_TYPE_NAME (s->plot);
			GogStyle const *style = s->default_plot_style;
			
			if (type != NULL && style->marker.mark != NULL &&
			    (!strcmp (type, "GogXYPlot") ||
			     !strcmp (type, "GogLinePlot") ||
			     !strcmp (type, "GogRadarPlot")))
				g_object_set (G_OBJECT (s->plot),
					"default-style-has-markers",
					style->marker.mark->shape != GO_MARKER_NONE,
					NULL);
			if (type != NULL && 0 == strcmp (type, "GogXYPlot"))
				g_object_set (G_OBJECT (s->plot),
					"default-style-has-lines", style->line.width >= 0,
					NULL);

			g_object_unref (s->default_plot_style);
			s->default_plot_style = NULL;
		}

		for (i = 0 ; i < s->series->len; i++ ) {
			eseries = g_ptr_array_index (s->series, i);
			if (eseries->chart_group != s->plot_counter)
				continue;
			series = gog_plot_new_series (s->plot);
			for (j = 0 ; j < GOG_MS_DIM_TYPES; j++ )
				if (eseries->data [j].data != NULL) {
					XL_gog_series_set_dim (series, j,
						eseries->data [j].data);
					eseries->data [j].data = NULL;
				}
			style = eseries->style;
			if (style != NULL)
				g_object_set (G_OBJECT (series),
					"style", style,
					NULL);
			if (!eseries->has_legend)
				g_object_set (G_OBJECT (series),
					"has-legend", FALSE,
					NULL);
			if (eseries->singletons != NULL)
				g_hash_table_foreach (eseries->singletons,
					(GHFunc) cb_store_singletons, series);
		}

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
				GogStyle *x_style, *y_style;
				int i;
				for (i = 0 ; i < AXIS_ELEM_MAX_ENTRY ; i++)
					xl_axis_swap_elem (x, y, i);
				g_object_get (G_OBJECT (x), "style", &x_style, NULL);
				g_object_get (G_OBJECT (y), "style", &y_style, NULL);
				g_object_set (G_OBJECT (y), "style", x_style, NULL);
				g_object_set (G_OBJECT (x), "style", y_style, NULL);
				g_object_unref (x_style);
				g_object_unref (y_style);
			}
		}
		s->plot = NULL;
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
			}
		} else if (s->plot != NULL) {
			g_return_val_if_fail (s->default_plot_style == NULL, TRUE);
			s->default_plot_style = s->style;
		} else
			g_object_unref (s->style);
		s->style = NULL;
		break;

	case BIFF_CHART_text :
#warning Do something with the style
		if (s->text != NULL) {
			g_free (s->text);
			s->text = NULL;
		}
		if (s->style != NULL) {
			g_object_unref (s->style);
			s->style = NULL;
		}
		break;

	default :
		break;
	}
	return FALSE;
}

/****************************************************************************/

static gboolean
BC_R(serauxtrend)(XLChartHandler const *handle,
		  XLChartReadState *s, BiffQuery *q)
{
	return FALSE;
}

/****************************************************************************/

static XLChartHandler const *chart_biff_handler[128];

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
	BIFF_CHART(siindex, 4);
	BIFF_CHART(surf, 2);
	BIFF_CHART(text, 26);
	BIFF_CHART(tick, 26);
	BIFF_CHART(units, 2);
	BIFF_CHART(valuerange, 42);
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
	unsigned const num_handler = sizeof(chart_biff_handler) /
		sizeof(XLChartHandler *);

	guint32 num = handle->opcode & 0xff;

	if (num >= num_handler)
		fprintf (stderr, "Invalid BIFF_CHART handler (%x)\n", handle->opcode);
	else if (chart_biff_handler[num])
		fprintf (stderr, "Multiple BIFF_CHART handlers for (%x)\n",
			handle->opcode);
	else
		chart_biff_handler[num] = handle;
}

static gboolean
chart_realize_obj (MSContainer *container, MSObj *obj)
{
	return FALSE;
}

static SheetObject *
chart_create_obj  (MSContainer *container, MSObj *obj)
{
	return NULL;
}

static GnmExpr const *
chart_parse_expr  (MSContainer *container, guint8 const *data, int length)
{
	return excel_parse_formula (container, NULL, 0, 0,
				    data, length, FALSE, NULL);
}

static Sheet *
chart_get_sheet (MSContainer const *container)
{
	return ms_container_sheet (container->parent);
}

gboolean
ms_excel_chart_read (BiffQuery *q, MSContainer *container, MsBiffVersion ver,
		     SheetObject *sog, Sheet *full_page)
{
	static MSContainerClass const vtbl = {
		chart_realize_obj,
		chart_create_obj,
		chart_parse_expr,
		chart_get_sheet,
		NULL
	};
	int const num_handler = sizeof(chart_biff_handler) /
		sizeof(XLChartHandler *);

	int i;
	gboolean done = FALSE;
	XLChartReadState state;

	/* Register the handlers if this is the 1st time through */
	BC(register_handlers)();

	/* FIXME : create an anchor parser for charts */
	ms_container_init (&state.container, &vtbl, container,
			   container->ewb, container->ver);

	state.stack	    = g_array_new (FALSE, FALSE, sizeof(int));
	state.prev_opcode   = 0xdeadbeef; /* Invalid */
	state.currentSeries = NULL;
	state.series	    = g_ptr_array_new ();
	state.plot_counter  = -1;
	state.has_a_grid    = FALSE;
	state.text	    = NULL;

	if (NULL != (state.sog = sog)) {
		GogStyle *style = gog_style_new ();
		style->outline.width = 0;
		style->outline.dash_type = GO_LINE_NONE;
		style->fill.type = GOG_FILL_STYLE_NONE;

		state.graph = sheet_object_graph_get_gog (sog);
		state.chart = GOG_CHART (gog_object_add_by_name (GOG_OBJECT (state.graph), "Chart", NULL));

		g_object_set (G_OBJECT (state.graph), "style", style, NULL);
		g_object_set (G_OBJECT (state.chart), "style", style, NULL);
		g_object_unref (style);
	} else {
		state.graph = NULL;
		state.chart = NULL;
	}
	state.default_plot_style = NULL;
	state.plot  = NULL;
	state.axis  = NULL;
	state.style = NULL;

	d (0, fputs ("{ /* CHART */\n", stderr););

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
				d (0, {	fprintf (stderr, "Unknown BIFF_CHART record\n");
					ms_biff_query_dump (q);});
			} else {
				XLChartHandler const *const h =
					chart_biff_handler [lsb];

				if (state.graph	!= NULL) {
					d (0, { if (!begin_end)
							fprintf (stderr, "%s(\n", h->name); });
					(void)(*h->read_fn)(h, &state, q);
					d (0, { if (!begin_end)
							fprintf (stderr, ");\n"); });
				}
			}
		} else switch (q->opcode) {
		case BIFF_EOF:
			done = TRUE;
			d (0, fputs ("}; /* CHART */\n", stderr););
			g_return_val_if_fail(state.stack->len == 0, TRUE);
			break;

		case BIFF_PROTECT : {
			gboolean const is_protected =
				(1 == GSF_LE_GET_GUINT16 (q->data));
			d (4, fprintf (stderr, "Chart is%s protected;\n",
				     is_protected ? "" : " not"););
			break;
		}

		case BIFF_BLANK_v0:
		case BIFF_BLANK_v2: /* Stores a missing value in the inline value tables */
			break;
		case BIFF_NUMBER_v0:
		case BIFF_NUMBER_v2: {
			unsigned offset = (q->opcode == BIFF_NUMBER_v2) ? 6: 7;
			double val = gsf_le_get_double (q->data + offset);
			/* Figure out how to assign these back to the series,
			 * are they just sequential ?  */
			d (10, fprintf (stderr, "%f\n", val););
			break;
		}

		case BIFF_LABEL_v0 : break; /* ignore for now */
		case BIFF_LABEL_v2 : {
			guint16 row = GSF_LE_GET_GUINT16 (q->data + 0);
			guint16 col = GSF_LE_GET_GUINT16 (q->data + 2);
			guint16 xf  = GSF_LE_GET_GUINT16 (q->data + 4);
			guint16 len = GSF_LE_GET_GUINT16 (q->data + 6);
			char *label = biff_get_text (q->data + 8, len, NULL, ver);
			d (10, {fprintf (stderr, "'%s'\n;hmm, what are these values for a chart ???\n"
					"row = %d, col = %d, xf = %d\n", label, row, col, xf);});
			g_free (label);
			break;
		}

		case BIFF_MS_O_DRAWING:
			ms_escher_parse (q, &state.container, FALSE);
			break;

		case BIFF_EXTERNCOUNT: /* ignore */ break;
		case BIFF_EXTERNSHEET: /* These cannot be biff8 */
			excel_read_EXTERNSHEET_v7 (q, &state.container);
			break;

		case BIFF_WINDOW2_v0 :
		case BIFF_WINDOW2_v2 :
			if (full_page != NULL && container->ver > MS_BIFF_V2)
				if (GSF_LE_GET_GUINT16 (q->data + 0) & 0x0400)
					wb_view_sheet_focus (container->ewb->wbv, full_page);
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
			d (8, fprintf (stderr, "Handled biff %x in chart;\n",
				     q->opcode););
			break;

		case BIFF_PRINTSIZE: {
#if 0
			/* Undocumented, seems like an enum ??? */
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
	if (state.chart != NULL && !state.has_a_grid) {
		GogGrid *grid = gog_chart_get_grid (state.chart);
		if (grid != NULL) {
			gog_object_clear_parent (GOG_OBJECT (grid));
			g_object_unref (grid);
		}
	}

	/* Cleanup */
	for (i = state.series->len; i-- > 0 ; ) {
		XLChartSeries *series = g_ptr_array_index (state.series, i);
		if (series != NULL)
			excel_chart_series_delete (series);
	}
	g_array_free (state.stack, TRUE);
	g_ptr_array_free (state.series, TRUE);
	ms_container_finalize (&state.container);

	if (full_page != NULL) {
		static GnmRange const fixed_size = { { 1, 1 }, { 12, 32 } };
		SheetObjectAnchor anchor;
		sheet_object_anchor_init (&anchor,
			&fixed_size, NULL, NULL, SO_DIR_DOWN_RIGHT);
		sheet_object_set_anchor (sog, &anchor);
		sheet_object_set_sheet (sog, full_page);
		g_object_unref (sog);
	}

	return FALSE;
}

/* A wrapper which reads and checks the BOF record then calls ms_excel_chart_read */
/**
 * ms_excel_chart_read_BOF :
 * @q : #BiffQuery
 * @container : #MSContainer
 * @sog : #SheetObjectGraph
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
	res = ms_excel_chart_read (q, container, container->ver, sog, NULL);

	ms_biff_bof_data_destroy (bof);
	return res;
}

/***************************************************************************/

typedef struct {
	BiffPut		*bp;
	ExcelWriteState *ewb;
	SheetObject	*so;
	GogGraph const	*graph;
	GogObject const	*chart;
	GogView const	*root_view;

	unsigned	 nest_level;
} XLChartWriteState;

typedef struct {
	GogAxis *axis [GOG_AXIS_TYPES];
	gboolean transpose, center_ticks;
	GSList  *plots;
} XLAxisSet;

static gint
cb_axis_set_cmp (XLAxisSet const *a, XLAxisSet const *b)
{
	int i;
	if (!a->transpose == !b->transpose)
		return FALSE;
	for (i = GOG_AXIS_X; i < GOG_AXIS_TYPES; i++)
		if (a->axis[i] != b->axis[i])
			return FALSE;
	return TRUE;
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
	abgr  = UINT_RGBA_R(c);
	abgr |= UINT_RGBA_G(c) << 8;
	abgr |= UINT_RGBA_B(c) << 16;
	GSF_LE_SET_GUINT32 (data, abgr);

	return palette_get_index (s->ewb, abgr & 0xffffff);
}

static void
chart_write_AREAFORMAT (XLChartWriteState *s, GogStyle const *style, gboolean disable_auto)
{
	guint8 *data = ms_biff_put_len_next (s->bp, BIFF_CHART_areaformat,
		(s->bp->version >= MS_BIFF_V8) ? 16: 12);
	guint16 fore_index, back_index, pat, flags = 0;
	GOColor fore, back;

	if (style != NULL) {
		switch (style->fill.type) {
		default :
			g_warning ("invalid fill type, saving as none");
		case GOG_FILL_STYLE_IMAGE:
#warning export images
		case GOG_FILL_STYLE_NONE:
			pat = 0;
			fore = RGBA_WHITE;
			back = RGBA_WHITE;
			break;
		case GOG_FILL_STYLE_PATTERN: {
			pat = style->fill.pattern.pattern + 1;
			if (pat == 1) {
				back = style->fill.pattern.fore;
				fore = style->fill.pattern.back;
			} else {
				fore = style->fill.pattern.fore;
				back = style->fill.pattern.back;
			}
			break;
		}
		case GOG_FILL_STYLE_GRADIENT:
			pat = 1;
			fore = back = style->fill.pattern.fore;
#warning export gradients
			break;
		}

		if (style->fill.auto_back && !disable_auto)
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
chart_write_LINEFORMAT (XLChartWriteState *s, GogStyleLine const *lstyle,
			gboolean draw_ticks, gboolean clear_lines_for_null)
{
	guint8 *data = ms_biff_put_len_next (s->bp, BIFF_CHART_lineformat,
		(s->bp->version >= MS_BIFF_V8) ? 12: 10);
	guint16 w, color_index, pat, flags = 0;

	if (lstyle != NULL) {
		color_index = chart_write_color (s, data, lstyle->color);
		pat = lstyle->pattern;
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
		if (lstyle->auto_color)
			flags |= 9; 	/* docs only mention 1, but there is an 8 in there too */
	} else {
		color_index = chart_write_color (s, data, 0);
		if (clear_lines_for_null) {
			pat = 5;
			flags = 8; 	/* docs only mention 1, but there is an 8 in there too */
		} else {
			pat = 0;
			flags = 9; 	/* docs only mention 1, but there is an 8 in there too */
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
chart_write_MARKERFORMAT (XLChartWriteState *s, GogStyle const *style,
			  gboolean clear_marks_for_null)
{
	guint8 *data = ms_biff_put_len_next (s->bp, BIFF_CHART_markerformat,
		(s->bp->version >= MS_BIFF_V8) ? 20: 12);
	guint16 fore_index, back_index, shape, flags = 0;
	guint32 size;
	GOColor fore, back;

	if (style != NULL) {
		fore = go_marker_get_outline_color (style->marker.mark);
		back = go_marker_get_fill_color	(style->marker.mark);
		shape = go_marker_get_shape (style->marker.mark); /* TODO : map */
		size = go_marker_get_size (style->marker.mark) * 20;
		if (style->marker.auto_outline_color &&
		    style->marker.auto_fill_color)
			flags |= 1;
		if (fore == 0)
			flags |= 0x10;
		if (back == 0)
			flags |= 0x20;
	} else {
		fore = back = 0;
		if (clear_marks_for_null) {
			shape = flags = 0;
		} else {
			shape = 2;
			flags = 1;
		}
		size = 60;
	}

	fore_index = chart_write_color (s, data+0, fore);
	back_index = chart_write_color (s, data+4, back);
	GSF_LE_SET_GUINT16 (data+8,  shape);
	GSF_LE_SET_GUINT16 (data+10, flags);
	if (s->bp->version >= MS_BIFF_V8) {
		GSF_LE_SET_GUINT16 (data+12, fore_index);
		GSF_LE_SET_GUINT16 (data+14, back_index);
		GSF_LE_SET_GUINT32 (data+16, size);
	}

	ms_biff_put_commit (s->bp);
}
static void
chart_write_PIEFORMAT (XLChartWriteState *s, float separation)
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
	/* double tmp = l / (is_horiz ? s->root_view->allocation.w : s->root_view->allocation.h); */
#warning use _tmp_ here when we get the null view in place
	return (unsigned)(4000. * l + .5);
}

static void
chart_write_position (XLChartWriteState *s, GogObject const *obj, guint8 *data)
{
	GogView *view = gog_view_find_child_view  (s->root_view, obj);
	guint32 tmp;

	g_return_if_fail (view != NULL);

	tmp = map_length (s, view->allocation.x, TRUE);
	GSF_LE_SET_GUINT32 (data + 0, tmp);
	tmp = map_length (s, view->allocation.y, FALSE);
	GSF_LE_SET_GUINT32 (data + 4, tmp);
	tmp = map_length (s, .9 /* view->allocation.w */, TRUE);
	GSF_LE_SET_GUINT32 (data + 8, tmp);
	tmp = map_length (s, .9 /* view->allocation.h */, FALSE);
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
chart_write_AI (XLChartWriteState *s, GOData const *dim, unsigned n,
		guint8 ref_type)
{
	guint8 buf[8], lendat[2];
	unsigned len;

	if (dim != NULL) {
		GType const t = G_OBJECT_TYPE (dim);
		if (t == GNM_GO_DATA_SCALAR_TYPE ||
		    t == GNM_GO_DATA_VECTOR_TYPE)
			ref_type = 2;
	}
	ms_biff_put_var_next (s->bp, BIFF_CHART_ai);
	GSF_LE_SET_GUINT8  (buf+0, n);
	GSF_LE_SET_GUINT8  (buf+1, ref_type);

	/* no custom number format support for a dimension yet */
	GSF_LE_SET_GUINT16 (buf+2, 0);
	GSF_LE_SET_GUINT16 (buf+4, 0);

	GSF_LE_SET_GUINT16 (buf+6, 0); /* placeholder length */
	ms_biff_put_var_write (s->bp, buf, 8);

	if (ref_type == 2) {
		len = excel_write_formula (s->ewb,
			gnm_go_data_get_expr (dim),
			gnm_go_data_get_sheet (dim),
			0, 0, EXCEL_CALLED_FROM_NAME);
		ms_biff_put_var_seekto (s->bp, 6);
		GSF_LE_SET_GUINT16 (lendat, len);
		ms_biff_put_var_write (s->bp, lendat, 2);
	}

	ms_biff_put_commit (s->bp);
}

static void
chart_write_text (XLChartWriteState *s, GOData const *src, GogStyle const *style)
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
		0, 0 		/* rotation */
	};
	guint8 *data;
	guint16 color_index = 0x4d;
	unsigned const len = (s->bp->version >= MS_BIFF_V8) ? 32: 26;
	
	/* TEXT */
	data = ms_biff_put_len_next (s->bp, BIFF_CHART_text, len);
	memcpy (data, default_text, len);
	/* chart_write_position (s, NULL, data+8); */
	if (style != NULL)
		color_index = chart_write_color (s, data+4, style->font.color);
	if (s->bp->version >= MS_BIFF_V8) {
		GSF_LE_SET_GUINT16 (data+26, color_index);
	}
	ms_biff_put_commit (s->bp);

	chart_write_BEGIN (s);

	/* BIFF_CHART_pos, optional we use auto positioning */
#warning get the right font
	ms_biff_put_2byte (s->bp, BIFF_CHART_fontx, 5);
	chart_write_AI (s, src, 0, 1);
	chart_write_END (s);
}

static void
store_dim (GogSeries const *series, GogMSDimType t,
	   guint8 *store_type, guint8 *store_count, guint16 default_count)
{
	int msdim = XL_gog_series_map_dim (series, t);
	GOData *dat = NULL;
	guint16 count, type;

	if (msdim >= -1)
		dat = gog_dataset_get_dim (GOG_DATASET (series), msdim);
	if (dat == NULL) {
		count = default_count;
		type = 1; /* numeric */
	} else if (IS_GO_DATA_SCALAR (dat)) {
		/* cheesy test to see if the content is strings or numbers */
		double tmp = go_data_scalar_get_value (GO_DATA_SCALAR (dat));
		type = finitegnum (tmp) ? 1 : 3;
		count = 1;
	} else if (IS_GO_DATA_VECTOR (dat)) {
		/* cheesy test to see if the content is strings or numbers */
		double tmp = go_data_vector_get_value (GO_DATA_VECTOR (dat), 0);
		type = finitegnum (tmp) ? 1 : 3;
		count = go_data_vector_get_len (GO_DATA_VECTOR (dat));
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
style_is_completely_auto (GogStyle const *style)
{
	if ((style->interesting_fields & GOG_STYLE_OUTLINE) &&
	    !style->outline.auto_color)
		return FALSE;
	if ((style->interesting_fields & GOG_STYLE_FILL)) {
		if (style->fill.type != GOG_FILL_STYLE_PATTERN ||
		    !style->fill.auto_back)
			return FALSE;
	}
	if ((style->interesting_fields & GOG_STYLE_LINE) &&
	    !style->line.auto_color)
		return FALSE;
	if ((style->interesting_fields & GOG_STYLE_MARKER)) {
		if (!style->marker.auto_shape ||
		    !style->marker.auto_outline_color ||
		    !style->marker.auto_fill_color)
			return FALSE;
	}
	return TRUE;
}

static void
chart_write_style (XLChartWriteState *s, GogStyle const *style,
		   guint16 indx, unsigned n, float separation)
{
	chart_write_DATAFORMAT (s, indx, n, n);
	chart_write_BEGIN (s);
	ms_biff_put_2byte (s->bp, BIFF_CHART_3dbarshape, 0); /* box */
	if (!style_is_completely_auto (style)) {
		if ((style->interesting_fields & GOG_STYLE_LINE))
			chart_write_LINEFORMAT (s, &style->line, FALSE, FALSE);
		else
			chart_write_LINEFORMAT (s, &style->outline, FALSE, FALSE);
		chart_write_AREAFORMAT (s, style, FALSE);
		chart_write_PIEFORMAT (s, separation);
		chart_write_MARKERFORMAT (s, style, FALSE);
	}
	chart_write_END (s);
}

static void
chart_write_series (XLChartWriteState *s, GogSeries const *series, unsigned n)
{
	static guint8 const default_ref_type[] = { 1, 2, 0, 1 };
	int i, msdim;
	guint8 *data;
	GOData *dat;
	unsigned num_elements = gog_series_num_elements (series);
	GList const *ptr;

	/* SERIES */
	data = ms_biff_put_len_next (s->bp, BIFF_CHART_series,
		(s->bp->version >= MS_BIFF_V8) ? 12: 8);
	store_dim (series, GOG_MS_DIM_CATEGORIES, data+0, data+4, num_elements);
	store_dim (series, GOG_MS_DIM_VALUES, data+2, data+6, num_elements);
	if (s->bp->version >= MS_BIFF_V8) {
		msdim = XL_gog_series_map_dim (series, GOG_MS_DIM_BUBBLES);
		store_dim (series, GOG_MS_DIM_BUBBLES, data+8, data+10,
			   (msdim >= 0) ? num_elements : 0);
	}
	ms_biff_put_commit (s->bp);

	chart_write_BEGIN (s);
	for (i = GOG_MS_DIM_LABELS; i <= GOG_MS_DIM_BUBBLES; i++) {
		msdim = XL_gog_series_map_dim (series, i);
		if (msdim >= -1)
			dat = gog_dataset_get_dim (GOG_DATASET (series),
				XL_gog_series_map_dim (series, i));
		else
			dat = NULL;
		chart_write_AI (s, dat, i, default_ref_type[i]);
	}

	chart_write_style (s, GOG_STYLED_OBJECT (series)->style, 0xffff, n, 0.);
	for (ptr = gog_series_get_overrides (series); ptr != NULL ; ptr = ptr->next) {
		float sep = 0;
		if (g_object_class_find_property (
			G_OBJECT_GET_CLASS (ptr->data), "separation"))
			g_object_get (G_OBJECT (ptr->data), "separation", &sep, NULL);

		chart_write_style (s, GOG_STYLED_OBJECT (ptr->data)->style,
			GOG_SERIES_ELEMENT (ptr->data)->index, n, sep);
	}

	ms_biff_put_2byte (s->bp, BIFF_CHART_sertocrt, 0);
	chart_write_END (s);
}

static void
chart_write_dummy_style (XLChartWriteState *s, float default_separation,
			 gboolean clear_marks, gboolean clear_lines)
{
	chart_write_DATAFORMAT (s, 0, 0, 0xfffd);
	chart_write_BEGIN (s);
	ms_biff_put_2byte (s->bp, BIFF_CHART_3dbarshape, 0); /* box */
	chart_write_LINEFORMAT (s, NULL, FALSE, clear_lines);
	chart_write_AREAFORMAT (s, NULL, FALSE);
	chart_write_MARKERFORMAT (s, NULL, clear_marks);
	chart_write_PIEFORMAT (s, default_separation);
	chart_write_END (s);
}

static void
chart_write_frame (XLChartWriteState *s, GogObject const *frame,
		   gboolean calc_size, gboolean disable_auto)
{
	GogStyle *style = gog_styled_object_get_style (GOG_STYLED_OBJECT (frame));
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
		  unsigned dim, guint16 flag, guint8 *data)
{
	gboolean user_defined = FALSE;
	double val = gog_axis_get_entry (axis, dim, &user_defined);
	gsf_le_set_double (data, user_defined ? val : 0.);
	return user_defined ? 0 : flag;
}

static void
chart_write_axis (XLChartWriteState *s, GogAxis const *axis,
		  unsigned i, gboolean centered)
{
	gboolean labeled, in, out, inverted = FALSE;
	guint16 tick_color_index, flags = 0;
	guint8 tmp, *data = ms_biff_put_len_next (s->bp, BIFF_CHART_axis, 18);

	GSF_LE_SET_GUINT32 (data + 0, i);
	memset (data+2, 0, 16);
	ms_biff_put_commit (s->bp);

	chart_write_BEGIN (s);
	if (gog_axis_is_discrete (axis)) {
		data = ms_biff_put_len_next (s->bp, BIFF_CHART_catserrange, 8);

		GSF_LE_SET_GUINT16 (data+0, 1); /* values_axis_crosses_at_cat_index */
		GSF_LE_SET_GUINT16 (data+2, 1); /* frequency_of_label */
		GSF_LE_SET_GUINT16 (data+4, 1); /* frequency_of_tick */
		g_object_get (G_OBJECT (axis), "invert-axis", &inverted, NULL);
		flags = centered ? 1 : 0; /* bit 0 == cross in middle of cat or between cats
					     bit 1 == enum cross point from max not min */
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
		gboolean log_scale = FALSE;

		g_object_get (G_OBJECT (axis),
#if 0
			      "log-scale",		&log_scale,
#endif
			      "invert-axis",		&inverted,
			      NULL);
		data = ms_biff_put_len_next (s->bp, BIFF_CHART_valuerange, 42);
		if (log_scale)
			flags |= 0x20;
		if (inverted)
			flags |= 0x40;
#if 0
		/* This will take more information */
		flags |= 0x80; /* partner crosses at max */
#endif
		flags |= 0x100; /* UNDOCUMENTED */

		flags |= xl_axis_set_elem (axis, AXIS_ELEM_MIN,	        0x01, data+ 0);
		flags |= xl_axis_set_elem (axis, AXIS_ELEM_MAX,	        0x02, data+ 8);
		flags |= xl_axis_set_elem (axis, AXIS_ELEM_MAJOR_TICK,  0x04, data+16);
		flags |= xl_axis_set_elem (axis, AXIS_ELEM_MINOR_TICK,  0x08, data+24);
		flags |= xl_axis_set_elem (axis, AXIS_ELEM_CROSS_POINT, 0x10, data+32);
		GSF_LE_SET_GUINT16 (data+40, flags);
		ms_biff_put_commit (s->bp);
	}
	data = ms_biff_put_len_next (s->bp, BIFF_CHART_tick,
		(s->bp->version >= MS_BIFF_V8) ? 30 : 26);
	g_object_get (G_OBJECT (axis),
		"major-tick-labeled",		&labeled,
		"major-tick-in", 		&in,
		"major-tick-out", 		&out,
		/* "major-tick-size-pts",	(unsupported in XL) */
		/* "minor-tick-size-pts",	(unsupported in XL) */
		NULL);
	tmp = out ? 2 : 0;
	if (in)
		tmp |= 1;
	GSF_LE_SET_GUINT8  (data+0, tmp);

	g_object_get (G_OBJECT (axis),
		"minor-tick-in", 	&in,
		"minor-tick-out", 	&out,
		NULL);
	tmp = out ? 2 : 0;
	if (in)
		tmp |= 1;
	GSF_LE_SET_GUINT8  (data+1, tmp);

	tmp = labeled ? 3 : 0; /* label : 0 == none
				* 	  1 == low	(unsupported in gnumeric)
				* 	  2 == high	(unsupported in gnumeric)
				* 	  3 == beside axis */
	GSF_LE_SET_GUINT8  (data+2, tmp);
	GSF_LE_SET_GUINT8  (data+3, 1); /* background mode : 1 == transparent
					 *		     2 == opaque */
	tick_color_index = chart_write_color (s, data+4, 0); /* tick color */
	memset (data+8, 0, 16);
	flags = 0x23;
	GSF_LE_SET_GUINT16 (data+24, flags);
	if (s->bp->version >= MS_BIFF_V8) {
		GSF_LE_SET_GUINT16 (data+26, tick_color_index);
		GSF_LE_SET_GUINT16 (data+28, 0);
	}
	ms_biff_put_commit (s->bp);

	if (axis != NULL) {
		ms_biff_put_2byte (s->bp, BIFF_CHART_axislineformat, 0); /* a real axis */
		chart_write_LINEFORMAT (s, &GOG_STYLED_OBJECT (axis)->style->line,
					TRUE, FALSE);
	}
	chart_write_END (s);
}

static guint16
map_1_5d_type (XLChartWriteState *s, GogPlot const *plot,
	       guint16 stacked, guint16 percentage, guint16 flag_3d)
{
	char const *type;
	gboolean in_3d = FALSE;
	guint16 res;

	g_object_get (G_OBJECT (plot), "type", &type, "in_3d", &in_3d, NULL);

	res = (s->bp->version >= MS_BIFF_V8 && in_3d) ? flag_3d : 0;
	if (0 == strcmp (type, "stacked"))
		return res | stacked;
	if (0 == strcmp (type, "as_percentage"))
		return res | percentage | stacked;
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

	if (0 == strcmp (type, "GogAreaPlot")) {
		ms_biff_put_2byte (s->bp, BIFF_CHART_area,
			map_1_5d_type (s, plot, 1, 2, 4));
	} else if (0 == strcmp (type, "GogBarColPlot")) {
		gboolean horizontal;
		int overlap_percentage, gap_percentage;

		g_object_get (G_OBJECT (plot),
			      "horizontal",		&horizontal,
			      "overlap_percentage",	&overlap_percentage,
			      "gap_percentage",		&gap_percentage,
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
		float initial_angle = 0., center_size = 0., default_separation = 0.;
		gint16 center = 0;
		g_object_get (G_OBJECT (plot),
			"in_3d",		&in_3d,
			"initial_angle",	&initial_angle,
			"default_separation",	&default_separation,
			NULL);

		data = ms_biff_put_len_next (s->bp, BIFF_CHART_pie,
			(s->bp->version >= MS_BIFF_V8) ? 6 : 4);
		GSF_LE_SET_GUINT16 (data + 0, (int)initial_angle);

		if (0 == strcmp (type, "GogRingPlot")) {
			g_object_get (G_OBJECT (plot),
				"center_size",		&center_size,
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
			chart_write_dummy_style (s, default_separation, FALSE, FALSE);
	} else if (0 == strcmp (type, "GogRadarPlot")) {
		gboolean area;
		g_object_get (G_OBJECT (plot), "area", &area, NULL);
		/* TODO : flags : chart contains radar axis labels */
		ms_biff_put_2byte (s->bp,
			area ? BIFF_CHART_radararea : BIFF_CHART_radar,
			flags);
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
				g_object_get (G_OBJECT (plot),
					"show_negatives",	&show_neg,
					"in_3d",		&in_3d,
					"size_as_area",		&as_area,
					NULL);
				/* TODO : find accurate size */
				GSF_LE_SET_GUINT16 (data + 0, 100);
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

	if (check_marks || check_lines)
		chart_write_dummy_style (s, 0., check_marks, check_lines);
}

static void
chart_write_axis_sets (XLChartWriteState *s, GSList *sets)
{
	guint16 i = 0;
	guint8 *data;
	GSList *sptr, *pptr;
	XLAxisSet *axis_set;
	GogObject const *legend = gog_object_get_child_by_role (s->chart,
		gog_object_find_role_by_name (s->chart, "Legend"));

	ms_biff_put_2byte (s->bp, BIFF_CHART_axesused, g_slist_length (sets));
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
		switch (gog_chart_axis_set (GOG_CHART (s->chart))) {
		default :
		case GOG_AXIS_SET_UNKNOWN :
		case GOG_AXIS_SET_NONE :
			break;
		case GOG_AXIS_SET_XY :
		case GOG_AXIS_SET_XY_pseudo_3d :
			/* BIFF_CHART_pos, optional we use auto positioning */
			if (axis_set->transpose) {
				chart_write_axis (s, axis_set->axis[GOG_AXIS_Y],
					0, axis_set->center_ticks);
				chart_write_axis (s, axis_set->axis[GOG_AXIS_X],
					1, TRUE);
			} else {
				chart_write_axis (s, axis_set->axis[GOG_AXIS_X],
					0, axis_set->center_ticks);
				chart_write_axis (s, axis_set->axis[GOG_AXIS_Y],
					1, TRUE);
			}
			break;
		case GOG_AXIS_SET_RADAR :
			break;
		}

		if (i == 0) {
			GogObject *grid = gog_object_get_child_by_role (s->chart,
				gog_object_find_role_by_name (s->chart, "Grid"));
			if (grid != NULL) {
				ms_biff_put_empty (s->bp, BIFF_CHART_plotarea);
				chart_write_frame (s, grid, TRUE, TRUE);
			}
		}

		for (pptr = axis_set[i].plots ; pptr != NULL ; pptr = pptr->next, i++) {
			gboolean vary;
			guint16 flags = 0;

			g_object_get (G_OBJECT (pptr->data),
				      "vary_style_by_element", &vary,
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
			if (i == 0 && legend != NULL) {
				/*GogObjectPosition pos = gog_object_get_pos (legend); */
				guint16 flags = 0x1f;

				data = ms_biff_put_len_next (s->bp, BIFF_CHART_legend, 20);
				chart_write_position (s, legend, data);
				GSF_LE_SET_GUINT8 (data + 16, 3);
				GSF_LE_SET_GUINT8 (data + 17, 1);
				GSF_LE_SET_GUINT16 (data + 18, flags);

				ms_biff_put_commit (s->bp);

				chart_write_BEGIN (s);
				/* BIFF_CHART_pos, optional we use auto positioning */
				chart_write_text (s, NULL, NULL);
				chart_write_END (s);
			}
			chart_write_END (s);
		}
		chart_write_END (s);

		g_slist_free (axis_set->plots);
		g_free (axis_set);
	}
	g_slist_free (sets);
}

void
ms_excel_chart_write (ExcelWriteState *ewb, SheetObject *so)
{
	guint8 *data;
	GogRenderer *renderer;
	XLChartWriteState state;
	unsigned i, num_series = 0;
	GSList const *plots, *series;
	GSList *sets, *ptr;
	XLAxisSet *axis_set;

	state.bp  = ewb->bp;
	state.ewb = ewb;
	state.so  = so;
	state.graph = sheet_object_graph_get_gog (so);

	/* WARNING : catch multiple charts */
	state.chart = gog_object_get_child_by_role (GOG_OBJECT (state.graph),
		gog_object_find_role_by_name (GOG_OBJECT (state.graph), "Chart"));
	state.nest_level = 0;
#warning TODO : create a null renderer class for use in sizing things
	renderer  = g_object_new (GOG_RENDERER_TYPE,
				  "model", state.graph,
				  NULL);
	g_object_get (G_OBJECT (renderer), "view", &state.root_view, NULL);

	g_return_if_fail (state.graph != NULL);

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
	chart_write_position (&state, state.chart, data);
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
	for (plots = gog_chart_get_plots (GOG_CHART (state.chart)) ; plots != NULL ; plots = plots->next) {
		/* XL can not handle plots with no data */
		if (gog_plot_get_series (plots->data) == NULL) {
			g_warning ("MS Excel can not handle plots with no data, dropping %s",
				gog_object_get_name (plots->data));
			continue;
		}

		axis_set = g_new0 (XLAxisSet, 1);
		for (i = GOG_AXIS_X; i < GOG_AXIS_TYPES; i++)
			axis_set->axis[i] = gog_plot_get_axis (plots->data, i);

		if (0 == strcmp (G_OBJECT_TYPE_NAME (plots->data), "GogBarColPlot")) {
			g_object_get (G_OBJECT (plots->data),
				      "horizontal", &axis_set->transpose,
				      NULL);
			axis_set->center_ticks = TRUE;
		} else if (0 == strcmp (G_OBJECT_TYPE_NAME (plots->data), "GogAreaPlot"))
			axis_set->center_ticks = TRUE;
		ptr = g_slist_find_custom (sets, axis_set,
			(GCompareFunc) cb_axis_set_cmp);
		if (ptr != NULL) {
			g_free (axis_set);
			axis_set = (XLAxisSet *)(ptr->data);
		} else
			sets = g_slist_prepend (sets, axis_set);
		axis_set->plots = g_slist_prepend (axis_set->plots, plots->data);
	}

	/* dump the associated series (skip any that we are dropping */
	for (ptr = sets; ptr != NULL ; ptr = ptr->next)
		for (plots = ((XLAxisSet *)ptr->data)->plots ; plots != NULL ; plots = plots->next)
			for (series = gog_plot_get_series (plots->data) ; series != NULL ; series = series->next)
				chart_write_series (&state, series->data, num_series++);

	data = ms_biff_put_len_next (state.bp, BIFF_CHART_shtprops, 4);
	GSF_LE_SET_GUINT32 (data + 0, 0xa);
	ms_biff_put_commit (state.bp);

#warning what do these connect to ?
	for (i = 2; i <= 3; i++) {
		ms_biff_put_2byte (state.bp, BIFF_CHART_defaulttext, i);
		chart_write_text (&state, NULL, NULL);
	}

	chart_write_axis_sets (&state, sets);
	chart_write_END (&state);

#if 0 /* they seem optional */
	BIFF_DIMENSIONS
	BIFF_CHART_siindex x num_series ?
#endif
	ms_biff_put_empty (ewb->bp, BIFF_EOF);

	g_object_unref (renderer);
}
