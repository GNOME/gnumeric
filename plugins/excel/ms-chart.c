/**
 * ms-chart.c: MS Excel chart support for Gnumeric
 *
 * Author:
 *    Jody Goldberg (jgoldberg@home.com)
 *
 * (C) 1999 Jody Goldberg
 **/

#include <stdio.h>

#include <config.h>
#include "excel.h"
#include "ms-chart.h"
#include "ms-formula-read.h"
#include "ms-excel-read.h"
#include "ms-escher.h"
#include "utils.h"
#include "gnumeric-chart.h"

/* #define NO_DEBUG_EXCEL */
extern int ms_excel_chart_debug;

typedef struct
{
	int		 depth;
	eBiff_version	 ver;
	guint32		 prev_opcode;
	ExcelWorkbook	*wb;

	GnumericChart	*chart;
} ExcelChartState;

typedef struct
{
	GnumericChart	*chart;
} GnumericChartState;

typedef struct biff_chart_handler ExcelChartHandler;
typedef gboolean (*ExcelChartReader)(ExcelChartHandler const *handle,
				     ExcelChartState *, BiffQuery *q);
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

static StyleColor *
BC_R(color)(guint8 const *data, char *type)
{
	guint32 const rgb = MS_OLE_GET_GUINT32 (data);
	guint16 const r = (rgb >>  0) & 0xff;
	guint16 const g = (rgb >>  8) & 0xff;
	guint16 const b = (rgb >> 16) & 0xff;

	printf("%s Color %02x%02x%02x\n", type, r, g, b);

	return style_color_new ((r<<8)|r, (g<<8)|g, (b<<8)|b);
}

/****************************************************************************/

