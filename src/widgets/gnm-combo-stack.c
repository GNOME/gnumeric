/* File import from gal to gnumeric by import-gal.  Do not edit.  */

/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * gnm-combo-stack.c - A combo box for displaying stacks (useful for Undo lists)
 *
 * Copyright (C) 2000 ÉRDI Gergõ <cactus@cactus.rulez.org>
 *
 * Authors:
 *   ÉRDI Gergõ <cactus@cactus.rulez.org>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License, version 2, as published by the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 */

#undef GTK_DISABLE_DEPRECATED
#warning "This file uses GTK_DISABLE_DEPRECATED for GtkList"

#include "gnm-combo-stack.h"
#include <gnm-marshalers.h>
#include <gsf/gsf-impl-utils.h>
#include <gtk/gtkwindow.h>
#include <gtk/gtkcontainer.h>
#include <gtk/gtklist.h>
#include <gtk/gtkwidget.h>
#include <gtk/gtkbutton.h>
#include <gtk/gtkscrolledwindow.h>
#include <gtk/gtkimage.h>

#include <stdio.h>

struct _GnmComboStack {
	GnmComboBox parent;

	GtkWidget *button;
	GtkWidget *list;
	GtkWidget *scrolled_window;

	gint num_items;
	gint curr_item;
};

typedef struct {
	GnmComboBoxClass parent_class;
	void (*pop) (GnmComboStack *cbox, GtkWidget *);
} GnmComboStackClass;

enum {
	POP,
	LAST_SIGNAL
};

static guint gnm_combo_stack_signals [LAST_SIGNAL] = { 0, };

static void
cb_screen_changed (GnmComboStack *cs, GdkScreen *previous_screen)
{
	GtkWidget *w = GTK_WIDGET (cs);
	GdkScreen *screen = gtk_widget_has_screen (w)
		? gtk_widget_get_screen (w)
		: NULL;

	if (screen) {
		GtkWidget *toplevel = gtk_widget_get_toplevel (cs->scrolled_window
		    ? cs->scrolled_window : cs->list);
		gtk_window_set_screen (GTK_WINDOW (toplevel), screen);
	}
}


static void
gnm_combo_stack_class_init (GObjectClass *klass)
{
	gnm_combo_stack_signals [POP] = g_signal_new ( "pop",
		G_TYPE_FROM_CLASS (klass),
		G_SIGNAL_RUN_LAST,
		G_STRUCT_OFFSET (GnmComboStackClass, pop),
		NULL, NULL,
		g_cclosure_marshal_VOID__INT,
		G_TYPE_NONE,
		1, G_TYPE_INT);
}

static void
gnm_combo_stack_init (GnmComboStack *object)
{
	object->num_items = 0;
}

GSF_CLASS (GnmComboStack, gnm_combo_stack,
	   gnm_combo_stack_class_init, gnm_combo_stack_init,
	   GNM_COMBO_BOX_TYPE)

static void
gnm_combo_stack_clear_selection (GnmComboStack *combo)
{
	GList *ptr, *items = gtk_container_get_children (GTK_CONTAINER (combo->list));
	for (ptr = items; ptr; ptr = ptr->next)
		gtk_widget_set_state (GTK_WIDGET (ptr->data), GTK_STATE_NORMAL);
	g_list_free (items);
}

static void
button_cb (GtkWidget *button, gpointer data)
{
	gnm_combo_stack_pop (GNM_COMBO_STACK (data), 1);
}

static gboolean
cb_button_release_event (GtkList *list, GdkEventButton *e, gpointer data)
{
	GnmComboStack *combo = GNM_COMBO_STACK (data);

	gnm_combo_stack_clear_selection (combo);
	gnm_combo_box_popup_hide (GNM_COMBO_BOX (combo));

	if (combo->curr_item > 0) {
		gint dummy, w, h;
		gdk_window_get_geometry (e->window, &dummy, &dummy, &w, &h, &dummy);
		if (0 <= e->x && e->x < w && 0 <= e->y && e->y < h)
			gnm_combo_stack_pop (combo, combo->curr_item);
	}
	gtk_list_end_drag_selection (list);

	return TRUE;
}

static void
list_select_cb (GtkList *list, GtkWidget *child, gpointer data)
{
	GnmComboStack *combo = GNM_COMBO_STACK (data);
	GList *ptr, *items = gtk_container_get_children (GTK_CONTAINER (list));
	guint i = 0;

	gnm_combo_stack_clear_selection (combo);

	for (ptr = items; ptr != NULL ; i++ ) {
		gtk_widget_set_state (GTK_WIDGET (ptr->data),
				      GTK_STATE_SELECTED);
		ptr = (ptr->data != child) ? ptr->next : NULL;
	}
	g_list_free (items);

	combo->curr_item = i;
}

