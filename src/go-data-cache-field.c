/*
 * go-data-cache-field.h : A field (named vector) within a cache of data
 *	containing the unique values (unodered).  The cache contains the
 *	ordering, and allows replication.
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
#include <go-data-cache-field-impl.h>
#include <go-data-cache-impl.h>
#include <go-data-cache.h>

#include <go-val.h>

#include <gsf/gsf-impl-utils.h>
#include <glib/gi18n-lib.h>
#include <string.h>

#define GO_DATA_CACHE_FIELD_CLASS(k)	 (G_TYPE_CHECK_CLASS_CAST ((k), GO_DATA_CACHE_FIELD_TYPE, GODataCacheFieldClass))
#define IS_GO_DATA_CACHE_FIELD_CLASS(k)	 (G_TYPE_CHECK_CLASS_TYPE ((k), GO_DATA_CACHE_FIELD_TYPE))
#define GO_DATA_CACHE_FIELD_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), GO_DATA_CACHE_FIELD_TYPE, GODataCacheFieldClass))

enum {
	PROP_0,
	PROP_CACHE,		/* GODataCache * */
	PROP_NAME,		/* GOString */
	PROP_INDEX,		/* int */
	PROP_BUCKETER,		/* GOValBucketer * */
	PROP_GROUP_PARENT	/* int */
};

static void
go_data_cache_field_init (GODataCacheField *field)
{
	field->cache		= NULL;
	field->name		= NULL;
	field->indx		= -1;
	field->group_parent	= -1;
	field->indexed		= NULL;
	field->grouped		= NULL;
	go_val_bucketer_init (&field->bucketer);
}

static GObjectClass *parent_klass;

static void
go_data_cache_field_finalize (GObject *obj)
{
	GODataCacheField *field = (GODataCacheField *)obj;

	field->cache = NULL; /* we do not hold a ref */

	go_string_unref (field->name); field->name = NULL;

	go_val_array_free (field->indexed);
	field->indexed = NULL;
	go_val_array_free (field->grouped);
	field->grouped = NULL;

	(parent_klass->finalize) (obj);
}

static void
go_data_cache_field_set_property (GObject *obj, guint property_id,
				  GValue const *value, GParamSpec *pspec)
{
	GODataCacheField *field = (GODataCacheField *)obj;

	switch (property_id) {
	/* we do not hold a ref */
	case PROP_CACHE : field->cache = g_value_get_object (value); break;
	case PROP_NAME :	 go_string_unref (field->name); field->name = g_value_dup_boxed (value); break;
	case PROP_BUCKETER :	 field->bucketer = *((GOValBucketer *)g_value_get_pointer (value)); break;
	case PROP_GROUP_PARENT : field->group_parent = g_value_get_int (value); break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, property_id, pspec);
	}
}

static void
go_data_cache_field_get_property (GObject *obj, guint property_id,
				    GValue *value, GParamSpec *pspec)
{
	GODataCacheField const *field = (GODataCacheField const *)obj;
	switch (property_id) {
	case PROP_CACHE : g_value_set_object (value, field->cache); break;
	case PROP_NAME  : g_value_set_boxed (value, field->name); break;
	case PROP_INDEX : g_value_set_int (value, field->indx); break;
	case PROP_BUCKETER :	 g_value_set_pointer (value, (gpointer) &field->bucketer); break;
	case PROP_GROUP_PARENT : g_value_set_int (value, field->group_parent); break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, property_id, pspec);
	}
}

