/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/**
 * ms-chart.c: MS Excel chart support for Gnumeric
 *
 * Author:
 *    Jody Goldberg (jody@gnome.org)
 *
 * (C) 1999-2001 Jody Goldberg
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
#include <style-color.h>
#include <format.h>
#include <expr.h>
#include <value.h>
#include <gutils.h>

#ifdef ENABLE_BONOBO
#include <gnumeric-graph.h>
#endif
#include <xml-io.h>
#include <gal/util/e-xml-utils.h>
#include <libxml/tree.h>
#include <stdio.h>
#include <math.h>

/* #define NO_DEBUG_EXCEL */
#ifndef NO_DEBUG_EXCEL
#define d(level, code)	do { if (ms_excel_chart_debug > level) { code } } while (0)
#else
#define d(level, code)
#endif

typedef enum {
	MS_VECTOR_PURPOSE_LABELS	= 0,
	MS_VECTOR_PURPOSE_VALUES	= 1,
	MS_VECTOR_PURPOSE_CATEGORIES	= 2,
	/* This is undocumented, but makes sense */
	MS_VECTOR_PURPOSE_BUBBLES	= 3,
	MS_VECTOR_PURPOSE_MAX		= 4
} MS_VECTOR_PURPOSE;

char const *const ms_vector_purpose_type_name [] =
{
    "labels", "values", "categories", "bubbles",
};

typedef struct _ExcelChartSeries
{
	struct {
#ifdef ENABLE_BONOBO
		GnmGraphVectorType type;
#endif
		int count, remote_ID;
	} vector [MS_VECTOR_PURPOSE_MAX];

	int chart_group;
	xmlNode	*xml;
} ExcelChartSeries;

typedef struct
{
	MSContainer	 container;

	MSContainer	*parent;
	GArray		*stack;
	MsBiffVersion	 ver;
	guint32		 prev_opcode;
	GnmGraph	*graph;

	struct {
		xmlDoc	*doc;
		xmlNs	*ns;
		xmlNode *plots;
		xmlNode *currentChartGroup;
		xmlNode *dataFormat;
	} xml;

	int plot_counter;
	ExcelChartSeries *currentSeries;
	GPtrArray	 *series;
} ExcelChartReadState;

typedef struct
{
	int dummy;
} GnumericChartState;

typedef struct biff_chart_handler ExcelChartHandler;
typedef gboolean (*ExcelChartReader)(ExcelChartHandler const *handle,
				     ExcelChartReadState *, BiffQuery *q);
typedef gboolean (*ExcelChartWriter)(ExcelChartHandler const *handle,
				     GnumericChartState *, BiffPut *os);
struct biff_chart_handler
{
	guint16 const opcode;
	int const	min_size; /* To be useful this needs to be versioned */
	char const *const name;
	ExcelChartReader const read_fn;
	ExcelChartWriter const write_fn;
};

#define BC(n)	biff_chart_ ## n
#define BC_R(n)	BC(read_ ## n)
#define BC_W(n)	BC(write_ ## n)

static ExcelChartSeries *
excel_chart_series_new (void)
{
	ExcelChartSeries *series;
	int i;

	series = g_new (ExcelChartSeries, 1);

	series->chart_group = -1;
	series->xml = NULL;
	for (i = MS_VECTOR_PURPOSE_MAX; i-- > 0 ; ) {
		series->vector [i].remote_ID = -1;
#ifdef ENABLE_BONOBO
		series->vector [i].type = GNM_VECTOR_AUTO; /* may be reset later */
#endif
	}

	/* labels are always strings */
#ifdef ENABLE_BONOBO
	series->vector [MS_VECTOR_PURPOSE_LABELS].type = GNM_VECTOR_STRING;
#endif

	return series;
}

static void
excel_chart_series_delete (ExcelChartSeries *series)
{
	g_free (series);
}


static void
excel_chart_series_write_xml (ExcelChartSeries *series,
			      ExcelChartReadState *s, xmlNode *data)
{
	unsigned i;

	g_return_if_fail (series->xml != NULL);

	xmlAddChild (data, series->xml);
	for (i = 0 ; i < MS_VECTOR_PURPOSE_MAX; i++ )
		if (series->vector [i].remote_ID >= 0) {
#ifdef ENABLE_BONOBO
			xmlNode *v = gnm_graph_series_add_dimension (series->xml,
				ms_vector_purpose_type_name [i]);
			if (v != NULL)
				e_xml_set_integer_prop_by_name (v, (xmlChar *)"ID",
					series->vector [i].remote_ID);
#endif
		}
}

static int
BC_R(top_state) (ExcelChartReadState *s)
{
	g_return_val_if_fail (s != NULL, 0);
	return g_array_index (s->stack, int, s->stack->len-1);
}

static void
BC_R(color) (guint8 const *data, xmlChar *type, xmlNode *node, gboolean transparent)
{
	static xmlChar buf[20];
	guint32 const rgb = MS_OLE_GET_GUINT32 (data);
	guint16 const r = (rgb >>  0) & 0xff;
	guint16 const g = (rgb >>  8) & 0xff;
	guint16 const b = (rgb >> 16) & 0xff;

	sprintf ((char *)buf, "%02x:%02x:%02x", r, g, b);
	xmlSetProp (node, type, buf);

	d (0, printf("%s %02x:%02x:%02x;\n", type, r, g, b););
}

static xmlNode *
BC_R(store_chartgroup_type)(ExcelChartReadState *s, xmlChar const *t)
{
	xmlNode *fmt;

	g_return_val_if_fail (s->xml.currentChartGroup != NULL, NULL);

	fmt = e_xml_get_child_by_name (s->xml.currentChartGroup, (xmlChar *)"Type");

	g_return_val_if_fail (fmt == NULL, NULL);

	fmt = xmlNewChild (s->xml.currentChartGroup, s->xml.ns, (xmlChar *)"Type", NULL);
	xmlSetProp (fmt, (xmlChar *)"name", t);
	return fmt;
}

/****************************************************************************/

static gboolean
BC_R(3dbarshape)(ExcelChartHandler const *handle,
		 ExcelChartReadState *s, BiffQuery *q)
{
	d (0, {
		guint16 const type = MS_OLE_GET_GUINT16 (q->data);
		switch (type) {
		case 0 : puts ("box"); break;
		case 1 : puts ("cylinder"); break;
		case 256 : puts ("pyramid"); break;
		case 257 : puts ("cone"); break;
		default :
			   printf ("unknown 3dshape %d\n", type);
		};
	});

	return FALSE;
}
static gboolean
BC_W(3dbarshape)(ExcelChartHandler const *handle,
		 GnumericChartState *s, BiffPut *os)
{
	return FALSE;
}

/****************************************************************************/

static gboolean
BC_R(3d)(ExcelChartHandler const *handle,
	 ExcelChartReadState *s, BiffQuery *q)
{
	d (0, {
		guint16 const rotation = MS_OLE_GET_GUINT16 (q->data);	/* 0-360 */
		guint16 const elevation = MS_OLE_GET_GUINT16 (q->data+2);	/* -90 - 90 */
		guint16 const distance = MS_OLE_GET_GUINT16 (q->data+4);	/* 0 - 100 */
		guint16 const height = MS_OLE_GET_GUINT16 (q->data+6);
		guint16 const depth = MS_OLE_GET_GUINT16 (q->data+8);
		guint16 const gap = MS_OLE_GET_GUINT16 (q->data+10);
		guint8 const flags = MS_OLE_GET_GUINT8 (q->data+12);
		guint8 const zero = MS_OLE_GET_GUINT8 (q->data+13);

		gboolean const use_perspective = (flags&0x01) ? TRUE :FALSE;
		gboolean const cluster = (flags&0x02) ? TRUE :FALSE;
		gboolean const auto_scale = (flags&0x04) ? TRUE :FALSE;
		gboolean const walls_2d = (flags&0x20) ? TRUE :FALSE;

		g_return_val_if_fail (zero == 0, FALSE); /* just warn for now */

		printf ("Rot = %hu\n", rotation);
		printf ("Elev = %hu\n", elevation);
		printf ("Dist = %hu\n", distance);
		printf ("Height = %hu\n", height);
		printf ("Depth = %hu\n", depth);
		printf ("Gap = %hu\n", gap);

		if (use_perspective)
			puts ("Use perspective");
		if (cluster)
			puts ("Cluster");
		if (auto_scale)
			puts ("Auto Scale");
		if (walls_2d)
			puts ("2D Walls");
	});

	return FALSE;
}

static gboolean
BC_W(3d)(ExcelChartHandler const *handle,
	 GnumericChartState *s, BiffPut *os)
{
	return FALSE;
}

/****************************************************************************/

