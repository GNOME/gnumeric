/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/*
 * go-action-combo-color.c: A custom GtkAction to handle color selection
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
#include "go-action-combo-color.h"
#include <src/widgets/widget-color-combo.h>
#include <src/gui-util.h>
#include <application.h>

#include <gtk/gtkaction.h>
#include <gtk/gtktoolitem.h>
#include <gsf/gsf-impl-utils.h>
#include <glib/gi18n.h>

typedef struct {
	GtkToolItem	 base;
	ColorCombo	*combo;	/* container has a ref, not us */
} GOToolComboColor;
typedef GtkToolItemClass GOToolComboColorClass;

#define GO_TOOL_COMBO_COLOR_TYPE	(go_tool_combo_color_get_type ())
#define GO_TOOL_COMBO_COLOR(o)		(G_TYPE_CHECK_INSTANCE_CAST (o, GO_TOOL_COMBO_COLOR_TYPE, GOToolComboColor))
#define IS_GO_TOOL_COMBO_COLOR(o)	(G_TYPE_CHECK_INSTANCE_TYPE (o, GO_TOOL_COMBO_COLOR_TYPE))

static GType go_tool_combo_color_get_type (void);
static gboolean
go_tool_combo_color_set_tooltip (GtkToolItem *tool_item, GtkTooltips *tooltips,
				 char const *tip_text,
				 char const *tip_private)
{
	GOToolComboColor *self = (GOToolComboColor *)tool_item;
	gtk_tooltips_set_tip (tooltips, self->combo->preview_button,
			      tip_text, tip_private);
	gtk_tooltips_set_tip (tooltips, gnm_combo_box_get_arrow	(GNM_COMBO_BOX (self->combo)),
			      tip_text, tip_private);
	return TRUE;
}
static void
go_tool_combo_color_class_init (GtkToolItemClass *tool_item_klass)
{
	tool_item_klass->set_tooltip = go_tool_combo_color_set_tooltip;
}

static GSF_CLASS (GOToolComboColor, go_tool_combo_color,
	   go_tool_combo_color_class_init, NULL,
	   GTK_TYPE_TOOL_ITEM)

/*****************************************************************************/

struct _GOActionComboColor {
	GtkAction	 base;
	GdkPixbuf	*icon;
	ColorGroup 	*color_group;
	char const 	*no_color_label;
	GdkColor const	*default_color;
};
typedef struct {
	GtkActionClass	base;
} GOActionComboColorClass;

static GObjectClass *combo_color_parent;
#if 0
static void
go_action_combo_color_connect_proxy (GtkAction *action, GtkWidget *proxy)
{
}

static void
go_action_combo_color_disconnect_proxy (GtkAction *action, GtkWidget *proxy)
{
}
#endif

static GtkWidget *
go_action_combo_color_create_tool_item (GtkAction *a)
{
	GOActionComboColor *caction = (GOActionComboColor *)a;
	GOToolComboColor *tool = g_object_new (GO_TOOL_COMBO_COLOR_TYPE, NULL);

	tool->combo = (ColorCombo *)color_combo_new (caction->icon,
		caction->no_color_label, caction->default_color,
		caction->color_group);

	gnm_widget_disable_focus (GTK_WIDGET (tool->combo));
	gtk_container_add (GTK_CONTAINER (tool), GTK_WIDGET (tool->combo));
	gtk_widget_show (GTK_WIDGET (tool->combo));
	gtk_widget_show (GTK_WIDGET (tool));
	return GTK_WIDGET (tool);
}

static void
go_action_combo_color_finalize (GObject *obj)
{
	GOActionComboColor *color = (GOActionComboColor *)obj;
	if (color->icon != NULL)
		g_object_unref (color->icon);
	if (color->color_group != NULL)
		g_object_unref (color->color_group);

	combo_color_parent->finalize (obj);
}

static void
go_action_combo_color_class_init (GtkActionClass *gtk_act_klass)
{
	GObjectClass *gobject_klass = (GObjectClass *)gtk_act_klass;

	combo_color_parent = g_type_class_peek_parent (gobject_klass);
	gobject_klass->finalize		= go_action_combo_color_finalize;

	gtk_act_klass->create_tool_item = go_action_combo_color_create_tool_item;
#if 0
	gtk_act_klass->create_menu_item = Use the default
	gtk_act_klass->connect_proxy	= go_action_combo_color_connect_proxy;
	gtk_act_klass->disconnect_proxy = go_action_combo_color_disconnect_proxy;
#endif
}

GSF_CLASS (GOActionComboColor, go_action_combo_color,
	   go_action_combo_color_class_init, NULL,
	   GTK_TYPE_ACTION)

GOActionComboColor *
go_action_combo_color_new (char const *action_name,
			   char const *stock_id,
			   char const *no_color_label,
			   GdkColor const *default_color,
			   gpointer group_key)
{
	GOActionComboColor *res = g_object_new (go_action_combo_color_get_type (),
					   "name", action_name,
					   NULL);
	res->icon = gnm_app_get_pixbuf (stock_id);
	res->color_group = color_group_fetch (action_name, group_key);
	res->no_color_label = g_strdup (no_color_label);
	res->default_color = default_color;

	return res;
}

void
go_action_combo_color_set_group (GOActionComboColor *action, gpointer group_key)
{
#warning TODO
}
