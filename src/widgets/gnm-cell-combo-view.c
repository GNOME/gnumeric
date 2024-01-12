/*
 * gnm-cell-combo-view.c: A canvas object for an in-cell combo-box
 *
 * Copyright (C) 2006-2007 Jody Goldberg (jody@gnome.org)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) version 3.
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
#include <widgets/gnm-cell-combo-view.h>
#include <widgets/gnm-cell-combo-view-impl.h>

#include <wbc-gtk.h>
#include <sheet.h>
#include <sheet-control-gui.h>
#include <sheet-merge.h>
#include <gnm-pane-impl.h>
#include <ranges.h>

#include <goffice/goffice.h>
#include <gdk/gdk.h>
#include <gdk/gdkkeysyms.h>
#include <gsf/gsf-impl-utils.h>

#define	SOV_ID		"sov"
#define	AUTOSCROLL_ID	"autoscroll-id"
#define	AUTOSCROLL_DIR	"autoscroll-dir"

static void ccombo_popup_destroy (GtkWidget *list);

static GtkWidget *
ccombo_create_arrow (GnmCComboView *ccombo, SheetObject *so)
{
	GnmCComboViewClass *klass = GNM_CCOMBO_VIEW_GET_CLASS (ccombo);
	return (klass->create_arrow) (so);
}

static gboolean
ccombo_activate (GtkTreeView *list, gboolean button)
{
	SheetObjectView		*sov    = g_object_get_data (G_OBJECT (list), SOV_ID);
	GocItem			*view   = GOC_ITEM (sov);
	GnmPane			*pane   = GNM_PANE (view->canvas);
	GnmCComboViewClass	*klass  = GNM_CCOMBO_VIEW_GET_CLASS (sov);

	if ((klass->activate) (sheet_object_view_get_so (sov), list,
			       scg_wbcg (pane->simple.scg), button))
	{
		ccombo_popup_destroy (GTK_WIDGET (list));
		return TRUE;
	}
	return FALSE;
}

static GtkWidget *
ccombo_create_list (GnmCComboView *ccombo, SheetObject *so,
		    GtkTreePath **clip, GtkTreePath **select, gboolean *make_buttons)
{
	GnmCComboViewClass *klass = GNM_CCOMBO_VIEW_GET_CLASS (ccombo);
	return (klass->create_list) (so, clip, select, make_buttons);
}

/****************************************************************************/

/* Cut and paste from gtkwindow.c */
static void
ccombo_focus_change (GtkWidget *widget, gboolean in)
{
	GdkEventFocus fevent;

	g_object_ref (widget);

	gtk_widget_set_can_focus (widget, in);

	fevent.type = GDK_FOCUS_CHANGE;
	fevent.window = gtk_widget_get_window (widget);
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
			guint timer_id = g_timeout_add (50,
				(GSourceFunc)cb_ccombo_autoscroll, list);
			g_object_set_data (list, AUTOSCROLL_ID,
				GUINT_TO_POINTER (timer_id));
		}
	} else if (dir == 0) {
		g_source_remove (GPOINTER_TO_UINT (id));
		g_object_set_data (list, AUTOSCROLL_ID, NULL);
	}
	g_object_set_data (list, AUTOSCROLL_DIR, GINT_TO_POINTER (dir));
}

static void
ccombo_popup_destroy (GtkWidget *list)
{
	ccombo_autoscroll_set (G_OBJECT (list), 0);
	ccombo_focus_change (list, FALSE);
	gtk_widget_destroy (gtk_widget_get_toplevel (list));
}

static gint
cb_ccombo_key_press (G_GNUC_UNUSED GtkWidget *popup, GdkEventKey *event, GtkWidget *list)
{
	switch (event->keyval) {
	case GDK_KEY_Escape :
		ccombo_popup_destroy (list);
		return TRUE;

	case GDK_KEY_KP_Down :
	case GDK_KEY_Down :
	case GDK_KEY_KP_Up :
		/* fallthrough */
	case GDK_KEY_Up :
		if (!(event->state & GDK_MOD1_MASK))
			return FALSE;

	case GDK_KEY_KP_Enter :
	case GDK_KEY_Return :
		ccombo_activate (GTK_TREE_VIEW (list), FALSE);
		return TRUE;
	default :
		;
	}
	return FALSE;
}

