/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/*
 * gnm-cell-combo-foo-view.c: A foocanvas object for an in-cell combo-box
 *
 * Copyright (C) 2006-2007 Jody Goldberg (jody@gnome.org)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301
 * USA
 */

#include <gnumeric-config.h>
#include "gnm-cell-combo-foo-view.h"
#include "gnm-cell-combo-foo-view-impl.h"

#include "wbc-gtk.h"
#include "sheet.h"
#include "sheet-control-gui.h"
#include "gnm-pane-impl.h"
#include "sheet-object-impl.h"

#include <goffice/cut-n-paste/foocanvas/foo-canvas-widget.h>
#include <gtk/gtk.h>
#include <gdk/gdkevents.h>
#include <gdk/gdkkeysyms.h>

#define	SOV_ID		"sov"
#define	AUTOSCROLL_ID	"autoscroll-id"
#define	AUTOSCROLL_DIR	"autoscroll-dir"

static void ccombo_popup_destroy (GtkWidget *popup, GtkWidget *list);

static GtkWidget *
ccombo_create_arrow (GnmCComboFooView *ccombo, SheetObject *so)
{
	GnmCComboFooViewIface *iface = GNM_CCOMBO_FOO_VIEW_GET_CLASS (ccombo);
	return (iface->create_arrow) (so);
}

static void
ccombo_activate (GtkWidget *popup, GtkTreeView *list)
{
	SheetObjectView		*sov    = g_object_get_data (G_OBJECT (list), SOV_ID);
	FooCanvasItem		*view   = FOO_CANVAS_ITEM (sov);
	GnmPane			*pane   = GNM_PANE (view->canvas);
	GnmCComboFooViewIface	*iface  = GNM_CCOMBO_FOO_VIEW_GET_CLASS (sov);

	(iface->activate) (sheet_object_view_get_so (sov), popup, list,
			   scg_wbcg (pane->simple.scg));
	ccombo_popup_destroy (popup, GTK_WIDGET (list));
}

static GtkListStore *
ccombo_fill_model (GnmCComboFooView *ccombo,
		   SheetObject *so, GtkTreePath **clip, GtkTreePath **select)
{
	GnmCComboFooViewIface *iface = GNM_CCOMBO_FOO_VIEW_GET_CLASS (ccombo);
	return (iface->fill_model) (so, clip, select);
}

/****************************************************************************/

/* Cut and paste from gtkwindow.c */
static void
ccombo_focus_change (GtkWidget *widget, gboolean in)
{
	GdkEventFocus fevent;

	g_object_ref (widget);

	if (in)
		GTK_WIDGET_SET_FLAGS (widget, GTK_HAS_FOCUS);
	else
		GTK_WIDGET_UNSET_FLAGS (widget, GTK_HAS_FOCUS);

	fevent.type = GDK_FOCUS_CHANGE;
	fevent.window = widget->window;
	fevent.in = in;

	gtk_widget_event (widget, (GdkEvent *)&fevent);

	g_object_notify (G_OBJECT (widget), "has-focus");

	g_object_unref (widget);
}

static gint
cb_ccombo_autoscroll (GtkTreeView *list)
{
	gboolean ok;
	GtkTreePath *path = NULL;
	gpointer dir = g_object_get_data (G_OBJECT (list), AUTOSCROLL_DIR);

	gtk_tree_view_get_cursor (list, &path, NULL);
	if (GPOINTER_TO_INT (dir) > 0) {
		GtkTreeIter iter;
		/* why does _next not return a boolean ? list _prev */
		gtk_tree_path_next (path);
		ok = gtk_tree_model_get_iter (gtk_tree_view_get_model (list),
					      &iter, path);
	} else
		ok = gtk_tree_path_prev (path);

	if (ok) {
		gtk_tree_selection_select_path (gtk_tree_view_get_selection (list), path);
		gtk_tree_view_set_cursor (list, path, NULL, FALSE);
	}
	gtk_tree_path_free (path);
	return ok;
}

