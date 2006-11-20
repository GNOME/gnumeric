/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/*
 * gnm-filter-combo.c: the autofilter combo box
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
#include "gnm-filter-combo.h"

#include "gui-gnumeric.h"
#include "sheet-filter.h"
#include "sheet-filter-combo.h"
#include "gnm-format.h"
#include "value.h"
#include "cell.h"
#include "sheet.h"
#include "workbook.h"
#include "style-color.h"
#include "sheet-control-gui.h"
#include "gnumeric-pane.h"
#include "gnumeric-canvas.h"
#include "../dialogs/dialogs.h"

#include <goffice/utils/regutf8.h>
#include <goffice/cut-n-paste/foocanvas/foo-canvas-widget.h>
#include <gsf/gsf-impl-utils.h>
#include <gtk/gtk.h>
#include <gdk/gdkevents.h>
#include <gdk/gdkkeysyms.h>
#include <glib/gi18n-lib.h>
#include <string.h>

#define	VIEW_ITEM_ID	"view-item"
#define	FCOMBO_ID	"fcombo"
#define	WBCG_ID		"wbcg"

static int
gnm_filter_combo_index (GnmFilterCombo const *fcombo)
{
	if (NULL != fcombo->filter)
		return fcombo->parent.anchor.cell_bound.start.col
			- fcombo->filter->r.start.col;
	return 0;
}

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
filter_popup_destroy (GtkWidget *popup, GtkWidget *list)
{
	do_focus_change (list, FALSE);
	gtk_widget_destroy (popup);
}

static gint
cb_filter_key_press (GtkWidget *popup, GdkEventKey *event, GtkWidget *list)
{
	if (event->keyval != GDK_Escape)
		return FALSE;
	filter_popup_destroy (popup, list);
	return TRUE;
}

static gint
cb_filter_button_press (GtkWidget *popup, GdkEventButton *event,
			GtkWidget *list)
{
	/* A press outside the popup cancels */
	if (event->window != popup->window) {
		filter_popup_destroy (popup, list);
		return TRUE;
	}
	return FALSE;
}

static gint
cb_filter_button_release (GtkWidget *popup, GdkEventButton *event,
			  GtkTreeView *list)
{
	GtkTreeIter  iter;
	GnmFilterCombo *fcombo;
	GnmFilterCondition *cond = NULL;
	WorkbookControlGUI *wbcg;
	GtkWidget *event_widget = gtk_get_event_widget ((GdkEvent *) event);
	int field_num;

	/* A release inside list accepts */
	if (event_widget != GTK_WIDGET (list))
		return FALSE;

	fcombo = g_object_get_data (G_OBJECT (list), FCOMBO_ID);
	wbcg  = g_object_get_data (G_OBJECT (list), WBCG_ID);
	if (fcombo != NULL &&
	    gtk_tree_selection_get_selected (gtk_tree_view_get_selection (list),
					     NULL, &iter)) {
		char    *strval;
		int	 type;
		gboolean set_condition = TRUE;

		gtk_tree_model_get (gtk_tree_view_get_model (list), &iter,
				    1, &strval, 2, &type,
				    -1);

		field_num = gnm_filter_combo_index (fcombo);
		switch (type) {
		case  0:
			cond = gnm_filter_condition_new_single (
				GNM_FILTER_OP_EQUAL,
				value_new_string_nocopy (strval));
			strval = NULL;			
			break;
		case  1: /* unfilter */
			cond = NULL;
			break;
		case  2: /* Custom */
			set_condition = FALSE;
			dialog_auto_filter (wbcg, fcombo->filter, field_num,
					    TRUE, fcombo->cond);
			break;
		case  3:
			cond = gnm_filter_condition_new_single (
				GNM_FILTER_OP_BLANKS, NULL);
			break;
		case  4:
			cond = gnm_filter_condition_new_single (
				GNM_FILTER_OP_NON_BLANKS, NULL);
			break;
		case 10: /* Top 10 */
			set_condition = FALSE;
			dialog_auto_filter (wbcg, fcombo->filter, field_num,
					    FALSE, fcombo->cond);
			break;
		default:
			set_condition = FALSE;
			g_warning ("Unknown type %d", type);
		}

		g_free (strval);

		if (set_condition) {
			gnm_filter_set_condition (fcombo->filter, field_num,
						  cond, TRUE);
			sheet_update (fcombo->filter->sheet);
		}
	}
	filter_popup_destroy (popup, GTK_WIDGET (list));
	return TRUE;
}