static gboolean
cb_ccombo_popup_motion (G_GNUC_UNUSED GtkWidget *widget, GdkEventMotion *event,
			GtkTreeView *list)
{
	int base, dir = 0;
	GtkAllocation la;

	gtk_widget_get_allocation (GTK_WIDGET (list), &la);

	gdk_window_get_origin (gtk_widget_get_window (GTK_WIDGET (list)),
			       NULL, &base);
	if (event->y_root < base)
		dir = -1;
	else if (event->y_root >= (base + la.height))
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
	GtkAllocation wa;

	gtk_widget_get_allocation (widget, &wa);

	if (event->x >= 0 && event->y >= 0 &&
	    event->x < wa.width && event->y < wa.height &&
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
	if (event->button == 1 &&
	    event->window != gtk_widget_get_window (popup)) {
		ccombo_popup_destroy (list);
		return TRUE;
	}
	return FALSE;
}

static gint
cb_ccombo_button_release (GtkWidget *popup, GdkEventButton *event,
			  GtkTreeView *list)
{
	if (event->button == 1) {
		if (gtk_get_event_widget ((GdkEvent *) event) == GTK_WIDGET (list))
			return ccombo_activate (list, FALSE);

		g_signal_handlers_disconnect_by_func (popup,
						      G_CALLBACK (cb_ccombo_popup_motion), list);
		ccombo_autoscroll_set (G_OBJECT (list), 0);
	}
	return FALSE;
}

static void cb_ccombo_button_pressed	(SheetObjectView *sov)	{ gnm_cell_combo_view_popdown (sov, GDK_CURRENT_TIME); }
static void cb_ccombo_ok_button		(GtkTreeView *list)	{ ccombo_activate (list, TRUE); }
static void cb_ccombo_cancel_button	(GtkWidget *list)	{ ccombo_popup_destroy (list); }

static void
cb_realize_treeview (GtkWidget *list, GtkWidget *sw)
{
	GtkRequisition req;
	GdkRectangle rect;
	GtkTreePath *clip = g_object_get_data (G_OBJECT (list), "clip");

	gtk_widget_get_preferred_size (GTK_WIDGET (list), &req, NULL);

	gtk_tree_view_get_background_area (GTK_TREE_VIEW (list),
					   clip, NULL, &rect);

	gtk_scrolled_window_set_min_content_width (GTK_SCROLLED_WINDOW (sw), req.width);

	gtk_scrolled_window_set_min_content_height (GTK_SCROLLED_WINDOW (sw), rect.y);
}

/**
 * gnm_cell_combo_view_popdown:
 * @sov: #SheetObjectView
 * @activate_time: event time
 *
 * Open the popup window associated with @sov
 **/
void
gnm_cell_combo_view_popdown (SheetObjectView *sov, guint32 activate_time)
{
	GocItem		   *view   = GOC_ITEM (sov);
	GnmPane		   *pane   = GNM_PANE (view->canvas);
	SheetControlGUI	   *scg    = pane->simple.scg;
	SheetObject	   *so     = sheet_object_view_get_so (sov);
	Sheet const	   *sheet  = sheet_object_get_sheet (so);
	GtkWidget *frame,  *popup, *list, *container;
	int root_x, root_y;
	gboolean 	make_buttons = FALSE;
	GtkTreePath	  *clip = NULL, *select = NULL;
	GtkWindow *toplevel = wbcg_toplevel (scg_wbcg (scg));
	GdkWindow *popup_window;
	GdkDevice *device;
	GnmRange const *merge;

	popup = gtk_window_new (GTK_WINDOW_POPUP);

	gtk_window_set_type_hint (GTK_WINDOW (popup), GDK_WINDOW_TYPE_HINT_COMBO);
	gtk_window_group_add_window (gtk_window_get_group (toplevel), GTK_WINDOW (popup));
	go_gtk_window_set_transient (toplevel, GTK_WINDOW (popup));
	gtk_window_set_resizable (GTK_WINDOW (popup), FALSE);
	gtk_window_set_decorated (GTK_WINDOW (popup), FALSE);
	gtk_window_set_screen (GTK_WINDOW (popup),
		gtk_widget_get_screen (GTK_WIDGET (toplevel)));

	list = ccombo_create_list (GNM_CCOMBO_VIEW (sov), so, &clip, &select, &make_buttons);

	gtk_tree_view_set_headers_visible (GTK_TREE_VIEW (list), FALSE);
	g_object_set_data (G_OBJECT (list), SOV_ID, sov);

	frame = gtk_frame_new (NULL);
	gtk_frame_set_shadow_type (GTK_FRAME (frame), GTK_SHADOW_OUT);

#if 0
	range_dump (&so->anchor.cell_bound, "");
	g_printerr (" : so = %p, view = %p\n", so, view);
#endif
	if (clip != NULL) {
		GtkWidget *sw = gtk_scrolled_window_new (
			gtk_scrollable_get_hadjustment (GTK_SCROLLABLE (list)),
			gtk_scrollable_get_vadjustment (GTK_SCROLLABLE (list)));
		gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (sw),
						GTK_POLICY_AUTOMATIC,
						GTK_POLICY_ALWAYS);
		g_object_set_data_full (G_OBJECT (list),
					"clip", clip,
					(GDestroyNotify)gtk_tree_path_free);

		gtk_container_add (GTK_CONTAINER (sw), list);

		/*
		 * Do the sizing in a realize handler as newer versions of
		 * gtk+ give us zero sizes until then.
		 */
		g_signal_connect_after (list, "realize",
					G_CALLBACK (cb_realize_treeview),
					sw);
		container = sw;
	} else
		container = list;

	if (make_buttons) {
		GtkWidget *vbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
		GtkWidget *hbox = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);

		GtkWidget *button;
		button = gtk_button_new_from_stock (GTK_STOCK_CANCEL);
		g_signal_connect_swapped (button, "clicked",
			G_CALLBACK (cb_ccombo_cancel_button), list);
		gtk_box_pack_start (GTK_BOX (hbox), button, FALSE, TRUE, 6);
		button = gtk_button_new_from_stock (GTK_STOCK_OK);
		g_signal_connect_swapped (button, "clicked",
			G_CALLBACK (cb_ccombo_ok_button), list);
		gtk_box_pack_start (GTK_BOX (hbox), button, FALSE, TRUE, 6);

		gtk_box_pack_start (GTK_BOX (vbox), container, FALSE, TRUE, 6);
		gtk_box_pack_start (GTK_BOX (vbox), hbox, FALSE, TRUE, 6);
		container = vbox;
	}

	gtk_container_add (GTK_CONTAINER (frame), container);

	/* do the popup */
	gdk_window_get_origin (gtk_widget_get_window (GTK_WIDGET (pane)),
			       &root_x, &root_y);
	if (sheet->text_is_rtl) {
		GtkAllocation pa;
		gtk_widget_get_allocation (GTK_WIDGET (pane), &pa);
		root_x += pa.width;
		root_x -= scg_colrow_distance_get (scg, TRUE,
			pane->first.col,
			so->anchor.cell_bound.start.col + 1);
	} else
		root_x += scg_colrow_distance_get (scg, TRUE,
			pane->first.col,
			so->anchor.cell_bound.start.col);
	merge = gnm_sheet_merge_is_corner (sheet, &(so->anchor.cell_bound.start));
	gtk_window_move (GTK_WINDOW (popup), root_x,
		root_y + scg_colrow_distance_get
			 (scg, FALSE,
			  pane->first.row,
			  so->anchor.cell_bound.start.row +
			  ((merge == NULL) ? 1 : range_height (merge))));

	gtk_container_add (GTK_CONTAINER (popup), frame);

	g_signal_connect (popup, "key_press_event",
		G_CALLBACK (cb_ccombo_key_press), list);
	g_signal_connect (popup, "button_press_event",
		G_CALLBACK (cb_ccombo_button_press), list);
	g_signal_connect_after (popup, "button_release_event",
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

	popup_window = gtk_widget_get_window (popup);

	device = gtk_get_current_event_device ();
	if (0 == gdk_device_grab (device, popup_window,
	                          GDK_OWNERSHIP_APPLICATION, TRUE,
	                          GDK_BUTTON_PRESS_MASK |
	                          GDK_BUTTON_RELEASE_MASK |
	                          GDK_POINTER_MOTION_MASK,
	                          NULL, activate_time)) {
		if (0 == gdk_device_grab (gdk_device_get_associated_device (device),
		                          popup_window,
		                          GDK_OWNERSHIP_APPLICATION, TRUE,
		                          GDK_KEY_PRESS_MASK |
		                          GDK_KEY_RELEASE_MASK,
		                          NULL, activate_time))
			gtk_grab_add (popup);
		else
			gdk_device_ungrab (device, activate_time);
	}
}