static void
ccombo_autoscroll_set (GObject *list, int dir)
{
	gpointer id = g_object_get_data (list, AUTOSCROLL_ID);
	if (id == NULL) {
		if (dir != 0) {
			int timer_id = g_timeout_add (50,
				(GSourceFunc)cb_ccombo_autoscroll, list);
			g_object_set_data (list, AUTOSCROLL_ID,
				GINT_TO_POINTER (timer_id));
		}
	} else if (dir == 0) {
		g_source_remove (GPOINTER_TO_INT (id));
		g_object_set_data (list, AUTOSCROLL_ID, NULL);
	}
	g_object_set_data (list, AUTOSCROLL_DIR, GINT_TO_POINTER (dir));
}

static void
ccombo_popup_destroy (GtkWidget *popup, GtkWidget *list)
{
	ccombo_autoscroll_set (G_OBJECT (list), 0);
	ccombo_focus_change (list, FALSE);
	gtk_widget_destroy (popup);
}

static gint
cb_ccombo_key_press (GtkWidget *popup, GdkEventKey *event, GtkWidget *list)
{
	switch (event->keyval) {
	case GDK_Escape :
		ccombo_popup_destroy (popup, list);
		return TRUE;

	case GDK_KP_Down :
	case GDK_Down :
	case GDK_KP_Up :
	case GDK_Up :
		if (!(event->state & GDK_MOD1_MASK))
			return FALSE;

	case GDK_KP_Enter :
	case GDK_Return :
		ccombo_activate (popup, GTK_TREE_VIEW (list));
		return TRUE;
	default :
		;
	}
	return FALSE;
}

static gboolean
cb_ccombo_popup_motion (GtkWidget *widget, GdkEventMotion *event,
			GtkTreeView *list)
{
	int base, dir = 0;

	gdk_window_get_origin (GTK_WIDGET (list)->window, NULL, &base);
	if (event->y_root < base)
		dir = -1;
	else if (event->y_root >= (base + GTK_WIDGET(list)->allocation.height))
		dir = 1;
	else
		dir = 0;
	ccombo_autoscroll_set (G_OBJECT (list), dir);
	return FALSE;
}

static gboolean
cb_ccombo_list_motion (GtkWidget *widget, GdkEventMotion *event,
		       GtkTreeView *list)
{
	GtkTreePath *path;
	if (event->x >= 0 && event->y >= 0 &&
	    event->x < widget->allocation.width &&
	    event->y < widget->allocation.height &&
	    gtk_tree_view_get_path_at_pos (list, event->x, event->y,
					   &path, NULL, NULL, NULL)) {
		gtk_tree_selection_select_path (gtk_tree_view_get_selection (list), path);
		gtk_tree_view_set_cursor (list, path, NULL, FALSE);
		gtk_tree_path_free (path);
	}
	ccombo_autoscroll_set (G_OBJECT (list), 0);
	return FALSE;
}

static gint
cb_ccombo_list_button_press (GtkWidget *list,
			     G_GNUC_UNUSED GdkEventButton *event,
			     GtkWidget *popup)
{
	if (event->button == 1)
		g_signal_connect (popup, "motion_notify_event",
			G_CALLBACK (cb_ccombo_popup_motion), list);
	return FALSE;
}

static gint
cb_ccombo_button_press (GtkWidget *popup, GdkEventButton *event,
			GtkWidget *list)
{
	/* btn1 down outside the popup cancels */
	if (event->button == 1 && event->window != popup->window) {
		ccombo_popup_destroy (popup, list);
		return TRUE;
	}
	return FALSE;
}

static gint
cb_ccombo_button_release (GtkWidget *popup, GdkEventButton *event,
			  GtkTreeView *list)
{
	if (event->button == 1) {
	    if (gtk_get_event_widget ((GdkEvent *) event) == GTK_WIDGET (list)) {
		    ccombo_activate (popup, list);
		    return TRUE;
	    }
	    g_signal_handlers_disconnect_by_func (popup,
			G_CALLBACK (cb_ccombo_popup_motion), list);
	    ccombo_autoscroll_set (G_OBJECT (list), 0);
	}
	return FALSE;
}