static gboolean
BC_R(ai)(ExcelChartHandler const *handle,
	 ExcelChartReadState *s, BiffQuery *q)
{
	guint8 const purpose = MS_OLE_GET_GUINT8 (q->data);
	guint8 const ref_type = MS_OLE_GET_GUINT8 (q->data + 1);
	guint16 const flags = MS_OLE_GET_GUINT16 (q->data + 2);
	guint16 const length = MS_OLE_GET_GUINT16 (q->data + 6);

	int popped_state = BC_R(top_state) (s);

	/* ignore these for now */
	if (popped_state == BIFF_CHART_text)
		return FALSE;

	/* Rest are 0 */
	if (flags&0x01) {
		StyleFormat *fmt = ms_container_get_fmt (&s->container, 
			MS_OLE_GET_GUINT16 (q->data + 4));
		d (2, puts ("Has Custom number format"););
		if (fmt != NULL) {
			char * desc = style_format_as_XL (fmt, FALSE);
			d (2, printf ("Format = '%s';\n", desc););
			g_free (desc);

			style_format_unref (fmt);
		}
	} else {
		d (2, puts ("Uses number format from data source"););
	}

	g_return_val_if_fail (purpose < MS_VECTOR_PURPOSE_MAX, TRUE);
	d (0, {
	switch (purpose) {
	case MS_VECTOR_PURPOSE_LABELS :	    puts ("Linking labels"); break;
	case MS_VECTOR_PURPOSE_VALUES :	    puts ("Linking values"); break;
	case MS_VECTOR_PURPOSE_CATEGORIES : puts ("Linking categories"); break;
	case MS_VECTOR_PURPOSE_BUBBLES :    puts ("Linking bubbles"); break;
	default :
		g_assert_not_reached ();
	};
	switch (ref_type) {
	case 0 : puts ("Use default categories"); break;
	case 1 : puts ("Text/Value entered directly"); break;
	case 2 : puts ("Linked to Container"); break;
	case 4 : puts ("'Error reported' what the heck is this ??"); break;
	default :
		 printf ("UKNOWN : reference type (%x)\n", ref_type);
	};
	});

	/* (2) == linked to container */
	if (ref_type == 2) {
		ExprTree *expr = ms_container_parse_expr (s->parent,
							  q->data+8, length);
		if (expr) {
			Sheet *sheet = ms_container_sheet (s->parent);

			g_return_val_if_fail (sheet != NULL, FALSE);
			g_return_val_if_fail (s->currentSeries != NULL, TRUE);

#ifdef ENABLE_BONOBO
			s->currentSeries->vector [purpose].remote_ID =
				gnm_graph_add_vector (s->graph, expr,
					s->currentSeries->vector [purpose].type,
					sheet);
#endif

		}
	} else {
		g_return_val_if_fail (length == 0, TRUE);
	}

	return FALSE;
}

static gboolean
BC_W(ai)(ExcelChartHandler const *handle,
	 GnumericChartState *s, BiffPut *os)
{
	return FALSE;
}

/****************************************************************************/

static gboolean
BC_R(alruns)(ExcelChartHandler const *handle,
	     ExcelChartReadState *s, BiffQuery *q)
{
	gint16 length = MS_OLE_GET_GUINT16 (q->data);
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
		guint32 const elem = MS_OLE_GET_GUINT32 (in);
		*out = (char)((elem >> 16) & 0xff);
	}
	*out = '\0';

	/*puts (ans);*/
	return FALSE;
}

static gboolean
BC_W(alruns)(ExcelChartHandler const *handle,
	     GnumericChartState *s, BiffPut *os)
{
	return FALSE;
}

/****************************************************************************/

static gboolean
BC_R(area)(ExcelChartHandler const *handle,
	   ExcelChartReadState *s, BiffQuery *q)
{
	guint16 const flags = MS_OLE_GET_GUINT16 (q->data);
	gboolean const stacked = (flags & 0x01) ? TRUE : FALSE;
	gboolean const as_percentage = (flags & 0x02) ? TRUE : FALSE;

	d (0, {
	if (as_percentage)
		/* TODO : test theory that percentage implies stacked */
		printf ("Stacked Percentage. (%d should be TRUE)\n", stacked);
	else if (stacked)
		printf ("Stacked Percentage values\n");
	else
		printf ("Overlayed values\n");
	});

	if (s->container.ver >= MS_BIFF_V8)
	{
		d (0, {
		gboolean const has_shadow = (flags & 0x04) ? TRUE : FALSE;
		if (has_shadow)
			puts ("in 3D");
		});
	}
	return FALSE;
}

static gboolean
BC_W(area)(ExcelChartHandler const *handle,
	   GnumericChartState *s, BiffPut *os)
{
	return FALSE;
}

/****************************************************************************/

static gboolean
BC_R(areaformat)(ExcelChartHandler const *handle,
		 ExcelChartReadState *s, BiffQuery *q)
{
	xmlNode *area = NULL;
	guint16 const flags = MS_OLE_GET_GUINT16 (q->data+10);
	gboolean const auto_format = (flags & 0x01) ? TRUE : FALSE;

	d (0, {
	guint16 const pattern = MS_OLE_GET_GUINT16 (q->data+8);
	gboolean const swap_color_for_negative = flags & 0x02;

	printf ("pattern = %d;\n", pattern);
	if (auto_format)
		puts ("Use auto format;");
	if (swap_color_for_negative)
		puts ("Swap fore and back colours when displaying negatives;");
	});

	/* These apply to frames also */
	if (s->xml.dataFormat != NULL) {
		area = e_xml_get_child_by_name (s->xml.dataFormat, (xmlChar *)"Area");
		if (area == NULL)
			area = xmlNewChild (s->xml.dataFormat, s->xml.ns,
					    (xmlChar *)"Area", NULL);
	}
	if (area != NULL && !auto_format) {
		BC_R(color) (q->data, (xmlChar *)"ForegroundColour", area, FALSE);
		BC_R(color) (q->data+4, (xmlChar *)"BackgroundColour", area, FALSE);
	}
#if 0
	/* Ignore the colour indicies.  Use the colours themselves
	 * to avoid problems with guessing the strange index values
	 */
	if (s->container.ver >= MS_BIFF_V8)
	{
		guint16 const fore_index = MS_OLE_GET_GUINT16 (q->data+12);
		guint16 const back_index = MS_OLE_GET_GUINT16 (q->data+14);

		/* TODO : Ignore result for now,
		 * Which to use, fore and back, or these ? */
		ms_excel_palette_get (s->wb->palette, fore_index);
		ms_excel_palette_get (s->wb->palette, back_index);
	}
#endif
	return FALSE;
}

static gboolean
BC_W(areaformat)(ExcelChartHandler const *handle,
		 GnumericChartState *s, BiffPut *os)
{
	return FALSE;
}

/****************************************************************************/

static gboolean
BC_R(attachedlabel)(ExcelChartHandler const *handle,
		    ExcelChartReadState *s, BiffQuery *q)
{
	d (3,{
	guint16 const flags = MS_OLE_GET_GUINT16 (q->data);
	gboolean const show_value = (flags&0x01) ? TRUE : FALSE;
	gboolean const show_percent = (flags&0x02) ? TRUE : FALSE;
	gboolean const show_label_prercent = (flags&0x04) ? TRUE : FALSE;
	gboolean const smooth_line = (flags&0x08) ? TRUE : FALSE;
	gboolean const show_label = (flags&0x10) ? TRUE : FALSE;

	if (show_value)
		puts ("Show Value");
	if (show_percent)
		puts ("Show as Percentage");
	if (show_label_prercent)
		puts ("Show as Label Percentage");
	if (smooth_line)
		puts ("Smooth line");
	if (show_label)
		puts ("Show the label");

	if (s->container.ver >= MS_BIFF_V8)
	{
		gboolean const show_bubble_size = (flags&0x20) ? TRUE : FALSE;
		if (show_bubble_size)
			puts ("Show bubble size");
	}
	});
	return FALSE;
}

static gboolean
BC_W(attachedlabel)(ExcelChartHandler const *handle,
		    GnumericChartState *s, BiffPut *os)
{
	return FALSE;
}

/****************************************************************************/

static gboolean
BC_R(axesused)(ExcelChartHandler const *handle,
	       ExcelChartReadState *s, BiffQuery *q)
{
	guint16 const num_axis = MS_OLE_GET_GUINT16 (q->data);
	g_return_val_if_fail(1 <= num_axis && num_axis <= 2, TRUE);
	d (0, printf ("There are %hu axis.\n", num_axis););
	return FALSE;
}

static gboolean
BC_W(axesused)(ExcelChartHandler const *handle,
	       GnumericChartState *s, BiffPut *os)
{
	return FALSE;
}

/****************************************************************************/

typedef enum
{
	MS_AXIS_X	= 0,
	MS_AXIS_Y	= 1,
	MS_AXIS_SERIES	= 2,
	MS_AXIS_MAX	= 3
} MS_AXIS;
static char const *const ms_axis[] =
{
	"X-axis", "Y-axis", "series-axis"
};

static gboolean
BC_R(axis)(ExcelChartHandler const *handle,
	   ExcelChartReadState *s, BiffQuery *q)
{
	guint16 const axis_type = MS_OLE_GET_GUINT16 (q->data);
	MS_AXIS atype;
	g_return_val_if_fail (axis_type < MS_AXIS_MAX, TRUE);
	atype = axis_type;
	d (0, printf ("This is a %s .\n", ms_axis[atype]););
	return FALSE;
}

static gboolean
BC_W(axis)(ExcelChartHandler const *handle,
	   GnumericChartState *s, BiffPut *os)
{
	return FALSE;
}

/****************************************************************************/

static gboolean
BC_R(axcext)(ExcelChartHandler const *handle,
	     ExcelChartReadState *s, BiffQuery *q)
{
	return FALSE;
}
static gboolean
BC_W(axcext)(ExcelChartHandler const *handle,
	     GnumericChartState *s, BiffPut *os)
{
	return FALSE;
}

/****************************************************************************/

static gboolean
BC_R(axislineformat)(ExcelChartHandler const *handle,
		     ExcelChartReadState *s, BiffQuery *q)
{
	d (0, {
	guint16 const type = MS_OLE_GET_GUINT16 (q->data);

	printf ("Axisline is ");
	switch (type)
	{
	case 0 : puts ("the axis line."); break;
	case 1 : puts ("a major grid along the axis."); break;
	case 2 : puts ("a minor grid along the axis."); break;

	/* TODO TODO : floor vs wall */
	case 3 : puts ("a floor/wall along the axis."); break;
	default : printf ("an ERROR.  unkown type (%x).\n", type);
	};
	});
	return FALSE;
}

