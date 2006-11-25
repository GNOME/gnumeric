/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/*
 * gnm-valudation-combo-foo-view.c: A foocanvas object for Validate from list
 * 				in cell combos
 *
 * Copyright (C) 2006 Jody Goldberg (jody@gnome.org)
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
#include "gnm-validation-combo-foo-view.h"
#include "validation-combo.h"

#include "commands.h"
#include "gnm-format.h"
#include "workbook-control.h"
#include "workbook.h"
#include "sheet-control-gui.h"
#include "sheet-view.h"
#include "sheet.h"
#include "cell.h"
#include "expr.h"
#include "value.h"

#include "gui-gnumeric.h"
#include "gnumeric-canvas.h"
#include "gnumeric-pane.h"

#include <goffice/utils/regutf8.h>
#include <goffice/cut-n-paste/foocanvas/foo-canvas-widget.h>
#include <gsf/gsf-impl-utils.h>
#include <gtk/gtk.h>
#include <gdk/gdkevents.h>
#include <gdk/gdkkeysyms.h>
#include <glib/gi18n-lib.h>
#include <string.h>

#define	VIEW_ITEM_ID	"view-item"
#define	VCOMBO_ID	"vcombo"
#define	WBC_ID		"wbcg"

/* Cut and paste from gtkwindow.c */
static void
do_focus_change (GtkWidget *widget, gboolean in)
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

static void
vcombo_popup_destroy (GtkWidget *popup, GtkWidget *list)
{
	do_focus_change (list, FALSE);
	gtk_widget_destroy (popup);
}

static gint
cb_cell_key_press (GtkWidget *popup, GdkEventKey *event, GtkWidget *list)
{
	if (event->keyval != GDK_Escape)
		return FALSE;
	vcombo_popup_destroy (popup, list);
	return TRUE;
}

static gint
cb_cell_button_press (GtkWidget *popup, GdkEventButton *event,
			GtkWidget *list)
{
	/* A press outside the popup cancels */
	if (event->window != popup->window) {
		vcombo_popup_destroy (popup, list);
		return TRUE;
	}
	return FALSE;
}

static gint
cb_cell_button_release (GtkWidget *popup, GdkEventButton *event,
			GtkTreeView *list)
{
	GtkTreeIter  iter;
	GnmValidationCombo *vcombo;
	WorkbookControl *wbc;
	GtkWidget *event_widget = gtk_get_event_widget ((GdkEvent *) event);

	/* A release inside list accepts */
	if (event_widget != GTK_WIDGET (list))
		return FALSE;

	vcombo = g_object_get_data (G_OBJECT (list), VCOMBO_ID);
	wbc    = g_object_get_data (G_OBJECT (list), WBC_ID);
	if (vcombo != NULL &&
	    gtk_tree_selection_get_selected (gtk_tree_view_get_selection (list),
					     NULL, &iter)) {
		char	*strval;
		SheetView *sv = wb_control_cur_sheet_view (wbc);
		gtk_tree_model_get (gtk_tree_view_get_model (list), &iter,
			1, &strval,
			-1);
		cmd_set_text (wbc, sv_sheet (sv), &sv->edit_pos, strval, NULL);
	}
	vcombo_popup_destroy (popup, GTK_WIDGET (list));
	return TRUE;
}

static gboolean
cb_cell_motion_notify_event (GtkWidget *widget, GdkEventMotion *event,
			       GtkTreeView *list)
{
	GtkTreePath *path;

	if (event->x >= 0 && event->y >= 0 &&
	    event->x < widget->allocation.width &&
	    event->y < widget->allocation.height &&
	    gtk_tree_view_get_path_at_pos (list, event->x, event->y,
					   &path, NULL, NULL, NULL)) {
		gtk_tree_selection_select_path (gtk_tree_view_get_selection (list), path);
		gtk_tree_path_free (path);
	}
	return TRUE;
}

typedef struct {
	GHashTable *hash;
	GODateConventions const *date_conv;
} UniqueCollection;

static GnmValue *
cb_collect_unique (GnmValueIter const *iter, UniqueCollection *uc)
{
	GOFormat const *fmt = (NULL != iter->cell_iter)
		? gnm_cell_get_format (iter->cell_iter->cell) : NULL;
	g_hash_table_replace (uc->hash, 
		format_value (fmt, iter->v, NULL, -1, uc->date_conv),
		value_dup (iter->v));
	return NULL;
}

static void
cb_hash_domain (GnmValue *key, gpointer value, gpointer accum)
{
	g_ptr_array_add (accum, key);
}

static int
order_alphabetically (void const *ptr_a, void const *ptr_b)
{
	char const * const *a = ptr_a;
	char const * const *b = ptr_b;
	return strcmp (*a, *b);
}

