/* vim: set sw=8: */

/*
 * ms-obj.c: MS Excel Object support for Gnumeric
 *
 * Copyright (C) 2000-2001
 *	Jody Goldberg (jody@gnome.org)
 *	Michael Meeks (mmeeks@gnu.org)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
 * USA
 */

#include <gnumeric-config.h>
#include <gnumeric.h>
#include <string.h>

#include "boot.h"
#include "ms-obj.h"
#include "ms-chart.h"
#include "ms-escher.h"

#include <expr.h>
#include <parse-util.h>
#include <sheet-object-widget.h>
#include <sheet-object-graphic.h>

#include <gsf/gsf-utils.h>

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

MSObjAttr *
ms_obj_attr_new_flag (MSObjAttrID id)
{
	MSObjAttr *res = g_new (MSObjAttr, 1);

	/* be anal about constness */
	*((MSObjAttrID *)&(res->id)) = id;
	res->v.v_ptr = NULL;
	return res;
}

MSObjAttr *
ms_obj_attr_new_uint (MSObjAttrID id, guint32 val)
{
	MSObjAttr *res = g_new (MSObjAttr, 1);

	g_return_val_if_fail ((id & MS_OBJ_ATTR_MASK) == MS_OBJ_ATTR_IS_INT_MASK, NULL);

	/* be anal about constness */
	*((MSObjAttrID *)&(res->id)) = id;
	res->v.v_uint = val;
	return res;
}
MSObjAttr *
ms_obj_attr_new_ptr (MSObjAttrID id, gpointer val)
{
	MSObjAttr *res = g_new (MSObjAttr, 1);

	g_return_val_if_fail ((id & MS_OBJ_ATTR_MASK) == MS_OBJ_ATTR_IS_PTR_MASK, NULL);

	/* be anal about constness */
	*((MSObjAttrID *)&(res->id)) = id;
	res->v.v_ptr = val;
	return res;
}

MSObjAttr *
ms_obj_attr_new_array (MSObjAttrID id, GArray *array)
{
	MSObjAttr *res = g_new (MSObjAttr, 1);

	g_return_val_if_fail ((id & MS_OBJ_ATTR_MASK) == MS_OBJ_ATTR_IS_GARRAY_MASK, NULL);

	/* be anal about constness */
	*((MSObjAttrID *)&(res->id)) = id;
	res->v.v_array = array;
	return res;
}

MSObjAttr *
ms_obj_attr_new_expr (MSObjAttrID id, GnmExpr const *expr)
{
	MSObjAttr *res = g_new (MSObjAttr, 1);

	g_return_val_if_fail ((id & MS_OBJ_ATTR_MASK) == MS_OBJ_ATTR_IS_EXPR_MASK, NULL);

	/* be anal about constness */
	*((MSObjAttrID *)&(res->id)) = id;
	res->v.v_expr = expr;
	return res;
}

guint32
ms_obj_attr_get_uint (MSObj *obj, MSObjAttrID id, guint32 default_value)
{
	MSObjAttr *attr;

	g_return_val_if_fail (obj != NULL, default_value);
	g_return_val_if_fail (id & MS_OBJ_ATTR_IS_INT_MASK, default_value);

	attr = ms_obj_attr_bag_lookup (obj->attrs, id);
	if (attr == NULL)
		return default_value;
	return attr->v.v_uint;
}

gint32
ms_obj_attr_get_int  (MSObj *obj, MSObjAttrID id, gint32 default_value)
{
	MSObjAttr *attr;

	g_return_val_if_fail (obj != NULL, default_value);
	g_return_val_if_fail (id & MS_OBJ_ATTR_IS_INT_MASK, default_value);

	attr = ms_obj_attr_bag_lookup (obj->attrs, id);
	if (attr == NULL)
		return default_value;
	return attr->v.v_int;
}

gpointer
ms_obj_attr_get_ptr  (MSObj *obj, MSObjAttrID id, gpointer default_value)
{
	MSObjAttr *attr;

	g_return_val_if_fail (obj != NULL, default_value);
	g_return_val_if_fail (id & MS_OBJ_ATTR_IS_PTR_MASK, default_value);

	attr = ms_obj_attr_bag_lookup (obj->attrs, id);
	if (attr == NULL)
		return default_value;
	return attr->v.v_ptr;
}

