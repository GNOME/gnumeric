/**
 * ms-obj.c: MS Excel Object support for Gnumeric
 *
 * Author:
 *    Jody Goldberg (jgoldberg@home.com)
 *    Michael Meeks (mmeeks@gnu.org)
 *
 * (C) 1998, 1999, 2000 Jody Goldberg, Michael Meeks
 **/

#include <config.h>

#include "ms-obj.h"
#include "ms-chart.h"
#include "ms-escher.h"
#include "parse-util.h"

int ms_excel_object_debug;

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


void
ms_destroy_OBJ (MSObj *obj)
{
	/* TODO : Fill in the blank */
	if (obj)
		g_free (obj);
}

/*
 * See: S59EOE.HTM
 */
char *
ms_read_TXO (BiffQuery *q)
{
	static char const * const orientations [] = {
	    "Left to right",
	    "Top to Bottom",
	    "Bottom to Top on Side",
	    "Top to Bottom on Side"
	};
	static char const * const haligns [] = {
	    "At left", "Horizontaly centered",
	    "At right", "Horizontaly justified"
	};
	static char const * const valigns [] = {
	    "At top", "Verticaly centered",
	    "At bottom", "Verticaly justified"
	};

	guint16 const options     = MS_OLE_GET_GUINT16 (q->data);
	guint16 const orient      = MS_OLE_GET_GUINT16 (q->data + 2);
	guint16 const text_len    = MS_OLE_GET_GUINT16 (q->data + 10);
/*	guint16 const num_formats = MS_OLE_GET_GUINT16 (q->data + 12);*/
	int const halign = (options >> 1) & 0x7;
	int const valign = (options >> 4) & 0x7;
	char         *text = g_new (char, text_len + 1);
	const guint8  unicode_flag = MS_OLE_GET_GUINT8 (q->data + 18);
	guint16       peek_op;

	g_return_val_if_fail (orient <= 3, NULL);
	g_return_val_if_fail (1 <= halign && halign <= 4, NULL);
	g_return_val_if_fail (1 <= valign && valign <= 4, NULL);

	text [0] = '\0';
	if (ms_biff_query_peek_next (q, &peek_op) &&
	    peek_op == BIFF_CONTINUE) {
		guint8 *data;
		int i, increment = 1;

		ms_biff_query_next (q);
		data = q->data;
		
		if (unicode_flag) {
			increment = 2;
			data++;
		}

		/*
		 * FIXME: Use biff_get_text or something ?
		 */
		if (q->length < increment * text_len) {
			g_free (text);
			text = g_strdup ("Broken continue");
		} else { 
			for (i = 0; i < text_len ; ++i)
				text [i] = data [i * increment];
			text [text_len] = '\0';
		} 

		if (ms_biff_query_peek_next (q, &peek_op) &&
		    peek_op == BIFF_CONTINUE)
			ms_biff_query_next (q);
		else
			g_warning ("Unusual, TXO text with no formatting");
	} else if (text_len > 0)
		g_warning ("TXO len of %d but no continue", text_len);

#ifndef NO_DEBUG_EXCEL
	if (ms_excel_object_debug > 0) {
		printf ("{ TextObject\n");
		printf ("Text '%s'\n", text);
		printf ("is %s, %s & %s;\n",
			orientations[orient], haligns[halign], valigns[valign]);
		printf ("}; /* TextObject */\n");
	}
#endif
	return text;
}

static void
ms_obj_dump (guint8 const * const data, int const len, char const * const name)
{
#ifndef NO_DEBUG_EXCEL
	if (ms_excel_object_debug < 2)
		return;

	printf ("{ %s \n", name);
	ms_ole_dump (data+4, len);
	printf ("}; /* %s */\n", name);
#endif
}


/*
 * See: S59DAD.HTM
 */
