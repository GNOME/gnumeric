#include "excel.h"
#include "ms-chart.h"
#include "ms-formula-read.h"

#include <stdio.h>

/* #define NO_DEBUG_EXCEL */
extern int ms_excel_chart_debug;

typedef struct
{
	int		depth;
	eBiff_version	ver;
	guint32		prev_opcode;
	ExcelWorkbook * wb;

	/* Used by DEFAULTTEXT to communicate with TEXT */
	int	defaulttext_applicability;
} BIFF_CHART_STATE;

typedef struct
{
	/* TODO TODO : fill in later */
} GUPPI_CHART_STATE;

typedef struct biff_chart_handler BIFF_CHART_HANDLER;
typedef gboolean (*BIFF_CHART_READER)(BIFF_CHART_HANDLER const * handle,
				      BIFF_CHART_STATE *, BiffQuery * q);
typedef gboolean (*BIFF_CHART_WRITER)(BIFF_CHART_HANDLER const * handle,
				      GUPPI_CHART_STATE * s, BiffPut * os);
struct biff_chart_handler
{
	guint16 const opcode;
	int const	min_size;
	char const * const name;
	BIFF_CHART_READER const read_fn;
	BIFF_CHART_WRITER const write_fn;
};

#define BC(n)	biff_chart_ ## n
#define BC_R(n)	BC(write_ ## n)
#define BC_W(n)	BC(read_ ## n)

static StyleColor *
BC_R(color)(guint8 const * data, char * type)
{
	guint32 const rgb = BIFF_GET_GUINT32 (data);
	guint16 const r = (rgb >>  0) & 0xff;
	guint16 const g = (rgb >>  8) & 0xff;
	guint16 const b = (rgb >> 16) & 0xff;

	printf("%s Color %x%x%x\n", type, r, g, b);

	return style_color_new ((r<<8)|r, (g<<8)|g, (b<<8)|b);
}

/****************************************************************************/

static gboolean
BC_R(3dbarshape)(BIFF_CHART_HANDLER const * handle,
		 BIFF_CHART_STATE * s, BiffQuery * q)
{
#if GUESS_GUESS
	/* All the charts I've seen have this record with value 0x0000
	 * its probably an enum of sorts.
	 */
	guint16 const type = BIFF_GET_GUINT16 (q->data);
#endif
	puts ("Undocumented biff 3dbarshape\n");
	dump_biff(q);

	return FALSE;
}
static gboolean
BC_W(3dbarshape)(BIFF_CHART_HANDLER const * handle,
		 GUPPI_CHART_STATE * s, BiffPut * os)
{
	return FALSE;
}

/****************************************************************************/

