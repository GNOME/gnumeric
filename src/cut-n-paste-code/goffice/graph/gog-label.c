/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * gog-label.c
 *
 * Copyright (C) 2003 Jody Goldberg (jody@gnome.org)
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
#include <goffice/graph/gog-label.h>
#include <goffice/graph/gog-styled-object.h>
#include <goffice/graph/gog-style.h>
#include <goffice/graph/gog-view.h>
#include <goffice/graph/gog-renderer.h>
#include <goffice/graph/gog-data-allocator.h>
#include <goffice/graph/go-data.h>

#include <src/gui-util.h>
#include <src/gnumeric-i18n.h>

#include <gsf/gsf-impl-utils.h>

struct _GogLabel {
	GogStyledObject	base;

	GODataScalar	*text;
	gboolean	 allow_markup;
};
typedef GogStyledObjectClass GogLabelClass;

enum {
	LABEL_PROP_0,
	LABEL_PROP_ALLOW_MARKUP,
};

static GType gog_label_view_get_type (void);
static GObjectClass *label_parent_klass;
static GogViewClass *lview_parent_klass;

static void
gog_label_set_property (GObject *obj, guint param_id,
			     GValue const *value, GParamSpec *pspec)
{
	GogLabel *label = GOG_LABEL (obj);

	switch (param_id) {
	case LABEL_PROP_ALLOW_MARKUP :
		label->allow_markup = g_value_get_boolean (value);
		break;

	default: G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, param_id, pspec);
		 return; /* NOTE : RETURN */
	}
	gog_object_emit_changed (GOG_OBJECT (obj), FALSE);
}

static void
gog_label_get_property (GObject *obj, guint param_id,
			     GValue *value, GParamSpec *pspec)
{
	GogLabel *label = GOG_LABEL (obj);

	switch (param_id) {
	case LABEL_PROP_ALLOW_MARKUP :
		g_value_set_boolean (value, label->allow_markup);
		break;

	default: G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, param_id, pspec);
		 break;
	}
}

static void
gog_label_finalize (GObject *obj)
{
	GogLabel *label = GOG_LABEL (obj);

	if (label->text != NULL) {
		g_object_unref (label->text);
		label->text = NULL;
	}
	if (label_parent_klass->finalize != NULL)
		(label_parent_klass->finalize) (obj);
}

static char const *
gog_label_type_name (GogObject const *item)
{
	return "GraphLabel";
}
static gpointer
gog_label_editor (GogObject *gobj, GogDataAllocator *dalloc, CommandContext *cc)
{
	GtkWidget *vbox = gtk_vbox_new (FALSE, 5);
	GtkWidget *hbox = gtk_hbox_new (FALSE, 5);

	gtk_box_pack_start (GTK_BOX (hbox), 
		gtk_label_new_with_mnemonic (_("_Text:")), TRUE, TRUE, 0);
	gtk_box_pack_start (GTK_BOX (hbox), 
		gog_data_allocator_editor (dalloc, GOG_DATASET (gobj), 0), TRUE, TRUE, 0);
	gtk_widget_show (hbox);
	gtk_box_pack_start (GTK_BOX (vbox), hbox, TRUE, TRUE, 0);
	gtk_box_pack_start (GTK_BOX (vbox), 
		    gog_style_editor (gobj, cc, GOG_STYLE_OUTLINE | GOG_STYLE_FILL | GOG_STYLE_FONT),
		    TRUE, TRUE, 0);
	return vbox;
}

static void
gog_label_class_init (GogLabelClass *klass)
{
	GObjectClass *gobject_klass = (GObjectClass *) klass;
	GogObjectClass *gog_klass = (GogObjectClass *) klass;

	label_parent_klass = g_type_class_peek_parent (klass);
	gobject_klass->set_property = gog_label_set_property;
	gobject_klass->get_property = gog_label_get_property;
	gobject_klass->finalize	    = gog_label_finalize;
	g_object_class_install_property (gobject_klass, LABEL_PROP_ALLOW_MARKUP,
		g_param_spec_boolean ("allow-markup", "allow-markup",
			"Support basic html-ish markup",
			TRUE, G_PARAM_READWRITE|GOG_PARAM_PERSISTENT));

	gog_klass->editor	= gog_label_editor;
	gog_klass->type_name	= gog_label_type_name;
	gog_klass->view_type	= gog_label_view_get_type ();
}

