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
#include <src/widgets/gnm-combo-stack.h>
#include <src/gui-util.h>

#include <gtk/gtkaction.h>
#include <gtk/gtktoolitem.h>
#include <gtk/gtkimagemenuitem.h>
#include <gsf/gsf-impl-utils.h>
#include <glib/gi18n.h>

typedef struct {
	GtkToolItem	 base;
	GnmComboStack	*combo; /* container has a ref, not us */
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
	gtk_tooltips_set_tip (tooltips, gnm_combo_stack_get_button (GNM_COMBO_STACK (self->combo)),
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
	GtkAction	base;
};

typedef GtkActionClass GOActionComboStackClass;

static GObjectClass *combo_stack_parent;

static GtkWidget *
go_action_combo_stack_create_tool_item (GtkAction *act)
{
	char const *id;
	GOToolComboStack *tool = g_object_new (GO_TOOL_COMBO_STACK_TYPE, NULL);

	g_object_get (G_OBJECT (act), "stock_id", &id, NULL);
	tool->combo = (GnmComboStack *) gnm_combo_stack_new (id, TRUE);

	gnm_widget_disable_focus (GTK_WIDGET (tool->combo));
	gtk_container_add (GTK_CONTAINER (tool), GTK_WIDGET (tool->combo));
	gtk_widget_show (GTK_WIDGET (tool->combo));
	gtk_widget_show (GTK_WIDGET (tool));
	return GTK_WIDGET (tool);
}

static GtkWidget *
go_action_combo_stack_create_menu_item (GtkAction *a)
{
	GOActionComboStack *saction = (GOActionComboStack *)a;
	GtkWidget *item = gtk_image_menu_item_new_with_label ("UNDOREDO");
	return item;
}

static void
go_action_combo_stack_finalize (GObject *obj)
{
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

GSF_CLASS (GOActionComboStack, go_action_combo_stack,
	   go_action_combo_stack_class_init, NULL,
	   GTK_TYPE_ACTION)

/**
 * go_action_combo_stack_push :
 * @act : #GOActionComboStack
 * @str : The label to push
 **/
void
go_action_combo_stack_push (GOActionComboStack *a, char const *str)
{
	GSList *p;

	for (p = gtk_action_get_proxies (GTK_ACTION (a)); p != NULL ; p = p->next)
		if (IS_GNM_COMBO_STACK (p->data))
			gnm_combo_stack_push (GNM_COMBO_STACK (p->data), str);
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
	GSList *p;

	for (p = gtk_action_get_proxies (GTK_ACTION (a)); p != NULL ; p = p->next)
		if (IS_GNM_COMBO_STACK (p->data))
			gnm_combo_stack_pop (GNM_COMBO_STACK (p->data), n);
}

/**
 * go_action_combo_stack_trunc :
 * @act : #GOActionComboStack
 * @n : 
 *
 * Ensure that list @act is no longer than @n, dropping any extra off the
 * bottom.
 **/
void
go_action_combo_stack_trunc (GOActionComboStack *a, unsigned n)
{
	GSList *p;

	for (p = gtk_action_get_proxies (GTK_ACTION (a)); p != NULL ; p = p->next)
		if (IS_GNM_COMBO_STACK (p->data))
			gnm_combo_stack_truncate (GNM_COMBO_STACK (p->data), n);
}
