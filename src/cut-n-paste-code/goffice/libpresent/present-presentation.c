/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- *//* vim: set sw=8: */

/*
 * present-presentation.c: MS Office Graphic Object support
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
#include <goffice/libpresent/present-presentation.h>
#include <gsf/gsf-impl-utils.h>
#include <string.h>

static GObjectClass *parent_class;

struct PresentPresentationPrivate_ {
	GPtrArray *slides; /* Of type PresentSlide. */
	GodDrawingGroup *drawing_group;
	GodAnchor *extents;
	GodAnchor *notes_extents;
	GPtrArray *default_attributes;
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

PresentPresentation *
present_presentation_new (void)
{
	PresentPresentation *presentation;

	presentation = g_object_new (PRESENT_PRESENTATION_TYPE, NULL);

	return presentation;
}

void
present_presentation_append_slide   (PresentPresentation  *presentation,
				     PresentSlide         *slide)
{
	present_presentation_insert_slide (presentation, slide, -1);
}

/* pos can be -1 to represent the end. */
void
present_presentation_insert_slide   (PresentPresentation  *presentation,
				     PresentSlide         *slide,
				     int                   pos)
{
	g_return_if_fail (presentation != NULL);
	g_return_if_fail (slide != NULL);

	if (pos == -1)
		pos = presentation->priv->slides->len;

	g_ptr_array_insert_val (presentation->priv->slides, pos, slide);
	g_object_ref (slide);
}

/* pos must be an actual position */
void
present_presentation_delete_slide   (PresentPresentation  *presentation,
				     int                   pos)
{
	PresentSlide *slide = g_ptr_array_remove_index (presentation->priv->slides, pos);
	g_object_unref (slide);
}

/* old_pos must be an actual position.  new_pos can be -1 to represent the end. */
void
present_presentation_reorder_slide  (PresentPresentation  *presentation,
				     int                   old_pos,
				     int                   new_pos)
{
	PresentSlide *slide = g_ptr_array_remove_index (presentation->priv->slides, old_pos);
	present_presentation_insert_slide (presentation, slide, new_pos);
	g_object_unref (slide);
}

int
present_presentation_get_slide_count  (PresentPresentation       *presentation)
{
	return presentation->priv->slides->len;
}

PresentSlide *
present_presentation_get_slide (PresentPresentation  *presentation,
				int                   pos)
{
	PresentSlide *slide;

	g_return_val_if_fail (pos < present_presentation_get_slide_count (presentation), NULL);

	slide = g_ptr_array_index (presentation->priv->slides, pos);

	g_return_val_if_fail (slide, NULL);

	g_object_ref (slide);
	return slide;
}

GodDrawingGroup *
present_presentation_get_drawing_group (PresentPresentation  *presentation)
{
	if (presentation->priv->drawing_group)
		g_object_ref (presentation->priv->drawing_group);
	return presentation->priv->drawing_group;
}

void
present_presentation_set_drawing_group (PresentPresentation  *presentation,
					GodDrawingGroup *drawing_group)
{
	if (presentation->priv->drawing_group)
		g_object_unref (presentation->priv->drawing_group);
	presentation->priv->drawing_group = drawing_group;
	if (presentation->priv->drawing_group)
		g_object_ref (presentation->priv->drawing_group);
}

GodAnchor *
present_presentation_get_extents (PresentPresentation  *presentation)
{
	if (presentation->priv->extents)
		g_object_ref (presentation->priv->extents);
	return presentation->priv->extents;
}

void
present_presentation_set_extents (PresentPresentation  *presentation,
					GodAnchor *extents)
{
	if (presentation->priv->extents)
		g_object_unref (presentation->priv->extents);
	presentation->priv->extents = extents;
	if (presentation->priv->extents)
		g_object_ref (presentation->priv->extents);
}

GodAnchor *
present_presentation_get_notes_extents (PresentPresentation  *presentation)
{
	if (presentation->priv->notes_extents)
		g_object_ref (presentation->priv->notes_extents);
	return presentation->priv->notes_extents;
}

void
present_presentation_set_notes_extents (PresentPresentation  *presentation,
					GodAnchor *notes_extents)
{
	if (presentation->priv->notes_extents)
		g_object_unref (presentation->priv->notes_extents);
	presentation->priv->notes_extents = notes_extents;
	if (presentation->priv->notes_extents)
		g_object_ref (presentation->priv->notes_extents);
}

void
present_presentation_set_default_attributes_for_text_type  (PresentPresentation *presentation,
							    guint text_type,
							    GodDefaultAttributes *default_attributes)
{
	GodDefaultAttributes **default_attributes_location;
	if (presentation->priv->default_attributes == NULL)
		presentation->priv->default_attributes = g_ptr_array_new();
	if (presentation->priv->default_attributes->len <= text_type)
		g_ptr_array_set_size (presentation->priv->default_attributes, text_type + 1);

	default_attributes_location = (GodDefaultAttributes **) &g_ptr_array_index (presentation->priv->default_attributes, text_type);
	if (*default_attributes_location)
		g_object_unref (*default_attributes_location);
	*default_attributes_location = default_attributes;
	if (*default_attributes_location)
		g_object_ref (*default_attributes_location);
}

const GodDefaultAttributes *
present_presentation_get_default_attributes_for_text_type  (PresentPresentation *presentation,
							    guint text_type)
{
	if (presentation->priv->default_attributes == NULL)
		return NULL;
	if (presentation->priv->default_attributes->len <= text_type)
		return NULL;
	return g_ptr_array_index (presentation->priv->default_attributes, text_type);
}

static void
present_presentation_init (GObject *object)
{
	PresentPresentation *presentation = PRESENT_PRESENTATION (object);
	presentation->priv = g_new0 (PresentPresentationPrivate, 1);
	presentation->priv->slides = g_ptr_array_new ();
}

static void
present_presentation_dispose (GObject *object)
{
	PresentPresentation *presentation = PRESENT_PRESENTATION (object);
	guint i;

	if (presentation->priv == NULL)
		return;

	for (i = 0; i < presentation->priv->slides->len; i++)
		g_object_unref (g_ptr_array_index (presentation->priv->slides, i));
	g_ptr_array_free (presentation->priv->slides, TRUE);
	if (presentation->priv->drawing_group)
		g_object_unref (presentation->priv->drawing_group);
	if (presentation->priv->extents)
		g_object_unref (presentation->priv->extents);
	if (presentation->priv->notes_extents)
		g_object_unref (presentation->priv->notes_extents);
	g_free (presentation->priv);
	presentation->priv = NULL;

	G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
present_presentation_class_init (PresentPresentationClass *class)
{
	GObjectClass *object_class;

	object_class          = (GObjectClass *) class;

	parent_class          = g_type_class_peek_parent (class);

	object_class->dispose = present_presentation_dispose;
}

GSF_CLASS (PresentPresentation, present_presentation,
	   present_presentation_class_init, present_presentation_init,
	   G_TYPE_OBJECT)
