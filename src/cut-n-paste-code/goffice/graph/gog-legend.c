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

	double	 swatch_size_pts;
	double	 swatch_padding_pts;
	gulong	 chart_cardinality_handle;
	gulong	 chart_child_name_changed_handle;
	int	 cached_count;
	gboolean names_changed;
};

typedef struct {
	GogStyledObjectClass	base;
} GogLegendClass;

enum {
	LEGEND_PROP_0,
	LEGEND_SWATCH_SIZE_PTS,
	LEGEND_SWATCH_PADDING_PTS
};

static GObjectClass *parent_klass;

static void
gog_legend_set_property (GObject *obj, guint param_id,
			 GValue const *value, GParamSpec *pspec)
{
	GogLegend *legend = GOG_LEGEND (obj);

	switch (param_id) {
	case LEGEND_SWATCH_SIZE_PTS :
		legend->swatch_size_pts = g_value_get_double (value);
		break;
	case LEGEND_SWATCH_PADDING_PTS :
		legend->swatch_padding_pts = g_value_get_double (value);
		break;

	default: G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, param_id, pspec);
		 return; /* NOTE : RETURN */
	}
}

static void
gog_legend_get_property (GObject *obj, guint param_id,
			 GValue *value, GParamSpec *pspec)
{
	GogLegend *legend = GOG_LEGEND (obj);

	switch (param_id) {
	case LEGEND_SWATCH_SIZE_PTS :
		g_value_set_double (value, legend->swatch_size_pts);
		break;
	case LEGEND_SWATCH_PADDING_PTS :
		g_value_set_double (value, legend->swatch_padding_pts);
		break;

	default: G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, param_id, pspec);
		 break;
	}
}

static void
cb_chart_names_changed (GogLegend *legend)
{
	if (legend->names_changed)
		return;
	legend->names_changed = TRUE;
	gog_object_request_update (GOG_OBJECT (legend));
}

static void
gog_legend_parent_changed (GogObject *obj, gboolean was_set)
{
	GogObjectClass *gog_object_klass = GOG_OBJECT_CLASS (parent_klass);
	GogLegend *legend = GOG_LEGEND (obj);

	if (was_set) {
		if (legend->chart_cardinality_handle == 0)
			legend->chart_cardinality_handle =
				g_signal_connect_object (G_OBJECT (obj->parent),
					"notify::cardinality-valid",
					G_CALLBACK (gog_object_request_update),
					legend, G_CONNECT_SWAPPED);
		if (legend->chart_child_name_changed_handle == 0)
			legend->chart_child_name_changed_handle =
				g_signal_connect_object (G_OBJECT (obj->parent),
					"child-name-changed",
					G_CALLBACK (cb_chart_names_changed),
					legend, G_CONNECT_SWAPPED);
	} else {
		if (legend->chart_cardinality_handle != 0) {
			g_signal_handler_disconnect (G_OBJECT (obj->parent),
				legend->chart_cardinality_handle);
			legend->chart_cardinality_handle = 0;
		}
		if (legend->chart_child_name_changed_handle != 0) {
			g_signal_handler_disconnect (G_OBJECT (obj->parent),
				legend->chart_child_name_changed_handle);
			legend->chart_child_name_changed_handle = 0;
		}
	}

	gog_object_klass->parent_changed (obj, was_set);
}

static void
gog_legend_update (GogObject *obj)
{
	GogLegend *legend = GOG_LEGEND (obj);
	int i = gog_chart_get_cardinality (GOG_CHART (obj->parent));
	if (legend->cached_count != i)
		legend->cached_count = i;
	else if (!legend->names_changed)
		return;
	legend->names_changed = FALSE;
	gog_object_emit_changed	(obj, TRUE);
}

static void
gog_legend_class_init (GogLegendClass *klass)
{
	static GogObjectRole const roles[] = {
		{ N_("Title"), "GogLabel",
		  GOG_POSITION_COMPASS, GOG_POSITION_N|GOG_POSITION_ALIGN_CENTER, GOG_OBJECT_NAME_BY_ROLE,
		  NULL, NULL, NULL, NULL, NULL, NULL },
	};

	GObjectClass *gobject_klass   = (GObjectClass *) klass;
	GogObjectClass *gog_klass = (GogObjectClass *) klass;

	parent_klass = g_type_class_peek_parent (klass);
	gobject_klass->set_property = gog_legend_set_property;
	gobject_klass->get_property = gog_legend_get_property;

	gog_klass->parent_changed = gog_legend_parent_changed;
	gog_klass->update	  = gog_legend_update;
	gog_klass->view_type	  = gog_legend_view_get_type ();
	gog_object_register_roles (gog_klass, roles, G_N_ELEMENTS (roles));

	g_object_class_install_property (gobject_klass, LEGEND_SWATCH_SIZE_PTS,
		g_param_spec_double ("swatch_size_pts", "Swatch Size pts",
			"size of the swatches in pts.",
			0, G_MAXDOUBLE, 0, G_PARAM_READWRITE|GOG_PARAM_PERSISTENT));
	g_object_class_install_property (gobject_klass, LEGEND_SWATCH_PADDING_PTS,
		g_param_spec_double ("swatch_padding_pts", "Swatch Padding pts",
			"padding between the swatches in pts.",
			0, G_MAXDOUBLE, 0, G_PARAM_READWRITE|GOG_PARAM_PERSISTENT));
}

