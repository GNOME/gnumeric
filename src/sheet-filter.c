/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/*
 * filter.c: support for filters
 *
 * Copyright (C) 2002 Jody Goldberg (jody@gnome.org)
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
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
 * USA
 */
#include <gnumeric-config.h>
#include "sheet-filter.h"

#include "sheet.h"
#include "cell.h"
#include "expr.h"
#include "value.h"
#include "sheet-control-gui.h"
#include "sheet-object-impl.h"
#include "gnumeric-pane.h"
#include "gnumeric-canvas.h"
#include "dependent.h"
#include "ranges.h"

#include <libfoocanvas/foo-canvas-widget.h>
#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>
#include <gsf/gsf-impl-utils.h>
#include <gnumeric-i18n.h>

typedef struct {
	SheetObject parent;

	GnmFilter   *filter;
	int i;
} GnmFilterField;

typedef struct {
	SheetObjectClass s_object_class;
} GnmFilterFieldClass;

struct _GnmFilter {
	Dependent dep;
	Range  r;

	GPtrArray *fields;
};

#define FILTER_FIELD_TYPE     (filter_field_get_type ())
#define FILTER_FIELD(obj)     (G_TYPE_CHECK_INSTANCE_CAST((obj), FILTER_FIELD_TYPE, GnmFilterField))

#define	VIEW_ITEM	"view-item"

static GType filter_field_get_type (void);

static void
filter_field_finalize (GObject *object)
{
	GnmFilterField *cc = FILTER_FIELD (object);
	GObjectClass *parent;

	g_return_if_fail (cc != NULL);

	parent = g_type_class_peek (SHEET_OBJECT_TYPE);
	if (parent != NULL && parent->finalize != NULL)
		parent->finalize (object);
}

static void
filter_field_update_bounds (SheetObject *so, GObject *view_obj)
{
	double coords [4], tmp;
	FooCanvasItem   *view = FOO_CANVAS_ITEM (view_obj);
	SheetControlGUI	*scg  =
		SHEET_CONTROL_GUI (sheet_object_view_control (view_obj));

 	scg_object_view_position (scg, so, coords);
	 
	/* clip vertically */
	tmp = (coords[3] - coords[1]);
	if (tmp > 20.) {
		tmp = 20.;
		coords[1] = coords[3] - tmp;
	}
	/* maintain squareness horzontally */
	tmp = coords[2] - tmp;
	if (coords[0] < tmp)
		coords[0] = tmp;

	/* NOTE : far point is EXCLUDED so we add 1 */
	foo_canvas_item_set (view,
		"x", coords [0], "y", coords [1],
		"width",  coords [2] - coords [0] + 1.,
		"height", coords [3] - coords [1] + 1.,
		NULL);

	if (so->is_visible)
		foo_canvas_item_show (view);
	else
		foo_canvas_item_hide (view);
}

static  gint
cb_filter_key_press (GtkWidget *popup, GdkEventKey *event,
		    gpointer ignored)
{
	if (event->keyval != GDK_Escape)
		return FALSE;
	gtk_widget_destroy (popup);
	return TRUE;
}

static  gint
cb_filter_button_press (GtkWidget *popup, GdkEventButton *event, gpointer ignored)
{
	/* A press outside the popup cancels */
	if (event->x >= 0 && event->y >= 0 &&
	    event->x < popup->allocation.width &&
	    event->y < popup->allocation.height)
		return FALSE;

	gtk_widget_destroy (popup);
	return TRUE;
}

static  gint
cb_filter_button_release (GtkWidget *popup, GdkEventButton *event,
			  GnmFilterField *field)
{
	/* A release inside popup accepts */
	if (event->x >= 0 && event->y >= 0 &&
	    event->x < popup->allocation.width &&
	    event->y < popup->allocation.height) {
		gtk_widget_destroy (popup);
	}
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
} UniqueCollection;