/**
 * gnm_cell_combo_view_new:
 * @so: #SheetObject
 * @type: #GType
 * @container: SheetObjectViewContainer (a GnmPane)
 *
 * Create and register an in cell combo to pick from an autofilter list.
 **/
SheetObjectView *
gnm_cell_combo_view_new (SheetObject *so, GType type,
			 SheetObjectViewContainer *container)
{
	GnmPane *pane = GNM_PANE (container);
	GtkWidget *view_widget = gtk_button_new ();
	GocItem *ccombo = goc_item_new (pane->object_views, type, NULL);
	goc_item_new (GOC_GROUP (ccombo), GOC_TYPE_WIDGET,
		"widget",	view_widget,
		NULL);
	gtk_widget_set_can_focus (view_widget, FALSE);

	gtk_container_add (GTK_CONTAINER (view_widget),
		ccombo_create_arrow (GNM_CCOMBO_VIEW (ccombo), so));
	g_signal_connect_swapped (view_widget, "pressed",
		G_CALLBACK (cb_ccombo_button_pressed), ccombo);
	gtk_widget_show_all (view_widget);

	return gnm_pane_object_register (so, ccombo, FALSE);
}

static void
gnm_cell_combo_view_init (SheetObjectView *view)
{
	view->resize_mode = GNM_SO_RESIZE_AUTO;
}

GSF_CLASS (GnmCComboView, gnm_ccombo_view,
	   NULL, gnm_cell_combo_view_init,
	   GNM_SO_VIEW_TYPE)
