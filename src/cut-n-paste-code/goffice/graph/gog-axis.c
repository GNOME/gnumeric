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

#include <gsf/gsf-impl-utils.h>
#include <src/gnumeric-i18n.h>

struct _GogAxis {
	GogObject	base;
};

typedef struct {
	GogObjectClass	base;
} GogAxisClass;

enum {
	AXIS_PROP_0,
};

static GObjectClass *parent_klass;

static void
gog_axis_set_property (GObject *obj, guint param_id,
			    GValue const *value, GParamSpec *pspec)
{
	/* GogAxis *axis = GOG_AXIS (obj); */

	switch (param_id) {

	default: G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, param_id, pspec);
		 return; /* NOTE : RETURN */
	}
}

static void
gog_axis_get_property (GObject *obj, guint param_id,
			    GValue *value, GParamSpec *pspec)
{
	/* GogAxis *axis = GOG_AXIS (obj); */

	switch (param_id) {

	default: G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, param_id, pspec);
		 break;
	}
}

static void
gog_axis_finalize (GObject *obj)
{
	/* GogAxis *axis = GOG_AXIS (obj); */

	if (parent_klass != NULL && parent_klass->finalize != NULL)
		(parent_klass->finalize) (obj);
}

static char const *
gog_axis_type_name (GogObject const *item)
{
	return "Axis";
}

static void
gog_axis_class_init (GogAxisClass *klass)
{
	GObjectClass *gobject_klass   = (GObjectClass *) klass;
	GogObjectClass *gog_klass = (GogObjectClass *) klass;

	parent_klass = g_type_class_peek_parent (klass);
	gobject_klass->set_property = gog_axis_set_property;
	gobject_klass->get_property = gog_axis_get_property;
	gobject_klass->finalize	    = gog_axis_finalize;

	gog_klass->type_name = gog_axis_type_name;
}

static void
gog_axis_init (GogGraph *graph)
{
}

GSF_CLASS (GogAxis, gog_axis,
	   gog_axis_class_init, gog_axis_init,
	   GOG_OBJECT_TYPE)