GArray *
ms_obj_attr_get_array (MSObj *obj, MSObjAttrID id, GArray *default_value)
{
	MSObjAttr *attr;

	g_return_val_if_fail (obj != NULL, default_value);
	g_return_val_if_fail (id & MS_OBJ_ATTR_IS_GARRAY_MASK, default_value);

	attr = ms_obj_attr_bag_lookup (obj->attrs, id);
	if (attr == NULL)
		return default_value;
	return attr->v.v_array;
}

GnmExpr const *
ms_obj_attr_get_expr (MSObj *obj, MSObjAttrID id, GnmExpr const *default_value)
{
	MSObjAttr *attr;

	g_return_val_if_fail (obj != NULL, default_value);
	g_return_val_if_fail (id & MS_OBJ_ATTR_IS_EXPR_MASK, default_value);

	attr = ms_obj_attr_bag_lookup (obj->attrs, id);
	if (attr == NULL)
		return default_value;
	return attr->v.v_expr;
}

static void
ms_obj_attr_destroy (MSObjAttr *attr)
{
	if (attr != NULL) {
		if ((attr->id & MS_OBJ_ATTR_IS_PTR_MASK) &&
		    attr->v.v_ptr != NULL) {
			g_free (attr->v.v_ptr);
			attr->v.v_ptr = NULL;
		} else if ((attr->id & MS_OBJ_ATTR_IS_GARRAY_MASK) &&
			   attr->v.v_array != NULL) {
			g_array_free (attr->v.v_array, TRUE);
			attr->v.v_array = NULL;
		} else if ((attr->id & MS_OBJ_ATTR_IS_EXPR_MASK) &&
			   attr->v.v_expr != NULL) {
			gnm_expr_unref (attr->v.v_expr);
			attr->v.v_expr = NULL;
		}
		g_free (attr);
	}
}

static guint
cb_ms_obj_attr_hash (gconstpointer key)
{
	return ((MSObjAttr const *)key)->id;
}

static gint
cb_ms_obj_attr_cmp (gconstpointer a, gconstpointer b)
{
	return ((MSObjAttr const *)a)->id == ((MSObjAttr const *)b)->id;
}

MSObjAttrBag *
ms_obj_attr_bag_new (void)
{
	return g_hash_table_new (cb_ms_obj_attr_hash, cb_ms_obj_attr_cmp);
}

static void
cb_ms_obj_attr_destroy (gpointer key, gpointer value, gpointer ignored)
{
	ms_obj_attr_destroy (value);
}
void
ms_obj_attr_bag_destroy (MSObjAttrBag *attrs)
{
	if (attrs != NULL) {
		g_hash_table_foreach (attrs, cb_ms_obj_attr_destroy, NULL);
		g_hash_table_destroy (attrs);
	}
}


void
ms_obj_attr_bag_insert (MSObjAttrBag *attrs, MSObjAttr *attr)
{
	g_return_if_fail (!g_hash_table_lookup (attrs, attr));
	g_hash_table_insert (attrs, attr, attr);
}

MSObjAttr *
ms_obj_attr_bag_lookup (MSObjAttrBag *attrs, MSObjAttrID id)
{
	if (attrs != NULL) {
		MSObjAttr attr;
		*((MSObjAttrID *)&(attr.id)) = id;
		return g_hash_table_lookup (attrs, &attr);
	}
	return NULL;
}

/********************************************************************************/

MSObj *
ms_obj_new (MSObjAttrBag *attrs)
{
	MSObj *obj = g_new0 (MSObj, 1);

	obj->excel_type = (unsigned)-1; /* Set to undefined */
	obj->excel_type_name = NULL;
	obj->id = -1;
	obj->gnum_obj = NULL;
	obj->attrs = (attrs != NULL) ? attrs : ms_obj_attr_bag_new ();
	obj->ignore_combo_in_filter	= FALSE;
	obj->is_linked			= FALSE;

	return obj;
}