static gboolean
BC_W(axislineformat)(ExcelChartHandler const *handle,
		     GnumericChartState *s, BiffPut *os)
{
	return FALSE;
}

/****************************************************************************/

static gboolean
BC_R(axisparent)(ExcelChartHandler const *handle,
		 ExcelChartReadState *s, BiffQuery *q)
{
	d (0, {
	guint16 const index = MS_OLE_GET_GUINT16 (q->data);	/* 1 or 2 */
	/* Measured in 1/4000ths of the chart width */
	guint32 const x = MS_OLE_GET_GUINT32 (q->data+2);
	guint32 const y = MS_OLE_GET_GUINT32 (q->data+6);
	guint32 const width = MS_OLE_GET_GUINT32 (q->data+10);
	guint32 const height = MS_OLE_GET_GUINT32 (q->data+14);

	printf ("Axis # %hu @ %f,%f, X=%f, Y=%f\n",
		index, x/4000., y/4000., width/4000., height/4000.);
	});
	return FALSE;
}

static gboolean
BC_W(axisparent)(ExcelChartHandler const *handle,
		 GnumericChartState *s, BiffPut *os)
{
	return FALSE;
}

/****************************************************************************/

static gboolean
BC_R(bar)(ExcelChartHandler const *handle,
	  ExcelChartReadState *s, BiffQuery *q)
{
	guint16 const flags = MS_OLE_GET_GUINT16 (q->data+4);

	xmlNode *tmp, *fmt = BC_R(store_chartgroup_type)(s, (xmlChar *)"Bar");

	g_return_val_if_fail (fmt != NULL, TRUE);

	/* Always set this it makes things clearer */
	xmlNewChild (fmt, fmt->ns, (xmlChar *)"horizontal",
		(xmlChar *)((flags & 0x01) ? "true" : "false"));

	if (flags & 0x04)
		xmlNewChild (fmt, fmt->ns, (xmlChar *)"as_percentage", NULL);
	else if (flags & 0x02)
		xmlNewChild (fmt, fmt->ns, (xmlChar *)"stacked", NULL);

	if (s->container.ver >= MS_BIFF_V8 && (flags & 0x08))
		xmlNewChild (fmt, fmt->ns, (xmlChar *)"in_3d", NULL);

	tmp = xmlNewChild (fmt, fmt->ns, (xmlChar *)"percentage_space_between_items", NULL);
	xml_node_set_int (tmp, NULL, MS_OLE_GET_GUINT16 (q->data));
	tmp = xmlNewChild (fmt, fmt->ns, (xmlChar *)"percentage_space_between_groups", NULL);
	xml_node_set_int (tmp, NULL, MS_OLE_GET_GUINT16 (q->data+2));

	return FALSE;
}

static gboolean
BC_W(bar)(ExcelChartHandler const *handle,
	  GnumericChartState *s, BiffPut *os)
{
	return FALSE;
}

/****************************************************************************/

static gboolean
BC_R(begin)(ExcelChartHandler const *handle,
	    ExcelChartReadState *s, BiffQuery *q)
{
	d(0, puts ("{"););
	s->stack = g_array_append_val (s->stack, s->prev_opcode);
	return FALSE;
}

static gboolean
BC_W(begin)(ExcelChartHandler const *handle,
	    GnumericChartState *s, BiffPut *os)
{
	return FALSE;
}

/****************************************************************************/

static gboolean
BC_R(boppop)(ExcelChartHandler const *handle,
	     ExcelChartReadState *s, BiffQuery *q)
{
#if 0
	guint8 const type = MS_OLE_GET_GUINT8 (q->data); /* 0-2 */
	gboolean const use_default_split = (MS_OLE_GET_GUINT8 (q->data+1) == 1);
	guint16 const split_type = MS_OLE_GET_GUINT8 (q->data+2); /* 0-3 */
#endif

	gboolean const is_3d = (MS_OLE_GET_GUINT16 (q->data+16) == 1);
	if (is_3d)
		puts("in 3D");

	return FALSE;
}
static gboolean
BC_W(boppop)(ExcelChartHandler const *handle,
	     GnumericChartState *s, BiffPut *os)
{
	return FALSE;
}

/****************************************************************************/

static gboolean
BC_R(boppopcustom)(ExcelChartHandler const *handle,
		   ExcelChartReadState *s, BiffQuery *q)
{
#if 0
	gint16 const count = MS_OLE_GET_GUINT16 (q->data);
	/* TODO TODO : figure out the bitfield array */
#endif
	return FALSE;
}

static gboolean
BC_W(boppopcustom)(ExcelChartHandler const *handle,
		   GnumericChartState *s, BiffPut *os)
{
	return FALSE;
}

/***************************************************************************/

static gboolean
BC_R(catserrange)(ExcelChartHandler const *handle,
		  ExcelChartReadState *s, BiffQuery *q)
{
	return FALSE;
}

static gboolean
BC_W(catserrange)(ExcelChartHandler const *handle,
		  GnumericChartState *s, BiffPut *os)
{
	return FALSE;
}

/****************************************************************************/

static gboolean
BC_R(chart)(ExcelChartHandler const *handle,
	    ExcelChartReadState *s, BiffQuery *q)
{
	d (0, {
	/* Fixed point 2 bytes fraction 2 bytes integer */
	guint32 const x_pos_fixed = MS_OLE_GET_GUINT32 (q->data + 0);
	guint32 const y_pos_fixed = MS_OLE_GET_GUINT32 (q->data + 4);
	guint32 const x_size_fixed = MS_OLE_GET_GUINT32 (q->data + 8);
	guint32 const y_size_fixed = MS_OLE_GET_GUINT32 (q->data + 12);

	/* Measured in points (1/72 of an inch) */
	double const x_pos = x_pos_fixed / (65535. * 72.);
	double const y_pos = y_pos_fixed / (65535. * 72.);
	double const x_size = x_size_fixed / (65535. * 72.);
	double const y_size = y_size_fixed / (65535. * 72.);
	printf("Chart @ %g, %g is %g\" x %g\"\n", x_pos, y_pos, x_size, y_size);
	});

	return FALSE;
}

static gboolean
BC_W(chart)(ExcelChartHandler const *handle,
	    GnumericChartState *s, BiffPut *os)
{
	return FALSE;
}

/****************************************************************************/

static gboolean
BC_R(chartformat)(ExcelChartHandler const *handle,
		  ExcelChartReadState *s, BiffQuery *q)
{
	guint16 const flags = MS_OLE_GET_GUINT16 (q->data+16);
	guint16 const z_order = MS_OLE_GET_GUINT16 (q->data+18);
	gboolean const vary_color = (flags&0x01) ? TRUE : FALSE;

	/* always update the counter to keep the index in line with the chart
	 * group specifier for series
	 */
	s->plot_counter++;

	g_return_val_if_fail (s->xml.currentChartGroup == NULL, TRUE);

	s->xml.currentChartGroup =
		xmlNewChild (s->xml.plots, s->xml.ns, (xmlChar *)"Plot", NULL);
	xml_node_set_int (s->xml.currentChartGroup, "index", s->plot_counter);
	xml_node_set_int (s->xml.currentChartGroup, "stacking_position", z_order);

	if (vary_color)
		e_xml_set_bool_prop_by_name (s->xml.currentChartGroup,
					     (xmlChar *)"color_individual_points", TRUE);

	d (0, {
		printf ("Z value = %uh\n", z_order);
		if (vary_color)
			printf ("Vary color of individual data points.\n");
	});

	return FALSE;
}

static gboolean
BC_W(chartformat)(ExcelChartHandler const *handle,
		  GnumericChartState *s, BiffPut *os)
{
	return FALSE;
}

/****************************************************************************/

static gboolean
BC_R(chartformatlink)(ExcelChartHandler const *handle,
		      ExcelChartReadState *s, BiffQuery *q)
{
	/* ignored */
	return FALSE;
}

static gboolean
BC_W(chartformatlink)(ExcelChartHandler const *handle,
		      GnumericChartState *s, BiffPut *os)
{
	/* ignored */
	return FALSE;
}

/****************************************************************************/

static gboolean
BC_R(chartline)(ExcelChartHandler const *handle,
		ExcelChartReadState *s, BiffQuery *q)
{
	guint16 const type = MS_OLE_GET_GUINT16 (q->data);

	g_return_val_if_fail (type <= 2, FALSE);

	d (0, printf ("Use %s lines\n",
	     (type == 0) ? "drop" : ((type == 1) ? "hi-lo" : "series")););

	return FALSE;
}

static gboolean
BC_W(chartline)(ExcelChartHandler const *handle,
		GnumericChartState *s, BiffPut *os)
{
	return FALSE;
}

/****************************************************************************/

static gboolean
BC_R(clrtclient)(ExcelChartHandler const *handle,
		 ExcelChartReadState *s, BiffQuery *q)
{
	puts ("Undocumented BIFF : clrtclient");
	dump_biff(q);
	return FALSE;
}
static gboolean
BC_W(clrtclient)(ExcelChartHandler const *handle,
		 GnumericChartState *s, BiffPut *os)
{
	return FALSE;
}

/****************************************************************************/

