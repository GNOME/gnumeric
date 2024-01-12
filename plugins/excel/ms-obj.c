/*
 * ms-obj.c: MS Excel Object support for Gnumeric
 *
 * Authors:
 *    Jody Goldberg (jody@gnome.org)
 *    Michael Meeks (michael@ximian.com)
 *
 * (C) 1998-2001 Michael Meeks
 * (C) 2002-2005 Jody Goldberg
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
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301
 * USA
 */

#include <gnumeric-config.h>
#include <gnumeric.h>
#include <string.h>

#include "boot.h"
#include "ms-obj.h"
#include "ms-chart.h"
#include "ms-escher.h"
#include "ms-excel-util.h"
#include "ms-formula-write.h"

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
ms_obj_attr_new_expr (MSObjAttrID id, GnmExprTop const *texpr)
{
	MSObjAttr *res = g_new (MSObjAttr, 1);

	g_return_val_if_fail ((id & MS_OBJ_ATTR_MASK) == MS_OBJ_ATTR_IS_EXPR_MASK, NULL);

	/* be anal about constness */
	*((MSObjAttrID *)&(res->id)) = id;
	res->v.v_texpr = texpr;
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

MSObjAttr *
ms_obj_attr_new_gobject (MSObjAttrID id, GObject *object)
{
	MSObjAttr *res = g_new (MSObjAttr, 1);

	g_return_val_if_fail ((id & MS_OBJ_ATTR_MASK) == MS_OBJ_ATTR_IS_GOBJECT_MASK, NULL);

	*((MSObjAttrID *)&(res->id)) = id;
	res->v.v_object = object;
	g_object_ref (object);
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

gboolean
ms_obj_attr_get_ptr (MSObjAttrBag *attrs, MSObjAttrID id,
		     gpointer *res, gboolean steal)
{
	MSObjAttr *attr;

	g_return_val_if_fail (attrs != NULL, FALSE);
	g_return_val_if_fail (id & MS_OBJ_ATTR_IS_PTR_MASK, FALSE);

	if (NULL == (attr = ms_obj_attr_bag_lookup (attrs, id)))
		return FALSE;

	*res = attr->v.v_ptr;
	if (steal)
		attr->v.v_ptr = NULL;

	return TRUE;
}

GArray *
ms_obj_attr_get_array (MSObjAttrBag *attrs, MSObjAttrID id,
		       GArray *default_value, gboolean steal)
{
	MSObjAttr *attr;
	GArray *res;

	g_return_val_if_fail (attrs != NULL, default_value);
	g_return_val_if_fail (id & MS_OBJ_ATTR_IS_GARRAY_MASK, default_value);

	attr = ms_obj_attr_bag_lookup (attrs, id);
	if (attr == NULL)
		return default_value;
	res = attr->v.v_array;
	if (steal)
		attr->v.v_array = NULL;
	return res;
}

GnmExprTop const *
ms_obj_attr_get_expr (MSObjAttrBag *attrs, MSObjAttrID id,
		      GnmExprTop const *default_value, gboolean steal)
{
	MSObjAttr *attr;
	GnmExprTop const *res;

	g_return_val_if_fail (attrs != NULL, default_value);
	g_return_val_if_fail (id & MS_OBJ_ATTR_IS_EXPR_MASK, default_value);

	attr = ms_obj_attr_bag_lookup (attrs, id);
	if (attr == NULL)
		return default_value;
	res = attr->v.v_texpr;
	if (steal)
		attr->v.v_texpr = NULL;
	return res;
}

PangoAttrList *
ms_obj_attr_get_markup (MSObjAttrBag *attrs, MSObjAttrID id,
			PangoAttrList *default_value, gboolean steal)
{
	MSObjAttr *attr;
	PangoAttrList *res;

	g_return_val_if_fail (attrs != NULL, default_value);
	g_return_val_if_fail (id & MS_OBJ_ATTR_IS_PANGO_ATTR_LIST_MASK, default_value);

	attr = ms_obj_attr_bag_lookup (attrs, id);
	if (attr == NULL)
		return default_value;
	res = attr->v.v_markup;
	if (steal)
		attr->v.v_markup = NULL;
	return res;
}

GObject *
ms_obj_attr_get_gobject (MSObjAttrBag *attrs, MSObjAttrID id)
{
	MSObjAttr *attr;

	g_return_val_if_fail (attrs != NULL, NULL);
	g_return_val_if_fail (id & MS_OBJ_ATTR_IS_GOBJECT_MASK, NULL);

	attr = ms_obj_attr_bag_lookup (attrs, id);
	if (attr == NULL)
		return NULL;
	return attr->v.v_object;
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
			   attr->v.v_texpr != NULL) {
			gnm_expr_top_unref (attr->v.v_texpr);
			attr->v.v_texpr = NULL;
		} else if ((attr->id & MS_OBJ_ATTR_IS_PANGO_ATTR_LIST_MASK) &&
			   attr->v.v_markup != NULL) {
			pango_attr_list_unref (attr->v.v_markup);
			attr->v.v_markup = NULL;
		} else if ((attr->id & MS_OBJ_ATTR_IS_GOBJECT_MASK) &&
			   attr->v.v_object != NULL) {
			g_object_unref (attr->v.v_object);
			attr->v.v_object = NULL;
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
	obj->auto_combo	= FALSE;
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
		"At left", "Horizontally centered",
		"At right", "Horizontally justified"
	};
	static char const * const valigns [] = {
		"At top", "Vertically centered",
		"At bottom", "Vertically justified"
	};

	guint16 options, orient, text_len;
	int halign, valign;
	char *text;
	guint16 op;
	GString *accum;
	gboolean continue_seen = FALSE;

	*markup = NULL;

	XL_CHECK_CONDITION_VAL (q->length >= 14, g_strdup (""));

	options = GSF_LE_GET_GUINT16 (q->data);
	orient = GSF_LE_GET_GUINT16 (q->data + 2);
	text_len = GSF_LE_GET_GUINT16 (q->data + 10);
/*	guint16 const num_formats = GSF_LE_GET_GUINT16 (q->data + 12);*/
	halign = (options >> 1) & 0x7;
	valign = (options >> 4) & 0x7;

	if (text_len == 0)
		return NULL;

	accum = g_string_new ("");
	while (ms_biff_query_peek_next (q, &op) && op == BIFF_CONTINUE) {
		gboolean use_utf16;
		guint maxlen;

		continue_seen = TRUE;
		ms_biff_query_next (q);
		if (q->length == 0)
			continue;

		use_utf16 = q->data[0] != 0;
		maxlen = (q->length - 1) / (use_utf16 ? 2 : 1);
		text = excel_get_chars (c->importer,
			q->data + 1, MIN (text_len, maxlen), use_utf16, NULL);
		g_string_append (accum, text);
		g_free (text);
		if (text_len <= maxlen)
			break;
		text_len -= maxlen;
	}
	text = g_string_free (accum, FALSE);
	if (continue_seen) {
		if (ms_biff_query_peek_next (q, &op) && op == BIFF_CONTINUE) {
			ms_biff_query_next (q);
			*markup = ms_container_read_markup (c, q->data, q->length,
							    text);
		} else {
			g_warning ("Unusual, TXO text with no formatting has 0x%x @ 0x%lx",
				   op, (long)q->streamPos);
		}
	} else {
		g_warning ("TXO len of %d but no continue", text_len);
	}

#ifndef NO_DEBUG_EXCEL
	if (ms_excel_object_debug > 0) {
		char const *o_msg =  (orient <= 3) ? orientations[orient] : "unknown orientation";
		char const *h_msg =  (1 <= halign && halign <= 4) ? haligns[halign-1] : "unknown h-align";
		char const *v_msg =  (1 <= valign && valign <= 4) ? valigns[valign-1] : "unknown v-align";

		g_printerr ("{ TextObject\n");
		g_printerr ("Text '%s'\n", text);
		g_printerr ("is %s(%d), %s(%d) & %s(%d);\n",
			o_msg, orient, h_msg, halign, v_msg, valign);
		g_printerr ("}; /* TextObject */\n");
	}
#endif
	return text;
}

#ifndef NO_DEBUG_EXCEL
static void
ms_obj_dump (guint8 const *data, int len, int data_left, char const *name)
{
	if (ms_excel_object_debug < 2)
		return;

	g_printerr ("{ %s \n", name);
	if (len+4 > data_left) {
		g_printerr ("/* invalid length %d (0x%x) > %d(0x%x)*/\n",
			len+4, len+4, data_left, data_left);
		len = data_left - 4;
	}
	if (ms_excel_object_debug > 2)
		gsf_mem_dump (data, len+4);
	g_printerr ("}; /* %s */\n", name);
}
#else
#define ms_obj_dump (data, len, data_left, name) do { } while (0)
#endif

static guint8 const *
ms_obj_read_expr (MSObj *obj, MSObjAttrID id, MSContainer *c,
		  guint8 const *data, guint8 const *last)
{
	guint16 len;
	GnmExprTop const *ref;

	if (ms_excel_object_debug > 2)
		gsf_mem_dump (data, last-data);

	/* <u16 length> <u32 calcid?> <var expr> */
	g_return_val_if_fail ((data + 2) <= last, NULL);
	len = GSF_LE_GET_GUINT16 (data);

	/* looks like they sometimes skip the calc id if there is expr */
	if (len == 0 && (data + 2) == last)
		return data + 2;

	g_return_val_if_fail ((data + 6 + len) <= last, NULL);
	if (NULL == (ref = ms_container_parse_expr (c, data + 6, len)))
		return NULL;
	ms_obj_attr_bag_insert (obj->attrs,
		ms_obj_attr_new_expr (id, ref));
	return data + 6 + len;
}


static gboolean
read_pre_biff8_read_text (BiffQuery *q, MSContainer *c, MSObj *obj,
			  guint8 const *first,
			  unsigned len, unsigned txo_len)
{
	PangoAttrList *markup = NULL;
	GByteArray    *markup_data = NULL;
	char *str;
	unsigned remaining;
	guint16  op;

	if (first == NULL)
		return TRUE;

	remaining = q->data + q->length - first;

	/* CONTINUE handling here is very odd.
	 * If the text needs CONTINUEs but the markup does not, the markup is
	 * stored at the end of the OBJ rather than after the text.  */
	if (txo_len > 0 && txo_len < remaining) {
		markup_data = g_byte_array_new ();
		g_byte_array_append (markup_data, q->data + q->length - txo_len, txo_len);
		remaining -= txo_len;
	}

	str = excel_get_chars (c->importer, first, MIN (remaining, len), FALSE, NULL);
	if (len > remaining) {
		GString *accum = g_string_new (str);
		g_free (str);
		len -= remaining;
		while (ms_biff_query_peek_next (q, &op) && op == BIFF_CONTINUE) {
			ms_biff_query_next (q);
			str = excel_get_chars (c->importer, q->data,
				MIN (q->length, len), FALSE, NULL);
			g_string_append (accum, str);
			g_free (str);
			if (len < q->length)
				break;
			len -= q->length;
		}
		str = g_string_free (accum, FALSE);
		if (len > q->length) {
			g_free (str);
			return TRUE;
		}
		first = q->data + len;
	} else
		first += len;
	if (((first - q->data) & 1))
		first++; /* pad to word bound */

	ms_obj_attr_bag_insert (obj->attrs,
		ms_obj_attr_new_ptr (MS_OBJ_ATTR_TEXT, str));

	if (NULL != markup_data) {
		markup = ms_container_read_markup (c, markup_data->data, markup_data->len,
						   str);
		g_byte_array_free (markup_data, TRUE);
	} else if (txo_len > 0) {
		remaining = q->data + q->length - first;
		if (txo_len > remaining) {
			GByteArray *accum = g_byte_array_new ();
			g_byte_array_append (accum, first, remaining);
			txo_len -= remaining;
			while (ms_biff_query_peek_next (q, &op) && op == BIFF_CONTINUE) {
				ms_biff_query_next (q);
				g_byte_array_append (accum, q->data, MIN (q->length, txo_len));
				if (txo_len <= q->length)
					break;
				txo_len -= q->length;
			}
			first = q->data + txo_len;
			markup = ms_container_read_markup (c, accum->data, accum->len,
							   str);
			g_byte_array_free (accum, TRUE);
		} else {
			markup = ms_container_read_markup (c, first, txo_len,
							   str);
			first += txo_len;
		}
	}
	if (NULL != markup) {
		ms_obj_attr_bag_insert (obj->attrs,
			ms_obj_attr_new_markup (MS_OBJ_ATTR_MARKUP, markup));
		pango_attr_list_unref (markup);
	}

	return FALSE;
}

static guint8 const *
read_pre_biff8_read_expr (BiffQuery *q, MSContainer *c, MSObj *obj,
			  guint8 const *data, unsigned total_len) /* including extras */
{
	if (total_len <= 0)
		return data;
	XL_CHECK_CONDITION_VAL (total_len <= q->length - (data - q->data), data);

	ms_obj_read_expr (obj, MS_OBJ_ATTR_LINKED_TO_CELL, c,
		  data, data + total_len);
	data += total_len;	/* use total_len not the stated expression len */
	if (((data - q->data) & 1) && data < q->data + q->length)
		data++; /* pad to word bound */
	return data;
}

static guint8 const *
read_pre_biff8_read_name_and_fmla (BiffQuery *q, MSContainer *c, MSObj *obj,
				   gboolean has_name, unsigned offset)
{
	guint8 const *data;
	gboolean fmla_len;

	XL_CHECK_CONDITION_VAL (q->length >= 28, NULL);
	fmla_len = GSF_LE_GET_GUINT16 (q->data+26);
	XL_CHECK_CONDITION_VAL (q->length >= offset + 2 + fmla_len, NULL);

	data = q->data + offset;

	if (has_name) {
		guint8 const *last = q->data + q->length;
		unsigned len = *data++;
		char *str;

		g_return_val_if_fail (last - data >= len, NULL);

		str = excel_get_chars (c->importer, data, len, FALSE, NULL);
		data += len;
		if (((data - q->data) & 1) && data < last)
			data++; /* pad to word bound */

		ms_obj_attr_bag_insert (obj->attrs,
			ms_obj_attr_new_ptr (MS_OBJ_ATTR_OBJ_NAME, str));
	}
	return read_pre_biff8_read_expr (q, c, obj, data, fmla_len);
}

static gboolean
ms_obj_read_pre_biff8_obj (BiffQuery *q, MSContainer *c, MSObj *obj)
{
	guint8 const *last = q->data + q->length;
	guint16 peek_op, tmp, len;
	unsigned txo_len, if_empty;
	guint8 const *data;
	gboolean has_name;
	guint8 *anchor;

	XL_CHECK_CONDITION_VAL (q->length >= 26, TRUE);

	has_name = (q->length >= 32 &&
		    GSF_LE_GET_GUINT16 (q->data+30) != 0); /* undocumented */
#if 0
	guint16 const flags = GSF_LE_GET_GUINT16(q->data+8);
#endif
	anchor = g_malloc (MS_ANCHOR_SIZE);
	memcpy (anchor, q->data+8, MS_ANCHOR_SIZE);
	ms_obj_attr_bag_insert (obj->attrs,
		ms_obj_attr_new_ptr (MS_OBJ_ATTR_ANCHOR, anchor));

	obj->excel_type = GSF_LE_GET_GUINT16(q->data + 4);
	obj->id         = GSF_LE_GET_GUINT32(q->data + 6);

	switch (obj->excel_type) {
	case 0: /* group */
		break;
	case MSOT_LINE:
		XL_CHECK_CONDITION_VAL (q->data + 41 <= last, TRUE);
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
			(obj->excel_type == MSOT_LINE) ? 42 : 44);
		break;

	case MSOT_RECTANGLE:
	case MSOT_OVAL:
	case MSOT_ARC:
	case MSOT_TEXTBOX:
		XL_CHECK_CONDITION_VAL (q->data + 36 <= last, TRUE);
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

		if (obj->excel_type == MSOT_TEXTBOX) {
			g_return_val_if_fail (q->data + 52 <= last, TRUE);
			len = GSF_LE_GET_GUINT16 (q->data + 44);
			txo_len = GSF_LE_GET_GUINT16 (q->data + 48);
			if_empty = GSF_LE_GET_GUINT16 (q->data + 50);

			data = read_pre_biff8_read_name_and_fmla (q, c, obj, has_name, 70);
			if (read_pre_biff8_read_text (q, c, obj, data, len, txo_len))
				return TRUE;
			if (txo_len == 0)
				ms_obj_attr_bag_insert (obj->attrs,
					ms_obj_attr_new_markup (MS_OBJ_ATTR_MARKUP,
						ms_container_get_markup (c, if_empty)));
		} else
			data = read_pre_biff8_read_name_and_fmla (q, c, obj, has_name, 44);
		break;

	case MSOT_CHART:
		data = read_pre_biff8_read_name_and_fmla (q, c, obj, has_name, 62);
		break;

	case MSOT_BUTTON:
		data = read_pre_biff8_read_name_and_fmla (q, c, obj, has_name, 70);
		break;
	case MSOT_PICTURE:
/* 50 uint16 cbPictFmla, 60 name len, name, fmla (respect cbMacro), fmla (cbPictFmla) */
		data = read_pre_biff8_read_name_and_fmla (q, c, obj, has_name, 60);
		break;

	case MSOT_POLYGON:
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

	case MSOT_CHECKBOX:
/* 76 name len, name, cbfmla1 (IGNORE cbMacro), fmla1, cbfmla2, fmla2, cbtext, text */
		break;
	case MSOT_OPTION: /* option button */
/* 88 name len, name, cbfmla1 (IGNORE cbMacro), fmla1, cbfmla2, fmla2, cbtext, text */
		break;
	case MSOT_EDIT:
/* 70 name len, name, fmla (respect cbMacro), cbtext, text */
		data = read_pre_biff8_read_name_and_fmla (q, c, obj, has_name, 70);
		break;
	case MSOT_LABEL:
/* 70 name len, name, fmla (respect cbMacro), cbtext, text */
		len = GSF_LE_GET_GUINT16 (q->data + 44);
		data = read_pre_biff8_read_name_and_fmla (q, c, obj, has_name, 70);
		if (read_pre_biff8_read_text (q, c, obj, data, len, 16))
			return TRUE;
		break;
	case MSOT_DIALOG: /* dialog frame */
/* 70 name len, name, fmla (respect cbMacro) */
		data = read_pre_biff8_read_name_and_fmla (q, c, obj, has_name, 70);
		break;
	case MSOT_SPINNER:
	case MSOT_SCROLLBAR:
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
		ms_obj_attr_bag_insert (obj->attrs,
			ms_obj_attr_new_uint (MS_OBJ_ATTR_SCROLLBAR_HORIZ,
				GSF_LE_GET_GUINT16 (q->data+58)));

		{
			guint8 const *last = q->data + q->length;
			guint8 const *ptr = q->data + 64;

			ptr += 1 + *ptr;		/* object name */
			if ((ptr - q->data) & 1) ptr++;	/* align on word */
			if (ptr >= last) break;

			ptr += 2 + GSF_LE_GET_GUINT16 (ptr); /* the macro */
			if ((ptr - q->data) & 1) ptr++;	/* align on word */
			if (ptr >= last) break;

			(void) ms_obj_read_expr (obj, MS_OBJ_ATTR_LINKED_TO_CELL, c,
				ptr, last);
		}
		break;
	case MSOT_LIST:
/* 88 name len, name, cbfmla1 (IGNORE cbMacro), fmla1, cbfmla2, fmla2, cbfmla3, fmla3 */
		break;
	case MSOT_GROUP:
/* 82 name len, name, fmla (respect cbMacro), cbtext, text */
		data = read_pre_biff8_read_name_and_fmla (q, c, obj, has_name, 82);
		break;
	case MSOT_COMBO:
/* 110 name len, name, cbfmla1 (IGNORE cbMacro), fmla1, cbfmla2, fmla2, cbfmla3, fmla3 */
		obj->auto_combo =
			(GSF_LE_GET_GUINT16 (q->data + 8) & 0x8000) ? TRUE : FALSE;
		break;
	default :
		break;
	}

	if (obj->excel_type == MSOT_PICTURE) { /* picture */
		guint16 op;
		if (ms_biff_query_peek_next (q, &op) && op == BIFF_IMDATA) {
			GdkPixbuf *pixbuf;

			ms_biff_query_next (q);
			pixbuf = excel_read_IMDATA (q, FALSE);
			if (pixbuf) {
				ms_obj_attr_bag_insert (obj->attrs,
					ms_obj_attr_new_gobject
						(MS_OBJ_ATTR_IMDATA,
						 G_OBJECT (pixbuf)));
				g_object_unref (pixbuf);
			}
		}
	}
	return FALSE;
}

