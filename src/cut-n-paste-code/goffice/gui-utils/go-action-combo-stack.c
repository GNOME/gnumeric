/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/*
 * go-action-combo-stack.c: A custom GtkAction to handle undo/redo menus/toolbars
 *
 * Copyright (C) 2004 Jody Goldberg (jody@gnome.org)
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
#include "go-action-combo-stack.h"
#include <src/widgets/gnm-combo-box.h>
#include <src/gui-util.h>

#include <gtk/gtkaction.h>
#include <gtk/gtktoolitem.h>
#include <gtk/gtkimagemenuitem.h>
#include <gtk/gtkimage.h>
#include <gtk/gtkwindow.h>
#include <gtk/gtkcontainer.h>
#include <gtk/gtktreeview.h>
#include <gtk/gtktreeselection.h>
#include <gtk/gtkliststore.h>
#include <gtk/gtkcellrenderertext.h>
#include <gtk/gtkwidget.h>
#include <gtk/gtktogglebutton.h>
#include <gtk/gtkscrolledwindow.h>

#include <gsf/gsf-impl-utils.h>
#include <glib/gi18n.h>
#include <stdio.h>

////////////////////////////////////////////////////////////////////////////

typedef struct {
	GnmComboBox	parent;

	GtkWidget   *button;
	GtkTreeView *list;
	GtkWidget   *scrolled;

	unsigned bottom; /* numbered from the BOTTOM */
} GOComboStack;

typedef struct {
	GnmComboBoxClass	parent;
	void (*pop) (GOComboStack *cbox, GtkWidget *);
} GOComboStackClass;

enum {
	POP,
	LAST_SIGNAL
};
enum {
	LABEL_COL,
	INDEX_COL,
	KEY_COL
};

#define GO_COMBO_STACK_TYPE	(go_combo_stack_get_type ())
#define GO_COMBO_STACK(o)	G_TYPE_CHECK_INSTANCE_CAST (o, GO_COMBO_STACK_TYPE, GOComboStack)
#define IS_GO_COMBO_STACK(o)	G_TYPE_CHECK_INSTANCE_TYPE (o, GO_COMBO_STACK_TYPE)

typedef struct _GnmComboStack	     GnmComboStack;

static GtkType go_combo_stack_get_type   (void);
static guint go_combo_stack_signals [LAST_SIGNAL] = { 0, };

static void
cb_screen_changed (GOComboStack *cs, GdkScreen *previous_screen)
{
	GtkWidget *w = GTK_WIDGET (cs);
	GdkScreen *screen = gtk_widget_has_screen (w)
		? gtk_widget_get_screen (w)
		: NULL;

	if (screen) {
		GtkWidget *toplevel = gtk_widget_get_toplevel (cs->scrolled
		    ? cs->scrolled : GTK_WIDGET (cs->list));
		gtk_window_set_screen (GTK_WINDOW (toplevel), screen);
	}
}


static gboolean
cb_button_release_event (GtkWidget *list, GdkEventButton *e, gpointer data)
{
	GOComboStack *stack = GO_COMBO_STACK (data);

	gnm_combo_box_popup_hide (GNM_COMBO_BOX (stack));

	if (stack->bottom > 0) {
		gint dummy, w, h;
		gdk_window_get_geometry (e->window, &dummy, &dummy, &w, &h, &dummy);
		if (0 <= e->x && e->x < w && 0 <= e->y && e->y < h)
			g_signal_emit_by_name (stack, "pop", stack->bottom);
	}

	return TRUE;
}

static void
cb_button_clicked (GOComboStack *stack)
{
	g_signal_emit_by_name (stack, "pop", 1);
}

