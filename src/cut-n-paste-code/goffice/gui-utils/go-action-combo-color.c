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
#include <src/widgets/gnm-combo-box.h>
#include <src/widgets/color-palette.h>
#include <src/gui-util.h>
#include <application.h>

#include <gtk/gtkaction.h>
#include <gtk/gtktoolitem.h>
#include <gtk/gtkimagemenuitem.h>
#include <gtk/gtkimage.h>
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
	gnm_combo_box_set_tooltip (GNM_COMBO_BOX (self->combo), tooltips,
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
	char const 	*default_val_label;
	GOColor		 default_val, current_color;
};
typedef GtkActionClass GOActionComboColorClass;

static GObjectClass *combo_color_parent;
static void
go_action_combo_color_connect_proxy (GtkAction *a, GtkWidget *proxy)
{
	GTK_ACTION_CLASS (combo_color_parent)->connect_proxy (a, proxy);

	if (GTK_IS_IMAGE_MENU_ITEM (proxy)) { /* set the icon */
		GOActionComboColor *caction = (GOActionComboColor *)a;
		GtkWidget *image = gtk_image_new_from_pixbuf (caction->icon);
		gtk_widget_show (image);
		gtk_image_menu_item_set_image (
			GTK_IMAGE_MENU_ITEM (proxy), image);
	}
}

static void
cb_color_changed (GtkWidget *cc, GdkColor const *c,
		  gboolean is_custom, gboolean by_user, gboolean is_default,
		  GOActionComboColor *caction)
{
	if (!by_user)
		return;
	caction->current_color = is_default
		? caction->default_val : GDK_TO_UINT (*c);
	gtk_action_activate (GTK_ACTION (caction));
}

static GtkWidget *
go_action_combo_color_create_tool_item (GtkAction *a)
{
	GOActionComboColor *caction = (GOActionComboColor *)a;
	GOToolComboColor *tool = g_object_new (GO_TOOL_COMBO_COLOR_TYPE, NULL);
	GdkColor gdk_default;

	tool->combo = (ColorCombo *)color_combo_new (caction->icon,
		caction->default_val_label,
		go_color_to_gdk	(caction->default_val, &gdk_default),
		caction->color_group);

	color_combo_set_instant_apply (COLOR_COMBO (tool->combo), TRUE);
	gnm_combo_box_set_relief (GNM_COMBO_BOX (tool->combo), GTK_RELIEF_NONE);
	gnm_combo_box_set_tearable (GNM_COMBO_BOX (tool->combo), TRUE);
	gnm_widget_disable_focus (GTK_WIDGET (tool->combo));
	gtk_container_add (GTK_CONTAINER (tool), GTK_WIDGET (tool->combo));
	gtk_widget_show (GTK_WIDGET (tool->combo));
	gtk_widget_show (GTK_WIDGET (tool));

	g_signal_connect (G_OBJECT (tool->combo),
		"color_changed",
		G_CALLBACK (cb_color_changed), a);
	return GTK_WIDGET (tool);
}

static GtkWidget *
go_action_combo_color_create_menu_item (GtkAction *a)
{
	GOActionComboColor *caction = (GOActionComboColor *)a;
	GdkColor gdk_default;
	GtkWidget *submenu = color_palette_make_menu (
		caction->default_val_label,
		go_color_to_gdk	(caction->default_val, &gdk_default),
		caction->color_group);
	GtkWidget *item = gtk_image_menu_item_new ();
	gtk_menu_item_set_submenu (GTK_MENU_ITEM (item), submenu);
	gtk_widget_show (submenu);

	g_signal_connect (G_OBJECT (submenu),
		"color_changed",
		G_CALLBACK (cb_color_changed), a);
	return item;
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
	gtk_act_klass->create_menu_item = go_action_combo_color_create_menu_item;
	gtk_act_klass->connect_proxy	= go_action_combo_color_connect_proxy;
}

GSF_CLASS (GOActionComboColor, go_action_combo_color,
	   go_action_combo_color_class_init, NULL,
	   GTK_TYPE_ACTION)

GOActionComboColor *
go_action_combo_color_new (char const  *action_name,
			   char const  *stock_id,
			   char const  *default_color_label,
			   GOColor	default_color,
			   gpointer	group_key)
{
	GOActionComboColor *res = g_object_new (go_action_combo_color_get_type (),
					   "name", action_name,
					   "stock_id", stock_id,
					   NULL);
	res->icon = gnm_app_get_pixbuf (stock_id);
	res->color_group = color_group_fetch (action_name, group_key);
	res->default_val_label = g_strdup (default_color_label);
	res->current_color = res->default_val = default_color;

	return res;
}

void
go_action_combo_color_set_group (GOActionComboColor *action, gpointer group_key)
{
#warning TODO
}

GOColor
go_action_combo_color_get_color (GOActionComboColor *a, gboolean *is_default)
{
	if (is_default != NULL)
		*is_default = (a->current_color == a->default_val);
	return a->current_color;
}

void
go_action_combo_color_set_color (GOActionComboColor *a, GOColor color)
{
#warning TODO
}
