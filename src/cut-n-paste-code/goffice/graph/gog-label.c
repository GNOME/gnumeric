/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * gog-label.c
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
#include <goffice/graph/gog-label.h>
#include <goffice/graph/gog-object.h>

#include <src/gui-util.h>
#include <src/gnumeric-i18n.h>

#include <glade/glade-xml.h>
#include <gtk/gtkspinbutton.h>
#include <widgets/widget-color-combo.h>
#include <gsf/gsf-impl-utils.h>

struct _GogLabel {
	GogStyledObject	base;

	GODataScalar	*text;
	gboolean	 allow_markup;
};

typedef struct {
	GogObjectClass	base;
} GogLabelClass;

enum {
	LABEL_PROP_0,
};

static GObjectClass *parent_klass;

static void
gog_label_set_property (GObject *obj, guint param_id,
			     GValue const *value, GParamSpec *pspec)
{
	GogLabel *label = GOG_LABEL (obj);

	switch (param_id) {
	default: G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, param_id, pspec);
		 return; /* NOTE : RETURN */
	}
	gog_object_emit_changed (GOG_OBJECT (obj), FALSE);
}

static void
gog_label_get_property (GObject *obj, guint param_id,
			     GValue *value, GParamSpec *pspec)
{
	GogLabel *label = GOG_LABEL (obj);

	switch (param_id) {

	default: G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, param_id, pspec);
		 break;
	}
}

static void
gog_label_finalize (GObject *obj)
{
	GogLabel *label = GOG_LABEL (obj);

	if (parent_klass != NULL && parent_klass->finalize != NULL)
		(parent_klass->finalize) (obj);
}

static char const *
gog_label_type_name (GogObject const *item)
{
	return "GraphLabel";
}

static void
gog_label_class_init (GogLabelClass *klass)
{
	GObjectClass *gobject_klass = (GObjectClass *) klass;
	GogObjectClass *gog_klass = (GogObjectClass *) klass;

	parent_klass = g_type_class_peek_parent (klass);
	gobject_klass->set_property = gog_label_set_property;
	gobject_klass->get_property = gog_label_get_property;
	gobject_klass->finalize	    = gog_label_finalize;

	gog_klass->type_name	= gog_label_type_name;
	gog_klass->editor	= gog_label_editor;
}

GSF_CLASS (GogLabel, gog_label,
	   gog_label_class_init, NULL,
	   GOG_OBJECT_TYPE)

float
go_fraph_label_border_width (GogLabel const *label)
{
	g_return_val_if_fail (IS_GOG_LABEL (label), 0.);
	return label->border_width;
}
