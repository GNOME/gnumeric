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
#include <goffice/graph/gog-data-set.h>
#include <goffice/graph/go-data.h>

#include <gsf/gsf-impl-utils.h>
#include <src/gnumeric-i18n.h>

enum {
	AXIS_ELEM_MIN = 0,
	AXIS_ELEM_MAX,
	AXIS_ELEM_MAJOR_TICK,
	AXIS_ELEM_MINOR_TICK,
	AXIS_ELEM_MICRO_TICK,
	AXIS_ELEM_LAST_ENTRY
};

struct _GogAxis {
	GogObject	 base;

	GogAxisType	 type;
	GogAxisPosition	 pos;
	GSList		*i_cross, *crosses_me, *contributors;

	GogDatasetElement source [AXIS_ELEM_LAST_ENTRY];
	GogAxisTickLevel  tick_level;
};

typedef GogObjectClass GogAxisClass;

static GObjectClass *parent_klass;

enum {
	AXIS_PROP_0,
	AXIS_PROP_TYPE
};

static void
gog_axis_set_property (GObject *obj, guint param_id,
		       GValue const *value, GParamSpec *pspec)
{
	GogAxis *axis = GOG_AXIS (obj);

	switch (param_id) {
	case AXIS_PROP_TYPE:
		axis->type = g_value_get_int (value);
		break;
	default: G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, param_id, pspec);
		 return; /* NOTE : RETURN */
	}
}

static void
gog_axis_get_property (GObject *obj, guint param_id,
		       GValue *value, GParamSpec *pspec)
{
	GogAxis *axis = GOG_AXIS (obj);

	switch (param_id) {
	case AXIS_PROP_TYPE:
		g_value_set_int (value, axis->type);
		break;
	default: G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, param_id, pspec);
		 break;
	}
}

static void
gog_axis_finalize (GObject *obj)
{
	GogAxis *axis = GOG_AXIS (obj);

	g_slist_free (axis->i_cross);	 	axis->i_cross = NULL;
	g_slist_free (axis->crosses_me); 	axis->crosses_me = NULL;
	g_slist_free (axis->contributors);	axis->contributors = NULL;

	gog_dataset_finalize (GOG_DATASET (axis));
	if (parent_klass != NULL && parent_klass->finalize != NULL)
		(parent_klass->finalize) (obj);
}

static void
gog_axis_update (GogObject *obj)
{
	GSList *ptr;
	GogAxis *axis = GOG_AXIS (obj);

	g_warning ("axis::update");

	for (ptr = axis->contributors ; ptr != NULL ; ptr = ptr->next) {
	}
}

static void
gog_axis_class_init (GObjectClass *gobject_klass)
{
	static GogObjectRole const roles[] = {
		{ N_("Label"), "GogLabel",
		  GOG_POSITION_COMPASS, GOG_POSITION_N|GOG_POSITION_ALIGN_CENTER, GOG_OBJECT_NAME_BY_ROLE,
		  NULL, NULL, NULL, NULL, NULL, NULL },
	};
	GogObjectClass *gog_klass = (GogObjectClass *) gobject_klass;

	parent_klass = g_type_class_peek_parent (gobject_klass);
	gobject_klass->set_property = gog_axis_set_property;
	gobject_klass->get_property = gog_axis_get_property;
	gobject_klass->finalize	    = gog_axis_finalize;

	/* no need to persist, the role handles that */
	g_object_class_install_property (gobject_klass, AXIS_PROP_TYPE,
		g_param_spec_int ("type", "Type",
			"GogAxisType",
			GOG_AXIS_X, GOG_AXIS_TYPES, GOG_AXIS_TYPES, G_PARAM_READWRITE));

	gog_object_register_roles (gog_klass, roles, G_N_ELEMENTS (roles));
	gog_klass->update = gog_axis_update;
}

static void
gog_axis_init (GogAxis *axis)
{
	axis->type	 = GOG_AXIS_UNKNOWN;
	axis->pos	 = GOG_AXIS_AT_LOW;
	axis->tick_level = AXIS_ELEM_MINOR_TICK,
	axis->i_cross =	axis->crosses_me = axis->contributors = NULL;
}

static void
gog_axis_dataset_dims (GogDataset const *set, int *first, int *last)
{
	*first = AXIS_ELEM_MIN;
	*last  = AXIS_ELEM_MICRO_TICK;
}

static GogDatasetElement *
gog_axis_dataset_get_elem (GogDataset const *set, int dim_i)
{
	GogAxis *axis = GOG_AXIS (set);
	if (AXIS_ELEM_MIN <= dim_i && dim_i <= AXIS_ELEM_MICRO_TICK)
		return &axis->source[dim_i];
	return NULL;
}

static void
gog_axis_dataset_init (GogDatasetClass *iface)
{
	iface->dims	= gog_axis_dataset_dims;
	iface->get_elem	= gog_axis_dataset_get_elem;
}

GSF_CLASS_FULL (GogAxis, gog_axis,
		gog_axis_class_init, gog_axis_init,
		GOG_OBJECT_TYPE, 0,
		GSF_INTERFACE (gog_axis_dataset_init, GOG_DATASET_TYPE))

GogAxisType
gog_axis_type (GogAxis const *axis)
{
	g_return_val_if_fail (GOG_AXIS (axis) != NULL, FALSE);
	return axis->type;
}

void
gog_axis_add_contributor (GogAxis *axis, GogObject *contrib)
{
	g_return_if_fail (GOG_AXIS (axis) != NULL);
	g_return_if_fail (g_slist_find (axis->contributors, contrib) == NULL);

	axis->contributors = g_slist_prepend (axis->contributors, contrib);
}


void
gog_axis_del_contributor (GogAxis *axis, GogObject *contrib)
{
	g_return_if_fail (GOG_AXIS (axis) != NULL);
	g_return_if_fail (g_slist_find (axis->contributors, contrib) != NULL);

	axis->contributors = g_slist_remove (axis->contributors, contrib);
}
