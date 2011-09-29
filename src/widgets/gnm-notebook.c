/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/**
 * gnm-notebook.c: Implements a button-only notebook.
 *
 * Copyright (c) 2008 Morten Welinder <terra@gnome.org>
 * Copyright notices for included gtknotebook.c, see below.
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
#include <dead-kittens.h>

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
gnm_notebook_size_allocate (GtkWidget     *widget,
			    GtkAllocation *allocation)
{
	int i, h = 0;
	GnmNotebook *gnb = (GnmNotebook *)widget;
	GtkAllocation alc = *allocation;

	for (i = 0; TRUE; i++) {
		GtkWidget *page = gtk_notebook_get_nth_page (GTK_NOTEBOOK (widget), i);
		GtkAllocation a;
		if (!page)
			break;
		if (!gtk_widget_get_visible (page))
			continue;
		gtk_widget_get_allocation (page, &a);
		h = MAX (h, a.height);
	}

	gnb->dummy_height = h;

	alc.y -= h;
	((GtkWidgetClass *)gnm_notebook_parent_class)->size_allocate
		(widget, &alc);
}

static void
gnm_notebook_adjust_this_tab_appearance (GnmNotebook *nb, GtkWidget *page, gboolean active)
{
	GtkWidget *tab;

	if (page == NULL)
		return;
	
	tab = gtk_notebook_get_tab_label 
		(GTK_NOTEBOOK (nb), page);
	gtk_entry_set_has_frame (GTK_ENTRY (tab), active);
}

static void
gnm_notebook_adjust_tab_appearance (GnmNotebook *nb, GtkWidget *old, GtkWidget *new)
{
	gnm_notebook_adjust_this_tab_appearance (nb, old, FALSE);
	gnm_notebook_adjust_this_tab_appearance (nb, new, TRUE);
}

static void
gnm_notebook_switch_page_cb (GtkNotebook *notebook,
			     GtkWidget   *page,
			     G_GNUC_UNUSED guint page_num,
			     GnmNotebook *nb)
{
	GtkWidget *current_page = NULL;
	gint current = gtk_notebook_get_current_page (notebook);

	if (current != -1)
		current_page = gtk_notebook_get_nth_page (notebook, current);
	gnm_notebook_adjust_tab_appearance (nb, current_page, page);
}

static void
gnm_notebook_class_init (GtkWidgetClass *klass)
{
	gnm_notebook_parent_class = g_type_class_peek (GTK_TYPE_NOTEBOOK);
	klass->size_allocate = gnm_notebook_size_allocate;
}

static void
gnm_notebook_init (GnmNotebook *notebook)
{
	GtkCssProvider *css;
	GtkStyleContext *context;

	gtk_notebook_set_scrollable (GTK_NOTEBOOK (notebook), TRUE);

	context = gtk_widget_get_style_context (GTK_WIDGET (notebook));
	css = gtk_css_provider_new ();
	gtk_css_provider_load_from_data (css,
					 "GnmNotebook {\n"
					 "  padding: 0;\n"
					 "/*  background-color: #dd0000; */\n"
					 "}\n"
					 "GnmNotebook tab {\n"
					 "}",
					 -1, NULL);
	gtk_style_context_add_provider (context,
					GTK_STYLE_PROVIDER (css),
					GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);

	g_signal_connect (G_OBJECT (notebook), "switch-page", 
			  (GCallback) gnm_notebook_switch_page_cb, 
			  notebook);

	g_object_unref (css);
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
		if (gtk_widget_get_visible (child))
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
	gtk_widget_set_visible (dummy, gtk_widget_get_visible (label));
}

void
gnm_notebook_insert_tab (GnmNotebook *nb, GtkWidget *label, int pos)
{
	GtkWidget *dummy_page = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
	gtk_widget_set_size_request (dummy_page, 1, 1);

	g_object_set_data (G_OBJECT (label), DUMMY_KEY, dummy_page);

	g_signal_connect_object (G_OBJECT (label), "destroy",
				 G_CALLBACK (cb_label_destroyed), dummy_page,
				 0);

	cb_label_visibility (label, NULL, dummy_page);
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