static void
cb_ccombo_button_pressed (G_GNUC_UNUSED GtkButton *button,
			  SheetObjectView *sov)
{
	gnm_cell_combo_foo_view_popdown (sov, GDK_CURRENT_TIME);
}

/**
 * gnm_cell_combo_foo_view_popdown:
 * @sov : #SheetObjectView
 * @activate_time : event time
 *
 * Open the popup window associated with @sov
 **/
void
gnm_cell_combo_foo_view_popdown (SheetObjectView *sov, guint32 activate_time)
{
	FooCanvasItem	   *view   = FOO_CANVAS_ITEM (sov);
	GnmPane		   *pane   = GNM_PANE (view->canvas);
	SheetControlGUI	   *scg    = pane->simple.scg;
	SheetObject	   *so     = sheet_object_view_get_so (sov);
	Sheet const	   *sheet  = sheet_object_get_sheet (so);
	GtkWidget *frame,  *popup, *list, *container;
	int root_x, root_y;
	GtkListStore  *model;
	GtkTreeViewColumn *column;
	GtkTreePath	  *clip = NULL, *select = NULL;
	GtkRequisition	req;
	GtkWindow *toplevel = wbcg_toplevel (scg_wbcg (scg));

	popup = gtk_window_new (GTK_WINDOW_POPUP);
	gtk_window_set_type_hint (GTK_WINDOW (popup), GDK_WINDOW_TYPE_HINT_COMBO);
	gtk_window_group_add_window (gtk_window_get_group (toplevel), GTK_WINDOW (popup));
	go_gtk_window_set_transient (toplevel, GTK_WINDOW (popup));
	gtk_window_set_resizable (GTK_WINDOW (popup), FALSE);
	gtk_window_set_decorated (GTK_WINDOW (popup), FALSE);
	gtk_window_set_screen (GTK_WINDOW (popup),
		gtk_widget_get_screen (GTK_WIDGET (toplevel)));

	model = ccombo_fill_model (GNM_CCOMBO_FOO_VIEW (sov), so, &clip, &select);
	column = gtk_tree_view_column_new_with_attributes ("ID",
		gtk_cell_renderer_text_new (), "text", 0,
		NULL);
	list = gtk_tree_view_new_with_model (GTK_TREE_MODEL (model));
	g_object_unref (model);
	gtk_tree_view_set_headers_visible (GTK_TREE_VIEW (list), FALSE);
	gtk_tree_view_append_column (GTK_TREE_VIEW (list), column);
	gtk_widget_size_request (GTK_WIDGET (list), &req);
	g_object_set_data (G_OBJECT (list), SOV_ID, sov);

	frame = gtk_frame_new (NULL);
	gtk_frame_set_shadow_type (GTK_FRAME (frame), GTK_SHADOW_OUT);

#if 0
	range_dump (&so->anchor.cell_bound, "");
	g_printerr (" : so = %p, view = %p\n", so, view);
#endif
	if (clip != NULL) {
		GdkRectangle  rect;
		GtkWidget *sw = gtk_scrolled_window_new (
			gtk_tree_view_get_hadjustment (GTK_TREE_VIEW (list)),
			gtk_tree_view_get_vadjustment (GTK_TREE_VIEW (list)));
		gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (sw),
						GTK_POLICY_AUTOMATIC,
						GTK_POLICY_ALWAYS);
		gtk_tree_view_get_background_area (GTK_TREE_VIEW (list),
						   clip, NULL, &rect);
		gtk_tree_path_free (clip);

		gtk_widget_set_size_request (list, req.width, rect.y);
		gtk_container_add (GTK_CONTAINER (sw), list);
		container = sw;
	} else
		container = list;

	gtk_container_add (GTK_CONTAINER (frame), container);

	/* do the popup */
	gdk_window_get_origin (GTK_WIDGET (pane)->window,
		&root_x, &root_y);
	if (sheet->text_is_rtl) {
		root_x += GTK_WIDGET (pane)->allocation.width;
		root_x -= scg_colrow_distance_get (scg, TRUE,
			pane->first.col,
			so->anchor.cell_bound.start.col + 1);
	} else
		root_x += scg_colrow_distance_get (scg, TRUE,
			pane->first.col,
			so->anchor.cell_bound.start.col);
	gtk_window_move (GTK_WINDOW (popup), root_x,
		root_y + scg_colrow_distance_get (scg, FALSE,
			pane->first.row,
			so->anchor.cell_bound.start.row + 1));

	gtk_container_add (GTK_CONTAINER (popup), frame);

	g_signal_connect (popup, "key_press_event",
		G_CALLBACK (cb_ccombo_key_press), list);
	g_signal_connect (popup, "button_press_event",
		G_CALLBACK (cb_ccombo_button_press), list);
	g_signal_connect (popup, "button_release_event",
		G_CALLBACK (cb_ccombo_button_release), list);
	g_signal_connect (list, "motion_notify_event",
		G_CALLBACK (cb_ccombo_list_motion), list);
	g_signal_connect (list, "button_press_event",
		G_CALLBACK (cb_ccombo_list_button_press), popup);

	gtk_widget_show_all (popup);

	/* after we show the window setup the selection (showing the list
	 * clears the selection) */
	if (select != NULL) {
		gtk_tree_selection_select_path (
			gtk_tree_view_get_selection (GTK_TREE_VIEW (list)),
			select);
		gtk_tree_view_set_cursor (GTK_TREE_VIEW (list),
			select, NULL, FALSE);
		gtk_tree_path_free (select);
	}

	gtk_widget_grab_focus (popup);
	gtk_widget_grab_focus (GTK_WIDGET (list));
	ccombo_focus_change (GTK_WIDGET (list), TRUE);

	if (0 == gdk_pointer_grab (popup->window, TRUE,
		GDK_BUTTON_PRESS_MASK |
		GDK_BUTTON_RELEASE_MASK |
		GDK_POINTER_MOTION_MASK,
		NULL, NULL, activate_time)) {
		if (0 ==  gdk_keyboard_grab (popup->window, TRUE, activate_time)) {
			gtk_grab_add (popup);
		} else {
			gdk_display_pointer_ungrab (
				gdk_drawable_get_display (popup->window), activate_time);
		}
	}
}