static gboolean
ms_obj_read_pre_biff8_obj (BiffQuery *q, MSContainer *container, MSObj *obj)
{
	/* TODO : Lots of docs for these things.  Write the parser. */

#if 0
	guint32 const numObjects = MS_OLE_GET_GUINT16(q->data);
	guint16 const flags = MS_OLE_GET_GUINT16(q->data+8);
#endif
	obj->excel_type = MS_OLE_GET_GUINT16(q->data + 4);
	obj->id         = MS_OLE_GET_GUINT32(q->data + 6);

	memcpy (obj->raw_anchor, q->data+8, MS_ANCHOR_SIZE);
	obj->anchor_set = TRUE;

	return TRUE;
}

/*
 * See: S59DAD.HTM
 */
static gboolean
ms_obj_read_biff8_obj (BiffQuery *q, MSContainer *container, MSObj *obj)
{
	guint8 *data;
	gint32 data_len_left;
	gboolean hit_end = FALSE;
	gboolean next_biff_record_maybe_imdata = FALSE;

	g_return_val_if_fail (q, TRUE);
	g_return_val_if_fail (q->ls_op == BIFF_OBJ, TRUE);

	data = q->data;
	data_len_left = q->length;

	/* Scan through the pseudo BIFF substream */
	while (data_len_left > 0 && !hit_end) {
		guint16 const record_type = MS_OLE_GET_GUINT16(data);

		/* All the sub-records seem to have this layout */
		guint16 const len = MS_OLE_GET_GUINT16(data+2);

		/* 1st record must be COMMON_OBJ*/
		g_return_val_if_fail (obj->excel_type >= 0 ||
				      record_type == GR_COMMON_OBJ_DATA,
				      TRUE);

		switch (record_type) {
		case GR_END:
			g_return_val_if_fail (len == 0, TRUE);
			ms_obj_dump (data, len, "ObjEnd");
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
		{
			guint16 pict_opt;
			g_return_val_if_fail (len == 2, TRUE);

			pict_opt = MS_OLE_GET_GUINT16(data+4);

#ifndef NO_DEBUG_EXCEL
			if (ms_excel_object_debug >= 1) {
				printf ("{ /* PictOpt */\n");
				printf ("value = %d;\n", pict_opt);
				printf ("}; /* PictOpt */\n");
			}
#endif

			next_biff_record_maybe_imdata = TRUE;
			break;
		}

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
#ifndef NO_DEBUG_EXCEL
			if (ms_excel_object_debug > 0)
				printf ("Checkbox linked to : %s%d\n", col_name(col), row+1);
			ms_obj_dump (data, len, "CheckBoxFmla");
#endif
			break;
		}

		case GR_COMMON_OBJ_DATA:
		{
			guint16 const options =MS_OLE_GET_GUINT16(data+8);

			/* Multiple objects in 1 record ?? */
			g_return_val_if_fail (obj->excel_type == -1, -1);

			obj->excel_type = MS_OLE_GET_GUINT16(data+4);
			obj->id = MS_OLE_GET_GUINT16(data+6);

			/* only print when debug is enabled */
			if (ms_excel_object_debug == 0)
				break;

#ifndef NO_DEBUG_EXCEL
			if (options&0x0001)
				printf ("Locked;\n");
			if (options&0x0010)
				printf ("Printable;\n");
			if (options&0x2000)
				printf ("AutoFilled;\n");
			if (options&0x4000)
				printf ("AutoLines;\n");

			if ((options & 0x9fee) != 0)
				printf ("WARNING : Why is option not 0 (%x)\n",
					options & 0x9fee);
#endif
		}
		break;

		default:
			printf ("ERROR : Unknown Obj record 0x%x len 0x%x dll %d;\n",
				record_type, len, data_len_left);
		}
		data += len+4,
		data_len_left -= len+4;

		/* FIXME : We need a structure akin to the escher code to do this properly */
		while (data_len_left < 0) {
			int const diff = data - q->data;
			guint16 peek_op;

			/* FIXME : what do we expect here ??
			 * I've seen what seem to be embedded drawings
			 * but I am not sure what is embedding what.
			 */
			g_return_val_if_fail (ms_biff_query_peek_next (q, &peek_op) &&
					      peek_op == BIFF_CONTINUE, TRUE);
			ms_biff_query_next (q);
			data = q->data + diff;
			data_len_left += q->length;
		}
	}

	/* The ftEnd record should have been the last */
	if (data_len_left > 0) {
		printf("OBJ : unexpected extra data after Object End record;\n");
		ms_ole_dump (data, data_len_left);
		return TRUE;
	}       

	/* Catch underflow too */
	g_return_val_if_fail (data_len_left == 0, TRUE);

	/* FIXME : Throw away the IMDATA that may follow.
	 * I am not sure when the IMDATA does follow, or how to display it,
	 * but very careful in case it is not there.
	 */
	if (next_biff_record_maybe_imdata) {
		guint16 opcode;

		if (ms_biff_query_peek_next (q, &opcode) &&
		    opcode == BIFF_IMDATA) {
			g_return_val_if_fail (ms_biff_query_next (q), TRUE);
		}
	}

	return FALSE;
}

