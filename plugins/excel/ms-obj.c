/**
 * ms-obj.c: MS Excel Object support for Gnumeric
 *
 * Author:
 *    Michael Meeks (michael@imaginator.com)
 *    Jody Goldberg (jgoldberg@home.com)
 **/

#include "ms-obj.h"
#include "ms-chart.h"
#include "ms-escher.h"
#include "utils.h"

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

void
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

	guint16 const options = MS_OLE_GET_GUINT16 (q->data);
	guint16 const orient = MS_OLE_GET_GUINT16 (q->data+2);
	guint16 const text_len = MS_OLE_GET_GUINT16 (q->data+10);
	guint16 const num_formats = MS_OLE_GET_GUINT16 (q->data+12);
	int const halign = (options >> 1) & 0x7;
	int const valign = (options >> 4) & 0x7;
	char * text = g_new(char, text_len+1);
	guint8 const unicode_flag = MS_OLE_GET_GUINT8 (q->data+18);
	guchar const * ptr;
	int i, increment = 1;

	g_return_if_fail (orient <= 3);
	g_return_if_fail (1 <= halign && halign <= 4);
	g_return_if_fail (1 <= valign && valign <= 4);

	/* TODO : figure this out.  There seem to be strings with 0 formats too.
	 * do they indicate empty strings ? */
	if (num_formats < 2) {
		g_warning ("EXCEL : docs state that there should be >= 2 formats.  "
			   "This record has %d", num_formats);
		return;
	}

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
void
ms_obj_read_text (BiffQuery *q, ExcelWorkbook * wb, int const id)
{
	/* next record must be a DRAWING, it will load the TXO records */
	g_return_if_fail (ms_biff_query_next (q));
	ms_escher_hack_get_drawing (q, wb, NULL);
}

static void
ms_obj_dump (guint8 const * const data, int const len, char const * const name)
{
	if (ms_excel_read_debug < 2)
		return;

	printf ("{ %s \n", name);
	dump (data+4, len);
	printf ("}; /* %s */\n", name);
}


static void
ms_obj_read_pre_biff8_obj (BiffQuery *q, ExcelWorkbook * wb)
{
	static char const * const obj_types[] = {
	    "Group", "Line", "Rectangle", "Oval", "Arc", "Chart", "Text",
	    "Button", "Picture", "Polygon", "Checkbox", "OptionButton",
	    "Edit box", "Label", "Dialog frame", "Spinner", "Listbox",
	    "Group box", "Dropdown"
	};
#if 0
	guint32 const numObjects = MS_OLE_GET_GUINT32(q->data);
	guint16 const flags = MS_OLE_GET_GUINT32(q->data+8);
#endif
	guint16 tmp = MS_OLE_GET_GUINT32(q->data+4);
	guint16 const obj_id = MS_OLE_GET_GUINT32(q->data+6);

	guint16 const left_col = MS_OLE_GET_GUINT32(q->data+10);
	guint16 const top_row = MS_OLE_GET_GUINT32(q->data+14);
	guint16 const right_col = MS_OLE_GET_GUINT32(q->data+18);
	guint16 const bottom_row = MS_OLE_GET_GUINT32(q->data+22);

	/* As 1/1024 of cell width */
	guint16 const left_offset = MS_OLE_GET_GUINT32(q->data+12);
	guint16 const top_offset = MS_OLE_GET_GUINT32(q->data+16);
	guint16 const right_offset = MS_OLE_GET_GUINT32(q->data+20);
	guint16 const bottom_offset = MS_OLE_GET_GUINT32(q->data+24);

	if (tmp >= (sizeof(obj_types)/sizeof(char const * const))) {
		printf ("EXCEL : invalid object type %d\n", tmp);
		return;
	}

	if (ms_excel_read_debug > 0) {
		printf ("EXCEL : Found %s @ (%s%d + %f %%, %f %%):(%s%d + %f %%, %f %%)\n",
			obj_types[tmp],
			col_name(left_col), top_row+1,
			left_offset/1024., top_offset/1024.,
			col_name(right_col), bottom_row+1,
			right_offset/1024., bottom_offset/1024.);
	}

	if (tmp == 0x5)
		ms_excel_read_chart (q, wb, obj_id);
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
		guint16 const record_type = MS_OLE_GET_GUINT16(data);

		/* All the sub-records seem to have this layout */
		guint16 const len = MS_OLE_GET_GUINT16(data+2);

		/* 1st record must be COMMON_OBJ*/
		g_return_if_fail (obj_type >= 0 ||
				  record_type == GR_COMMON_OBJ_DATA);

		switch (record_type) {
		case GR_END:
			g_return_if_fail (len == 0);
			hit_end = TRUE;
			break;

		case GR_MACRO :
			ms_obj_dump (data, len, "MacroObject");
			break;

		case GR_COMMAND_BUTTON :
			ms_obj_dump (data, len, "CommandButton");
			break;

		case GR_GROUP_BUTTON :
			ms_obj_dump (data, len, "GroupButton");
			break;

		case GR_CLIPBOARD_FORMAT:
			ms_obj_dump (data, len, "ClipboardFmt");
			break;

		case GR_PICTURE_OPTIONS:
			ms_obj_dump (data, len, "PictOpt");
			break;

		case GR_PICTURE_FORMULA:
			ms_obj_dump (data, len, "PictFormula");
			break;

		case GR_CHECKBOX_LINK :
			ms_obj_dump (data, len, "CheckboxLink");
			break;

		case GR_RADIO_BUTTON :
			ms_obj_dump (data, len, "RadioButton");
			break;

		case GR_SCROLLBAR :
			ms_obj_dump (data, len, "ScrollBar");
			break;

		case GR_NOTE_STRUCTURE :
			ms_obj_dump (data, len, "Note");
			break;

		case GR_SCROLLBAR_FORMULA :
			ms_obj_dump (data, len, "ScrollbarFmla");
			break;

		case GR_GROUP_BOX_DATA :
			ms_obj_dump (data, len, "GroupBoxData");
			break;

		case GR_EDIT_CONTROL_DATA :
			ms_obj_dump (data, len, "EditCtrlData");
			break;

		case GR_RADIO_BUTTON_DATA :
			ms_obj_dump (data, len, "RadioData");
			break;

		case GR_CHECKBOX_DATA :
			ms_obj_dump (data, len, "CheckBoxData");
			break;

		case GR_LISTBOX_DATA :
			ms_obj_dump (data, len, "ListBoxData");
			break;

		case GR_CHECKBOX_FORMULA :
		{
			guint16 const row = MS_OLE_GET_GUINT16(data+11);
			guint16 const col = MS_OLE_GET_GUINT16(data+13) &0x3fff;
			if (ms_excel_read_debug > 0)
				printf ("Checkbox linked to : %s%d\n", col_name(col), row+1);
			ms_obj_dump (data, len, "CheckBoxFmla");
			break;
		}

		case GR_COMMON_OBJ_DATA:
		{
			char const *type_name = NULL;
			guint16 const options =MS_OLE_GET_GUINT16(data+8);

			/* Multiple objects in 1 record ?? */
			g_return_if_fail (obj_type == -1);

			obj_type = MS_OLE_GET_GUINT16(data+4);
			obj_id = MS_OLE_GET_GUINT16(data+6);
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
	case 0x05 :
		ms_excel_read_chart (q, wb, obj_id);
		break;

	case 0x02 : /* Text Box */
	case 0x06 : /* Text Box */
	case 0x07 : /* Button */
		ms_obj_read_text (q, wb, obj_id);
		break;

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
		ms_obj_read_pre_biff8_obj (q, wb);
}