static gboolean
BC_R(3dbarshape)(ExcelChartHandler const *handle,
		 ExcelChartState *s, BiffQuery *q)
{
	/*
	 * All the charts I've seen have this record with value 0x0000
	 * its probably an enum of sorts.
	 */
	guint16 const type = MS_OLE_GET_GUINT16 (q->data);
	printf ("shape %d\n", type);

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
	 ExcelChartState *s, BiffQuery *q)
{
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
	 ExcelChartState *s, BiffQuery *q)
{
	guint8 const id = MS_OLE_GET_GUINT8 (q->data);
	guint8 const rt = MS_OLE_GET_GUINT8 (q->data + 1);
	guint16 const flags = MS_OLE_GET_GUINT16 (q->data + 2);
	guint16 const fmt_index = MS_OLE_GET_GUINT16 (q->data + 4);
	guint16 const length = MS_OLE_GET_GUINT16 (q->data + 6);
	StyleFormat * fmt = biff_format_data_lookup (s->wb, fmt_index);

	if (fmt != NULL)
		printf ("Format = '%s';\n", fmt->format);

	switch (id) {
	case 0 : puts ("Linking title or text"); break;
	case 1 : puts ("Linking values"); break;
	case 2 : puts ("Linking categories"); break;

	/* This is undocumented, but makes sense */
	case 3 : puts ("Linking bubbles"); break;
	default :
		 printf ("Unknown link type(%x)\n", id);
	};
	switch (rt) {
	case 0 : puts ("Use default categories"); break;
	case 1 : puts ("Text/Value entered directly"); break;
	case 2 : puts ("Linked to Worksheet"); break;
	default :
		 printf ("UKNOWN :data source (%x)\n", id);
	};

	{
		/* Simulate a sheet */
		ExcelSheet sheet;
		ExprTree *expr;
		sheet.ver = s->ver;
		sheet.wb = s->wb;
		sheet.gnum_sheet = NULL;
		sheet.shared_formulae = NULL;

		if (length > 0) {
			expr = ms_excel_parse_formula (s->wb, &sheet, q->data+8, 0, 0,
						       FALSE, length, NULL);
			if (ms_excel_chart_debug > 2)
				expr_dump_tree (expr);
		}
	}

	/* Rest are 0 */
	if (flags&0x01)
		puts ("Has Custom number format");
	else
		puts ("Uses number format from data source");
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
	     ExcelChartState *s, BiffQuery *q)
{
	gint16 length = MS_OLE_GET_GUINT16 (q->data);
	guint8 const *in = (q->data + 2);
	char *const ans = (char *) g_new (char, length + 2);
	char *out = ans;

	for (; --length >= 0 ; in+=4, ++out)
	{
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

	puts (ans);
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
	   ExcelChartState *s, BiffQuery *q)
{
	guint16 const flags = MS_OLE_GET_GUINT16 (q->data);
	gboolean const stacked = (flags & 0x01) ? TRUE : FALSE;
	gboolean const as_percentage = (flags & 0x02) ? TRUE : FALSE;

	if (as_percentage)
		/* TODO : test theory that percentage implies stacked */
		printf ("Stacked Percentage. (%d should be TRUE)\n", stacked);
	else if (stacked)
		printf ("Stacked Percentage values\n");
	else
		printf ("Overlayed values\n");

	if (s->ver >= eBiffV8)
	{
		gboolean const has_shadow = (flags & 0x04) ? TRUE : FALSE;
		if (has_shadow)
			puts ("in 3D");
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
		 ExcelChartState *s, BiffQuery *q)
{
#if 0
	StyleColor *fore = BC_R(color) (q->data, "Area Fore");
	StyleColor *back = BC_R(color) (q->data+4, "Area Back");
	guint16 const pattern = MS_OLE_GET_GUINT16 (q->data+8);
	guint16 const flags = MS_OLE_GET_GUINT16 (q->data+10);
	gboolean const auto_format = (flags & 0x01) ? TRUE : FALSE;
	gboolean const swap_color_for_negative = flags & 0x02;
#endif

	if (s->ver >= eBiffV8)
	{
		guint16 const fore_index = MS_OLE_GET_GUINT16 (q->data+12);
		guint16 const back_index = MS_OLE_GET_GUINT16 (q->data+14);

		/* TODO : Ignore result for now,
		 * Which to use, fore and back, or these ? */
		ms_excel_palette_get (s->wb->palette, fore_index, NULL);
		ms_excel_palette_get (s->wb->palette, back_index, NULL);
	}
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
		    ExcelChartState *s, BiffQuery *q)
{
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

	if (s->ver >= eBiffV8)
	{
		gboolean const show_bubble_size = (flags&0x20) ? TRUE : FALSE;
		if (show_bubble_size)
			puts ("Show bubble size");
	}
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
	       ExcelChartState *s, BiffQuery *q)
{
	guint16 const num_axis = MS_OLE_GET_GUINT16 (q->data);
	g_return_val_if_fail(1 <= num_axis && num_axis <= 2, TRUE);
	printf ("There are %hu axis.\n", num_axis);
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
	   ExcelChartState *s, BiffQuery *q)
{
	guint16 const axis_type = MS_OLE_GET_GUINT16 (q->data);
	MS_AXIS atype;
	g_return_val_if_fail (axis_type < MS_AXIS_MAX, TRUE);
	atype = axis_type;
	printf ("This is a %s .\n", ms_axis[atype]);
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
	     ExcelChartState *s, BiffQuery *q)
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
		     ExcelChartState *s, BiffQuery *q)
{
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
		 ExcelChartState *s, BiffQuery *q)
{
	guint16 const index = MS_OLE_GET_GUINT16 (q->data);	/* 1 or 2 */
	/* Measured in 1/4000ths of the chart width */
	guint32 const x = MS_OLE_GET_GUINT32 (q->data+2);
	guint32 const y = MS_OLE_GET_GUINT32 (q->data+6);
	guint32 const width = MS_OLE_GET_GUINT32 (q->data+10);
	guint32 const height = MS_OLE_GET_GUINT32 (q->data+14);

	printf ("Axis # %hu @ %f,%f, X=%f, Y=%f\n",
		index, x/4000., y/4000., width/4000., height/4000.);
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
	  ExcelChartState *s, BiffQuery *q)
{
	/* percent of bar width */
	guint16 const space_between_bar = MS_OLE_GET_GUINT16(q->data);
	guint16 const space_between_categories = MS_OLE_GET_GUINT16(q->data+2);

	guint16 const flags = MS_OLE_GET_GUINT16(q->data+4);

	gboolean const horizontal_bar = (flags&0x01) ? TRUE : FALSE;
	gboolean const stacked = (flags&0x02) ? TRUE : FALSE;
	gboolean const as_percentage = (flags&0x04) ? TRUE : FALSE;

	printf ( (horizontal_bar) ? "Horizontal " : "Vertical ");
	if (as_percentage)
		/* TODO : test theory that percentage implies stacked */
		printf ("Stacked Percentage. (%d should be TRUE)\n", stacked);
	else if (stacked)
		printf ("Stacked Percentage values\n");
	else
		printf ("Overlayed values\n");

	printf ("Space between bars = %d %% of width\n",
		space_between_bar);
	printf ("Space between categories = %d %% of width\n",
		space_between_categories);
	if (s->ver >= eBiffV8)
	{
		gboolean const has_shadow = (flags & 0x04) ? TRUE : FALSE;
		if (has_shadow)
			puts ("in 3D");
	}
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
	    ExcelChartState *s, BiffQuery *q)
{
	puts ("{");
	++(s->depth);
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
	     ExcelChartState *s, BiffQuery *q)
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
		   ExcelChartState *s, BiffQuery *q)
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
		  ExcelChartState *s, BiffQuery *q)
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
	    ExcelChartState *s, BiffQuery *q)
{
	/* TODO TODO TODO : Why are all charts listed as starting at 0,0 ?? */
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
		  ExcelChartState *s, BiffQuery *q)
{
	guint16 const flags = MS_OLE_GET_GUINT16 (q->data+16);
	guint16 const z_order = MS_OLE_GET_GUINT16 (q->data+18);
	gboolean const vary_color = (flags&0x01) ? TRUE : FALSE;

	printf ("Z value = %uh\n", z_order);
	if (vary_color)
		printf ("Vary color of every data point\n");
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
		      ExcelChartState *s, BiffQuery *q)
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
		ExcelChartState *s, BiffQuery *q)
{
	guint16 const type = MS_OLE_GET_GUINT16 (q->data);
	g_return_val_if_fail (type <= 2, FALSE);
	printf ("Use %s lines\n",
		(type == 0) ? "drop" : ((type == 1) ? "hi-lo" : "series"));
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
		 ExcelChartState *s, BiffQuery *q)
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
	  ExcelChartState *s, BiffQuery *q)
{
	gint16 const flags = MS_OLE_GET_GUINT16 (q->data);
	gboolean const horiz_border = (flags&0x01) ? TRUE : FALSE;
	gboolean const vert_border = (flags&0x02) ? TRUE : FALSE;
	gboolean const border = (flags&0x04) ? TRUE : FALSE;
	gboolean const series_keys = (flags&0x08) ? TRUE : FALSE;
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
		 ExcelChartState *s, BiffQuery *q)
{
	guint16 const pt_num = MS_OLE_GET_GUINT16 (q->data);
	guint16 const series_index = MS_OLE_GET_GUINT16 (q->data+2);
	guint16 const series_index_for_label = MS_OLE_GET_GUINT16 (q->data+4);
	guint16 const excel4_auto_color = MS_OLE_GET_GUINT16 (q->data+6) & 0x01;

	if (pt_num == 0xffff)
		printf ("All points");
	else
		printf ("Point-%hd", pt_num);

	printf (", series=%hd\n", series_index);
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
		  ExcelChartState *s, BiffQuery *q)
{
	guint16	const tmp = MS_OLE_GET_GUINT16 (q->data);
	printf ("applicability = %hd\n", tmp);

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
	      ExcelChartState *s, BiffQuery *q)
{
	guint16 const width = MS_OLE_GET_GUINT16 (q->data);	/* 0-100 */
	g_return_val_if_fail (width <= 100, FALSE);
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
	  ExcelChartState *s, BiffQuery *q)
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

	printf ("Font %hu (%hu x %hu) scale=%hu, height=%hu\n",
		index, x_basis, y_basis, scale_basis, applied_height);
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
	    ExcelChartState *s, BiffQuery *q)
{
	/* Child of TEXT, index into FONT table */
	guint16 const font = MS_OLE_GET_GUINT16 (q->data);
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
	    ExcelChartState *s, BiffQuery *q)
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
	       ExcelChartState *s, BiffQuery *q)
{
	ms_escher_parse (q, s->wb, NULL);
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
	   ExcelChartState *s, BiffQuery *q)
{
	guint16 const fmt_index = MS_OLE_GET_GUINT16 (q->data);
	StyleFormat * fmt = biff_format_data_lookup (s->wb, fmt_index);

	if (fmt != NULL)
		printf ("Format = '%s';\n", fmt->format);
	return FALSE;
}

