/**
 * ms-obj.c: MS Excel Object support for Gnumeric
 *
 * Author:
 *    Jody Goldberg (jgoldberg@home.com)
 *    Michael Meeks (mmeeks@gnu.org)
 *
 * (C) 1998, 1999 Jody Goldberg, Michael Meeks
 **/

#include <config.h>
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

/**
 * object_anchor_to_position:
 * @points	Array which receives anchor coordinates in points
 * @obj         The object
 * @sheet	The sheet
 *
 * Converts anchor coordinates in Excel units to points. Anchor
 * coordinates are x and y of upper left and lower right corner. Each
 * is expressed as a pair: Row/cell number + position within cell as
 * fraction of cell dimension.
 *
 * NOTE: According to docs, position within cell is expressed as
 * 1/1024 of cell dimension. However, this doesn't seem to be true
 * vertically, for Excel 97. We use 256 for >= XL97 and 1024 for
 * preceding.
  */
static void
object_anchor_to_position (double pixels[4], MSObj*obj, Sheet const * sheet,
			   eBiff_version const ver)
{
	float const row_denominator = (ver >= eBiffV8) ? 256. : 1024.;

	int	i;

#ifndef NO_DEBUG_EXCEL
	if (ms_excel_read_debug > 0)
		printf ("%s\n", sheet->name);
#endif
	for (i = 0; i < 4; i++) {
		int const pos   = obj->anchor[i].pos;
		int const nths  = obj->anchor[i].nths;

		if (i & 1) { /* odds are rows */
			ColRowInfo const *ri = sheet_row_get_info (sheet, pos);

			/* warning logged elsewhere */
			if (ri != NULL) {
				pixels[i] = ri->size_pixels;
				pixels[i] *= nths / row_denominator;
			} else
				pixels[i] = 0;
			pixels[i] += sheet_row_get_distance_pixels (sheet, 0, pos);

#ifndef NO_DEBUG_EXCEL
			if (ms_excel_read_debug > 0)
				printf ("%d + %d\n", pos+1, nths);
#endif
		} else {
			ColRowInfo const *ci = sheet_col_get_info (sheet, pos);

			/* warning logged elsewhere */
			if (ci != NULL) {
				pixels[i] = ci->size_pixels;
				pixels[i] *= nths / 1024.;
			} else
				pixels[i] = 0;
			pixels[i] += sheet_col_get_distance_pixels (sheet, 0, pos);

#ifndef NO_DEBUG_EXCEL
			if (ms_excel_read_debug > 0)
				printf ("%s + %d\n", col_name(pos), nths);
#endif
		}
	}

#ifndef NO_DEBUG_EXCEL
	if (ms_excel_read_debug > 0)
		printf ("Anchor position in pixels"
			" left = %g, top = %g, right = %g, bottom = %g;\n",
			pixels[0], pixels[1], pixels[2], pixels[3]);
#endif
}

/*
 * Attempt to install an object in supplied work book.
 *
 * Return TRUE on failure, FALSE on success.
 */
gboolean
ms_obj_realize (MSObj *obj, ExcelWorkbook *wb, ExcelSheet *sheet)
{
	double   position[4];

	g_return_val_if_fail (sheet != NULL, TRUE);

	if (obj == NULL)
		return TRUE;

	object_anchor_to_position (position, obj, sheet->gnum_sheet,
				   wb->ver);

	/* Handle Comments */
	if (wb->ver >= eBiffV8 && obj->excel_type == 0x19) {
	}

	switch (obj->gnumeric_type) {
	case SHEET_OBJECT_BUTTON :
		sheet_object_create_button (sheet->gnum_sheet,
					    position[0], position[1],
					    position[2], position[3]);
		break;

	case SHEET_OBJECT_CHECKBOX :
		sheet_object_create_checkbox (sheet->gnum_sheet,
					      position[0], position[1],
					      position[2], position[3]);
		break;

	case SHEET_OBJECT_BOX :
		sheet_object_realize (
		sheet_object_create_filled (sheet->gnum_sheet,
					    SHEET_OBJECT_BOX,
					    position[0], position[1],
					    position[2], position[3],
					    "white", "black", 1));
		break;

	case SHEET_OBJECT_GRAPHIC : /* If this was a picture */
	{
		int blip_id;
		GPtrArray const * blips = wb->blips;
		EscherBlip *blip = NULL;
#ifdef ENABLE_BONOBO
		SheetObject  *so;
#endif

		blip_id = obj->v.picture.blip_id;

		g_return_val_if_fail (blip_id >= 0, FALSE);
		g_return_val_if_fail (blip_id < blips->len, FALSE);

		blip = g_ptr_array_index (blips, blip_id);

		g_return_val_if_fail (blip != NULL, FALSE);

#ifdef ENABLE_BONOBO
		g_return_val_if_fail (blip->stream != NULL, FALSE);
		g_return_val_if_fail (blip->reproid != NULL, FALSE);
		so = sheet_object_container_new (sheet->gnum_sheet,
						 position[0], position[1],
						 position[2], position[3],
						 blip->reproid);
		if (!sheet_object_bonobo_load (SHEET_OBJECT_BONOBO (so), blip->stream))
			g_warning ("Failed to load '%s' from stream",
				   blip->reproid);
#endif
	}
	break;

	default :
	break;
	};

	return FALSE;
}

static void
ms_obj_destroy (MSObj *obj)
{
	if (obj) {
		g_free (obj);
	}
}

/**
 * ms_excel_sheet_realize_objs:
 * @sheet:
 *
 *   This realizes the objects after the zoom factor has been
 * loaded.
 **/