static Value *
cb_collect_unique (Sheet *sheet, int col, int row, Cell *cell,
		   UniqueCollection *uc)
{
	if (cell_is_blank (cell)) {
		uc->has_blank = TRUE;
		printf ("%d\n", row+1);
	} else
		g_hash_table_replace (uc->hash, cell->value, cell->value);

	return NULL;
}
static void
cb_copy_hash_to_array (Value *key, gpointer value, gpointer sorted)
{
	g_ptr_array_add (sorted, key);
}

static GtkListStore *
collect_unique_elements (Sheet *sheet, Range const *r, GtkTreePath **clip)
{
	UniqueCollection uc;
	GtkTreeIter	 iter;
	GtkListStore *model;
	GPtrArray    *sorted = g_ptr_array_new ();
	unsigned i;

	model = gtk_list_store_new (3, G_TYPE_STRING, G_TYPE_POINTER, G_TYPE_INT);
	gtk_list_store_append (model, &iter);
	gtk_list_store_set (model, &iter, 0, _("(All)"),	   1, NULL, 2, 1, -1);
	gtk_list_store_append (model, &iter);
	gtk_list_store_set (model, &iter, 0, _("(Top 10...)"),     1, NULL, 2, 10,-1);
	gtk_list_store_append (model, &iter);
	gtk_list_store_set (model, &iter, 0, _("(Custom...)"),     1, NULL, 2, 2, -1);

	uc.has_blank = FALSE;
	uc.hash = g_hash_table_new (
			(GHashFunc) value_hash, (GEqualFunc) value_equal);
	sheet_foreach_cell_in_range (sheet,
		CELL_ITER_IGNORE_HIDDEN,
		r->start.col, r->start.row, r->end.col, r->end.row,
		(CellIterFunc)&cb_collect_unique, &uc);

	g_hash_table_foreach (uc.hash,
		(GHFunc) cb_copy_hash_to_array, sorted);
	qsort (&g_ptr_array_index (sorted, 0),
	       sorted->len, sizeof (Value *), value_cmp);
	for (i = 0; i < sorted->len ; i++) {
		Value const *v = g_ptr_array_index (sorted, i);
		gtk_list_store_append (model, &iter);
		gtk_list_store_set (model, &iter,
			0, value_peek_string (v),
			1, NULL,
			2, 0,
			-1);
		if (i == 10)
			*clip = gtk_tree_model_get_path (GTK_TREE_MODEL (model),
							 &iter);
	}

	if (uc.has_blank) {
		gtk_list_store_append (model, &iter);
		gtk_list_store_set (model, &iter, 0, _("(Blanks...)"),	   1, NULL, 2, 3, -1);
		gtk_list_store_append (model, &iter);
		gtk_list_store_set (model, &iter, 0, _("(Non Blanks...)"), 1, NULL, 2, 4, -1);
	}

	g_hash_table_destroy (uc.hash);

	return model;
}

