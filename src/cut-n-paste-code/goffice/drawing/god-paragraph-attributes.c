/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * god-paragraph-attributes.c: MS Office Graphic Object support
 *
 * Copyright (C) 2000-2002
 *	Jody Goldberg (jody@gnome.org)
 *	Michael Meeks (mmeeks@gnu.org)
 *      Christopher James Lahey <clahey@ximian.com>
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
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
 * USA
 */

#include <goffice/goffice-config.h>
#include "drawing/god-paragraph-attributes.h"

#include <glib/gi18n.h>
#include <gsf/gsf-impl-utils.h>

static GObjectClass *parent_class;

struct GodParagraphAttributesPrivate_ {
	double indent;
	double space_before;
	double space_after;
};

enum {
	PROP_0,
	PROP_INDENT,
	PROP_SPACE_BEFORE,
	PROP_SPACE_AFTER,
};

GodParagraphAttributes *
god_paragraph_attributes_new (void)
{
	GodParagraphAttributes *paragraph;

	paragraph = g_object_new (GOD_PARAGRAPH_ATTRIBUTES_TYPE, NULL);

	return paragraph;
}

static void
god_paragraph_attributes_init (GObject *object)
{
	GodParagraphAttributes *paragraph = GOD_PARAGRAPH_ATTRIBUTES (object);
	paragraph->priv = g_new0 (GodParagraphAttributesPrivate, 1);
	paragraph->priv->indent = 0;
	paragraph->priv->space_before = 0;
	paragraph->priv->space_after = 22860000;
}

static void
god_paragraph_attributes_finalize (GObject *object)
{
	GodParagraphAttributes *paragraph = GOD_PARAGRAPH_ATTRIBUTES (object);

	g_free (paragraph->priv);
	paragraph->priv = NULL;

	G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
god_paragraph_attributes_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
	GodParagraphAttributes *paragraph = GOD_PARAGRAPH_ATTRIBUTES (object);

	switch (prop_id) {
	case PROP_INDENT:
		paragraph->priv->indent = g_value_get_double (value);
		break;
	case PROP_SPACE_BEFORE:
		paragraph->priv->space_before = g_value_get_double (value);
		break;
	case PROP_SPACE_AFTER:
		paragraph->priv->space_after = g_value_get_double (value);
		break;
	}
}

static void
god_paragraph_attributes_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
	GodParagraphAttributes *paragraph = GOD_PARAGRAPH_ATTRIBUTES (object);

	switch (prop_id){
	case PROP_INDENT:
		g_value_set_double (value, paragraph->priv->indent);
		break;
	case PROP_SPACE_BEFORE:
		g_value_set_double (value, paragraph->priv->space_before);
		break;
	case PROP_SPACE_AFTER:
		g_value_set_double (value, paragraph->priv->space_after);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
god_paragraph_attributes_class_init (GodParagraphAttributesClass *class)
{
	GObjectClass *object_class;

	object_class               = (GObjectClass *) class;

	parent_class               = g_type_class_peek_parent (class);

	object_class->finalize     = god_paragraph_attributes_finalize;
	object_class->get_property = god_paragraph_attributes_get_property;
	object_class->set_property = god_paragraph_attributes_set_property;

	g_object_class_install_property (object_class, PROP_INDENT,
					 g_param_spec_double ("indent",
							      _( "Indent" ),
							      _( "Indent" ),
							      0.0, G_MAXDOUBLE, 0.0,
							      G_PARAM_READWRITE));
	g_object_class_install_property (object_class, PROP_SPACE_BEFORE,
					 g_param_spec_double ("space_before",
							      _( "Space Before" ),
							      _( "Space Before" ),
							      0.0, G_MAXDOUBLE, 0.0,
							      G_PARAM_READWRITE));
	g_object_class_install_property (object_class, PROP_SPACE_AFTER,
					 g_param_spec_double ("space_after",
							      _( "Space After" ),
							      _( "Space After" ),
							      0.0, G_MAXDOUBLE, 0.0,
							      G_PARAM_READWRITE));
}

GSF_CLASS (GodParagraphAttributes, god_paragraph_attributes,
	   god_paragraph_attributes_class_init, god_paragraph_attributes_init,
	   G_TYPE_OBJECT)
