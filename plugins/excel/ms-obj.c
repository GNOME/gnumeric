/**
 * ms-obj.c: MS Excel Object support for Gnumeric
 *
 * Author:
 *    Michael Meeks (michael@imaginator.com)
 **/
#include <stdio.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <assert.h>
#include <config.h>
#include <stdio.h>
#include <ctype.h>
#include <gnome.h>
#include "gnumeric.h"
#include "gnumeric-util.h"
#include "gnome-xml/tree.h"
#include "gnome-xml/parser.h"
#include "gnumeric-sheet.h"
#include "format.h"
#include "color.h"
#include "sheet-object.h"
#include "style.h"

#include "excel.h"
#include "ms-biff.h"
#include "ms-excel-read.h"
#include "ms-excel-biff.h"
#include "ms-obj.h"

/* Confusingly this micro-biff will use the offsets
   as specified in the docs */
#define GR_BIFF_LENGTH(p) (BIFF_GETWORD(p+2))
#define GR_BIFF_OPCODE(p) (BIFF_GETWORD(p))

#define GR_END                0x00
#define GR_MACRO              0x04
#define GR_COMMAND_BUTTON     0x05
#define GR_GROUP_BUTTON       0x06
#define GR_CLIPBOARD_FORMAT   0x07
#define GR_PICTURE_OPTIONS    0x08
#define GR_PICTURE_FORMULA    0x09
#define GR_CHECKBOX_LINK      0x0A
#define GR_RADIO_BUTTON       0x0B
#define GR_SCROLLBAR          0x0C
#define GR_NOTE_STRUCTURE     0x0D
#define GR_SCROLLBAR_FORMULA  0x0E
#define GR_GROUP_BOX_DATA     0x0F
#define GR_EDIT_CONTROL_DATA  0x10
#define GR_RADIO_BUTTON_DATA  0x11
#define GR_CHECKBOX_DATA      0x12
#define GR_LISTBOX_DATA       0x13
#define GR_CHECKBOX_FORMULA   0x14
#define GR_COMMON_OBJ_DATA    0x15

static char *
object_type_names[] =
{
        "Group", /* 0x00 */
        "Line",		/* 0x01 */
        "Rectangle",	/* 0x02 */
        "Oval",		/* 0x03 */
        "Arc",		/* 0x04 */
        "Chart",	/* 0x05 */
        "Text",		/* 0x06 */
        "Button",	/* 0x07 */
        "Picture",	/* 0x08 */
        "Polygon",	/* 0x09 */
        NULL,		/* 0x0A */
        "Check",	/* 0x0B */
        "Option",	/* 0x0C */
        "Edit",		/* 0x0D */
        "Label",	/* 0x0E */
        "Dialog",	/* 0x0F */
        "Spinner",	/* 0x10 */
        "Scroll",	/* 0x11 */
        "List",		/* 0x12 */
        "Group",	/* 0x13 */
        "Combo",	/* 0x14 */
        NULL,		/* 0x15 */
        NULL,		/* 0x16 */
        NULL,		/* 0x17 */
        NULL,		/* 0x18 */
        "Comment",	/* 0x19 */
        NULL,		/* 0x1A */
        NULL,		/* 0x1B */
        NULL,		/* 0x1C */
        NULL,		/* 0x1D */
        "MS Drawing"	/* 0x1E */
};

/* HACK HACK HACK
 * Use this temporarily to get a handle on nesting behaviour.
 * Replace it with a thread safe state structure when we fill in the guts.
 */
static int ms_chart_chart_depth = 0;

void
ms_obj_read_obj (BIFF_QUERY *q)
{
	guint8 *data;
	gint32 data_len_left;
	int hit_end;

	g_return_if_fail (q);
	g_return_if_fail (q->ls_op==BIFF_OBJ);

/*	printf ("Graphic object\n");
	dump (q->data, q->length); */
	
	data = q->data;
	data_len_left = q->length;

	/* Scan through the pseudo BIFF substream */
	for(hit_end=0;data_len_left > 0 && !hit_end;) {
		switch (GR_BIFF_OPCODE(data)) {
		case GR_END:
			hit_end=1;
			break;

		case GR_MACRO :
			break;

		case GR_COMMAND_BUTTON :
			break;
		case GR_GROUP_BUTTON :
			break;

		case GR_CLIPBOARD_FORMAT:
			break;
		case GR_PICTURE_OPTIONS:
			break;
		case GR_PICTURE_FORMULA:
			break;
		case GR_CHECKBOX_LINK :
			break;
		case GR_RADIO_BUTTON :
			break;
		case GR_SCROLLBAR :
			break;
		case GR_NOTE_STRUCTURE :
			break;
		case GR_SCROLLBAR_FORMULA :
			break;
		case GR_GROUP_BOX_DATA :
			break;
		case GR_EDIT_CONTROL_DATA :
			break;
		case GR_RADIO_BUTTON_DATA :
			break;
		case GR_CHECKBOX_DATA :
			break;
		case GR_LISTBOX_DATA :
			break;
		case GR_CHECKBOX_FORMULA :
			break;

		case GR_COMMON_OBJ_DATA:
		{
			guint16 len=BIFF_GETWORD(data+2);
			guint16 obj_type=BIFF_GETWORD(data+4);
			guint16 obj_id  =BIFF_GETWORD(data+6);
			guint16 options =BIFF_GETWORD(data+8);
			char *type = NULL;
			enum { Locked=0x1, Printable=0x2,
			       AutoFill=0x4, AutoLine=0x8 } flags;
			printf ("Common object data len 0x%x "
				"Type 0x%x id 0x%x options 0x%x\n",
				len, obj_type, obj_id, options);
			flags=0;
			if (options&0x0001)
				flags|=Locked;
			if (options&0x0010)
				flags|=Printable;
			if (options&0x2000)
				flags|=AutoFill;
			if (options&0x4000)
				flags|=AutoLine;
			if (options&0x9fee)
				printf ("FIXME: Error in common object flags\n");
			if (obj_type<sizeof(object_type_names)/sizeof(char*))
				type =object_type_names[obj_type];
			if (type)
			{
				printf ("Object '%s'\n", type);
				if (0x5 == obj_type) /* Chart */
				{
					/* There appear to be 2 ends generated
					 * For each OBJ record ???
					 */
					ms_chart_chart_depth += 2;
					puts("---------------");
					puts("1 : Chart Begin");
					puts("2 : Chart Begin");
				}
			}
			else
				printf ("Unknown object type\n");
			break;
		}
		default:
			printf ("Unknown GR subop 0x%x len 0x%x dll %d\n",
				GR_BIFF_OPCODE(data),
				GR_BIFF_LENGTH(data), data_len_left);
			break;
		}

		data         +=GR_BIFF_LENGTH(data)+4;
		data_len_left-=GR_BIFF_LENGTH(data)+4;
	}
}

