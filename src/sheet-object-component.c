/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * sheet-object-component.c
 *
 * Copyright (C) 2008 Jean Bréfort <jean.brefort@normalesup.org>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
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
#include "gnumeric.h"
#include "application.h"
#include "gnm-pane-impl.h"
#include "gui-util.h"
#include "sheet-control-gui.h"
#include "sheet-object-component.h"
#include "sheet-object-impl.h"
#include "wbc-gtk.h"
#include <goffice/goffice.h>
#include <goffice/component/go-component.h>
#include <gsf/gsf-impl-utils.h>
#include <glib/gi18n-lib.h>


static void
so_component_view_set_bounds (SheetObjectView *sov, double const *coords, gboolean visible)
{
	GocItem *view = GOC_ITEM (GOC_GROUP (sov)->children->data);
	double scale = goc_canvas_get_pixels_per_unit (view->canvas);

	if (visible) {
		GOComponent *component = sheet_object_component_get_component (sheet_object_view_get_so (sov));
		double width, height;
		goc_item_set (GOC_ITEM (sov),
			"x", MIN (coords [0], coords[2]) / scale,
			"y", MIN (coords [3], coords[1]) / scale,
			NULL);
		if (component && ! go_component_is_resizable (component)) {
			go_component_get_size (component, &width, &height);
			goc_item_set (view,
				"width", width * gnm_app_display_dpi_get (TRUE),
				"height", height * gnm_app_display_dpi_get (FALSE),
				NULL);
		} else
			goc_item_set (view,
				"width", (fabs (coords [2] - coords [0]) + 1.) / scale,
				"height", (fabs (coords [3] - coords [1]) + 1.) / scale,
				NULL);

		goc_item_show (view);
	} else
		goc_item_hide (view);
}

typedef SheetObjectView		SOComponentGocView;
typedef SheetObjectViewClass	SOComponentGocViewClass;

static void
so_component_goc_view_class_init (SheetObjectViewClass *sov_klass)
{
	sov_klass->set_bounds	= so_component_view_set_bounds;
}

static GSF_CLASS (SOComponentGocView, so_component_goc_view,
	so_component_goc_view_class_init, NULL,
	SHEET_OBJECT_VIEW_TYPE)


/****************************************************************************/
#define SHEET_OBJECT_CONFIG_KEY "sheet-object-component-key"

#define SHEET_OBJECT_COMPONENT_CLASS(k) (G_TYPE_CHECK_CLASS_CAST ((k), SHEET_OBJECT_COMPONENT_TYPE, SheetObjectComponentClass))

typedef struct {
	SheetObject  base;
	GOComponent	*component;
	guint changed_signal;
} SheetObjectComponent;
typedef SheetObjectClass SheetObjectComponentClass;

static GObjectClass *parent_klass;

static void
gnm_soc_finalize (GObject *obj)
{
	SheetObjectComponent *soc = SHEET_OBJECT_COMPONENT (obj);

	g_object_unref (soc->component);

	parent_klass->finalize (obj);
}

static SheetObjectView *
gnm_soc_new_view (SheetObject *so, SheetObjectViewContainer *container)
{
	GnmPane *pane = GNM_PANE (container);
	SheetObjectComponent *soc = SHEET_OBJECT_COMPONENT (so);
	GocItem *view = goc_item_new (pane->object_views,
		so_component_goc_view_get_type (),
		NULL);
	goc_item_hide (goc_item_new (GOC_GROUP (view),
		      GOC_TYPE_COMPONENT,
		      "object", soc->component,
		      NULL));
	return gnm_pane_object_register (so, view, TRUE);
}

static GtkTargetList *
gnm_soc_get_target_list (SheetObject const *so)
{
	GtkTargetList *tl = gtk_target_list_new (NULL, 0);
	return tl;
}

static GtkTargetList *
gnm_soc_get_object_target_list (SheetObject const *so)
{
	GtkTargetList *tl = gtk_target_list_new (NULL, 0);
	return tl;
}

static void
gnm_soc_write_image (SheetObject const *so, char const *format, double resolution,
		     GsfOutput *output, GError **err)
{
}

static void
soc_cb_save_as (SheetObject *so, SheetControl *sc)
{
}

static void
gnm_soc_populate_menu (SheetObject *so, GPtrArray *actions)
{
	static SheetObjectAction const soc_actions[] = {
		{ GTK_STOCK_SAVE_AS, N_("_Save as Image"), NULL, 0, soc_cb_save_as }
	};

	unsigned int i;

	SHEET_OBJECT_CLASS (parent_klass)->populate_menu (so, actions);

	for (i = 0; i < G_N_ELEMENTS (soc_actions); i++)
		go_ptr_array_insert (actions, (gpointer) (soc_actions + i), 1 + i);
}

static void
gnm_soc_write_object (SheetObject const *so, char const *format,
		      GsfOutput *output, GError **err,
		      GnmConventions const *convs)
{
}

static void
gnm_soc_write_xml_sax (SheetObject const *so, GsfXMLOut *output,
		       GnmConventions const *convs)
{
	SheetObjectComponent const *soc = SHEET_OBJECT_COMPONENT (so);
	go_component_write_xml_sax (soc->component, output);
}

static void
soc_xml_finish (GOComponent *component, SheetObject *so)
{
	sheet_object_component_set_component (so, component);
}

static void
gnm_soc_prep_sax_parser (SheetObject *so, GsfXMLIn *xin, xmlChar const **attrs,
			 GnmConventions const *convs)
{
	go_component_sax_push_parser (xin, attrs,
				    (GOComponentSaxHandler) soc_xml_finish, so);
}