static gboolean
BC_W(ifmt)(ExcelChartHandler const *handle,
	   GnumericChartState *s, BiffPut *os)
{
	return FALSE;
}

/****************************************************************************/

typedef enum
{
	MS_LEGEND_LOCATION_BOTTOM	= 0,
	MS_LEGEND_LOCATION_CORNER	= 1,
	MS_LEGEND_LOCATION_TOP		= 2,
	MS_LEGEND_LOCATION_LEFT		= 3,
	MS_LEGEND_LOCATION_RIGHT	= 4,
	MS_LEGEND_LOCATION_invalid1	= 5,
	MS_LEGEND_LOCATION_invalid2	= 6,
	MS_LEGEND_LOCATION_NOT_DOCKED	= 7,
	MS_LEGEND_LOCATION_MAX		= 8
} MS_LEGEND_LOCATION;
static char const *const ms_legend_location[] =
{
	"bottom", "corner", "top", "left", "right",
	"IMPOSSIBLE1", "IMPOSSIBLE2", "Not docked"
};

static gboolean
BC_R(legend)(ExcelChartHandler const *handle,
	     ExcelChartState *s, BiffQuery *q)
{
	/* Measured in 1/4000ths of the chart width */
	guint32 const x_pos = MS_OLE_GET_GUINT32  (q->data);
	guint32 const y_pos = MS_OLE_GET_GUINT32  (q->data+4);
	guint32 const width = MS_OLE_GET_GUINT32  (q->data+8);
	guint32 const height = MS_OLE_GET_GUINT32 (q->data+12);
	guint8 const tmp = MS_OLE_GET_GUINT8     (q->data+16);
	guint8 const spacing = MS_OLE_GET_GUINT8  (q->data+17);
	guint16 const flags = MS_OLE_GET_GUINT16  (q->data+18);

	MS_LEGEND_LOCATION location;
	g_return_val_if_fail (tmp < MS_LEGEND_LOCATION_MAX &&
			      tmp != MS_LEGEND_LOCATION_invalid1 &&
			      tmp != MS_LEGEND_LOCATION_invalid2, TRUE);

	/* TODO  : What is pupose of this flag ?? */
	location = tmp;
	printf ("Legend is @ %s.\n", ms_legend_location[location]);

	printf ("Legend @ %f,%f, X=%f, Y=%f\n",
		x_pos/4000., y_pos/4000., width/4000., height/4000.);

	/* FIXME : Parse the flags too */
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
	       ExcelChartState *s, BiffQuery *q)
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
	   ExcelChartState *s, BiffQuery *q)
{
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
		 ExcelChartState *s, BiffQuery *q)
{
	StyleColor *color = BC_R(color) (q->data, "Line");
	guint16 const pattern = MS_OLE_GET_GUINT16 (q->data+4);
	gint16 const weight = MS_OLE_GET_GUINT16 (q->data+6);
	guint16 const flags = MS_OLE_GET_GUINT16 (q->data+8);
	gboolean	auto_format, draw_ticks;
	MS_LINE_PATTERN pat;
	MS_LINE_WGT	wgt;

	g_return_val_if_fail (pattern < MS_LINE_PATTERN_MAX, TRUE);
	pat = pattern;
	printf ("Lines have a %s pattern.\n", ms_line_pattern[pat]);

	g_return_val_if_fail (weight < MS_LINE_WGT_MAX, TRUE);
	g_return_val_if_fail (weight > MS_LINE_WGT_MIN, TRUE);
	wgt = weight;
	printf ("Lines are %s wide.\n", ms_line_wgt[wgt+1]);

	auto_format = (flags & 0x01) ? TRUE : FALSE;
	draw_ticks = (flags & 0x04) ? TRUE : FALSE;

	if (s->ver >= eBiffV8)
	{
		guint16 const color_index = MS_OLE_GET_GUINT16 (q->data+10);

		/* Ignore result for now */
		ms_excel_palette_get (s->wb->palette, color_index, NULL);
	}
	return FALSE;
}