/**
 * gnm_cell_combo_foo_view_new:
 * @so : #SheetObject
 * @type : #GType
 * @container : SheetObjectViewContainer (a GnmPane)
 *
 * Create and register an in cell combo to pick from an autofilter list.
 **/
SheetObjectView *
gnm_cell_combo_foo_view_new (SheetObject *so, GType type,
			     SheetObjectViewContainer *container)
{
	GnmPane *pane = GNM_PANE (container);
	GtkWidget *view_widget = gtk_button_new ();
	FooCanvasItem *ccombo = foo_canvas_item_new (pane->object_views, type,
		"widget",	view_widget,
		"size_pixels",	FALSE,
		NULL);
	GTK_WIDGET_UNSET_FLAGS (view_widget, GTK_CAN_FOCUS);

	gtk_container_add (GTK_CONTAINER (view_widget),
		ccombo_create_arrow (GNM_CCOMBO_FOO_VIEW (ccombo), so));
	g_signal_connect (view_widget, "pressed",
		G_CALLBACK (cb_ccombo_button_pressed), ccombo);
	gtk_widget_show_all (view_widget);

	return gnm_pane_object_register (so, ccombo, FALSE);
}

GType
gnm_ccombo_foo_view_get_type (void)
{
	static GType type = 0;

	if (!type) {
		static GTypeInfo const type_info = {
			sizeof (GnmCComboFooViewIface),	/* class_size */
			NULL,				/* base_init */
			NULL,				/* base_finalize */
		};

		type = g_type_register_static (G_TYPE_INTERFACE,
			"GnmCComboFooView", &type_info, 0);
	}

	return type;
}
