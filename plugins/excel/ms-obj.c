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

#include "ms-ole.h"
#include "ms-biff.h"
#include "ms-excel.h"
#include "ms-excel-biff.h"
#include "ms-obj.h"

/* Confusingly this micro-biff will use the offsets
   as specified in the docs */
#define GR_BIFF_LENGTH(p) (BIFF_GETWORD(p+2))
#define GR_BIFF_OPCODE(p) (BIFF_GETWORD(p))

#define GR_END                0x00
#define GR_MACRO              0x04
#define GR_CLIPBOARD_FORMAT   0x07
#define GR_PICTURE_OPTIONS    0x08
#define GR_PICTURE_FORMULA    0x09
#define GR_COMMON_OBJ_DATA    0x15

static char *
object_type_names[] =
{
        "Group", /* 0x00 */
        "Line",
        "Rectangle",
        "Oval",
        "Arc",
        "Chart",
        "Text",
        "Button",
        "Picture",
        "Polygon",
        NULL,
        "Check",
        "Option",
        "Edit",
        "Label",
        "Dialog",
        "Spinner",
        "Scroll",
        "List",
        "Group",
        "Combo",
        NULL,
        NULL,
        NULL,
        NULL,
        "Comment",
        NULL,
        NULL,
        NULL,
        NULL,
        "MS Drawing"
};

void
ms_obj_read_obj (MS_EXCEL_SHEET *sheet, BIFF_QUERY *q)
{
	guint8 *data;
	gint32 data_len_left;
	int hit_end;

	g_return_if_fail (q);
	g_return_if_fail (sheet);
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
		case GR_CLIPBOARD_FORMAT:
			break;
		case GR_PICTURE_OPTIONS:
			break;
		case GR_PICTURE_FORMULA:
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
				printf ("Object '%s'\n", type);
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