static gboolean
cb_filter_motion_notify_event (GtkWidget *widget, GdkEventMotion *event,
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
	gboolean has_blank;
	GHashTable *hash;
	GODateConventions const *date_conv;
} UniqueCollection;

static GnmValue *
cb_collect_unique (GnmCellIter const *iter, UniqueCollection *uc)
{
	if (gnm_cell_is_blank (iter->cell))
		uc->has_blank = TRUE;
	else {
		GOFormat const *format = gnm_cell_get_format (iter->cell);			
		GnmValue const *v = iter->cell->value;
		char *str = format_value (format, v, NULL, -1, uc->date_conv);
		g_hash_table_replace (uc->hash, str, iter->cell);
	}

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
collect_unique_elements (GnmFilterCombo *fcombo,
			 GtkTreePath **clip, GtkTreePath **select)
{
	UniqueCollection uc;
	GtkTreeIter	 iter;
	GtkListStore *model;
	GPtrArray    *sorted = g_ptr_array_new ();
	unsigned i;
	gboolean is_custom = FALSE;
	GnmRange	 r = fcombo->filter->r;
	GnmValue const *check = NULL;
	Sheet *sheet = fcombo->filter->sheet;

	if (fcombo->cond != NULL &&
	    fcombo->cond->op[0] == GNM_FILTER_OP_EQUAL &&
	    fcombo->cond->op[1] == GNM_FILTER_UNUSED) {
		check = fcombo->cond->value[0];
	}

	model = gtk_list_store_new (3, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_INT);

	gtk_list_store_append (model, &iter);
	gtk_list_store_set (model, &iter, 0, _("(All)"),	   1, NULL, 2, 1, -1);
	if (fcombo->cond == NULL || fcombo->cond->op[0] == GNM_FILTER_UNUSED)
		*select = gtk_tree_model_get_path (GTK_TREE_MODEL (model), &iter);

	gtk_list_store_append (model, &iter);
	gtk_list_store_set (model, &iter, 0, _("(Top 10...)"),     1, NULL, 2, 10,-1);
	if (fcombo->cond != NULL &&
	    (GNM_FILTER_OP_TYPE_MASK & fcombo->cond->op[0]) == GNM_FILTER_OP_TOP_N)
		*select = gtk_tree_model_get_path (GTK_TREE_MODEL (model), &iter);

	/* default to this we can easily revamp later */
	gtk_list_store_append (model, &iter);
	gtk_list_store_set (model, &iter, 0, _("(Custom...)"),     1, NULL, 2, 2, -1);
	if (*select == NULL) {
		is_custom = TRUE;
		*select = gtk_tree_model_get_path (GTK_TREE_MODEL (model), &iter);
	}

	r.start.row++;
	/* r.end.row =  XL actually extend to the first non-empty element in the list */
	r.end.col = r.start.col += gnm_filter_combo_index (fcombo);
	uc.has_blank = FALSE;
	uc.hash = g_hash_table_new_full (g_str_hash, g_str_equal,
					 (GDestroyNotify)g_free,
					 NULL);
	uc.date_conv = workbook_date_conv (sheet->workbook);

	sheet_foreach_cell_in_range (sheet,
		CELL_ITER_ALL,
		r.start.col, r.start.row, r.end.col, r.end.row,
		(CellIterFunc)&cb_collect_unique, &uc);

	g_hash_table_foreach (uc.hash, (GHFunc)cb_hash_domain, sorted);
	qsort (&g_ptr_array_index (sorted, 0),
	       sorted->len, sizeof (char *),
	       order_alphabetically);
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
				    2, 0,
				    -1);
		g_free (label);
		if (i == 10)
			*clip = gtk_tree_model_get_path (GTK_TREE_MODEL (model),
							 &iter);
		if (check != NULL) {
			if (strcmp (value_peek_string (check), str) == 0) {
				gtk_tree_path_free (*select);
				*select = gtk_tree_model_get_path (GTK_TREE_MODEL (model), &iter);
			}
		}
	}

	if (uc.has_blank) {
		gtk_list_store_append (model, &iter);
		gtk_list_store_set (model, &iter, 0, _("(Blanks...)"),	   1, NULL, 2, 3, -1);
		if (fcombo->cond != NULL &&
		    fcombo->cond->op[0] == GNM_FILTER_OP_BLANKS)
			*select = gtk_tree_model_get_path (GTK_TREE_MODEL (model), &iter);

		gtk_list_store_append (model, &iter);
		gtk_list_store_set (model, &iter, 0, _("(Non Blanks...)"), 1, NULL, 2, 4, -1);
		if (fcombo->cond != NULL &&
		    fcombo->cond->op[0] == GNM_FILTER_OP_NON_BLANKS)
			*select = gtk_tree_model_get_path (GTK_TREE_MODEL (model), &iter);
	} else if (is_custom && fcombo->cond != NULL &&
		   (GNM_FILTER_OP_TYPE_MASK & fcombo->cond->op[0]) == GNM_FILTER_OP_BLANKS) {
		gtk_tree_path_free (*select);
		*select = NULL;
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
cb_filter_button_pressed (GtkButton *button, FooCanvasItem *view)
{
	GnmPane		*pane = GNM_CANVAS (view->canvas)->pane;
	SheetControlGUI *scg = pane->gcanvas->simple.scg;
	SheetObject	*so = sheet_object_view_get_so (SHEET_OBJECT_VIEW (view));
	GnmFilterCombo	*fcombo = GNM_FILTER_COMBO (so);
	GtkWidget *frame, *popup, *list, *container;
	int root_x, root_y;
	GtkListStore  *model;
	GtkTreeViewColumn *column;
	GtkTreePath	  *clip = NULL, *select = NULL;
	GtkRequisition	req;

	popup = gtk_window_new (GTK_WINDOW_POPUP);
	model = collect_unique_elements (fcombo, &clip, &select);
	column = gtk_tree_view_column_new_with_attributes ("ID",
			gtk_cell_renderer_text_new (), "text", 0,
			NULL);
	list = gtk_tree_view_new_with_model (GTK_TREE_MODEL (model));
	gtk_tree_view_set_headers_visible (GTK_TREE_VIEW (list), FALSE);
	gtk_tree_view_append_column (GTK_TREE_VIEW (list), column);
	gtk_widget_size_request (GTK_WIDGET (list), &req);
	g_object_set_data (G_OBJECT (list), FCOMBO_ID, fcombo);
	g_object_set_data (G_OBJECT (list), WBCG_ID, scg_wbcg (scg));
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
			fcombo->parent.anchor.cell_bound.start.col + 1) - req.width,
		root_y + scg_colrow_distance_get (scg, FALSE,
			pane->gcanvas->first.row,
			fcombo->filter->r.start.row + 1));

	gtk_container_add (GTK_CONTAINER (popup), frame);

	g_signal_connect (popup,
		"key_press_event",
		G_CALLBACK (cb_filter_key_press), list);
	g_signal_connect (popup,
		"button_press_event",
		G_CALLBACK (cb_filter_button_press), list);
	g_signal_connect (popup,
		"button_release_event",
		G_CALLBACK (cb_filter_button_release), list);
	g_signal_connect (list,
		"motion_notify_event",
		G_CALLBACK (cb_filter_motion_notify_event), list);

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

