/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * gog-axis.c :
 *
 * Copyright (C) 2003 Jody Goldberg (jody@gnome.org)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
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
#include <goffice/graph/gog-axis.h>
#include <goffice/graph/gog-object.h>
#include <goffice/graph/gog-data-allocator.h>

#include <gsf/gsf-impl-utils.h>
#include <src/gnumeric-i18n.h>

struct _GogAxis {
	GogObject	 base;

	GogAxisType	 type;
	GogAxisPosition	 pos;
	GSList		*i_cross, *crosses_me, *plots;
	GOData		*min_source, *max_source;
};

typedef struct {
	GogObjectClass	base;
} GogAxisClass;

static GObjectClass *parent_klass;

static void
gog_axis_finalize (GObject *obj)
{
	GogAxis *axis = GOG_AXIS (obj);

	g_slist_free (axis->i_cross);	 axis->i_cross = NULL;
	g_slist_free (axis->crosses_me); axis->crosses_me = NULL;
	g_slist_free (axis->plots);	 axis->plots = NULL;
	if (axis->min_source != NULL) {
		g_object_unref (axis->min_source);
		axis->min_source = NULL;
	}
	if (axis->max_source != NULL) {
		g_object_unref (axis->max_source);
		axis->max_source = NULL;
	}

	if (parent_klass != NULL && parent_klass->finalize != NULL)
		(parent_klass->finalize) (obj);
}

static char const *
gog_axis_type_name (GogObject const *obj)
{
	return "Axis";
}

static void
gog_axis_class_init (GogAxisClass *klass)
{
	static GogObjectRole const roles[] = {
		{ N_("Label"), "GogLabel",
		  GOG_POSITION_COMPASS, GOG_POSITION_N|GOG_POSITION_ALIGN_CENTER, FALSE,
		  NULL, NULL, NULL, NULL, NULL, NULL },
	};
	GObjectClass *gobject_klass   = (GObjectClass *) klass;
	GogObjectClass *gog_klass = (GogObjectClass *) klass;

	parent_klass = g_type_class_peek_parent (klass);
	gobject_klass->finalize	    = gog_axis_finalize;

	gog_klass->type_name = gog_axis_type_name;
	gog_object_register_roles (gog_klass, roles, G_N_ELEMENTS (roles));
}

static void
gog_axis_init (GogGraph *graph)
{
}

static void
gog_axis_dataset_dims (GogDataset const *set, int *first, int *last)
{
}

static GOData *
gog_axis_dataset_get_dim (GogDataset const *set, int dim_i)
{
	return NULL;
}

static void
gog_axis_dataset_set_dim (GogDataset *set, int dim_i,
			  GOData *val, GError **err)
{
}

static void
gog_axis_dataset_init (GogDatasetClass *iface)
{
	iface->dims	= gog_axis_dataset_dims;
	iface->get_dim	= gog_axis_dataset_get_dim;
	iface->set_dim	= gog_axis_dataset_set_dim;
}

GSF_CLASS_FULL (GogAxis, gog_axis,
		gog_axis_class_init, gog_axis_init,
		GOG_OBJECT_TYPE, 0,
		GSF_INTERFACE (gog_axis_dataset_init, GOG_DATASET_TYPE))