void
ms_obj_delete (MSObj *obj)
{
	if (obj) {
		if (obj->gnum_obj) {
			g_object_unref (obj->gnum_obj);
			obj->gnum_obj = NULL;
		}
		if (obj->attrs) {
			ms_obj_attr_bag_destroy (obj->attrs);
			obj->attrs = NULL;
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

	guint16 const options     = GSF_LE_GET_GUINT16 (q->data);
	guint16 const orient      = GSF_LE_GET_GUINT16 (q->data + 2);
	guint16 const text_len    = GSF_LE_GET_GUINT16 (q->data + 10);
/*	guint16 const num_formats = GSF_LE_GET_GUINT16 (q->data + 12);*/
	int const halign = (options >> 1) & 0x7;
	int const valign = (options >> 4) & 0x7;
	char         *text;
	guint16       peek_op;

	if (text_len == 0)
		return NULL;

	g_return_val_if_fail (orient <= 3, NULL);
	g_return_val_if_fail (1 <= halign && halign <= 4, NULL);
	g_return_val_if_fail (1 <= valign && valign <= 4, NULL);

	text = g_new (char, text_len + 1);
	text [0] = '\0';
	if (ms_biff_query_peek_next (q, &peek_op) &&
	    peek_op == BIFF_CONTINUE) {
		guint8 *data;
		int i, increment = 1;

		ms_biff_query_next (q);
		increment = (GSF_LE_GET_GUINT8 (q->data)) ? 2 : 1;
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
			g_warning ("Unusual, TXO text with no formatting has 0x%x @ 0x%x", peek_op, q->streamPos);
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
		gsf_mem_dump (data, len+4);
	printf ("}; /* %s */\n", name);
}
#else
#define ms_obj_dump (data, len, data_left, name)
#endif

/* S59DAD.HTM */
static gboolean
ms_obj_read_pre_biff8_obj (BiffQuery *q, MSContainer *container, MSObj *obj)
{
	guint16 peek_op, tmp;
	guint8 const *data;
	/* TODO : Lots of docs for these things.  Write the parser. */

#if 0
	guint32 const numObjects = GSF_LE_GET_GUINT16(q->data);
	guint16 const flags = GSF_LE_GET_GUINT16(q->data+8);
#endif
	guint8 *anchor = g_malloc (MS_ANCHOR_SIZE);
	memcpy (anchor, q->data+8, MS_ANCHOR_SIZE);
	ms_obj_attr_bag_insert (obj->attrs,
		ms_obj_attr_new_ptr (MS_OBJ_ATTR_ANCHOR, anchor));

	obj->excel_type = GSF_LE_GET_GUINT16(q->data + 4);
	obj->id         = GSF_LE_GET_GUINT32(q->data + 6);

	switch (obj->excel_type) {
	case 0: /* group */
		break;
	case 1: { /* line */
		if (GSF_LE_GET_GUINT8 (q->data+38))
			ms_obj_attr_bag_insert (obj->attrs,
				ms_obj_attr_new_flag (MS_OBJ_ATTR_ARROW_END));
		ms_obj_attr_bag_insert (obj->attrs,
			ms_obj_attr_new_uint (MS_OBJ_ATTR_FILL_COLOR,
				0x80000000 | GSF_LE_GET_GUINT8 (q->data+34)));

		tmp = GSF_LE_GET_GUINT8 (q->data+40);
		if (tmp & 0x1)
			ms_obj_attr_bag_insert (obj->attrs,
				ms_obj_attr_new_flag (MS_OBJ_ATTR_FLIP_H));
		if (tmp & 0x2)
			ms_obj_attr_bag_insert (obj->attrs,
				ms_obj_attr_new_flag (MS_OBJ_ATTR_FLIP_V));
		break;
	}
	case 2: /* rectangle */
		break;
	case 3: /* oval */
		break;
	case 4: /* arc */
		break;
	case 5: /* chart */
		break;
	case 6: /* textbox */
		if (GSF_LE_GET_GUINT8 (q->data+36) > 0) {
			ms_obj_attr_bag_insert (obj->attrs,
				ms_obj_attr_new_flag (MS_OBJ_ATTR_FILLED));
			ms_obj_attr_bag_insert (obj->attrs,
				ms_obj_attr_new_uint (MS_OBJ_ATTR_FILL_COLOR,
					0x80000000 | GSF_LE_GET_GUINT8 (q->data+35)));
		}
		ms_obj_attr_bag_insert (obj->attrs,
			ms_obj_attr_new_uint (MS_OBJ_ATTR_OUTLINE_COLOR,
				0x80000000 | GSF_LE_GET_GUINT8 (q->data+38)));
		data = q->data + 70;
		g_return_val_if_fail ((unsigned)(data - q->data) < q->length, TRUE);
		data += *data + ((*data & 0x1) ? 1 : 2); /* padding byte */
		g_return_val_if_fail ((unsigned)(data - q->data) < q->length, TRUE);
		/* docs lie, there is no fmla structure */
		ms_obj_attr_bag_insert (obj->attrs,
			ms_obj_attr_new_ptr (MS_OBJ_ATTR_TEXT,
				g_strndup (data, GSF_LE_GET_GUINT16 (q->data + 44))));
		break;

	case 7: /* button */
		break;
	case 8: /* picture */
		break;
	case 9: /* polygon */

		ms_obj_attr_bag_insert (obj->attrs,
			ms_obj_attr_new_uint (MS_OBJ_ATTR_FILL_COLOR,
				0x80000000 | GSF_LE_GET_GUINT8 (q->data+35)));
		ms_obj_attr_bag_insert (obj->attrs,
			ms_obj_attr_new_uint (MS_OBJ_ATTR_OUTLINE_COLOR,
				0x80000000 | GSF_LE_GET_GUINT8 (q->data+38)));

		if (ms_biff_query_peek_next (q, &peek_op) &&
		    peek_op == BIFF_COORDLIST) {
			unsigned i, n;
			guint tmp;
			GArray *array;

			ms_biff_query_next (q);
			n = q->length / 2;
			array = g_array_set_size (
				g_array_new (FALSE, FALSE, sizeof (double)), n + 2);

			for (i = 0; i < n ; i++) {
				tmp = GSF_LE_GET_GUINT16 (q->data + 2*i);
				g_array_index (array, double, i) = (double)tmp/ 16384.;
			}
			g_array_index (array, double, i)   = g_array_index (array, double, 0);
			g_array_index (array, double, i+1) = g_array_index (array, double, 1);
			ms_obj_attr_bag_insert (obj->attrs,
				ms_obj_attr_new_array (MS_OBJ_ATTR_POLYGON_COORDS, array));
		}
		break;

	case 0xB  : /* check box */
		break;
	case 0xC  : /* option button */
		break;
	case 0xD  : /* edit box */
		break;
	case 0xE  : /* label */
		break;
	case 0xF  : /* dialog frame */
		break;
	case 0x10 : /* spinner */
		break;
	case 0x11 : /* scrollbar */
		break;
	case 0x12 : /* list box */
		break;
	case 0x13 : /* group box */
		break;
	case 0x14 : /* drop down */
		break;
	default :
		;
	}

	return FALSE;
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
		guint16 const record_type = GSF_LE_GET_GUINT16(data);

		/* All the sub-records seem to have this layout
		 * 2001/Mar/29 JEG : liars.  Ok not all records have this
		 * layout.  Create a list box.  It seems to do something
		 * unique.  It acts like an end, and has no length specified.
		 */
		guint16 len = GSF_LE_GET_GUINT16(data+2);

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
			if (len == 2) {
				guint16 opt = GSF_LE_GET_GUINT16 (data + 4);

				obj->is_linked = (opt & 0x2) ? TRUE : FALSE;
#ifndef NO_DEBUG_EXCEL
				if (ms_excel_object_debug >= 1) {
					printf ("{ /* PictOpt */\n");
					printf ("value = %x;\n", opt);
					printf ("}; /* PictOpt */\n");
				}
#endif
			} else {
				/* no docs on this so be careful */
				g_warning ("PictOpt record with size other than 2");
			}

			next_biff_record_maybe_imdata = TRUE;
			break;

		case GR_PICTURE_FORMULA :
			ms_obj_dump (data, len, data_len_left, "PictFormula");
			break;

		case GR_CHECKBOX_LINK :
			ms_obj_dump (data, len, data_len_left, "CheckboxLink");
			break;

		case GR_RADIO_BUTTON :
			ms_obj_dump (data, len, data_len_left, "RadioButton");
			break;

		case GR_SCROLLBAR : {
			ms_obj_attr_bag_insert (obj->attrs,
				ms_obj_attr_new_uint (MS_OBJ_ATTR_SCROLLBAR_VALUE,
					GSF_LE_GET_GUINT16 (data+8)));
			ms_obj_attr_bag_insert (obj->attrs,
				ms_obj_attr_new_uint (MS_OBJ_ATTR_SCROLLBAR_MIN,
					GSF_LE_GET_GUINT16 (data+10)));
			ms_obj_attr_bag_insert (obj->attrs,
				ms_obj_attr_new_uint (MS_OBJ_ATTR_SCROLLBAR_MAX,
					GSF_LE_GET_GUINT16 (data+12)));
			ms_obj_attr_bag_insert (obj->attrs,
				ms_obj_attr_new_uint (MS_OBJ_ATTR_SCROLLBAR_INC,
					GSF_LE_GET_GUINT16 (data+14)));
			ms_obj_attr_bag_insert (obj->attrs,
				ms_obj_attr_new_uint (MS_OBJ_ATTR_SCROLLBAR_PAGE,
					GSF_LE_GET_GUINT16 (data+16)));
			ms_obj_dump (data, len, data_len_left, "ScrollBar");
			break;
		}

		case GR_NOTE_STRUCTURE :
			ms_obj_dump (data, len, data_len_left, "Note");
			break;

		case GR_SCROLLBAR_FORMULA : {
			guint16 const expr_len = GSF_LE_GET_GUINT16 (data+4);
			GnmExpr const *ref = ms_container_parse_expr (container, data+10, expr_len);
			if (ref != NULL)
				ms_obj_attr_bag_insert (obj->attrs,
					ms_obj_attr_new_expr (MS_OBJ_ATTR_SCROLLBAR_LINK, ref));
			ms_obj_dump (data, len, data_len_left, "ScrollbarFmla");
			break;
		}

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

		case GR_LISTBOX_DATA : {
			/* FIXME : find some docs for this
			 * It seems as if list box data does not conform to
			 * the docs.  It acts like an end and has no size.
			 */
			hit_end = TRUE;
			len = data_len_left - 4;

			ms_obj_dump (data, len, data_len_left, "ListBoxData");
			break;
		}

		case GR_CHECKBOX_FORMULA : {
			guint16 const expr_len = GSF_LE_GET_GUINT16 (data+4);
			GnmExpr const *ref = ms_container_parse_expr (container, data+10, expr_len);
			if (ref != NULL)
				ms_obj_attr_bag_insert (obj->attrs,
					ms_obj_attr_new_expr (MS_OBJ_ATTR_CHECKBOX_LINK, ref));
			ms_obj_dump (data, len, data_len_left, "CheckBoxFmla");
			break;
		}

		case GR_COMMON_OBJ_DATA : {
			guint16 const options =GSF_LE_GET_GUINT16 (data+8);

			/* Multiple objects in 1 record ?? */
			g_return_val_if_fail (obj->excel_type == -1, -1);

			obj->excel_type = GSF_LE_GET_GUINT16(data+4);
			obj->id = GSF_LE_GET_GUINT16(data+6);

			/* Undocumented.  It appears that combos for filters are marked
			 * with flag 0x100
			 */
			obj->ignore_combo_in_filter =
				(obj->excel_type == 0x14) && (options & 0x100);

#ifndef NO_DEBUG_EXCEL
			/* only print when debug is enabled */
			if (ms_excel_object_debug == 0)
				break;

			printf ("OBJECT TYPE = %d\n", obj->excel_type);
			if (options&0x0001)
				printf ("Locked;\n");
			if (options&0x0010)
				printf ("Printable;\n");
			if (options&0x2000)
				printf ("AutoFilled;\n");
			if (options&0x4000)
				printf ("AutoLines;\n");

			if (ms_excel_object_debug > 4) {
				/* According to the docs this should not fail
				 * but there appears to be a flag at 0x200 for
				 * scrollbars and 0x100 for combos
				 * associated with filters.
				 */
				if ((options & 0x9fee) != 0)
					printf ("WARNING : Why is option not 0 (%x)\n",
						options & 0x9fee);
			}
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
		gsf_mem_dump (data, data_len_left);
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
			excel_read_IMDATA (q);
		}
	}

	return FALSE;
}

/**
 * ms_read_OBJ :
 * @q : The biff record to start with.
 * @container : The object's container
 * @attrs : an OPTIONAL hash of object attributes.
 */
void
ms_read_OBJ (BiffQuery *q, MSContainer *container, MSObjAttrBag *attrs)
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
	MSObj *obj = ms_obj_new (attrs);

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
		ms_obj_delete (obj);
		return;
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

	if (container->vtbl->create_obj != NULL)
		obj->gnum_obj = (*container->vtbl->create_obj) (container, obj);

	/* Chart, There should be a BOF next */
	if (obj->excel_type == 0x5) {
		if (ms_excel_read_chart (q, container, obj->gnum_obj)) {
			ms_obj_delete (obj);
			return;
		}
	}

	if (obj->gnum_obj == NULL) {
		ms_obj_delete (obj);
		return;
	}
#if 0
	printf ("Registered object 0x%p\n", obj);
#endif
	ms_container_add_obj (container, obj);
}