static void
go_data_cache_field_class_init (GODataCacheFieldClass *klass)
{
	GObjectClass *gobject_class = (GObjectClass *)klass;
	gobject_class->set_property	= go_data_cache_field_set_property;
	gobject_class->get_property	= go_data_cache_field_get_property;
	gobject_class->finalize		= go_data_cache_field_finalize;

	g_object_class_install_property (gobject_class, PROP_CACHE,
		 g_param_spec_object ("cache", NULL, NULL,
			GO_DATA_CACHE_TYPE, GSF_PARAM_STATIC | G_PARAM_READWRITE));
	g_object_class_install_property (gobject_class, PROP_NAME,
		 g_param_spec_boxed ("name", NULL, NULL, go_string_get_type (),
			GSF_PARAM_STATIC | G_PARAM_READWRITE));
	g_object_class_install_property (gobject_class, PROP_INDEX,
		 g_param_spec_int ("index", NULL,
			"Index to identify field if the header changes in the source",
			-1, G_MAXINT, -1,
			GSF_PARAM_STATIC | G_PARAM_READABLE));
	g_object_class_install_property (gobject_class, PROP_BUCKETER,
		 g_param_spec_pointer ("bucketer", NULL,
			"How to group values",
			GSF_PARAM_STATIC | G_PARAM_READWRITE));

	g_object_class_install_property (gobject_class, PROP_GROUP_PARENT,
		 g_param_spec_int ("group-base", NULL,
			"Index to CacheField of the source of the group",
			-1, G_MAXINT, -1,
			GSF_PARAM_STATIC | G_PARAM_READWRITE));

	/* Unclear how to select what is parent and what is child when dealing with groups */
	g_object_class_install_property (gobject_class, PROP_GROUP_PARENT,
		 g_param_spec_int ("group-parent", NULL,
			"Index to CacheField with higher precedence in the group ?",
			-1, G_MAXINT, -1,
			GSF_PARAM_STATIC | G_PARAM_READWRITE));

	parent_klass = g_type_class_peek_parent (klass);
}

GSF_CLASS (GODataCacheField, go_data_cache_field,
	   go_data_cache_field_class_init, go_data_cache_field_init,
	   G_TYPE_OBJECT)

GODataCache *
go_data_cache_field_get_cache (GODataCacheField const *field)
{
	g_return_val_if_fail (IS_GO_DATA_CACHE_FIELD (field), NULL);
	return field->cache;
}

GOString *
go_data_cache_field_get_name (GODataCacheField const *field)
{
	g_return_val_if_fail (IS_GO_DATA_CACHE_FIELD (field), go_string_ERROR ());
	return field->name;
}

GOValArray const *
go_data_cache_field_get_vals (GODataCacheField const *field, gboolean group_val)
{
	g_return_val_if_fail (IS_GO_DATA_CACHE_FIELD (field), NULL);
	return group_val ? field->grouped : field->indexed;
}

void
go_data_cache_field_set_vals (GODataCacheField *field, gboolean group_val,
			      GOValArray *vals)
{
	g_return_if_fail (IS_GO_DATA_CACHE_FIELD (field));

	go_val_array_free (group_val ? field->grouped : field->indexed);
	if (group_val)
		field->grouped = vals;
	else
		field->indexed = vals;
}

gboolean
go_data_cache_field_is_base (GODataCacheField const *field)
{
	g_return_val_if_fail (IS_GO_DATA_CACHE_FIELD (field), FALSE);
	g_print ("[%d] %s : parent = %d\n", field->indx, field->name->str,
		 field->group_parent);
	return field->group_parent < 0 || field->group_parent == field->indx;
}

GODataCacheFieldType
go_data_cache_field_ref_type (GODataCacheField const *field)
{
	g_return_val_if_fail (IS_GO_DATA_CACHE_FIELD (field), GO_DATA_CACHE_FIELD_TYPE_NONE);
	return field->ref_type;
}

GOVal const *
go_data_cache_field_get_val (GODataCacheField const *field, unsigned int record_num)
{
	gpointer p;
	unsigned int idx;

	g_return_val_if_fail (IS_GO_DATA_CACHE_FIELD (field), NULL);

	p = go_data_cache_records_index (field->cache, record_num) + field->offset;
	switch (field->ref_type) {
	case GO_DATA_CACHE_FIELD_TYPE_NONE:
		return NULL;
	case GO_DATA_CACHE_FIELD_TYPE_INLINE:
		return *((GOVal **)p);
	case GO_DATA_CACHE_FIELD_TYPE_INDEXED_I8:
		idx = *(guint8 *)p;
		break;
	case GO_DATA_CACHE_FIELD_TYPE_INDEXED_I16:
		idx = *(guint16 *)p;
		break;
	case GO_DATA_CACHE_FIELD_TYPE_INDEXED_I32:
		idx = *(guint32 *)p;
		break;
	default:
		g_warning ("unknown field type %d", field->ref_type);
		return NULL;
	}

	return (idx-- > 0) ? g_ptr_array_index (field->indexed, idx) : NULL;
}