void
ms_excel_sheet_realize_objs (ExcelSheet *sheet)
{
	GList *l;

	g_return_if_fail (sheet != NULL);

	for (l = sheet->obj_queue; l; l = g_list_next (l))
		ms_obj_realize (l->data, sheet->wb, sheet);
}

void
ms_excel_sheet_destroy_objs (ExcelSheet *sheet)
{
	GList *l;

	g_return_if_fail (sheet != NULL);

	for (l = sheet->obj_queue; l; l = g_list_next (l))
		ms_obj_destroy (l->data);

	g_list_free (sheet->obj_queue);
	sheet->obj_queue = NULL;
}

gboolean
ms_parse_object_anchor (anchor_point anchor[4],
			Sheet const * sheet, guint8 const * data)
{
	/* Words 0, 4, 8, 12 : The row/col of the corners */
	/* Words 2, 6, 10, 14 : distance from cell edge */

	int	i;

	for (i = 0; i < 4; ++i) {
		anchor[i].pos = MS_OLE_GET_GUINT16 (data + 4 * i);
		anchor[i].nths = MS_OLE_GET_GUINT16 (data + 4 * i + 2);

#ifndef NO_DEBUG_EXCEL
		if (ms_excel_read_debug > 1) {
			int pos  = anchor[i].pos;
			printf ("%d/%d cell %s from ",
				anchor[i].nths, (i & 1) ? 256 : 1024,
				(i & 1) ? "heights" : "widths");
			if (i & 1)
				printf ("row %d;\n", pos + 1);
			else
				printf ("col %s (%d);\n", col_name(pos), pos);
		}
#endif
	}

	return FALSE;
}

/*
 * See: S59EOE.HTM
 */
char *
ms_read_TXO (BiffQuery *q, ExcelWorkbook * wb)
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

	g_return_val_if_fail (orient <= 3, NULL);
	g_return_val_if_fail (1 <= halign && halign <= 4, NULL);
	g_return_val_if_fail (1 <= valign && valign <= 4, NULL);

#if 0
	/* TODO : figure this out.  There seem to be strings with 0 formats too.
	 * do they indicate empty strings ? */
	if (num_formats < 2) {
		g_warning ("EXCEL : docs state that there should be >= 2 formats.  "
			   "This record has %d", num_formats);
		return;
	}
#endif

	/* MS-Documentation error.  The offset for the reserved 4 x 0 is 18 */
	if (unicode_flag) {
		static gboolean first = TRUE;
		if (first) {
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
	for (i = 0; i < num_formats ; ++i) {
	    /* TODO TODO finish */
	}

#ifndef NO_DEBUG_EXCEL
	if (ms_excel_read_debug > 0) {
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
	if (ms_excel_read_debug < 2)
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
ms_obj_read_pre_biff8_obj (BiffQuery *q, ExcelWorkbook * wb,
			   Sheet * sheet, MSObj * obj)
{
	/* TODO : Lots of docs for these things.  Write the parser. */

#if 0
	guint32 const numObjects = MS_OLE_GET_GUINT16(q->data);
	guint16 const flags = MS_OLE_GET_GUINT16(q->data+8);
#endif
	obj->excel_type = MS_OLE_GET_GUINT16(q->data + 4);
	obj->id         = MS_OLE_GET_GUINT32(q->data + 6);

	return ms_parse_object_anchor (obj->anchor, sheet, q->data+10);
}

/*
 * See: S59DAD.HTM
 */
static gboolean
ms_obj_read_biff8_obj (BiffQuery *q, ExcelWorkbook * wb, Sheet * sheet, MSObj * obj)
{
	guint8 *data;
	gint32 data_len_left;
	gboolean hit_end = FALSE;
	gboolean next_biff_record_is_imdata = FALSE;

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
			if (ms_excel_read_debug >= 1) {
				printf ("{ /* PictOpt */\n");
				printf ("value = %d;\n", pict_opt);
				printf ("}; /* PictOpt */\n");
			}
#endif

			/* A value of 2 seems to indicate an IMDATA follows */
			next_biff_record_is_imdata = (pict_opt == 2);
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
			if (ms_excel_read_debug > 0)
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
			if (ms_excel_read_debug == 0)
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
	}

	/* The ftEnd record should have been the last */
	g_return_val_if_fail (data_len_left == 0, TRUE);

	if (next_biff_record_is_imdata) {
		guint16 opcode;

		/* Read the IMDATA. I am not sure that this record must follow
		 * a PictOpt.  For now be very careful
		 */
		g_return_val_if_fail (ms_biff_query_peek_next (q, &opcode), TRUE);
		g_return_val_if_fail (opcode == BIFF_IMDATA, TRUE);
		g_return_val_if_fail (ms_biff_query_next (q), TRUE);
	}

	return FALSE;
}

MSObj *
ms_read_OBJ (BiffQuery *q, ExcelWorkbook * wb, Sheet * sheet)
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

	errors = (wb->ver >= eBiffV8)
		? ms_obj_read_biff8_obj (q, wb, sheet, obj)
		: ms_obj_read_pre_biff8_obj (q, wb, sheet, obj);

	if (errors) {
		g_free (obj);
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
		ms_excel_read_chart (q, wb);
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
		return NULL;
	}

	obj->gnumeric_type = type;

#ifndef NO_DEBUG_EXCEL
	if (ms_excel_read_debug > 0)
		printf ("Object (%d) is a '%s'\n", obj->id, type_name);
#endif

	return obj;
}