static void
gog_label_dims (GogDataset const *set, int *first, int *last)
{
	*first = *last = 0;
}

static GOData *
gog_label_get_dim (GogDataset const *set, int dim_i)
{
	GogLabel const *label = GOG_LABEL (set);
	return GO_DATA (label->text);
}

static void
gog_label_set_dim (GogDataset *set, int dim_i, GOData *val, GError **err)
{
	GogLabel *label = GOG_LABEL (set);
	if (val != NULL)
		g_object_ref (val);
	if (label->text != NULL)
		g_object_unref (label->text);
	label->text = GO_DATA_SCALAR (val);
	gog_object_request_update (GOG_OBJECT (label));
}

static void
gog_label_dataset_init (GogDatasetClass *iface)
{
	iface->dims	= gog_label_dims;
	iface->get_dim	= gog_label_get_dim;
	iface->set_dim	= gog_label_set_dim;
}

GSF_CLASS_FULL (GogLabel, gog_label,
		gog_label_class_init, NULL,
		GOG_STYLED_OBJECT_TYPE, 0,
		GSF_INTERFACE (gog_label_dataset_init, GOG_DATASET_TYPE))

/************************************************************************/

typedef GogView		GogLabelView;
typedef GogViewClass	GogLabelViewClass;

#define GOG_LABEL_VIEW_TYPE	(gog_label_view_get_type ())
#define GOG_LABEL_VIEW(o)	(G_TYPE_CHECK_INSTANCE_CAST ((o), GOG_LABEL_VIEW_TYPE, GogLabelView))
#define IS_GOG_LABEL_VIEW(o)	(G_TYPE_CHECK_INSTANCE_TYPE ((o), GOG_LABEL_VIEW_TYPE))

static void
gog_label_view_size_request (GogView *view, GogViewRequisition *req)
{
	req->w = req->h = 1.;
}

static void
gog_label_view_size_allocate (GogView *view, GogViewAllocation const *a)
{
	GogLabel *label = GOG_LABEL (view->model);
	GogViewAllocation res = *a;
	double outline = gog_renderer_outline_size (
		view->renderer, label->base.style);

	res.x += outline;
	res.y += outline;
	res.w -= outline * 2.;
	res.h -= outline * 2.;
	(lview_parent_klass->size_allocate) (view, &res);
}

static void
gog_label_view_render (GogView *view, GogViewAllocation const *bbox)
{
	GogLabel *label = GOG_LABEL (view->model);

	gog_renderer_push_style (view->renderer, label->base.style);
	gog_renderer_draw_rectangle (view->renderer, &view->allocation);


	if (label->text != NULL) {
		char const *text = go_data_scalar_get_str (label->text);
		if (text != NULL) {
			ArtPoint  point;
			point.x = view->residual.x;
			point.y = view->residual.y;
			gog_renderer_draw_text (view->renderer, &point, text, NULL);
		}
	}
	gog_renderer_pop_style (view->renderer);
	(lview_parent_klass->render) (view, bbox);
}

static void
gog_label_view_class_init (GogLabelViewClass *gview_klass)
{
	GogViewClass *view_klass    = (GogViewClass *) gview_klass;

	lview_parent_klass = g_type_class_peek_parent (gview_klass);
	view_klass->size_request    = gog_label_view_size_request;
	view_klass->size_allocate   = gog_label_view_size_allocate;
	view_klass->render	    = gog_label_view_render;
}

static GSF_CLASS (GogLabelView, gog_label_view,
	   gog_label_view_class_init, NULL,
	   GOG_VIEW_TYPE)