static void
cb_filter_button_pressed (GtkButton *button, GnmFilterField *field)
{
	GObject	     *view = g_object_get_data (G_OBJECT (button), VIEW_ITEM);
	GnumericPane *pane = sheet_object_view_key (G_OBJECT (view));
	GtkWidget    *frame, *popup, *list;
	SheetControlGUI *scg = pane->gcanvas->simple.scg;
	int root_x, root_y;
	GtkListStore  *model;
	GtkTreeViewColumn *column;
	GtkTreePath	  *clip = NULL;
	GtkRequisition	req;
	Range r = field->filter->r;

	popup = gtk_window_new (GTK_WINDOW_POPUP);

	r.start.row++;
	/* r.end.row =  XL actually extend to the first non-empty element in the list */
	r.end.col = r.start.col += field->i;
	model = collect_unique_elements (field->filter->dep.sheet, &r, &clip);
	column = gtk_tree_view_column_new_with_attributes ("ID",
			gtk_cell_renderer_text_new (), "text", 0,
			NULL);
	list = gtk_tree_view_new_with_model (GTK_TREE_MODEL (model));
	gtk_tree_view_set_headers_visible (GTK_TREE_VIEW (list), FALSE);
	gtk_tree_view_append_column (GTK_TREE_VIEW (list), column);
	gtk_widget_size_request (GTK_WIDGET (list), &req);

	frame = gtk_frame_new (NULL);
	gtk_frame_set_shadow_type (GTK_FRAME (frame), GTK_SHADOW_OUT);

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
		gtk_container_add (GTK_CONTAINER (frame), sw);
	} else
		gtk_container_add (GTK_CONTAINER (frame), list);

	/* do the popup */
	gtk_window_set_decorated (GTK_WINDOW (popup), FALSE);
	gdk_window_get_origin (GTK_WIDGET (pane->gcanvas)->window,
		&root_x, &root_y);
	gtk_window_move (GTK_WINDOW (popup),
		root_x + scg_colrow_distance_get (scg, TRUE,
			pane->gcanvas->first.col,
			field->filter->r.start.col + field->i + 1) - req.width,
		root_y + scg_colrow_distance_get (scg, FALSE,
			pane->gcanvas->first.row,
			field->filter->r.start.row + 1));

	gtk_container_add (GTK_CONTAINER (popup), frame);

	g_signal_connect (popup,
		"key_press_event",
		G_CALLBACK (cb_filter_key_press), NULL);
	g_signal_connect (popup,
		"button_press_event",
		G_CALLBACK (cb_filter_button_press), NULL);
	g_signal_connect (popup,
		"button_release_event",
		G_CALLBACK (cb_filter_button_release), field);
	g_signal_connect (popup,
		"motion_notify_event",
		G_CALLBACK (cb_filter_motion_notify_event), list);

	gtk_widget_show_all (popup);
	gtk_grab_add (popup);
	gdk_pointer_grab (popup->window, FALSE,
		GDK_BUTTON_PRESS_MASK | 
		GDK_BUTTON_RELEASE_MASK |
		GDK_POINTER_MOTION_MASK, 
		NULL, NULL, GDK_CURRENT_TIME);
}

static GObject *
filter_field_new_view (SheetObject *so, SheetControl *sc, gpointer key)
{
	GnmCanvas *gcanvas = ((GnumericPane *)key)->gcanvas;
	GtkWidget *arrow, *view_widget = gtk_button_new ();
	FooCanvasItem *view_item = foo_canvas_item_new (
		gcanvas->object_group,
		foo_canvas_widget_get_type (),
		"widget",	view_widget,
		"size_pixels",	FALSE,
		NULL);

	GTK_WIDGET_UNSET_FLAGS (view_widget, GTK_CAN_FOCUS);
	arrow = gtk_arrow_new (GTK_ARROW_DOWN, GTK_SHADOW_IN);
	gtk_container_add (GTK_CONTAINER (view_widget), arrow);

	g_object_set_data (G_OBJECT (view_widget), VIEW_ITEM, view_item);
	g_signal_connect (view_widget,
		"pressed",
		G_CALLBACK (cb_filter_button_pressed), so);

	/* Do not use the standard handler the combo is not editable */
	/* gnm_pane_widget_register (so, view_widget, view_item); */

	gtk_widget_show_all (view_widget);

	return G_OBJECT (view_item);
}

static void
filter_field_class_init (GObjectClass *object_class)
{
	SheetObjectClass *sheet_object_class = SHEET_OBJECT_CLASS (object_class);

	/* Object class method overrides */
	object_class->finalize = filter_field_finalize;

	/* SheetObject class method overrides */
	sheet_object_class->update_bounds = filter_field_update_bounds;
	sheet_object_class->new_view	  = filter_field_new_view;
	sheet_object_class->read_xml      = NULL;
	sheet_object_class->write_xml     = NULL;
	sheet_object_class->print         = NULL;
	sheet_object_class->clone         = NULL;
}

GSF_CLASS (GnmFilterField, filter_field,
	   filter_field_class_init, NULL, SHEET_OBJECT_TYPE);