MSObj *
ms_read_OBJ (BiffQuery *q, MSContainer *container)
{
	static char * object_type_names[] =
	{
		"Group", 	/* 0x00 */
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
		NULL, NULL, NULL, NULL, /* 0x15 - 0x18 */
		"Comment",	/* 0x19 */
		NULL, NULL, NULL, NULL,	/* 0x1A - 0x1D */
		"MS Drawing"	/* 0x1E */
	};

	gboolean errors;
	SheetObjectType type;
	gchar const * type_name = NULL;
	MSObj * obj = g_new(MSObj, 1);
	obj->excel_type = (unsigned)-1; /* Set to undefined */
	obj->id = -1;
	obj->anchor_set = FALSE;

#ifndef NO_DEBUG_EXCEL
	if (ms_excel_object_debug > 0)
		printf ("{ /* OBJ start */\n");
#endif
	errors = (container->ver >= MS_BIFF_V8)
		? ms_obj_read_biff8_obj (q, container, obj)
		: ms_obj_read_pre_biff8_obj (q, container, obj);

	if (errors) {
		g_free (obj);
#ifndef NO_DEBUG_EXCEL
		if (ms_excel_object_debug > 0)
			printf ("}; /* OBJ error 1 */\n");
#endif
		return NULL;
	}

	if (obj->excel_type < sizeof(object_type_names)/sizeof(char*))
		type_name = object_type_names[obj->excel_type];
	if (type_name == NULL)
		type_name = "Unknown";

	switch (obj->excel_type) {
	case 0x05 : /* Chart */
		type = SHEET_OBJECT_BOX;
		/* There should be a BOF next */
		ms_excel_read_chart (q, container);
		break;

	case 0x01 : /* Line */
		type = SHEET_OBJECT_LINE;
		break;

	case 0x02 : /* Rectangle */
		type = SHEET_OBJECT_BOX;
		break;

	case 0x03 : /* Oval */
		type = SHEET_OBJECT_OVAL;
		break;

	case 0x06 : /* TextBox */
		type = SHEET_OBJECT_BOX;
		break;

	case 0x07 : /* Button */
		type = SHEET_OBJECT_BUTTON;
		break;

	case 0x08 : /* Picture */
		type = SHEET_OBJECT_GRAPHIC;
		break;

	case 0x0B : /* CheckBox */
		type = SHEET_OBJECT_CHECKBOX;
		break;

	case 0x0E : /* Label */
		type = SHEET_OBJECT_BUTTON;
		break;

	case 0x19 : /* Comment */
		type = -1; /* FIXME : Invalid */
		break;

	default :
		printf ("EXCEL : unhandled excel object of type %s (0x%x) id = %d\n",
			type_name, obj->excel_type, obj->id);
		g_free(obj);
		printf ("}; /* OBJ error 2 */\n");
		return NULL;
	}

	obj->gnumeric_type = type;

#ifndef NO_DEBUG_EXCEL
	if (ms_excel_object_debug > 0) {
		printf ("Object (%d) is a '%s'\n", obj->id, type_name);
		printf ("}; /* OBJ end */\n");
	}
#endif

	ms_container_add_obj (container, obj);

	return obj;
}
