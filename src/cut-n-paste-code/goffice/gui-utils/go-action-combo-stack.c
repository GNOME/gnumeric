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
#if 0
static void
go_tool_combo_stack_finalize (GObject *obj)
{
}
static gboolean
go_tool_combo_stack_create_menu_proxy (GtkToolItem *tool_item)
{
}
static gboolean
go_tool_combo_stack_set_tooltip (GtkToolItem *tool_item, GtkTooltips *tooltips,
				 char const *tip_text,
				 char const *tip_private)
{
}
#endif
static void
go_tool_combo_stack_class_init (GtkToolItemClass *tool_item_klass)
{
#if 0
	gobject_klass->finalize		   = go_tool_combo_stack_finalize;
	tool_item_klass->create_menu_proxy = go_tool_combo_stack_create_menu_proxy;
	tool_item_klass->set_tooltip	   = go_tool_combo_stack_set_tooltip;
#endif
}

static GSF_CLASS (GOToolComboStack, go_tool_combo_stack,
	   go_tool_combo_stack_class_init, NULL,
	   GTK_TYPE_TOOL_ITEM)

/*****************************************************************************/

struct _GOActionComboStack {
	GtkAction	base;
};

typedef struct {
	GtkActionClass	base;
} GOActionComboStackClass;

static GObjectClass *combo_stack_parent;
#if 0
static void
go_action_combo_stack_connect_proxy (GtkAction *action, GtkWidget *proxy)
{
}

static void
go_action_combo_stack_disconnect_proxy (GtkAction *action, GtkWidget *proxy)
{
}
#endif

static GtkWidget *
go_action_combo_stack_create_tool_item (GtkAction *act)
{
	char const *id;
	GOActionComboStack *caction = (GOActionComboStack *)act;
	GOToolComboStack *tool = g_object_new (GO_TOOL_COMBO_STACK_TYPE, NULL);

	g_object_get (G_OBJECT (act), "stock_id", &id, NULL);
	tool->combo = (GnmComboStack *) gnm_combo_stack_new (id, TRUE);

	gnm_widget_disable_focus (GTK_WIDGET (tool->combo));
	gtk_container_add (GTK_CONTAINER (tool), GTK_WIDGET (tool->combo));
	gtk_widget_show (GTK_WIDGET (tool->combo));
	gtk_widget_show (GTK_WIDGET (tool));
	return GTK_WIDGET (tool);
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
	gobject_klass->finalize		= go_action_combo_stack_finalize;

	gtk_act_klass->create_tool_item = go_action_combo_stack_create_tool_item;
#if 0
	gtk_act_klass->create_menu_item = Use the default
	gtk_act_klass->connect_proxy	= go_action_combo_stack_connect_proxy;
	gtk_act_klass->disconnect_proxy = go_action_combo_stack_disconnect_proxy;
#endif
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
go_action_combo_stack_push (GOActionComboStack *act, char const *str)
{
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
go_action_combo_stack_pop (GOActionComboStack *act, unsigned n)
{
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
go_action_combo_stack_trunc (GOActionComboStack *act, unsigned n)
{
}
#if 0
static void
cb_proxy_push (GtkAction *act, char const *str, GnmComboStack *proxy)
{
	gnm_combo_stack_push_item (proxy, str);
}

static void
cb_proxy_pop (GtkAction *act, unsigned n, GnmComboStack *proxy)
{
	gnm_combo_stack_remove_top (proxy, n);
}

static void
cb_proxy_truncate (GtkAction *act, unsigned n, GnmComboStack *proxy)
{
	gnm_combo_stack_truncate (proxy, n);
}

#endif
