/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/**
 * ms-chart.c: MS Excel chart support for Gnumeric
 *
 * Author:
 *    Jody Goldberg (jody@gnome.org)
 *
 * (C) 1999-2002 Jody Goldberg
 **/

#include <gnumeric-config.h>
#include <gnumeric.h>
#include "boot.h"
#include "excel.h"
#include "ms-chart.h"
#include "ms-formula-read.h"
#include "ms-excel-read.h"
#include "ms-escher.h"

#include <parse-util.h>
#include <format.h>
#include <expr.h>
#include <value.h>
#include <gutils.h>
#include <graph.h>
#include <sheet-object-graph.h>

#include <goffice/graph/goffice-graph.h>
#include <goffice/graph/gog-graph.h>
#include <goffice/graph/gog-chart.h>
#include <goffice/graph/gog-plot-impl.h>
#include <goffice/graph/gog-series-impl.h>
#include <goffice/graph/gog-object.h>
#include <goffice/graph/gog-styled-object.h>
#include <goffice/graph/gog-style.h>
#include <goffice/graph/gog-plot-engine.h>
#include <goffice/utils/go-color.h>
#include <goffice/utils/go-pattern.h>
#include <goffice/utils/go-marker.h>

#include <gsf/gsf-utils.h>
#include <math.h>
#include <stdio.h>

/* #define NO_DEBUG_EXCEL */
#ifndef NO_DEBUG_EXCEL
#define d(level, code)	do { if (ms_excel_chart_debug > level) { code } } while (0)
#else
#define d(level, code)
#endif

typedef struct _XLChartSeries {
	struct {
		int num_elements;
		GOData *data;
	} data [GOG_MS_DIM_TYPES];
	int chart_group;
} XLChartSeries;

typedef struct {
	MSContainer	 container;

	GArray		*stack;
	MsBiffVersion	 ver;
	guint32		 prev_opcode;

	SheetObject	*sog;
	GogGraph	*graph;
	GogChart	*chart;
	GogPlot		*plot;
	GogObject	*axis;

	GogStyle	*style;
	int		 style_element;

	int plot_counter;
	XLChartSeries *currentSeries;
	GPtrArray	 *series;
} XLChartReadState;