static void
ms_obj_map_forms_obj (MSObj *obj, MSContainer *c,
		      guint8 const *data, guint8 const *last)
{
	static struct {
		char const *key;
		unsigned    excel_type;
		gboolean    has_result_link;
		gboolean    has_source_link; /* requires has_result_link */
	} const map_forms [] = {
		{ "ScrollBar.1",	MSOT_SCROLLBAR, TRUE,	FALSE },
		{ "CheckBox.1",		MSOT_CHECKBOX,  TRUE,	FALSE },
		{ "TextBox.1",		MSOT_TEXTBOX,   FALSE,	FALSE },
		{ "CommandButton.1",	MSOT_BUTTON,    FALSE,	FALSE },
		{ "OptionButton.1",	MSOT_OPTION,    TRUE,	FALSE },
		{ "ListBox.1",		MSOT_LIST,      TRUE,	TRUE },
		{ "ComboBox.1",		MSOT_COMBO,     TRUE,	TRUE },
		{ "ToggleButton.1",	MSOT_TOGGLE,    TRUE,	FALSE },
		{ "SpinButton.1",	MSOT_SPINNER,   TRUE,	FALSE },
		{ "Label.1",		MSOT_LABEL,     FALSE,	FALSE },
		{ "Image.1",		MSOT_PICTURE,   FALSE,	FALSE }
	};
	int i;
	char *type;
	guint32 len;

	if (last - data < 16)
		return;
	type = excel_get_text (c->importer, data + 16,
			       GSF_LE_GET_GUINT16 (data + 14),
			       &len, NULL, last - data);
	if (NULL == type || strncmp (type, "Forms.", 6)) {
		g_free (type);
		return;
	}

#ifndef NO_DEBUG_EXCEL
	if (ms_excel_object_debug > 0) {
		g_printerr ("'%s' = %d\n", type, len);
		if (ms_excel_object_debug > 4)
			gsf_mem_dump (data, last-data);
	}
#endif

	for (i = G_N_ELEMENTS (map_forms); i-- > 0 ; )
		if (map_forms [i].excel_type > 0 &&
		    !strcmp (type+6, map_forms[i].key))
			break;

	g_free (type);

	if (i < 0)
		return;
	obj->excel_type = map_forms [i].excel_type;
#ifndef NO_DEBUG_EXCEL
	if (ms_excel_object_debug > 0)
		g_printerr ("found = %s\n", map_forms[i].key);
#endif

	if (map_forms [i].has_result_link) {
		/* round to word length */
		data = ms_obj_read_expr (obj, MS_OBJ_ATTR_LINKED_TO_CELL, c,
			data + 16 + len + (len &1) + 14, last);
		if (NULL != data && map_forms [i].has_source_link)
			ms_obj_read_expr (obj, MS_OBJ_ATTR_INPUT_FROM, c,
				data+3, last);
	}
}

