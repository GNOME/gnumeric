/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * gog-legend.c :
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
#include <goffice/graph/gog-legend.h>
#include <goffice/graph/gog-styled-object.h>
#include <goffice/graph/gog-view.h>
#include <goffice/graph/gog-renderer.h>
#include <goffice/graph/gog-chart.h>
#include <goffice/graph/gog-style.h>
#include <goffice/utils/go-color.h>
#include <goffice/utils/go-units.h>

#include <gsf/gsf-impl-utils.h>
#include <src/gnumeric-i18n.h>

static GType gog_legend_view_get_type (void);

struct _GogLegend {
	GogStyledObject	base;
};

typedef struct {
	GogStyledObjectClass	base;
} GogLegendClass;

enum {
	LEGEND_PROP_0,
};

static GObjectClass *parent_klass;

static void
gog_legend_set_property (GObject *obj, guint param_id,
			    GValue const *value, GParamSpec *pspec)
{
	/* GogLegend *legend = GOG_LEGEND (obj); */

	switch (param_id) {

	default: G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, param_id, pspec);
		 return; /* NOTE : RETURN */
	}
}

static void
gog_legend_get_property (GObject *obj, guint param_id,
			    GValue *value, GParamSpec *pspec)
{
	/* GogLegend *legend = GOG_LEGEND (obj); */

	switch (param_id) {

	default: G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, param_id, pspec);
		 break;
	}
}

static void
gog_legend_finalize (GObject *obj)
{
	/* GogLegend *legend = GOG_LEGEND (obj); */

	if (parent_klass != NULL && parent_klass->finalize != NULL)
		(parent_klass->finalize) (obj);
}

static char const *
gog_legend_type_name (GogObject const *item)
{
	return "Legend";
}

static void
gog_legend_class_init (GogLegendClass *klass)
{
	GObjectClass *gobject_klass   = (GObjectClass *) klass;
	GogObjectClass *gog_klass = (GogObjectClass *) klass;

	parent_klass = g_type_class_peek_parent (klass);
	gobject_klass->set_property = gog_legend_set_property;
	gobject_klass->get_property = gog_legend_get_property;
	gobject_klass->finalize	    = gog_legend_finalize;

	gog_klass->type_name	= gog_legend_type_name;
	gog_klass->view_type	= gog_legend_view_get_type ();
}

static void
gog_legend_init (GogLegend *legend)
{
}

GSF_CLASS (GogLegend, gog_legend,
	   gog_legend_class_init, gog_legend_init,
	   GOG_STYLED_OBJECT_TYPE)

typedef GogView		GogLegendView;
typedef GogViewClass	GogLegendViewClass;

#define GOG_LEGEND_VIEW_TYPE	(gog_legend_view_get_type ())
#define GOG_LEGEND_VIEW(o)	(G_TYPE_CHECK_INSTANCE_CAST ((o), GOG_LEGEND_VIEW_TYPE, GogLegendView))
#define IS_GOG_LEGEND_VIEW(o)	(G_TYPE_CHECK_INSTANCE_TYPE ((o), GOG_LEGEND_VIEW_TYPE))

static GogViewClass *cview_parent_klass;

static void
gog_legend_view_size_request (GogView *view, GogViewRequisition *req)
{
	req->w = 50.;
	req->h = 15 * gog_chart_get_carnality (GOG_CHART (view->model->parent));
}

typedef struct {
	GogView const *view;
	GogViewAllocation swatch;
} render_closure;

static void
cb_render_elements (unsigned i, GogStyle const *base_style, char const *name,
		    render_closure *data)
{
	GogViewAllocation swatch = data->swatch;
	GogStyle *style;
	
	if (!gog_style_has_marker (style)) {
		style = gog_style_dup (base_style);
		style->outline.width = 0; /* hairline */
		style->outline.color = RGBA_BLACK;
	} else
		style = (GogStyle *)base_style;

	swatch.y += i * 15.;
	gog_renderer_push_style (data->view->renderer, style);
	gog_renderer_draw_rectangle (data->view->renderer, &swatch);
	gog_renderer_pop_style (data->view->renderer);

	if (style != base_style)
		g_object_unref (style);
}

static void
gog_legend_view_render (GogView *view, GogViewAllocation const *bbox)
{
	render_closure closure;
	GogLegend *legend = GOG_LEGEND (view->model);
	double outline = gog_renderer_outline_size (view->renderer,
						    legend->base.style);

	gog_renderer_push_style (view->renderer, legend->base.style);
	gog_renderer_draw_rectangle (view->renderer, &view->allocation);
	gog_renderer_pop_style (view->renderer);

	closure.view = view;
#warning TODO convert to pts and use the renderer scaling routines
	closure.swatch.x = view->allocation.x + outline + 2;
	closure.swatch.y = view->allocation.y + outline + 2;
	closure.swatch.w = closure.swatch.h = 10.;
	g_warning ("1\" x == %g pixels", gog_renderer_pt2r_x (view->renderer, GO_IN_TO_PT (1)));
	g_warning ("1\" y == %g pixels", gog_renderer_pt2r_y (view->renderer, GO_IN_TO_PT (1)));
	gog_chart_foreach_elem (GOG_CHART (view->model->parent),
		(GogEnumFunc) cb_render_elements, &closure);

	(cview_parent_klass->render) (view, bbox);
}

static void
gog_legend_view_class_init (GogLegendViewClass *gview_klass)
{
	GogViewClass *view_klass    = (GogViewClass *) gview_klass;

	cview_parent_klass = g_type_class_peek_parent (gview_klass);
	view_klass->size_request    = gog_legend_view_size_request;
	view_klass->render	    = gog_legend_view_render;
}

static GSF_CLASS (GogLegendView, gog_legend_view,
		  gog_legend_view_class_init, NULL,
		  GOG_VIEW_TYPE)
