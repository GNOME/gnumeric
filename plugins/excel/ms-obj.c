/* vim: set sw=8: */

/*
 * ms-obj.c: MS Excel Object support for Gnumeric
 *
 * Copyright (C) 2000-2004
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

#include <gsf/gsf-utils.h>
#include <stdio.h>

#define GR_END                0x00
#define GR_MACRO              0x04
#define GR_COMMAND_BUTTON     0x05
#define GR_GROUP	      0x06
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

	g_return_val_if_fail ((id & MS_OBJ_ATTR_MASK) == 0, NULL);

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

MSObjAttr *
ms_obj_attr_new_markup (MSObjAttrID id, PangoAttrList *markup)
{
	MSObjAttr *res = g_new (MSObjAttr, 1);

	g_return_val_if_fail ((id & MS_OBJ_ATTR_MASK) == MS_OBJ_ATTR_IS_PANGO_ATTR_LIST_MASK, NULL);

	/* be anal about constness */
	*((MSObjAttrID *)&(res->id)) = id;
	res->v.v_markup = markup;
	pango_attr_list_ref (markup);
	return res;
}

guint32
ms_obj_attr_get_uint (MSObjAttrBag *attrs, MSObjAttrID id, guint32 default_value)
{
	MSObjAttr *attr;

	g_return_val_if_fail (attrs != NULL, default_value);
	g_return_val_if_fail (id & MS_OBJ_ATTR_IS_INT_MASK, default_value);

	attr = ms_obj_attr_bag_lookup (attrs, id);
	if (attr == NULL)
		return default_value;
	return attr->v.v_uint;
}

gint32
ms_obj_attr_get_int  (MSObjAttrBag *attrs, MSObjAttrID id, gint32 default_value)
{
	MSObjAttr *attr;

	g_return_val_if_fail (attrs != NULL, default_value);
	g_return_val_if_fail (id & MS_OBJ_ATTR_IS_INT_MASK, default_value);

	attr = ms_obj_attr_bag_lookup (attrs, id);
	if (attr == NULL)
		return default_value;
	return attr->v.v_int;
}

gpointer
ms_obj_attr_get_ptr  (MSObjAttrBag *attrs, MSObjAttrID id, gpointer default_value)
{
	MSObjAttr *attr;

	g_return_val_if_fail (attrs != NULL, default_value);
	g_return_val_if_fail (id & MS_OBJ_ATTR_IS_PTR_MASK, default_value);

	attr = ms_obj_attr_bag_lookup (attrs, id);
	if (attr == NULL)
		return default_value;
	return attr->v.v_ptr;
}

GArray *
ms_obj_attr_get_array (MSObjAttrBag *attrs, MSObjAttrID id, GArray *default_value)
{
	MSObjAttr *attr;

	g_return_val_if_fail (attrs != NULL, default_value);
	g_return_val_if_fail (id & MS_OBJ_ATTR_IS_GARRAY_MASK, default_value);

	attr = ms_obj_attr_bag_lookup (attrs, id);
	if (attr == NULL)
		return default_value;
	return attr->v.v_array;
}

GnmExpr const *
ms_obj_attr_get_expr (MSObjAttrBag *attrs, MSObjAttrID id, GnmExpr const *default_value)
{
	MSObjAttr *attr;

	g_return_val_if_fail (attrs != NULL, default_value);
	g_return_val_if_fail (id & MS_OBJ_ATTR_IS_EXPR_MASK, default_value);

	attr = ms_obj_attr_bag_lookup (attrs, id);
	if (attr == NULL)
		return default_value;
	return attr->v.v_expr;
}

