/*
 * go-data-slicer.h : The definition of a content for a data slicer
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
#include <go-data-slicer-impl.h>
#include <go-data-slicer-field-impl.h>
#include <go-data-cache.h>

#include <gsf/gsf-impl-utils.h>
#include <glib/gi18n-lib.h>
#include <string.h>

#define GO_DATA_SLICER_CLASS(k)		(G_TYPE_CHECK_CLASS_CAST ((k), GO_DATA_SLICER_TYPE, GODataSlicerClass))
#define IS_GO_DATA_SLICER_CLASS(k)	(G_TYPE_CHECK_CLASS_TYPE ((k), GO_DATA_SLICER_TYPE))
#define GO_DATA_SLICER_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), GO_DATA_SLICER_TYPE, GODataSlicerClass))

enum {
	PROP_0,
	PROP_CACHE,	/* GODataCache * */
	PROP_NAME,	/* GOString */
};
static void
go_data_slicer_init (GODataSlicer *ds)
{
	int i;

	ds->cache = NULL;
	ds->name  = NULL;
	ds->all_fields  = g_ptr_array_new ();
	for (i = GDS_FIELD_TYPE_MAX ; --i > GDS_FIELD_TYPE_UNSET ; )
		ds->fields[i] = g_array_new (FALSE, FALSE, sizeof (int));
}

static GObjectClass *parent_klass;
static void
go_data_slicer_finalize (GObject *obj)
{
	GODataSlicer *ds = (GODataSlicer *)obj;
	int i;

	for (i = GDS_FIELD_TYPE_MAX ; --i > GDS_FIELD_TYPE_UNSET ; ) {
		g_array_free (ds->fields[i], TRUE);
		ds->fields[i] = NULL;
	}

	for (i = (int)ds->all_fields->len ; i-- > 0 ; )
		g_object_unref (g_ptr_array_index (ds->all_fields, i));
	g_ptr_array_free (ds->all_fields, TRUE);
	ds->all_fields = NULL;

	go_data_slicer_set_cache (ds, NULL);
	go_string_unref (ds->name); ds->name   = NULL;

	(parent_klass->finalize) (obj);
}

static void
go_data_slicer_set_property (GObject *obj, guint property_id,
				  GValue const *value, GParamSpec *pspec)
{
	GODataSlicer *ds = (GODataSlicer *)obj;

	switch (property_id) {
	case PROP_CACHE : go_data_slicer_set_cache (ds, g_value_get_object (value)); break;
	case PROP_NAME :  go_string_unref (ds->name); ds->name = g_value_dup_boxed (value); break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, property_id, pspec);
	}
}

static void
go_data_slicer_get_property (GObject *obj, guint property_id,
				    GValue *value, GParamSpec *pspec)
{
	GODataSlicer const *ds = (GODataSlicer const *)obj;
	switch (property_id) {
	case PROP_CACHE : g_value_set_object (value, ds->cache); break;
	case PROP_NAME  : g_value_set_boxed (value, ds->name); break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, property_id, pspec);
	}
}

static void
go_data_slicer_class_init (GODataSlicerClass *klass)
{
	GObjectClass *gobject_class = (GObjectClass *)klass;
	gobject_class->set_property	= go_data_slicer_set_property;
	gobject_class->get_property	= go_data_slicer_get_property;
	gobject_class->finalize		= go_data_slicer_finalize;

	g_object_class_install_property (gobject_class, PROP_CACHE,
		 g_param_spec_object ("cache", NULL, NULL,
			GO_DATA_CACHE_TYPE, GSF_PARAM_STATIC | G_PARAM_READWRITE));
	g_object_class_install_property (gobject_class, PROP_NAME,
		 g_param_spec_boxed ("name", NULL, NULL, go_string_get_type (),
			GSF_PARAM_STATIC | G_PARAM_READWRITE));

	parent_klass = g_type_class_peek_parent (klass);
}

GSF_CLASS (GODataSlicer, go_data_slicer,
	   go_data_slicer_class_init, go_data_slicer_init,
	   G_TYPE_OBJECT)

/**
 * go_data_slicer_get_cache:
 * @ds: #GODataSlicer
 *
 * Does not add a reference.
 *
 * Returns : the #GODataCache associated with @ds
 **/
GODataCache *
go_data_slicer_get_cache (GODataSlicer const *ds)
{
	g_return_val_if_fail (IS_GO_DATA_SLICER (ds), NULL);
	return ds->cache;
}

/**
 * go_data_slicer_set_cache:
 * @ds: #GODataSlicer
 * @cache: #GODataCache
 *
 * Assign @cache to @ds, and adds a reference to @cache
 **/
void
go_data_slicer_set_cache (GODataSlicer *ds, GODataCache *cache)
{
	g_return_if_fail (IS_GO_DATA_SLICER (ds));

	if (NULL != cache)
		g_object_ref (cache);
	if (NULL != ds->cache)
		g_object_unref (ds->cache);
	ds->cache = cache;
}

void
go_data_slicer_add_field (GODataSlicer *ds, GODataSlicerField *field)
{
	g_return_if_fail (IS_GO_DATA_SLICER (ds));
	g_return_if_fail (IS_GO_DATA_SLICER_FIELD (field));
	g_return_if_fail (field->indx < 0);
	g_return_if_fail (field->ds == NULL);

	field->indx = ds->all_fields->len;
	field->ds   = ds;
	g_ptr_array_add (ds->all_fields, field);
}

unsigned int
go_data_slicer_num_fields (GODataSlicer const *ds)
{
	g_return_val_if_fail (IS_GO_DATA_SLICER (ds), 0);
	return ds->all_fields->len;
}

GODataSlicerField *
go_data_slicer_get_field (GODataSlicer const *ds, unsigned int field_index)
{
	g_return_val_if_fail (IS_GO_DATA_SLICER (ds), NULL);
	g_return_val_if_fail (field_index < ds->all_fields->len, NULL);
	return g_ptr_array_index (ds->all_fields, field_index);
}