static gboolean
BC_R(dat)(ExcelChartHandler const *handle,
	  ExcelChartReadState *s, BiffQuery *q)
{
#if 0
	gint16 const flags = MS_OLE_GET_GUINT16 (q->data);
	gboolean const horiz_border = (flags&0x01) ? TRUE : FALSE;
	gboolean const vert_border = (flags&0x02) ? TRUE : FALSE;
	gboolean const border = (flags&0x04) ? TRUE : FALSE;
	gboolean const series_keys = (flags&0x08) ? TRUE : FALSE;
#endif
	return FALSE;
}
static gboolean
BC_W(dat)(ExcelChartHandler const *handle,
	  GnumericChartState *s, BiffPut *os)
{
	return FALSE;
}

/****************************************************************************/

static gboolean
BC_R(dataformat)(ExcelChartHandler const *handle,
		 ExcelChartReadState *s, BiffQuery *q)
{
	ExcelChartSeries *series;
	guint16 const pt_num = MS_OLE_GET_GUINT16 (q->data);
	guint16 const series_index = MS_OLE_GET_GUINT16 (q->data+2);
#if 0
	guint16 const series_index_for_label = MS_OLE_GET_GUINT16 (q->data+4);
	guint16 const excel4_auto_color = MS_OLE_GET_GUINT16 (q->data+6) & 0x01;
#endif

	g_return_val_if_fail (s->xml.dataFormat == NULL, TRUE);
	g_return_val_if_fail (series_index < s->series->len, TRUE);

	series = g_ptr_array_index (s->series, series_index);

	g_return_val_if_fail (series != NULL, TRUE);
	g_return_val_if_fail (series->xml != NULL, TRUE);

	if (pt_num == 0xffff) {
		s->xml.dataFormat = xmlNewChild (series->xml, s->xml.ns,
						 (xmlChar *)"Format", NULL);
		d (0, printf ("All points"););
	} else {
		s->xml.dataFormat = xmlNewChild (series->xml, s->xml.ns,
						 (xmlChar *)"FormatPoint", NULL);
		e_xml_set_integer_prop_by_name (s->xml.dataFormat,
			(xmlChar *)"index", pt_num);
		d (0, printf ("Point-%hd", pt_num););
	}

	d (0, printf (", series=%hd\n", series_index););

	return FALSE;
}

static gboolean
BC_W(dataformat)(ExcelChartHandler const *handle,
		 GnumericChartState *s, BiffPut *os)
{
	return FALSE;
}

/****************************************************************************/

static gboolean
BC_R(defaulttext)(ExcelChartHandler const *handle,
		  ExcelChartReadState *s, BiffQuery *q)
{
	guint16	const tmp = MS_OLE_GET_GUINT16 (q->data);
	
	d (2, printf ("applicability = %hd\n", tmp););

	/*
	 * 0 == 'show labels' label
	 * 1 == Value and percentage data label
	 * 2 == All text in chart
	 * 3 == Undocumented ??
	 */
	g_return_val_if_fail (tmp <= 3, TRUE);
	return FALSE;
}

static gboolean
BC_W(defaulttext)(ExcelChartHandler const *handle,
		  GnumericChartState *s, BiffPut *os)
{
	return FALSE;
}

/****************************************************************************/

static gboolean
BC_R(dropbar)(ExcelChartHandler const *handle,
	      ExcelChartReadState *s, BiffQuery *q)
{
	/* NOTE : The docs lie.  values > 100 seem legal.  My guess based on
	 * the ui is 500.
	guint16 const width = MS_OLE_GET_GUINT16 (q->data);
	 */
	return FALSE;
}

static gboolean
BC_W(dropbar)(ExcelChartHandler const *handle,
	      GnumericChartState *s, BiffPut *os)
{
	return FALSE;
}

/****************************************************************************/

static gboolean
BC_R(fbi)(ExcelChartHandler const *handle,
	  ExcelChartReadState *s, BiffQuery *q)
{
	/*
	 * TODO TODO TODO : Work on appropriate scales.
	 * Is any of this useful other than the index ?
	 */
	guint16 const x_basis = MS_OLE_GET_GUINT16 (q->data);
	guint16 const y_basis = MS_OLE_GET_GUINT16 (q->data+2);
	guint16 const applied_height = MS_OLE_GET_GUINT16 (q->data+4);
	guint16 const scale_basis = MS_OLE_GET_GUINT16 (q->data+6);
	guint16 const index = MS_OLE_GET_GUINT16 (q->data+8);

	d (2, printf ("Font %hu (%hu x %hu) scale=%hu, height=%hu\n",
		index, x_basis, y_basis, scale_basis, applied_height););
	return FALSE;
}
static gboolean
BC_W(fbi)(ExcelChartHandler const *handle,
	  GnumericChartState *s, BiffPut *os)
{
	return FALSE;
}

/****************************************************************************/

static gboolean
BC_R(fontx)(ExcelChartHandler const *handle,
	    ExcelChartReadState *s, BiffQuery *q)
{
#if 0
	/* Child of TEXT, index into FONT table */
	guint16 const font = MS_OLE_GET_GUINT16 (q->data);
#endif
	return FALSE;
}

static gboolean
BC_W(fontx)(ExcelChartHandler const *handle,
	    GnumericChartState *s, BiffPut *os)
{
	return FALSE;
}

/****************************************************************************/