static GtkListStore *
collect_unique_elements (GnmValidationCombo *vcombo,
			 GtkTreePath **clip, GtkTreePath **select)
{
	unsigned	 i;
	UniqueCollection uc;
	GnmEvalPos	 ep;
	GtkTreeIter	 iter;
	GPtrArray	*sorted;
	GtkListStore	*model;
	GnmValue	*v;
	GnmValue const	*cur_val;
	GnmValidation const *val = vcombo->validation;

	model = gtk_list_store_new (3,
		G_TYPE_STRING, G_TYPE_STRING, gnm_value_get_type ());

	g_return_val_if_fail (val != NULL, model);
	g_return_val_if_fail (val->type == VALIDATION_TYPE_IN_LIST, model);
	g_return_val_if_fail (val->texpr[0] != NULL, model);
	g_return_val_if_fail (vcombo->sv != NULL, model);

	eval_pos_init_pos (&ep, sv_sheet (vcombo->sv), &vcombo->sv->edit_pos);
	v = gnm_expr_top_eval (val->texpr[0], &ep,
		 GNM_EXPR_EVAL_PERMIT_NON_SCALAR | GNM_EXPR_EVAL_PERMIT_EMPTY);
	if (NULL == v)
		return model;

	uc.date_conv = workbook_date_conv (vcombo->parent.sheet->workbook);
	uc.hash = g_hash_table_new_full (g_str_hash, g_str_equal,
		(GDestroyNotify)g_free, (GDestroyNotify)value_release);
	value_area_foreach (v, &ep, CELL_ITER_IGNORE_BLANK,
		 (GnmValueIterFunc) cb_collect_unique, &uc);
	value_release (v);

	sorted = g_ptr_array_new ();
	g_hash_table_foreach (uc.hash, (GHFunc)cb_hash_domain, sorted);
	qsort (&g_ptr_array_index (sorted, 0),
	       sorted->len, sizeof (char *),
	       order_alphabetically);

	cur_val = sheet_cell_get_value (ep.sheet, ep.eval.col, ep.eval.row);
	for (i = 0; i < sorted->len ; i++) {
		char const *str = g_ptr_array_index (sorted, i);
		char *label = NULL;
		gsize len = g_utf8_strlen (str, -1);
		unsigned const max = 50;

		if (len > max + 3) {
			label = g_strdup (str);
			strcpy (g_utf8_offset_to_pointer (label, max), "...");
		}

		gtk_list_store_append (model, &iter);
		gtk_list_store_set (model, &iter,
				    0, label ? label : str, /* Menu text */
				    1, str, /* Actual string selected on.  */
				    -1);
		g_free (label);
		if (i == 10)
			*clip = gtk_tree_model_get_path (GTK_TREE_MODEL (model), &iter);

		v = g_hash_table_lookup (uc.hash, str);
		if (cur_val != NULL && v != NULL &&
		    value_equal	(cur_val, v)) {
			gtk_tree_path_free (*select);
			*select = gtk_tree_model_get_path (GTK_TREE_MODEL (model), &iter);
		}
	}

	g_hash_table_destroy (uc.hash);
	g_ptr_array_free (sorted, TRUE);

	return model;
}

static void
cb_focus_changed (GtkWindow *toplevel)
{
#if 0
	g_warning (gtk_window_has_toplevel_focus (toplevel) ? "focus" : "no focus");
#endif
}

