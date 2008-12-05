/**
 * gnm-notebook.h: Implements a button-only notebook.
 *
 * Copyright (c) 2008 Morten Welinder <terra@gnome.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 **/

#include <gnumeric-config.h>
#include "gnm-notebook.h"
#include <gsf/gsf-impl-utils.h>

struct _GnmNotebook {
	GtkNotebook parent;

	/*
	 * This is the number of pixels from a regular notebook that
	 * we are not drawing.  It is caused by the empty widgets
	 * that we have to use.
	 */	   
	int dummy_height;
};

typedef struct {
	GtkNotebookClass parent_class;
} GnmNotebookClass;

static GtkNotebookClass *gnm_notebook_parent_class;

#define DUMMY_KEY "GNM-NOTEBOOK-DUMMY-WIDGET"

static void
gnm_notebook_size_request (GtkWidget      *widget,
			   GtkRequisition *requisition)
{
	((GtkWidgetClass *)gnm_notebook_parent_class)->size_request
		(widget, requisition);
	widget->requisition.height -= widget->style->ythickness;
}

static void
gnm_notebook_size_allocate (GtkWidget     *widget,
			    GtkAllocation *allocation)
{
	int i, h = 0;
	GnmNotebook *gnb = (GnmNotebook *)widget;
	GtkAllocation alc = *allocation;

	for (i = 0; TRUE; i++) {
		GtkWidget *page = gtk_notebook_get_nth_page (GTK_NOTEBOOK (widget), i);
		if (!page)
			break;
		if (!GTK_WIDGET_VISIBLE (page))
			continue;
		h = MAX (h, page->allocation.height);
	}
	h += widget->style->ythickness;

	gnb->dummy_height = h;

	alc.y -= h;
	((GtkWidgetClass *)gnm_notebook_parent_class)->size_allocate
		(widget, &alc);
}

#if 0
static void
dump_region (GdkRegion *reg)
{
	GdkRectangle *rects;
	gint i, n;

	gdk_region_get_rectangles (reg, &rects, &n);
	g_printerr ("Region has %d rectangles.\n", n);
	for (i = 0; i < n; i++)
		g_printerr ("  region %d : (%d,%d) at %dx%d\n",
			    i,
			    rects[i].x, rects[i].y,
			    rects[i].width, rects[i].height);
}
#endif

static gint
gnm_notebook_expose (GtkWidget      *widget,
		     GdkEventExpose *event)
{
	GnmNotebook *gnb = (GnmNotebook *)widget;
	GdkEvent *ev = gdk_event_copy ((GdkEvent *)event);
	GdkEventExpose *eve = (GdkEventExpose *)ev;
	GtkAllocation alc = widget->allocation;
	int res = FALSE;

	alc.y += gnb->dummy_height;
	alc.height -= gnb->dummy_height;
	if (gdk_rectangle_intersect (&alc, &eve->area, &eve->area)) {
		GdkRegion *reg = gdk_region_rectangle (&eve->area);
		gdk_region_intersect (reg, eve->region);
		gdk_region_destroy (eve->region);
		eve->region = reg;

		gdk_window_begin_paint_region (eve->window, reg);
		res = ((GtkWidgetClass *)gnm_notebook_parent_class)->expose_event (widget, eve);
		gdk_window_end_paint (eve->window);
	}

	gdk_event_free (ev);

	return res;
}

static void
gnm_notebook_class_init (GtkObjectClass *klass)
{
	GtkWidgetClass *widget_class = (GtkWidgetClass *)klass;

	gnm_notebook_parent_class = g_type_class_peek (GTK_TYPE_NOTEBOOK);
	widget_class->size_request = gnm_notebook_size_request;
	widget_class->size_allocate = gnm_notebook_size_allocate;
	widget_class->expose_event = gnm_notebook_expose;

	gtk_rc_parse_string ("style \"gnm-notebook-default-style\" {\n"
			     "  ythickness = 0\n"
			     "}\n"
			     "class \"GnmNotebook\" style \"gnm-notebook-default-style\"\n"
		);
}

static void
gnm_notebook_init (G_GNUC_UNUSED GnmNotebook *notebook)
{
}

GSF_CLASS (GnmNotebook, gnm_notebook,
	   gnm_notebook_class_init, gnm_notebook_init, GTK_TYPE_NOTEBOOK)

int
gnm_notebook_get_n_visible (GnmNotebook *nb)
{
	int count = 0;
	GList *l, *children = gtk_container_get_children (GTK_CONTAINER (nb));

	for (l = children; l; l = l->next) {
		GtkWidget *child = l->data;
		if (GTK_WIDGET_VISIBLE (child))
			count++;
	}

	g_list_free (children);

	return count;
}

GtkWidget *
gnm_notebook_get_nth_label (GnmNotebook *nb, int n)
{
	GtkWidget *page;

	g_return_val_if_fail (IS_GNM_NOTEBOOK (nb), NULL);

	page = gtk_notebook_get_nth_page (GTK_NOTEBOOK (nb), n);
	if (!page)
		return NULL;

	return gtk_notebook_get_tab_label (GTK_NOTEBOOK (nb), page);
}

static void
cb_label_destroyed (G_GNUC_UNUSED GtkWidget *label, GtkWidget *dummy)
{
	gtk_widget_destroy (dummy);
}

static void
cb_label_visibility (GtkWidget *label,
		     G_GNUC_UNUSED GParamSpec *pspec,
		     GtkWidget *dummy)
{
	g_object_set (GTK_OBJECT (dummy),
		      "visible", GTK_WIDGET_VISIBLE (label),
		      NULL);
}

void
gnm_notebook_insert_tab (GnmNotebook *nb, GtkWidget *label, int pos)
{
	GtkWidget *dummy_page = gtk_hbox_new (FALSE, 0);
	gtk_widget_set_size_request (dummy_page, 1, 1);
	gtk_widget_show (dummy_page);
	g_object_set_data (G_OBJECT (label), DUMMY_KEY, dummy_page);

	g_signal_connect_object (G_OBJECT (label), "destroy",
				 G_CALLBACK (cb_label_destroyed), dummy_page,
				 0);
	g_signal_connect_object (G_OBJECT (label), "notify::visible",
				 G_CALLBACK (cb_label_visibility), dummy_page,
				 0);

	gtk_notebook_insert_page (GTK_NOTEBOOK (nb), dummy_page, label, pos);
}

void
gnm_notebook_move_tab (GnmNotebook *nb, GtkWidget *label, int newpos)
{
	GtkWidget *child = g_object_get_data (G_OBJECT (label), DUMMY_KEY);
	gtk_notebook_reorder_child (GTK_NOTEBOOK (nb), child, newpos);
}

void
gnm_notebook_set_tab_visible (GnmNotebook *nb, int page, gboolean viz)
{
	GtkWidget *dummy;
	g_return_if_fail (IS_GNM_NOTEBOOK (nb));

	dummy = gtk_notebook_get_nth_page (GTK_NOTEBOOK (nb), page);
	if (!dummy)
		return;

	if (viz)
		gtk_widget_show (dummy);
	else
		gtk_widget_hide (dummy);
}

void
gnm_notebook_set_current_page (GnmNotebook *nb, int page)
{
	gtk_notebook_set_current_page (GTK_NOTEBOOK (nb), page);
}

void
gnm_notebook_prev_page (GnmNotebook *nb)
{
	gtk_notebook_prev_page (GTK_NOTEBOOK (nb));
}

void
gnm_notebook_next_page (GnmNotebook *nb)
{
	gtk_notebook_next_page (GTK_NOTEBOOK (nb));
}
