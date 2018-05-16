/*
 * go-data-slicer-field.h : The definition of a content for a data slicer
 *
 * Copyright (C) 2008 Jody Goldberg (jody@gnome.org)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) version 3.
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
#include <go-data-slicer-field-impl.h>
#include <go-data-slicer-impl.h>
#include <go-data-cache-field.h>
#include <go-data-cache.h>

#include <gsf/gsf-impl-utils.h>
#include <glib/gi18n-lib.h>
#include <string.h>

#define GO_DATA_SLICER_FIELD_CLASS(k)		(G_TYPE_CHECK_CLASS_CAST ((k), GO_DATA_SLICER_FIELD_TYPE, GODataSlicerFieldClass))
#define IS_GO_DATA_SLICER_FIELD_CLASS(k)	(G_TYPE_CHECK_CLASS_TYPE ((k), GO_DATA_SLICER_FIELD_TYPE))
#define GO_DATA_SLICER_FIELD_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), GO_DATA_SLICER_FIELD_TYPE, GODataSlicerFieldClass))

enum {
	PROP_0,
	PROP_SLICER,			/* GODataSlicer * */
	PROP_NAME,			/* GOString * */
	PROP_INDEX,			/* int */
	PROP_DATA_CACHE_FIELD_INDEX,	/* int */
	PROP_AGGREGATIONS
};

static void
go_data_slicer_field_init (GODataSlicerField *dsf)
{
	int i;

	dsf->ds = NULL;
	dsf->name = NULL;
	dsf->indx = -1;
	dsf->data_cache_field_indx = -1;
	dsf->aggregations = 0;

	for (i = 0 ; i < GDS_FIELD_TYPE_UNSET ; i++)
		dsf->field_type_pos[i] = -1;
}

static GObjectClass *parent_klass;
static void
go_data_slicer_field_finalize (GObject *obj)
{
	GODataSlicerField *dsf = (GODataSlicerField *)obj;

	go_string_unref (dsf->name);
	dsf->name = NULL;

	parent_klass->finalize (obj);
}

static void
go_data_slicer_field_set_property (GObject *obj, guint property_id,
				   GValue const *value, GParamSpec *pspec)
{
	GODataSlicerField *dsf = (GODataSlicerField *)obj;

	switch (property_id) {
	/* we do not hold a ref */
	case PROP_SLICER:
		dsf->ds = g_value_get_object (value);
		break;
	case PROP_NAME:
		go_string_unref (dsf->name);
		dsf->name = g_value_dup_boxed (value);
		break;
	case PROP_DATA_CACHE_FIELD_INDEX:
		dsf->data_cache_field_indx = g_value_get_int (value);
		break;
	case PROP_AGGREGATIONS:
		dsf->aggregations = g_value_get_uint (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, property_id, pspec);
	}
}

static void
go_data_slicer_field_get_property (GObject *obj, guint property_id,
				   GValue *value, GParamSpec *pspec)
{
	GODataSlicerField const *dsf = (GODataSlicerField const *)obj;
	switch (property_id) {
	case PROP_SLICER:
		g_value_set_object (value, dsf->ds);
		break;
	case PROP_NAME:
		g_value_set_boxed (value, dsf->name);
		break;	/* actual name, do not fall back to cache */
	case PROP_INDEX:
		g_value_set_int (value, dsf->indx);
		break;
	case PROP_DATA_CACHE_FIELD_INDEX:
		g_value_set_int (value, dsf->data_cache_field_indx);
		break;
	case PROP_AGGREGATIONS:
		g_value_set_uint (value, dsf->aggregations);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, property_id, pspec);
	}
}

static void
go_data_slicer_field_class_init (GODataSlicerFieldClass *klass)
{
	GObjectClass *gobject_class = (GObjectClass *)klass;
	gobject_class->finalize		= go_data_slicer_field_finalize;
	gobject_class->set_property	= go_data_slicer_field_set_property;
	gobject_class->get_property	= go_data_slicer_field_get_property;

	g_object_class_install_property (gobject_class, PROP_SLICER,
		 g_param_spec_object ("slicer", NULL, NULL,
			GO_DATA_SLICER_TYPE, GSF_PARAM_STATIC | G_PARAM_READWRITE));
	g_object_class_install_property (gobject_class, PROP_NAME,
		 g_param_spec_boxed ("name", NULL, NULL, go_string_get_type (),
			GSF_PARAM_STATIC | G_PARAM_READWRITE));
	g_object_class_install_property (gobject_class, PROP_INDEX,
		 g_param_spec_int ("index", NULL,
			"Index of the field within the GODataSlicer",
			-1, G_MAXINT, -1,
			GSF_PARAM_STATIC | G_PARAM_READABLE));
	g_object_class_install_property (gobject_class, PROP_DATA_CACHE_FIELD_INDEX,
		 g_param_spec_int ("data-cache-field-index", NULL,
			"Index of the underlying GODataCacheField",
			-1, G_MAXINT, -1,
			GSF_PARAM_STATIC | G_PARAM_READWRITE));
	g_object_class_install_property (gobject_class, PROP_AGGREGATIONS,
		 g_param_spec_uint ("aggregations", NULL,
			"bitwise OR of the set of aggregations",
			0, ~0, 0,
			GSF_PARAM_STATIC | G_PARAM_READWRITE));

	parent_klass = g_type_class_peek_parent (klass);
}

