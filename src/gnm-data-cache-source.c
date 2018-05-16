/*
 * gnm-data-cache-source.c : GODataCacheSource from a Sheet
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
#include <gnm-data-cache-source.h>
#include <go-data-cache-source.h>
#include <go-data-cache.h>


#include <gnumeric.h>
#include <ranges.h>
#include <sheet.h>
#include <expr-name.h>

#include <gsf/gsf-impl-utils.h>
#include <gnm-i18n.h>
#include <string.h>

struct _GnmDataCacheSource {
	GObject		base;

	Sheet	  *src_sheet;
	GnmRange   src_range;	/* treated as cache if src_name is non-NULL */
	GOString  *src_name;	/* optionally NULL */
};
typedef GObjectClass GnmDataCacheSourceClass;

enum {
	PROP_0,
	PROP_SHEET,	/* GnmSheet * */
	PROP_RANGE,	/* GnmRange */
	PROP_NAME	/* char *, optionally NULL */
};

static GODataCache *
gdcs_allocate (GODataCacheSource const *src)
{
	GnmDataCacheSource *gdcs = (GnmDataCacheSource *)src;
	GODataCache *res;

	g_return_val_if_fail (gdcs->src_sheet != NULL, NULL);

	if (NULL != gdcs->src_name) {
		GnmParsePos pp;
		GnmEvalPos ep;
		GnmNamedExpr *nexpr = expr_name_lookup (
			parse_pos_init_sheet (&pp, gdcs->src_sheet), gdcs->src_name->str);
		if (NULL != nexpr) {
			GnmValue *v = expr_name_eval (nexpr,
				eval_pos_init_sheet (&ep, gdcs->src_sheet),
				GNM_EXPR_EVAL_PERMIT_NON_SCALAR	| GNM_EXPR_EVAL_PERMIT_EMPTY);

			if (NULL != v) {
				value_release (v);
			}
		}
	}

	res = g_object_new (GO_DATA_CACHE_TYPE, NULL);

	return res;
}

static GError *
gdcs_validate (GODataCacheSource const *src)
{
	return NULL;
}

static gboolean
gdcs_needs_update (GODataCacheSource const *src)
{
	return FALSE;
}

static void
gnm_data_cache_source_init (GnmDataCacheSource *src)
{
	src->src_sheet = NULL;
	range_init_invalid (&src->src_range);
}

static GObjectClass *parent_klass;
static void
gnm_data_cache_source_finalize (GObject *obj)
{
	GnmDataCacheSource *src = (GnmDataCacheSource *)obj;
	go_string_unref (src->src_name);
	(parent_klass->finalize) (obj);
}
static void
gnm_data_cache_source_set_property (GObject *obj, guint property_id,
				    GValue const *value, GParamSpec *pspec)
{
	GnmDataCacheSource *src = (GnmDataCacheSource *)obj;

	switch (property_id) {
	case PROP_SHEET :
		gnm_data_cache_source_set_sheet (src, g_value_get_object (value));
		break;
	case PROP_RANGE :
		gnm_data_cache_source_set_range (src, g_value_get_boxed (value));
		break;
	case PROP_NAME :
		gnm_data_cache_source_set_name (src, g_value_get_string (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, property_id, pspec);
	}
}

static void
gnm_data_cache_source_get_property (GObject *obj, guint property_id,
				    GValue *value, GParamSpec *pspec)
{
	GnmDataCacheSource const *src = (GnmDataCacheSource const *)obj;
	switch (property_id) {
	case PROP_SHEET :
		g_value_set_object (value, gnm_data_cache_source_get_sheet (src));
		break;
	case PROP_RANGE :
		g_value_set_boxed (value, gnm_data_cache_source_get_range (src));
		break;
	case PROP_NAME :
		g_value_set_string (value, gnm_data_cache_source_get_name (src));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, property_id, pspec);
	}
}

