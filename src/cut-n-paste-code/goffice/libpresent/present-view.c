/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- *//* vim: set sw=8: */

/*
 * present-view.c: MS Office Graphic Object support
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

#include <gnumeric-config.h>
#include "libpresent/present-view.h"
#include <gsf/gsf-impl-utils.h>
#include <string.h>
#include <drawing/god-drawing-view.h>

static GObjectClass *parent_class;

struct PresentViewPrivate_ {
	PresentPresentation *presentation;
	GodDrawingView *view;
	int page;
};

static void
update_to_page (PresentView *view,
		int page)
{
	PresentSlide *slide;
	GodDrawing *drawing;

	g_print ("Displaying page %d\n", page);
	if (!view->priv->presentation || present_presentation_get_slide_count (view->priv->presentation) == 0) {
		god_drawing_view_set_drawing (view->priv->view, NULL);
		return;
	}

	if (page < 0 || page >= present_presentation_get_slide_count(view->priv->presentation))
		return;

	view->priv->page = page;
	slide = present_presentation_get_slide (view->priv->presentation, view->priv->page);
	drawing = present_slide_get_drawing (slide);
	if (drawing) {
		god_drawing_view_set_drawing (view->priv->view, drawing);
		g_object_unref (drawing);
	}
	g_object_unref (slide);
}

PresentView *
present_view_new (PresentPresentation *presentation)
{
	PresentView *view;

	view = g_object_new (PRESENT_VIEW_TYPE, NULL);

	present_view_set_presentation (view, presentation);

	return view;
}

PresentPresentation *
present_view_get_presentation (PresentView  *view)
{
	if (view->priv->presentation)
		g_object_ref (view->priv->presentation);
	return view->priv->presentation;
}

void
present_view_set_presentation (PresentView *view,
			       PresentPresentation   *presentation)
{
	if (view->priv->presentation)
		g_object_unref (view->priv->presentation);
	view->priv->presentation = presentation;
	if (view->priv->presentation)
		g_object_ref (view->priv->presentation);

	if (presentation) {
		GodAnchor *extents;

		extents = present_presentation_get_extents (view->priv->presentation);
		god_drawing_view_set_extents (view->priv->view, extents);
		g_object_unref (extents);
	}
	update_to_page (view, 0);
}

static void
present_view_init (GObject *object)
{
	PresentView *view = PRESENT_VIEW (object);
	view->priv = g_new0 (PresentViewPrivate, 1);
	view->priv->view = god_drawing_view_new();

	gtk_container_add (GTK_CONTAINER (view), GTK_WIDGET (view->priv->view));

	gtk_widget_show (GTK_WIDGET (view->priv->view));
}

static void
present_view_dispose (GObject *object)
{
	PresentView *view = PRESENT_VIEW (object);

	if (view->priv == NULL)
		return;

	g_object_unref (view->priv->presentation);
	g_free (view->priv);
	view->priv = NULL;

	G_OBJECT_CLASS (parent_class)->dispose (object);
}

static gboolean
present_view_button_press_event (GtkWidget	     *widget,
				 GdkEventButton      *event)
{
	PresentView *view = PRESENT_VIEW (widget);

	if (event->button == 1)
		update_to_page (view, view->priv->page + 1);
	else if (event->button == 3)
		update_to_page (view, view->priv->page - 1);
	else
		return FALSE;

	return TRUE;
}

static void
present_view_class_init (PresentViewClass *class)
{
	GObjectClass *object_class;
	GtkWidgetClass *widget_class;

	object_class                     = (GObjectClass *) class;
	widget_class                     = (GtkWidgetClass *) class;

	parent_class                     = g_type_class_peek_parent (class);

	object_class->dispose            = present_view_dispose;
	widget_class->button_press_event = present_view_button_press_event;
}

GSF_CLASS (PresentView, present_view,
	   present_view_class_init, present_view_init,
	   GTK_TYPE_EVENT_BOX)