struct ms_excel_chart_state
{
	int chart_depth;
};

gboolean
ms_chart_biff_read (MS_EXCEL_WORKBOOK * wb,
		    BIFF_QUERY * q)
{
	switch (q->opcode)
	{
	case BIFF_CHART_UNITS :
	case BIFF_CHART_CHART :
	case BIFF_CHART_SERIES :
	case BIFF_CHART_DATAFORMAT :
	case BIFF_CHART_LINEFORMAT :
	case BIFF_CHART_MARKERFORMAT :
	case BIFF_CHART_AREAFORMAT :
	case BIFF_CHART_PIEFORMAT :
	case BIFF_CHART_ATTACHEDLABEL :
	case BIFF_CHART_SERIESTEXT :
	case BIFF_CHART_CHARTFORMAT :
	case BIFF_CHART_LEGEND :
	case BIFF_CHART_SERIESLIST :
	case BIFF_CHART_BAR :
	case BIFF_CHART_LINE :
	case BIFF_CHART_PIE :
	case BIFF_CHART_AREA :
	case BIFF_CHART_SCATTER :
	case BIFF_CHART_CHARTLINE :
	case BIFF_CHART_AXIS :
	case BIFF_CHART_TICK :
	case BIFF_CHART_VALUERANGE :
	case BIFF_CHART_CATSERRANGE :
	case BIFF_CHART_AXISLINEFORMAT :
	case BIFF_CHART_CHARTFORMATLINK :
	case BIFF_CHART_DEFAULTTEXT :
	case BIFF_CHART_TEXT :
	case BIFF_CHART_FONTX :
	case BIFF_CHART_OBJECTLINK :
	case BIFF_CHART_FRAME :
	    break;

	case BIFF_CHART_BEGIN :
	    ++ms_chart_chart_depth;
	    printf("%d : Chart Begin\n", ms_chart_chart_depth);
	    break;

	case BIFF_CHART_END :
	    printf("%d : Chart End\n", ms_chart_chart_depth);
	    if (0 == --ms_chart_chart_depth)
		    puts("---------------");
	    break;

	case BIFF_CHART_PLOTAREA :
	case BIFF_CHART_3D :
	case BIFF_CHART_PICF :
	case BIFF_CHART_DROPBAR :
	case BIFF_CHART_RADAR :
	case BIFF_CHART_SURF :
	case BIFF_CHART_RADARAREA :
	case BIFF_CHART_AXISPARENT :
	case BIFF_CHART_LEGENDXN :
	case BIFF_CHART_SHTPROPS :
	case BIFF_CHART_SERTOCRT :
	case BIFF_CHART_AXESUSED :
	case BIFF_CHART_SBASEREF :
	case BIFF_CHART_SERPARENT :
	case BIFF_CHART_SERAUXTREND :
	case BIFF_CHART_IFMT :
	case BIFF_CHART_POS :
	case BIFF_CHART_ALRUNS :
	case BIFF_CHART_AI :
	case BIFF_CHART_SERAUXERRBAR :
	case BIFF_CHART_SERFMT :
	case BIFF_CHART_FBI :
	case BIFF_CHART_BOPPOP :
	case BIFF_CHART_AXCEXT :
	case BIFF_CHART_DAT :
	case BIFF_CHART_PLOTGROWTH :
	case BIFF_CHART_SIINDEX :
	case BIFF_CHART_GELFRAME :
	case BIFF_CHART_BOPPOPCUSTOM :
	    break;

	default :
		/* "Unknown Chart BIFF record */
		return FALSE;
	}
	return TRUE;
}