GSF_CLASS (GODataSlicerField, go_data_slicer_field,
	   go_data_slicer_field_class_init, go_data_slicer_field_init,
	   G_TYPE_OBJECT)

/**
 * go_data_slicer_field_get_cache_field:
 * @dsf: #GODataSlicerField const
 *
 * Returns : the underlying cache field
 **/
GODataCacheField *
go_data_slicer_field_get_cache_field (GODataSlicerField const *dsf)
{
	g_return_val_if_fail (IS_GO_DATA_SLICER_FIELD (dsf), NULL);
	return go_data_cache_get_field (go_data_slicer_get_cache (dsf->ds),
		dsf->data_cache_field_indx);
}

/**
 * go_data_slicer_field_get_name:
 * @dsf: #GODataSlicerField const
 *
 * If @dsf has a name return that, otherwise default to the name of the
 * underlying cache field.   If there is a need (e.g. for export) to get the
 * exact name without the default, use the properties.
 *
 * Returns: the name of the field.
 **/
GOString *
go_data_slicer_field_get_name (GODataSlicerField const *dsf)
{
	g_return_val_if_fail (IS_GO_DATA_SLICER_FIELD (dsf), NULL);
	if (dsf->name)
		return dsf->name;
	return go_data_cache_field_get_name (
		go_data_slicer_field_get_cache_field (dsf));
}

int
go_data_slicer_field_get_field_type_pos (GODataSlicerField const *dsf,
					 GODataSlicerFieldType field_type)
{
	g_return_val_if_fail (IS_GO_DATA_SLICER_FIELD (dsf), -1);
	g_return_val_if_fail (field_type > GDS_FIELD_TYPE_UNSET &&
			      field_type < GDS_FIELD_TYPE_MAX, -1);
	return dsf->field_type_pos[field_type];
}

/**
 * go_data_slicer_field_set_field_type_pos:
 * @dsf: #GODataSlicerField
 * @field_type: #GODataSlicerFieldType
 * @pos: >= len => append, else move ahead of @pos, -1 removes
 *
 * Make @dsf a @field_type, and move it to position @pos other @field_type's
 * in the slicer.
 **/
void
go_data_slicer_field_set_field_type_pos (GODataSlicerField *dsf,
					 GODataSlicerFieldType field_type,
					 int pos)
{
	GArray *headers;
	int cur_pos, i;

	g_return_if_fail (IS_GO_DATA_SLICER_FIELD (dsf));
	g_return_if_fail (IS_GO_DATA_SLICER (dsf->ds));
	g_return_if_fail (field_type > GDS_FIELD_TYPE_UNSET &&
			  field_type < GDS_FIELD_TYPE_MAX);

	headers = dsf->ds->fields [field_type];
	if (pos < 0) pos = -1;
	else if (pos >= (int)headers->len) pos = headers->len;

	cur_pos = dsf->field_type_pos[field_type];
	if (pos == cur_pos) return;

	/* Remove it */
	if (cur_pos >= 0) {
		g_return_if_fail (cur_pos < (int)headers->len);
		g_return_if_fail (g_array_index (headers, int, cur_pos) == dsf->indx);

		g_array_remove_index (headers, cur_pos);
		dsf->field_type_pos[field_type] = -1;
		for (i = cur_pos; i < (int)headers->len ; i++) {
			GODataSlicerField *other = go_data_slicer_get_field (dsf->ds,
				g_array_index (headers, int, i));
			if (NULL != other && other->field_type_pos[field_type] == (i+1))
				--(other->field_type_pos[field_type]);
			else
				g_warning ("inconsistent field_type_pos");
		}

		/* adjust target index if our removal would change it */
		if (cur_pos < pos) pos--;
	}

	/* put it back in the right place */
	if (pos >= 0) {
		if (pos < (int)headers->len) {
			g_array_insert_val (headers, pos, dsf->indx);
			for (i = pos; ++i < (int)headers->len ; ) {
				GODataSlicerField *other = go_data_slicer_get_field (dsf->ds,
					g_array_index (headers, int, i));
				if (NULL != other && other->field_type_pos[field_type] == (i-1))
					++(other->field_type_pos[field_type]);
				else
					g_warning ("inconsistent field_type_pos");
			}
		} else
			g_array_append_val (headers, dsf->indx);
	}
	dsf->field_type_pos[field_type] = pos;
}