typedef struct {
	int dummy;
} XLChartWriteState;

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
	g_free (series);
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
	guint32 const rgb = GSF_LE_GET_GUINT32 (data);

	d (1, {
		guint16 const r = (rgb >>  0) & 0xff;
		guint16 const g = (rgb >>  8) & 0xff;
		guint16 const b = (rgb >> 16) & 0xff;
		fprintf(stderr, "%s %02x:%02x:%02x;\n", type, r, g, b);
	});
	return RGB_TO_RGBA (rgb, 0xff);
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
			fputs ("Use perspective", stderr);
		if (cluster)
			fputs ("Cluster", stderr);
		if (auto_scale)
			fputs ("Auto Scale", stderr);
		if (walls_2d)
			fputs ("2D Walls", stderr);
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
		StyleFormat *fmt = ms_container_get_fmt (&s->container,
			GSF_LE_GET_GUINT16 (q->data + 4));
		d (2, fputs ("Has Custom number format", stderr););
		if (fmt != NULL) {
			char * desc = style_format_as_XL (fmt, FALSE);
			d (2, fprintf (stderr, "Format = '%s';\n", desc););
			g_free (desc);

			style_format_unref (fmt);
		}
	} else {
		d (2, fputs ("Uses number format from data source", stderr););
	}

	g_return_val_if_fail (purpose < GOG_MS_DIM_TYPES, TRUE);
	d (0, {
	switch (purpose) {
	case GOG_MS_DIM_LABELS :     fputs ("Linking labels", stderr); break;
	case GOG_MS_DIM_VALUES :     fputs ("Linking values", stderr); break;
	case GOG_MS_DIM_CATEGORIES : fputs ("Linking categories", stderr); break;
	case GOG_MS_DIM_BUBBLES :    fputs ("Linking bubbles", stderr); break;
	default :
		g_assert_not_reached ();
	};
	switch (ref_type) {
	case 0 : fputs ("Use default categories", stderr); break;
	case 1 : fputs ("Text/Value entered directly", stderr); break;
	case 2 : fputs ("Linked to Container", stderr); break;
	case 4 : fputs ("'Error reported' what the heck is this ??", stderr); break;
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
	/* gboolean in_3d = (s->container.ver >= MS_BIFF_V8 && (flags & 0x04)); */

	g_return_val_if_fail (s->plot == NULL, TRUE);
	s->plot = gog_plot_new_by_name ("GogAreaPlot");
	g_return_val_if_fail (s->plot != NULL, TRUE);

	if (flags & 0x02)
		type = "as_percentage";
	else if (flags & 0x01)
		type = "stacked";

	g_object_set (G_OBJECT (s->plot),
		"type",			type,
		/* "in_3d",		in_3d, */
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
			fputs ("Use auto format;", stderr);
		if (invert_if_negative)
			fputs ("Swap fore and back colours when displaying negatives;", stderr);
	});

	/* These apply to frames also */
	if (s->style == NULL)
		return FALSE;

	s->style->fill.type = GOG_FILL_STYLE_PATTERN;
	s->style->fill.is_auto = auto_format;
	s->style->fill.invert_if_negative = invert_if_negative;
	s->style->fill.u.pattern.pat.pattern = pattern;
	s->style->fill.u.pattern.pat.fore = BC_R(color) (q->data+0, "AreaFore");
	s->style->fill.u.pattern.pat.back = BC_R(color) (q->data+4, "AreaBack");

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
		fputs ("Show Value", stderr);
	if (show_percent)
		fputs ("Show as Percentage", stderr);
	if (show_label_prercent)
		fputs ("Show as Label Percentage", stderr);
	if (smooth_line)
		fputs ("Smooth line", stderr);
	if (show_label)
		fputs ("Show the label", stderr);

	if (s->container.ver >= MS_BIFF_V8)
	{
		gboolean const show_bubble_size = (flags&0x20) ? TRUE : FALSE;
		if (show_bubble_size)
			fputs ("Show bubble size", stderr);
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
BC_R(axislineformat)(XLChartHandler const *handle,
		     XLChartReadState *s, BiffQuery *q)
{
	d (0, {
	guint16 const type = GSF_LE_GET_GUINT16 (q->data);

	fprintf (stderr, "Axisline is ");
	switch (type)
	{
	case 0 : fputs ("the axis line.", stderr); break;
	case 1 : fputs ("a major grid along the axis.", stderr); break;
	case 2 : fputs ("a minor grid along the axis.", stderr); break;

	/* TODO TODO : floor vs wall */
	case 3 : fputs ("a floor/wall along the axis.", stderr); break;
	default : fprintf (stderr, "an ERROR.  unkown type (%x).\n", type);
	};
	});
	return FALSE;
}

/****************************************************************************/

static gboolean
BC_R(axisparent)(XLChartHandler const *handle,
		 XLChartReadState *s, BiffQuery *q)
{
	d (0, {
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
	/* gboolean in_3d = (s->container.ver >= MS_BIFF_V8 && (flags & 0x08)); */

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
		/* "in_3d",		in_3d, */
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
	d(0, fputs ("{", stderr););
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
	d (0, {
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
	dump_biff (q);
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

	g_return_val_if_fail (s->style == NULL, TRUE);
	g_return_val_if_fail (series_index < s->series->len, TRUE);

	series = g_ptr_array_index (s->series, series_index);

	g_return_val_if_fail (series != NULL, TRUE);

	if (pt_num == 0xffff) {
		s->style_element = -1;
		d (0, fprintf (stderr, "All points"););
	} else {
		s->style_element = pt_num;
		d (0, fprintf (stderr, "Point-%hd", pt_num););
	}
	s->style = gog_style_new ();

	d (0, fprintf (stderr, ", series=%hd\n", series_index););

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
	guint16 const type = GSF_LE_GET_GUINT16 (q->data);
	guint16 const flags = GSF_LE_GET_GUINT16 (q->data+2);
	gboolean border_shadow, auto_size, auto_pos;

#if 0
	g_return_val_if_fail (type == 1 || type ==4, TRUE);
#endif
	/* FIXME FIXME FIXME : figure out what other values are */
	border_shadow = (type == 4) ? TRUE : FALSE;
	auto_size = (flags&0x01) ? TRUE : FALSE;
	auto_pos = (flags&0x02) ? TRUE : FALSE;

	return FALSE;
}

/****************************************************************************/

static gboolean
BC_R(gelframe)(XLChartHandler const *handle,
	       XLChartReadState *s, BiffQuery *q)
{
	gint tmp = ms_excel_escher_debug;
	ms_excel_escher_debug = 2;
	ms_escher_parse (q, &s->container);
	ms_excel_escher_debug = tmp;
	return FALSE;
}
/****************************************************************************/

static gboolean
BC_R(ifmt)(XLChartHandler const *handle,
	   XLChartReadState *s, BiffQuery *q)
{
	StyleFormat *fmt = ms_container_get_fmt (&s->container,
		GSF_LE_GET_GUINT16 (q->data));

	if (fmt != NULL) {
		char * desc = style_format_as_XL (fmt, FALSE);
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
	case 0: pos = GOG_POSITION_S; break;
	case 1: pos = GOG_POSITION_N | GOG_POSITION_E; break;
	case 2: pos = GOG_POSITION_N; break;
	case 3: pos = GOG_POSITION_E; break;
	case 4: pos = GOG_POSITION_W; break;
	case 7: break; /* treat floating legends as being on east */
	default :
		g_warning ("Unknown legend position (%d), assuming east.",
			   XL_pos);
	};

	gog_object_add_by_name (GOG_OBJECT (s->chart), "Legend", NULL);

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
	return FALSE;
}

/****************************************************************************/

static gboolean
BC_R(line)(XLChartHandler const *handle,
	   XLChartReadState *s, BiffQuery *q)
{
	guint16 const flags = GSF_LE_GET_GUINT16 (q->data);
	char const *type = "normal";
	/* gboolean in_3d = (s->container.ver >= MS_BIFF_V8 && (flags & 0x04)); */

	g_return_val_if_fail (s->plot == NULL, TRUE);
	s->plot = gog_plot_new_by_name ("GogLinePlot");
	g_return_val_if_fail (s->plot != NULL, TRUE);

	if (flags & 0x02)
		type = "as_percentage";
	else if (flags & 0x01)
		type = "stacked";

	g_object_set (G_OBJECT (s->plot),
		"type",			type,
		/* "in_3d",		in_3d, */
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
	guint16 const flags = GSF_LE_GET_GUINT16 (q->data+8);
	GogStyle *style = s->style;

	if (style == NULL) {
		if (s->axis != NULL)
			style = gog_styled_object_get_style (GOG_STYLED_OBJECT (s->axis));
		if (style == NULL)
			return FALSE;
	}

	switch (GSF_LE_GET_GUINT16 (q->data+6)) {
	default :
	case -1 : style->line.width = 0; /* hairline */
		break;
	case  0 : style->line.width = 1; /* 'normal' */
		break;
	case  1 : style->line.width = 3; /* 'medium' */
		break;
	case  2 : style->line.width = 5; /* 'wide' */
		break;
	}
	style->line.color      = BC_R(color) (q->data, "LineColor");
	style->line.auto_color = (flags & 0x01) ? TRUE : FALSE;
	style->line.pattern    = GSF_LE_GET_GUINT16 (q->data+4);

	if (s->axis != NULL)
		g_object_set (G_OBJECT (s->axis),
			"major-tick-labeled",	((flags & 0x04) ? TRUE : FALSE),
			NULL);

	d (0, fprintf (stderr, "Lines are %f pts wide.\n", s->style->line.width););
	d (0, fprintf (stderr, "Lines have a %s pattern.\n",
		       ms_line_pattern [s->style->line.pattern ]););

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
	/* gboolean const auto_color = (flags & 0x01) ? TRUE : FALSE; */

	if (s->style == NULL)
		return FALSE;

	marker = go_marker_new ();

	d (0, fprintf (stderr, "Marker = %s\n", ms_chart_marker [shape]););
	if (shape >= G_N_ELEMENTS (shape_map))
		shape = 1; /* square */
	go_marker_set_shape (marker, shape_map [shape]);

	go_marker_set_outline_color (marker, 
		(flags & 0x20) ? 0 : BC_R(color) (q->data + 0, "MarkerFore"));
	go_marker_set_fill_color (marker, 
		(flags & 0x10) ? 0 : BC_R(color) (q->data + 4, "MarkerBack"));

	if (s->container.ver >= MS_BIFF_V8) {
		guint32 const marker_size = GSF_LE_GET_GUINT32 (q->data+16);
		go_marker_set_size (marker, marker_size);
		d (1, fprintf (stderr, "Marker is %u\n", marker_size););
	}

	gog_style_set_marker (s->style, marker);

	return FALSE;
}

/****************************************************************************/

static gboolean
BC_R(objectlink)(XLChartHandler const *handle,
		 XLChartReadState *s, BiffQuery *q)
{
	d (2, {
	guint16 const purpose = GSF_LE_GET_GUINT16 (q->data);
	guint16 const series_num = GSF_LE_GET_GUINT16 (q->data+2);
	guint16 const pt_num = GSF_LE_GET_GUINT16 (q->data+2);

	switch (purpose)
	{
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
	float default_separation = GSF_LE_GET_GUINT16 (q->data+2); /* 0-100 */
	guint16 const flags = GSF_LE_GET_GUINT16 (q->data+4);
	gboolean in_3d = (s->container.ver >= MS_BIFF_V8 && (flags & 0x01));

	g_return_val_if_fail (s->plot == NULL, TRUE);
	s->plot = gog_plot_new_by_name ("GogPiePlot");
	g_return_val_if_fail (s->plot != NULL, TRUE);

	g_object_set (G_OBJECT (s->plot),
		"in_3d",		in_3d,
		"initial_angle",	initial_angle,
		"default_separation",	default_separation,
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
	guint16 const percent_diam = GSF_LE_GET_GUINT16 (q->data); /* 0-100 */

	g_return_val_if_fail (percent_diam <= 100, TRUE);

#if 0
	pie = e_xml_get_child_by_name (s->xml.style, (xmlChar *)"Pie");
	if (pie == NULL)
		pie = xmlNewChild (s->xml.style, s->xml.ns, (xmlChar *)"Pie", NULL);

	/* This is for individual slices */
	if (percent_diam > 0) {
		xmlNode *tmp = xmlNewChild (pie, pie->ns,
			(xmlChar *)"separation_percent_of_radius", NULL);
		xml_node_set_int (tmp, NULL, percent_diam);
	}

#endif
	d (2, fprintf (stderr, "Pie slice is %hu %% of diam from center\n", percent_diam););
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
	return FALSE;
}

/****************************************************************************/

static gboolean
BC_R(radararea)(XLChartHandler const *handle,
		XLChartReadState *s, BiffQuery *q)
{
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
	s->plot = gog_plot_new_by_name ("GogXYPlot");
	g_return_val_if_fail (s->plot != NULL, TRUE);

	if (s->container.ver >= MS_BIFF_V8) {
		guint16 const flags = GSF_LE_GET_GUINT16 (q->data+4);

		/* Has bubbles */
		if (flags & 0x01) {
#if 0
			guint16 const size_type = GSF_LE_GET_GUINT16 (q->data+2);
			if (!(flags & 0x02))	/* hide negatives */
			if (flags & 0x04)	/* in_3d */

			/* huh ? */
			xml_node_set_int (fmt, "percentage_largest_tochart",
					  GSF_LE_GET_GUINT16 (q->data));
			xmlNewChild (fmt, fmt->ns,
				     (xmlChar *)((size_type == 2)
					     ? "bubble_sized_as_width"
					     : "bubble_sized_as_area"),
				     NULL);
	g_object_set (G_OBJECT (s->plot),
		"type",			type,
		/* "in_3d",		in_3d, */
		NULL);

#endif
		}
	}

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

	str = biff_get_text (q->data + 3, slen, NULL);
	d (2, fputs (str, stderr););

	/* A quick heuristic */
	if (s->currentSeries != NULL &&
	    s->currentSeries->data [GOG_MS_DIM_LABELS].data == NULL) {
		s->currentSeries->data [GOG_MS_DIM_LABELS].data =
			gnm_go_data_scalar_new_expr (
				ms_container_sheet (s->container.parent),
				gnm_expr_new_constant (value_new_string (str)));
	}

	/* TODO : handle axis and chart titles */

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
	d (2, fputs (ms_chart_blank[blanks], stderr););

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
		d (4, fputs ("Text follows defaulttext", stderr););
	} else {
	}

#if 0
case BIFF_CHART_chart :
	fputs ("Text follows chart", stderr);
	break;
case BIFF_CHART_legend :
	fputs ("Text follows legend", stderr);
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
	d (1, {
	guint16 const major_type = GSF_LE_GET_GUINT8 (q->data);
	guint16 const minor_type = GSF_LE_GET_GUINT8 (q->data+1);
	guint16 const position   = GSF_LE_GET_GUINT8 (q->data+2);

	guint16 const flags = GSF_LE_GET_GUINT8 (q->data+24);

	switch (major_type) {
	case 0: fputs ("no major tick;", stderr); break;
	case 1: fputs ("major tick inside axis;", stderr); break;
	case 2: fputs ("major tick outside axis;", stderr); break;
	case 3: fputs ("major tick across axis;", stderr); break;
	default : fputs ("unknown major tick type", stderr);
	}
	switch (minor_type) {
	case 0: fputs ("no minor tick;", stderr); break;
	case 1: fputs ("minor tick inside axis;", stderr); break;
	case 2: fputs ("minor tick outside axis;", stderr); break;
	case 3: fputs ("minor tick across axis;", stderr); break;
	default : fputs ("unknown minor tick type", stderr);
	}
	switch (position) {
	case 0: fputs ("no tick label;", stderr); break;
	case 1: fputs ("tick label at low end;", stderr); break;
	case 2: fputs ("tick label at high end;", stderr); break;
	case 3: fputs ("tick label near axis;", stderr); break;
	default : fputs ("unknown tick label position", stderr);
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
	case 0: fputs ("no rotation;", stderr); break;
	case 1: fputs ("top to bottom letters upright;", stderr); break;
	case 2: fputs ("rotate 90deg counter-clockwise;", stderr); break;
	case 3: fputs ("rotate 90deg clockwise;", stderr); break;
	default : fputs ("unknown rotation", stderr);
	}

	if (flags&0x20)
		fputs ("Auto rotate", stderr);
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


static gboolean
conditional_get_double (gboolean flag, guint8 const *data,
			gchar const *name)
{
	if (!flag) {
		double const val = gsf_le_get_double (data);
		d (1, fprintf (stderr, "%s = %f\n", name, val););
		return TRUE;
	}
	d (1, fprintf (stderr, "%s = Auto\n", name););
	return FALSE;
}

static gboolean
BC_R(valuerange)(XLChartHandler const *handle,
		 XLChartReadState *s, BiffQuery *q)
{
	guint16 const flags = gsf_le_get_double (q->data+40);

	conditional_get_double (flags&0x01, q->data+ 0, "Min Value");
	conditional_get_double (flags&0x02, q->data+ 8, "Max Value");
	conditional_get_double (flags&0x04, q->data+16, "Major Increment");
	conditional_get_double (flags&0x08, q->data+24, "Minor Increment");
	conditional_get_double (flags&0x10, q->data+32, "Cross over point");

	d (1, {
	if (flags&0x20)
		fputs ("Log scaled", stderr);
	if (flags&0x40)
		fputs ("Values in reverse order", stderr);
	if (flags&0x80)
		fputs ("Cross over at max value", stderr);
	});

	return FALSE;
}

/****************************************************************************/

static void
XL_gog_series_set_dim (GogSeries *series, GogMSDimType ms_type, GOData *val)
{
	GogSeriesDesc const *desc = &series->plot->desc.series;
	unsigned i = desc->num_dim;

	if (ms_type == GOG_MS_DIM_LABELS) {
		gog_series_set_dim (series, -1, val, NULL);
		return;
	}
	while (i-- > 0)
		if (desc->dim[i].ms_type == ms_type) {
			gog_series_set_dim (series, i, val, NULL);
			return;
		}
	g_warning ("Unexpected val for dim %d", ms_type);
}

static gboolean
BC_R(end)(XLChartHandler const *handle,
	  XLChartReadState *s, BiffQuery *q)
{
	int popped_state;

	d (0, fputs ("}", stderr););

	g_return_val_if_fail (s->stack != NULL, TRUE);
	g_return_val_if_fail (s->stack->len > 0, TRUE);

	popped_state = BC_R(top_state) (s);
	s->stack = g_array_remove_index_fast (s->stack, s->stack->len-1);

	switch (popped_state) {
	case BIFF_CHART_axis :
		s->axis = NULL;
		break;

	case BIFF_CHART_series :
		g_return_val_if_fail (s->currentSeries != NULL, TRUE);
		s->currentSeries = NULL;
		break;

	case BIFF_CHART_chartformat : {
		unsigned i, j;
		XLChartSeries *eseries;
		GogSeries     *series;

		g_return_val_if_fail (s->plot != NULL, TRUE);

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
		}

		gog_object_add_by_name (GOG_OBJECT (s->chart),
			"Plot", GOG_OBJECT (s->plot));
		s->plot = NULL;
		break;
	}

	case BIFF_CHART_dataformat :
		g_return_val_if_fail (s->style != NULL, TRUE);
		g_object_unref (s->style);
		s->style = NULL;
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

static StyleFormat *
chart_get_fmt (MSContainer const *container, guint16 indx)
{
	return ms_container_get_fmt (container->parent, indx);
}

gboolean
ms_excel_read_chart (BiffQuery *q, MSContainer *container, MsBiffVersion ver,
		     SheetObject *sog)
{
	static MSContainerClass const vtbl = {
		chart_realize_obj,
		chart_create_obj,
		chart_parse_expr,
		chart_get_sheet,
		chart_get_fmt
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

	if (NULL != (state.sog = sog)) {
		state.graph = sheet_object_graph_get_gog (sog);
		state.chart = GOG_CHART (gog_object_add_by_name (GOG_OBJECT (state.graph), "Chart", NULL));
	} else {
		state.graph = NULL;
		state.chart = NULL;
	}
	state.plot  = NULL;
	state.axis  = NULL;
	state.style = NULL;

	d (0, fputs ("{ CHART", stderr););

	while (!done && ms_biff_query_next (q)) {
		int const lsb = q->opcode & 0xff;

		/* Use registered jump table for chart records */
		if ((q->opcode & 0xff00) == 0x1000) {
			int const begin_end =
				(q->opcode == BIFF_CHART_begin ||
				 q->opcode == BIFF_CHART_end);

			if (lsb >= num_handler ||
			    !chart_biff_handler [lsb] ||
			    chart_biff_handler  [lsb]->opcode != q->opcode) {
				d (0, {	fprintf (stderr, "Unknown BIFF_CHART record\n");
					dump_biff (q);});
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
		} else {
			switch (lsb) {
			case BIFF_EOF:
				done = TRUE;
				d (0, fputs ("}; /* CHART */", stderr););
				g_return_val_if_fail(state.stack->len == 0, TRUE);
				break;

			case BIFF_PROTECT : {
				gboolean const is_protected =
					(1 == GSF_LE_GET_GUINT16 (q->data));
				d (4, fprintf (stderr, "Chart is%s protected;\n",
					     is_protected ? "" : " not"););
				break;
			}

			case BIFF_NUMBER: {
				double val;
				val = gsf_le_get_double (q->data + 6);
				/* Figure out how to assign these back to the series,
				 * are they just sequential ?
				 */
				d (10, fprintf (stderr, "%f\n", val););
				break;
			}

			case BIFF_LABEL : {
				guint16 row = GSF_LE_GET_GUINT16 (q->data + 0);
				guint16 col = GSF_LE_GET_GUINT16 (q->data + 2);
				guint16 xf  = GSF_LE_GET_GUINT16 (q->data + 4);
				guint16 len = GSF_LE_GET_GUINT16 (q->data + 6);
				char *label = biff_get_text (q->data + 8, len, NULL);
				d (10, {fputs (label, stderr);
					fprintf (stderr, "hmm, what are these values for a chart ???\n"
						"row = %d, col = %d, xf = %d\n", row, col, xf);});
				g_free (label);
				break;
			}

			case BIFF_MS_O_DRAWING:
				ms_escher_parse (q, &state.container);
				break;

			case BIFF_EXTERNCOUNT: /* ignore */ break;
			case BIFF_EXTERNSHEET: /* These cannot be biff8 */
				excel_read_EXTERNSHEET_v7 (q, &state.container);
				break;
			case BIFF_PLS:		/* Skip for Now */
			case BIFF_DIMENSIONS :	/* Skip for Now */
			case BIFF_HEADER :	/* Skip for Now */
			case BIFF_FOOTER :	/* Skip for Now */
			case BIFF_HCENTER :	/* Skip for Now */
			case BIFF_VCENTER :	/* Skip for Now */
			case 0x200|BIFF_WINDOW2 :
			case BIFF_CODENAME :
			case BIFF_SCL :		/* Are charts scaled separately from the sheet ? */
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
		}
		state.prev_opcode = q->opcode;
	}

	/* Cleanup */
	for (i = state.series->len; i-- > 0 ; ) {
		XLChartSeries *series = g_ptr_array_index (state.series, i);
		if (series != NULL)
			excel_chart_series_delete (series);
	}
	g_ptr_array_free (state.series, TRUE);
	ms_container_finalize (&state.container);

	return FALSE;
}

/* A wrapper which reads and checks the BOF record then calls ms_excel_read_chart */
/**
 * ms_excel_read_chart_BOF :
 * @q : #BiffQuery
 * @container : #MSContainer
 * @sog : #SheetObjectGraph
 **/
gboolean
ms_excel_read_chart_BOF (BiffQuery *q, MSContainer *container, SheetObject *sog)
{
	MsBiffBofData *bof;
	gboolean res = TRUE;

	/* 1st record must be a valid BOF record */
	g_return_val_if_fail (ms_biff_query_next (q), TRUE);
	bof = ms_biff_bof_data_new (q);

	g_return_val_if_fail (bof != NULL, TRUE);
	g_return_val_if_fail (bof->type == MS_BIFF_TYPE_Chart, TRUE);

	if (bof->version != MS_BIFF_V_UNKNOWN)
		res = ms_excel_read_chart (q, container, bof->version, sog);
	ms_biff_bof_data_destroy (bof);
	return res;
}