static gboolean
BC_W(lineformat)(ExcelChartHandler const *handle,
		 GnumericChartState *s, BiffPut *os)
{
	return FALSE;
}

/****************************************************************************/

typedef enum
{
	MS_CHART_MARKER_NONE	= 0,
	MS_CHART_MARKER_SQUARE	= 1,
	MS_CHART_MARKER_DIAMOND	= 2,
	MS_CHART_MARKER_TRIANGLE= 3,
	MS_CHART_MARKER_X	= 4,
	MS_CHART_MARKER_STAR	= 5,
	MS_CHART_MARKER_DOW	= 6,
	MS_CHART_MARKER_STD	= 7,
	MS_CHART_MARKER_CIRCLE	= 8,
	MS_CHART_MARKER_PLUS	= 9,
	MS_CHART_MARKER_MAX	= 10
} MS_CHART_MARKER;
static char const *const ms_chart_marker[] =
{
	"not marked", "marked with squares", "marked with diamonds",
	"marked with triangle", "marked with an x", "marked with star",
	"marked with a dow symbol", "marked with a std", "marked with a circle",
	"marked with a plus"
};

static gboolean
BC_R(markerformat)(ExcelChartHandler const *handle,
		   ExcelChartState *s, BiffQuery *q)
{
	StyleColor *fore = BC_R(color) (q->data, "MarkerFore");
	StyleColor *back = BC_R(color) (q->data+4, "MarkerBack");
	guint16 const tmp = MS_OLE_GET_GUINT16 (q->data+8);
	guint16 const flags = MS_OLE_GET_GUINT16 (q->data+10);
	gboolean const auto_color = (flags & 0x01) ? TRUE : FALSE;
	gboolean const no_fore	= (flags & 0x10) ? TRUE : FALSE;
	gboolean const no_back = (flags & 0x20) ? TRUE : FALSE;
	MS_CHART_MARKER marker;

	g_return_val_if_fail (tmp < MS_CHART_MARKER_MAX, TRUE);
	marker = tmp;
	printf ("Points are %s\n", ms_chart_marker[marker]);

	if (auto_color)
		printf ("Ignore the specified colors do it ourselves\n");
	if (no_fore)
		printf ("Transparent borders\n");
	if (no_fore)
		printf ("Transparent interior\n");

	if (s->ver >= eBiffV8)
	{
		/* What are these for ?
		 * We already have the colors ?
		 */
		StyleColor const * marker_border =
		    ms_excel_palette_get (s->wb->palette, 
					  MS_OLE_GET_GUINT16 (q->data+12),
					  NULL);
		StyleColor const * marker_fill =
		    ms_excel_palette_get (s->wb->palette, 
					  MS_OLE_GET_GUINT16 (q->data+14),
					  NULL);
		guint32 const marker_size = MS_OLE_GET_GUINT32 (q->data+16);

		printf ("Marker is %u, with border %x and interior %x\n",
			marker_size,
			(guint32)marker_border, (guint32)marker_fill);
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
		 ExcelChartState *s, BiffQuery *q)
{
	guint16 const link_type = MS_OLE_GET_GUINT16 (q->data);
	guint16 const series_num = MS_OLE_GET_GUINT16 (q->data+2);
	guint16 const pt_num = MS_OLE_GET_GUINT16 (q->data+2);

	switch (link_type)
	{
	case 1 : printf ("TEXT is chart title\n"); break;
	case 2 : printf ("TEXT is Y axis title\n"); break;
	case 3 : printf ("TEXT is X axis title\n"); break;
	case 4 : printf ("TEXT is data label for pt %hd in series %hd\n",
			 pt_num, series_num); break;
	case 7 : printf ("TEXT is Z axis title\n"); break;
	default :
		 printf ("ERROR : TEXT is linked to undocumented object\n");
	};
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
	   ExcelChartState *s, BiffQuery *q)
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
	  ExcelChartState *s, BiffQuery *q)
{
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
		ExcelChartState *s, BiffQuery *q)
{
	guint16 const percent_diam = MS_OLE_GET_GUINT16 (q->data); /* 0-100 */
	g_return_val_if_fail (percent_diam <= 100, TRUE);
	printf ("Pie slice is %hu %% of diam from center\n", percent_diam);
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
	       ExcelChartState *s, BiffQuery *q)
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
		 ExcelChartState *s, BiffQuery *q)
{
	/* Docs say these are longs
	 *But it appears that only 2 lsb are valid ??
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
	  ExcelChartState *s, BiffQuery *q)
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
	    ExcelChartState *s, BiffQuery *q)
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
		ExcelChartState *s, BiffQuery *q)
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
	       ExcelChartState *s, BiffQuery *q)
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
	      ExcelChartState *s, BiffQuery *q)
{
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
		   ExcelChartState *s, BiffQuery *q)
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
	     ExcelChartState *s, BiffQuery *q)
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

typedef enum
{
	MS_CHART_SERIES_DATES		= 0,
	MS_CHART_SERIES_NUMBERS		= 1,
	MS_CHART_SERIES_SEQUENCES	= 2,
	MS_CHART_SERIES_STRINGS		= 3,
	MS_CHART_SERIES_MAX		= 4
} MS_CHART_SERIES;

static char const *const ms_chart_series[] =
{
    "Dates (This is invalid)", "Numbers",
    "Sequences (This is invalid)", "Strings",
};

typedef struct _ExcelChartSeries
{
	struct
	{
		MS_CHART_SERIES type;
		int		count;
	} category, value, bubble;
	gboolean has_bubbles;
} ExcelChartSeries;

static gboolean
BC_R(series_impl)(ExcelChartState *s, BiffQuery *q, ExcelChartSeries * ser)
{
	guint16 tmp;

	/*
	 * WARNING : The offsets in the documentation are WRONG.
	 *           Use the sizes instead.
	 */

	/* Categories */
	tmp = MS_OLE_GET_GUINT16 (q->data+0);
	g_return_val_if_fail (tmp < MS_CHART_SERIES_MAX, TRUE);
	ser->category.type = tmp;
	ser->category.count = MS_OLE_GET_GUINT16 (q->data+4);
	printf ("%d Categories are %s\n",
		ser->category.count,
		ms_chart_series[ser->category.type]);

	/* Values */
	tmp = MS_OLE_GET_GUINT16 (q->data+2);
	g_return_val_if_fail (tmp < MS_CHART_SERIES_MAX, TRUE);
	ser->value.type = tmp;
	ser->value.count = MS_OLE_GET_GUINT16 (q->data+6);
	printf ("%d Values are %s\n",
		ser->value.count,
		ms_chart_series[ser->value.type]);

	if ((ser->has_bubbles = (s->ver >= eBiffV8)))
	{
		tmp = MS_OLE_GET_GUINT16 (q->data+8);
		g_return_val_if_fail (tmp < MS_CHART_SERIES_MAX, TRUE);
		ser->bubble.type = tmp;
		ser->bubble.count = MS_OLE_GET_GUINT16 (q->data+10);
		printf ("%d Bubbles are %s\n",
			ser->bubble.count,
			ms_chart_series[ser->bubble.type]);
	}

	g_return_val_if_fail (s->chart != NULL, TRUE);
	if (s->chart->series == NULL)
		s->chart->series = g_ptr_array_new ();
	g_ptr_array_add (s->chart->series, ser);
	printf ("SERIES = %d\n", s->chart->series->len);
	return FALSE;
}