static void
filter_combo_arrow_format (GnmFilterCombo *fcombo, GtkWidget *arrow)
{
	gtk_arrow_set (GTK_ARROW (arrow),
		fcombo->cond != NULL ? GTK_ARROW_RIGHT : GTK_ARROW_DOWN,
		GTK_SHADOW_IN);
	gtk_widget_modify_fg (arrow, GTK_STATE_NORMAL,
		fcombo->cond != NULL ? &gs_yellow : &gs_black);
}

static void
filter_view_destroy (SheetObjectView *sov)
{
	gtk_object_destroy (GTK_OBJECT (sov));
}
static void
filter_view_set_bounds (SheetObjectView *sov, double const *coords, gboolean visible)
{
	FooCanvasItem *view = FOO_CANVAS_ITEM (sov);

	if (visible) {
		double h, x;
		/* clip vertically */
		h = (coords[3] - coords[1]);
		if (h > 20.)
			h = 20.;

		/* maintain squareness horzontally */
		x = coords[2] - h;
		if (x < coords[0])
			x = coords[0];

		/* NOTE : far point is EXCLUDED so we add 1 */
		foo_canvas_item_set (view,
			"x",	x,
			"y",	coords [3] - h,
			"width",  coords [2] - x,
			"height", h + 1.,
			NULL);

		foo_canvas_item_show (view);
	} else
		foo_canvas_item_hide (view);
}

