/**
 * ms-obj.c: MS Excel Object support for Gnumeric
 *
 * Author:
 *    Michael Meeks (michael@imaginator.com)
 *    Jody Goldberg (jgoldberg@home.com)
 **/

#include "ms-obj.h"
#include "ms-chart.h"

extern int ms_excel_read_debug;

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
        "TextBox",	/* 0x06 */
        "Button",	/* 0x07 */
        "Picture",	/* 0x08 */
        "Polygon",	/* 0x09 */
        NULL,		/* 0x0A */
        "CheckBox",	/* 0x0B */
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

static void
ms_obj_read_text_impl (BiffQuery *q, ExcelWorkbook * wb)
{
	static char const * const orientations[] = {
	    "Left to right",
	    "Top to Bottom",
	    "Bottom to Top on Side",
	    "Top to Bottom on Side"
	};
	static char const * const haligns[] = {
	    "At left", "Horizontaly centered",
	    "At right", "Horizontaly justified"
	};
	static char const * const valigns[] = {
	    "At top", "Verticaly centered",
	    "At bottom", "Verticaly justified"
	};

	guint16 const options = BIFF_GET_GUINT16 (q->data);
	guint16 const orient = BIFF_GET_GUINT16 (q->data+2);
	guint16 const text_len = BIFF_GET_GUINT16 (q->data+10);
	guint16 const num_formats = BIFF_GET_GUINT16 (q->data+12);
	int const halign = (options >> 1) & 0x7;
	int const valign = (options >> 4) & 0x7;
	char * text = g_new(char, text_len+1);
	guint8 const unicode_flag = BIFF_GET_GUINT8 (q->data+18);
	guchar const * ptr;
	int i, increment = 1;

	g_return_if_fail (orient <= 3);
	g_return_if_fail (1 <= halign && halign <= 4);
	g_return_if_fail (1 <= valign && valign <= 4);
	g_return_if_fail (num_formats >= 2);

	/* MS-Documentation error.  The offset for the reserved 4 x 0 is 18 */
	if (unicode_flag)
	{
		static gboolean first = TRUE;
		if (first)
		{
			first = FALSE;
			g_warning ("EXCEL : Unicode text is unsupported");
		}
		increment = 2;
		ptr = q->data + 20;
	} else
		ptr = q->data + 19;

	for (i = 0; i < text_len ; ++i)
		text[i] = ptr[i*increment];
	text[text_len] = '\0';

	/* FIXME : Should I worry about padding between the records ? */
	for (i = 0; i < num_formats ; ++i)
	{
	    /* TODO TODO finish */
	}

	if (ms_excel_read_debug > 0)
	{
		printf ("{ TextObject\n");
		printf ("Text '%s'\n", text);
		printf ("is %s, %s & %s;\n",
			orientations[orient], haligns[halign], valigns[valign]);
		printf ("}; /* TextObject */\n");
	}
}
static void
ms_obj_read_text (BiffQuery *q, ExcelWorkbook * wb, int const id)
{
	/* next record must be a DRAWING followed by a TXO */
	g_return_if_fail (ms_biff_query_next (q));
	g_return_if_fail (q->opcode == BIFF_MS_O_DRAWING);

	if (ms_excel_read_debug > 0)
		dump (q->data, q->length);

	/* and finally a TXO */
	g_return_if_fail (ms_biff_query_next (q));
	g_return_if_fail (q->opcode == BIFF_TXO);
	ms_obj_read_text_impl (q, wb);
}

static void
ms_obj_dump (guint8 const * const data, int const len, char const * const name)
{
	if (ms_excel_read_debug == 0)
		return;

	printf ("{ %s \n", name);
	dump (data+4, len);
	printf ("}; /* %s */\n", name);
}