static gboolean
cb_motion_notify_event (GtkWidget *widget, GdkEventMotion *event,
			GOComboStack *stack)
{
	GtkTreePath	 *start, *pos;
	GtkTreeSelection *sel   = gtk_tree_view_get_selection (stack->list);

	if (event->x < 0 || event->y < 0 ||
	    event->x >= widget->allocation.width ||
	    event->y >= widget->allocation.height ||
	    !gtk_tree_view_get_path_at_pos (stack->list, event->x, event->y,
					    &pos, NULL, NULL, NULL))
		return TRUE;

	start = gtk_tree_path_new_first ();
	gtk_tree_selection_unselect_all (sel);
	gtk_tree_selection_select_range (sel, start, pos);
	gtk_tree_path_free (start);
	gtk_tree_path_free (pos);

	return TRUE;
}
static void
go_combo_stack_init (GOComboStack *stack)
{
	GtkScrolledWindow *scrolled;
	GtkTreeSelection *selection;

	stack->bottom = G_MAXINT;
	stack->button = gtk_toggle_button_new ();
	gtk_button_set_relief (GTK_BUTTON (stack->button), GTK_RELIEF_NONE);
	GTK_WIDGET_UNSET_FLAGS (stack->button, GTK_CAN_FOCUS);
	gnm_widget_disable_focus (GTK_WIDGET (stack));

	stack->list = (GtkTreeView *)gtk_tree_view_new ();
	selection = gtk_tree_view_get_selection (stack->list);
	gtk_tree_selection_set_mode (selection, GTK_SELECTION_MULTIPLE);

	stack->scrolled = gtk_scrolled_window_new (
		gtk_tree_view_get_hadjustment (stack->list),
		gtk_tree_view_get_vadjustment (stack->list));
	scrolled = GTK_SCROLLED_WINDOW (stack->scrolled);
	gtk_scrolled_window_set_policy (scrolled,
		GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
	gtk_scrolled_window_add_with_viewport (scrolled, GTK_WIDGET (stack->list));
	gtk_widget_set_size_request (stack->scrolled, -1, 200); /* MAGIC NUMBER */

	/* Set up the dropdown list */
	g_signal_connect (G_OBJECT (stack), "screen-changed", G_CALLBACK (cb_screen_changed), NULL);
	g_signal_connect (G_OBJECT (stack->list), "button_release_event",
		G_CALLBACK (cb_button_release_event),
		(gpointer) stack);
	g_signal_connect (G_OBJECT (stack->list),
		"motion_notify_event",
		G_CALLBACK (cb_motion_notify_event), stack);
	g_signal_connect_swapped (stack->button, "clicked",
		G_CALLBACK (cb_button_clicked),
		(gpointer) stack);

	gtk_widget_show (GTK_WIDGET (stack->list));
	gtk_widget_show (stack->scrolled);
	gtk_widget_show (stack->button);
	gnm_combo_box_construct (GNM_COMBO_BOX (stack),
		stack->button, stack->scrolled, GTK_WIDGET (stack->list));
}

static void
go_combo_stack_class_init (GObjectClass *klass)
{
	go_combo_stack_signals [POP] = g_signal_new ( "pop",
		G_TYPE_FROM_CLASS (klass),
		G_SIGNAL_RUN_LAST,
		G_STRUCT_OFFSET (GOComboStackClass, pop),
		NULL, NULL,
		g_cclosure_marshal_VOID__INT,
		G_TYPE_NONE,
		1, G_TYPE_INT);
}

GSF_CLASS (GOComboStack, go_combo_stack,
	   go_combo_stack_class_init, go_combo_stack_init,
	   GNM_COMBO_BOX_TYPE)

////////////////////////////////////////////////////////////////////////////

typedef struct {
	GtkToolItem	 base;
	GOComboStack	*combo; /* container has a ref, not us */
} GOToolComboStack;
typedef GtkToolItemClass GOToolComboStackClass;

#define GO_TOOL_COMBO_STACK_TYPE	(go_tool_combo_stack_get_type ())
#define GO_TOOL_COMBO_STACK(o)		(G_TYPE_CHECK_INSTANCE_CAST (o, GO_TOOL_COMBO_STACK_TYPE, GOToolComboStack))
#define IS_GO_TOOL_COMBO_STACK(o)	(G_TYPE_CHECK_INSTANCE_TYPE (o, GO_TOOL_COMBO_STACK_TYPE))

static GType go_tool_combo_stack_get_type (void);
static gboolean
go_tool_combo_stack_set_tooltip (GtkToolItem *tool_item, GtkTooltips *tooltips,
				 char const *tip_text,
				 char const *tip_private)
{
	GOToolComboStack *self = (GOToolComboStack *)tool_item;
#warning this is ugly the tip moves as we jump from preview to arrow
	gtk_tooltips_set_tip (tooltips, self->combo->button,
		tip_text, tip_private);
	gtk_tooltips_set_tip (tooltips, gnm_combo_box_get_arrow	(GNM_COMBO_BOX (self->combo)),
		tip_text, tip_private);
	return TRUE;
}

static void
go_tool_combo_stack_class_init (GtkToolItemClass *tool_item_klass)
{
	tool_item_klass->set_tooltip = go_tool_combo_stack_set_tooltip;
}

static GSF_CLASS (GOToolComboStack, go_tool_combo_stack,
	   go_tool_combo_stack_class_init, NULL,
	   GTK_TYPE_TOOL_ITEM)

/*****************************************************************************/

struct _GOActionComboStack {
	GtkAction	 base;
	GtkListStore	*model;
};
typedef GtkActionClass GOActionComboStackClass;

static GObjectClass *combo_stack_parent;

static GtkWidget *
go_action_combo_stack_create_tool_item (GtkAction *a)
{
	GOActionComboStack *saction = (GOActionComboStack *)a;
	GtkWidget *image;
	GtkTreeView *tree_view;
	GOToolComboStack *tool = g_object_new (GO_TOOL_COMBO_STACK_TYPE, NULL);
	char const *stock_id;
	gboolean is_sensitive = gtk_tree_model_iter_n_children (
		GTK_TREE_MODEL (saction->model), NULL) > 0;

	tool->combo = g_object_new (GO_COMBO_STACK_TYPE, NULL);
	tree_view = GTK_TREE_VIEW (tool->combo->list);
	gtk_tree_view_set_model (tree_view, GTK_TREE_MODEL (saction->model));
	gtk_tree_view_set_headers_visible (tree_view, FALSE);
	gtk_tree_view_append_column (tree_view,
		gtk_tree_view_column_new_with_attributes (NULL,
			gtk_cell_renderer_text_new (),
			"text", 0,
			NULL));

	g_object_get (G_OBJECT (a), "stock_id", &stock_id, NULL);
	image = gtk_image_new_from_stock (
		stock_id, GTK_ICON_SIZE_LARGE_TOOLBAR);
	gtk_widget_show (image);
	gtk_container_add (GTK_CONTAINER (tool->combo->button), image);

	gtk_container_add (GTK_CONTAINER (tool), GTK_WIDGET (tool->combo));
	gtk_widget_show (GTK_WIDGET (tool->combo));
	gtk_widget_show (GTK_WIDGET (tool));

	gtk_widget_set_sensitive (GTK_WIDGET (tool), is_sensitive);

	return GTK_WIDGET (tool);
}

static GtkWidget *
go_action_combo_stack_create_menu_item (GtkAction *a)
{
	GOActionComboStack *saction = (GOActionComboStack *)a;
	GtkWidget *item = gtk_image_menu_item_new_with_label ("UNDOREDO");
	gboolean is_sensitive = gtk_tree_model_iter_n_children (
		GTK_TREE_MODEL (saction->model), NULL) > 0;
	gtk_widget_set_sensitive (GTK_WIDGET (item), is_sensitive);
	return item;
}

static void
go_action_combo_stack_finalize (GObject *obj)
{
	GOActionComboStack *saction = (GOActionComboStack *)obj;
	g_object_unref (saction->model);
	saction->model = NULL;
	combo_stack_parent->finalize (obj);
}

static void
go_action_combo_stack_class_init (GtkActionClass *gtk_act_klass)
{
	GObjectClass *gobject_klass = (GObjectClass *)gtk_act_klass;

	combo_stack_parent = g_type_class_peek_parent (gobject_klass);

	gobject_klass->finalize = go_action_combo_stack_finalize;
	gtk_act_klass->create_tool_item = go_action_combo_stack_create_tool_item;
	gtk_act_klass->create_menu_item = go_action_combo_stack_create_menu_item;
}

static void
go_action_combo_stack_init (GOActionComboStack *saction)
{
	saction->model = gtk_list_store_new (3, G_TYPE_STRING, G_TYPE_INT, G_TYPE_POINTER);
}

GSF_CLASS (GOActionComboStack, go_action_combo_stack,
	   go_action_combo_stack_class_init, go_action_combo_stack_init,
	   GTK_TYPE_ACTION)

static void
check_sensitivity (GOActionComboStack *saction, unsigned old_count)
{
	unsigned new_count = gtk_tree_model_iter_n_children (
		GTK_TREE_MODEL (saction->model), NULL);

	if ((old_count > 0) ^ (new_count > 0)) {
		GSList *ptr = gtk_action_get_proxies (GTK_ACTION (saction));
		gboolean is_sensitive = (new_count > 0);
		for ( ; ptr != NULL ; ptr = ptr->next)
			gtk_widget_set_sensitive (ptr->data, is_sensitive);
	}
}

/**
 * go_action_combo_stack_push :
 * @act : #GOActionComboStack
 * @str : The label to push
 * @key : a key value to id the pushe item
 **/
void
go_action_combo_stack_push (GOActionComboStack *a,
			    char const *label, gpointer key)
{
	GOActionComboStack *saction = GO_ACTION_COMBO_STACK (a);
	GtkTreeIter iter;
	unsigned old_count = gtk_tree_model_iter_n_children (
		GTK_TREE_MODEL (saction->model), NULL);

	g_return_if_fail (saction != NULL);

	gtk_list_store_insert (saction->model, &iter, 0);
	gtk_list_store_set (saction->model, &iter,
		LABEL_COL,	label,
		KEY_COL,	key,
		-1);
	check_sensitivity (saction, old_count);
}

/**
 * go_action_combo_stack_pop :
 * @act : #GOActionComboStack
 * @n :
 *
 * Shorten list @act by removing @n off the top (or fewer if the list is
 * shorter)
 **/
void
go_action_combo_stack_pop (GOActionComboStack *a, unsigned n)
{
	GOActionComboStack *saction = GO_ACTION_COMBO_STACK (a);
	GtkTreeIter iter;
	unsigned old_count = gtk_tree_model_iter_n_children (
		GTK_TREE_MODEL (saction->model), NULL);

	g_return_if_fail (saction != NULL);

	if (gtk_tree_model_iter_nth_child (GTK_TREE_MODEL (saction->model), &iter, NULL, 0))
		while (n-- > 0 &&
		       gtk_list_store_remove (saction->model, &iter))
			;
	check_sensitivity (saction, old_count);
}

/**
 * go_action_combo_stack_truncate :
 * @act : #GOActionComboStack
 * @n : 
 *
 * Ensure that list @act is no longer than @n, dropping any extra off the
 * bottom.
 **/
void
go_action_combo_stack_truncate (GOActionComboStack *a, unsigned n)
{
	GOActionComboStack *saction = GO_ACTION_COMBO_STACK (a);
	GtkTreeIter iter;
	unsigned old_count = gtk_tree_model_iter_n_children (
		GTK_TREE_MODEL (saction->model), NULL);

	g_return_if_fail (saction != NULL);

	if (gtk_tree_model_iter_nth_child (GTK_TREE_MODEL (saction->model), &iter, NULL, n))
		while (gtk_list_store_remove (saction->model, &iter))
			;
	check_sensitivity (saction, old_count);
}
