/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- *//* vim: set sw=8: */

/*
 * present-text.c: MS Office Graphic Object support
 *
 * Copyright (C) 2000-2004
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
#include "libpresent/present-text.h"
#include <libpresent/present-presentation.h>

#include <glib/gi18n.h>
#include <gsf/gsf-impl-utils.h>

static GObjectClass *parent_class;

struct PresentTextPrivate_ {
	int id;
	PresentTextType type;
	PresentPresentation *presentation;
};

enum {
	PROP_0,
	PROP_PRESENTATION,
};

void
present_text_set       (PresentText     *text,
			int              id,
			PresentTextType  type)
{
	text->priv->id = id;
	text->priv->type = type;
}

int
present_text_get_text_id    (PresentText     *text)
{
	return text->priv->id;
}

PresentTextType
present_text_get_text_type  (PresentText     *text)
{
	return text->priv->type;
}

GodTextModel *
present_text_new (int id, PresentTextType type)
{
	GodTextModel *text_model;

	text_model = g_object_new (PRESENT_TEXT_TYPE, NULL);

	present_text_set (PRESENT_TEXT (text_model), id, type);

	return text_model;
}

static void
present_text_init (GObject *object)
{
	PresentText *text_model = PRESENT_TEXT (object);
	text_model->priv        = g_new0 (PresentTextPrivate, 1);
	text_model->priv->id    = 0;
	text_model->priv->type  = PRESENT_TEXT_OTHER;
}

static void
present_text_finalize (GObject *object)
{
	PresentText *text_model = PRESENT_TEXT (object);

	g_free (text_model->priv);
	text_model->priv = NULL;

	G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
present_text_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
	PresentText *text = PRESENT_TEXT (object);

	switch (prop_id) {
	case PROP_PRESENTATION:
		if (text->priv->presentation != NULL)
			g_object_unref (text->priv->presentation);
		text->priv->presentation = g_value_get_object (value);
		if (text->priv->presentation != NULL)
			g_object_ref (text->priv->presentation);
		break;
	}
}

static void
present_text_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
	PresentText *text = PRESENT_TEXT (object);

	switch (prop_id){
	case PROP_PRESENTATION:
		g_value_set_object (value, text->priv->presentation);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static const GodDefaultAttributes *
present_text_get_default_attributes (GodTextModel *text)
{
	PresentText *present_text = PRESENT_TEXT (text);
	return present_presentation_get_default_attributes_for_text_type (present_text->priv->presentation, present_text->priv->type);
}


static void
present_text_class_init (PresentTextClass *class)
{
	GObjectClass *object_class;
	GodTextModelClass *god_text_model_class;

	object_class                                 = (GObjectClass *) class;
	god_text_model_class                         = (GodTextModelClass *) class;

	parent_class                                 = g_type_class_peek_parent (class);

	object_class->finalize                       = present_text_finalize;
	object_class->get_property                   = present_text_get_property;
	object_class->set_property                   = present_text_set_property;

	god_text_model_class->get_default_attributes = present_text_get_default_attributes;

	g_object_class_install_property (object_class, PROP_PRESENTATION,
					 g_param_spec_object ("presentation",
							      _( "Presentation" ),
							      _( "Presentation" ),
							      PRESENT_PRESENTATION_TYPE,
							      G_PARAM_READWRITE));
}

GSF_CLASS (PresentText, present_text,
	   present_text_class_init, present_text_init,
	   GOD_TEXT_MODEL_TYPE)
