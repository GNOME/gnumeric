/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/*
 * go-action-combo-pixmaps.c: A custom GtkAction to chose among a set of images
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
#include "go-action-combo-pixmaps.h"
#include <src/widgets/widget-pixmap-combo.h>
#include <src/gui-util.h>

#include <gtk/gtkaction.h>
#include <gtk/gtktoolitem.h>
#include <gsf/gsf-impl-utils.h>
#include <glib/gi18n.h>

typedef struct {
	GtkToolItem	 base;
	PixmapCombo	*combo;	/* container has a ref, not us */
} GOToolComboPixmaps;
typedef GtkToolItemClass GOToolComboPixmapsClass;

#define GO_TOOL_COMBO_PIXMAPS_TYPE	(go_tool_combo_pixmaps_get_type ())
#define GO_TOOL_COMBO_PIXMAPS(o)	(G_TYPE_CHECK_INSTANCE_CAST (o, GO_TOOL_COMBO_PIXMAPS_TYPE, GOToolComboPixmaps))
#define IS_GO_TOOL_COMBO_PIXMAPS(o)	(G_TYPE_CHECK_INSTANCE_TYPE (o, GO_TOOL_COMBO_PIXMAPS_TYPE))

static GType go_tool_combo_pixmaps_get_type (void);
#if 0
static void
go_tool_combo_pixmaps_finalize (GObject *obj)
{
}
static gboolean
go_tool_combo_pixmaps_create_menu_proxy (GtkToolItem *tool_item)
{
}
static gboolean
go_tool_combo_pixmaps_set_tooltip (GtkToolItem *tool_item, GtkTooltips *tooltips,
				   char const *tip_text,
				   char const *tip_private)
{
}
#endif
static void
go_tool_combo_pixmaps_class_init (GtkToolItemClass *tool_item_klass)
{
#if 0
	gobject_klass->finalize		   = go_tool_combo_pixmaps_finalize;
	tool_item_klass->create_menu_proxy = go_tool_combo_pixmaps_create_menu_proxy;
	tool_item_klass->set_tooltip	   = go_tool_combo_pixmaps_set_tooltip;
#endif
}

static GSF_CLASS (GOToolComboPixmaps, go_tool_combo_pixmaps,
	   go_tool_combo_pixmaps_class_init, NULL,
	   GTK_TYPE_TOOL_ITEM)

/*****************************************************************************/

struct _GOActionComboPixmaps {
	GtkAction	base;
	PixmapComboElement const *elements;
	int ncols, nrows;
};
typedef struct {
	GtkActionClass	base;
} GOActionComboPixmapsClass;

static GObjectClass *combo_pixmaps_parent;
#if 0
static void
go_action_combo_pixmaps_connect_proxy (GtkAction *action, GtkWidget *proxy)
{
}

static void
go_action_combo_pixmaps_disconnect_proxy (GtkAction *action, GtkWidget *proxy)
{
}
#endif

static GtkWidget *
go_action_combo_pixmaps_create_tool_item (GtkAction *a)
{
	GOActionComboPixmaps *caction = (GOActionComboPixmaps *)a;
	GOToolComboPixmaps *tool = g_object_new (GO_TOOL_COMBO_PIXMAPS_TYPE, NULL);

	tool->combo = (PixmapCombo *)pixmap_combo_new (caction->elements,
		caction->ncols, caction->nrows, TRUE);

	gnm_widget_disable_focus (GTK_WIDGET (tool->combo));
	gtk_container_add (GTK_CONTAINER (tool), GTK_WIDGET (tool->combo));
	gtk_widget_show (GTK_WIDGET (tool->combo));
	gtk_widget_show (GTK_WIDGET (tool));
	return GTK_WIDGET (tool);
}

static void
go_action_combo_pixmaps_finalize (GObject *obj)
{
	combo_pixmaps_parent->finalize (obj);
}

static void
go_action_combo_pixmaps_class_init (GtkActionClass *gtk_act_klass)
{
	GObjectClass *gobject_klass = (GObjectClass *)gtk_act_klass;

	combo_pixmaps_parent = g_type_class_peek_parent (gobject_klass);
	gobject_klass->finalize		= go_action_combo_pixmaps_finalize;

	gtk_act_klass->create_tool_item = go_action_combo_pixmaps_create_tool_item;
#if 0
	gtk_act_klass->create_menu_item = Use the default
	gtk_act_klass->connect_proxy	= go_action_combo_pixmaps_connect_proxy;
	gtk_act_klass->disconnect_proxy = go_action_combo_pixmaps_disconnect_proxy;
#endif
}

GSF_CLASS (GOActionComboPixmaps, go_action_combo_pixmaps,
	   go_action_combo_pixmaps_class_init, NULL,
	   GTK_TYPE_ACTION)

GOActionComboPixmaps *
go_action_combo_pixmaps_new (char const *name,
		       PixmapComboElement const *elements, int ncols, int nrows)
{
	GOActionComboPixmaps *res = g_object_new (go_action_combo_pixmaps_get_type (),
					     "name", name,
					     NULL);
	res->elements = elements;
	res->ncols = ncols;
	res->nrows = nrows;
	return res;
}