static void
gnm_data_cache_source_class_init (GnmDataCacheSourceClass *klass)
{
	GObjectClass *gobject_class = (GObjectClass *)klass;
	gobject_class->set_property	= gnm_data_cache_source_set_property;
	gobject_class->get_property	= gnm_data_cache_source_get_property;
	gobject_class->finalize		= gnm_data_cache_source_finalize;

	g_object_class_install_property (gobject_class, PROP_SHEET,
		 g_param_spec_object ("src-sheet",
				      P_("Sheet"),
				      P_("The source sheet"),
			GNM_SHEET_TYPE, GSF_PARAM_STATIC | G_PARAM_READWRITE));
	g_object_class_install_property (gobject_class, PROP_RANGE,
		 g_param_spec_boxed ("src-range",
				     P_("Range"),
				     P_("Optional named expression to generate a source range"),
			gnm_range_get_type (), GSF_PARAM_STATIC | G_PARAM_READWRITE));
	g_object_class_install_property (gobject_class, PROP_NAME,
		 g_param_spec_string ("src-name",
				      P_("source-name"),
				      P_("Optional named expression to generate a source range"),
				      NULL,
			GSF_PARAM_STATIC | G_PARAM_READWRITE));

	parent_klass = g_type_class_peek_parent (klass);
}

static void
gnm_data_cache_source_iface_init (GODataCacheSourceClass *iface)
{
	iface->allocate	    = gdcs_allocate;
	iface->validate	    = gdcs_validate;
	iface->needs_update = gdcs_needs_update;
}

GSF_CLASS_FULL (GnmDataCacheSource, gnm_data_cache_source, NULL, NULL,
		gnm_data_cache_source_class_init, NULL,
		gnm_data_cache_source_init, G_TYPE_OBJECT, 0,
		GSF_INTERFACE (gnm_data_cache_source_iface_init, GO_DATA_CACHE_SOURCE_TYPE))

/**
 * gnm_data_cache_source_new:
 * @src_sheet: #Sheet
 * @src_range: #GnmRange
 * @src_name: char *, optionally %NULL
 *
 * Allocates a new Allocates a new #GnmDataCacheSource
 *
 * Returns : #GODataCacheSource
 **/
GODataCacheSource *
gnm_data_cache_source_new (Sheet *src_sheet,
			   GnmRange const *src_range, char const *src_name)
{
	GnmDataCacheSource *res;

	g_return_val_if_fail (IS_SHEET (src_sheet), NULL);
	g_return_val_if_fail (src_range != NULL, NULL);

	res = g_object_new (GNM_DATA_CACHE_SOURCE_TYPE, NULL);
	res->src_sheet = src_sheet;
	res->src_range = *src_range;
	gnm_data_cache_source_set_name (res, src_name);

	return GO_DATA_CACHE_SOURCE (res);
}

/**
 * gnm_data_cache_source_get_sheet:
 * @src: #GnmDataCacheSource
 *
 * Returns: (transfer none): the #Sheet for @src.
 **/
Sheet *
gnm_data_cache_source_get_sheet (GnmDataCacheSource const *src)
{
	g_return_val_if_fail (GNM_IS_DATA_CACHE_SOURCE (src), NULL);
	return src->src_sheet;
}

void
gnm_data_cache_source_set_sheet (GnmDataCacheSource *src, Sheet *sheet)
{
	g_return_if_fail (GNM_IS_DATA_CACHE_SOURCE (src));
}

GnmRange const	*
gnm_data_cache_source_get_range (GnmDataCacheSource const *src)
{
	g_return_val_if_fail (GNM_IS_DATA_CACHE_SOURCE (src), NULL);
	return &src->src_range;
}

void
gnm_data_cache_source_set_range (GnmDataCacheSource *src, GnmRange const *r)
{
	g_return_if_fail (GNM_IS_DATA_CACHE_SOURCE (src));
	src->src_range = *r;
}

char const *
gnm_data_cache_source_get_name  (GnmDataCacheSource const *src)
{
	g_return_val_if_fail (GNM_IS_DATA_CACHE_SOURCE (src), NULL);
	return src->src_name ? src->src_name->str : NULL;
}

void
gnm_data_cache_source_set_name (GnmDataCacheSource *src, char const *name)
{
	GOString *new_val;

	g_return_if_fail (GNM_IS_DATA_CACHE_SOURCE (src));

	new_val = go_string_new (name);
	go_string_unref (src->src_name);
	src->src_name =  new_val;
}
