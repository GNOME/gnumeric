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
#include <goffice/graph/gog-data-set.h>
#include <goffice/graph/gog-data-allocator.h>
#include <goffice/graph/go-data.h>

#include <src/gui-util.h>
#include <src/gnumeric-i18n.h>

#include <gsf/gsf-impl-utils.h>
#include <gtk/gtknotebook.h>
#include <gtk/gtklabel.h>
#include <gtk/gtkbox.h>

struct _GogLabel {
	GogStyledObject	base;

	GogDatasetElement text;
	gboolean	  allow_markup;
};
typedef GogStyledObjectClass GogLabelClass;

enum {
	LABEL_PROP_0,
	LABEL_PROP_ALLOW_MARKUP,
};

static GType gog_label_view_get_type (void);
static GObjectClass *label_parent_klass;
static GogViewClass *lview_parent_klass;

/* a property ? */
#define PAD_HACK	4	/* pts */

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
	gog_dataset_finalize (GOG_DATASET (obj));
	if (label_parent_klass->finalize != NULL)
		(label_parent_klass->finalize) (obj);
}

static gpointer
gog_label_editor (GogObject *gobj, GogDataAllocator *dalloc, GnmCmdContext *cc)
{
	GtkWidget *notebook = gtk_notebook_new ();
	GtkWidget *hbox = gtk_hbox_new (FALSE, 5);

	gtk_box_pack_start (GTK_BOX (hbox), 
		gtk_label_new_with_mnemonic (_("_Text:")), FALSE, TRUE, 6);
	gtk_box_pack_start (GTK_BOX (hbox), 
		gog_data_allocator_editor (dalloc, GOG_DATASET (gobj), 0, TRUE),
		TRUE, TRUE, 0);
	gtk_widget_show_all (hbox);
	gtk_notebook_prepend_page (GTK_NOTEBOOK (notebook), hbox,
		gtk_label_new (_("Data")));
	gog_style_editor (gobj, cc, notebook,
		GOG_STYLE_OUTLINE | GOG_STYLE_FILL | GOG_STYLE_FONT);
	return notebook;
}

static unsigned
gog_label_interesting_fields (GogStyledObject *obj)
{
	return GOG_STYLE_OUTLINE | GOG_STYLE_FILL | GOG_STYLE_FONT;
}

static void
gog_label_class_init (GogLabelClass *klass)
{
	GObjectClass *gobject_klass = (GObjectClass *) klass;
	GogObjectClass *gog_klass = (GogObjectClass *) klass;
	GogStyledObjectClass *style_klass = (GogStyledObjectClass *) klass;

	label_parent_klass = g_type_class_peek_parent (klass);
	gobject_klass->set_property = gog_label_set_property;
	gobject_klass->get_property = gog_label_get_property;
	gobject_klass->finalize	    = gog_label_finalize;
	g_object_class_install_property (gobject_klass, LABEL_PROP_ALLOW_MARKUP,
		g_param_spec_boolean ("allow-markup", "allow-markup",
			"Support basic html-ish markup",
			TRUE, G_PARAM_READWRITE|GOG_PARAM_PERSISTENT));

	gog_klass->editor	= gog_label_editor;
	gog_klass->view_type	= gog_label_view_get_type ();
	style_klass->interesting_fields = gog_label_interesting_fields;
}

static void
gog_label_dims (GogDataset const *set, int *first, int *last)
{
	*first = *last = 0;
}

static GogDatasetElement *
gog_label_get_elem (GogDataset const *set, int dim_i)
{
	GogLabel *label = GOG_LABEL (set);
	return &label->text;
}

static void
gog_label_dim_changed (GogDataset *set, int dim_i)
{
	gog_object_emit_changed (GOG_OBJECT (set), TRUE);
}

static void
gog_label_dataset_init (GogDatasetClass *iface)
{
	iface->dims	   = gog_label_dims;
	iface->get_elem	   = gog_label_get_elem;
	iface->dim_changed = gog_label_dim_changed;
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
gog_label_view_size_request (GogView *v, GogViewRequisition *req)
{
	GogLabel *l = GOG_LABEL (v->model);
	double outline = gog_renderer_line_size (
		v->renderer, l->base.style->outline.width);

	req->w = req->h = 0.;
	if (l->text.data != NULL) {
		char const *text = go_data_scalar_get_str (GO_DATA_SCALAR (l->text.data));
		if (text != NULL) {
			gog_renderer_push_style (v->renderer, l->base.style);
			gog_renderer_measure_text (v->renderer, text, req);
			gog_renderer_pop_style (v->renderer);
		}
	}
	if (outline > 0) {
		double pad_y = gog_renderer_pt2r_y (v->renderer, PAD_HACK);
		double pad_x = gog_renderer_pt2r_y (v->renderer, PAD_HACK);
		req->w += outline * 2 + pad_x;
		req->h += outline * 2 + pad_y;
	}
}

static void
gog_label_view_size_allocate (GogView *v, GogViewAllocation const *a)
{
	GogLabel *l = GOG_LABEL (v->model);
	GogViewAllocation res = *a;
	double outline = gog_renderer_line_size (
		v->renderer, l->base.style->outline.width);

	/* We only need internal padding if there is an outline */
	if (outline > 0) {
		double pad_x = gog_renderer_pt2r_x (v->renderer, PAD_HACK);
		double pad_y = gog_renderer_pt2r_y (v->renderer, PAD_HACK);

		res.x += outline + pad_x/2;
		res.y += outline + pad_y/2;
		res.w -= outline * 2. + pad_x;
		res.h -= outline * 2. + pad_y;
	}
	(lview_parent_klass->size_allocate) (v, &res);
}

static void
gog_label_view_render (GogView *view, GogViewAllocation const *bbox)
{
	GogLabel *l = GOG_LABEL (view->model);

	gog_renderer_push_style (view->renderer, l->base.style);
	gog_renderer_draw_rectangle (view->renderer, &view->allocation);

	if (l->text.data != NULL) {
		char const *text = go_data_scalar_get_str (GO_DATA_SCALAR (l->text.data));
		if (text != NULL)
			gog_renderer_draw_text (view->renderer, text,
				&view->residual, GTK_ANCHOR_NW, NULL);
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