static void
gnm_soc_copy (SheetObject *dst, SheetObject const *src)
{
}

static void
gnm_soc_default_size (SheetObject const *so, double *w, double *h)
{
	SheetObjectComponent const *soc = SHEET_OBJECT_COMPONENT (so);
	if (soc->component && !go_component_is_resizable (soc->component)) {
		go_component_get_size (soc->component, w, h);
		*w = GO_IN_TO_PT (*w);
		*h = GO_IN_TO_PT (*h);
	} else {
		*w = GO_CM_TO_PT ((double)5);
		*h = GO_CM_TO_PT ((double)5);
	}
}

static void
gnm_soc_user_config (SheetObject *so, SheetControl *sc)
{
	SheetObjectComponent *soc = SHEET_OBJECT_COMPONENT (so);
	GtkWidget *w;
	g_return_if_fail (soc && soc->component);

	w = (GtkWidget *) go_component_edit (soc->component);
	if (w)
		wbc_gtk_attach_guru (scg_wbcg (SHEET_CONTROL_GUI (sc)), w);
}

static void
gnm_soc_draw_cairo (SheetObject const *so, cairo_t *cr,
			  double width, double height)
{
	SheetObjectComponent *soc = SHEET_OBJECT_COMPONENT (so);
	g_return_if_fail (soc && soc->component);

	go_component_render (soc->component, cr, width, height);
}

static void
gnm_soc_class_init (GObjectClass *klass)
{
	SheetObjectClass	*so_class  = SHEET_OBJECT_CLASS (klass);

	parent_klass = g_type_class_peek_parent (klass);

	/* Object class method overrides */
	klass->finalize = gnm_soc_finalize;

	/* SheetObject class method overrides */
	so_class->new_view		= gnm_soc_new_view;
	so_class->populate_menu		= gnm_soc_populate_menu;
	so_class->write_xml_sax		= gnm_soc_write_xml_sax;
	so_class->prep_sax_parser	= gnm_soc_prep_sax_parser;
	so_class->copy			= gnm_soc_copy;
	so_class->user_config		= gnm_soc_user_config;
	so_class->default_size		= gnm_soc_default_size;
	so_class->draw_cairo		= gnm_soc_draw_cairo;

	so_class->rubber_band_directly = FALSE;
}

static void
gnm_soc_init (GObject *obj)
{
	SheetObject *so = SHEET_OBJECT (obj);
	so->anchor.base.direction = GOD_ANCHOR_DIR_DOWN_RIGHT;
}

static void
soc_imageable_init (SheetObjectImageableIface *soi_iface)
{
	soi_iface->get_target_list = gnm_soc_get_target_list;
	soi_iface->write_image	   = gnm_soc_write_image;
}

static void
soc_exportable_init (SheetObjectExportableIface *soe_iface)
{
	soe_iface->get_target_list = gnm_soc_get_object_target_list;
	soe_iface->write_object	   = gnm_soc_write_object;
}

GSF_CLASS_FULL (SheetObjectComponent, sheet_object_component,
		NULL, NULL, gnm_soc_class_init,NULL,
		gnm_soc_init, SHEET_OBJECT_TYPE, 0,
		GSF_INTERFACE (soc_imageable_init, SHEET_OBJECT_IMAGEABLE_TYPE) \
		GSF_INTERFACE (soc_exportable_init, SHEET_OBJECT_EXPORTABLE_TYPE));

/**
 * sheet_object_component_new :
 **/
SheetObject *
sheet_object_component_new (GOComponent *component)
{
	SheetObjectComponent *soc = g_object_new (SHEET_OBJECT_COMPONENT_TYPE, NULL);
	sheet_object_component_set_component (SHEET_OBJECT (soc), component);
	return SHEET_OBJECT (soc);
}

void
sheet_object_component_edit (WBCGtk *wbcg, GOComponent *component, GClosure *closure)
{
}

GOComponent*
sheet_object_component_get_component (SheetObject *soc)
{
	g_return_val_if_fail (IS_SHEET_OBJECT_COMPONENT (soc), NULL);

	return ((SheetObjectComponent *) soc)->component;
}

static void
component_changed_cb (SheetObject *so)
{
	if (!(so->flags & SHEET_OBJECT_CAN_RESIZE)) {
		GList *l = so->realized_list;
		double coords[4];
		g_object_get (l->data, "x", coords, "y", coords + 1, NULL);
		coords[2] = coords[3] = G_MAXDOUBLE;
		for (l = so->realized_list; l; l = l->next) {
			if (l->data)
				sheet_object_view_set_bounds (l->data, coords, so->flags & SHEET_OBJECT_IS_VISIBLE);
		}
		sheet_object_update_bounds (so, NULL);
	}
}

void
sheet_object_component_set_component (SheetObject *so, GOComponent *component)
{
	SheetObjectComponent *soc;

	g_return_if_fail (IS_SHEET_OBJECT_COMPONENT (so));
	soc = SHEET_OBJECT_COMPONENT (so);
	if (soc->component != NULL) {
		g_signal_handler_disconnect (soc->component, soc->changed_signal);
		g_object_unref (soc->component);
	}

	soc->component = component;
	if (component) {
		if (go_component_is_resizable (component))
			so->flags |= SHEET_OBJECT_CAN_RESIZE;
		else
			so->flags &= ~SHEET_OBJECT_CAN_RESIZE;
		if (go_component_is_editable (component))
			so->flags |= SHEET_OBJECT_CAN_EDIT;
		else
			so->flags &= ~SHEET_OBJECT_CAN_EDIT;
		g_signal_connect_swapped (soc->component, "changed", G_CALLBACK (component_changed_cb), soc);
	}
}
