/**
 * ms-obj.c: MS Excel Object support for Gnumeric
 *
 * Author:
 *    Jody Goldberg (jgoldberg@home.com)
 *    Michael Meeks (mmeeks@gnu.org)
 *
 * (C) 1998-2001 Jody Goldberg, Michael Meeks
 **/

#include <config.h>

#include "boot.h"
#include "ms-obj.h"
#include "ms-chart.h"
#include "ms-escher.h"
#include "parse-util.h"
#include "sheet-object-widget.h"
#include "sheet-object-graphic.h"

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
	if (obj) {
		if (obj->gnum_obj) {
			gtk_object_unref (obj->gnum_obj);
			obj->gnum_obj = NULL;
		}
		g_free (obj);
	}
}

/* S59EOE.HTM */
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
		increment = (MS_OLE_GET_GUINT8 (q->data)) ? 2 : 1;
		data = q->data + 1;

		/*
		 * FIXME: Use biff_get_text or something ?
		 */
		if ((int)q->length < increment * text_len) {
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

#ifndef NO_DEBUG_EXCEL
#define ms_obj_dump(data, len, data_left, name) ms_obj_dump_impl (data, len, data_left, name)
static void
ms_obj_dump_impl (guint8 const *data, int len, int data_left, char const *name)
{
	if (ms_excel_object_debug < 2)
		return;

	printf ("{ %s \n", name);
	if (len+4 > data_left) {
		printf ("/* invalid length %d (0x%x) > %d(0x%x)*/\n",
			len+4, len+4, data_left, data_left);
		len = data_left - 4;
	}
	if (ms_excel_object_debug > 2)
		ms_ole_dump (data, len+4);
	printf ("}; /* %s */\n", name);
}
#else
#define ms_obj_dump (data, len, data_left, name)
#endif

/* S59DAD.HTM */
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

/* S59DAD.HTM */
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

#if 0
	dump_biff (q);
#endif

	/* Scan through the pseudo BIFF substream */
	while (data_len_left > 0 && !hit_end) {
		guint16 const record_type = MS_OLE_GET_GUINT16(data);

		/* All the sub-records seem to have this layout
		 * 2001/Mar/29 JEG : liars.  Ok not all records have this
		 * layout.  Create a list box.  It seems to do something
		 * unique.  It acts like an end, and has no length specified.
		 */
		guint16 len = MS_OLE_GET_GUINT16(data+2);

		/* 1st record must be COMMON_OBJ*/
		g_return_val_if_fail (obj->excel_type >= 0 ||
				      record_type == GR_COMMON_OBJ_DATA,
				      TRUE);

		switch (record_type) {
		case GR_END:
			g_return_val_if_fail (len == 0, TRUE);
			ms_obj_dump (data, len, data_len_left, "ObjEnd");
			hit_end = TRUE;
			break;

		case GR_MACRO :
			ms_obj_dump (data, len, data_len_left, "MacroObject");
			break;

		case GR_COMMAND_BUTTON :
			ms_obj_dump (data, len, data_len_left, "CommandButton");
			break;

		case GR_GROUP_BUTTON :
			ms_obj_dump (data, len, data_len_left, "GroupButton");
			break;

		case GR_CLIPBOARD_FORMAT :
			ms_obj_dump (data, len, data_len_left, "ClipboardFmt");
			break;

		case GR_PICTURE_OPTIONS :
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

		case GR_PICTURE_FORMULA :
			ms_obj_dump (data, len, data_len_left, "PictFormula");
			break;

		case GR_CHECKBOX_LINK :
			ms_obj_dump (data, len, data_len_left, "CheckboxLink");
			break;

		case GR_RADIO_BUTTON :
			ms_obj_dump (data, len, data_len_left, "RadioButton");
			break;

		case GR_SCROLLBAR :
			ms_obj_dump (data, len, data_len_left, "ScrollBar");
			break;

		case GR_NOTE_STRUCTURE :
			ms_obj_dump (data, len, data_len_left, "Note");
			break;

		case GR_SCROLLBAR_FORMULA :
			/* A touch of spelunking suggests that
			 * 0x06 uint16 == first visible element (0 based)
			 * 0x12 uint16 == number of elements
			 * 0x14 uint16 == current element (1 based)
			 */
			ms_obj_dump (data, len, data_len_left, "ScrollbarFmla");
			break;

		case GR_GROUP_BOX_DATA :
			ms_obj_dump (data, len, data_len_left, "GroupBoxData");
			break;

		case GR_EDIT_CONTROL_DATA :
			ms_obj_dump (data, len, data_len_left, "EditCtrlData");
			break;

		case GR_RADIO_BUTTON_DATA :
			ms_obj_dump (data, len, data_len_left, "RadioData");
			break;

		case GR_CHECKBOX_DATA :
			ms_obj_dump (data, len, data_len_left, "CheckBoxData");
			break;

		case GR_LISTBOX_DATA :
		{
			/* FIXME : find some docs for this
			 * It seems as if list box data does not conform to
			 * the docs.  It acts like an end and has no size.
			 */
			hit_end = TRUE;
			len = data_len_left - 4;

			ms_obj_dump (data, len, data_len_left, "ListBoxData");
			break;
		}

		case GR_CHECKBOX_FORMULA :
		{
			guint16 const row = MS_OLE_GET_GUINT16(data+11);
			guint16 const col = MS_OLE_GET_GUINT16(data+13) &0x3fff;
#ifndef NO_DEBUG_EXCEL
			if (ms_excel_object_debug > 0)
				printf ("Checkbox linked to : %s%d\n", col_name(col), row+1);
			ms_obj_dump (data, len, data_len_left, "CheckBoxFmla");
#endif
			break;
		}

		case GR_COMMON_OBJ_DATA :
		{
			guint16 const options =MS_OLE_GET_GUINT16 (data+8);

			/* Multiple objects in 1 record ?? */
			g_return_val_if_fail (obj->excel_type == -1, -1);

			obj->excel_type = MS_OLE_GET_GUINT16(data+4);
			obj->id = MS_OLE_GET_GUINT16(data+6);

#ifndef NO_DEBUG_EXCEL
			/* only print when debug is enabled */
			if (ms_excel_object_debug == 0)
				break;

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

		if (data_len_left < len+4)
			printf ("record len %d (0x%x) > %d\n", len+4, len+4, data_len_left);

		/* FIXME : We need a structure akin to the escher code to do this properly */
		for (data_len_left -= len+4; data_len_left < 0; ) {
			guint16 peek_op;

			printf ("deficit of %d\n", data_len_left);

			/* FIXME : what do we expect here ??
			 * I've seen what seem to be embedded drawings
			 * but I am not sure what is embedding what.
			 */
			if (!ms_biff_query_peek_next (q, &peek_op) ||
			    (peek_op != BIFF_CONTINUE &&
			     peek_op != BIFF_MS_O_DRAWING &&
			     peek_op != BIFF_TXO &&
			     peek_op != BIFF_OBJ)) {
				printf ("0x%x vs 0x%x\n", q->opcode, peek_op);
				return TRUE;
			}

			ms_biff_query_next (q);
			data_len_left += q->length;
			printf ("merged in 0x%x with len %d\n", q->opcode, q->length);
		}
		data = q->data + q->length - data_len_left;
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
		guint16 op;

		if (ms_biff_query_peek_next (q, &op) && op == BIFF_IMDATA) {
			printf ("Reading trailing IMDATA;\n");
			ms_biff_query_next (q);
			ms_excel_read_imdata (q);
		}
	}

	return FALSE;
}

MSObj *
ms_read_OBJ (BiffQuery *q, MSContainer *container)
{
	static char const * const object_type_names[] =
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
	MSObj *obj = g_new0 (MSObj, 1);

	obj->excel_type = (unsigned)-1; /* Set to undefined */
	obj->excel_type_name = NULL;
	obj->id = -1;
	obj->anchor_set = FALSE;
	obj->gnum_obj = NULL;

#ifndef NO_DEBUG_EXCEL
	if (ms_excel_object_debug > 0)
		printf ("{ /* OBJ start */\n");
#endif
	errors = (container->ver >= MS_BIFF_V8)
		? ms_obj_read_biff8_obj (q, container, obj)
		: ms_obj_read_pre_biff8_obj (q, container, obj);

	if (errors) {
#ifndef NO_DEBUG_EXCEL
		if (ms_excel_object_debug > 0)
			printf ("}; /* OBJ error 1 */\n");
#endif
		ms_destroy_OBJ (obj);
		return NULL;
	}

	obj->excel_type_name = NULL;
	if (obj->excel_type < (int)(sizeof(object_type_names)/sizeof(char*)))
		obj->excel_type_name = object_type_names [obj->excel_type];
	if (obj->excel_type_name == NULL)
		obj->excel_type_name = "Unknown";

#ifndef NO_DEBUG_EXCEL
	if (ms_excel_object_debug > 0) {
		printf ("Object (%d) is a '%s'\n", obj->id, obj->excel_type_name);
		printf ("}; /* OBJ end */\n");
	}
#endif

	obj->gnum_obj = (*container->vtbl->create_obj) (container, obj);
	if (obj->gnum_obj == NULL) {
		ms_destroy_OBJ (obj);
		return NULL;
	}

	/* Chart, There should be a BOF next */
	if (obj->excel_type == 0x5) {
		if (ms_excel_read_chart (q, container, obj->gnum_obj)) {
			ms_destroy_OBJ (obj);
			return NULL;
		}
	}

#if 0
	printf ("Registered object 0x%p\n", obj);
#endif
	ms_container_add_obj (container, obj);

	return obj;
}
