/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
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

#include <goffice/goffice-config.h>
#include "drawing/god-text-model.h"
#include <gsf/gsf-impl-utils.h>
#include <string.h>

#define PARAGRAPH(i) (text && text->priv && text->priv->paragraphs ? ((GodTextModelParagraph *)(text->priv->paragraphs->data)) + (i) : (GodTextModelParagraph *) NULL)

static GObjectClass *parent_class;

struct GodTextModelPrivate {
	GArray *paragraphs; /* Of type GodTextModelParagraph */
	char *text_cache;
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

void
god_text_model_set_paragraph_attributes (GodTextModel *text,
					 int start,
					 int end,
					 GodParagraphAttributes *attributes)
{
	if (GOD_TEXT_MODEL_GET_CLASS (text)->set_paragraph_attributes)
		GOD_TEXT_MODEL_GET_CLASS (text)->set_paragraph_attributes (text, start, end, attributes);
}

void
god_text_model_set_pango_attributes (GodTextModel *text,
				     int start,
				     int end,
				     PangoAttrList *attributes)
{
	if (GOD_TEXT_MODEL_GET_CLASS (text)->set_pango_attributes)
		GOD_TEXT_MODEL_GET_CLASS (text)->set_pango_attributes (text, start, end, attributes);
}

void
god_text_model_paragraph_foreach  (GodTextModel *text,
				   GodTextModelParagraphForeachCallback callback,
				   gpointer user_data)
{
	if (GOD_TEXT_MODEL_GET_CLASS (text)->paragraph_foreach)
		GOD_TEXT_MODEL_GET_CLASS (text)->paragraph_foreach (text, callback, user_data);
}

static void
god_text_model_init (GObject *object)
{
	GodTextModel *text = GOD_TEXT_MODEL (object);
	text->priv = g_new0 (GodTextModelPrivate, 1);
}

static void
god_text_model_finalize (GObject *object)
{
	GodTextModel *text = GOD_TEXT_MODEL (object);

	g_free (text->priv->text_cache);
	g_free (text->priv);
	text->priv = NULL;

	G_OBJECT_CLASS (parent_class)->finalize (object);
}

static const char *
real_god_text_model_get_text (GodTextModel *text)
{
	guint i;
	if (text->priv->text_cache == NULL && text->priv->paragraphs) {
		GString *string;
		string = g_string_new ("");
		for (i = 0; i < text->priv->paragraphs->len; i++) {
			g_string_append (string, PARAGRAPH(i)->text);
			g_string_append_c (string, '\n');
		}
		if (string->len > 0)
			g_string_truncate (string, string->len - 1);
		text->priv->text_cache = string->str;
		g_string_free (string, FALSE);
	}
	return text->priv->text_cache;
}

int
god_text_model_get_length (GodTextModel *text)
{
	guint i;
	if (text->priv->text_cache != NULL)
		return strlen (text->priv->text_cache);
	if (text->priv->paragraphs) {
		int length = 0;
		for (i = 0; i < text->priv->paragraphs->len; i++)
			length += strlen (PARAGRAPH(i)->text) + 1;
		if (length > 0)
			length --;
		return length;
	}
	return 0;
}

static void
real_god_text_model_set_text (GodTextModel *text,
			       const char    *text_value)
{
	gchar **paras;
	guint i;

	g_free (text->priv->text_cache);
	text->priv->text_cache = NULL;
	
	if (text->priv->paragraphs) {
		for (i = 0; i < text->priv->paragraphs->len; i++) {
			g_free (PARAGRAPH(i)->text);
			if (PARAGRAPH(i)->char_attributes)
				g_object_unref (PARAGRAPH(i)->char_attributes);
			if (PARAGRAPH(i)->para_attributes)
				g_object_unref (PARAGRAPH(i)->para_attributes);
		}
		g_array_free (text->priv->paragraphs, TRUE);
	}

	text->priv->paragraphs = g_array_new (TRUE, TRUE, sizeof (GodTextModelParagraph));
	
	paras = g_strsplit (text_value, "\r", 0);
	for (i = 0; paras[i]; i++) {
		GodTextModelParagraph paragraph;
		paragraph.text = paras[i];
		paragraph.char_attributes = NULL;
		paragraph.para_attributes = NULL;
		g_array_append_vals (text->priv->paragraphs, &paragraph, 1);
	}
	g_free (paras);
}

static void
real_god_text_model_set_paragraph_attributes (GodTextModel *text,
					      int start,
					      int end,
					      GodParagraphAttributes *attributes)
{
	guint i;
	guint count = 0;
	if (text->priv->paragraphs) {
		for (i = 0; i < text->priv->paragraphs->len; i++) {
			int length = strlen (PARAGRAPH(i)->text);
			if (count > end)
				return;
			if (count + length >= start) {
				if (PARAGRAPH(i)->para_attributes)
					g_object_unref (PARAGRAPH(i)->para_attributes);
				PARAGRAPH(i)->para_attributes = attributes;
				if (PARAGRAPH(i)->para_attributes)
					g_object_ref (PARAGRAPH(i)->para_attributes);
			}
			count += length;
		}
	}
}

static void
real_god_text_model_set_pango_attributes (GodTextModel *text,
					  int start,
					  int end,
					  PangoAttrList *attributes)
{
}

static void
real_god_text_model_paragraph_foreach  (GodTextModel *text,
					GodTextModelParagraphForeachCallback callback,
					gpointer user_data)
{
	guint i;

	if (callback == NULL)
		return;
	if (text->priv->paragraphs) {
		for (i = 0; i < text->priv->paragraphs->len; i++) {
			callback (text, PARAGRAPH(i), user_data);
		}
	}
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

	class->set_paragraph_attributes = real_god_text_model_set_paragraph_attributes;
	class->set_pango_attributes = real_god_text_model_set_pango_attributes;
	class->paragraph_foreach = real_god_text_model_paragraph_foreach;
}

GSF_CLASS (GodTextModel, god_text_model,
	   god_text_model_class_init, god_text_model_init,
	   G_TYPE_OBJECT)