static void
gnm_combo_stack_construct (GnmComboStack *combo,
			   const gchar *stock_name,
			   gboolean const is_scrolled)
{
	GtkWidget *button, *list, *scroll, *display_widget, *pixmap;

	button = combo->button = gtk_button_new ();
	gtk_button_set_relief (GTK_BUTTON (button), GTK_RELIEF_NONE);
	GTK_WIDGET_UNSET_FLAGS (button, GTK_CAN_FOCUS);

	list = combo->list = gtk_list_new ();

	/* Create the button */
	pixmap = gtk_image_new_from_stock (
		stock_name, GTK_ICON_SIZE_LARGE_TOOLBAR);
	gtk_widget_show (pixmap);
	gtk_container_add (GTK_CONTAINER (button), pixmap);
	gtk_widget_set_size_request (GTK_WIDGET (pixmap), 24, 22);

	if (is_scrolled) {
		display_widget = scroll = gtk_scrolled_window_new (NULL, NULL);
		gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW(scroll),
						GTK_POLICY_NEVER,
						GTK_POLICY_AUTOMATIC);

		gtk_scrolled_window_add_with_viewport (
			GTK_SCROLLED_WINDOW(scroll), list);
		gtk_container_set_focus_hadjustment (
			GTK_CONTAINER (list),
			gtk_scrolled_window_get_hadjustment (
				GTK_SCROLLED_WINDOW (scroll)));
		gtk_container_set_focus_vadjustment (
			GTK_CONTAINER (list),
			gtk_scrolled_window_get_vadjustment (
				GTK_SCROLLED_WINDOW (scroll)));
		gtk_widget_set_size_request (scroll, -1, 200); /* MAGIC NUMBER */
	} else
		display_widget = list;

	/* Set up the dropdown list */
	gtk_list_set_selection_mode (GTK_LIST (list), GTK_SELECTION_BROWSE);
	g_signal_connect (G_OBJECT (combo), "screen-changed", G_CALLBACK (cb_screen_changed), NULL);
	g_signal_connect (list, "select-child",
			  G_CALLBACK (list_select_cb),
			  (gpointer) combo);
	g_signal_connect (list, "button_release_event",
			  G_CALLBACK (cb_button_release_event),
			  (gpointer) combo);

	g_signal_connect (button, "clicked",
			  G_CALLBACK (button_cb),
			  (gpointer) combo);

	gtk_widget_show (display_widget);
	gtk_widget_show (button);
	gnm_combo_box_construct (GNM_COMBO_BOX (combo), button, display_widget);
	gtk_widget_set_sensitive (GTK_WIDGET (combo), FALSE);
}

GtkWidget*
gnm_combo_stack_new (const gchar *stock,
		     gboolean const is_scrolled)
{
	GnmComboStack *combo;

	combo = g_object_new (GNM_COMBO_STACK_TYPE, NULL);
	gnm_combo_stack_construct (combo, stock, is_scrolled);

	return GTK_WIDGET (combo);
}

void
gnm_combo_stack_push_item (GnmComboStack *combo,
			   const gchar *item)
{
	GtkWidget *listitem;

	g_return_if_fail (item != NULL);

	combo->num_items++;

	listitem = gtk_list_item_new_with_label (item);
	gtk_widget_show (listitem);

	gtk_list_prepend_items (GTK_LIST (combo->list),
		g_list_prepend (NULL, listitem));
/*	gtk_list_unselect_all (GTK_LIST (combo->list)); */
	gnm_combo_stack_clear_selection (combo);

	/* Kluge: GtkList selects the focused item. ComboStack selects
	 * all items above it. Set focus to the top item so only that
	 * one gets selected. 
	 * http://bugzilla.gnome.org/show_bug.cgi?id=115361 */
	gtk_widget_grab_focus (listitem);
	gtk_widget_set_sensitive (GTK_WIDGET (combo), TRUE);
}

void
gnm_combo_stack_pop (GnmComboStack *combo, gint num)
{
	g_signal_emit_by_name (combo, "pop", num);
}

void
gnm_combo_stack_remove_top (GnmComboStack *combo, gint num)
{
	gint i;
	GList *child, *children;
	GtkWidget *list = combo->list;

	g_return_if_fail (combo->num_items != 0);

	if (num > combo->num_items)
		num = combo->num_items;

	children = child = gtk_container_get_children (GTK_CONTAINER (list));
	for (i = 0; i < num; i++) {
		gtk_container_remove (GTK_CONTAINER (list), child->data);
		child = g_list_next (child);
	}
	g_list_free (children);

	gnm_combo_stack_clear_selection (combo);

	combo->num_items -= num;
	combo->curr_item = -1;
	if (!combo->num_items)
		gtk_widget_set_sensitive (GTK_WIDGET (combo), FALSE);
}

/*
 * Make sure stack is not deeper than @n elements.
 *
 * (Think undo/redo where we don't want to use too much memory.)
 */
void
gnm_combo_stack_truncate (GnmComboStack *combo, int n)
{
	if (combo->num_items > n) {
		combo->num_items = n;

		gtk_list_clear_items (GTK_LIST (combo->list), n, -1);
		if (n == 0)
			gtk_widget_set_sensitive (GTK_WIDGET (combo), FALSE);
	}
}
