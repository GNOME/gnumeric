/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * gog-styled-object.c : A base class for objects that have associated styles
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
#include <goffice/graph/gog-styled-object.h>
#include <goffice/graph/gog-style.h>
#include <goffice/graph/gog-theme.h>

#include <src/gnumeric-i18n.h>
#include <gsf/gsf-impl-utils.h>

enum {
	STYLED_OBJECT_PROP_0,
	STYLED_OBJECT_PROP_STYLE
};

static GObjectClass *parent_klass;

static void
gog_styled_object_set_property (GObject *obj, guint param_id,
				GValue const *value, GParamSpec *pspec)
{
	GogStyledObject *gso = GOG_STYLED_OBJECT (obj);
	GogStyle *style;
	gboolean resize = FALSE;

	switch (param_id) {

	case STYLED_OBJECT_PROP_STYLE :
		style = g_value_get_object (value);
		if (gso->style == style)
			return;
		resize = gog_style_is_different_size (gso->style, style);
		if (style != NULL)
			g_object_ref (style);
		if (gso->style != NULL)
			g_object_unref (gso->style);
		gso->style = style;
		break;

	default: G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, param_id, pspec);
		 return; /* NOTE : RETURN */
	}
	gog_object_emit_changed (GOG_OBJECT (obj), resize);
}

static void
gog_styled_object_get_property (GObject *obj, guint param_id,
			     GValue *value, GParamSpec *pspec)
{
	GogStyledObject *gso = GOG_STYLED_OBJECT (obj);

	switch (param_id) {
	case STYLED_OBJECT_PROP_STYLE :
		g_value_set_object (value, gso->style);
		break;

	default: G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, param_id, pspec);
		 break;
	}
}

static void
gog_styled_object_finalize (GObject *obj)
{
	GogStyledObject *gso = GOG_STYLED_OBJECT (obj);

	if (gso->style != NULL)
		g_object_unref (gso->style);

	if (parent_klass != NULL && parent_klass->finalize != NULL)
		(parent_klass->finalize) (obj);
}

static void
gog_styled_object_class_init (GogStyledObjectClass *klass)
{
	GObjectClass *gobject_klass = (GObjectClass *) klass;

	parent_klass = g_type_class_peek_parent (klass);
	gobject_klass->set_property = gog_styled_object_set_property;
	gobject_klass->get_property = gog_styled_object_get_property;
	gobject_klass->finalize	    = gog_styled_object_finalize;

	g_object_class_install_property (gobject_klass, STYLED_OBJECT_PROP_STYLE,
		g_param_spec_object ("style", "style",
			"GogStyle *",
			GOG_STYLE_TYPE, G_PARAM_READWRITE|GOG_PARAM_PERSISTENT));
}

static void
gog_styled_object_init (GogStyledObject *gso, gpointer klass)
{
	gso->style = gog_style_new (); /* use the defaults */
	gog_theme_init_style (NULL, gso->style, klass, 0);
}

GSF_CLASS (GogStyledObject, gog_styled_object,
	   gog_styled_object_class_init, gog_styled_object_init,
	   GOG_OBJECT_TYPE)

GogStyle *
gog_styled_object_get_style (GogStyledObject *gso)
{
	g_return_val_if_fail (GOG_STYLED_OBJECT (gso) != NULL, NULL);
	return gso->style;
}