PangoAttrList *
ms_obj_attr_get_markup (MSObjAttrBag *attrs, MSObjAttrID id, PangoAttrList *default_value)
{
	MSObjAttr *attr;

	g_return_val_if_fail (attrs != NULL, default_value);
	g_return_val_if_fail (id & MS_OBJ_ATTR_IS_PANGO_ATTR_LIST_MASK, default_value);

	attr = ms_obj_attr_bag_lookup (attrs, id);
	if (attr == NULL)
		return default_value;
	return attr->v.v_markup;
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
		} else if ((attr->id & MS_OBJ_ATTR_IS_PANGO_ATTR_LIST_MASK) &&
			   attr->v.v_markup != NULL) {
			pango_attr_list_unref (attr->v.v_markup);
			attr->v.v_markup = NULL;
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
		MSObjAttr attr = {0, {0}};
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
	obj->combo_in_autofilter	= FALSE;
	obj->is_linked			= FALSE;
	obj->comment_pos.col = obj->comment_pos.row = -1;

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

char *
ms_read_TXO (BiffQuery *q, MSContainer *c, PangoAttrList **markup)
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
	guint16       op;

	*markup = NULL;
	if (text_len == 0)
		return NULL;

	g_return_val_if_fail (orient <= 3, NULL);
	g_return_val_if_fail (1 <= halign && halign <= 4, NULL);
	g_return_val_if_fail (1 <= valign && valign <= 4, NULL);

	if (ms_biff_query_peek_next (q, &op) && op == BIFF_CONTINUE) {
		ms_biff_query_next (q);

		if ((int)q->length < text_len) {
			g_warning ("Broken continue in TXO record");
			text = g_strdup ("Broken continue");
		} else
			text = ms_biff_get_chars (q->data + 1, text_len,
						  *(q->data) != 0);

		if (ms_biff_query_peek_next (q, &op) && op == BIFF_CONTINUE) {
			ms_biff_query_next (q);
			*markup = ms_container_read_markup (c, q->data, q->length, text);
		} else
			g_warning ("Unusual, TXO text with no formatting has 0x%x @ 0x%x", op, q->streamPos);
	} else {
		if (text_len > 0)
			g_warning ("TXO len of %d but no continue", text_len);
		text = g_strdup ("");
	}

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

static gboolean
read_pre_biff8_read_str (BiffQuery *q, MSContainer *container, MSObj *obj,
			 MSObjAttrID text_id, guint8 const **first,
			 unsigned len, unsigned txo_len)
{
	guint8 const *last = q->data + q->length;
	char *str;

	g_return_val_if_fail (*first + len <= last, TRUE);
	g_return_val_if_fail (text_id != MS_OBJ_ATTR_NONE, TRUE);

	str = ms_biff_get_chars (*first, len, FALSE);
	ms_obj_attr_bag_insert (obj->attrs, ms_obj_attr_new_ptr (text_id, str));

	*first += len;
	if (((*first - q->data) & 1))
		(*first)++; /* pad to word bound */

	if (txo_len > 0) {
		guint8 const *last = q->data + q->length;
		PangoAttrList *markup;

		g_return_val_if_fail ((*first + txo_len) <= last, TRUE);

		markup = ms_container_read_markup (container, *first, txo_len, str);
		ms_obj_attr_bag_insert (obj->attrs,
			ms_obj_attr_new_markup (MS_OBJ_ATTR_MARKUP, markup));
		pango_attr_list_unref (markup);

		*first += txo_len;
	}

	return FALSE;
}

static gboolean
read_pre_biff8_read_expr (BiffQuery *q, MSContainer *container, MSObj *obj,
			  MSObjAttrID id, guint8 const **first,
			  unsigned total_len) /* including extras */
{
	guint8 const *ptr  = *first;
	guint8 const *last = q->data + q->length;
	GnmExpr const *ref;
	unsigned len;

	if (total_len <= 0)
		return FALSE;

	g_return_val_if_fail (ptr + 2 <= last, TRUE);

	len = GSF_LE_GET_GUINT16 (ptr);

	g_return_val_if_fail (ptr + 6 + len <= last, TRUE);

	ref = ms_container_parse_expr (container, ptr + 6, len);
	if (ref != NULL)
		ms_obj_attr_bag_insert (obj->attrs,
			ms_obj_attr_new_expr (MS_OBJ_ATTR_LINKED_TO_CELL, ref));

	*first = ptr + total_len;
	if (((*first - q->data) & 1))
		(*first)++; /* pad to word bound */

	return FALSE;
}

static guint8 const *
read_pre_biff8_read_name_and_fmla (BiffQuery *q, MSContainer *container, MSObj *obj,
				   gboolean has_name, unsigned offset)
{
	guint8 const *data = q->data + offset;
	gboolean const fmla_len = GSF_LE_GET_GUINT16 (q->data+26);

	if (has_name &&
	    read_pre_biff8_read_str (q, container, obj, MS_OBJ_ATTR_OBJ_NAME,
				     &data, *(data++), 0))
		return NULL;
	if (read_pre_biff8_read_expr (q, container, obj,
				      MS_OBJ_ATTR_NONE, &data, fmla_len))
		return NULL;
	return data;
}

static gboolean
ms_obj_read_pre_biff8_obj (BiffQuery *q, MSContainer *c, MSObj *obj)
{
	guint8 const *last = q->data + q->length;
	guint16 peek_op, tmp, len;
	unsigned txo_len, if_empty;
	guint8 const *data;
	gboolean const has_name = GSF_LE_GET_GUINT16 (q->data+30) != 0; /* undocumented */

#if 0
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
	case 1: /* line */
		g_return_val_if_fail (q->data + 41 <= last, TRUE);
		tmp = GSF_LE_GET_GUINT8 (q->data+38) & 0x0F;
		if (tmp > 0)
			ms_obj_attr_bag_insert (obj->attrs,
				ms_obj_attr_new_uint (MS_OBJ_ATTR_ARROW_END, tmp));
		ms_obj_attr_bag_insert (obj->attrs,
			ms_obj_attr_new_uint (MS_OBJ_ATTR_OUTLINE_COLOR,
				0x80000000 | GSF_LE_GET_GUINT8 (q->data+34)));
		tmp = GSF_LE_GET_GUINT8 (q->data+35);
		ms_obj_attr_bag_insert (obj->attrs,
			ms_obj_attr_new_uint (MS_OBJ_ATTR_OUTLINE_STYLE,
					      ((tmp == 0xff) ? 0 : tmp+1)));

		tmp = GSF_LE_GET_GUINT8 (q->data+40);
		if (tmp == 1 || tmp == 2)
			ms_obj_attr_bag_insert (obj->attrs,
				ms_obj_attr_new_flag (MS_OBJ_ATTR_FLIP_H));
		if (tmp >= 2)
			ms_obj_attr_bag_insert (obj->attrs,
				ms_obj_attr_new_flag (MS_OBJ_ATTR_FLIP_V));
		data = read_pre_biff8_read_name_and_fmla (q, c, obj, has_name,
			(obj->excel_type == 1) ? 42 : 44);
		break;

	case 2: /* rectangle */
	case 3: /* oval */
	case 4: /* arc */
	case 6: /* textbox */
		g_return_val_if_fail (q->data + 36 <= last, TRUE);
		ms_obj_attr_bag_insert (obj->attrs,
			ms_obj_attr_new_uint (MS_OBJ_ATTR_FILL_BACKGROUND,
				0x80000000 | GSF_LE_GET_GUINT8 (q->data+34)));
		ms_obj_attr_bag_insert (obj->attrs,
			ms_obj_attr_new_uint (MS_OBJ_ATTR_FILL_COLOR,
				0x80000000 | GSF_LE_GET_GUINT8 (q->data+35)));
		if (GSF_LE_GET_GUINT8 (q->data+36) == 0)
			ms_obj_attr_bag_insert (obj->attrs,
				ms_obj_attr_new_flag (MS_OBJ_ATTR_UNFILLED));

		tmp = GSF_LE_GET_GUINT8 (q->data+39);
		ms_obj_attr_bag_insert (obj->attrs,
			ms_obj_attr_new_uint (MS_OBJ_ATTR_OUTLINE_STYLE,
					      ((tmp == 0xff) ? 0 : tmp+1)));
		ms_obj_attr_bag_insert (obj->attrs,
			ms_obj_attr_new_uint (MS_OBJ_ATTR_OUTLINE_COLOR,
				0x80000000 | GSF_LE_GET_GUINT8 (q->data+38)));
		ms_obj_attr_bag_insert (obj->attrs,
			ms_obj_attr_new_uint (MS_OBJ_ATTR_OUTLINE_WIDTH,
					      GSF_LE_GET_GUINT8 (q->data+40) * 256));

		if (obj->excel_type == 6) {
			g_return_val_if_fail (q->data + 52 <= last, TRUE);
			len = GSF_LE_GET_GUINT16 (q->data + 44);
			txo_len = GSF_LE_GET_GUINT16 (q->data + 48);
			if_empty = GSF_LE_GET_GUINT16 (q->data + 50);

			data = read_pre_biff8_read_name_and_fmla (q, c, obj, has_name, 70);
			if (data == NULL ||
			    read_pre_biff8_read_str (q, c, obj,
				MS_OBJ_ATTR_TEXT, &data, len, txo_len))
				return TRUE;

			if (txo_len == 0)
				ms_obj_attr_bag_insert (obj->attrs,
					ms_obj_attr_new_markup (MS_OBJ_ATTR_MARKUP,
						ms_container_get_markup (c, if_empty)));
		} else
			data = read_pre_biff8_read_name_and_fmla (q, c, obj, has_name, 44);
		break;

	case 5: /* chart */
		data = read_pre_biff8_read_name_and_fmla (q, c, obj, has_name, 62);
		break;

	case 7: /* button */
		data = read_pre_biff8_read_name_and_fmla (q, c, obj, has_name, 70);
		break;
	case 8: /* picture */
/* 50 uint16 cbPictFmla, 60 name len, name, fmla (respect cbMacro), fmla (cbPictFmla) */
		data = read_pre_biff8_read_name_and_fmla (q, c, obj, has_name, 60);
		break;

	case 9: /* polygon */
/* 66 name len, name, fmla (respect cbMacro) */
		ms_obj_attr_bag_insert (obj->attrs,
			ms_obj_attr_new_uint (MS_OBJ_ATTR_FILL_COLOR,
				0x80000000 | GSF_LE_GET_GUINT8 (q->data+35)));
		ms_obj_attr_bag_insert (obj->attrs,
			ms_obj_attr_new_uint (MS_OBJ_ATTR_OUTLINE_COLOR,
				0x80000000 | GSF_LE_GET_GUINT8 (q->data+38)));

		data = read_pre_biff8_read_name_and_fmla (q, c, obj, has_name, 66);

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
/* 76 name len, name, cbfmla1 (IGNORE cbMacro), fmla1, cbfmla2, fmla2, cbtext, text */
		break;
	case 0xC  : /* option button */
/* 88 name len, name, cbfmla1 (IGNORE cbMacro), fmla1, cbfmla2, fmla2, cbtext, text */
		break;
	case 0xD  : /* edit box */
/* 70 name len, name, fmla (respect cbMacro), cbtext, text */
		data = read_pre_biff8_read_name_and_fmla (q, c, obj, has_name, 70);
		break;
	case 0xE  : /* label */
/* 70 name len, name, fmla (respect cbMacro), cbtext, text */
		len = GSF_LE_GET_GUINT16 (q->data + 44);

		data = read_pre_biff8_read_name_and_fmla (q, c, obj, has_name, 70);
		if (data == NULL ||
		    read_pre_biff8_read_str (q, c, obj,
			MS_OBJ_ATTR_TEXT, &data, len, 16))
			return TRUE;
		break;
	case 0xF  : /* dialog frame */
/* 70 name len, name, fmla (respect cbMacro) */
		data = read_pre_biff8_read_name_and_fmla (q, c, obj, has_name, 70);
		break;
	case 0x10 : /* spinner & scrollbar (layout is the same) */
	case 0x11 :
/* 68 name len, name, cbfmla1 (IGNORE cbMacro), fmla1, cbfmla2, fmla2 */
		ms_obj_attr_bag_insert (obj->attrs,
			ms_obj_attr_new_uint (MS_OBJ_ATTR_SCROLLBAR_VALUE,
				GSF_LE_GET_GUINT16 (q->data+48)));
		ms_obj_attr_bag_insert (obj->attrs,
			ms_obj_attr_new_uint (MS_OBJ_ATTR_SCROLLBAR_MIN,
				GSF_LE_GET_GUINT16 (q->data+50)));
		ms_obj_attr_bag_insert (obj->attrs,
			ms_obj_attr_new_uint (MS_OBJ_ATTR_SCROLLBAR_MAX,
				GSF_LE_GET_GUINT16 (q->data+52)));
		ms_obj_attr_bag_insert (obj->attrs,
			ms_obj_attr_new_uint (MS_OBJ_ATTR_SCROLLBAR_INC,
				GSF_LE_GET_GUINT16 (q->data+54)));
		ms_obj_attr_bag_insert (obj->attrs,
			ms_obj_attr_new_uint (MS_OBJ_ATTR_SCROLLBAR_PAGE,
				GSF_LE_GET_GUINT16 (q->data+56)));

		{
			GnmExpr const *ref;
			guint16 len;
			guint8 const *last = q->data + q->length;
			guint8 const *ptr = q->data + 64;

			ptr += 1 + *ptr;		/* object name */
			if ((ptr - q->data) & 1) ptr++;	/* align on word */
			if (ptr >= last) break;

			ptr += 2 + GSF_LE_GET_GUINT16 (ptr); /* the macro */
			if ((ptr - q->data) & 1) ptr++;	/* align on word */
			if (ptr >= last) break;

			len = GSF_LE_GET_GUINT16 (ptr+2); /* the assigned macro */
			ref = ms_container_parse_expr (c, ptr + 8, len);
			if (ref != NULL)
				ms_obj_attr_bag_insert (obj->attrs,
					ms_obj_attr_new_expr (MS_OBJ_ATTR_LINKED_TO_CELL, ref));
		}
		break;
	case 0x12 : /* list box */
/* 88 name len, name, cbfmla1 (IGNORE cbMacro), fmla1, cbfmla2, fmla2, cbfmla3, fmla3 */
		break;
	case 0x13 : /* group box */
/* 82 name len, name, fmla (respect cbMacro), cbtext, text */
		data = read_pre_biff8_read_name_and_fmla (q, c, obj, has_name, 82);
		break;
	case 0x14 : /* drop down */
/* 110 name len, name, cbfmla1 (IGNORE cbMacro), fmla1, cbfmla2, fmla2, cbfmla3, fmla3 */
		obj->combo_in_autofilter =
			(GSF_LE_GET_GUINT16 (q->data + 8) & 0x8000) ? TRUE : FALSE;
		break;
	default :
		break;
	}

	if (obj->excel_type == 8) { /* picture */
		guint16 op;
		if (ms_biff_query_peek_next (q, &op) && op == BIFF_IMDATA) {
			GdkPixbuf *pixbuf; 

			ms_biff_query_next (q);
			pixbuf = excel_read_IMDATA (q, FALSE);
			if (pixbuf)
				g_object_unref (pixbuf);
		}
	}
	return FALSE;
}