/*
 * Wrapper function to avoid leaking memory on failure
 */
static gboolean
BC_R(series)(ExcelChartHandler const *handle,
	     ExcelChartState *s, BiffQuery *q)
{
	ExcelChartSeries * ser = g_new (ExcelChartSeries, 1);

	if (BC_R(series_impl)(s, q, ser))
	{
		g_free (ser);
		return TRUE;
	}
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
		 ExcelChartState *s, BiffQuery *q)
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
		 ExcelChartState *s, BiffQuery *q)
{
	guint16 const id = MS_OLE_GET_GUINT16 (q->data);	/* must be 0 */
	int const slen = MS_OLE_GET_GUINT8 (q->data + 2);
	char *text = biff_get_text (q->data + 3, slen, NULL);
	puts (text);

	g_return_val_if_fail (id == 0, FALSE);
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
		ExcelChartState *s, BiffQuery *q)
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
	       ExcelChartState *s, BiffQuery *q)
{
	guint16 const index = MS_OLE_GET_GUINT16 (q->data);
	printf ("Series chart group index is %hd\n", index);
	return FALSE;
}

static gboolean
BC_W(sertocrt)(ExcelChartHandler const *handle,
	       GnumericChartState *s, BiffPut *os)
{
	return FALSE;
}

/****************************************************************************/