static void
ms_obj_read_biff8_obj (BiffQuery *q, ExcelWorkbook * wb)
{
	guint8 *data;
	gint32 data_len_left;
	int obj_type = -1; /* Set to undefined */
	int obj_id = -1;
	gboolean hit_end = FALSE;

	g_return_if_fail (q);
	g_return_if_fail (q->ls_op == BIFF_OBJ);

	data = q->data;
	data_len_left = q->length;

	/* Scan through the pseudo BIFF substream */
	while (data_len_left > 0 && !hit_end) {
		guint16 const record_type = BIFF_GET_GUINT16(data);

		/* All the sub-records seem to have this layout */
		guint16 const len = BIFF_GET_GUINT16(data+2);

		/* 1st record must be COMMON_OBJ*/
		g_return_if_fail (obj_type >= 0 ||
				  record_type == GR_COMMON_OBJ_DATA);

		switch (record_type) {
		case GR_END:
			g_return_if_fail (len == 0);
			hit_end = TRUE;
			break;

		case GR_MACRO :
			ms_obj_dump (data+4, len, "MacroObject");
			break;

		case GR_COMMAND_BUTTON :
			ms_obj_dump (data+4, len, "CommandButton");
			break;

		case GR_GROUP_BUTTON :
			ms_obj_dump (data+4, len, "GroupButton");
			break;

		case GR_CLIPBOARD_FORMAT:
			ms_obj_dump (data+4, len, "ClipboardFmt");
			break;

		case GR_PICTURE_OPTIONS:
			ms_obj_dump (data+4, len, "PictOpt");
			break;

		case GR_PICTURE_FORMULA:
			ms_obj_dump (data+4, len, "PictFormula");
			break;

		case GR_CHECKBOX_LINK :
			ms_obj_dump (data+4, len, "CheckboxLink");
			break;

		case GR_RADIO_BUTTON :
			ms_obj_dump (data+4, len, "RadioButton");
			break;

		case GR_SCROLLBAR :
			ms_obj_dump (data+4, len, "ScrollBar");
			break;

		case GR_NOTE_STRUCTURE :
			ms_obj_dump (data+4, len, "Note");
			break;

		case GR_SCROLLBAR_FORMULA :
			ms_obj_dump (data+4, len, "ScrollbarFmla");
			break;

		case GR_GROUP_BOX_DATA :
			ms_obj_dump (data+4, len, "GroupBoxData");
			break;

		case GR_EDIT_CONTROL_DATA :
			ms_obj_dump (data+4, len, "EditCtrlData");
			break;

		case GR_RADIO_BUTTON_DATA :
			ms_obj_dump (data+4, len, "RadioData");
			break;

		case GR_CHECKBOX_DATA :
			ms_obj_dump (data+4, len, "CheckBoxData");
			break;

		case GR_LISTBOX_DATA :
			ms_obj_dump (data+4, len, "ListBoxData");
			break;

		case GR_CHECKBOX_FORMULA :
			ms_obj_dump (data+4, len, "CheckBoxFmla");
			break;

		case GR_COMMON_OBJ_DATA:
		{
			char const *type_name = NULL;
			guint16 const options =BIFF_GET_GUINT16(data+8);

			/* Multiple objects in 1 record ?? */
			g_return_if_fail (obj_type == -1);

			obj_type = BIFF_GET_GUINT16(data+4);
			obj_id = BIFF_GET_GUINT16(data+6);
			if (obj_type<sizeof(object_type_names)/sizeof(char*))
				type_name =object_type_names[obj_type];
			else
				type_name = "Unknown";

			/* only print when debug is enabled */
			if (ms_excel_read_debug == 0)
				break;

			printf ("\n\nObject (%d) is a '%s'\n", obj_id, type_name);

			if (options&0x0001)
				printf ("The %s is Locked\n", type_name);
			if (options&0x0010)
				printf ("The %s is Printable\n", type_name);
			if (options&0x2000)
				printf ("The %s is AutoFilled\n", type_name);
			if (options&0x4000)
				printf ("The %s has AutoLines\n", type_name);

			if ((options & 0x9fee) != 0)
				printf ("WARNING : Why is option not 0 (%x)\n",
					options & 0x9fee);
		}
		break;

		default:
			printf ("Unknown Obj record 0x%x len 0x%x dll %d\n",
				record_type, len, data_len_left);
		}
		data += len+4,
		data_len_left -= len+4;
	}

	/* The ftEnd record should have been the last */
	g_return_if_fail (data_len_left == 0);

	/* If this was a Chart then there should be a BOF next */
	switch (obj_type)
	{
	case 0x05 : ms_excel_read_chart (q, wb, obj_id); break;
	case 0x06 : ms_obj_read_text (q, wb, obj_id); break;
	default:
		    break;
	}

	if (ms_excel_read_debug > 0)
	    printf ("\n\n");
}

void
ms_obj_read_obj (BiffQuery *q, ExcelWorkbook * wb)
{
	if (wb->ver >= eBiffV8)
		ms_obj_read_biff8_obj (q, wb);
	else
		printf ("Only Biff8 Objects are supporting currently\n");
}