static void
ms_obj_map_forms_obj (MSObj *obj, MSContainer *container, guint8 const *data, gint32 len)
{
	static struct {
		char const	*key;
		unsigned	 excel_type;
		unsigned	 offset_to_link;
	} const map_forms [] = {
		{ "ScrollBar",		0x11, 16 },
		{ "CheckBox",		0x0B, 17 },
		{ "TextBox",		0x06 },
		{ "CommandButton",	0x07 },
		{ "OptionButton",	0x0C, 17 },
		{ "ListBox",		0x12, 16 },
		{ "ComboBox",		0x14, 17 },
		{ "ToggleButton",	0x70, 17 },
		{ "SpinButton",		0x10, 17 },
		{ "Label",		0x0E },
		{ "Image",		0x08 }
	};
	int i = G_N_ELEMENTS (map_forms);
	char const *key;
	int key_len;

	if (obj->excel_type != 8 || len <= 27 ||
	    strncmp (data+21, "Forms.", 6))
		return;

	while (i-- > 0) {
		key	= map_forms[i].key;
		key_len = strlen (key);
		if (map_forms [i].excel_type > 0 &&
		    len >= (27+key_len) &&
		    0 == strncmp (data+27, map_forms [i].key, key_len))
			break;
	}

	if (i >= 0) {
		obj->excel_type = map_forms [i].excel_type;
		if (map_forms [i].offset_to_link != 0) {
			guint8 const *ptr = data + 27 + key_len + map_forms [i].offset_to_link;
			guint16 expr_len;
			GnmExpr const *ref;
			g_return_if_fail (ptr + 2 <= (data + len));
			expr_len = GSF_LE_GET_GUINT16 (ptr);
			g_return_if_fail (ptr + 2 + expr_len <= (data + len));
			ref = ms_container_parse_expr (container, ptr + 6, expr_len);
			if (ref != NULL)
				ms_obj_attr_bag_insert (obj->attrs,
					ms_obj_attr_new_expr (MS_OBJ_ATTR_LINKED_TO_CELL, ref));
		}
	}
}