static void
gog_legend_init (GogLegend *legend)
{
	legend->swatch_size_pts = GO_CM_TO_PT (.25);
	legend->swatch_padding_pts = GO_CM_TO_PT (.25);
	legend->cached_count = 0;
}

GSF_CLASS (GogLegend, gog_legend,
	   gog_legend_class_init, gog_legend_init,
	   GOG_STYLED_OBJECT_TYPE)

typedef GogView		GogLegendView;
typedef GogViewClass	GogLegendViewClass;

#define GOG_LEGEND_VIEW_TYPE	(gog_legend_view_get_type ())
#define GOG_LEGEND_VIEW(o)	(G_TYPE_CHECK_INSTANCE_CAST ((o), GOG_LEGEND_VIEW_TYPE, GogLegendView))
#define IS_GOG_LEGEND_VIEW(o)	(G_TYPE_CHECK_INSTANCE_TYPE ((o), GOG_LEGEND_VIEW_TYPE))

static GogViewClass *lview_parent_klass;

static void
gog_legend_view_size_request (GogView *view, GogViewRequisition *req)
{
	GogLegend *legend = GOG_LEGEND (view->model);
	double outline = gog_renderer_outline_size (view->renderer,
						    legend->base.style);
	req->w = 2 * outline + gog_renderer_pt2r_x (view->renderer, GO_CM_TO_PT (2));
	req->h = 2 * outline +
		(gog_chart_get_cardinality (GOG_CHART (view->model->parent)) *
		 gog_renderer_pt2r_y (view->renderer,
			legend->swatch_size_pts + legend->swatch_padding_pts));
}

typedef struct {
	GogView const *view;
	GogViewAllocation swatch;
	double step;
	double base_line;
} render_closure;

static void
cb_render_elements (unsigned i, GogStyle const *base_style, char const *name,
		    render_closure *data)
{
	GogViewAllocation swatch = data->swatch;
	GogStyle *style = NULL;
	ArtPoint pos;
	
	if (!gog_style_has_marker (base_style)) {
		style = gog_style_dup (base_style);
		style->outline.width = 0; /* hairline */
		style->outline.color = RGBA_BLACK;
	} else
		style = (GogStyle *)base_style;

	swatch.y += i * data->step;
	gog_renderer_push_style (data->view->renderer, style);
	gog_renderer_draw_rectangle (data->view->renderer, &swatch);

	pos.x = swatch.x + data->step;
	pos.y = swatch.y;
	gog_renderer_draw_text (data->view->renderer, &pos, name, NULL);

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
	double pad = gog_renderer_pt2r_y (view->renderer,
					  legend->swatch_padding_pts);

	gog_renderer_push_style (view->renderer, legend->base.style);
	gog_renderer_draw_rectangle (view->renderer, &view->allocation);
	gog_renderer_pop_style (view->renderer);

	(lview_parent_klass->render) (view, bbox);

	closure.view = view;
	closure.swatch.x  = view->allocation.x + outline;
	closure.swatch.y  = view->allocation.y + outline + pad / 2.;
	closure.swatch.w  = gog_renderer_pt2r_x (view->renderer, legend->swatch_size_pts);
	closure.swatch.h  = gog_renderer_pt2r_y (view->renderer, legend->swatch_size_pts);
	closure.step      = closure.swatch.h + pad;
	closure.base_line = pad / 2.; /* bottom of the swatch */
	gog_chart_foreach_elem (GOG_CHART (view->model->parent),
		(GogEnumFunc) cb_render_elements, &closure);
}

static void
gog_legend_view_class_init (GogLegendViewClass *gview_klass)
{
	GogViewClass *view_klass    = (GogViewClass *) gview_klass;

	lview_parent_klass = g_type_class_peek_parent (gview_klass);
	view_klass->size_request    = gog_legend_view_size_request;
	view_klass->render	    = gog_legend_view_render;
}

static GSF_CLASS (GogLegendView, gog_legend_view,
		  gog_legend_view_class_init, NULL,
		  GOG_VIEW_TYPE)
