/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- *//* vim: set sw=8: */

/*
 * present-slide.c: MS Office Graphic Object support
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
#include "libpresent/present-slide.h"
#include <gsf/gsf-impl-utils.h>
#include <string.h>

static GObjectClass *parent_class;

struct PresentSlidePrivate_ {
	GPtrArray *texts; /* Of type PresentText. */
	GodDrawing *drawing;
};

static GPtrArray*
g_ptr_array_insert_val (GPtrArray        *array,
			guint             index,
			gpointer          data) 
{
	g_ptr_array_add (array, data);
	memmove (array->pdata + index + 1,
		 array->pdata + index,
		 array->len - index - 1);
	g_ptr_array_index (array, index) = data;
	return array;
}

PresentSlide *
present_slide_new (void)
{
	PresentSlide *slide;

	slide = g_object_new (PRESENT_SLIDE_TYPE, NULL);

	return slide;
}

void
present_slide_append_text   (PresentSlide  *slide,
			     PresentText         *text)
{
	present_slide_insert_text (slide, text, -1);
}

/* pos can be -1 to represent the end. */
void
present_slide_insert_text   (PresentSlide  *slide,
			     PresentText         *text,
			     int                   pos)
{
	g_return_if_fail (slide != NULL);
	g_return_if_fail (text != NULL);

	if (pos == -1)
		pos = slide->priv->texts->len;

	g_ptr_array_insert_val (slide->priv->texts, pos, text);
	g_object_ref (text);
}

/* pos must be an actual position */
void
present_slide_delete_text   (PresentSlide  *slide,
			     int                   pos)
{
	PresentText *text = g_ptr_array_remove_index (slide->priv->texts, pos);
	g_object_unref (text);
}

/* old_pos must be an actual position.  new_pos can be -1 to represent the end. */
void
present_slide_reorder_text  (PresentSlide  *slide,
			     int                   old_pos,
			     int                   new_pos)
{
	PresentText *text = g_ptr_array_remove_index (slide->priv->texts, old_pos);
	present_slide_insert_text (slide, text, new_pos);
	g_object_unref (text);
}

int
present_slide_get_text_count  (PresentSlide       *slide)
{
	return slide->priv->texts->len;
}

PresentText *
present_slide_get_text (PresentSlide  *slide,
			int            pos)
{
	PresentText *text;

	g_return_val_if_fail (pos < present_slide_get_text_count (slide), NULL);

	text = g_ptr_array_index (slide->priv->texts, pos);

	g_return_val_if_fail (text, NULL);

	g_object_ref (text);
	return text;
}

GodDrawing *
present_slide_get_drawing (PresentSlide  *slide)
{
	if (slide->priv->drawing)
		g_object_ref (slide->priv->drawing);
	return slide->priv->drawing;
}

void
present_slide_set_drawing (PresentSlide *slide,
			   GodDrawing   *drawing)
{
	if (slide->priv->drawing)
		g_object_unref (slide->priv->drawing);
	slide->priv->drawing = drawing;
	if (slide->priv->drawing)
		g_object_ref (slide->priv->drawing);
}

static void
present_slide_init (GObject *object)
{
	PresentSlide *slide = PRESENT_SLIDE (object);
	slide->priv = g_new0 (PresentSlidePrivate, 1);
	slide->priv->texts = g_ptr_array_new ();
}

static void
present_slide_dispose (GObject *object)
{
	PresentSlide *slide = PRESENT_SLIDE (object);
	guint i;

	if (slide->priv == NULL)
		return;

	for (i = 0; i < slide->priv->texts->len; i++)
		g_object_unref (g_ptr_array_index (slide->priv->texts, i));
	g_ptr_array_free (slide->priv->texts, TRUE);
	g_object_unref (slide->priv->drawing);
	g_free (slide->priv);
	slide->priv = NULL;

	G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
present_slide_class_init (PresentSlideClass *class)
{
	GObjectClass *object_class;

	object_class          = (GObjectClass *) class;

	parent_class          = g_type_class_peek_parent (class);

	object_class->dispose = present_slide_dispose;
}

GSF_CLASS (PresentSlide, present_slide,
	   present_slide_class_init, present_slide_init,
	   G_TYPE_OBJECT)