typedef enum
{
	MS_CHART_BLANK_SKIP		= 0,
	MS_CHART_BLANK_ZERO		= 1,
	MS_CHART_BLANK_INTERPOLATE	= 2,
	MS_CHART_BLANK_MAX		= 3
} MS_CHART_BLANK;
static char const *const ms_chart_blank[] =
{
	"Skip blanks", "Blanks are zero", "Interpolate blanks"
};

static gboolean
BC_R(shtprops)(ExcelChartHandler const *handle,
	       ExcelChartState *s, BiffQuery *q)
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
	puts (ms_chart_blank[blanks]);

	if (s->ver >= eBiffV8)
	{
		ignore_pos_record = (flags&0x10) ? TRUE : FALSE;
	}
	printf ("%sesize chart with window.\n",
		dont_size_with_window ? "Don't r": "R");

	if (has_pos_record && !ignore_pos_record)
		printf ("There should be a POS record around here soon\n");

	if (manual_format);
		printf ("Manually formated\n");
	if (only_plot_visible_cells);
		printf ("Only plot visible (to who??) cells\n");
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
	      ExcelChartState *s, BiffQuery *q)
{
	static int count = 0;
	/* UNDOCUMENTED : Docs says this is long
	 *Biff record is only length 2 */
	gint16 const index = MS_OLE_GET_GUINT16 (q->data);
	printf ("Series %d is %hd\n", ++count, index);
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
	   ExcelChartState *s, BiffQuery *q)
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
	   ExcelChartState *s, BiffQuery *q)
{
	if (s->prev_opcode == BIFF_CHART_defaulttext)
	{
		puts ("Text follows defaulttext");
	} else
	{
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
	   ExcelChartState *s, BiffQuery *q)
{
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
	    ExcelChartState *s, BiffQuery *q)
{
	guint16 const type = MS_OLE_GET_GUINT16 (q->data);
	g_return_val_if_fail(type == 0, TRUE);

	puts ("Irrelevant");
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
BC_R(valuerange)(ExcelChartHandler const *handle,
		 ExcelChartState *s, BiffQuery *q)
{
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
	  ExcelChartState *s, BiffQuery *q)
{
	puts ("}");
	--(s->depth);
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
		  ExcelChartState *s, BiffQuery *q)
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
{	static ExcelChartHandler const handle = \
	{ BIFF_CHART_ ## name, size, #name, & BC_R(name), & BC_W(name) }; \
	BC(register_handler)(& handle); \
}

static void
BC(register_handlers)()
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
 *This is a temporary routine.  I wanted each handler in a seperate function
 *to avoid massive nesting.  While experimenting with the real (vs MS
 *documentation) structure of a saved chart, this form offers maximum error
 *checking.
 */
static void
BC(register_handler)(ExcelChartHandler const *const handle)
{
	int const num_handler = sizeof(chart_biff_handler) /
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

void
ms_excel_biff_dimensions (BiffQuery *q, ExcelWorkbook *wb)
{
	guint32 first_row;
	guint32 last_row;
	guint16 first_col;
	guint16 last_col;

	/* What the heck was a 0x00 ? */
	if (q->opcode != 0x200)
		return;

	if (wb->ver >= eBiffV8)
	{
		first_row = MS_OLE_GET_GUINT32 (q->data);
		last_row  = MS_OLE_GET_GUINT32 (q->data+4);
		first_col = MS_OLE_GET_GUINT16 (q->data+8);
		last_col  = MS_OLE_GET_GUINT16 (q->data+10);
	} else
	{
		first_row = MS_OLE_GET_GUINT16 (q->data);
		last_row  = MS_OLE_GET_GUINT16 (q->data+2);
		first_col = MS_OLE_GET_GUINT16 (q->data+4);
		last_col  = MS_OLE_GET_GUINT16 (q->data+6);
	}

	if (ms_excel_chart_debug > 0)
		printf ("Dimension = %s%d:%s%d\n",
			col_name(first_col), first_row+1,
			col_name(last_col), last_row+1);
}

void
ms_excel_chart (BiffQuery *q, ExcelWorkbook *wb, BIFF_BOF_DATA *bof)
{
	int const num_handler = sizeof(chart_biff_handler) /
		sizeof(ExcelChartHandler *);

	gboolean done = FALSE;
	ExcelChartState state;

	/* Register the handlers if this is the 1sttime through */
	BC(register_handlers)();

	g_return_if_fail (bof->type == eBiffTChart);
	state.ver = bof->version;
	state.depth = 0;
	state.prev_opcode = 0xdead; /* Invalid */
	state.wb = wb;
	state.chart = gnumeric_chart_new();

	if (ms_excel_chart_debug > 0)
		puts ("{ CHART");
	while (!done && ms_biff_query_next (q))
	{
		int const lsb = q->opcode & 0xff;

		/* Use registered jump table for chart records */
		if ((q->opcode&0xff00) == 0x1000)
		{
			int const begin_end =
				(q->opcode == BIFF_CHART_begin ||
				 q->opcode == BIFF_CHART_end);

			if (lsb >= num_handler ||
			    !chart_biff_handler[lsb] ||
			    chart_biff_handler[lsb]->opcode !=q->opcode)
			{
				printf ("Unknown BIFF_CHART record\n");
				dump_biff (q);
			} else
			{
				ExcelChartHandler const *const h =
					chart_biff_handler[lsb];

				/*
				 *All chart handling is debug for now, so just
				 *lobotomize it here if user isnt interested.
				 */
				if (ms_excel_chart_debug > 0)
				{
					if (!begin_end)
						printf ("%s(\n", h->name);
					(void)(*h->read_fn)(h, &state, q);
					if (!begin_end)
						printf (");\n");
				}
			}
		} else
		{
			switch (lsb)
			{
			case BIFF_EOF:
				done = TRUE;
				if (ms_excel_chart_debug > 0)
					puts ("}; /* CHART */");
				g_return_if_fail(state.depth == 0);
				break;

			case BIFF_PROTECT :
			{
				gboolean const protected =
					(1 == MS_OLE_GET_GUINT16 (q->data));
				if (ms_excel_chart_debug > 0)
					printf ("Chart is%s protected;\n",
						protected ? "" : " not");
			}
			break;

			/* The ordering seems constant at the start
			 * of a chart
			 */
			case BIFF_DIMENSIONS :
				ms_excel_biff_dimensions (q, wb);
				break;

			case BIFF_HEADER :	/* Skip for Now */
			case BIFF_FOOTER :	/* Skip for Now */
			case BIFF_HCENTER :	/* Skip for Now */
			case BIFF_VCENTER :	/* Skip for Now */
			case BIFF_SCL :
			case BIFF_SETUP :
				if (ms_excel_chart_debug > 0)
					printf ("Handled biff %x in chart;\n",
						q->opcode);
				break;

			case BIFF_PRINTSIZE:
			{
#if 0
				/* Undocumented, seems like an enum ??? */
				gint16 const v = MS_OLE_GET_GUINT16 (q->data);
#endif
			}
			break;

			default :
				ms_excel_unexpected_biff (q, "Chart");
			};
		}
		state.prev_opcode = q->opcode;
	}
}

void
ms_excel_read_chart (BiffQuery *q, ExcelWorkbook *wb)
{
	BIFF_BOF_DATA *bof;

	/* 1st record must be a valid BOF record */
	g_return_if_fail (ms_biff_query_next (q));
	bof = ms_biff_bof_data_new (q);
	if (bof->version != eBiffVUnknown)
		ms_excel_chart (q, wb, bof);
	ms_biff_bof_data_destroy (bof);
}