static gboolean
BC_R(3d)(BIFF_CHART_HANDLER const * handle,
	 BIFF_CHART_STATE * s, BiffQuery * q)
{
	guint16 const rotation = BIFF_GET_GUINT16 (q->data);	/* 0-360 */
	guint16 const elevation = BIFF_GET_GUINT16 (q->data+2);	/* -90 - 90 */
	guint16 const distance = BIFF_GET_GUINT16 (q->data+4);	/* 0 - 100 */
	guint16 const height = BIFF_GET_GUINT16 (q->data+6);
	guint16 const depth = BIFF_GET_GUINT16 (q->data+8);
	guint16 const gap = BIFF_GET_GUINT16 (q->data+10);
	guint8 const flags = BIFF_GET_GUINT8 (q->data+12);
	guint8 const zero = BIFF_GET_GUINT8 (q->data+13);

	gboolean const use_perspective = (flags&0x01) ? TRUE :FALSE;
	gboolean const cluster = (flags&0x02) ? TRUE :FALSE;
	gboolean const auto_scale = (flags&0x04) ? TRUE :FALSE;
	gboolean const walls_2d = (flags&0x20) ? TRUE :FALSE;

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
BC_W(3d)(BIFF_CHART_HANDLER const * handle,
	 GUPPI_CHART_STATE * s, BiffPut * os)
{
	return FALSE;
}

/****************************************************************************/

static gboolean
BC_R(ai)(BIFF_CHART_HANDLER const * handle,
	 BIFF_CHART_STATE * s, BiffQuery * q)
{
	guint8 const id = BIFF_GET_GUINT8 (q->data);
	guint8 const rt = BIFF_GET_GUINT8 (q->data + 1);
	guint16 const flags = BIFF_GET_GUINT16 (q->data + 2);
	guint16 const fmt_index = BIFF_GET_GUINT16 (q->data + 4);
	guint16 const length = BIFF_GET_GUINT16 (q->data + 6);

	switch (id) {
	case 0 : puts ("Linking title or text"); break;
	case 1 : puts ("Linking values"); break;
	case 2 : puts ("Linking categories"); break;
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
		ExprTree * expr;
		sheet.ver = s->ver;
		sheet.wb = s->wb;
		sheet.blank = 0;
		sheet.gnum_sheet = NULL;
		sheet.shared_formulae = NULL;

		if (length > 0)
		{
			expr = ms_excel_parse_formula (&sheet, q->data+8, 0, 0,
						       FALSE, length);
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
BC_W(ai)(BIFF_CHART_HANDLER const * handle,
	 GUPPI_CHART_STATE * s, BiffPut * os)
{
	return FALSE;
}

/****************************************************************************/

static gboolean
BC_R(alruns)(BIFF_CHART_HANDLER const * handle,
	     BIFF_CHART_STATE * s, BiffQuery * q)
{
	gint16 length = BIFF_GET_GUINT16 (q->data);
	long * in = (q->data + 2);
	char * const ans = (char *) g_new (char, length + 2);
	char * out = ans;

	for (; --length >= 0 ; ++in, ++out)
	{
		/* FIXME FIXME FIXME : don't toss font info */
		guint32 const rtf_run = BIFF_GET_GUINT32 (in);
		*out = (char)((*in >> 16) & 0xff);
	}
	*out = '\0';

	puts (ans);
	return FALSE;
}

static gboolean
BC_W(alruns)(BIFF_CHART_HANDLER const * handle,
	     GUPPI_CHART_STATE * s, BiffPut * os)
{
	return FALSE;
}

/****************************************************************************/

static gboolean
BC_R(area)(BIFF_CHART_HANDLER const * handle,
	   BIFF_CHART_STATE * s, BiffQuery * q)
{
	guint16 const flags = BIFF_GET_GUINT16 (q->data);
	gboolean const stacked = (flags & 0x01) ? TRUE : FALSE;
	gboolean const as_percentage = (flags & 0x02) ? TRUE : FALSE;

	if (s->ver >= eBiffV8)
	{
		gboolean const has_shadow = (flags & 0x04) ? TRUE : FALSE;
	}
	return FALSE;
}

static gboolean
BC_W(area)(BIFF_CHART_HANDLER const * handle,
	   GUPPI_CHART_STATE * s, BiffPut * os)
{
	return FALSE;
}

/****************************************************************************/

static gboolean
BC_R(areaformat)(BIFF_CHART_HANDLER const * handle,
		 BIFF_CHART_STATE * s, BiffQuery * q)
{
	StyleColor * fore = BC_R(color) (q->data, "Area Fore");
	StyleColor * back = BC_R(color) (q->data+4, "Area Back");
	guint16 const pattern = BIFF_GET_GUINT16 (q->data+8);
	guint16 const flags = BIFF_GET_GUINT16 (q->data+10);
	gboolean const auto_format = flags & 0x01;
	gboolean const swap_color_for_negative = flags & 0x02;

	if (s->ver >= eBiffV8)
	{
		guint16 const fore_index = BIFF_GET_GUINT16 (q->data+12);
		guint16 const back_index = BIFF_GET_GUINT16 (q->data+14);
	}
	return FALSE;
}

static gboolean
BC_W(areaformat)(BIFF_CHART_HANDLER const * handle,
		 GUPPI_CHART_STATE * s, BiffPut * os)
{
	return FALSE;
}

/****************************************************************************/

static gboolean
BC_R(attachedlabel)(BIFF_CHART_HANDLER const * handle,
		    BIFF_CHART_STATE * s, BiffQuery * q)
{
	guint16 const flags = BIFF_GET_GUINT16 (q->data);
	gboolean const show_value = (flags&0x01) ? TRUE : FALSE;
	gboolean const show_percent = (flags&0x02) ? TRUE : FALSE;
	gboolean const show_label_prercent = (flags&0x04) ? TRUE : FALSE;
	gboolean const smooth_line = (flags&0x08) ? TRUE : FALSE;
	gboolean const show_label = (flags&0x10) ? TRUE : FALSE;
	if (s->ver >= eBiffV8)
	{
		gboolean const show_bubble_size = (flags&0x20) ? TRUE : FALSE;
	}
	return FALSE;
}

static gboolean
BC_W(attachedlabel)(BIFF_CHART_HANDLER const * handle,
		    GUPPI_CHART_STATE * s, BiffPut * os)
{
	return FALSE;
}

/****************************************************************************/

static gboolean
BC_R(axesused)(BIFF_CHART_HANDLER const * handle,
	       BIFF_CHART_STATE * s, BiffQuery * q)
{
	guint16 const num_axis = BIFF_GET_GUINT16 (q->data);
	g_return_val_if_fail(1 <= num_axis && num_axis <= 2, TRUE);
	printf ("There are %hu axis.\n", num_axis);
	return FALSE;
}

static gboolean
BC_W(axesused)(BIFF_CHART_HANDLER const * handle,
	       GUPPI_CHART_STATE * s, BiffPut * os)
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
static char const * const ms_axis[] =
{
	"X-axis", "Y-axis", "series-axis"
};

static gboolean
BC_R(axis)(BIFF_CHART_HANDLER const * handle,
	   BIFF_CHART_STATE * s, BiffQuery * q)
{
	guint16 const axis_type = BIFF_GET_GUINT16 (q->data);
	MS_AXIS atype;
	g_return_val_if_fail (axis_type < MS_AXIS_MAX, TRUE);
	atype = axis_type;
	printf ("This is a %s .\n", ms_axis[atype]);
	return FALSE;
}

static gboolean
BC_W(axis)(BIFF_CHART_HANDLER const * handle,
	   GUPPI_CHART_STATE * s, BiffPut * os)
{
	return FALSE;
}

/****************************************************************************/

static gboolean
BC_R(axcext)(BIFF_CHART_HANDLER const * handle,
	     BIFF_CHART_STATE * s, BiffQuery * q)
{
	return FALSE;
}
static gboolean
BC_W(axcext)(BIFF_CHART_HANDLER const * handle,
	     GUPPI_CHART_STATE * s, BiffPut * os)
{
	return FALSE;
}

/****************************************************************************/

static gboolean
BC_R(axislineformat)(BIFF_CHART_HANDLER const * handle,
		     BIFF_CHART_STATE * s, BiffQuery * q)
{
	guint16 const type = BIFF_GET_GUINT16 (q->data);

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
BC_W(axislineformat)(BIFF_CHART_HANDLER const * handle,
		     GUPPI_CHART_STATE * s, BiffPut * os)
{
	return FALSE;
}

/****************************************************************************/

static gboolean
BC_R(axisparent)(BIFF_CHART_HANDLER const * handle,
		 BIFF_CHART_STATE * s, BiffQuery * q)
{
	guint16 const index = BIFF_GET_GUINT16 (q->data);	/* 1 or 2 */
	/* Measured in 1/4000ths of the chart width */
	guint32 const x = BIFF_GET_GUINT32 (q->data+2);
	guint32 const y = BIFF_GET_GUINT16 (q->data+6);
	guint32 const x_length = BIFF_GET_GUINT16 (q->data+10);
	guint32 const y_length = BIFF_GET_GUINT16 (q->data+14);

	printf ("Axis # %hu @ %f,%f, X=%f, Y=%f\n",
		index, x/4000., y/4000., x_length/4000., y_length/4000.);
	return FALSE;
}

static gboolean
BC_W(axisparent)(BIFF_CHART_HANDLER const * handle,
		 GUPPI_CHART_STATE * s, BiffPut * os)
{
	return FALSE;
}

/****************************************************************************/

static gboolean
BC_R(bar)(BIFF_CHART_HANDLER const * handle,
	  BIFF_CHART_STATE * s, BiffQuery * q)
{
	return FALSE;
}

static gboolean
BC_W(bar)(BIFF_CHART_HANDLER const * handle,
	  GUPPI_CHART_STATE * s, BiffPut * os)
{
	return FALSE;
}

/****************************************************************************/

static gboolean
BC_R(begin)(BIFF_CHART_HANDLER const * handle,
	    BIFF_CHART_STATE * s, BiffQuery * q)
{
	puts ("{");
	++(s->depth);
	return FALSE;
}

static gboolean
BC_W(begin)(BIFF_CHART_HANDLER const * handle,
	    GUPPI_CHART_STATE * s, BiffPut * os)
{
	return FALSE;
}

/****************************************************************************/

static gboolean
BC_R(boppop)(BIFF_CHART_HANDLER const * handle,
	     BIFF_CHART_STATE * s, BiffQuery * q)
{
	return FALSE;
}
static gboolean
BC_W(boppop)(BIFF_CHART_HANDLER const * handle,
	     GUPPI_CHART_STATE * s, BiffPut * os)
{
	return FALSE;
}

/****************************************************************************/

static gboolean
BC_R(boppopcustom)(BIFF_CHART_HANDLER const * handle,
		   BIFF_CHART_STATE * s, BiffQuery * q)
{
	gint16 const count = BIFF_GET_GUINT16 (q->data);
	/* TODO TODO : figure out the bitfield */
	return FALSE;
}

static gboolean
BC_W(boppopcustom)(BIFF_CHART_HANDLER const * handle,
		   GUPPI_CHART_STATE * s, BiffPut * os)
{
	return FALSE;
}

/***************************************************************************/

static gboolean
BC_R(catserrange)(BIFF_CHART_HANDLER const * handle,
		  BIFF_CHART_STATE * s, BiffQuery * q)
{
	return FALSE;
}

static gboolean
BC_W(catserrange)(BIFF_CHART_HANDLER const * handle,
		  GUPPI_CHART_STATE * s, BiffPut * os)
{
	return FALSE;
}

/****************************************************************************/

static gboolean
BC_R(chart)(BIFF_CHART_HANDLER const * handle,
	    BIFF_CHART_STATE * s, BiffQuery * q)
{
	/* TODO TODO TODO : How is fixed point represented */
	/* TODO TODO TODO : Why are all charts listed as starting at 0,0 ?? */
	/* 2 bytes fraction 2 bytes integer, only integer form is known */
	gint32 const x_pos = BIFF_GET_GUINT16 (q->data + 2);
	gint32 const y_pos = BIFF_GET_GUINT16 (q->data + 6);
	gint32 const x_size = BIFF_GET_GUINT16 (q->data + 10);
	gint32 const y_size = BIFF_GET_GUINT16 (q->data + 14);
	printf("Chart @ %hd, %hd is %hdx%hd\n", x_pos, y_pos, x_size, y_size);

	return FALSE;
}

static gboolean
BC_W(chart)(BIFF_CHART_HANDLER const * handle,
	    GUPPI_CHART_STATE * s, BiffPut * os)
{
	return FALSE;
}

/****************************************************************************/

static gboolean
BC_R(chartformat)(BIFF_CHART_HANDLER const * handle,
		  BIFF_CHART_STATE * s, BiffQuery * q)
{
	guint16 const flags = BIFF_GET_GUINT16 (q->data+16);
	guint16 const z_order = BIFF_GET_GUINT16 (q->data+18);
	gboolean const vary_color = (flags&0x01) ? TRUE : FALSE;
	return FALSE;
}

static gboolean
BC_W(chartformat)(BIFF_CHART_HANDLER const * handle,
		  GUPPI_CHART_STATE * s, BiffPut * os)
{
	return FALSE;
}

/****************************************************************************/

static gboolean
BC_R(chartformatlink)(BIFF_CHART_HANDLER const * handle,
		      BIFF_CHART_STATE * s, BiffQuery * q)
{
	return FALSE;
}

static gboolean
BC_W(chartformatlink)(BIFF_CHART_HANDLER const * handle,
		      GUPPI_CHART_STATE * s, BiffPut * os)
{
	return FALSE;
}

/****************************************************************************/

static gboolean
BC_R(chartline)(BIFF_CHART_HANDLER const * handle,
		BIFF_CHART_STATE * s, BiffQuery * q)
{
	return FALSE;
}

static gboolean
BC_W(chartline)(BIFF_CHART_HANDLER const * handle,
		GUPPI_CHART_STATE * s, BiffPut * os)
{
	return FALSE;
}

/****************************************************************************/

static gboolean
BC_R(clrtclient)(BIFF_CHART_HANDLER const * handle,
		 BIFF_CHART_STATE * s, BiffQuery * q)
{
	puts ("Undocumented BIFF : clrtclient");
	dump_biff(q);
	return FALSE;
}
static gboolean
BC_W(clrtclient)(BIFF_CHART_HANDLER const * handle,
		 GUPPI_CHART_STATE * s, BiffPut * os)
{
	return FALSE;
}

/****************************************************************************/

static gboolean
BC_R(dat)(BIFF_CHART_HANDLER const * handle,
	  BIFF_CHART_STATE * s, BiffQuery * q)
{
	gint16 const flags = BIFF_GET_GUINT16 (q->data);
	gboolean const horiz_border = (flags&0x01) ? TRUE : FALSE;
	gboolean const vert_border = (flags&0x02) ? TRUE : FALSE;
	gboolean const border = (flags&0x04) ? TRUE : FALSE;
	gboolean const series_keys = (flags&0x08) ? TRUE : FALSE;
	return FALSE;
}
static gboolean
BC_W(dat)(BIFF_CHART_HANDLER const * handle,
	  GUPPI_CHART_STATE * s, BiffPut * os)
{
	return FALSE;
}

/****************************************************************************/

static gboolean
BC_R(dataformat)(BIFF_CHART_HANDLER const * handle,
		 BIFF_CHART_STATE * s, BiffQuery * q)
{
	guint16 const pt_num = BIFF_GET_GUINT16 (q->data);
	guint16 const series_index = BIFF_GET_GUINT16 (q->data+2);
	guint16 const series_index_for_label = BIFF_GET_GUINT16 (q->data+4);
	guint16 const excel4_auto_color = BIFF_GET_GUINT16 (q->data+6) & 0x01;

	if (pt_num == 0xffff)
		printf ("All points");
	else
		printf ("Point-%hd", pt_num);

	printf (", series=%hd\n", series_index);
	return FALSE;
}

static gboolean
BC_W(dataformat)(BIFF_CHART_HANDLER const * handle,
		 GUPPI_CHART_STATE * s, BiffPut * os)
{
	return FALSE;
}

/****************************************************************************/

static gboolean
BC_R(defaulttext)(BIFF_CHART_HANDLER const * handle,
		  BIFF_CHART_STATE * s, BiffQuery * q)
{
	guint16	const tmp = BIFF_GET_GUINT16 (q->data);
	s->defaulttext_applicability = -1;
	printf ("applicability = %hd\n", tmp);

	/*
	 * 0 == 'show labels' label
	 * 1 == Value and percentage data label
	 * 2 == All text in chart
	 * 3 == Undocumented ??
	 */
	g_return_val_if_fail (tmp <= 3, TRUE);
	s->defaulttext_applicability = tmp;
	return FALSE;
}

static gboolean
BC_W(defaulttext)(BIFF_CHART_HANDLER const * handle,
		  GUPPI_CHART_STATE * s, BiffPut * os)
{
	return FALSE;
}

/****************************************************************************/

static gboolean
BC_R(dropbar)(BIFF_CHART_HANDLER const * handle,
	      BIFF_CHART_STATE * s, BiffQuery * q)
{
	guint16 const width = BIFF_GET_GUINT16 (q->data);	/* 0-100 */
	return FALSE;
}

static gboolean
BC_W(dropbar)(BIFF_CHART_HANDLER const * handle,
	      GUPPI_CHART_STATE * s, BiffPut * os)
{
	return FALSE;
}

/****************************************************************************/

static gboolean
BC_R(fbi)(BIFF_CHART_HANDLER const * handle,
	  BIFF_CHART_STATE * s, BiffQuery * q)
{
	/* TODO TODO TODO : Work on appropriate scales */
	guint16 const x_basis = BIFF_GET_GUINT16 (q->data);
	guint16 const y_basis = BIFF_GET_GUINT16 (q->data+2);
	guint16 const applied_height = BIFF_GET_GUINT16 (q->data+4);
	guint16 const scale_basis = BIFF_GET_GUINT16 (q->data+6);
	guint16 const index = BIFF_GET_GUINT16 (q->data+8);

	printf ("Font %hu (%hu x %hu) scale=%hu, height=%hu\n",
		index, x_basis, y_basis, scale_basis, applied_height);
	return FALSE;
}
static gboolean
BC_W(fbi)(BIFF_CHART_HANDLER const * handle,
	  GUPPI_CHART_STATE * s, BiffPut * os)
{
	return FALSE;
}

/****************************************************************************/

static gboolean
BC_R(fontx)(BIFF_CHART_HANDLER const * handle,
	    BIFF_CHART_STATE * s, BiffQuery * q)
{
	/* Child of TEXT, index into FONT table */
	guint16 const font = BIFF_GET_GUINT16 (q->data);
	return FALSE;
}

static gboolean
BC_W(fontx)(BIFF_CHART_HANDLER const * handle,
	    GUPPI_CHART_STATE * s, BiffPut * os)
{
	return FALSE;
}

/****************************************************************************/

static gboolean
BC_R(frame)(BIFF_CHART_HANDLER const * handle,
	    BIFF_CHART_STATE * s, BiffQuery * q)
{
	guint16 const type = BIFF_GET_GUINT16 (q->data);
	guint16 const flags = BIFF_GET_GUINT16 (q->data+2);
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
BC_W(frame)(BIFF_CHART_HANDLER const * handle,
	    GUPPI_CHART_STATE * s, BiffPut * os)
{
	return FALSE;
}

/****************************************************************************/

static gboolean
BC_R(gelframe)(BIFF_CHART_HANDLER const * handle,
	       BIFF_CHART_STATE * s, BiffQuery * q)
{
	return FALSE;
}
static gboolean
BC_W(gelframe)(BIFF_CHART_HANDLER const * handle,
	       GUPPI_CHART_STATE * s, BiffPut * os)
{
	/* TODO TODO : From MS Office Drawing
	 * Has something to do with gradient fills
	 * patterns & textures
	 */
	return FALSE;
}

/****************************************************************************/

static gboolean
BC_R(ifmt)(BIFF_CHART_HANDLER const * handle,
	   BIFF_CHART_STATE * s, BiffQuery * q)
{
	return FALSE;
}

static gboolean
BC_W(ifmt)(BIFF_CHART_HANDLER const * handle,
	   GUPPI_CHART_STATE * s, BiffPut * os)
{
	return FALSE;
}

/****************************************************************************/

static gboolean
BC_R(legend)(BIFF_CHART_HANDLER const * handle,
	     BIFF_CHART_STATE * s, BiffQuery * q)
{
	return FALSE;
}

static gboolean
BC_W(legend)(BIFF_CHART_HANDLER const * handle,
	     GUPPI_CHART_STATE * s, BiffPut * os)
{
	return FALSE;
}

/****************************************************************************/

static gboolean
BC_R(legendxn)(BIFF_CHART_HANDLER const * handle,
	       BIFF_CHART_STATE * s, BiffQuery * q)
{
	return FALSE;
}

static gboolean
BC_W(legendxn)(BIFF_CHART_HANDLER const * handle,
	       GUPPI_CHART_STATE * s, BiffPut * os)
{
	return FALSE;
}

/****************************************************************************/

static gboolean
BC_R(line)(BIFF_CHART_HANDLER const * handle,
	   BIFF_CHART_STATE * s, BiffQuery * q)
{
	return FALSE;
}

static gboolean
BC_W(line)(BIFF_CHART_HANDLER const * handle,
	   GUPPI_CHART_STATE * s, BiffPut * os)
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
static char const * const ms_line_pattern[] =
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
static char const * const ms_line_wgt[] = {
	"hairline", "normal", "medium", "extra"
};

static gboolean
BC_R(lineformat)(BIFF_CHART_HANDLER const * handle,
		 BIFF_CHART_STATE * s, BiffQuery * q)
{
	StyleColor * color = BC_R(color) (q->data, "Line");
	guint16 const pattern = BIFF_GET_GUINT16 (q->data+4);
	gint16 const weight = BIFF_GET_GUINT16 (q->data+6);
	guint16 const flags = BIFF_GET_GUINT16 (q->data+8);
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
		guint16 const color_index = BIFF_GET_GUINT16 (q->data+10);
	}
	return FALSE;
}

static gboolean
BC_W(lineformat)(BIFF_CHART_HANDLER const * handle,
		 GUPPI_CHART_STATE * s, BiffPut * os)
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
static char const * const ms_chart_marker[] = {
	"not marked", "marked with squares", "marked with diamonds",
	"marked with triangle", "marked with an x", "marked with star",
	"marked with a dow symbol", "marked with a std", "marked with a circle",
	"marked with a plus"
};


static gboolean
BC_R(markerformat)(BIFF_CHART_HANDLER const * handle,
		   BIFF_CHART_STATE * s, BiffQuery * q)
{
	StyleColor * fore = BC_R(color) (q->data, "MarkerFore");
	StyleColor * back = BC_R(color) (q->data+4, "MarkerBack");
	guint16 const tmp = BIFF_GET_GUINT16 (q->data+8);
	guint16 const flags = BIFF_GET_GUINT16 (q->data+10);
	MS_CHART_MARKER marker;
	gboolean	auto_color, no_fore, no_back;

	g_return_val_if_fail (tmp < MS_CHART_MARKER_MAX, TRUE);
	marker = tmp;
	printf ("Points are %s\n", ms_chart_marker[marker]);

	auto_color = (flags & 0x01) ? TRUE : FALSE;
	no_fore	= (flags & 0x10) ? TRUE : FALSE;
	no_back = (flags & 0x20) ? TRUE : FALSE;

	if (s->ver >= eBiffV8)
	{
		guint16 const fore_index = BIFF_GET_GUINT16 (q->data+12);
		guint16 const back_index = BIFF_GET_GUINT16 (q->data+14);
		guint32 const marker_size = BIFF_GET_GUINT32 (q->data+16);
	}
	return FALSE;
}

static gboolean
BC_W(markerformat)(BIFF_CHART_HANDLER const * handle,
		   GUPPI_CHART_STATE * s, BiffPut * os)
{
	return FALSE;
}

/****************************************************************************/

static gboolean
BC_R(objectlink)(BIFF_CHART_HANDLER const * handle,
		 BIFF_CHART_STATE * s, BiffQuery * q)
{
	guint16 const link_type = BIFF_GET_GUINT16 (q->data);
	guint16 const series_num = BIFF_GET_GUINT16 (q->data+2);
	guint16 const pt_num = BIFF_GET_GUINT16 (q->data+2);
	char * str;

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
BC_W(objectlink)(BIFF_CHART_HANDLER const * handle,
		 GUPPI_CHART_STATE * s, BiffPut * os)
{
	return FALSE;
}

/****************************************************************************/

static gboolean
BC_R(picf)(BIFF_CHART_HANDLER const * handle,
	   BIFF_CHART_STATE * s, BiffQuery * q)
{
	return FALSE;
}

static gboolean
BC_W(picf)(BIFF_CHART_HANDLER const * handle,
	   GUPPI_CHART_STATE * s, BiffPut * os)
{
	return FALSE;
}

/****************************************************************************/

static gboolean
BC_R(pie)(BIFF_CHART_HANDLER const * handle,
	  BIFF_CHART_STATE * s, BiffQuery * q)
{
	return FALSE;
}

static gboolean
BC_W(pie)(BIFF_CHART_HANDLER const * handle,
	  GUPPI_CHART_STATE * s, BiffPut * os)
{
	return FALSE;
}

/****************************************************************************/

static gboolean
BC_R(pieformat)(BIFF_CHART_HANDLER const * handle,
		BIFF_CHART_STATE * s, BiffQuery * q)
{
	guint16 const percent_diam = BIFF_GET_GUINT16 (q->data); /* 0-100 */
	g_return_val_if_fail (percent_diam <= 100, TRUE);
	printf ("Pie slice is %hu %% of diam from center\n", percent_diam);
	return FALSE;
}

static gboolean
BC_W(pieformat)(BIFF_CHART_HANDLER const * handle,
		GUPPI_CHART_STATE * s, BiffPut * os)
{
	return FALSE;
}

/****************************************************************************/

static gboolean
BC_R(plotarea)(BIFF_CHART_HANDLER const * handle,
	       BIFF_CHART_STATE * s, BiffQuery * q)
{
	/* Does nothing.  Should always have a 'FRAME' record following */
	return FALSE;
}

static gboolean
BC_W(plotarea)(BIFF_CHART_HANDLER const * handle,
	       GUPPI_CHART_STATE * s, BiffPut * os)
{
	return FALSE;
}

/****************************************************************************/

static gboolean
BC_R(plotgrowth)(BIFF_CHART_HANDLER const * handle,
		 BIFF_CHART_STATE * s, BiffQuery * q)
{
	/* Docs say these are longs
	 * But it appears that only 2 lsb are valid ??
	 */
	gint16 const horiz = BIFF_GET_GUINT16 (q->data+2);
	gint16 const vert = BIFF_GET_GUINT16 (q->data+6);

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
BC_W(plotgrowth)(BIFF_CHART_HANDLER const * handle,
		 GUPPI_CHART_STATE * s, BiffPut * os)
{
	return FALSE;
}

/****************************************************************************/

static gboolean
BC_R(pos)(BIFF_CHART_HANDLER const * handle,
	  BIFF_CHART_STATE * s, BiffQuery * q)
{
	return FALSE;
}

static gboolean
BC_W(pos)(BIFF_CHART_HANDLER const * handle,
	  GUPPI_CHART_STATE * s, BiffPut * os)
{
	return FALSE;
}

/****************************************************************************/

static gboolean
BC_R(radar)(BIFF_CHART_HANDLER const * handle,
	    BIFF_CHART_STATE * s, BiffQuery * q)
{
	return FALSE;
}

static gboolean
BC_W(radar)(BIFF_CHART_HANDLER const * handle,
	    GUPPI_CHART_STATE * s, BiffPut * os)
{
	return FALSE;
}

/****************************************************************************/

static gboolean
BC_R(radararea)(BIFF_CHART_HANDLER const * handle,
		BIFF_CHART_STATE * s, BiffQuery * q)
{
	return FALSE;
}

static gboolean
BC_W(radararea)(BIFF_CHART_HANDLER const * handle,
		GUPPI_CHART_STATE * s, BiffPut * os)
{
	return FALSE;
}

/****************************************************************************/

static gboolean
BC_R(sbaseref)(BIFF_CHART_HANDLER const * handle,
	       BIFF_CHART_STATE * s, BiffQuery * q)
{
	return FALSE;
}

static gboolean
BC_W(sbaseref)(BIFF_CHART_HANDLER const * handle,
	       GUPPI_CHART_STATE * s, BiffPut * os)
{
	return FALSE;
}

/****************************************************************************/

static gboolean
BC_R(scatter)(BIFF_CHART_HANDLER const * handle,
	      BIFF_CHART_STATE * s, BiffQuery * q)
{
	return FALSE;
}

static gboolean
BC_W(scatter)(BIFF_CHART_HANDLER const * handle,
	      GUPPI_CHART_STATE * s, BiffPut * os)
{
	return FALSE;
}

/****************************************************************************/

static gboolean
BC_R(serauxerrbar)(BIFF_CHART_HANDLER const * handle,
		   BIFF_CHART_STATE * s, BiffQuery * q)
{
	return FALSE;
}

static gboolean
BC_W(serauxerrbar)(BIFF_CHART_HANDLER const * handle,
		   GUPPI_CHART_STATE * s, BiffPut * os)
{
	return FALSE;
}

/****************************************************************************/

static gboolean
BC_R(serfmt)(BIFF_CHART_HANDLER const * handle,
	     BIFF_CHART_STATE * s, BiffQuery * q)
{
	return FALSE;
}

static gboolean
BC_W(serfmt)(BIFF_CHART_HANDLER const * handle,
	     GUPPI_CHART_STATE * s, BiffPut * os)
{
	return FALSE;
}

/****************************************************************************/

static int
map_series_types(BiffQuery * q, int const offset, char const * const pre)
{
	guint16 const type = BIFF_GET_GUINT16 (q->data);

	printf("%s", pre);
	switch (type)
	{
	case 0 : puts(" are Dates !! (Should not happen)"); break;
	case 1 : puts(" are numbers."); break;
	case 2 : puts(" are sequences !! (Should not happen)"); break;
	case 3 : puts(" are strings."); break;
	default :
		 puts(" UNKNOWN!."); break;
	};
	return type;
}

static gboolean
BC_R(series)(BIFF_CHART_HANDLER const * handle,
	     BIFF_CHART_STATE * s, BiffQuery * q)
{
	int const category_type = map_series_types(q, 0, "Categories");
	int const value_type = map_series_types(q, 4, "Values");
	guint16 const num_categories = BIFF_GET_GUINT16 (q->data+6);
	guint16 const num_values = BIFF_GET_GUINT16 (q->data+8);

	if (s->ver >= eBiffV8)
	{
		int const bubble_type = map_series_types(q, 10, "Bubbles");
		guint16 const num_bubble = BIFF_GET_GUINT16 (q->data+12);
	}
	return FALSE;
}

static gboolean
BC_W(series)(BIFF_CHART_HANDLER const * handle,
	     GUPPI_CHART_STATE * s, BiffPut * os)
{
	return FALSE;
}

/****************************************************************************/

static gboolean
BC_R(serieslist)(BIFF_CHART_HANDLER const * handle,
		 BIFF_CHART_STATE * s, BiffQuery * q)
{
	return FALSE;
}

static gboolean
BC_W(serieslist)(BIFF_CHART_HANDLER const * handle,
		 GUPPI_CHART_STATE * s, BiffPut * os)
{
	return FALSE;
}

/****************************************************************************/

static gboolean
BC_R(seriestext)(BIFF_CHART_HANDLER const * handle,
		 BIFF_CHART_STATE * s, BiffQuery * q)
{
	guint16 const id = BIFF_GET_GUINT16 (q->data);	/* must be 0 */
	int const slen = BIFF_GET_GUINT8 (q->data + 2);
	char * text = biff_get_text (q->data + 3, slen, NULL);
	puts (text);
	return FALSE;
}

static gboolean
BC_W(seriestext)(BIFF_CHART_HANDLER const * handle,
		 GUPPI_CHART_STATE * s, BiffPut * os)
{
	return FALSE;
}

/****************************************************************************/

static gboolean
BC_R(serparent)(BIFF_CHART_HANDLER const * handle,
		BIFF_CHART_STATE * s, BiffQuery * q)
{
	return FALSE;
}

static gboolean
BC_W(serparent)(BIFF_CHART_HANDLER const * handle,
		GUPPI_CHART_STATE * s, BiffPut * os)
{
	return FALSE;
}

/****************************************************************************/

static gboolean
BC_R(sertocrt)(BIFF_CHART_HANDLER const * handle,
	       BIFF_CHART_STATE * s, BiffQuery * q)
{
	guint16 const index = BIFF_GET_GUINT16 (q->data);
	printf ("Series chart group index is %hd\n", index);
	return FALSE;
}

static gboolean
BC_W(sertocrt)(BIFF_CHART_HANDLER const * handle,
	       GUPPI_CHART_STATE * s, BiffPut * os)
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
static char const * const ms_chart_blank[] = {
	"Skip blanks", "Blanks are zero", "Interpolate blanks"};

	static gboolean
	BC_R(shtprops)(BIFF_CHART_HANDLER const * handle,
		       BIFF_CHART_STATE * s, BiffQuery * q)
{
	guint16 const flags = BIFF_GET_GUINT16 (q->data);
	guint8 const tmp = BIFF_GET_GUINT16 (q->data+2);
	gboolean const manual_format		= (flags&0x01) ? TRUE : FALSE;
	gboolean const only_plot_visible_cells	= (flags&0x02) ? TRUE : FALSE;
	gboolean const dont_size_with_window	= (flags&0x04) ? TRUE : FALSE;
	gboolean const has_pos_record		= (flags&0x08) ? TRUE : FALSE;
	MS_CHART_BLANK blanks;

	g_return_val_if_fail (tmp < MS_CHART_BLANK_MAX, TRUE);
	blanks = tmp;
	puts (ms_chart_blank[blanks]);

	if (s->ver >= eBiffV8)
	{
		gboolean const ignore_pos_record = (flags&0x10) ? TRUE : FALSE;
	}
	printf ("%sesize chart with window.\n",
		dont_size_with_window ? "Don't r": "R");
	return FALSE;
}

static gboolean
BC_W(shtprops)(BIFF_CHART_HANDLER const * handle,
	       GUPPI_CHART_STATE * s, BiffPut * os)
{
	return FALSE;
}

/****************************************************************************/

static gboolean
BC_R(siindex)(BIFF_CHART_HANDLER const * handle,
	      BIFF_CHART_STATE * s, BiffQuery * q)
{
	static int count = 0;
	/* UNDOCUMENTED : Docs says this is long
	 * Biff record is only length 2 */
	gint16 const index = BIFF_GET_GUINT16 (q->data);
	printf ("Series %d is %hd\n", ++count, index);
	return FALSE;
}
static gboolean
BC_W(siindex)(BIFF_CHART_HANDLER const * handle,
	      GUPPI_CHART_STATE * s, BiffPut * os)
{
	return FALSE;
}

/****************************************************************************/

static gboolean
BC_R(surf)(BIFF_CHART_HANDLER const * handle,
	   BIFF_CHART_STATE * s, BiffQuery * q)
{
	return FALSE;
}

static gboolean
BC_W(surf)(BIFF_CHART_HANDLER const * handle,
	   GUPPI_CHART_STATE * s, BiffPut * os)
{
	return FALSE;
}

/****************************************************************************/

static gboolean
BC_R(text)(BIFF_CHART_HANDLER const * handle,
	   BIFF_CHART_STATE * s, BiffQuery * q)
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
	break;
default :
	printf ("BIFF ERROR : A Text record follows a %x\n",
		s->prev_opcode);

};
#endif
return FALSE;
}

static gboolean
BC_W(text)(BIFF_CHART_HANDLER const * handle,
	   GUPPI_CHART_STATE * s, BiffPut * os)
{
	return FALSE;
}

/****************************************************************************/

static gboolean
BC_R(tick)(BIFF_CHART_HANDLER const * handle,
	   BIFF_CHART_STATE * s, BiffQuery * q)
{
	return FALSE;
}

static gboolean
BC_W(tick)(BIFF_CHART_HANDLER const * handle,
	   GUPPI_CHART_STATE * s, BiffPut * os)
{
	return FALSE;
}

/****************************************************************************/

static gboolean
BC_R(units)(BIFF_CHART_HANDLER const * handle,
	    BIFF_CHART_STATE * s, BiffQuery * q)
{
	guint16 const type = BIFF_GET_GUINT16 (q->data);
	g_return_val_if_fail(type == 0, TRUE);

	puts ("Irrelevant");
	return FALSE;
}
static gboolean
BC_W(units)(BIFF_CHART_HANDLER const * handle,
	    GUPPI_CHART_STATE * s, BiffPut * os)
{
	g_warning("Should not write BIFF_CHART_UNITS");
	return FALSE;
}

/****************************************************************************/

static gboolean
BC_R(valuerange)(BIFF_CHART_HANDLER const * handle,
		 BIFF_CHART_STATE * s, BiffQuery * q)
{
	return FALSE;
}

static gboolean
BC_W(valuerange)(BIFF_CHART_HANDLER const * handle,
		 GUPPI_CHART_STATE * s, BiffPut * os)
{
	return FALSE;
}

/****************************************************************************/

static gboolean
BC_R(end)(BIFF_CHART_HANDLER const * handle,
	  BIFF_CHART_STATE * s, BiffQuery * q)
{
	puts ("}");
	--(s->depth);
	return FALSE;
}

static gboolean
BC_W(end)(BIFF_CHART_HANDLER const * handle,
	  GUPPI_CHART_STATE * s, BiffPut * os)
{
	return FALSE;
}

/****************************************************************************/

static gboolean
BC_R(serauxtrend)(BIFF_CHART_HANDLER const * handle,
		  BIFF_CHART_STATE * s, BiffQuery * q)
{
	return FALSE;
}

static gboolean
BC_W(serauxtrend)(BIFF_CHART_HANDLER const * handle,
		  GUPPI_CHART_STATE * s, BiffPut * os)
{
	return FALSE;
}

/****************************************************************************/

static BIFF_CHART_HANDLER const * chart_biff_handler[128];

static void
BC(register_handler)(BIFF_CHART_HANDLER const * const handle);
#define BIFF_CHART(name, size) \
{	static BIFF_CHART_HANDLER const handle = \
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
	i = sizeof(chart_biff_handler) / sizeof(BIFF_CHART_HANDLER *);
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
	BIFF_CHART(series, 10);
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
 * This is a temporary routine.  I wanted each handler in a seperate function
 * to avoid massive nesting.  While experimenting with the real (vs MS
 * documentation) structure of a saved chart, this form offers maximum error
 * checking.
 */
static void
BC(register_handler)(BIFF_CHART_HANDLER const * const handle)
{
	int const num_handler = sizeof(chart_biff_handler) /
		sizeof(BIFF_CHART_HANDLER *);

	guint32 num = handle->opcode & 0xff;

	if (num >= num_handler)
		printf ("Invalid BIFF_CHART handler (%x)\n", handle->opcode);
	else if (chart_biff_handler[num])
		printf ("Multiple BIFF_CHART handlers for (%x)\n",
			handle->opcode);
	else
		chart_biff_handler[num] = handle;
}


static void
ms_excel_chart (BIFF_BOF_DATA* bof, ExcelWorkbook * wb, BiffQuery * q)
{
	int const num_handler = sizeof(chart_biff_handler) /
		sizeof(BIFF_CHART_HANDLER *);

	gboolean done = FALSE;
	BIFF_CHART_STATE state;

	g_return_if_fail (bof->type == eBiffTChart);
	state.ver = bof->version;
	state.depth = 0;
	state.prev_opcode = 0xdead; /* Invalid */
	state.wb = wb;
	state.defaulttext_applicability = -1; /* Invalid */

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
				BIFF_CHART_HANDLER const * const h =
					chart_biff_handler[lsb];

				/*
				 * All chart handling is debug for now, so just
				 * lobotomize it here if user isnt interested.
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
					(1 == BIFF_GET_GUINT16 (q->data));
				if (ms_excel_chart_debug > 0)
					printf ("Chart is%s protected;\n",
						protected ? "" : " not");
			}
			break;

			/* The ordering seems constant at the start
			 * of a chart
			 */
			case BIFF_DIMENSIONS :
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

			default :
				ms_excel_unexpected_biff (q, "Chart");
			};
		}
		state.prev_opcode = q->opcode;
	}
}

void
ms_excel_read_chart (ExcelWorkbook * wb, BiffQuery * q)
{
	BIFF_BOF_DATA * bof;
	BC(register_handlers)();

	/* 1st record must be a valid BOF record */
	g_return_if_fail (ms_biff_query_next (q));
	bof = ms_biff_bof_data_new (q);
	if (bof->version != eBiffVUnknown)
		ms_excel_chart (bof, wb, q);
	ms_biff_bof_data_destroy (bof);
}
