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
#include "ms-chart.h"

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
ms_obj_read_obj (BIFF_QUERY *q, MS_EXCEL_WORKBOOK * wb)
{
	guint8 *data;
	gint32 data_len_left;
	int obj_type = -1; /* Set to undefined */
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
			guint16 obj_id  =BIFF_GETWORD(data+6);
			guint16 options =BIFF_GETWORD(data+8);
			char *type = NULL;
			enum { Locked=0x1, Printable=0x2,
			       AutoFill=0x4, AutoLine=0x8 } flags;

		        /* Multiple objects in 1 record ?? */
		        g_return_if_fail (obj_type == -1);
			obj_type = BIFF_GETWORD(data+4);

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
			if (type) {
				printf ("Object '%s'\n", type);
			} else
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

	if (obj_type == 0x05)
	    ms_excel_read_chart (wb, q);
}
