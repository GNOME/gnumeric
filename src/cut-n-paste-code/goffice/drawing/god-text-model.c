/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- *//* vim: set sw=8: */

/*
 * god-text-model.c: MS Office Graphic Object support
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

#include "god-text-model.h"
#include <gsf/gsf-impl-utils.h>
#include <string.h>

static GObjectClass *parent_class;

struct GodTextModelPrivate_ {
	char *text;
};

GodTextModel *
god_text_model_new (void)
{
	GodTextModel *text;

	text = g_object_new (GOD_TEXT_MODEL_TYPE, NULL);

	return text;
}

const char *
god_text_model_get_text  (GodTextModel *text)
{
	if (GOD_TEXT_MODEL_GET_CLASS (text)->get_text)
		return GOD_TEXT_MODEL_GET_CLASS (text)->get_text (text);
	else
		return NULL;
}

void
god_text_model_set_text  (GodTextModel *text,
			   const char    *text_value)
{
	if (GOD_TEXT_MODEL_GET_CLASS (text)->set_text)
		GOD_TEXT_MODEL_GET_CLASS (text)->set_text (text, text_value);
}

static void
god_text_model_init (GObject *object)
{
	GodTextModel *text = GOD_TEXT_MODEL (object);
	text->priv = g_new0 (GodTextModelPrivate, 1);
	text->priv->text = NULL;
}

static void
god_text_model_finalize (GObject *object)
{
	GodTextModel *text = GOD_TEXT_MODEL (object);

	g_free (text->priv->text);
	g_free (text->priv);
	text->priv = NULL;

	G_OBJECT_CLASS (parent_klass)->finalize (obj);
}

static const char *
real_god_text_model_get_text (GodTextModel *text)
{
	return text->priv->text;
}

static void
real_god_text_model_set_text (GodTextModel *text,
			       const char    *text_value)
{
	if (text->priv->text)
		g_free (text->priv->text);
	text->priv->text = g_strdup (text_value);
}

static void
god_text_model_class_init (GodTextModelClass *class)
{
	GObjectClass *object_class;

	object_class           = (GObjectClass *) class;

	parent_class           = g_type_class_peek_parent (class);

	object_class->finalize = god_text_model_finalize;

	class->get_text        = real_god_text_model_get_text;
	class->set_text        = real_god_text_model_set_text;
}

GSF_CLASS (GodTextModel, god_text_model,
	   god_text_model_class_init, god_text_model_init,
	   G_TYPE_OBJECT)