static gboolean
BC_R(frame)(ExcelChartHandler const *handle,
	    ExcelChartReadState *s, BiffQuery *q)
{
	guint16 const type = MS_OLE_GET_GUINT16 (q->data);
	guint16 const flags = MS_OLE_GET_GUINT16 (q->data+2);
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

static gboolean
BC_W(frame)(ExcelChartHandler const *handle,
	    GnumericChartState *s, BiffPut *os)
{
	return FALSE;
}

/****************************************************************************/

static gboolean
BC_R(gelframe)(ExcelChartHandler const *handle,
	       ExcelChartReadState *s, BiffQuery *q)
{
	ms_escher_parse (q, &s->container);
	return FALSE;
}
static gboolean
BC_W(gelframe)(ExcelChartHandler const *handle,
	       GnumericChartState *s, BiffPut *os)
{
	return FALSE;
}

/****************************************************************************/

static gboolean
BC_R(ifmt)(ExcelChartHandler const *handle,
	   ExcelChartReadState *s, BiffQuery *q)
{
	StyleFormat *fmt = ms_container_get_fmt (&s->container, 
		MS_OLE_GET_GUINT16 (q->data));

	if (fmt != NULL) {
		char * desc = style_format_as_XL (fmt, FALSE);
		d (0, printf ("Format = '%s';\n", desc););
		g_free (desc);

		style_format_unref (fmt);
	}

	return FALSE;
}

static gboolean
BC_W(ifmt)(ExcelChartHandler const *handle,
	   GnumericChartState *s, BiffPut *os)
{
	return FALSE;
}

/****************************************************************************/

static gboolean
BC_R(legend)(ExcelChartHandler const *handle,
	     ExcelChartReadState *s, BiffQuery *q)
{
#if 0
	/* Measured in 1/4000ths of the chart width */
	guint32 const x_pos = MS_OLE_GET_GUINT32  (q->data);
	guint32 const y_pos = MS_OLE_GET_GUINT32  (q->data+4);
	guint32 const width = MS_OLE_GET_GUINT32  (q->data+8);
	guint32 const height = MS_OLE_GET_GUINT32 (q->data+12);
	guint8 const spacing = MS_OLE_GET_GUINT8  (q->data+17);
	guint16 const flags = MS_OLE_GET_GUINT16  (q->data+18);
#endif
	guint16 const position = MS_OLE_GET_GUINT8 (q->data+16);
	char const *position_txt = "east";
	xmlNode *legend;

	switch (position) {
	case 0: position_txt = "south"; break;
	case 1: break; /* What is corner ? */
	case 2: position_txt = "north";	break;
	case 3: break; /* east */
	case 4: position_txt = "west";	break;
	case 7: break; /* treat floating legends as being on east */
	default :
		g_warning ("Unknown legend position (%d), assuming east.",
			   position);
	};

	legend = e_xml_get_child_by_name (s->xml.doc->xmlRootNode, (xmlChar *)"Legend");

	g_return_val_if_fail (legend == NULL, TRUE);

	legend = xmlNewChild (s->xml.doc->xmlRootNode, s->xml.ns,
			      (xmlChar *)"Legend", NULL);
	legend = xmlNewChild (legend, s->xml.ns, (xmlChar *)"Position", (xmlChar *)position_txt);

#if 0
	printf ("Legend @ %f,%f, X=%f, Y=%f\n",
		x_pos/4000., y_pos/4000., width/4000., height/4000.);

	/* FIXME : Parse the flags too */
#endif

	return FALSE;
}

static gboolean
BC_W(legend)(ExcelChartHandler const *handle,
	     GnumericChartState *s, BiffPut *os)
{
	return FALSE;
}

/****************************************************************************/

static gboolean
BC_R(legendxn)(ExcelChartHandler const *handle,
	       ExcelChartReadState *s, BiffQuery *q)
{
	return FALSE;
}

static gboolean
BC_W(legendxn)(ExcelChartHandler const *handle,
	       GnumericChartState *s, BiffPut *os)
{
	return FALSE;
}

/****************************************************************************/

static gboolean
BC_R(line)(ExcelChartHandler const *handle,
	   ExcelChartReadState *s, BiffQuery *q)
{
	guint16 const flags = MS_OLE_GET_GUINT16 (q->data);

	xmlNode *fmt = BC_R(store_chartgroup_type)(s, (xmlChar *)"Line");

	g_return_val_if_fail (fmt != NULL, TRUE);

	if (flags & 0x02)
		xmlNewChild (fmt, fmt->ns, (xmlChar *)"as_percentage", NULL);
	else if (flags & 0x01)
		xmlNewChild (fmt, fmt->ns, (xmlChar *)"stacked", NULL);

	if (s->container.ver >= MS_BIFF_V8 && (flags & 0x04))
		xmlNewChild (fmt, fmt->ns, (xmlChar *)"in_3d", NULL);

	return FALSE;
}

static gboolean
BC_W(line)(ExcelChartHandler const *handle,
	   GnumericChartState *s, BiffPut *os)
{
	return FALSE;
}

/****************************************************************************/

typedef enum
{
	MS_LINE_PATTERN_SOLID		= 0,
	MS_LINE_PATTERN_DASH		= 1,
	MS_LINE_PATTERN_DOT		= 2,
	MS_LINE_PATTERN_DASH_DOT	= 3,
	MS_LINE_PATTERN_DASH_DOT_DOT	= 4,
	MS_LINE_PATTERN_NONE		= 5,
	MS_LINE_PATTERN_DARK_GRAY	= 6,
	MS_LINE_PATTERN_MED_GRAY	= 7,
	MS_LINE_PATTERN_LIGHT_GRAY	= 8,
	MS_LINE_PATTERN_MAX	= 9
} MS_LINE_PATTERN;
static char const *const ms_line_pattern[] =
{
	"solid", "dashed", "doted", "dash doted", "dash dot doted", "invisible",
	"dark gray", "medium gray", "light gray"
};

typedef enum
{
	MS_LINE_WGT_MIN	= -2,
	MS_LINE_WGT_HAIRLINE	= -1,
	MS_LINE_WGT_NORMAL	= 0,
	MS_LINE_WGT_MEDIUM	= 1,
	MS_LINE_WGT_WIDE	= 2,
	MS_LINE_WGT_MAX	= 3
} MS_LINE_WGT;
static char const *const ms_line_wgt[] =
{
	"hairline", "normal", "medium", "extra"
};

static gboolean
BC_R(lineformat)(ExcelChartHandler const *handle,
		 ExcelChartReadState *s, BiffQuery *q)
{
	xmlNode *line = NULL;
	guint16 const pattern = MS_OLE_GET_GUINT16 (q->data+4);
	gint16 const weight = MS_OLE_GET_GUINT16 (q->data+6);
	guint16 const flags = MS_OLE_GET_GUINT16 (q->data+8);
	gboolean	auto_format, draw_ticks;
	MS_LINE_PATTERN pat;
	MS_LINE_WGT	wgt;

	g_return_val_if_fail (pattern < MS_LINE_PATTERN_MAX, TRUE);
	pat = pattern;
	d (0, printf ("Lines have a %s pattern.\n", ms_line_pattern[pat]););

	g_return_val_if_fail (weight < MS_LINE_WGT_MAX, TRUE);
	g_return_val_if_fail (weight > MS_LINE_WGT_MIN, TRUE);
	wgt = weight;
	d (0, printf ("Lines are %s wide.\n", ms_line_wgt[wgt+1]););

	auto_format = (flags & 0x01) ? TRUE : FALSE;
	draw_ticks = (flags & 0x04) ? TRUE : FALSE;

	/* Applies to frames too */
	if (s->xml.dataFormat != NULL) {
		line = e_xml_get_child_by_name (s->xml.dataFormat, (xmlChar *)"Line");
		if (line == NULL)
			line = xmlNewChild (s->xml.dataFormat, s->xml.ns,
					    (xmlChar *)"Line", NULL);
	}

	if (line != NULL && !auto_format)
		BC_R(color) (q->data, (xmlChar *)"Colour", line, FALSE);

#if 0
	/* Ignore the colour indicies.  Use the colours themselves
	 * to avoid problems with guessing the strange index values
	 */
	if (s->container.ver >= MS_BIFF_V8)
	{
		guint16 const color_index = MS_OLE_GET_GUINT16 (q->data+10);

		/* Ignore result for now */
		ms_excel_palette_get (s->wb->palette, color_index);
	}
#endif
	return FALSE;
}

static gboolean
BC_W(lineformat)(ExcelChartHandler const *handle,
		 GnumericChartState *s, BiffPut *os)
{
	return FALSE;
}

/****************************************************************************/

static gboolean
BC_R(markerformat)(ExcelChartHandler const *handle,
		   ExcelChartReadState *s, BiffQuery *q)
{
	static char const *const ms_chart_marker[] = {
		"none", "square", "diamond", "triangle", "x", "star",
		"dow", "std", "circle", "plus"
	};
	guint16 const tmp = MS_OLE_GET_GUINT16 (q->data+8);
	guint16 const flags = MS_OLE_GET_GUINT16 (q->data+10);
	gboolean const auto_color = (flags & 0x01) ? TRUE : FALSE;
	gboolean const no_fore	= (flags & 0x10) ? TRUE : FALSE;
	gboolean const no_back = (flags & 0x20) ? TRUE : FALSE;
	xmlNode *marker;

	g_return_val_if_fail (s->xml.dataFormat, TRUE);

	marker = e_xml_get_child_by_name (s->xml.dataFormat, (xmlChar *)"Marker");
	if (marker == NULL)
		marker = xmlNewChild (s->xml.dataFormat, s->xml.ns,
				      (xmlChar *)"Marker", NULL);

	g_return_val_if_fail (tmp < 10, TRUE);

	d (0, printf ("Marker = %s\n", ms_chart_marker [tmp]););
	if (tmp > 0)
		xmlSetProp (marker, (xmlChar *)"shape", (xmlChar *)ms_chart_marker [tmp]);

	if (!auto_color) {
		BC_R(color) (q->data, (xmlChar *)"BorderColour", marker, no_fore);
		BC_R(color) (q->data+4, (xmlChar *)"InteriorColour", marker, no_back);
	}

	if (s->container.ver >= MS_BIFF_V8) {
#if 0
		/* Ignore the colour indicies.  Use the colours themselves to
		 * avoid problems with guessing the strange index values
		 */
		StyleColor const * marker_border =
		    ms_excel_palette_get (s->wb->palette,
					  MS_OLE_GET_GUINT16 (q->data+12));
		StyleColor const * marker_fill =
		    ms_excel_palette_get (s->wb->palette,
					  MS_OLE_GET_GUINT16 (q->data+14));
#endif
		d (1, {
		guint32 const marker_size = MS_OLE_GET_GUINT32 (q->data+16);
		printf ("Marker is %u\n", marker_size);
		});
	}
	return FALSE;
}

static gboolean
BC_W(markerformat)(ExcelChartHandler const *handle,
		   GnumericChartState *s, BiffPut *os)
{
	return FALSE;
}

/****************************************************************************/

static gboolean
BC_R(objectlink)(ExcelChartHandler const *handle,
		 ExcelChartReadState *s, BiffQuery *q)
{
	d (2, {
	guint16 const purpose = MS_OLE_GET_GUINT16 (q->data);
	guint16 const series_num = MS_OLE_GET_GUINT16 (q->data+2);
	guint16 const pt_num = MS_OLE_GET_GUINT16 (q->data+2);

	switch (purpose)
	{
	case 1 : printf ("TEXT is chart title\n"); break;
	case 2 : printf ("TEXT is Y axis title\n"); break;
	case 3 : printf ("TEXT is X axis title\n"); break;
	case 4 : printf ("TEXT is data label for pt %hd in series %hd\n",
			 pt_num, series_num); break;
	case 7 : printf ("TEXT is Z axis title\n"); break;
	default :
		 printf ("ERROR : TEXT is linked to undocumented object\n");
	};});
	return FALSE;
}

static gboolean
BC_W(objectlink)(ExcelChartHandler const *handle,
		 GnumericChartState *s, BiffPut *os)
{
	return FALSE;
}

/****************************************************************************/

static gboolean
BC_R(picf)(ExcelChartHandler const *handle,
	   ExcelChartReadState *s, BiffQuery *q)
{
	return FALSE;
}

static gboolean
BC_W(picf)(ExcelChartHandler const *handle,
	   GnumericChartState *s, BiffPut *os)
{
	return FALSE;
}

/****************************************************************************/

static gboolean
BC_R(pie)(ExcelChartHandler const *handle,
	  ExcelChartReadState *s, BiffQuery *q)
{
	double radians;
	xmlNode *tmp, *fmt = BC_R(store_chartgroup_type)(s, (xmlChar *)"Pie");
	guint16 const percent_diam = MS_OLE_GET_GUINT16 (q->data+2); /* 0-100 */

	/* This is for the whole pie */
	if (percent_diam > 0) {
		xmlNode *tmp = xmlNewChild (fmt, fmt->ns,
			(xmlChar *)"separation_percent_of_radius", NULL);
		xml_node_set_int (tmp, NULL, percent_diam);
	}

	radians = MS_OLE_GET_GUINT16 (q->data);
	radians = (radians * 2. * M_PI / 360.);
	tmp = xmlNewChild (fmt, fmt->ns, (xmlChar *)"radians_of_first_pie", NULL);
	xml_node_set_double (fmt, NULL, radians, -1); 

	if (s->container.ver >= MS_BIFF_V8) {
		guint16 const flags = MS_OLE_GET_GUINT16 (q->data+4);

		if (flags & 0x1)
			xmlNewChild (fmt, fmt->ns, (xmlChar *)"in_3d", NULL);
#if 0
		if (flags & 0x2)
			e_xml_set_bool_prop_by_name (fmt, "leader_lines", TRUE);
#endif
	}

	return FALSE;
}

static gboolean
BC_W(pie)(ExcelChartHandler const *handle,
	  GnumericChartState *s, BiffPut *os)
{
	return FALSE;
}

/****************************************************************************/

static gboolean
BC_R(pieformat)(ExcelChartHandler const *handle,
		ExcelChartReadState *s, BiffQuery *q)
{
	guint16 const percent_diam = MS_OLE_GET_GUINT16 (q->data); /* 0-100 */
	xmlNode *pie;

	g_return_val_if_fail (percent_diam <= 100, TRUE);
	g_return_val_if_fail (s->xml.dataFormat, TRUE);

	pie = e_xml_get_child_by_name (s->xml.dataFormat, (xmlChar *)"Pie");
	if (pie == NULL)
		pie = xmlNewChild (s->xml.dataFormat, s->xml.ns, (xmlChar *)"Pie", NULL);

	/* This is for individual slices */
	if (percent_diam > 0) {
		xmlNode *tmp = xmlNewChild (pie, pie->ns,
			(xmlChar *)"separation_percent_of_radius", NULL);
		xml_node_set_int (tmp, NULL, percent_diam);
	}

	d (2, printf ("Pie slice is %hu %% of diam from center\n", percent_diam););
	return FALSE;
}

static gboolean
BC_W(pieformat)(ExcelChartHandler const *handle,
		GnumericChartState *s, BiffPut *os)
{
	return FALSE;
}

/****************************************************************************/

static gboolean
BC_R(plotarea)(ExcelChartHandler const *handle,
	       ExcelChartReadState *s, BiffQuery *q)
{
	/* Does nothing.  Should always have a 'FRAME' record following */
	return FALSE;
}

static gboolean
BC_W(plotarea)(ExcelChartHandler const *handle,
	       GnumericChartState *s, BiffPut *os)
{
	return FALSE;
}

/****************************************************************************/

static gboolean
BC_R(plotgrowth)(ExcelChartHandler const *handle,
		 ExcelChartReadState *s, BiffQuery *q)
{
	d (2, {
	/* Docs say these are longs
	 * But it appears that only 2 lsb are valid ??
	 */
	gint16 const horiz = MS_OLE_GET_GUINT16 (q->data+2);
	gint16 const vert = MS_OLE_GET_GUINT16 (q->data+6);

	printf ("Scale H=");
	if (horiz != -1)
		printf ("%u", horiz);
	else
		printf ("Unscaled");
	printf (", V=");
	if (vert != -1)
		printf ("%u", vert);
	else
		printf ("Unscaled");
	});
	return FALSE;
}
static gboolean
BC_W(plotgrowth)(ExcelChartHandler const *handle,
		 GnumericChartState *s, BiffPut *os)
{
	return FALSE;
}

/****************************************************************************/

static gboolean
BC_R(pos)(ExcelChartHandler const *handle,
	  ExcelChartReadState *s, BiffQuery *q)
{
	return FALSE;
}

static gboolean
BC_W(pos)(ExcelChartHandler const *handle,
	  GnumericChartState *s, BiffPut *os)
{
	return FALSE;
}

/****************************************************************************/

static gboolean
BC_R(radar)(ExcelChartHandler const *handle,
	    ExcelChartReadState *s, BiffQuery *q)
{
	return FALSE;
}

static gboolean
BC_W(radar)(ExcelChartHandler const *handle,
	    GnumericChartState *s, BiffPut *os)
{
	return FALSE;
}

/****************************************************************************/

static gboolean
BC_R(radararea)(ExcelChartHandler const *handle,
		ExcelChartReadState *s, BiffQuery *q)
{
	return FALSE;
}

static gboolean
BC_W(radararea)(ExcelChartHandler const *handle,
		GnumericChartState *s, BiffPut *os)
{
	return FALSE;
}

/****************************************************************************/

static gboolean
BC_R(sbaseref)(ExcelChartHandler const *handle,
	       ExcelChartReadState *s, BiffQuery *q)
{
	return FALSE;
}

static gboolean
BC_W(sbaseref)(ExcelChartHandler const *handle,
	       GnumericChartState *s, BiffPut *os)
{
	return FALSE;
}

/****************************************************************************/

static gboolean
BC_R(scatter)(ExcelChartHandler const *handle,
	      ExcelChartReadState *s, BiffQuery *q)
{
	xmlNode *fmt = BC_R(store_chartgroup_type)(s, (xmlChar *)"Scatter");

	g_return_val_if_fail (fmt != NULL, TRUE);

	if (s->container.ver >= MS_BIFF_V8) {
		guint16 const flags = MS_OLE_GET_GUINT16 (q->data+4);

		/* Has bubbles */
		if (flags & 0x01) {
			guint16 const size_type = MS_OLE_GET_GUINT16 (q->data+2);
			e_xml_set_bool_prop_by_name (fmt, (xmlChar *)"has_bubbles", TRUE);
			if (!(flags & 0x02))
				xmlNewChild (fmt, fmt->ns, (xmlChar *)"hide_negatives", NULL);
			if (flags & 0x04)
				xmlNewChild (fmt, fmt->ns, (xmlChar *)"in_3d", NULL);

#if 0
			/* huh ? */
			xml_node_set_int (fmt, "percentage_largest_tochart",
					  MS_OLE_GET_GUINT16 (q->data));
#endif
			xmlNewChild (fmt, fmt->ns,
				     (xmlChar *)((size_type == 2)
					     ? "bubble_sized_as_width"
					     : "bubble_sized_as_area"),
				     NULL);
		}
	}

	return FALSE;
}

static gboolean
BC_W(scatter)(ExcelChartHandler const *handle,
	      GnumericChartState *s, BiffPut *os)
{
	return FALSE;
}

/****************************************************************************/

static gboolean
BC_R(serauxerrbar)(ExcelChartHandler const *handle,
		   ExcelChartReadState *s, BiffQuery *q)
{
	return FALSE;
}

static gboolean
BC_W(serauxerrbar)(ExcelChartHandler const *handle,
		   GnumericChartState *s, BiffPut *os)
{
	return FALSE;
}

/****************************************************************************/

static gboolean
BC_R(serfmt)(ExcelChartHandler const *handle,
	     ExcelChartReadState *s, BiffQuery *q)
{
	return FALSE;
}

static gboolean
BC_W(serfmt)(ExcelChartHandler const *handle,
	     GnumericChartState *s, BiffPut *os)
{
	return FALSE;
}

/****************************************************************************/

static void
BC_R(vector_details)(ExcelChartReadState *s, BiffQuery *q, ExcelChartSeries *series,
		     MS_VECTOR_PURPOSE purpose,
		     int type_offset, int count_offset, char const *name)
{
#ifdef ENABLE_BONOBO
	GnmGraphVectorType type;
	guint16 e_type = MS_OLE_GET_GUINT16 (q->data + type_offset);

	switch (e_type) {
	case 0 : type = GNM_VECTOR_DATE;
		 break;

	case 1 : type = GNM_VECTOR_SCALAR;
		 break;

	case 2 : g_warning ("Unsupported vector type 'sequences', converting to scalar");
		 type = GNM_VECTOR_SCALAR;
		 break;
	case 3 : type = GNM_VECTOR_STRING;
		 break;

	default :
		g_warning ("Unsupported vector type '%d', converting to scalar", e_type);
		type = GNM_VECTOR_SCALAR;
	};

	series->vector [purpose].type = type;
	series->vector [purpose].count = MS_OLE_GET_GUINT16 (q->data+count_offset);
	d (0, printf ("%d %s are %s\n",
		series->vector [purpose].count, name,
		gnm_graph_vector_type_name [series->vector [purpose].type]););
#endif
}


static gboolean
BC_R(series)(ExcelChartHandler const *handle,
	     ExcelChartReadState *s, BiffQuery *q)
{
	ExcelChartSeries *series;

	g_return_val_if_fail (s->xml.doc != NULL, TRUE);
	g_return_val_if_fail (s->currentSeries == NULL, TRUE);

	d (2, printf ("SERIES = %d\n", s->series->len););

	series = excel_chart_series_new ();
	series->xml = xmlNewDocNode (s->xml.doc, s->xml.ns, (xmlChar *)"Series", NULL);
	e_xml_set_integer_prop_by_name (series->xml, (xmlChar *)"index", s->series->len);

	/* WARNING : The offsets in the documentation are WRONG.
	 *           Use the sizes instead.
	 */
	BC_R(vector_details) (s, q, series, MS_VECTOR_PURPOSE_CATEGORIES,
			      0, 4, "Categories");
	BC_R(vector_details) (s, q, series, MS_VECTOR_PURPOSE_VALUES,
			      2, 6, "Values");
	if (s->container.ver >= MS_BIFF_V8)
		BC_R(vector_details) (s, q, series, MS_VECTOR_PURPOSE_VALUES,
				      8, 10, "Bubbles");

	g_ptr_array_add (s->series, series);
	s->currentSeries = series;

	return FALSE;
}

static gboolean
BC_W(series)(ExcelChartHandler const *handle,
	     GnumericChartState *s, BiffPut *os)
{
	return FALSE;
}

/****************************************************************************/

static gboolean
BC_R(serieslist)(ExcelChartHandler const *handle,
		 ExcelChartReadState *s, BiffQuery *q)
{
	return FALSE;
}

static gboolean
BC_W(serieslist)(ExcelChartHandler const *handle,
		 GnumericChartState *s, BiffPut *os)
{
	return FALSE;
}

/****************************************************************************/

static gboolean
BC_R(seriestext)(ExcelChartHandler const *handle,
		 ExcelChartReadState *s, BiffQuery *q)
{
	guint16 const id = MS_OLE_GET_GUINT16 (q->data);	/* must be 0 */
	int const slen = MS_OLE_GET_GUINT8 (q->data + 2);
	char *str;

	g_return_val_if_fail (id == 0, FALSE);

	str = biff_get_text (q->data + 3, slen, NULL);
	d (2, puts (str););

	/* A quick heuristic */
	if (s->currentSeries != NULL &&
	    s->currentSeries->vector [MS_VECTOR_PURPOSE_LABELS].remote_ID == -1) {
#ifdef ENABLE_BONOBO
		s->currentSeries->vector [MS_VECTOR_PURPOSE_LABELS].type = GNM_VECTOR_STRING;

		s->currentSeries->vector [MS_VECTOR_PURPOSE_LABELS].remote_ID =
			gnm_graph_add_vector (s->graph,
				expr_tree_new_constant (value_new_string (str)),
				GNM_VECTOR_STRING,
				ms_container_sheet (s->parent));
#endif
	}

	/* TODO : handle axis and chart titles */

	g_free (str);

	return FALSE;
}

static gboolean
BC_W(seriestext)(ExcelChartHandler const *handle,
		 GnumericChartState *s, BiffPut *os)
{
	return FALSE;
}

/****************************************************************************/

static gboolean
BC_R(serparent)(ExcelChartHandler const *handle,
		ExcelChartReadState *s, BiffQuery *q)
{
	return FALSE;
}

static gboolean
BC_W(serparent)(ExcelChartHandler const *handle,
		GnumericChartState *s, BiffPut *os)
{
	return FALSE;
}

/****************************************************************************/

static gboolean
BC_R(sertocrt)(ExcelChartHandler const *handle,
	       ExcelChartReadState *s, BiffQuery *q)
{
	guint16 const index = MS_OLE_GET_GUINT16 (q->data);

	g_return_val_if_fail (s->currentSeries != NULL, FALSE);

	s->currentSeries->chart_group = index;

	d (1, printf ("Series chart group index is %hd\n", index););
	return FALSE;
}

static gboolean
BC_W(sertocrt)(ExcelChartHandler const *handle,
	       GnumericChartState *s, BiffPut *os)
{
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
BC_R(shtprops)(ExcelChartHandler const *handle,
	       ExcelChartReadState *s, BiffQuery *q)
{
	guint16 const flags = MS_OLE_GET_GUINT16 (q->data);
	guint8 const tmp = MS_OLE_GET_GUINT16 (q->data+2);
	gboolean const manual_format		= (flags&0x01) ? TRUE : FALSE;
	gboolean const only_plot_visible_cells	= (flags&0x02) ? TRUE : FALSE;
	gboolean const dont_size_with_window	= (flags&0x04) ? TRUE : FALSE;
	gboolean const has_pos_record		= (flags&0x08) ? TRUE : FALSE;
	gboolean ignore_pos_record = FALSE;
	MS_CHART_BLANK blanks;

	g_return_val_if_fail (tmp < MS_CHART_BLANK_MAX, TRUE);
	blanks = tmp;
	d (2, puts (ms_chart_blank[blanks]););

	if (s->container.ver >= MS_BIFF_V8) {
		ignore_pos_record = (flags&0x10) ? TRUE : FALSE;
	}
	d (1, {
	printf ("%sesize chart with window.\n",
		dont_size_with_window ? "Don't r": "R");

	if (has_pos_record && !ignore_pos_record)
		printf ("There should be a POS record around here soon\n");

	if (manual_format);
		printf ("Manually formated\n");
	if (only_plot_visible_cells);
		printf ("Only plot visible (to whom?) cells\n");
	});
	return FALSE;
}

static gboolean
BC_W(shtprops)(ExcelChartHandler const *handle,
	       GnumericChartState *s, BiffPut *os)
{
	return FALSE;
}

/****************************************************************************/

static gboolean
BC_R(siindex)(ExcelChartHandler const *handle,
	      ExcelChartReadState *s, BiffQuery *q)
{
	d (1, {
	/* UNDOCUMENTED : Docs says this is long
	 * Biff record is only length 2
	 */
	gint16 const index = MS_OLE_GET_GUINT16 (q->data);
	printf ("Series %d is %hd\n", s->series->len, index);});
	return FALSE;
}
static gboolean
BC_W(siindex)(ExcelChartHandler const *handle,
	      GnumericChartState *s, BiffPut *os)
{
	return FALSE;
}

/****************************************************************************/

static gboolean
BC_R(surf)(ExcelChartHandler const *handle,
	   ExcelChartReadState *s, BiffQuery *q)
{
	return FALSE;
}

static gboolean
BC_W(surf)(ExcelChartHandler const *handle,
	   GnumericChartState *s, BiffPut *os)
{
	return FALSE;
}

/****************************************************************************/

static gboolean
BC_R(text)(ExcelChartHandler const *handle,
	   ExcelChartReadState *s, BiffQuery *q)
{
	if (s->prev_opcode == BIFF_CHART_defaulttext) {
		d (4, puts ("Text follows defaulttext"););
	} else {
	}

#if 0
case BIFF_CHART_chart :
	puts ("Text follows chart");
	break;
case BIFF_CHART_legend :
	puts ("Text follows legend");
	break;
default :
	printf ("BIFF ERROR : A Text record follows a %x\n",
		s->prev_opcode);

};
#endif
return FALSE;
}

static gboolean
BC_W(text)(ExcelChartHandler const *handle,
	   GnumericChartState *s, BiffPut *os)
{
	return FALSE;
}

/****************************************************************************/

static gboolean
BC_R(tick)(ExcelChartHandler const *handle,
	   ExcelChartReadState *s, BiffQuery *q)
{
	d (1, {
	guint16 const major_type = MS_OLE_GET_GUINT8 (q->data);
	guint16 const minor_type = MS_OLE_GET_GUINT8 (q->data+1);
	guint16 const position   = MS_OLE_GET_GUINT8 (q->data+2);

	guint16 const flags = MS_OLE_GET_GUINT8 (q->data+24);

	switch (major_type) {
	case 0: puts ("no major tick;"); break;
	case 1: puts ("major tick inside axis;"); break;
	case 2: puts ("major tick outside axis;"); break;
	case 3: puts ("major tick across axis;"); break;
	default : puts ("unknown major tick type");
	};
	switch (minor_type) {
	case 0: puts ("no minor tick;"); break;
	case 1: puts ("minor tick inside axis;"); break;
	case 2: puts ("minor tick outside axis;"); break;
	case 3: puts ("minor tick across axis;"); break;
	default : puts ("unknown minor tick type");
	};
	switch (position) {
	case 0: puts ("no tick label;"); break;
	case 1: puts ("tick label at low end;"); break;
	case 2: puts ("tick label at high end;"); break;
	case 3: puts ("tick label near axis;"); break;
	default : puts ("unknown tick label position");
	};

	/*
	if (flags&0x01)
		puts ("Auto tick label colour");
	else
		BC_R(color) (q->data+4, "LabelColour", tick, FALSE);
	*/

	if (flags&0x02)
		puts ("Auto text background mode");
	else
		printf ("background mode = %d\n", (unsigned)MS_OLE_GET_GUINT8 (q->data+3));

	switch (flags&0x1c) {
	case 0: puts ("no rotation;"); break;
	case 1: puts ("top to bottom letters upright;"); break;
	case 2: puts ("rotate 90deg counter-clockwise;"); break;
	case 3: puts ("rotate 90deg clockwise;"); break;
	default : puts ("unknown rotation");
	};

	if (flags&0x20)
		puts ("Auto rotate");
	});

#if 0
	/* Ignore the colour indicies.  Use the colours themselves
	 * to avoid problems with guessing the strange index values
	 */
	if (s->container.ver >= MS_BIFF_V8) {
		guint16 const index = MS_OLE_GET_GUINT16 (q->data+26);
		ms_excel_palette_get (s->wb->palette, index);
	}
#endif
	return FALSE;
}

static gboolean
BC_W(tick)(ExcelChartHandler const *handle,
	   GnumericChartState *s, BiffPut *os)
{
	return FALSE;
}

/****************************************************************************/

static gboolean
BC_R(units)(ExcelChartHandler const *handle,
	    ExcelChartReadState *s, BiffQuery *q)
{
	/* Irrelevant */
	guint16 const type = MS_OLE_GET_GUINT16 (q->data);
	g_return_val_if_fail(type == 0, TRUE);

	return FALSE;
}
static gboolean
BC_W(units)(ExcelChartHandler const *handle,
	    GnumericChartState *s, BiffPut *os)
{
	g_warning("Should not write BIFF_CHART_UNITS");
	return FALSE;
}

/****************************************************************************/


static gboolean
conditional_get_double (gboolean flag, guint8 const *data,
			gchar const *name)
{
	if (!flag) {
		double const val = gnumeric_get_le_double (data);
		d (1, printf ("%s = %f\n", name, val););
		return TRUE;
	}
	d (1, printf ("%s = Auto\n", name););
	return FALSE;
}

static gboolean
BC_R(valuerange)(ExcelChartHandler const *handle,
		 ExcelChartReadState *s, BiffQuery *q)
{
	guint16 const flags = gnumeric_get_le_double (q->data+40);

	conditional_get_double (flags&0x01, q->data+ 0, "Min Value");
	conditional_get_double (flags&0x02, q->data+ 8, "Max Value");
	conditional_get_double (flags&0x04, q->data+16, "Major Increment");
	conditional_get_double (flags&0x08, q->data+24, "Minor Increment");
	conditional_get_double (flags&0x10, q->data+32, "Cross over point");

	d (1, {
	if (flags&0x20)
		puts ("Log scaled");
	if (flags&0x40)
		puts ("Values in reverse order");
	if (flags&0x80)
		puts ("Cross over at max value");
	});

	return FALSE;
}

static gboolean
BC_W(valuerange)(ExcelChartHandler const *handle,
		 GnumericChartState *s, BiffPut *os)
{
	return FALSE;
}

/****************************************************************************/

static gboolean
BC_R(end)(ExcelChartHandler const *handle,
	  ExcelChartReadState *s, BiffQuery *q)
{
	int popped_state;

	d (0, puts ("}"););

	g_return_val_if_fail (s->stack != NULL, TRUE);
	g_return_val_if_fail (s->stack->len > 0, TRUE);

	popped_state = BC_R(top_state) (s);
	s->stack = g_array_remove_index_fast (s->stack, s->stack->len-1);

	switch (popped_state) {
	case BIFF_CHART_series :
		g_return_val_if_fail (s->currentSeries != NULL, TRUE);
		s->currentSeries = NULL;
		break;

	case BIFF_CHART_chartformat : {
		unsigned i;
		xmlNode * data;
		ExcelChartSeries *series;

		g_return_val_if_fail (s->xml.currentChartGroup != NULL, TRUE);

		data = xmlNewChild (s->xml.currentChartGroup, s->xml.ns, (xmlChar *)"Data", NULL);
		for (i = 0 ; i < s->series->len; i++ ) {
			series = g_ptr_array_index (s->series, i);

			if (series->chart_group != s->plot_counter)
				continue;
			excel_chart_series_write_xml (series, s, data);
		}

		s->xml.currentChartGroup = NULL;
		break;
	}

	case BIFF_CHART_dataformat : {
		g_return_val_if_fail (s->xml.dataFormat != NULL, TRUE);
		s->xml.dataFormat = NULL;
		break;
	}

	default :
		break;
	};
	return FALSE;
}

static gboolean
BC_W(end)(ExcelChartHandler const *handle,
	  GnumericChartState *s, BiffPut *os)
{
	return FALSE;
}

/****************************************************************************/

static gboolean
BC_R(serauxtrend)(ExcelChartHandler const *handle,
		  ExcelChartReadState *s, BiffQuery *q)
{
	return FALSE;
}

static gboolean
BC_W(serauxtrend)(ExcelChartHandler const *handle,
		  GnumericChartState *s, BiffPut *os)
{
	return FALSE;
}

/****************************************************************************/

static ExcelChartHandler const *chart_biff_handler[128];

static void
BC(register_handler)(ExcelChartHandler const *const handle);
#define BIFF_CHART(name, size) \
{	static ExcelChartHandler const handle = { \
	BIFF_CHART_ ## name, size, #name, & BC_R(name), & BC_W(name) }; \
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
	i = sizeof(chart_biff_handler) / sizeof(ExcelChartHandler *);
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
BC(register_handler)(ExcelChartHandler const *const handle)
{
	unsigned const num_handler = sizeof(chart_biff_handler) /
		sizeof(ExcelChartHandler *);

	guint32 num = handle->opcode & 0xff;

	if (num >= num_handler)
		printf ("Invalid BIFF_CHART handler (%x)\n", handle->opcode);
	else if (chart_biff_handler[num])
		printf ("Multiple BIFF_CHART handlers for (%x)\n",
			handle->opcode);
	else
		chart_biff_handler[num] = handle;
}

static gboolean
chart_realize_obj (MSContainer *container, MSObj *obj)
{
	return FALSE;
}

static GObject *
chart_create_obj  (MSContainer *container, MSObj *obj)
{
	return NULL;
}

static ExprTree  *
chart_parse_expr  (MSContainer *container, guint8 const *data, int length)
{
	return NULL;
}

static StyleFormat *
chart_get_fmt (MSContainer const *container, guint16 indx)
{
	return ms_container_get_fmt (container->parent_container, indx);
}

gboolean
ms_excel_chart (BiffQuery *q, MSContainer *container, MsBiffVersion ver, GObject *graph)
{
	static MSContainerClass const vtbl = {
		chart_realize_obj,
		chart_create_obj,
		chart_parse_expr,
		NULL,
		chart_get_fmt
	};
	int const num_handler = sizeof(chart_biff_handler) /
		sizeof(ExcelChartHandler *);

	int i;
	gboolean done = FALSE;
	ExcelChartReadState state;

	/* Register the handlers if this is the 1st time through */
	BC(register_handlers)();

	/* FIXME : create an anchor parser for charts */
	ms_container_init (&state.container, &vtbl, container);

	state.container.ver = ver;
	state.stack	    = g_array_new (FALSE, FALSE, sizeof(int));
	state.prev_opcode   = 0xdeadbeef; /* Invalid */
	state.parent	    = container;
	state.currentSeries = NULL;
	state.series	    = g_ptr_array_new ();
	state.plot_counter  = -1;
	state.xml.doc       = xmlNewDoc ((xmlChar *)"1.0");
	state.xml.doc->xmlRootNode =
		xmlNewDocNode (state.xml.doc, NULL, (xmlChar *)"Graph", NULL);
	state.xml.ns        = xmlNewNs (state.xml.doc->xmlRootNode,
		(xmlChar *)"http://www.gnumeric.org/graph_v1",
		(xmlChar *)"graph");
	state.xml.plots = xmlNewChild (state.xml.doc->xmlRootNode,
				       state.xml.ns, (xmlChar *)"Plots", NULL);
	state.xml.currentChartGroup = NULL;
	state.xml.dataFormat = NULL;

#ifdef ENABLE_BONOBO
	if (graph != NULL)
		state.graph = GNUMERIC_GRAPH (graph);
	else
#endif
		state.graph = NULL;

	d (0, puts ("{ CHART"););

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
				d (0, {	printf ("Unknown BIFF_CHART record\n");
					dump_biff (q);});
			} else {
				ExcelChartHandler const *const h =
					chart_biff_handler [lsb];

				if (state.graph	!= NULL) {
					d (0, { if (!begin_end)
							printf ("%s(\n", h->name); });
					(void)(*h->read_fn)(h, &state, q);
					d (0, { if (!begin_end)
							printf (");\n"); });
				}
			}
		} else {
			switch (lsb) {
			case BIFF_EOF:
				done = TRUE;
				d (0, puts ("}; /* CHART */"););
				g_return_val_if_fail(state.stack->len == 0, TRUE);
				break;

			case BIFF_PROTECT : {
				gboolean const protected =
					(1 == MS_OLE_GET_GUINT16 (q->data));
				d (4, printf ("Chart is%s protected;\n",
					     protected ? "" : " not"););
				break;
			}

			case BIFF_NUMBER: {
				double val;
				val = gnumeric_get_le_double (q->data + 6);
				/* Figure out how to assign these back to the series,
				 * are they just sequential ?
				 */
				d (10, printf ("%f\n", val););
				break;
			}

			case BIFF_LABEL : {
				guint16 row = MS_OLE_GET_GUINT16 (q->data + 0);
				guint16 col = MS_OLE_GET_GUINT16 (q->data + 2);
				guint16 xf  = MS_OLE_GET_GUINT16 (q->data + 4);
				guint16 len = MS_OLE_GET_GUINT16 (q->data + 6);
				char *label = biff_get_text (q->data + 8, len, NULL);
				d (10, {puts (label);
					printf ("hmm, what are these values for a chart ???\n"
						"row = %d, col = %d, xf = %d\n", row, col, xf);});
				g_free (label);
				break;
			}

			case BIFF_MS_O_DRAWING:
				ms_escher_parse (q, &state.container);
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
				d (8, printf ("Handled biff %x in chart;\n",
					     q->opcode););
				break;

			case BIFF_PRINTSIZE: {
#if 0
				/* Undocumented, seems like an enum ??? */
				gint16 const v = MS_OLE_GET_GUINT16 (q->data);
#endif
			}
			break;

			default :
				ms_excel_unexpected_biff (q, "Chart", ms_excel_chart_debug);
			};
		}
		state.prev_opcode = q->opcode;
	}

#ifdef ENABLE_BONOBO
	if (state.graph != NULL)
		gnm_graph_import_specification (state.graph, state.xml.doc);
#endif

	/* Cleanup */
	xmlFreeDoc (state.xml.doc);
	for (i = state.series->len; i-- > 0 ; ) {
		ExcelChartSeries *series = g_ptr_array_index (state.series, i);
		if (series != NULL)
			excel_chart_series_delete (series);
	}
	g_ptr_array_free (state.series, TRUE);
	ms_container_finalize (&state.container);

	return FALSE;
}

gboolean
ms_excel_read_chart (BiffQuery *q, MSContainer *container, GObject *graph)
{
	MsBiffBofData *bof;
	gboolean res = TRUE;

	/* 1st record must be a valid BOF record */
	g_return_val_if_fail (ms_biff_query_next (q), TRUE);
	bof = ms_biff_bof_data_new (q);

	g_return_val_if_fail (bof != NULL, TRUE);
	g_return_val_if_fail (bof->type == MS_BIFF_TYPE_Chart, TRUE);

	if (bof->version != MS_BIFF_V_UNKNOWN)
		res = ms_excel_chart (q, container, bof->version, graph);
	ms_biff_bof_data_destroy (bof);
	return res;
}