/*************************************************************************/

#define DEP_TO_FILTER(d_ptr) (GnmFilter *)(((char *)d_ptr) - G_STRUCT_OFFSET(GnmFilter, dep))

static void
filter_eval (Dependent *dep)
{
	/* do nothing for now, its unclear whether people want the filter to
	 * auto reapply */
}
static void
filter_set_expr (Dependent *dep, GnmExpr const *new_expr)
{
	g_warning ("TODO : move or invalidate the filter");
}

static void
filter_debug_name (Dependent const *dep, FILE *out)
{
	fprintf (out, "Filter%p", dep);
}

static DEPENDENT_MAKE_TYPE (filter, filter_set_expr)

/**
 * gnm_filter_new :
 * @sheet :
 * @r :
 *
 * Init a filter
 **/
GnmFilter *
gnm_filter_new (Sheet *sheet, Range const *r)
{
	static CellPos const dummy = { 0, 0 };
	/* pretend to fill the cell, then clip the X start later */
	static SheetObjectAnchorType const anchor_types [4] = {
		SO_ANCHOR_PERCENTAGE_FROM_COLROW_START,
		SO_ANCHOR_PERCENTAGE_FROM_COLROW_START,
		SO_ANCHOR_PERCENTAGE_FROM_COLROW_END,
		SO_ANCHOR_PERCENTAGE_FROM_COLROW_END
	};
	static float const offsets [4] = { .0, .0, 0., 0. };
	GnmFilter	*filter;
	GnmFilterField	*field;
	SheetObjectAnchor anchor;
	Range tmp;
	int i;

	g_return_val_if_fail (IS_SHEET (sheet), NULL);
	g_return_val_if_fail (r != NULL, NULL);
	
	filter = g_new0 (GnmFilter, 1);
	filter->dep.sheet = sheet;
	filter->dep.flags = filter_get_dep_type ();
	filter->dep.expression = gnm_expr_new_constant (
		value_new_cellrange_r (sheet, r));

	filter->r = *r;
	filter->fields = g_ptr_array_new ();

	tmp.start.row = tmp.end.row = r->start.row;
	for (i = range_width (r); i-- > 0 ;) {
		field = g_object_new (filter_field_get_type (), NULL);
		field->filter = filter;
		field->i      = i;
		tmp.start.col = tmp.end.col = r->start.col + i;
		sheet_object_anchor_init (&anchor, &tmp, offsets, anchor_types,
					  SO_DIR_DOWN_RIGHT);
		sheet_object_anchor_set (&field->parent, &anchor);
		sheet_object_set_sheet (&field->parent, sheet);
		g_ptr_array_add (filter->fields, field);
	}

	dependent_link (&filter->dep, &dummy);
	sheet->filters = g_slist_prepend (sheet->filters, filter);

	return filter;
}

void
gnm_filter_free	(GnmFilter *filter)
{
	unsigned i;

	g_return_if_fail (filter != NULL);

	for (i = 0 ; i < filter->fields->len ; i++)
		sheet_object_clear_sheet (g_ptr_array_index (filter->fields, i));
	g_ptr_array_free (filter->fields, TRUE);

	gnm_expr_unref (filter->dep.expression);
	filter->fields = NULL;
	g_free (filter);
}

void
gnm_filter_remove (GnmFilter *filter)
{
	static CellPos const dummy = { 0, 0 };
	Sheet *sheet;

	g_return_if_fail (filter != NULL);

	sheet = filter->dep.sheet;
	sheet->filters = g_slist_remove (sheet->filters, filter);
	dependent_unlink (&filter->dep, &dummy);
}

/**
 * gnm_filter_contains_row :
 * @filter :
 * @col :
 *
 **/
gboolean
gnm_filter_contains_row (GnmFilter const *filter, int row)
{
	g_return_val_if_fail (filter != NULL, FALSE);

	return (filter->r.start.row <= row && row <= filter->r.end.row);
}