static gboolean
ms_obj_read_biff8_obj (BiffQuery *q, MSContainer *c, MSObj *obj)
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
	while (data_len_left >= 4 && !hit_end) {
		guint16 const record_type = GSF_LE_GET_GUINT16(data);

		/* All the sub-records seem to have this layout
		 * 2001/Mar/29 JEG : liars.  Ok not all records have this
		 * layout.  Create a list box.  It seems to do something
		 * unique.  It acts like an end, and has no length specified.
		 */
		guint16 len = GSF_LE_GET_GUINT16(data+2);

		// The would-be "len" field has different meaning for
		// GR_LISTBOX_DATA.
		if (record_type != GR_LISTBOX_DATA)
			XL_CHECK_CONDITION_VAL (data_len_left >= 4 + len, TRUE);

		/* 1st record must be COMMON_OBJ*/
		XL_CHECK_CONDITION_VAL (obj->excel_type >= 0 ||
				      record_type == GR_COMMON_OBJ_DATA,
				      TRUE);

		switch (record_type) {
		case GR_END:
			XL_CHECK_CONDITION_VAL (len == 0, TRUE);
			/* ms_obj_dump (data, len, data_len_left, "ObjEnd"); */
			hit_end = TRUE;
			break;

		case GR_MACRO :
			ms_obj_read_expr (obj, MS_OBJ_ATTR_MACRO_EXPR, c,
				data+4, data + 4 + len);
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
					g_printerr ("{ /* PictOpt */\n");
					g_printerr ("value = %x;\n", opt);
					g_printerr ("}; /* PictOpt */\n");
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
			if (obj->excel_type == 8)
				ms_obj_map_forms_obj (obj, c, data+4, data+4+len);
			break;

		case GR_CHECKBOX_LINK :
			ms_obj_dump (data, len, data_len_left, "CheckboxLink");
			break;

		case GR_RADIO_BUTTON :
			ms_obj_dump (data, len, data_len_left, "RadioButton");
			break;

		case GR_SCROLLBAR :
			XL_CHECK_CONDITION_VAL (data_len_left >= 20, TRUE);
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
			ms_obj_attr_bag_insert (obj->attrs,
				ms_obj_attr_new_uint (MS_OBJ_ATTR_SCROLLBAR_HORIZ,
					GSF_LE_GET_GUINT16 (data+18)));
			ms_obj_dump (data, len, data_len_left, "ScrollBar");
			break;

		case GR_NOTE_STRUCTURE :
			ms_obj_dump (data, len, data_len_left, "Note");
			break;

		case GR_SCROLLBAR_FORMULA :
			ms_obj_read_expr (obj, MS_OBJ_ATTR_LINKED_TO_CELL, c,
				data+4, data + 4 + len);
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
			if (!obj->auto_combo)
				ms_obj_read_expr (obj, MS_OBJ_ATTR_INPUT_FROM, c,
					data+6, data + data_len_left);

			/* UNDOCUMENTED :
			 * It seems as if list box data does not conform to
			 * the docs.  It acts like an end and has no size.
			 */
			hit_end = TRUE;
			len = data_len_left - 4;

#warning "We should import selection too, see ms_objv8_write_list_data"

			ms_obj_dump (data, len, data_len_left, "ListBoxData");
			break;

		case GR_CHECKBOX_FORMULA :
			ms_obj_read_expr (obj, MS_OBJ_ATTR_LINKED_TO_CELL, c,
				data+4, data + 4 + len);
			ms_obj_dump (data, len, data_len_left, "CheckBoxFmla");
			break;

		case GR_COMMON_OBJ_DATA : {
			guint16 options;

			XL_CHECK_CONDITION_VAL (data_len_left >= 10, TRUE);

			options = GSF_LE_GET_GUINT16 (data+8);

			/* Multiple objects in 1 record ?? */
			XL_CHECK_CONDITION_VAL (obj->excel_type == -1, TRUE);

			obj->excel_type = GSF_LE_GET_GUINT16(data+4);
			obj->id = GSF_LE_GET_GUINT16(data+6);

			// "P" flag in FtCmo.
			obj->auto_combo =
				(obj->excel_type == 0x14) && (options & 0x100);

#ifndef NO_DEBUG_EXCEL
			/* only print when debug is enabled */
			if (ms_excel_object_debug == 0)
				break;

			g_printerr ("OBJECT TYPE = %d, id = %d;\n", obj->excel_type, obj->id);
			if (options&0x0001)
				g_printerr ("Locked;\n");
			if (options&0x0010)
				g_printerr ("Printable;\n");
			if (options&0x2000)
				g_printerr ("AutoFilled;\n");
			if (options&0x4000)
				g_printerr ("AutoLines;\n");

			if (ms_excel_object_debug > 4) {
				/* According to the docs this should not fail
				 * but there appears to be a flag at 0x200 for
				 * scrollbars and 0x100 for combos associated
				 * with filters.  */
				if ((options & 0x9eee) != 0)
					g_printerr ("Unknown option flag : %x;\n",
						options & 0x9eee);
			}
#endif
		}
		break;

		default:
			g_printerr ("ERROR : Unknown Obj record 0x%x len 0x%x dll %d;\n",
				record_type, len, data_len_left);
		}

		if (data_len_left < len+4)
			g_printerr ("record len %d (0x%x) > %d\n", len+4, len+4, data_len_left);

		/* FIXME : We need a structure akin to the escher code to do this properly */
		for (data_len_left -= len+4; data_len_left < 0; ) {
			guint16 peek_op;

			g_printerr ("deficit of %d\n", data_len_left);

			/* FIXME : what do we expect here ??
			 * I've seen what seem to be embedded drawings
			 * but I am not sure what is embedding what.
			 */
			if (!ms_biff_query_peek_next (q, &peek_op) ||
			    (peek_op != BIFF_CONTINUE &&
			     peek_op != BIFF_MS_O_DRAWING &&
			     peek_op != BIFF_TXO &&
			     peek_op != BIFF_OBJ)) {
				g_printerr ("0x%x vs 0x%x\n", q->opcode, peek_op);
				return TRUE;
			}

			ms_biff_query_next (q);
			data_len_left += q->length;
			g_printerr ("merged in 0x%x with len %d\n", q->opcode, q->length);
		}
		data = q->data + q->length - data_len_left;
	}

	/* The ftEnd record should have been the last */
	if (data_len_left > 0) {
		g_printerr("OBJ : unexpected extra data after Object End record;\n");
		gsf_mem_dump (data, data_len_left);
		return TRUE;
	}

	/* Catch underflow too */
	XL_CHECK_CONDITION_VAL (data_len_left == 0, TRUE);

	/* FIXME : Throw away the IMDATA that may follow.
	 * I am not sure when the IMDATA does follow, or how to display it,
	 * but very careful in case it is not there. */
	if (next_biff_record_maybe_imdata) {
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

/**
 * ms_read_OBJ :
 * @q: The biff record to start with.
 * @c: The object's container
 * @attrs: an optional hash of object attributes.
 *
 * This function takes ownership of attrs.
 *
 * Returns: %TRUE on success.
 **/
gboolean
ms_read_OBJ (BiffQuery *q, MSContainer *c, MSObjAttrBag *attrs)
{
	static char const * const object_type_names[] = {
		"Group",	/* 0x00 */
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
	MSObj *obj;

	/* no decent docs for this */
	if (c->importer->ver <= MS_BIFF_V4)
		return FALSE;

#ifndef NO_DEBUG_EXCEL
	if (ms_excel_object_debug > 0)
		g_printerr ("{ /* OBJ start */\n");
#endif
	obj = ms_obj_new (attrs);
	/* When called from escher (@attrs != NULL) use the biff8 variant.
	 * When embdedded directly in the stream (@attrs == NULL) the OBJ
	 * record appears to be in the old format. (#546887) */
	errors = (NULL != attrs)
		? ms_obj_read_biff8_obj (q, c, obj)
		: ms_obj_read_pre_biff8_obj (q, c, obj);

	if (errors) {
#ifndef NO_DEBUG_EXCEL
		if (ms_excel_object_debug > 0)
			g_printerr ("}; /* OBJ error 1 */\n");
#endif
		ms_obj_delete (obj);
		return TRUE;
	}

	obj->excel_type_name = NULL;
	if ((size_t)obj->excel_type < G_N_ELEMENTS (object_type_names))
		obj->excel_type_name = object_type_names[obj->excel_type];
	if (obj->excel_type_name == NULL)
		obj->excel_type_name = "Unknown";

#ifndef NO_DEBUG_EXCEL
	if (ms_excel_object_debug > 0) {
		g_printerr ("Object (%d) is a '%s'\n", obj->id, obj->excel_type_name);
		g_printerr ("}; /* OBJ end */\n");
	}
#endif

	if (c->vtbl->create_obj != NULL)
		obj->gnum_obj = c->vtbl->create_obj (c, obj);

	/* Chart, There should be a BOF next */
	if (obj->excel_type == 0x5) {
		if (ms_excel_chart_read_BOF (q, c, obj->gnum_obj)) {
			ms_obj_delete (obj);
			return TRUE;
		}
	}

	ms_container_add_obj (c, obj);

	return FALSE;
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
ms_objv8_write_scrollbar_old (BiffPut *bp)
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
ms_objv8_write_listbox (BiffPut *bp, guint8 lct, gboolean filtered)
{
	static guint8 const data[] = {
		0x13, 0, // GR_LISTBOX_DATA
		0xee, 0x1f,	/* totally contradicts docs, see above */
		0, 0, // Formula?
		3, 0, // # lines
		0, 0, // Nothing selected
		1,    // Flags (fUseCB)
		6,    // lct
		0, 0, // iEdit
		2, 0, 8, 0, 0x40, 0, 0, 0, 0, 0 // LbsDropData
	};
	guint8 buf[sizeof data];
	memcpy (buf, data, sizeof data);
	if (filtered)
		GSF_LE_SET_GUINT16 (buf + 14, 0xa);
	GSF_LE_SET_GUINT8 (buf + 11, lct);
	ms_biff_put_var_write (bp, buf, sizeof data);
}

void
ms_objv8_write_note (BiffPut *bp)
{
	static guint8 const data[] = {
		0x0d, 0,	/* Note */
		0x16, 0,	/* length 0x16 */
#if 0
		/* no idea, and no docs */
		54 80 79 64 08 0a 77 4f b3 d2 6b 26 88 2a 22 1a 00 00 10 00 00 00
		46 2d 5a 01 10 5c e7 46 9b 97 e2 7e 49 7f 08 b8 00 00 bf 00 08 00
#endif
		0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
	};
	guint8 buf[sizeof data];

	memcpy (buf, data, sizeof data);
	ms_biff_put_var_write (bp, buf, sizeof data);
}

static void
ms_objv8_write_checkbox_data (BiffPut *bp, gboolean active)
{
	char cboxdata[12];

	GSF_LE_SET_GUINT16 (cboxdata, GR_CHECKBOX_DATA);
	GSF_LE_SET_GUINT16 (cboxdata + 2, sizeof (cboxdata) - 4);
	GSF_LE_SET_GUINT16 (cboxdata + 4, active);
	GSF_LE_SET_GUINT16 (cboxdata + 6, 0); /* accel */
	GSF_LE_SET_GUINT16 (cboxdata + 8, 0); /* reserved */
	GSF_LE_SET_GUINT16 (cboxdata + 10, 2);  /* 3d display.  */
	ms_biff_put_var_write (bp, cboxdata, sizeof cboxdata);
}

static void
ms_objv8_write_checkbox_link (BiffPut *bp, gboolean active)
{
	char data[16];

	GSF_LE_SET_GUINT16 (data, GR_CHECKBOX_LINK);
	GSF_LE_SET_GUINT16 (data + 2, sizeof (data) - 4);
	GSF_LE_SET_GUINT16 (data + 4, active); /* ? */
	GSF_LE_SET_GUINT16 (data + 6, 0x12b0); /* ? */
	GSF_LE_SET_GUINT16 (data + 8, 0x01ce); /* ? */
	GSF_LE_SET_GUINT16 (data + 10, 0);
	GSF_LE_SET_GUINT16 (data + 12, 0);
	GSF_LE_SET_GUINT16 (data + 14, 2); /* style? */
	ms_biff_put_var_write (bp, data, sizeof data);
}

static void
ms_objv8_write_link_fmla (BiffPut *bp, guint16 typ,
			  ExcelWriteSheet *esheet,
			  GnmExprTop const *texpr)
{
	char hfmla[10];
	unsigned pos, end_pos;
	guint16 fmla_len;

	pos = bp->curpos;
	GSF_LE_SET_GUINT16 (hfmla, typ);
	GSF_LE_SET_GUINT16 (hfmla + 2, 0);  /* record len */
	GSF_LE_SET_GUINT16 (hfmla + 4, 0);  /* formula len */
	GSF_LE_SET_GUINT32 (hfmla + 6, 0);  /* calcid? */
	ms_biff_put_var_write (bp, hfmla, sizeof hfmla);
	fmla_len = excel_write_formula (esheet->ewb,
					texpr,
					esheet->gnum_sheet, 0, 0,
					EXCEL_CALLED_FROM_OBJ);
	if (fmla_len & 1)
		ms_biff_put_var_write (bp, "", 1);
	end_pos = bp->curpos;
	ms_biff_put_var_seekto (bp, pos);
	GSF_LE_SET_GUINT16 (hfmla + 2, (fmla_len + 7) & ~1);
	GSF_LE_SET_GUINT16 (hfmla + 4, fmla_len);
	ms_biff_put_var_write (bp, hfmla, sizeof hfmla);
	ms_biff_put_var_seekto (bp, end_pos);
}

static void
ms_objv8_write_macro_fmla (BiffPut *bp,
			   ExcelWriteSheet *esheet,
			   GnmExprTop const *texpr)
{
	char hfmla[10];
	unsigned pos, end_pos;
	guint16 fmla_len;

	pos = bp->curpos;
	GSF_LE_SET_GUINT16 (hfmla, GR_MACRO);
	GSF_LE_SET_GUINT16 (hfmla + 2, 0);  /* record len */
	GSF_LE_SET_GUINT16 (hfmla + 4, 0);  /* formula len */
	GSF_LE_SET_GUINT32 (hfmla + 6, 0);  /* calcid? */
	ms_biff_put_var_write (bp, hfmla, sizeof hfmla);
	fmla_len = excel_write_formula (esheet->ewb,
					texpr,
					esheet->gnum_sheet, 0, 0,
					EXCEL_CALLED_FROM_OBJ);
	if (fmla_len & 1)
		ms_biff_put_var_write (bp, "", 1);
	end_pos = bp->curpos;
	ms_biff_put_var_seekto (bp, pos);
	GSF_LE_SET_GUINT16 (hfmla + 2, (fmla_len + 7) & ~1);
	GSF_LE_SET_GUINT16 (hfmla + 4, fmla_len);
	ms_biff_put_var_write (bp, hfmla, sizeof hfmla);
	ms_biff_put_var_seekto (bp, end_pos);
}

static void
ms_objv8_write_macro_ref (BiffPut *bp,
			  ExcelWriteSheet *esheet,
			  GnmNamedExpr *macro_nexpr)
{
	GnmExprTop const *texpr =
		gnm_expr_top_new
		(gnm_expr_new_name (macro_nexpr,
				    esheet->gnum_sheet,
				    NULL));
	ms_objv8_write_macro_fmla (bp, esheet, texpr);
	gnm_expr_top_unref (texpr);
}

void
ms_objv8_write_checkbox (BiffPut *bp,
			 gboolean active,
			 ExcelWriteSheet *esheet,
			 GnmExprTop const *link_texpr,
			 GnmNamedExpr *macro_nexpr)
{
	ms_objv8_write_checkbox_link (bp, active);
	if (link_texpr)
		ms_objv8_write_link_fmla (bp, GR_CHECKBOX_FORMULA,
					  esheet, link_texpr);
	if (0 && macro_nexpr)
		ms_objv8_write_macro_ref (bp, esheet, macro_nexpr);
	ms_objv8_write_checkbox_data (bp, active);
}

static void
ms_objv8_write_radiobutton_rec (BiffPut *bp)
{
	char rb[10];

	GSF_LE_SET_GUINT16 (rb, GR_RADIO_BUTTON);
	GSF_LE_SET_GUINT16 (rb + 2, sizeof (rb) - 4);
	GSF_LE_SET_GUINT32 (rb + 4, 0);  /* ignore */
	GSF_LE_SET_GUINT16 (rb + 8, 0);  /* ignore */
	ms_biff_put_var_write (bp, rb, sizeof rb);
}

static void
ms_objv8_write_radiobutton_data (BiffPut *bp, guint16 nobj, gboolean first)
{
	char rb[8];

	GSF_LE_SET_GUINT16 (rb, GR_RADIO_BUTTON_DATA);
	GSF_LE_SET_GUINT16 (rb + 2, sizeof (rb) - 4);
	GSF_LE_SET_GUINT16 (rb + 4, nobj);
	GSF_LE_SET_GUINT16 (rb + 6, !!first);
	ms_biff_put_var_write (bp, rb, sizeof rb);
}

void
ms_objv8_write_radiobutton (BiffPut *bp,
			    gboolean active,
			    ExcelWriteSheet *esheet,
			    GnmExprTop const *link_texpr,
			    GnmNamedExpr *macro_nexpr)
{
	ms_objv8_write_checkbox_link (bp, active);
	ms_objv8_write_radiobutton_rec (bp);
	if (link_texpr)
		ms_objv8_write_link_fmla (bp, GR_CHECKBOX_FORMULA,
					  esheet, link_texpr);
	if (0 && macro_nexpr)
		ms_objv8_write_macro_ref (bp, esheet, macro_nexpr);
	ms_objv8_write_checkbox_data (bp, active);
	ms_objv8_write_radiobutton_data (bp, 0, TRUE);
}

static void
ms_objv8_write_adjustment (BiffPut *bp,
			   GtkAdjustment *adj, gboolean horiz)
{
	char data[24];

	GSF_LE_SET_GUINT16 (data, GR_SCROLLBAR);
	GSF_LE_SET_GUINT16 (data + 2, sizeof (data) - 4);
	GSF_LE_SET_GUINT32 (data + 4, 0); /* Unused */
#define SQUEEZE(f) ((guint16)CLAMP(f, -32768, 32767))
	GSF_LE_SET_GUINT16 (data +  8, SQUEEZE (gtk_adjustment_get_value (adj)));
	GSF_LE_SET_GINT16  (data + 10, SQUEEZE (gtk_adjustment_get_lower (adj)));
	GSF_LE_SET_GINT16  (data + 12, SQUEEZE (gtk_adjustment_get_upper (adj) + gtk_adjustment_get_step_increment (adj)));
	GSF_LE_SET_GINT16  (data + 14, SQUEEZE (gtk_adjustment_get_step_increment (adj)));
	GSF_LE_SET_GINT16  (data + 16, SQUEEZE (gtk_adjustment_get_page_increment (adj)));
#undef SQUEEZE
	GSF_LE_SET_GINT16  (data + 18, !!horiz);
	GSF_LE_SET_GINT16  (data + 20, 15);  /* widget in pixels */
	GSF_LE_SET_GINT16  (data + 22, 0x0001);  /* draw */

	ms_biff_put_var_write (bp, data, sizeof data);
}

void
ms_objv8_write_spinbutton (BiffPut *bp,
			   ExcelWriteSheet *esheet,
			   GtkAdjustment *adj, gboolean horiz,
			   GnmExprTop const *link_texpr,
			   GnmNamedExpr *macro_nexpr)
{
	ms_objv8_write_adjustment (bp, adj, horiz);
	if (link_texpr)
		ms_objv8_write_link_fmla (bp, GR_SCROLLBAR_FORMULA,
					  esheet, link_texpr);
	if (0 && macro_nexpr)
		ms_objv8_write_macro_ref (bp, esheet, macro_nexpr);
}

void
ms_objv8_write_scrollbar (BiffPut *bp,
			  ExcelWriteSheet *esheet,
			  GtkAdjustment *adj, gboolean horiz,
			  GnmExprTop const *link_texpr,
			  GnmNamedExpr *macro_nexpr)
{
	ms_objv8_write_adjustment (bp, adj, horiz);
	if (link_texpr)
		ms_objv8_write_link_fmla (bp, GR_SCROLLBAR_FORMULA,
					  esheet, link_texpr);
	if (0 && macro_nexpr)
		ms_objv8_write_macro_ref (bp, esheet, macro_nexpr);
}

static void
ms_objv8_write_list_data (BiffPut *bp,
			  ExcelWriteSheet *esheet,
			  GnmExprTop const *texpr,
			  guint16 N, guint16 selected)
{
	char hfmla[12];
	char data[8];
	unsigned pos, end_pos;
	guint16 fmla_len;
	guint16 ui;
	guint16 style = 0;
	guint8 *selection;

	pos = bp->curpos;
	GSF_LE_SET_GUINT16 (hfmla, GR_LISTBOX_DATA);
	GSF_LE_SET_GUINT16 (hfmla + 2, 0x1fcc);  /* ??? */
	GSF_LE_SET_GUINT16 (hfmla + 4, 0);  /* record len */
	GSF_LE_SET_GUINT16 (hfmla + 6, 0);  /* formula len */
	GSF_LE_SET_GUINT32 (hfmla + 8, 0);  /* calcid? */
	ms_biff_put_var_write (bp, hfmla, sizeof hfmla);
	if (texpr) {
		fmla_len = excel_write_formula (esheet->ewb,
						texpr,
						esheet->gnum_sheet, 0, 0,
						EXCEL_CALLED_FROM_OBJ);
		if (fmla_len & 1)
			ms_biff_put_var_write (bp, "", 1);
		GSF_LE_SET_GUINT16 (hfmla + 6, fmla_len);
	} else {
		/* Needs testing.  */
		ms_biff_put_var_write (bp, "\0", 2);
		fmla_len = 0;
	}

	end_pos = bp->curpos;
	ms_biff_put_var_seekto (bp, pos);
	GSF_LE_SET_GUINT16 (hfmla + 4, (fmla_len + 7) & ~1);
	ms_biff_put_var_write (bp, hfmla, sizeof hfmla);
	ms_biff_put_var_seekto (bp, end_pos);

	selection = g_new0 (char, N);
	for (ui = 0; ui < N; ui++)
		selection[ui] = (ui + 1 == selected);

	GSF_LE_SET_GUINT16 (data, N);
	GSF_LE_SET_GUINT16 (data + 2, selected);  /* iSel */
	GSF_LE_SET_GUINT16 (data + 4, style);
	GSF_LE_SET_GUINT16 (data + 6, 0);  /* edit object id */
	ms_biff_put_var_write (bp, data, sizeof data);
	ms_biff_put_var_write (bp, selection, N);

	g_free (selection);
}

void
ms_objv8_write_list (BiffPut *bp,
		     ExcelWriteSheet *esheet,
		     GtkAdjustment *adj,
		     GnmExprTop const *res_texpr,
		     GnmExprTop const *data_texpr,
		     GnmNamedExpr *macro_nexpr)
{
	ms_objv8_write_adjustment (bp, adj, FALSE);
	if (res_texpr)
		ms_objv8_write_link_fmla (bp, GR_SCROLLBAR_FORMULA,
					  esheet, res_texpr);
	if (0 && macro_nexpr)
		ms_objv8_write_macro_ref (bp, esheet, macro_nexpr);
	ms_objv8_write_list_data (bp, esheet, data_texpr,
				  (guint16)gtk_adjustment_get_upper (adj) - 1,
				  (guint16)gtk_adjustment_get_value (adj));
}

void
ms_objv8_write_button (BiffPut *bp,
		       ExcelWriteSheet *esheet,
		       GnmNamedExpr *macro_nexpr)
{
	if (0 && macro_nexpr)
		ms_objv8_write_macro_ref (bp, esheet, macro_nexpr);
}