static gboolean
ms_obj_read_biff8_obj (BiffQuery *q, MSContainer *container, MSObj *obj)
{
	guint8 *data;
	gint32 data_len_left;
	gboolean hit_end = FALSE;
	gboolean next_biff_record_maybe_imdata = FALSE;

	g_return_val_if_fail (q, TRUE);
	g_return_val_if_fail (q->opcode == BIFF_OBJ, TRUE);

	data = q->data;
	data_len_left = q->length;

#if 0
	ms_biff_query_dump (q);
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
			/* ms_obj_dump (data, len, data_len_left, "ObjEnd"); */
			hit_end = TRUE;
			break;

		case GR_MACRO :
			ms_obj_dump (data, len, data_len_left, "MacroObject");
			break;

		case GR_COMMAND_BUTTON :
			ms_obj_dump (data, len, data_len_left, "CommandButton");
			break;

		case GR_GROUP :
			ms_obj_dump (data, len, data_len_left, "Group");
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
			/* Check for form objects stored here for no apparent reason */
			ms_obj_map_forms_obj (obj, container, data, len);
			break;

		case GR_CHECKBOX_LINK :
			ms_obj_dump (data, len, data_len_left, "CheckboxLink");
			break;

		case GR_RADIO_BUTTON :
			ms_obj_dump (data, len, data_len_left, "RadioButton");
			break;

		case GR_SCROLLBAR :
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

		case GR_NOTE_STRUCTURE :
			ms_obj_dump (data, len, data_len_left, "Note");
			break;

		case GR_SCROLLBAR_FORMULA : {
			guint16 const expr_len = GSF_LE_GET_GUINT16 (data+4);
			GnmExpr const *ref = ms_container_parse_expr (container, data+10, expr_len);
			if (ref != NULL)
				ms_obj_attr_bag_insert (obj->attrs,
					ms_obj_attr_new_expr (MS_OBJ_ATTR_LINKED_TO_CELL, ref));
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
					ms_obj_attr_new_expr (MS_OBJ_ATTR_LINKED_TO_CELL, ref));
			ms_obj_dump (data, len, data_len_left, "CheckBoxFmla");
			break;
		}

		case GR_COMMON_OBJ_DATA : {
			guint16 const options =GSF_LE_GET_GUINT16 (data+8);

			/* Multiple objects in 1 record ?? */
			g_return_val_if_fail (obj->excel_type == -1, TRUE);

			obj->excel_type = GSF_LE_GET_GUINT16(data+4);
			obj->id = GSF_LE_GET_GUINT16(data+6);

			/* Undocumented.  It appears that combos for filters are marked
			 * with flag 0x100
			 */
			obj->combo_in_autofilter =
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
				 * scrollbars and 0x100 for combos associated
				 * with filters.
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
			GdkPixbuf *pixbuf; 

			printf ("Reading trailing IMDATA;\n");
			ms_biff_query_next (q);
			pixbuf = excel_read_IMDATA (q, FALSE);
			if (pixbuf)
				g_object_unref (pixbuf);
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
	static char const * const object_type_names[] = {
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
	if (obj->excel_type < (int)G_N_ELEMENTS (object_type_names))
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
	if (obj->excel_type == 0x5 &&
	    ms_excel_chart_read_BOF (q, container, obj->gnum_obj)) {
		ms_obj_delete (obj);
		return;
	}

#if 0
	g_warning ("registered obj %d\n", obj->id);
#endif
	ms_container_add_obj (container, obj);
}

/**********************************************************************/

void
ms_objv8_write_common (BiffPut *bp, int id, int type, guint16 flags)
{
	guint8 buf[22];
	GSF_LE_SET_GUINT16 (buf + 0, 0x15);	/* common record */
	GSF_LE_SET_GUINT16 (buf + 2, 0x12);	/* len 0x12 */
	GSF_LE_SET_GUINT16 (buf + 4, type);
	GSF_LE_SET_GUINT16 (buf + 6, id);
	GSF_LE_SET_GUINT16 (buf + 8, flags);

	GSF_LE_SET_GUINT32 (buf + 10, 0);
	/* docs say 0, but n the wild this is some sort of pointer ?*/
	GSF_LE_SET_GUINT32 (buf + 14, 0);
	GSF_LE_SET_GUINT32 (buf + 18, 0);
	ms_biff_put_var_write (bp, buf, sizeof buf);
}

void
ms_objv8_write_scrollbar (BiffPut *bp)
{
	/* no docs, but some guesses.  See above */
	static guint8 const data[] = {
		0x0c, 0,
		0x14, 0,
		0, 0,
		0, 0,
		0, 0,	/* value */
		0, 0,	/* min */
		0x64, 0,/* max */
		1, 0,	/* increment */
		0xa, 0,	/* page */
		0, 0,
		0x10, 0,
		1, 0
	};
	ms_biff_put_var_write (bp, data, sizeof data);
}

void
ms_objv8_write_listbox (BiffPut *bp, gboolean filtered)
{
	static guint8 const data[] = {
		0x13, 0,
		0xee, 0x1f,	/* totally contradicts docs, see above */
		0, 0, 0, 0, 1, 0, 1, 3, 0, 0, 2, 0,
		8, 0, 0x57, 0, 0, 0, 0, 0
	};
	guint8 buf[sizeof data];
	memcpy (buf, data, sizeof data);
	if (filtered)
		GSF_LE_SET_GUINT16 (buf + 14, 0xa);
	ms_biff_put_var_write (bp, buf, sizeof data);
}

void
ms_objv8_write_chart (BiffPut *bp, SheetObject *sog)
{
}