static void
gnm_filter_combo_foo_view_init (SheetObjectViewIface *sov_iface)
{
	sov_iface->destroy	= filter_view_destroy;
	sov_iface->set_bounds	= filter_view_set_bounds;
}
typedef FooCanvasWidget		GnmFilterComboFooView;
typedef FooCanvasWidgetClass	GnmFilterComboFooViewClass;
static GSF_CLASS_FULL (GnmFilterComboFooView, gnm_filter_combo_foo_view,
	NULL, NULL, NULL, NULL,
	NULL, FOO_TYPE_CANVAS_WIDGET, 0,
	GSF_INTERFACE (gnm_filter_combo_foo_view_init, SHEET_OBJECT_VIEW_TYPE))

SheetObjectView *
gnm_filter_combo_new_foo_view (SheetObject *so, SheetObjectViewContainer *container)
{
	GnmCanvas *gcanvas = ((GnmPane *)container)->gcanvas;
	GtkWidget *arrow, *view_widget = gtk_button_new ();
	GnmFilterCombo *fcombo = (GnmFilterCombo *) so;
	FooCanvasItem *view_item = foo_canvas_item_new (gcanvas->object_views,
		gnm_filter_combo_foo_view_get_type (),
		"widget",	view_widget,
		"size_pixels",	FALSE,
		NULL);

	GTK_WIDGET_UNSET_FLAGS (view_widget, GTK_CAN_FOCUS);
	arrow = gtk_arrow_new (fcombo->cond != NULL ? GTK_ARROW_RIGHT : GTK_ARROW_DOWN,
			       GTK_SHADOW_IN);
	filter_combo_arrow_format (fcombo, arrow);
	gtk_container_add (GTK_CONTAINER (view_widget), arrow);
	g_signal_connect_object (G_OBJECT (so), 
		"cond-changed",
		G_CALLBACK (filter_combo_arrow_format), arrow, 0);

	g_object_set_data (G_OBJECT (view_widget), VIEW_ITEM_ID, view_item);
	g_signal_connect (view_widget,
		"pressed",
		G_CALLBACK (cb_filter_button_pressed), view_item);
	gtk_widget_show_all (view_widget);

	return gnm_pane_object_register (so, view_item, FALSE);
}