static void
cb_cell_button_pressed (GtkButton *button, FooCanvasItem *view)
{
	GnmPane		   *pane = GNM_CANVAS (view->canvas)->pane;
	SheetControlGUI	   *scg  = pane->gcanvas->simple.scg;
	SheetObject	   *so   = sheet_object_view_get_so (SHEET_OBJECT_VIEW (view));
	GnmValidationCombo *vcombo = GNM_VALIDATION_COMBO (so);
	GtkWidget *frame,  *popup, *list, *container;
	int root_x, root_y;
	GtkListStore  *model;
	GtkTreeViewColumn *column;
	GtkTreePath	  *clip = NULL, *select = NULL;
	GtkRequisition	req;

	popup = gtk_window_new (GTK_WINDOW_POPUP);
	model = collect_unique_elements (vcombo, &clip, &select);
	column = gtk_tree_view_column_new_with_attributes ("ID",
			gtk_cell_renderer_text_new (), "text", 0,
			NULL);
	list = gtk_tree_view_new_with_model (GTK_TREE_MODEL (model));
	gtk_tree_view_set_headers_visible (GTK_TREE_VIEW (list), FALSE);
	gtk_tree_view_append_column (GTK_TREE_VIEW (list), column);
	gtk_widget_size_request (GTK_WIDGET (list), &req);
	g_object_set_data (G_OBJECT (list), VCOMBO_ID, so);
	g_object_set_data (G_OBJECT (list), WBC_ID, scg_wbcg (scg));
	g_signal_connect (G_OBJECT (wbcg_toplevel (scg_wbcg (scg))),
		"notify::has-toplevel-focus",
		G_CALLBACK (cb_focus_changed), list);

	frame = gtk_frame_new (NULL);
	gtk_frame_set_shadow_type (GTK_FRAME (frame), GTK_SHADOW_OUT);

#if 0
	range_dump (&so->anchor.cell_bound, "");
	fprintf (stderr, " : so = %p, view = %p\n", so, view);
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
	gtk_window_set_decorated (GTK_WINDOW (popup), FALSE);
	gdk_window_get_origin (GTK_WIDGET (pane->gcanvas)->window,
		&root_x, &root_y);
	gtk_window_move (GTK_WINDOW (popup),
		root_x + scg_colrow_distance_get (scg, TRUE,
			pane->gcanvas->first.col,
			vcombo->parent.anchor.cell_bound.start.col + 1) - req.width,
		root_y + scg_colrow_distance_get (scg, FALSE,
			pane->gcanvas->first.row,
			vcombo->parent.anchor.cell_bound.start.row + 1));

	gtk_container_add (GTK_CONTAINER (popup), frame);

	g_signal_connect (popup, "key_press_event",
		G_CALLBACK (cb_cell_key_press), list);
	g_signal_connect (popup, "button_press_event",
		G_CALLBACK (cb_cell_button_press), list);
	g_signal_connect (popup, "button_release_event",
		G_CALLBACK (cb_cell_button_release), list);
	g_signal_connect (list,	 "motion_notify_event",
		G_CALLBACK (cb_cell_motion_notify_event), list);

	gtk_widget_show_all (popup);

	/* after we show the window setup the selection (showing the list
	 * clears the selection) */
	if (select != NULL) {
		gtk_tree_selection_select_path (
			gtk_tree_view_get_selection (GTK_TREE_VIEW (list)),
			select);
		gtk_tree_path_free (select);
	}

	gtk_widget_grab_focus (GTK_WIDGET (list));
	do_focus_change (GTK_WIDGET (list), TRUE);

	gtk_grab_add (popup);
	gdk_pointer_grab (popup->window, TRUE,
		GDK_BUTTON_PRESS_MASK |
		GDK_BUTTON_RELEASE_MASK |
		GDK_POINTER_MOTION_MASK,
		NULL, NULL, GDK_CURRENT_TIME);
}

/* Somewhat magic.
 * We do not honour all of the anchor flags.  All that is used is the far corner. */
static void
vcombo_set_bounds (SheetObjectView *sov, double const *coords, gboolean visible)
{
	FooCanvasItem *view = FOO_CANVAS_ITEM (sov);

	if (visible) {
		/* Far point is EXCLUDED so we add 1 */
		double h = (coords[3] - coords[1]) + 1;
		if (h > 20.)	/* clip vertically */
			h = 20.;
		foo_canvas_item_set (view,
			"x",	  coords [2],
			"y",	  coords [3] - h,
			"width",  h,	/* force a square, use h for width too */
			"height", h,
			NULL);
		foo_canvas_item_show (view);
	} else
		foo_canvas_item_hide (view);
}

static void vcombo_destroy (SheetObjectView *sov) { gtk_object_destroy (GTK_OBJECT (sov)); }
static void
gnm_validation_combo_foo_view_init (SheetObjectViewIface *sov_iface)
{
	sov_iface->destroy	= vcombo_destroy;
	sov_iface->set_bounds	= vcombo_set_bounds;
}
typedef FooCanvasWidget		GnmValidationComboFooView;
typedef FooCanvasWidgetClass	GnmValidationComboFooViewClass;
static GSF_CLASS_FULL (GnmValidationComboFooView, gnm_validation_combo_foo_view,
	NULL, NULL, NULL, NULL,
	NULL, FOO_TYPE_CANVAS_WIDGET, 0,
	GSF_INTERFACE (gnm_validation_combo_foo_view_init, SHEET_OBJECT_VIEW_TYPE))

SheetObjectView *
gnm_validation_combo_new_foo_view (SheetObject *so, SheetObjectViewContainer *container)
{
	GtkWidget *arrow, *view_widget;
	FooCanvasItem *view_item;
	GnmCanvas *gcanvas = ((GnmPane *)container)->gcanvas;
	GnmValidationCombo *vcombo = GNM_VALIDATION_COMBO (so);

	if (NULL == gcanvas || NULL == vcombo ||
	    scg_view (gcanvas->simple.scg) != vcombo->sv)
		return NULL;

	view_widget = gtk_button_new ();
	view_item = foo_canvas_item_new (gcanvas->object_views,
		gnm_validation_combo_foo_view_get_type (),
		"widget",	view_widget,
		"size_pixels",	FALSE,
		NULL);

	GTK_WIDGET_UNSET_FLAGS (view_widget, GTK_CAN_FOCUS);
	arrow = gtk_arrow_new (GTK_ARROW_DOWN, GTK_SHADOW_IN);
	gtk_container_add (GTK_CONTAINER (view_widget), arrow);

	g_object_set_data (G_OBJECT (view_widget), VIEW_ITEM_ID, view_item);
	g_signal_connect (view_widget,
		"pressed",
		G_CALLBACK (cb_cell_button_pressed), view_item);
	gtk_widget_show_all (view_widget);

	return gnm_pane_object_register (so, view_item, FALSE);
}
